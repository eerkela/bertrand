"""Wrappers for the Kubernetes Namespace API."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Self

from kubernetes import client as kube_client

from .api import (
    KubeMetadata,
)
from .api._helpers import (
    _create_or_patch,
    _list_cluster_items,
    _typed_payload,
    _validate_delete_status,
    _wait_until_deleted,
)

if TYPE_CHECKING:
    import builtins
    from collections.abc import Mapping

    from .api import Kube

NAMESPACE_WAIT_POLL_INTERVAL_SECONDS = 0.5


@dataclass(frozen=True)
class Namespace(KubeMetadata[kube_client.V1Namespace]):
    """General-purpose wrapper around one Kubernetes Namespace object.

    Parameters
    ----------
    _obj : kube_client.V1Namespace
        Typed Kubernetes Namespace payload returned by the cluster API.
    """

    _obj: kube_client.V1Namespace

    @classmethod
    async def get(cls, kube: Kube, *, name: str, timeout: float) -> Self | None:
        """Read one Kubernetes Namespace by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Namespace name to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Namespace | None
            Wrapped Kubernetes Namespace, or `None` if it does not exist.

        """
        payload = await kube.run(
            lambda request_timeout: kube.core.read_namespace(
                name=name,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to read Namespace {name!r}",
        )
        if payload is None:
            return None
        return cls(
            _obj=_typed_payload(payload, kube_client.V1Namespace, context="Namespace")
        )

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[Self]:
        """List Kubernetes Namespaces with optional label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.

        Returns
        -------
        list[Namespace]
            Wrapped Namespaces matching the requested filters.

        """
        return [
            cls(_obj=item)
            for item in await _list_cluster_items(
                kube,
                timeout=timeout,
                labels=labels,
                list_items=lambda label_selector, request_timeout: (
                    kube.core.list_namespace(
                        label_selector=label_selector,
                        _request_timeout=request_timeout,
                    )
                ),
                list_type=kube_client.V1NamespaceList,
                item_type=kube_client.V1Namespace,
                context="failed to list Namespaces",
                list_context="Namespace",
                item_context="Namespace",
            )
        ]

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
        payload = await _create_or_patch(
            kube,
            timeout=timeout,
            create=lambda request_timeout: kube.core.create_namespace(
                body=manifest,
                _request_timeout=request_timeout,
            ),
            patch=lambda request_timeout: kube.core.patch_namespace(
                name=name,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            create_context=f"failed to create Namespace {name}",
            patch_context=f"failed to patch Namespace {name}",
            expected=kube_client.V1Namespace,
            payload_context="Namespace",
        )
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

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this Namespace by its metadata name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Namespace | None
            Fresh wrapper for the same Namespace, or `None` if it no longer exists.
        """
        name = self._require_name("refresh Namespace")
        return await type(self).get(kube, name=name, timeout=timeout)

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this Namespace from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        name = self._require_name("delete Namespace")
        payload = await kube.run(
            lambda request_timeout: kube.core.delete_namespace(
                name=name,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete Namespace {name}",
        )
        _validate_delete_status(payload, label=self._object_label(name))

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this Namespace is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        """
        name = self._require_name("wait for Namespace deletion")
        await _wait_until_deleted(
            label=self._object_label(name),
            timeout=timeout,
            refresh=lambda remaining: self.refresh(kube, timeout=remaining),
        )
