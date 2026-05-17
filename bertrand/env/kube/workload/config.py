"""Adapters from validated Bertrand workload config to native workload intent."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Protocol, cast

from bertrand.env.git.bertrand_git import ENV_ID_ENV, IMAGE_TAG_ENV
from bertrand.env.kube.api.spec import ContainerPortSpec, ContainerSpec, PodTemplateSpec
from bertrand.env.kube.workload.base import WorkloadPod, WorkloadRepository
from bertrand.env.kube.workload.capability import (
    ResolvedWorkloadDevice,
    resolve_workload_capabilities,
)

if TYPE_CHECKING:
    from collections.abc import Sequence
    from pathlib import PurePosixPath

    from bertrand.env.kube.api.client import Kube
    from bertrand.env.kube.api.spec import PortProtocol
    from bertrand.env.kube.workload.capability import (
        WorkloadDeviceRequest,
        WorkloadSecretRequest,
    )

WORKLOAD_PRIMARY_CONTAINER = "main"


class _WorkloadPort(Protocol):
    container: int
    protocol: str


class _WorkloadConfig(Protocol):
    cmd: Sequence[str]
    ports: Sequence[_WorkloadPort]
    secrets: Sequence[WorkloadSecretRequest]
    devices: Sequence[WorkloadDeviceRequest]


@dataclass(frozen=True)
class ConfiguredWorkloadPod:
    """Rendered pod intent and unresolved native device handoff.

    Parameters
    ----------
    pod : WorkloadPod
        Workload pod intent rendered from validated config.
    devices : tuple[ResolvedWorkloadDevice, ...]
        Runtime device capabilities resolved from config. These are intentionally
        kept out of the Kubernetes pod template until device projection is designed.
    """

    pod: WorkloadPod
    devices: tuple[ResolvedWorkloadDevice, ...]


async def workload_pod_from_config(
    kube: Kube,
    *,
    workload: _WorkloadConfig,
    tag: str,
    repo_id: str,
    worktree: str | PurePosixPath,
    env_id: str,
    image: str,
    node: str | None = None,
    timeout: float,
) -> ConfiguredWorkloadPod:
    """Render validated Bertrand workload config into a native pod intent.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    workload : _WorkloadConfig
        Validated `[tool.bertrand.workload.<tag>]` config object.
    tag : str
        Workload/image key used for diagnostics.
    repo_id : str
        Stable repository UUID used to mount the managed Ceph repository PVC.
    worktree : str | PurePosixPath
        Relative worktree path inside the repository volume.
    env_id : str
        Environment UUID used for capability resolution.
    image : str
        Container image reference to run.
    node : str | None, optional
        Kubernetes node name used for node-scoped capability resolution.
    timeout : float
        Maximum capability resolution budget in seconds.

    Returns
    -------
    ConfiguredWorkloadPod
        Pod intent plus resolved device capability intent.

    Raises
    ------
    ValueError
        If `image` or the workload command is empty.
    """
    image = image.strip()
    if not image:
        msg = f"workload image for tag {tag!r} cannot be empty"
        raise ValueError(msg)
    command = _workload_command(workload.cmd, tag=tag)

    capabilities = await resolve_workload_capabilities(
        kube,
        secrets=tuple(workload.secrets),
        devices=tuple(workload.devices),
        env_id=env_id,
        node=node,
        timeout=timeout,
    )
    primary = ContainerSpec(
        name=WORKLOAD_PRIMARY_CONTAINER,
        image=image,
        command=command,
        ports=_container_ports(workload.ports),
        volume_mounts=capabilities.primary_mounts,
    )
    return ConfiguredWorkloadPod(
        pod=WorkloadPod(
            template=PodTemplateSpec(
                containers=(primary,),
                volumes=capabilities.volumes,
            ),
            primary_container=WORKLOAD_PRIMARY_CONTAINER,
            repository=WorkloadRepository(repo_id=repo_id, worktree=worktree),
            runtime_env={ENV_ID_ENV: env_id, IMAGE_TAG_ENV: tag},
        ),
        devices=capabilities.devices,
    )


def _workload_command(command: Sequence[str], *, tag: str) -> tuple[str, ...]:
    out: list[str] = []
    for part in command:
        value = part.strip()
        if not value:
            msg = f"workload command entries for tag {tag!r} cannot be empty"
            raise ValueError(msg)
        out.append(value)
    if not out:
        msg = f"workload tag {tag!r} requires an explicit command"
        raise ValueError(msg)
    return tuple(out)


def _container_ports(ports: Sequence[_WorkloadPort]) -> tuple[ContainerPortSpec, ...]:
    rendered: list[ContainerPortSpec] = []
    for index, port in enumerate(ports):
        rendered.append(
            ContainerPortSpec(
                name=f"port-{index}",
                container_port=port.container,
                protocol=cast("PortProtocol", port.protocol.upper()),
            )
        )
    return tuple(rendered)
