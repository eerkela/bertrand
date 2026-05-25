"""Shared external CLI formatting for Bertrand DRA device inventory."""

from __future__ import annotations

from typing import TYPE_CHECKING

from bertrand.env.kube.capability.device import (
    delete_device_inventory,
    list_device_inventory,
    refresh_node_resource_slice,
    upsert_device_inventory,
)
from bertrand.env.kube.node_identity import (
    ensure_local_bertrand_node,
    list_bertrand_nodes,
)

if TYPE_CHECKING:
    from bertrand.env.kube.api.client import Kube
    from bertrand.env.kube.capability.device import BertrandDeviceRecord
    from bertrand.env.kube.node_identity import BertrandNodeRecord


async def local_device_inventory(
    kube: Kube,
    *,
    timeout: float,
) -> list[BertrandDeviceRecord]:
    """Return local Bertrand node identity and managed device inventory.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum Kubernetes request budget in seconds.

    Returns
    -------
    list[BertrandDeviceRecord]
        Matching local DRA inventory records.
    """
    node = await ensure_local_bertrand_node(kube, timeout=timeout)
    return await list_device_inventory(
        kube,
        host_ids=(node.host_id,),
        timeout=timeout,
    )


async def cluster_device_inventory(
    kube: Kube,
    *,
    node: str | None,
    capability_id: str | None,
    timeout: float,
) -> tuple[list[BertrandDeviceRecord], dict[str, BertrandNodeRecord]]:
    """Return cluster DRA inventory and Bertrand node owner records.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    node : str | None
        Optional Bertrand host UUID filter.
    capability_id : str | None
        Optional device capability ID filter.
    timeout : float
        Maximum Kubernetes request budget in seconds.

    Returns
    -------
    tuple[list[BertrandDeviceRecord], dict[str, BertrandNodeRecord]]
        Inventory records and owner records keyed by host UUID.
    """
    records = await list_device_inventory(
        kube,
        capability_id=capability_id,
        host_ids=None if node is None else (node,),
        timeout=timeout,
    )
    nodes = {
        item.host_id: item
        for item in await list_bertrand_nodes(kube, timeout=timeout)
    }
    return records, nodes


async def upsert_local_device_inventory(
    kube: Kube,
    *,
    capability_id: str,
    device_name: str,
    cdi_selector: str,
    attributes: dict[str, str],
    timeout: float,
) -> BertrandDeviceRecord:
    """Create or update one local device record and refresh its ResourceSlice.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
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

    Returns
    -------
    BertrandDeviceRecord
        Created or updated device inventory record.
    """
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
    return record


async def delete_local_device_inventory(
    kube: Kube,
    *,
    capability_id: str,
    device_name: str,
    timeout: float,
) -> bool:
    """Delete one local device record and refresh its ResourceSlice.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    capability_id : str
        Host-agnostic device capability ID.
    device_name : str
        Node-local device inventory name.
    timeout : float
        Maximum Kubernetes request budget in seconds.

    Returns
    -------
    bool
        Whether a matching inventory record was deleted.
    """
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
    return deleted


def device_payload(
    record: BertrandDeviceRecord,
    *,
    owner: BertrandNodeRecord | None = None,
    include_display_name: bool = False,
) -> dict[str, object]:
    """Return the stable JSON payload for one DRA inventory record.

    Parameters
    ----------
    record : BertrandDeviceRecord
        Device inventory record.
    owner : BertrandNodeRecord | None, default None
        Optional owning Bertrand node record.
    include_display_name : bool, default False
        Whether to include the cluster-wide ``display_name`` field.

    Returns
    -------
    dict[str, object]
        JSON-serializable device inventory payload.
    """
    payload: dict[str, object] = {
        "name": record.name,
        "capability_id": record.capability_id,
        "host_id": record.host_id,
        "node_name": record.node_name,
        "device_name": record.spec.device_name,
        "cdi_selector": record.cdi_selector,
        "attributes": dict(record.spec.attributes),
    }
    if include_display_name:
        payload["display_name"] = "" if owner is None else owner.display_name
    return payload


def device_line(
    record: BertrandDeviceRecord,
    *,
    owner: BertrandNodeRecord | None = None,
    cluster: bool = False,
) -> str:
    """Return the human-readable inventory line for one DRA record.

    Parameters
    ----------
    record : BertrandDeviceRecord
        Device inventory record.
    owner : BertrandNodeRecord | None, default None
        Optional owning Bertrand node record for cluster-wide output.
    cluster : bool, default False
        Whether to include cluster-wide owner context.

    Returns
    -------
    str
        Human-readable inventory line.
    """
    if not cluster:
        location = record.node_name
    elif owner is None or not owner.display_name:
        location = f"{record.host_id}; kube={record.node_name}"
    else:
        location = f"{owner.display_name} ({record.host_id}); kube={record.node_name}"
    return (
        f"{record.capability_id} {record.spec.device_name} "
        f"[{location}] -> {record.cdi_selector}"
    )
