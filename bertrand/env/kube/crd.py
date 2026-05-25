"""Wrappers for Kubernetes CustomResourceDefinition objects."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Self

from kubernetes import client as kube_client

from bertrand.env.git import until

from .api.metadata import KubeMetadata
from .api.resource import ClusterMutableResourceMixin, ResourceClient

CRD_WAIT_POLL_INTERVAL_SECONDS = 0.5

if TYPE_CHECKING:
    from collections.abc import Collection, Mapping

    from .api.client import Kube


@dataclass(frozen=True)
class CustomResourceDefinition(
    ClusterMutableResourceMixin[kube_client.V1CustomResourceDefinition],
    KubeMetadata[kube_client.V1CustomResourceDefinition],
):
    """General-purpose wrapper around one Kubernetes CRD object."""

    _obj: kube_client.V1CustomResourceDefinition

    @classmethod
    def _client(
        cls,
    ) -> ResourceClient[kube_client.V1CustomResourceDefinition, Self]:
        return ResourceClient(
            scope="cluster",
            kind="CustomResourceDefinition",
            expected=kube_client.V1CustomResourceDefinition,
            list_type=kube_client.V1CustomResourceDefinitionList,
            wrapper=lambda payload: cls(_obj=payload),
            read=lambda kube, _namespace, name, request_timeout: (
                kube.apiextensions.read_custom_resource_definition(
                    name=name,
                    _request_timeout=request_timeout,
                )
            ),
            list_all=lambda kube, label_selector, field_selector, request_timeout: (
                kube.apiextensions.list_custom_resource_definition(
                    label_selector=label_selector,
                    field_selector=field_selector,
                    _request_timeout=request_timeout,
                )
            ),
            create=lambda kube, _namespace, _name, manifest, request_timeout: (
                kube.apiextensions.create_custom_resource_definition(
                    body=manifest,
                    _request_timeout=request_timeout,
                )
            ),
            patch=lambda kube, _namespace, name, manifest, request_timeout: (
                kube.apiextensions.patch_custom_resource_definition(
                    name=name,
                    body=manifest,
                    _request_timeout=request_timeout,
                )
            ),
            delete=lambda kube, _namespace, name, request_timeout: (
                kube.apiextensions.delete_custom_resource_definition(
                    name=name,
                    _request_timeout=request_timeout,
                )
            ),
        )

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
        return await cls._client().upsert(
            kube,
            name=name,
            manifest=body,
            timeout=timeout,
        )

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
