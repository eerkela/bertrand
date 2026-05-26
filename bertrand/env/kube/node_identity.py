"""Cluster-backed Bertrand host identity records."""

from __future__ import annotations

import hashlib
import platform
import uuid
from datetime import UTC, datetime
from typing import TYPE_CHECKING, Annotated, Literal

from pydantic import BaseModel, ConfigDict, Field, field_validator

from bertrand.env.config.core import _check_uuid
from bertrand.env.git import BERTRAND_ENV
from bertrand.env.host import HOST_ID_FILE
from bertrand.env.kube.custom_object import (
    CustomObjectMetadata,
    CustomObjectResource,
)
from bertrand.env.kube.node import Node

if TYPE_CHECKING:
    from collections.abc import Collection

    from bertrand.env.kube.api.client import Kube

BERTRAND_NODE_GROUP = "node.bertrand.dev"
BERTRAND_NODE_VERSION = "v1alpha1"
BERTRAND_NODE_KIND = "BertrandNode"
BERTRAND_NODE_PLURAL = "bertrandnodes"
BERTRAND_NODE_LABEL = "bertrand.dev/node"
BERTRAND_NODE_LABEL_VALUE = "v1"
BERTRAND_NODE_HOST_LABEL = "bertrand.dev/node-host"
BERTRAND_NODE_KUBE_LABEL = "bertrand.dev/node-kubernetes"
BERTRAND_NODE_PHASE_LABEL = "bertrand.dev/node-phase"

_BERTRAND_NODE_LABELS = {
    BERTRAND_ENV: "1",
    BERTRAND_NODE_LABEL: BERTRAND_NODE_LABEL_VALUE,
}
type _NonEmptyString = Annotated[str, Field(min_length=1)]
type _BertrandNodePhase = Literal["Active", "Retired"]


class _BertrandNodeSpec(BaseModel):
    model_config = ConfigDict(extra="forbid", frozen=True)
    host_id: _NonEmptyString
    node_name: _NonEmptyString
    display_name: str = ""
    phase: _BertrandNodePhase = "Active"
    created_at: datetime
    last_seen_at: datetime
    retired_at: datetime | None = None

    @field_validator("host_id")
    @classmethod
    def _validate_host_id(cls, value: str) -> str:
        return _check_uuid(value.strip())

    @field_validator("node_name")
    @classmethod
    def _validate_node_name(cls, value: str) -> str:
        text = value.strip()
        if not text:
            msg = "BertrandNode node_name cannot be empty"
            raise ValueError(msg)
        return text

    @field_validator("display_name")
    @classmethod
    def _normalize_display_name(cls, value: str) -> str:
        return value.strip()

    @field_validator("created_at", "last_seen_at", "retired_at")
    @classmethod
    def _normalize_datetime(cls, value: datetime | None) -> datetime | None:
        if value is None:
            return None
        if value.tzinfo is None:
            return value.replace(tzinfo=UTC)
        return value.astimezone(UTC)


class BertrandNodeRecord(BaseModel):
    """Cluster-scoped Bertrand host identity record.

    Parameters
    ----------
    metadata : CustomObjectMetadata
        Kubernetes metadata for the custom object.
    spec : _BertrandNodeSpec
        Validated Bertrand node identity payload.
    """

    model_config = ConfigDict(extra="ignore", frozen=True, populate_by_name=True)
    api_version: str = Field(default="", alias="apiVersion")
    kind: str = ""
    metadata: CustomObjectMetadata
    spec: _BertrandNodeSpec

    @property
    def name(self) -> str:
        """Return the Kubernetes custom-object name."""
        return self.metadata.name

    @property
    def host_id(self) -> str:
        """Return the durable Bertrand host UUID."""
        return self.spec.host_id

    @property
    def node_name(self) -> str:
        """Return the current Kubernetes node name for this host."""
        return self.spec.node_name

    @property
    def display_name(self) -> str:
        """Return the optional human-readable node display name."""
        return self.spec.display_name

    @property
    def phase(self) -> _BertrandNodePhase:
        """Return the Bertrand node lifecycle phase."""
        return self.spec.phase

    @property
    def retired_at(self) -> datetime | None:
        """Return the node retirement timestamp, if retired."""
        return self.spec.retired_at


