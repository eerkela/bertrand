"""Durable BuildKit build requests and their Kubernetes controller."""

from __future__ import annotations

import asyncio
import sys
from datetime import UTC, datetime, timedelta
from typing import TYPE_CHECKING

from bertrand.env.git import (
    BERTRAND_NAMESPACE,
    INFINITY,
    Deadline,
)
from bertrand.env.kube.api.client import (
    CLUSTER_REGISTRY_READY_LABEL,
    CLUSTER_REGISTRY_READY_VALUE,
    Kube,
)
from bertrand.env.kube.api.spec import (
    ContainerSpec,
    PodTemplateSpec,
)
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
    active_buildkit_build_names,
    gc_project_images,
    has_active_buildkit_builds,
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
    upsert_rbac_binding,
    upsert_rbac_role,
)
from bertrand.env.kube.service_account import ServiceAccount

if TYPE_CHECKING:
    from collections.abc import Mapping

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
        await _patch_running(
            kube,
            request=request,
            message="BuildKit build is running",
            reset=True,
            deadline=deadline,
        )
        policy = await read_storage_state(kube, timeout=deadline.remaining())
        reservation_bytes = parse_size_bytes(policy.spec.default_write_reservation)
        async with reserve_ceph_storage(
            kube,
            owner_kind=BUILDKIT_BUILD_KIND,
            owner_name=request.metadata.name,
            request_id=str(request.metadata.generation),
            requested_bytes=reservation_bytes,
            reason="BuildKit image publication",
            timeout=deadline.remaining(),
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
        await _patch_succeeded(
            kube,
            request=request,
            publication=publication,
            deadline=deadline,
        )
        project_image_gc.schedule_no_later_than(
            datetime.now(UTC) + timedelta(seconds=BUILDKIT_IMAGE_GC_GRACE_SECONDS)
        )
    except (OSError, TimeoutError, ValueError) as err:
        await _patch_failed(
            kube,
            request=request,
            error=err,
            deadline=deadline,
        )


async def _patch_running(
    kube: Kube,
    *,
    request: BuildKitBuildRecord,
    message: str,
    deadline: Deadline,
    active_job: str = "",
    active_platform: str = "",
    reset: bool = False,
) -> None:
    status: dict[str, object] = {
        "active_job": active_job,
        "active_platform": active_platform,
        "message": message,
    }
    if reset:
        status.update(
            {
                "started_at": datetime.now(UTC).isoformat(),
                "completed_at": None,
                "internal_digest_ref": "",
                "external_digest_ref": "",
                "platform_images": {},
                "image_phase": "Pending",
                "published_at": None,
                "retired_at": None,
                "last_gc_at": None,
                "image_last_error": "",
                "log_excerpt": "",
            }
        )
    await patch_buildkit_build_status(
        kube,
        name=request.metadata.name,
        phase="Running",
        generation=request.metadata.generation,
        timeout=deadline.remaining(),
        **status,
    )


async def _patch_succeeded(
    kube: Kube,
    *,
    request: BuildKitBuildRecord,
    publication: tuple[str, str | None, dict[str, str]],
    deadline: Deadline,
) -> None:
    internal_digest_ref, external_digest_ref, platform_refs = publication
    now = datetime.now(UTC)
    record = await patch_buildkit_build_status(
        kube,
        name=request.metadata.name,
        phase="Succeeded",
        generation=request.metadata.generation,
        completed_at=now.isoformat(),
        active_job="",
        active_platform="",
        internal_digest_ref=internal_digest_ref,
        external_digest_ref=external_digest_ref or "",
        platform_images=platform_refs,
        image_phase="Active",
        published_at=now.isoformat(),
        retired_at=None,
        last_gc_at=None,
        image_last_error="",
        message="BuildKit build succeeded",
        log_excerpt="",
        timeout=deadline.remaining(),
    )
    await retire_project_images(
        kube,
        repo_id=request.spec.repo_id,
        worktree_id=request.spec.worktree_id,
        exclude_names={record.name},
        timeout=deadline.remaining(),
    )


async def _patch_failed(
    kube: Kube,
    *,
    request: BuildKitBuildRecord,
    error: BaseException,
    deadline: Deadline,
) -> None:
    message = str(error).splitlines()[0][:240] if str(error) else "Build failed"
    await patch_buildkit_build_status(
        kube,
        name=request.metadata.name,
        phase="Failed",
        generation=request.metadata.generation,
        completed_at=datetime.now(UTC).isoformat(),
        active_job="",
        active_platform="",
        message=message,
        log_excerpt=_log_excerpt(str(error)),
        timeout=deadline.remaining(),
    )


async def _publish_platforms(
    kube: Kube,
    *,
    request: BuildKitBuildRecord,
    deadline: Deadline,
) -> dict[str, str]:
    spec = request.spec

    async def observe_job(platform: str, job: Job) -> None:
        await _patch_running(
            kube,
            request=request,
            active_job=job.name,
            active_platform=platform,
            message=f"BuildKit build is running for {platform}",
            deadline=deadline,
        )

    return await publish_project_platforms(
        kube,
        spec,
        build_name=request.metadata.name,
        job_observer=observe_job,
        timeout=deadline.remaining(),
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
        await _patch_running(
            kube,
            request=request,
            active_job=job.name,
            active_platform="",
            message="image manifest assembly is running",
            deadline=deadline,
        )

    return await _publish_project_image_manifest(
        kube,
        spec=spec,
        platform_refs=platform_refs,
        job_observer=observe_job,
        timeout=deadline.remaining(),
    )


async def _maybe_gc(
    kube: Kube,
    *,
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
        timeout=PROJECT_IMAGE_GC_TIMEOUT_SECONDS,
    )
    if pass_deadline is None:
        return

    try:
        next_gc = await next_project_image_gc_time(
            kube,
            timeout=pass_deadline.remaining(),
        )
        if next_gc is None:
            project_image_gc.schedule_after(PROJECT_IMAGE_GC_EMPTY_CHECK_SECONDS)
            return
        if next_gc > now:
            project_image_gc.schedule_at(next_gc)
            return

        prior_maintenance = await image_repository_maintenance_status(
            kube,
            timeout=pass_deadline.remaining(),
        )
        marker_was_clean = not prior_maintenance.storage_dirty
        if marker_was_clean:
            await mark_image_repository_storage_dirty(
                kube,
                count=1,
                timeout=pass_deadline.remaining(),
            )
        collected = await gc_project_images(
            kube,
            timeout=pass_deadline.remaining(),
        )
        if collected:
            extra_count = len(collected) - 1 if marker_was_clean else len(collected)
            if extra_count > 0:
                await mark_image_repository_storage_dirty(
                    kube,
                    count=extra_count,
                    timeout=pass_deadline.remaining(),
                )
            updated_maintenance = await image_repository_maintenance_status(
                kube,
                timeout=pass_deadline.remaining(),
            )
            if updated_maintenance.dirty_count >= REGISTRY_STORAGE_GC_DIRTY_THRESHOLD:
                registry_storage_gc.schedule_now()
        elif marker_was_clean:
            await clear_image_repository_storage_dirty(
                kube,
                timeout=pass_deadline.remaining(),
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
        timeout=REGISTRY_STORAGE_GC_TIMEOUT_SECONDS,
    )
    if pass_deadline is None:
        return

    try:
        maintenance = await image_repository_maintenance_status(
            kube,
            timeout=pass_deadline.remaining(),
        )
        if not maintenance.storage_dirty:
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

        if await has_active_buildkit_builds(kube, timeout=pass_deadline.remaining()):
            registry_storage_gc.schedule_after(
                REGISTRY_STORAGE_GC_ACTIVE_BUILD_RETRY_SECONDS
            )
            return

        deployment = await Deployment.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=IMAGE_REPOSITORY_NAME,
            timeout=pass_deadline.remaining(),
        )
        if deployment is None or not deployment.rollout_ready(minimum=1):
            registry_storage_gc.schedule_after(
                REGISTRY_STORAGE_GC_NOT_READY_RETRY_SECONDS
            )
            return

        async def registry_storage_gc_still_idle(remaining: float) -> bool:
            return not await has_active_buildkit_builds(kube, timeout=remaining)

        ran = await garbage_collect_image_repository_storage(
            kube,
            timeout=pass_deadline.remaining(),
            preflight=registry_storage_gc_still_idle,
        )
        if not ran:
            registry_storage_gc.schedule_after(
                REGISTRY_STORAGE_GC_ACTIVE_BUILD_RETRY_SECONDS
            )
            return
        await clear_image_repository_storage_dirty(
            kube,
            last_gc_at=datetime.now(UTC),
            timeout=pass_deadline.remaining(),
        )
        registry_storage_gc.schedule_after(REGISTRY_STORAGE_GC_MIN_INTERVAL_SECONDS)
    except (OSError, TimeoutError, ValueError) as err:
        registry_storage_gc.schedule_after(REGISTRY_STORAGE_GC_FAILURE_RETRY_SECONDS)
        _warn(f"image registry storage garbage collection failed: {err}")


async def _restore_registry_writable(kube: Kube, *, deadline: Deadline) -> None:
    timeout = deadline.bounded(REGISTRY_STORAGE_GC_RESTORE_TIMEOUT_SECONDS)
    if timeout <= 0:
        return
    try:
        await restore_image_repository_writable(kube, timeout=timeout)
    except (OSError, TimeoutError, ValueError) as err:
        _warn(f"failed to restore image registry writable mode: {err}")


async def _maybe_build_source_gc(
    kube: Kube,
    *,
    deadline: Deadline,
    build_source_gc: MaintenanceClock,
) -> None:
    now = datetime.now(UTC)
    pass_deadline = build_source_gc.pass_deadline(
        now,
        deadline=deadline,
        timeout=BUILD_SOURCE_GC_TIMEOUT_SECONDS,
    )
    if pass_deadline is None:
        return
    try:
        active_names = await active_buildkit_build_names(
            kube,
            timeout=pass_deadline.remaining(),
        )
        deleted = await cleanup_orphaned_build_sources(
            kube,
            active_build_names=active_names,
            timeout=pass_deadline.remaining(),
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
    timeout: float,
) -> None:
    """Converge the in-cluster BuildKit build controller.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    image : str
        Bertrand control-plane image containing this package and runtime
        dependencies.
    timeout : float
        Maximum convergence budget in seconds.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive or rollout exceeds the budget.
    ValueError
        If the controller image reference is empty.
    """
    image = image.strip()
    if not image:
        msg = "BuildKit build controller image cannot be empty"
        raise ValueError(msg)
    if timeout <= 0:
        msg = "BuildKit build controller timeout must be non-negative"
        raise TimeoutError(msg)
    deadline = Deadline.from_timeout(
        timeout, message="timeout must be non-negative"
    )
    await BUILDKIT_BUILD_RESOURCE.ensure_crd(kube, timeout=deadline.remaining())
    await ServiceAccount.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=BUILDKIT_BUILD_SERVICE_ACCOUNT,
        labels=BUILDKIT_BUILD_LABELS,
        timeout=deadline.remaining(),
    )
    await upsert_rbac_role(
        kube,
        kind="ClusterRole",
        name=BUILDKIT_BUILD_CONTROLLER,
        labels=BUILDKIT_BUILD_LABELS,
        rules=_controller_rules(),
        timeout=deadline.remaining(),
    )
    await upsert_rbac_binding(
        kube,
        kind="ClusterRoleBinding",
        name=BUILDKIT_BUILD_CONTROLLER,
        role_kind="ClusterRole",
        role_name=BUILDKIT_BUILD_CONTROLLER,
        service_account_name=BUILDKIT_BUILD_SERVICE_ACCOUNT,
        service_account_namespace=BERTRAND_NAMESPACE,
        labels=BUILDKIT_BUILD_LABELS,
        timeout=deadline.remaining(),
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
        timeout=deadline.remaining(),
    )
    await deployment.wait_rollout(kube, timeout=deadline.remaining())


