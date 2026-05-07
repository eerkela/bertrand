"""MicroCeph host command facade for Bertrand's Ceph runtime."""

from __future__ import annotations

import json
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path

from ...run import run

MICROCEPH_HOST_ROOT = Path("/host")
MICROCEPH_LOOP_STORAGE_PATH = Path("/var/snap/microceph/common")


@dataclass(frozen=True)
class CephCapacitySnapshot:
    """Raw Ceph capacity snapshot used by autoscaler reconciliation.

    Attributes
    ----------
    total_bytes : int
        Total raw cluster capacity in bytes.
    used_bytes : int
        Currently used raw cluster capacity in bytes.
    used_ratio : float
        Used/total ratio in [0, 1].
    """

    total_bytes: int
    used_bytes: int
    used_ratio: float


@dataclass(frozen=True)
class NodeCapacitySnapshot:
    """Host-local capacity snapshot for one autoscaler agent node.

    Attributes
    ----------
    free_bytes : int
        Free bytes on the filesystem where MicroCeph loop files are stored.
    path : Path
        Host-visible path that was inspected.
    """

    free_bytes: int
    path: Path


def _container_path(path: Path) -> Path:
    if MICROCEPH_HOST_ROOT.is_dir():
        return MICROCEPH_HOST_ROOT / path.relative_to("/")
    return path


async def host_command(
    argv: list[str],
    *,
    timeout: float,
) -> subprocess.CompletedProcess[str]:
    """Run a host MicroCeph command from either host or privileged container context."""
    if timeout <= 0:
        raise TimeoutError("timeout must be non-negative")
    if MICROCEPH_HOST_ROOT.is_dir() and not shutil.which(argv[0]):
        cmd = ["chroot", str(MICROCEPH_HOST_ROOT), *argv]
    else:
        cmd = argv
    return await run(
        cmd,
        check=False,
        capture_output=True,
        timeout=timeout,
    )


async def ceph_df(*, timeout: float) -> CephCapacitySnapshot:
    """Inspect raw Ceph cluster capacity using MicroCeph's Ceph CLI."""
    result = await host_command(["microceph.ceph", "df", "--format", "json"], timeout=timeout)
    if result.returncode != 0:
        raise OSError(f"failed to inspect Ceph capacity:\n{result}")
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as err:
        raise OSError(f"ceph df returned malformed JSON: {err}") from err

    stats = payload.get("stats", {}) if isinstance(payload, dict) else {}
    total = int(stats.get("total_bytes") or stats.get("total_space") or 0)
    used = int(
        stats.get("total_used_bytes")
        or stats.get("total_used_raw_bytes")
        or stats.get("total_used")
        or 0
    )
    if total <= 0:
        raise OSError(f"ceph df reported invalid total capacity: {total}")
    if used < 0:
        raise OSError(f"ceph df reported invalid used capacity: {used}")
    return CephCapacitySnapshot(total_bytes=total, used_bytes=used, used_ratio=used / total)


def host_free_bytes(path: Path = MICROCEPH_LOOP_STORAGE_PATH) -> NodeCapacitySnapshot:
    """Inspect free host bytes for MicroCeph loop-backed OSD storage."""
    target = _container_path(path)
    usage = shutil.disk_usage(target)
    return NodeCapacitySnapshot(free_bytes=int(usage.free), path=path)


async def add_loop_osd(loop_spec: str, *, timeout: float) -> None:
    """Add one or more loop-backed OSDs through MicroCeph."""
    loop_spec = loop_spec.strip()
    if not loop_spec:
        raise ValueError("MicroCeph loop spec cannot be empty")
    result = await host_command(["microceph", "disk", "add", loop_spec], timeout=timeout)
    if result.returncode != 0:
        raise OSError(f"microceph disk add failed for {loop_spec}:\n{result}")
