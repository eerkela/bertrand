"""MicroCeph host command facade for Bertrand's Ceph runtime."""

from __future__ import annotations

import asyncio
import json
import math
import re
import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING, Any

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
MICROCEPH_DISK_REMOVE_TIMEOUT_SECONDS = 300
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
class CephHealthSnapshot:
    """Cluster health state used by shrink safety planning.

    Attributes
    ----------
    status : str
        Ceph health status, such as ``"HEALTH_OK"``.
    clean : bool
        Whether Ceph reports healthy placement groups with no active recovery or
        degradation states.
    detail : str
        Concise diagnostic text explaining the health decision.
    """

    status: str
    clean: bool
    detail: str


@dataclass(frozen=True)
class CephOSD:
    """One OSD reported by Ceph's CRUSH tree.

    Attributes
    ----------
    osd_id : int
        Numeric OSD identifier.
    node_name : str
        Host bucket that currently owns the OSD.
    up : bool
        Whether the OSD is up.
    in_cluster : bool
        Whether the OSD currently has nonzero cluster weight.
    """

    osd_id: int
    node_name: str
    up: bool
    in_cluster: bool

    @property
    def name(self) -> str:
        """Return the canonical Ceph OSD name.

        Returns
        -------
        str
            OSD name in ``osd.<id>`` form.
        """
        return f"osd.{self.osd_id}"


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


def _json_payload(stdout: str, *, context: str) -> Mapping[str, Any]:
    try:
        payload = json.loads(stdout)
    except json.JSONDecodeError as err:
        msg = f"{context} returned malformed JSON: {err}"
        raise OSError(msg) from err
    if not isinstance(payload, dict):
        msg = f"{context} returned malformed JSON: expected an object"
        raise OSError(msg)
    return payload


def _osd_id(value: object) -> int | None:
    if isinstance(value, int) and value >= 0:
        return value
    if isinstance(value, str):
        match = re.fullmatch(r"osd\.([0-9]+)", value.strip())
        if match is not None:
            return int(match.group(1))
    return None


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
    payload = _json_payload(result.stdout, context="ceph df")

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


async def ceph_health(*, timeout: float) -> CephHealthSnapshot:
    """Inspect Ceph health and placement-group cleanliness.

    Parameters
    ----------
    timeout : float
        Maximum command runtime in seconds.

    Returns
    -------
    CephHealthSnapshot
        Parsed Ceph health and cleanliness state.

    Raises
    ------
    OSError
        If Ceph health cannot be queried or parsed.
    """
    result = await host_command(
        ["microceph.ceph", "status", "--format", "json"],
        timeout=timeout,
    )
    if result.returncode != 0:
        msg = f"failed to inspect Ceph health:\n{result}"
        raise OSError(msg)
    payload = _json_payload(result.stdout, context="ceph status")
    health = payload.get("health", {})
    status = ""
    if isinstance(health, dict):
        status = str(health.get("status") or "").strip().upper()
    pgmap = payload.get("pgmap", {})
    states = pgmap.get("pgs_by_state", []) if isinstance(pgmap, dict) else []
    non_clean: list[str] = []
    if isinstance(states, list):
        for item in states:
            if not isinstance(item, dict):
                continue
            state = str(item.get("state_name") or "").strip()
            count = int(item.get("count") or 0)
            if count > 0 and state != "active+clean":
                non_clean.append(f"{count} {state}")
    clean = status == "HEALTH_OK" and not non_clean
    detail = status or "UNKNOWN"
    if non_clean:
        detail = f"{detail}; non-clean PGs: {', '.join(non_clean)}"
    return CephHealthSnapshot(status=status, clean=clean, detail=detail)


