"""External CLI endpoints for local Bertrand node operations."""

from __future__ import annotations

import asyncio
import json
import platform
from typing import TYPE_CHECKING, cast

from bertrand.env.git import INFINITY, confirm
from bertrand.env.host import HOST_ID_FILE
from bertrand.env.kube.api.bootstrap import microk8s_cluster_ready
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.capability.device import list_device_inventory
from bertrand.env.kube.ceph.api import BlockOSDSpec, validate_block_osd_devices
from bertrand.env.kube.ceph.bootstrap import microceph_cluster_ready
from bertrand.env.kube.ceph.capacity import (
    CephStoragePlanner,
    create_manual_block_osd_action,
    list_storage_actions,
    list_storage_node_reports,
    read_storage_policy,
)
from bertrand.env.kube.node import Node

if TYPE_CHECKING:
    import argparse
    from collections.abc import Awaitable, Callable


async def bertrand_node(args: argparse.Namespace) -> None:
    """Execute a ``bertrand node`` subcommand.

    Parameters
    ----------
    args : argparse.Namespace
        Parsed external CLI arguments.

    Raises
    ------
    ValueError
        If the parsed node subcommand is unsupported.
    """
    command = args.node_command
    if command == "status":
        await bertrand_node_status(json_output=args.json)
        return
    if command == "storage":
        await _bertrand_node_storage(args)
        return
    msg = f"unsupported node command: {command!r}"
    raise ValueError(msg)


async def bertrand_node_status(*, json_output: bool) -> None:
    """Print local Bertrand node status.

    Parameters
    ----------
    json_output : bool
        Whether to emit machine-readable JSON.
    """
    payload = await _node_status_payload()
    if json_output:
        print(json.dumps(payload, indent=2, sort_keys=True))
        return
    print("node:")
    print(f"  host id: {payload['host_id'] or 'unconfigured'}")
    print(f"  host name: {payload['host_name']}")
    kubernetes = payload["kubernetes"]
    if isinstance(kubernetes, dict):
        node_status = cast("dict[str, object]", kubernetes)
        print(f"  kubernetes node: {node_status.get('name') or 'unknown'}")
        print(f"  ready: {node_status.get('ready')}")
        print(f"  build eligible: {node_status.get('build_eligible')}")
        print(f"  platform: {node_status.get('platform') or 'unknown'}")
    print(f"  microk8s: {'ready' if payload['microk8s_ready'] else 'not ready'}")
    print(f"  microceph: {'ready' if payload['microceph_ready'] else 'not ready'}")
    print(f"  storage report: {payload['storage_report'] or 'unavailable'}")
    devices = payload["devices"]
    if isinstance(devices, list):
        print(f"  DRA devices: {len(devices)}")


async def _bertrand_node_storage(args: argparse.Namespace) -> None:
    command = args.storage_command
    if command == "status":
        await bertrand_node_storage_status(json_output=args.json)
        return
    if command == "actions":
        await bertrand_node_storage_actions(json_output=args.json)
        return
    if command == "osd":
        await _bertrand_node_storage_osd(args)
        return
    msg = f"unsupported node storage command: {command!r}"
    raise ValueError(msg)


async def bertrand_node_storage_status(*, json_output: bool) -> None:
    """Print Ceph storage autoscaler status.

    Parameters
    ----------
    json_output : bool
        Whether to emit machine-readable JSON.
    """
    with await Kube.host(timeout=INFINITY) as kube:
        policy = await read_storage_policy(kube, timeout=INFINITY)
        actions = await list_storage_actions(kube, timeout=INFINITY)
        reports = await list_storage_node_reports(kube, timeout=INFINITY)
    planner = CephStoragePlanner()
    payload = {
        "policy": policy.spec.model_dump(mode="json"),
        "status": policy.status.model_dump(mode="json") if policy.status else None,
        "action_counts": planner.action_counts(actions),
        "reports": [report.model_dump(mode="json") for report in reports],
    }
    if json_output:
        print(json.dumps(payload, indent=2, sort_keys=True))
        return
    print("storage:")
    status = payload["status"]
    if isinstance(status, dict):
        print(f"  used: {status.get('used_ratio')}")
        print(f"  elastic loop OSDs: {status.get('loop_osds')}")
        print(f"  manual block OSDs: {status.get('block_osds')}")
        print(f"  elastic bytes: {status.get('elastic_bytes')}")
        print(f"  durable bytes: {status.get('durable_bytes')}")
        print(f"  block preferred: {status.get('block_preferred')}")
        print(f"  managed OSDs: {status.get('managed_osds')}")
        print(f"  shrink candidates: {status.get('shrink_candidates')}")
        if status.get("last_error"):
            print(f"  last error: {status['last_error']}")
    print("  actions:")
    for phase, count in payload["action_counts"].items():
        print(f"    {phase}: {count}")
    print(f"  node reports: {len(reports)}")


