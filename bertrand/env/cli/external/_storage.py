"""Shared external CLI helpers for Bertrand-managed Ceph storage status."""

from __future__ import annotations

import asyncio
from collections.abc import Mapping
from dataclasses import dataclass
from datetime import UTC, datetime
from typing import TYPE_CHECKING, cast

from bertrand.env.git import BERTRAND_NAMESPACE, INFINITY
from bertrand.env.kube.ceph.capacity import (
    STORAGE_ACTION_PHASES,
    STORAGE_ACTION_RESOURCE,
    STORAGE_NODE_REPORT_MAX_AGE_SECONDS,
    read_storage_state,
)
from bertrand.env.kube.ceph.csi import (
    CSI_CONTROLLER_NAME,
    CSI_DRIVER_NAME,
    CSI_NODE_NAME,
)
from bertrand.env.kube.daemonset import DaemonSet
from bertrand.env.kube.deployment import Deployment

if TYPE_CHECKING:
    from bertrand.env.kube.api.client import Kube
    from bertrand.env.kube.ceph.capacity import (
        CephStorageActionRecord,
        CephStorageNodeReport,
        CephStorageOSD,
        CephStorageReservation,
        CephStorageStateRecord,
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


def _storage_action_counts(
    actions: list[CephStorageActionRecord],
) -> dict[str, int]:
    counts: dict[str, int] = dict.fromkeys(STORAGE_ACTION_PHASES, 0)
    for action in actions:
        counts[action.status.phase] += 1
    return counts


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
    policy : CephStorageStateRecord
        Cluster-wide storage policy.
    actions : list[CephStorageActionRecord]
        Storage action records in command scope.
    reservations : list[CephStorageReservation]
        Cluster-wide storage reservation records.
    reports : list[CephStorageNodeReport]
        Storage node reports in command scope.
    osds : list[CephStorageOSD]
        Managed OSD records in command scope.
    csi : dict[str, object]
        Bertrand OSD CSI readiness payload.
    """

    policy: CephStorageStateRecord
    actions: list[CephStorageActionRecord]
    reservations: list[CephStorageReservation]
    reports: list[CephStorageNodeReport]
    osds: list[CephStorageOSD]
    csi: dict[str, object]

    def status_payload(self) -> dict[str, object]:
        """Return the stable JSON status payload for storage commands.

        Returns
        -------
        dict[str, object]
            JSON-serializable storage status payload.
        """
        return {
            "policy": self.policy.spec.model_dump(mode="json"),
            "status": (
                self.policy.policy_status.model_dump(mode="json")
                if self.policy.policy_status
                else None
            ),
            "action_counts": _storage_action_counts(self.actions),
            "reservations": [
                {
                    "name": reservation.name,
                    "spec": _reservation_spec_payload(reservation),
                    "status": _reservation_status_payload(reservation),
                }
                for reservation in self.reservations
            ],
            "reports": [
                {
                    "name": report.name,
                    "spec": _node_report_spec_payload(report),
                    "status": node_report_status_payload(report),
                }
                for report in self.reports
            ],
            "osds": [
                {
                    "name": osd.name,
                    "spec": _osd_spec_payload(osd),
                    "status": _osd_status_payload(osd),
                }
                for osd in self.osds
            ],
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
                {
                    "name": reservation.name,
                    "spec": _reservation_spec_payload(reservation),
                    "status": _reservation_status_payload(reservation),
                }
                for reservation in self.reservations
            ],
            "reports": [
                {
                    "name": report.name,
                    "spec": _node_report_spec_payload(report),
                    "status": node_report_status_payload(report),
                }
                for report in self.reports
            ],
            "osds": [
                {
                    "name": osd.name,
                    "spec": _osd_spec_payload(osd),
                    "status": _osd_status_payload(osd),
                }
                for osd in self.osds
            ],
        }

    def active_reservations(self) -> list[CephStorageReservation]:
        """Return pending or ready storage reservations.

        Returns
        -------
        list[CephStorageReservation]
            Storage reservations that still contribute to active demand.
        """
        return [
            reservation
            for reservation in self.reservations
            if reservation.phase in {"Pending", "Ready"}
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
    policy = await read_storage_state(kube, timeout=timeout)
    actions = await STORAGE_ACTION_RESOURCE.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        timeout=timeout,
    )
    reservations = sorted(
        policy.status.reservations.values(),
        key=lambda item: item.name,
    )
    reports = sorted(policy.status.nodes.values(), key=lambda item: item.name)
    osds = sorted(policy.status.osds.values(), key=lambda item: item.name)
    if host_id is not None:
        actions = [action for action in actions if action.spec.host_id == host_id]
        reports = [report for report in reports if report.host_id == host_id]
        osds = [osd for osd in osds if osd.host_id == host_id]
    return StorageCliSnapshot(
        policy=policy,
        actions=actions,
        reservations=reservations,
        reports=reports,
        osds=osds,
        csi=await storage_csi_status(kube),
    )


def node_report_status_payload(report: CephStorageNodeReport) -> dict[str, object]:
    """Return the stable JSON status payload for a storage node report.

    Returns
    -------
    dict[str, object]
        JSON-serializable node-report status.
    """
    return report.model_dump(
        mode="json",
        include={
            "free_bytes",
            "path",
            "lvm_free_bytes",
            "lvm_pvs",
            "lvm_pv_inventory",
            "loop_fallback_active",
            "heartbeat_at",
            "last_error",
        },
    )


def _reservation_spec_payload(
    reservation: CephStorageReservation,
) -> dict[str, object]:
    return reservation.model_dump(
        mode="json",
        include={
            "owner_kind",
            "owner_name",
            "request_id",
            "requested_bytes",
            "reason",
            "expires_at",
        },
    )


def _reservation_status_payload(
    reservation: CephStorageReservation,
) -> dict[str, object]:
    return reservation.model_dump(
        mode="json",
        include={
            "phase",
            "ready_at",
            "released_at",
            "observed_free_bytes",
            "last_error",
        },
    )


def _node_report_spec_payload(report: CephStorageNodeReport) -> dict[str, object]:
    return report.model_dump(mode="json", include={"node_name", "host_id"})


def _osd_spec_payload(osd: CephStorageOSD) -> dict[str, object]:
    return osd.model_dump(
        mode="json",
        include={
            "origin",
            "node_name",
            "host_id",
            "pv_name",
            "pv_uuid",
            "pv_device",
            "lv_name",
            "lv_path",
            "loop_file",
            "loop_device",
            "block_path",
            "csi_volume_id",
            "persistent_volume_name",
            "persistent_volume_claim_namespace",
            "persistent_volume_claim_name",
            "device_set_name",
            "target_bytes",
        },
    )


def _osd_status_payload(osd: CephStorageOSD) -> dict[str, object]:
    return osd.model_dump(
        mode="json",
        include={
            "phase",
            "observed_bytes",
            "ceph_osd_id",
            "created_at",
            "phase_changed_at",
            "last_seen_at",
            "retired_at",
            "last_error",
        },
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
    driver, controller, node = await asyncio.gather(
        kube.run(
            lambda request_timeout: kube.storage.read_csi_driver(
                name=CSI_DRIVER_NAME,
                _request_timeout=request_timeout,
            ),
            timeout=INFINITY,
            context=f"failed to inspect CSIDriver {CSI_DRIVER_NAME!r}",
        ),
        Deployment.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=CSI_CONTROLLER_NAME,
            timeout=INFINITY,
        ),
        DaemonSet.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=CSI_NODE_NAME,
            timeout=INFINITY,
        ),
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


def print_cluster_storage_status(snapshot: StorageCliSnapshot) -> None:
    """Print cluster-wide storage status text.

    Parameters
    ----------
    snapshot : StorageCliSnapshot
        Storage records collected for the cluster command.
    """
    payload = snapshot.status_payload()
    print("storage:")
    print_storage_status_fields(payload["status"], local=False)
    print(f"  node reports: {len(snapshot.reports)}")
    print(f"  managed OSD records: {len(snapshot.osds)}")
    print_storage_csi_line(payload["csi"])
    for osd in snapshot.osds:
        print(storage_osd_line(osd, include_ceph_id=True))
    _print_action_counts(payload["action_counts"])
    print(f"  active reservations: {len(snapshot.active_reservations())}")


def print_node_storage_status(snapshot: StorageCliSnapshot) -> None:
    """Print local-node storage status text.

    Parameters
    ----------
    snapshot : StorageCliSnapshot
        Storage records collected for one local node.
    """
    payload = snapshot.status_payload()
    print("storage:")
    print_storage_status_fields(payload["status"], local=True)
    _print_action_counts(payload["action_counts"])
    print(f"  active reservations: {len(snapshot.active_reservations())}")
    print(f"  node reports for this host: {len(snapshot.reports)}")
    print(f"  managed OSD records: {len(snapshot.osds)}")
    print_storage_csi_line(payload["csi"])
    for osd in snapshot.osds:
        if osd.node_name:
            print(storage_osd_line(osd, include_ceph_id=False))


def print_cluster_storage_doctor(snapshot: StorageCliSnapshot) -> None:
    """Print cluster-wide storage diagnostics.

    Parameters
    ----------
    snapshot : StorageCliSnapshot
        Storage records collected for the cluster command.
    """
    print("doctor:")
    if not snapshot.reports:
        print("  no storage-agent node reports are available")
    _print_stale_reports(snapshot.reports)
    if not _has_lvm_pvs(snapshot.reports):
        print("  no node reports a 'bertrand' LVM volume group with free PVs")
        print("  create a host LVM volume group named 'bertrand' for preferred OSDs")
    else:
        total_lvm_free = sum(report.lvm_free_bytes for report in snapshot.reports)
        print(f"  reported free LVM bytes in 'bertrand' VG: {total_lvm_free}")
    if not storage_csi_ready(snapshot.csi):
        print(
            "  Bertrand OSD CSI is not fully ready; PVC-backed OSD growth may "
            "be blocked"
        )
    _print_stuck_osds(
        snapshot.osds,
        message_prefix="Rook/CSI may still be reconciling the PVC-backed OSD since",
    )
    if _active_loop(snapshot.osds) and _lvm_available(snapshot.reports):
        print(
            "  loop fallback is active while LVM space is available; "
            "Bertrand will grow LVM capacity and retire the loop OSD once "
            "Ceph reports it is safe"
        )
    _print_cluster_policy_guidance(snapshot.status_payload()["status"])
    _print_pending_reservations(snapshot.active_reservations(), limit=5)
    _print_osd_errors(snapshot.osds)
    failed = [action for action in snapshot.actions if action.status.phase == "Failed"]
    for action in failed[:5]:
        print(f"  failed {action.name}: {action.status.message}")


def print_node_storage_doctor(snapshot: StorageCliSnapshot) -> None:
    """Print local-node storage diagnostics and action records.

    Parameters
    ----------
    snapshot : StorageCliSnapshot
        Storage records collected for one local node.
    """
    print("storage doctor:")
    if not snapshot.actions:
        print("  no tracked storage actions for this host")
    if not storage_csi_ready(snapshot.csi):
        print("  Bertrand OSD CSI is not fully ready")
    if not _has_lvm_pvs(snapshot.reports):
        print("  no reported local 'bertrand' LVM PVs; loop fallback may be used")
    _print_stuck_osds(
        snapshot.osds,
        message_prefix="waiting for Rook/CSI reconciliation since",
    )
    _print_osd_errors(snapshot.osds)
    if _active_loop(snapshot.osds) and _lvm_available(snapshot.reports):
        print(
            "  loop fallback is active and LVM space is available; migration will "
            "proceed once Ceph is healthy"
        )
    _print_node_policy_guidance(snapshot)
    _print_pending_reservations(snapshot.reservations, limit=None)
    for action in snapshot.actions:
        _print_action_detail(action)
    print(
        "  tip: create a host LVM volume group named 'bertrand' and add PVs "
        "to give Bertrand preferred storage capacity."
    )


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


def storage_osd_line(osd: CephStorageOSD, *, include_ceph_id: bool) -> str:
    """Return the human-readable status line for one managed OSD.

    Parameters
    ----------
    osd : CephStorageOSD
        Managed OSD record.
    include_ceph_id : bool
        Whether to append the observed Ceph OSD ID when available.

    Returns
    -------
    str
        Human-readable OSD status line.
    """
    line = f"    {osd.name}: {osd.origin} {osd.phase} node={osd.node_name}"
    if include_ceph_id and osd.ceph_osd_id is not None:
        line = f"{line} ceph=osd.{osd.ceph_osd_id}"
    return line


def _print_action_counts(counts: object) -> None:
    print("  actions:")
    if not isinstance(counts, Mapping):
        return
    action_counts = cast("Mapping[str, object]", counts)
    for phase, count in action_counts.items():
        print(f"    {phase}: {count}")


def _has_lvm_pvs(reports: list[CephStorageNodeReport]) -> bool:
    return any(report.lvm_pvs for report in reports)


def _lvm_available(reports: list[CephStorageNodeReport]) -> bool:
    return any(report.lvm_free_bytes > 0 for report in reports)


def _active_loop(osds: list[CephStorageOSD]) -> bool:
    return any(
        osd.origin == "loop-fallback" and osd.phase not in {"Retired", "Failed"}
        for osd in osds
    )


def _stuck_osds(osds: list[CephStorageOSD]) -> list[CephStorageOSD]:
    return [
        osd
        for osd in osds
        if osd.phase in {"HostPrepared", "Binding", "Expanding", "Shrinking"}
    ]


def _print_stale_reports(reports: list[CephStorageNodeReport]) -> None:
    now = datetime.now(UTC)
    stale_reports = [
        report
        for report in reports
        if report.heartbeat_at is None
        or (
            now
            - (
                report.heartbeat_at.replace(tzinfo=UTC)
                if report.heartbeat_at.tzinfo is None
                else report.heartbeat_at.astimezone(UTC)
            )
        ).total_seconds()
        > STORAGE_NODE_REPORT_MAX_AGE_SECONDS
    ]
    for report in stale_reports[:5]:
        print(
            f"  storage report {report.name} is stale "
            f"(host={report.host_id}, kube={report.node_name})"
        )


def _print_stuck_osds(
    osds: list[CephStorageOSD],
    *,
    message_prefix: str,
) -> None:
    for osd in _stuck_osds(osds)[:5]:
        changed_at = (
            osd.phase_changed_at.isoformat()
            if osd.phase_changed_at is not None
            else "unknown"
        )
        print(f"  OSD {osd.name} is {osd.phase}; {message_prefix} {changed_at}")


def _print_osd_errors(osds: list[CephStorageOSD]) -> None:
    for osd in osds:
        if osd.last_error:
            print(f"  OSD {osd.name} error: {osd.last_error}")


def _print_cluster_policy_guidance(status: object) -> None:
    if not isinstance(status, dict):
        return
    status_map = cast("dict[str, object]", status)
    missing_lvm_osds = status_map.get("missing_lvm_osd_pvs")
    if isinstance(missing_lvm_osds, int) and missing_lvm_osds > 0:
        print(
            "  one or more usable LVM PVs do not yet have managed OSD "
            "coverage; Bertrand will create minimum-size OSDs first"
        )
    if status_map.get("lvm_shrink_candidate"):
        print(
            "  LVM shrink candidate selected: "
            f"{status_map.get('lvm_shrink_candidate')} -> "
            f"{status_map.get('lvm_shrink_target_bytes')} bytes"
        )


def _print_node_policy_guidance(snapshot: StorageCliSnapshot) -> None:
    policy = snapshot.policy
    if policy.policy_status is None:
        return
    if policy.policy_status.missing_lvm_osd_pvs > 0:
        print(
            "  usable LVM PVs are missing managed OSD coverage; Bertrand "
            "will create minimum-size PV-pinned OSDs before shrinking"
        )
    if policy.policy_status.lvm_shrink_candidate:
        print(
            "  LVM shrink candidate selected: "
            f"{policy.policy_status.lvm_shrink_candidate} -> "
            f"{policy.policy_status.lvm_shrink_target_bytes} bytes"
        )


def _print_pending_reservations(
    reservations: list[CephStorageReservation],
    *,
    limit: int | None,
) -> None:
    pending = [
        reservation for reservation in reservations if reservation.phase == "Pending"
    ]
    for reservation in pending[:limit]:
        print(
            f"  reservation {reservation.name} is pending: "
            f"{reservation.last_error or 'waiting for headroom'}"
        )


def _print_action_detail(action: CephStorageActionRecord) -> None:
    print(f"{action.name}: {action.spec.operation} {action.status.phase}")
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
