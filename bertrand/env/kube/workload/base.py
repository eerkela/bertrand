"""Minimal native Kubernetes workload rendering intents."""

from __future__ import annotations

from dataclasses import dataclass, replace
from types import MappingProxyType
from typing import TYPE_CHECKING

from bertrand.env.kube.cronjob import CronJob
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.job import Job

if TYPE_CHECKING:
    from collections.abc import Collection, Mapping, Sequence

    from bertrand.env.kube.api import (
        ContainerSpec,
        DeploymentStrategySpec,
        Kube,
        PodTemplateSpec,
        VolumeMountSpec,
    )
    from bertrand.env.kube.cronjob import CronJobConcurrencyPolicy

    from .cache import CacheVolume


@dataclass(frozen=True)
class WorkloadPod:
    """Manual pod-template intent shared by native workloads.

    Parameters
    ----------
    template : PodTemplateSpec
        Base pod template to render. The template may contain multiple containers.
    primary_container : str
        Container name that receives job entrypoints and default cache mounts.
    cache_containers : Collection[str], optional
        Additional container names that should receive resource cache mounts.
    """

    template: PodTemplateSpec
    primary_container: str
    cache_containers: Collection[str] = ()

    def __post_init__(self) -> None:
        """Validate the primary and cache container names.

        Raises
        ------
        ValueError
            If the pod has no containers, names are duplicated, or the primary/cache
            containers are not part of the template.
        """
        containers = tuple(self.template.containers)
        container_names = _container_names(containers)
        primary = self.primary_container.strip()
        if primary not in container_names:
            msg = f"unknown primary workload container: {self.primary_container!r}"
            raise ValueError(msg)

        cache_containers = _unique_names(self.cache_containers, label="cache container")
        unknown = sorted(
            name for name in cache_containers if name not in container_names
        )
        if unknown:
            msg = f"unknown cache workload containers: {', '.join(unknown)}"
            raise ValueError(msg)

        object.__setattr__(
            self,
            "template",
            replace(
                self.template,
                containers=containers,
                volumes=tuple(self.template.volumes),
            ),
        )
        object.__setattr__(self, "primary_container", primary)
        object.__setattr__(self, "cache_containers", cache_containers)

    def pod_template(
        self,
        *,
        cache_volumes: Collection[CacheVolume] = (),
        cache_read_only: bool | None = None,
        primary_command: Sequence[str] | None = None,
        primary_args: Sequence[str] | None = None,
    ) -> PodTemplateSpec:
        """Render this workload as a Kubernetes pod template.

        Parameters
        ----------
        cache_volumes : Collection[CacheVolume], optional
            Resource cache volumes to mount into selected containers.
        cache_read_only : bool | None, optional
            Whether cache PVCs should be mounted read-only. ``None`` leaves the
            Kubernetes default.
        primary_command : Sequence[str] | None, optional
            Optional command override for the primary container.
        primary_args : Sequence[str] | None, optional
            Optional argument override for the primary container.

        Returns
        -------
        PodTemplateSpec
            Pod template with command overrides and cache mounts applied.
        """
        command = (
            _command(primary_command, label="primary command")
            if primary_command is not None
            else None
        )
        args = (
            _command(primary_args, label="primary args", allow_empty=True)
            if primary_args is not None
            else None
        )
        return self._with_cache_volumes(
            cache_volumes=tuple(cache_volumes),
            cache_read_only=cache_read_only,
            primary_command=command,
            primary_args=args,
        )

    def _with_cache_volumes(
        self,
        *,
        cache_volumes: tuple[CacheVolume, ...],
        cache_read_only: bool | None,
        primary_command: tuple[str, ...] | None,
        primary_args: tuple[str, ...] | None,
    ) -> PodTemplateSpec:
        cache_specs = tuple(volume.volume_spec() for volume in cache_volumes)
        cache_mounts = tuple(
            volume.volume_mount(read_only=cache_read_only) for volume in cache_volumes
        )
        existing_volume_names = {volume.name for volume in self.template.volumes}
        for volume in cache_specs:
            if volume.name in existing_volume_names:
                msg = f"workload pod already defines volume {volume.name!r}"
                raise ValueError(msg)
            existing_volume_names.add(volume.name)

        cache_targets = {self.primary_container, *self.cache_containers}
        rendered_containers: list[ContainerSpec] = []
        for container in self.template.containers:
            volume_mounts = tuple(container.volume_mounts)
            if container.name.strip() in cache_targets:
                volume_mounts = _append_cache_mounts(container, cache_mounts)
            if container.name.strip() == self.primary_container:
                container = replace(
                    container,
                    command=primary_command
                    if primary_command is not None
                    else container.command,
                    args=primary_args if primary_args is not None else container.args,
                    volume_mounts=volume_mounts,
                )
            else:
                container = replace(container, volume_mounts=volume_mounts)
            rendered_containers.append(container)

        return replace(
            self.template,
            containers=tuple(rendered_containers),
            volumes=(*self.template.volumes, *cache_specs),
        )


