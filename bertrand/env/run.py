"""Utility functions for running subprocesses and handling command-line interactions."""
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
from typing import Mapping, TextIO

#pylint: disable=redefined-builtin


HIDDEN: str = rf"^(?P<prefix>.*{re.escape(os.path.sep)})?(?P<suffix>\..*)"


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
    capture_output: bool = False,
    tee: bool = True,
    input: str | None = None,
    cwd: Path | None = None,
    env: Mapping[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    """A wrapper around `subprocess.run` that defaults to text mode and properly
    formats errors.

    Parameters
    ----------
    argv : list[str]
        The command and its arguments to run.
    check : bool, optional
        Whether to raise a `CommandError` if the command fails (default is True).  If
        false, then any errors will be ignored.
    capture_output : bool, optional
        If true, or if `tee` is true (the default), then include both stdout and
        stderr in the returned `CompletedProcess` or `CommandError`.  If false, then
        the command's stdout and stderr will be inherited from the parent process,
        and if `tee` is also false, they will not be captured in the resulting objects.
    tee : bool, optional
        If true (the default), and `capture_output` is false (the default), then stdout
        and stderr will be printed to the console while also being captured in the
        returned `CompletedProcess` or `CommandError`.  If false, then stdout and
        stderr will either be captured (if `capture_output` is true) or inherited
        from the parent process and not recorded in the resulting objects (if
        `capture_output` is false).
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
        if capture_output or not tee:
            return subprocess.run(
                argv,
                check=check,
                capture_output=capture_output,
                text=True,
                input=input,
                cwd=cwd,
                env=env,
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

            result = subprocess.CompletedProcess(
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


def host_user_ids() -> tuple[int, int] | None:
    """Return a (uid, gid) tuple for the current host user, if one can be determined.

    Returns
    -------
    tuple[int, int] | None
        A tuple containing the user ID and group ID, or None if not determinable.
    """
    if os.name != "posix":
        return None
    return (os.getuid(), os.getgid())


def host_username() -> str:
    """Return the username of the current host user.

    Returns
    -------
    str
        The host username for the active process.
    """
    return pwd.getpwuid(os.getuid()).pw_name


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
