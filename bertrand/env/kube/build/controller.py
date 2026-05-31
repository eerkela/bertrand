"""Durable BuildKit build requests and their Kubernetes controller."""

from __future__ import annotations

import asyncio
import sys
from datetime import UTC, datetime, timedelta
from typing import TYPE_CHECKING

from bertrand.env.git import BERTRAND_NAMESPACE, NO_DEADLINE, Deadline
from bertrand.env.kube.api.client import (
    CLUSTER_REGISTRY_READY_LABEL,
    CLUSTER_REGISTRY_READY_VALUE,
    Kube,
    is_missing_api_resource,
)
from bertrand.env.kube.api.spec import ContainerSpec, PodTemplateSpec
from bertrand.env.kube.build.job import publish_project_platforms
from bertrand.env.kube.build.manifest import _publish_project_image_manifest
from bertrand.env.kube.build.repository import (
    IMAGE_REPOSITORY_NAME,
    clear_image_repository_storage_dirty,
    garbage_collect_image_repository_storage,
    image_repository_maintenance_status,
    mark_image_repository_storage_dirty,
    restore_image_repository_writable,
)
from bertrand.env.kube.build.request import (
    BUILDKIT_BUILD_GROUP,
    BUILDKIT_BUILD_KIND,
    BUILDKIT_BUILD_LABEL,
    BUILDKIT_BUILD_LABEL_VALUE,
    BUILDKIT_BUILD_LABELS,
    BUILDKIT_BUILD_PLURAL,
    BUILDKIT_BUILD_RESOURCE,
    BUILDKIT_IMAGE_GC_GRACE_SECONDS,
    BuildKitBuildRecord,
    gc_project_images,
    next_project_image_gc_time,
    patch_buildkit_build_status,
    retire_project_images,
)
from bertrand.env.kube.capability.device import (
    BERTRAND_DEVICE_GROUP,
    BERTRAND_DEVICE_PLURAL,
)
from bertrand.env.kube.ceph.api import parse_size_bytes
from bertrand.env.kube.ceph.capacity import (
    CEPH_CAPACITY_GROUP,
    STORAGE_STATE_PLURAL,
    read_storage_state,
    reserve_ceph_storage,
)
from bertrand.env.kube.ceph.snapshot import cleanup_orphaned_build_sources
from bertrand.env.kube.control import MaintenanceClock
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.dra import (
    DRA_GROUP,
    RESOURCE_CLAIM_PLURAL,
    RESOURCE_CLAIM_TEMPLATE_PLURAL,
)
from bertrand.env.kube.rbac import (
    CLUSTER_ROLE_BINDING_RESOURCE,
    CLUSTER_ROLE_RESOURCE,
    rbac_role_manifest,
    rbac_service_account_binding_manifest,
)
from bertrand.env.kube.service_account import ServiceAccount

if TYPE_CHECKING:
    from collections.abc import Collection, Mapping

    from bertrand.env.kube.api.spec import PolicyRuleManifest
    from bertrand.env.kube.job import Job

BUILDKIT_BUILD_CONTROLLER = "bertrand-build-controller"
BUILDKIT_BUILD_SERVICE_ACCOUNT = "bertrand-build-controller"
BUILDKIT_BUILD_RECONCILE_SECONDS = 2.0
BUILDKIT_BUILD_LOG_EXCERPT_CHARS = 4000
PROJECT_IMAGE_GC_EMPTY_CHECK_SECONDS = 3600.0
PROJECT_IMAGE_GC_READY_CHECK_SECONDS = 900.0
PROJECT_IMAGE_GC_FAILURE_RETRY_SECONDS = 300.0
PROJECT_IMAGE_GC_TIMEOUT_SECONDS = 60.0
REGISTRY_STORAGE_GC_MIN_INTERVAL_SECONDS = 21_600.0
REGISTRY_STORAGE_GC_CLEAN_CHECK_SECONDS = 3600.0
REGISTRY_STORAGE_GC_DIRTY_THRESHOLD = 16
REGISTRY_STORAGE_GC_IDLE_GRACE_SECONDS = 60.0
REGISTRY_STORAGE_GC_FAILURE_RETRY_SECONDS = 1800.0
REGISTRY_STORAGE_GC_ACTIVE_BUILD_RETRY_SECONDS = 120.0
REGISTRY_STORAGE_GC_NOT_READY_RETRY_SECONDS = 300.0
REGISTRY_STORAGE_GC_RESTORE_TIMEOUT_SECONDS = 120.0
REGISTRY_STORAGE_GC_TIMEOUT_SECONDS = 300.0
BUILD_SOURCE_GC_EMPTY_CHECK_SECONDS = 3600.0
BUILD_SOURCE_GC_READY_CHECK_SECONDS = 900.0
BUILD_SOURCE_GC_FAILURE_RETRY_SECONDS = 300.0
BUILD_SOURCE_GC_TIMEOUT_SECONDS = 60.0