BERTRAND_NODE_RESOURCE = CustomObjectResource[BertrandNodeRecord](
    group=BERTRAND_NODE_GROUP,
    version=BERTRAND_NODE_VERSION,
    kind=BERTRAND_NODE_KIND,
    plural=BERTRAND_NODE_PLURAL,
    scope="cluster",
    labels=_BERTRAND_NODE_LABELS,
    singular="bertrandnode",
    payload_parser=BertrandNodeRecord.model_validate,
    payload_error_context=f"{BERTRAND_NODE_KIND} payload",
    spec_model=_BertrandNodeSpec,
    spec_schema_overrides={
        "required": ["host_id", "node_name", "phase", "created_at", "last_seen_at"],
    },
)


async def ensure_local_bertrand_node(
    kube: Kube,
    *,
    host_id: str | None = None,
    display_name: str | None = None,
    timeout: float,
) -> BertrandNodeRecord:
    """Create or refresh the local host's Bertrand node identity record.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    host_id : str | None, optional
        Durable host UUID. When omitted, read it from Bertrand host state.
    display_name : str | None, optional
        Replacement display name. When omitted, preserve any existing display name
        or default new records to the platform hostname.
    timeout : float
        Maximum convergence budget in seconds.

    Returns
    -------
    BertrandNodeRecord
        Converged local node identity record.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    """
    if timeout <= 0:
        msg = "BertrandNode convergence timeout must be non-negative"
        raise TimeoutError(msg)
    host_id = current_host_id() if host_id is None else _check_uuid(host_id)
    await BERTRAND_NODE_RESOURCE.ensure_crd(kube, timeout=timeout)
    node = await Node.local(kube, timeout=timeout)
    existing = await get_bertrand_node(kube, host_id=host_id, timeout=timeout)
    now = datetime.now(UTC)
    created_at = existing.spec.created_at if existing is not None else now
    if display_name is None:
        chosen_display = (
            existing.display_name if existing is not None else platform.node().strip()
        )
    else:
        chosen_display = display_name.strip()
    spec = _BertrandNodeSpec(
        host_id=host_id,
        node_name=node.name,
        display_name=chosen_display,
        phase="Active",
        created_at=created_at,
        last_seen_at=now,
        retired_at=None,
    )
    return await BERTRAND_NODE_RESOURCE.upsert(
        kube,
        name=bertrand_node_name(host_id),
        spec=spec,
        labels={
            BERTRAND_NODE_HOST_LABEL: _hash_label(host_id),
            BERTRAND_NODE_KUBE_LABEL: _hash_label(node.name),
            BERTRAND_NODE_PHASE_LABEL: "active",
        },
        timeout=timeout,
    )


async def get_bertrand_node(
    kube: Kube,
    *,
    host_id: str,
    timeout: float,
) -> BertrandNodeRecord | None:
    """Read one Bertrand node record by host UUID.

    Returns
    -------
    BertrandNodeRecord | None
        Matching record, or ``None`` when it does not exist.
    """
    return await BERTRAND_NODE_RESOURCE.get(
        kube,
        name=bertrand_node_name(host_id),
        timeout=timeout,
    )


