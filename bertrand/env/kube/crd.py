"""Wrappers for Kubernetes CustomResourceDefinition objects."""

from __future__ import annotations

import asyncio
from collections.abc import AsyncIterator, Collection, Mapping
from dataclasses import dataclass, field
from types import MappingProxyType
from typing import TYPE_CHECKING, Any, Self, cast

from kubernetes import client as kube_client

from .api import (
    CustomResourceSpec,
    Kube,
    WatchEvent,
    WatchExpired,
    _label_selector,
)

CRD_WAIT_POLL_INTERVAL_SECONDS = 0.5

if TYPE_CHECKING:
    import builtins
    from datetime import datetime


@dataclass(frozen=True)
class _CustomResourceSnapshot:
    items: list[NamespacedCustomObject]
    resource_version: str


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
        return await NamespacedCustomObject.get(
            kube,
            group=self.spec.group,
            version=self.spec.version,
            namespace=namespace,
            plural=self.spec.plural,
            name=name,
            timeout=timeout,
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
        return await NamespacedCustomObject.list(
            kube,
            group=self.spec.group,
            version=self.spec.version,
            namespace=namespace,
            plural=self.spec.plural,
            labels=labels,
            timeout=timeout,
        )

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
                snapshot = await NamespacedCustomObject._snapshot(
                    kube,
                    group=self.spec.group,
                    version=self.spec.version,
                    namespace=namespace,
                    plural=self.spec.plural,
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
                async for event in kube.watch(
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
        return await NamespacedCustomObject._create(
            kube,
            group=self.spec.group,
            version=self.spec.version,
            namespace=namespace,
            plural=self.spec.plural,
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
        return await NamespacedCustomObject._upsert(
            kube,
            group=self.spec.group,
            version=self.spec.version,
            namespace=namespace,
            plural=self.spec.plural,
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
        obj = NamespacedCustomObject(
            group=self.spec.group,
            version=self.spec.version,
            plural=self.spec.plural,
            payload={},
        )
        return await obj.patch_status(
            kube,
            namespace=namespace,
            name=name,
            status=status,
            timeout=timeout,
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

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        group: str,
        version: str,
        namespace: str,
        plural: str,
        name: str,
        timeout: float,
    ) -> Self | None:
        """Read one namespaced custom object by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        group : str
            API group that owns the custom object.
        version : str
            Served API version.
        namespace : str
            Namespace that owns the custom object.
        plural : str
            Plural REST resource name.
        name : str
            Custom object name to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        NamespacedCustomObject | None
            Wrapped custom object, or `None` if it does not exist.

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or the API call fails.
        """
        payload = await kube.run(
            lambda request_timeout: kube.custom.get_namespaced_custom_object(
                group=group,
                version=version,
                namespace=namespace,
                plural=plural,
                name=name,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=(
                f"failed to read custom object {plural}/{name} "
                f"in namespace {namespace!r}"
            ),
        )
        if payload is None:
            return None
        if not isinstance(payload, Mapping):
            msg = f"malformed Kubernetes custom object payload for {plural}/{name}"
            raise OSError(msg)
        return cls(
            group=group,
            version=version,
            plural=plural,
            payload=cast("Mapping[str, Any]", payload),
        )

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        group: str,
        version: str,
        namespace: str,
        plural: str,
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[NamespacedCustomObject]:
        """List namespaced custom objects with optional label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        group : str
            API group that owns the custom objects.
        version : str
            Served API version.
        namespace : str
            Namespace to query.
        plural : str
            Plural REST resource name.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.

        Returns
        -------
        list[NamespacedCustomObject]
            Wrapped custom objects matching the requested filters.

        """
        snapshot = await cls._snapshot(
            kube,
            group=group,
            version=version,
            namespace=namespace,
            plural=plural,
            labels=labels,
            timeout=timeout,
        )
        return snapshot.items

    @classmethod
    async def _snapshot(
        cls,
        kube: Kube,
        *,
        group: str,
        version: str,
        namespace: str,
        plural: str,
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> _CustomResourceSnapshot:
        payload = await kube.run(
            lambda request_timeout: kube.custom.list_namespaced_custom_object(
                group=group,
                version=version,
                namespace=namespace,
                plural=plural,
                label_selector=_label_selector(labels),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=(
                f"failed to list custom objects {plural} in namespace {namespace!r}"
            ),
        )
        if payload is None:
            msg = f"Kubernetes custom object list for {plural} returned no payload"
            raise OSError(msg)
        if not isinstance(payload, Mapping):
            msg = f"malformed Kubernetes custom object list payload for {plural}"
            raise OSError(msg)
        payload = cast("Mapping[str, object]", payload)
        metadata = payload.get("metadata", {})
        if not isinstance(metadata, Mapping):
            msg = f"malformed Kubernetes custom object list metadata for {plural}"
            raise OSError(msg)
        metadata = cast("Mapping[str, object]", metadata)
        resource_version = str(metadata.get("resourceVersion") or "").strip()
        if not resource_version:
            msg = f"Kubernetes custom object list for {plural} had no resourceVersion"
            raise OSError(msg)
        items = payload.get("items", [])
        if not isinstance(items, list):
            msg = f"malformed Kubernetes custom object list items for {plural}"
            raise OSError(msg)
        out = [
            NamespacedCustomObject._from_payload(
                group=group,
                version=version,
                plural=plural,
                payload=item,
            )
            for item in items
        ]
        return _CustomResourceSnapshot(items=out, resource_version=resource_version)

    @classmethod
    async def _create(
        cls,
        kube: Kube,
        *,
        group: str,
        version: str,
        namespace: str,
        plural: str,
        body: Mapping[str, object],
        timeout: float,
    ) -> Self:
        """Create one namespaced custom object.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        group : str
            API group that owns the custom object.
        version : str
            Served API version.
        namespace : str
            Namespace that owns the custom object.
        plural : str
            Plural REST resource name.
        body : Mapping[str, object]
            Kubernetes custom-object body to create.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        NamespacedCustomObject
            Wrapped created custom object.

        Raises
        ------
        OSError
            If Kubernetes create fails or returns malformed data.
        """
        payload = await kube.run(
            lambda request_timeout: kube.custom.create_namespaced_custom_object(
                group=group,
                version=version,
                namespace=namespace,
                plural=plural,
                body=dict(body),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=(
                f"failed to create custom object {plural} in namespace {namespace!r}"
            ),
        )
        if not isinstance(payload, Mapping):
            msg = f"malformed Kubernetes custom object create payload for {plural}"
            raise OSError(msg)
        return cls(group=group, version=version, plural=plural, payload=payload)

    @classmethod
    async def _upsert(
        cls,
        kube: Kube,
        *,
        group: str,
        version: str,
        namespace: str,
        plural: str,
        body: Mapping[str, object],
        timeout: float,
    ) -> Self:
        """Create or patch one namespaced custom object.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        group : str
            API group that owns the custom object.
        version : str
            Served API version.
        namespace : str
            Namespace that owns the custom object.
        plural : str
            Plural REST resource name.
        body : Mapping[str, object]
            Kubernetes custom-object body to create or patch.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        NamespacedCustomObject
            Wrapped created or patched custom object.

        Raises
        ------
        OSError
            If the body has no name, or Kubernetes create/patch fails or returns
            malformed data.
        """
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
            return await cls._create(
                kube,
                group=group,
                version=version,
                namespace=namespace,
                plural=plural,
                body=body,
                timeout=timeout,
            )
        except OSError as err:
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise

        payload = await kube.run(
            lambda request_timeout: kube.custom.patch_namespaced_custom_object(
                group=group,
                version=version,
                namespace=namespace,
                plural=plural,
                name=name,
                body=dict(body),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=(
                f"failed to patch custom object {plural}/{name} "
                f"in namespace {namespace!r}"
            ),
        )
        if not isinstance(payload, Mapping):
            msg = (
                f"malformed Kubernetes custom object patch payload for {plural}/{name}"
            )
            raise OSError(msg)
        return cls(group=group, version=version, plural=plural, payload=payload)

    async def patch_status(
        self,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        status: Mapping[str, object],
        timeout: float,
    ) -> Self:
        """Patch the status subresource for this custom object.

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

        Raises
        ------
        OSError
            If Kubernetes status patch fails or returns malformed data.
        """
        payload = await kube.run(
            lambda request_timeout: kube.custom.patch_namespaced_custom_object_status(
                group=self.group,
                version=self.version,
                namespace=namespace,
                plural=self.plural,
                name=name,
                body={"status": dict(status)},
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to patch custom object status {self.plural}/{name}",
        )
        if not isinstance(payload, Mapping):
            msg = f"malformed Kubernetes custom object status payload for {self.plural}"
            raise OSError(msg)
        return type(self)(
            group=self.group,
            version=self.version,
            plural=self.plural,
            payload=payload,
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
class CustomResourceDefinition:
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
        status_schema: Mapping[str, object],
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
        scope: str,
        short_names: Collection[str],
    ) -> dict[str, object]:
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
                "versions": [
                    {
                        "name": version,
                        "served": True,
                        "storage": True,
                        "schema": {
                            "openAPIV3Schema": {
                                "type": "object",
                                "properties": {
                                    "spec": dict(spec_schema),
                                    "status": dict(status_schema),
                                },
                            }
                        },
                        "subresources": {"status": {}},
                    }
                ],
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

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or the API call fails.
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
        if not isinstance(payload, kube_client.V1CustomResourceDefinition):
            msg = f"malformed Kubernetes CRD payload for {name!r}"
            raise OSError(msg)
        return cls(_obj=payload)

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

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or a list call fails.
        """
        payload = await kube.run(
            lambda request_timeout: kube.apiextensions.list_custom_resource_definition(
                label_selector=_label_selector(labels),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context="failed to list CustomResourceDefinitions",
        )
        if payload is None:
            return []
        if not isinstance(payload, kube_client.V1CustomResourceDefinitionList):
            msg = "malformed Kubernetes CRD list payload"
            raise OSError(msg)
        out: builtins.list[Self] = []
        for item in payload.items or []:
            if not isinstance(item, kube_client.V1CustomResourceDefinition):
                msg = "malformed Kubernetes CRD entry in list payload"
                raise OSError(msg)
            out.append(cls(_obj=item))
        return out

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
        status_schema: Mapping[str, object],
        timeout: float,
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
        status_schema : Mapping[str, object]
            OpenAPI schema for the custom resource `status` object.
        timeout : float
            Maximum request budget in seconds.
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
            If required CRD identity fields are empty, or Kubernetes create/patch
            fails or returns malformed data.
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
        try:
            created = await kube.run(
                lambda request_timeout: (
                    kube.apiextensions.create_custom_resource_definition(
                        body=body,
                        _request_timeout=request_timeout,
                    )
                ),
                timeout=timeout,
                context=f"failed to create CustomResourceDefinition {name}",
            )
        except OSError as err:
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise
        else:
            if not isinstance(created, kube_client.V1CustomResourceDefinition):
                msg = f"malformed Kubernetes CRD payload while creating {name!r}"
                raise OSError(msg)
            return cls(_obj=created)

        patched = await kube.run(
            lambda request_timeout: kube.apiextensions.patch_custom_resource_definition(
                name=name,
                body=body,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to patch CustomResourceDefinition {name}",
        )
        if not isinstance(patched, kube_client.V1CustomResourceDefinition):
            msg = f"malformed Kubernetes CRD payload while patching {name!r}"
            raise OSError(msg)
        return cls(_obj=patched)

    @property
    def name(self) -> str:
        """Return this CRD's name.

        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        metadata = self._obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def labels(self) -> Mapping[str, str]:
        """Return this CRD's labels.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.labels`, or an empty mapping when unavailable.
        """
        metadata = self._obj.metadata
        if metadata is None or metadata.labels is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.labels)

    @property
    def annotations(self) -> Mapping[str, str]:
        """Return this CRD's annotations.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.annotations`, or an empty mapping when
            unavailable.
        """
        metadata = self._obj.metadata
        if metadata is None or metadata.annotations is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.annotations)

    @property
    def resource_version(self) -> str:
        """Return this CRD's resource version.

        Returns
        -------
        str
            Kubernetes `metadata.resourceVersion`, or an empty string when
            unavailable.
        """
        metadata = self._obj.metadata
        return (metadata.resource_version or "").strip() if metadata is not None else ""

    @property
    def uid(self) -> str:
        """Return this CRD's UID.

        Returns
        -------
        str
            Kubernetes `metadata.uid`, or an empty string when unavailable.
        """
        metadata = self._obj.metadata
        return (metadata.uid or "").strip() if metadata is not None else ""

    @property
    def created_at(self) -> datetime | None:
        """Return this CRD's creation timestamp.

        Returns
        -------
        datetime | None
            Kubernetes `metadata.creationTimestamp`, or `None` when unavailable.
        """
        metadata = self._obj.metadata
        return metadata.creation_timestamp if metadata is not None else None

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

        Raises
        ------
        OSError
            If this wrapper does not contain enough metadata to identify the CRD,
            or if Kubernetes returns malformed data.
        """
        name = self.name
        if not name:
            msg = "cannot refresh CRD with missing metadata.name"
            raise OSError(msg)
        return await type(self).get(kube, name=name, timeout=timeout)

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this CRD from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Raises
        ------
        OSError
            If this wrapper does not contain enough metadata to identify the CRD, if
            the delete request fails, or if Kubernetes returns malformed data.
        """
        name = self.name
        if not name:
            msg = "cannot delete CRD with missing metadata.name"
            raise OSError(msg)
        payload = await kube.run(
            lambda request_timeout: (
                kube.apiextensions.delete_custom_resource_definition(
                    name=name,
                    body=kube_client.V1DeleteOptions(),
                    _request_timeout=request_timeout,
                )
            ),
            timeout=timeout,
            context=f"failed to delete CustomResourceDefinition {name}",
        )
        if payload is not None and not isinstance(payload, kube_client.V1Status):
            msg = f"malformed Kubernetes response while deleting CRD {name}"
            raise OSError(msg)

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this CRD is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        Raises
        ------
        OSError
            If this wrapper does not contain enough metadata to identify the CRD, or
            if a refresh request returns malformed data.
        TimeoutError
            If the CRD still exists when `timeout` expires.
        """
        name = self.name
        if not name:
            msg = "cannot wait for CRD deletion with missing metadata.name"
            raise OSError(msg)
        if timeout <= 0:
            msg = f"timed out waiting for CRD {name!r} deletion"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                msg = f"timed out waiting for CRD {name!r} deletion"
                raise TimeoutError(msg)
            live = await self.refresh(kube, timeout=remaining)
            if live is None:
                return
            await asyncio.sleep(min(CRD_WAIT_POLL_INTERVAL_SECONDS, remaining))

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
        OSError
            If this wrapper cannot be refreshed or disappears while waiting.
        """
        if timeout <= 0:
            msg = f"timed out waiting for CRD {self.name!r} establishment"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        current: Self = self
        while True:
            if current.is_established:
                return current
            remaining = deadline - loop.time()
            if remaining <= 0:
                msg = f"timed out waiting for CRD {self.name!r} establishment"
                raise TimeoutError(msg)
            await asyncio.sleep(min(CRD_WAIT_POLL_INTERVAL_SECONDS, remaining))
            refreshed = await current.refresh(kube, timeout=deadline - loop.time())
            if refreshed is None:
                msg = f"CRD {self.name!r} disappeared"
                raise OSError(msg)
            current = refreshed