@dataclass(frozen=True)
class JobWorkload:
    """Manual intent for a native Kubernetes Job or CronJob workload.

    Parameters
    ----------
    namespace : str
        Namespace that owns the workload.
    name : str
        Kubernetes workload name.
    pod : WorkloadPod
        Pod intent whose primary container is used for the job entrypoint.
    entrypoint : Sequence[str]
        Primary container command to run as PID 1.
    args : Sequence[str], optional
        Primary container command arguments.
    labels : Mapping[str, str], optional
        Labels to apply to the native workload object.
    annotations : Mapping[str, str] | None, optional
        Annotations to apply to the native workload object.
    schedule : str | None, optional
        Cron schedule. If omitted, a one-shot Job is created.
    backoff_limit : int, optional
        Kubernetes Job retry limit.
    ttl_seconds_after_finished : int | None, optional
        Optional TTL controller retention period for finished Jobs.
    concurrency_policy : {"Allow", "Forbid", "Replace"}, optional
        CronJob concurrency policy used when ``schedule`` is present.
    suspend : bool | None, optional
        Whether to suspend future scheduled Jobs.
    starting_deadline_seconds : int | None, optional
        Optional deadline for starting missed CronJobs.
    successful_jobs_history_limit : int | None, optional
        Optional number of successful CronJobs to retain.
    failed_jobs_history_limit : int | None, optional
        Optional number of failed CronJobs to retain.
    time_zone : str | None, optional
        Optional IANA time zone name for CronJob schedule interpretation.
    """

    namespace: str
    name: str
    pod: WorkloadPod
    entrypoint: Sequence[str]
    args: Sequence[str] = ()
    labels: Mapping[str, str] = MappingProxyType({})
    annotations: Mapping[str, str] | None = None
    schedule: str | None = None
    backoff_limit: int = 0
    ttl_seconds_after_finished: int | None = 3600
    concurrency_policy: CronJobConcurrencyPolicy = "Forbid"
    suspend: bool | None = None
    starting_deadline_seconds: int | None = None
    successful_jobs_history_limit: int | None = None
    failed_jobs_history_limit: int | None = None
    time_zone: str | None = None

    def __post_init__(self) -> None:
        """Validate and normalize the job workload intent."""
        namespace, name = _namespace_name(
            self.namespace,
            self.name,
            kind="Job workload",
        )
        object.__setattr__(self, "namespace", namespace)
        object.__setattr__(self, "name", name)
        object.__setattr__(self, "entrypoint", _command(self.entrypoint))
        object.__setattr__(
            self,
            "args",
            _command(self.args, label="job args", allow_empty=True),
        )
        schedule = self.schedule.strip() if self.schedule is not None else ""
        object.__setattr__(self, "schedule", schedule or None)
        _check_nonnegative("job backoff limit", self.backoff_limit)
        if self.ttl_seconds_after_finished is not None:
            _check_nonnegative("job TTL", self.ttl_seconds_after_finished)

    def pod_template(
        self,
        *,
        cache_volumes: Collection[CacheVolume] = (),
        cache_read_only: bool | None = None,
    ) -> PodTemplateSpec:
        """Render this job workload's pod template.

        Parameters
        ----------
        cache_volumes : Collection[CacheVolume], optional
            Resource cache volumes to mount into selected containers.
        cache_read_only : bool | None, optional
            Whether cache PVCs should be mounted read-only.

        Returns
        -------
        PodTemplateSpec
            Pod template with entrypoint and cache mounts applied.
        """
        return self.pod.pod_template(
            cache_volumes=cache_volumes,
            cache_read_only=cache_read_only,
            primary_command=self.entrypoint,
            primary_args=self.args,
        )

    async def converge(
        self,
        kube: Kube,
        *,
        timeout: float,
        cache_volumes: Collection[CacheVolume] = (),
        cache_read_only: bool | None = None,
    ) -> Job | CronJob:
        """Create or converge the native Kubernetes workload.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        cache_volumes : Collection[CacheVolume], optional
            Resource cache volumes to mount into selected containers.
        cache_read_only : bool | None, optional
            Whether cache PVCs should be mounted read-only.

        Returns
        -------
        Job | CronJob
            Created Job when unscheduled, or upserted CronJob when scheduled.
        """
        pod_template = self.pod_template(
            cache_volumes=cache_volumes,
            cache_read_only=cache_read_only,
        )
        if self.schedule is None:
            return await Job.create(
                kube,
                namespace=self.namespace,
                name=self.name,
                labels=self.labels,
                pod_template=pod_template,
                timeout=timeout,
                backoff_limit=self.backoff_limit,
                ttl_seconds_after_finished=self.ttl_seconds_after_finished,
                annotations=self.annotations,
            )
        return await CronJob.upsert(
            kube,
            namespace=self.namespace,
            name=self.name,
            labels=self.labels,
            pod_template=pod_template,
            schedule=self.schedule,
            timeout=timeout,
            annotations=self.annotations,
            backoff_limit=self.backoff_limit,
            ttl_seconds_after_finished=self.ttl_seconds_after_finished,
            concurrency_policy=self.concurrency_policy,
            suspend=self.suspend,
            starting_deadline_seconds=self.starting_deadline_seconds,
            successful_jobs_history_limit=self.successful_jobs_history_limit,
            failed_jobs_history_limit=self.failed_jobs_history_limit,
            time_zone=self.time_zone,
        )


