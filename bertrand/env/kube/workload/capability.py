"""Runtime capability resolution for native Kubernetes workloads."""

from __future__ import annotations

import asyncio
import hashlib
from dataclasses import dataclass
from typing import TYPE_CHECKING, Protocol

from bertrand.env.config.core import _check_kube_name
from bertrand.env.kube.api.spec import VolumeMountSpec, VolumeSpec
from bertrand.env.kube.capability.base import Capability

if TYPE_CHECKING:
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
    container_path : str | None
        Optional future container-facing device path.
    permissions : str
        Requested future device permissions.
    """

    id: KubeName
    required: bool
    container_path: str | None
    permissions: str


@dataclass(frozen=True)
class ResolvedWorkloadSecret:
    """Resolved runtime Secret capability mounted into a workload.

    Parameters
    ----------
    capability_id : str
        Host-agnostic capability ID from project configuration.
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
    secret_name: str
    volume_name: str
    mount_path: str
    payload_path: str


@dataclass(frozen=True)
class ResolvedWorkloadDevice:
    """Resolved runtime device capability kept as workload intent.

    Parameters
    ----------
    capability_id : str
        Host-agnostic capability ID from project configuration.
    selector : str
        CDI selector from the resolved cluster capability.
    container_path : str | None
        Optional requested container-facing device path.
    permissions : str
        Requested device permissions.
    """

    capability_id: str
    selector: str
    container_path: str | None
    permissions: str


@dataclass(frozen=True)
class WorkloadCapabilities:
    """Resolved capability additions for a native workload pod.

    Parameters
    ----------
    volumes : tuple[VolumeSpec, ...]
        Pod volumes required by resolved Secret capabilities.
    primary_mounts : tuple[VolumeMountSpec, ...]
        Volume mounts to apply only to the primary container.
    secrets : tuple[ResolvedWorkloadSecret, ...]
        Resolved Secret capability records.
    devices : tuple[ResolvedWorkloadDevice, ...]
        Resolved device capability records. These are not rendered into Kubernetes
        Pod fields yet.
    """

    volumes: tuple[VolumeSpec, ...]
    primary_mounts: tuple[VolumeMountSpec, ...]
    secrets: tuple[ResolvedWorkloadSecret, ...]
    devices: tuple[ResolvedWorkloadDevice, ...]


async def resolve_workload_capabilities(
    kube: Kube,
    *,
    secrets: tuple[WorkloadSecretRequest, ...],
    devices: tuple[WorkloadDeviceRequest, ...],
    env_id: str,
    node: str | None = None,
    timeout: float,
) -> WorkloadCapabilities:
    """Resolve runtime Secret and device capabilities for one workload.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    secrets : tuple[WorkloadSecretRequest, ...]
        Runtime Secret capability requests.
    devices : tuple[WorkloadDeviceRequest, ...]
        Runtime device capability requests.
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
    volumes: list[VolumeSpec] = []
    mounts: list[VolumeMountSpec] = []
    resolved_secrets: list[ResolvedWorkloadSecret] = []
    resolved_devices: list[ResolvedWorkloadDevice] = []

    for request in secrets:
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
        volumes.append(
            VolumeSpec.secret(
                volume_name,
                secret_name=capability.ref.name,
                default_mode=0o400,
            )
        )
        mounts.append(
            VolumeMountSpec(
                name=volume_name,
                mount_path=mount_path,
                read_only=True,
            )
        )
        resolved_secrets.append(
            ResolvedWorkloadSecret(
                capability_id=capability_id,
                secret_name=capability.ref.name,
                volume_name=volume_name,
                mount_path=mount_path,
                payload_path=f"{mount_path}/{CAPABILITY_VALUE_KEY}",
            )
        )

    for request in devices:
        capability_id = _check_kube_name(str(request.id))
        capability = await Capability.resolve_device(
            kube,
            capability_id=capability_id,
            env_id=env_id,
            node=node,
            required=request.required,
            timeout=deadline - loop.time(),
        )
        if capability is None:
            continue
        resolved_devices.append(
            ResolvedWorkloadDevice(
                capability_id=capability_id,
                selector=capability.selector,
                container_path=(
                    str(request.container_path)
                    if request.container_path is not None
                    else None
                ),
                permissions=request.permissions,
            )
        )

    return WorkloadCapabilities(
        volumes=tuple(volumes),
        primary_mounts=tuple(mounts),
        secrets=tuple(resolved_secrets),
        devices=tuple(resolved_devices),
    )


def _capability_volume_name(kind: str, capability_id: str) -> str:
    payload = f"{kind}:{capability_id}".encode()
    digest = hashlib.sha256(payload).hexdigest()[:16]
    return f"workload-{kind}-{digest}"