def _warn(message: str) -> None:
    print(f"warning: {message}", file=sys.stderr, flush=True)


async def _reconcile_build(
    kube: Kube,
    *,
    request: BuildKitBuildRecord,
    deadline: Deadline,
    project_image_gc: MaintenanceClock,
) -> None:
    try:
        request = await patch_buildkit_build_status(
            kube,
            record=request,
            status=request.status.running(
                generation=request.metadata.generation,
                message="BuildKit build is running",
                reset=True,
            ),
            deadline=deadline,
        )
        policy = await read_storage_state(kube, deadline=deadline)
        reservation_bytes = parse_size_bytes(policy.spec.default_write_reservation)
        async with reserve_ceph_storage(
            kube,
            owner_kind=BUILDKIT_BUILD_KIND,
            owner_name=request.metadata.name,
            request_id=str(request.metadata.generation),
            requested_bytes=reservation_bytes,
            reason="BuildKit image publication",
            deadline=deadline,
        ):
            platform_refs = await _publish_platforms(
                kube,
                request=request,
                deadline=deadline,
            )
            publication = await _publish_manifest(
                kube,
                request=request,
                platform_refs=platform_refs,
                deadline=deadline,
            )
        internal_digest_ref, external_digest_ref, platform_refs = publication
        now = datetime.now(UTC)
        record = await patch_buildkit_build_status(
            kube,
            record=request,
            status=request.status.succeeded(
                generation=request.metadata.generation,
                completed_at=now,
                internal_digest_ref=internal_digest_ref,
                external_digest_ref=external_digest_ref,
                platform_images=platform_refs,
            ),
            deadline=deadline,
        )
        await retire_project_images(
            kube,
            repo_id=request.spec.repo_id,
            worktree_id=request.spec.worktree_id,
            exclude_names={record.name},
            deadline=deadline,
        )
        project_image_gc.schedule_no_later_than(
            datetime.now(UTC) + timedelta(seconds=BUILDKIT_IMAGE_GC_GRACE_SECONDS)
        )
    except (OSError, TimeoutError, ValueError) as err:
        message = str(err).splitlines()[0][:240] if str(err) else "Build failed"
        await patch_buildkit_build_status(
            kube,
            record=request,
            status=request.status.failed(
                generation=request.metadata.generation,
                completed_at=datetime.now(UTC),
                message=message,
                log_excerpt=_log_excerpt(str(err)),
            ),
            deadline=deadline,
        )


async def _publish_platforms(
    kube: Kube,
    *,
    request: BuildKitBuildRecord,
    deadline: Deadline,
) -> dict[str, str]:
    spec = request.spec

    async def observe_job(platform: str, job: Job) -> None:
        await patch_buildkit_build_status(
            kube,
            record=request,
            status=request.status.running(
                generation=request.metadata.generation,
                active_job=job.name,
                active_platform=platform,
                message=f"BuildKit build is running for {platform}",
            ),
            deadline=deadline,
        )

    return await publish_project_platforms(
        kube,
        spec,
        build_name=request.metadata.name,
        job_observer=observe_job,
        deadline=deadline,
    )


