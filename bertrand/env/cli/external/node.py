"""External CLI endpoints for local Bertrand node operations."""

from __future__ import annotations

from typing import TYPE_CHECKING, cast

from bertrand.env.cli.external._device import device_line, device_payload
from bertrand.env.cli.external._runtime import emit_json
from bertrand.env.cli.external._storage import (
    print_storage_csi_line,
    print_storage_status_fields,
    storage_cli_snapshot,
    storage_csi_ready,
    storage_osd_line,
)
from bertrand.env.cli.external.secret import (
    add_capability,
    list_capabilities,
    local_node_capability_ref,
    local_node_scope_targets,
    remove_capability,
)
from bertrand.env.git import INFINITY
from bertrand.env.kube.api.bootstrap import microk8s_cluster_ready
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.capability.device import (
    delete_device_inventory,
    list_device_inventory,
    refresh_node_resource_slice,
    upsert_device_inventory,
)
from bertrand.env.kube.ceph.bootstrap import rook_ceph_ready
from bertrand.env.kube.ceph.capacity import list_storage_node_reports
from bertrand.env.kube.node import Node
from bertrand.env.kube.node_identity import (
    BertrandNodeRecord,
    current_host_id,
    ensure_local_bertrand_node,
)

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
    if command == "name":
        await _bertrand_node_name(args)
        return
    if command == "secret":
        await _bertrand_node_secret(args)
        return
    if command == "device":
        await _bertrand_node_device(args)
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
        emit_json(payload)
        return
    print("node:")
    print(f"  host id: {payload['host_id'] or 'unconfigured'}")
    print(f"  display name: {payload['display_name'] or '(none)'}")
    print(f"  phase: {payload['phase'] or 'unknown'}")
    kubernetes = payload["kubernetes"]
    if isinstance(kubernetes, dict):
        node_status = cast("dict[str, object]", kubernetes)
        print(f"  kubernetes node: {node_status.get('name') or 'unknown'}")
        print(f"  ready: {node_status.get('ready')}")
        print(f"  build eligible: {node_status.get('build_eligible')}")
        print(f"  platform: {node_status.get('platform') or 'unknown'}")
    print(f"  microk8s: {'ready' if payload['microk8s_ready'] else 'not ready'}")
    print(f"  rook ceph: {'ready' if payload['rook_ceph_ready'] else 'not ready'}")
    print(f"  storage report: {payload['storage_report'] or 'unavailable'}")
    devices = payload["devices"]
    if isinstance(devices, list):
        print(f"  DRA devices: {len(devices)}")


async def _bertrand_node_storage(args: argparse.Namespace) -> None:
    command = args.storage_command
    if command == "status":
        await bertrand_node_storage_status(json_output=args.json)
        return
    if command == "doctor":
        await bertrand_node_storage_doctor(json_output=args.json)
        return
    msg = f"unsupported node storage command: {command!r}"
    raise ValueError(msg)


async def _bertrand_node_name(args: argparse.Namespace) -> None:
    command = args.node_name_command
    if command == "set":
        await bertrand_node_name_set(display_name=args.name, timeout=args.timeout)
        return
    if command == "clear":
        await bertrand_node_name_set(display_name="", timeout=args.timeout)
        return
    msg = f"unsupported node name command: {command!r}"
    raise ValueError(msg)


async def bertrand_node_name_set(*, display_name: str, timeout: float) -> None:
    """Set or clear the local Bertrand node display name.

    Parameters
    ----------
    display_name : str
        Human-readable display name. An empty string clears the name.
    timeout : float
        Maximum Kubernetes convergence budget in seconds.
    """
    with await Kube.host(timeout=timeout) as kube:
        record = await ensure_local_bertrand_node(
            kube,
            display_name=display_name,
            timeout=timeout,
        )
    shown = record.display_name or "(none)"
    print(f"node display name: {shown}")


async def _bertrand_node_secret(args: argparse.Namespace) -> None:
    command = args.node_secret_command
    if command == "add":
        ref = await local_node_capability_ref(
            kind=args.kind,
            capability_id=args.id,
            timeout=args.timeout,
        )
        await add_capability(ref, source=args.source, timeout=args.timeout)
        return
    if command == "rm":
        ref = await local_node_capability_ref(
            kind=args.kind,
            capability_id=args.id,
            timeout=args.timeout,
        )
        await remove_capability(ref, timeout=args.timeout)
        return
    if command == "list":
        await list_capabilities(
            await local_node_scope_targets(timeout=args.timeout),
            kind=args.kind,
            json_output=args.json,
            timeout=args.timeout,
        )
        return
    msg = f"unsupported node secret command: {command!r}"
    raise ValueError(msg)


async def _bertrand_node_device(args: argparse.Namespace) -> None:
    command = args.node_device_command
    if command == "list":
        await bertrand_node_device_list(json_output=args.json, timeout=args.timeout)
        return
    if command == "add":
        await bertrand_node_device_add(
            capability_id=args.capability,
            device_name=args.name,
            cdi_selector=args.cdi,
            attributes=_parse_attrs(args.attr),
            timeout=args.timeout,
        )
        return
    if command == "rm":
        await bertrand_node_device_rm(
            capability_id=args.capability,
            device_name=args.name,
            timeout=args.timeout,
        )
        return
    msg = f"unsupported node device command: {command!r}"
    raise ValueError(msg)


