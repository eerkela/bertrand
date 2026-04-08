"""Async lock utilities for local and cluster-scoped mutual exclusion."""
from __future__ import annotations

import asyncio
import errno
import hashlib
import json
import os
import re
import socket
import threading
import uuid
from dataclasses import dataclass, field
from datetime import UTC, datetime
from pathlib import Path
from types import TracebackType
from typing import Any, Literal, Self

from .bertrand_git import (
    NERDCTL_NAMESPACE,
    CommandError,
    TimeoutExpired,
    kubectl,
)

if os.name == "nt":
    import msvcrt
else:
    import fcntl


type LockMode = Literal["local", "cluster"]


LEASE_DURATION_SECONDS = 30
LEASE_RENEW_SECONDS = 10
LEASE_POLL_SECONDS = 0.25
LEASE_QUERY_TIMEOUT = 5.0
LOCK_POLL_SECONDS = 0.1
LEASE_NAME_HEX_LENGTH = 48

LOCK_GUARD = threading.RLock()
LOCKS: dict[tuple[str, LockMode], Lock] = {}


def _to_rfc3339(value: datetime) -> str:
    return value.astimezone(UTC).isoformat(
        timespec="microseconds"
    ).replace("+00:00", "Z")


def _parse_rfc3339(value: str) -> datetime | None:
    raw = value.strip()
    if raw.endswith("Z"):
        raw = f"{raw[:-1]}+00:00"
    try:
        out = datetime.fromisoformat(raw)
    except ValueError:
        return None
    if out.tzinfo is None:
        return None
    return out.astimezone(UTC)