@dataclass(frozen=True)
class DeploymentWorkload:
    """Manual intent for a native Kubernetes Deployment workload.

    Parameters
    ----------
    namespace : str
        Namespace that owns the Deployment.
    name : str
        Kubernetes Deployment name.
    pod : WorkloadPod
        Pod intent to render into the Deployment.
    selector : Mapping[str, str]
        Immutable pod selector labels for the Deployment.
    labels : Mapping[str, str], optional
        Labels to apply to the Deployment object.
    annotations : Mapping[str, str] | None, optional
        Annotations to apply to the Deployment object.
    replicas : int, optional
        Desired replica count.
    strategy : DeploymentStrategySpec | None, optional
        Optional Deployment rollout strategy.
    """

    namespace: str
    name: str
    pod: WorkloadPod
    selector: Mapping[str, str]
    labels: Mapping[str, str] = MappingProxyType({})
    annotations: Mapping[str, str] | None = None
    replicas: int = 1
    strategy: DeploymentStrategySpec | None = None

    def __post_init__(self) -> None:
        """Validate and normalize the deployment workload intent.

        Raises
        ------
        ValueError
            If the namespace, name, selector, or replica count is invalid.
        """
        namespace, name = _namespace_name(
            self.namespace,
            self.name,
            kind="Deployment workload",
        )
        object.__setattr__(self, "namespace", namespace)
        object.__setattr__(self, "name", name)
        selector = _mapping(self.selector, label="deployment selector")
        if not selector:
            msg = "deployment workload selector cannot be empty"
            raise ValueError(msg)
        object.__setattr__(self, "selector", selector)
        _check_nonnegative("deployment replicas", self.replicas)

    def pod_template(
        self,
        *,
        cache_volumes: Collection[CacheVolume] = (),
        cache_read_only: bool | None = None,
    ) -> PodTemplateSpec:
        """Render this deployment workload's pod template.

        Parameters
        ----------
        cache_volumes : Collection[CacheVolume], optional
            Resource cache volumes to mount into selected containers.
        cache_read_only : bool | None, optional
            Whether cache PVCs should be mounted read-only.

        Returns
        -------
        PodTemplateSpec
            Pod template with cache mounts applied.
        """
        return self.pod.pod_template(
            cache_volumes=cache_volumes,
            cache_read_only=cache_read_only,
        )

    async def converge(
        self,
        kube: Kube,
        *,
        timeout: float,
        cache_volumes: Collection[CacheVolume] = (),
        cache_read_only: bool | None = None,
    ) -> Deployment:
        """Create or patch the native Kubernetes Deployment.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        cache_volumes : Collection[CacheVolume], optional
            Resource cache volumes to mount into selected containers.
        cache_read_only : bool | None, optional
            Whether cache PVCs should be mounted read-only.

        Returns
        -------
        Deployment
            Wrapped created or patched Deployment.
        """
        return await Deployment.upsert(
            kube,
            namespace=self.namespace,
            name=self.name,
            labels=self.labels,
            selector=self.selector,
            pod_template=self.pod_template(
                cache_volumes=cache_volumes,
                cache_read_only=cache_read_only,
            ),
            timeout=timeout,
            replicas=self.replicas,
            annotations=self.annotations,
            strategy=self.strategy,
        )


