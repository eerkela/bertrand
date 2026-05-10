"""Wrappers for the Kubernetes ServiceAccount API."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Self

from kubernetes import client as kube_client

from .api import (
    NamespacedKubeMetadata,
)
from .api._helpers import (
    _create_or_patch,
    _list_namespaced_items,
    _typed_payload,
    _validate_delete_status,
    _wait_until_deleted,
)

if TYPE_CHECKING:
    import builtins
    from collections.abc import Collection, Mapping

    from .api import Kube

SERVICE_ACCOUNT_WAIT_POLL_INTERVAL_SECONDS = 0.5


@dataclass(frozen=True)
class ServiceAccount(NamespacedKubeMetadata[kube_client.V1ServiceAccount]):
    """General-purpose wrapper around one Kubernetes ServiceAccount object.

    Parameters
    ----------
    _obj : kube_client.V1ServiceAccount
        Typed Kubernetes ServiceAccount payload returned by the cluster API.
    """

    _obj: kube_client.V1ServiceAccount

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        timeout: float,
    ) -> Self | None:
        """Read one Kubernetes ServiceAccount by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the ServiceAccount.
        name : str
            ServiceAccount name to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        ServiceAccount | None
            Wrapped Kubernetes ServiceAccount, or `None` if it does not exist.
        """
        payload = await kube.run(
            lambda request_timeout: kube.core.read_namespaced_service_account(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=(
                f"failed to read ServiceAccount {name!r} in namespace {namespace!r}"
            ),
        )
        if payload is None:
            return None
        return cls(
            _obj=_typed_payload(
                payload,
                kube_client.V1ServiceAccount,
                context="ServiceAccount",
            )
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
        """List Kubernetes ServiceAccounts with optional filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        namespaces : Collection[str] | None, optional
            Optional namespace filters. `None` queries all namespaces.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.

        Returns
        -------
        list[ServiceAccount]
            Wrapped ServiceAccounts matching the requested filters.
        """
        return [
            cls(_obj=item)
            for item in await _list_namespaced_items(
                kube,
                timeout=timeout,
                namespaces=namespaces,
                labels=labels,
                list_all=lambda label_selector, request_timeout: (
                    kube.core.list_service_account_for_all_namespaces(
                        label_selector=label_selector,
                        _request_timeout=request_timeout,
                    )
                ),
                list_namespace=lambda namespace, label_selector, request_timeout: (
                    kube.core.list_namespaced_service_account(
                        namespace=namespace,
                        label_selector=label_selector,
                        _request_timeout=request_timeout,
                    )
                ),
                list_type=kube_client.V1ServiceAccountList,
                item_type=kube_client.V1ServiceAccount,
                all_context="failed to list ServiceAccounts across all namespaces",
                namespace_context=lambda namespace: (
                    f"failed to list ServiceAccounts in namespace {namespace!r}"
                ),
                list_context="ServiceAccount",
                item_context="ServiceAccount",
            )
        ]

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
        timeout: float,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> Self:
        """Create or patch one Kubernetes ServiceAccount.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the ServiceAccount.
        name : str
            ServiceAccount name to create or patch.
        timeout : float
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
        payload = await _create_or_patch(
            kube,
            timeout=timeout,
            create=lambda request_timeout: kube.core.create_namespaced_service_account(
                namespace=namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            patch=lambda request_timeout: kube.core.patch_namespaced_service_account(
                name=name,
                namespace=namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            create_context=f"failed to create ServiceAccount {namespace}/{name}",
            patch_context=f"failed to patch ServiceAccount {namespace}/{name}",
            expected=kube_client.V1ServiceAccount,
            payload_context="ServiceAccount",
        )
        return cls(_obj=payload)

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this ServiceAccount by its metadata namespace and name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        ServiceAccount | None
            Fresh wrapper for the same ServiceAccount, or `None` if it no longer
            exists.
        """
        namespace, name = self._require_namespace_name("refresh ServiceAccount")
        return await type(self).get(
            kube,
            namespace=namespace,
            timeout=timeout,
            name=name,
        )

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this ServiceAccount from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        namespace, name = self._require_namespace_name("delete ServiceAccount")
        payload = await kube.run(
            lambda request_timeout: kube.core.delete_namespaced_service_account(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete ServiceAccount {namespace}/{name}",
        )
        _validate_delete_status(
            payload, label=self._object_label(name=name, namespace=namespace)
        )

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this ServiceAccount is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        """
        namespace, name = self._require_namespace_name(
            "wait for ServiceAccount deletion"
        )
        await _wait_until_deleted(
            label=self._object_label(name=name, namespace=namespace),
            timeout=timeout,
            refresh=lambda remaining: self.refresh(kube, timeout=remaining),
        )