async def _publish_manifest(
    kube: Kube,
    *,
    request: BuildKitBuildRecord,
    platform_refs: Mapping[str, str],
    deadline: Deadline,
) -> tuple[str, str | None, dict[str, str]]:
    spec = request.spec

    async def observe_job(job: Job) -> None:
        await patch_buildkit_build_status(
            kube,
            record=request,
            status=request.status.running(
                generation=request.metadata.generation,
                active_job=job.name,
                active_platform="",
                message="image manifest assembly is running",
            ),
            deadline=deadline,
        )

    return await _publish_project_image_manifest(
        kube,
        spec=spec,
        platform_refs=platform_refs,
        job_observer=observe_job,
        deadline=deadline,
    )


async def _maybe_gc(
    kube: Kube,
    *,
    requests: Collection[BuildKitBuildRecord] | None,
    deadline: Deadline,
    project_image_gc: MaintenanceClock,
    registry_storage_gc: MaintenanceClock,
    build_source_gc: MaintenanceClock,
) -> None:
    await _maybe_project_image_gc(
        kube,
        deadline=deadline,
        project_image_gc=project_image_gc,
        registry_storage_gc=registry_storage_gc,
    )
    await _maybe_registry_storage_gc(
        kube,
        deadline=deadline,
        registry_storage_gc=registry_storage_gc,
    )
    await _maybe_build_source_gc(
        kube,
        requests=requests,
        deadline=deadline,
        build_source_gc=build_source_gc,
    )


async def _maybe_project_image_gc(
    kube: Kube,
    *,
    deadline: Deadline,
    project_image_gc: MaintenanceClock,
    registry_storage_gc: MaintenanceClock,
) -> None:
    now = datetime.now(UTC)
    pass_deadline = project_image_gc.pass_deadline(
        now,
        deadline=deadline,
        budget=PROJECT_IMAGE_GC_TIMEOUT_SECONDS,
    )
    if pass_deadline is None:
        return

    try:
        next_gc = await next_project_image_gc_time(
            kube,
            deadline=pass_deadline,
        )
        if next_gc is None:
            project_image_gc.schedule_after(PROJECT_IMAGE_GC_EMPTY_CHECK_SECONDS)
            return
        if next_gc > now:
            project_image_gc.schedule_at(next_gc)
            return

        prior_maintenance = await image_repository_maintenance_status(
            kube,
            deadline=pass_deadline,
        )
        marker_was_clean = not prior_maintenance.storage_dirty
        if marker_was_clean:
            await mark_image_repository_storage_dirty(
                kube,
                count=1,
                deadline=pass_deadline,
            )
        collected = await gc_project_images(
            kube,
            deadline=pass_deadline,
        )
        if collected:
            extra_count = len(collected) - 1 if marker_was_clean else len(collected)
            if extra_count > 0:
                await mark_image_repository_storage_dirty(
                    kube,
                    count=extra_count,
                    deadline=pass_deadline,
                )
            updated_maintenance = await image_repository_maintenance_status(
                kube,
                deadline=pass_deadline,
            )
            if updated_maintenance.dirty_count >= REGISTRY_STORAGE_GC_DIRTY_THRESHOLD:
                registry_storage_gc.schedule_now()
            elif updated_maintenance.storage_dirty:
                dirty_since = updated_maintenance.dirty_since or datetime.now(UTC)
                registry_storage_gc.schedule_at(
                    dirty_since
                    + timedelta(seconds=REGISTRY_STORAGE_GC_IDLE_GRACE_SECONDS)
                )
        elif marker_was_clean:
            await clear_image_repository_storage_dirty(
                kube,
                deadline=pass_deadline,
            )
        project_image_gc.schedule_after(PROJECT_IMAGE_GC_READY_CHECK_SECONDS)
    except (OSError, TimeoutError, ValueError) as err:
        project_image_gc.schedule_after(PROJECT_IMAGE_GC_FAILURE_RETRY_SECONDS)
        _warn(f"project image garbage collection failed: {err}")


