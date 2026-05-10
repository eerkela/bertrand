"""Wrappers for Kubernetes coordination Lease resources."""

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
from .api.watch import (
    _watch_namespaced_resource,
)

if TYPE_CHECKING:
    import builtins
    from collections.abc import AsyncIterator, Collection, Mapping
    from datetime import datetime

    from .api import Kube, WatchEvent


@dataclass(frozen=True)
class Lease(NamespacedKubeMetadata[kube_client.V1Lease]):
    """General-purpose wrapper around one Kubernetes Lease object.

    Parameters
    ----------
    _obj : kube_client.V1Lease
        Typed Kubernetes Lease payload returned by the cluster API.
    """

    _obj: kube_client.V1Lease

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        timeout: float,
    ) -> Self | None:
        """Read one Kubernetes Lease by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the Lease.
        name : str
            Lease name to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Lease | None
            Wrapped Kubernetes Lease, or `None` if it does not exist.
        """
        payload = await kube.run(
            lambda request_timeout: kube.coordination.read_namespaced_lease(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to read Lease {name!r} in namespace {namespace!r}",
        )
        if payload is None:
            return None
        return cls(_obj=_typed_payload(payload, kube_client.V1Lease, context="Lease"))

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        timeout: float,
        namespaces: Collection[str] | None = None,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[Self]:
        """List Kubernetes Leases with optional namespace and label filtering.

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
        list[Lease]
            Wrapped Leases matching the requested filters.
        """
        return [
            cls(_obj=item)
            for item in await _list_namespaced_items(
                kube,
                timeout=timeout,
                namespaces=namespaces,
                labels=labels,
                list_all=lambda label_selector, request_timeout: (
                    kube.coordination.list_lease_for_all_namespaces(
                        label_selector=label_selector,
                        _request_timeout=request_timeout,
                    )
                ),
                list_namespace=lambda namespace, label_selector, request_timeout: (
                    kube.coordination.list_namespaced_lease(
                        namespace=namespace,
                        label_selector=label_selector,
                        _request_timeout=request_timeout,
                    )
                ),
                list_type=kube_client.V1LeaseList,
                item_type=kube_client.V1Lease,
                all_context="failed to list Leases across all namespaces",
                namespace_context=lambda namespace: (
                    f"failed to list Leases in namespace {namespace!r}"
                ),
                list_context="Lease",
                item_context="Lease",
            )
        ]

    @classmethod
    async def watch(
        cls,
        kube: Kube,
        *,
        timeout: float,
        namespace: str | None = None,
        labels: Mapping[str, str] | None = None,
        field_selector: str | None = None,
        resource_version: str | None = None,
    ) -> AsyncIterator[WatchEvent[Self]]:
        """Watch Kubernetes Leases.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum watch budget in seconds. If infinite, wait indefinitely.
        namespace : str | None, optional
            Namespace to watch. If omitted, watches Leases across all namespaces.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.
        field_selector : str | None, optional
            Raw Kubernetes field selector.
        resource_version : str | None, optional
            Resource version to watch from.

        Yields
        ------
        WatchEvent[Lease]
            Typed watch events containing wrapped Leases.
        """
        async for event in _watch_namespaced_resource(
            kube,
            expected=kube_client.V1Lease,
            wrapper=lambda payload: cls(_obj=payload),
            timeout=timeout,
            namespace=namespace,
            resource_version=resource_version,
            labels=labels,
            field_selector=field_selector,
            watch_all=kube.coordination.list_lease_for_all_namespaces,
            watch_namespace=kube.coordination.list_namespaced_lease,
            all_context="failed to watch Leases across all namespaces",
            namespace_context=lambda namespace: (
                f"failed to watch Leases in namespace {namespace!r}"
            ),
            payload_context="Lease watch",
        ):
            yield event

    @staticmethod
    def _manifest(
        *,
        namespace: str,
        name: str,
        holder_identity: str | None,
        lease_duration_seconds: int,
        acquire_time: datetime | None,
        renew_time: datetime | None,
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
        resource_version: str | None = None,
    ) -> dict[str, object]:
        spec: dict[str, object] = {"leaseDurationSeconds": lease_duration_seconds}
        if holder_identity is not None:
            spec["holderIdentity"] = holder_identity
        if acquire_time is not None:
            spec["acquireTime"] = acquire_time
        if renew_time is not None:
            spec["renewTime"] = renew_time
        body: dict[str, object] = {
            "apiVersion": "coordination.k8s.io/v1",
            "kind": "Lease",
            "metadata": {
                "name": name,
                "namespace": namespace,
                "labels": dict(labels or {}),
                "annotations": dict(annotations or {}),
            },
            "spec": spec,
        }
        if resource_version:
            metadata = body["metadata"]
            if isinstance(metadata, dict):
                metadata["resourceVersion"] = resource_version
        return body

    @classmethod
    async def create(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        holder_identity: str,
        lease_duration_seconds: int,
        timeout: float,
        acquire_time: datetime | None = None,
        renew_time: datetime | None = None,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> Self:
        """Create one Kubernetes Lease.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the Lease.
        name : str
            Lease name to create.
        holder_identity : str
            Identity string for the current lease holder.
        lease_duration_seconds : int
            Lease duration in seconds.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        acquire_time : datetime | None, optional
            Time when the lease was first acquired.
        renew_time : datetime | None, optional
            Time when the lease was last renewed.
        labels : Mapping[str, str] | None, optional
            Labels to apply to `metadata.labels`.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to `metadata.annotations`.

        Returns
        -------
        Lease
            Wrapped created Lease.

        Raises
        ------
        OSError
            If required identity fields are empty, duration is invalid, or Kubernetes
            create fails or returns malformed data.
        """
        namespace = namespace.strip()
        name = name.strip()
        holder_identity = holder_identity.strip()
        if not namespace or not name or not holder_identity:
            msg = "Lease create requires non-empty namespace, name, and holder identity"
            raise OSError(msg)
        if lease_duration_seconds <= 0:
            msg = "Lease duration must be positive"
            raise OSError(msg)

        created = await kube.run(
            lambda request_timeout: kube.coordination.create_namespaced_lease(
                namespace=namespace,
                body=cls._manifest(
                    namespace=namespace,
                    name=name,
                    holder_identity=holder_identity,
                    lease_duration_seconds=lease_duration_seconds,
                    acquire_time=acquire_time,
                    renew_time=renew_time,
                    labels=labels,
                    annotations=annotations,
                ),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to create Lease {namespace}/{name}",
        )
        if not isinstance(created, kube_client.V1Lease):
            msg = (
                f"malformed Kubernetes Lease payload while creating {namespace}/{name}"
            )
            raise OSError(msg)
        return cls(_obj=created)

    @classmethod
    async def replace(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        holder_identity: str | None,
        lease_duration_seconds: int,
        resource_version: str,
        timeout: float,
        acquire_time: datetime | None = None,
        renew_time: datetime | None = None,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> Self:
        """Replace one Kubernetes Lease with a resource-version guard.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the Lease.
        name : str
            Lease name to replace.
        holder_identity : str | None
            Identity string for the current lease holder. If `None`, the holder field
            is omitted.
        lease_duration_seconds : int
            Lease duration in seconds.
        resource_version : str
            Kubernetes resource version required for optimistic concurrency.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        acquire_time : datetime | None, optional
            Time when the lease was first acquired.
        renew_time : datetime | None, optional
            Time when the lease was last renewed.
        labels : Mapping[str, str] | None, optional
            Labels to apply to `metadata.labels`.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to `metadata.annotations`.

        Returns
        -------
        Lease
            Wrapped replaced Lease.

        Raises
        ------
        OSError
            If required identity fields are empty, duration is invalid, or Kubernetes
            replace fails or returns malformed data.
        """
        namespace = namespace.strip()
        name = name.strip()
        resource_version = resource_version.strip()
        holder = holder_identity.strip() if holder_identity is not None else None
        if holder == "":
            msg = "Lease holder identity must be non-empty when provided"
            raise OSError(msg)
        if not namespace or not name or not resource_version:
            msg = (
                "Lease replace requires non-empty namespace, name, and resource version"
            )
            raise OSError(msg)
        if lease_duration_seconds <= 0:
            msg = "Lease duration must be positive"
            raise OSError(msg)

        replaced = await kube.run(
            lambda request_timeout: kube.coordination.replace_namespaced_lease(
                name=name,
                namespace=namespace,
                body=cls._manifest(
                    namespace=namespace,
                    name=name,
                    holder_identity=holder,
                    lease_duration_seconds=lease_duration_seconds,
                    acquire_time=acquire_time,
                    renew_time=renew_time,
                    labels=labels,
                    annotations=annotations,
                    resource_version=resource_version,
                ),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to replace Lease {namespace}/{name}",
        )
        if not isinstance(replaced, kube_client.V1Lease):
            msg = (
                f"malformed Kubernetes Lease payload while replacing {namespace}/{name}"
            )
            raise OSError(msg)
        return cls(_obj=replaced)

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        holder_identity: str,
        lease_duration_seconds: int,
        timeout: float,
        acquire_time: datetime | None = None,
        renew_time: datetime | None = None,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> Self:
        """Create or patch one Kubernetes Lease.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the Lease.
        name : str
            Lease name to create or patch.
        holder_identity : str
            Identity string for the current lease holder.
        lease_duration_seconds : int
            Lease duration in seconds.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        acquire_time : datetime | None, optional
            Time when the lease was first acquired.
        renew_time : datetime | None, optional
            Time when the lease was last renewed.
        labels : Mapping[str, str] | None, optional
            Labels to apply to `metadata.labels`.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to `metadata.annotations`.

        Returns
        -------
        Lease
            Wrapped created or patched Lease.

        Raises
        ------
        OSError
            If required identity fields are empty, duration is invalid, or Kubernetes
            create/patch fails or returns malformed data.
        """
        namespace = namespace.strip()
        name = name.strip()
        holder_identity = holder_identity.strip()
        if not namespace or not name or not holder_identity:
            msg = "Lease upsert requires non-empty namespace, name, and holder identity"
            raise OSError(msg)
        if lease_duration_seconds <= 0:
            msg = "Lease duration must be positive"
            raise OSError(msg)
        manifest = cls._manifest(
            namespace=namespace,
            name=name,
            holder_identity=holder_identity,
            lease_duration_seconds=lease_duration_seconds,
            acquire_time=acquire_time,
            renew_time=renew_time,
            labels=labels,
            annotations=annotations,
        )
        payload = await _create_or_patch(
            kube,
            timeout=timeout,
            create=lambda request_timeout: kube.coordination.create_namespaced_lease(
                namespace=namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            patch=lambda request_timeout: kube.coordination.patch_namespaced_lease(
                name=name,
                namespace=namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            create_context=f"failed to create Lease {namespace}/{name}",
            patch_context=f"failed to patch Lease {namespace}/{name}",
            expected=kube_client.V1Lease,
            payload_context="Lease",
        )
        return cls(_obj=payload)

    @property
    def holder_identity(self) -> str:
        """Return the current Lease holder identity.

        Returns
        -------
        str
            Lease `spec.holderIdentity`, or an empty string when unavailable.
        """
        spec = self._obj.spec
        return (spec.holder_identity or "").strip() if spec is not None else ""

    @property
    def lease_duration_seconds(self) -> int | None:
        """Return the Lease duration in seconds.

        Returns
        -------
        int | None
            Lease `spec.leaseDurationSeconds`, or `None` when unavailable.
        """
        spec = self._obj.spec
        return spec.lease_duration_seconds if spec is not None else None

    @property
    def acquire_time(self) -> datetime | None:
        """Return the Lease acquire time.

        Returns
        -------
        datetime | None
            Lease `spec.acquireTime`, or `None` when unavailable.
        """
        spec = self._obj.spec
        return spec.acquire_time if spec is not None else None

    @property
    def renew_time(self) -> datetime | None:
        """Return the Lease renew time.

        Returns
        -------
        datetime | None
            Lease `spec.renewTime`, or `None` when unavailable.
        """
        spec = self._obj.spec
        return spec.renew_time if spec is not None else None

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this Lease by its metadata namespace and name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Lease | None
            Fresh wrapper for the same Lease, or `None` if it no longer exists.
        """
        namespace, name = self._require_namespace_name("refresh Lease")
        return await type(self).get(
            kube,
            namespace=namespace,
            timeout=timeout,
            name=name,
        )

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this Lease from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        namespace, name = self._require_namespace_name("delete Lease")
        payload = await kube.run(
            lambda request_timeout: kube.coordination.delete_namespaced_lease(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete Lease {namespace}/{name}",
        )
        _validate_delete_status(
            payload, label=self._object_label(name=name, namespace=namespace)
        )

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this Lease is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        """
        namespace, name = self._require_namespace_name("wait for Lease deletion")
        await _wait_until_deleted(
            label=self._object_label(name=name, namespace=namespace),
            timeout=timeout,
            refresh=lambda remaining: self.refresh(kube, timeout=remaining),
        )