async def bertrand_node_device_list(*, json_output: bool, timeout: float) -> None:
    """Print managed DRA inventory for the local Kubernetes node.

    Parameters
    ----------
    json_output : bool
        Whether to emit machine-readable JSON.
    timeout : float
        Maximum Kubernetes request budget in seconds.
    """
    with await Kube.host(timeout=timeout) as kube:
        node = await ensure_local_bertrand_node(kube, timeout=timeout)
        records = await list_device_inventory(
            kube,
            host_ids=(node.host_id,),
            timeout=timeout,
        )
    payload = [device_payload(record) for record in records]
    if json_output:
        emit_json(payload)
        return
    if not records:
        print("no DRA devices")
        return
    for record in records:
        print(device_line(record))


async def bertrand_node_device_add(
    *,
    capability_id: str,
    device_name: str,
    cdi_selector: str,
    attributes: dict[str, str],
    timeout: float,
) -> None:
    """Create or update one local managed DRA inventory record.

    Parameters
    ----------
    capability_id : str
        Host-agnostic device capability ID.
    device_name : str
        Node-local device inventory name.
    cdi_selector : str
        CDI selector exposed when Kubernetes allocates the device.
    attributes : dict[str, str]
        Additional string attributes published on the ResourceSlice.
    timeout : float
        Maximum Kubernetes request budget in seconds.
    """
    with await Kube.host(timeout=timeout) as kube:
        node = await ensure_local_bertrand_node(kube, timeout=timeout)
        record = await upsert_device_inventory(
            kube,
            capability_id=capability_id,
            host_id=node.host_id,
            node_name=node.node_name,
            device_name=device_name,
            cdi_selector=cdi_selector,
            attributes=attributes,
            timeout=timeout,
        )
        await refresh_node_resource_slice(
            kube,
            node_name=node.node_name,
            timeout=timeout,
        )
    print(device_line(record))


async def bertrand_node_device_rm(
    *,
    capability_id: str,
    device_name: str,
    timeout: float,
) -> None:
    """Delete one local managed DRA inventory record.

    Parameters
    ----------
    capability_id : str
        Host-agnostic device capability ID.
    device_name : str
        Node-local device inventory name.
    timeout : float
        Maximum Kubernetes request budget in seconds.
    """
    with await Kube.host(timeout=timeout) as kube:
        node = await ensure_local_bertrand_node(kube, timeout=timeout)
        deleted = await delete_device_inventory(
            kube,
            capability_id=capability_id,
            host_id=node.host_id,
            node_name=node.node_name,
            device_name=device_name,
            timeout=timeout,
        )
        await refresh_node_resource_slice(
            kube,
            node_name=node.node_name,
            timeout=timeout,
        )
    state = "deleted" if deleted else "not found"
    print(f"{capability_id} {device_name}: {state}")


async def bertrand_node_storage_status(*, json_output: bool) -> None:
    """Print Ceph storage autoscaler status.

    Parameters
    ----------
    json_output : bool
        Whether to emit machine-readable JSON.
    """
    with await Kube.host(timeout=INFINITY) as kube:
        node = await ensure_local_bertrand_node(kube, timeout=INFINITY)
        snapshot = await storage_cli_snapshot(kube, host_id=node.host_id)
    payload = snapshot.status_payload()
    if json_output:
        emit_json(payload)
        return
    print("storage:")
    print_storage_status_fields(payload["status"], local=True)
    print("  actions:")
    action_counts = cast("dict[str, object]", payload["action_counts"])
    for phase, count in action_counts.items():
        print(f"    {phase}: {count}")
    print(f"  active reservations: {len(snapshot.active_reservations())}")
    print(f"  node reports for this host: {len(snapshot.reports)}")
    print(f"  managed OSD records: {len(snapshot.osds)}")
    print_storage_csi_line(payload["csi"])
    for osd in snapshot.osds:
        if osd.spec.node_name:
            print(storage_osd_line(osd, include_ceph_id=False))


