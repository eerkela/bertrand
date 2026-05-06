"""Wrappers for the Kubernetes Service API and related operations."""

from __future__ import annotations

import asyncio
import builtins
from collections.abc import Collection, Mapping
from dataclasses import dataclass
from types import MappingProxyType
from typing import Literal, Self, cast

import kubernetes

from .api import Kube, ServicePortSpec, _label_selector

SERVICE_WAIT_INTERVAL = 0.5
type ServiceType = Literal["ClusterIP", "NodePort", "LoadBalancer", "ExternalName"]


@dataclass(frozen=True)
class Service:
    """General-purpose wrapper around one Kubernetes Service object.

    Parameters
    ----------
    obj : kubernetes.client.V1Service
        Typed Kubernetes Service payload returned by the cluster API.

    Notes
    -----
    The public convergence API accepts intent-level fields and keeps raw Kubernetes
    manifests as an internal implementation detail.
    """

    obj: kubernetes.client.V1Service

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

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds `timeout`.
        OSError
            If Kubernetes returns malformed data or the API call fails.
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
        if not isinstance(payload, kubernetes.client.V1Service):
            raise OSError(
                f"malformed Kubernetes Service payload for {name!r} in namespace {namespace!r}"
            )
        return cls(obj=payload)

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

        Raises
        ------
        TimeoutError
            If any Kubernetes request exceeds `timeout`.
        OSError
            If Kubernetes returns malformed data or a list call fails.
        """
        label_selector = _label_selector(labels)
        payloads: builtins.list[kubernetes.client.V1ServiceList] = []

        if namespaces is None:
            payload = await kube.run(
                lambda request_timeout: kube.core.list_service_for_all_namespaces(
                    label_selector=label_selector,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context="failed to list Services across all namespaces",
            )
            if payload is not None:
                payloads.append(payload)
        else:
            normalized = {namespace.strip() for namespace in namespaces}
            normalized.discard("")
            if not normalized:
                return []
            for namespace in sorted(normalized):
                payload = await kube.run(
                    lambda request_timeout, namespace=namespace: kube.core.list_namespaced_service(
                        namespace=namespace,
                        label_selector=label_selector,
                        _request_timeout=request_timeout,
                    ),
                    timeout=timeout,
                    context=f"failed to list Services in namespace {namespace!r}",
                )
                if payload is not None:
                    payloads.append(payload)

        out: builtins.list[Self] = []
        for payload in payloads:
            if not isinstance(payload, kubernetes.client.V1ServiceList):
                raise OSError("malformed Kubernetes Service list payload")
            for item in payload.items or []:
                if not isinstance(item, kubernetes.client.V1Service):
                    raise OSError("malformed Kubernetes Service entry in list payload")
                out.append(cls(obj=item))
        return out

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
                "ports": [port.manifest() for port in ports],
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
        service_type : {"ClusterIP", "NodePort", "LoadBalancer", "ExternalName"}, optional
            Kubernetes Service type.

        Returns
        -------
        Service
            Wrapped created or patched Service.

        Raises
        ------
        TimeoutError
            If any Kubernetes request exceeds `timeout`.
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
            raise OSError("Service manifest must define metadata")
        metadata = cast("dict[object, object]", metadata)
        namespace = str(metadata.get("namespace") or "").strip()
        name = str(metadata.get("name") or "").strip()
        if not namespace or not name:
            raise OSError("Service manifest must define metadata.namespace and metadata.name")

        # try to create the Service if it is missing
        try:
            created = await kube.run(
                lambda request_timeout: kube.core.create_namespaced_service(
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=f"failed to create Service {namespace}/{name}",
            )
            if not isinstance(created, kubernetes.client.V1Service):
                raise OSError(f"malformed Kubernetes Service payload while creating {name!r}")
            return cls(obj=created)
        except OSError as err:
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise

        # patch the Service if it already exists
        patched = await kube.run(
            lambda request_timeout: kube.core.patch_namespaced_service(
                name=name,
                namespace=namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to patch Service {namespace}/{name}",
        )
        if not isinstance(patched, kubernetes.client.V1Service):
            raise OSError(f"malformed Kubernetes Service payload while patching {name!r}")
        return cls(obj=patched)

    @property
    def name(self) -> str:
        """
        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def namespace(self) -> str:
        """
        Returns
        -------
        str
            Trimmed `metadata.namespace`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.namespace or "").strip() if metadata is not None else ""

    @property
    def labels(self) -> Mapping[str, str]:
        """
        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.labels`, or an empty mapping when unavailable.
        """
        metadata = self.obj.metadata
        if metadata is None or metadata.labels is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.labels)

    @property
    def annotations(self) -> Mapping[str, str]:
        """
        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.annotations`, or an empty mapping when
            unavailable.
        """
        metadata = self.obj.metadata
        if metadata is None or metadata.annotations is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.annotations)

    @property
    def selector(self) -> Mapping[str, str]:
        """
        Returns
        -------
        Mapping[str, str]
            Read-only view of `spec.selector`, or an empty mapping when unavailable.
        """
        spec = self.obj.spec
        if spec is None or spec.selector is None:
            return MappingProxyType({})
        return MappingProxyType(spec.selector)

    @property
    def type(self) -> str:
        """
        Returns
        -------
        str
            Trimmed Service type, or an empty string when unavailable.
        """
        spec = self.obj.spec
        return (spec.type or "").strip() if spec is not None else ""

    @property
    def ports(self) -> tuple[kubernetes.client.V1ServicePort, ...]:
        """
        Returns
        -------
        tuple[kubernetes.client.V1ServicePort, ...]
            Immutable snapshot of Service ports.
        """
        spec = self.obj.spec
        return tuple(spec.ports or ()) if spec is not None else ()

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this Service by identity.

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

        Raises
        ------
        OSError
            If this wrapper does not contain enough metadata to identify the
            Service, or if Kubernetes returns malformed data.
        TimeoutError
            If the Kubernetes request exceeds `timeout`.
        """
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            raise OSError("cannot refresh Service with missing metadata.name/namespace")
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

        Raises
        ------
        OSError
            If this wrapper does not contain enough metadata to identify the
            Service, if the delete request fails, or if Kubernetes returns
            malformed data.
        TimeoutError
            If the Kubernetes request exceeds `timeout`.
        """
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            raise OSError("cannot delete Service with missing metadata.name/namespace")
        payload = await kube.run(
            lambda request_timeout: kube.core.delete_namespaced_service(
                name=name,
                namespace=namespace,
                body=kubernetes.client.V1DeleteOptions(),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete Service {namespace}/{name}",
        )
        if payload is not None and not isinstance(payload, kubernetes.client.V1Status):
            raise OSError(
                f"malformed Kubernetes response while deleting Service {namespace}/{name}"
            )

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this Service is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        Raises
        ------
        OSError
            If this wrapper does not contain enough metadata to identify the
            Service, or if a refresh request returns malformed data.
        TimeoutError
            If the Service still exists when `timeout` expires.
        """
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            raise OSError("cannot wait for Service deletion with missing metadata.name/namespace")
        if timeout <= 0:
            raise TimeoutError(f"timed out waiting for Service {namespace}/{name} deletion")
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                raise TimeoutError(f"timed out waiting for Service {namespace}/{name} deletion")
            live = await self.refresh(kube, timeout=remaining)
            if live is None:
                return
            await asyncio.sleep(min(SERVICE_WAIT_INTERVAL, remaining))
