"""Durable BuildKit build requests and their Kubernetes controller."""

from __future__ import annotations

import asyncio
import sys
from datetime import UTC, datetime, timedelta
from typing import TYPE_CHECKING

from bertrand.env.git import (
    BERTRAND_NAMESPACE,
    INFINITY,
)
from bertrand.env.kube.api.client import (
    CLUSTER_REGISTRY_READY_LABEL,
    CLUSTER_REGISTRY_READY_VALUE,
    Kube,
)
from bertrand.env.kube.api.spec import (
    ContainerSpec,
    PodTemplateSpec,
    PolicyRuleSpec,
)
from bertrand.env.kube.build.job import _ProjectBuildExecutor
from bertrand.env.kube.build.lifecycle import (
    PROJECT_IMAGE_GC_GRACE_SECONDS,
    ProjectImagePublication,
    ensure_project_image_crd,
    gc_project_images,
    next_project_image_gc_time,
)
from bertrand.env.kube.build.manifest import _publish_project_image_manifest
from bertrand.env.kube.build.repository import IMAGES
from bertrand.env.kube.build.request import (
    BUILDKIT_BUILD_GROUP,
    BUILDKIT_BUILD_LABEL,
    BUILDKIT_BUILD_LABEL_VALUE,
    BUILDKIT_BUILD_LABELS,
    BUILDKIT_BUILD_PLURAL,
    BuildKitBuildRecord,
    active_buildkit_build_names,
    ensure_buildkit_build_crd,
    has_active_buildkit_builds,
    list_buildkit_builds,
    patch_buildkit_build_status,
)
from bertrand.env.kube.ceph.snapshot import cleanup_orphaned_build_sources
from bertrand.env.kube.control import MaintenanceClock
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.rbac import ClusterRole, ClusterRoleBinding
from bertrand.env.kube.service_account import ServiceAccount

if TYPE_CHECKING:
    from collections.abc import Mapping

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


