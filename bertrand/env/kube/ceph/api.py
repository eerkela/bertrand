"""MicroCeph host command facade for Bertrand's Ceph runtime."""

from __future__ import annotations

import json
import re
import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING

from bertrand.env.git import (
    INFINITY,
    CompletedProcess,
    run,
)

if TYPE_CHECKING:
    import subprocess
    from collections.abc import Mapping

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
        """Normalize and validate the allocation request.

        Raises
        ------
        ValueError
            If the requested loop size or count is invalid.
        """
        object.__setattr__(self, "size", _normalize_size(self.size))
        if (
            not isinstance(self.count, int)
            or isinstance(self.count, bool)
            or self.count <= 0
        ):
            msg = f"invalid MicroCeph loop OSD count: {self.count!r}"
            raise ValueError(msg)

    @property
    def bytes(self) -> int:
        """Return the total requested allocation size in bytes.

        Returns
        -------
        int
            Total requested bytes across all loop OSDs.
        """
        return parse_size_bytes(self.size) * self.count

    def render(self) -> str:
        """Render this allocation as a MicroCeph loop OSD spec string.

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


async def ceph(
    argv: list[str],
    *,
    check: bool = True,
    capture_output: bool | None = False,
    stdin: str | None = None,
    timeout: float = INFINITY,
    attempts: int = 1,
    delay: float = 0.1,
    cwd: Path | None = None,
    env: Mapping[str, str] | None = None,
) -> CompletedProcess:
    """Invoke the MicroCeph-backed Ceph CLI.

    Parameters
    ----------
    argv : list[str]
        Ceph arguments without the `microceph.ceph` prefix.
    check : bool, optional
        Whether nonzero command exits raise `CommandError`.
    capture_output : bool | None, optional
        Whether to capture, inherit, or tee subprocess output.
    stdin : str | None, optional
        Optional text to pass to command stdin.
    timeout : float, optional
        Maximum command runtime in seconds.
    attempts : int, optional
        Number of command attempts.
    delay : float, optional
        Delay between attempts in seconds.
    cwd : Path | None, optional
        Optional working directory.
    env : Mapping[str, str] | None, optional
        Optional environment overrides.

    Returns
    -------
    CompletedProcess
        Completed command result.
    """
    return await run(
        ["microceph.ceph", *argv],
        check=check,
        capture_output=capture_output,
        stdin=stdin,
        timeout=timeout,
        attempts=attempts,
        delay=delay,
        cwd=cwd,
        env=env,
    )


def _normalize_size(size: str) -> str:
    normalized = size.strip().upper()
    if not re.fullmatch(LOOP_OSD_SIZE_PATTERN, normalized):
        msg = f"invalid MicroCeph loop OSD size: {size!r}"
        raise ValueError(msg)
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

    Raises
    ------
    ValueError
        If `value` does not use the supported MicroCeph loop OSD spec format.
    """
    parts = [part.strip() for part in value.strip().split(",")]
    if len(parts) != 3 or parts[0] != "loop":
        msg = f"invalid MicroCeph loop OSD spec: {value!r}"
        raise ValueError(msg)
    try:
        count = int(parts[2])
    except ValueError as err:
        msg = f"invalid MicroCeph loop OSD count: {parts[2]!r}"
        raise ValueError(msg) from err
    return LoopOSDSpec(size=parts[1], count=count)


async def host_command(
    argv: list[str],
    *,
    timeout: float,
) -> subprocess.CompletedProcess[str]:
    """Run a MicroCeph command in host or privileged-container context.

    Parameters
    ----------
    argv : list[str]
        Command vector to execute.
    timeout : float
        Maximum command runtime in seconds.

    Returns
    -------
    subprocess.CompletedProcess[str]
        Completed command result with captured text streams.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    """
    if timeout <= 0:
        msg = "timeout must be non-negative"
        raise TimeoutError(msg)
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
    """Inspect raw Ceph cluster capacity using MicroCeph's Ceph CLI.

    Parameters
    ----------
    timeout : float
        Maximum command runtime in seconds.

    Returns
    -------
    CephCapacitySnapshot
        Parsed raw capacity snapshot.

    Raises
    ------
    OSError
        If the MicroCeph command fails or returns malformed capacity data.
    """
    result = await host_command(
        ["microceph.ceph", "df", "--format", "json"], timeout=timeout
    )
    if result.returncode != 0:
        msg = f"failed to inspect Ceph capacity:\n{result}"
        raise OSError(msg)
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as err:
        msg = f"ceph df returned malformed JSON: {err}"
        raise OSError(msg) from err

    stats = payload.get("stats", {}) if isinstance(payload, dict) else {}
    total = int(stats.get("total_bytes") or stats.get("total_space") or 0)
    used = int(
        stats.get("total_used_bytes")
        or stats.get("total_used_raw_bytes")
        or stats.get("total_used")
        or 0
    )
    if total <= 0:
        msg = f"ceph df reported invalid total capacity: {total}"
        raise OSError(msg)
    if used < 0:
        msg = f"ceph df reported invalid used capacity: {used}"
        raise OSError(msg)
    return CephCapacitySnapshot(
        total_bytes=total, used_bytes=used, used_ratio=used / total
    )


def host_free_bytes(path: Path = MICROCEPH_LOOP_STORAGE_PATH) -> NodeCapacitySnapshot:
    """Inspect free host bytes for MicroCeph loop-backed OSD storage.

    Parameters
    ----------
    path : Path, default MICROCEPH_LOOP_STORAGE_PATH
        Host path whose filesystem free space should be inspected.

    Returns
    -------
    NodeCapacitySnapshot
        Free-space snapshot for the requested host path.
    """
    target = _container_path(path)
    usage = shutil.disk_usage(target)
    return NodeCapacitySnapshot(free_bytes=int(usage.free), path=path)


async def add_loop_osd(loop_spec: LoopOSDSpec | str, *, timeout: float) -> None:
    """Add one or more loop-backed OSDs through MicroCeph.

    Parameters
    ----------
    loop_spec : LoopOSDSpec | str
        Loop OSD allocation request.
    timeout : float
        Maximum command runtime in seconds.

    Raises
    ------
    OSError
        If MicroCeph rejects the loop OSD allocation.
    """
    spec = (
        loop_spec
        if isinstance(loop_spec, LoopOSDSpec)
        else parse_loop_osd_spec(loop_spec)
    )
    rendered = spec.render()
    result = await host_command(["microceph", "disk", "add", rendered], timeout=timeout)
    if result.returncode != 0:
        msg = f"microceph disk add failed for {rendered}:\n{result}"
        raise OSError(msg)
