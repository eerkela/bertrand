"""Wrappers for the Kubernetes ConfigMap API and related operations."""

from __future__ import annotations

from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING

from kubernetes import client as kube_client

from bertrand.env.kube.api.resource import (
    KubeResource,
    namespaced_resource,
)

if TYPE_CHECKING:
    from collections.abc import Mapping


@dataclass(frozen=True)
class ConfigMapManifest:
    """Desired state for one Kubernetes ConfigMap."""

    namespace: str
    name: str
    data: Mapping[str, str]
    binary_data: Mapping[str, str] | None = None
    labels: Mapping[str, str] | None = None
    annotations: Mapping[str, str] | None = None

    def manifest(self) -> Mapping[str, object]:
        """Render this desired state as a Kubernetes manifest.

        Returns
        -------
        Mapping[str, object]
            Kubernetes ConfigMap manifest payload.
        """
        manifest: dict[str, object] = {
            "apiVersion": "v1",
            "kind": "ConfigMap",
            "metadata": {
                "name": self.name,
                "namespace": self.namespace,
                "labels": dict(self.labels or {}),
                "annotations": dict(self.annotations or {}),
            },
            "data": dict(self.data),
        }
        if self.binary_data is not None:
            manifest["binaryData"] = dict(self.binary_data)
        return manifest


@namespaced_resource(
    api=kube_client.CoreV1Api,
    read=kube_client.CoreV1Api.read_namespaced_config_map,
    list=kube_client.CoreV1Api.list_namespaced_config_map,
    list_all=kube_client.CoreV1Api.list_config_map_for_all_namespaces,
    create=kube_client.CoreV1Api.create_namespaced_config_map,
    patch=kube_client.CoreV1Api.patch_namespaced_config_map,
    delete=kube_client.CoreV1Api.delete_namespaced_config_map,
)
@dataclass(frozen=True)
class ConfigMap(
    KubeResource[kube_client.V1ConfigMap, ConfigMapManifest],
):
    """General-purpose wrapper around one Kubernetes ConfigMap object.

    Parameters
    ----------
    _obj : kubernetes.client.V1ConfigMap
        Typed Kubernetes ConfigMap payload returned by the cluster API.

    Notes
    -----
    The convergence API accepts intent-level fields and keeps raw Kubernetes
    manifests as an internal implementation detail.
    """

    _obj: kube_client.V1ConfigMap

    @property
    def data(self) -> Mapping[str, str]:
        """Return this ConfigMap's text data.

        Returns
        -------
        Mapping[str, str]
            Read-only view of ConfigMap text data.
        """
        return MappingProxyType(self._obj.data or {})

    @property
    def binary_data(self) -> Mapping[str, str]:
        """Return this ConfigMap's binary data.

        Returns
        -------
        Mapping[str, str]
            Read-only view of ConfigMap binary data.
        """
        return MappingProxyType(self._obj.binary_data or {})
