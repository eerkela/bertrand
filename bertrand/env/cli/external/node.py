"""External CLI endpoints for local Bertrand node operations."""

from __future__ import annotations

from typing import TYPE_CHECKING, cast

from bertrand.env.cli.external._storage import (
    node_report_status_payload,
    print_node_storage_doctor,
    print_node_storage_status,
    storage_cli_snapshot,
)
from bertrand.env.cli.util import emit_json
from bertrand.env.git import STATE
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.capability.device import (
    delete_device_inventory,
    list_device_inventory,
    refresh_node_resource_slice,
    upsert_device_inventory,
)
from bertrand.env.kube.ceph.bootstrap import rook_ceph_ready
from bertrand.env.kube.ceph.capacity import read_storage_state
from bertrand.env.kube.node import Node
from bertrand.env.kube.node_identity import (
    BertrandNodeRecord,
    ensure_local_bertrand_node,
)

if TYPE_CHECKING:
    from bertrand.env.git import Deadline
    from bertrand.env.kube.capability.device import BertrandDeviceRecord


async def bertrand_node_status(*, json_output: bool, deadline: Deadline) -> None:
    """Print local Bertrand node status.

    Parameters
    ----------
    json_output : bool
        Whether to emit machine-readable JSON.
    deadline : Deadline
        Kubernetes request budget.
    """
    payload = await _node_status_payload(deadline=deadline)
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
    print(f"  k0s: {'ready' if payload['k0s_ready'] else 'not ready'}")
    print(f"  rook ceph: {'ready' if payload['rook_ceph_ready'] else 'not ready'}")
    print(f"  storage report: {payload['storage_report'] or 'unavailable'}")
    devices = payload["devices"]
    if isinstance(devices, list):
        print(f"  DRA devices: {len(devices)}")


async def bertrand_node_name_set(*, display_name: str, deadline: Deadline) -> None:
    """Set or clear the local Bertrand node display name.

    Parameters
    ----------
    display_name : str
        Human-readable display name. An empty string clears the name.
    deadline : Deadline
        Kubernetes convergence budget.
    """
    with Kube.external() as kube:
        record = await ensure_local_bertrand_node(
            kube,
            display_name=display_name,
            deadline=deadline,
        )
    shown = record.display_name or "(none)"
    print(f"node display name: {shown}")


async def bertrand_node_device_list(
    *,
    json_output: bool,
    deadline: Deadline,
) -> None:
    """Print managed DRA inventory for the local Kubernetes node.

    Parameters
    ----------
    json_output : bool
        Whether to emit machine-readable JSON.
    deadline : Deadline
        Kubernetes request budget.
    """
    with Kube.external() as kube:
        node = await ensure_local_bertrand_node(kube, deadline=deadline)
        records = await list_device_inventory(
            kube,
            host_ids=(node.host_id,),
            deadline=deadline,
        )
    payload = [_device_payload(record) for record in records]
    if json_output:
        emit_json(payload)
        return
    if not records:
        print("no DRA devices")
        return
    for record in records:
        print(_device_line(record))


