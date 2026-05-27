"""Rook-backed Ceph command and host-capacity helpers."""

from __future__ import annotations

import contextlib
import json
import math
import os
import re
import shutil
import uuid
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Any, Literal, TypedDict, overload

from bertrand.env.git import (
    INFINITY,
    CompletedProcess,
    Deadline,
    HostLock,
    run,
    until,
)
from bertrand.env.host import HOST_ID_FILE

HOST_ROOT = Path("/host")
LOOP_FALLBACK_STORAGE_PATH = Path("/var/lib/bertrand/rook-loop")
OSD_DEVICE_LINK_ROOT = Path("/var/lib/bertrand/rook-osd/devices")
OSD_LOCK_FILE = Path("/var/lib/bertrand/run/rook-osd.lock")
ROOK_NAMESPACE = "rook-ceph"
ROOK_TOOLBOX_DEPLOYMENT = "deploy/rook-ceph-tools"
BERTRAND_LVM_VG = "bertrand"
SIZE_PATTERN = r"^[1-9][0-9]*[MGT]$"
LVM_TAG = "bertrand"


@dataclass(frozen=True)
class CephCapacitySnapshot:
    """Raw Ceph capacity snapshot used by autoscaler reconciliation."""

    total_bytes: int
    used_bytes: int
    used_ratio: float


class _LvmPV(TypedDict):
    """One physical volume belonging to Bertrand's preferred LVM volume group."""

    pv_name: str
    pv_uuid: str
    size_bytes: int
    free_bytes: int


@dataclass(frozen=True)
class PreparedOSD:
    """Host-local block substrate prepared for a Rook PVC-backed OSD."""

    block_path: Path
    observed_bytes: int
    pv_name: str = ""
    pv_uuid: str = ""
    pv_device: str = ""
    lv_name: str = ""
    lv_path: str = ""
    loop_file: Path | None = None
    loop_device: str = ""


@dataclass(frozen=True)
class CephOSD:
    """One OSD reported by Ceph's CRUSH tree."""

    osd_id: int
    node_name: str
    up: bool
    in_cluster: bool

    @property
    def name(self) -> str:
        """Return the canonical Ceph OSD name."""
        return f"osd.{self.osd_id}"


def parse_size_bytes(size: str) -> int:
    """Parse a Bertrand storage size string into bytes.

    Parameters
    ----------
    size : str
        Storage size such as ``"16G"``.

    Returns
    -------
    int
        Size in bytes.

    Raises
    ------
    ValueError
        If `size` is not a supported Bertrand storage size.
    """
    normalized = size.strip().upper()
    if not re.fullmatch(SIZE_PATTERN, normalized):
        msg = f"invalid storage size: {size!r}"
        raise ValueError(msg)
    scale = {"M": 1024**2, "G": 1024**3, "T": 1024**4}[normalized[-1]]
    return int(normalized[:-1]) * scale


async def _run_text(argv: list[str], *, timeout: float) -> str:
    result = await run(argv, timeout=timeout, capture_output=True)
    return result.stdout if isinstance(result.stdout, str) else result.stdout.decode()


def _ceph_argv(argv: list[str]) -> list[str]:
    if shutil.which("ceph"):
        return ["ceph", *argv]
    if shutil.which("microk8s"):
        return [
            "microk8s",
            "kubectl",
            "-n",
            ROOK_NAMESPACE,
            "exec",
            ROOK_TOOLBOX_DEPLOYMENT,
            "--",
            "ceph",
            *argv,
        ]
    return ["ceph", *argv]


@overload
async def ceph(
    argv: list[str],
    *,
    timeout: float = INFINITY,
    check: bool = True,
    capture_output: Literal[True],
) -> CompletedProcess: ...


@overload
async def ceph(
    argv: list[str],
    *,
    timeout: float = INFINITY,
    check: bool = True,
    capture_output: Literal[False] = False,
) -> str: ...


