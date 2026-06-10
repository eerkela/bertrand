"""Ceph storage controller control plane composition."""

from __future__ import annotations

import asyncio
import math
import sys
from contextlib import suppress
from dataclasses import dataclass
from datetime import UTC, datetime
from typing import TYPE_CHECKING, Any

from bertrand.env.git import BERTRAND_NAMESPACE, NO_DEADLINE, Deadline
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.api.spec import ContainerSpec, PodTemplateSpec, VolumeSpec
from bertrand.env.kube.build.repository import (
    CLUSTER_REGISTRY_READY_LABEL,
    CLUSTER_REGISTRY_READY_VALUE,
)
from bertrand.env.kube.build.request import BUILDKIT_BUILD_GROUP, BUILDKIT_BUILD_PLURAL
from bertrand.env.kube.ceph.api import (
    CephCapacitySnapshot,
    ceph_df,
    ceph_health,
    ceph_osds,
    parse_size_bytes,
)
from bertrand.env.kube.ceph.capacity import (
    CEPH_CAPACITY_GROUP,
    STORAGE_ACTION_PHASES,
    STORAGE_ACTION_PLURAL,
    STORAGE_ACTION_RESOURCE,
    STORAGE_ACTION_STALE_SECONDS,
    STORAGE_CONTROLLER_LABELS,
    STORAGE_NODE_REPORT_MAX_AGE_SECONDS,
    STORAGE_OSD_IN_FLIGHT_PHASES,
    STORAGE_OSD_STALE_PHASE_SECONDS,
    STORAGE_STATE_PLURAL,
    STORAGE_STATE_RESOURCE,
    STORAGE_TARGET_RETRY_COOLDOWN_SECONDS,
    CephStorageActionRecord,
    CephStorageActionSpec,
    CephStorageNodeReport,
    CephStorageOSD,
    CephStoragePolicyStatus,
    CephStorageReservation,
    CephStorageStateRecord,
    StorageActionOperation,
    create_storage_actions,
    ensure_ceph_capacity_crds,
    ensure_default_storage_policy,
    patch_storage_action_status,
    patch_storage_osd_status,
    patch_storage_reservation_status,
    read_storage_state,
    storage_loop_osd_name,
    storage_lvm_osd_name,
)
from bertrand.env.kube.ceph.csi import (
    ensure_ceph_osd_csi_driver,
)
from bertrand.env.kube.ceph.rook import patch_rook_device_sets
from bertrand.env.kube.ceph.snapshot import (
    ensure_repository_snapshot_support,
    maintain_repository_snapshots,
    next_repository_snapshot_time,
)
from bertrand.env.kube.ceph.volume import (
    REPOSITORY_STATE_PLURAL,
    REPOSITORY_STATE_RESOURCE,
    gc_repository_volumes,
    next_repository_volume_gc_time,
)
from bertrand.env.kube.control import MaintenanceClock
from bertrand.env.kube.daemonset import DaemonSet
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.node import NODE_RESOURCE
from bertrand.env.kube.rbac import (
    CLUSTER_ROLE_BINDING_RESOURCE,
    CLUSTER_ROLE_RESOURCE,
    rbac_role_manifest,
    rbac_service_account_binding_manifest,
)
from bertrand.env.kube.service_account import ServiceAccount

if TYPE_CHECKING:
    from collections.abc import Collection, Sequence

    from bertrand.env.kube.api.spec import PolicyRuleManifest
    from bertrand.env.kube.custom_object import CustomObjectResource

STORAGE_CONTROLLER_SERVICE_ACCOUNT = "bertrand-ceph-storage-controller"
STORAGE_CONTROLLER_NAME = "bertrand-ceph-storage-controller"
STORAGE_AGENT_NAME = "bertrand-ceph-storage-agent"
STORAGE_WATCH_RESTART_DELAY_SECONDS = 1.0
STORAGE_CONTROLLER_DEFAULT_RECONCILE_SECONDS = 30.0
REPOSITORY_VOLUME_GC_EMPTY_CHECK_SECONDS = 3600.0
REPOSITORY_VOLUME_GC_READY_CHECK_SECONDS = 900.0
REPOSITORY_VOLUME_GC_FAILURE_RETRY_SECONDS = 300.0
REPOSITORY_VOLUME_GC_TIMEOUT_SECONDS = 60.0
REPOSITORY_SNAPSHOT_EMPTY_CHECK_SECONDS = 3600.0
REPOSITORY_SNAPSHOT_READY_CHECK_SECONDS = 900.0
REPOSITORY_SNAPSHOT_FAILURE_RETRY_SECONDS = 300.0
REPOSITORY_SNAPSHOT_TIMEOUT_SECONDS = 120.0

HOST_ROOT_VOLUME = "host-root"
HOST_ROOT_MOUNT = "/host"


def _storage_controller_container(*, image: str, role: str) -> ContainerSpec:
    return ContainerSpec(
        name=role,
        image=image,
        image_pull_policy="Always",
        args=[role],
        env=[
            {
                "name": "NODE_NAME",
                "valueFrom": {"fieldRef": {"fieldPath": "spec.nodeName"}},
            }
        ],
        security_context={"privileged": True, "runAsUser": 0},
        volume_mounts=[{"name": HOST_ROOT_VOLUME, "mountPath": HOST_ROOT_MOUNT}],
    )


async def _ensure_rbac(kube: Kube, *, deadline: Deadline) -> None:
    await asyncio.gather(
        ServiceAccount.upsert(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
            labels=STORAGE_CONTROLLER_LABELS,
            deadline=deadline,
        ),
        CLUSTER_ROLE_RESOURCE.upsert(
            kube,
            name=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
            manifest=rbac_role_manifest(
                kind="ClusterRole",
                namespace=None,
                name=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
                labels=STORAGE_CONTROLLER_LABELS,
                rules=_storage_controller_rbac_rules(),
            ),
            deadline=deadline,
        ),
    )
    await CLUSTER_ROLE_BINDING_RESOURCE.upsert(
        kube,
        name=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
        manifest=rbac_service_account_binding_manifest(
            kind="ClusterRoleBinding",
            namespace=None,
            name=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
            role_kind="ClusterRole",
            role_name=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
            service_account_name=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
            service_account_namespace=BERTRAND_NAMESPACE,
            labels=STORAGE_CONTROLLER_LABELS,
        ),
        deadline=deadline,
    )


