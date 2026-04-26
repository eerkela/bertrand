"""Teardown Bertrand-managed local runtime state.

This cleanup intentionally removes Bertrand-owned runtime artifacts only.  It does
not uninstall MicroK8s, MicroCeph, system packages, or host user groups.
"""
from __future__ import annotations

import asyncio
import os
import shutil
import sys
from collections.abc import Awaitable, Callable
from dataclasses import dataclass, field
from pathlib import Path

from ..ceph import MountInfo, RepoMount
from ..run import (
    BERTRAND_ENV,
    NERDCTL_BIN,
    REPO_DIR,
    REPO_MOUNT_EXT,
    RUN_DIR,
    RUN_TMPFS_MOUNT_UNIT_NAME,
    RUN_TMPFS_MOUNT_UNIT_PATH,
    STATE_DIR,
    confirm,
    nerdctl,
    nerdctl_ids,
    run,
    stop_buildkit,
)


@dataclass
class CleanState:
    """In-memory convergence state for `bertrand clean` stages.

    Attributes
    ----------
    assume_yes : bool
        Whether interactive confirmations should be auto-accepted.
    force : bool
        Whether stage errors should be downgraded to warnings for non-strict stages.
    deadline : float
        Absolute event-loop timestamp when cleanup must finish.
    captured_aliases : set[Path]
        Managed aliases discovered during cleanup for residual verification.
    """
    assume_yes: bool
    force: bool
    deadline: float
    captured_aliases: set[Path] = field(default_factory=set)


async def _stop_buildkitd(state: CleanState) -> None:
    loop = asyncio.get_running_loop()
    await stop_buildkit(timeout=state.deadline - loop.time())


async def _clean_nerdctl_objects(state: CleanState) -> None:
    if not NERDCTL_BIN.exists():
        return

    chunk_size = 32  # chunks of 32 to avoid arg limits
    cleanup_table: tuple[tuple[str, list[str]], ...] = (
        ("container", ["container", "rm", "-f", "-i"]),
        ("image", ["image", "rm", "-f", "-i"]),
        ("volume", ["volume", "rm", "-f"]),
        ("network", ["network", "rm", "-f"]),
    )
    loop = asyncio.get_running_loop()
    for kind, remove_prefix in cleanup_table:
        ids = await nerdctl_ids(
            kind,
            {BERTRAND_ENV: "1"},
            timeout=state.deadline - loop.time(),
        )
        for i in range(0, len(ids), chunk_size):
            await nerdctl(
                [*remove_prefix, *ids[i:i + chunk_size]],
                check=False,
                timeout=state.deadline - loop.time(),
            )


async def _clean_repo_mounts_aliases(state: CleanState) -> None:
    if not REPO_DIR.exists():
        return
    if not REPO_DIR.is_dir():
        raise OSError(f"repository root is not a directory: {REPO_DIR}")
    loop = asyncio.get_running_loop()

    repo_roots = sorted(
        (
            entry for entry in REPO_DIR.iterdir()
            if entry.is_dir() and not entry.is_symlink()
        ),
        key=lambda item: item.as_posix(),
    )

    for repo_root in repo_roots:
        mount_path = repo_root / REPO_MOUNT_EXT
        try:
            managed_aliases = RepoMount.managed_aliases(repo_root)
        except (OSError, TypeError, ValueError) as err:
            print(
                "bertrand: warning: failed to parse repository alias index at "
                f"{repo_root}: {err}",
                file=sys.stderr,
            )
            managed_aliases = set()
        state.captured_aliases.update(managed_aliases)
        for alias in sorted(
            managed_aliases,
            key=lambda item: item.as_posix(),
        ):
            alias.unlink(missing_ok=True)

        mount = MountInfo.search(mount_path)
        if mount is not None:
            await mount.unmount(
                timeout=state.deadline - loop.time(),
                force=True,
            )
        shutil.rmtree(repo_root)

    # safety sweep in case metadata was missing or corrupt
    mounts = sorted(
        MountInfo.under(REPO_DIR),
        key=lambda item: len(item.mount_point.parts),
        reverse=True,
    )
    for mount in mounts:
        await mount.unmount(
            timeout=state.deadline - loop.time(),
            force=True,
        )


async def _disable_unmount_run_tmpfs(state: CleanState) -> None:
    loop = asyncio.get_running_loop()

    # disable Bertrand's managed tmpfs unit when systemd is available
    if shutil.which("systemctl"):
        await run(
            ["systemctl", "disable", "--now", RUN_TMPFS_MOUNT_UNIT_NAME],
            check=False,
            capture_output=True,
            timeout=state.deadline - loop.time(),
        )
        RUN_TMPFS_MOUNT_UNIT_PATH.unlink(missing_ok=True)
        await run(
            ["systemctl", "daemon-reload"],
            check=False,
            capture_output=True,
            timeout=state.deadline - loop.time(),
        )

    # always attempt runtime tmpfs unmount, even without systemd.
    run_mount = MountInfo.search(RUN_DIR)
    if run_mount is not None:
        await run_mount.unmount(
            timeout=state.deadline - loop.time(),
            force=True,
        )


