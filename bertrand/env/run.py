"""Utility functions for running subprocesses and handling command-line interactions."""
import json
import os
import pwd
import re
import shlex
import shutil
import subprocess
import sys
import threading
import time
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from types import TracebackType
from typing import Mapping, TextIO

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
    def __init__(self, returncode: int, cmd: list[str], stdout: str, stderr: str) -> None:
        super().__init__(returncode, cmd, stdout, stderr)

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
    def __init__(self, cmd: list[str], timeout: float, stdout: str, stderr: str) -> None:
        super().__init__(cmd, timeout, stdout, stderr)

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


def _tee(src: TextIO, sink: TextIO, buf_list: list[str]) -> None:
    for line in src:
        buf_list.append(line)
        sink.write(line)
        sink.flush()
    src.close()


def _write_stdin(dst: TextIO | None, data: str) -> None:
    if dst is None:
        return
    try:
        dst.write(data)
        dst.flush()
    except (BrokenPipeError, OSError):
        pass
    finally:
        try:
            dst.close()
        except OSError:
            pass


def run(
    argv: list[str],
    *,
    check: bool = True,
    capture_output: bool | None = False,
    input: str | None = None,
    timeout: float | None = None,
    cwd: Path | None = None,
    env: Mapping[str, str] | None = None,
) -> CompletedProcess:
    """A wrapper around `subprocess.run` that defaults to text mode and properly
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
        None, then a separate thread will be used to "tee" output to both the console
        and the returned objects simultaneously.  Note that teeing output in this way
        may break TTY behavior for some commands.
    input : str | None, optional
        Input to send to the command's stdin (default is None).
    timeout : float | None, optional
        An optional timeout in seconds to wait for the command to complete before
        raising a `subprocess.TimeoutExpired` exception.  Default is None, which means
        to wait indefinitely.
    cwd : Path | None, optional
        An optional working directory to run the command in.  If None (the default),
        then the current working directory will be used.
    env : Mapping[str, str] | None, optional
        An optional environment dictionary to use for the command.  If None (the
        default), then the current process's environment will be used.

    Returns
    -------
    subprocess.CompletedProcess[str]
        The completed process result.

    Raises
    ------
    CommandError
        If the command fails and `check` is True.  The text of the error reflects
        the error code, original command, and captured output from stderr and stdout.
    TimeoutExpired
        If the command does not complete within the specified timeout.
    OSError
        If we failed to open the subprocess or its output streams.
    """
    try:
        if capture_output is not None:
            cp = subprocess.run(
                argv,
                check=check,
                capture_output=capture_output,
                text=True,
                input=input,
                timeout=timeout,
                cwd=cwd,
                env=env,
            )
            return CompletedProcess(
                cp.args,
                cp.returncode,
                cp.stdout or "",
                cp.stderr or "",
            )

        # tee stdout/stderr to console while capturing both for error reporting
        # streams are consumed in dedicated threads to avoid pipe deadlocks
        stdout_lines: list[str] = []
        stderr_lines: list[str] = []
        rc: int | None = None
        p = subprocess.Popen(
            argv,
            stdin=subprocess.PIPE if input is not None else None,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            errors="replace",
            bufsize=1,  # line-buffered in text mode
            cwd=cwd,
            env=env,
        )
        try:
            if p.stdout is None or p.stderr is None:
                raise OSError("failed to open subprocess output streams")

            # start reader threads immediately so child output is drained live
            threads = [
                threading.Thread(
                    target=_tee,
                    args=(p.stdout, sys.stdout, stdout_lines),
                    daemon=True
                ),
                threading.Thread(
                    target=_tee,
                    args=(p.stderr, sys.stderr, stderr_lines),
                    daemon=True
                )
            ]
            for t in threads:
                t.start()

            # feed stdin asynchronously if provided so reads/writes do not block each other
            if input is not None:
                t_in = threading.Thread(
                    target=_write_stdin,
                    args=(p.stdin, input),
                    daemon=True
                )
                threads.append(t_in)
                t_in.start()

            # wait for process completion or timeout while output is being drained
            try:
                rc = p.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                p.kill()
                rc = p.wait()
                raise
            finally:
                for t in threads:
                    t.join()

        # close process
        finally:
            if p.poll() is None:
                p.kill()
                p.wait()

    except subprocess.TimeoutExpired as err:
        raise TimeoutExpired(
            cmd=argv,
            timeout=err.timeout,
            stdout=err.output or "".join(stdout_lines),
            stderr=str(err.stderr) or "".join(stderr_lines),
        ) from err
    except subprocess.CalledProcessError as err:
        raise CommandError(
            returncode=err.returncode,
            cmd=argv,
            stdout=err.stdout or "".join(stdout_lines),
            stderr=err.stderr or "".join(stderr_lines),
        ) from err

    # construct result
    assert rc is not None
    result = CompletedProcess(argv, rc, "".join(stdout_lines), "".join(stderr_lines))
    if check and rc != 0:
        raise CommandError(rc, argv, result.stdout, result.stderr)
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