def _storage_controller_rbac_rules() -> list[PolicyRuleManifest]:
    return [
        {
            "apiGroups": [CEPH_CAPACITY_GROUP],
            "resources": [
                STORAGE_STATE_PLURAL,
                STORAGE_ACTION_PLURAL,
                REPOSITORY_STATE_PLURAL,
            ],
            "verbs": ["get", "list", "watch", "create", "update", "patch"],
        },
        {
            "apiGroups": [CEPH_CAPACITY_GROUP],
            "resources": [
                f"{STORAGE_STATE_PLURAL}/status",
                f"{STORAGE_ACTION_PLURAL}/status",
                f"{REPOSITORY_STATE_PLURAL}/status",
            ],
            "verbs": ["get", "update", "patch"],
        },
        {
            "apiGroups": ["ceph.rook.io"],
            "resources": ["cephclusters"],
            "verbs": ["get", "list", "watch", "patch"],
        },
        {
            "apiGroups": [""],
            "resources": [
                "events",
                "persistentvolumes",
                "persistentvolumeclaims",
            ],
            "verbs": [
                "get",
                "list",
                "watch",
                "create",
                "update",
                "patch",
                "delete",
            ],
        },
        {
            "apiGroups": ["storage.k8s.io"],
            "resources": [
                "csidrivers",
                "csinodes",
                "storageclasses",
                "volumeattachments",
            ],
            "verbs": [
                "get",
                "list",
                "watch",
                "create",
                "update",
                "patch",
                "delete",
            ],
        },
        {
            "apiGroups": ["coordination.k8s.io"],
            "resources": ["leases"],
            "verbs": [
                "get",
                "list",
                "watch",
                "create",
                "update",
                "patch",
                "delete",
            ],
        },
        {
            "apiGroups": [CEPH_CAPACITY_GROUP],
            "resources": [REPOSITORY_STATE_PLURAL],
            "verbs": ["delete"],
        },
        {
            "apiGroups": [""],
            "resources": ["secrets"],
            "verbs": ["get", "list", "watch", "delete"],
        },
        {
            "apiGroups": [BUILDKIT_BUILD_GROUP],
            "resources": [BUILDKIT_BUILD_PLURAL],
            "verbs": ["get", "list", "watch"],
        },
        {
            "apiGroups": [""],
            "resources": ["nodes"],
            "verbs": ["get", "list", "watch"],
        },
        {
            "apiGroups": [""],
            "resources": ["pods"],
            "verbs": ["get", "list", "watch"],
        },
        {
            "apiGroups": ["apps"],
            "resources": ["deployments"],
            "verbs": ["get", "list", "watch"],
        },
        {
            "apiGroups": ["batch"],
            "resources": ["jobs", "cronjobs"],
            "verbs": ["get", "list", "watch"],
        },
        {
            "apiGroups": ["snapshot.storage.k8s.io"],
            "resources": ["volumesnapshots"],
            "verbs": ["get", "list", "watch", "create", "delete"],
        },
        {
            "apiGroups": ["snapshot.storage.k8s.io"],
            "resources": ["volumesnapshotclasses"],
            "verbs": ["get", "list", "watch", "create"],
        },
    ]


async def _ensure_workloads(kube: Kube, *, image: str, deadline: Deadline) -> None:
    volumes = [
        VolumeSpec.host_path(HOST_ROOT_VOLUME, path="/", host_path_type="Directory")
    ]

    controller = await Deployment.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=STORAGE_CONTROLLER_NAME,
        labels={
            "app.kubernetes.io/name": STORAGE_CONTROLLER_NAME,
            "app.kubernetes.io/part-of": "bertrand",
            **STORAGE_CONTROLLER_LABELS,
        },
        selector={"app.kubernetes.io/name": STORAGE_CONTROLLER_NAME},
        pod_template=PodTemplateSpec(
            containers=[_storage_controller_container(image=image, role="controller")],
            volumes=volumes,
            service_account_name=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
            automount_service_account_token=True,
            node_selector={CLUSTER_REGISTRY_READY_LABEL: CLUSTER_REGISTRY_READY_VALUE},
            host_pid=True,
        ),
        deadline=deadline,
    )
    await controller.wait_rollout(kube, deadline=deadline)

    agent = await DaemonSet.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=STORAGE_AGENT_NAME,
        labels={
            "app.kubernetes.io/name": STORAGE_AGENT_NAME,
            "app.kubernetes.io/part-of": "bertrand",
            **STORAGE_CONTROLLER_LABELS,
        },
        selector={"app.kubernetes.io/name": STORAGE_AGENT_NAME},
        pod_template=PodTemplateSpec(
            containers=[_storage_controller_container(image=image, role="agent")],
            volumes=volumes,
            service_account_name=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
            automount_service_account_token=True,
            node_selector={CLUSTER_REGISTRY_READY_LABEL: CLUSTER_REGISTRY_READY_VALUE},
            host_pid=True,
        ),
        deadline=deadline,
    )
    await agent.wait_rollout(kube, deadline=deadline)


async def ensure_ceph_storage_controller(
    kube: Kube, *, image: str, deadline: Deadline
) -> None:
    """Converge Ceph storage controller CRDs, RBAC, and workloads in the local cluster.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    image : str
        Fully qualified storage controller image reference.
    deadline : Deadline
        Operation deadline for controller convergence.

    Raises
    ------
    ValueError
        If `image` is empty.
    """
    image = image.strip()
    if not image:
        msg = "control plane image reference cannot be empty"
        raise ValueError(msg)
    await ensure_ceph_capacity_crds(
        kube,
        deadline=deadline,
    )
    await REPOSITORY_STATE_RESOURCE.ensure_crd(
        kube,
        deadline=deadline,
    )
    await ensure_repository_snapshot_support(
        kube,
        deadline=deadline,
    )
    await _ensure_rbac(kube, deadline=deadline)
    await ensure_default_storage_policy(
        kube,
        deadline=deadline,
    )
    await ensure_ceph_osd_csi_driver(
        kube,
        image=image,
        service_account=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
        deadline=deadline,
    )
    await _ensure_workloads(kube, image=image, deadline=deadline)


async def _watch_storage_resource(
    kube: Kube,
    *,
    client: CustomObjectResource[Any],
    wake: asyncio.Event,
    deadline: Deadline,
    context: str,
) -> None:
    while True:
        try:
            async for _event in client.watch(
                kube,
                namespace=BERTRAND_NAMESPACE,
                deadline=deadline,
            ):
                wake.set()
            wake.set()
            remaining = deadline.remaining
            if remaining <= 0:
                return
            await deadline.sleep(STORAGE_WATCH_RESTART_DELAY_SECONDS)
        except asyncio.CancelledError:
            raise
        except (OSError, RuntimeError, ValueError) as err:
            print(
                "bertrand: warning: Ceph storage controller "
                f"{context} watch failed: {err}",
                file=sys.stderr,
            )
            wake.set()
            remaining = deadline.remaining
            if remaining <= 0:
                return
            await deadline.sleep(STORAGE_WATCH_RESTART_DELAY_SECONDS)


async def _ready_storage_nodes(kube: Kube, *, deadline: Deadline) -> list[str]:
    nodes = await NODE_RESOURCE.list(
        kube,
        labels={CLUSTER_REGISTRY_READY_LABEL: CLUSTER_REGISTRY_READY_VALUE},
        deadline=deadline,
    )
    return sorted(node.name for node in nodes if node.name and node.is_ready)


@dataclass(frozen=True)
class _StorageTarget:
    node_name: str
    operation: StorageActionOperation
    host_id: str
    storage_osd_name: str
    current_bytes: int
    available_bytes: int
    target_bytes: int
    pv_name: str = ""
    lv_name: str = ""


@dataclass(frozen=True)
class _ShrinkPlan:
    actions: tuple[CephStorageActionSpec, ...]
    shrink_candidate_count: int
    lvm_reclaimable_bytes: int
    lvm_shrink_candidate: str
    lvm_shrink_target_bytes: int
    loop_offload_offset: int