def _container_names(containers: tuple[ContainerSpec, ...]) -> set[str]:
    if not containers:
        msg = "workload pod requires at least one container"
        raise ValueError(msg)
    names: set[str] = set()
    for container in containers:
        name = container.name.strip()
        if not name:
            msg = "workload container names cannot be empty"
            raise ValueError(msg)
        if name in names:
            msg = f"duplicate workload container name: {name!r}"
            raise ValueError(msg)
        names.add(name)
    return names


def _unique_names(names: Collection[str], *, label: str) -> tuple[str, ...]:
    out: list[str] = []
    seen: set[str] = set()
    for raw in names:
        name = raw.strip()
        if not name:
            msg = f"{label} names cannot be empty"
            raise ValueError(msg)
        if name in seen:
            msg = f"duplicate {label} name: {name!r}"
            raise ValueError(msg)
        seen.add(name)
        out.append(name)
    return tuple(out)


def _append_cache_mounts(
    container: ContainerSpec,
    cache_mounts: tuple[VolumeMountSpec, ...],
) -> tuple[VolumeMountSpec, ...]:
    existing = tuple(container.volume_mounts)
    names = {mount.name for mount in existing}
    paths = {mount.mount_path for mount in existing}
    for mount in cache_mounts:
        if mount.name in names:
            msg = f"container {container.name!r} already mounts volume {mount.name!r}"
            raise ValueError(msg)
        if mount.mount_path in paths:
            msg = (
                f"container {container.name!r} already mounts path {mount.mount_path!r}"
            )
            raise ValueError(msg)
        names.add(mount.name)
        paths.add(mount.mount_path)
    return (*existing, *cache_mounts)


def _command(
    command: Sequence[str],
    *,
    label: str = "job entrypoint",
    allow_empty: bool = False,
) -> tuple[str, ...]:
    out: list[str] = []
    for raw in command:
        part = raw.strip()
        if not part:
            msg = f"{label} entries cannot be empty"
            raise ValueError(msg)
        out.append(part)
    if not allow_empty and not out:
        msg = f"{label} cannot be empty"
        raise ValueError(msg)
    return tuple(out)


def _namespace_name(namespace: str, name: str, *, kind: str) -> tuple[str, str]:
    namespace = namespace.strip()
    name = name.strip()
    if not namespace or not name:
        msg = f"{kind} requires non-empty namespace and name"
        raise ValueError(msg)
    return namespace, name


def _mapping(values: Mapping[str, str], *, label: str) -> Mapping[str, str]:
    out: dict[str, str] = {}
    for raw_key, raw_value in values.items():
        key = raw_key.strip()
        value = raw_value.strip()
        if not key or not value:
            msg = f"{label} keys and values cannot be empty"
            raise ValueError(msg)
        out[key] = value
    return MappingProxyType(out)


def _check_nonnegative(label: str, value: int) -> None:
    if value < 0:
        msg = f"{label} cannot be negative"
        raise ValueError(msg)
