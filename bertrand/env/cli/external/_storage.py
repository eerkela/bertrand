"""Shared external CLI helpers for Bertrand-managed Ceph storage status."""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
from typing import TYPE_CHECKING, cast

from bertrand.env.git import BERTRAND_NAMESPACE, INFINITY
from bertrand.env.kube.ceph.capacity import (
    CephStoragePlanner,
    list_storage_actions,
    list_storage_node_reports,
    list_storage_osds,
    list_storage_reservations,
    read_storage_policy,
)
from bertrand.env.kube.ceph.csi import CSI_DRIVER_NAME
from bertrand.env.kube.ceph.storage import CSI_CONTROLLER_NAME, CSI_NODE_NAME
from bertrand.env.kube.daemonset import DaemonSet
from bertrand.env.kube.deployment import Deployment

if TYPE_CHECKING:
    from bertrand.env.kube.api.client import Kube
    from bertrand.env.kube.ceph.capacity import (
        CephStorageActionRecord,
        CephStorageNodeRecord,
        CephStorageOSDRecord,
        CephStoragePolicyRecord,
        CephStorageReservationRecord,
    )

_COMMON_STATUS_FIELDS = (
    ("used", "used_ratio"),
    ("loop fallback OSDs", "loop_osds"),
    ("LVM-backed OSDs", "lvm_osds"),
    ("elastic bytes", "elastic_bytes"),
    ("durable bytes", "durable_bytes"),
    ("free bytes", "free_bytes"),
    ("target headroom bytes", "headroom_target_bytes"),
    ("reserved bytes", "reserved_bytes"),
    ("write rate EWMA bytes/s", "write_rate_ewma_bytes_per_second"),
    ("projected seconds to headroom floor", "projected_seconds_to_headroom_floor"),
    ("growth recommendation bytes", "growth_recommendation_bytes"),
    ("missing LVM OSD PVs", "missing_lvm_osd_pvs"),
    ("LVM reclaimable bytes", "lvm_reclaimable_bytes"),
    ("LVM shrink candidate", "lvm_shrink_candidate"),
    ("LVM shrink target bytes", "lvm_shrink_target_bytes"),
)

_LOCAL_STATUS_FIELDS = (
    ("LVM preferred", "lvm_preferred"),
    ("managed OSDs", "managed_osds"),
    ("shrink candidates", "shrink_candidates"),
)


@dataclass(slots=True)
class StorageCliSnapshot:
    """Storage records collected for one external CLI command.

    Parameters
    ----------
    policy : CephStoragePolicyRecord
        Cluster-wide storage policy.
    actions : list[CephStorageActionRecord]
        Storage action records in command scope.
    reservations : list[CephStorageReservationRecord]
        Cluster-wide storage reservation records.
    reports : list[CephStorageNodeRecord]
        Storage node reports in command scope.
    osds : list[CephStorageOSDRecord]
        Managed OSD records in command scope.
    csi : dict[str, object]
        Bertrand OSD CSI readiness payload.
    """

    policy: CephStoragePolicyRecord
    actions: list[CephStorageActionRecord]
    reservations: list[CephStorageReservationRecord]
    reports: list[CephStorageNodeRecord]
    osds: list[CephStorageOSDRecord]
    csi: dict[str, object]

    def status_payload(self) -> dict[str, object]:
        """Return the stable JSON status payload for storage commands.

        Returns
        -------
        dict[str, object]
            JSON-serializable storage status payload.
        """
        planner = CephStoragePlanner()
        return {
            "policy": self.policy.spec.model_dump(mode="json"),
            "status": (
                self.policy.status.model_dump(mode="json")
                if self.policy.status
                else None
            ),
            "action_counts": planner.action_counts(self.actions),
            "reservations": [
                reservation.model_dump(mode="json")
                for reservation in self.reservations
            ],
            "reports": [report.model_dump(mode="json") for report in self.reports],
            "osds": [osd.model_dump(mode="json") for osd in self.osds],
            "csi": self.csi,
        }

    def doctor_payload(self) -> dict[str, object]:
        """Return the stable JSON diagnostic payload for local storage doctor.

        Returns
        -------
        dict[str, object]
            JSON-serializable local storage diagnostic payload.
        """
        return {
            "actions": [action.model_dump(mode="json") for action in self.actions],
            "csi": self.csi,
            "reservations": [
                reservation.model_dump(mode="json")
                for reservation in self.reservations
            ],
            "reports": [report.model_dump(mode="json") for report in self.reports],
            "osds": [osd.model_dump(mode="json") for osd in self.osds],
        }

    def active_reservations(self) -> list[CephStorageReservationRecord]:
        """Return pending or ready storage reservations.

        Returns
        -------
        list[CephStorageReservationRecord]
            Storage reservations that still contribute to active demand.
        """
        return [
            reservation
            for reservation in self.reservations
            if reservation.status.phase in {"Pending", "Ready"}
        ]


