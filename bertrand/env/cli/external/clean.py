"""Teardown Bertrand-managed local runtime state in a shared cluster.

Bertrand v1 treats the default MicroK8s snap and in-cluster Rook-Ceph substrate
as shared runtime state.  Cleanup removes only reconstructible Bertrand cluster
state, host-local Bertrand state, and this host's mount aliases; it never
uninstalls snaps or deletes durable repository PVCs, volume records,
credentials, snapshots, or Ceph data.
"""

from __future__ import annotations

import asyncio
import os
import shutil
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import TYPE_CHECKING
from uuid import UUID

from bertrand.env.git import (
    Deadline,
    confirm,
    symlink_points_to,
)
from bertrand.env.host import (
    HOST_ID_FILE,
    REPO_DIR,
    REPO_MOUNT_EXT,
    RUN_DIR,
    STATE_DIR,
    disable_run_tmpfs_mount,
)
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.ceph.mount import MountInfo
from bertrand.env.kube.ceph.volume import (
    REPOSITORY_MOUNT_HOST_HASH_LABEL,
    REPOSITORY_MOUNT_PHASE_LABEL,
    ensure_repository_mount_crd,
    list_repository_mount_records,
    repository_mount_host_hash,
    retire_repository_mount_record,
)
from bertrand.env.kube.dashboard import delete_dashboard_backend
from bertrand.env.kube.dev import delete_dev_backend_state
from bertrand.env.kube.node_identity import retire_bertrand_node

if TYPE_CHECKING:
    from collections.abc import Awaitable, Callable


@dataclass
class CleanState:
    """In-memory convergence state for `bertrand clean` stages.

    Attributes
    ----------
    assume_yes : bool
        Whether interactive confirmations should be auto-accepted.
    force : bool
        Whether intermediate stage errors should be downgraded to warnings.
    deadline : Deadline
        Shared cleanup deadline.
    kube : Kube | None
        Optional Kubernetes API context.  When unavailable under `--force`, cleanup
        is limited to local host state.
    host_id : str | None
        Durable Bertrand host UUID used to retire this host's mount records.
    captured_aliases : set[Path]
        Managed aliases discovered during cleanup for residual verification.
    """

    assume_yes: bool
    force: bool
    deadline: Deadline
    kube: Kube | None
    host_id: str | None
    captured_aliases: set[Path] = field(default_factory=set)


def _host_id() -> str | None:
    try:
        return UUID(HOST_ID_FILE.read_text(encoding="utf-8").strip()).hex
    except (OSError, ValueError):
        return None


def _warn(message: str) -> None:
    print(f"bertrand: warning: {message}", file=sys.stderr)


_CLEAN_ERROR_TYPES = (OSError, RuntimeError, ValueError)