async def ceph(
    argv: list[str],
    *,
    timeout: float = INFINITY,
    check: bool = True,
    capture_output: bool = False,
) -> CompletedProcess | str:
    """Invoke the Ceph CLI for the Rook-managed cluster.

    Returns
    -------
    CompletedProcess | str
        Captured process output when requested, otherwise stdout text.
    """
    if capture_output:
        return await run(
            _ceph_argv(argv),
            timeout=timeout,
            check=check,
            capture_output=True,
        )
    return await _run_text(_ceph_argv(argv), timeout=timeout)


def _json_object(text: str, *, context: str) -> dict[str, Any]:
    try:
        payload = json.loads(text)
    except json.JSONDecodeError as err:
        msg = f"could not parse {context} JSON: {err}"
        raise OSError(msg) from err
    if not isinstance(payload, dict):
        msg = f"{context} did not return a JSON object"
        raise OSError(msg)
    return payload


async def ceph_df(*, timeout: float) -> CephCapacitySnapshot:
    """Inspect raw Ceph cluster capacity.

    Returns
    -------
    CephCapacitySnapshot
        Raw capacity totals reported by Ceph.

    Raises
    ------
    OSError
        If the Ceph CLI output is malformed or reports no capacity.
    """
    payload = _json_object(
        str(await ceph(["df", "--format", "json"], timeout=timeout)),
        context="ceph df",
    )
    stats = payload.get("stats")
    if not isinstance(stats, dict):
        msg = "ceph df output did not include stats"
        raise OSError(msg)
    total = int(stats.get("total_bytes") or 0)
    used = int(stats.get("total_used_bytes") or stats.get("total_used_raw_bytes") or 0)
    if total <= 0:
        msg = "ceph df reported no raw cluster capacity"
        raise OSError(msg)
    return CephCapacitySnapshot(
        total_bytes=total,
        used_bytes=used,
        used_ratio=used / total,
    )


async def ceph_health(*, timeout: float) -> tuple[bool, str, str]:
    """Inspect Ceph health and recovery state.

    Returns
    -------
    tuple[bool, str, str]
        Clean flag, detail text, and raw health status.

    Raises
    ------
    OSError
        If the Ceph CLI output is malformed.
    """
    payload = _json_object(
        str(await ceph(["status", "--format", "json"], timeout=timeout)),
        context="ceph status",
    )
    health = payload.get("health")
    if not isinstance(health, dict):
        msg = "ceph status output did not include health"
        raise OSError(msg)
    status = str(health.get("status") or "HEALTH_UNKNOWN")
    detail = json.dumps(health.get("checks") or {}, sort_keys=True)
    raw_pgmap = payload.get("pgmap")
    pgmap = raw_pgmap if isinstance(raw_pgmap, dict) else {}
    states = str(pgmap.get("states") or "")
    blocked = ("recover", "degraded", "undersized", "peering", "backfill")
    clean = status == "HEALTH_OK" and not any(token in states for token in blocked)
    return clean, detail or states, status


async def ceph_osds(*, timeout: float) -> tuple[CephOSD, ...]:
    """Inspect live Ceph OSD inventory.

    Returns
    -------
    tuple[CephOSD, ...]
        Live OSD inventory.

    Raises
    ------
    OSError
        If the Ceph CLI output is malformed.
    """
    payload = _json_object(
        str(await ceph(["osd", "tree", "--format", "json"], timeout=timeout)),
        context="ceph osd tree",
    )
    nodes = payload.get("nodes")
    if not isinstance(nodes, list):
        msg = "ceph osd tree output did not include nodes"
        raise OSError(msg)
    hosts: dict[int, str] = {}
    for item in nodes:
        if not isinstance(item, dict) or item.get("type") != "host":
            continue
        children = item.get("children")
        if not isinstance(children, list):
            continue
        for child in children:
            if isinstance(child, int):
                hosts[child] = str(item.get("name") or "")
    osds: list[CephOSD] = []
    for item in nodes:
        if not isinstance(item, dict) or item.get("type") != "osd":
            continue
        osd_id = item.get("id")
        if not isinstance(osd_id, int) or osd_id < 0:
            continue
        osds.append(
            CephOSD(
                osd_id=osd_id,
                node_name=hosts.get(osd_id, ""),
                up=item.get("status") == "up",
                in_cluster=float(item.get("reweight") or 0.0) > 0,
            )
        )
    return tuple(sorted(osds, key=lambda osd: osd.osd_id))


