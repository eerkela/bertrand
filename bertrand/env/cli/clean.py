"""Teardown Bertrand-managed local runtime state.

This cleanup intentionally removes Bertrand-owned runtime artifacts only.  It does
not uninstall MicroK8s, MicroCeph, system packages, or host user groups.
"""
from __future__ import annotations

import asyncio
import json
import os
import shutil
import sys
from collections.abc import Awaitable, Callable
from dataclasses import dataclass, field
from pathlib import Path

from ..ceph import MountInfo
from ..run import (
    BERTRAND_ENV,
    NERDCTL_BIN,
    REPO_ALIASES_EXT,
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
    symlink_points_to,
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
    captured_aliases : set[Path]
        Managed aliases discovered during cleanup for residual verification.
    """
    assume_yes: bool
    force: bool
    captured_aliases: set[Path] = field(default_factory=set)


def _managed_aliases_for_mount(repo_root: Path, mount_path: Path) -> set[Path]:
    path = repo_root / REPO_ALIASES_EXT
    if not path.exists():
        return set()
    if not path.is_file():
        print(
            f"bertrand: warning: repository alias index path is not a file: {path}",
            file=sys.stderr,
        )
        return set()
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError) as err:
        print(
            f"bertrand: warning: failed to parse repository alias index at {path}: {err}",
            file=sys.stderr,
        )
        return set()
    if not isinstance(data, list):
        print(
            "bertrand: warning: repository alias index has invalid format "
            f"(expected JSON list): {path}",
            file=sys.stderr,
        )
        return set()
    aliases: set[Path] = set()
    for item in data:
        if not isinstance(item, str):
            print(
                "bertrand: warning: repository alias index contains non-string entry "
                f"at {path}: {type(item).__name__}",
                file=sys.stderr,
            )
            continue
        alias = Path(item)
        if not alias.is_absolute() or any(part in (".", "..") for part in alias.parts):
            print(
                f"bertrand: warning: repository alias index contains invalid alias "
                f"path at {path}: {alias}",
                file=sys.stderr,
            )
            continue
        if symlink_points_to(alias, mount_path):
            aliases.add(alias)
    return aliases


async def _force_unmount(path: Path) -> None:
    if MountInfo.search(path) is None:
        return
    try:
        result = await run(
            ["umount", "-f", str(path)],
            check=False,
            capture_output=True,
        )
        if result.returncode != 0 and result.stderr:
            print(
                f"bertrand: warning: forced unmount failed for {path}: "
                f"{result.stderr.strip()}",
                file=sys.stderr,
            )
    except OSError as err:
        print(
            f"bertrand: warning: failed to run forced unmount for {path}: {err}",
            file=sys.stderr,
        )

    if MountInfo.search(path) is None:
        return

    try:
        result = await run(
            ["umount", "-l", str(path)],
            check=False,
            capture_output=True,
        )
        if result.returncode != 0 and result.stderr:
            print(
                f"bertrand: warning: lazy unmount failed for {path}: "
                f"{result.stderr.strip()}",
                file=sys.stderr,
            )
    except OSError as err:
        print(
            f"bertrand: warning: failed to run lazy unmount for {path}: {err}",
            file=sys.stderr,
        )

    if MountInfo.search(path) is not None:
        raise OSError(f"mount still attached after forced/lazy unmount: {path}")


def _residual_aliases(captured_aliases: set[Path]) -> list[str]:
    return sorted(
        str(alias)
        for alias in captured_aliases
        if alias.exists() or alias.is_symlink()
    )


def _residual_mounts() -> list[str]:
    mounts = sorted({str(path) for path in MountInfo.under(REPO_DIR)}, key=str)
    if MountInfo.search(RUN_DIR) is not None:
        mounts.append(str(RUN_DIR))
    return mounts


# TODO: bertrand clean should be able to take a timeout parameter and pass that through
# to stop_buildkit and any other stages that might need it


async def _stop_buildkitd(state: CleanState) -> None:
    await stop_buildkit()


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
    for kind, remove_prefix in cleanup_table:
        ids = await nerdctl_ids(kind, {BERTRAND_ENV: "1"})
        for i in range(0, len(ids), chunk_size):
            await nerdctl(
                [*remove_prefix, *ids[i:i + chunk_size]],
                check=False,
            )


async def _clean_repo_mounts_aliases(state: CleanState) -> None:
    if not REPO_DIR.exists():
        return
    if not REPO_DIR.is_dir():
        raise OSError(f"repository root is not a directory: {REPO_DIR}")

    repo_roots = sorted(
        (
            entry for entry in REPO_DIR.iterdir()
            if entry.is_dir() and not entry.is_symlink()
        ),
        key=lambda item: item.as_posix(),
    )

    for repo_root in repo_roots:
        mount_path = repo_root / REPO_MOUNT_EXT
        managed_aliases = _managed_aliases_for_mount(repo_root, mount_path)
        state.captured_aliases.update(managed_aliases)
        for alias in sorted(
            managed_aliases,
            key=lambda item: item.as_posix(),
        ):
            alias.unlink()

        await _force_unmount(mount_path)
        shutil.rmtree(repo_root)

    # safety sweep in case metadata was missing or corrupt
    mounts = sorted(
        MountInfo.under(REPO_DIR),
        key=lambda item: len(item.parts),
        reverse=True,
    )
    for mount in mounts:
        await _force_unmount(mount)


async def _disable_unmount_run_tmpfs(state: CleanState) -> None:
    # disable Bertrand's managed tmpfs unit when systemd is available
    if shutil.which("systemctl"):
        await run(
            ["systemctl", "disable", "--now", RUN_TMPFS_MOUNT_UNIT_NAME],
            check=False,
            capture_output=True,
        )
        RUN_TMPFS_MOUNT_UNIT_PATH.unlink(missing_ok=True)
        await run(
            ["systemctl", "daemon-reload"],
            check=False,
            capture_output=True,
        )

    # always attempt runtime tmpfs unmount, even without systemd.
    await run(
        ["umount", str(RUN_DIR)],
        check=False,
        capture_output=True,
    )
    await _force_unmount(RUN_DIR)


async def _verify_runtime_residue(state: CleanState) -> None:
    issues: list[str] = []
    residual_mounts = _residual_mounts()
    residual_aliases = _residual_aliases(state.captured_aliases)
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
    residual_mounts = _residual_mounts()
    residual_aliases = _residual_aliases(state.captured_aliases)
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


async def bertrand_clean(*, assume_yes: bool, force: bool) -> None:
    """Clean Bertrand-managed runtime objects and local state on the host.

    Parameters
    ----------
    assume_yes : bool
        Whether to auto-accept prompts during cleanup.
    force : bool
        Whether to continue through non-strict stage failures, logging warnings
        instead of failing immediately.

    Raises
    ------
    PermissionError
        If the user lacks root privileges or they decline cleanup.
    OSError
        If cleanup fails to converge or a strict stage fails.
    """
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

    state = CleanState(
        assume_yes=assume_yes,
        force=force,
    )
    for name, stage, strict in CLEAN_STAGES:
        try:
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
