"""Runtime capability resolution for native Kubernetes workloads."""

from __future__ import annotations

import hashlib
from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING, Protocol

from bertrand.env.config.core import _check_kube_name
from bertrand.env.git import Deadline
from bertrand.env.kube.api.spec import (
    SecretVolumeItemSpec,
    VolumeMountSpec,
    VolumeSpec,
)
from bertrand.env.kube.capability.base import Capability
from bertrand.env.kube.capability.device import (
    DRAResourceClaimIntent,
    resource_claim_intents,
    select_device_claims,
)

if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence

    from bertrand.env.config.core import KubeName
    from bertrand.env.kube.api.client import Kube

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


class WorkloadSecretRequest(CapabilityRequest, Protocol):
    """Structural runtime Secret capability request."""


class WorkloadSSHRequest(CapabilityRequest, Protocol):
    """Structural runtime SSH credential capability request.

    Attributes
    ----------
    id : KubeName
        Host-agnostic SSH credential capability ID.
    required : bool
        Whether resolution must fail if the capability is unavailable.
    """


class WorkloadDeviceRequest(CapabilityRequest, Protocol):
    """Structural runtime device capability request.

    Attributes
    ----------
    id : KubeName
        Host-agnostic device capability ID.
    required : bool
        Whether resolution must fail if the capability is unavailable.
    """


class WorkloadContainerCapabilityRequest(Protocol):
    """Structural runtime capability request for one workload container.

    Attributes
    ----------
    name : str
        Container name that owns these runtime capability requests.
    secrets : Sequence[WorkloadSecretRequest]
        Secret capability requests mounted into this container.
    ssh : Sequence[WorkloadSSHRequest]
        SSH credential capability requests mounted into this container.
    devices : Sequence[WorkloadDeviceRequest]
        Device capability requests resolved for this container.
    """

    name: str
    secrets: Sequence[WorkloadSecretRequest]
    ssh: Sequence[WorkloadSSHRequest]
    devices: Sequence[WorkloadDeviceRequest]


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
    mounts_by_container : Mapping[str, tuple[VolumeMountSpec, ...]]
        Secret mounts keyed by container name.
    secrets : tuple[ResolvedWorkloadSecret, ...]
        Resolved Secret capability records.
    ssh : tuple[ResolvedWorkloadSSH, ...]
        Resolved SSH credential capability records.
    resource_claims : tuple[DRAResourceClaimIntent, ...]
        DRA resource claim templates requested by workload containers.
    """

    volumes: tuple[VolumeSpec, ...]
    mounts_by_container: Mapping[str, tuple[VolumeMountSpec, ...]]
    secrets: tuple[ResolvedWorkloadSecret, ...]
    ssh: tuple[ResolvedWorkloadSSH, ...]
    resource_claims: tuple[DRAResourceClaimIntent, ...]


async def resolve_workload_capabilities(
    kube: Kube,
    *,
    containers: tuple[WorkloadContainerCapabilityRequest, ...],
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
    containers : tuple[WorkloadContainerCapabilityRequest, ...]
        Runtime capability requests grouped by container.
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
    mounts_by_container: dict[str, list[VolumeMountSpec]] = {
        _check_kube_name(container.name): [] for container in containers
    }
    resolved_secrets: list[ResolvedWorkloadSecret] = []
    resolved_ssh: list[ResolvedWorkloadSSH] = []
    resource_claims: list[DRAResourceClaimIntent] = []

    for container in containers:
        container_name = _check_kube_name(container.name)
        for request in container.secrets:
            capability_id = _check_kube_name(str(request.id))
            capability = await Capability.resolve(
                kube,
                kind="secret",
                capability_id=capability_id,
                worktree_id=worktree_id,
                repo_id=repo_id,
                host_id=host_id,
                required=request.required,
                timeout=deadline.remaining(),
            )
            if capability is None:
                continue
            volume_name = _capability_volume_name("secret", capability_id)
            mount_path = f"{WORKLOAD_SECRET_MOUNT}/{capability_id}"
            volumes.setdefault(
                volume_name,
                VolumeSpec.secret(
                    volume_name,
                    secret_name=capability.ref.name,
                    default_mode=0o400,
                ),
            )
            mounts_by_container[container_name].append(
                VolumeMountSpec(
                    name=volume_name,
                    mount_path=mount_path,
                    read_only=True,
                )
            )
            resolved_secrets.append(
                ResolvedWorkloadSecret(
                    capability_id=capability_id,
                    container_name=container_name,
                    secret_name=capability.ref.name,
                    volume_name=volume_name,
                    mount_path=mount_path,
                    payload_path=f"{mount_path}/{CAPABILITY_VALUE_KEY}",
                )
            )

        for request in container.ssh:
            capability_id = _check_kube_name(str(request.id))
            capability = await Capability.resolve(
                kube,
                kind="ssh",
                capability_id=capability_id,
                worktree_id=worktree_id,
                repo_id=repo_id,
                host_id=host_id,
                required=request.required,
                timeout=deadline.remaining(),
            )
            if capability is None:
                continue
            volume_name = _capability_volume_name("ssh", capability_id)
            mount_path = f"{WORKLOAD_SSH_MOUNT}/{capability_id}"
            volumes.setdefault(
                volume_name,
                VolumeSpec.secret(
                    volume_name,
                    secret_name=capability.ref.name,
                    default_mode=0o400,
                    items=(
                        SecretVolumeItemSpec(
                            key=CAPABILITY_VALUE_KEY,
                            path=SSH_PRIVATE_KEY_FILE,
                        ),
                    ),
                ),
            )
            mounts_by_container[container_name].append(
                VolumeMountSpec(
                    name=volume_name,
                    mount_path=mount_path,
                    read_only=True,
                )
            )
            resolved_ssh.append(
                ResolvedWorkloadSSH(
                    capability_id=capability_id,
                    container_name=container_name,
                    secret_name=capability.ref.name,
                    volume_name=volume_name,
                    mount_path=mount_path,
                    private_key_path=f"{mount_path}/{SSH_PRIVATE_KEY_FILE}",
                )
            )

        device_requests = await select_device_claims(
            kube,
            requests={
                _check_kube_name(str(request.id)): request.required
                for request in container.devices
            },
            host_ids=(host_id,) if host_id is not None else None,
            node_names=(node_name,) if node_name is not None else None,
            timeout=deadline.remaining(),
        )
        resource_claims.extend(
            resource_claim_intents(
                owner=claim_owner,
                requests=device_requests,
                container_name=container_name,
            )
        )

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


def _capability_volume_name(kind: str, capability_id: str) -> str:
    payload = f"{kind}:{capability_id}".encode()
    digest = hashlib.sha256(payload).hexdigest()[:16]
    return f"workload-{kind}-{digest}"
