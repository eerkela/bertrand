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
    DRAResourceClaimIntent,
    resource_claim_intents,
    select_device_claims,
)

if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence

    from bertrand.env.config.bertrand import BertrandModel
    from bertrand.env.config.core import KubeName
    from bertrand.env.kube.api.client import Kube
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
class ResolvedWorkloadSecret:
    """Resolved runtime Secret capability mounted into a workload.

    Parameters
    ----------
    capability_id : str
        Host-agnostic capability ID from project configuration.
    container_name : str
        Container that receives the Secret mount.
    secret_name : str
        Kubernetes Secret name backing the resolved capability.
    volume_name : str
        Deterministic pod volume name used for the Secret.
    mount_path : str
        Container path where the Secret is mounted.
    payload_path : str
        Path to the capability payload file inside the mounted Secret.
    """

    capability_id: str
    container_name: str
    secret_name: str
    volume_name: str
    mount_path: str
    payload_path: str


@dataclass(frozen=True)
class ResolvedWorkloadSSH:
    """Resolved runtime SSH credential capability mounted into a workload.

    Parameters
    ----------
    capability_id : str
        Host-agnostic capability ID from project configuration.
    container_name : str
        Container that receives the SSH credential mount.
    secret_name : str
        Kubernetes Secret name backing the resolved capability.
    volume_name : str
        Deterministic pod volume name used for the Secret.
    mount_path : str
        Container directory where the SSH credential Secret is mounted.
    private_key_path : str
        Path to the projected private key file inside the mounted Secret.
    """

    capability_id: str
    container_name: str
    secret_name: str
    volume_name: str
    mount_path: str
    private_key_path: str


@dataclass(frozen=True)
class WorkloadCapabilities:
    """Resolved capability additions for a native workload pod.

    Parameters
    ----------
    volumes : tuple[VolumeSpec, ...]
        Pod volumes required by resolved Secret capabilities.
    mounts_by_container : Mapping[str, tuple[Mapping[str, object], ...]]
        Secret mounts keyed by container name.
    secrets : tuple[ResolvedWorkloadSecret, ...]
        Resolved Secret capability records.
    ssh : tuple[ResolvedWorkloadSSH, ...]
        Resolved SSH credential capability records.
    resource_claims : tuple[DRAResourceClaimIntent, ...]
        DRA resource claim templates requested by workload containers.
    """

    volumes: tuple[VolumeSpec, ...]
    mounts_by_container: Mapping[str, tuple[Mapping[str, object], ...]]
    secrets: tuple[ResolvedWorkloadSecret, ...]
    ssh: tuple[ResolvedWorkloadSSH, ...]
    resource_claims: tuple[DRAResourceClaimIntent, ...]


@dataclass(frozen=True)
class _CapabilityResolutionContext:
    kube: Kube
    worktree_id: str
    repo_id: str
    claim_owner: str
    host_id: str | None
    node_name: str | None
    deadline: Deadline


@dataclass(frozen=True)
class _ResolvedMount:
    capability_id: str
    container_name: str
    secret_name: str
    volume: VolumeSpec
    mount: Mapping[str, object]
    payload_path: str


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
    context = _CapabilityResolutionContext(
        kube=kube,
        worktree_id=worktree_id,
        repo_id=repo_id,
        claim_owner=claim_owner,
        host_id=host_id,
        node_name=node_name,
        deadline=deadline,
    )
    volumes: dict[str, VolumeSpec] = {}
    mounts_by_container: dict[str, list[Mapping[str, object]]] = {
        _check_kube_name(container.name): [] for container in containers
    }
    resolved_secrets: list[ResolvedWorkloadSecret] = []
    resolved_ssh: list[ResolvedWorkloadSSH] = []
    resource_claims: list[DRAResourceClaimIntent] = []

    for container in containers:
        container_name = _check_kube_name(container.name)
        for request in container.secrets:
            resolved = await _resolve_secret_mount(
                context,
                kind="secret",
                request=request,
                container_name=container_name,
            )
            if resolved is None:
                continue
            _record_secret_mount(
                volumes,
                mounts_by_container,
                resolved_secrets,
                resolved=resolved,
            )

        for request in container.ssh:
            resolved = await _resolve_secret_mount(
                context,
                kind="ssh",
                request=request,
                container_name=container_name,
            )
            if resolved is None:
                continue
            _record_ssh_mount(
                volumes,
                mounts_by_container,
                resolved_ssh,
                resolved=resolved,
            )

        resource_claims.extend(
            await _resolve_device_claims(
                context,
                container_name=container_name,
                requests=container.devices,
            )
        )

    return _finalize_capabilities(
        volumes=volumes,
        mounts_by_container=mounts_by_container,
        resolved_secrets=resolved_secrets,
        resolved_ssh=resolved_ssh,
        resource_claims=resource_claims,
    )


