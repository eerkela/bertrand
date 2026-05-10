"""Wrappers for the Kubernetes Secret API and related operations."""

from __future__ import annotations

import base64
import binascii
from dataclasses import dataclass
from typing import TYPE_CHECKING, Self

from kubernetes import client as kube_client

from .api import (
    Kube,
    NamespacedKubeMetadata,
    _label_selector,
    _validate_delete_status,
    _wait_until_deleted,
)

if TYPE_CHECKING:
    import builtins
    from collections.abc import Collection, Mapping


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

        Raises
        ------
        OSError
            If the Kubernetes API call fails or returns malformed data.
        """
        payload = await kube.run(
            lambda request_timeout: kube.core.read_namespaced_secret(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=(
                f"failed to read cluster secret {name!r} in namespace {namespace!r}"
            ),
        )
        if payload is None:
            return None
        if not isinstance(payload, kube_client.V1Secret):
            msg = (
                f"malformed Kubernetes Secret payload for {name!r} in namespace "
                f"{namespace!r}"
            )
            raise OSError(msg)
        return cls(_obj=payload)

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

        Raises
        ------
        OSError
            If a Kubernetes API call fails or returns malformed data.
        """
        label_selector = _label_selector(labels)
        payloads: builtins.list[kube_client.V1SecretList] = []

        # search all namespaces
        if namespaces is None:
            payload = await kube.run(
                lambda request_timeout: kube.core.list_secret_for_all_namespaces(
                    label_selector=label_selector,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context="failed to list cluster secrets across all namespaces",
            )
            if payload is not None:
                payloads.append(payload)

        # search specific namespaces
        else:
            normalized = {namespace.strip() for namespace in namespaces}
            normalized.discard("")
            if not normalized:
                return []
            for namespace in sorted(normalized):
                payload = await kube.run(
                    lambda request_timeout, namespace=namespace: (
                        kube.core.list_namespaced_secret(
                            namespace=namespace,
                            label_selector=label_selector,
                            _request_timeout=request_timeout,
                        )
                    ),
                    timeout=timeout,
                    context=(
                        f"failed to list cluster secrets in namespace {namespace!r}"
                    ),
                )
                if payload is not None:
                    payloads.append(payload)

        out: builtins.list[Self] = []
        for payload in payloads:
            if not isinstance(payload, kube_client.V1SecretList):
                msg = "malformed Kubernetes Secret list payload"
                raise OSError(msg)
            for item in payload.items or []:
                if not isinstance(item, kube_client.V1Secret):
                    msg = "malformed Kubernetes Secret entry in list payload"
                    raise OSError(msg)
                out.append(cls(_obj=item))
        return out

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

        Raises
        ------
        OSError
            If Kubernetes create/patch fails or returns malformed data.
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

        try:
            created = await kube.run(
                lambda request_timeout: kube.core.create_namespaced_secret(
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=f"failed to create cluster secret {name!r}",
            )
        except OSError as err:
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise
        else:
            if not isinstance(created, kube_client.V1Secret):
                msg = f"malformed Kubernetes Secret payload while creating {name!r}"
                raise OSError(msg)
            return cls(_obj=created)

        updated = await kube.run(
            lambda request_timeout: kube.core.patch_namespaced_secret(
                name=name,
                namespace=namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to update cluster secret {name!r}",
        )
        if not isinstance(updated, kube_client.V1Secret):
            msg = f"malformed Kubernetes Secret payload while updating {name!r}"
            raise OSError(msg)
        return cls(_obj=updated)

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
        payload = await kube.run(
            lambda request_timeout: kube.core.delete_namespaced_secret(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete cluster secret {namespace}/{name}",
        )
        _validate_delete_status(
            payload, label=self._object_label(name=name, namespace=namespace)
        )

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this secret is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        Raises
        ------
        TimeoutError
            If the secret still exists when `timeout` expires.
        """
        namespace, name = self._require_namespace_name("wait for secret deletion")
        try:
            await _wait_until_deleted(
                label=self._object_label(name=name, namespace=namespace),
                timeout=timeout,
                refresh=lambda remaining: self.refresh(kube, timeout=remaining),
            )
        except TimeoutError as err:
            raise TimeoutError(str(err)) from err
