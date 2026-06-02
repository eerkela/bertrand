"""Bertrand's host-local, persistent state directory."""

from __future__ import annotations

import contextlib
import grp
import os
import tempfile
import uuid
from pathlib import Path

from bertrand.env.git import (
    BERTRAND_GROUP,
    CACHE_DIR,
    HOST_ID_FILE,
    REPO_DIR,
    RUN_DIR,
    STATE_DIR,
    Deadline,
    can_escalate,
    confirm,
    run,
    sudo,
)
from bertrand.env.host.systemd import (
    RUN_TMPFS_MOUNT_UNIT_NAME,
    RUN_TMPFS_MOUNT_UNIT_PATH,
    configure_run_tmpfs_mount,
    run_dir_tmpfs_mounted,
    run_mount_unit_configured,
)
from bertrand.env.host.systemd import (
    disable_run_tmpfs_mount as _disable_run_tmpfs_mount,
)
from bertrand.env.host.user import UserGroup, ensure_bertrand_group

STATE_DIR_MODE = 0o2770
STATE_FILE_MODE = 0o640
STATE_EXECUTABLE_MODE = 0o750


def _dir_configured(path: Path, *, group_gid: int, mode: int) -> bool:
    try:
        if path.is_symlink() or not path.is_dir():
            return False
        stat_info = path.stat()
    except OSError:
        return False
    return (
        stat_info.st_uid == 0
        and stat_info.st_gid == group_gid
        and (stat_info.st_mode & 0o7777) == mode
    )


def _file_configured(path: Path, *, group_gid: int, mode: int) -> bool:
    try:
        if path.is_symlink() or not path.is_file():
            return False
        stat_info = path.stat()
    except OSError:
        return False
    return (
        stat_info.st_uid == 0
        and stat_info.st_gid == group_gid
        and (stat_info.st_mode & 0o7777) == mode
    )


def _host_id_configured(*, group_gid: int | None = None) -> bool:
    try:
        uuid.UUID(HOST_ID_FILE.read_text(encoding="utf-8").strip())
    except (OSError, ValueError):
        return False
    if group_gid is None:
        return True
    return _file_configured(HOST_ID_FILE, group_gid=group_gid, mode=STATE_FILE_MODE)


async def install_state_dir(
    path: Path,
    *,
    mode: int = STATE_DIR_MODE,
    assume_yes: bool,
    deadline: Deadline,
) -> None:
    """Converge one root-owned Bertrand state directory."""
    await run(
        sudo(
            [
                "install",
                "-d",
                "-m",
                f"{mode:o}",
                "-o",
                "root",
                "-g",
                BERTRAND_GROUP,
                str(path),
            ],
            non_interactive=assume_yes,
        ),
        deadline=deadline,
    )


async def normalize_state_file(
    path: Path,
    *,
    mode: int = STATE_FILE_MODE,
    assume_yes: bool = True,
    deadline: Deadline,
) -> None:
    """Set the intended owner, group, and mode for one Bertrand state file."""
    if not path.exists():
        return
    await run(
        sudo(
            ["chown", f"root:{BERTRAND_GROUP}", str(path)], non_interactive=assume_yes
        ),
        deadline=deadline,
    )
    await run(
        sudo(["chmod", f"{mode:o}", str(path)], non_interactive=assume_yes),
        deadline=deadline,
    )


async def write_state_file(
    path: Path,
    payload: str,
    *,
    mode: int = STATE_FILE_MODE,
    assume_yes: bool,
    deadline: Deadline,
) -> None:
    """Write one root-owned Bertrand state file with the intended permissions."""
    fd: int | None = None
    temp_file: Path | None = None
    try:
        fd, name = tempfile.mkstemp(prefix="bertrand-state.", suffix=".tmp")
        temp_file = Path(name)
        os.write(fd, payload.encode("utf-8"))
        os.fsync(fd)
        os.close(fd)
        fd = None
        await run(
            sudo(
                [
                    "install",
                    "-D",
                    "-m",
                    f"{mode:o}",
                    "-o",
                    "root",
                    "-g",
                    BERTRAND_GROUP,
                    str(temp_file),
                    str(path),
                ],
                non_interactive=assume_yes,
            ),
            deadline=deadline,
        )
    finally:
        if fd is not None:
            with contextlib.suppress(OSError):
                os.close(fd)
        if temp_file is not None:
            temp_file.unlink(missing_ok=True)


async def normalize_state_executable(
    path: Path,
    *,
    assume_yes: bool = True,
    deadline: Deadline,
) -> None:
    """Set the intended owner, group, and mode for one Bertrand executable."""
    await normalize_state_file(
        path,
        mode=STATE_EXECUTABLE_MODE,
        assume_yes=assume_yes,
        deadline=deadline,
    )


