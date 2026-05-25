"""Ceph capacity policy records and grow/shrink planning."""

from __future__ import annotations

import asyncio
import hashlib
import math
import uuid
from contextlib import asynccontextmanager, suppress
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta
from typing import TYPE_CHECKING, Annotated, Any, Literal, cast

from pydantic import (
    BaseModel,
    ConfigDict,
    Field,
    PositiveInt,
    ValidationError,
    field_validator,
    model_validator,
)

from bertrand.env.git import BERTRAND_ENV, BERTRAND_NAMESPACE, Deadline
from bertrand.env.kube.ceph.api import CephCapacitySnapshot, CephOSD, parse_size_bytes
from bertrand.env.kube.custom_object import CustomObjectMetadata  # noqa: TC001
from bertrand.env.kube.custom_resource import CustomResource, ensure_custom_resources

if TYPE_CHECKING:
    from collections.abc import AsyncIterator, Collection, Mapping

    from bertrand.env.kube.api.client import Kube

CEPH_CAPACITY_GROUP = "ceph.bertrand.dev"
CEPH_CAPACITY_VERSION = "v1alpha1"
STORAGE_POLICY_KIND = "CephStorageAutoscaler"
STORAGE_POLICY_PLURAL = "cephstorageautoscalers"
STORAGE_ACTION_KIND = "CephStorageAction"
STORAGE_ACTION_PLURAL = "cephstorageactions"
STORAGE_NODE_KIND = "CephStorageNode"
STORAGE_NODE_PLURAL = "cephstoragenodes"
STORAGE_OSD_KIND = "CephStorageOSD"
STORAGE_OSD_PLURAL = "cephstorageosds"
STORAGE_RESERVATION_KIND = "CephStorageReservation"
STORAGE_RESERVATION_PLURAL = "cephstoragereservations"
STORAGE_POLICY_NAME = "default"
STORAGE_CONTROLLER_LABEL = "bertrand.dev/ceph-storage-controller"
STORAGE_CONTROLLER_LABEL_VALUE = "v1"
STORAGE_OSD_LABEL = "bertrand.dev/ceph-storage-osd"
STORAGE_OSD_LABEL_VALUE = "v1"
STORAGE_OSD_NAME_LABEL = "bertrand.dev/ceph-storage-osd-name"
STORAGE_OSD_ORIGIN_LABEL = "bertrand.dev/ceph-storage-osd-origin"
STORAGE_OSD_PHASE_LABEL = "bertrand.dev/ceph-storage-osd-phase"
STORAGE_OSD_NODE_LABEL = "bertrand.dev/ceph-storage-osd-node"
STORAGE_OSD_HOST_LABEL = "bertrand.dev/ceph-storage-osd-host"
STORAGE_OSD_PV_LABEL = "bertrand.dev/ceph-storage-osd-pv"
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


class _CephStoragePolicyStatus(BaseModel):
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


class CephStoragePolicyRecord(BaseModel):
    """Validated `CephStorageAutoscaler` custom-resource payload."""

    model_config = ConfigDict(extra="forbid")
    api_version: str = Field(alias="apiVersion")
    kind: Literal["CephStorageAutoscaler"]
    metadata: CustomObjectMetadata
    spec: _CephStoragePolicySpec = Field(default_factory=_CephStoragePolicySpec)
    status: _CephStoragePolicyStatus | None = None

    @classmethod
    def from_payload(cls, payload: object) -> CephStoragePolicyRecord:
        """Validate a Kubernetes custom-object payload.

        Parameters
        ----------
        payload : object
            Raw custom-object payload returned by Kubernetes.

        Returns
        -------
        CephStoragePolicyRecord
            Validated storage policy record.

        Raises
        ------
        OSError
            If the payload is malformed.
        """
        try:
            return cls.model_validate(payload)
        except ValidationError as err:
            msg = f"malformed {STORAGE_POLICY_KIND} custom object: {err}"
            raise OSError(msg) from err


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

    @classmethod
    def from_payload(cls, payload: object) -> CephStorageActionRecord:
        """Validate a Kubernetes custom-object payload.

        Parameters
        ----------
        payload : object
            Raw custom-object payload returned by Kubernetes.

        Returns
        -------
        CephStorageActionRecord
            Validated storage action record.

        Raises
        ------
        OSError
            If the payload is malformed.
        """
        try:
            return cls.model_validate(payload)
        except ValidationError as err:
            msg = f"malformed {STORAGE_ACTION_KIND} custom object: {err}"
            raise OSError(msg) from err


class _CephStorageReservationSpec(BaseModel):
    """Desired storage reservation for one Bertrand-owned write path."""

    model_config = ConfigDict(extra="forbid")
    owner_kind: Annotated[str, Field(min_length=1)]
    owner_name: Annotated[str, Field(min_length=1)]
    request_id: Annotated[str, Field(min_length=1)]
    requested_bytes: Annotated[int, Field(ge=1)]
    reason: Annotated[str, Field(min_length=1)]
    expires_at: datetime


class _CephStorageReservationStatus(BaseModel):
    """Observed readiness state for one storage reservation."""

    model_config = ConfigDict(extra="forbid")
    phase: StorageReservationPhase = "Pending"
    ready_at: datetime | None = None
    released_at: datetime | None = None
    observed_free_bytes: Annotated[int, Field(ge=0)] = 0
    last_error: str = ""


