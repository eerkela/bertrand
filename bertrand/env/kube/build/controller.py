"""Durable BuildKit build requests and their Kubernetes controller."""

from __future__ import annotations

import asyncio
import hashlib
import json
import sys
import uuid
from datetime import UTC, datetime, timedelta
from typing import TYPE_CHECKING

from bertrand.env.config.core import _check_uuid
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
    CustomResourceSpec,
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
    BUILDKIT_BUILD_KIND,
    BUILDKIT_BUILD_LABEL,
    BUILDKIT_BUILD_LABEL_VALUE,
    BUILDKIT_BUILD_LABELS,
    BuildKitBuildPhase,
    BuildKitBuildRecord,
    BuildKitBuildSpec,
    BuildKitBuildStatus,
)
from bertrand.env.kube.crd import (
    CustomResourceClient,
    CustomResourceDefinition,
)
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.rbac import ClusterRole, ClusterRoleBinding
from bertrand.env.kube.service_account import ServiceAccount

if TYPE_CHECKING:
    from collections.abc import Awaitable, Callable, Mapping

    from bertrand.env.kube.job import Job


BUILDKIT_BUILD_GROUP = "build.bertrand.dev"
BUILDKIT_BUILD_VERSION = "v1alpha1"
BUILDKIT_BUILD_PLURAL = "buildkitbuilds"
BUILDKIT_BUILD_CONTROLLER = "bertrand-build-controller"
BUILDKIT_BUILD_SERVICE_ACCOUNT = "bertrand-build-controller"
BUILDKIT_BUILD_RECONCILE_SECONDS = 2.0
BUILDKIT_BUILD_WAIT_POLL_SECONDS = 2.0
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


_STRING_MAP_SCHEMA = {"type": "object", "additionalProperties": {"type": "string"}}
_BOOL_MAP_SCHEMA = {"type": "object", "additionalProperties": {"type": "boolean"}}
_STRING_LIST_SCHEMA = {
    "type": "array",
    "items": {"type": "string", "minLength": 1},
    "uniqueItems": True,
}
_BUILDKIT_BUILD_SPEC_SCHEMA = {
    "type": "object",
    "required": [
        "repo_id",
        "worktree",
        "tag",
        "env_id",
        "config_id",
        "image",
        "dockerfile",
        "network",
        "channels",
    ],
    "properties": {
        "repo_id": {"type": "string", "minLength": 1},
        "worktree": {"type": "string", "minLength": 1},
        "tag": {"type": "string"},
        "env_id": {"type": "string", "minLength": 1},
        "config_id": {"type": "string", "minLength": 1},
        "image": {"type": "string", "minLength": 1},
        "dockerfile": {"type": "string", "minLength": 1},
        "build_args": _STRING_MAP_SCHEMA,
        "target": {"type": "string", "nullable": True},
        "network": {"type": "string", "enum": ["default", "none", "host"]},
        "secrets": _BOOL_MAP_SCHEMA,
        "ssh": _BOOL_MAP_SCHEMA,
        "devices": _BOOL_MAP_SCHEMA,
        "channels": _STRING_LIST_SCHEMA,
        "external_image": {"type": "string", "nullable": True},
        "auth_id": {"type": "string", "nullable": True},
    },
}
_BUILDKIT_BUILD_STATUS_SCHEMA = {
    "type": "object",
    "properties": {
        "phase": {
            "type": "string",
            "enum": ["Pending", "Running", "Succeeded", "Failed"],
        },
        "observedGeneration": {"type": "integer", "nullable": True},
        "started_at": {"type": "string", "format": "date-time", "nullable": True},
        "completed_at": {"type": "string", "format": "date-time", "nullable": True},
        "active_job": {"type": "string"},
        "active_platform": {"type": "string"},
        "external_digest_ref": {"type": "string"},
        "external_channel_digest_refs": _STRING_MAP_SCHEMA,
        "record_name": {"type": "string"},
        "message": {"type": "string"},
        "log_excerpt": {"type": "string"},
    },
}
_BUILDKIT_BUILD_SPEC = CustomResourceSpec(
    group=BUILDKIT_BUILD_GROUP,
    version=BUILDKIT_BUILD_VERSION,
    kind=BUILDKIT_BUILD_KIND,
    plural=BUILDKIT_BUILD_PLURAL,
    labels=BUILDKIT_BUILD_LABELS,
)
_BUILDKIT_BUILD_CLIENT = CustomResourceClient(_BUILDKIT_BUILD_SPEC)


