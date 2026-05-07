"""Small helpers for namespaced Kubernetes custom objects."""

from __future__ import annotations

import builtins
from collections.abc import Mapping
from dataclasses import dataclass
from types import MappingProxyType
from typing import Any, Self, cast

from .api import Kube, _label_selector


@dataclass(frozen=True)
class CustomResourceSpec:
    """Intent description for one namespaced Kubernetes custom resource type.

    Parameters
    ----------
    group : str
        Kubernetes API group that owns the resource.
    version : str
        Served API version for the resource.
    kind : str
        Kubernetes kind name.
    plural : str
        Plural REST resource name.
    labels : Mapping[str, str], optional
        Default labels to apply to objects created through this spec.
    """

    group: str
    version: str
    kind: str
    plural: str
    labels: Mapping[str, str] = MappingProxyType({})

    @property
    def api_version(self) -> str:
        """
        Returns
        -------
        str
            Fully qualified Kubernetes API version for objects of this type.
        """
        return f"{self.group}/{self.version}"

    def body(
        self,
        *,
        namespace: str,
        name: str,
        spec: Mapping[str, object],
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> dict[str, object]:
        """Render an object body for create or patch operations.

        Parameters
        ----------
        namespace : str
            Namespace that owns the custom object.
        name : str
            Object name.
        spec : Mapping[str, object]
            Desired `spec` payload.
        labels : Mapping[str, str] | None, optional
            Labels to merge over the spec defaults.
        annotations : Mapping[str, str] | None, optional
            Metadata annotations to apply.

        Returns
        -------
        dict[str, object]
            Kubernetes custom-object payload.
        """
        merged_labels = dict(self.labels)
        merged_labels.update(labels or {})
        return {
            "apiVersion": self.api_version,
            "kind": self.kind,
            "metadata": {
                "name": name,
                "namespace": namespace,
                "labels": merged_labels,
                "annotations": dict(annotations or {}),
            },
            "spec": dict(spec),
        }

    def client(self) -> CustomResourceClient:
        """Create a bound client for this custom resource type.

        Returns
        -------
        CustomResourceClient
            Client whose operations are bound to this resource spec.
        """
        return CustomResourceClient(self)


@dataclass(frozen=True)
class CustomResourceClient:
    """Bound helper for namespaced custom-resource API operations.

    Parameters
    ----------
    spec : CustomResourceSpec
        Resource type handled by this client.
    """

    spec: CustomResourceSpec

    async def get(
        self,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        timeout: float,
    ) -> NamespacedCustomObject | None:
        """Read one namespaced custom object by name."""
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
        """List namespaced custom objects with optional label filtering."""
        return await NamespacedCustomObject.list(
            kube,
            group=self.spec.group,
            version=self.spec.version,
            namespace=namespace,
            plural=self.spec.plural,
            labels=labels,
            timeout=timeout,
        )

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
        """Create one namespaced custom object from intent-level fields."""
        return await NamespacedCustomObject.create(
            kube,
            group=self.spec.group,
            version=self.spec.version,
            namespace=namespace,
            plural=self.spec.plural,
            body=self.spec.body(
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
        """Create or patch one namespaced custom object from intent fields."""
        return await NamespacedCustomObject.upsert(
            kube,
            group=self.spec.group,
            version=self.spec.version,
            namespace=namespace,
            plural=self.spec.plural,
            body=self.spec.body(
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
        """Patch the status subresource for one custom object."""
        obj = NamespacedCustomObject(
            group=self.spec.group,
            version=self.spec.version,
            plural=self.spec.plural,
            obj={},
        )
        return await obj.patch_status(
            kube,
            namespace=namespace,
            name=name,
            status=status,
            timeout=timeout,
        )


@dataclass(frozen=True)
class NamespacedCustomObject:
    """Generic wrapper around one namespaced Kubernetes custom object."""

    group: str
    version: str
    plural: str
    obj: Mapping[str, Any]

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
        """Read one namespaced custom object by name."""
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
            context=f"failed to read custom object {plural}/{name} in namespace {namespace!r}",
        )
        if payload is None:
            return None
        if not isinstance(payload, Mapping):
            raise OSError(f"malformed Kubernetes custom object payload for {plural}/{name}")
        return cls(group=group, version=version, plural=plural, obj=payload)

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
    ) -> builtins.list[Self]:
        """List namespaced custom objects with optional label filtering."""
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
            context=f"failed to list custom objects {plural} in namespace {namespace!r}",
        )
        if payload is None:
            return []
        if not isinstance(payload, Mapping):
            raise OSError(f"malformed Kubernetes custom object list payload for {plural}")
        items = payload.get("items", [])
        if not isinstance(items, list):
            raise OSError(f"malformed Kubernetes custom object list items for {plural}")
        out: builtins.list[Self] = []
        for item in items:
            if not isinstance(item, Mapping):
                raise OSError(f"malformed Kubernetes custom object list entry for {plural}")
            out.append(cls(group=group, version=version, plural=plural, obj=item))
        return out

    @classmethod
    async def create(
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
        """Create one namespaced custom object."""
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
            context=f"failed to create custom object {plural} in namespace {namespace!r}",
        )
        if not isinstance(payload, Mapping):
            raise OSError(f"malformed Kubernetes custom object create payload for {plural}")
        return cls(group=group, version=version, plural=plural, obj=payload)

    @classmethod
    async def upsert(
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
        """Create or patch one namespaced custom object."""
        metadata = body.get("metadata")
        if not isinstance(metadata, Mapping):
            raise OSError("custom object body must define metadata")
        metadata = cast("Mapping[str, object]", metadata)
        name = str(metadata.get("name") or "").strip()
        if not name:
            raise OSError("custom object body must define metadata.name")
        try:
            return await cls.create(
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
            context=f"failed to patch custom object {plural}/{name} in namespace {namespace!r}",
        )
        if not isinstance(payload, Mapping):
            raise OSError(f"malformed Kubernetes custom object patch payload for {plural}/{name}")
        return cls(group=group, version=version, plural=plural, obj=payload)

    async def patch_status(
        self,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        status: Mapping[str, object],
        timeout: float,
    ) -> Self:
        """Patch the status subresource for this custom object."""
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
            raise OSError(f"malformed Kubernetes custom object status payload for {self.plural}")
        return type(self)(
            group=self.group,
            version=self.version,
            plural=self.plural,
            obj=payload,
        )

    @property
    def metadata(self) -> Mapping[str, Any]:
        """
        Returns
        -------
        Mapping[str, Any]
            Read-only view of `metadata`, or an empty mapping when unavailable.
        """
        metadata = self.obj.get("metadata", {})
        if not isinstance(metadata, Mapping):
            return MappingProxyType({})
        return MappingProxyType(cast("dict[str, Any]", dict(metadata)))

    @property
    def name(self) -> str:
        """
        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        return str(self.metadata.get("name") or "").strip()