def _storage_action_counts(
    actions: Collection[CephStorageActionRecord],
) -> dict[str, int]:
    counts: dict[str, int] = dict.fromkeys(STORAGE_ACTION_PHASES, 0)
    for action in actions:
        counts[action.status.phase] += 1
    return counts


def _storage_actions_in_flight(actions: Collection[CephStorageActionRecord]) -> int:
    counts = _storage_action_counts(actions)
    return counts["Pending"] + counts["Running"]


def _storage_utc(value: datetime | None) -> datetime | None:
    if value is None:
        return None
    if value.tzinfo is None:
        return value.replace(tzinfo=UTC)
    return value.astimezone(UTC)


def _last_storage_shrink_at(
    actions: Collection[CephStorageActionRecord],
) -> datetime | None:
    timestamps = [
        _storage_utc(action.status.finished_at or action.status.started_at)
        for action in actions
        if action.spec.operation in {"retire-loop", "shrink-lvm"}
        and action.status.phase in ("Running", "Succeeded", "Failed")
    ]
    return max((item for item in timestamps if item is not None), default=None)


def _storage_osd_counts(
    osds: Collection[CephStorageOSD],
) -> tuple[int, int, int, int, int]:
    ready = [record for record in osds if record.phase == "Ready"]
    loop_ids = {
        record.ceph_osd_id
        for record in ready
        if record.origin == "loop-fallback" and record.ceph_osd_id is not None
    }
    lvm_ids = {
        record.ceph_osd_id
        for record in ready
        if record.origin == "lvm-pv" and record.ceph_osd_id is not None
    }
    elastic_bytes = sum(
        record.observed_bytes or record.target_bytes
        for record in ready
        if record.origin == "loop-fallback"
    )
    durable_bytes = sum(
        record.observed_bytes or record.target_bytes
        for record in ready
        if record.origin == "lvm-pv"
    )
    return (
        len(loop_ids) + len(lvm_ids),
        len(loop_ids),
        len(lvm_ids),
        elastic_bytes,
        durable_bytes,
    )


def _storage_osd_admission_in_flight(osds: Collection[CephStorageOSD]) -> bool:
    return any(record.phase in STORAGE_OSD_IN_FLIGHT_PHASES for record in osds)


def _storage_osd_id(record: CephStorageOSD) -> int:
    osd_id = record.ceph_osd_id
    if osd_id is None:
        msg = f"storage OSD record {record.name!r} is missing Ceph OSD id"
        raise ValueError(msg)
    return osd_id


def _storage_osd_bytes(record: CephStorageOSD) -> int:
    return record.observed_bytes or record.target_bytes


def _storage_growth_status(
    *,
    policy: CephStorageStateRecord,
    capacity: CephCapacitySnapshot,
    reservations: Collection[CephStorageReservation],
    now: datetime,
) -> CephStoragePolicyStatus:
    spec = policy.spec
    free_bytes = max(0, capacity.total_bytes - capacity.used_bytes)
    previous_rate = (
        policy.policy_status.write_rate_ewma_bytes_per_second
        if policy.policy_status is not None
        else 0.0
    )
    instantaneous_rate = 0.0
    if (
        policy.policy_status is not None
        and policy.policy_status.used_bytes is not None
        and policy.policy_status.last_reconciled_at is not None
    ):
        previous_time = _storage_utc(policy.policy_status.last_reconciled_at)
        if previous_time is not None:
            elapsed = max(0.0, (now - previous_time).total_seconds())
            if elapsed > 0:
                delta = max(0, capacity.used_bytes - policy.policy_status.used_bytes)
                instantaneous_rate = delta / elapsed
    alpha = spec.write_rate_ewma_alpha
    ewma_rate = (alpha * instantaneous_rate) + ((1 - alpha) * previous_rate)
    reserved_bytes = sum(
        reservation.requested_bytes
        for reservation in reservations
        if reservation.is_active(now)
    )
    burst_bytes = math.ceil(
        ewma_rate * spec.burst_window_seconds * spec.burst_multiplier
    )
    headroom_target = max(
        parse_size_bytes(spec.min_headroom),
        math.ceil(capacity.total_bytes * spec.target_headroom_ratio),
        burst_bytes,
    )
    required_free = headroom_target + reserved_bytes
    raw_growth = max(0, required_free - free_bytes)
    min_step = parse_size_bytes(spec.min_growth_step)
    max_growth = parse_size_bytes(spec.max_growth_per_reconcile)
    recommendation = 0
    if raw_growth > 0:
        recommendation = min(
            max_growth,
            _round_up(max(raw_growth, min_step), min_step),
        )
    projected_seconds: float | None = None
    headroom_floor = headroom_target + reserved_bytes
    if ewma_rate > 0 and free_bytes > headroom_floor:
        projected_seconds = (free_bytes - headroom_floor) / ewma_rate
    elif ewma_rate > 0:
        projected_seconds = 0.0
    return CephStoragePolicyStatus(
        free_bytes=free_bytes,
        headroom_target_bytes=headroom_target,
        reserved_bytes=reserved_bytes,
        write_rate_ewma_bytes_per_second=ewma_rate,
        projected_seconds_to_headroom_floor=projected_seconds,
        growth_recommendation_bytes=recommendation,
    )


def _round_up(value: int, step: int) -> int:
    return int(math.ceil(value / step) * step) if value > 0 else 0


async def _mark_stale_actions_failed(
    kube: Kube,
    *,
    actions: Collection[CephStorageActionRecord],
    deadline: Deadline,
) -> bool:
    now = datetime.now(UTC)
    changed = False
    for action in actions:
        if action.status.phase != "Running":
            continue
        started_at = _storage_utc(action.status.started_at)
        if started_at is None:
            continue
        age = (now - started_at).total_seconds()
        if age < STORAGE_ACTION_STALE_SECONDS:
            continue
        await patch_storage_action_status(
            kube,
            action=action,
            status={
                "phase": "Failed",
                "finished_at": now.isoformat(),
                "message": (
                    f"storage action timed out after {int(age)}s without "
                    "node-agent progress"
                ),
                "diagnostics": (
                    "The storage agent may have crashed or lost Kubernetes "
                    "connectivity. Re-run storage doctor and inspect the "
                    "bertrand-ceph-storage-agent logs."
                ),
            },
            deadline=deadline,
        )
        changed = True
    return changed


async def _mark_stale_osds_failed(
    kube: Kube,
    *,
    osd_records: Collection[CephStorageOSD],
    deadline: Deadline,
) -> bool:
    now = datetime.now(UTC)
    changed = False
    for record in osd_records:
        if record.phase not in {"HostPrepared", "Binding", "Expanding", "Shrinking"}:
            continue
        changed_at = (
            _storage_utc(record.phase_changed_at)
            or _storage_utc(record.last_seen_at)
            or _storage_utc(record.created_at)
        )
        if changed_at is None:
            continue
        age = (now - changed_at).total_seconds()
        if age < STORAGE_OSD_STALE_PHASE_SECONDS:
            continue
        await patch_storage_osd_status(
            kube,
            osd=record,
            status={
                "phase": "Failed",
                "last_error": (
                    f"OSD admission timed out after {int(age)}s in phase "
                    f"{record.phase}; inspect Rook OSD pods, the Bertrand "
                    "OSD CSI driver, and this record's PVC binding"
                ),
            },
            deadline=deadline,
        )
        changed = True
    return changed