async def _host_json(argv: list[str], *, timeout: float) -> dict[str, Any]:
    command = _host_argv(argv)
    payload = await _run_text(command, timeout=timeout)
    return _json_object(payload, context=" ".join(argv))


def _host_argv(argv: list[str]) -> list[str]:
    if (
        HOST_ROOT.exists()
        and shutil.which("nsenter")
        and Path("/proc/1/ns/mnt").exists()
    ):
        return ["nsenter", "--mount=/proc/1/ns/mnt", "--", *argv]
    if HOST_ROOT.exists() and (HOST_ROOT / "bin").exists():
        return ["chroot", HOST_ROOT.as_posix(), *argv]
    return argv


async def _host_text(argv: list[str], *, timeout: float) -> str:
    return await _run_text(_host_argv(argv), timeout=timeout)


def _local_path(path: Path) -> Path:
    if HOST_ROOT.exists():
        return HOST_ROOT / path.relative_to("/")
    return path


def _host_lock(timeout: float) -> HostLock:
    return HostLock(_local_path(OSD_LOCK_FILE), timeout=timeout)


def host_id_from_host_state() -> str:
    """Return the durable Bertrand host UUID from the mounted host state.

    Returns
    -------
    str
        Host UUID hex string.

    Raises
    ------
    OSError
        If the mounted host state is missing or malformed.
    """
    try:
        return uuid.UUID(
            _local_path(HOST_ID_FILE).read_text(encoding="utf-8").strip()
        ).hex
    except (OSError, ValueError) as err:
        msg = (
            f"failed to read Bertrand host identity at {HOST_ID_FILE}; run "
            "`bertrand init`"
        )
        raise OSError(msg) from err


def _ceil_mib(value: int) -> int:
    return max(1, math.ceil(value / 1024**2))


def _lvm_size(value: int) -> str:
    return f"{_ceil_mib(value)}M"


def kube_quantity(value: int) -> str:
    """Return a Kubernetes binary storage quantity for a byte request.

    Returns
    -------
    str
        Quantity string using mebibyte units.
    """
    return f"{_ceil_mib(value)}Mi"


def _parse_lvm_int(value: object) -> int:
    text = str(value or "0").strip()
    if not text:
        return 0
    return int(float(text))


def _safe_name(value: str) -> str:
    return re.sub(r"[^a-zA-Z0-9_.+-]+", "-", value).strip("-") or uuid.uuid4().hex


def _fallback_loop_active(path: Path = LOOP_FALLBACK_STORAGE_PATH) -> bool:
    root = _local_path(path)
    return any(root.glob("*.img")) if root.exists() else False


async def _lvm_inventory(*, timeout: float = 5.0) -> tuple[_LvmPV, ...]:
    """Return concrete PV inventory for the ``bertrand`` LVM VG.

    Returns
    -------
    tuple[_LvmPV, ...]
        Physical volumes in the Bertrand VG, sorted by stable UUID.
    """
    if not (shutil.which("pvs") or HOST_ROOT.exists()):
        return ()
    try:
        payload = await _host_json(
            [
                "pvs",
                "--reportformat",
                "json",
                "--units",
                "b",
                "--nosuffix",
                "--options",
                "pv_name,pv_uuid,vg_name,pv_size,pv_free",
            ],
            timeout=timeout,
        )
    except (OSError, TimeoutError):
        return ()
    reports = payload.get("report")
    rows: list[dict[str, object]] = []
    if isinstance(reports, list):
        for report in reports:
            if isinstance(report, dict) and isinstance(report.get("pv"), list):
                rows.extend(row for row in report["pv"] if isinstance(row, dict))
    pvs: list[_LvmPV] = []
    for row in rows:
        if str(row.get("vg_name") or "").strip() != BERTRAND_LVM_VG:
            continue
        pv_name = str(row.get("pv_name") or "").strip()
        pv_uuid = str(row.get("pv_uuid") or "").strip()
        pv_free = _parse_lvm_int(row.get("pv_free"))
        pv_size = _parse_lvm_int(row.get("pv_size"))
        if pv_name and pv_uuid:
            pvs.append(
                {
                    "pv_name": pv_name,
                    "pv_uuid": pv_uuid,
                    "size_bytes": pv_size,
                    "free_bytes": pv_free,
                }
            )
    return tuple(sorted(pvs, key=lambda item: (item["pv_uuid"], item["pv_name"])))