async def bertrand_node_storage_doctor(*, json_output: bool) -> None:
    """Print local Ceph storage diagnostics and action records.

    Parameters
    ----------
    json_output : bool
        Whether to emit machine-readable JSON.
    """
    with await Kube.host(timeout=INFINITY) as kube:
        node = await ensure_local_bertrand_node(kube, timeout=INFINITY)
        snapshot = await storage_cli_snapshot(kube, host_id=node.host_id)
    if json_output:
        emit_json(snapshot.doctor_payload())
        return
    actions = snapshot.actions
    reports = snapshot.reports
    osds = snapshot.osds
    csi_ready = storage_csi_ready(snapshot.csi)
    print("storage doctor:")
    if not actions:
        print("  no tracked storage actions for this host")
    if not csi_ready:
        print("  Bertrand OSD CSI is not fully ready")
    local_reports = [report for report in reports if report.status is not None]
    if not any(report.status and report.status.lvm_pvs for report in local_reports):
        print("  no reported local 'bertrand' LVM PVs; loop fallback may be used")
    for osd in osds:
        if osd.status.phase in {"HostPrepared", "Binding", "Expanding", "Shrinking"}:
            changed_at = (
                osd.status.phase_changed_at.isoformat()
                if osd.status.phase_changed_at is not None
                else "unknown"
            )
            print(
                f"  OSD {osd.metadata.name} is {osd.status.phase}; "
                f"waiting for Rook/CSI reconciliation since {changed_at}"
            )
        if osd.status.last_error:
            print(f"  OSD {osd.metadata.name} error: {osd.status.last_error}")
    active_loop = any(
        osd.spec.origin == "loop-fallback"
        and osd.status.phase not in {"Retired", "Failed"}
        for osd in osds
    )
    lvm_available = any(
        report.status is not None and report.status.lvm_free_bytes > 0
        for report in reports
    )
    if active_loop and lvm_available:
        print(
            "  loop fallback is active and LVM space is available; migration will "
            "proceed once Ceph is healthy"
        )
    policy = snapshot.policy
    if policy.status is not None:
        if policy.status.missing_lvm_osd_pvs > 0:
            print(
                "  usable LVM PVs are missing managed OSD coverage; Bertrand "
                "will create minimum-size PV-pinned OSDs before shrinking"
            )
        if policy.status.lvm_shrink_candidate:
            print(
                "  LVM shrink candidate selected: "
                f"{policy.status.lvm_shrink_candidate} -> "
                f"{policy.status.lvm_shrink_target_bytes} bytes"
            )
    for reservation in snapshot.reservations:
        if reservation.status.phase == "Pending":
            print(
                f"  reservation {reservation.metadata.name} is pending: "
                f"{reservation.status.last_error or 'waiting for headroom'}"
            )
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
        if action.status.source_pv:
            print(f"  source PV: {action.status.source_pv}")
        if action.status.source_lv:
            print(f"  source LV: {action.status.source_lv}")
        if action.status.provisioned_bytes is not None:
            print(f"  provisioned bytes: {action.status.provisioned_bytes}")
        if action.status.message:
            print(f"  {action.status.message}")
    print(
        "  tip: create a host LVM volume group named 'bertrand' and add PVs "
        "to give Bertrand preferred storage capacity."
    )


def _parse_attrs(values: list[str]) -> dict[str, str]:
    attributes: dict[str, str] = {}
    for raw in values:
        if "=" not in raw:
            msg = f"device attribute must use KEY=VALUE syntax: {raw!r}"
            raise ValueError(msg)
        key, value = raw.split("=", 1)
        key = key.strip()
        if not key:
            msg = f"device attribute key cannot be empty: {raw!r}"
            raise ValueError(msg)
        attributes[key] = value
    return dict(sorted(attributes.items()))


async def _node_status_payload() -> dict[str, object]:
    try:
        host_id = current_host_id()
    except OSError:
        host_id = ""
    payload: dict[str, object] = {
        "host_id": host_id,
        "display_name": "",
        "phase": "",
        "microk8s_ready": await _safe_ready(microk8s_cluster_ready),
        "rook_ceph_ready": False,
        "kubernetes": {},
        "storage_report": "",
        "devices": [],
    }
    try:
        with await Kube.host(timeout=INFINITY) as kube:
            payload["rook_ceph_ready"] = await rook_ceph_ready(kube, timeout=INFINITY)
            bertrand_node: BertrandNodeRecord | None = None
            if host_id:
                bertrand_node = await ensure_local_bertrand_node(
                    kube,
                    host_id=host_id,
                    timeout=INFINITY,
                )
                node = await Node.get(
                    kube,
                    name=bertrand_node.node_name,
                    timeout=INFINITY,
                )
                if node is None:
                    node = await Node.local(kube, timeout=INFINITY)
            else:
                node = await Node.local(kube, timeout=INFINITY)
            if bertrand_node is not None:
                payload["display_name"] = bertrand_node.display_name
                payload["phase"] = bertrand_node.phase
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
                (item for item in reports if item.spec.host_id == host_id), None
            )
            if report is not None and report.status is not None:
                payload["storage_report"] = report.status.model_dump(mode="json")
            devices = await list_device_inventory(
                kube,
                host_ids=(host_id,) if host_id else None,
                node_names=(node.name,),
                timeout=INFINITY,
            )
            payload["devices"] = [device_payload(device) for device in devices]
    except (OSError, TimeoutError, RuntimeError, ValueError) as err:
        payload["kubernetes_error"] = str(err)
    return payload


async def _safe_ready(fn: Callable[..., Awaitable[bool]]) -> bool:
    try:
        return bool(await fn(timeout=INFINITY))
    except (OSError, TimeoutError, RuntimeError, ValueError):
        return False