async def _refresh_osd_readiness(
    kube: Kube,
    *,
    osd_records: Collection[CephStorageOSD],
    deadline: Deadline,
) -> bool:
    try:
        live = await ceph_osds(deadline=deadline)
    except (OSError, TimeoutError):
        return False
    verified = {osd.osd_id for osd in live if osd.up and osd.in_cluster}
    changed = False
    for record in osd_records:
        osd_id = record.ceph_osd_id
        if osd_id is None or record.phase not in {"Binding", "Ready", "Expanding"}:
            continue
        if osd_id in verified and record.phase != "Ready":
            await patch_storage_osd_status(
                kube,
                osd=record,
                status={"phase": "Ready", "last_error": ""},
                deadline=deadline,
            )
            changed = True
        elif osd_id not in verified and record.phase == "Ready":
            await patch_storage_osd_status(
                kube,
                osd=record,
                status={
                    "phase": "Binding",
                    "last_error": (
                        f"Ceph osd.{osd_id} is no longer verified as up and in"
                    ),
                },
                deadline=deadline,
            )
            changed = True
    return changed


async def _reconcile_reservations(
    kube: Kube,
    *,
    reservations: Collection[CephStorageReservation],
    osd_records: Collection[CephStorageOSD],
    growth: CephStoragePolicyStatus,
    now: datetime,
    deadline: Deadline,
) -> bool:
    changed = False
    try:
        health_clean, health_detail, health_status = await ceph_health(
            deadline=deadline
        )
        health_error = "" if health_clean else health_detail or health_status
    except (OSError, TimeoutError) as err:
        health_error = str(err)
    free_bytes = growth.free_bytes or 0
    ready_possible = (
        not health_error
        and not _storage_osd_admission_in_flight(osd_records)
        and free_bytes >= growth.headroom_target_bytes + growth.reserved_bytes
    )
    for reservation in reservations:
        if reservation.phase in {"Released", "Expired", "Failed"}:
            continue
        if reservation.expires_at_utc() <= now:
            await patch_storage_reservation_status(
                kube,
                reservation=reservation,
                status={
                    "phase": "Expired",
                    "released_at": now.isoformat(),
                    "observed_free_bytes": free_bytes,
                    "last_error": "reservation expired before it was released",
                },
                deadline=deadline,
            )
            changed = True
            continue
        if ready_possible and reservation.phase != "Ready":
            await patch_storage_reservation_status(
                kube,
                reservation=reservation,
                status={
                    "phase": "Ready",
                    "ready_at": now.isoformat(),
                    "observed_free_bytes": free_bytes,
                    "last_error": "",
                },
                deadline=deadline,
            )
            changed = True
        elif not ready_possible and reservation.phase == "Pending":
            blockers: list[str] = []
            if health_error:
                blockers.append(f"Ceph health is not clean: {health_error}")
            if _storage_osd_admission_in_flight(osd_records):
                blockers.append("OSD admission or retirement is still in flight")
            required_free = growth.headroom_target_bytes + growth.reserved_bytes
            if free_bytes < required_free:
                blockers.append("free capacity is below reservation-adjusted headroom")
            await patch_storage_reservation_status(
                kube,
                reservation=reservation,
                status={
                    "phase": "Pending",
                    "observed_free_bytes": free_bytes,
                    "last_error": "; ".join(blockers),
                },
                deadline=deadline,
            )
            changed = True
    return changed


def _blocked_storage_targets(
    *,
    actions: Collection[CephStorageActionRecord],
    osd_records: Collection[CephStorageOSD],
    now: datetime,
) -> set[str]:
    blocked: set[str] = set()
    for record in osd_records:
        if record.phase != "Failed":
            continue
        failed_at = (
            _storage_utc(record.phase_changed_at)
            or _storage_utc(record.last_seen_at)
            or _storage_utc(record.created_at)
        )
        if (
            failed_at is not None
            and (now - failed_at).total_seconds()
            < STORAGE_TARGET_RETRY_COOLDOWN_SECONDS
        ):
            blocked.add(record.name)
    for action in actions:
        target_name = action.spec.storage_osd_name
        if (
            action.status.phase != "Failed"
            or action.spec.operation not in {"expand-lvm", "expand-loop", "shrink-lvm"}
            or not target_name
        ):
            continue
        failed_at = _storage_utc(action.status.finished_at or action.status.started_at)
        if (
            failed_at is not None
            and (now - failed_at).total_seconds()
            < STORAGE_TARGET_RETRY_COOLDOWN_SECONDS
        ):
            blocked.add(target_name)
    return blocked


def _discover_storage_targets(
    *,
    reports: Collection[CephStorageNodeReport],
    osd_records: Collection[CephStorageOSD],
    ready_nodes: Collection[str],
    blocked_targets: Collection[str],
    min_growth_bytes: int,
    now: datetime,
) -> tuple[list[_StorageTarget], list[_StorageTarget]]:
    active_osds = [
        record
        for record in osd_records
        if record.phase not in {"Failed", "Retired", "Retiring"}
    ]
    ready = frozenset(ready_nodes)
    blocked = frozenset(blocked_targets)
    lvm_by_pv = {
        (record.host_id, record.pv_uuid): record
        for record in active_osds
        if record.origin == "lvm-pv" and record.pv_uuid
    }
    loop_by_host = {
        record.host_id: record
        for record in active_osds
        if record.origin == "loop-fallback"
    }
    lvm_targets: list[_StorageTarget] = []
    loop_targets: list[_StorageTarget] = []
    for report in reports:
        if report.node_name not in ready:
            continue
        heartbeat = _storage_utc(report.heartbeat_at)
        if (
            heartbeat is None
            or (now - heartbeat).total_seconds() > STORAGE_NODE_REPORT_MAX_AGE_SECONDS
        ):
            continue
        for pv in report.lvm_pv_inventory:
            if pv.pv_free_bytes < min_growth_bytes:
                continue
            existing = lvm_by_pv.get((report.host_id, pv.pv_uuid))
            target_name = (
                existing.name
                if existing is not None
                else storage_lvm_osd_name(report.host_id, pv.pv_uuid)
            )
            if target_name in blocked:
                continue
            lvm_targets.append(
                _StorageTarget(
                    node_name=report.node_name,
                    host_id=report.host_id,
                    operation="expand-lvm",
                    pv_name=pv.pv_name,
                    lv_name=(
                        existing.lv_name
                        if existing is not None and existing.lv_name
                        else ""
                    ),
                    storage_osd_name=target_name,
                    current_bytes=existing.target_bytes if existing is not None else 0,
                    available_bytes=pv.pv_free_bytes,
                    target_bytes=(
                        existing.target_bytes + min_growth_bytes
                        if existing is not None
                        else min_growth_bytes
                    ),
                )
            )
        if report.free_bytes < min_growth_bytes:
            continue
        existing = loop_by_host.get(report.host_id)
        target_name = (
            existing.name
            if existing is not None
            else storage_loop_osd_name(report.host_id)
        )
        if target_name in blocked:
            continue
        loop_targets.append(
            _StorageTarget(
                node_name=report.node_name,
                host_id=report.host_id,
                operation="expand-loop",
                storage_osd_name=target_name,
                current_bytes=existing.target_bytes if existing is not None else 0,
                available_bytes=report.free_bytes,
                target_bytes=(
                    existing.target_bytes + min_growth_bytes
                    if existing is not None
                    else min_growth_bytes
                ),
            )
        )
    lvm_targets.sort(
        key=lambda target: (
            0 if not target.lv_name else 1,
            target.node_name,
            target.pv_name,
        )
    )
    loop_targets.sort(key=lambda target: (target.node_name, target.storage_osd_name))
    return lvm_targets, loop_targets