class CephStorageReservationRecord(BaseModel):
    """Validated `CephStorageReservation` custom-resource payload."""

    model_config = ConfigDict(extra="forbid")
    api_version: str = Field(alias="apiVersion")
    kind: Literal["CephStorageReservation"]
    metadata: CustomObjectMetadata
    spec: _CephStorageReservationSpec
    status: _CephStorageReservationStatus = Field(
        default_factory=_CephStorageReservationStatus
    )

    @classmethod
    def from_payload(cls, payload: object) -> CephStorageReservationRecord:
        """Validate a Kubernetes custom-object payload.

        Parameters
        ----------
        payload : object
            Raw custom-object payload returned by Kubernetes.

        Returns
        -------
        CephStorageReservationRecord
            Validated storage reservation record.

        Raises
        ------
        OSError
            If the payload is malformed.
        """
        try:
            return cls.model_validate(payload)
        except ValidationError as err:
            msg = f"malformed {STORAGE_RESERVATION_KIND} custom object: {err}"
            raise OSError(msg) from err

    def expires_at_utc(self) -> datetime:
        """Return the reservation expiry timestamp normalized to UTC.

        Returns
        -------
        datetime
            UTC-normalized expiry timestamp.
        """
        expires_at = self.spec.expires_at
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
        return self.status.phase in {"Pending", "Ready"} and self.expires_at_utc() > now


class _CephStorageNodeSpec(BaseModel):
    """Desired identity contract for one node capacity report."""

    model_config = ConfigDict(extra="forbid")
    node_name: Annotated[str, Field(min_length=1)]
    host_id: Annotated[str, Field(min_length=1)]


class _CephStorageNodePVStatus(BaseModel):
    """Observed free capacity for one physical volume in the Bertrand VG."""

    model_config = ConfigDict(extra="forbid")
    pv_name: Annotated[str, Field(min_length=1)]
    pv_uuid: Annotated[str, Field(min_length=1)]
    pv_size_bytes: Annotated[int, Field(ge=0)] = 0
    pv_free_bytes: Annotated[int, Field(ge=0)] = 0


class _CephStorageNodeStatus(BaseModel):
    """Observed host-local capacity state reported by one node agent."""

    model_config = ConfigDict(extra="forbid")
    free_bytes: Annotated[int, Field(ge=0)] = 0
    path: str = ""
    lvm_free_bytes: Annotated[int, Field(ge=0)] = 0
    lvm_pvs: tuple[str, ...] = ()
    lvm_pv_inventory: tuple[_CephStorageNodePVStatus, ...] = ()
    loop_fallback_active: bool = False
    heartbeat_at: datetime | None = None
    last_error: str = ""


class CephStorageNodeRecord(BaseModel):
    """Validated `CephStorageNode` custom-resource payload."""

    model_config = ConfigDict(extra="forbid")
    api_version: str = Field(alias="apiVersion")
    kind: Literal["CephStorageNode"]
    metadata: CustomObjectMetadata
    spec: _CephStorageNodeSpec
    status: _CephStorageNodeStatus | None = None

    @classmethod
    def from_payload(cls, payload: object) -> CephStorageNodeRecord:
        """Validate a Kubernetes custom-object payload.

        Parameters
        ----------
        payload : object
            Raw custom-object payload returned by Kubernetes.

        Returns
        -------
        CephStorageNodeRecord
            Validated storage node report.

        Raises
        ------
        OSError
            If the payload is malformed.
        """
        try:
            return cls.model_validate(payload)
        except ValidationError as err:
            msg = f"malformed {STORAGE_NODE_KIND} custom object: {err}"
            raise OSError(msg) from err


class _CephStorageOSDSpec(BaseModel):
    """Desired/storage identity for one Bertrand-managed Rook OSD substrate."""

    model_config = ConfigDict(extra="forbid")
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

    @model_validator(mode="after")
    def _validate_origin_fields(self) -> _CephStorageOSDSpec:
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


class _CephStorageOSDStatus(BaseModel):
    """Observed lifecycle state for one managed OSD substrate."""

    model_config = ConfigDict(extra="forbid")
    phase: StorageOSDPhase = "Pending"
    observed_bytes: Annotated[int, Field(ge=0)] = 0
    ceph_osd_id: Annotated[int, Field(ge=0)] | None = None
    created_at: datetime | None = None
    phase_changed_at: datetime | None = None
    last_seen_at: datetime | None = None
    retired_at: datetime | None = None
    last_error: str = ""


