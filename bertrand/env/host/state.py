"""Shared host-state bootstrap for Bertrand's runtime."""

from __future__ import annotations

import asyncio
import contextlib
import grp
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from bertrand.env.git import bertrand_git as _git
from bertrand.env.git.bertrand_git import (
    CommandError,
    GroupStatus,
    can_escalate,
    confirm,
    run,
    sudo,
)

BERTRAND_GROUP = _git.BERTRAND_GROUP
BIN_DIR = _git.BIN_DIR
CACHE_DIR = _git.CACHE_DIR
HOST_MOUNTS = _git.HOST_MOUNTS
REPO_ALIASES_EXT = _git.REPO_ALIASES_EXT
REPO_DIR = _git.REPO_DIR
REPO_LOCK_EXT = _git.REPO_LOCK_EXT
REPO_MOUNT_EXT = _git.REPO_MOUNT_EXT
RUN_DIR = _git.RUN_DIR
STATE_DIR = _git.STATE_DIR
TOOLS_DIR = _git.TOOLS_DIR
STATE_DIR_MODE = 0o2770
RUN_TMPFS_MOUNT_UNIT_NAME = "bertrand-run.mount"
RUN_TMPFS_MOUNT_UNIT_PATH = Path("/etc/systemd/system") / RUN_TMPFS_MOUNT_UNIT_NAME


def _state_root_configured(group_gid: int) -> bool:
    try:
        if STATE_DIR.is_symlink():
            return False
        if not STATE_DIR.is_dir():
            return False
        stat_info = STATE_DIR.stat()
    except OSError:
        return False
    return (
        stat_info.st_uid == 0
        and stat_info.st_gid == group_gid
        and (stat_info.st_mode & 0o7777) == STATE_DIR_MODE
    )