def _storage_target_action(
    *,
    policy_generation: int,
    target: _StorageTarget,
    target_bytes: int,
    reason: str,
) -> CephStorageActionSpec:
    return CephStorageActionSpec(
        policy_generation=policy_generation,
        operation=target.operation,
        node_name=target.node_name,
        host_id=target.host_id,
        target_bytes=target_bytes,
        pv_name=target.pv_name or None,
        lv_name=target.lv_name or None,
        storage_osd_name=target.storage_osd_name,
        reason=reason,
    )


def _plan_growth_actions(
    *,
    policy: CephStorageStateRecord,
    capacity: CephCapacitySnapshot,
    growth: CephStoragePolicyStatus,
    eligible_targets: Collection[_StorageTarget],
    min_growth_bytes: int,
    in_flight: int,
    admission_in_flight: bool,
) -> list[CephStorageActionSpec]:
    spec = policy.spec
    if not spec.enabled or not eligible_targets or admission_in_flight:
        return []
    budget = spec.max_actions_per_reconcile - in_flight
    if budget <= 0:
        return []

    targets = list(eligible_targets)
    if capacity.total_bytes <= 0:
        target = targets[0]
        allocation = min(
            max(min_growth_bytes, growth.growth_recommendation_bytes),
            target.available_bytes,
        )
        return [
            _storage_target_action(
                policy_generation=policy.generation,
                target=target,
                target_bytes=target.current_bytes + allocation,
                reason="cluster has no usable OSD capacity yet",
            )
        ]

    if growth.growth_recommendation_bytes <= 0:
        return []

    planned: list[CephStorageActionSpec] = []
    remaining = growth.growth_recommendation_bytes
    for target in targets:
        if remaining <= 0 or len(planned) >= budget:
            break
        allocation = min(remaining, target.available_bytes)
        if allocation <= 0:
            continue
        allocation = max(allocation, min_growth_bytes)
        allocation = min(allocation, target.available_bytes)
        if allocation <= 0:
            continue
        planned.append(
            _storage_target_action(
                policy_generation=policy.generation,
                target=target,
                target_bytes=target.current_bytes + allocation,
                reason=(
                    "free Ceph capacity is below adaptive headroom target "
                    f"({growth.free_bytes} < "
                    f"{growth.headroom_target_bytes + growth.reserved_bytes})"
                ),
            )
        )
        remaining -= allocation
    return planned


def _plan_lvm_coverage_actions(
    *,
    policy: CephStorageStateRecord,
    capacity: CephCapacitySnapshot,
    lvm_targets: Collection[_StorageTarget],
    in_flight: int,
    admission_in_flight: bool,
) -> list[CephStorageActionSpec]:
    spec = policy.spec
    if (
        capacity.total_bytes <= 0
        or not spec.enabled
        or in_flight != 0
        or admission_in_flight
    ):
        return []

    min_lvm_size = parse_size_bytes(spec.min_lvm_osd_size)
    planned: list[CephStorageActionSpec] = []
    for target in lvm_targets:
        if target.current_bytes > 0:
            continue
        allocation = min(min_lvm_size, target.available_bytes)
        if allocation < min_lvm_size:
            continue
        planned.append(
            _storage_target_action(
                policy_generation=policy.generation,
                target=target,
                target_bytes=allocation,
                reason="usable Bertrand LVM PV is missing steady-state OSD coverage",
            )
        )
        if len(planned) >= spec.max_actions_per_reconcile:
            break
    return planned


def _empty_shrink_plan(loop_offload_offset: int) -> _ShrinkPlan:
    return _ShrinkPlan(
        actions=(),
        shrink_candidate_count=0,
        lvm_reclaimable_bytes=0,
        lvm_shrink_candidate="",
        lvm_shrink_target_bytes=0,
        loop_offload_offset=loop_offload_offset,
    )


