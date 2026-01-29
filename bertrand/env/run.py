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
from datetime import datetime
from pathlib import Path
from types import TracebackType
from typing import Mapping, TextIO

import psutil

#pylint: disable=redefined-builtin


HIDDEN: str = rf"^(?P<prefix>.*{re.escape(os.path.sep)})?(?P<suffix>\..*)"


class CompletedProcess(subprocess.CompletedProcess[str]):
    """A custom CompletedProcess that captures the output of stdout/stderr and prints
    it when converted to a string.
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
    """A custom exception for command-line and docker errors, which captures the
    output of stdout/stderr and prints it when converted to a string.
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


def _pump_output(src: TextIO, sink: TextIO, buf_list: list[str]) -> None:
    for line in src:
        buf_list.append(line)
        sink.write(line)
        sink.flush()
    src.close()


def run(
    argv: list[str],
    *,
    check: bool = True,
    capture_output: bool | None = False,
    input: str | None = None,
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
    """
    try:
        if capture_output is not None:
            cp = subprocess.run(
                argv,
                check=check,
                capture_output=capture_output,
                text=True,
                input=input,
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
        with subprocess.Popen(
            argv,
            stdin=subprocess.PIPE if input is not None else None,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            errors="replace",
            bufsize=1,  # line-buffered in text mode
            cwd=cwd,
            env=env,
        ) as p:
            stdout_lines: list[str] = []
            stderr_lines: list[str] = []
            if input is not None and p.stdin is not None:
                try:
                    p.stdin.write(input)
                finally:
                    p.stdin.close()

            # read both streams without deadlock
            t_out = threading.Thread(
                target=_pump_output,
                args=(p.stdout, sys.stdout, stdout_lines),
                daemon=True
            )
            t_err = threading.Thread(
                target=_pump_output,
                args=(p.stderr, sys.stderr, stderr_lines),
                daemon=True
            )
            t_out.start()
            t_err.start()
            rc = p.wait()
            t_out.join()
            t_err.join()

            result = CompletedProcess(
                argv,
                rc,
                "".join(stdout_lines),
                "".join(stderr_lines),
            )
    except subprocess.CalledProcessError as err:
        raise CommandError(err.returncode, argv, err.stdout or "", err.stderr or "") from err

    if check and rc != 0:
        raise CommandError(rc, argv, result.stdout, result.stderr)
    return result


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


class UserInfo:
    """A simple structure representing a user identity by user ID and group ID."""

    def __init__(self) -> None:
        euid = os.geteuid()
        sudo_uid = os.environ.get("SUDO_UID")
        sudo_user = os.environ.get("SUDO_USER")
        if euid == 0 and sudo_uid:
            self._uid = int(sudo_uid)
            pw = pwd.getpwuid(self._uid)
            self._gid = pw.pw_gid
            self._name = sudo_user or pw.pw_name
            self._home = Path(pw.pw_dir)
            self._shell = Path(pw.pw_shell)
        else:
            self._uid = os.getuid()
            pw = pwd.getpwuid(self._uid)
            self._gid = pw.pw_gid
            self._name = pw.pw_name
            self._home = Path(pw.pw_dir)
            self._shell = Path(pw.pw_shell)

    @property
    def uid(self) -> int:
        """
        Returns
        -------
        int
            The numeric user ID.
        """
        return self._uid

    @property
    def gid(self) -> int:
        """
        Returns
        -------
        int
            The numeric group ID.
        """
        return self._gid

    @property
    def name(self) -> str:
        """
        Returns
        -------
        str
            The host username.
        """
        return self._name

    @property
    def home(self) -> Path:
        """
        Returns
        -------
        Path
            The path to the user's home directory.
        """
        return self._home

    @property
    def shell(self) -> Path:
        """
        Returns
        -------
        Path
            The path to the user's login shell.
        """
        return self._shell


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
                        continue
                except Exception:  # pylint: disable=broad-except
                    shutil.rmtree(self.path, ignore_errors=True)
                    continue

                # check whether owning process is still alive
                owner_pid = owner.get("pid")
                owner_start = owner.get("pid_start")
                tolerance = 0.001  # tolerate floating point precision issues
                if isinstance(owner_pid, int) and isinstance(owner_start, (int, float)) and (
                    not psutil.pid_exists(owner_pid) or
                    psutil.Process(owner_pid).create_time() > (owner_start + tolerance)
                ):
                    shutil.rmtree(self.path, ignore_errors=True)
                    continue

                # error on timeout
                if (now - start) > self.timeout:
                    detail = f"\nlock owner: {json.dumps(owner, indent=2)})" if owner else ""
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


def atomic_write_text(path: Path, text: str) -> None:
    """Atomically write text to a file, avoiding race conditions and partial writes.

    Parameters
    ----------
    path : Path
        The path to write to.
    text : str
        The text to write.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(f"{path.name}.tmp.{os.getpid()}.{int(time.time())}")
    tmp.write_text(text, encoding="utf-8")
    try:
        with tmp.open("r+", encoding="utf-8") as f:
            f.flush()
            os.fsync(f.fileno())
    except OSError:
        pass
    tmp.replace(path)


def atomic_write_bytes(path: Path, data: bytes) -> None:
    """Atomically write bytes to a file, avoiding race conditions and partial writes.

    Parameters
    ----------
    path : Path
        The path to write to.
    data : bytes
        The bytes to write.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(f"{path.name}.tmp.{os.getpid()}.{int(time.time())}")
    tmp.write_bytes(data)
    try:
        with tmp.open("r+b") as f:
            f.flush()
            os.fsync(f.fileno())
    except OSError:
        pass
    tmp.replace(path)


def up_to_date(start: Path, timestamp: datetime, exclude: str = HIDDEN) -> bool:
    """Check whether all files under a given directory are older than the specified
    timestamp, excluding files that match a given regex pattern.

    Parameters
    ----------
    start : Path
        The path to start checking from.  If this is a directory, then all files
        under it will be checked recursively.
    timestamp : datetime
        The timestamp to compare against.
    exclude : str, optional
        A regex pattern for files or directories to exclude from the check.  The
        default pattern excludes any path component (relative to `start`) that begins
        with a dot (usually indicating hidden files or directories).

    Returns
    -------
    bool
        True if all relevant files are older than the timestamp, false otherwise.

    Raises
    ------
    re.error
        If the provided regex pattern is invalid.
    """
    mtime = timestamp.timestamp()
    regex = re.compile(exclude)
    remaining = [start.expanduser().resolve()]
    while remaining:
        path = remaining.pop()
        if path.exists() and not regex.match(str(path.relative_to(start))):
            if path.is_dir():  # recursively explore directories
                remaining.extend(path.iterdir())
            elif path.stat().st_mtime > mtime:  # found a newer file
                return False
    return True
