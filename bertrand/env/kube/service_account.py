"""Wrappers for the Kubernetes ServiceAccount API."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from kubernetes import client as kube_client

from .api.client import Kube
from .api.resource import (
    KubeResource,
    namespaced_resource,
)

if TYPE_CHECKING:
    from collections.abc import Mapping

    from bertrand.env.git import Deadline


@namespaced_resource(
    api=kube_client.CoreV1Api,
    payload=kube_client.V1ServiceAccount,
    read=kube_client.CoreV1Api.read_namespaced_service_account,
    list=kube_client.CoreV1Api.list_namespaced_service_account,
    list_all=kube_client.CoreV1Api.list_service_account_for_all_namespaces,
    delete=kube_client.CoreV1Api.delete_namespaced_service_account,
)
@dataclass(frozen=True)
class ServiceAccount(
    KubeResource[kube_client.V1ServiceAccount],
):
    """General-purpose wrapper around one Kubernetes ServiceAccount object.

    Parameters
    ----------
    _obj : kube_client.V1ServiceAccount
        Typed Kubernetes ServiceAccount payload returned by the cluster API.
    """

    _obj: kube_client.V1ServiceAccount

    @staticmethod
    def _manifest(
        *,
        namespace: str,
        name: str,
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
    ) -> dict[str, object]:
        return {
            "apiVersion": "v1",
            "kind": "ServiceAccount",
            "metadata": {
                "name": name,
                "namespace": namespace,
                "labels": dict(labels or {}),
                "annotations": dict(annotations or {}),
            },
        }

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        deadline: Deadline,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> ServiceAccount:
        """Create or patch one Kubernetes ServiceAccount.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the ServiceAccount.
        name : str
            ServiceAccount name to create or patch.
        deadline : Deadline
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Labels to apply to `metadata.labels`.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to `metadata.annotations`.

        Returns
        -------
        ServiceAccount
            Wrapped created or patched ServiceAccount.

        Raises
        ------
        OSError
            If Kubernetes create/patch fails or returns malformed data.
        """
        namespace = namespace.strip()
        name = name.strip()
        if not namespace or not name:
            msg = "ServiceAccount upsert requires non-empty namespace and name"
            raise OSError(msg)
        manifest = cls._manifest(
            namespace=namespace,
            name=name,
            labels=labels,
            annotations=annotations,
        )
        api = kube_client.CoreV1Api(kube.client)
        try:
            payload = await kube.run(
                lambda request_timeout: api.create_namespaced_service_account(
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                deadline=deadline,
                context=f"failed to create ServiceAccount {namespace}/{name}",
                missing_ok=False,
            )
        except OSError as err:
            if not isinstance(err, Kube.APIError) or err.status != 409:
                raise
            payload = await kube.run(
                lambda request_timeout: api.patch_namespaced_service_account(
                    name=name,
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                deadline=deadline,
                context=f"failed to patch ServiceAccount {namespace}/{name}",
                missing_ok=False,
            )
        if not isinstance(payload, kube_client.V1ServiceAccount):
            msg = "malformed Kubernetes ServiceAccount payload"
            raise OSError(msg)
        return cls(_obj=payload)
