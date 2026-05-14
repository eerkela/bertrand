"""Minimal native Kubernetes workload rendering intents."""

from __future__ import annotations

from dataclasses import dataclass, replace
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from collections.abc import Sequence

    from bertrand.env.kube.api import (
        ContainerSpec,
        PodTemplateSpec,
    )


@dataclass(frozen=True)
class WorkloadPod:
    """Manual pod-template intent shared by native workloads.

    Parameters
    ----------
    template : PodTemplateSpec
        Base pod template to render. The template may contain multiple containers.
    primary_container : str
        Container name that receives command overrides.
    """

    template: PodTemplateSpec
    primary_container: str

    def __post_init__(self) -> None:
        """Validate the primary container name.

        Raises
        ------
        ValueError
            If the pod has no containers, names are duplicated, or the primary
            container is not part of the template.
        """
        containers = tuple(self.template.containers)
        container_names = _container_names(containers)
        primary = self.primary_container.strip()
        if primary not in container_names:
            msg = f"unknown primary workload container: {self.primary_container!r}"
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

    def pod_template(
        self,
        *,
        primary_command: Sequence[str] | None = None,
        primary_args: Sequence[str] | None = None,
    ) -> PodTemplateSpec:
        """Render this workload as a Kubernetes pod template.

        Parameters
        ----------
        primary_command : Sequence[str] | None, optional
            Optional command override for the primary container.
        primary_args : Sequence[str] | None, optional
            Optional argument override for the primary container.

        Returns
        -------
        PodTemplateSpec
            Pod template with command overrides applied.
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
        rendered_containers: list[ContainerSpec] = []
        for container in self.template.containers:
            if container.name.strip() == self.primary_container:
                container = replace(
                    container,
                    command=command if command is not None else container.command,
                    args=args if args is not None else container.args,
                )
            rendered_containers.append(container)

        return replace(
            self.template,
            containers=tuple(rendered_containers),
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