def _state_acl_configured_sync(group: str) -> bool:
    if not shutil.which("getfacl"):
        return False
    result = subprocess.run(
        ["getfacl", "-cp", str(STATE_DIR)],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    if result.returncode != 0:
        return False
    lines = {line.strip() for line in result.stdout.splitlines() if line.strip()}
    access = f"group:{group}:rwx"
    default = f"default:group:{group}:rwx"
    return access in lines and default in lines


async def _state_acl_configured(group: str) -> bool:
    if not shutil.which("getfacl"):
        return False
    result = await run(
        ["getfacl", "-cp", str(STATE_DIR)],
        check=False,
        capture_output=True,
    )
    if result.returncode != 0:
        return False
    lines = {line.strip() for line in result.stdout.splitlines() if line.strip()}
    access = f"group:{group}:rwx"
    default = f"default:group:{group}:rwx"
    return access in lines and default in lines


async def _configure_state_acl(*, deadline: float, assume_yes: bool) -> None:
    loop = asyncio.get_running_loop()
    if not shutil.which("setfacl") or not shutil.which("getfacl"):
        msg = (
            "Strict Bertrand state ACL setup requires `setfacl` and `getfacl`, "
            "but they were not found.  Install the host `acl` package and rerun "
            "`bertrand init`."
        )
        raise OSError(msg)
    for cmd in (
        ["setfacl", "-m", f"group:{BERTRAND_GROUP}:rwx", str(STATE_DIR)],
        ["setfacl", "-m", "mask::rwx", str(STATE_DIR)],
        ["setfacl", "-d", "-m", f"group:{BERTRAND_GROUP}:rwx", str(STATE_DIR)],
        ["setfacl", "-d", "-m", "mask::rwx", str(STATE_DIR)],
    ):
        await run(sudo(cmd, non_interactive=assume_yes), timeout=deadline - loop.time())

    await run(
        sudo(
            [
                "install",
                "-d",
                "-m",
                f"{STATE_DIR_MODE:o}",
                "-o",
                "root",
                "-g",
                BERTRAND_GROUP,
                str(RUN_DIR),
            ],
            non_interactive=assume_yes,
        ),
        timeout=deadline - loop.time(),
    )


def _run_mount_unit_text(*, group_gid: int) -> str:
    return "\n".join(
        (
            "[Unit]",
            "Description=Bertrand tmpfs runtime state",
            "After=local-fs.target",
            "",
            "[Mount]",
            "What=tmpfs",
            f"Where={RUN_DIR}",
            "Type=tmpfs",
            f"Options=mode={STATE_DIR_MODE:o},uid=0,gid={group_gid},nosuid,nodev,noexec",
            "",
            "[Install]",
            "WantedBy=multi-user.target",
            "",
        )
    )


def _run_mount_unit_configured(*, group_gid: int) -> bool:
    try:
        current = RUN_TMPFS_MOUNT_UNIT_PATH.read_text(encoding="utf-8")
        return current == _run_mount_unit_text(group_gid=group_gid)
    except OSError:
        return False


def _run_dir_tmpfs_mounted() -> bool:
    try:
        lines = HOST_MOUNTS.read_text(encoding="utf-8").splitlines()
    except OSError:
        return False
    needle = os.path.normpath(str(RUN_DIR))
    for line in lines:
        parts = line.strip().split()
        if len(parts) < 10:
            continue
        try:
            sep = parts.index("-")
        except ValueError:
            continue
        if sep + 2 >= len(parts):
            continue
        mount_point = (
            parts[4]
            .replace("\\040", " ")
            .replace("\\011", "\t")
            .replace("\\012", "\n")
            .replace("\\134", "\\")
        )
        if os.path.normpath(mount_point) == needle:
            return parts[sep + 1] == "tmpfs"
    return False


async def _configure_run_tmpfs_mount(
    *,
    group_gid: int,
    assume_yes: bool,
    timeout: float,
) -> None:
    if not shutil.which("systemctl"):
        msg = (
            "Bertrand requires systemd (`systemctl`) to manage the runtime tmpfs "
            f"mount at {RUN_DIR}."
        )
        raise OSError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout

    fd: int | None = None
    temp_unit: Path | None = None
    try:
        fd, name = tempfile.mkstemp(prefix="bertrand-run-mount.", suffix=".mount")
        temp_unit = Path(name)
        os.write(fd, _run_mount_unit_text(group_gid=group_gid).encode("utf-8"))
        os.fsync(fd)
        os.close(fd)
        fd = None

        await run(
            sudo(
                [
                    "install",
                    "-m",
                    "0644",
                    "-o",
                    "root",
                    "-g",
                    "root",
                    str(temp_unit),
                    str(RUN_TMPFS_MOUNT_UNIT_PATH),
                ],
                non_interactive=assume_yes,
            ),
            timeout=deadline - loop.time(),
        )
    finally:
        if fd is not None:
            with contextlib.suppress(OSError):
                os.close(fd)
        if temp_unit is not None:
            temp_unit.unlink(missing_ok=True)

    for cmd in (
        ["systemctl", "daemon-reload"],
        ["systemctl", "enable", "--now", RUN_TMPFS_MOUNT_UNIT_NAME],
    ):
        await run(
            sudo(cmd, non_interactive=assume_yes),
            timeout=deadline - loop.time(),
        )


def host_state_backend_trustworthy() -> bool:
    """Return True when Bertrand's shared state backend is reusable.

    Returns
    -------
    bool
        True if the state root, default ACLs, systemd runtime mount unit, and live
        tmpfs runtime mount are all configured, otherwise False.
    """
    if (
        not shutil.which("setfacl")
        or not shutil.which("getfacl")
        or not STATE_DIR.is_dir()
        or STATE_DIR.is_symlink()
    ):
        return False
    try:
        group_info = grp.getgrnam(BERTRAND_GROUP)
    except KeyError:
        return False
    return (
        _state_root_configured(group_gid=group_info.gr_gid)
        and _state_acl_configured_sync(BERTRAND_GROUP)
        and _run_mount_unit_configured(group_gid=group_info.gr_gid)
        and _run_dir_tmpfs_mounted()
    )


async def ensure_host_group(
    *,
    timeout: float,
    assume_yes: bool,
) -> grp.struct_group:
    """Ensure Bertrand's shared host group exists.

    Parameters
    ----------
    timeout : float
        Maximum time in seconds to wait for required host commands.
    assume_yes : bool
        Whether to automatically confirm prompts for creating the shared group.

    Returns
    -------
    grp.struct_group
        The shared Bertrand group information.

    Raises
    ------
    PermissionError
        If group creation requires root privileges, but elevation is unavailable or
        declined.
    OSError
        If the shared group cannot be created or verified.
    CommandError
        If the host group creation command fails unexpectedly.
    """
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout

    try:
        return grp.getgrnam(BERTRAND_GROUP)
    except KeyError:
        pass

    if not confirm(
        f"Bertrand uses a shared host group named {BERTRAND_GROUP!r} for unprivileged "
        "access to global runtime state.  Create this system group now "
        "(requires sudo)?\n"
        "[y/N] ",
        assume_yes=assume_yes,
    ):
        msg = "Bertrand shared-group bootstrap declined by user."
        raise PermissionError(msg)
    if os.geteuid() != 0 and not can_escalate():
        msg = (
            f"Creating group {BERTRAND_GROUP!r} requires root privileges; sudo not "
            "available."
        )
        raise PermissionError(msg)
    try:
        await run(
            sudo(
                ["groupadd", "--system", BERTRAND_GROUP],
                non_interactive=assume_yes,
            ),
            capture_output=True,
            timeout=deadline - loop.time(),
        )
    except CommandError as err:
        out = f"{err.stdout}\n{err.stderr}".lower().strip()
        if "already exists" not in out and "alreadyexist" not in out:
            raise

    try:
        return grp.getgrnam(BERTRAND_GROUP)
    except KeyError as err:
        msg = f"Failed to create shared Bertrand group {BERTRAND_GROUP!r}."
        raise OSError(msg) from err


async def ensure_host_state(
    *,
    user: str,
    assume_yes: bool,
    timeout: float,
) -> GroupStatus:
    """Ensure Bertrand's shared host state and runtime directory are configured.

    Parameters
    ----------
    user : str
        Host username to configure for Bertrand group access.
    assume_yes : bool
        Whether to automatically confirm prompts during installation.
    timeout : float
        Maximum time in seconds to wait for required host commands.

    Returns
    -------
    GroupStatus
        The user's Bertrand group membership status after host-state convergence.

    Raises
    ------
    PermissionError
        If configuration requires elevation, but elevation is unavailable or declined.
    OSError
        If the host is unsupported, shared state configuration fails, ACL inheritance
        is missing, or the tmpfs runtime mount cannot be converged and verified.
    """
    if os.name != "posix":
        msg = "Bertrand state bootstrap requires a POSIX host."
        raise OSError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout

    group_info = await ensure_host_group(
        timeout=deadline - loop.time(),
        assume_yes=assume_yes,
    )
    if (
        not _state_root_configured(group_gid=group_info.gr_gid)
        or not await _state_acl_configured(BERTRAND_GROUP)
        or not _run_mount_unit_configured(group_gid=group_info.gr_gid)
        or not _run_dir_tmpfs_mounted()
    ):
        if not confirm(
            "Bertrand requires shared host state under "
            f"{STATE_DIR} with root-owned {BERTRAND_GROUP!r} access and strict ACL "
            f"inheritance, with a tmpfs runtime mount at {RUN_DIR}.  Configure it "
            "now (requires sudo)?\n[y/N] ",
            assume_yes=assume_yes,
        ):
            msg = "Bertrand shared-state bootstrap declined by user."
            raise PermissionError(msg)
        if os.geteuid() != 0 and not can_escalate():
            msg = (
                "Configuring Bertrand shared-state directories requires root "
                "privileges; sudo not available."
            )
            raise PermissionError(msg)
        await run(
            sudo(
                [
                    "install",
                    "-d",
                    "-m",
                    f"{STATE_DIR_MODE:o}",
                    "-o",
                    "root",
                    "-g",
                    BERTRAND_GROUP,
                    str(STATE_DIR),
                ],
                non_interactive=assume_yes,
            ),
            timeout=deadline - loop.time(),
        )
        await _configure_state_acl(
            deadline=deadline,
            assume_yes=assume_yes,
        )
        await _configure_run_tmpfs_mount(
            group_gid=group_info.gr_gid,
            assume_yes=assume_yes,
            timeout=deadline - loop.time(),
        )

    if not _state_root_configured(group_gid=group_info.gr_gid):
        msg = f"Failed to configure shared Bertrand state directory: {STATE_DIR}"
        raise OSError(msg)
    if not await _state_acl_configured(BERTRAND_GROUP):
        msg = (
            f"Failed to configure strict ACL inheritance for shared Bertrand state: "
            f"{STATE_DIR}"
        )
        raise OSError(msg)
    if not _run_mount_unit_configured(group_gid=group_info.gr_gid):
        msg = (
            "Failed to install Bertrand systemd tmpfs mount unit for runtime state: "
            f"{RUN_TMPFS_MOUNT_UNIT_PATH}"
        )
        raise OSError(msg)
    if not _run_dir_tmpfs_mounted():
        msg = (
            f"Failed to activate Bertrand tmpfs runtime mount at {RUN_DIR}.  Check "
            f"`systemctl status {RUN_TMPFS_MOUNT_UNIT_NAME}` for diagnostics."
        )
        raise OSError(msg)

    group = GroupStatus.get(user, BERTRAND_GROUP)
    await group.activate(assume_yes=assume_yes)
    return group


async def disable_run_tmpfs_mount(*, timeout: float) -> None:
    """Disable Bertrand's managed systemd runtime tmpfs unit.

    Parameters
    ----------
    timeout : float
        Maximum time in seconds to wait for systemd commands.
    """
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    if shutil.which("systemctl"):
        await run(
            ["systemctl", "disable", "--now", RUN_TMPFS_MOUNT_UNIT_NAME],
            check=False,
            capture_output=True,
            timeout=deadline - loop.time(),
        )
        RUN_TMPFS_MOUNT_UNIT_PATH.unlink(missing_ok=True)
        await run(
            ["systemctl", "daemon-reload"],
            check=False,
            capture_output=True,
            timeout=deadline - loop.time(),
        )
