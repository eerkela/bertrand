"""Wrappers for the Kubernetes Service API and related operations."""

from __future__ import annotations

from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING, Literal, Self, cast

import kubernetes

from .api import (
    Kube,
    NamespacedKubeMetadata,
    ServicePortSpec,
)
from .api._helpers import (
    _create_or_patch,
    _list_namespaced_items,
    _typed_payload,
    _validate_delete_status,
    _wait_until_deleted,
)
from .api.view import ServicePortView

if TYPE_CHECKING:
    import builtins
    from collections.abc import Collection, Mapping

SERVICE_WAIT_INTERVAL = 0.5
type ServiceType = Literal["ClusterIP", "NodePort", "LoadBalancer", "ExternalName"]


@dataclass(frozen=True)
class Service(NamespacedKubeMetadata[kubernetes.client.V1Service]):
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

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        timeout: float,
        name: str,
    ) -> Self | None:
        """Read one Kubernetes Service by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the Service.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        name : str
            Service name to read.

        Returns
        -------
        Service | None
            Wrapped Kubernetes Service, or `None` if it does not exist.
        """
        payload = await kube.run(
            lambda request_timeout: kube.core.read_namespaced_service(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to read Service {name!r} in namespace {namespace!r}",
        )
        if payload is None:
            return None
        return cls(
            _obj=_typed_payload(payload, kubernetes.client.V1Service, context="Service")
        )

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        timeout: float,
        namespaces: Collection[str] | None = None,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[Self]:
        """List Kubernetes Services with optional namespace and label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        namespaces : Collection[str] | None, optional
            Optional namespace filters. `None` queries all namespaces. Otherwise,
            entries are trimmed, deduplicated, and queried individually.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.

        Returns
        -------
        list[Service]
            Wrapped Kubernetes Services matching the requested filters.
        """
        return [
            cls(_obj=item)
            for item in await _list_namespaced_items(
                kube,
                timeout=timeout,
                namespaces=namespaces,
                labels=labels,
                list_all=lambda label_selector, request_timeout: (
                    kube.core.list_service_for_all_namespaces(
                        label_selector=label_selector,
                        _request_timeout=request_timeout,
                    )
                ),
                list_namespace=lambda namespace, label_selector, request_timeout: (
                    kube.core.list_namespaced_service(
                        namespace=namespace,
                        label_selector=label_selector,
                        _request_timeout=request_timeout,
                    )
                ),
                list_type=kubernetes.client.V1ServiceList,
                item_type=kubernetes.client.V1Service,
                all_context="failed to list Services across all namespaces",
                namespace_context=lambda namespace: (
                    f"failed to list Services in namespace {namespace!r}"
                ),
                list_context="Service",
                item_context="Service",
            )
        ]

    @staticmethod
    def _manifest(
        *,
        namespace: str,
        name: str,
        selector: Mapping[str, str],
        ports: Collection[ServicePortSpec],
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
                "ports": [
                    {
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
                    for port in ports
                ],
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
        ports: Collection[ServicePortSpec],
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
        ports : Collection[ServicePortSpec]
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
        manifest = cls._manifest(
            namespace=namespace,
            name=name,
            selector=selector,
            ports=ports,
            labels=labels,
            annotations=annotations,
            service_type=service_type,
        )
        metadata = manifest.get("metadata")
        if not isinstance(metadata, dict):
            msg = "Service manifest must define metadata"
            raise OSError(msg)
        metadata = cast("dict[object, object]", metadata)
        namespace = str(metadata.get("namespace") or "").strip()
        name = str(metadata.get("name") or "").strip()
        if not namespace or not name:
            msg = "Service manifest must define metadata.namespace and metadata.name"
            raise OSError(msg)

        payload = await _create_or_patch(
            kube,
            timeout=timeout,
            create=lambda request_timeout: kube.core.create_namespaced_service(
                namespace=namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            patch=lambda request_timeout: kube.core.patch_namespaced_service(
                name=name,
                namespace=namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            create_context=f"failed to create Service {namespace}/{name}",
            patch_context=f"failed to patch Service {namespace}/{name}",
            expected=kubernetes.client.V1Service,
            payload_context="Service",
        )
        return cls(_obj=payload)

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

    def exposes(self, port: ServicePortSpec) -> bool:
        """Return whether this Service exposes a matching port.

        Parameters
        ----------
        port : ServicePortSpec
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
        ports: Collection[ServicePortSpec],
    ) -> bool:
        """Return whether this Service matches the expected shape.

        Parameters
        ----------
        service_type : ServiceType
            Expected Kubernetes Service type.
        selector : Mapping[str, str]
            Expected selector labels.
        ports : Collection[ServicePortSpec]
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

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this Service by its metadata namespace and name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Service | None
            Fresh wrapper for the same Service, or `None` if it no longer exists.
        """
        namespace, name = self._require_namespace_name("refresh Service")
        return await type(self).get(
            kube,
            namespace=namespace,
            timeout=timeout,
            name=name,
        )

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this Service from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        namespace, name = self._require_namespace_name("delete Service")
        payload = await kube.run(
            lambda request_timeout: kube.core.delete_namespaced_service(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete cluster Service {namespace}/{name}",
        )
        _validate_delete_status(
            payload, label=self._object_label(name=name, namespace=namespace)
        )

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this Service is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        """
        namespace, name = self._require_namespace_name("wait for Service deletion")
        await _wait_until_deleted(
            label=self._object_label(name=name, namespace=namespace),
            timeout=timeout,
            refresh=lambda remaining: self.refresh(kube, timeout=remaining),
        )
