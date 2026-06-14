"""Wrappers for the Kubernetes Namespace API."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from kubernetes import client as kube_client

from .api.resource import (
    KubeResource,
    cluster_resource,
)

if TYPE_CHECKING:
    from collections.abc import Mapping


@dataclass(frozen=True)
class NamespaceManifest:
    """Desired state for one Kubernetes Namespace."""

    name: str
    labels: Mapping[str, str] | None = None
    annotations: Mapping[str, str] | None = None

    @property
    def namespace(self) -> None:
        """Return no namespace for this cluster-scoped resource."""
        return None

    def manifest(self) -> Mapping[str, object]:
        """Render this desired state as a Kubernetes manifest.

        Returns
        -------
        Mapping[str, object]
            Kubernetes Namespace manifest payload.
        """
        return {
            "apiVersion": "v1",
            "kind": "Namespace",
            "metadata": {
                "name": self.name,
                "labels": dict(self.labels or {}),
                "annotations": dict(self.annotations or {}),
            },
        }


@cluster_resource(
    api=kube_client.CoreV1Api,
    read=kube_client.CoreV1Api.read_namespace,
    list=kube_client.CoreV1Api.list_namespace,
    create=kube_client.CoreV1Api.create_namespace,
    patch=kube_client.CoreV1Api.patch_namespace,
    delete=kube_client.CoreV1Api.delete_namespace,
)
@dataclass(frozen=True)
class Namespace(
    KubeResource[kube_client.V1Namespace, NamespaceManifest],
):
    """General-purpose wrapper around one Kubernetes Namespace object.

    Parameters
    ----------
    _obj : kube_client.V1Namespace
        Typed Kubernetes Namespace payload returned by the cluster API.
    """

    _obj: kube_client.V1Namespace

    @property
    def phase(self) -> str:
        """Return the Namespace lifecycle phase.

        Returns
        -------
        str
            Namespace `status.phase`, or an empty string when unavailable.
        """
        status = self._obj.status
        return (status.phase or "").strip() if status is not None else ""
