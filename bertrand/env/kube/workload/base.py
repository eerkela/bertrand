"""Minimal native Kubernetes workload rendering intents."""

from __future__ import annotations

from dataclasses import dataclass, replace
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from collections.abc import Collection, Sequence

    from bertrand.env.kube.api import (
        ContainerSpec,
        PodTemplateSpec,
        VolumeMountSpec,
    )

    from .cache import CacheVolume


@dataclass(frozen=True)
class WorkloadPod:
    """Manual pod-template intent shared by native workloads.

    Parameters
    ----------
    template : PodTemplateSpec
        Base pod template to render. The template may contain multiple containers.
    primary_container : str
        Container name that receives command overrides and default cache mounts.
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