async def host_capacity_snapshot(
    path: Path = LOOP_FALLBACK_STORAGE_PATH,
    *,
    timeout: float,
) -> dict[str, object]:
    """Inspect host capacity from async controller code.

    Returns
    -------
    dict[str, object]
        Node-report status payload for loop fallback and Bertrand LVM capacity.
    """
    root = _local_path(path)
    root.mkdir(parents=True, exist_ok=True)
    stats = os.statvfs(root)
    lvm_pvs = await _lvm_inventory(timeout=timeout)
    return {
        "free_bytes": stats.f_bavail * stats.f_frsize,
        "path": path.as_posix(),
        "lvm_free_bytes": sum(pv["free_bytes"] for pv in lvm_pvs),
        "lvm_pvs": [pv["pv_name"] for pv in lvm_pvs],
        "lvm_pv_inventory": [
            {
                "pv_name": pv["pv_name"],
                "pv_uuid": pv["pv_uuid"],
                "pv_size_bytes": pv["size_bytes"],
                "pv_free_bytes": pv["free_bytes"],
            }
            for pv in lvm_pvs
        ],
        "loop_fallback_active": _fallback_loop_active(path),
        "heartbeat_at": datetime.now(UTC).isoformat(),
        "last_error": "",
    }


async def _lvs(*, timeout: float) -> list[dict[str, object]]:
    payload = await _host_json(
        [
            "lvs",
            "--segments",
            "--reportformat",
            "json",
            "--units",
            "b",
            "--nosuffix",
            "--options",
            "lv_name,vg_name,lv_path,lv_size,devices,seg_pe_ranges,lv_tags",
        ],
        timeout=timeout,
    )
    reports = payload.get("report")
    rows: list[dict[str, object]] = []
    if isinstance(reports, list):
        for report in reports:
            if isinstance(report, dict) and isinstance(report.get("lv"), list):
                rows.extend(row for row in report["lv"] if isinstance(row, dict))
    return rows


async def _find_lv(lv_name: str, *, timeout: float) -> dict[str, object] | None:
    matches = [
        row
        for row in await _lvs(timeout=timeout)
        if str(row.get("vg_name") or "").strip() == BERTRAND_LVM_VG
        and str(row.get("lv_name") or "").strip() == lv_name
    ]
    if not matches:
        return None
    merged = dict(matches[0])
    merged["devices"] = ",".join(str(row.get("devices") or "") for row in matches)
    merged["seg_pe_ranges"] = ",".join(
        str(row.get("seg_pe_ranges") or "") for row in matches
    )
    return merged


def _validate_lv_devices(row: dict[str, object], *, pv_name: str) -> None:
    devices = str(row.get("seg_pe_ranges") or row.get("devices") or "")
    if not devices:
        msg = "LVM did not report devices for Bertrand OSD LV"
        raise OSError(msg)
    mismatched = [
        chunk
        for chunk in re.split(r"[, ]+", devices)
        if chunk and not chunk.startswith(f"{pv_name}(")
    ]
    if mismatched:
        joined = ", ".join(mismatched)
        msg = (
            f"Bertrand OSD LV spans non-original PV extents: {joined}; "
            f"expected all extents on {pv_name!r}"
        )
        raise OSError(msg)


