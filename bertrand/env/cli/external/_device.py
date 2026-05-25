"""Shared external CLI formatting for Bertrand DRA device inventory."""

from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from bertrand.env.kube.capability.device import BertrandDeviceRecord
    from bertrand.env.kube.node_identity import BertrandNodeRecord


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
