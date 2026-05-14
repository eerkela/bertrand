"""Wrappers for the Kubernetes ConfigMap API and related operations."""

from __future__ import annotations

from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING, Self

from kubernetes import client as kube_client

from .api.metadata import NamespacedKubeMetadata
from .api.resource import ResourceClient

if TYPE_CHECKING:
    import builtins
    from collections.abc import Collection, Mapping

    from .api.client import Kube


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
    def _client(cls) -> ResourceClient[kube_client.V1ConfigMap, Self]:
        return ResourceClient(
            scope="namespaced",
            kind="ConfigMap",
            expected=kube_client.V1ConfigMap,
            list_type=kube_client.V1ConfigMapList,
            wrapper=lambda payload: cls(_obj=payload),
            read=lambda kube, namespace, name, request_timeout: (
                kube.core.read_namespaced_config_map(
                    name=name,
                    namespace=namespace,
                    _request_timeout=request_timeout,
                )
            ),
            list_all=lambda kube, label_selector, field_selector, request_timeout: (
                kube.core.list_config_map_for_all_namespaces(
                    label_selector=label_selector,
                    field_selector=field_selector,
                    _request_timeout=request_timeout,
                )
            ),
            list_namespace=lambda kube, namespace, labels, fields, timeout: (
                kube.core.list_namespaced_config_map(
                    namespace=namespace,
                    label_selector=labels,
                    field_selector=fields,
                    _request_timeout=timeout,
                )
            ),
            create=lambda kube, namespace, _name, manifest, request_timeout: (
                kube.core.create_namespaced_config_map(
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                )
            ),
            patch=lambda kube, namespace, name, manifest, request_timeout: (
                kube.core.patch_namespaced_config_map(
                    name=name,
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                )
            ),
            delete=lambda kube, namespace, name, request_timeout: (
                kube.core.delete_namespaced_config_map(
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
        """
        return await cls._client().list(
            kube,
            timeout=timeout,
            namespaces=namespaces,
            labels=labels,
        )

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

        return await cls._client().upsert(
            kube,
            namespace=namespace,
            name=name,
            manifest=manifest,
            timeout=timeout,
        )

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
        """Wait until this ConfigMap is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        """
        namespace, name = self._require_namespace_name("wait for ConfigMap deletion")
        await (
            type(self)
            ._client()
            .wait_deleted(
                label=self._object_label(name=name, namespace=namespace),
                timeout=timeout,
                refresh=lambda remaining: self.refresh(kube, timeout=remaining),
            )
        )
