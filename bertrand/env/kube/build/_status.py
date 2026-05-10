"""Private status extraction helpers for the build runtime."""

from __future__ import annotations

from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from collections.abc import Collection, Mapping

    from bertrand.env.kube.api import ServicePortSpec
    from bertrand.env.kube.deployment import Deployment
    from bertrand.env.kube.service import Service, ServiceType
    from bertrand.env.kube.volume import PersistentVolumeClaim


@dataclass(frozen=True)
class _ServiceStatus:
    present: bool
    selector_ready: bool
    port_ready: bool
    ready: bool


@dataclass(frozen=True)
class _DeploymentStatus:
    present: bool
    available_replicas: int
    updated_replicas: int
    observed_generation: int
    generation: int
    rollout_ready: bool
    pod_annotations: Mapping[str, str]


@dataclass(frozen=True)
class _PersistentVolumeClaimStatus:
    present: bool
    managed: bool
    bound: bool
    phase: str
    storage_class: str
    access_modes: tuple[str, ...]
    storage_request: str
    ready: bool


def _service_status(
    service: Service | None,
    *,
    service_type: ServiceType,
    selector: Mapping[str, str],
    ports: Collection[ServicePortSpec],
) -> _ServiceStatus:
    if service is None:
        return _ServiceStatus(
            present=False,
            selector_ready=False,
            port_ready=False,
            ready=False,
        )
    selector_ready = service.selects(selector)
    port_ready = all(service.exposes(port) for port in ports)
    return _ServiceStatus(
        present=True,
        selector_ready=selector_ready,
        port_ready=port_ready,
        ready=service.matches(
            service_type=service_type,
            selector=selector,
            ports=ports,
        ),
    )


def _deployment_status(
    deployment: Deployment | None,
    *,
    minimum: int = 1,
) -> _DeploymentStatus:
    if deployment is None:
        return _DeploymentStatus(
            present=False,
            available_replicas=0,
            updated_replicas=0,
            observed_generation=0,
            generation=0,
            rollout_ready=False,
            pod_annotations=MappingProxyType({}),
        )
    return _DeploymentStatus(
        present=True,
        available_replicas=deployment.available_replicas,
        updated_replicas=deployment.updated_replicas,
        observed_generation=deployment.observed_generation,
        generation=deployment.generation,
        rollout_ready=deployment.rollout_ready(minimum=minimum),
        pod_annotations=deployment.pod_annotations,
    )


def _pvc_status(
    pvc: PersistentVolumeClaim | None,
    *,
    required_labels: Mapping[str, str],
    access_mode: str,
) -> _PersistentVolumeClaimStatus:
    if pvc is None:
        return _PersistentVolumeClaimStatus(
            present=False,
            managed=False,
            bound=False,
            phase="",
            storage_class="",
            access_modes=(),
            storage_request="",
            ready=False,
        )
    managed = all(
        pvc.labels.get(key) == value for key, value in required_labels.items()
    )
    storage_class = pvc.storage_class_name
    ready = (
        managed
        and pvc.is_bound
        and bool(storage_class)
        and pvc.has_access_mode(access_mode)
    )
    return _PersistentVolumeClaimStatus(
        present=True,
        managed=managed,
        bound=pvc.is_bound,
        phase=pvc.phase,
        storage_class=storage_class,
        access_modes=pvc.access_modes,
        storage_request=pvc.requested_storage,
        ready=ready,
    )