async def _plan_shrink_actions(
    *,
    policy: CephStorageStateRecord,
    actions: Collection[CephStorageActionRecord],
    osd_records: Collection[CephStorageOSD],
    lvm_targets: Sequence[_StorageTarget],
    capacity: CephCapacitySnapshot,
    growth: CephStoragePolicyStatus,
    min_growth_bytes: int,
    in_flight: int,
    admission_in_flight: bool,
    loop_offload_offset: int,
    now: datetime,
    deadline: Deadline,
) -> _ShrinkPlan:
    spec = policy.spec
    if not spec.enabled or not spec.shrink_enabled:
        return _empty_shrink_plan(loop_offload_offset)

    health_clean, _health_detail, _health_status = await ceph_health(deadline=deadline)
    if not health_clean:
        return _empty_shrink_plan(loop_offload_offset)

    live_osds = await ceph_osds(deadline=deadline)
    live_osd_ids = {
        osd.osd_id for osd in live_osds if osd.up and osd.in_cluster and osd.node_name
    }
    shrink_candidates = [
        record
        for record in osd_records
        if record.origin == "loop-fallback"
        and record.phase == "Ready"
        and record.ceph_osd_id in live_osd_ids
    ]
    lvm_shrink_candidates = [
        record
        for record in osd_records
        if record.origin == "lvm-pv"
        and record.phase == "Ready"
        and record.ceph_osd_id in live_osd_ids
    ]

    selected_loop: CephStorageOSD | None = None
    loop_groups: dict[str, list[CephStorageOSD]] = {}
    for candidate in shrink_candidates:
        loop_groups.setdefault(candidate.node_name, []).append(candidate)
    if loop_groups:
        loop_node = min(loop_groups, key=lambda item: (-len(loop_groups[item]), item))
        selected_loop = max(
            loop_groups[loop_node],
            key=lambda item: (
                _storage_utc(item.created_at) or datetime.min.replace(tzinfo=UTC),
                _storage_osd_id(item),
            ),
        )

    selected_lvm: CephStorageOSD | None = None
    lvm_reclaimable_bytes = 0
    lvm_shrink_candidate = ""
    lvm_shrink_target_bytes = 0
    if lvm_shrink_candidates:
        min_lvm_size = parse_size_bytes(spec.min_lvm_osd_size)
        min_reclaim = parse_size_bytes(spec.lvm_shrink_min_reclaim)
        desired_total = max(
            capacity.used_bytes + growth.headroom_target_bytes + growth.reserved_bytes,
            min_lvm_size * len(lvm_shrink_candidates),
        )
        lvm_shrink_target_bytes = max(
            min_lvm_size,
            _round_up(desired_total, len(lvm_shrink_candidates))
            // len(lvm_shrink_candidates),
        )
        reclaimable: list[tuple[int, CephStorageOSD]] = []
        for candidate in lvm_shrink_candidates:
            reclaim = _storage_osd_bytes(candidate) - lvm_shrink_target_bytes
            if reclaim >= min_reclaim:
                reclaimable.append((reclaim, candidate))
        if reclaimable:
            lvm_reclaimable_bytes = sum(item[0] for item in reclaimable)
            _, selected_lvm = max(
                reclaimable,
                key=lambda item: (
                    item[0],
                    _storage_osd_bytes(item[1]),
                    _storage_utc(item[1].created_at)
                    or datetime.min.replace(tzinfo=UTC),
                    item[1].name,
                ),
            )
            lvm_shrink_candidate = selected_lvm.name

    planned: list[CephStorageActionSpec] = []
    next_loop_offload_offset = loop_offload_offset
    selected_loop_id = _storage_osd_id(selected_loop) if selected_loop else 0
    selected_loop_bytes = _storage_osd_bytes(selected_loop) if selected_loop else 0
    if in_flight == 0 and capacity.total_bytes > 0 and selected_loop is not None:
        projected_total = capacity.total_bytes - selected_loop_bytes
        if projected_total > 0:
            projected_ratio = capacity.used_bytes / projected_total
            if projected_ratio <= spec.shrink_target_watermark:
                planned.append(
                    CephStorageActionSpec(
                        policy_generation=policy.generation,
                        operation="retire-loop",
                        node_name=selected_loop.node_name,
                        host_id=selected_loop.host_id,
                        osd_id=selected_loop_id,
                        reason=(
                            "LVM-backed capacity can absorb loop fallback "
                            f"osd.{selected_loop_id}; projected usage after "
                            f"retirement is {projected_ratio:.2%}"
                        ),
                    )
                )

        if not planned and lvm_targets:
            target_total = math.ceil(capacity.used_bytes / spec.shrink_target_watermark)
            missing = max(0, target_total - max(0, projected_total))
            desired = max(1, math.ceil(missing / min_growth_bytes))
            budget = spec.max_actions_per_reconcile - in_flight
            count = max(0, min(desired, budget, len(lvm_targets)))
            for index in range(count):
                target = lvm_targets[(loop_offload_offset + index) % len(lvm_targets)]
                planned.append(
                    _storage_target_action(
                        policy_generation=policy.generation,
                        target=target,
                        target_bytes=target.target_bytes or min_growth_bytes,
                        reason=(
                            "LVM capacity is available while loop fallback "
                            f"osd.{selected_loop_id} is active"
                        ),
                    )
                )
            if count:
                next_loop_offload_offset = (loop_offload_offset + count) % len(
                    lvm_targets
                )

    last_shrink_at = _last_storage_shrink_at(actions)
    shrink_on_cooldown = (
        last_shrink_at is not None
        and (now - last_shrink_at).total_seconds() < spec.shrink_cooldown_seconds
    )
    if (
        not planned
        and capacity.used_ratio < spec.low_watermark
        and in_flight == 0
        and not shrink_on_cooldown
        and selected_loop is not None
    ):
        projected_total = capacity.total_bytes - selected_loop_bytes
        if projected_total > 0:
            projected_ratio = capacity.used_bytes / projected_total
            if projected_ratio <= spec.shrink_target_watermark:
                planned.append(
                    CephStorageActionSpec(
                        policy_generation=policy.generation,
                        operation="retire-loop",
                        node_name=selected_loop.node_name,
                        host_id=selected_loop.host_id,
                        osd_id=selected_loop_id,
                        reason=(
                            "cluster usage "
                            f"{capacity.used_ratio:.2%} <= low watermark "
                            f"{spec.low_watermark:.2%}; projected usage after "
                            f"removing osd.{selected_loop_id} is "
                            f"{projected_ratio:.2%}"
                        ),
                    )
                )

    if (
        not planned
        and growth.growth_recommendation_bytes <= 0
        and growth.reserved_bytes <= 0
        and not shrink_candidates
        and in_flight == 0
        and not admission_in_flight
        and not shrink_on_cooldown
        and selected_lvm is not None
        and lvm_reclaimable_bytes > 0
    ):
        selected_lvm_id = _storage_osd_id(selected_lvm)
        selected_lvm_bytes = _storage_osd_bytes(selected_lvm)
        planned.append(
            CephStorageActionSpec(
                policy_generation=policy.generation,
                operation="shrink-lvm",
                node_name=selected_lvm.node_name,
                host_id=selected_lvm.host_id,
                osd_id=selected_lvm_id,
                target_bytes=lvm_shrink_target_bytes,
                pv_name=selected_lvm.pv_name,
                lv_name=selected_lvm.lv_name,
                storage_osd_name=selected_lvm.name,
                reason=(
                    "LVM-backed raw capacity exceeds adaptive headroom; "
                    f"drain/recreate osd.{selected_lvm_id} from "
                    f"{selected_lvm_bytes} to {lvm_shrink_target_bytes} bytes"
                ),
            )
        )

    return _ShrinkPlan(
        actions=tuple(planned),
        shrink_candidate_count=len(shrink_candidates),
        lvm_reclaimable_bytes=lvm_reclaimable_bytes,
        lvm_shrink_candidate=lvm_shrink_candidate,
        lvm_shrink_target_bytes=lvm_shrink_target_bytes,
        loop_offload_offset=next_loop_offload_offset,
    )


async def _plan_storage_actions(
    kube: Kube,
    *,
    policy: CephStorageStateRecord,
    actions: Collection[CephStorageActionRecord],
    reports: Collection[CephStorageNodeReport],
    osd_records: Collection[CephStorageOSD],
    capacity: CephCapacitySnapshot,
    growth: CephStoragePolicyStatus,
    loop_offload_offset: int,
    deadline: Deadline,
) -> tuple[list[CephStorageActionSpec], CephStoragePolicyStatus, int]:
    now = datetime.now(UTC)
    min_growth_bytes = parse_size_bytes(policy.spec.min_growth_step)
    ready_nodes = await _ready_storage_nodes(kube, deadline=deadline)
    blocked_targets = _blocked_storage_targets(
        actions=actions,
        osd_records=osd_records,
        now=now,
    )
    lvm_targets, loop_targets = _discover_storage_targets(
        reports=reports,
        osd_records=osd_records,
        ready_nodes=ready_nodes,
        blocked_targets=blocked_targets,
        min_growth_bytes=min_growth_bytes,
        now=now,
    )
    eligible_nodes = [*lvm_targets, *loop_targets]
    in_flight = _storage_actions_in_flight(actions)
    admission_in_flight = _storage_osd_admission_in_flight(osd_records)

    planned = _plan_growth_actions(
        policy=policy,
        capacity=capacity,
        growth=growth,
        eligible_targets=eligible_nodes,
        min_growth_bytes=min_growth_bytes,
        in_flight=in_flight,
        admission_in_flight=admission_in_flight,
    )
    if not planned:
        planned = _plan_lvm_coverage_actions(
            policy=policy,
            capacity=capacity,
            lvm_targets=lvm_targets,
            in_flight=in_flight,
            admission_in_flight=admission_in_flight,
        )

    shrink = _empty_shrink_plan(loop_offload_offset)
    if not planned:
        shrink = await _plan_shrink_actions(
            policy=policy,
            actions=actions,
            osd_records=osd_records,
            lvm_targets=lvm_targets,
            capacity=capacity,
            growth=growth,
            min_growth_bytes=min_growth_bytes,
            in_flight=in_flight,
            admission_in_flight=admission_in_flight,
            loop_offload_offset=loop_offload_offset,
            now=now,
            deadline=deadline,
        )
        planned.extend(shrink.actions)

    status = _storage_policy_status(
        capacity=capacity,
        actions=actions,
        osd_records=osd_records,
        growth=growth,
        missing_lvm_osd_pvs=sum(
            1 for target in lvm_targets if target.current_bytes <= 0
        ),
        shrink_candidate_count=shrink.shrink_candidate_count,
        lvm_reclaimable_bytes=shrink.lvm_reclaimable_bytes,
        lvm_shrink_candidate=shrink.lvm_shrink_candidate,
        lvm_shrink_target_bytes=shrink.lvm_shrink_target_bytes,
    )
    return planned, status, shrink.loop_offload_offset