def _lv_tags(row: dict[str, object]) -> frozenset[str]:
    return frozenset(
        tag.strip() for tag in str(row.get("lv_tags") or "").split(",") if tag.strip()
    )


def _tag_value(tags: frozenset[str], prefix: str) -> str:
    prefix = prefix.rstrip("=") + "="
    for tag in tags:
        if tag.startswith(prefix):
            return tag.removeprefix(prefix).strip()
    return ""


def _first_lvm_pv(row: dict[str, object]) -> str:
    devices = str(row.get("seg_pe_ranges") or row.get("devices") or "")
    for chunk in re.split(r"[, ]+", devices):
        if not chunk:
            continue
        return chunk.split("(", 1)[0].strip()
    return ""


def _device_link(name: str) -> Path:
    return OSD_DEVICE_LINK_ROOT / f"{_safe_name(name)}.block"


def _ensure_link(path: Path, target: str) -> None:
    local = _local_path(path)
    local.parent.mkdir(parents=True, exist_ok=True)
    if local.is_symlink() or local.exists():
        local.unlink()
    local.symlink_to(target)


async def prepare_lvm_osd(
    *,
    name: str,
    target_bytes: int,
    pv_name: str,
    lv_name: str | None,
    timeout: float,
) -> PreparedOSD:
    """Expand or create a PV-pinned LVM block substrate for a Rook OSD.

    Returns
    -------
    PreparedOSD
        Prepared block-device substrate and observed host metadata.

    Raises
    ------
    OSError
        If LVM cannot allocate the LV entirely on the requested physical volume.
    ValueError
        If the requested size or physical volume is invalid.
    """
    if target_bytes <= 0:
        msg = "LVM OSD target size must be positive"
        raise ValueError(msg)
    pv_name = pv_name.strip()
    if not pv_name:
        msg = "LVM OSD allocation requires a concrete PV name"
        raise ValueError(msg)
    deadline = Deadline.from_timeout(timeout, message="timeout must be non-negative")
    async with _host_lock(deadline.remaining()):
        inventory = await _lvm_inventory(timeout=deadline.remaining())
        pv = next((item for item in inventory if item["pv_name"] == pv_name), None)
        if pv is None:
            msg = f"PV {pv_name!r} is not part of the {BERTRAND_LVM_VG!r} VG"
            raise OSError(msg)
        lv_name = (lv_name or f"bertrand-osd-{_safe_name(name)[-32:]}").strip()
        existing = await _find_lv(lv_name, timeout=deadline.remaining())
        if existing is None:
            if pv["free_bytes"] < target_bytes:
                msg = (
                    f"PV {pv_name!r} has only {pv['free_bytes']} bytes free; "
                    f"{target_bytes} bytes are required"
                )
                raise OSError(msg)
            await _host_text(
                [
                    "lvcreate",
                    "--yes",
                    "--wipesignatures",
                    "y",
                    "--addtag",
                    LVM_TAG,
                    "--addtag",
                    f"bertrand.osd={name}",
                    "--name",
                    lv_name,
                    "--size",
                    _lvm_size(target_bytes),
                    BERTRAND_LVM_VG,
                    pv_name,
                ],
                timeout=deadline.remaining(),
            )
        else:
            current = _parse_lvm_int(existing.get("lv_size"))
            if current < target_bytes:
                if pv["free_bytes"] < target_bytes - current:
                    msg = (
                        f"PV {pv_name!r} has only {pv['free_bytes']} bytes free; "
                        f"{target_bytes - current} bytes are required"
                    )
                    raise OSError(msg)
                lv_path = str(
                    existing.get("lv_path") or f"/dev/{BERTRAND_LVM_VG}/{lv_name}"
                )
                await _host_text(
                    [
                        "lvextend",
                        "--yes",
                        "--size",
                        _lvm_size(target_bytes),
                        lv_path,
                        pv_name,
                    ],
                    timeout=deadline.remaining(),
                )
        live = await _find_lv(lv_name, timeout=deadline.remaining())
        if live is None:
            msg = f"Bertrand OSD LV {lv_name!r} disappeared after allocation"
            raise OSError(msg)
        _validate_lv_devices(live, pv_name=pv_name)
        lv_path = str(live.get("lv_path") or f"/dev/{BERTRAND_LVM_VG}/{lv_name}")
        observed = _parse_lvm_int(live.get("lv_size"))
        block_path = _device_link(name)
        _ensure_link(block_path, lv_path)
        return PreparedOSD(
            block_path=block_path,
            observed_bytes=observed,
            pv_name=pv["pv_name"],
            pv_uuid=pv["pv_uuid"],
            pv_device=pv["pv_name"],
            lv_name=lv_name,
            lv_path=lv_path,
        )


