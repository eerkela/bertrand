"""Wrappers for the Kubernetes Namespace API."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING, Self

from kubernetes import client as kube_client

from .api import _label_selector

if TYPE_CHECKING:
    import builtins
    from collections.abc import Mapping

    from .api import Kube

NAMESPACE_WAIT_POLL_INTERVAL_SECONDS = 0.5


@dataclass(frozen=True)
class Namespace:
    """General-purpose wrapper around one Kubernetes Namespace object.

    Parameters
    ----------
    obj : kube_client.V1Namespace
        Typed Kubernetes Namespace payload returned by the cluster API.
    """

    obj: kube_client.V1Namespace

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

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or the API call fails.
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
        if not isinstance(payload, kube_client.V1Namespace):
            msg = f"malformed Kubernetes Namespace payload for {name!r}"
            raise OSError(msg)
        return cls(obj=payload)

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

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or a list call fails.
        """
        payload = await kube.run(
            lambda request_timeout: kube.core.list_namespace(
                label_selector=_label_selector(labels),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context="failed to list Namespaces",
        )
        if payload is None:
            return []
        if not isinstance(payload, kube_client.V1NamespaceList):
            msg = "malformed Kubernetes Namespace list payload"
            raise OSError(msg)
        out: builtins.list[Self] = []
        for item in payload.items or []:
            if not isinstance(item, kube_client.V1Namespace):
                msg = "malformed Kubernetes Namespace entry in list payload"
                raise OSError(msg)
            out.append(cls(obj=item))
        return out

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
        try:
            created = await kube.run(
                lambda request_timeout: kube.core.create_namespace(
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=f"failed to create Namespace {name}",
            )
        except OSError as err:
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise
        else:
            if not isinstance(created, kube_client.V1Namespace):
                msg = f"malformed Kubernetes Namespace payload while creating {name!r}"
                raise OSError(msg)
            return cls(obj=created)

        patched = await kube.run(
            lambda request_timeout: kube.core.patch_namespace(
                name=name,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to patch Namespace {name}",
        )
        if not isinstance(patched, kube_client.V1Namespace):
            msg = f"malformed Kubernetes Namespace payload while patching {name!r}"
            raise OSError(msg)
        return cls(obj=patched)

    @property
    def name(self) -> str:
        """Return the Namespace name.

        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def labels(self) -> Mapping[str, str]:
        """Return the Namespace labels.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.labels`, or an empty mapping when unavailable.
        """
        metadata = self.obj.metadata
        if metadata is None or metadata.labels is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.labels)

    @property
    def annotations(self) -> Mapping[str, str]:
        """Return the Namespace annotations.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.annotations`, or an empty mapping when
            unavailable.
        """
        metadata = self.obj.metadata
        if metadata is None or metadata.annotations is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.annotations)

    @property
    def resource_version(self) -> str:
        """Return the Namespace resource version.

        Returns
        -------
        str
            Kubernetes `metadata.resourceVersion`, or an empty string when
            unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.resource_version or "").strip() if metadata is not None else ""

    @property
    def phase(self) -> str:
        """Return the Namespace lifecycle phase.

        Returns
        -------
        str
            Namespace `status.phase`, or an empty string when unavailable.
        """
        status = self.obj.status
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

        Raises
        ------
        OSError
            If this wrapper does not contain enough metadata to identify the
            Namespace, or if Kubernetes returns malformed data.
        """
        name = self.name
        if not name:
            msg = "cannot refresh Namespace with missing metadata.name"
            raise OSError(msg)
        return await type(self).get(kube, name=name, timeout=timeout)

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this Namespace from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Raises
        ------
        OSError
            If this wrapper does not contain enough metadata to identify the
            Namespace, if the delete request fails, or if Kubernetes returns
            malformed data.
        """
        name = self.name
        if not name:
            msg = "cannot delete Namespace with missing metadata.name"
            raise OSError(msg)
        payload = await kube.run(
            lambda request_timeout: kube.core.delete_namespace(
                name=name,
                body=kube_client.V1DeleteOptions(),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete Namespace {name}",
        )
        if payload is not None and not isinstance(payload, kube_client.V1Status):
            msg = f"malformed Kubernetes response while deleting Namespace {name}"
            raise OSError(msg)

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this Namespace is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        Raises
        ------
        OSError
            If this wrapper does not contain enough metadata to identify the
            Namespace, or if a refresh request returns malformed data.
        TimeoutError
            If the Namespace still exists when `timeout` expires.
        """
        name = self.name
        if not name:
            msg = "cannot wait for Namespace deletion with missing metadata.name"
            raise OSError(msg)
        if timeout <= 0:
            msg = f"timed out waiting for Namespace {name} deletion"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                msg = f"timed out waiting for Namespace {name} deletion"
                raise TimeoutError(msg)
            live = await self.refresh(kube, timeout=remaining)
            if live is None:
                return
            await asyncio.sleep(min(NAMESPACE_WAIT_POLL_INTERVAL_SECONDS, remaining))