async def disable_run_tmpfs_mount(*, deadline: Deadline) -> None:
    """Disable Bertrand's managed systemd runtime tmpfs unit."""
    await _disable_run_tmpfs_mount(deadline=deadline)


def host_state_backend_trustworthy() -> bool:
    """Return True when Bertrand's shared state backend is reusable.

    Returns
    -------
    bool
        True if the state directories, durable host identity, systemd runtime mount
        unit, and live tmpfs runtime mount are configured, otherwise False.
    """
    try:
        group_info = grp.getgrnam(BERTRAND_GROUP)
    except KeyError:
        return False
    return (
        _dir_configured(STATE_DIR, group_gid=group_info.gr_gid, mode=STATE_DIR_MODE)
        and _dir_configured(REPO_DIR, group_gid=group_info.gr_gid, mode=STATE_DIR_MODE)
        and _dir_configured(CACHE_DIR, group_gid=group_info.gr_gid, mode=STATE_DIR_MODE)
        and _host_id_configured(group_gid=group_info.gr_gid)
        and run_mount_unit_configured(group_gid=group_info.gr_gid)
        and run_dir_tmpfs_mounted()
    )


async def ensure_host_state(
    *,
    user: str,
    assume_yes: bool,
    deadline: Deadline,
) -> UserGroup:
    """Ensure Bertrand's shared host state and runtime directory are configured.

    Parameters
    ----------
    user : str
        Host username to configure for Bertrand group access.
    assume_yes : bool
        Whether to automatically confirm prompts during installation.
    deadline : Deadline
        Host-state convergence budget.

    Returns
    -------
    UserGroup
        The user's Bertrand group membership status after host-state convergence.

    Raises
    ------
    PermissionError
        If configuration requires elevation, but elevation is unavailable or declined.
    OSError
        If the host is unsupported or shared state configuration fails.
    """
    if os.name != "posix":
        msg = "Bertrand state bootstrap requires a POSIX host."
        raise OSError(msg)

    group_info = await ensure_bertrand_group(
        deadline=deadline,
        assume_yes=assume_yes,
    )
    directories_ready = all(
        _dir_configured(path, group_gid=group_info.gr_gid, mode=STATE_DIR_MODE)
        for path in (STATE_DIR, REPO_DIR, CACHE_DIR)
    )
    runtime_ready = (
        run_mount_unit_configured(group_gid=group_info.gr_gid)
        and run_dir_tmpfs_mounted()
    )
    if not directories_ready or not runtime_ready:
        if not confirm(
            "Bertrand requires shared host state under "
            f"{STATE_DIR} with root-owned {BERTRAND_GROUP!r} group access, plus a "
            f"tmpfs runtime mount at {RUN_DIR}.  Configure it now (requires sudo)?\n"
            "[y/N] ",
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
        for path in (STATE_DIR, REPO_DIR, CACHE_DIR):
            await install_state_dir(path, assume_yes=assume_yes, deadline=deadline)
        await configure_run_tmpfs_mount(
            group_gid=group_info.gr_gid,
            assume_yes=assume_yes,
            deadline=deadline,
        )

    if not _host_id_configured():
        await write_state_file(
            HOST_ID_FILE,
            f"{uuid.uuid4().hex}\n",
            assume_yes=assume_yes,
            deadline=deadline,
        )
    await normalize_state_file(
        HOST_ID_FILE,
        assume_yes=assume_yes,
        deadline=deadline,
    )

    for path in (STATE_DIR, REPO_DIR, CACHE_DIR):
        if not _dir_configured(path, group_gid=group_info.gr_gid, mode=STATE_DIR_MODE):
            msg = f"Failed to configure shared Bertrand state directory: {path}"
            raise OSError(msg)
    if not _host_id_configured(group_gid=group_info.gr_gid):
        msg = f"Failed to configure durable Bertrand host identity: {HOST_ID_FILE}"
        raise OSError(msg)
    if not run_mount_unit_configured(group_gid=group_info.gr_gid):
        msg = (
            "Failed to install Bertrand systemd tmpfs mount unit for runtime state: "
            f"{RUN_TMPFS_MOUNT_UNIT_PATH}"
        )
        raise OSError(msg)
    if not run_dir_tmpfs_mounted():
        msg = (
            f"Failed to activate Bertrand tmpfs runtime mount at {RUN_DIR}.  Check "
            f"`systemctl status {RUN_TMPFS_MOUNT_UNIT_NAME}` for diagnostics."
        )
        raise OSError(msg)

    group = UserGroup(user=user, group=BERTRAND_GROUP)
    await group.activate(assume_yes=assume_yes)
    return group
