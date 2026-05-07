"""Wrappers for the Kubernetes Secret API and related operations."""

from __future__ import annotations

import base64
import binascii
import builtins
from collections.abc import Collection, Mapping
from dataclasses import dataclass
from types import MappingProxyType
from typing import Self

from kubernetes import client as kube_client

from ..config.core import KubeName
from .api import Kube, _label_selector


@dataclass(frozen=True)
class Secret:
    """General-purpose wrapper around one Kubernetes Secret object."""

    obj: kube_client.V1Secret

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: KubeName,
        timeout: float,
    ) -> Self | None:
        """Read one Kubernetes Secret by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the secret.
        name : KubeName
            Name of the secret to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Secret | None
            Wrapped Kubernetes secret, or `None` when the object does not exist.

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds `timeout`.
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
            context=f"failed to read cluster secret {name!r} in namespace {namespace!r}",
        )
        if payload is None:
            return None
        if not isinstance(payload, kube_client.V1Secret):
            raise OSError(
                f"malformed Kubernetes Secret payload for {name!r} in namespace {namespace!r}"
            )
        return cls(obj=payload)

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
        TimeoutError
            If any Kubernetes request exceeds `timeout`.
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
                    lambda request_timeout, namespace=namespace: kube.core.list_namespaced_secret(
                        namespace=namespace,
                        label_selector=label_selector,
                        _request_timeout=request_timeout,
                    ),
                    timeout=timeout,
                    context=f"failed to list cluster secrets in namespace {namespace!r}",
                )
                if payload is not None:
                    payloads.append(payload)

        out: builtins.list[Self] = []
        for payload in payloads:
            if not isinstance(payload, kube_client.V1SecretList):
                raise OSError("malformed Kubernetes Secret list payload")
            for item in payload.items or []:
                if not isinstance(item, kube_client.V1Secret):
                    raise OSError("malformed Kubernetes Secret entry in list payload")
                out.append(cls(obj=item))
        return out

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: KubeName,
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
        name : KubeName
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
        TimeoutError
            If any Kubernetes request exceeds `timeout`.
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
            if not isinstance(created, kube_client.V1Secret):
                raise OSError(f"malformed Kubernetes Secret payload while creating {name!r}")
            return cls(obj=created)
        except OSError as err:
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise

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
            raise OSError(f"malformed Kubernetes Secret payload while updating {name!r}")
        return cls(obj=updated)

    @property
    def namespace(self) -> str:
        """Return this secret's namespace.

        Returns
        -------
        str
            Trimmed namespace from `metadata`.

        Raises
        ------
        OSError
            If `metadata.namespace` is missing or malformed.
        """
        metadata = self.obj.metadata or kube_client.V1ObjectMeta()
        namespace = (metadata.namespace or "").strip()
        if not namespace:
            raise OSError("secret metadata is missing namespace")
        return namespace

    @property
    def name(self) -> KubeName:
        """Return this secret's name.

        Returns
        -------
        KubeName
            Trimmed name from `metadata`.

        Raises
        ------
        OSError
            If `metadata.name` is missing or malformed.
        """
        metadata = self.obj.metadata or kube_client.V1ObjectMeta()
        name = (metadata.name or "").strip()
        if not name:
            raise OSError("secret metadata is missing name")
        return name

    @property
    def labels(self) -> Mapping[str, str]:
        """Return this secret's labels.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.labels`, or an empty mapping when unavailable.
        """
        metadata = self.obj.metadata
        if metadata is None or metadata.labels is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.labels)

    @property
    def annotations(self) -> Mapping[str, str]:
        """Return this secret's annotations.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.annotations`, or an empty mapping when
            unavailable.
        """
        metadata = self.obj.metadata
        if metadata is None or metadata.annotations is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.annotations)

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
        value = (self.obj.data or {}).get("value")
        if value is None:
            raise OSError(f"cluster secret {name!r} does not define required key 'data.value'")
        try:
            return base64.b64decode(value, validate=True)
        except (binascii.Error, ValueError) as err:
            raise OSError(
                f"cluster secret {name!r} contains invalid base64 data for key 'data.value'"
            ) from err

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this secret by identity.

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

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds `timeout`.
        OSError
            If identity is malformed, API request fails, or payload is malformed.
        """
        return await type(self).get(
            kube=kube,
            namespace=self.namespace,
            timeout=timeout,
            name=self.name,
        )

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this secret from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds `timeout`.
        OSError
            If identity is malformed or Kubernetes delete fails.
        """
        name = self.name
        await kube.run(
            lambda request_timeout: kube.core.delete_namespaced_secret(
                name=name,
                namespace=self.namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete cluster secret {name!r}",
        )
