"""Ceph capacity policy records and grow/shrink planning."""

from __future__ import annotations

import asyncio
import math
import uuid
from dataclasses import dataclass
from datetime import UTC, datetime
from typing import TYPE_CHECKING, Annotated, Literal, cast

from pydantic import (
    BaseModel,
    ConfigDict,
    Field,
    PositiveInt,
    ValidationError,
    field_validator,
    model_validator,
)

from bertrand.env.git import BERTRAND_ENV, BERTRAND_NAMESPACE
from bertrand.env.kube.ceph.api import (
    LOOP_OSD_SIZE_PATTERN,
    LOOP_OSD_SPEC_PATTERN,
    BlockOSDSpec,
    CephCapacitySnapshot,
    CephOSD,
    LoopOSDSpec,
    parse_loop_osd_spec,
    parse_size_bytes,
)
from bertrand.env.kube.crd import CustomResourceDefinition
from bertrand.env.kube.custom_object import (
    CustomObjectClient,
    CustomObjectMetadata,
    CustomObjectSpec,
)

if TYPE_CHECKING:
    from collections.abc import Collection, Mapping

    from bertrand.env.kube.api.client import Kube

CEPH_CAPACITY_GROUP = "ceph.bertrand.dev"
CEPH_CAPACITY_VERSION = "v1alpha1"
STORAGE_POLICY_KIND = "CephStorageAutoscaler"
STORAGE_POLICY_PLURAL = "cephstorageautoscalers"
STORAGE_ACTION_KIND = "CephStorageAction"
STORAGE_ACTION_PLURAL = "cephstorageactions"
STORAGE_NODE_KIND = "CephStorageNode"
STORAGE_NODE_PLURAL = "cephstoragenodes"
STORAGE_POLICY_NAME = "default"
STORAGE_CONTROLLER_LABEL = "bertrand.dev/ceph-storage-controller"
STORAGE_CONTROLLER_LABEL_VALUE = "v1"
STORAGE_CONTROLLER_LABELS = {
    BERTRAND_ENV: "1",
    STORAGE_CONTROLLER_LABEL: STORAGE_CONTROLLER_LABEL_VALUE,
}
STORAGE_ACTION_PHASES = ("Pending", "Running", "Succeeded", "Failed")
STORAGE_NODE_REPORT_MAX_AGE_SECONDS = 120

type _Watermark = Annotated[float, Field(gt=0.0, lt=1.0)]
type _LoopSize = Annotated[str, Field(pattern=LOOP_OSD_SIZE_PATTERN)]
type _LoopSpec = Annotated[str, Field(pattern=LOOP_OSD_SPEC_PATTERN)]
type _DevicePath = Annotated[str, Field(min_length=1, pattern=r"^/.*")]
type StorageActionOperation = Literal["grow", "shrink", "add-block"]
type StorageActionPhase = Literal["Pending", "Running", "Succeeded", "Failed"]
type StorageOSDOrigin = Literal["autoscaled-loop", "manual-block"]
type StorageOSDQuality = Literal["elastic", "durable"]


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
    loop_size: _LoopSize = "4G"
    max_actions_per_reconcile: PositiveInt = 3
    reconcile_interval_seconds: PositiveInt = 30

    @field_validator("loop_size")
    @classmethod
    def _validate_loop_size(cls, value: str) -> str:
        return LoopOSDSpec(size=value).size

    @model_validator(mode="after")
    def _validate_watermarks(self) -> _CephStoragePolicySpec:
        if not self.low_watermark < self.shrink_target_watermark < self.high_watermark:
            msg = (
                "Ceph autoscale watermarks must satisfy "
                "low_watermark < shrink_target_watermark < high_watermark"
            )
            raise ValueError(msg)
        return self


