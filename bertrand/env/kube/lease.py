"""Wrappers for Kubernetes coordination Lease resources."""

from __future__ import annotations

import asyncio
import contextlib
import hashlib
import os
import re
import socket
import threading
import uuid
from dataclasses import dataclass
from datetime import UTC, datetime
from typing import TYPE_CHECKING, Any, ClassVar, Self, cast

from kubernetes import client as kube_client

from bertrand.env.git import BERTRAND_ENV, BERTRAND_NAMESPACE

from .api import (
    NamespacedKubeMetadata,
    _label_selector,
    _validate_delete_status,
    _wait_until_deleted,
)

if TYPE_CHECKING:
    import builtins
    from collections.abc import AsyncIterator, Collection, Mapping
    from types import TracebackType

    from .api import Kube, WatchEvent

LEASE_WAIT_POLL_INTERVAL_SECONDS = 0.5
CLUSTER_LOCK_DURATION_SECONDS = 30
CLUSTER_LOCK_RENEW_SECONDS = 10
CLUSTER_LOCK_POLL_SECONDS = 0.25
CLUSTER_LOCK_QUERY_TIMEOUT_SECONDS = 5.0
CLUSTER_LOCK_NAME_HEX_LENGTH = 48
CLUSTER_LOCK_GUARD = threading.RLock()
CLUSTER_LOCKS: dict[tuple[int, str, str], ClusterLock] = {}


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
        resource_version: str | None = None,
    ) -> dict[str, object]:
        spec: dict[str, object] = {
            "holderIdentity": holder_identity,
            "leaseDurationSeconds": lease_duration_seconds,
        }
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


