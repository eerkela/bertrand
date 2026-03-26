"""Utility functions for running subprocesses and handling command-line interactions."""
from __future__ import annotations

import asyncio
import json
import os
import shutil
import time
import threading
from pathlib import Path
from types import TracebackType
from typing import Any, Self

import psutil


# TODO: Lock should probably implement a `try_lock()` method that attempts to acquire
# the lock and returns immediately with a boolean success value, instead of blocking.


LOCK_GUARD = threading.RLock()
LOCK_TIMEOUT: float = 30.0
LOCKS: dict[str, Lock] = {}


class Lock:
    """A lock that can be used from both sync and async contexts.

    This lock uses a filesystem directory as its cross-process mutex and tracks
    in-process re-entrancy by owner identity.  Owner identity is task-first:
    if called from an asyncio task, ownership is associated with that task;
    otherwise ownership is associated with the current thread.

    Parameters
    ----------
    path : Path
        The path to use for the lock.  This should be a directory that does not
        already exist.  A `lock.json` file will be created within this directory to
        track the owning process and clear stale locks.  The directory and its
        contents will be removed when the outermost lock in this process is released.
    timeout : float, optional
        The maximum number of seconds to wait for the lock to be acquired before
        raising a `TimeoutError`.  Default is 30.0 seconds.  Note that due to the
        shared lock instances across the process, this timeout may be ignored in
        favor of a larger timeout from a previous lock acquisition with the same path.
        The result is that the timeout will monotonically increase for locks with the
        same path, and will always reflect the maximum timeout across all acquisitions
        of that lock within the process.

    Raises
    ------
    OSError
        If the lock path already exists and is not a directory, or if it contains files
        other than `lock.json`.
    TimeoutError
        If the lock cannot be acquired within the specified timeout period upon entering
        the context manager.
    """
    path: Path
    timeout: float
    _lock: Path
    _pid: int
    _create_time: float
    _owner: asyncio.Task[Any] | int | None
    _depth: int

    def __new__(cls, path: Path, timeout: float = LOCK_TIMEOUT) -> Lock:
        if timeout < 0:
            raise TimeoutError(f"Lock timeout must be non-negative, got {timeout}")
        if path.exists():
            if not path.is_dir():
                raise OSError(f"Lock path must be a directory: {path}")
            for child in path.iterdir():
                if child != path / "lock.json":
                    raise OSError(
                        f"Lock path must be a directory containing only 'lock.json': {path}"
                    )

        path = path.expanduser().resolve()
        path_str = str(path)
        with LOCK_GUARD:
            self = LOCKS.get(path_str)
            if self is None:
                self = super().__new__(cls)
                self.path = path
                self.timeout = timeout
                self._lock = path / "lock.json"
                self._pid = os.getpid()
                self._create_time = psutil.Process(self._pid).create_time()
                self._owner = None
                self._depth = 0
                LOCKS[path_str] = self
            elif self.timeout < timeout:
                self.timeout = timeout  # use max timeout
        return self

    @staticmethod
    def _owner_token() -> asyncio.Task[Any] | int:
        """Resolve the current in-process lock owner identity.

        Returns
        -------
        asyncio.Task[Any] | int
            The current task when running inside an asyncio task, otherwise the
            current thread ID.
        """
        try:
            task = asyncio.current_task()
        except RuntimeError:
            task = None
        if task is not None:
            return task
        return threading.get_ident()

    def _is_stale(self, data: dict[str, Any]) -> bool:
        owner_pid = data.get("pid")
        owner_start = data.get("pid_start")
        tolerance = 0.001  # floating point tolerance

        if (
            owner_pid != self._pid and
            isinstance(owner_pid, int) and
            isinstance(owner_start, (int, float))
        ):
            try:
                return (
                    not psutil.pid_exists(owner_pid) or
                    psutil.Process(owner_pid).create_time() > (owner_start + tolerance)
                )
            except (psutil.AccessDenied, psutil.ZombieProcess):
                return False
            except psutil.NoSuchProcess:
                return True

        return False

    def _release(self, owner: asyncio.Task[Any] | int) -> None:
        with LOCK_GUARD:
            if self._owner != owner or self._depth < 1:
                raise RuntimeError("lock is not held by the current owner")
            self._depth -= 1
            if self._depth > 0:
                return
            self._owner = None

        try:
            shutil.rmtree(self.path, ignore_errors=True)
        finally:
            with LOCK_GUARD:
                LOCKS.pop(str(self.path.resolve()), None)

    def __enter__(self) -> Self:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        owner = self._owner_token()
        start = time.time()

        # fast path: in-process ownership / re-entrancy
        while True:
            with LOCK_GUARD:
                if self._owner is None:
                    break
                if self._owner == owner:
                    self._depth += 1
                    return self
            if (time.time() - start) > self.timeout:
                raise TimeoutError(
                    f"could not acquire environment lock within {self.timeout} seconds"
                )
            time.sleep(0.1)

        # slow path: cross-process lock via filesystem
        unreadable_since: float | None = None
        unreadable_grace = 5.0
        while True:
            try:
                self.path.mkdir(parents=True)  # atomic
            except FileExistsError as err:
                now = time.time()
                try:
                    owner_data = json.loads(self._lock.read_text(encoding="utf-8"))
                    if not isinstance(owner_data, dict):
                        shutil.rmtree(self.path, ignore_errors=True)
                        unreadable_since = None
                        continue
                except Exception:  # pylint: disable=broad-except
                    if unreadable_since is None:
                        unreadable_since = now
                    if (now - unreadable_since) > unreadable_grace:
                        shutil.rmtree(self.path, ignore_errors=True)
                        unreadable_since = None
                        continue
                    if (now - start) > self.timeout:
                        raise TimeoutError(
                            f"could not acquire environment lock within {self.timeout} "
                            "seconds"
                        ) from err
                    time.sleep(0.1)
                    continue
                unreadable_since = None
                if self._is_stale(owner_data):
                    shutil.rmtree(self.path, ignore_errors=True)
                    continue

                if (now - start) > self.timeout:
                    detail = (
                        f"\nlock owner: {json.dumps(owner_data, indent=2)}"
                        if owner_data else ""
                    )
                    raise TimeoutError(
                        f"could not acquire environment lock within {self.timeout} "
                        f"seconds{detail}"
                    ) from err
                time.sleep(0.1)
                continue

            try:
                self._lock.write_text(json.dumps({
                    "pid": self._pid,
                    "pid_start": self._create_time,
                }, indent=2) + "\n", encoding="utf-8")
            except Exception:
                shutil.rmtree(self.path, ignore_errors=True)
                raise
            break

        with LOCK_GUARD:
            self._owner = owner
            self._depth = 1
        return self

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc_value: BaseException | None,
        traceback: TracebackType | None
    ) -> None:
        self._release(self._owner_token())

    async def __aenter__(self) -> Self:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        owner = self._owner_token()
        start = time.time()

        # fast path: in-process ownership / re-entrancy
        while True:
            with LOCK_GUARD:
                if self._owner is None:
                    break
                if self._owner == owner:
                    self._depth += 1
                    return self
            if (time.time() - start) > self.timeout:
                raise TimeoutError(
                    f"could not acquire environment lock within {self.timeout} seconds"
                )
            await asyncio.sleep(0.1)

        # slow path: cross-process lock via filesystem with cooperative wait
        unreadable_since: float | None = None
        unreadable_grace = 5.0
        while True:
            try:
                self.path.mkdir(parents=True)  # atomic
            except FileExistsError as err:
                now = time.time()
                try:
                    owner_data = json.loads(self._lock.read_text(encoding="utf-8"))
                    if not isinstance(owner_data, dict):
                        shutil.rmtree(self.path, ignore_errors=True)
                        unreadable_since = None
                        continue
                except Exception:  # pylint: disable=broad-except
                    if unreadable_since is None:
                        unreadable_since = now
                    if (now - unreadable_since) > unreadable_grace:
                        shutil.rmtree(self.path, ignore_errors=True)
                        unreadable_since = None
                        continue
                    if (now - start) > self.timeout:
                        raise TimeoutError(
                            f"could not acquire environment lock within {self.timeout} seconds"
                        ) from err
                    await asyncio.sleep(0.1)
                    continue
                unreadable_since = None
                if self._is_stale(owner_data):
                    shutil.rmtree(self.path, ignore_errors=True)
                    continue

                if (now - start) > self.timeout:
                    detail = (
                        f"\nlock owner: {json.dumps(owner_data, indent=2)}"
                        if owner_data else ""
                    )
                    raise TimeoutError(
                        f"could not acquire environment lock within {self.timeout} seconds{detail}"
                    ) from err
                await asyncio.sleep(0.1)
                continue

            try:
                self._lock.write_text(json.dumps({
                    "pid": self._pid,
                    "pid_start": self._create_time,
                }, indent=2) + "\n", encoding="utf-8")
            except Exception:
                shutil.rmtree(self.path, ignore_errors=True)
                raise
            break

        with LOCK_GUARD:
            self._owner = owner
            self._depth = 1
        return self

    async def __aexit__(
        self,
        exc_type: type[BaseException] | None,
        exc_value: BaseException | None,
        traceback: TracebackType | None
    ) -> None:
        self._release(self._owner_token())

    def __bool__(self) -> bool:
        with LOCK_GUARD:
            return self._depth > 0

    def __repr__(self) -> str:
        return f"Lock(path={repr(self.path)}, timeout={self.timeout})"
