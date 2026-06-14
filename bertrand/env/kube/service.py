"""Wrappers for the Kubernetes Service API and related operations."""

from __future__ import annotations

from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING, Literal

import kubernetes

from .api.resource import (
    KubeResource,
    namespaced_resource,
)

if TYPE_CHECKING:
    from collections.abc import Collection, Mapping


type ServiceType = Literal["ClusterIP", "NodePort", "LoadBalancer", "ExternalName"]


@dataclass(frozen=True)
class ServicePortView:
    """Read-only Kubernetes Service port view.

    Parameters
    ----------
    name : str
        Service port name.
    port : int
        Service port number.
    target_port : int | str
        Target container port number or name.
    protocol : str
        Service port protocol.
    node_port : int | None
        Allocated or requested NodePort value, when present.
    """

    name: str
    port: int
    target_port: int | str
    protocol: str
    node_port: int | None = None


@dataclass(frozen=True)
class ServiceManifest:
    """Desired state for one Kubernetes Service."""

    namespace: str
    name: str
    selector: Mapping[str, str]
    ports: Collection[ServicePortView]
    labels: Mapping[str, str] | None = None
    annotations: Mapping[str, str] | None = None
    service_type: ServiceType = "ClusterIP"

    def manifest(self) -> Mapping[str, object]:
        """Render this desired state as a Kubernetes manifest.

        Returns
        -------
        Mapping[str, object]
            Kubernetes Service manifest payload.
        """
        return {
            "apiVersion": "v1",
            "kind": "Service",
            "metadata": {
                "name": self.name,
                "namespace": self.namespace,
                "labels": dict(self.labels or {}),
                "annotations": dict(self.annotations or {}),
            },
            "spec": {
                "type": self.service_type,
                "selector": dict(self.selector),
                "ports": [_service_port_manifest(port) for port in self.ports],
            },
        }


@namespaced_resource(
    api=kubernetes.client.CoreV1Api,
    read=kubernetes.client.CoreV1Api.read_namespaced_service,
    list=kubernetes.client.CoreV1Api.list_namespaced_service,
    list_all=kubernetes.client.CoreV1Api.list_service_for_all_namespaces,
    create=kubernetes.client.CoreV1Api.create_namespaced_service,
    patch=kubernetes.client.CoreV1Api.patch_namespaced_service,
    delete=kubernetes.client.CoreV1Api.delete_namespaced_service,
)
@dataclass(frozen=True)
class Service(
    KubeResource[kubernetes.client.V1Service, ServiceManifest],
):
    """General-purpose wrapper around one Kubernetes Service object.

    Parameters
    ----------
    _obj : kubernetes.client.V1Service
        Typed Kubernetes Service payload returned by the cluster API.

    Notes
    -----
    The public convergence API accepts intent-level fields and keeps raw Kubernetes
    manifests as an internal implementation detail.
    """

    _obj: kubernetes.client.V1Service

    @property
    def selector(self) -> Mapping[str, str]:
        """Return the Service selector.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `spec.selector`, or an empty mapping when unavailable.
        """
        spec = self._obj.spec
        if spec is None or spec.selector is None:
            return MappingProxyType({})
        return MappingProxyType(spec.selector)

    @property
    def type(self) -> str:
        """Return the Service type.

        Returns
        -------
        str
            Trimmed Service type, or an empty string when unavailable.
        """
        spec = self._obj.spec
        return (spec.type or "").strip() if spec is not None else ""

    @property
    def ports(self) -> tuple[ServicePortView, ...]:
        """Return the Service ports.

        Returns
        -------
        tuple[ServicePortView, ...]
            Immutable snapshot of Service port views.
        """
        spec = self._obj.spec
        if spec is None:
            return ()
        return tuple(
            ServicePortView(
                name=(port.name or "").strip(),
                port=int(port.port or 0),
                target_port=port.target_port
                if port.target_port is not None
                else int(port.port or 0),
                protocol=(port.protocol or "TCP").strip(),
                node_port=port.node_port,
            )
            for port in spec.ports or ()
        )


def _service_port_manifest(port: ServicePortView) -> dict[str, object]:
    return {
        key: value
        for key, value in {
            "name": port.name,
            "port": port.port,
            "targetPort": port.target_port,
            "protocol": port.protocol,
            "nodePort": port.node_port,
        }.items()
        if value is not None
    }