class CephStorageOSDRecord(BaseModel):
    """Validated `CephStorageOSD` custom-resource payload."""

    model_config = ConfigDict(extra="forbid")
    api_version: str = Field(alias="apiVersion")
    kind: Literal["CephStorageOSD"]
    metadata: CustomObjectMetadata
    spec: _CephStorageOSDSpec
    status: _CephStorageOSDStatus = Field(default_factory=_CephStorageOSDStatus)

    @classmethod
    def from_payload(cls, payload: object) -> CephStorageOSDRecord:
        """Validate a Kubernetes custom-object payload.

        Parameters
        ----------
        payload : object
            Raw custom-object payload returned by Kubernetes.

        Returns
        -------
        CephStorageOSDRecord
            Validated storage OSD record.

        Raises
        ------
        OSError
            If the payload is malformed.
        """
        try:
            return cls.model_validate(payload)
        except ValidationError as err:
            msg = f"malformed {STORAGE_OSD_KIND} custom object: {err}"
            raise OSError(msg) from err


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
        osds: Collection[CephStorageOSDRecord],
    ) -> StorageOSDInventory:
        """Return loop/LVM OSD inventory inferred from durable OSD records.

        Parameters
        ----------
        osds : Collection[CephStorageOSDRecord]
            OSD records to inspect.

        Returns
        -------
        StorageOSDInventory
            Quality-oriented OSD inventory.
        """
        ready = [record for record in osds if record.status.phase == "Ready"]
        loop_ids = {
            record.status.ceph_osd_id
            for record in ready
            if record.spec.origin == "loop-fallback"
            and record.status.ceph_osd_id is not None
        }
        lvm_ids = {
            record.status.ceph_osd_id
            for record in ready
            if record.spec.origin == "lvm-pv" and record.status.ceph_osd_id is not None
        }
        elastic_bytes = sum(
            record.status.observed_bytes or record.spec.target_bytes
            for record in ready
            if record.spec.origin == "loop-fallback"
        )
        durable_bytes = sum(
            record.status.observed_bytes or record.spec.target_bytes
            for record in ready
            if record.spec.origin == "lvm-pv"
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
        osds: Collection[CephStorageOSDRecord],
    ) -> bool:
        """Return whether Rook is still admitting or retiring a managed OSD.

        Parameters
        ----------
        osds : Collection[CephStorageOSDRecord]
            Durable OSD inventory records to inspect.

        Returns
        -------
        bool
            True when any active OSD record is not yet terminal or ready.
        """
        return any(
            record.status.phase in STORAGE_OSD_IN_FLIGHT_PHASES for record in osds
        )

    def managed_osds(
        self,
        *,
        osd_records: Collection[CephStorageOSDRecord],
        osds: Collection[CephOSD],
    ) -> list[_ManagedOSD]:
        """Return shrink-eligible managed OSD inventory.

        Parameters
        ----------
        osd_records : Collection[CephStorageOSDRecord]
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
            if record.spec.origin != "loop-fallback" or record.status.phase != "Ready":
                continue
            osd_id = record.status.ceph_osd_id
            if osd_id is None:
                continue
            osd = live.get(osd_id)
            if osd is None:
                continue
            candidates.append(
                _ManagedOSD(
                    osd_id=osd_id,
                    node_name=record.spec.node_name or osd.node_name,
                    host_id=record.spec.host_id,
                    size_bytes=record.status.observed_bytes or record.spec.target_bytes,
                    created_at=self.utc(record.status.created_at),
                )
            )
        return candidates

    @staticmethod
    def eligible_nodes(
        *,
        ready_nodes: Collection[str],
        reports: Collection[CephStorageNodeRecord],
        actions: Collection[CephStorageActionRecord],
        osds: Collection[CephStorageOSDRecord],
        growth_bytes: int,
    ) -> list[_EligibleStorageNode]:
        """Return deterministic node slots eligible for storage expansion.

        Parameters
        ----------
        ready_nodes : Collection[str]
            Kubernetes nodes currently ready for Bertrand registry pulls.
        reports : Collection[CephStorageNodeRecord]
            Node-local free-space reports.
        actions : Collection[CephStorageActionRecord]
            Existing storage actions used to apply failed-target retry cooldowns.
        osds : Collection[CephStorageOSDRecord]
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
            if record.status.phase not in {"Failed", "Retired", "Retiring"}
        ]
        failed_targets = CephStoragePlanner._failed_osd_targets_in_cooldown(
            osds, now=now
        ) | CephStoragePlanner._failed_action_targets_in_cooldown(actions, now=now)
        lvm_by_pv = {
            (record.spec.host_id, record.spec.pv_uuid): record
            for record in active_osds
            if record.spec.origin == "lvm-pv" and record.spec.pv_uuid
        }
        loop_by_host = {
            record.spec.host_id: record
            for record in active_osds
            if record.spec.origin == "loop-fallback"
        }
        for report in reports:
            status = report.status
            if report.spec.node_name not in ready or status is None:
                continue
            heartbeat = status.heartbeat_at
            if heartbeat is None:
                continue
            if heartbeat.tzinfo is None:
                heartbeat = heartbeat.replace(tzinfo=UTC)
            heartbeat = heartbeat.astimezone(UTC)
            if (now - heartbeat).total_seconds() > STORAGE_NODE_REPORT_MAX_AGE_SECONDS:
                continue
            for pv in status.lvm_pv_inventory:
                if pv.pv_free_bytes < growth_bytes:
                    continue
                existing = lvm_by_pv.get((report.spec.host_id, pv.pv_uuid))
                target_name = (
                    existing.metadata.name
                    if existing is not None
                    else storage_lvm_osd_name(report.spec.host_id, pv.pv_uuid)
                )
                if target_name in failed_targets:
                    continue
                lvm.append(
                    _EligibleStorageNode(
                        node_name=report.spec.node_name,
                        host_id=report.spec.host_id,
                        operation="expand-lvm",
                        pv_name=pv.pv_name,
                        pv_uuid=pv.pv_uuid,
                        pv_free_bytes=pv.pv_free_bytes,
                        lv_name=existing.spec.lv_name if existing is not None else None,
                        storage_osd_name=target_name,
                        current_bytes=(
                            existing.spec.target_bytes if existing is not None else 0
                        ),
                        available_bytes=pv.pv_free_bytes,
                        target_bytes=(
                            existing.spec.target_bytes + growth_bytes
                            if existing is not None
                            else growth_bytes
                        ),
                    )
                )
            if status.free_bytes >= growth_bytes:
                existing = loop_by_host.get(report.spec.host_id)
                target_name = (
                    existing.metadata.name
                    if existing is not None
                    else storage_loop_osd_name(report.spec.host_id)
                )
                if target_name in failed_targets:
                    continue
                loop.append(
                    _EligibleStorageNode(
                        node_name=report.spec.node_name,
                        host_id=report.spec.host_id,
                        operation="expand-loop",
                        storage_osd_name=target_name,
                        current_bytes=(
                            existing.spec.target_bytes if existing is not None else 0
                        ),
                        available_bytes=status.free_bytes,
                        target_bytes=(
                            existing.spec.target_bytes + growth_bytes
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
        osds: Collection[CephStorageOSDRecord],
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
            if record.status.phase != "Failed":
                continue
            failed_at = (
                CephStoragePlanner.utc(record.status.phase_changed_at)
                or CephStoragePlanner.utc(record.status.last_seen_at)
                or CephStoragePlanner.utc(record.status.created_at)
            )
            if failed_at is None:
                continue
            if (
                now - failed_at
            ).total_seconds() < STORAGE_TARGET_RETRY_COOLDOWN_SECONDS:
                names.add(record.metadata.name)
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
        reservations: Collection[CephStorageReservationRecord],
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
            reservation.spec.requested_bytes
            for reservation in reservations
            if reservation.is_active(now)
        )

    def growth_recommendation(
        self,
        *,
        policy: CephStoragePolicyRecord,
        capacity: CephCapacitySnapshot,
        reservations: Collection[CephStorageReservationRecord],
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
            policy.status.write_rate_ewma_bytes_per_second
            if policy.status is not None
            else 0.0
        )
        instantaneous_rate = 0.0
        if (
            policy.status is not None
            and policy.status.used_bytes is not None
            and policy.status.last_reconciled_at is not None
        ):
            previous_time = self.utc(policy.status.last_reconciled_at)
            if previous_time is not None:
                elapsed = max(0.0, (now - previous_time).total_seconds())
                if elapsed > 0:
                    delta = max(0, capacity.used_bytes - policy.status.used_bytes)
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
        policy: CephStoragePolicyRecord,
        capacity: CephCapacitySnapshot,
        actions: Collection[CephStorageActionRecord],
        osd_records: Collection[CephStorageOSDRecord],
        eligible_nodes: list[_EligibleStorageNode],
        growth: StorageGrowthRecommendation,
        min_growth_bytes: int,
    ) -> list[PlannedStorageAction]:
        """Return expand actions needed to reach the policy target watermark.

        Parameters
        ----------
        policy : CephStoragePolicyRecord
            Active storage policy.
        capacity : CephCapacitySnapshot
            Current Ceph capacity snapshot.
        actions : Collection[CephStorageActionRecord]
            Existing storage action records.
        osd_records : Collection[CephStorageOSDRecord]
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
        policy: CephStoragePolicyRecord,
        actions: Collection[CephStorageActionRecord],
        osd_records: Collection[CephStorageOSDRecord],
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
        osd_records: Collection[CephStorageOSDRecord],
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
            if record.spec.origin != "lvm-pv" or record.status.phase != "Ready":
                continue
            osd_id = record.status.ceph_osd_id
            if osd_id is None or osd_id not in live:
                continue
            candidates.append(
                _ManagedLVMOSD(
                    name=record.metadata.name,
                    osd_id=osd_id,
                    node_name=record.spec.node_name,
                    host_id=record.spec.host_id,
                    pv_name=record.spec.pv_name,
                    pv_uuid=record.spec.pv_uuid,
                    lv_name=record.spec.lv_name,
                    size_bytes=record.status.observed_bytes or record.spec.target_bytes,
                    created_at=self.utc(record.status.created_at),
                )
            )
        return candidates

    def lvm_shrink_preview(
        self,
        *,
        policy: CephStoragePolicyRecord,
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
        policy: CephStoragePolicyRecord,
        capacity: CephCapacitySnapshot,
        actions: Collection[CephStorageActionRecord],
        osd_records: Collection[CephStorageOSDRecord],
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
        policy: CephStoragePolicyRecord,
        capacity: CephCapacitySnapshot,
        actions: Collection[CephStorageActionRecord],
        candidates: Collection[_ManagedOSD],
    ) -> list[PlannedStorageAction]:
        """Return one conservative shrink action when every safety gate passes.

        Parameters
        ----------
        policy : CephStoragePolicyRecord
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
        policy: CephStoragePolicyRecord,
        capacity: CephCapacitySnapshot,
        actions: Collection[CephStorageActionRecord],
        eligible_nodes: list[_EligibleStorageNode],
        candidates: Collection[_ManagedOSD],
        growth_bytes: int,
    ) -> list[PlannedStorageAction]:
        """Return LVM growth or loop retirement needed to evacuate fallback OSDs.

        Parameters
        ----------
        policy : CephStoragePolicyRecord
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


_STORAGE_POLICY_SPEC_SCHEMA = {
    "type": "object",
    "properties": {
        "enabled": {"type": "boolean", "default": True},
        "high_watermark": {
            "type": "number",
            "minimum": 0,
            "maximum": 1,
            "default": 0.75,
        },
        "target_watermark": {
            "type": "number",
            "minimum": 0,
            "maximum": 1,
            "default": 0.65,
        },
        "shrink_enabled": {"type": "boolean", "default": True},
        "low_watermark": {
            "type": "number",
            "minimum": 0,
            "maximum": 1,
            "default": 0.45,
        },
        "shrink_target_watermark": {
            "type": "number",
            "minimum": 0,
            "maximum": 1,
            "default": 0.60,
        },
        "shrink_cooldown_seconds": {
            "type": "integer",
            "minimum": 1,
            "default": 3600,
        },
        "min_lvm_osd_size": {
            "type": "string",
            "pattern": r"^[1-9][0-9]*[MGT]$",
            "default": "16G",
        },
        "lvm_shrink_min_reclaim": {
            "type": "string",
            "pattern": r"^[1-9][0-9]*[MGT]$",
            "default": "16G",
        },
        "growth_step": {
            "type": "string",
            "pattern": r"^[1-9][0-9]*[MGT]$",
            "default": "16G",
        },
        "min_growth_step": {
            "type": "string",
            "pattern": r"^[1-9][0-9]*[MGT]$",
            "default": "16G",
        },
        "max_growth_per_reconcile": {
            "type": "string",
            "pattern": r"^[1-9][0-9]*[MGT]$",
            "default": "128G",
        },
        "target_headroom_ratio": {
            "type": "number",
            "minimum": 0,
            "maximum": 1,
            "default": 0.35,
        },
        "min_headroom": {
            "type": "string",
            "pattern": r"^[1-9][0-9]*[MGT]$",
            "default": "16G",
        },
        "burst_window_seconds": {"type": "integer", "minimum": 1, "default": 900},
        "burst_multiplier": {"type": "number", "exclusiveMinimum": 0, "default": 2.0},
        "write_rate_ewma_alpha": {
            "type": "number",
            "minimum": 0,
            "maximum": 1,
            "default": 0.35,
        },
        "default_write_reservation": {
            "type": "string",
            "pattern": r"^[1-9][0-9]*[MGT]$",
            "default": "16G",
        },
        "max_actions_per_reconcile": {"type": "integer", "minimum": 1, "default": 3},
        "reconcile_interval_seconds": {"type": "integer", "minimum": 1, "default": 30},
    },
}
_STORAGE_POLICY_STATUS_SCHEMA = {
    "type": "object",
    "properties": {
        "observedGeneration": {"type": "integer"},
        "total_bytes": {"type": "integer"},
        "used_bytes": {"type": "integer"},
        "used_ratio": {"type": "number"},
        "free_bytes": {"type": "integer", "nullable": True},
        "headroom_target_bytes": {"type": "integer"},
        "reserved_bytes": {"type": "integer"},
        "write_rate_ewma_bytes_per_second": {"type": "number"},
        "projected_seconds_to_headroom_floor": {
            "type": "number",
            "nullable": True,
        },
        "growth_recommendation_bytes": {"type": "integer"},
        "pending_actions": {"type": "integer"},
        "running_actions": {"type": "integer"},
        "succeeded_actions": {"type": "integer"},
        "failed_actions": {"type": "integer"},
        "managed_osds": {"type": "integer"},
        "loop_osds": {"type": "integer"},
        "lvm_osds": {"type": "integer"},
        "elastic_bytes": {"type": "integer"},
        "durable_bytes": {"type": "integer"},
        "lvm_preferred": {"type": "boolean"},
        "shrink_candidates": {"type": "integer"},
        "missing_lvm_osd_pvs": {"type": "integer"},
        "lvm_reclaimable_bytes": {"type": "integer"},
        "lvm_shrink_candidate": {"type": "string"},
        "lvm_shrink_target_bytes": {"type": "integer"},
        "last_shrink_at": {
            "type": "string",
            "format": "date-time",
            "nullable": True,
        },
        "last_reconciled_at": {"type": "string", "format": "date-time"},
        "last_error": {"type": "string"},
    },
}
_STORAGE_ACTION_SPEC_SCHEMA = {
    "type": "object",
    "required": ["policy_generation", "operation", "node_name", "host_id", "reason"],
    "properties": {
        "policy_generation": {"type": "integer", "minimum": 0},
        "operation": {
            "type": "string",
            "enum": ["expand-lvm", "expand-loop", "retire-loop", "shrink-lvm"],
        },
        "node_name": {"type": "string", "minLength": 1},
        "host_id": {"type": "string", "minLength": 1},
        "osd_id": {"type": "integer", "minimum": 0, "nullable": True},
        "target_bytes": {"type": "integer", "minimum": 1, "nullable": True},
        "pv_name": {"type": "string", "nullable": True},
        "lv_name": {"type": "string", "nullable": True},
        "storage_osd_name": {"type": "string", "nullable": True},
        "reason": {"type": "string", "minLength": 1},
    },
}
_STORAGE_ACTION_STATUS_SCHEMA = {
    "type": "object",
    "properties": {
        "phase": {"type": "string", "enum": list(STORAGE_ACTION_PHASES)},
        "started_at": {"type": "string", "format": "date-time"},
        "finished_at": {"type": "string", "format": "date-time"},
        "message": {"type": "string"},
        "diagnostics": {"type": "string"},
        "worker_node": {"type": "string"},
        "created_osd_ids": {
            "type": "array",
            "items": {"type": "integer", "minimum": 0},
        },
        "removed_osd_ids": {
            "type": "array",
            "items": {"type": "integer", "minimum": 0},
        },
        "osd_origin": {
            "type": "string",
            "enum": ["lvm-pv", "loop-fallback"],
            "nullable": True,
        },
        "osd_quality": {
            "type": "string",
            "enum": ["elastic", "durable"],
            "nullable": True,
        },
        "source_pv": {"type": "string"},
        "source_lv": {"type": "string"},
        "provisioned_bytes": {
            "type": "integer",
            "minimum": 0,
            "nullable": True,
        },
    },
}
_STORAGE_RESERVATION_SPEC_SCHEMA = {
    "type": "object",
    "required": [
        "owner_kind",
        "owner_name",
        "request_id",
        "requested_bytes",
        "reason",
        "expires_at",
    ],
    "properties": {
        "owner_kind": {"type": "string", "minLength": 1},
        "owner_name": {"type": "string", "minLength": 1},
        "request_id": {"type": "string", "minLength": 1},
        "requested_bytes": {"type": "integer", "minimum": 1},
        "reason": {"type": "string", "minLength": 1},
        "expires_at": {"type": "string", "format": "date-time"},
    },
}
_STORAGE_RESERVATION_STATUS_SCHEMA = {
    "type": "object",
    "properties": {
        "phase": {"type": "string", "enum": list(STORAGE_RESERVATION_PHASES)},
        "ready_at": {"type": "string", "format": "date-time", "nullable": True},
        "released_at": {"type": "string", "format": "date-time", "nullable": True},
        "observed_free_bytes": {"type": "integer", "minimum": 0},
        "last_error": {"type": "string"},
    },
}
_STORAGE_NODE_SPEC_SCHEMA = {
    "type": "object",
    "required": ["node_name", "host_id"],
    "properties": {
        "node_name": {"type": "string", "minLength": 1},
        "host_id": {"type": "string", "minLength": 1},
    },
}
_STORAGE_NODE_STATUS_SCHEMA = {
    "type": "object",
    "properties": {
        "free_bytes": {"type": "integer", "minimum": 0},
        "path": {"type": "string"},
        "lvm_free_bytes": {"type": "integer", "minimum": 0},
        "lvm_pvs": {"type": "array", "items": {"type": "string"}},
        "lvm_pv_inventory": {
            "type": "array",
            "items": {
                "type": "object",
                "required": ["pv_name", "pv_uuid"],
                "properties": {
                    "pv_name": {"type": "string", "minLength": 1},
                    "pv_uuid": {"type": "string", "minLength": 1},
                    "pv_size_bytes": {"type": "integer", "minimum": 0},
                    "pv_free_bytes": {"type": "integer", "minimum": 0},
                },
            },
        },
        "loop_fallback_active": {"type": "boolean"},
        "heartbeat_at": {"type": "string", "format": "date-time"},
        "last_error": {"type": "string"},
    },
}
_STORAGE_OSD_SPEC_SCHEMA = {
    "type": "object",
    "required": [
        "origin",
        "node_name",
        "host_id",
        "block_path",
        "device_set_name",
        "target_bytes",
    ],
    "properties": {
        "origin": {"type": "string", "enum": ["lvm-pv", "loop-fallback"]},
        "node_name": {"type": "string", "minLength": 1},
        "host_id": {"type": "string", "minLength": 1},
        "pv_name": {"type": "string"},
        "pv_uuid": {"type": "string"},
        "pv_device": {"type": "string"},
        "lv_name": {"type": "string"},
        "lv_path": {"type": "string"},
        "loop_file": {"type": "string"},
        "loop_device": {"type": "string"},
        "block_path": {"type": "string", "minLength": 1},
        "csi_volume_id": {"type": "string"},
        "persistent_volume_name": {"type": "string"},
        "persistent_volume_claim_namespace": {"type": "string"},
        "persistent_volume_claim_name": {"type": "string"},
        "device_set_name": {"type": "string", "minLength": 1},
        "target_bytes": {"type": "integer", "minimum": 1},
    },
}
_STORAGE_OSD_STATUS_SCHEMA = {
    "type": "object",
    "properties": {
        "phase": {"type": "string", "enum": list(STORAGE_OSD_PHASES)},
        "observed_bytes": {"type": "integer", "minimum": 0},
        "ceph_osd_id": {"type": "integer", "minimum": 0, "nullable": True},
        "created_at": {
            "type": "string",
            "format": "date-time",
            "nullable": True,
        },
        "phase_changed_at": {
            "type": "string",
            "format": "date-time",
            "nullable": True,
        },
        "last_seen_at": {
            "type": "string",
            "format": "date-time",
            "nullable": True,
        },
        "retired_at": {
            "type": "string",
            "format": "date-time",
            "nullable": True,
        },
        "last_error": {"type": "string"},
    },
}

STORAGE_POLICY_RESOURCE = CustomResource[CephStoragePolicyRecord](
    group=CEPH_CAPACITY_GROUP,
    version=CEPH_CAPACITY_VERSION,
    kind=STORAGE_POLICY_KIND,
    plural=STORAGE_POLICY_PLURAL,
    singular="cephstorageautoscaler",
    short_names=("csa",),
    parser=CephStoragePolicyRecord.from_payload,
    spec_schema=_STORAGE_POLICY_SPEC_SCHEMA,
    status_schema=_STORAGE_POLICY_STATUS_SCHEMA,
    labels=STORAGE_CONTROLLER_LABELS,
)
STORAGE_ACTION_RESOURCE = CustomResource[CephStorageActionRecord](
    group=CEPH_CAPACITY_GROUP,
    version=CEPH_CAPACITY_VERSION,
    kind=STORAGE_ACTION_KIND,
    plural=STORAGE_ACTION_PLURAL,
    singular="cephstorageaction",
    short_names=("csact",),
    parser=CephStorageActionRecord.from_payload,
    spec_schema=_STORAGE_ACTION_SPEC_SCHEMA,
    status_schema=_STORAGE_ACTION_STATUS_SCHEMA,
    labels=STORAGE_CONTROLLER_LABELS,
)
STORAGE_RESERVATION_RESOURCE = CustomResource[CephStorageReservationRecord](
    group=CEPH_CAPACITY_GROUP,
    version=CEPH_CAPACITY_VERSION,
    kind=STORAGE_RESERVATION_KIND,
    plural=STORAGE_RESERVATION_PLURAL,
    singular="cephstoragereservation",
    short_names=("csres",),
    parser=CephStorageReservationRecord.from_payload,
    spec_schema=_STORAGE_RESERVATION_SPEC_SCHEMA,
    status_schema=_STORAGE_RESERVATION_STATUS_SCHEMA,
    labels=STORAGE_CONTROLLER_LABELS,
)
STORAGE_NODE_RESOURCE = CustomResource[CephStorageNodeRecord](
    group=CEPH_CAPACITY_GROUP,
    version=CEPH_CAPACITY_VERSION,
    kind=STORAGE_NODE_KIND,
    plural=STORAGE_NODE_PLURAL,
    singular="cephstoragenode",
    short_names=("csnode",),
    parser=CephStorageNodeRecord.from_payload,
    spec_schema=_STORAGE_NODE_SPEC_SCHEMA,
    status_schema=_STORAGE_NODE_STATUS_SCHEMA,
    labels=STORAGE_CONTROLLER_LABELS,
)
STORAGE_OSD_RESOURCE = CustomResource[CephStorageOSDRecord](
    group=CEPH_CAPACITY_GROUP,
    version=CEPH_CAPACITY_VERSION,
    kind=STORAGE_OSD_KIND,
    plural=STORAGE_OSD_PLURAL,
    singular="cephstorageosd",
    short_names=("csosd",),
    parser=CephStorageOSDRecord.from_payload,
    spec_schema=_STORAGE_OSD_SPEC_SCHEMA,
    status_schema=_STORAGE_OSD_STATUS_SCHEMA,
    labels={**STORAGE_CONTROLLER_LABELS, STORAGE_OSD_LABEL: STORAGE_OSD_LABEL_VALUE},
    crd_labels=STORAGE_CONTROLLER_LABELS,
)
_STORAGE_RESOURCES: tuple[CustomResource[Any], ...] = (
    STORAGE_POLICY_RESOURCE,
    STORAGE_ACTION_RESOURCE,
    STORAGE_RESERVATION_RESOURCE,
    STORAGE_NODE_RESOURCE,
    STORAGE_OSD_RESOURCE,
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
    await ensure_custom_resources(kube, resources=_STORAGE_RESOURCES, timeout=timeout)


async def ensure_default_storage_policy(kube: Kube, *, timeout: float) -> None:
    """Converge the singleton default storage policy record.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum convergence budget in seconds.
    """
    deadline = _deadline_from_budget(timeout)
    await STORAGE_POLICY_RESOURCE.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=STORAGE_POLICY_NAME,
        spec=cast(
            "dict[str, object]",
            _CephStoragePolicySpec().model_dump(mode="json"),
        ),
        timeout=deadline.remaining(),
    )


def storage_watch_targets() -> tuple[tuple[CustomResource[Any], str], ...]:
    """Return capacity resources watched by the storage controller.

    Returns
    -------
    tuple[tuple[CustomResource[Any], str], ...]
        Resource/context pairs for storage policy, actions, and node reports.
    """
    return (
        (STORAGE_POLICY_RESOURCE, STORAGE_POLICY_PLURAL),
        (STORAGE_ACTION_RESOURCE, STORAGE_ACTION_PLURAL),
        (STORAGE_RESERVATION_RESOURCE, STORAGE_RESERVATION_PLURAL),
        (STORAGE_NODE_RESOURCE, STORAGE_NODE_PLURAL),
        (STORAGE_OSD_RESOURCE, STORAGE_OSD_PLURAL),
    )


async def read_storage_policy(kube: Kube, *, timeout: float) -> CephStoragePolicyRecord:
    """Read and validate the singleton storage policy.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum request budget in seconds.

    Returns
    -------
    CephStoragePolicyRecord
        Validated singleton storage policy.

    Raises
    ------
    OSError
        If the singleton policy resource does not exist.
    """
    record = await STORAGE_POLICY_RESOURCE.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=STORAGE_POLICY_NAME,
        timeout=timeout,
    )
    if record is None:
        msg = f"{STORAGE_POLICY_KIND} {STORAGE_POLICY_NAME!r} is missing"
        raise OSError(msg)
    return record


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


async def list_storage_reservations(
    kube: Kube, *, timeout: float
) -> list[CephStorageReservationRecord]:
    """List and validate storage reservation resources.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum request budget in seconds.

    Returns
    -------
    list[CephStorageReservationRecord]
        Validated storage reservation records.
    """
    return await STORAGE_RESERVATION_RESOURCE.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        timeout=timeout,
    )


async def read_storage_reservation(
    kube: Kube,
    *,
    name: str,
    timeout: float,
) -> CephStorageReservationRecord | None:
    """Read one storage reservation by name.

    Returns
    -------
    CephStorageReservationRecord | None
        Validated reservation, or None when it does not exist.
    """
    return await STORAGE_RESERVATION_RESOURCE.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=name,
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
) -> CephStorageReservationRecord:
    """Create or refresh a pending storage reservation.

    Returns
    -------
    CephStorageReservationRecord
        Validated reservation record.
    """
    name = storage_reservation_name(
        owner_kind=owner_kind,
        owner_name=owner_name,
        request_id=request_id,
    )
    spec = _CephStorageReservationSpec(
        owner_kind=owner_kind,
        owner_name=owner_name,
        request_id=request_id,
        requested_bytes=requested_bytes,
        reason=reason,
        expires_at=expires_at,
    ).model_dump(mode="json")
    record = await STORAGE_RESERVATION_RESOURCE.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=name,
        spec=cast("dict[str, object]", spec),
        timeout=timeout,
    )
    await STORAGE_RESERVATION_RESOURCE.patch_status(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=name,
        status={
            "phase": "Pending",
            "ready_at": None,
            "released_at": None,
            "observed_free_bytes": 0,
            "last_error": "",
        },
        timeout=timeout,
    )
    refreshed = await STORAGE_RESERVATION_RESOURCE.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=name,
        timeout=timeout,
    )
    return refreshed or record


async def patch_storage_reservation_status(
    kube: Kube,
    *,
    reservation: CephStorageReservationRecord,
    status: Mapping[str, object],
    timeout: float,
) -> CephStorageReservationRecord:
    """Patch one storage reservation status.

    Returns
    -------
    CephStorageReservationRecord
        Freshly validated reservation returned by the Kubernetes API.
    """
    return await STORAGE_RESERVATION_RESOURCE.patch_status(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=reservation.metadata.name,
        status=dict(status),
        timeout=timeout,
    )


async def release_storage_reservation(
    kube: Kube,
    *,
    reservation: CephStorageReservationRecord,
    timeout: float,
) -> None:
    """Mark a reservation released.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    reservation : CephStorageReservationRecord
        Reservation to release.
    timeout : float
        Maximum request budget in seconds.
    """
    if reservation.status.phase in {"Released", "Expired", "Failed"}:
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
    reservation: CephStorageReservationRecord,
    timeout: float,
) -> CephStorageReservationRecord:
    """Wait until a storage reservation becomes ready.

    Returns
    -------
    CephStorageReservationRecord
        Reservation in `Ready` phase.

    Raises
    ------
    OSError
        If the reservation disappears or reaches a terminal unusable phase.
    TimeoutError
        If the reservation does not become ready before `timeout`.
    """
    msg = (
        f"storage reservation {reservation.metadata.name!r} was not ready before "
        "the operation timeout; run `bertrand cluster storage doctor`"
    )
    deadline = Deadline.from_timeout(timeout, message=msg)
    while deadline.remaining() > 0:
        fresh = await read_storage_reservation(
            kube,
            name=reservation.metadata.name,
            timeout=deadline.remaining(),
        )
        if fresh is None:
            msg = f"storage reservation {reservation.metadata.name!r} disappeared"
            raise OSError(msg)
        if fresh.status.phase == "Ready":
            return fresh
        if fresh.status.phase in {"Failed", "Expired", "Released"}:
            detail = fresh.status.last_error or f"phase is {fresh.status.phase}"
            msg = f"storage reservation {fresh.metadata.name!r} is not usable: {detail}"
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
) -> AsyncIterator[CephStorageReservationRecord]:
    """Hold a hard storage reservation for one Bertrand-owned write.

    Yields
    ------
    CephStorageReservationRecord
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


