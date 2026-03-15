"""Utility functions for running subprocesses and handling command-line interactions."""
from __future__ import annotations

import asyncio
import json
import os
import pwd
import re
import shlex
import shutil
import subprocess
import sys
import time
import threading
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from types import TracebackType
from typing import Any, Callable, Mapping, Self, TextIO


import psutil


#pylint: disable=redefined-builtin


class CompletedProcess(subprocess.CompletedProcess[str]):
    """A subclass of `subprocess.CompletedProcess` that prints the command and its
    output when converted to a string.
    """
    def __str__(self) -> str:
        out = [
            f"Exit code {self.returncode} from command:\n\n"
            f"    {' '.join(shlex.quote(a) for a in self.args)}"
        ]
        if self.stderr:
            out.append(self.stderr.strip())
        return "\n\n".join(out)


class CommandError(subprocess.CalledProcessError):
    """A subclass of `subprocess.CalledProcessError` that prints the command and its
    output when converted to a string.
    """
    def __str__(self) -> str:
        out = [
            f"Exit code {self.returncode} from command:\n\n"
            f"    {' '.join(shlex.quote(a) for a in self.cmd)}"
        ]
        if self.stderr:
            out.append(self.stderr.strip())
        return "\n\n".join(out)


class TimeoutExpired(subprocess.TimeoutExpired):
    """A subclass of `subprocess.TimeoutExpired` that prints the command and any captured
    output when converted to a string.
    """
    def __str__(self) -> str:
        out = [
            f"Command timed out after {self.timeout} seconds:\n\n"
            f"    {' '.join(shlex.quote(a) for a in self.cmd)}"
        ]
        if self.output:
            out.append(self.output.strip())
        if self.stderr:
            out.append(str(self.stderr.strip()))
        return "\n\n".join(out)


def can_escalate() -> bool:
    """
    Returns
    -------
    bool
        True if privilege escalation is possible on the current system, false
        otherwise.
    """
    if os.name != "posix":
        return False
    return bool(shutil.which("sudo") or shutil.which("doas"))


def sudo(argv: list[str], *, non_interactive: bool = False) -> list[str]:
    """Return a command with privilege escalation prepended when needed.

    Parameters
    ----------
    argv : list[str]
        The command and its arguments.
    non_interactive : bool, optional
        If True, add non-interactive flags for the selected escalator so it fails
        immediately instead of prompting for a password.

    Returns
    -------
    list[str]
        A new list containing either the original command (when no escalation is
        needed/available) or the escalated command.

    Notes
    -----
    Escalation is only attempted on POSIX systems for non-root users.  The selection
    order is `sudo`, then `doas`.
    """
    # no-op outside POSIX, when already root, or when no supported escalator is found
    if os.name != "posix" or os.geteuid() == 0:
        return argv.copy()
    escalator = None
    if shutil.which("sudo"):
        escalator = "sudo"
    elif shutil.which("doas"):
        escalator = "doas"
    if escalator is None:
        return argv.copy()

    out = [escalator]
    if non_interactive:
        out.append("-n")
    out.extend(argv)
    return out


def confirm(prompt: str, *, assume_yes: bool = False) -> bool:
    """Ask the user for a yes/no confirmation for a given prompt.

    Parameters
    ----------
    prompt : str
        The prompt to display to the user.
    assume_yes : bool, optional
        If True, automatically return True without prompting the user.  Default is
        False.

    Returns
    -------
    bool
        True if the user confirmed yes, false otherwise.
    """
    if assume_yes:
        return True
    try:
        response = input(prompt).strip().lower()
    except EOFError:
        return False
    return response in {"y", "yes"}


async def _tee(src: asyncio.StreamReader | None, sink: TextIO, buf_list: list[str]) -> None:
    if src is None:
        return
    while True:
        chunk = await src.readline()
        if not chunk:
            break
        text = chunk.decode("utf-8", errors="replace")
        buf_list.append(text)
        try:
            sink.write(text)
            sink.flush()
        except OSError:
            pass


async def _write_stdin(dst: asyncio.StreamWriter | None, data: str) -> None:
    if dst is None:
        return
    try:
        dst.write(data.encode("utf-8", errors="replace"))
        await dst.drain()
    except (BrokenPipeError, ConnectionResetError, OSError):
        pass
    finally:
        try:
            dst.close()
            await dst.wait_closed()
        except (AttributeError, RuntimeError, OSError):
            pass


