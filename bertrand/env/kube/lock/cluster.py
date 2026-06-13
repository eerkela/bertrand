"""Lease-backed cluster locks for Bertrand control-plane operations."""

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
from typing import Any, ClassVar, Self, cast

from bertrand.env.git import (
    BERTRAND_LABEL,
    BERTRAND_LABEL_MANAGED,
    BERTRAND_NAMESPACE,
    Deadline,
)
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.lease import Lease, LeaseManifest, LeaseReplacementManifest

CLUSTER_LOCK_DURATION_SECONDS = 30
CLUSTER_LOCK_RENEW_SECONDS = 10
CLUSTER_LOCK_POLL_SECONDS = 0.25
CLUSTER_LOCK_QUERY_TIMEOUT_SECONDS = 5.0
CLUSTER_LOCK_NAME_HEX_LENGTH = 48
CLUSTER_LOCK_GUARD = threading.RLock()
CLUSTER_LOCKS: dict[tuple[int, str, str], ClusterLock] = {}
_CLUSTER_LOCK_ERRORS: tuple[type[Exception], ...] = (
    OSError,
    RuntimeError,
    TimeoutError,
    ValueError,
)


class ClusterLock:
    """Provide a re-entrant asynchronous Kubernetes Lease lock.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context used to read and update the backing Lease.
    key : str
        Stable logical lock key. The key is hashed into a Kubernetes Lease name, so it
        does not need to be a valid Kubernetes resource name.
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

    def _clear_owner(
        self,
        *,
        owner: asyncio.Task[Any] | None = None,
        unregister: bool = False,
    ) -> None:
        with CLUSTER_LOCK_GUARD:
            if owner is not None and self._owner != owner:
                return
            self._owner = None
            self._depth = 0
            if unregister:
                CLUSTER_LOCKS.pop(self._registry_key, None)

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
            BERTRAND_LABEL: BERTRAND_LABEL_MANAGED,
            "bertrand.dev/cluster-lock": "true",
            "bertrand.dev/cluster-lock-key-hash": (
                self.key_hash[:CLUSTER_LOCK_NAME_HEX_LENGTH]
            ),
        }

    def _annotations(self) -> dict[str, str]:
        return {
            "bertrand.dev/cluster-lock-key-sha256": self.key_hash,
        }

    async def _get_lease(self, deadline: Deadline) -> _LeaseState | None:
        lease = await Lease.get(
            self.kube,
            namespace=self.namespace,
            name=self.name,
            deadline=deadline,
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

    async def _create_lease(self, now: datetime, deadline: Deadline) -> bool:
        try:
            await Lease.create(
                self.kube,
                intent=LeaseManifest(
                    namespace=self.namespace,
                    name=self.name,
                    holder_identity=self._holder,
                    lease_duration_seconds=self.lease_duration,
                    renew_time=now,
                    labels=self._labels(),
                    annotations=self._annotations(),
                ),
                deadline=deadline,
            )
        except OSError as err:
            if isinstance(err, Kube.APIError) and err.status == 409:
                return False
            raise
        return True

    async def _replace_lease(
        self,
        lease: _LeaseState,
        now: datetime,
        *,
        deadline: Deadline,
        holder_identity: str | None,
    ) -> bool:
        try:
            await Lease.replace(
                self.kube,
                intent=LeaseReplacementManifest(
                    namespace=self.namespace,
                    name=self.name,
                    holder_identity=holder_identity,
                    lease_duration_seconds=self.lease_duration,
                    resource_version=lease.resource_version,
                    renew_time=now,
                    labels=self._labels(),
                    annotations=self._annotations(),
                ),
                deadline=deadline,
            )
        except OSError as err:
            if isinstance(err, Kube.APIError) and err.status in (404, 409):
                return False
            raise
        return True

    def _lease_expired(self, lease: _LeaseState, now: datetime) -> bool:
        if lease.holder is None or lease.renew_time is None:
            return True
        age = (now - lease.renew_time).total_seconds()
        return age > max(1, lease.duration_seconds)

    async def _try_acquire_lease(self, deadline: Deadline) -> bool:
        lease = await self._get_lease(deadline=deadline)
        now = datetime.now(UTC)
        if lease is None:
            created = await self._create_lease(now, deadline=deadline)
            if created:
                self._begin_renew_loop()
            return created
        if lease.holder == self._holder or self._lease_expired(lease, now):
            updated = await self._replace_lease(
                lease,
                now,
                deadline=deadline,
                holder_identity=self._holder,
            )
            if updated:
                self._begin_renew_loop()
            return updated
        return False

    async def _acquire_lease(self, deadline: Deadline) -> bool:
        while deadline.remaining > 0:
            if await self._try_acquire_lease(deadline):
                return True
            await deadline.sleep(CLUSTER_LOCK_POLL_SECONDS)
        return False

    async def _clear_lease(self) -> None:
        deadline = Deadline(CLUSTER_LOCK_QUERY_TIMEOUT_SECONDS)
        lease = await self._get_lease(deadline=deadline)
        if lease is None or lease.holder != self._holder:
            return
        await self._replace_lease(
            lease,
            datetime.now(UTC),
            deadline=deadline,
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
                    deadline=Deadline(CLUSTER_LOCK_QUERY_TIMEOUT_SECONDS)
                )
                if lease is None:
                    msg = f"cluster lock Lease {self.namespace}/{self.name} disappeared"
                    raise OSError(msg)
                if lease.holder != self._holder:
                    msg = (
                        f"cluster lock Lease {self.namespace}/{self.name} ownership "
                        f"was lost to {lease.holder!r}"
                    )
                    raise OSError(msg)
                renewed = await self._replace_lease(
                    lease,
                    datetime.now(UTC),
                    deadline=Deadline(CLUSTER_LOCK_QUERY_TIMEOUT_SECONDS),
                    holder_identity=self._holder,
                )
                if not renewed:
                    msg = (
                        f"cluster lock Lease {self.namespace}/{self.name} renewal "
                        "encountered a conflict"
                    )
                    raise OSError(msg)
            except asyncio.CancelledError:
                raise
            except _CLUSTER_LOCK_ERRORS as err:
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
            with contextlib.suppress(asyncio.CancelledError):
                await self._renew_task
        self._renew_stop = None
        self._renew_task = None

    async def lock(self, deadline: Deadline) -> Self:
        """Acquire this cluster lock, waiting for this lock's timeout if needed.

        Returns
        -------
        Self
            This lock instance after acquisition.

        Raises
        ------
        TimeoutError
            If the lock cannot be acquired before its timeout.
        OSError
            If the Lease cannot be read, created, renewed, or cleaned up after a failed
            acquisition.
        """
        owner = self._get_owner()
        while True:
            with CLUSTER_LOCK_GUARD:
                if self._owner is None:
                    self._owner = owner
                    self._depth = 1
                    break
                if self._owner == owner:
                    self._depth += 1
                    return self
            if deadline.remaining <= 0:
                msg = "could not acquire cluster lock before deadline"
                raise TimeoutError(msg)
            await deadline.sleep(CLUSTER_LOCK_POLL_SECONDS)

        try:
            if not await self._acquire_lease(deadline):
                msg = "could not acquire cluster lock before deadline"
                raise TimeoutError(msg)
            return self  # noqa: TRY300
        except OSError:
            with contextlib.suppress(*_CLUSTER_LOCK_ERRORS):
                await self._stop_renew_loop()
            self._clear_owner(owner=owner)
            raise

    async def try_lock(self) -> bool:
        """Attempt to acquire this cluster lock without waiting on contention.

        Returns
        -------
        bool
            `True` if the lock was acquired, otherwise `False`.

        Raises
        ------
        OSError
            If the Lease cannot be read, created, renewed, or cleaned up after a failed
            acquisition.
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
            acquired = await self._try_acquire_lease(
                Deadline(CLUSTER_LOCK_QUERY_TIMEOUT_SECONDS)
            )
        except OSError:
            with contextlib.suppress(*_CLUSTER_LOCK_ERRORS):
                await self._stop_renew_loop()
            self._clear_owner(owner=owner)
            raise

        if acquired:
            return True

        self._clear_owner(owner=owner)
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
        OSError
            If the Lease cannot be released and `ignore_errors` is `False`.
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
        except OSError:
            if not ignore_errors:
                raise
        finally:
            self._clear_owner(unregister=True)

        if not ignore_errors and self._renew_error is not None:
            error = self._renew_error
            self._renew_error = None
            raise error

    def __bool__(self) -> bool:  # noqa: D105
        with CLUSTER_LOCK_GUARD:
            return self._depth > 0

    def __repr__(self) -> str:  # noqa: D105
        return (
            f"ClusterLock(key={self.key!r}, namespace={self.namespace!r}, "
            f"lease_duration={self.lease_duration})"
        )
