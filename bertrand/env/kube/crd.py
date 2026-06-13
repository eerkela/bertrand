"""Wrappers for Kubernetes CustomResourceDefinition objects."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from kubernetes import client as kube_client

from .api.resource import (
    KubeResource,
    cluster_resource,
)

if TYPE_CHECKING:
    from collections.abc import Collection, Mapping


@dataclass(frozen=True)
class CustomResourceDefinitionManifest:
    """Desired state for one Kubernetes CustomResourceDefinition."""

    group: str
    version: str
    plural: str
    singular: str
    kind: str
    spec_schema: Mapping[str, object]
    status_schema: Mapping[str, object] | None = None
    labels: Mapping[str, str] | None = None
    annotations: Mapping[str, str] | None = None
    scope: str = "Namespaced"
    short_names: Collection[str] = ()

    @property
    def name(self) -> str:
        """Return the CRD object name."""
        return f"{self.plural.strip()}.{self.group.strip()}"

    @property
    def namespace(self) -> None:
        """Return no namespace for this cluster-scoped resource."""
        return None

    def manifest(self) -> Mapping[str, object]:
        """Render this desired state as a Kubernetes manifest.

        Returns
        -------
        Mapping[str, object]
            Kubernetes CustomResourceDefinition manifest payload.

        Raises
        ------
        OSError
            If required CRD identity fields are empty.
        """
        group = self.group.strip()
        version = self.version.strip()
        plural = self.plural.strip()
        singular = self.singular.strip()
        kind = self.kind.strip()
        scope = self.scope.strip()
        if not all((group, version, plural, singular, kind, scope)):
            msg = (
                "CRD manifest requires non-empty group, version, names, kind, "
                "and scope"
            )
            raise OSError(msg)
        schema_properties: dict[str, object] = {"spec": dict(self.spec_schema)}
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
        if self.status_schema is not None:
            schema_properties["status"] = dict(self.status_schema)
            version_entry["subresources"] = {"status": {}}
        return {
            "apiVersion": "apiextensions.k8s.io/v1",
            "kind": "CustomResourceDefinition",
            "metadata": {
                "name": f"{plural}.{group}",
                "labels": dict(self.labels or {}),
                "annotations": dict(self.annotations or {}),
            },
            "spec": {
                "group": group,
                "scope": scope,
                "names": {
                    "plural": plural,
                    "singular": singular,
                    "kind": kind,
                    "shortNames": list(self.short_names),
                },
                "versions": [version_entry],
            },
        }


@cluster_resource(
    api=kube_client.ApiextensionsV1Api,
    payload=kube_client.V1CustomResourceDefinition,
    read=kube_client.ApiextensionsV1Api.read_custom_resource_definition,
    list=kube_client.ApiextensionsV1Api.list_custom_resource_definition,
    create=kube_client.ApiextensionsV1Api.create_custom_resource_definition,
    patch=kube_client.ApiextensionsV1Api.patch_custom_resource_definition,
    delete=kube_client.ApiextensionsV1Api.delete_custom_resource_definition,
)
@dataclass(frozen=True)
class CustomResourceDefinition(
    KubeResource[
        kube_client.V1CustomResourceDefinition,
        CustomResourceDefinitionManifest,
    ],
):
    """General-purpose wrapper around one Kubernetes CRD object."""

    _obj: kube_client.V1CustomResourceDefinition

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