async def run(
    argv: list[str],
    *,
    check: bool = True,
    capture_output: bool | None = False,
    input: str | None = None,
    timeout: float | None = None,
    cwd: Path | None = None,
    env: Mapping[str, str] | None = None,
) -> CompletedProcess:
    """An asynchronous subprocess wrapper that defaults to text mode and properly
    formats errors.

    Parameters
    ----------
    argv : list[str]
        The command and its arguments to run.
    check : bool, optional
        Whether to raise a `CommandError` if the command fails (default is True).  If
        false, then any errors will be ignored.
    capture_output : bool | None, optional
        If true, then all output will be redirected to the returned `CompletedProcess`
        or `CommandError`, and excluded from the inherited stdout/stderr streams.  If
        false (the default), then the opposite is the case, and the returned
        `CompletedProcess` or `CommandError` will not include any captured output.  If
        None, then output will be "tee'd" to both the console and the returned objects
        simultaneously.  Note that teeing output in this way may break TTY behavior for
        some commands, and is not recommended for interactive use.
    input : str | None, optional
        Input to send to the command's stdin (default is None).
    timeout : float | None, optional
        An optional timeout in seconds to wait for the command to complete before
        raising a `TimeoutExpired` exception.  Default is None, which means to wait
        indefinitely.
    cwd : Path | None, optional
        An optional working directory to run the command in.  If None (the default),
        then the current working directory will be used.
    env : Mapping[str, str] | None, optional
        An optional environment dictionary to use for the command.  If None (the
        default), then the current process's environment will be used.

    Returns
    -------
    CompletedProcess
        The completed process result.

    Raises
    ------
    CommandError
        If the command fails and `check` is True.
    TimeoutExpired
        If the command does not complete within the specified timeout.
    OSError
        If we failed to open the subprocess or its output streams.
    """
    # capture_output=False -> inherit terminal streams and do not capture output
    if capture_output is False:
        proc = await asyncio.create_subprocess_exec(
            *argv,
            stdin=asyncio.subprocess.PIPE if input is not None else None,
            stdout=None,
            stderr=None,
            cwd=cwd,
            env=env,
        )
        try:
            input_bytes = None if input is None else input.encode("utf-8", errors="replace")
            try:
                await asyncio.wait_for(proc.communicate(input_bytes), timeout=timeout)
            except asyncio.TimeoutError as err:
                proc.kill()
                await proc.communicate()
                raise TimeoutExpired(
                    cmd=argv,
                    timeout=timeout or 0.0,
                    output=None,
                    stderr=None
                ) from err
        finally:
            if proc.returncode is None:
                proc.kill()
                await proc.wait()

        assert proc.returncode is not None
        result = CompletedProcess(argv, proc.returncode, "", "")
        if check and result.returncode != 0:
            raise CommandError(result.returncode, argv, "", "")
        return result

    # capture_output=True -> full capture with no tee
    if capture_output is True:
        proc = await asyncio.create_subprocess_exec(
            *argv,
            stdin=asyncio.subprocess.PIPE if input is not None else None,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
            cwd=cwd,
            env=env,
        )
        try:
            input_bytes = None if input is None else input.encode("utf-8", errors="replace")
            try:
                stdout_raw, stderr_raw = await asyncio.wait_for(
                    proc.communicate(input_bytes),
                    timeout=timeout,
                )
            except asyncio.TimeoutError as err:
                proc.kill()
                stdout_raw, stderr_raw = await proc.communicate()
                raise TimeoutExpired(
                    cmd=argv,
                    timeout=timeout or 0.0,
                    output=stdout_raw.decode("utf-8", errors="replace") or None,
                    stderr=stderr_raw.decode("utf-8", errors="replace") or None,
                ) from err
        finally:
            if proc.returncode is None:
                proc.kill()
                await proc.wait()

        assert proc.returncode is not None
        stdout_text = stdout_raw.decode("utf-8", errors="replace")
        stderr_text = stderr_raw.decode("utf-8", errors="replace")
        result = CompletedProcess(argv, proc.returncode, stdout_text, stderr_text)
        if check and result.returncode != 0:
            raise CommandError(result.returncode, argv, stdout_text, stderr_text)
        return result

    # capture_output=None -> tee streams while capturing for return/errors
    proc = await asyncio.create_subprocess_exec(
        *argv,
        stdin=asyncio.subprocess.PIPE if input is not None else None,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
        cwd=cwd,
        env=env,
    )
    stdout_lines: list[str] = []
    stderr_lines: list[str] = []
    tasks: list[asyncio.Task[None]] = [
        asyncio.create_task(_tee(proc.stdout, sys.stdout, stdout_lines)),
        asyncio.create_task(_tee(proc.stderr, sys.stderr, stderr_lines)),
    ]
    if input is not None:
        tasks.append(asyncio.create_task(_write_stdin(proc.stdin, input)))

    try:
        try:
            await asyncio.wait_for(proc.wait(), timeout=timeout)
        except asyncio.TimeoutError as err:
            proc.kill()
            await proc.wait()
            for task in tasks:
                await task
            stdout_text = "".join(stdout_lines)
            stderr_text = "".join(stderr_lines)
            raise TimeoutExpired(
                cmd=argv,
                timeout=timeout or 0.0,
                output=stdout_text or None,
                stderr=stderr_text or None,
            ) from err
    finally:
        if proc.returncode is None:
            proc.kill()
            await proc.wait()
        for task in tasks:
            await task

    stdout_text = "".join(stdout_lines)
    stderr_text = "".join(stderr_lines)
    assert proc.returncode is not None
    result = CompletedProcess(argv, proc.returncode, stdout_text, stderr_text)
    if check and result.returncode != 0:
        raise CommandError(result.returncode, argv, stdout_text, stderr_text)
    return result