class _CephStoragePolicyStatus(BaseModel):
    """Observed status emitted by the Ceph capacity controller."""

    model_config = ConfigDict(extra="forbid")
    observed_generation: int | None = Field(default=None, alias="observedGeneration")
    total_bytes: int | None = None
    used_bytes: int | None = None
    used_ratio: float | None = None
    pending_actions: int = 0
    running_actions: int = 0
    succeeded_actions: int = 0
    failed_actions: int = 0
    managed_osds: int = 0
    loop_osds: int = 0
    block_osds: int = 0
    elastic_bytes: int = 0
    durable_bytes: int = 0
    block_preferred: bool = False
    shrink_candidates: int = 0
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
    loop_spec: _LoopSpec | None = None
    osd_id: Annotated[int, Field(ge=0)] | None = None
    device: _DevicePath | None = None
    wal_device: _DevicePath | None = None
    db_device: _DevicePath | None = None
    encrypt: bool = False
    wipe: bool = False
    reason: Annotated[str, Field(min_length=1)]

    @field_validator("loop_spec")
    @classmethod
    def _validate_loop_spec(cls, value: str | None) -> str | None:
        if value is None:
            return None
        return parse_loop_osd_spec(value).render()

    @model_validator(mode="after")
    def _validate_operation_contract(self) -> _CephStorageActionSpec:
        if self.operation == "grow":
            if (
                self.loop_spec is None
                or self.osd_id is not None
                or self.device is not None
                or self.wal_device is not None
                or self.db_device is not None
                or self.encrypt
                or self.wipe
            ):
                msg = (
                    "grow actions require loop_spec and cannot set osd_id or block "
                    "device fields"
                )
                raise ValueError(msg)
            return self
        if self.operation == "shrink":
            if (
                self.osd_id is None
                or self.loop_spec is not None
                or self.device is not None
                or self.wal_device is not None
                or self.db_device is not None
                or self.encrypt
                or self.wipe
            ):
                msg = (
                    "shrink actions require osd_id and cannot set loop_spec or block "
                    "device fields"
                )
                raise ValueError(msg)
            return self
        if self.device is None or self.loop_spec is not None or self.osd_id is not None:
            msg = "add-block actions require device and cannot set loop_spec or osd_id"
            raise ValueError(msg)
        BlockOSDSpec(
            device=self.device,
            wal_device=self.wal_device,
            db_device=self.db_device,
            encrypt=self.encrypt,
            wipe=self.wipe,
        )
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
    source_devices: tuple[str, ...] = ()
    source_device_bytes: Annotated[int, Field(ge=0)] | None = None


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


class _CephStorageNodeSpec(BaseModel):
    """Desired identity contract for one node capacity report."""

    model_config = ConfigDict(extra="forbid")
    node_name: Annotated[str, Field(min_length=1)]


class _CephStorageNodeStatus(BaseModel):
    """Observed host-local capacity state reported by one node agent."""

    model_config = ConfigDict(extra="forbid")
    free_bytes: Annotated[int, Field(ge=0)] = 0
    path: str = ""
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


@dataclass(frozen=True)
class PlannedStorageAction:
    """One node-scoped MicroCeph storage action selected by policy planning."""

    operation: StorageActionOperation
    node_name: str
    reason: str
    loop_spec: str | None = None
    osd_id: int | None = None
    device: str | None = None
    wal_device: str | None = None
    db_device: str | None = None
    encrypt: bool = False
    wipe: bool = False


@dataclass(frozen=True)
class _ManagedOSD:
    """Autoscaler-created OSD that is eligible for shrink planning."""

    osd_id: int
    node_name: str
    size_bytes: int
    created_at: datetime | None


@dataclass(frozen=True)
class StoragePlan:
    """Storage actions and status inputs selected by one planning pass."""

    actions: list[PlannedStorageAction]
    managed_osd_count: int
    loop_osd_count: int
    block_osd_count: int
    elastic_bytes: int
    durable_bytes: int
    shrink_candidate_count: int
    last_shrink_at: datetime | None


