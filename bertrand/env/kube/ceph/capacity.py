"""Ceph capacity policy records and grow/shrink planning."""

from __future__ import annotations

import asyncio
import hashlib
import math
import uuid
from contextlib import asynccontextmanager, suppress
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta
from typing import TYPE_CHECKING, Annotated, Any, Literal

from pydantic import (
    BaseModel,
    ConfigDict,
    Field,
    PositiveInt,
    field_validator,
    model_validator,
)

from bertrand.env.git import BERTRAND_ENV, BERTRAND_NAMESPACE, Deadline
from bertrand.env.kube.ceph.api import CephCapacitySnapshot, CephOSD, parse_size_bytes
from bertrand.env.kube.custom_object import (
    CustomObjectMetadata,
    CustomObjectResource,
)

if TYPE_CHECKING:
    from collections.abc import AsyncIterator, Collection, Mapping

    from bertrand.env.kube.api.client import Kube

CEPH_CAPACITY_GROUP = "ceph.bertrand.dev"
CEPH_CAPACITY_VERSION = "v1alpha1"
STORAGE_STATE_KIND = "CephStorageState"
STORAGE_STATE_PLURAL = "cephstoragestates"
STORAGE_ACTION_KIND = "CephStorageAction"
STORAGE_ACTION_PLURAL = "cephstorageactions"
STORAGE_STATE_NAME = "default"
STORAGE_CONTROLLER_LABEL = "bertrand.dev/ceph-storage-controller"
STORAGE_CONTROLLER_LABEL_VALUE = "v1"
STORAGE_OSD_LABEL = "bertrand.dev/ceph-storage-osd"
STORAGE_OSD_LABEL_VALUE = "v1"
STORAGE_OSD_NAME_LABEL = "bertrand.dev/ceph-storage-osd-name"
STORAGE_CONTROLLER_LABELS = {
    BERTRAND_ENV: "1",
    STORAGE_CONTROLLER_LABEL: STORAGE_CONTROLLER_LABEL_VALUE,
}
STORAGE_ACTION_PHASES = ("Pending", "Running", "Succeeded", "Failed")
STORAGE_RESERVATION_PHASES = (
    "Pending",
    "Ready",
    "Released",
    "Expired",
    "Failed",
)
STORAGE_OSD_PHASES = (
    "Pending",
    "Preparing",
    "HostPrepared",
    "Binding",
    "Ready",
    "Expanding",
    "Shrinking",
    "Failed",
    "Retiring",
    "Retired",
)
STORAGE_OSD_IN_FLIGHT_PHASES = frozenset(
    {
        "Pending",
        "Preparing",
        "HostPrepared",
        "Binding",
        "Expanding",
        "Shrinking",
        "Retiring",
    }
)
STORAGE_NODE_REPORT_MAX_AGE_SECONDS = 120
STORAGE_TARGET_RETRY_COOLDOWN_SECONDS = 300
STORAGE_OSD_STALE_PHASE_SECONDS = 1800
STORAGE_ACTION_STALE_SECONDS = 1800


def _deadline_from_budget(seconds: float) -> Deadline:
    if seconds <= 0:
        return Deadline(
            expires_at=asyncio.get_running_loop().time(),
            timeout=seconds,
        )
    return Deadline.from_timeout(seconds, message="")


type _Watermark = Annotated[float, Field(gt=0.0, lt=1.0)]
type _Size = Annotated[str, Field(pattern=r"^[1-9][0-9]*[MGT]$")]
type StorageActionOperation = Literal[
    "expand-lvm", "expand-loop", "retire-loop", "shrink-lvm"
]
type StorageActionPhase = Literal["Pending", "Running", "Succeeded", "Failed"]
type StorageReservationPhase = Literal[
    "Pending", "Ready", "Released", "Expired", "Failed"
]
type StorageOSDOrigin = Literal["lvm-pv", "loop-fallback"]
type StorageOSDQuality = Literal["elastic", "durable"]
type StorageOSDPhase = Literal[
    "Pending",
    "Preparing",
    "HostPrepared",
    "Binding",
    "Ready",
    "Expanding",
    "Shrinking",
    "Failed",
    "Retiring",
    "Retired",
]