SANITIZE = re.compile(r"[^a-zA-Z0-9._]+")


def sanitize_name(name: str, *, replace: str = "_") -> str:
    """Replace any characters in the given name that are not alphanumeric, '.', or '_'
    with the specified replacement character, and then strip leading and trailing
    replacement characters from the result.

    Parameters
    ----------
    name : str
        The name to sanitize.
    replace : str, optional
        The character to use as a replacement for invalid characters.  Default is '_'.

    Returns
    -------
    str
        The sanitized name.
    """
    return SANITIZE.sub(replace, name).strip(replace)


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


@dataclass(frozen=True)
class User:
    """A simple structure representing a user identity by user ID and group ID.

    Attributes
    ----------
    uid : int
        The numeric user ID.
    gid : int
        The numeric group ID.
    name : str
        The username.
    home : Path
        The path to the user's home directory.
    """
    uid: int = field(init=False)
    gid: int = field(init=False)
    name: str = field(init=False)
    home: Path = field(init=False)

    def __post_init__(self) -> None:
        euid = os.geteuid()
        sudo_uid = os.environ.get("SUDO_UID")
        sudo_user = os.environ.get("SUDO_USER")
        if euid == 0 and sudo_uid:
            object.__setattr__(self, 'uid', int(sudo_uid))
            pw = pwd.getpwuid(self.uid)
            object.__setattr__(self, 'gid', pw.pw_gid)
            object.__setattr__(self, 'name', sudo_user or pw.pw_name)
            object.__setattr__(self, 'home', Path(pw.pw_dir))
        else:
            object.__setattr__(self, 'uid', os.getuid())
            pw = pwd.getpwuid(self.uid)
            object.__setattr__(self, 'gid', pw.pw_gid)
            object.__setattr__(self, 'name', pw.pw_name)
            object.__setattr__(self, 'home', Path(pw.pw_dir))


def mkdir_private(path: Path) -> None:
    """Create a directory with private permissions (0700) if it does not already exist.

    Parameters
    ----------
    path : Path
        The path to create.
    """
    path.mkdir(parents=True, exist_ok=True)
    try:
        path.chmod(0o700)
    except OSError:
        pass


def atomic_write_text(
    path: Path,
    text: str,
    encoding: str | None = None,
    private: bool = False,
) -> None:
    """Atomically write text to a file, avoiding race conditions and partial writes.

    Parameters
    ----------
    path : Path
        The path to write to.
    text : str
        The text to write.
    encoding : str | None, optional
        The text encoding to use (default is None, which uses the system default).
    private : bool, optional
        Whether to set private permissions (0600) on the written file, by default False.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(f"{path.name}.tmp.{uuid.uuid4().hex}")
    tmp.write_text(text, encoding=encoding)
    try:
        with tmp.open("r+", encoding=encoding) as f:
            f.flush()
            os.fsync(f.fileno())
    except OSError:
        pass
    if private:
        try:
            tmp.chmod(0o600)
        except OSError:
            pass
    tmp.replace(path)


def atomic_write_bytes(path: Path, data: bytes, private: bool = False) -> None:
    """Atomically write bytes to a file, avoiding race conditions and partial writes.

    Parameters
    ----------
    path : Path
        The path to write to.
    data : bytes
        The bytes to write.
    private : bool, optional
        Whether to set private permissions (0600) on the written file, by default False.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(f"{path.name}.tmp.{uuid.uuid4().hex}")
    tmp.write_bytes(data)
    try:
        with tmp.open("r+b") as f:
            f.flush()
            os.fsync(f.fileno())
    except OSError:
        pass
    if private:
        try:
            tmp.chmod(0o600)
        except OSError:
            pass
    tmp.replace(path)
