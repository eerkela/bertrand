"""Wrappers for the Kubernetes Service API and related operations."""

from __future__ import annotations

from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING, ClassVar, Literal, Self

import kubernetes

from .api.metadata import NamespacedKubeMetadata
from .api.resource import BuiltinResource, BuiltinResourceObject
from .api.view import ServicePortView

if TYPE_CHECKING:
    from collections.abc import Collection, Mapping

    from .api.client import Kube

type ServiceType = Literal["ClusterIP", "NodePort", "LoadBalancer", "ExternalName"]


@dataclass(frozen=True)
class Service(
    BuiltinResourceObject[kubernetes.client.V1Service],
    NamespacedKubeMetadata[kubernetes.client.V1Service],
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

    resource: ClassVar[BuiltinResource[kubernetes.client.V1Service]] = (
        BuiltinResource.namespaced(
            api="core",
            kind="Service",
            slug="service",
            expected=kubernetes.client.V1Service,
            list_type=kubernetes.client.V1ServiceList,
            create=True,
            patch=True,
            delete=True,
        )
    )

    @staticmethod
    def _manifest(
        *,
        namespace: str,
        name: str,
        selector: Mapping[str, str],
        ports: Collection[ServicePortView],
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
        service_type: ServiceType,
    ) -> dict[str, object]:
        return {
            "apiVersion": "v1",
            "kind": "Service",
            "metadata": {
                "name": name,
                "namespace": namespace,
                "labels": dict(labels or {}),
                "annotations": dict(annotations or {}),
            },
            "spec": {
                "type": service_type,
                "selector": dict(selector),
                "ports": [_service_port_manifest(port) for port in ports],
            },
        }

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        selector: Mapping[str, str],
        ports: Collection[ServicePortView],
        timeout: float,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
        service_type: ServiceType = "ClusterIP",
    ) -> Self:
        """Create or patch one Kubernetes Service from intent-level fields.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the Service.
        name : str
            Service name to create or patch.
        selector : Mapping[str, str]
            Pod label selector for the Service.
        ports : Collection[ServicePortView]
            Ports exposed by the Service.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Labels to apply to `metadata.labels`.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to `metadata.annotations`.
        service_type : {"ClusterIP", "NodePort", "LoadBalancer", ...
                "ExternalName"}, optional
            Kubernetes Service type.

        Returns
        -------
        Service
            Wrapped created or patched Service.

        Raises
        ------
        OSError
            If Kubernetes create/patch fails or returns malformed data.
        """
        namespace = namespace.strip()
        name = name.strip()
        if not namespace or not name:
            msg = "Service upsert requires non-empty namespace and name"
            raise OSError(msg)

        manifest = cls._manifest(
            namespace=namespace,
            name=name,
            selector=selector,
            ports=ports,
            labels=labels,
            annotations=annotations,
            service_type=service_type,
        )
        return await cls.resource.upsert(
            kube,
            owner=cls,
            namespace=namespace,
            name=name,
            manifest=manifest,
            timeout=timeout,
        )

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

    def selects(self, selector: Mapping[str, str]) -> bool:
        """Return whether this Service has exactly the expected selector.

        Parameters
        ----------
        selector : Mapping[str, str]
            Expected selector labels.

        Returns
        -------
        bool
            Whether the Service selector exactly matches `selector`.
        """
        return dict(self.selector) == dict(selector)

    def exposes(self, port: ServicePortView) -> bool:
        """Return whether this Service exposes a matching port.

        Parameters
        ----------
        port : ServicePortView
            Expected Service port declaration.

        Returns
        -------
        bool
            Whether any current Service port matches `port`.
        """
        return any(
            actual.name == port.name
            and actual.port == port.port
            and actual.target_port == port.target_port
            and actual.protocol == port.protocol
            and actual.node_port == port.node_port
            for actual in self.ports
        )

    def matches(
        self,
        *,
        service_type: ServiceType,
        selector: Mapping[str, str],
        ports: Collection[ServicePortView],
    ) -> bool:
        """Return whether this Service matches the expected shape.

        Parameters
        ----------
        service_type : ServiceType
            Expected Kubernetes Service type.
        selector : Mapping[str, str]
            Expected selector labels.
        ports : Collection[ServicePortView]
            Expected Service ports.

        Returns
        -------
        bool
            Whether the Service type, selector, and ports all match.
        """
        return (
            self.type == service_type
            and self.selects(selector)
            and all(self.exposes(port) for port in ports)
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
