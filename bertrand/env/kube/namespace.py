"""Wrappers for the Kubernetes Namespace API."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, ClassVar, Self

from kubernetes import client as kube_client

from .api.metadata import KubeMetadata
from .api.resource import BuiltinResource, BuiltinResourceObject

if TYPE_CHECKING:
    from collections.abc import Mapping

    from .api.client import Kube

NAMESPACE_WAIT_POLL_INTERVAL_SECONDS = 0.5


@dataclass(frozen=True)
class Namespace(
    BuiltinResourceObject[kube_client.V1Namespace],
    KubeMetadata[kube_client.V1Namespace],
):
    """General-purpose wrapper around one Kubernetes Namespace object.

    Parameters
    ----------
    _obj : kube_client.V1Namespace
        Typed Kubernetes Namespace payload returned by the cluster API.
    """

    _obj: kube_client.V1Namespace

    resource: ClassVar[BuiltinResource[kube_client.V1Namespace]] = (
        BuiltinResource.cluster(
            api="core",
            kind="Namespace",
            slug="namespace",
            expected=kube_client.V1Namespace,
            list_type=kube_client.V1NamespaceList,
            create=True,
            patch=True,
            delete=True,
        )
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
        return await cls.resource.upsert(
            kube,
            owner=cls,
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