async def _maybe_registry_storage_gc(
    kube: Kube,
    *,
    deadline: Deadline,
    registry_storage_gc: MaintenanceClock,
) -> None:
    now = datetime.now(UTC)
    pass_deadline = registry_storage_gc.pass_deadline(
        now,
        deadline=deadline,
        budget=REGISTRY_STORAGE_GC_TIMEOUT_SECONDS,
    )
    if pass_deadline is None:
        return

    try:

        async def buildkit_idle(attempt_deadline: Deadline) -> bool:
            try:
                records = await BUILDKIT_BUILD_RESOURCE.list(
                    kube,
                    deadline=attempt_deadline,
                )
            except OSError as err:
                if is_missing_api_resource(err):
                    return True
                raise
            return not any(record.is_active for record in records)

        maintenance = await image_repository_maintenance_status(
            kube,
            deadline=pass_deadline,
        )
        if not maintenance.storage_dirty:
            registry_storage_gc.schedule_after(REGISTRY_STORAGE_GC_CLEAN_CHECK_SECONDS)
            return

        dirty_since = maintenance.dirty_since or now
        idle_boundary = dirty_since + timedelta(
            seconds=REGISTRY_STORAGE_GC_IDLE_GRACE_SECONDS
        )
        if now < idle_boundary:
            registry_storage_gc.schedule_at(idle_boundary)
            return

        last_gc_at = maintenance.last_gc_at
        if (
            maintenance.dirty_count < REGISTRY_STORAGE_GC_DIRTY_THRESHOLD
            and last_gc_at is not None
            and now
            < last_gc_at + timedelta(seconds=REGISTRY_STORAGE_GC_MIN_INTERVAL_SECONDS)
        ):
            registry_storage_gc.schedule_at(
                last_gc_at + timedelta(seconds=REGISTRY_STORAGE_GC_MIN_INTERVAL_SECONDS)
            )
            return

        if not await buildkit_idle(pass_deadline):
            registry_storage_gc.schedule_after(
                REGISTRY_STORAGE_GC_ACTIVE_BUILD_RETRY_SECONDS
            )
            return

        deployment = await Deployment.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=IMAGE_REPOSITORY_NAME,
            deadline=pass_deadline,
        )
        if deployment is None or not deployment.rollout_ready(minimum=1):
            registry_storage_gc.schedule_after(
                REGISTRY_STORAGE_GC_NOT_READY_RETRY_SECONDS
            )
            return

        ran = await garbage_collect_image_repository_storage(
            kube,
            deadline=pass_deadline,
            preflight=buildkit_idle,
        )
        if not ran:
            registry_storage_gc.schedule_after(
                REGISTRY_STORAGE_GC_ACTIVE_BUILD_RETRY_SECONDS
            )
            return
        await clear_image_repository_storage_dirty(
            kube,
            last_gc_at=datetime.now(UTC),
            deadline=pass_deadline,
        )
        registry_storage_gc.schedule_after(REGISTRY_STORAGE_GC_MIN_INTERVAL_SECONDS)
    except (OSError, TimeoutError, ValueError) as err:
        registry_storage_gc.schedule_after(REGISTRY_STORAGE_GC_FAILURE_RETRY_SECONDS)
        _warn(f"image registry storage garbage collection failed: {err}")


async def _restore_registry_writable(kube: Kube, *, deadline: Deadline) -> None:
    remaining = deadline.remaining
    if remaining <= 0:
        return
    restore_deadline = Deadline(
        min(REGISTRY_STORAGE_GC_RESTORE_TIMEOUT_SECONDS, remaining)
    )
    try:
        await restore_image_repository_writable(kube, deadline=restore_deadline)
    except (OSError, TimeoutError, ValueError) as err:
        _warn(f"failed to restore image registry writable mode: {err}")


