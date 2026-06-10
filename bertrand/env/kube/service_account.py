"""Wrappers for the Kubernetes ServiceAccount API."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from kubernetes import client as kube_client

from .api.metadata import KubeObject
from .api.resource import BuiltinResource

if TYPE_CHECKING:
    from collections.abc import Mapping

    from bertrand.env.git import Deadline

    from .api.client import Kube


@dataclass(frozen=True)
class ServiceAccount(KubeObject[kube_client.V1ServiceAccount]):
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
        return await SERVICE_ACCOUNT_RESOURCE.upsert(
            kube,
            namespace=namespace,
            name=name,
            manifest=manifest,
            deadline=deadline,
        )


SERVICE_ACCOUNT_RESOURCE: BuiltinResource[
    kube_client.V1ServiceAccount,
    ServiceAccount,
] = BuiltinResource(
    scope="namespaced",
    api="core",
    kind="ServiceAccount",
    slug="service_account",
    expected=kube_client.V1ServiceAccount,
    list_type=kube_client.V1ServiceAccountList,
    wrapper=ServiceAccount.from_payload,
    can_create=True,
    can_patch=True,
    can_delete=True,
)