class _BuildKitBuildController:
    """In-cluster reconciler for durable BuildKit build requests."""

    def __init__(self) -> None:
        self._project_image_gc = MaintenanceClock()
        self._registry_storage_gc = MaintenanceClock()
        self._build_source_gc = MaintenanceClock()

    async def run(self, *, timeout: float = INFINITY) -> None:
        """Run the controller reconciliation loop.

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
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        with Kube.inside_cluster(namespace=BERTRAND_NAMESPACE) as kube:
            await self._restore_registry_writable(kube, deadline=deadline)
            while loop.time() < deadline:
                try:
                    await self.reconcile_all(kube, deadline=deadline)
                except (OSError, TimeoutError, ValueError) as err:
                    print(
                        f"warning: BuildKit build reconciliation failed: {err}",
                        file=sys.stderr,
                        flush=True,
                    )
                await self._maybe_gc(kube, deadline=deadline)
                remaining = deadline - loop.time()
                if remaining <= 0:
                    break
                await asyncio.sleep(min(BUILDKIT_BUILD_RECONCILE_SECONDS, remaining))

    async def reconcile_all(self, kube: Kube, *, deadline: float) -> None:
        """Reconcile every non-terminal `BuildKitBuild` in the Bertrand namespace.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : float
            Absolute event-loop deadline for this pass.
        """
        loop = asyncio.get_running_loop()
        requests = await list_buildkit_builds(
            kube,
            timeout=deadline - loop.time(),
        )
        for request in sorted(requests, key=lambda item: item.metadata.name):
            if request.is_terminal:
                continue
            await self.reconcile(kube, request=request, deadline=deadline)

    async def reconcile(
        self,
        kube: Kube,
        *,
        request: BuildKitBuildRecord,
        deadline: float,
    ) -> None:
        """Reconcile one build request generation.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        request : BuildKitBuildRecord
            Build request to execute.
        deadline : float
            Absolute event-loop deadline for this reconciliation.
        """
        loop = asyncio.get_running_loop()
        try:
            await patch_buildkit_build_status(
                kube,
                name=request.metadata.name,
                phase="Running",
                generation=request.metadata.generation,
                started_at=datetime.now(UTC).isoformat(),
                completed_at=None,
                active_job="",
                active_platform="",
                external_digest_ref="",
                record_name="",
                message="BuildKit build is running",
                log_excerpt="",
                timeout=deadline - loop.time(),
            )
            platform_refs = await self._publish_platforms(
                kube,
                request=request,
                deadline=deadline,
            )
            publication = await self._publish_manifest(
                kube,
                request=request,
                platform_refs=platform_refs,
                deadline=deadline,
            )
            await patch_buildkit_build_status(
                kube,
                name=request.metadata.name,
                phase="Succeeded",
                generation=request.metadata.generation,
                completed_at=datetime.now(UTC).isoformat(),
                active_job="",
                active_platform="",
                external_digest_ref=publication.external_digest_ref or "",
                record_name=publication.record.name,
                message="BuildKit build succeeded",
                log_excerpt="",
                timeout=deadline - loop.time(),
            )
            self._project_image_gc.schedule_no_later_than(
                datetime.now(UTC) + timedelta(seconds=PROJECT_IMAGE_GC_GRACE_SECONDS)
            )
        except (OSError, TimeoutError, ValueError) as err:
            await patch_buildkit_build_status(
                kube,
                name=request.metadata.name,
                phase="Failed",
                generation=request.metadata.generation,
                completed_at=datetime.now(UTC).isoformat(),
                active_job="",
                active_platform="",
                message=str(err).splitlines()[0][:240] if str(err) else "Build failed",
                log_excerpt=_log_excerpt(str(err)),
                timeout=deadline - loop.time(),
            )

    async def _publish_platforms(
        self,
        kube: Kube,
        *,
        request: BuildKitBuildRecord,
        deadline: float,
    ) -> dict[str, str]:
        spec = request.spec
        build = _ProjectBuildExecutor(spec=spec)
        loop = asyncio.get_running_loop()

        async def observe_job(platform: str, job: Job) -> None:
            await patch_buildkit_build_status(
                kube,
                name=request.metadata.name,
                phase="Running",
                generation=request.metadata.generation,
                active_job=job.name,
                active_platform=platform,
                message=f"BuildKit build is running for {platform}",
                timeout=deadline - loop.time(),
            )

        return await build.publish_platforms(
            kube,
            build_name=request.metadata.name,
            job_observer=observe_job,
            timeout=deadline - loop.time(),
        )

    async def _publish_manifest(
        self,
        kube: Kube,
        *,
        request: BuildKitBuildRecord,
        platform_refs: Mapping[str, str],
        deadline: float,
    ) -> ProjectImagePublication:
        spec = request.spec
        identity = spec.identity
        loop = asyncio.get_running_loop()

        async def observe_job(job: Job) -> None:
            await patch_buildkit_build_status(
                kube,
                name=request.metadata.name,
                phase="Running",
                generation=request.metadata.generation,
                active_job=job.name,
                active_platform="",
                message="image manifest assembly is running",
                timeout=deadline - loop.time(),
            )

        return await _publish_project_image_manifest(
            kube,
            identity=identity,
            platform_refs=platform_refs,
            external_image=spec.external_image,
            auth_id=spec.auth_id,
            env_id=identity.env_id,
            job_observer=observe_job,
            timeout=deadline - loop.time(),
        )

    async def _maybe_gc(self, kube: Kube, *, deadline: float) -> None:
        await self._maybe_project_image_gc(kube, deadline=deadline)
        await self._maybe_registry_storage_gc(kube, deadline=deadline)
        await self._maybe_build_source_gc(kube, deadline=deadline)

    async def _maybe_project_image_gc(self, kube: Kube, *, deadline: float) -> None:
        now = datetime.now(UTC)
        pass_deadline = self._project_image_gc.pass_deadline(
            now,
            loop_deadline=deadline,
            timeout=PROJECT_IMAGE_GC_TIMEOUT_SECONDS,
        )
        if pass_deadline is None:
            return
        loop = asyncio.get_running_loop()

        try:
            next_gc = await next_project_image_gc_time(
                kube,
                timeout=pass_deadline - loop.time(),
            )
            if next_gc is None:
                self._project_image_gc.schedule_after(
                    PROJECT_IMAGE_GC_EMPTY_CHECK_SECONDS
                )
                return
            if next_gc > now:
                self._project_image_gc.schedule_at(next_gc)
                return

            prior_maintenance = await IMAGES.maintenance_status(
                kube,
                timeout=pass_deadline - loop.time(),
            )
            marker_was_clean = not prior_maintenance.storage_dirty
            if marker_was_clean:
                await IMAGES.mark_storage_dirty(
                    kube,
                    count=1,
                    timeout=pass_deadline - loop.time(),
                )
            collected = await gc_project_images(
                kube,
                timeout=pass_deadline - loop.time(),
            )
            if collected:
                extra_count = len(collected) - 1 if marker_was_clean else len(collected)
                if extra_count > 0:
                    await IMAGES.mark_storage_dirty(
                        kube,
                        count=extra_count,
                        timeout=pass_deadline - loop.time(),
                    )
                updated_maintenance = await IMAGES.maintenance_status(
                    kube,
                    timeout=pass_deadline - loop.time(),
                )
                if (
                    updated_maintenance.dirty_count
                    >= REGISTRY_STORAGE_GC_DIRTY_THRESHOLD
                ):
                    self._registry_storage_gc.schedule_now()
            elif marker_was_clean:
                await IMAGES.clear_storage_dirty(
                    kube,
                    timeout=pass_deadline - loop.time(),
                )
            self._project_image_gc.schedule_after(PROJECT_IMAGE_GC_READY_CHECK_SECONDS)
        except (OSError, TimeoutError, ValueError) as err:
            self._project_image_gc.schedule_after(
                PROJECT_IMAGE_GC_FAILURE_RETRY_SECONDS
            )
            print(
                f"warning: project image garbage collection failed: {err}",
                file=sys.stderr,
                flush=True,
            )

    async def _maybe_registry_storage_gc(
        self,
        kube: Kube,
        *,
        deadline: float,
    ) -> None:
        loop = asyncio.get_running_loop()
        pass_deadline = min(
            deadline,
            loop.time() + REGISTRY_STORAGE_GC_TIMEOUT_SECONDS,
        )
        if pass_deadline <= loop.time():
            return

        try:
            maintenance = await IMAGES.maintenance_status(
                kube,
                timeout=pass_deadline - loop.time(),
            )
            if not maintenance.storage_dirty:
                return

            now = datetime.now(UTC)
            if not self._registry_storage_gc.due(now):
                return
            dirty_since = maintenance.dirty_since or now
            idle_boundary = dirty_since + timedelta(
                seconds=REGISTRY_STORAGE_GC_IDLE_GRACE_SECONDS
            )
            if now < idle_boundary:
                self._registry_storage_gc.schedule_at(idle_boundary)
                return

            last_gc_at = maintenance.last_gc_at
            if (
                maintenance.dirty_count < REGISTRY_STORAGE_GC_DIRTY_THRESHOLD
                and last_gc_at is not None
                and now
                < last_gc_at
                + timedelta(seconds=REGISTRY_STORAGE_GC_MIN_INTERVAL_SECONDS)
            ):
                self._registry_storage_gc.schedule_at(
                    last_gc_at
                    + timedelta(seconds=REGISTRY_STORAGE_GC_MIN_INTERVAL_SECONDS)
                )
                return

            if await has_active_buildkit_builds(
                kube,
                timeout=pass_deadline - loop.time(),
            ):
                self._registry_storage_gc.schedule_after(
                    REGISTRY_STORAGE_GC_ACTIVE_BUILD_RETRY_SECONDS
                )
                return
            if not await self._registry_deployment_ready(
                kube,
                timeout=pass_deadline - loop.time(),
            ):
                self._registry_storage_gc.schedule_after(
                    REGISTRY_STORAGE_GC_NOT_READY_RETRY_SECONDS
                )
                return

            ran = await IMAGES.garbage_collect_storage(
                kube,
                timeout=pass_deadline - loop.time(),
                preflight=lambda remaining: self._registry_storage_gc_still_idle(
                    kube,
                    timeout=remaining,
                ),
            )
            if not ran:
                self._registry_storage_gc.schedule_after(
                    REGISTRY_STORAGE_GC_ACTIVE_BUILD_RETRY_SECONDS
                )
                return
            await IMAGES.clear_storage_dirty(
                kube,
                last_gc_at=datetime.now(UTC),
                timeout=pass_deadline - loop.time(),
            )
            self._registry_storage_gc.schedule_after(
                REGISTRY_STORAGE_GC_MIN_INTERVAL_SECONDS
            )
        except (OSError, TimeoutError, ValueError) as err:
            self._registry_storage_gc.schedule_after(
                REGISTRY_STORAGE_GC_FAILURE_RETRY_SECONDS
            )
            print(
                f"warning: image registry storage garbage collection failed: {err}",
                file=sys.stderr,
                flush=True,
            )

    async def _restore_registry_writable(self, kube: Kube, *, deadline: float) -> None:
        loop = asyncio.get_running_loop()
        remaining = deadline - loop.time()
        if remaining <= 0:
            return
        timeout = min(REGISTRY_STORAGE_GC_RESTORE_TIMEOUT_SECONDS, remaining)
        try:
            await IMAGES.restore_writable(kube, timeout=timeout)
        except (OSError, TimeoutError, ValueError) as err:
            print(
                f"warning: failed to restore image registry writable mode: {err}",
                file=sys.stderr,
                flush=True,
            )

    async def _maybe_build_source_gc(
        self,
        kube: Kube,
        *,
        deadline: float,
    ) -> None:
        now = datetime.now(UTC)
        pass_deadline = self._build_source_gc.pass_deadline(
            now,
            loop_deadline=deadline,
            timeout=BUILD_SOURCE_GC_TIMEOUT_SECONDS,
        )
        if pass_deadline is None:
            return
        loop = asyncio.get_running_loop()
        try:
            active_names = await active_buildkit_build_names(
                kube,
                timeout=pass_deadline - loop.time(),
            )
            deleted = await cleanup_orphaned_build_sources(
                kube,
                active_build_names=active_names,
                timeout=pass_deadline - loop.time(),
            )
            self._build_source_gc.schedule_after(
                BUILD_SOURCE_GC_READY_CHECK_SECONDS
                if deleted
                else BUILD_SOURCE_GC_EMPTY_CHECK_SECONDS
            )
        except (OSError, TimeoutError, ValueError) as err:
            self._build_source_gc.schedule_after(BUILD_SOURCE_GC_FAILURE_RETRY_SECONDS)
            print(
                f"warning: BuildKit snapshot source garbage collection failed: {err}",
                file=sys.stderr,
                flush=True,
            )

    async def _registry_storage_gc_still_idle(
        self,
        kube: Kube,
        *,
        timeout: float,
    ) -> bool:
        return not await has_active_buildkit_builds(kube, timeout=timeout)

    async def _registry_deployment_ready(
        self,
        kube: Kube,
        *,
        timeout: float,
    ) -> bool:
        deployment = await Deployment.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=IMAGES.service,
            timeout=timeout,
        )
        return deployment is not None and deployment.rollout_ready(minimum=1)


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
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    await ensure_buildkit_build_crd(kube, timeout=deadline - loop.time())
    await ensure_project_image_crd(kube, timeout=deadline - loop.time())
    await ServiceAccount.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=BUILDKIT_BUILD_SERVICE_ACCOUNT,
        labels=BUILDKIT_BUILD_LABELS,
        timeout=deadline - loop.time(),
    )
    await ClusterRole.upsert(
        kube,
        name=BUILDKIT_BUILD_CONTROLLER,
        labels=BUILDKIT_BUILD_LABELS,
        rules=_controller_rules(),
        timeout=deadline - loop.time(),
    )
    await ClusterRoleBinding.upsert(
        kube,
        name=BUILDKIT_BUILD_CONTROLLER,
        role_name=BUILDKIT_BUILD_CONTROLLER,
        service_account_name=BUILDKIT_BUILD_SERVICE_ACCOUNT,
        service_account_namespace=BERTRAND_NAMESPACE,
        labels=BUILDKIT_BUILD_LABELS,
        timeout=deadline - loop.time(),
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
        timeout=deadline - loop.time(),
    )
    await deployment.wait_rollout(kube, timeout=deadline - loop.time())


async def run_buildkit_build_controller(*, timeout: float = INFINITY) -> None:
    """Run the in-cluster durable BuildKit build controller.

    Parameters
    ----------
    timeout : float, optional
        Maximum runtime budget in seconds. If infinite, run indefinitely.
    """
    await _BuildKitBuildController().run(timeout=timeout)


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


def _controller_rules() -> tuple[PolicyRuleSpec, ...]:
    return (
        PolicyRuleSpec(
            api_groups=[BUILDKIT_BUILD_GROUP],
            resources=[
                BUILDKIT_BUILD_PLURAL,
                f"{BUILDKIT_BUILD_PLURAL}/status",
                "bertrandimages",
            ],
            verbs=["get", "list", "watch", "create", "update", "patch", "delete"],
        ),
        PolicyRuleSpec(
            api_groups=["batch"],
            resources=["jobs"],
            verbs=["get", "list", "watch", "create", "delete"],
        ),
        PolicyRuleSpec(
            api_groups=["apps"],
            resources=["deployments"],
            verbs=["get", "list", "watch", "create", "update", "patch"],
        ),
        PolicyRuleSpec(
            api_groups=[""],
            resources=["pods", "pods/log", "configmaps", "secrets", "nodes"],
            verbs=["get", "list", "watch", "create", "update", "patch", "delete"],
        ),
        PolicyRuleSpec(
            api_groups=[""],
            resources=["persistentvolumeclaims"],
            verbs=["get", "list", "watch", "create", "delete"],
        ),
        PolicyRuleSpec(
            api_groups=["snapshot.storage.k8s.io"],
            resources=["volumesnapshots"],
            verbs=["get", "list", "watch", "create", "delete"],
        ),
        PolicyRuleSpec(
            api_groups=["snapshot.storage.k8s.io"],
            resources=["volumesnapshotclasses"],
            verbs=["get", "list", "watch", "create"],
        ),
    )


def _log_excerpt(text: str) -> str:
    lines = text.strip().splitlines()
    excerpt = "\n".join(lines[-80:])
    if len(excerpt) > BUILDKIT_BUILD_LOG_EXCERPT_CHARS:
        return excerpt[-BUILDKIT_BUILD_LOG_EXCERPT_CHARS:]
    return excerpt


if __name__ == "__main__":
    raise SystemExit(main())