async def list_storage_node_reports(
    kube: Kube, *, timeout: float
) -> list[CephStorageNodeRecord]:
    """List and validate node capacity report resources.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum request budget in seconds.

    Returns
    -------
    list[CephStorageNodeRecord]
        Validated node capacity report records.
    """
    return await STORAGE_NODE_RESOURCE.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        timeout=timeout,
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
        spec: dict[str, object] = {
            "policy_generation": policy_generation,
            "operation": action.operation,
            "node_name": action.node_name,
            "host_id": action.host_id,
            "reason": action.reason,
        }
        if action.osd_id is not None:
            spec["osd_id"] = action.osd_id
        if action.target_bytes is not None:
            spec["target_bytes"] = action.target_bytes
        if action.pv_name is not None:
            spec["pv_name"] = action.pv_name
        if action.lv_name is not None:
            spec["lv_name"] = action.lv_name
        if action.storage_osd_name is not None:
            spec["storage_osd_name"] = action.storage_osd_name
        await STORAGE_ACTION_RESOURCE.create(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=f"{STORAGE_POLICY_NAME}-{uuid.uuid4().hex[:12]}",
            spec=spec,
            timeout=timeout,
        )


async def list_storage_osds(
    kube: Kube, *, timeout: float
) -> list[CephStorageOSDRecord]:
    """List and validate managed OSD inventory resources.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum request budget in seconds.

    Returns
    -------
    list[CephStorageOSDRecord]
        Validated OSD inventory records.
    """
    return await STORAGE_OSD_RESOURCE.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        timeout=timeout,
    )


