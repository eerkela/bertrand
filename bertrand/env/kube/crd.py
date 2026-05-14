"""Wrappers for Kubernetes CustomResourceDefinition objects."""

from __future__ import annotations

import asyncio
from collections.abc import AsyncIterator, Collection, Mapping
from dataclasses import dataclass, field
from types import MappingProxyType
from typing import TYPE_CHECKING, Any, Self, cast

from kubernetes import client as kube_client
from pydantic import BaseModel, ConfigDict, Field

from bertrand.env.git import until

from .api._helpers import (
    _create_or_patch,
    _label_selector,
    _list_cluster_items,
    _typed_payload,
    _validate_delete_status,
    _wait_until_deleted,
)
from .api.metadata import KubeMetadata
from .api.watch import WatchEvent, WatchExpired
from .api.watch import watch as kube_watch

CRD_WAIT_POLL_INTERVAL_SECONDS = 0.5

if TYPE_CHECKING:
    import builtins

    from .api.client import Kube
    from .api.spec import CustomResourceSpec


@dataclass(frozen=True)
class _CustomResourceSnapshot:
    items: list[NamespacedCustomObject]
    resource_version: str


class CustomObjectMetadata(BaseModel):
    """Validated subset of Kubernetes custom-object metadata.

    Parameters
    ----------
    name : str
        Kubernetes object name.
    namespace : str
        Namespace that owns the object.
    generation : int
        Kubernetes metadata generation.
    resource_version : str
        Kubernetes resource version, parsed from ``resourceVersion``.
    labels : dict[str, str]
        Kubernetes metadata labels.
    """

    model_config = ConfigDict(extra="ignore", frozen=True)

    name: str = ""
    namespace: str = ""
    generation: int = 0
    resource_version: str = Field(default="", alias="resourceVersion")
    labels: dict[str, str] = Field(default_factory=dict)


