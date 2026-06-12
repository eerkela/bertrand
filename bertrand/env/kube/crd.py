"""Wrappers for Kubernetes CustomResourceDefinition objects."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from kubernetes import client as kube_client

from .api.client import Kube
from .api.resource import (
    KubeResource,
    cluster_resource,
)

if TYPE_CHECKING:
    from collections.abc import Collection, Mapping

    from bertrand.env.git import Deadline


@cluster_resource(
    api=kube_client.ApiextensionsV1Api,
    payload=kube_client.V1CustomResourceDefinition,
    read=kube_client.ApiextensionsV1Api.read_custom_resource_definition,
    list=kube_client.ApiextensionsV1Api.list_custom_resource_definition,
    delete=kube_client.ApiextensionsV1Api.delete_custom_resource_definition,
)
@dataclass(frozen=True)
class CustomResourceDefinition(
    KubeResource[kube_client.V1CustomResourceDefinition],
):
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
        deadline: Deadline,
        status_schema: Mapping[str, object] | None = None,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
        scope: str = "Namespaced",
        short_names: Collection[str] = (),
    ) -> CustomResourceDefinition:
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
        deadline : Deadline
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
        api = kube_client.ApiextensionsV1Api(kube.client)
        try:
            payload = await kube.run(
                lambda request_timeout: api.create_custom_resource_definition(
                    body=body,
                    _request_timeout=request_timeout,
                ),
                deadline=deadline,
                context=f"failed to create CRD {name}",
                missing_ok=False,
            )
        except OSError as err:
            if not isinstance(err, Kube.APIError) or err.status != 409:
                raise
            payload = await kube.run(
                lambda request_timeout: api.patch_custom_resource_definition(
                    name=name,
                    body=body,
                    _request_timeout=request_timeout,
                ),
                deadline=deadline,
                context=f"failed to patch CRD {name}",
                missing_ok=False,
            )
        if not isinstance(payload, kube_client.V1CustomResourceDefinition):
            msg = "malformed Kubernetes CustomResourceDefinition payload"
            raise OSError(msg)
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

    async def wait_established(
        self, kube: Kube, *, deadline: Deadline
    ) -> CustomResourceDefinition:
        """Wait until this CRD reports `Established=True`.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum wait budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        CustomResourceDefinition
            Refreshed CRD wrapper that reports `Established=True`.
        """
        return await self.wait_until(
            kube,
            deadline=deadline,
            predicate=lambda live: live.is_established,
            check_current=True,
        )