@dataclass(frozen=True)
class StorageOSDInventory:
    """Quality-oriented OSD inventory inferred from storage actions.

    Attributes
    ----------
    loop_osd_ids : frozenset[int]
        Autoscaler-created loop OSD IDs still present in managed history.
    block_osd_ids : frozenset[int]
        Manual block OSD IDs created by successful tracked block actions.
    elastic_bytes : int
        Approximate autoscaled loop-backed raw bytes tracked by Bertrand.
    durable_bytes : int
        Approximate manual block-backed raw bytes tracked by Bertrand.
    """

    loop_osd_ids: frozenset[int]
    block_osd_ids: frozenset[int]
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
            if action.spec.operation == "shrink"
            and action.status.phase in ("Running", "Succeeded", "Failed")
        ]
        return max((item for item in timestamps if item is not None), default=None)

    @staticmethod
    def managed_osd_ids(actions: Collection[CephStorageActionRecord]) -> set[int]:
        """Return live autoscaler-created OSD IDs not consumed by shrink.

        Parameters
        ----------
        actions : Collection[CephStorageActionRecord]
            Storage actions to inspect.

        Returns
        -------
        set[int]
            Managed OSD IDs still eligible for future shrink planning.
        """
        created: set[int] = set()
        consumed: set[int] = set()
        for action in actions:
            if action.spec.operation == "grow" and action.status.phase == "Succeeded":
                created.update(action.status.created_osd_ids)
                continue
            if action.spec.operation != "shrink":
                continue
            if action.status.phase in ("Pending", "Running", "Succeeded"):
                if action.spec.osd_id is not None:
                    consumed.add(action.spec.osd_id)
                consumed.update(action.status.removed_osd_ids)
        return created - consumed

    @staticmethod
    def manual_block_osd_ids(actions: Collection[CephStorageActionRecord]) -> set[int]:
        """Return manual block OSD IDs created by tracked actions.

        Parameters
        ----------
        actions : Collection[CephStorageActionRecord]
            Storage actions to inspect.

        Returns
        -------
        set[int]
            Manual block OSD IDs known to Bertrand.
        """
        created: set[int] = set()
        for action in actions:
            if (
                action.spec.operation == "add-block"
                and action.status.phase == "Succeeded"
            ):
                created.update(action.status.created_osd_ids)
        return created

    def osd_inventory(
        self,
        actions: Collection[CephStorageActionRecord],
    ) -> StorageOSDInventory:
        """Return loop/block OSD inventory inferred from action history.

        Parameters
        ----------
        actions : Collection[CephStorageActionRecord]
            Storage actions to inspect.

        Returns
        -------
        StorageOSDInventory
            Quality-oriented OSD inventory.
        """
        loop_ids = self.managed_osd_ids(actions)
        elastic_bytes = 0
        for action in actions:
            if (
                action.spec.operation != "grow"
                or action.status.phase != "Succeeded"
                or action.spec.loop_spec is None
            ):
                continue
            spec = parse_loop_osd_spec(action.spec.loop_spec)
            size_bytes = parse_size_bytes(spec.size)
            elastic_bytes += sum(
                size_bytes
                for osd_id in action.status.created_osd_ids
                if osd_id in loop_ids
            )

        block_ids = self.manual_block_osd_ids(actions)
        durable_bytes = sum(
            action.status.source_device_bytes or 0
            for action in actions
            if action.spec.operation == "add-block"
            and action.status.phase == "Succeeded"
        )
        return StorageOSDInventory(
            loop_osd_ids=frozenset(loop_ids),
            block_osd_ids=frozenset(block_ids),
            elastic_bytes=elastic_bytes,
            durable_bytes=durable_bytes,
        )

    @staticmethod
    def block_actions_in_flight(
        actions: Collection[CephStorageActionRecord],
    ) -> bool:
        """Return whether manual block capacity is currently being added.

        Parameters
        ----------
        actions : Collection[CephStorageActionRecord]
            Storage actions to inspect.

        Returns
        -------
        bool
            True when a block OSD action is pending or running.
        """
        return any(
            action.spec.operation == "add-block"
            and action.status.phase in ("Pending", "Running")
            for action in actions
        )

    def managed_osds(
        self,
        *,
        actions: Collection[CephStorageActionRecord],
        osds: Collection[CephOSD],
    ) -> list[_ManagedOSD]:
        """Return shrink-eligible managed OSD inventory.

        Parameters
        ----------
        actions : Collection[CephStorageActionRecord]
            Storage actions that identify autoscaler-created OSDs.
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
        managed_ids = self.managed_osd_ids(actions)
        candidates: list[_ManagedOSD] = []
        for action in actions:
            if (
                action.spec.operation != "grow"
                or action.status.phase != "Succeeded"
                or action.spec.loop_spec is None
            ):
                continue
            loop_spec = parse_loop_osd_spec(action.spec.loop_spec)
            size_bytes = parse_size_bytes(loop_spec.size)
            for osd_id in action.status.created_osd_ids:
                osd = live.get(osd_id)
                if osd is None or osd_id not in managed_ids:
                    continue
                candidates.append(
                    _ManagedOSD(
                        osd_id=osd_id,
                        node_name=action.spec.node_name or osd.node_name,
                        size_bytes=size_bytes,
                        created_at=self.utc(action.status.finished_at),
                    )
                )
        return candidates

    @staticmethod
    def eligible_nodes(
        *,
        ready_nodes: Collection[str],
        reports: Collection[CephStorageNodeRecord],
        loop_bytes: int,
    ) -> list[str]:
        """Return deterministic node slots eligible for new loop OSDs.

        Parameters
        ----------
        ready_nodes : Collection[str]
            Kubernetes nodes currently ready for Bertrand registry pulls.
        reports : Collection[CephStorageNodeRecord]
            Node-local free-space reports.
        loop_bytes : int
            Bytes required by one loop OSD.

        Returns
        -------
        list[str]
            Sorted node slots, with repeated names representing available capacity.
        """
        ready = frozenset(ready_nodes)
        now = datetime.now(UTC)
        eligible: list[str] = []
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
            slots = status.free_bytes // loop_bytes
            eligible.extend([report.spec.node_name] * min(slots, 32))
        return sorted(eligible)

    def plan_grow_actions(
        self,
        *,
        policy: CephStoragePolicyRecord,
        capacity: CephCapacitySnapshot,
        actions: Collection[CephStorageActionRecord],
        eligible_nodes: list[str],
        loop_bytes: int,
    ) -> list[PlannedStorageAction]:
        """Return grow actions needed to reach the policy target watermark.

        Parameters
        ----------
        policy : CephStoragePolicyRecord
            Active storage policy.
        capacity : CephCapacitySnapshot
            Current Ceph capacity snapshot.
        actions : Collection[CephStorageActionRecord]
            Existing storage action records.
        eligible_nodes : list[str]
            Node slots eligible for new loop OSDs.
        loop_bytes : int
            Bytes required by one loop OSD.

        Returns
        -------
        list[PlannedStorageAction]
            Grow actions selected for this reconcile pass.
        """
        spec = policy.spec
        if (
            not spec.enabled
            or not eligible_nodes
            or capacity.used_ratio < spec.high_watermark
            or self.block_actions_in_flight(actions)
        ):
            return []
        target_used = spec.target_watermark * capacity.total_bytes
        deficit = capacity.used_bytes - target_used
        if deficit <= 0:
            return []

        budget = spec.max_actions_per_reconcile - self.in_flight(actions)
        if budget <= 0:
            return []

        desired = math.ceil(deficit / loop_bytes)
        count = max(0, min(desired, budget, len(eligible_nodes)))
        planned: list[PlannedStorageAction] = []
        for index in range(count):
            node = eligible_nodes[(self.offset + index) % len(eligible_nodes)]
            planned.append(
                PlannedStorageAction(
                    operation="grow",
                    node_name=node,
                    loop_spec=LoopOSDSpec(size=spec.loop_size).render(),
                    reason=(
                        "cluster usage "
                        f"{capacity.used_ratio:.2%} >= high watermark "
                        f"{spec.high_watermark:.2%}"
                    ),
                )
            )
        if eligible_nodes:
            self.offset = (self.offset + count) % len(eligible_nodes)
        return planned

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
                operation="shrink",
                node_name=candidate.node_name,
                osd_id=candidate.osd_id,
                reason=(
                    "cluster usage "
                    f"{capacity.used_ratio:.2%} <= low watermark "
                    f"{spec.low_watermark:.2%}; projected usage after removing "
                    f"osd.{candidate.osd_id} is {projected_ratio:.2%}"
                ),
            )
        ]

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
        "loop_size": {
            "type": "string",
            "pattern": LOOP_OSD_SIZE_PATTERN,
            "default": "4G",
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
        "pending_actions": {"type": "integer"},
        "running_actions": {"type": "integer"},
        "succeeded_actions": {"type": "integer"},
        "failed_actions": {"type": "integer"},
        "managed_osds": {"type": "integer"},
        "loop_osds": {"type": "integer"},
        "block_osds": {"type": "integer"},
        "elastic_bytes": {"type": "integer"},
        "durable_bytes": {"type": "integer"},
        "block_preferred": {"type": "boolean"},
        "shrink_candidates": {"type": "integer"},
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
    "required": ["policy_generation", "operation", "node_name", "reason"],
    "properties": {
        "policy_generation": {"type": "integer", "minimum": 0},
        "operation": {"type": "string", "enum": ["grow", "shrink", "add-block"]},
        "node_name": {"type": "string", "minLength": 1},
        "loop_spec": {
            "type": "string",
            "pattern": LOOP_OSD_SPEC_PATTERN,
            "nullable": True,
        },
        "osd_id": {"type": "integer", "minimum": 0, "nullable": True},
        "device": {"type": "string", "pattern": "^/.*", "nullable": True},
        "wal_device": {"type": "string", "pattern": "^/.*", "nullable": True},
        "db_device": {"type": "string", "pattern": "^/.*", "nullable": True},
        "encrypt": {"type": "boolean", "default": False},
        "wipe": {"type": "boolean", "default": False},
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
            "enum": ["autoscaled-loop", "manual-block"],
            "nullable": True,
        },
        "osd_quality": {
            "type": "string",
            "enum": ["elastic", "durable"],
            "nullable": True,
        },
        "source_devices": {
            "type": "array",
            "items": {"type": "string"},
        },
        "source_device_bytes": {
            "type": "integer",
            "minimum": 0,
            "nullable": True,
        },
    },
}
_STORAGE_NODE_SPEC_SCHEMA = {
    "type": "object",
    "required": ["node_name"],
    "properties": {"node_name": {"type": "string", "minLength": 1}},
}
_STORAGE_NODE_STATUS_SCHEMA = {
    "type": "object",
    "properties": {
        "free_bytes": {"type": "integer", "minimum": 0},
        "path": {"type": "string"},
        "heartbeat_at": {"type": "string", "format": "date-time"},
        "last_error": {"type": "string"},
    },
}

STORAGE_POLICY = CustomObjectSpec(
    group=CEPH_CAPACITY_GROUP,
    version=CEPH_CAPACITY_VERSION,
    kind=STORAGE_POLICY_KIND,
    plural=STORAGE_POLICY_PLURAL,
    labels=STORAGE_CONTROLLER_LABELS,
)
STORAGE_ACTION = CustomObjectSpec(
    group=CEPH_CAPACITY_GROUP,
    version=CEPH_CAPACITY_VERSION,
    kind=STORAGE_ACTION_KIND,
    plural=STORAGE_ACTION_PLURAL,
    labels=STORAGE_CONTROLLER_LABELS,
)
STORAGE_NODE = CustomObjectSpec(
    group=CEPH_CAPACITY_GROUP,
    version=CEPH_CAPACITY_VERSION,
    kind=STORAGE_NODE_KIND,
    plural=STORAGE_NODE_PLURAL,
    labels=STORAGE_CONTROLLER_LABELS,
)
_STORAGE_POLICY_CLIENT = CustomObjectClient(STORAGE_POLICY)
_STORAGE_ACTION_CLIENT = CustomObjectClient(STORAGE_ACTION)
_STORAGE_NODE_CLIENT = CustomObjectClient(STORAGE_NODE)


async def _ensure_crd(
    kube: Kube,
    *,
    plural: str,
    singular: str,
    kind: str,
    short_names: Collection[str],
    spec_schema: Mapping[str, object],
    status_schema: Mapping[str, object],
    deadline: float,
) -> None:
    loop = asyncio.get_running_loop()
    crd = await CustomResourceDefinition.upsert(
        kube,
        group=CEPH_CAPACITY_GROUP,
        version=CEPH_CAPACITY_VERSION,
        plural=plural,
        singular=singular,
        kind=kind,
        short_names=short_names,
        spec_schema=spec_schema,
        status_schema=status_schema,
        labels=STORAGE_CONTROLLER_LABELS,
        timeout=deadline - loop.time(),
    )
    await crd.wait_established(kube, timeout=deadline - loop.time())


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
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    await _ensure_crd(
        kube,
        plural=STORAGE_POLICY_PLURAL,
        singular="cephstorageautoscaler",
        kind=STORAGE_POLICY_KIND,
        short_names=["csa"],
        spec_schema=_STORAGE_POLICY_SPEC_SCHEMA,
        status_schema=_STORAGE_POLICY_STATUS_SCHEMA,
        deadline=deadline,
    )
    await _ensure_crd(
        kube,
        plural=STORAGE_ACTION_PLURAL,
        singular="cephstorageaction",
        kind=STORAGE_ACTION_KIND,
        short_names=["csact"],
        spec_schema=_STORAGE_ACTION_SPEC_SCHEMA,
        status_schema=_STORAGE_ACTION_STATUS_SCHEMA,
        deadline=deadline,
    )
    await _ensure_crd(
        kube,
        plural=STORAGE_NODE_PLURAL,
        singular="cephstoragenode",
        kind=STORAGE_NODE_KIND,
        short_names=["csnode"],
        spec_schema=_STORAGE_NODE_SPEC_SCHEMA,
        status_schema=_STORAGE_NODE_STATUS_SCHEMA,
        deadline=deadline,
    )


async def ensure_default_storage_policy(kube: Kube, *, timeout: float) -> None:
    """Converge the singleton default storage policy record.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum convergence budget in seconds.
    """
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    await _STORAGE_POLICY_CLIENT.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=STORAGE_POLICY_NAME,
        spec=cast(
            "dict[str, object]",
            _CephStoragePolicySpec().model_dump(mode="json"),
        ),
        timeout=deadline - loop.time(),
    )


def storage_watch_targets() -> tuple[tuple[CustomObjectClient, str], ...]:
    """Return capacity resources watched by the storage controller.

    Returns
    -------
    tuple[tuple[CustomObjectClient, str], ...]
        Client/context pairs for storage policy, actions, and node reports.
    """
    return (
        (_STORAGE_POLICY_CLIENT, STORAGE_POLICY_PLURAL),
        (_STORAGE_ACTION_CLIENT, STORAGE_ACTION_PLURAL),
        (_STORAGE_NODE_CLIENT, STORAGE_NODE_PLURAL),
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
    obj = await _STORAGE_POLICY_CLIENT.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=STORAGE_POLICY_NAME,
        timeout=timeout,
    )
    if obj is None:
        msg = f"{STORAGE_POLICY_KIND} {STORAGE_POLICY_NAME!r} is missing"
        raise OSError(msg)
    return CephStoragePolicyRecord.from_payload(obj.payload)


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
    objects = await _STORAGE_ACTION_CLIENT.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        labels={STORAGE_CONTROLLER_LABEL: STORAGE_CONTROLLER_LABEL_VALUE},
        timeout=timeout,
    )
    return [CephStorageActionRecord.from_payload(obj.payload) for obj in objects]


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
    objects = await _STORAGE_NODE_CLIENT.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        labels={STORAGE_CONTROLLER_LABEL: STORAGE_CONTROLLER_LABEL_VALUE},
        timeout=timeout,
    )
    return [CephStorageNodeRecord.from_payload(obj.payload) for obj in objects]


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
            "reason": action.reason,
        }
        if action.loop_spec is not None:
            spec["loop_spec"] = action.loop_spec
        if action.osd_id is not None:
            spec["osd_id"] = action.osd_id
        if action.device is not None:
            spec["device"] = action.device
        if action.wal_device is not None:
            spec["wal_device"] = action.wal_device
        if action.db_device is not None:
            spec["db_device"] = action.db_device
        if action.encrypt:
            spec["encrypt"] = action.encrypt
        if action.wipe:
            spec["wipe"] = action.wipe
        await _STORAGE_ACTION_CLIENT.create(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=f"{STORAGE_POLICY_NAME}-{uuid.uuid4().hex[:12]}",
            spec=spec,
            timeout=timeout,
        )


def _block_action_devices(action: CephStorageActionRecord) -> frozenset[str]:
    return frozenset(
        path
        for path in (action.spec.device, action.spec.wal_device, action.spec.db_device)
        if path is not None
    )


async def create_manual_block_osd_action(
    kube: Kube,
    *,
    node_name: str,
    device: str,
    wal_device: str | None = None,
    db_device: str | None = None,
    encrypt: bool = False,
    wipe: bool = False,
    timeout: float,
) -> str:
    """Create a manual block-backed OSD action for one node.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    node_name : str
        Kubernetes node name whose storage agent should execute the action.
    device : str
        Absolute host block device path to add as the data device.
    wal_device : str | None, optional
        Optional absolute WAL device path.
    db_device : str | None, optional
        Optional absolute DB device path.
    encrypt : bool, default False
        Whether MicroCeph should encrypt the data device.
    wipe : bool, default False
        Whether MicroCeph should wipe the data device.
    timeout : float
        Maximum creation budget in seconds.

    Returns
    -------
    str
        Created action resource name.

    Raises
    ------
    ValueError
        If another active tracked block OSD action already references one of the
        requested devices.
    """
    block_spec = BlockOSDSpec(
        device=device,
        wal_device=wal_device,
        db_device=db_device,
        encrypt=encrypt,
        wipe=wipe,
    )
    requested = frozenset(
        path
        for path in (block_spec.device, block_spec.wal_device, block_spec.db_device)
        if path is not None
    )
    for action in await list_storage_actions(kube, timeout=timeout):
        if action.spec.operation != "add-block":
            continue
        if action.status.phase not in ("Pending", "Running", "Succeeded"):
            continue
        overlap = requested & _block_action_devices(action)
        if overlap:
            paths = ", ".join(sorted(overlap))
            msg = (
                f"block OSD device already appears in tracked action "
                f"{action.metadata.name!r}: {paths}"
            )
            raise ValueError(msg)
    name = f"{STORAGE_POLICY_NAME}-block-{uuid.uuid4().hex[:12]}"
    spec: dict[str, object] = {
        "policy_generation": 0,
        "operation": "add-block",
        "node_name": node_name,
        "device": block_spec.device,
        "encrypt": encrypt,
        "wipe": wipe,
        "reason": "manual block OSD request",
    }
    if block_spec.wal_device is not None:
        spec["wal_device"] = block_spec.wal_device
    if block_spec.db_device is not None:
        spec["db_device"] = block_spec.db_device
    spec = cast(
        "dict[str, object]",
        _CephStorageActionSpec.model_validate(spec).model_dump(
            mode="json",
            exclude_none=True,
        ),
    )
    await _STORAGE_ACTION_CLIENT.create(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=name,
        spec=spec,
        timeout=timeout,
    )
    return name


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
    await _STORAGE_POLICY_CLIENT.patch_status(
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
    status : Mapping[str, object]
        Node report status payload.
    timeout : float
        Maximum update budget in seconds.
    """
    await _STORAGE_NODE_CLIENT.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=node_name,
        spec={"node_name": node_name},
        timeout=timeout,
    )
    await _STORAGE_NODE_CLIENT.patch_status(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=node_name,
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
    await _STORAGE_ACTION_CLIENT.patch_status(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=action.metadata.name,
        status=status,
        timeout=timeout,
    )
