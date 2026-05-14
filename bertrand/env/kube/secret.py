"""Wrappers for the Kubernetes Secret API and related operations."""

from __future__ import annotations

import base64
import binascii
from dataclasses import dataclass
from typing import TYPE_CHECKING, Self

from kubernetes import client as kube_client

from .api.metadata import NamespacedKubeMetadata
from .api.resource import ResourceClient

if TYPE_CHECKING:
    import builtins
    from collections.abc import Collection, Mapping

    from .api.client import Kube


@dataclass(frozen=True)
class Secret(NamespacedKubeMetadata[kube_client.V1Secret]):
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
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        timeout: float,
    ) -> Self | None:
        """Read one Kubernetes Secret by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the secret.
        name : str
            Name of the secret to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Secret | None
            Wrapped Kubernetes secret, or `None` when the object does not exist.
        """
        return await cls._client().get(
            kube,
            namespace=namespace,
            name=name,
            timeout=timeout,
        )

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        namespaces: Collection[str] | None = None,
        labels: Mapping[str, str] | None = None,
        timeout: float,
    ) -> builtins.list[Self]:
        """List Kubernetes Secrets with optional namespace and label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespaces : Collection[str] | None, optional
            Optional namespace selector. If `None`, query all namespaces. Otherwise,
            entries are trimmed, deduplicated, and queried individually.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        builtins.list[Secret]
            Validated secret wrappers matching the requested filters.
        """
        return await cls._client().list(
            kube,
            timeout=timeout,
            namespaces=namespaces,
            labels=labels,
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

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this secret by its metadata namespace and name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Secret | None
            Latest secret wrapper if it still exists, otherwise `None`.
        """
        namespace, name = self._require_namespace_name("refresh secret")
        return await type(self).get(
            kube,
            namespace=namespace,
            timeout=timeout,
            name=name,
        )

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this secret from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        namespace, name = self._require_namespace_name("delete secret")
        await (
            type(self)
            ._client()
            .delete_by_name(
                kube,
                namespace=namespace,
                name=name,
                timeout=timeout,
            )
        )

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this secret is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        """
        namespace, name = self._require_namespace_name("wait for secret deletion")
        await (
            type(self)
            ._client()
            .wait_deleted(
                label=self._object_label(name=name, namespace=namespace),
                timeout=timeout,
                refresh=lambda remaining: self.refresh(kube, timeout=remaining),
            )
        )
