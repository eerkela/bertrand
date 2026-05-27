"""Runtime capability resolution for native Kubernetes workloads."""

from __future__ import annotations

import hashlib
from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING, Protocol

from bertrand.env.config.core import _check_kube_name
from bertrand.env.git import Deadline
from bertrand.env.kube.api.spec import VolumeSpec
from bertrand.env.kube.capability.base import resolve_capability_secret
from bertrand.env.kube.capability.device import (
    pod_resource_claim,
    resource_claim_name,
    select_device_claims,
)

if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence

    from bertrand.env.config.bertrand import BertrandModel
    from bertrand.env.config.core import KubeName
    from bertrand.env.kube.api.client import Kube
    from bertrand.env.kube.api.spec import PodResourceClaimManifest
    from bertrand.env.kube.capability.base import CapabilityKind

WORKLOAD_SECRET_MOUNT = "/run/secrets"
WORKLOAD_SSH_MOUNT = "/run/bertrand/ssh"
CAPABILITY_VALUE_KEY = "value"
SSH_PRIVATE_KEY_FILE = "ssh-privatekey"


class CapabilityRequest(Protocol):
    """Structural capability request with an ID and required flag.

    Attributes
    ----------
    id : KubeName
        Host-agnostic capability ID.
    required : bool
        Whether resolution must fail if the capability is unavailable.
    """

    id: KubeName
    required: bool


@dataclass(frozen=True)
class WorkloadCapabilities:
    """Resolved capability additions for a native workload pod.

    Parameters
    ----------
    volumes : tuple[VolumeSpec, ...]
        Pod volumes required by resolved Secret capabilities.
    mounts_by_container : Mapping[str, tuple[Mapping[str, object], ...]]
        Secret mounts keyed by container name.
    resource_claims : tuple[PodResourceClaimManifest, ...]
        Pod resource claims requested by workload containers.
    claim_names_by_container : Mapping[str, tuple[str, ...]]
        DRA claim names keyed by container for container resource references.
    claim_capabilities_by_container : Mapping[str, tuple[str, ...]]
        Selected DRA capability IDs keyed by container for template convergence.
    """

    volumes: tuple[VolumeSpec, ...]
    mounts_by_container: Mapping[str, tuple[Mapping[str, object], ...]]
    resource_claims: tuple[PodResourceClaimManifest, ...]
    claim_names_by_container: Mapping[str, tuple[str, ...]]
    claim_capabilities_by_container: Mapping[str, tuple[str, ...]]


async def resolve_workload_capabilities(
    kube: Kube,
    *,
    containers: tuple[BertrandModel.Container, ...],
    worktree_id: str,
    repo_id: str,
    claim_owner: str,
    host_id: str | None = None,
    node_name: str | None = None,
    timeout: float,
) -> WorkloadCapabilities:
    """Resolve runtime Secret, SSH, and device capabilities for one workload.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    containers : tuple[BertrandModel.Container, ...]
        Validated workload containers whose capability requests should be resolved.
    worktree_id : str
        Persistent worktree UUID used for the first capability lookup tier.
    repo_id : str
        Stable repository UUID used for the second capability lookup tier.
    claim_owner : str
        Stable workload owner string used to derive DRA ResourceClaimTemplate names.
    host_id : str | None, optional
        Bertrand host UUID used for node-scoped Secret and SSH lookup.
    node_name : str | None, optional
        Kubernetes node name used to constrain DRA inventory.
    timeout : float
        Maximum resolution budget in seconds.

    Returns
    -------
    WorkloadCapabilities
        Resolved workload capability intent.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    """
    if timeout <= 0:
        msg = "workload capability resolution timeout must be non-negative"
        raise TimeoutError(msg)
    deadline = Deadline.from_timeout(
        timeout,
        message="workload capability resolution timeout must be non-negative",
    )
    volumes: dict[str, VolumeSpec] = {}
    mounts_by_container: dict[str, list[Mapping[str, object]]] = {
        _check_kube_name(container.name): [] for container in containers
    }
    resource_claims: list[PodResourceClaimManifest] = []
    claim_names_by_container: dict[str, list[str]] = {
        _check_kube_name(container.name): [] for container in containers
    }
    claim_capabilities_by_container: dict[str, list[str]] = {
        _check_kube_name(container.name): [] for container in containers
    }

    for container in containers:
        container_name = _check_kube_name(container.name)
        for request in container.secrets:
            resolved = await _resolve_secret_mount(
                kube,
                kind="secret",
                request=request,
                worktree_id=worktree_id,
                repo_id=repo_id,
                host_id=host_id,
                deadline=deadline,
            )
            if resolved is None:
                continue
            volume, mount = resolved
            volumes.setdefault(volume.name, volume)
            mounts_by_container[container_name].append(mount)

        for request in container.ssh:
            resolved = await _resolve_secret_mount(
                kube,
                kind="ssh",
                request=request,
                worktree_id=worktree_id,
                repo_id=repo_id,
                host_id=host_id,
                deadline=deadline,
            )
            if resolved is None:
                continue
            volume, mount = resolved
            volumes.setdefault(volume.name, volume)
            mounts_by_container[container_name].append(mount)

        _record_device_claims(
            await _resolve_device_claims(
                kube,
                requests=container.devices,
                host_id=host_id,
                node_name=node_name,
                deadline=deadline,
            ),
            owner=claim_owner,
            container_name=container_name,
            resource_claims=resource_claims,
            claim_names_by_container=claim_names_by_container,
            claim_capabilities_by_container=claim_capabilities_by_container,
        )

    return _finalize_capabilities(
        volumes=volumes,
        mounts_by_container=mounts_by_container,
        resource_claims=resource_claims,
        claim_names_by_container=claim_names_by_container,
        claim_capabilities_by_container=claim_capabilities_by_container,
    )


