"""Wrappers for Kubernetes coordination Lease resources."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Self

from kubernetes import client as kube_client

from .api import (
    NamespacedKubeMetadata,
    _label_selector,
    _validate_delete_status,
    _wait_until_deleted,
)

if TYPE_CHECKING:
    import builtins
    from collections.abc import AsyncIterator, Collection, Mapping
    from datetime import datetime

    from .api import Kube, WatchEvent

LEASE_WAIT_POLL_INTERVAL_SECONDS = 0.5


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

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or the API call fails.
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
        if not isinstance(payload, kube_client.V1Lease):
            msg = (
                f"malformed Kubernetes Lease payload for {name!r} in namespace "
                f"{namespace!r}"
            )
            raise OSError(msg)
        return cls(_obj=payload)

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

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or a list call fails.
        """
        label_selector = _label_selector(labels)
        payloads: builtins.list[kube_client.V1LeaseList] = []
        if namespaces is None:
            payload = await kube.run(
                lambda request_timeout: kube.coordination.list_lease_for_all_namespaces(
                    label_selector=label_selector,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context="failed to list Leases across all namespaces",
            )
            if payload is not None:
                payloads.append(payload)
        else:
            normalized = {namespace.strip() for namespace in namespaces}
            normalized.discard("")
            if not normalized:
                return []
            for namespace in sorted(normalized):
                payload = await kube.run(
                    lambda request_timeout, namespace=namespace: (
                        kube.coordination.list_namespaced_lease(
                            namespace=namespace,
                            label_selector=label_selector,
                            _request_timeout=request_timeout,
                        )
                    ),
                    timeout=timeout,
                    context=f"failed to list Leases in namespace {namespace!r}",
                )
                if payload is not None:
                    payloads.append(payload)

        out: builtins.list[Self] = []
        for payload in payloads:
            if not isinstance(payload, kube_client.V1LeaseList):
                msg = "malformed Kubernetes Lease list payload"
                raise OSError(msg)
            for item in payload.items or []:
                if not isinstance(item, kube_client.V1Lease):
                    msg = "malformed Kubernetes Lease entry in list payload"
                    raise OSError(msg)
                out.append(cls(_obj=item))
        return out

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
        namespace = namespace.strip() if namespace is not None else ""
        if namespace:
            fn = kube.coordination.list_namespaced_lease
            api_kwargs: Mapping[str, object] = {"namespace": namespace}
            context = f"failed to watch Leases in namespace {namespace!r}"
        else:
            fn = kube.coordination.list_lease_for_all_namespaces
            api_kwargs = {}
            context = "failed to watch Leases across all namespaces"

        async for event in kube.watch(
            fn,
            wrapper=cls._watch_payload,
            timeout=timeout,
            context=context,
            resource_version=resource_version,
            labels=labels,
            field_selector=field_selector,
            api_kwargs=api_kwargs,
        ):
            yield event

    @classmethod
    def _watch_payload(cls, payload: object) -> Self:
        if not isinstance(payload, kube_client.V1Lease):
            msg = "malformed Kubernetes Lease watch payload"
            raise OSError(msg)
        return cls(_obj=payload)

    @staticmethod
    def _manifest(
        *,
        namespace: str,
        name: str,
        holder_identity: str,
        lease_duration_seconds: int,
        acquire_time: datetime | None,
        renew_time: datetime | None,
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
    ) -> dict[str, object]:
        spec: dict[str, object] = {
            "holderIdentity": holder_identity,
            "leaseDurationSeconds": lease_duration_seconds,
        }
        if acquire_time is not None:
            spec["acquireTime"] = acquire_time
        if renew_time is not None:
            spec["renewTime"] = renew_time
        return {
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
        try:
            created = await kube.run(
                lambda request_timeout: kube.coordination.create_namespaced_lease(
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=f"failed to create Lease {namespace}/{name}",
            )
        except OSError as err:
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise
        else:
            if not isinstance(created, kube_client.V1Lease):
                msg = (
                    "malformed Kubernetes Lease payload while creating "
                    f"{namespace}/{name}"
                )
                raise OSError(msg)
            return cls(_obj=created)

        patched = await kube.run(
            lambda request_timeout: kube.coordination.patch_namespaced_lease(
                name=name,
                namespace=namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to patch Lease {namespace}/{name}",
        )
        if not isinstance(patched, kube_client.V1Lease):
            msg = (
                f"malformed Kubernetes Lease payload while patching {namespace}/{name}"
            )
            raise OSError(msg)
        return cls(_obj=patched)

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

        Raises
        ------
        TimeoutError
            If the Lease still exists when `timeout` expires.
        """
        namespace, name = self._require_namespace_name("wait for Lease deletion")
        try:
            await _wait_until_deleted(
                label=self._object_label(name=name, namespace=namespace),
                timeout=timeout,
                refresh=lambda remaining: self.refresh(kube, timeout=remaining),
            )
        except TimeoutError as err:
            raise TimeoutError(str(err)) from err