@dataclass
class _LocalFileLock:
    """Local OS file-lock backend."""
    path: Path
    _fd: int | None = field(default=None, init=False)

    async def acquire(self, deadline: float) -> bool:
        """Acquire an OS file lock, retrying until it is acquired or the timeout is
        reached.

        Parameters
        ----------
        deadline : float
            The absolute time (in seconds from the event loop's time) at which to stop
            trying to acquire the lock.

        Returns
        -------
        bool
            `True` if the lock was successfully acquired, or `False` if the deadline
            was reached before the lock could be acquired.
        """
        loop = asyncio.get_event_loop()
        while not await self.try_acquire():
            if loop.time() >= deadline:
                return False
            await asyncio.sleep(LOCK_POLL_SECONDS)
        return True

    async def try_acquire(self) -> bool:
        """Attempt to acquire the local file lock once, without waiting.

        Returns
        -------
        bool
            `True` if the lock was successfully acquired, or `False` if the lock is
            currently held by another process.
        """
        self.path.parent.mkdir(parents=True, exist_ok=True)
        if self._fd is None:
            self._fd = os.open(self.path, os.O_RDWR | os.O_CREAT, 0o600)

        # Windows file lock on a 1-byte region
        if os.name == "nt":
            os.lseek(self._fd, 0, os.SEEK_SET)
            if os.fstat(self._fd).st_size < 1:
                os.write(self._fd, b"\0")
            try:
                msvcrt.locking(self._fd, msvcrt.LK_NBLCK, 1)
            except OSError as err:
                if (
                    err.errno in (errno.EACCES, errno.EDEADLK) or
                    getattr(err, "winerror", None) in {33, 36}
                ):
                    return False
                raise
            return True

        # POSIX non-blocking file lock
        try:
            fcntl.flock(self._fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return False
        except OSError as err:
            if err.errno in (errno.EACCES, errno.EAGAIN):
                return False
            raise
        return True

    async def release(self) -> None:
        """Release the local file lock, if it is currently held."""
        if self._fd is None:
            return
        fd = self._fd
        self._fd = None
        try:
            if os.name == "nt":
                os.lseek(fd, 0, os.SEEK_SET)
                try:
                    msvcrt.locking(fd, msvcrt.LK_UNLCK, 1)
                except OSError:
                    pass
            else:
                try:
                    fcntl.flock(fd, fcntl.LOCK_UN)
                except OSError:
                    pass
        finally:
            os.close(fd)
            try:
                self.path.unlink()
            except OSError:
                pass

    def pop_error(self) -> Exception | None:
        """Return any exception encountered during lock release.  Not applicable for
        local file locks.
        """
        return None


class _ClusterLeaseLock:
    """Cluster-wide lock backend implemented via Kubernetes Lease resources in the
    local MicroK8s cluster, which must be up and running before attempting to acquire
    the lock.
    """
    HOST_RE = re.compile(r"[^a-z0-9-]+")

    _namespace: str
    _lease_duration: int
    _lease_name: str
    _holder: str
    _renew_stop: asyncio.Event | None
    _renew_task: asyncio.Task[None] | None
    _renew_error: Exception | None

    def __init__(
        self,
        path: Path,
        *,
        namespace: str = NERDCTL_NAMESPACE,
        lease_duration: int = LEASE_DURATION_SECONDS,
    ) -> None:
        self._namespace = namespace
        self._lease_duration = lease_duration
        digest = hashlib.sha256(
            str(path).encode("utf-8")
        ).hexdigest()
        self._lease_name = f"bertrand-lock-{digest[:LEASE_NAME_HEX_LENGTH]}"
        host = self.HOST_RE.sub("-", socket.gethostname().lower()).strip("-")
        if not host:
            host = "host"
        self._holder = f"{host}-{os.getpid()}-{uuid.uuid4().hex[:8]}"
        self._renew_stop = None
        self._renew_task = None
        self._renew_error = None

    @dataclass(frozen=True)
    class _LeaseState:
        resource_version: str
        holder: str | None
        renew_time: datetime | None
        duration_seconds: int

    async def _get_lease(self, timeout: float) -> _LeaseState | None:
        try:
            result = await kubectl(
                ["-n", self._namespace, "get", "lease", self._lease_name, "-o", "json"],
                capture_output=True,
                timeout=timeout,
            )
            payload = json.loads(result.stdout)
        except CommandError as err:
            stderr = (err.stderr or "").lower()
            if "not found" in stderr or "notfound" in stderr:
                return None
            raise OSError(
                f"failed to read cluster lease '{self._lease_name}': {err}"
            ) from err
        except json.JSONDecodeError as err:
            raise OSError(
                f"cluster lease '{self._lease_name}' returned malformed JSON payload"
            ) from err

        metadata = payload.get("metadata")
        spec = payload.get("spec")
        if not isinstance(metadata, dict) or not isinstance(spec, dict):
            raise OSError(
                f"cluster lease '{self._lease_name}' payload is malformed:\n"
                f"{json.dumps(payload, indent=2)}"
            )
        resource_version = metadata.get("resourceVersion")
        if not isinstance(resource_version, str) or not resource_version.strip():
            raise OSError(
                f"cluster lease '{self._lease_name}' is missing resourceVersion:\n"
                f"{json.dumps(payload, indent=2)}"
            )

        holder = spec.get("holderIdentity")
        if not isinstance(holder, str):
            holder = None

        renew_time: datetime | None = None
        renew_raw = spec.get("renewTime")
        if isinstance(renew_raw, str):
            renew_time = _parse_rfc3339(renew_raw)

        duration: int = self._lease_duration
        duration_raw = spec.get("leaseDurationSeconds")
        if isinstance(duration_raw, int) and duration_raw > 0:
            duration = duration_raw

        return self._LeaseState(
            resource_version=resource_version,
            holder=holder,
            renew_time=renew_time,
            duration_seconds=duration,
        )

    def _lease_payload(self, now: datetime, lease: _LeaseState | None) -> str:
        payload: dict[str, Any] = {
            "apiVersion": "coordination.k8s.io/v1",
            "kind": "Lease",
            "metadata": {
                "name": self._lease_name,
                "namespace": self._namespace,
            },
            "spec": {
                "holderIdentity": self._holder,
                "leaseDurationSeconds": self._lease_duration,
                "renewTime": _to_rfc3339(now),
            },
        }
        if lease is not None:
            payload["metadata"]["resourceVersion"] = lease.resource_version
        return json.dumps(payload, separators=(",", ":"))

    async def _create_lease(self, now: datetime, timeout: float) -> bool:
        try:
            await kubectl(
                ["create", "-f", "-"],
                capture_output=True,
                input=self._lease_payload(now, None),
                timeout=timeout,
            )
            return True
        except CommandError as err:
            stderr = (err.stderr or "").lower()
            if "already exists" in stderr or "alreadyexists" in stderr or "conflict" in stderr:
                return False
            raise OSError(
                f"failed to create cluster lease '{self._lease_name}':\n{err}"
            ) from err

    async def _replace_lease(self, lease: _LeaseState, now: datetime, timeout: float) -> bool:
        try:
            await kubectl(
                ["replace", "-f", "-"],
                capture_output=True,
                input=self._lease_payload(now, lease),
                timeout=timeout,
            )
            return True
        except CommandError as err:
            stderr = (err.stderr or "").lower()
            if "conflict" in stderr or "not found" in stderr or "notfound" in stderr:
                return False
            raise OSError(
                f"failed to update cluster lease '{self._lease_name}':\n{err}"
            ) from err

    async def _renew_loop(self) -> None:
        interval = max(1.0, min(LEASE_RENEW_SECONDS, self._lease_duration / 2))
        while True:
            try:
                stop = self._renew_stop
                if stop is None:
                    return
                await asyncio.wait_for(stop.wait(), timeout=interval)
                return  # stop was signaled
            except TimeoutError:
                pass  # time to renew

            try:
                lease = await self._get_lease(timeout=LEASE_QUERY_TIMEOUT)
                if lease is None:
                    raise OSError(f"cluster lease '{self._lease_name}' disappeared")
                if lease.holder != self._holder:
                    raise OSError(
                        f"cluster lease '{self._lease_name}' ownership was lost to "
                        f"{lease.holder!r}"
                    )
                ok = await self._replace_lease(
                    lease,
                    datetime.now(UTC),
                    timeout=LEASE_QUERY_TIMEOUT,
                )
                if not ok:
                    raise OSError(
                        f"cluster lease '{self._lease_name}' renewal encountered a conflict"
                    )
            except asyncio.CancelledError:
                raise
            except Exception as err:
                self._renew_error = err
                return

    def _begin_renew_loop(self) -> None:
        self._renew_error = None
        self._renew_stop = asyncio.Event()
        self._renew_task = asyncio.create_task(self._renew_loop())

    async def acquire(self, deadline: float) -> bool:
        """Attempt to acquire the lock until the specified deadline.

        Parameters
        ----------
        deadline : float
            The absolute time (in seconds from the event loop's time) at which to stop
            trying to acquire the lock.

        Returns
        -------
        bool
            `True` if the lock was successfully acquired, or `False` if the deadline
            was reached before the lock could be acquired.
        """
        loop = asyncio.get_event_loop()
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                return False
            if await self.try_acquire(remaining):
                return True

            # wait before retrying
            await asyncio.sleep(
                min(LEASE_POLL_SECONDS, max(0.0, deadline - loop.time()))
            )

    async def try_acquire(self, timeout: float = LEASE_QUERY_TIMEOUT) -> bool:
        """Attempt to acquire or steal the cluster lease once, without retrying.

        Parameters
        ----------
        timeout : float
            The maximum number of seconds to wait for Kubernetes API calls before
            giving up and raising an `OSError`.

        Returns
        -------
        bool
            `True` if the lock was successfully acquired, or `False` if the lock is
            currently held by another process or if the timeout was reached before the
            lock could be acquired.
        """
        # get current lease state, if any
        try:
            lease = await self._get_lease(timeout=timeout)
        except TimeoutExpired as err:
            raise OSError(
                "timed out while querying Kubernetes lease state; ensure "
                "MicroK8s is reachable and responding"
            ) from err

        now = datetime.now(UTC)

        # fresh lease
        if lease is None:
            try:
                created = await self._create_lease(now, timeout=timeout)
            except TimeoutExpired as err:
                raise OSError(
                    "timed out while creating Kubernetes lease; ensure "
                    "MicroK8s is reachable and responding"
                ) from err
            if created:
                self._begin_renew_loop()
                return True
            return False

        # existing lease; update or steal if expired
        if lease.holder == self._holder or (
            lease.holder is None or
            lease.renew_time is None or
            (now - lease.renew_time).total_seconds() > max(1, lease.duration_seconds)
        ):
            try:
                updated = await self._replace_lease(
                    lease,
                    now,
                    timeout=timeout
                )
            except TimeoutExpired as err:
                raise OSError(
                    "timed out while updating Kubernetes lease; ensure "
                    "MicroK8s is reachable and responding"
                ) from err
            if updated:
                self._begin_renew_loop()
                return True
        return False

    async def release(self) -> None:
        """Release the cluster lease if it is currently held, and stop the renewal
        loop.
        """
        if self._renew_stop is not None:
            self._renew_stop.set()
        if self._renew_task is not None:
            self._renew_task.cancel()
            try:
                await self._renew_task
            except asyncio.CancelledError:
                pass
            except Exception:
                pass
        self._renew_stop = None
        self._renew_task = None

    def pop_error(self) -> Exception | None:
        """Return any exception encountered during lease renewal, if applicable, and
        clear the deferred error state.

        Returns
        -------
        Exception | None
            The exception encountered during lease renewal, or `None` if no errors were
            encountered.
        """
        error = self._renew_error
        self._renew_error = None
        return error


class Lock:
    """A re-entrant, asynchronous lock that supports both local filesystem and
    cluster-wide mutual exclusion.

    Parameters
    ----------
    path : Path
        The path used to derive lock identity and resources.  In local mode, this is
        the lock file path.  In cluster mode, this path is hashed into a stable lease
        name.
    timeout : float
        The maximum number of seconds to wait for the lock to be acquired before
        raising a `TimeoutError`.  Note that due to the shared lock instances across
        the process, this timeout may be ignored in favor of a larger timeout from a
        previous lock acquisition with the same path/mode pair.
    mode : Literal["local", "cluster"]
        The backend used to perform cross-process synchronization.  "local" uses host
        OS file locking primitives, while "cluster" uses a Kubernetes Lease in the
        local MicroK8s runtime.

    Raises
    ------
    OSError
        If the lock path is invalid for the selected lock backend.
    TimeoutError
        If the lock cannot be acquired within the specified timeout period upon entering
        the context manager.
    """
    path: Path
    timeout: float
    mode: LockMode
    _key: tuple[str, LockMode]
    _backend: _LocalFileLock | _ClusterLeaseLock
    _owner: asyncio.Task[Any] | None
    _depth: int

    def __new__(
        cls,
        path: Path,
        timeout: float,
        mode: LockMode,
    ) -> Lock:
        if timeout < 0:
            raise TimeoutError(f"Lock timeout must be non-negative, got {timeout}")
        path = path.expanduser().resolve()
        if mode == "local" and path.exists() and path.is_dir():
            raise OSError(
                "local lock path must be a file, but a directory already exists: "
                f"{path}"
            )

        # allow re-entrancy with unique lock instances per owning task
        with LOCK_GUARD:
            key = (str(path), mode)
            self = LOCKS.get(key)
            if self is None:
                self = super().__new__(cls)
                self.path = path
                self.timeout = timeout
                self.mode = mode
                self._key = key
                self._backend = (
                    _LocalFileLock(path)
                    if mode == "local" else
                    _ClusterLeaseLock(path)
                )
                self._owner = None
                self._depth = 0
                LOCKS[key] = self
            elif self.timeout < timeout:
                self.timeout = timeout
        return self

    @staticmethod
    def _get_owner() -> asyncio.Task[Any]:
        try:
            task = asyncio.current_task()
        except RuntimeError:
            task = None
        if task is None:
            raise RuntimeError("Lock requires an active asyncio Task")
        return task

    async def lock(self) -> Self:
        """Acquire this lock, waiting for this lock's timeout if needed."""
        owner = self._get_owner()
        loop = asyncio.get_event_loop()
        deadline = loop.time() + self.timeout

        # fast-path: same task re-entrancy
        while True:
            with LOCK_GUARD:
                if self._owner is None:
                    self._owner = owner
                    self._depth = 1
                    break
                if self._owner == owner:
                    self._depth += 1
                    return self
            if loop.time() >= deadline:
                raise TimeoutError(f"could not acquire lock within {self.timeout} seconds")
            await asyncio.sleep(LOCK_POLL_SECONDS)

        # slow path: acquire via backend
        try:
            if not await self._backend.acquire(deadline):
                raise TimeoutError(f"could not acquire lock within {self.timeout} seconds")
            return self
        except Exception:
            try:
                await self._backend.release()
            except Exception:
                pass
            finally:
                with LOCK_GUARD:
                    self._owner = None
                    self._depth = 0
            raise

    async def try_lock(self) -> bool:
        """Attempt to acquire this lock immediately, without waiting on contention.

        Returns
        -------
        bool
            `True` if the lock was successfully acquired, or `False` if the lock is
            currently held by another task or process.
        """
        owner = self._get_owner()

        # fast-paths: re-entrant success or immediate contention failure
        with LOCK_GUARD:
            if self._owner == owner:
                self._depth += 1
                return True
            if self._owner is not None:
                return False
            self._owner = owner
            self._depth = 1

        # slow path: single backend attempt only
        try:
            return await self._backend.try_acquire()
        except Exception:
            try:
                await self._backend.release()
            except Exception:
                pass
            raise
        finally:
            with LOCK_GUARD:
                if self._owner == owner:
                    self._owner = None
                    self._depth = 0

    async def unlock(self, *, suppress_backend_errors: bool = False) -> None:
        """Release this lock once.  Re-entrant lock depth is decremented."""
        owner = self._get_owner()
        with LOCK_GUARD:
            if self._owner != owner or self._depth < 1:
                raise RuntimeError("lock is not held by the current owner")
            self._depth -= 1
            if self._depth > 0:
                return  # re-entrant case

        try:
            await self._backend.release()
        except Exception as err:  # pylint: disable=broad-except
            if not suppress_backend_errors:
                raise err
        finally:
            with LOCK_GUARD:
                self._owner = None
                self._depth = 0
                LOCKS.pop(self._key, None)

        # report deferred errors, if any
        if not suppress_backend_errors:
            deferred_error = self._backend.pop_error()
            if deferred_error is not None:
                raise deferred_error

    async def __aenter__(self) -> Self:
        return await self.lock()

    async def __aexit__(
        self,
        exc_type: type[BaseException] | None,
        exc_value: BaseException | None,
        traceback: TracebackType | None
    ) -> None:
        await self.unlock(suppress_backend_errors=exc_value is not None)

    def __bool__(self) -> bool:
        with LOCK_GUARD:
            return self._depth > 0

    def __repr__(self) -> str:
        return (
            f"Lock(path={repr(self.path)}, timeout={self.timeout}, mode={repr(self.mode)})"
        )
