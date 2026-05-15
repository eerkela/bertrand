"""Wrappers for Kubernetes CustomResourceDefinition objects."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Self

from kubernetes import client as kube_client

from bertrand.env.git import until

from .api._helpers import (
    _create_or_patch,
    _list_cluster_items,
    _typed_payload,
    _validate_delete_status,
    _wait_until_deleted,
)
from .api.metadata import KubeMetadata

CRD_WAIT_POLL_INTERVAL_SECONDS = 0.5

if TYPE_CHECKING:
    import builtins
    from collections.abc import Collection, Mapping

    from .api.client import Kube


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