async def _resolve_secret_mount(
    kube: Kube,
    *,
    kind: CapabilityKind,
    request: CapabilityRequest,
    worktree_id: str,
    repo_id: str,
    host_id: str | None,
    deadline: Deadline,
) -> tuple[VolumeSpec, Mapping[str, object]] | None:
    capability_id = _check_kube_name(str(request.id))
    secret = await resolve_capability_secret(
        kube,
        kind=kind,
        capability_id=capability_id,
        worktree_id=worktree_id,
        repo_id=repo_id,
        host_id=host_id,
        required=request.required,
        timeout=deadline.remaining(),
    )
    if secret is None:
        return None

    volume_name = _capability_volume_name(kind, capability_id)
    mount_path = _capability_mount_path(kind, capability_id)
    volume = _capability_volume(
        kind,
        volume_name=volume_name,
        secret_name=secret.name,
    )
    mount = {"name": volume_name, "mountPath": mount_path, "readOnly": True}
    return volume, mount


async def _resolve_device_claims(
    kube: Kube,
    *,
    requests: Sequence[CapabilityRequest],
    host_id: str | None,
    node_name: str | None,
    deadline: Deadline,
) -> tuple[str, ...]:
    return await select_device_claims(
        kube,
        requests={
            _check_kube_name(str(request.id)): request.required for request in requests
        },
        host_ids=(host_id,) if host_id is not None else None,
        node_names=(node_name,) if node_name is not None else None,
        timeout=deadline.remaining(),
    )


def _record_device_claims(
    capability_ids: tuple[str, ...],
    *,
    owner: str,
    container_name: str,
    resource_claims: list[PodResourceClaimManifest],
    claim_names_by_container: dict[str, list[str]],
    claim_capabilities_by_container: dict[str, list[str]],
) -> None:
    for capability_id in capability_ids:
        resource_claims.append(
            pod_resource_claim(
                owner=owner,
                capability_id=capability_id,
                container_name=container_name,
            )
        )
        claim_names_by_container[container_name].append(
            resource_claim_name(
                owner=owner,
                capability_id=capability_id,
                container_name=container_name,
            )
        )
        claim_capabilities_by_container[container_name].append(capability_id)


def _finalize_capabilities(
    *,
    volumes: dict[str, VolumeSpec],
    mounts_by_container: dict[str, list[Mapping[str, object]]],
    resource_claims: list[PodResourceClaimManifest],
    claim_names_by_container: dict[str, list[str]],
    claim_capabilities_by_container: dict[str, list[str]],
) -> WorkloadCapabilities:
    mounts = MappingProxyType(
        {
            container: tuple(entries)
            for container, entries in sorted(mounts_by_container.items())
        }
    )
    return WorkloadCapabilities(
        volumes=tuple(volumes[name] for name in sorted(volumes)),
        mounts_by_container=mounts,
        resource_claims=tuple(resource_claims),
        claim_names_by_container=MappingProxyType(
            {
                container: tuple(entries)
                for container, entries in sorted(claim_names_by_container.items())
            }
        ),
        claim_capabilities_by_container=MappingProxyType(
            {
                container: tuple(entries)
                for container, entries in sorted(
                    claim_capabilities_by_container.items()
                )
            }
        ),
    )


def _capability_volume(
    kind: CapabilityKind,
    *,
    volume_name: str,
    secret_name: str,
) -> VolumeSpec:
    if kind == "ssh":
        return VolumeSpec.secret(
            volume_name,
            secret_name=secret_name,
            default_mode=0o400,
            items=(
                {"key": CAPABILITY_VALUE_KEY, "path": SSH_PRIVATE_KEY_FILE},
            ),
        )
    return VolumeSpec.secret(
        volume_name,
        secret_name=secret_name,
        default_mode=0o400,
    )


def _capability_mount_path(kind: CapabilityKind, capability_id: str) -> str:
    root = WORKLOAD_SSH_MOUNT if kind == "ssh" else WORKLOAD_SECRET_MOUNT
    return f"{root}/{capability_id}"


def _capability_volume_name(kind: str, capability_id: str) -> str:
    payload = f"{kind}:{capability_id}".encode()
    digest = hashlib.sha256(payload).hexdigest()[:16]
    return f"workload-{kind}-{digest}"