async def _maybe_build_source_gc(
    kube: Kube,
    *,
    requests: Collection[BuildKitBuildRecord] | None,
    deadline: Deadline,
    build_source_gc: MaintenanceClock,
) -> None:
    now = datetime.now(UTC)
    pass_deadline = build_source_gc.pass_deadline(
        now,
        deadline=deadline,
        budget=BUILD_SOURCE_GC_TIMEOUT_SECONDS,
    )
    if pass_deadline is None:
        return
    if requests is None:
        build_source_gc.schedule_after(BUILD_SOURCE_GC_FAILURE_RETRY_SECONDS)
        _warn(
            "BuildKit snapshot source garbage collection failed: "
            "BuildKitBuild inventory unavailable for build-source GC"
        )
        return
    try:
        active_names = {request.name for request in requests if request.is_active}
        deleted = await cleanup_orphaned_build_sources(
            kube,
            active_build_names=active_names,
            deadline=pass_deadline,
        )
        build_source_gc.schedule_after(
            BUILD_SOURCE_GC_READY_CHECK_SECONDS
            if deleted
            else BUILD_SOURCE_GC_EMPTY_CHECK_SECONDS
        )
    except (OSError, TimeoutError, ValueError) as err:
        build_source_gc.schedule_after(BUILD_SOURCE_GC_FAILURE_RETRY_SECONDS)
        _warn(f"BuildKit snapshot source garbage collection failed: {err}")


async def ensure_buildkit_build_controller(
    kube: Kube,
    *,
    image: str,
    deadline: Deadline,
) -> None:
    """Converge the in-cluster BuildKit build controller.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    image : str
        Bertrand control-plane image containing this package and runtime
        dependencies.
    deadline : Deadline
        Maximum convergence budget in seconds.

    Raises
    ------
    ValueError
        If the controller image reference is empty.
    """
    image = image.strip()
    if not image:
        msg = "BuildKit build controller image cannot be empty"
        raise ValueError(msg)
    await asyncio.gather(
        BUILDKIT_BUILD_RESOURCE.ensure_crd(kube, deadline=deadline),
        ServiceAccount.upsert(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=BUILDKIT_BUILD_SERVICE_ACCOUNT,
            labels=BUILDKIT_BUILD_LABELS,
            deadline=deadline,
        ),
        CLUSTER_ROLE_RESOURCE.upsert(
            kube,
            name=BUILDKIT_BUILD_CONTROLLER,
            manifest=rbac_role_manifest(
                kind="ClusterRole",
                namespace=None,
                name=BUILDKIT_BUILD_CONTROLLER,
                labels=BUILDKIT_BUILD_LABELS,
                rules=_controller_rules(),
            ),
            deadline=deadline,
        ),
    )
    await CLUSTER_ROLE_BINDING_RESOURCE.upsert(
        kube,
        name=BUILDKIT_BUILD_CONTROLLER,
        manifest=rbac_service_account_binding_manifest(
            kind="ClusterRoleBinding",
            namespace=None,
            name=BUILDKIT_BUILD_CONTROLLER,
            role_kind="ClusterRole",
            role_name=BUILDKIT_BUILD_CONTROLLER,
            service_account_name=BUILDKIT_BUILD_SERVICE_ACCOUNT,
            service_account_namespace=BERTRAND_NAMESPACE,
            labels=BUILDKIT_BUILD_LABELS,
        ),
        deadline=deadline,
    )
    deployment = await Deployment.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=BUILDKIT_BUILD_CONTROLLER,
        labels=BUILDKIT_BUILD_LABELS,
        selector={BUILDKIT_BUILD_LABEL: BUILDKIT_BUILD_LABEL_VALUE},
        replicas=1,
        pod_template=PodTemplateSpec(
            containers=[
                ContainerSpec(
                    name="controller",
                    image=image,
                    image_pull_policy="IfNotPresent",
                    command=["python", "-m", "bertrand.env.kube.build.controller"],
                )
            ],
            service_account_name=BUILDKIT_BUILD_SERVICE_ACCOUNT,
            automount_service_account_token=True,
            node_selector={
                CLUSTER_REGISTRY_READY_LABEL: CLUSTER_REGISTRY_READY_VALUE,
            },
        ),
        deadline=deadline,
    )
    await deployment.wait_rollout(kube, deadline=deadline)