def _hash_label(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()[:32]


def storage_lvm_osd_name(host_id: str, pv_uuid: str) -> str:
    """Return the deterministic inventory name for one PV-pinned LVM OSD.

    Parameters
    ----------
    host_id : str
        Durable Bertrand host UUID that owns the OSD substrate.
    pv_uuid : str
        LVM physical volume UUID.

    Returns
    -------
    str
        Kubernetes custom-object-safe OSD inventory name.

    Raises
    ------
    ValueError
        If either input is empty.
    """
    host_id = host_id.strip()
    pv_uuid = pv_uuid.strip()
    if not host_id or not pv_uuid:
        msg = "LVM OSD names require non-empty host ID and PV UUID"
        raise ValueError(msg)
    return f"bertrand-osd-lvm-{_hash_label(f'{host_id}:{pv_uuid}')[:32]}"


def storage_loop_osd_name(host_id: str) -> str:
    """Return the deterministic inventory name for a node's loop fallback OSD.

    Returns
    -------
    str
        Kubernetes custom-object-safe loop OSD inventory name.

    Raises
    ------
    ValueError
        If `host_id` is empty.
    """
    host_id = host_id.strip()
    if not host_id:
        msg = "loop fallback OSD names require a non-empty host ID"
        raise ValueError(msg)
    return f"bertrand-osd-loop-{_hash_label(host_id)[:32]}"


def storage_node_report_name(host_id: str) -> str:
    """Return the deterministic capacity-report name for a Bertrand host.

    Parameters
    ----------
    host_id : str
        Durable Bertrand host UUID.

    Returns
    -------
    str
        Kubernetes custom-object-safe node report name.

    Raises
    ------
    ValueError
        If `host_id` is empty.
    """
    host_id = host_id.strip()
    if not host_id:
        msg = "storage node reports require a non-empty host ID"
        raise ValueError(msg)
    return f"bertrand-storage-node-{_hash_label(host_id)[:32]}"


def storage_reservation_name(
    *,
    owner_kind: str,
    owner_name: str,
    request_id: str,
) -> str:
    """Return the deterministic name for one storage reservation.

    Parameters
    ----------
    owner_kind : str
        Kind of the object or subsystem that owns the reservation.
    owner_name : str
        Owner name.
    request_id : str
        Stable request identifier under the owner.

    Returns
    -------
    str
        Kubernetes custom-object-safe reservation name.

    Raises
    ------
    ValueError
        If any identity component is empty.
    """
    owner_kind = owner_kind.strip()
    owner_name = owner_name.strip()
    request_id = request_id.strip()
    if not owner_kind or not owner_name or not request_id:
        msg = "storage reservation names require owner kind, owner name, and request ID"
        raise ValueError(msg)
    identity = f"{owner_kind}:{owner_name}:{request_id}"
    return f"bertrand-storage-reservation-{_hash_label(identity)}"


def storage_osd_resource_names(name: str) -> tuple[str, str, str]:
    """Return resource names for an OSD inventory record.

    Returns
    -------
    tuple[str, str, str]
        Deterministic reserved PV/PVC name stems and Rook device-set name.

    Raises
    ------
    ValueError
        If `name` is empty.
    """
    name = name.strip()
    if not name:
        msg = "OSD resource names require a non-empty OSD inventory name"
        raise ValueError(msg)
    suffix = name.removeprefix("bertrand-osd-")
    return (
        f"bertrand-osd-pv-{suffix}",
        f"bertrand-osd-pvc-{suffix}",
        f"bertrand-osd-{suffix}",
    )


class _CephStoragePolicySpec(BaseModel):
    """Desired policy for Ceph capacity autoscaling."""

    model_config = ConfigDict(extra="forbid")
    enabled: bool = True
    high_watermark: _Watermark = 0.75
    target_watermark: _Watermark = 0.65
    shrink_enabled: bool = True
    low_watermark: _Watermark = 0.45
    shrink_target_watermark: _Watermark = 0.60
    shrink_cooldown_seconds: PositiveInt = 3600
    min_lvm_osd_size: _Size = "16G"
    lvm_shrink_min_reclaim: _Size = "16G"
    growth_step: _Size = "16G"
    min_growth_step: _Size = "16G"
    max_growth_per_reconcile: _Size = "128G"
    target_headroom_ratio: _Watermark = 0.35
    min_headroom: _Size = "16G"
    burst_window_seconds: PositiveInt = 900
    burst_multiplier: Annotated[float, Field(gt=0.0)] = 2.0
    write_rate_ewma_alpha: _Watermark = 0.35
    default_write_reservation: _Size = "16G"
    max_actions_per_reconcile: PositiveInt = 3
    reconcile_interval_seconds: PositiveInt = 30

    @field_validator(
        "growth_step",
        "min_lvm_osd_size",
        "lvm_shrink_min_reclaim",
        "min_growth_step",
        "max_growth_per_reconcile",
        "min_headroom",
        "default_write_reservation",
    )
    @classmethod
    def _validate_size(cls, value: str) -> str:
        normalized = value.strip().upper()
        parse_size_bytes(normalized)
        return normalized

    @model_validator(mode="after")
    def _validate_watermarks(self) -> _CephStoragePolicySpec:
        if not self.low_watermark < self.shrink_target_watermark < self.high_watermark:
            msg = (
                "Ceph autoscale watermarks must satisfy "
                "low_watermark < shrink_target_watermark < high_watermark"
            )
            raise ValueError(msg)
        if parse_size_bytes(self.min_growth_step) > parse_size_bytes(
            self.max_growth_per_reconcile
        ):
            msg = "min_growth_step cannot exceed max_growth_per_reconcile"
            raise ValueError(msg)
        return self


class CephStoragePolicyStatus(BaseModel):
    """Observed status emitted by the Ceph capacity controller."""

    model_config = ConfigDict(extra="forbid")
    observed_generation: int | None = Field(default=None, alias="observedGeneration")
    total_bytes: int | None = None
    used_bytes: int | None = None
    used_ratio: float | None = None
    free_bytes: int | None = None
    headroom_target_bytes: int = 0
    reserved_bytes: int = 0
    write_rate_ewma_bytes_per_second: float = 0.0
    projected_seconds_to_headroom_floor: float | None = None
    growth_recommendation_bytes: int = 0
    pending_actions: int = 0
    running_actions: int = 0
    succeeded_actions: int = 0
    failed_actions: int = 0
    managed_osds: int = 0
    loop_osds: int = 0
    lvm_osds: int = 0
    elastic_bytes: int = 0
    durable_bytes: int = 0
    lvm_preferred: bool = False
    shrink_candidates: int = 0
    missing_lvm_osd_pvs: int = 0
    lvm_reclaimable_bytes: int = 0
    lvm_shrink_candidate: str = ""
    lvm_shrink_target_bytes: int = 0
    last_shrink_at: datetime | None = None
    last_reconciled_at: datetime | None = None
    last_error: str = ""


class _CephStorageActionSpec(BaseModel):
    """Desired node-local storage action contract."""

    model_config = ConfigDict(extra="forbid")
    policy_generation: Annotated[int, Field(ge=0)]
    operation: StorageActionOperation
    node_name: Annotated[str, Field(min_length=1)]
    host_id: Annotated[str, Field(min_length=1)]
    osd_id: Annotated[int, Field(ge=0)] | None = None
    target_bytes: Annotated[int, Field(ge=1)] | None = None
    pv_name: str | None = None
    lv_name: str | None = None
    storage_osd_name: str | None = None
    reason: Annotated[str, Field(min_length=1)]

    @model_validator(mode="after")
    def _validate_operation_contract(self) -> _CephStorageActionSpec:
        if self.operation in ("expand-lvm", "expand-loop"):
            if self.target_bytes is None or self.osd_id is not None:
                msg = f"{self.operation} actions require target_bytes only"
                raise ValueError(msg)
            if self.operation == "expand-lvm" and not (self.pv_name or "").strip():
                msg = "expand-lvm actions require pv_name"
                raise ValueError(msg)
            return self
        if self.operation == "shrink-lvm":
            if self.osd_id is None or self.target_bytes is None:
                msg = "shrink-lvm actions require osd_id and target_bytes"
                raise ValueError(msg)
            if not (self.storage_osd_name or "").strip():
                msg = "shrink-lvm actions require storage_osd_name"
                raise ValueError(msg)
            if not (self.pv_name or "").strip() or not (self.lv_name or "").strip():
                msg = "shrink-lvm actions require pv_name and lv_name"
                raise ValueError(msg)
            return self
        if self.osd_id is None or self.target_bytes is not None:
            msg = "retire-loop actions require osd_id and cannot set target_bytes"
            raise ValueError(msg)
        return self


class _CephStorageActionStatus(BaseModel):
    """Observed lifecycle state for one node-local storage action."""

    model_config = ConfigDict(extra="forbid")
    phase: StorageActionPhase = "Pending"
    started_at: datetime | None = None
    finished_at: datetime | None = None
    message: str = ""
    diagnostics: str = ""
    worker_node: str = ""
    created_osd_ids: tuple[int, ...] = ()
    removed_osd_ids: tuple[int, ...] = ()
    osd_origin: StorageOSDOrigin | None = None
    osd_quality: StorageOSDQuality | None = None
    source_pv: str = ""
    source_lv: str = ""
    provisioned_bytes: Annotated[int, Field(ge=0)] | None = None


class CephStorageActionRecord(BaseModel):
    """Validated `CephStorageAction` custom-resource payload."""

    model_config = ConfigDict(extra="forbid")
    api_version: str = Field(alias="apiVersion")
    kind: Literal["CephStorageAction"]
    metadata: CustomObjectMetadata
    spec: _CephStorageActionSpec
    status: _CephStorageActionStatus = Field(default_factory=_CephStorageActionStatus)

    @property
    def name(self) -> str:
        """Return the Kubernetes action object name."""
        return self.metadata.name


class CephStorageReservation(BaseModel):
    """Storage reservation embedded in `CephStorageState`."""

    model_config = ConfigDict(extra="forbid", frozen=True)
    name: Annotated[str, Field(min_length=1)]
    owner_kind: Annotated[str, Field(min_length=1)]
    owner_name: Annotated[str, Field(min_length=1)]
    request_id: Annotated[str, Field(min_length=1)]
    requested_bytes: Annotated[int, Field(ge=1)]
    reason: Annotated[str, Field(min_length=1)]
    expires_at: datetime
    phase: StorageReservationPhase = "Pending"
    ready_at: datetime | None = None
    released_at: datetime | None = None
    observed_free_bytes: Annotated[int, Field(ge=0)] = 0
    last_error: str = ""

    def expires_at_utc(self) -> datetime:
        """Return the reservation expiry timestamp normalized to UTC.

        Returns
        -------
        datetime
            UTC-normalized expiry timestamp.
        """
        expires_at = self.expires_at
        if expires_at.tzinfo is None:
            return expires_at.replace(tzinfo=UTC)
        return expires_at.astimezone(UTC)

    def is_active(self, now: datetime) -> bool:
        """Return whether this reservation still contributes to demand.

        Parameters
        ----------
        now : datetime
            UTC timestamp used for expiry checks.

        Returns
        -------
        bool
            True when the reservation is pending or ready and unexpired.
        """
        return self.phase in {"Pending", "Ready"} and self.expires_at_utc() > now

    def spec_payload(self) -> dict[str, object]:
        """Return the legacy reservation spec payload.

        Returns
        -------
        dict[str, object]
            JSON-serializable reservation spec.
        """
        return self.model_dump(
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

    def status_payload(self) -> dict[str, object]:
        """Return the legacy reservation status payload.

        Returns
        -------
        dict[str, object]
            JSON-serializable reservation status.
        """
        return self.model_dump(
            mode="json",
            include={
                "phase",
                "ready_at",
                "released_at",
                "observed_free_bytes",
                "last_error",
            },
        )


class _CephStorageNodePVStatus(BaseModel):
    """Observed free capacity for one physical volume in the Bertrand VG."""

    model_config = ConfigDict(extra="forbid")
    pv_name: Annotated[str, Field(min_length=1)]
    pv_uuid: Annotated[str, Field(min_length=1)]
    pv_size_bytes: Annotated[int, Field(ge=0)] = 0
    pv_free_bytes: Annotated[int, Field(ge=0)] = 0


class CephStorageNodeReport(BaseModel):
    """Host-local capacity state reported by one node agent."""

    model_config = ConfigDict(extra="forbid")
    name: Annotated[str, Field(min_length=1)]
    node_name: Annotated[str, Field(min_length=1)]
    host_id: Annotated[str, Field(min_length=1)]
    free_bytes: Annotated[int, Field(ge=0)] = 0
    path: str = ""
    lvm_free_bytes: Annotated[int, Field(ge=0)] = 0
    lvm_pvs: tuple[str, ...] = ()
    lvm_pv_inventory: tuple[_CephStorageNodePVStatus, ...] = ()
    loop_fallback_active: bool = False
    heartbeat_at: datetime | None = None
    last_error: str = ""

    def spec_payload(self) -> dict[str, object]:
        """Return the legacy node-report spec payload.

        Returns
        -------
        dict[str, object]
            JSON-serializable node-report spec.
        """
        return self.model_dump(mode="json", include={"node_name", "host_id"})

    def status_payload(self) -> dict[str, object]:
        """Return the legacy node-report status payload.

        Returns
        -------
        dict[str, object]
            JSON-serializable node-report status.
        """
        return self.model_dump(
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


class CephStorageOSD(BaseModel):
    """Managed OSD inventory embedded in `CephStorageState`."""

    model_config = ConfigDict(extra="forbid")
    name: Annotated[str, Field(min_length=1)]
    origin: StorageOSDOrigin
    node_name: Annotated[str, Field(min_length=1)]
    host_id: Annotated[str, Field(min_length=1)]
    pv_name: str = ""
    pv_uuid: str = ""
    pv_device: str = ""
    lv_name: str = ""
    lv_path: str = ""
    loop_file: str = ""
    loop_device: str = ""
    block_path: Annotated[str, Field(min_length=1)]
    csi_volume_id: str = ""
    persistent_volume_name: str = ""
    persistent_volume_claim_namespace: str = ""
    persistent_volume_claim_name: str = ""
    device_set_name: Annotated[str, Field(min_length=1)]
    target_bytes: Annotated[int, Field(ge=1)]
    phase: StorageOSDPhase = "Pending"
    observed_bytes: Annotated[int, Field(ge=0)] = 0
    ceph_osd_id: Annotated[int, Field(ge=0)] | None = None
    created_at: datetime | None = None
    phase_changed_at: datetime | None = None
    last_seen_at: datetime | None = None
    retired_at: datetime | None = None
    last_error: str = ""

    @model_validator(mode="after")
    def _validate_origin_fields(self) -> CephStorageOSD:
        if self.origin == "lvm-pv":
            if not (self.pv_name and self.pv_uuid and self.lv_name and self.lv_path):
                msg = "LVM-backed OSD records require PV and LV identity fields"
                raise ValueError(msg)
            if self.loop_file or self.loop_device:
                msg = "LVM-backed OSD records cannot set loop identity fields"
                raise ValueError(msg)
            return self
        if not self.loop_file:
            msg = "loop fallback OSD records require loop_file"
            raise ValueError(msg)
        if self.pv_name or self.pv_uuid or self.pv_device or self.lv_name:
            msg = "loop fallback OSD records cannot set LVM identity fields"
            raise ValueError(msg)
        return self

    def spec_payload(self) -> dict[str, object]:
        """Return the legacy OSD spec payload.

        Returns
        -------
        dict[str, object]
            JSON-serializable OSD spec.
        """
        return self.model_dump(
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

    def status_payload(self) -> dict[str, object]:
        """Return the legacy OSD status payload.

        Returns
        -------
        dict[str, object]
            JSON-serializable OSD status.
        """
        return self.model_dump(
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


class CephStorageStateStatus(BaseModel):
    """Collapsed Ceph storage controller state."""

    model_config = ConfigDict(extra="forbid")
    policy: CephStoragePolicyStatus | None = None
    reservations: dict[str, CephStorageReservation] = Field(default_factory=dict)
    nodes: dict[str, CephStorageNodeReport] = Field(default_factory=dict)
    osds: dict[str, CephStorageOSD] = Field(default_factory=dict)


class CephStorageStateRecord(BaseModel):
    """Validated `CephStorageState` custom-resource payload."""

    model_config = ConfigDict(extra="forbid", populate_by_name=True)
    api_version: str = Field(alias="apiVersion")
    kind: Literal["CephStorageState"]
    metadata: CustomObjectMetadata
    spec: _CephStoragePolicySpec = Field(default_factory=_CephStoragePolicySpec)
    status: CephStorageStateStatus = Field(default_factory=CephStorageStateStatus)

    @property
    def name(self) -> str:
        """Return the Kubernetes storage state object name."""
        return self.metadata.name

    @property
    def generation(self) -> int:
        """Return the Kubernetes metadata generation."""
        return self.metadata.generation

    @property
    def policy_status(self) -> CephStoragePolicyStatus | None:
        """Return the latest controller policy summary."""
        return self.status.policy


@dataclass(frozen=True)
class PlannedStorageAction:
    """One node-scoped Rook/LVM storage action selected by policy planning."""

    operation: StorageActionOperation
    node_name: str
    reason: str
    host_id: str = ""
    osd_id: int | None = None
    target_bytes: int | None = None
    pv_name: str | None = None
    lv_name: str | None = None
    storage_osd_name: str | None = None


@dataclass(frozen=True)
class _EligibleStorageNode:
    """One node-local storage growth target selected from agent reports."""

    node_name: str
    operation: StorageActionOperation
    host_id: str = ""
    pv_name: str | None = None
    pv_uuid: str | None = None
    pv_free_bytes: int = 0
    lv_name: str | None = None
    storage_osd_name: str | None = None
    current_bytes: int = 0
    available_bytes: int = 0
    target_bytes: int = 0


@dataclass(frozen=True)
class _ManagedOSD:
    """Autoscaler-created loop fallback OSD that is eligible for retirement."""

    osd_id: int
    node_name: str
    host_id: str
    size_bytes: int
    created_at: datetime | None


@dataclass(frozen=True)
class _ManagedLVMOSD:
    """PV-pinned LVM OSD that may be safely drain/recreated smaller."""

    name: str
    osd_id: int
    node_name: str
    host_id: str
    pv_name: str
    pv_uuid: str
    lv_name: str
    size_bytes: int
    created_at: datetime | None


@dataclass(frozen=True)
class StoragePlan:
    """Storage actions and status inputs selected by one planning pass."""

    actions: list[PlannedStorageAction]
    managed_osd_count: int
    loop_osd_count: int
    lvm_osd_count: int
    elastic_bytes: int
    durable_bytes: int
    shrink_candidate_count: int
    missing_lvm_osd_pvs: int
    lvm_reclaimable_bytes: int
    lvm_shrink_candidate: str
    lvm_shrink_target_bytes: int
    last_shrink_at: datetime | None
    free_bytes: int
    headroom_target_bytes: int
    reserved_bytes: int
    write_rate_ewma_bytes_per_second: float
    projected_seconds_to_headroom_floor: float | None
    growth_recommendation_bytes: int


@dataclass(frozen=True)
class StorageGrowthRecommendation:
    """Adaptive growth inputs selected from observed capacity and reservations."""

    free_bytes: int
    headroom_target_bytes: int
    reserved_bytes: int
    write_rate_ewma_bytes_per_second: float
    projected_seconds_to_headroom_floor: float | None
    growth_recommendation_bytes: int


@dataclass(frozen=True)
class StorageOSDInventory:
    """Quality-oriented OSD inventory inferred from durable OSD records.

    Attributes
    ----------
    loop_osd_ids : frozenset[int]
        Autoscaler-created loop OSD IDs still present in managed inventory.
    lvm_osd_ids : frozenset[int]
        LVM-backed OSD IDs present in managed inventory.
    elastic_bytes : int
        Approximate autoscaled loop-backed raw bytes tracked by Bertrand.
    durable_bytes : int
        Approximate LVM-backed raw bytes tracked by Bertrand.
    """

    loop_osd_ids: frozenset[int]
    lvm_osd_ids: frozenset[int]
    elastic_bytes: int
    durable_bytes: int


@dataclass
class CephStoragePlanner:
    """Pure Ceph capacity planning state for controller reconciliation."""

    offset: int = 0

    @staticmethod
    def action_counts(
        actions: Collection[CephStorageActionRecord],
    ) -> dict[str, int]:
        """Return storage action phase counts.

        Parameters
        ----------
        actions : Collection[CephStorageActionRecord]
            Storage actions to summarize.

        Returns
        -------
        dict[str, int]
            Counts keyed by action phase.
        """
        counts: dict[str, int] = dict.fromkeys(STORAGE_ACTION_PHASES, 0)
        for action in actions:
            counts[action.status.phase] += 1
        return counts

    def in_flight(self, actions: Collection[CephStorageActionRecord]) -> int:
        """Return pending or running storage action count.

        Parameters
        ----------
        actions : Collection[CephStorageActionRecord]
            Storage actions to inspect.

        Returns
        -------
        int
            Number of pending or running actions.
        """
        counts = self.action_counts(actions)
        return counts["Pending"] + counts["Running"]

    @staticmethod
    def utc(value: datetime | None) -> datetime | None:
        """Normalize a timestamp to UTC.

        Parameters
        ----------
        value : datetime | None
            Timestamp to normalize.

        Returns
        -------
        datetime | None
            UTC-normalized timestamp, or `None`.
        """
        if value is None:
            return None
        if value.tzinfo is None:
            return value.replace(tzinfo=UTC)
        return value.astimezone(UTC)

    def last_shrink_at(
        self,
        actions: Collection[CephStorageActionRecord],
    ) -> datetime | None:
        """Return the latest shrink action attempt timestamp.

        Parameters
        ----------
        actions : Collection[CephStorageActionRecord]
            Storage actions to inspect.

        Returns
        -------
        datetime | None
            Latest shrink attempt timestamp, if one exists.
        """
        timestamps = [
            self.utc(action.status.finished_at or action.status.started_at)
            for action in actions
            if action.spec.operation in {"retire-loop", "shrink-lvm"}
            and action.status.phase in ("Running", "Succeeded", "Failed")
        ]
        return max((item for item in timestamps if item is not None), default=None)

    def osd_inventory(
        self,
        osds: Collection[CephStorageOSD],
    ) -> StorageOSDInventory:
        """Return loop/LVM OSD inventory inferred from durable OSD records.

        Parameters
        ----------
        osds : Collection[CephStorageOSD]
            OSD records to inspect.

        Returns
        -------
        StorageOSDInventory
            Quality-oriented OSD inventory.
        """
        ready = [record for record in osds if record.phase == "Ready"]
        loop_ids = {
            record.ceph_osd_id
            for record in ready
            if record.origin == "loop-fallback"
            and record.ceph_osd_id is not None
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
        return StorageOSDInventory(
            loop_osd_ids=frozenset(loop_ids),
            lvm_osd_ids=frozenset(lvm_ids),
            elastic_bytes=elastic_bytes,
            durable_bytes=durable_bytes,
        )

    @staticmethod
    def lvm_actions_in_flight(
        actions: Collection[CephStorageActionRecord],
    ) -> bool:
        """Return whether preferred LVM capacity is currently being expanded.

        Parameters
        ----------
        actions : Collection[CephStorageActionRecord]
            Storage actions to inspect.

        Returns
        -------
        bool
            True when an LVM OSD action is pending or running.
        """
        return any(
            action.spec.operation == "expand-lvm"
            and action.status.phase in ("Pending", "Running")
            for action in actions
        )

    @staticmethod
    def osd_admission_in_flight(
        osds: Collection[CephStorageOSD],
    ) -> bool:
        """Return whether Rook is still admitting or retiring a managed OSD.

        Parameters
        ----------
        osds : Collection[CephStorageOSD]
            Durable OSD inventory records to inspect.

        Returns
        -------
        bool
            True when any active OSD record is not yet terminal or ready.
        """
        return any(
            record.phase in STORAGE_OSD_IN_FLIGHT_PHASES for record in osds
        )

    def managed_osds(
        self,
        *,
        osd_records: Collection[CephStorageOSD],
        osds: Collection[CephOSD],
    ) -> list[_ManagedOSD]:
        """Return shrink-eligible managed OSD inventory.

        Parameters
        ----------
        osd_records : Collection[CephStorageOSD]
            Durable OSD records that identify autoscaler-created OSDs.
        osds : Collection[CephOSD]
            Live Ceph OSD inventory.

        Returns
        -------
        list[_ManagedOSD]
            Live managed OSDs eligible for shrink consideration.
        """
        live = {
            osd.osd_id: osd
            for osd in osds
            if osd.up and osd.in_cluster and osd.node_name
        }
        candidates: list[_ManagedOSD] = []
        for record in osd_records:
            if record.origin != "loop-fallback" or record.phase != "Ready":
                continue
            osd_id = record.ceph_osd_id
            if osd_id is None:
                continue
            osd = live.get(osd_id)
            if osd is None:
                continue
            candidates.append(
                _ManagedOSD(
                    osd_id=osd_id,
                    node_name=record.node_name or osd.node_name,
                    host_id=record.host_id,
                    size_bytes=record.observed_bytes or record.target_bytes,
                    created_at=self.utc(record.created_at),
                )
            )
        return candidates

    @staticmethod
    def eligible_nodes(
        *,
        ready_nodes: Collection[str],
        reports: Collection[CephStorageNodeReport],
        actions: Collection[CephStorageActionRecord],
        osds: Collection[CephStorageOSD],
        growth_bytes: int,
    ) -> list[_EligibleStorageNode]:
        """Return deterministic node slots eligible for storage expansion.

        Parameters
        ----------
        ready_nodes : Collection[str]
            Kubernetes nodes currently ready for Bertrand registry pulls.
        reports : Collection[CephStorageNodeReport]
            Node-local free-space reports.
        actions : Collection[CephStorageActionRecord]
            Existing storage actions used to apply failed-target retry cooldowns.
        osds : Collection[CephStorageOSD]
            Durable managed OSD inventory.
        growth_bytes : int
            Bytes required by one fallback growth step.

        Returns
        -------
        list[_EligibleStorageNode]
            Sorted storage targets, preferring LVM PVs before loop fallback.
        """
        ready = frozenset(ready_nodes)
        now = datetime.now(UTC)
        lvm: list[_EligibleStorageNode] = []
        loop: list[_EligibleStorageNode] = []
        active_osds = [
            record
            for record in osds
            if record.phase not in {"Failed", "Retired", "Retiring"}
        ]
        failed_targets = CephStoragePlanner._failed_osd_targets_in_cooldown(
            osds, now=now
        ) | CephStoragePlanner._failed_action_targets_in_cooldown(actions, now=now)
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
        for report in reports:
            if report.node_name not in ready:
                continue
            heartbeat = report.heartbeat_at
            if heartbeat is None:
                continue
            if heartbeat.tzinfo is None:
                heartbeat = heartbeat.replace(tzinfo=UTC)
            heartbeat = heartbeat.astimezone(UTC)
            if (now - heartbeat).total_seconds() > STORAGE_NODE_REPORT_MAX_AGE_SECONDS:
                continue
            for pv in report.lvm_pv_inventory:
                if pv.pv_free_bytes < growth_bytes:
                    continue
                existing = lvm_by_pv.get((report.host_id, pv.pv_uuid))
                target_name = (
                    existing.name
                    if existing is not None
                    else storage_lvm_osd_name(report.host_id, pv.pv_uuid)
                )
                if target_name in failed_targets:
                    continue
                lvm.append(
                    _EligibleStorageNode(
                        node_name=report.node_name,
                        host_id=report.host_id,
                        operation="expand-lvm",
                        pv_name=pv.pv_name,
                        pv_uuid=pv.pv_uuid,
                        pv_free_bytes=pv.pv_free_bytes,
                        lv_name=existing.lv_name if existing is not None else None,
                        storage_osd_name=target_name,
                        current_bytes=(
                            existing.target_bytes if existing is not None else 0
                        ),
                        available_bytes=pv.pv_free_bytes,
                        target_bytes=(
                            existing.target_bytes + growth_bytes
                            if existing is not None
                            else growth_bytes
                        ),
                    )
                )
            if report.free_bytes >= growth_bytes:
                existing = loop_by_host.get(report.host_id)
                target_name = (
                    existing.name
                    if existing is not None
                    else storage_loop_osd_name(report.host_id)
                )
                if target_name in failed_targets:
                    continue
                loop.append(
                    _EligibleStorageNode(
                        node_name=report.node_name,
                        host_id=report.host_id,
                        operation="expand-loop",
                        storage_osd_name=target_name,
                        current_bytes=(
                            existing.target_bytes if existing is not None else 0
                        ),
                        available_bytes=report.free_bytes,
                        target_bytes=(
                            existing.target_bytes + growth_bytes
                            if existing is not None
                            else growth_bytes
                        ),
                    )
                )
        lvm = sorted(
            lvm,
            key=lambda item: (
                0 if not item.lv_name else 1,
                item.node_name,
                item.pv_name or "",
            ),
        )
        return [
            *lvm,
            *sorted(
                loop,
                key=lambda item: (item.node_name, item.storage_osd_name or ""),
            ),
        ]

    @staticmethod
    def _failed_osd_targets_in_cooldown(
        osds: Collection[CephStorageOSD],
        *,
        now: datetime,
    ) -> frozenset[str]:
        """Return failed OSD targets that should not be retried yet.

        Returns
        -------
        frozenset[str]
            OSD inventory names still inside their retry cooldown.
        """
        names: set[str] = set()
        for record in osds:
            if record.phase != "Failed":
                continue
            failed_at = (
                CephStoragePlanner.utc(record.phase_changed_at)
                or CephStoragePlanner.utc(record.last_seen_at)
                or CephStoragePlanner.utc(record.created_at)
            )
            if failed_at is None:
                continue
            if (
                now - failed_at
            ).total_seconds() < STORAGE_TARGET_RETRY_COOLDOWN_SECONDS:
                names.add(record.name)
        return frozenset(names)

    @staticmethod
    def _failed_action_targets_in_cooldown(
        actions: Collection[CephStorageActionRecord],
        *,
        now: datetime,
    ) -> frozenset[str]:
        """Return recently failed action targets that should not be retried.

        Returns
        -------
        frozenset[str]
            OSD inventory names from failed expand actions still cooling down.
        """
        names: set[str] = set()
        for action in actions:
            if (
                action.status.phase != "Failed"
                or action.spec.operation
                not in {"expand-lvm", "expand-loop", "shrink-lvm"}
                or not action.spec.storage_osd_name
            ):
                continue
            failed_at = CephStoragePlanner.utc(
                action.status.finished_at or action.status.started_at
            )
            if failed_at is None:
                continue
            if (
                now - failed_at
            ).total_seconds() < STORAGE_TARGET_RETRY_COOLDOWN_SECONDS:
                names.add(action.spec.storage_osd_name)
        return frozenset(names)

    @staticmethod
    def active_reservation_bytes(
        reservations: Collection[CephStorageReservation],
        *,
        now: datetime,
    ) -> int:
        """Return bytes reserved by active storage reservations.

        Returns
        -------
        int
            Sum of unexpired pending and ready reservation bytes.
        """
        return sum(
            reservation.requested_bytes
            for reservation in reservations
            if reservation.is_active(now)
        )

    def growth_recommendation(
        self,
        *,
        policy: CephStorageStateRecord,
        capacity: CephCapacitySnapshot,
        reservations: Collection[CephStorageReservation],
        now: datetime,
    ) -> StorageGrowthRecommendation:
        """Return adaptive headroom and growth demand for one reconcile pass.

        Returns
        -------
        StorageGrowthRecommendation
            Computed free-space, trend, reservation, and growth demand values.
        """
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
            previous_time = self.utc(policy.policy_status.last_reconciled_at)
            if previous_time is not None:
                elapsed = max(0.0, (now - previous_time).total_seconds())
                if elapsed > 0:
                    delta = max(
                        0,
                        capacity.used_bytes - policy.policy_status.used_bytes,
                    )
                    instantaneous_rate = delta / elapsed
        alpha = spec.write_rate_ewma_alpha
        ewma_rate = (alpha * instantaneous_rate) + ((1 - alpha) * previous_rate)
        reserved_bytes = self.active_reservation_bytes(reservations, now=now)
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
                self._round_up(max(raw_growth, min_step), min_step),
            )
        projected_seconds: float | None = None
        headroom_floor = headroom_target + reserved_bytes
        if ewma_rate > 0 and free_bytes > headroom_floor:
            projected_seconds = (free_bytes - headroom_floor) / ewma_rate
        elif ewma_rate > 0:
            projected_seconds = 0.0
        return StorageGrowthRecommendation(
            free_bytes=free_bytes,
            headroom_target_bytes=headroom_target,
            reserved_bytes=reserved_bytes,
            write_rate_ewma_bytes_per_second=ewma_rate,
            projected_seconds_to_headroom_floor=projected_seconds,
            growth_recommendation_bytes=recommendation,
        )

    @staticmethod
    def _round_up(value: int, step: int) -> int:
        return int(math.ceil(value / step) * step) if value > 0 else 0

    def plan_grow_actions(
        self,
        *,
        policy: CephStorageStateRecord,
        capacity: CephCapacitySnapshot,
        actions: Collection[CephStorageActionRecord],
        osd_records: Collection[CephStorageOSD],
        eligible_nodes: list[_EligibleStorageNode],
        growth: StorageGrowthRecommendation,
        min_growth_bytes: int,
    ) -> list[PlannedStorageAction]:
        """Return expand actions needed to reach the policy target watermark.

        Parameters
        ----------
        policy : CephStorageStateRecord
            Active storage policy.
        capacity : CephCapacitySnapshot
            Current Ceph capacity snapshot.
        actions : Collection[CephStorageActionRecord]
            Existing storage action records.
        osd_records : Collection[CephStorageOSD]
            Durable OSD inventory used to gate overlapping admission.
        eligible_nodes : list[_EligibleStorageNode]
            Node-local growth targets, ordered by storage preference.
        growth : StorageGrowthRecommendation
            Adaptive growth recommendation for this reconcile pass.
        min_growth_bytes : int
            Minimum bytes requested by one capacity expansion.

        Returns
        -------
        list[PlannedStorageAction]
            Grow actions selected for this reconcile pass.
        """
        spec = policy.spec
        if (
            not spec.enabled
            or not eligible_nodes
            or self.osd_admission_in_flight(osd_records)
        ):
            return []
        if capacity.total_bytes <= 0:
            budget = spec.max_actions_per_reconcile - self.in_flight(actions)
            if budget <= 0:
                return []
            target = eligible_nodes[0]
            allocation = min(
                max(min_growth_bytes, growth.growth_recommendation_bytes),
                target.available_bytes,
            )
            return [
                PlannedStorageAction(
                    operation=target.operation,
                    node_name=target.node_name,
                    host_id=target.host_id,
                    target_bytes=target.current_bytes + allocation,
                    pv_name=target.pv_name,
                    lv_name=target.lv_name,
                    storage_osd_name=target.storage_osd_name,
                    reason="cluster has no usable OSD capacity yet",
                )
            ]
        if growth.growth_recommendation_bytes <= 0:
            return []

        budget = spec.max_actions_per_reconcile - self.in_flight(actions)
        if budget <= 0:
            return []

        remaining = growth.growth_recommendation_bytes
        preferred_targets = [
            target for target in eligible_nodes if target.operation == "expand-lvm"
        ]
        fallback_targets = [
            target for target in eligible_nodes if target.operation == "expand-loop"
        ]
        planned: list[PlannedStorageAction] = []
        for target in [*preferred_targets, *fallback_targets]:
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
                PlannedStorageAction(
                    operation=target.operation,
                    node_name=target.node_name,
                    host_id=target.host_id,
                    target_bytes=target.current_bytes + allocation,
                    pv_name=target.pv_name,
                    lv_name=target.lv_name,
                    storage_osd_name=target.storage_osd_name,
                    reason=(
                        "free Ceph capacity is below adaptive headroom target "
                        f"({growth.free_bytes} < "
                        f"{growth.headroom_target_bytes + growth.reserved_bytes})"
                    ),
                )
            )
            remaining -= allocation
        return planned

    def plan_lvm_coverage_actions(
        self,
        *,
        policy: CephStorageStateRecord,
        actions: Collection[CephStorageActionRecord],
        osd_records: Collection[CephStorageOSD],
        eligible_nodes: list[_EligibleStorageNode],
    ) -> list[PlannedStorageAction]:
        """Return actions that preserve one LVM OSD per usable PV.

        Returns
        -------
        list[PlannedStorageAction]
            Minimum-size LVM OSD creation actions for uncovered PVs.
        """
        spec = policy.spec
        if (
            not spec.enabled
            or self.in_flight(actions) > 0
            or self.osd_admission_in_flight(osd_records)
        ):
            return []
        min_lvm_size = parse_size_bytes(spec.min_lvm_osd_size)
        budget = spec.max_actions_per_reconcile
        planned: list[PlannedStorageAction] = []
        for target in eligible_nodes:
            if target.operation != "expand-lvm" or target.current_bytes > 0:
                continue
            allocation = min(min_lvm_size, target.available_bytes)
            if allocation < min_lvm_size:
                continue
            planned.append(
                PlannedStorageAction(
                    operation="expand-lvm",
                    node_name=target.node_name,
                    host_id=target.host_id,
                    target_bytes=allocation,
                    pv_name=target.pv_name,
                    lv_name=target.lv_name,
                    storage_osd_name=target.storage_osd_name,
                    reason=(
                        "usable Bertrand LVM PV is missing steady-state OSD coverage"
                    ),
                )
            )
            if len(planned) >= budget:
                break
        return planned

    def lvm_osds_for_shrink(
        self,
        *,
        osd_records: Collection[CephStorageOSD],
        osds: Collection[CephOSD],
    ) -> list[_ManagedLVMOSD]:
        """Return live LVM OSDs eligible for drain/recreate shrink planning.

        Returns
        -------
        list[_ManagedLVMOSD]
            Ready LVM-backed OSDs verified as live by Ceph.
        """
        live = {
            osd.osd_id: osd
            for osd in osds
            if osd.up and osd.in_cluster and osd.node_name
        }
        candidates: list[_ManagedLVMOSD] = []
        for record in osd_records:
            if record.origin != "lvm-pv" or record.phase != "Ready":
                continue
            osd_id = record.ceph_osd_id
            if osd_id is None or osd_id not in live:
                continue
            candidates.append(
                _ManagedLVMOSD(
                    name=record.name,
                    osd_id=osd_id,
                    node_name=record.node_name,
                    host_id=record.host_id,
                    pv_name=record.pv_name,
                    pv_uuid=record.pv_uuid,
                    lv_name=record.lv_name,
                    size_bytes=record.observed_bytes or record.target_bytes,
                    created_at=self.utc(record.created_at),
                )
            )
        return candidates

    def lvm_shrink_preview(
        self,
        *,
        policy: CephStorageStateRecord,
        capacity: CephCapacitySnapshot,
        growth: StorageGrowthRecommendation,
        lvm_osds: Collection[_ManagedLVMOSD],
    ) -> tuple[int, str, int]:
        """Return reclaimable bytes and selected LVM shrink candidate.

        Returns
        -------
        tuple[int, str, int]
            Total reclaimable bytes, selected OSD record name, and selected
            target bytes.
        """
        if not lvm_osds:
            return 0, "", 0
        min_lvm_size = parse_size_bytes(policy.spec.min_lvm_osd_size)
        min_reclaim = parse_size_bytes(policy.spec.lvm_shrink_min_reclaim)
        desired_total = max(
            capacity.used_bytes + growth.headroom_target_bytes + growth.reserved_bytes,
            min_lvm_size * len(lvm_osds),
        )
        per_osd_target = max(
            min_lvm_size,
            self._round_up(desired_total, len(lvm_osds)) // len(lvm_osds),
        )
        reclaimable: list[tuple[int, _ManagedLVMOSD]] = []
        for candidate in lvm_osds:
            reclaim = candidate.size_bytes - per_osd_target
            if reclaim >= min_reclaim:
                reclaimable.append((reclaim, candidate))
        if not reclaimable:
            return 0, "", per_osd_target
        total_reclaimable = sum(item[0] for item in reclaimable)
        _, selected = max(
            reclaimable,
            key=lambda item: (
                item[0],
                item[1].size_bytes,
                item[1].created_at or datetime.min.replace(tzinfo=UTC),
                item[1].name,
            ),
        )
        return total_reclaimable, selected.name, per_osd_target

    def plan_lvm_shrink_action(
        self,
        *,
        policy: CephStorageStateRecord,
        capacity: CephCapacitySnapshot,
        actions: Collection[CephStorageActionRecord],
        osd_records: Collection[CephStorageOSD],
        growth: StorageGrowthRecommendation,
        lvm_candidates: Collection[_ManagedLVMOSD],
        loop_candidates: Collection[_ManagedOSD],
    ) -> list[PlannedStorageAction]:
        """Return one safe LVM drain/recreate shrink action.

        Returns
        -------
        list[PlannedStorageAction]
            Empty or single-element LVM shrink action.
        """
        spec = policy.spec
        if (
            not spec.enabled
            or not spec.shrink_enabled
            or growth.growth_recommendation_bytes > 0
            or growth.reserved_bytes > 0
            or loop_candidates
            or self.in_flight(actions) > 0
            or self.osd_admission_in_flight(osd_records)
        ):
            return []
        last_shrink_at = self.last_shrink_at(actions)
        if (
            last_shrink_at is not None
            and (datetime.now(UTC) - last_shrink_at).total_seconds()
            < spec.shrink_cooldown_seconds
        ):
            return []
        reclaimable, selected_name, target_bytes = self.lvm_shrink_preview(
            policy=policy,
            capacity=capacity,
            growth=growth,
            lvm_osds=lvm_candidates,
        )
        if not selected_name or reclaimable <= 0:
            return []
        selected = next(
            candidate for candidate in lvm_candidates if candidate.name == selected_name
        )
        return [
            PlannedStorageAction(
                operation="shrink-lvm",
                node_name=selected.node_name,
                host_id=selected.host_id,
                osd_id=selected.osd_id,
                target_bytes=target_bytes,
                pv_name=selected.pv_name,
                lv_name=selected.lv_name,
                storage_osd_name=selected.name,
                reason=(
                    "LVM-backed raw capacity exceeds adaptive headroom; "
                    f"drain/recreate osd.{selected.osd_id} from "
                    f"{selected.size_bytes} to {target_bytes} bytes"
                ),
            )
        ]

    def plan_shrink_action(
        self,
        *,
        policy: CephStorageStateRecord,
        capacity: CephCapacitySnapshot,
        actions: Collection[CephStorageActionRecord],
        candidates: Collection[_ManagedOSD],
    ) -> list[PlannedStorageAction]:
        """Return one conservative shrink action when every safety gate passes.

        Parameters
        ----------
        policy : CephStorageStateRecord
            Active storage policy.
        capacity : CephCapacitySnapshot
            Current Ceph capacity snapshot.
        actions : Collection[CephStorageActionRecord]
            Existing storage action records.
        candidates : Collection[_ManagedOSD]
            Managed OSDs eligible for shrink planning.

        Returns
        -------
        list[PlannedStorageAction]
            Empty or single-element shrink plan.
        """
        spec = policy.spec
        if (
            not spec.enabled
            or not spec.shrink_enabled
            or capacity.used_ratio >= spec.low_watermark
            or self.in_flight(actions) > 0
        ):
            return []
        last_shrink_at = self.last_shrink_at(actions)
        if (
            last_shrink_at is not None
            and (datetime.now(UTC) - last_shrink_at).total_seconds()
            < spec.shrink_cooldown_seconds
        ):
            return []
        candidate = self._select_shrink_candidate(candidates)
        if candidate is None:
            return []
        projected_total = capacity.total_bytes - candidate.size_bytes
        if projected_total <= 0:
            return []
        projected_ratio = capacity.used_bytes / projected_total
        if projected_ratio > spec.shrink_target_watermark:
            return []
        return [
            PlannedStorageAction(
                operation="retire-loop",
                node_name=candidate.node_name,
                host_id=candidate.host_id,
                osd_id=candidate.osd_id,
                reason=(
                    "cluster usage "
                    f"{capacity.used_ratio:.2%} <= low watermark "
                    f"{spec.low_watermark:.2%}; projected usage after removing "
                    f"osd.{candidate.osd_id} is {projected_ratio:.2%}"
                ),
            )
        ]

    def plan_loop_offload_action(
        self,
        *,
        policy: CephStorageStateRecord,
        capacity: CephCapacitySnapshot,
        actions: Collection[CephStorageActionRecord],
        eligible_nodes: list[_EligibleStorageNode],
        candidates: Collection[_ManagedOSD],
        growth_bytes: int,
    ) -> list[PlannedStorageAction]:
        """Return LVM growth or loop retirement needed to evacuate fallback OSDs.

        Parameters
        ----------
        policy : CephStorageStateRecord
            Active storage policy.
        capacity : CephCapacitySnapshot
            Current Ceph capacity snapshot.
        actions : Collection[CephStorageActionRecord]
            Existing storage action records.
        eligible_nodes : list[_EligibleStorageNode]
            Node-local growth targets, ordered by storage preference.
        candidates : Collection[_ManagedOSD]
            Managed loop OSDs eligible for retirement.
        growth_bytes : int
            Bytes requested by one capacity expansion.

        Returns
        -------
        list[PlannedStorageAction]
            Empty, one retire action, or preferred LVM expansion actions.
        """
        spec = policy.spec
        if (
            not spec.enabled
            or not spec.shrink_enabled
            or self.in_flight(actions) > 0
            or capacity.total_bytes <= 0
        ):
            return []
        candidate = self._select_shrink_candidate(candidates)
        if candidate is None:
            return []
        projected_total = capacity.total_bytes - candidate.size_bytes
        if projected_total > 0:
            projected_ratio = capacity.used_bytes / projected_total
            if projected_ratio <= spec.shrink_target_watermark:
                return [
                    PlannedStorageAction(
                        operation="retire-loop",
                        node_name=candidate.node_name,
                        host_id=candidate.host_id,
                        osd_id=candidate.osd_id,
                        reason=(
                            "LVM-backed capacity can absorb loop fallback "
                            f"osd.{candidate.osd_id}; projected usage after "
                            f"retirement is {projected_ratio:.2%}"
                        ),
                    )
                ]
        lvm_targets = [
            target for target in eligible_nodes if target.operation == "expand-lvm"
        ]
        if not lvm_targets:
            return []
        target_total = math.ceil(capacity.used_bytes / spec.shrink_target_watermark)
        missing = max(0, target_total - max(0, projected_total))
        desired = max(1, math.ceil(missing / growth_bytes))
        budget = spec.max_actions_per_reconcile - self.in_flight(actions)
        count = max(0, min(desired, budget, len(lvm_targets)))
        planned: list[PlannedStorageAction] = []
        for index in range(count):
            target = lvm_targets[(self.offset + index) % len(lvm_targets)]
            planned.append(
                PlannedStorageAction(
                    operation="expand-lvm",
                    node_name=target.node_name,
                    host_id=target.host_id,
                    target_bytes=target.target_bytes or growth_bytes,
                    pv_name=target.pv_name,
                    lv_name=target.lv_name,
                    storage_osd_name=target.storage_osd_name,
                    reason=(
                        "LVM capacity is available while loop fallback "
                        f"osd.{candidate.osd_id} is active"
                    ),
                )
            )
        if lvm_targets and count:
            self.offset = (self.offset + count) % len(lvm_targets)
        return planned

    @staticmethod
    def _select_shrink_candidate(
        candidates: Collection[_ManagedOSD],
    ) -> _ManagedOSD | None:
        groups: dict[str, list[_ManagedOSD]] = {}
        for candidate in candidates:
            groups.setdefault(candidate.node_name, []).append(candidate)
        if not groups:
            return None
        node = min(groups, key=lambda item: (-len(groups[item]), item))
        return max(
            groups[node],
            key=lambda item: (
                item.created_at or datetime.min.replace(tzinfo=UTC),
                item.osd_id,
            ),
        )


STORAGE_STATE_RESOURCE = CustomObjectResource[CephStorageStateRecord](
    group=CEPH_CAPACITY_GROUP,
    version=CEPH_CAPACITY_VERSION,
    kind=STORAGE_STATE_KIND,
    plural=STORAGE_STATE_PLURAL,
    labels=STORAGE_CONTROLLER_LABELS,
    singular="cephstoragestate",
    short_names=("csstate",),
    payload_parser=CephStorageStateRecord.model_validate,
    payload_error_context=f"{STORAGE_STATE_KIND} custom object",
    spec_model=_CephStoragePolicySpec,
    spec_schema_include_defaults=True,
    status_model=CephStorageStateStatus,
)
STORAGE_ACTION_RESOURCE = CustomObjectResource[CephStorageActionRecord](
    group=CEPH_CAPACITY_GROUP,
    version=CEPH_CAPACITY_VERSION,
    kind=STORAGE_ACTION_KIND,
    plural=STORAGE_ACTION_PLURAL,
    labels=STORAGE_CONTROLLER_LABELS,
    singular="cephstorageaction",
    short_names=("csact",),
    payload_parser=CephStorageActionRecord.model_validate,
    payload_error_context=f"{STORAGE_ACTION_KIND} custom object",
    spec_model=_CephStorageActionSpec,
    status_model=_CephStorageActionStatus,
    status_schema_overrides={
        "properties": {
            "started_at": {"type": "string", "format": "date-time"},
            "finished_at": {"type": "string", "format": "date-time"},
            "created_osd_ids": {
                "type": "array",
                "items": {"type": "integer", "minimum": 0},
            },
            "removed_osd_ids": {
                "type": "array",
                "items": {"type": "integer", "minimum": 0},
            },
        },
    },
)
_STORAGE_RESOURCES: tuple[CustomObjectResource[Any], ...] = (
    STORAGE_STATE_RESOURCE,
    STORAGE_ACTION_RESOURCE,
)


async def ensure_ceph_capacity_crds(kube: Kube, *, timeout: float) -> None:
    """Converge Ceph capacity CRDs.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum convergence budget in seconds.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive or CRD establishment exceeds the budget.
    """
    if timeout <= 0:
        msg = "Ceph capacity CRD timeout must be non-negative"
        raise TimeoutError(msg)
    deadline = Deadline.from_timeout(
        timeout,
        message="Ceph capacity CRD timeout must be non-negative",
    )
    for resource in _STORAGE_RESOURCES:
        await resource.ensure_crd(kube, timeout=deadline.remaining())


async def ensure_default_storage_policy(kube: Kube, *, timeout: float) -> None:
    """Converge the singleton collapsed Ceph storage state.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum convergence budget in seconds.
    """
    deadline = _deadline_from_budget(timeout)
    await STORAGE_STATE_RESOURCE.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=STORAGE_STATE_NAME,
        spec=_CephStoragePolicySpec(),
        timeout=deadline.remaining(),
    )


def storage_watch_targets() -> tuple[tuple[CustomObjectResource[Any], str], ...]:
    """Return capacity resources watched by the storage controller.

    Returns
    -------
    tuple[tuple[CustomObjectResource[Any], str], ...]
        Resource/context pairs for storage state and action queue updates.
    """
    return (
        (STORAGE_STATE_RESOURCE, STORAGE_STATE_PLURAL),
        (STORAGE_ACTION_RESOURCE, STORAGE_ACTION_PLURAL),
    )


async def read_storage_state(kube: Kube, *, timeout: float) -> CephStorageStateRecord:
    """Read and validate the singleton collapsed storage state.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum request budget in seconds.

    Returns
    -------
    CephStorageStateRecord
        Validated singleton storage state.

    Raises
    ------
    OSError
        If the singleton storage state resource does not exist.
    """
    record = await STORAGE_STATE_RESOURCE.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=STORAGE_STATE_NAME,
        timeout=timeout,
    )
    if record is None:
        msg = f"{STORAGE_STATE_KIND} {STORAGE_STATE_NAME!r} is missing"
        raise OSError(msg)
    return record


async def _patch_storage_state_status(
    kube: Kube,
    *,
    status: CephStorageStateStatus,
    timeout: float,
) -> CephStorageStateRecord:
    return await STORAGE_STATE_RESOURCE.patch_status(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=STORAGE_STATE_NAME,
        status=status,
        timeout=timeout,
    )


async def list_storage_actions(
    kube: Kube, *, timeout: float
) -> list[CephStorageActionRecord]:
    """List and validate storage action resources.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum request budget in seconds.

    Returns
    -------
    list[CephStorageActionRecord]
        Validated storage action records.
    """
    return await STORAGE_ACTION_RESOURCE.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        timeout=timeout,
    )


async def upsert_storage_reservation(
    kube: Kube,
    *,
    owner_kind: str,
    owner_name: str,
    request_id: str,
    requested_bytes: int,
    reason: str,
    expires_at: datetime,
    timeout: float,
) -> CephStorageReservation:
    """Create or refresh a pending storage reservation.

    Returns
    -------
    CephStorageReservation
        Validated reservation record.
    """
    name = storage_reservation_name(
        owner_kind=owner_kind,
        owner_name=owner_name,
        request_id=request_id,
    )
    state = await read_storage_state(kube, timeout=timeout)
    entry = CephStorageReservation(
        name=name,
        owner_kind=owner_kind,
        owner_name=owner_name,
        request_id=request_id,
        requested_bytes=requested_bytes,
        reason=reason,
        expires_at=expires_at,
        phase="Pending",
        ready_at=None,
        released_at=None,
        observed_free_bytes=0,
        last_error="",
    )
    status = state.status.model_copy(
        update={"reservations": {**state.status.reservations, name: entry}},
    )
    refreshed = await _patch_storage_state_status(
        kube,
        status=status,
        timeout=timeout,
    )
    return refreshed.status.reservations.get(name, entry)


async def patch_storage_reservation_status(
    kube: Kube,
    *,
    reservation: CephStorageReservation,
    status: Mapping[str, object],
    timeout: float,
) -> CephStorageReservation:
    """Patch one storage reservation status.

    Returns
    -------
    CephStorageReservation
        Freshly validated reservation returned by the Kubernetes API.
    """
    patched = CephStorageReservation.model_validate(
        {**reservation.model_dump(mode="python"), **dict(status)}
    )
    state = await read_storage_state(kube, timeout=timeout)
    refreshed = await _patch_storage_state_status(
        kube,
        status=state.status.model_copy(
            update={
                "reservations": {
                    **state.status.reservations,
                    reservation.name: patched,
                },
            },
        ),
        timeout=timeout,
    )
    return refreshed.status.reservations.get(reservation.name, patched)


async def release_storage_reservation(
    kube: Kube,
    *,
    reservation: CephStorageReservation,
    timeout: float,
) -> None:
    """Mark a reservation released.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    reservation : CephStorageReservation
        Reservation to release.
    timeout : float
        Maximum request budget in seconds.
    """
    if reservation.phase in {"Released", "Expired", "Failed"}:
        return
    await patch_storage_reservation_status(
        kube,
        reservation=reservation,
        status={
            "phase": "Released",
            "released_at": datetime.now(UTC).isoformat(),
            "last_error": "",
        },
        timeout=timeout,
    )


async def wait_storage_reservation_ready(
    kube: Kube,
    *,
    reservation: CephStorageReservation,
    timeout: float,
) -> CephStorageReservation:
    """Wait until a storage reservation becomes ready.

    Returns
    -------
    CephStorageReservation
        Reservation in `Ready` phase.

    Raises
    ------
    OSError
        If the reservation disappears or reaches a terminal unusable phase.
    TimeoutError
        If the reservation does not become ready before `timeout`.
    """
    msg = (
        f"storage reservation {reservation.name!r} was not ready before "
        "the operation timeout; run `bertrand cluster storage doctor`"
    )
    deadline = Deadline.from_timeout(timeout, message=msg)
    while deadline.remaining() > 0:
        state = await read_storage_state(kube, timeout=deadline.remaining())
        fresh = state.status.reservations.get(reservation.name)
        if fresh is None:
            msg = f"storage reservation {reservation.name!r} disappeared"
            raise OSError(msg)
        if fresh.phase == "Ready":
            return fresh
        if fresh.phase in {"Failed", "Expired", "Released"}:
            detail = fresh.last_error or f"phase is {fresh.phase}"
            msg = f"storage reservation {fresh.name!r} is not usable: {detail}"
            raise OSError(msg)
        await asyncio.sleep(deadline.bounded(2.0))
    raise TimeoutError(msg)


@asynccontextmanager
async def reserve_ceph_storage(
    kube: Kube,
    *,
    owner_kind: str,
    owner_name: str,
    request_id: str,
    requested_bytes: int,
    reason: str,
    timeout: float,
) -> AsyncIterator[CephStorageReservation]:
    """Hold a hard storage reservation for one Bertrand-owned write.

    Yields
    ------
    CephStorageReservation
        Ready reservation record.
    """
    deadline = _deadline_from_budget(timeout)
    reservation = await upsert_storage_reservation(
        kube,
        owner_kind=owner_kind,
        owner_name=owner_name,
        request_id=request_id,
        requested_bytes=requested_bytes,
        reason=reason,
        expires_at=datetime.now(UTC) + timedelta(seconds=max(1.0, timeout)),
        timeout=deadline.remaining(),
    )
    try:
        yield await wait_storage_reservation_ready(
            kube,
            reservation=reservation,
            timeout=deadline.remaining(),
        )
    finally:
        with suppress(OSError, TimeoutError, ValueError):
            await release_storage_reservation(
                kube,
                reservation=reservation,
                timeout=max(1.0, deadline.remaining()),
            )


async def create_storage_actions(
    kube: Kube,
    *,
    policy_generation: int,
    actions: Collection[PlannedStorageAction],
    timeout: float,
) -> None:
    """Create node-scoped storage action resources.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    policy_generation : int
        Policy generation that selected the actions.
    actions : Collection[PlannedStorageAction]
        Planned storage actions to create.
    timeout : float
        Maximum creation budget in seconds.
    """
    for action in actions:
        spec = _CephStorageActionSpec(
            policy_generation=policy_generation,
            operation=action.operation,
            node_name=action.node_name,
            host_id=action.host_id,
            osd_id=action.osd_id,
            target_bytes=action.target_bytes,
            pv_name=action.pv_name,
            lv_name=action.lv_name,
            storage_osd_name=action.storage_osd_name,
            reason=action.reason,
        )
        await STORAGE_ACTION_RESOURCE.create(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=f"{STORAGE_STATE_NAME}-{uuid.uuid4().hex[:12]}",
            spec=spec,
            timeout=timeout,
        )


async def upsert_storage_osd(
    kube: Kube,
    *,
    name: str,
    spec: Mapping[str, object],
    phase: StorageOSDPhase,
    timeout: float,
) -> CephStorageOSD:
    """Upsert one managed OSD record and refresh its phase status.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    name : str
        OSD inventory object name.
    spec : Mapping[str, object]
        Desired OSD identity/spec payload.
    phase : StorageOSDPhase
        Lifecycle phase to publish.
    timeout : float
        Maximum request budget in seconds.

    Returns
    -------
    CephStorageOSD
        Converged OSD inventory record.
    """
    now = datetime.now(UTC)
    state = await read_storage_state(kube, timeout=timeout)
    existing = state.status.osds.get(name)
    entry = CephStorageOSD.model_validate(
        {
            "name": name,
            **dict(spec),
            "phase": phase,
            "created_at": existing.created_at if existing is not None else now,
            "phase_changed_at": (
                existing.phase_changed_at
                if existing is not None
                and existing.phase == phase
                and existing.phase_changed_at is not None
                else now
            ),
            "last_seen_at": now,
            "last_error": "",
        }
    )
    refreshed = await _patch_storage_state_status(
        kube,
        status=state.status.model_copy(
            update={"osds": {**state.status.osds, name: entry}},
        ),
        timeout=timeout,
    )
    return refreshed.status.osds.get(name, entry)


async def patch_storage_osd_status(
    kube: Kube,
    *,
    osd: CephStorageOSD,
    status: Mapping[str, object],
    timeout: float,
) -> CephStorageOSD:
    """Patch the status for one managed OSD record.

    Returns
    -------
    CephStorageOSD
        Freshly validated OSD record returned by the Kubernetes API.
    """
    now = datetime.now(UTC)
    payload = {"last_seen_at": now.isoformat(), **dict(status)}
    phase = payload.get("phase")
    if isinstance(phase, str) and phase != osd.phase:
        payload.setdefault("phase_changed_at", now.isoformat())
    elif isinstance(phase, str) and "phase_changed_at" not in payload:
        if osd.phase_changed_at is not None:
            payload["phase_changed_at"] = osd.phase_changed_at.isoformat()
        else:
            payload["phase_changed_at"] = now.isoformat()
    patched = CephStorageOSD.model_validate(
        {**osd.model_dump(mode="python"), **payload}
    )
    state = await read_storage_state(kube, timeout=timeout)
    refreshed = await _patch_storage_state_status(
        kube,
        status=state.status.model_copy(
            update={"osds": {**state.status.osds, osd.name: patched}},
        ),
        timeout=timeout,
    )
    return refreshed.status.osds.get(osd.name, patched)


async def upsert_storage_node_report(
    kube: Kube,
    *,
    node_name: str,
    host_id: str,
    status: Mapping[str, object],
    timeout: float,
) -> None:
    """Upsert one node report and patch its current status.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    node_name : str
        Kubernetes node name reported by the agent.
    host_id : str
        Durable Bertrand host UUID reported by the agent.
    status : Mapping[str, object]
        Node report status payload.
    timeout : float
        Maximum update budget in seconds.
    """
    name = storage_node_report_name(host_id)
    state = await read_storage_state(kube, timeout=timeout)
    entry = CephStorageNodeReport.model_validate(
        {"name": name, "node_name": node_name, "host_id": host_id, **dict(status)}
    )
    await _patch_storage_state_status(
        kube,
        status=state.status.model_copy(
            update={"nodes": {**state.status.nodes, name: entry}},
        ),
        timeout=timeout,
    )


async def pending_storage_actions(
    kube: Kube,
    *,
    node_name: str,
    timeout: float,
) -> list[CephStorageActionRecord]:
    """List pending storage actions assigned to one node.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    node_name : str
        Node name to filter by.
    timeout : float
        Maximum request budget in seconds.

    Returns
    -------
    list[CephStorageActionRecord]
        Pending actions for the node.
    """
    actions = await list_storage_actions(kube, timeout=timeout)
    pending = [
        action
        for action in actions
        if action.spec.node_name == node_name and action.status.phase == "Pending"
    ]
    pending.sort(key=lambda action: action.name)
    return pending


async def patch_storage_action_status(
    kube: Kube,
    *,
    action: CephStorageActionRecord,
    status: Mapping[str, object],
    timeout: float,
) -> None:
    """Patch the status for one storage action.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    action : CephStorageActionRecord
        Storage action to patch.
    status : Mapping[str, object]
        Status fields to apply.
    timeout : float
        Maximum patch budget in seconds.
    """
    await STORAGE_ACTION_RESOURCE.patch_status(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=action.name,
        status=status,
        timeout=timeout,
    )