async def discover_lvm_osds(*, timeout: float) -> tuple[tuple[str, PreparedOSD], ...]:
    """Discover Bertrand-tagged LVM OSD substrates on this host.

    Returns
    -------
    tuple[tuple[str, PreparedOSD], ...]
        Prepared LVM OSD substrates recovered from host LVM tags.
    """
    deadline = Deadline.from_timeout(timeout, message="timeout must be non-negative")
    async with _host_lock(deadline.remaining()):
        pvs = await _lvm_inventory(timeout=deadline.remaining())
        inventory = {item["pv_name"]: item for item in pvs}
        discoveries: list[tuple[str, PreparedOSD]] = []
        for row in await _lvs(timeout=deadline.remaining()):
            if str(row.get("vg_name") or "").strip() != BERTRAND_LVM_VG:
                continue
            tags = _lv_tags(row)
            if LVM_TAG not in tags:
                continue
            name = _tag_value(tags, "bertrand.osd")
            lv_name = str(row.get("lv_name") or "").strip()
            if not name or not lv_name:
                continue
            pv_name = _first_lvm_pv(row)
            pv = inventory.get(pv_name)
            if pv is None:
                continue
            live = await _find_lv(lv_name, timeout=deadline.remaining())
            if live is None:
                continue
            _validate_lv_devices(live, pv_name=pv_name)
            lv_path = str(live.get("lv_path") or f"/dev/{BERTRAND_LVM_VG}/{lv_name}")
            block_path = _device_link(name)
            _ensure_link(block_path, lv_path)
            discoveries.append(
                (
                    name,
                    PreparedOSD(
                        block_path=block_path,
                        observed_bytes=_parse_lvm_int(live.get("lv_size")),
                        pv_name=pv["pv_name"],
                        pv_uuid=pv["pv_uuid"],
                        pv_device=pv["pv_name"],
                        lv_name=lv_name,
                        lv_path=lv_path,
                    ),
                )
            )
        return tuple(sorted(discoveries, key=lambda item: item[0]))


async def _loop_devices(*, timeout: float) -> list[dict[str, object]]:
    try:
        payload = await _host_json(
            ["losetup", "--json", "--list", "--output", "NAME,BACK-FILE"],
            timeout=timeout,
        )
    except OSError:
        return []
    devices = payload.get("loopdevices")
    if not isinstance(devices, list):
        return []
    return [item for item in devices if isinstance(item, dict)]


async def _loop_device_for_file(path: Path, *, timeout: float) -> str | None:
    for item in await _loop_devices(timeout=timeout):
        if str(item.get("back-file") or "") == path.as_posix():
            name = str(item.get("name") or "").strip()
            if name:
                return name
    return None


