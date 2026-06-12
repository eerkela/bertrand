"""Wrappers for the Kubernetes ServiceAccount API."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from kubernetes import client as kube_client

from .api.resource import (
    KubeResource,
    namespaced_resource,
)

if TYPE_CHECKING:
    from collections.abc import Mapping


@dataclass(frozen=True)
class ServiceAccountManifest:
    """Desired state for one Kubernetes ServiceAccount."""

    namespace: str
    name: str
    labels: Mapping[str, str] | None = None
    annotations: Mapping[str, str] | None = None

    def manifest(self) -> Mapping[str, object]:
        """Render this desired state as a Kubernetes manifest.

        Returns
        -------
        Mapping[str, object]
            Kubernetes ServiceAccount manifest payload.
        """
        return {
            "apiVersion": "v1",
            "kind": "ServiceAccount",
            "metadata": {
                "name": self.name,
                "namespace": self.namespace,
                "labels": dict(self.labels or {}),
                "annotations": dict(self.annotations or {}),
            },
        }


@namespaced_resource(
    api=kube_client.CoreV1Api,
    payload=kube_client.V1ServiceAccount,
    read=kube_client.CoreV1Api.read_namespaced_service_account,
    list=kube_client.CoreV1Api.list_namespaced_service_account,
    list_all=kube_client.CoreV1Api.list_service_account_for_all_namespaces,
    create=kube_client.CoreV1Api.create_namespaced_service_account,
    patch=kube_client.CoreV1Api.patch_namespaced_service_account,
    delete=kube_client.CoreV1Api.delete_namespaced_service_account,
)
@dataclass(frozen=True)
class ServiceAccount(
    KubeResource[kube_client.V1ServiceAccount, ServiceAccountManifest],
):
    """General-purpose wrapper around one Kubernetes ServiceAccount object.

    Parameters
    ----------
    _obj : kube_client.V1ServiceAccount
        Typed Kubernetes ServiceAccount payload returned by the cluster API.
    """

    _obj: kube_client.V1ServiceAccount
