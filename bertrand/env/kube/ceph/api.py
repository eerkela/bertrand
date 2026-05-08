"""MicroCeph host command facade for Bertrand's Ceph runtime."""

from __future__ import annotations

import json
import re
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path

from ...run import run

MICROCEPH_HOST_ROOT = Path("/host")
MICROCEPH_LOOP_STORAGE_PATH = Path("/var/snap/microceph/common")
LOOP_OSD_SIZE_PATTERN = r"^[1-9][0-9]*[MGT]$"
LOOP_OSD_SPEC_PATTERN = r"^loop,[1-9][0-9]*[MGT],[1-9][0-9]*$"


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


@dataclass(frozen=True)
class LoopOSDSpec:
    """MicroCeph loop-backed OSD allocation request.

    Parameters
    ----------
    size : str
        Per-loop OSD size such as ``"4G"``.
    count : int, default 1
        Number of loop OSDs to allocate.
    """

    size: str
    count: int = 1

    def __post_init__(self) -> None:
        object.__setattr__(self, "size", _normalize_size(self.size))
        if not isinstance(self.count, int) or isinstance(self.count, bool) or self.count <= 0:
            raise ValueError(f"invalid MicroCeph loop OSD count: {self.count!r}")

    @property
    def bytes(self) -> int:
        """
        Returns
        -------
        int
            Total requested bytes across all loop OSDs.
        """
        return parse_size_bytes(self.size) * self.count

    def render(self) -> str:
        """
        Returns
        -------
        str
            MicroCeph ``disk add`` loop spec string.
        """
        return f"loop,{self.size},{self.count}"


def _container_path(path: Path) -> Path:
    if MICROCEPH_HOST_ROOT.is_dir():
        return MICROCEPH_HOST_ROOT / path.relative_to("/")
    return path


def _normalize_size(size: str) -> str:
    normalized = size.strip().upper()
    if not re.fullmatch(LOOP_OSD_SIZE_PATTERN, normalized):
        raise ValueError(f"invalid MicroCeph loop OSD size: {size!r}")
    return normalized


def parse_size_bytes(size: str) -> int:
    """Parse a MicroCeph size string into bytes.

    Parameters
    ----------
    size : str
        Size string using ``M``, ``G``, or ``T`` suffixes.

    Returns
    -------
    int
        Parsed byte count.
    """
    normalized = _normalize_size(size)
    scale = {"M": 2**20, "G": 2**30, "T": 2**40}[normalized[-1]]
    return int(normalized[:-1]) * scale


def parse_loop_osd_spec(value: str) -> LoopOSDSpec:
    """Parse a MicroCeph loop OSD spec string.

    Parameters
    ----------
    value : str
        Loop OSD spec such as ``"loop,4G,1"``.

    Returns
    -------
    LoopOSDSpec
        Parsed allocation request.
    """
    parts = [part.strip() for part in value.strip().split(",")]
    if len(parts) != 3 or parts[0] != "loop":
        raise ValueError(f"invalid MicroCeph loop OSD spec: {value!r}")
    try:
        count = int(parts[2])
    except ValueError as err:
        raise ValueError(f"invalid MicroCeph loop OSD count: {parts[2]!r}") from err
    return LoopOSDSpec(size=parts[1], count=count)


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


async def add_loop_osd(loop_spec: LoopOSDSpec | str, *, timeout: float) -> None:
    """Add one or more loop-backed OSDs through MicroCeph."""
    spec = loop_spec if isinstance(loop_spec, LoopOSDSpec) else parse_loop_osd_spec(loop_spec)
    rendered = spec.render()
    result = await host_command(["microceph", "disk", "add", rendered], timeout=timeout)
    if result.returncode != 0:
        raise OSError(f"microceph disk add failed for {rendered}:\n{result}")