async def _resolve_secret_mount(
    context: _CapabilityResolutionContext,
    *,
    kind: CapabilityKind,
    request: CapabilityRequest,
    container_name: str,
) -> _ResolvedMount | None:
    capability_id = _check_kube_name(str(request.id))
    secret = await resolve_capability_secret(
        context.kube,
        kind=kind,
        capability_id=capability_id,
        worktree_id=context.worktree_id,
        repo_id=context.repo_id,
        host_id=context.host_id,
        required=request.required,
        timeout=context.deadline.remaining(),
    )
    if secret is None:
        return None

    volume_name = _capability_volume_name(kind, capability_id)
    mount_path = _capability_mount_path(kind, capability_id)
    secret_name = secret.name
    return _ResolvedMount(
        capability_id=capability_id,
        container_name=container_name,
        secret_name=secret_name,
        volume=_capability_volume(
            kind,
            volume_name=volume_name,
            secret_name=secret_name,
        ),
        mount={"name": volume_name, "mountPath": mount_path, "readOnly": True},
        payload_path=_capability_payload_path(kind, mount_path),
    )


def _record_resolved_mount(
    volumes: dict[str, VolumeSpec],
    mounts_by_container: dict[str, list[Mapping[str, object]]],
    *,
    resolved: _ResolvedMount,
) -> None:
    volumes.setdefault(resolved.volume.name, resolved.volume)
    mounts_by_container[resolved.container_name].append(resolved.mount)


def _record_secret_mount(
    volumes: dict[str, VolumeSpec],
    mounts_by_container: dict[str, list[Mapping[str, object]]],
    resolved_secrets: list[ResolvedWorkloadSecret],
    *,
    resolved: _ResolvedMount,
) -> None:
    _record_resolved_mount(volumes, mounts_by_container, resolved=resolved)
    resolved_secrets.append(
        ResolvedWorkloadSecret(
            capability_id=resolved.capability_id,
            container_name=resolved.container_name,
            secret_name=resolved.secret_name,
            volume_name=resolved.volume.name,
            mount_path=str(resolved.mount["mountPath"]),
            payload_path=resolved.payload_path,
        )
    )


def _record_ssh_mount(
    volumes: dict[str, VolumeSpec],
    mounts_by_container: dict[str, list[Mapping[str, object]]],
    resolved_ssh: list[ResolvedWorkloadSSH],
    *,
    resolved: _ResolvedMount,
) -> None:
    _record_resolved_mount(volumes, mounts_by_container, resolved=resolved)
    resolved_ssh.append(
        ResolvedWorkloadSSH(
            capability_id=resolved.capability_id,
            container_name=resolved.container_name,
            secret_name=resolved.secret_name,
            volume_name=resolved.volume.name,
            mount_path=str(resolved.mount["mountPath"]),
            private_key_path=resolved.payload_path,
        )
    )


async def _resolve_device_claims(
    context: _CapabilityResolutionContext,
    *,
    container_name: str,
    requests: Sequence[CapabilityRequest],
) -> tuple[DRAResourceClaimIntent, ...]:
    device_requests = await select_device_claims(
        context.kube,
        requests={
            _check_kube_name(str(request.id)): request.required for request in requests
        },
        host_ids=(context.host_id,) if context.host_id is not None else None,
        node_names=(context.node_name,) if context.node_name is not None else None,
        timeout=context.deadline.remaining(),
    )
    return resource_claim_intents(
        owner=context.claim_owner,
        requests=device_requests,
        container_name=container_name,
    )


def _finalize_capabilities(
    *,
    volumes: dict[str, VolumeSpec],
    mounts_by_container: dict[str, list[Mapping[str, object]]],
    resolved_secrets: list[ResolvedWorkloadSecret],
    resolved_ssh: list[ResolvedWorkloadSSH],
    resource_claims: list[DRAResourceClaimIntent],
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
        secrets=tuple(resolved_secrets),
        ssh=tuple(resolved_ssh),
        resource_claims=tuple(resource_claims),
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


def _capability_payload_path(kind: CapabilityKind, mount_path: str) -> str:
    filename = SSH_PRIVATE_KEY_FILE if kind == "ssh" else CAPABILITY_VALUE_KEY
    return f"{mount_path}/{filename}"


def _capability_volume_name(kind: str, capability_id: str) -> str:
    payload = f"{kind}:{capability_id}".encode()
    digest = hashlib.sha256(payload).hexdigest()[:16]
    return f"workload-{kind}-{digest}"