async def _clean_repo_mounts_aliases(state: CleanState) -> None:
    if state.kube is not None and state.host_id is not None:
        # This is the only durable cluster mutation in `bertrand clean`: retire
        # this host's mount aliases while preserving repository volumes for
        # recovery or future explicit destructive cleanup.
        await ensure_repository_mount_crd(
            state.kube,
            timeout=state.deadline.remaining(),
        )
        records = await list_repository_mount_records(
            state.kube,
            labels={
                REPOSITORY_MOUNT_HOST_HASH_LABEL: repository_mount_host_hash(
                    state.host_id
                ),
                REPOSITORY_MOUNT_PHASE_LABEL: "active",
            },
            timeout=state.deadline.remaining(),
        )
        for record in records:
            if record.host_id != state.host_id or record.phase != "Active":
                continue
            alias = Path(record.alias_path)
            hidden_mount = REPO_DIR / record.repo_id / REPO_MOUNT_EXT
            state.captured_aliases.add(alias)
            await retire_repository_mount_record(
                state.kube,
                record=record,
                timeout=state.deadline.remaining(),
            )
            if symlink_points_to(alias, hidden_mount):
                alias.unlink()
            elif alias.exists() or alias.is_symlink():
                msg = (
                    f"recorded repository alias path {alias} is occupied but is not "
                    f"a managed symlink to {hidden_mount}"
                )
                if not state.force:
                    raise OSError(msg)
                _warn(msg)

    if not REPO_DIR.exists():
        return
    if not REPO_DIR.is_dir():
        msg = f"repository root is not a directory: {REPO_DIR}"
        raise OSError(msg)

    # collect all repository mounts on the host system
    repo_roots = sorted(
        (
            entry
            for entry in REPO_DIR.iterdir()
            if entry.is_dir() and not entry.is_symlink()
        ),
        key=lambda item: item.as_posix(),
    )

    # for each hidden repository mount, detach it before deleting local state
    for repo_root in repo_roots:
        mount_path = repo_root / REPO_MOUNT_EXT
        mount = MountInfo.search(mount_path)
        if mount is not None:
            await mount.unmount(timeout=state.deadline.remaining(), force=True)
        shutil.rmtree(repo_root)

    # safety sweep in case metadata was missing or corrupt
    mounts = sorted(
        MountInfo.under(REPO_DIR).values(),
        key=lambda item: len(item.mount_point.parts),
        reverse=True,
    )
    for mount in mounts:
        await mount.unmount(timeout=state.deadline.remaining(), force=True)


async def _disable_unmount_run_tmpfs(state: CleanState) -> None:
    await disable_run_tmpfs_mount(timeout=state.deadline.remaining())

    # always attempt runtime tmpfs unmount, even without systemd
    run_mount = MountInfo.search(RUN_DIR)
    if run_mount is not None:
        await run_mount.unmount(timeout=state.deadline.remaining(), force=True)


async def _clean_dashboard_backend(state: CleanState) -> None:
    if state.kube is None:
        return
    await delete_dashboard_backend(state.kube, timeout=state.deadline.remaining())


async def _clean_dev_backend(state: CleanState) -> None:
    if state.kube is None:
        return
    await delete_dev_backend_state(
        state.kube,
        host_id=state.host_id,
        timeout=state.deadline.remaining(),
    )


async def _retire_local_node_record(state: CleanState) -> None:
    if state.kube is None or state.host_id is None:
        return
    await retire_bertrand_node(
        state.kube,
        host_id=state.host_id,
        timeout=state.deadline.remaining(),
    )


def _runtime_residue(state: CleanState) -> tuple[list[str], list[str]]:
    residual_mounts = sorted(str(mount) for mount in MountInfo.under(REPO_DIR))
    if MountInfo.search(RUN_DIR) is not None:
        residual_mounts.append(str(RUN_DIR))
    residual_aliases = sorted(
        str(alias)
        for alias in state.captured_aliases
        if alias.exists() or alias.is_symlink()
    )
    return residual_mounts, residual_aliases


async def _finalize_cleanup(state: CleanState) -> None:
    issues: list[str] = []

    # attempt final remediation
    residual_mounts, residual_aliases = _runtime_residue(state)
    if (residual_mounts or residual_aliases) and state.force:
        await _clean_repo_mounts_aliases(state)
        state.deadline.check("bertrand clean stage 'finalize_cleanup' timed out")
        await _disable_unmount_run_tmpfs(state)
        residual_mounts, residual_aliases = _runtime_residue(state)
    if residual_mounts:
        issues.append(
            f"residual mounts remain after cleanup: {', '.join(residual_mounts)}"
        )
    if residual_aliases:
        issues.append(
            "residual managed repository aliases remain after cleanup: "
            f"{', '.join(residual_aliases)}"
        )

    # if mounts and aliases are all clean, remove state directory
    if not issues and (STATE_DIR.exists() or STATE_DIR.is_symlink()):
        if STATE_DIR.is_symlink() or STATE_DIR.is_file():
            STATE_DIR.unlink()
        else:
            shutil.rmtree(STATE_DIR)
    if STATE_DIR.exists() or STATE_DIR.is_symlink():
        issues.append(f"state directory still exists after cleanup: {STATE_DIR}")

    # raise errors together at the end for a non-zero exit code
    if issues:
        msg = f"runtime cleanup did not converge:\n- {'\n- '.join(issues)}"
        raise OSError(msg)