class _BuildKitBuildController:
    """In-cluster reconciler for durable BuildKit build requests."""

    def __init__(self) -> None:
        self._next_gc_at = datetime.min.replace(tzinfo=UTC)
        self._next_registry_gc_at = datetime.min.replace(tzinfo=UTC)

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
        objects = await _BUILDKIT_BUILD_CLIENT.list(
            kube,
            namespace=BERTRAND_NAMESPACE,
            labels={BUILDKIT_BUILD_LABEL: BUILDKIT_BUILD_LABEL_VALUE},
            timeout=deadline - loop.time(),
        )
        requests = [BuildKitBuildRecord.from_payload(obj.payload) for obj in objects]
        for request in sorted(requests, key=lambda item: item.metadata.name):
            if (
                request.status.observed_generation == request.metadata.generation
                and request.status.phase in ("Succeeded", "Failed")
            ):
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
            await _patch_build_status(
                kube,
                name=request.metadata.name,
                phase="Running",
                generation=request.metadata.generation,
                started_at=datetime.now(UTC).isoformat(),
                completed_at=None,
                active_job="",
                active_platform="",
                external_digest_ref="",
                external_channel_digest_refs={},
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
            await _patch_build_status(
                kube,
                name=request.metadata.name,
                phase="Succeeded",
                generation=request.metadata.generation,
                completed_at=datetime.now(UTC).isoformat(),
                active_job="",
                active_platform="",
                external_digest_ref=publication.external_digest_ref or "",
                external_channel_digest_refs=publication.external_channel_digest_refs,
                record_name=publication.record.name,
                message="BuildKit build succeeded",
                log_excerpt="",
                timeout=deadline - loop.time(),
            )
            self._schedule_gc_no_later_than(
                datetime.now(UTC) + timedelta(seconds=PROJECT_IMAGE_GC_GRACE_SECONDS)
            )
        except (OSError, TimeoutError, ValueError) as err:
            await _patch_build_status(
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
            await _patch_build_status(
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
            await _patch_build_status(
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

    async def _maybe_project_image_gc(self, kube: Kube, *, deadline: float) -> None:
        now = datetime.now(UTC)
        if now < self._next_gc_at:
            return

        loop = asyncio.get_running_loop()
        pass_deadline = min(
            deadline,
            loop.time() + PROJECT_IMAGE_GC_TIMEOUT_SECONDS,
        )
        if pass_deadline <= loop.time():
            return

        try:
            next_gc = await next_project_image_gc_time(
                kube,
                timeout=pass_deadline - loop.time(),
            )
            if next_gc is None:
                self._schedule_gc_after(PROJECT_IMAGE_GC_EMPTY_CHECK_SECONDS)
                return
            if next_gc > now:
                self._next_gc_at = next_gc
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
                    self._next_registry_gc_at = datetime.min.replace(tzinfo=UTC)
            elif marker_was_clean:
                await IMAGES.clear_storage_dirty(
                    kube,
                    timeout=pass_deadline - loop.time(),
                )
            self._schedule_gc_after(PROJECT_IMAGE_GC_READY_CHECK_SECONDS)
        except (OSError, TimeoutError, ValueError) as err:
            self._schedule_gc_after(PROJECT_IMAGE_GC_FAILURE_RETRY_SECONDS)
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
            if now < self._next_registry_gc_at:
                return
            dirty_since = maintenance.dirty_since or now
            idle_boundary = dirty_since + timedelta(
                seconds=REGISTRY_STORAGE_GC_IDLE_GRACE_SECONDS
            )
            if now < idle_boundary:
                self._next_registry_gc_at = idle_boundary
                return

            last_gc_at = maintenance.last_gc_at
            if (
                maintenance.dirty_count < REGISTRY_STORAGE_GC_DIRTY_THRESHOLD
                and last_gc_at is not None
                and now
                < last_gc_at
                + timedelta(seconds=REGISTRY_STORAGE_GC_MIN_INTERVAL_SECONDS)
            ):
                self._next_registry_gc_at = last_gc_at + timedelta(
                    seconds=REGISTRY_STORAGE_GC_MIN_INTERVAL_SECONDS
                )
                return

            if await self._has_active_build_requests(
                kube,
                timeout=pass_deadline - loop.time(),
            ):
                self._schedule_registry_gc_after(
                    REGISTRY_STORAGE_GC_ACTIVE_BUILD_RETRY_SECONDS
                )
                return
            if not await self._registry_deployment_ready(
                kube,
                timeout=pass_deadline - loop.time(),
            ):
                self._schedule_registry_gc_after(
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
                self._schedule_registry_gc_after(
                    REGISTRY_STORAGE_GC_ACTIVE_BUILD_RETRY_SECONDS
                )
                return
            await IMAGES.clear_storage_dirty(
                kube,
                last_gc_at=datetime.now(UTC),
                timeout=pass_deadline - loop.time(),
            )
            self._schedule_registry_gc_after(REGISTRY_STORAGE_GC_MIN_INTERVAL_SECONDS)
        except (OSError, TimeoutError, ValueError) as err:
            self._schedule_registry_gc_after(REGISTRY_STORAGE_GC_FAILURE_RETRY_SECONDS)
            print(
                f"warning: image registry storage garbage collection failed: {err}",
                file=sys.stderr,
                flush=True,
            )

    def _schedule_gc_after(self, seconds: float) -> None:
        self._next_gc_at = datetime.now(UTC) + timedelta(seconds=seconds)

    def _schedule_gc_no_later_than(self, when: datetime) -> None:
        if self._next_gc_at <= datetime.now(UTC) or when < self._next_gc_at:
            self._next_gc_at = when

    def _schedule_registry_gc_after(self, seconds: float) -> None:
        self._next_registry_gc_at = datetime.now(UTC) + timedelta(seconds=seconds)

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

    async def _has_active_build_requests(
        self,
        kube: Kube,
        *,
        timeout: float,
    ) -> bool:
        objects = await _BUILDKIT_BUILD_CLIENT.list(
            kube,
            namespace=BERTRAND_NAMESPACE,
            labels={BUILDKIT_BUILD_LABEL: BUILDKIT_BUILD_LABEL_VALUE},
            timeout=timeout,
        )
        for obj in objects:
            request = BuildKitBuildRecord.from_payload(obj.payload)
            if request.status.observed_generation != request.metadata.generation:
                return True
            if request.status.phase not in ("Succeeded", "Failed"):
                return True
        return False

    async def _registry_storage_gc_still_idle(
        self,
        kube: Kube,
        *,
        timeout: float,
    ) -> bool:
        return not await self._has_active_build_requests(kube, timeout=timeout)

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


async def ensure_buildkit_build_crd(kube: Kube, *, timeout: float) -> None:
    """Converge the durable BuildKit build request CRD.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum convergence budget in seconds.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive or CRD establishment exceeds the budget.
    """
    if timeout <= 0:
        msg = "BuildKitBuild CRD timeout must be non-negative"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    crd = await CustomResourceDefinition.upsert(
        kube,
        group=BUILDKIT_BUILD_GROUP,
        version=BUILDKIT_BUILD_VERSION,
        plural=BUILDKIT_BUILD_PLURAL,
        singular="buildkitbuild",
        kind=BUILDKIT_BUILD_KIND,
        short_names=("bkbuild",),
        spec_schema=_BUILDKIT_BUILD_SPEC_SCHEMA,
        status_schema=_BUILDKIT_BUILD_STATUS_SCHEMA,
        labels=BUILDKIT_BUILD_LABELS,
        timeout=deadline - loop.time(),
    )
    await crd.wait_established(kube, timeout=deadline - loop.time())


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


async def submit_buildkit_build(
    kube: Kube,
    *,
    spec: BuildKitBuildSpec,
    timeout: float,
) -> BuildKitBuildRecord:
    """Create one durable project-image BuildKit request.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    spec : BuildKitBuildSpec
        Validated project image build request.
    timeout : float
        Maximum creation budget in seconds.

    Returns
    -------
    BuildKitBuildRecord
        Submitted build request.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive or request creation exceeds the budget.
    """
    if timeout <= 0:
        msg = "BuildKitBuild submit timeout must be non-negative"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    await ensure_buildkit_build_crd(kube, timeout=deadline - loop.time())
    obj = await _BUILDKIT_BUILD_CLIENT.create(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=_buildkit_build_name(spec),
        spec=spec.model_dump(mode="json"),
        labels=spec.request_labels,
        timeout=deadline - loop.time(),
    )
    return BuildKitBuildRecord.from_payload(obj.payload)


async def get_buildkit_build(
    kube: Kube,
    *,
    name: str,
    timeout: float,
) -> BuildKitBuildRecord | None:
    """Read one durable BuildKit build request.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    name : str
        Build request name.
    timeout : float
        Maximum request budget in seconds.

    Returns
    -------
    BuildKitBuildRecord | None
        Build request, or `None` if it does not exist.
    """
    obj = await _BUILDKIT_BUILD_CLIENT.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=name,
        timeout=timeout,
    )
    if obj is None:
        return None
    return BuildKitBuildRecord.from_payload(obj.payload)


async def has_active_buildkit_builds(
    kube: Kube,
    *,
    repo_id: str,
    timeout: float,
) -> bool:
    """Return whether a repository has non-terminal build requests.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : str
        Stable repository UUID to check.
    timeout : float
        Maximum request budget in seconds.

    Returns
    -------
    bool
        True when any current-generation or stale-generation build request for the
        repository has not reached a terminal phase.

    Raises
    ------
    OSError
        If BuildKit request records are malformed or cannot be listed.
    TimeoutError
        If `timeout` is non-positive.
    """
    if timeout <= 0:
        msg = "BuildKitBuild active-request check timeout must be non-negative"
        raise TimeoutError(msg)
    repo_id = _check_uuid(repo_id)
    try:
        objects = await _BUILDKIT_BUILD_CLIENT.list(
            kube,
            namespace=BERTRAND_NAMESPACE,
            labels={BUILDKIT_BUILD_LABEL: BUILDKIT_BUILD_LABEL_VALUE},
            timeout=timeout,
        )
    except OSError as err:
        detail = str(err).lower()
        if "not found" in detail or "status 404" in detail:
            return False
        raise
    records = [BuildKitBuildRecord.from_payload(obj.payload) for obj in objects]
    for record in records:
        if record.spec.repo_id != repo_id:
            continue
        if record.status.observed_generation != record.metadata.generation:
            return True
        if record.status.phase not in ("Succeeded", "Failed"):
            return True
    return False


async def wait_buildkit_build(
    kube: Kube,
    *,
    name: str,
    timeout: float,
    on_update: Callable[[BuildKitBuildRecord], Awaitable[None]] | None = None,
) -> BuildKitBuildRecord:
    """Wait for one durable BuildKit build request to reach a terminal phase.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    name : str
        Build request name.
    timeout : float
        Maximum wait budget in seconds.
    on_update : Callable[[BuildKitBuildRecord], Awaitable[None]] | None, optional
        Async callback invoked when the observed resource version changes.

    Returns
    -------
    BuildKitBuildRecord
        Terminal build request record.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive or expires before the request is terminal.
    OSError
        If the request disappears while waiting.
    """
    if timeout <= 0:
        msg = "BuildKitBuild wait timeout must be non-negative"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    seen_version = ""
    while True:
        remaining = deadline - loop.time()
        if remaining <= 0:
            msg = f"BuildKitBuild {name!r} did not finish before timeout"
            raise TimeoutError(msg)
        record = await get_buildkit_build(kube, name=name, timeout=remaining)
        if record is None:
            msg = f"BuildKitBuild {name!r} disappeared while waiting"
            raise OSError(msg)
        if record.resource_version != seen_version:
            seen_version = record.resource_version
            if on_update is not None:
                await on_update(record)
        if record.status.phase in ("Succeeded", "Failed"):
            return record
        await asyncio.sleep(min(BUILDKIT_BUILD_WAIT_POLL_SECONDS, remaining))


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
    )


def _buildkit_build_name(spec: BuildKitBuildSpec) -> str:
    text = json.dumps(
        spec.model_dump(mode="json"),
        sort_keys=True,
        separators=(",", ":"),
    )
    digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
    return f"bertrand-build-{digest[:16]}-{uuid.uuid4().hex[:8]}"


async def _patch_build_status(
    kube: Kube,
    *,
    name: str,
    phase: BuildKitBuildPhase,
    generation: int,
    timeout: float,
    **updates: object,
) -> BuildKitBuildRecord:
    if timeout <= 0:
        msg = "BuildKitBuild status patch timeout must be non-negative"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    payload: dict[str, object] = {}
    payload.update(updates)
    payload["phase"] = phase
    payload["observedGeneration"] = generation
    payload = BuildKitBuildStatus.model_validate(payload).model_dump(
        mode="json",
        by_alias=True,
        exclude_unset=True,
    )
    obj = await _BUILDKIT_BUILD_CLIENT.patch_status(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=name,
        status=payload,
        timeout=deadline - loop.time(),
    )
    return BuildKitBuildRecord.from_payload(obj.payload)


def _log_excerpt(text: str) -> str:
    lines = text.strip().splitlines()
    excerpt = "\n".join(lines[-80:])
    if len(excerpt) > BUILDKIT_BUILD_LOG_EXCERPT_CHARS:
        return excerpt[-BUILDKIT_BUILD_LOG_EXCERPT_CHARS:]
    return excerpt


if __name__ == "__main__":
    raise SystemExit(main())