async def storage_cli_snapshot(
    kube: Kube,
    *,
    host_id: str | None = None,
    timeout: float = INFINITY,
) -> StorageCliSnapshot:
    """Collect storage records for external CLI status and doctor commands.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    host_id : str | None, default None
        Optional host ID used to scope node-local records. Reservations remain
        cluster-wide to preserve existing CLI output.
    timeout : float, default INFINITY
        Kubernetes request budget in seconds.

    Returns
    -------
    StorageCliSnapshot
        Storage records and CSI readiness in command scope.
    """
    policy = await read_storage_policy(kube, timeout=timeout)
    actions = await list_storage_actions(kube, timeout=timeout)
    reservations = await list_storage_reservations(kube, timeout=timeout)
    reports = await list_storage_node_reports(kube, timeout=timeout)
    osds = await list_storage_osds(kube, timeout=timeout)
    if host_id is not None:
        actions = [action for action in actions if action.spec.host_id == host_id]
        reports = [report for report in reports if report.spec.host_id == host_id]
        osds = [osd for osd in osds if osd.spec.host_id == host_id]
    return StorageCliSnapshot(
        policy=policy,
        actions=actions,
        reservations=reservations,
        reports=reports,
        osds=osds,
        csi=await storage_csi_status(kube),
    )


async def storage_csi_status(kube: Kube) -> dict[str, object]:
    """Return Bertrand OSD CSI driver readiness.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.

    Returns
    -------
    dict[str, object]
        JSON-serializable CSI readiness payload.
    """
    driver = await kube.run(
        lambda request_timeout: kube.storage.read_csi_driver(
            name=CSI_DRIVER_NAME,
            _request_timeout=request_timeout,
        ),
        timeout=INFINITY,
        context=f"failed to inspect CSIDriver {CSI_DRIVER_NAME!r}",
    )
    controller = await Deployment.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=CSI_CONTROLLER_NAME,
        timeout=INFINITY,
    )
    node = await DaemonSet.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=CSI_NODE_NAME,
        timeout=INFINITY,
    )
    return {
        "driver": driver is not None,
        "controller_ready": controller is not None and controller.ready_replicas >= 1,
        "node_ready": (
            node is not None
            and node.desired_number_scheduled > 0
            and node.number_available >= node.desired_number_scheduled
        ),
        "node_available": node.number_available if node is not None else 0,
        "node_desired": node.desired_number_scheduled if node is not None else 0,
    }


def print_storage_status_fields(status: object, *, local: bool) -> None:
    """Print shared storage policy status fields.

    Parameters
    ----------
    status : object
        JSON-style policy status payload.
    local : bool
        Whether to print local-node-only status fields.
    """
    if not isinstance(status, Mapping):
        return
    status_map = cast("Mapping[str, object]", status)
    for label, key in _COMMON_STATUS_FIELDS:
        value = status_map.get(key)
        if key == "lvm_shrink_candidate":
            value = value or "none"
        print(f"  {label}: {value}")
    if local:
        for label, key in _LOCAL_STATUS_FIELDS:
            print(f"  {label}: {status_map.get(key)}")
        if status_map.get("last_error"):
            print(f"  last error: {status_map['last_error']}")
        return
    print(f"  last error: {status_map.get('last_error') or 'none'}")


def print_storage_csi_line(csi: object) -> None:
    """Print a compact Bertrand OSD CSI readiness line.

    Parameters
    ----------
    csi : object
        JSON-style CSI readiness payload.
    """
    if not isinstance(csi, Mapping):
        return
    csi_map = cast("Mapping[str, object]", csi)
    print(
        "  CSI: "
        f"driver={'ready' if csi_map.get('driver') else 'missing'}, "
        f"controller={'ready' if csi_map.get('controller_ready') else 'not ready'}, "
        f"nodes={csi_map.get('node_available')}/{csi_map.get('node_desired')}"
    )


def storage_csi_ready(csi: Mapping[str, object]) -> bool:
    """Return whether all Bertrand OSD CSI components look ready.

    Parameters
    ----------
    csi : Mapping[str, object]
        CSI readiness payload.

    Returns
    -------
    bool
        True when the driver, controller, and node plugin are all ready.
    """
    return all((csi.get("driver"), csi.get("controller_ready"), csi.get("node_ready")))


def storage_osd_line(osd: CephStorageOSDRecord, *, include_ceph_id: bool) -> str:
    """Return the human-readable status line for one managed OSD.

    Parameters
    ----------
    osd : CephStorageOSDRecord
        Managed OSD record.
    include_ceph_id : bool
        Whether to append the observed Ceph OSD ID when available.

    Returns
    -------
    str
        Human-readable OSD status line.
    """
    line = (
        f"    {osd.metadata.name}: {osd.spec.origin} "
        f"{osd.status.phase} node={osd.spec.node_name}"
    )
    if include_ceph_id and osd.status.ceph_osd_id is not None:
        line = f"{line} ceph=osd.{osd.status.ceph_osd_id}"
    return line
