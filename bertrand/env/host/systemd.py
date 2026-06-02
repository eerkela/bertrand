"""Systemd helpers for Bertrand's host-local runtime mounts."""

from __future__ import annotations

import contextlib
import os
import shutil
import tempfile
from pathlib import Path

from bertrand.env.git import (
    BERTRAND_GROUP,
    HOST_MOUNTS,
    RUN_DIR,
    Deadline,
    run,
    sudo,
)

RUN_DIR_MODE = 0o2770
RUN_TMPFS_MOUNT_UNIT_NAME = "bertrand-run.mount"
RUN_TMPFS_MOUNT_UNIT_PATH = Path("/etc/systemd/system") / RUN_TMPFS_MOUNT_UNIT_NAME


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
            f"Options=mode={RUN_DIR_MODE:o},uid=0,gid={group_gid},nosuid,nodev,noexec",
            "",
            "[Install]",
            "WantedBy=multi-user.target",
            "",
        )
    )


def run_mount_unit_configured(*, group_gid: int) -> bool:
    """Return whether Bertrand's runtime tmpfs unit has the expected payload.

    Returns
    -------
    bool
        True when the systemd unit exists and matches the expected payload.
    """
    try:
        current = RUN_TMPFS_MOUNT_UNIT_PATH.read_text(encoding="utf-8")
    except OSError:
        return False
    return current == _run_mount_unit_text(group_gid=group_gid)


def run_dir_tmpfs_mounted() -> bool:
    """Return whether Bertrand's runtime directory is currently a tmpfs mount.

    Returns
    -------
    bool
        True when the runtime directory is mounted as tmpfs.
    """
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


async def configure_run_tmpfs_mount(
    *,
    group_gid: int,
    assume_yes: bool,
    deadline: Deadline,
) -> None:
    """Install and activate Bertrand's managed runtime tmpfs unit.

    Raises
    ------
    OSError
        If systemd is unavailable.
    """
    if not shutil.which("systemctl"):
        msg = (
            "Bertrand requires systemd (`systemctl`) to manage the runtime tmpfs "
            f"mount at {RUN_DIR}."
        )
        raise OSError(msg)

    await run(
        sudo(
            [
                "install",
                "-d",
                "-m",
                f"{RUN_DIR_MODE:o}",
                "-o",
                "root",
                "-g",
                BERTRAND_GROUP,
                str(RUN_DIR),
            ],
            non_interactive=assume_yes,
        ),
        deadline=deadline,
    )

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
            deadline=deadline,
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
        await run(sudo(cmd, non_interactive=assume_yes), deadline=deadline)


async def disable_run_tmpfs_mount(*, deadline: Deadline) -> None:
    """Disable Bertrand's managed systemd runtime tmpfs unit."""
    if not shutil.which("systemctl"):
        return
    await run(
        sudo(["systemctl", "disable", "--now", RUN_TMPFS_MOUNT_UNIT_NAME]),
        check=False,
        capture_output=True,
        deadline=deadline,
    )
    await run(
        sudo(["rm", "-f", str(RUN_TMPFS_MOUNT_UNIT_PATH)]),
        check=False,
        capture_output=True,
        deadline=deadline,
    )
    await run(
        sudo(["systemctl", "daemon-reload"]),
        check=False,
        capture_output=True,
        deadline=deadline,
    )