async def ceph_osds(*, timeout: float) -> tuple[CephOSD, ...]:
    """Inspect the current Ceph OSD tree.

    Parameters
    ----------
    timeout : float
        Maximum command runtime in seconds.

    Returns
    -------
    tuple[CephOSD, ...]
        Live OSD inventory sorted by OSD ID.

    Raises
    ------
    OSError
        If the OSD tree cannot be queried or parsed.
    """
    result = await host_command(
        ["microceph.ceph", "osd", "tree", "--format", "json"],
        timeout=timeout,
    )
    if result.returncode != 0:
        msg = f"failed to inspect Ceph OSD tree:\n{result}"
        raise OSError(msg)
    payload = _json_payload(result.stdout, context="ceph osd tree")
    nodes = payload.get("nodes")
    if not isinstance(nodes, list):
        msg = "ceph osd tree returned malformed JSON: expected 'nodes' list"
        raise OSError(msg)

    host_by_osd: dict[int, str] = {}
    for node in nodes:
        if not isinstance(node, dict) or node.get("type") != "host":
            continue
        name = str(node.get("name") or "").strip()
        children = node.get("children")
        if not name or not isinstance(children, list):
            continue
        for child in children:
            osd_id = _osd_id(child)
            if osd_id is not None:
                host_by_osd[osd_id] = name

    osds: list[CephOSD] = []
    for node in nodes:
        if not isinstance(node, dict) or node.get("type") != "osd":
            continue
        osd_id = _osd_id(node.get("id"))
        if osd_id is None:
            osd_id = _osd_id(node.get("name"))
        if osd_id is None:
            continue
        status = str(node.get("status") or "").strip().lower()
        try:
            reweight = float(node.get("reweight", 0.0) or 0.0)
        except (TypeError, ValueError):
            reweight = 0.0
        osds.append(
            CephOSD(
                osd_id=osd_id,
                node_name=host_by_osd.get(osd_id, ""),
                up=status == "up",
                in_cluster=reweight > 0.0,
            )
        )
    return tuple(sorted(osds, key=lambda item: item.osd_id))


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


async def add_loop_osd(
    loop_spec: LoopOSDSpec | str,
    *,
    timeout: float,
) -> tuple[int, ...]:
    """Add one or more loop-backed OSDs through MicroCeph.

    Parameters
    ----------
    loop_spec : LoopOSDSpec | str
        Loop OSD allocation request.
    timeout : float
        Maximum command runtime in seconds.

    Returns
    -------
    tuple[int, ...]
        Numeric OSD IDs created by the add operation, when they can be inferred.

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
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    before = {osd.osd_id for osd in await ceph_osds(timeout=deadline - loop.time())}
    result = await host_command(
        ["microceph", "disk", "add", rendered],
        timeout=deadline - loop.time(),
    )
    if result.returncode != 0:
        msg = f"microceph disk add failed for {rendered}:\n{result}"
        raise OSError(msg)
    after = {osd.osd_id for osd in await ceph_osds(timeout=deadline - loop.time())}
    return tuple(sorted(after - before))


async def remove_osd(osd_id: int, *, timeout: float) -> None:
    """Remove one OSD through MicroCeph's guarded disk removal command.

    Parameters
    ----------
    osd_id : int
        Numeric OSD identifier to remove.
    timeout : float
        Maximum command runtime in seconds.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    OSError
        If MicroCeph rejects the disk removal or the OSD remains present.
    ValueError
        If `osd_id` is negative.
    """
    if osd_id < 0:
        msg = f"invalid Ceph OSD ID: {osd_id!r}"
        raise ValueError(msg)
    if timeout <= 0:
        msg = "MicroCeph OSD removal timeout must be non-negative"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    if math.isinf(timeout):
        remove_timeout = MICROCEPH_DISK_REMOVE_TIMEOUT_SECONDS
    else:
        remove_timeout = max(
            1,
            min(MICROCEPH_DISK_REMOVE_TIMEOUT_SECONDS, int(timeout)),
        )
    name = f"osd.{osd_id}"
    result = await host_command(
        ["microceph", "disk", "remove", name, "--timeout", str(remove_timeout)],
        timeout=deadline - loop.time(),
    )
    if result.returncode != 0:
        msg = f"microceph disk remove failed for {name}:\n{result}"
        raise OSError(msg)
    remaining = {osd.osd_id for osd in await ceph_osds(timeout=deadline - loop.time())}
    if osd_id in remaining:
        msg = f"microceph disk remove completed but {name} is still present"
        raise OSError(msg)