@dataclass(frozen=True)
class CustomResourceClient:
    """Bound helper for namespaced custom-resource API operations.

    Parameters
    ----------
    spec : CustomResourceSpec
        Resource type handled by this client.
    """

    spec: CustomResourceSpec

    def _body(
        self,
        *,
        namespace: str,
        name: str,
        spec: Mapping[str, object],
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
    ) -> dict[str, object]:
        merged_labels = dict(self.spec.labels)
        merged_labels.update(labels or {})
        return {
            "apiVersion": self.spec.api_version,
            "kind": self.spec.kind,
            "metadata": {
                "name": name,
                "namespace": namespace,
                "labels": merged_labels,
                "annotations": dict(annotations or {}),
            },
            "spec": dict(spec),
        }

    async def get(
        self,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        timeout: float,
    ) -> NamespacedCustomObject | None:
        """Read one namespaced custom object by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the custom object.
        name : str
            Custom object name to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        NamespacedCustomObject | None
            Wrapped custom object, or `None` if it does not exist.
        """
        payload = await kube.run(
            lambda request_timeout: kube.custom.get_namespaced_custom_object(
                group=self.spec.group,
                version=self.spec.version,
                namespace=namespace,
                plural=self.spec.plural,
                name=name,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=(
                f"failed to read custom object {self.spec.plural}/{name} "
                f"in namespace {namespace!r}"
            ),
        )
        if payload is None:
            return None
        return NamespacedCustomObject._from_payload(
            group=self.spec.group,
            version=self.spec.version,
            plural=self.spec.plural,
            payload=payload,
        )

    async def list(
        self,
        kube: Kube,
        *,
        namespace: str,
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[NamespacedCustomObject]:
        """List namespaced custom objects with optional label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace to query.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.

        Returns
        -------
        list[NamespacedCustomObject]
            Wrapped custom objects matching the requested filters.
        """
        snapshot = await self._snapshot(
            kube,
            namespace=namespace,
            labels=labels,
            timeout=timeout,
        )
        return snapshot.items

    async def watch(
        self,
        kube: Kube,
        *,
        namespace: str,
        timeout: float,
        labels: Mapping[str, str] | None = None,
        resource_version: str | None = None,
        emit_initial: bool = False,
    ) -> AsyncIterator[WatchEvent[NamespacedCustomObject]]:
        """Watch namespaced custom objects with relist-on-expiry recovery.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace to watch.
        timeout : float
            Maximum watch budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.
        resource_version : str | None, optional
            Resource version to resume from. If omitted, the client lists once and
            watches from the list resource version.
        emit_initial : bool, optional
            Whether to emit listed objects as synthetic `"ADDED"` events before
            live watch events. Only applies when `resource_version` is omitted.

        Yields
        ------
        WatchEvent[NamespacedCustomObject]
            Typed custom-object watch events.

        Raises
        ------
        TimeoutError
            If the watch exceeds the timeout budget.
        """
        if timeout <= 0:
            msg = "custom resource watch timeout must be non-negative"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        current_version = resource_version.strip() if resource_version else ""
        can_emit_initial = emit_initial and not current_version
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                return
            if not current_version:
                snapshot = await self._snapshot(
                    kube,
                    namespace=namespace,
                    labels=labels,
                    timeout=remaining,
                )
                current_version = snapshot.resource_version
                if can_emit_initial:
                    for item in snapshot.items:
                        yield WatchEvent(
                            type="ADDED",
                            object=item,
                            resource_version=item.resource_version or current_version,
                            raw_type="ADDED",
                        )
                    can_emit_initial = False

            remaining = deadline - loop.time()
            if remaining <= 0:
                return
            try:
                async for event in kube_watch(
                    kube.custom.list_namespaced_custom_object,
                    wrapper=lambda payload: NamespacedCustomObject._from_payload(
                        group=self.spec.group,
                        version=self.spec.version,
                        plural=self.spec.plural,
                        payload=payload,
                    ),
                    timeout=remaining,
                    context=(
                        f"custom object {self.spec.plural} watch in namespace "
                        f"{namespace!r}"
                    ),
                    resource_version=current_version,
                    labels=labels,
                    api_kwargs={
                        "group": self.spec.group,
                        "version": self.spec.version,
                        "namespace": namespace,
                        "plural": self.spec.plural,
                    },
                ):
                    if event.resource_version:
                        current_version = event.resource_version
                    yield event
            except WatchExpired:
                current_version = ""
            else:
                return

    async def create(
        self,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        spec: Mapping[str, object],
        timeout: float,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> NamespacedCustomObject:
        """Create one namespaced custom object from intent-level fields.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the custom object.
        name : str
            Custom object name to create.
        spec : Mapping[str, object]
            Desired custom object `spec` payload.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Labels to merge over the resource defaults.
        annotations : Mapping[str, str] | None, optional
            Metadata annotations to apply.

        Returns
        -------
        NamespacedCustomObject
            Wrapped created custom object.
        """
        return await self._create(
            kube,
            namespace=namespace,
            body=self._body(
                namespace=namespace,
                name=name,
                spec=spec,
                labels=labels,
                annotations=annotations,
            ),
            timeout=timeout,
        )

    async def upsert(
        self,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        spec: Mapping[str, object],
        timeout: float,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> NamespacedCustomObject:
        """Create or patch one namespaced custom object from intent fields.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the custom object.
        name : str
            Custom object name to create or patch.
        spec : Mapping[str, object]
            Desired custom object `spec` payload.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Labels to merge over the resource defaults.
        annotations : Mapping[str, str] | None, optional
            Metadata annotations to apply.

        Returns
        -------
        NamespacedCustomObject
            Wrapped created or patched custom object.
        """
        return await self._upsert(
            kube,
            namespace=namespace,
            body=self._body(
                namespace=namespace,
                name=name,
                spec=spec,
                labels=labels,
                annotations=annotations,
            ),
            timeout=timeout,
        )

    async def patch_status(
        self,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        status: Mapping[str, object],
        timeout: float,
    ) -> NamespacedCustomObject:
        """Patch the status subresource for one custom object.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the custom object.
        name : str
            Custom object name to patch.
        status : Mapping[str, object]
            Desired `status` payload.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        NamespacedCustomObject
            Wrapped custom object returned by the status patch.
        """
        return await self._patch_status(
            kube,
            namespace=namespace,
            name=name,
            status=status,
            timeout=timeout,
        )

    async def delete(
        self,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        timeout: float,
    ) -> None:
        """Delete one namespaced custom object.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the custom object.
        name : str
            Custom object name to delete.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        await kube.run(
            lambda request_timeout: kube.custom.delete_namespaced_custom_object(
                group=self.spec.group,
                version=self.spec.version,
                namespace=namespace,
                plural=self.spec.plural,
                name=name,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=(
                f"failed to delete custom object {self.spec.plural}/{name} "
                f"in namespace {namespace!r}"
            ),
        )

    async def _snapshot(
        self,
        kube: Kube,
        *,
        namespace: str,
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> _CustomResourceSnapshot:
        payload = await kube.run(
            lambda request_timeout: kube.custom.list_namespaced_custom_object(
                group=self.spec.group,
                version=self.spec.version,
                namespace=namespace,
                plural=self.spec.plural,
                label_selector=_label_selector(labels),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=(
                f"failed to list custom objects {self.spec.plural} "
                f"in namespace {namespace!r}"
            ),
        )
        if payload is None:
            msg = (
                f"Kubernetes custom object list for {self.spec.plural} "
                "returned no payload"
            )
            raise OSError(msg)
        if not isinstance(payload, Mapping):
            msg = (
                f"malformed Kubernetes custom object list payload for "
                f"{self.spec.plural}"
            )
            raise OSError(msg)
        payload = cast("Mapping[str, object]", payload)
        metadata = payload.get("metadata", {})
        if not isinstance(metadata, Mapping):
            msg = (
                f"malformed Kubernetes custom object list metadata for "
                f"{self.spec.plural}"
            )
            raise OSError(msg)
        metadata = cast("Mapping[str, object]", metadata)
        resource_version = str(metadata.get("resourceVersion") or "").strip()
        if not resource_version:
            msg = (
                f"Kubernetes custom object list for {self.spec.plural} "
                "had no resourceVersion"
            )
            raise OSError(msg)
        items = payload.get("items", [])
        if not isinstance(items, list):
            msg = (
                f"malformed Kubernetes custom object list items for {self.spec.plural}"
            )
            raise OSError(msg)
        out = [
            NamespacedCustomObject._from_payload(
                group=self.spec.group,
                version=self.spec.version,
                plural=self.spec.plural,
                payload=item,
            )
            for item in items
        ]
        return _CustomResourceSnapshot(items=out, resource_version=resource_version)

    async def _create(
        self,
        kube: Kube,
        *,
        namespace: str,
        body: Mapping[str, object],
        timeout: float,
    ) -> NamespacedCustomObject:
        payload = await kube.run(
            lambda request_timeout: kube.custom.create_namespaced_custom_object(
                group=self.spec.group,
                version=self.spec.version,
                namespace=namespace,
                plural=self.spec.plural,
                body=dict(body),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=(
                f"failed to create custom object {self.spec.plural} "
                f"in namespace {namespace!r}"
            ),
        )
        return NamespacedCustomObject._from_payload(
            group=self.spec.group,
            version=self.spec.version,
            plural=self.spec.plural,
            payload=payload,
        )

    async def _upsert(
        self,
        kube: Kube,
        *,
        namespace: str,
        body: Mapping[str, object],
        timeout: float,
    ) -> NamespacedCustomObject:
        metadata = body.get("metadata")
        if not isinstance(metadata, Mapping):
            msg = "custom object body must define metadata"
            raise OSError(msg)
        metadata = cast("Mapping[str, object]", metadata)
        name = str(metadata.get("name") or "").strip()
        if not name:
            msg = "custom object body must define metadata.name"
            raise OSError(msg)
        try:
            return await self._create(
                kube,
                namespace=namespace,
                body=body,
                timeout=timeout,
            )
        except OSError as err:
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise

        payload = await kube.run(
            lambda request_timeout: kube.custom.patch_namespaced_custom_object(
                group=self.spec.group,
                version=self.spec.version,
                namespace=namespace,
                plural=self.spec.plural,
                name=name,
                body=dict(body),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=(
                f"failed to patch custom object {self.spec.plural}/{name} "
                f"in namespace {namespace!r}"
            ),
        )
        return NamespacedCustomObject._from_payload(
            group=self.spec.group,
            version=self.spec.version,
            plural=self.spec.plural,
            payload=payload,
        )

    async def _patch_status(
        self,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        status: Mapping[str, object],
        timeout: float,
    ) -> NamespacedCustomObject:
        payload = await kube.run(
            lambda request_timeout: kube.custom.patch_namespaced_custom_object_status(
                group=self.spec.group,
                version=self.spec.version,
                namespace=namespace,
                plural=self.spec.plural,
                name=name,
                body={"status": dict(status)},
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to patch custom object status {self.spec.plural}/{name}",
        )
        return NamespacedCustomObject._from_payload(
            group=self.spec.group,
            version=self.spec.version,
            plural=self.spec.plural,
            payload=payload,
        )


@dataclass(frozen=True, init=False)
class NamespacedCustomObject:
    """Generic wrapper around one namespaced Kubernetes custom object.

    Parameters
    ----------
    group : str
        API group that owns the custom object.
    version : str
        Served API version.
    plural : str
        Plural REST resource name.
    payload : Mapping[str, Any]
        Custom-object payload returned by Kubernetes.
    """

    group: str
    version: str
    plural: str
    _payload: Mapping[str, Any] = field(repr=False)

    def __init__(
        self,
        *,
        group: str,
        version: str,
        plural: str,
        payload: Mapping[str, Any],
    ) -> None:
        """Initialize one custom-object wrapper.

        Parameters
        ----------
        group : str
            API group that owns the custom object.
        version : str
            Served API version.
        plural : str
            Plural REST resource name.
        payload : Mapping[str, Any]
            Custom-object payload returned by Kubernetes.
        """
        object.__setattr__(self, "group", group)
        object.__setattr__(self, "version", version)
        object.__setattr__(self, "plural", plural)
        object.__setattr__(self, "_payload", payload)

    @classmethod
    def _from_payload(
        cls,
        *,
        group: str,
        version: str,
        plural: str,
        payload: object,
    ) -> Self:
        if not isinstance(payload, Mapping):
            msg = f"malformed Kubernetes custom object payload for {plural}"
            raise OSError(msg)
        return cls(
            group=group,
            version=version,
            plural=plural,
            payload=cast("Mapping[str, Any]", payload),
        )

    @property
    def payload(self) -> Mapping[str, Any]:
        """Return this custom object's payload.

        Returns
        -------
        Mapping[str, Any]
            Read-only top-level view of the custom-object payload returned by
            Kubernetes.
        """
        return MappingProxyType(dict(self._payload))

    @property
    def metadata(self) -> Mapping[str, Any]:
        """Return this custom object's metadata.

        Returns
        -------
        Mapping[str, Any]
            Read-only view of `metadata`, or an empty mapping when unavailable.
        """
        metadata = self._payload.get("metadata", {})
        if not isinstance(metadata, Mapping):
            return MappingProxyType({})
        return MappingProxyType(cast("dict[str, Any]", dict(metadata)))

    @property
    def name(self) -> str:
        """Return this custom object's name.

        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        return str(self.metadata.get("name") or "").strip()

    @property
    def namespace(self) -> str:
        """Return this custom object's namespace.

        Returns
        -------
        str
            Trimmed `metadata.namespace`, or an empty string when unavailable.
        """
        return str(self.metadata.get("namespace") or "").strip()

    @property
    def labels(self) -> Mapping[str, str]:
        """Return this custom object's labels.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.labels`, or an empty mapping when
            unavailable.
        """
        labels = self.metadata.get("labels", {})
        if not isinstance(labels, Mapping):
            return MappingProxyType({})
        return MappingProxyType({str(key): str(value) for key, value in labels.items()})

    @property
    def annotations(self) -> Mapping[str, str]:
        """Return this custom object's annotations.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.annotations`, or an empty mapping when
            unavailable.
        """
        annotations = self.metadata.get("annotations", {})
        if not isinstance(annotations, Mapping):
            return MappingProxyType({})
        return MappingProxyType(
            {str(key): str(value) for key, value in annotations.items()}
        )

    @property
    def resource_version(self) -> str:
        """Return this custom object's resource version.

        Returns
        -------
        str
            Kubernetes `metadata.resourceVersion`, or an empty string when
            unavailable.
        """
        return str(self.metadata.get("resourceVersion") or "").strip()

    @property
    def uid(self) -> str:
        """Return this custom object's UID.

        Returns
        -------
        str
            Kubernetes `metadata.uid`, or an empty string when unavailable.
        """
        return str(self.metadata.get("uid") or "").strip()

    @property
    def created_at(self) -> str:
        """Return this custom object's creation timestamp.

        Returns
        -------
        str
            Kubernetes `metadata.creationTimestamp`, or an empty string when
            unavailable.
        """
        return str(self.metadata.get("creationTimestamp") or "").strip()


@dataclass(frozen=True)
class CustomResourceDefinition(KubeMetadata[kube_client.V1CustomResourceDefinition]):
    """General-purpose wrapper around one Kubernetes CRD object."""

    _obj: kube_client.V1CustomResourceDefinition

    @staticmethod
    def _manifest(
        *,
        group: str,
        version: str,
        plural: str,
        singular: str,
        kind: str,
        spec_schema: Mapping[str, object],
        status_schema: Mapping[str, object] | None,
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
        scope: str,
        short_names: Collection[str],
    ) -> dict[str, object]:
        schema_properties: dict[str, object] = {"spec": dict(spec_schema)}
        version_entry: dict[str, object] = {
            "name": version,
            "served": True,
            "storage": True,
            "schema": {
                "openAPIV3Schema": {
                    "type": "object",
                    "properties": schema_properties,
                },
            },
        }
        if status_schema is not None:
            schema_properties["status"] = dict(status_schema)
            version_entry["subresources"] = {"status": {}}
        return {
            "apiVersion": "apiextensions.k8s.io/v1",
            "kind": "CustomResourceDefinition",
            "metadata": {
                "name": f"{plural}.{group}",
                "labels": dict(labels or {}),
                "annotations": dict(annotations or {}),
            },
            "spec": {
                "group": group,
                "scope": scope,
                "names": {
                    "plural": plural,
                    "singular": singular,
                    "kind": kind,
                    "shortNames": list(short_names),
                },
                "versions": [version_entry],
            },
        }

    @classmethod
    async def get(cls, kube: Kube, *, name: str, timeout: float) -> Self | None:
        """Read one Kubernetes CustomResourceDefinition by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            CRD name to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        CustomResourceDefinition | None
            Wrapped Kubernetes CRD, or `None` if it does not exist.

        """
        payload = await kube.run(
            lambda request_timeout: kube.apiextensions.read_custom_resource_definition(
                name=name,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to read CustomResourceDefinition {name!r}",
        )
        if payload is None:
            return None
        return cls(
            _obj=_typed_payload(
                payload,
                kube_client.V1CustomResourceDefinition,
                context="CRD",
            )
        )

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[Self]:
        """List Kubernetes CustomResourceDefinitions with optional filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.

        Returns
        -------
        list[CustomResourceDefinition]
            Wrapped CRDs matching the requested filters.

        """
        return [
            cls(_obj=item)
            for item in await _list_cluster_items(
                kube,
                timeout=timeout,
                labels=labels,
                list_items=lambda label_selector, request_timeout: (
                    kube.apiextensions.list_custom_resource_definition(
                        label_selector=label_selector,
                        _request_timeout=request_timeout,
                    )
                ),
                list_type=kube_client.V1CustomResourceDefinitionList,
                item_type=kube_client.V1CustomResourceDefinition,
                context="failed to list CustomResourceDefinitions",
                list_context="CRD",
                item_context="CRD",
            )
        ]

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        group: str,
        version: str,
        plural: str,
        singular: str,
        kind: str,
        spec_schema: Mapping[str, object],
        timeout: float,
        status_schema: Mapping[str, object] | None = None,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
        scope: str = "Namespaced",
        short_names: Collection[str] = (),
    ) -> Self:
        """Create or patch one Kubernetes CustomResourceDefinition.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        group : str
            API group that owns the custom resource.
        version : str
            Served and stored API version.
        plural : str
            Plural REST resource name.
        singular : str
            Singular resource name.
        kind : str
            Kubernetes kind name.
        spec_schema : Mapping[str, object]
            OpenAPI schema for the custom resource `spec` object.
        timeout : float
            Maximum request budget in seconds.
        status_schema : Mapping[str, object] | None, optional
            Optional OpenAPI schema for the custom resource `status` object. When
            omitted, the CRD has no status subresource.
        labels : Mapping[str, str] | None, optional
            Labels to apply to the CRD object.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to the CRD object.
        scope : str, optional
            CRD scope, typically `"Namespaced"` or `"Cluster"`.
        short_names : Collection[str], optional
            Optional short names for kubectl discovery.

        Returns
        -------
        CustomResourceDefinition
            Wrapped CRD returned by the cluster.

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or the API call fails.
        """
        group = group.strip()
        version = version.strip()
        plural = plural.strip()
        singular = singular.strip()
        kind = kind.strip()
        scope = scope.strip()
        if not all((group, version, plural, singular, kind, scope)):
            msg = "CRD upsert requires non-empty group, version, names, kind, and scope"
            raise OSError(msg)
        name = f"{plural}.{group}"
        body = cls._manifest(
            group=group,
            version=version,
            plural=plural,
            singular=singular,
            kind=kind,
            spec_schema=spec_schema,
            status_schema=status_schema,
            labels=labels,
            annotations=annotations,
            scope=scope,
            short_names=short_names,
        )
        payload = await _create_or_patch(
            kube,
            timeout=timeout,
            create=lambda request_timeout: (
                kube.apiextensions.create_custom_resource_definition(
                    body=body,
                    _request_timeout=request_timeout,
                )
            ),
            patch=lambda request_timeout: (
                kube.apiextensions.patch_custom_resource_definition(
                    name=name,
                    body=body,
                    _request_timeout=request_timeout,
                )
            ),
            create_context=f"failed to create CustomResourceDefinition {name}",
            patch_context=f"failed to patch CustomResourceDefinition {name}",
            expected=kube_client.V1CustomResourceDefinition,
            payload_context="CRD",
        )
        return cls(_obj=payload)

    @property
    def is_established(self) -> bool:
        """Return whether this CRD is established.

        Returns
        -------
        bool
            Whether the CRD has an `Established=True` condition.
        """
        status = self._obj.status
        for condition in (status.conditions or []) if status is not None else []:
            if condition.type == "Established" and condition.status == "True":
                return True
        return False

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this CRD by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        CustomResourceDefinition | None
            Fresh wrapper for the same CRD, or `None` if it no longer exists.
        """
        name = self._require_name("refresh CRD")
        return await type(self).get(kube, name=name, timeout=timeout)

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this CRD from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        name = self._require_name("delete CRD")
        payload = await kube.run(
            lambda request_timeout: (
                kube.apiextensions.delete_custom_resource_definition(
                    name=name,
                    _request_timeout=request_timeout,
                )
            ),
            timeout=timeout,
            context=f"failed to delete CRD {name}",
        )
        _validate_delete_status(payload, label=self._object_label(name))

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this CRD is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        """
        name = self._require_name("wait for CRD deletion")
        await _wait_until_deleted(
            label=self._object_label(name),
            timeout=timeout,
            refresh=lambda remaining: self.refresh(kube, timeout=remaining),
        )

    async def wait_established(self, kube: Kube, *, timeout: float) -> Self:
        """Wait until this CRD reports `Established=True`.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        CustomResourceDefinition
            Refreshed CRD wrapper that reports `Established=True`.

        Raises
        ------
        TimeoutError
            If the CRD does not become established before `timeout`.
        """
        current: Self = self

        async def established(remaining: float) -> Self:
            nonlocal current
            if current.is_established:
                return current
            refreshed = await current.refresh(kube, timeout=remaining)
            if refreshed is None:
                msg = f"CRD {self.name!r} disappeared"
                raise OSError(msg)
            current = refreshed
            if current.is_established:
                return current
            msg = f"CRD {self.name!r} is not established yet"
            raise TimeoutError(msg)

        try:
            return await until(
                established,
                timeout=timeout,
                interval=CRD_WAIT_POLL_INTERVAL_SECONDS,
                action=f"waiting for CRD {self.name!r} establishment",
            )
        except TimeoutError as err:
            msg = f"timed out waiting for CRD {self.name!r} establishment"
            raise TimeoutError(msg) from err