def _storage_policy_status(
    *,
    capacity: CephCapacitySnapshot,
    actions: Collection[CephStorageActionRecord],
    osd_records: Collection[CephStorageOSD],
    growth: CephStoragePolicyStatus,
    missing_lvm_osd_pvs: int,
    shrink_candidate_count: int,
    lvm_reclaimable_bytes: int,
    lvm_shrink_candidate: str,
    lvm_shrink_target_bytes: int,
) -> CephStoragePolicyStatus:
    counts = _storage_action_counts(actions)
    managed, loop, lvm, elastic_bytes, durable_bytes = _storage_osd_counts(osd_records)
    return growth.model_copy(
        update={
            "total_bytes": capacity.total_bytes,
            "used_bytes": capacity.used_bytes,
            "used_ratio": capacity.used_ratio,
            "pending_actions": counts.get("Pending", 0),
            "running_actions": counts.get("Running", 0),
            "succeeded_actions": counts.get("Succeeded", 0),
            "failed_actions": counts.get("Failed", 0),
            "managed_osds": managed,
            "loop_osds": loop,
            "lvm_osds": lvm,
            "elastic_bytes": elastic_bytes,
            "durable_bytes": durable_bytes,
            "lvm_preferred": lvm > 0,
            "shrink_candidates": shrink_candidate_count,
            "missing_lvm_osd_pvs": missing_lvm_osd_pvs,
            "lvm_reclaimable_bytes": lvm_reclaimable_bytes,
            "lvm_shrink_candidate": lvm_shrink_candidate,
            "lvm_shrink_target_bytes": lvm_shrink_target_bytes,
            "last_shrink_at": _last_storage_shrink_at(actions),
            "last_reconciled_at": datetime.now(UTC),
            "last_error": "",
        },
    )


async def _read_ceph_capacity(
    *,
    deadline: Deadline,
) -> tuple[CephCapacitySnapshot, str]:
    try:
        return await ceph_df(deadline=deadline), ""
    except (OSError, TimeoutError) as err:
        return (
            CephCapacitySnapshot(total_bytes=0, used_bytes=0, used_ratio=1.0),
            str(err),
        )


async def _reconcile_storage_controller(
    kube: Kube,
    *,
    deadline: Deadline,
    loop_offload_offset: int,
) -> tuple[float, int]:
    policy = await read_storage_state(kube, deadline=deadline)
    actions = await STORAGE_ACTION_RESOURCE.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        deadline=deadline,
    )
    reservations = sorted(
        policy.status.reservations.values(),
        key=lambda item: item.name,
    )
    reports = sorted(policy.status.nodes.values(), key=lambda item: item.name)
    osd_records = sorted(policy.status.osds.values(), key=lambda item: item.name)

    if await _mark_stale_actions_failed(
        kube,
        actions=actions,
        deadline=deadline,
    ):
        actions = await STORAGE_ACTION_RESOURCE.list(
            kube,
            namespace=BERTRAND_NAMESPACE,
            deadline=deadline,
        )
    if await _mark_stale_osds_failed(
        kube,
        osd_records=osd_records,
        deadline=deadline,
    ):
        storage = await read_storage_state(kube, deadline=deadline)
        osd_records = sorted(storage.status.osds.values(), key=lambda item: item.name)
    await patch_rook_device_sets(
        kube,
        records=osd_records,
        deadline=deadline,
    )
    capacity, capacity_error = await _read_ceph_capacity(deadline=deadline)
    if await _refresh_osd_readiness(
        kube,
        osd_records=osd_records,
        deadline=deadline,
    ):
        storage = await read_storage_state(kube, deadline=deadline)
        osd_records = sorted(storage.status.osds.values(), key=lambda item: item.name)
    now = datetime.now(UTC)
    growth = _storage_growth_status(
        policy=policy,
        capacity=capacity,
        reservations=reservations,
        now=now,
    )
    if await _reconcile_reservations(
        kube,
        reservations=reservations,
        osd_records=osd_records,
        growth=growth,
        now=now,
        deadline=deadline,
    ):
        storage = await read_storage_state(kube, deadline=deadline)
        reservations = sorted(
            storage.status.reservations.values(),
            key=lambda item: item.name,
        )
        growth = _storage_growth_status(
            policy=policy,
            capacity=capacity,
            reservations=reservations,
            now=datetime.now(UTC),
        )

    planned, status, next_loop_offload_offset = await _plan_storage_actions(
        kube,
        policy=policy,
        actions=actions,
        reports=reports,
        osd_records=osd_records,
        capacity=capacity,
        growth=growth,
        loop_offload_offset=loop_offload_offset,
        deadline=deadline,
    )

    if planned:
        await create_storage_actions(
            kube,
            actions=planned,
            deadline=deadline,
        )
        actions = await STORAGE_ACTION_RESOURCE.list(
            kube,
            namespace=BERTRAND_NAMESPACE,
            deadline=deadline,
        )
        storage = await read_storage_state(kube, deadline=deadline)
        osd_records = sorted(storage.status.osds.values(), key=lambda item: item.name)
        shrink_candidate_count = (
            0
            if any(
                action.operation in {"retire-loop", "shrink-lvm"} for action in planned
            )
            else status.shrink_candidates
        )
        status = _storage_policy_status(
            capacity=capacity,
            actions=actions,
            osd_records=osd_records,
            growth=status,
            missing_lvm_osd_pvs=status.missing_lvm_osd_pvs,
            shrink_candidate_count=shrink_candidate_count,
            lvm_reclaimable_bytes=status.lvm_reclaimable_bytes,
            lvm_shrink_candidate=status.lvm_shrink_candidate,
            lvm_shrink_target_bytes=status.lvm_shrink_target_bytes,
        )

    storage = await read_storage_state(kube, deadline=deadline)
    await STORAGE_STATE_RESOURCE.patch_status(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=storage.name,
        status=storage.status.model_copy(
            update={
                "policy": status.model_copy(
                    update={"observed_generation": policy.generation}
                )
            },
        ),
        deadline=deadline,
    )
    if capacity_error and not planned:
        raise OSError(capacity_error)
    return float(policy.spec.reconcile_interval_seconds), next_loop_offload_offset


