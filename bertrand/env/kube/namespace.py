"""Wrappers for the Kubernetes Namespace API."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from kubernetes import client as kube_client

from .api.client import Kube
from .api.resource import (
    KubeResource,
    cluster_resource,
)

if TYPE_CHECKING:
    from collections.abc import Mapping

    from bertrand.env.git import Deadline


@cluster_resource(
    api=kube_client.CoreV1Api,
    payload=kube_client.V1Namespace,
    read=kube_client.CoreV1Api.read_namespace,
    list=kube_client.CoreV1Api.list_namespace,
    delete=kube_client.CoreV1Api.delete_namespace,
)
@dataclass(frozen=True)
class Namespace(
    KubeResource[kube_client.V1Namespace],
):
    """General-purpose wrapper around one Kubernetes Namespace object.

    Parameters
    ----------
    _obj : kube_client.V1Namespace
        Typed Kubernetes Namespace payload returned by the cluster API.
    """

    _obj: kube_client.V1Namespace

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
        deadline: Deadline,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> Namespace:
        """Create or patch one Kubernetes Namespace.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Namespace name to create or patch.
        deadline : Deadline
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
        api = kube_client.CoreV1Api(kube.client)
        try:
            payload = await kube.run(
                lambda request_timeout: api.create_namespace(
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                deadline=deadline,
                context=f"failed to create Namespace {name}",
                missing_ok=False,
            )
        except OSError as err:
            if not isinstance(err, Kube.APIError) or err.status != 409:
                raise
            payload = await kube.run(
                lambda request_timeout: api.patch_namespace(
                    name=name,
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                deadline=deadline,
                context=f"failed to patch Namespace {name}",
                missing_ok=False,
            )
        if not isinstance(payload, kube_client.V1Namespace):
            msg = "malformed Kubernetes Namespace payload"
            raise OSError(msg)
        return cls(_obj=payload)

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