async def bertrand_node_device_add(
    *,
    capability_id: str,
    device_name: str,
    cdi_selector: str,
    attributes: dict[str, str],
    deadline: Deadline,
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
    deadline : Deadline
        Kubernetes request budget.
    """
    with Kube.external() as kube:
        node = await ensure_local_bertrand_node(kube, deadline=deadline)
        record = await upsert_device_inventory(
            kube,
            capability_id=capability_id,
            host_id=node.host_id,
            node_name=node.node_name,
            device_name=device_name,
            cdi_selector=cdi_selector,
            attributes=attributes,
            deadline=deadline,
        )
        await refresh_node_resource_slice(
            kube,
            node_name=node.node_name,
            deadline=deadline,
        )
    print(_device_line(record))


async def bertrand_node_device_rm(
    *,
    capability_id: str,
    device_name: str,
    deadline: Deadline,
) -> None:
    """Delete one local managed DRA inventory record.

    Parameters
    ----------
    capability_id : str
        Host-agnostic device capability ID.
    device_name : str
        Node-local device inventory name.
    deadline : Deadline
        Kubernetes request budget.
    """
    with Kube.external() as kube:
        node = await ensure_local_bertrand_node(kube, deadline=deadline)
        deleted = await delete_device_inventory(
            kube,
            capability_id=capability_id,
            host_id=node.host_id,
            node_name=node.node_name,
            device_name=device_name,
            deadline=deadline,
        )
        await refresh_node_resource_slice(
            kube,
            node_name=node.node_name,
            deadline=deadline,
        )
    state = "deleted" if deleted else "not found"
    print(f"{capability_id} {device_name}: {state}")


async def bertrand_node_storage_status(
    *,
    json_output: bool,
    deadline: Deadline,
) -> None:
    """Print Ceph storage autoscaler status.

    Parameters
    ----------
    json_output : bool
        Whether to emit machine-readable JSON.
    deadline : Deadline
        Kubernetes request budget.
    """
    with Kube.external() as kube:
        node = await ensure_local_bertrand_node(kube, deadline=deadline)
        snapshot = await storage_cli_snapshot(
            kube,
            host_id=node.host_id,
            deadline=deadline,
        )
    payload = snapshot.status_payload()
    if json_output:
        emit_json(payload)
        return
    print_node_storage_status(snapshot)


async def bertrand_node_storage_doctor(
    *,
    json_output: bool,
    deadline: Deadline,
) -> None:
    """Print local Ceph storage diagnostics and action records.

    Parameters
    ----------
    json_output : bool
        Whether to emit machine-readable JSON.
    deadline : Deadline
        Kubernetes request budget.
    """
    with Kube.external() as kube:
        node = await ensure_local_bertrand_node(kube, deadline=deadline)
        snapshot = await storage_cli_snapshot(
            kube,
            host_id=node.host_id,
            deadline=deadline,
        )
    if json_output:
        emit_json(snapshot.doctor_payload())
        return
    print_node_storage_doctor(snapshot)


def parse_device_attrs(values: list[str]) -> dict[str, str]:
    """Parse repeated `KEY=VALUE` device attributes.

    Returns
    -------
    dict[str, str]
        Parsed attributes sorted by key.

    Raises
    ------
    ValueError
        If any attribute does not use `KEY=VALUE` syntax.
    """
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


async def _node_status_payload(*, deadline: Deadline) -> dict[str, object]:
    try:
        host_id = STATE.id
    except OSError:
        host_id = ""
    try:
        k0s_ready = await Kube.ready(deadline=deadline)
    except (OSError, TimeoutError, RuntimeError, ValueError):
        k0s_ready = False
    payload: dict[str, object] = {
        "host_id": host_id,
        "display_name": "",
        "phase": "",
        "k0s_ready": k0s_ready,
        "rook_ceph_ready": False,
        "kubernetes": {},
        "storage_report": "",
        "devices": [],
    }
    try:
        with Kube.external() as kube:
            payload["rook_ceph_ready"] = await rook_ceph_ready(
                kube,
                deadline=deadline,
            )
            bertrand_node: BertrandNodeRecord | None = None
            if host_id:
                bertrand_node = await ensure_local_bertrand_node(
                    kube,
                    host_id=host_id,
                    deadline=deadline,
                )
                node = await Node.get(
                    kube,
                    name=bertrand_node.node_name,
                    deadline=deadline,
                )
                if node is None:
                    node = await Node.local(kube, deadline=deadline)
            else:
                node = await Node.local(kube, deadline=deadline)
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
            storage = await read_storage_state(kube, deadline=deadline)
            report = next(
                (
                    item
                    for item in storage.status.nodes.values()
                    if item.host_id == host_id
                ),
                None,
            )
            if report is not None:
                payload["storage_report"] = node_report_status_payload(report)
            devices = await list_device_inventory(
                kube,
                host_ids=(host_id,) if host_id else None,
                node_names=(node.name,),
                deadline=deadline,
            )
            payload["devices"] = [_device_payload(device) for device in devices]
    except (OSError, TimeoutError, RuntimeError, ValueError) as err:
        payload["kubernetes_error"] = str(err)
    return payload


def _device_payload(record: BertrandDeviceRecord) -> dict[str, object]:
    return {
        "name": record.name,
        "capability_id": record.capability_id,
        "host_id": record.host_id,
        "node_name": record.node_name,
        "device_name": record.spec.device_name,
        "cdi_selector": record.cdi_selector,
        "attributes": dict(record.spec.attributes),
    }


def _device_line(record: BertrandDeviceRecord) -> str:
    return (
        f"{record.capability_id} {record.spec.device_name} "
        f"[{record.node_name}] -> {record.cdi_selector}"
    )
