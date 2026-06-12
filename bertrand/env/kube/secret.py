"""Wrappers for the Kubernetes Secret API and related operations."""

from __future__ import annotations

import base64
import binascii
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
class SecretManifest:
    """Desired state for one opaque Kubernetes Secret."""

    namespace: str
    name: str
    payload: bytes
    labels: Mapping[str, str] | None = None
    annotations: Mapping[str, str] | None = None

    def manifest(self) -> Mapping[str, object]:
        """Render this desired state as a Kubernetes manifest.

        Returns
        -------
        Mapping[str, object]
            Kubernetes Secret manifest payload.
        """
        return {
            "apiVersion": "v1",
            "kind": "Secret",
            "metadata": {
                "name": self.name,
                "namespace": self.namespace,
                "labels": dict(self.labels or {}),
                "annotations": dict(self.annotations or {}),
            },
            "type": "Opaque",
            "data": {"value": base64.b64encode(self.payload).decode("ascii")},
        }


@namespaced_resource(
    api=kube_client.CoreV1Api,
    payload=kube_client.V1Secret,
    read=kube_client.CoreV1Api.read_namespaced_secret,
    list=kube_client.CoreV1Api.list_namespaced_secret,
    list_all=kube_client.CoreV1Api.list_secret_for_all_namespaces,
    create=kube_client.CoreV1Api.create_namespaced_secret,
    patch=kube_client.CoreV1Api.patch_namespaced_secret,
    delete=kube_client.CoreV1Api.delete_namespaced_secret,
)
@dataclass(frozen=True)
class Secret(
    KubeResource[kube_client.V1Secret, SecretManifest],
):
    """General-purpose wrapper around one Kubernetes Secret object.

    Parameters
    ----------
    _obj : kube_client.V1Secret
        Typed Kubernetes Secret payload returned by the cluster API.
    """

    _obj: kube_client.V1Secret

    @property
    def value(self) -> bytes:
        """Decode the binary payload stored in the secret's `value`.

        Returns
        -------
        bytes
            Decoded payload bytes.

        Raises
        ------
        OSError
            If the secret's `value` is missing or contains invalid base64.
        """
        name = self.name
        value = (self._obj.data or {}).get("value")
        if value is None:
            msg = f"cluster secret {name!r} does not define required key 'data.value'"
            raise OSError(msg)
        try:
            return base64.b64decode(value, validate=True)
        except (binascii.Error, ValueError) as err:
            msg = (
                f"cluster secret {name!r} contains invalid base64 data for key "
                "'data.value'"
            )
            raise OSError(msg) from err
