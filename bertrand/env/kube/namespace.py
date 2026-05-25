"""Wrappers for the Kubernetes Namespace API."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Self

from kubernetes import client as kube_client

from .api.metadata import KubeMetadata
from .api.resource import ClusterMutableResourceMixin, ResourceClient

if TYPE_CHECKING:
    from collections.abc import Mapping

    from .api.client import Kube

NAMESPACE_WAIT_POLL_INTERVAL_SECONDS = 0.5


@dataclass(frozen=True)
class Namespace(
    ClusterMutableResourceMixin[kube_client.V1Namespace],
    KubeMetadata[kube_client.V1Namespace],
):
    """General-purpose wrapper around one Kubernetes Namespace object.

    Parameters
    ----------
    _obj : kube_client.V1Namespace
        Typed Kubernetes Namespace payload returned by the cluster API.
    """

    _obj: kube_client.V1Namespace

    @classmethod
    def _client(cls) -> ResourceClient[kube_client.V1Namespace, Self]:
        return ResourceClient(
            scope="cluster",
            kind="Namespace",
            expected=kube_client.V1Namespace,
            list_type=kube_client.V1NamespaceList,
            wrapper=lambda payload: cls(_obj=payload),
            read=lambda kube, _namespace, name, request_timeout: (
                kube.core.read_namespace(
                    name=name,
                    _request_timeout=request_timeout,
                )
            ),
            list_all=lambda kube, label_selector, field_selector, request_timeout: (
                kube.core.list_namespace(
                    label_selector=label_selector,
                    field_selector=field_selector,
                    _request_timeout=request_timeout,
                )
            ),
            create=lambda kube, _namespace, _name, manifest, request_timeout: (
                kube.core.create_namespace(
                    body=manifest,
                    _request_timeout=request_timeout,
                )
            ),
            patch=lambda kube, _namespace, name, manifest, request_timeout: (
                kube.core.patch_namespace(
                    name=name,
                    body=manifest,
                    _request_timeout=request_timeout,
                )
            ),
            delete=lambda kube, _namespace, name, request_timeout: (
                kube.core.delete_namespace(
                    name=name,
                    _request_timeout=request_timeout,
                )
            ),
        )

    @staticmethod
    def _manifest(
        *,
        name: str,
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
    ) -> dict[str, object]:
        return {
            "apiVersion": "v1",
            "kind": "Namespace",
            "metadata": {
                "name": name,
                "labels": dict(labels or {}),
                "annotations": dict(annotations or {}),
            },
        }

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        name: str,
        timeout: float,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> Self:
        """Create or patch one Kubernetes Namespace.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Namespace name to create or patch.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Labels to apply to `metadata.labels`.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to `metadata.annotations`.

        Returns
        -------
        Namespace
            Wrapped created or patched Namespace.

        Raises
        ------
        OSError
            If the name is empty, or Kubernetes create/patch fails or returns
            malformed data.
        """
        name = name.strip()
        if not name:
            msg = "Namespace upsert requires non-empty name"
            raise OSError(msg)
        manifest = cls._manifest(name=name, labels=labels, annotations=annotations)
        return await cls._client().upsert(
            kube,
            name=name,
            manifest=manifest,
            timeout=timeout,
        )

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
