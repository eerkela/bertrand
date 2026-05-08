"""Wrappers for Kubernetes CustomResourceDefinition objects."""

from __future__ import annotations

import asyncio
from collections.abc import Collection, Mapping
from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING, Any, Self, cast

from kubernetes import client as kube_client

from .api import Kube, _label_selector

CRD_WAIT_POLL_INTERVAL_SECONDS = 0.5

if TYPE_CHECKING:
    import builtins


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
        """Return the fully qualified Kubernetes API version.

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

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or a list call fails.
        """
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
            return []
        if not isinstance(payload, Mapping):
            msg = f"malformed Kubernetes custom object list payload for {plural}"
            raise OSError(msg)
        items = payload.get("items", [])
        if not isinstance(items, list):
            msg = f"malformed Kubernetes custom object list items for {plural}"
            raise OSError(msg)
        out: builtins.list[Self] = []
        for item in items:
            if not isinstance(item, Mapping):
                msg = f"malformed Kubernetes custom object list entry for {plural}"
                raise OSError(msg)
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
            obj=payload,
        )

    @property
    def metadata(self) -> Mapping[str, Any]:
        """Return this custom object's metadata.

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
        """Return this custom object's name.

        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        return str(self.metadata.get("name") or "").strip()


@dataclass(frozen=True)
class CustomResourceDefinition:
    """General-purpose wrapper around one Kubernetes CRD object."""

    obj: kube_client.V1CustomResourceDefinition

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
        return cls(obj=payload)

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
            return cls(obj=created)

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
        return cls(obj=patched)

    @property
    def name(self) -> str:
        """Return this CRD's name.

        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def labels(self) -> Mapping[str, str]:
        """Return this CRD's labels.

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
    def is_established(self) -> bool:
        """Return whether this CRD is established.

        Returns
        -------
        bool
            Whether the CRD has an `Established=True` condition.
        """
        status = self.obj.status
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