async def retire_bertrand_node(
    kube: Kube,
    *,
    host_id: str | None = None,
    timeout: float,
) -> BertrandNodeRecord | None:
    """Retire one Bertrand node record without deleting scoped state.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    host_id : str | None, optional
        Durable host UUID. When omitted, read it from local host state.
    timeout : float
        Maximum request budget in seconds.

    Returns
    -------
    BertrandNodeRecord | None
        Retired record, or None when no record exists.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive or retirement exceeds the budget.
    """
    if timeout <= 0:
        msg = "BertrandNode retirement timeout must be non-negative"
        raise TimeoutError(msg)
    host_id = current_host_id() if host_id is None else _check_uuid(host_id)
    await BERTRAND_NODE_RESOURCE.ensure_crd(kube, timeout=timeout)
    existing = await get_bertrand_node(kube, host_id=host_id, timeout=timeout)
    if existing is None:
        return None
    now = datetime.now(UTC)
    spec = _BertrandNodeSpec(
        host_id=existing.host_id,
        node_name=existing.node_name,
        display_name=existing.display_name,
        phase="Retired",
        created_at=existing.spec.created_at,
        last_seen_at=existing.spec.last_seen_at,
        retired_at=existing.retired_at or now,
    )
    return await BERTRAND_NODE_RESOURCE.upsert(
        kube,
        name=existing.name,
        spec=spec,
        labels={
            BERTRAND_NODE_HOST_LABEL: _hash_label(existing.host_id),
            BERTRAND_NODE_KUBE_LABEL: _hash_label(existing.node_name),
            BERTRAND_NODE_PHASE_LABEL: "retired",
        },
        timeout=timeout,
    )


async def list_bertrand_nodes(
    kube: Kube,
    *,
    host_ids: Collection[str] | None = None,
    node_names: Collection[str] | None = None,
    timeout: float,
) -> list[BertrandNodeRecord]:
    """List Bertrand node records with optional client-side filters.

    Returns
    -------
    list[BertrandNodeRecord]
        Matching Bertrand node records.
    """
    records = await BERTRAND_NODE_RESOURCE.list(kube, timeout=timeout)
    allowed_hosts = {_check_uuid(item) for item in host_ids or ()}
    allowed_nodes = {item.strip() for item in node_names or () if item.strip()}
    if allowed_hosts:
        records = [record for record in records if record.host_id in allowed_hosts]
    if allowed_nodes:
        records = [record for record in records if record.node_name in allowed_nodes]
    return sorted(records, key=lambda item: (item.display_name, item.host_id))


async def resolve_host_id_for_node(
    kube: Kube,
    *,
    node_name: str,
    timeout: float,
) -> str | None:
    """Return the Bertrand host UUID for a Kubernetes node name, if known.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    node_name : str
        Kubernetes node name to translate.
    timeout : float
        Maximum lookup budget in seconds.

    Returns
    -------
    str | None
        Durable host UUID, or ``None`` when no BertrandNode record exists.

    Raises
    ------
    OSError
        If multiple Bertrand hosts claim the same Kubernetes node name.
    """
    node_name = node_name.strip()
    if not node_name:
        return None
    records = await list_bertrand_nodes(
        kube,
        node_names=(node_name,),
        timeout=timeout,
    )
    records = [record for record in records if record.phase == "Active"]
    if not records:
        return None
    hosts = {record.host_id for record in records}
    if len(hosts) > 1:
        names = ", ".join(sorted(hosts))
        msg = (
            f"Kubernetes node {node_name!r} maps to multiple Bertrand host IDs: {names}"
        )
        raise OSError(msg)
    return records[0].host_id


def current_host_id() -> str:
    """Return the durable Bertrand host UUID from local host state.

    Returns
    -------
    str
        Host UUID hex string.

    Raises
    ------
    OSError
        If the host identity is missing or malformed.
    """
    try:
        return uuid.UUID(HOST_ID_FILE.read_text(encoding="utf-8").strip()).hex
    except (OSError, ValueError) as err:
        msg = (
            f"failed to read Bertrand host identity at {HOST_ID_FILE}; run "
            "`bertrand init`"
        )
        raise OSError(msg) from err


def bertrand_node_name(host_id: str) -> str:
    """Return the deterministic CRD name for a host UUID.

    Returns
    -------
    str
        Cluster-scoped custom-object name.
    """
    return f"bertrand-node-{_check_uuid(host_id)}"


def _hash_label(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()[:16]