async def run_buildkit_build_controller(*, timeout: float = INFINITY) -> None:
    """Run the in-cluster durable BuildKit build controller.

    Parameters
    ----------
    timeout : float, optional
        Maximum runtime budget in seconds. If infinite, run indefinitely.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    """
    if timeout <= 0:
        msg = "BuildKit build controller timeout must be non-negative"
        raise TimeoutError(msg)
    deadline = Deadline.from_timeout(timeout, message="timeout must be non-negative")
    project_image_gc = MaintenanceClock()
    registry_storage_gc = MaintenanceClock()
    build_source_gc = MaintenanceClock()
    with Kube.inside_cluster(namespace=BERTRAND_NAMESPACE) as kube:
        await _restore_registry_writable(kube, deadline=deadline)
        while deadline.remaining() > 0:
            try:
                requests = await BUILDKIT_BUILD_RESOURCE.list(
                    kube,
                    timeout=deadline.remaining(),
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
            await _maybe_gc(
                kube,
                deadline=deadline,
                project_image_gc=project_image_gc,
                registry_storage_gc=registry_storage_gc,
                build_source_gc=build_source_gc,
            )
            if deadline.remaining() <= 0:
                break
            await asyncio.sleep(deadline.bounded(BUILDKIT_BUILD_RECONCILE_SECONDS))


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