async def bertrand_node_storage_actions(*, json_output: bool) -> None:
    """Print Ceph storage action records.

    Parameters
    ----------
    json_output : bool
        Whether to emit machine-readable JSON.
    """
    with await Kube.host(timeout=INFINITY) as kube:
        actions = await list_storage_actions(kube, timeout=INFINITY)
    payload = [action.model_dump(mode="json") for action in actions]
    if json_output:
        print(json.dumps(payload, indent=2, sort_keys=True))
        return
    if not actions:
        print("no storage actions")
        return
    for action in actions:
        print(f"{action.metadata.name}: {action.spec.operation} {action.status.phase}")
        if action.status.osd_origin or action.status.osd_quality:
            print(
                "  storage: "
                f"{action.status.osd_origin or 'unknown'} / "
                f"{action.status.osd_quality or 'unknown'}"
            )
        if action.status.created_osd_ids:
            print(f"  created OSDs: {list(action.status.created_osd_ids)}")
        if action.status.source_devices:
            print(f"  source devices: {', '.join(action.status.source_devices)}")
        if action.status.message:
            print(f"  {action.status.message}")


async def _bertrand_node_storage_osd(args: argparse.Namespace) -> None:
    command = args.osd_command
    if command == "add-block":
        await bertrand_node_storage_osd_add_block(
            device=args.device,
            wal_device=args.wal,
            db_device=args.db,
            encrypt=args.encrypt,
            wipe=args.wipe,
            assume_yes=args.yes,
            timeout=args.timeout,
        )
        return
    msg = f"unsupported node storage osd command: {command!r}"
    raise ValueError(msg)


async def bertrand_node_storage_osd_add_block(
    *,
    device: str,
    wal_device: str | None,
    db_device: str | None,
    encrypt: bool,
    wipe: bool,
    assume_yes: bool,
    timeout: float,
) -> None:
    """Create a manual block-backed OSD action for the local node.

    Parameters
    ----------
    device : str
        Absolute host block device path to add as the data device.
    wal_device : str | None
        Optional absolute WAL device path.
    db_device : str | None
        Optional absolute DB device path.
    encrypt : bool
        Whether MicroCeph should encrypt the data device.
    wipe : bool
        Whether MicroCeph should wipe the data device.
    assume_yes : bool
        Whether to bypass the destructive wipe confirmation.
    timeout : float
        Maximum action creation budget in seconds.

    Raises
    ------
    PermissionError
        If `wipe` is requested and the user declines confirmation.
    TimeoutError
        If `timeout` is non-positive.
    """
    if timeout <= 0:
        msg = "node storage OSD timeout must be non-negative"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    spec = BlockOSDSpec(
        device=device,
        wal_device=wal_device,
        db_device=db_device,
        encrypt=encrypt,
        wipe=wipe,
    )
    inspections = await validate_block_osd_devices(
        spec,
        timeout=deadline - loop.time(),
    )
    if wipe and not confirm(
        f"MicroCeph will wipe {spec.device!r} before adding it as an OSD. "
        "Continue? [y/N] ",
        assume_yes=assume_yes,
    ):
        msg = "block OSD wipe declined by user"
        raise PermissionError(msg)
    with await Kube.host(timeout=deadline - loop.time()) as kube:
        node = await Node.local(kube, timeout=deadline - loop.time())
        action_name = await create_manual_block_osd_action(
            kube,
            node_name=node.name,
            device=spec.device,
            wal_device=spec.wal_device,
            db_device=spec.db_device,
            encrypt=encrypt,
            wipe=wipe,
            timeout=deadline - loop.time(),
        )
    print(f"created storage action: {action_name}")
    print(
        "validated block devices: "
        + ", ".join(
            f"{report.path} ({report.size_bytes} bytes)" for report in inspections
        )
    )


async def _node_status_payload() -> dict[str, object]:
    host_id = ""
    if HOST_ID_FILE.is_file():
        host_id = HOST_ID_FILE.read_text(encoding="utf-8").strip()
    payload: dict[str, object] = {
        "host_id": host_id,
        "host_name": platform.node(),
        "microk8s_ready": await _safe_ready(microk8s_cluster_ready),
        "microceph_ready": await _safe_ready(microceph_cluster_ready),
        "kubernetes": {},
        "storage_report": "",
        "devices": [],
    }
    try:
        with await Kube.host(timeout=INFINITY) as kube:
            node = await Node.local(kube, timeout=INFINITY)
            payload["kubernetes"] = {
                "name": node.name,
                "ready": node.is_ready,
                "schedulable": node.is_schedulable,
                "build_eligible": node.is_build_eligible,
                "platform": node.platform,
                "roles": sorted(node.roles),
                "internal_ips": list(node.internal_ips),
                "external_ips": list(node.external_ips),
            }
            reports = await list_storage_node_reports(kube, timeout=INFINITY)
            report = next(
                (item for item in reports if item.spec.node_name == node.name),
                None,
            )
            if report is not None and report.status is not None:
                payload["storage_report"] = report.status.model_dump(mode="json")
            devices = await list_device_inventory(
                kube,
                node_names=(node.name,),
                timeout=INFINITY,
            )
            payload["devices"] = [
                {
                    "name": device.name,
                    "capability_id": device.capability_id,
                    "node_name": device.node_name,
                    "device_name": device.spec.device_name,
                    "cdi_selector": device.cdi_selector,
                    "attributes": dict(device.spec.attributes),
                }
                for device in devices
            ]
    except (OSError, TimeoutError, RuntimeError, ValueError) as err:
        payload["kubernetes_error"] = str(err)
    return payload


async def _safe_ready(fn: Callable[..., Awaitable[bool]]) -> bool:
    try:
        return bool(await fn(timeout=INFINITY))
    except (OSError, TimeoutError, RuntimeError, ValueError):
        return False