async def prepare_loop_fallback_osd(
    *,
    name: str,
    target_bytes: int,
    timeout: float,
) -> PreparedOSD:
    """Expand or create the node's single loop-backed fallback block substrate.

    Returns
    -------
    PreparedOSD
        Prepared loop-backed block-device substrate and observed host metadata.

    Raises
    ------
    OSError
        If the loop device cannot be created or refreshed.
    ValueError
        If the requested size is invalid.
    """
    if target_bytes <= 0:
        msg = "loop fallback OSD target size must be positive"
        raise ValueError(msg)
    deadline = Deadline.from_timeout(timeout, message="timeout must be non-negative")
    async with _host_lock(deadline.remaining()):
        file_path = LOOP_FALLBACK_STORAGE_PATH / f"{_safe_name(name)}.img"
        local_file = _local_path(file_path)
        local_file.parent.mkdir(parents=True, exist_ok=True)
        with local_file.open("ab") as handle:
            handle.truncate(target_bytes)
        device = await _loop_device_for_file(file_path, timeout=deadline.remaining())
        if device is None:
            device = (
                await _host_text(
                    ["losetup", "--find", "--show", file_path.as_posix()],
                    timeout=deadline.remaining(),
                )
            ).strip()
        else:
            await _host_text(
                ["losetup", "--set-capacity", device],
                timeout=deadline.remaining(),
            )
        if not device:
            msg = f"failed to create loop device for {file_path}"
            raise OSError(msg)
        block_path = _device_link(name)
        _ensure_link(block_path, device)
        return PreparedOSD(
            block_path=block_path,
            observed_bytes=target_bytes,
            loop_file=file_path,
            loop_device=device,
        )


async def discover_loop_fallback_osd(
    *,
    name: str,
    timeout: float,
) -> PreparedOSD | None:
    """Discover and reattach this host's deterministic loop fallback OSD.

    Returns
    -------
    PreparedOSD | None
        Prepared loop fallback substrate, or None when no backing file exists.
    """
    file_path = LOOP_FALLBACK_STORAGE_PATH / f"{_safe_name(name)}.img"
    local_file = _local_path(file_path)
    if not local_file.exists():
        return None
    target_bytes = local_file.stat().st_size
    if target_bytes <= 0:
        return None
    return await prepare_loop_fallback_osd(
        name=name,
        target_bytes=target_bytes,
        timeout=timeout,
    )


async def drain_loop_osd(osd_id: int, *, timeout: float) -> None:
    """Mark a loop-backed fallback OSD out and wait until Ceph says it is safe."""
    await drain_ceph_osd(osd_id, timeout=timeout)


async def drain_ceph_osd(osd_id: int, *, timeout: float) -> None:
    """Mark an OSD out and wait until Ceph says it is safe to destroy."""
    deadline = Deadline.from_timeout(timeout, message="timeout must be non-negative")
    await ceph(["osd", "out", str(osd_id)], timeout=deadline.remaining())

    async def safe_to_destroy(remaining: float) -> None:
        result = await ceph(
            ["osd", "safe-to-destroy", str(osd_id)],
            timeout=remaining,
            check=False,
            capture_output=True,
        )
        if result.returncode != 0:
            detail = result.stderr or result.stdout or "not safe to destroy yet"
            msg = f"osd.{osd_id} is not safe to destroy: {detail}".strip()
            raise TimeoutError(msg)

    await until(
        safe_to_destroy,
        timeout=deadline.remaining(),
        interval=5.0,
        action=f"waiting for osd.{osd_id} to become safe to destroy",
    )


async def purge_loop_osd(osd_id: int, *, timeout: float) -> None:
    """Purge a loop-backed fallback OSD after Rook has stopped owning it."""
    await purge_ceph_osd(osd_id, timeout=timeout)


async def purge_ceph_osd(osd_id: int, *, timeout: float) -> None:
    """Purge an OSD after Rook has stopped owning it."""
    await ceph(
        ["osd", "purge", str(osd_id), "--yes-i-really-mean-it"],
        timeout=timeout,
    )


