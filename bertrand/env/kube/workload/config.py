"""Adapters from validated Bertrand workload config to native workload intent."""

from __future__ import annotations

from typing import TYPE_CHECKING, Protocol, cast

from bertrand.env.git.bertrand_git import ENV_ID_ENV
from bertrand.env.kube.api.spec import (
    ContainerPortSpec,
    ContainerResourcesSpec,
    ContainerSpec,
    PodTemplateSpec,
)
from bertrand.env.kube.workload.base import WorkloadPod, WorkloadRepository
from bertrand.env.kube.workload.capability import resolve_workload_capabilities

if TYPE_CHECKING:
    from collections.abc import Sequence
    from pathlib import PurePosixPath

    from bertrand.env.kube.api.client import Kube
    from bertrand.env.kube.api.spec import PortProtocol
    from bertrand.env.kube.workload.capability import (
        WorkloadDeviceRequest,
        WorkloadSecretRequest,
    )


class _WorkloadPort(Protocol):
    name: str
    port: int
    protocol: str


class _WorkloadContainer(Protocol):
    name: str
    cmd: Sequence[str]
    ports: Sequence[_WorkloadPort]
    secrets: Sequence[WorkloadSecretRequest]
    devices: Sequence[WorkloadDeviceRequest]


class _WorkloadConfig(Protocol):
    containers: Sequence[_WorkloadContainer]


async def workload_pod_from_config(
    kube: Kube,
    *,
    config: _WorkloadConfig | None,
    repo_id: str,
    worktree: str | PurePosixPath,
    env_id: str,
    image: str,
    node: str | None = None,
    timeout: float,
) -> WorkloadPod | None:
    """Render validated Bertrand workload config into a native pod intent.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    config : _WorkloadConfig
        Validated `[tool.bertrand]` config object, or `None` for image/library-only
        worktrees.
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
    WorkloadPod | None
        Pod intent, or `None` when no workload is configured.

    Raises
    ------
    ValueError
        If `image` or any workload container command is empty.
    """
    if config is None:
        return None
    image = image.strip()
    if not image:
        msg = "workload image cannot be empty"
        raise ValueError(msg)
    containers = tuple(config.containers)
    if not containers:
        return None

    capabilities = await resolve_workload_capabilities(
        kube,
        containers=containers,
        env_id=env_id,
        node=node,
        timeout=timeout,
    )
    rendered: list[ContainerSpec] = []
    for container in containers:
        claims = tuple(
            claim.claim_name
            for claim in capabilities.resource_claims
            if claim.container_name == container.name
        )
        rendered.append(
            ContainerSpec(
                name=container.name,
                image=image,
                command=_workload_command(container.cmd, container=container.name),
                ports=_container_ports(container.ports),
                volume_mounts=capabilities.mounts_by_container.get(
                    container.name,
                    (),
                ),
                resources=ContainerResourcesSpec(claims=claims) if claims else None,
            )
        )
    rendered_containers = tuple(rendered)
    return WorkloadPod(
        template=PodTemplateSpec(
            containers=rendered_containers,
            volumes=capabilities.volumes,
            resource_claims=tuple(
                claim.pod_claim() for claim in capabilities.resource_claims
            ),
        ),
        primary_container=containers[0].name,
        repository=WorkloadRepository(repo_id=repo_id, worktree=worktree),
        resource_claim_templates=capabilities.resource_claims,
        runtime_env={ENV_ID_ENV: env_id},
    )


def _workload_command(command: Sequence[str], *, container: str) -> tuple[str, ...]:
    out: list[str] = []
    for part in command:
        value = part.strip()
        if not value:
            msg = (
                f"workload command entries for container {container!r} cannot be empty"
            )
            raise ValueError(msg)
        out.append(value)
    if not out:
        msg = f"workload container {container!r} requires an explicit command"
        raise ValueError(msg)
    return tuple(out)


def _container_ports(
    ports: Sequence[_WorkloadPort],
) -> tuple[ContainerPortSpec, ...]:
    return tuple(
        ContainerPortSpec(
            name=port.name,
            container_port=port.port,
            protocol=cast("PortProtocol", port.protocol.upper()),
        )
        for port in ports
    )