async def _verify_runtime_residue(state: CleanState) -> None:
    issues: list[str] = []
    residual_mounts = sorted(
        {str(mount.mount_point) for mount in MountInfo.under(REPO_DIR)}
    )
    if MountInfo.search(RUN_DIR) is not None:
        residual_mounts.append(str(RUN_DIR))
    residual_aliases = sorted(
        str(alias)
        for alias in state.captured_aliases
        if alias.exists() or alias.is_symlink()
    )
    if residual_mounts:
        issues.append(
            f"residual mounts remain after cleanup: {', '.join(residual_mounts)}"
        )
    if residual_aliases:
        issues.append(
            "residual managed repository aliases remain after cleanup: "
            + ", ".join(residual_aliases)
        )
    if issues:
        raise OSError("runtime cleanup did not converge:\n- " + "\n- ".join(issues))


async def _remove_state_dir(state: CleanState) -> None:
    if not STATE_DIR.exists() and not STATE_DIR.is_symlink():
        return
    if STATE_DIR.is_symlink() or STATE_DIR.is_file():
        STATE_DIR.unlink()
    else:
        shutil.rmtree(STATE_DIR)


async def _verify_post_clean(state: CleanState) -> None:
    issues: list[str] = []
    if STATE_DIR.exists() or STATE_DIR.is_symlink():
        issues.append(f"state directory still exists after cleanup: {STATE_DIR}")
    residual_mounts = sorted(
        {str(mount.mount_point) for mount in MountInfo.under(REPO_DIR)}
    )
    if MountInfo.search(RUN_DIR) is not None:
        residual_mounts.append(str(RUN_DIR))
    residual_aliases = sorted(
        str(alias)
        for alias in state.captured_aliases
        if alias.exists() or alias.is_symlink()
    )
    if residual_mounts:
        issues.append(
            f"residual mounts remain after cleanup: {', '.join(residual_mounts)}"
        )
    if residual_aliases:
        issues.append(
            "residual managed repository aliases remain after cleanup: "
            + ", ".join(residual_aliases)
        )
    if issues:
        raise OSError("post-clean verification failed:\n- " + "\n- ".join(issues))


CLEAN_STAGES: tuple[tuple[str, Callable[[CleanState], Awaitable[None]], bool], ...] = (
    ("stop_buildkitd", _stop_buildkitd, False),
    ("clean_nerdctl_objects", _clean_nerdctl_objects, False),
    ("clean_repo_mounts_aliases", _clean_repo_mounts_aliases, False),
    ("disable_unmount_run_tmpfs", _disable_unmount_run_tmpfs, False),
    ("verify_runtime_residue", _verify_runtime_residue, True),
    ("remove_state_dir", _remove_state_dir, False),
    ("verify_post_clean", _verify_post_clean, True),
)


async def bertrand_clean(*, timeout: float, assume_yes: bool, force: bool) -> None:
    """Clean Bertrand-managed runtime objects and local state on the host.

    Parameters
    ----------
    timeout : float
        Maximum time in seconds to wait for cleanup convergence.  If infinite, wait
        indefinitely.
    assume_yes : bool
        Whether to auto-accept prompts during cleanup.
    force : bool
        Whether to continue through non-strict stage failures, logging warnings
        instead of failing immediately.

    Raises
    ------
    TimeoutError
        If cleanup does not complete before `timeout`.
    PermissionError
        If the user lacks root privileges or they decline cleanup.
    OSError
        If cleanup fails to converge or a strict stage fails.
    """
    if timeout <= 0:
        raise TimeoutError("timed out before cleanup started")

    if os.geteuid() != 0:
        raise PermissionError(
            "Global Bertrand cleanup requires root privileges.  Re-run this command "
            "with sudo."
        )
    if not confirm(
        "This will remove Bertrand-managed containers, images, volumes, and "
        f"networks (label `{BERTRAND_ENV}=1`) and then delete local Bertrand state in "
        f"{STATE_DIR}.  It will not uninstall "
        "MicroK8s or revert host system settings.  Do you want to proceed?\n[y/N] ",
        assume_yes=assume_yes,
    ):
        raise PermissionError("Cleanup declined by user.")

    loop = asyncio.get_running_loop()
    state = CleanState(
        assume_yes=assume_yes,
        force=force,
        deadline=loop.time() + timeout,
    )
    for name, stage, strict in CLEAN_STAGES:
        try:
            if loop.time() >= state.deadline:
                raise TimeoutError(
                    f"bertrand clean stage '{name}' timed out before execution"
                )
            await stage(state)
        except asyncio.CancelledError:
            raise
        except Exception as err:
            if not state.force or strict:
                raise OSError(f"bertrand clean stage '{name}' failed: {err}") from err
            print(
                "bertrand: warning: clean stage "
                f"'{name}' failed; continuing due to --force: {err}",
                file=sys.stderr,
            )