class ClusterLock:
    """Provide a re-entrant asynchronous Kubernetes Lease lock.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context used to read and update the backing Lease.
    key : str
        Stable logical lock key. The key is hashed into a Kubernetes Lease name, so it
        does not need to be a valid Kubernetes resource name.
    timeout : float
        Maximum number of seconds to wait for lock acquisition. Non-positive timeouts
        are permitted for opportunistic `try_lock()` calls, but blocking `lock()` calls
        will reject them.
    namespace : str, optional
        Namespace that owns the Lease. Defaults to Bertrand's runtime namespace.
    lease_duration : int, optional
        Number of seconds after the last renew time before another holder may steal the
        Lease. Defaults to 30 seconds.

    Raises
    ------
    ValueError
        If `key`, `namespace`, or `lease_duration` is invalid.
    """

    HOST_RE: ClassVar[re.Pattern[str]] = re.compile(r"[^a-z0-9-]+")

    kube: Kube
    key: str
    timeout: float
    namespace: str
    lease_duration: int
    name: str
    key_hash: str
    _holder: str
    _registry_key: tuple[int, str, str]
    _owner: asyncio.Task[Any] | None
    _depth: int
    _renew_stop: asyncio.Event | None
    _renew_task: asyncio.Task[None] | None
    _renew_error: Exception | None

    @dataclass(frozen=True)
    class _LeaseState:
        resource_version: str
        holder: str | None
        renew_time: datetime | None
        duration_seconds: int

    def __new__(  # noqa: D102
        cls,
        kube: Kube,
        key: str,
        *,
        timeout: float,
        namespace: str = BERTRAND_NAMESPACE,
        lease_duration: int = CLUSTER_LOCK_DURATION_SECONDS,
    ) -> Self:
        key = key.strip()
        namespace = namespace.strip()
        if not key:
            msg = "ClusterLock key must not be empty"
            raise ValueError(msg)
        if not namespace:
            msg = "ClusterLock namespace must not be empty"
            raise ValueError(msg)
        if lease_duration <= 0:
            msg = "ClusterLock lease duration must be positive"
            raise ValueError(msg)

        with CLUSTER_LOCK_GUARD:
            registry_key = (id(kube), namespace, key)
            self = CLUSTER_LOCKS.get(registry_key)
            if self is None:
                self = super().__new__(cls)
                key_hash = hashlib.sha256(key.encode("utf-8")).hexdigest()
                host = cls.HOST_RE.sub("-", socket.gethostname().lower()).strip("-")
                if not host:
                    host = "host"
                self.kube = kube
                self.key = key
                self.timeout = timeout
                self.namespace = namespace
                self.lease_duration = lease_duration
                self.name = f"bertrand-lock-{key_hash[:CLUSTER_LOCK_NAME_HEX_LENGTH]}"
                self.key_hash = key_hash
                self._holder = f"{host}-{os.getpid()}-{uuid.uuid4().hex[:8]}"
                self._registry_key = registry_key
                self._owner = None
                self._depth = 0
                self._renew_stop = None
                self._renew_task = None
                self._renew_error = None
                CLUSTER_LOCKS[registry_key] = self
            else:
                if self.timeout < timeout:
                    self.timeout = timeout
                if self.lease_duration < lease_duration:
                    self.lease_duration = lease_duration
        return cast("Self", self)

    @staticmethod
    def _get_owner() -> asyncio.Task[Any]:
        try:
            task = asyncio.current_task()
        except RuntimeError:
            task = None
        if task is None:
            msg = "ClusterLock requires an active asyncio Task"
            raise RuntimeError(msg)
        return task

    @property
    def holder(self) -> str:
        """Return this process's Lease holder identity.

        Returns
        -------
        str
            Holder identity used for Lease acquisition and renewal.
        """
        return self._holder

    def _labels(self) -> dict[str, str]:
        return {
            BERTRAND_ENV: "1",
            "bertrand.dev/cluster-lock": "true",
            "bertrand.dev/cluster-lock-key-hash": (
                self.key_hash[:CLUSTER_LOCK_NAME_HEX_LENGTH]
            ),
        }

    def _annotations(self) -> dict[str, str]:
        return {
            "bertrand.dev/cluster-lock-key-sha256": self.key_hash,
        }

    def _manifest(
        self,
        now: datetime,
        lease: _LeaseState | None,
        *,
        holder_identity: str | None,
    ) -> dict[str, object]:
        metadata: dict[str, object] = {
            "name": self.name,
            "namespace": self.namespace,
            "labels": self._labels(),
            "annotations": self._annotations(),
        }
        if lease is not None:
            metadata["resourceVersion"] = lease.resource_version

        spec: dict[str, object] = {
            "leaseDurationSeconds": self.lease_duration,
            "renewTime": now,
        }
        if holder_identity is not None:
            spec["holderIdentity"] = holder_identity
        return {
            "apiVersion": "coordination.k8s.io/v1",
            "kind": "Lease",
            "metadata": metadata,
            "spec": spec,
        }

    async def _get_lease(self, timeout: float) -> _LeaseState | None:
        lease = await Lease.get(
            self.kube,
            namespace=self.namespace,
            name=self.name,
            timeout=timeout,
        )
        if lease is None:
            return None

        resource_version = lease.resource_version
        if not resource_version:
            msg = (
                f"cluster lock Lease {self.namespace}/{self.name} has no "
                "resourceVersion"
            )
            raise OSError(msg)

        holder = lease.holder_identity or None
        duration = lease.lease_duration_seconds
        if duration is None or duration <= 0:
            duration = self.lease_duration
        return self._LeaseState(
            resource_version=resource_version,
            holder=holder,
            renew_time=lease.renew_time,
            duration_seconds=duration,
        )

    async def _create_lease(self, now: datetime, timeout: float) -> bool:
        try:
            created = await self.kube.run(
                lambda request_timeout: self.kube.coordination.create_namespaced_lease(
                    namespace=self.namespace,
                    body=self._manifest(now, None, holder_identity=self._holder),
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=(
                    f"failed to create cluster lock Lease {self.namespace}/{self.name}"
                ),
            )
        except OSError as err:
            detail = str(err).lower()
            if "status 409" in detail or "already exists" in detail:
                return False
            raise
        if not isinstance(created, kube_client.V1Lease):
            msg = (
                f"malformed cluster lock Lease create payload for "
                f"{self.namespace}/{self.name}"
            )
            raise OSError(msg)
        return True

    async def _replace_lease(
        self,
        lease: _LeaseState,
        now: datetime,
        *,
        timeout: float,
        holder_identity: str | None,
    ) -> bool:
        try:
            updated = await self.kube.run(
                lambda request_timeout: self.kube.coordination.replace_namespaced_lease(
                    name=self.name,
                    namespace=self.namespace,
                    body=self._manifest(now, lease, holder_identity=holder_identity),
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=(
                    f"failed to replace cluster lock Lease {self.namespace}/{self.name}"
                ),
            )
        except OSError as err:
            detail = str(err).lower()
            if (
                "status 409" in detail
                or "status 404" in detail
                or "not found" in detail
            ):
                return False
            raise
        if updated is None:
            return False
        if not isinstance(updated, kube_client.V1Lease):
            msg = (
                f"malformed cluster lock Lease replace payload for "
                f"{self.namespace}/{self.name}"
            )
            raise OSError(msg)
        return True

    def _lease_expired(self, lease: _LeaseState, now: datetime) -> bool:
        if lease.holder is None or lease.renew_time is None:
            return True
        age = (now - lease.renew_time).total_seconds()
        return age > max(1, lease.duration_seconds)

    async def _try_acquire_lease(self, timeout: float) -> bool:
        if timeout <= 0:
            return False
        lease = await self._get_lease(timeout=timeout)
        now = datetime.now(UTC)
        if lease is None:
            created = await self._create_lease(now, timeout=timeout)
            if created:
                self._begin_renew_loop()
            return created
        if lease.holder == self._holder or self._lease_expired(lease, now):
            updated = await self._replace_lease(
                lease,
                now,
                timeout=timeout,
                holder_identity=self._holder,
            )
            if updated:
                self._begin_renew_loop()
            return updated
        return False

    async def _acquire_lease(self, deadline: float) -> bool:
        loop = asyncio.get_running_loop()
        timestamp = loop.time()
        while timestamp <= deadline:
            if await self._try_acquire_lease(deadline - timestamp):
                return True
            timestamp = loop.time()
            await asyncio.sleep(
                min(CLUSTER_LOCK_POLL_SECONDS, max(0.0, deadline - timestamp))
            )
        return False

    async def _clear_lease(self) -> None:
        lease = await self._get_lease(timeout=CLUSTER_LOCK_QUERY_TIMEOUT_SECONDS)
        if lease is None or lease.holder != self._holder:
            return
        await self._replace_lease(
            lease,
            datetime.now(UTC),
            timeout=CLUSTER_LOCK_QUERY_TIMEOUT_SECONDS,
            holder_identity=None,
        )

    async def _renew_loop(self) -> None:
        interval = max(1.0, min(CLUSTER_LOCK_RENEW_SECONDS, self.lease_duration / 2))
        while True:
            try:
                stop = self._renew_stop
                if stop is None:
                    return
                await asyncio.wait_for(stop.wait(), timeout=interval)
            except TimeoutError:
                pass
            else:
                return

            try:
                lease = await self._get_lease(
                    timeout=CLUSTER_LOCK_QUERY_TIMEOUT_SECONDS
                )
                if lease is None:
                    msg = f"cluster lock Lease {self.namespace}/{self.name} disappeared"
                    raise OSError(msg)  # noqa: TRY301
                if lease.holder != self._holder:
                    msg = (
                        f"cluster lock Lease {self.namespace}/{self.name} ownership "
                        f"was lost to {lease.holder!r}"
                    )
                    raise OSError(msg)  # noqa: TRY301
                renewed = await self._replace_lease(
                    lease,
                    datetime.now(UTC),
                    timeout=CLUSTER_LOCK_QUERY_TIMEOUT_SECONDS,
                    holder_identity=self._holder,
                )
                if not renewed:
                    msg = (
                        f"cluster lock Lease {self.namespace}/{self.name} renewal "
                        "encountered a conflict"
                    )
                    raise OSError(msg)  # noqa: TRY301
            except asyncio.CancelledError:
                raise
            except Exception as err:  # noqa: BLE001
                self._renew_error = err
                return

    def _begin_renew_loop(self) -> None:
        if self._renew_task is not None and not self._renew_task.done():
            return
        self._renew_error = None
        self._renew_stop = asyncio.Event()
        self._renew_task = asyncio.create_task(self._renew_loop())

    async def _stop_renew_loop(self) -> None:
        if self._renew_stop is not None:
            self._renew_stop.set()
        if self._renew_task is not None:
            self._renew_task.cancel()
            try:
                await self._renew_task
            except asyncio.CancelledError:
                pass
            except Exception:  # noqa: BLE001
                pass
        self._renew_stop = None
        self._renew_task = None

    async def lock(self) -> Self:
        """Acquire this cluster lock, waiting for this lock's timeout if needed.

        Returns
        -------
        Self
            This lock instance after acquisition.

        Raises
        ------
        TimeoutError
            If the lock cannot be acquired before its timeout.
        """
        if self.timeout <= 0:
            msg = f"could not acquire cluster lock within {self.timeout} seconds"
            raise TimeoutError(msg)

        owner = self._get_owner()
        loop = asyncio.get_running_loop()
        deadline = loop.time() + self.timeout
        while True:
            with CLUSTER_LOCK_GUARD:
                if self._owner is None:
                    self._owner = owner
                    self._depth = 1
                    break
                if self._owner == owner:
                    self._depth += 1
                    return self
            if loop.time() >= deadline:
                msg = f"could not acquire cluster lock within {self.timeout} seconds"
                raise TimeoutError(msg)
            await asyncio.sleep(CLUSTER_LOCK_POLL_SECONDS)

        try:
            if not await self._acquire_lease(deadline):
                msg = f"could not acquire cluster lock within {self.timeout} seconds"
                raise TimeoutError(msg)  # noqa: TRY301
            return self  # noqa: TRY300
        except Exception:
            with contextlib.suppress(Exception):
                await self._stop_renew_loop()
            with CLUSTER_LOCK_GUARD:
                self._owner = None
                self._depth = 0
            raise

    async def try_lock(self) -> bool:
        """Attempt to acquire this cluster lock without waiting on contention.

        Returns
        -------
        bool
            `True` if the lock was acquired, otherwise `False`.

        """
        owner = self._get_owner()
        with CLUSTER_LOCK_GUARD:
            if self._owner == owner:
                self._depth += 1
                return True
            if self._owner is not None:
                return False
            self._owner = owner
            self._depth = 1

        try:
            acquired = await self._try_acquire_lease(CLUSTER_LOCK_QUERY_TIMEOUT_SECONDS)
        except Exception:
            with contextlib.suppress(Exception):
                await self._stop_renew_loop()
            with CLUSTER_LOCK_GUARD:
                if self._owner == owner:
                    self._owner = None
                    self._depth = 0
            raise

        if acquired:
            return True

        with CLUSTER_LOCK_GUARD:
            if self._owner == owner:
                self._owner = None
                self._depth = 0
        return False

    async def unlock(self, *, ignore_errors: bool = False) -> None:
        """Release this cluster lock once.

        Parameters
        ----------
        ignore_errors : bool, optional
            If `True`, suppress backend release errors while still clearing local lock
            ownership. Defaults to `False`.

        Raises
        ------
        RuntimeError
            If the current task does not hold this lock.
        """
        owner = self._get_owner()
        with CLUSTER_LOCK_GUARD:
            if self._owner != owner or self._depth < 1:
                msg = "cluster lock is not held by the current owner"
                raise RuntimeError(msg)
            self._depth -= 1
            if self._depth > 0:
                return

        try:
            await self._stop_renew_loop()
            await self._clear_lease()
        except Exception:
            if not ignore_errors:
                raise
        finally:
            with CLUSTER_LOCK_GUARD:
                self._owner = None
                self._depth = 0
                CLUSTER_LOCKS.pop(self._registry_key, None)

        if not ignore_errors and self._renew_error is not None:
            error = self._renew_error
            self._renew_error = None
            raise error

    async def __aenter__(self) -> Self:  # noqa: D105
        return await self.lock()

    async def __aexit__(  # noqa: D105
        self,
        exc_type: type[BaseException] | None,
        exc_value: BaseException | None,
        traceback: TracebackType | None,
    ) -> None:
        await self.unlock(ignore_errors=exc_value is not None)

    def __bool__(self) -> bool:  # noqa: D105
        with CLUSTER_LOCK_GUARD:
            return self._depth > 0

    def __repr__(self) -> str:  # noqa: D105
        return (
            f"ClusterLock(key={self.key!r}, namespace={self.namespace!r}, "
            f"timeout={self.timeout})"
        )
