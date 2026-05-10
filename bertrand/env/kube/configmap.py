"""Wrappers for the Kubernetes ConfigMap API and related operations."""

from __future__ import annotations

from dataclasses import dataclass
from types import MappingProxyType
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
class ConfigMap(NamespacedKubeMetadata[kube_client.V1ConfigMap]):
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

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        timeout: float,
        name: str,
    ) -> Self | None:
        """Read one Kubernetes ConfigMap by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the ConfigMap.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        name : str
            ConfigMap name to read.

        Returns
        -------
        ConfigMap | None
            Wrapped Kubernetes ConfigMap, or `None` if it does not exist.

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or the API call fails.
        """
        payload = await kube.run(
            lambda request_timeout: kube.core.read_namespaced_config_map(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=(
                f"failed to read cluster ConfigMap {name!r} in namespace {namespace!r}"
            ),
        )
        if payload is None:
            return None
        if not isinstance(payload, kube_client.V1ConfigMap):
            msg = (
                f"malformed Kubernetes ConfigMap payload for {name!r} "
                f"in namespace {namespace!r}"
            )
            raise OSError(msg)
        return cls(_obj=payload)

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        timeout: float,
        namespaces: Collection[str] | None = None,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[Self]:
        """List Kubernetes ConfigMaps with optional namespace and label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        namespaces : Collection[str] | None, optional
            Optional namespace filters. `None` queries all namespaces. Otherwise,
            entries are trimmed, deduplicated, and queried individually.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.

        Returns
        -------
        list[ConfigMap]
            Wrapped Kubernetes ConfigMaps matching the requested filters.

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or a list call fails.
        """
        label_selector = _label_selector(labels)
        payloads: builtins.list[kube_client.V1ConfigMapList] = []

        if namespaces is None:
            payload = await kube.run(
                lambda request_timeout: kube.core.list_config_map_for_all_namespaces(
                    label_selector=label_selector,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context="failed to list cluster ConfigMaps across all namespaces",
            )
            if payload is not None:
                payloads.append(payload)
        else:
            normalized = {namespace.strip() for namespace in namespaces}
            normalized.discard("")
            if not normalized:
                return []
            for namespace in sorted(normalized):
                payload = await kube.run(
                    lambda request_timeout, namespace=namespace: (
                        kube.core.list_namespaced_config_map(
                            namespace=namespace,
                            label_selector=label_selector,
                            _request_timeout=request_timeout,
                        )
                    ),
                    timeout=timeout,
                    context=(
                        f"failed to list cluster ConfigMaps in namespace {namespace!r}"
                    ),
                )
                if payload is not None:
                    payloads.append(payload)

        out: builtins.list[Self] = []
        for payload in payloads:
            if not isinstance(payload, kube_client.V1ConfigMapList):
                msg = "malformed Kubernetes ConfigMap list payload"
                raise OSError(msg)
            for item in payload.items or []:
                if not isinstance(item, kube_client.V1ConfigMap):
                    msg = "malformed Kubernetes ConfigMap entry in list payload"
                    raise OSError(msg)
                out.append(cls(_obj=item))
        return out

    @staticmethod
    def _manifest(
        *,
        namespace: str,
        name: str,
        data: Mapping[str, str],
        binary_data: Mapping[str, str] | None,
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
    ) -> dict[str, object]:
        manifest: dict[str, object] = {
            "apiVersion": "v1",
            "kind": "ConfigMap",
            "metadata": {
                "name": name,
                "namespace": namespace,
                "labels": dict(labels or {}),
                "annotations": dict(annotations or {}),
            },
            "data": dict(data),
        }
        if binary_data is not None:
            manifest["binaryData"] = dict(binary_data)
        return manifest

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        data: Mapping[str, str],
        timeout: float,
        binary_data: Mapping[str, str] | None = None,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> Self:
        """Create or patch one Kubernetes ConfigMap from intent-level fields.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the ConfigMap.
        name : str
            ConfigMap name to create or patch.
        data : Mapping[str, str]
            Text data to apply to `data`.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        binary_data : Mapping[str, str] | None, optional
            Base64-encoded binary payloads to apply to `binaryData`.
        labels : Mapping[str, str] | None, optional
            Labels to apply to `metadata.labels`.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to `metadata.annotations`.

        Returns
        -------
        ConfigMap
            Wrapped created or patched ConfigMap.

        Raises
        ------
        OSError
            If namespace/name are empty, or Kubernetes create/patch fails or returns
            malformed data.
        """
        namespace = namespace.strip()
        name = name.strip()
        if not namespace or not name:
            msg = "ConfigMap upsert requires non-empty namespace and name"
            raise OSError(msg)
        manifest = cls._manifest(
            namespace=namespace,
            name=name,
            data=data,
            binary_data=binary_data,
            labels=labels,
            annotations=annotations,
        )

        try:
            created = await kube.run(
                lambda request_timeout: kube.core.create_namespaced_config_map(
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=f"failed to create cluster ConfigMap {name!r}",
            )
        except OSError as err:
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise
        else:
            if not isinstance(created, kube_client.V1ConfigMap):
                msg = f"malformed Kubernetes ConfigMap payload while creating {name!r}"
                raise OSError(msg)
            return cls(_obj=created)

        updated = await kube.run(
            lambda request_timeout: kube.core.patch_namespaced_config_map(
                name=name,
                namespace=namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to update cluster ConfigMap {name!r}",
        )
        if not isinstance(updated, kube_client.V1ConfigMap):
            msg = f"malformed Kubernetes ConfigMap payload while updating {name!r}"
            raise OSError(msg)
        return cls(_obj=updated)

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

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this ConfigMap by its metadata namespace and name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        ConfigMap | None
            Fresh wrapper for the same ConfigMap, or `None` if it no longer exists.
        """
        namespace, name = self._require_namespace_name("refresh ConfigMap")
        return await type(self).get(
            kube,
            namespace=namespace,
            timeout=timeout,
            name=name,
        )

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this ConfigMap from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        namespace, name = self._require_namespace_name("delete ConfigMap")
        payload = await kube.run(
            lambda request_timeout: kube.core.delete_namespaced_config_map(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete cluster ConfigMap {namespace}/{name}",
        )
        _validate_delete_status(
            payload, label=self._object_label(name=name, namespace=namespace)
        )

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this ConfigMap is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        Raises
        ------
        TimeoutError
            If the ConfigMap still exists when `timeout` expires.
        """
        namespace, name = self._require_namespace_name("wait for ConfigMap deletion")
        try:
            await _wait_until_deleted(
                label=self._object_label(name=name, namespace=namespace),
                timeout=timeout,
                refresh=lambda remaining: self.refresh(kube, timeout=remaining),
            )
        except TimeoutError as err:
            raise TimeoutError(str(err)) from err
