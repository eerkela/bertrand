"""Runtime capability resolution for native Kubernetes workloads."""

from __future__ import annotations

import asyncio
import hashlib
from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING, Protocol

from bertrand.env.config.core import _check_kube_name
from bertrand.env.kube.api.spec import VolumeMountSpec, VolumeSpec
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
CAPABILITY_VALUE_KEY = "value"


class WorkloadSecretRequest(Protocol):
    """Structural runtime Secret capability request.

    Attributes
    ----------
    id : KubeName
        Host-agnostic Secret capability ID.
    required : bool
        Whether resolution must fail if the capability is unavailable.
    """

    id: KubeName
    required: bool


class WorkloadDeviceRequest(Protocol):
    """Structural runtime device capability request.

    Attributes
    ----------
    id : KubeName
        Host-agnostic device capability ID.
    required : bool
        Whether resolution must fail if the capability is unavailable.
    """

    id: KubeName
    required: bool


class WorkloadContainerCapabilityRequest(Protocol):
    """Structural runtime capability request for one workload container.

    Attributes
    ----------
    name : str
        Container name that owns these runtime capability requests.
    secrets : Sequence[WorkloadSecretRequest]
        Secret capability requests mounted into this container.
    devices : Sequence[WorkloadDeviceRequest]
        Device capability requests resolved for this container.
    """

    name: str
    secrets: Sequence[WorkloadSecretRequest]
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
    resource_claims : tuple[DRAResourceClaimIntent, ...]
        DRA resource claim templates requested by workload containers.
    """

    volumes: tuple[VolumeSpec, ...]
    mounts_by_container: Mapping[str, tuple[VolumeMountSpec, ...]]
    secrets: tuple[ResolvedWorkloadSecret, ...]
    resource_claims: tuple[DRAResourceClaimIntent, ...]


async def resolve_workload_capabilities(
    kube: Kube,
    *,
    containers: tuple[WorkloadContainerCapabilityRequest, ...],
    env_id: str,
    node: str | None = None,
    timeout: float,
) -> WorkloadCapabilities:
    """Resolve runtime Secret and device capabilities for one workload.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    containers : tuple[WorkloadContainerCapabilityRequest, ...]
        Runtime capability requests grouped by container.
    env_id : str
        Environment UUID used for the first capability lookup tier.
    node : str | None, optional
        Kubernetes node name used for the second capability lookup tier.
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
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    volumes: dict[str, VolumeSpec] = {}
    mounts_by_container: dict[str, list[VolumeMountSpec]] = {
        _check_kube_name(container.name): [] for container in containers
    }
    resolved_secrets: list[ResolvedWorkloadSecret] = []
    resource_claims: list[DRAResourceClaimIntent] = []

    for container in containers:
        container_name = _check_kube_name(container.name)
        for request in container.secrets:
            capability_id = _check_kube_name(str(request.id))
            capability = await Capability.resolve(
                kube,
                kind="secret",
                capability_id=capability_id,
                env_id=env_id,
                node=node,
                required=request.required,
                timeout=deadline - loop.time(),
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

        device_requests = await select_device_claims(
            kube,
            requests={
                _check_kube_name(str(request.id)): request.required
                for request in container.devices
            },
            node_names=(node,) if node is not None else None,
            timeout=deadline - loop.time(),
        )
        resource_claims.extend(
            resource_claim_intents(
                owner=f"workload-{env_id}",
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
        resource_claims=tuple(resource_claims),
    )


def _capability_volume_name(kind: str, capability_id: str) -> str:
    payload = f"{kind}:{capability_id}".encode()
    digest = hashlib.sha256(payload).hexdigest()[:16]
    return f"workload-{kind}-{digest}"