async def _maybe_repository_volume_gc(
    kube: Kube,
    *,
    clock: MaintenanceClock,
    deadline: Deadline,
) -> None:
    now = datetime.now(UTC)
    pass_deadline = clock.pass_deadline(
        now,
        deadline=deadline,
        budget=REPOSITORY_VOLUME_GC_TIMEOUT_SECONDS,
    )
    if pass_deadline is None:
        return
    try:
        next_run = await next_repository_volume_gc_time(
            kube,
            deadline=pass_deadline,
        )
        if next_run is None:
            clock.schedule_after(REPOSITORY_VOLUME_GC_EMPTY_CHECK_SECONDS)
            return
        if next_run > now:
            clock.schedule_at(next_run)
            return
        await gc_repository_volumes(kube, deadline=pass_deadline)
        clock.schedule_after(REPOSITORY_VOLUME_GC_READY_CHECK_SECONDS)
    except (OSError, TimeoutError, ValueError) as err:
        clock.schedule_after(REPOSITORY_VOLUME_GC_FAILURE_RETRY_SECONDS)
        print(
            f"bertrand: warning: repository volume garbage collection failed: {err}",
            file=sys.stderr,
        )


async def _maybe_repository_snapshot_maintenance(
    kube: Kube,
    *,
    clock: MaintenanceClock,
    deadline: Deadline,
) -> None:
    now = datetime.now(UTC)
    pass_deadline = clock.pass_deadline(
        now,
        deadline=deadline,
        budget=REPOSITORY_SNAPSHOT_TIMEOUT_SECONDS,
    )
    if pass_deadline is None:
        return
    try:
        next_run = await next_repository_snapshot_time(
            kube,
            deadline=pass_deadline,
        )
        if next_run is None:
            clock.schedule_after(REPOSITORY_SNAPSHOT_EMPTY_CHECK_SECONDS)
            return
        if next_run > now:
            clock.schedule_at(next_run)
            return
        await maintain_repository_snapshots(kube, deadline=pass_deadline)
        clock.schedule_after(REPOSITORY_SNAPSHOT_READY_CHECK_SECONDS)
    except (OSError, TimeoutError, ValueError) as err:
        clock.schedule_after(REPOSITORY_SNAPSHOT_FAILURE_RETRY_SECONDS)
        print(
            f"bertrand: warning: repository snapshot maintenance failed: {err}",
            file=sys.stderr,
        )


async def _patch_storage_controller_error(
    kube: Kube,
    *,
    error: str,
    deadline: Deadline,
) -> None:
    storage = await read_storage_state(kube, deadline=deadline)
    policy = CephStoragePolicyStatus.model_validate(
        {
            "observedGeneration": storage.generation,
            "total_bytes": None,
            "used_bytes": None,
            "used_ratio": None,
            "free_bytes": None,
            "headroom_target_bytes": 0,
            "reserved_bytes": 0,
            "write_rate_ewma_bytes_per_second": 0.0,
            "projected_seconds_to_headroom_floor": None,
            "growth_recommendation_bytes": 0,
            "pending_actions": 0,
            "running_actions": 0,
            "succeeded_actions": 0,
            "failed_actions": 0,
            "managed_osds": 0,
            "loop_osds": 0,
            "lvm_osds": 0,
            "elastic_bytes": 0,
            "durable_bytes": 0,
            "lvm_preferred": False,
            "shrink_candidates": 0,
            "missing_lvm_osd_pvs": 0,
            "lvm_reclaimable_bytes": 0,
            "lvm_shrink_candidate": "",
            "lvm_shrink_target_bytes": 0,
            "last_shrink_at": None,
            "last_reconciled_at": datetime.now(UTC).isoformat(),
            "last_error": error,
        }
    )
    await STORAGE_STATE_RESOURCE.patch_status(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=storage.name,
        status=storage.status.model_copy(update={"policy": policy}),
        deadline=deadline,
    )


async def run_ceph_storage_controller(*, deadline: Deadline = NO_DEADLINE) -> None:
    """Run the Ceph storage controller loop until cancelled or timed out.

    Parameters
    ----------
    deadline : Deadline, optional
        Maximum controller runtime.

    Raises
    ------
    TimeoutError
        If the loop exceeds the deadline.
    asyncio.CancelledError
        If the surrounding task is cancelled.
    """
    wake = asyncio.Event()
    wake.set()
    loop_offload_offset = 0
    repository_volume_gc = MaintenanceClock()
    repository_snapshot = MaintenanceClock()
    with Kube.internal() as kube:
        async with asyncio.TaskGroup() as group:
            for client, context in (
                (STORAGE_STATE_RESOURCE, STORAGE_STATE_PLURAL),
                (STORAGE_ACTION_RESOURCE, STORAGE_ACTION_PLURAL),
            ):
                group.create_task(
                    _watch_storage_resource(
                        kube,
                        client=client,
                        wake=wake,
                        deadline=deadline,
                        context=context,
                    )
                )
            interval = STORAGE_CONTROLLER_DEFAULT_RECONCILE_SECONDS
            while True:
                if not wake.is_set():
                    remaining = deadline.remaining
                    if remaining <= 0:
                        return
                    wait_timeout = min(interval, remaining)
                    with suppress(TimeoutError):
                        await asyncio.wait_for(wake.wait(), timeout=wait_timeout)
                wake.clear()
                interval = STORAGE_CONTROLLER_DEFAULT_RECONCILE_SECONDS
                error: str | None = None
                try:
                    interval, loop_offload_offset = await _reconcile_storage_controller(
                        kube,
                        deadline=deadline,
                        loop_offload_offset=loop_offload_offset,
                    )
                except asyncio.CancelledError:
                    raise
                except TimeoutError as err:
                    if deadline.remaining <= 0:
                        raise
                    error = str(err)
                except (OSError, ValueError, RuntimeError) as err:
                    error = str(err)
                if error is not None:
                    with suppress(OSError, TimeoutError, ValueError):
                        await _patch_storage_controller_error(
                            kube,
                            error=error,
                            deadline=deadline,
                        )
                await _maybe_repository_volume_gc(
                    kube,
                    clock=repository_volume_gc,
                    deadline=deadline,
                )
                await _maybe_repository_snapshot_maintenance(
                    kube,
                    clock=repository_snapshot,
                    deadline=deadline,
                )


def main(argv: list[str] | None = None) -> int:
    """Entry point for control plane container role dispatch.

    Parameters
    ----------
    argv : list[str] | None, optional
        Command-line arguments without the executable name. If `None`, use
        `sys.argv[1:]`.

    Returns
    -------
    int
        Process exit code.
    """
    if argv is None:
        argv = sys.argv[1:]
    role = argv[0].strip().lower() if argv else "controller"
    if role not in {"controller", "agent"}:
        print(
            "usage: python -m bertrand.env.kube.ceph.storage [controller|agent]",
            file=sys.stderr,
        )
        return 2
    with asyncio.Runner() as runner:
        if role == "controller":
            runner.run(run_ceph_storage_controller(deadline=NO_DEADLINE))
        else:
            from bertrand.env.kube.ceph.agent import CephStorageAgent

            runner.run(CephStorageAgent().run(deadline=NO_DEADLINE))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