async def upsert_storage_osd(
    kube: Kube,
    *,
    name: str,
    spec: Mapping[str, object],
    phase: StorageOSDPhase,
    timeout: float,
) -> CephStorageOSDRecord:
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
    CephStorageOSDRecord
        Converged OSD inventory record.
    """
    now = datetime.now(UTC)
    existing_record = await STORAGE_OSD_RESOURCE.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=name,
        timeout=timeout,
    )
    record = await STORAGE_OSD_RESOURCE.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=name,
        spec=spec,
        labels=_storage_osd_labels(name=name, spec=spec, phase=phase),
        timeout=timeout,
    )
    await STORAGE_OSD_RESOURCE.patch_status(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=name,
        status={
            "phase": phase,
            "created_at": (
                existing_record.status.created_at.isoformat()
                if existing_record is not None
                and existing_record.status.created_at is not None
                else now.isoformat()
            ),
            "phase_changed_at": (
                existing_record.status.phase_changed_at.isoformat()
                if existing_record is not None
                and existing_record.status.phase == phase
                and existing_record.status.phase_changed_at is not None
                else now.isoformat()
            ),
            "last_seen_at": now.isoformat(),
            "last_error": "",
        },
        timeout=timeout,
    )
    refreshed = await STORAGE_OSD_RESOURCE.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=name,
        timeout=timeout,
    )
    return refreshed or record


async def patch_storage_osd_status(
    kube: Kube,
    *,
    osd: CephStorageOSDRecord,
    status: Mapping[str, object],
    timeout: float,
) -> CephStorageOSDRecord:
    """Patch the status for one managed OSD record.

    Returns
    -------
    CephStorageOSDRecord
        Freshly validated OSD record returned by the Kubernetes API.
    """
    now = datetime.now(UTC)
    payload = {"last_seen_at": now.isoformat(), **dict(status)}
    phase = payload.get("phase")
    if isinstance(phase, str) and phase != osd.status.phase:
        payload.setdefault("phase_changed_at", now.isoformat())
    elif isinstance(phase, str) and "phase_changed_at" not in payload:
        if osd.status.phase_changed_at is not None:
            payload["phase_changed_at"] = osd.status.phase_changed_at.isoformat()
        else:
            payload["phase_changed_at"] = now.isoformat()
    return await STORAGE_OSD_RESOURCE.patch_status(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=osd.metadata.name,
        status=payload,
        timeout=timeout,
    )


def _storage_osd_labels(
    *,
    name: str,
    spec: Mapping[str, object],
    phase: StorageOSDPhase,
) -> dict[str, str]:
    node_name = str(spec.get("node_name") or "")
    host_id = str(spec.get("host_id") or "")
    pv_uuid = str(spec.get("pv_uuid") or spec.get("pv_name") or name)
    return {
        STORAGE_OSD_ORIGIN_LABEL: str(spec.get("origin") or ""),
        STORAGE_OSD_PHASE_LABEL: phase.lower(),
        STORAGE_OSD_NODE_LABEL: _hash_label(node_name),
        STORAGE_OSD_HOST_LABEL: _hash_label(host_id),
        STORAGE_OSD_PV_LABEL: _hash_label(pv_uuid),
    }


async def patch_storage_policy_status(
    kube: Kube,
    *,
    policy: CephStoragePolicyRecord,
    status: Mapping[str, object],
    timeout: float,
) -> None:
    """Patch the singleton storage policy status.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    policy : CephStoragePolicyRecord
        Policy record whose generation should be observed.
    status : Mapping[str, object]
        Status fields to patch.
    timeout : float
        Maximum patch budget in seconds.
    """
    payload = {"observedGeneration": policy.metadata.generation, **dict(status)}
    await STORAGE_POLICY_RESOURCE.patch_status(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=STORAGE_POLICY_NAME,
        status=payload,
        timeout=timeout,
    )


def storage_action_from_payload(payload: object) -> CephStorageActionRecord:
    """Validate one storage action payload.

    Parameters
    ----------
    payload : object
        Raw custom-object payload.

    Returns
    -------
    CephStorageActionRecord
        Validated storage action record.
    """
    return CephStorageActionRecord.from_payload(payload)


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
    await STORAGE_NODE_RESOURCE.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=storage_node_report_name(host_id),
        spec={"node_name": node_name, "host_id": host_id},
        timeout=timeout,
    )
    await STORAGE_NODE_RESOURCE.patch_status(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=storage_node_report_name(host_id),
        status=status,
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
    pending.sort(key=lambda action: action.metadata.name)
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
        name=action.metadata.name,
        status=status,
        timeout=timeout,
    )