async def run_buildkit_build_controller(*, deadline: Deadline = NO_DEADLINE) -> None:
    """Run the in-cluster durable BuildKit build controller.

    Parameters
    ----------
    deadline : Deadline
        Maximum runtime budget in seconds. If infinite, run indefinitely.

    """
    project_image_gc = MaintenanceClock()
    registry_storage_gc = MaintenanceClock()
    build_source_gc = MaintenanceClock()
    with await Kube.internal(namespace=BERTRAND_NAMESPACE) as kube:
        await _restore_registry_writable(kube, deadline=deadline)
        while deadline.remaining > 0:
            requests: list[BuildKitBuildRecord] | None = None
            try:
                requests = await BUILDKIT_BUILD_RESOURCE.list(
                    kube,
                    deadline=deadline,
                )
                for request in sorted(requests, key=lambda item: item.metadata.name):
                    if request.is_terminal:
                        continue
                    await _reconcile_build(
                        kube,
                        request=request,
                        deadline=deadline,
                        project_image_gc=project_image_gc,
                    )
            except (OSError, TimeoutError, ValueError) as err:
                _warn(f"BuildKit build reconciliation failed: {err}")
                if isinstance(err, OSError) and is_missing_api_resource(err):
                    requests = []
            await _maybe_gc(
                kube,
                requests=requests,
                deadline=deadline,
                project_image_gc=project_image_gc,
                registry_storage_gc=registry_storage_gc,
                build_source_gc=build_source_gc,
            )
            if deadline.remaining <= 0:
                break
            await deadline.sleep(BUILDKIT_BUILD_RECONCILE_SECONDS)


def main() -> int:
    """Run the BuildKit build controller module entrypoint.

    Returns
    -------
    int
        Process exit status.
    """
    try:
        asyncio.run(run_buildkit_build_controller())
    except KeyboardInterrupt:
        return 0
    return 0


def _controller_rules() -> tuple[PolicyRuleManifest, ...]:
    return (
        {
            "apiGroups": [BUILDKIT_BUILD_GROUP],
            "resources": [
                BUILDKIT_BUILD_PLURAL,
                f"{BUILDKIT_BUILD_PLURAL}/status",
            ],
            "verbs": ["get", "list", "watch", "create", "update", "patch", "delete"],
        },
        {
            "apiGroups": ["batch"],
            "resources": ["jobs"],
            "verbs": ["get", "list", "watch", "create", "delete"],
        },
        {
            "apiGroups": ["apps"],
            "resources": ["deployments"],
            "verbs": ["get", "list", "watch", "create", "update", "patch"],
        },
        {
            "apiGroups": [""],
            "resources": ["pods", "pods/log", "configmaps", "secrets", "nodes"],
            "verbs": ["get", "list", "watch", "create", "update", "patch", "delete"],
        },
        {
            "apiGroups": [""],
            "resources": ["persistentvolumeclaims"],
            "verbs": ["get", "list", "watch", "create", "delete"],
        },
        {
            "apiGroups": ["snapshot.storage.k8s.io"],
            "resources": ["volumesnapshots"],
            "verbs": ["get", "list", "watch", "create", "delete"],
        },
        {
            "apiGroups": ["snapshot.storage.k8s.io"],
            "resources": ["volumesnapshotclasses"],
            "verbs": ["get", "list", "watch", "create"],
        },
        {
            "apiGroups": [DRA_GROUP],
            "resources": [RESOURCE_CLAIM_PLURAL, RESOURCE_CLAIM_TEMPLATE_PLURAL],
            "verbs": ["get", "list", "watch", "create", "delete"],
        },
        {
            "apiGroups": [BERTRAND_DEVICE_GROUP],
            "resources": [BERTRAND_DEVICE_PLURAL],
            "verbs": ["get", "list", "watch"],
        },
        {
            "apiGroups": [CEPH_CAPACITY_GROUP],
            "resources": [STORAGE_STATE_PLURAL],
            "verbs": ["get", "list", "watch", "create", "update", "patch"],
        },
        {
            "apiGroups": [CEPH_CAPACITY_GROUP],
            "resources": [f"{STORAGE_STATE_PLURAL}/status"],
            "verbs": ["get", "update", "patch"],
        },
    )


def _log_excerpt(text: str) -> str:
    lines = text.strip().splitlines()
    excerpt = "\n".join(lines[-80:])
    if len(excerpt) > BUILDKIT_BUILD_LOG_EXCERPT_CHARS:
        return excerpt[-BUILDKIT_BUILD_LOG_EXCERPT_CHARS:]
    return excerpt


if __name__ == "__main__":
    raise SystemExit(main())