async def delete_lvm_osd_substrate(
    *,
    lv_name: str,
    block_path: str,
    timeout: float,
) -> None:
    """Remove a retired Bertrand-tagged LVM OSD substrate.

    Raises
    ------
    ValueError
        If the LV name is empty.
    OSError
        If the target LV exists but is not Bertrand-managed.
    """
    lv_name = lv_name.strip()
    if not lv_name:
        msg = "LVM OSD deletion requires a non-empty LV name"
        raise ValueError(msg)
    deadline = Deadline.from_timeout(timeout, message="timeout must be non-negative")
    async with _host_lock(deadline.remaining()):
        row = await _find_lv(lv_name, timeout=deadline.remaining())
        if row is not None:
            tags = _lv_tags(row)
            if LVM_TAG not in tags:
                msg = f"refusing to delete unmanaged LV {lv_name!r}"
                raise OSError(msg)
            lv_path = str(row.get("lv_path") or f"/dev/{BERTRAND_LVM_VG}/{lv_name}")
            await _host_text(
                ["lvremove", "--yes", lv_path],
                timeout=deadline.remaining(),
            )
        if block_path.strip():
            with contextlib.suppress(OSError):
                _local_path(Path(block_path)).unlink()


async def delete_loop_fallback_substrate(
    *,
    loop_file: str,
    loop_device: str,
    block_path: str,
    timeout: float,
) -> None:
    """Detach and remove a retired loop fallback substrate."""
    deadline = Deadline.from_timeout(timeout, message="timeout must be non-negative")
    async with _host_lock(deadline.remaining()):
        device = loop_device.strip()
        file_path = Path(loop_file)
        if file_path.as_posix():
            device = device or (
                await _loop_device_for_file(file_path, timeout=deadline.remaining())
                or ""
            )
        if device:
            await _host_text(
                ["losetup", "--detach", device],
                timeout=deadline.remaining(),
            )
        if block_path.strip():
            with contextlib.suppress(OSError):
                _local_path(Path(block_path)).unlink()
        if file_path.as_posix():
            with contextlib.suppress(OSError):
                _local_path(file_path).unlink()


async def bind_block_device(
    *,
    block_path: str,
    target_path: str,
    timeout: float,
) -> None:
    """Bind-mount a host block device path to a kubelet CSI target path.

    Raises
    ------
    ValueError
        If either path is empty.
    OSError
        If the target is already mounted from a different source.
    """
    block = block_path.strip()
    target = target_path.strip()
    if not block or not target:
        msg = "block device bind mount requires non-empty source and target paths"
        raise ValueError(msg)
    deadline = Deadline.from_timeout(timeout, message="timeout must be non-negative")
    async with _host_lock(deadline.remaining()):
        local_target = _local_path(Path(target))
        local_target.parent.mkdir(parents=True, exist_ok=True)
        local_target.touch(exist_ok=True)
        try:
            mounted_source = (
                await _host_text(
                    [
                        "findmnt",
                        "--noheadings",
                        "--output",
                        "SOURCE",
                        "--target",
                        target,
                    ],
                    timeout=deadline.remaining(),
                )
            ).strip()
        except (OSError, TimeoutError):
            mounted_source = ""
        if mounted_source:
            expected = (
                await _host_text(
                    ["readlink", "-f", block],
                    timeout=deadline.remaining(),
                )
            ).strip()
            observed = (
                await _host_text(
                    ["readlink", "-f", mounted_source],
                    timeout=deadline.remaining(),
                )
            ).strip()
            if observed == expected:
                return
            msg = (
                f"CSI target {target!r} is already mounted from {mounted_source!r}; "
                f"expected {block!r}"
            )
            raise OSError(msg)
        await _host_text(
            ["mount", "--bind", block, target],
            timeout=deadline.remaining(),
        )


async def unbind_block_device(*, target_path: str, timeout: float) -> None:
    """Unmount and remove a kubelet CSI raw block target path if present.

    Raises
    ------
    ValueError
        If the target path is empty.
    """
    target = target_path.strip()
    if not target:
        msg = "block device unmount requires a non-empty target path"
        raise ValueError(msg)
    deadline = Deadline.from_timeout(timeout, message="timeout must be non-negative")
    async with _host_lock(deadline.remaining()):
        with contextlib.suppress(OSError, TimeoutError):
            await _host_text(["umount", target], timeout=deadline.remaining())
        with contextlib.suppress(OSError):
            _local_path(Path(target)).unlink()
