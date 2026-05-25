"""Wrappers for the Kubernetes Secret API and related operations."""

from __future__ import annotations

import base64
import binascii
from dataclasses import dataclass
from typing import TYPE_CHECKING, Self

from kubernetes import client as kube_client

from .api.metadata import NamespacedKubeMetadata
from .api.resource import NamespacedMutableResourceMixin, ResourceClient

if TYPE_CHECKING:
    from collections.abc import Mapping

    from .api.client import Kube


@dataclass(frozen=True)
class Secret(
    NamespacedMutableResourceMixin[kube_client.V1Secret],
    NamespacedKubeMetadata[kube_client.V1Secret],
):
    """General-purpose wrapper around one Kubernetes Secret object.

    Parameters
    ----------
    _obj : kube_client.V1Secret
        Typed Kubernetes Secret payload returned by the cluster API.
    """

    _obj: kube_client.V1Secret

    @classmethod
    def _client(cls) -> ResourceClient[kube_client.V1Secret, Self]:
        return ResourceClient(
            scope="namespaced",
            kind="Secret",
            expected=kube_client.V1Secret,
            list_type=kube_client.V1SecretList,
            wrapper=lambda payload: cls(_obj=payload),
            read=lambda kube, namespace, name, request_timeout: (
                kube.core.read_namespaced_secret(
                    name=name,
                    namespace=namespace,
                    _request_timeout=request_timeout,
                )
            ),
            list_all=lambda kube, label_selector, field_selector, request_timeout: (
                kube.core.list_secret_for_all_namespaces(
                    label_selector=label_selector,
                    field_selector=field_selector,
                    _request_timeout=request_timeout,
                )
            ),
            list_namespace=lambda kube, namespace, labels, fields, timeout: (
                kube.core.list_namespaced_secret(
                    namespace=namespace,
                    label_selector=labels,
                    field_selector=fields,
                    _request_timeout=timeout,
                )
            ),
            create=lambda kube, namespace, _name, manifest, request_timeout: (
                kube.core.create_namespaced_secret(
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                )
            ),
            patch=lambda kube, namespace, name, manifest, request_timeout: (
                kube.core.patch_namespaced_secret(
                    name=name,
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                )
            ),
            delete=lambda kube, namespace, name, request_timeout: (
                kube.core.delete_namespaced_secret(
                    name=name,
                    namespace=namespace,
                    _request_timeout=request_timeout,
                )
            ),
        )

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
        payload: bytes,
        timeout: float,
    ) -> Self:
        """Create or patch one Kubernetes Secret payload.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the secret.
        name : str
            Secret name to create or patch.
        labels : Mapping[str, str] | None
            Labels to apply to `metadata.labels`.
        annotations : Mapping[str, str] | None
            Annotations to apply to `metadata.annotations`.
        payload : bytes
            Raw payload bytes to store at `data.value` (base64-encoded on write).
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Secret
            Wrapped created or patched Kubernetes secret.
        """
        manifest = {
            "apiVersion": "v1",
            "kind": "Secret",
            "metadata": {
                "name": name,
                "namespace": namespace,
                "labels": dict(labels or {}),
                "annotations": dict(annotations or {}),
            },
            "type": "Opaque",
            "data": {"value": base64.b64encode(payload).decode("ascii")},
        }

        return await cls._client().upsert(
            kube,
            namespace=namespace,
            name=name,
            manifest=manifest,
            timeout=timeout,
        )

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
