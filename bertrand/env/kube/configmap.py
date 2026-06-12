"""Wrappers for the Kubernetes ConfigMap API and related operations."""

from __future__ import annotations

from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING, Self

from kubernetes import client as kube_client

from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.api.resource import (
    KubeResource,
    namespaced_resource,
)

if TYPE_CHECKING:
    from collections.abc import Mapping

    from bertrand.env.git import Deadline


@namespaced_resource(
    api=kube_client.CoreV1Api,
    payload=kube_client.V1ConfigMap,
    read=kube_client.CoreV1Api.read_namespaced_config_map,
    list=kube_client.CoreV1Api.list_namespaced_config_map,
    list_all=kube_client.CoreV1Api.list_config_map_for_all_namespaces,
    delete=kube_client.CoreV1Api.delete_namespaced_config_map,
)
@dataclass(frozen=True)
class ConfigMap(
    KubeResource[kube_client.V1ConfigMap],
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
        deadline: Deadline,
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
        deadline : Deadline
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

        api = kube_client.CoreV1Api(kube.client)
        try:
            payload = await kube.run(
                lambda request_timeout: api.create_namespaced_config_map(
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                deadline=deadline,
                context=f"failed to create ConfigMap {namespace}/{name}",
                missing_ok=False,
            )
        except OSError as err:
            if not isinstance(err, Kube.APIError) or err.status != 409:
                raise
            payload = await kube.run(
                lambda request_timeout: api.patch_namespaced_config_map(
                    name=name,
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                deadline=deadline,
                context=f"failed to patch ConfigMap {namespace}/{name}",
                missing_ok=False,
            )
        if not isinstance(payload, kube_client.V1ConfigMap):
            msg = "malformed Kubernetes ConfigMap payload"
            raise OSError(msg)
        return cls(_obj=payload)

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