CLEAN_STAGES: tuple[tuple[str, Callable[[CleanState], Awaitable[None]]], ...] = (
    ("clean_dev_backend", _clean_dev_backend),
    ("clean_dashboard_backend", _clean_dashboard_backend),
    ("clean_repo_mounts_aliases", _clean_repo_mounts_aliases),
    ("retire_local_node_record", _retire_local_node_record),
    ("disable_unmount_run_tmpfs", _disable_unmount_run_tmpfs),
    ("finalize_cleanup", _finalize_cleanup),
)


async def _run_clean_stages(state: CleanState) -> None:
    for i, (name, stage) in enumerate(CLEAN_STAGES):
        state.deadline.check(
            f"bertrand clean stage '{name}' timed out before execution"
        )
        try:
            await stage(state)
        except asyncio.CancelledError:
            raise
        except _CLEAN_ERROR_TYPES as err:
            if not state.force or i == len(CLEAN_STAGES) - 1:
                msg = f"bertrand clean stage {name!r} failed: {err}"
                raise OSError(msg) from err
            _warn(
                f"clean stage {name!r} failed; continuing due to --force: {err}"
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
        Whether to continue through intermediate stage failures, logging warnings
        and attempting subsequent stages.

    Raises
    ------
    TimeoutError
        If cleanup does not complete before `timeout`.
    PermissionError
        If the user lacks root privileges or they decline cleanup.
    OSError
        If cleanup fails to converge.
    """
    message = "timed out before cleanup started"
    if timeout <= 0:
        raise TimeoutError(message)
    deadline = Deadline.from_timeout(timeout, message=message)

    # require root privileges for global cleanup
    if os.geteuid() != 0:
        msg = (
            "Global Bertrand cleanup requires root privileges.  Re-run this command "
            "with sudo."
        )
        raise PermissionError(msg)
    if not confirm(
        "This operates on a shared MicroK8s/Rook-Ceph runtime. It will retire "
        "this host's Bertrand repository mount and node records, delete volatile "
        "dev-session and dashboard resources, remove local repository aliases and "
        f"hidden mounts, and delete local Bertrand state in {STATE_DIR}. "
        "It preserves repository PVCs, volume records, credentials, snapshots, and "
        "Ceph data, and does not uninstall MicroK8s. Do you want to "
        "proceed?\n[y/N] ",
        assume_yes=assume_yes,
    ):
        msg = "Cleanup declined by user."
        raise PermissionError(msg)

    kube: Kube | None = None
    try:
        kube = await Kube.host(timeout=deadline.remaining())
    except _CLEAN_ERROR_TYPES as err:
        if not force:
            msg = (
                "failed to connect to the shared Bertrand Kubernetes runtime for "
                "mount-record retirement. Normal cleanup needs cluster access so "
                f"it can retire this host's records without guessing: {err}"
            )
            raise OSError(msg) from err
        _warn(
            "continuing local cleanup without Kubernetes mount record retirement due "
            "to --force. Repository PVCs and active mount records may remain "
            f"recoverable in the shared cluster: {err}"
        )

    host_id = _host_id()
    if kube is not None and host_id is None and not force:
        msg = (
            f"failed to read Bertrand host identity at {HOST_ID_FILE}; cannot retire "
            "this host's repository mount records safely"
        )
        raise OSError(msg)

    # execute clean convergence stages in sequence
    state = CleanState(
        assume_yes=assume_yes,
        force=force,
        deadline=deadline,
        kube=kube,
        host_id=host_id,
    )
    if kube is None:
        await _run_clean_stages(state)
    else:
        with kube:
            await _run_clean_stages(state)