# TODO: sudo_prefix should maybe be rethought to be more reliable and ideally just
# not necessary at all.


def sudo_prefix() -> list[str]:
    """Return a base command prefix that uses `sudo` if the current user is not already
    root.

    Returns
    -------
    list[str]
        An empty list or a list containing the super-user command for the current OS.
    """
    if os.name != "posix" or os.geteuid() == 0 or not shutil.which("sudo"):
        return []
    preserve = "DOCKER_HOST,DOCKER_CONTEXT,DOCKER_CONFIG"
    return ["sudo", f"--preserve-env={preserve}"]


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


class LockDir:
    """A simple context manager that implements a file-based, cross-platform mutual
    exclusion lock for atomic file operations.

    Parameters
    ----------
    path : Path
        The path to use for the lock.  This should be a directory that does not
        already exist.  A `lock.json` file will be created within this directory to
        track the owning process and clear stale locks.  The directory and its
        contents will be removed when the lock is released.
    timeout : int, optional
        The maximum number of seconds to wait for the lock to be acquired before
        raising a `TimeoutError`.  Default is 30 seconds.

    Attributes
    ----------
    path : Path
        The path to the directory to use for the lock.
    lock : Path
        The path to the lock file within the lock directory.
    pid : int
        The process ID of the owning process.
    create_time : float
        The creation time for the owning process.
    timeout : int
        The maximum number of seconds to wait for the lock to be acquired.
    depth : int
        The current depth of nested context managers using this lock, in order to allow
        re-entrant locking within the same process.
    """
    path: Path
    lock: Path
    pid: int
    create_time: float
    timeout: int
    depth: int

    def __init__(self, path: Path, timeout: int = 30) -> None:
        assert not path.exists() or path.is_dir(), "Lock path must be a directory"
        self.path = path
        self.lock = path / "lock.json"
        self.pid = os.getpid()
        self.create_time = psutil.Process(self.pid).create_time()
        self.timeout = timeout
        self.depth = 0

    def __enter__(self) -> None:
        # allow nested context managers without deadlocking
        self.depth += 1
        if self.depth > 1:
            return

        # attempt to acquire lock
        start = time.time()
        unreadable_owner_since: float | None = None
        unreadable_owner_grace = 5.0
        while True:
            try:
                self.path.mkdir(parents=True)  # atomic
            except FileExistsError as err:
                # another process holds the lock - check if it's stale
                now = time.time()
                try:
                    owner = json.loads(self.lock.read_text(encoding="utf-8"))
                    if not isinstance(owner, dict):
                        shutil.rmtree(self.path, ignore_errors=True)
                        unreadable_owner_since = None
                        continue
                except Exception:  # pylint: disable=broad-except
                    if unreadable_owner_since is None:
                        unreadable_owner_since = now
                    if (now - unreadable_owner_since) > unreadable_owner_grace:
                        shutil.rmtree(self.path, ignore_errors=True)
                        unreadable_owner_since = None
                        continue
                    if (now - start) > self.timeout:
                        raise TimeoutError(
                            f"could not acquire environment lock within {self.timeout} seconds"
                        ) from err
                    time.sleep(0.1)
                    continue
                unreadable_owner_since = None

                # check whether owning process is still alive
                owner_pid = owner.get("pid")
                owner_start = owner.get("pid_start")
                tolerance = 0.001  # tolerate floating point precision issues
                owner_stale = False
                if isinstance(owner_pid, int) and isinstance(owner_start, (int, float)):
                    if not psutil.pid_exists(owner_pid):
                        owner_stale = True
                    else:
                        owner_create_time: float | None = None
                        try:
                            owner_create_time = psutil.Process(owner_pid).create_time()
                        except (psutil.AccessDenied, psutil.ZombieProcess):
                            owner_create_time = None
                        except psutil.NoSuchProcess:
                            owner_stale = True
                        if (
                            not owner_stale and
                            owner_create_time is not None and
                            owner_create_time > (owner_start + tolerance)
                        ):
                            owner_stale = True
                if owner_stale:
                    shutil.rmtree(self.path, ignore_errors=True)
                    continue

                # error on timeout
                if (now - start) > self.timeout:
                    detail = f"\nlock owner: {json.dumps(owner, indent=2)}" if owner else ""
                    raise TimeoutError(
                        f"could not acquire environment lock within {self.timeout} seconds{detail}"
                    ) from err

                # wait and retry
                time.sleep(0.1)

            self.lock.write_text(json.dumps({
                "pid": self.pid,
                "pid_start": self.create_time,
            }, indent=2) + "\n", encoding="utf-8")
            break

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc_value: BaseException | None,
        traceback: TracebackType | None
    ) -> None:
        # allow nested context managers without deadlocking
        self.depth -= 1
        if self.depth > 0:
            return

        # release lock
        shutil.rmtree(self.path, ignore_errors=True)

    def __bool__(self) -> bool:
        return self.depth > 0

    def __repr__(self) -> str:
        return f"Lock(path={self.path!r}, timeout={self.timeout})"


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
