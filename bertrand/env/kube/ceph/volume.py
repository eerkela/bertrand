"""CephFS-backed repository volume lifecycle helpers."""

from __future__ import annotations

import asyncio
import hashlib
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta
from pathlib import PurePosixPath
from typing import TYPE_CHECKING, Annotated, Literal

from pydantic import (
    BaseModel,
    ConfigDict,
    Field,
    ValidationError,
    field_serializer,
    field_validator,
)

from bertrand.env.config.core import _check_uuid
from bertrand.env.git import (
    BERTRAND_LABEL,
    BERTRAND_LABEL_MANAGED,
    BERTRAND_NAMESPACE,
    REPO_ID_LABEL,
    Deadline,
)
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.build.request import (
    BUILDKIT_BUILD_RESOURCE,
    BuildKitBuildRecord,
)
from bertrand.env.kube.capability.base import delete_capabilities_for_scope
from bertrand.env.kube.ceph.auth import RepoCredentials
from bertrand.env.kube.cronjob import CronJob
from bertrand.env.kube.custom_object import (
    CustomObject,
    CustomObjectMetadata,
    CustomObjectResource,
)
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.job import Job
from bertrand.env.kube.pod import Pod
from bertrand.env.kube.snapshot import VolumeSnapshot
from bertrand.env.kube.volume import (
    PersistentVolume,
    PersistentVolumeClaim,
    StorageClass,
)
from bertrand.env.kube.workload.base import (
    WORKLOAD_LABEL,
    WORKLOAD_LABEL_VALUE,
)

if TYPE_CHECKING:
    from pathlib import PosixPath

REPO_VOLUME_CLAIM_LABEL: str = "BERTRAND_REPO_VOLUME"
CEPHFS_STORAGE_CLASS_PREFERENCES: tuple[str, ...] = ("cephfs", "rook-cephfs")
DEFAULT_VOLUME_SIZE = "16Mi"
REPOSITORY_VOLUME_GROUP = "ceph.bertrand.dev"
REPOSITORY_VOLUME_VERSION = "v1alpha1"
REPOSITORY_STATE_KIND = "CephRepositoryState"
REPOSITORY_STATE_PLURAL = "cephrepositorystates"
REPOSITORY_STATE_LABEL = "bertrand.dev/ceph-repository-state"
REPOSITORY_VOLUME_LABEL = REPOSITORY_STATE_LABEL
REPOSITORY_VOLUME_LABEL_VALUE = "v1"
REPOSITORY_VOLUME_PHASE_LABEL = "bertrand.dev/ceph-repository-volume-phase"
REPOSITORY_SNAPSHOT_LABEL = "bertrand.dev/ceph-repository-snapshot"
REPOSITORY_SNAPSHOT_LABEL_VALUE = "v1"
REPOSITORY_SNAPSHOT_PURPOSE_LABEL = "bertrand.dev/ceph-repository-snapshot-purpose"
REPOSITORY_SNAPSHOT_PURPOSE_RETAINED = "retained"
REPOSITORY_SNAPSHOT_PURPOSE_BUILD = "build"
REPOSITORY_BUILD_SOURCE_LABEL = "bertrand.dev/ceph-repository-build-source"
REPOSITORY_BUILD_SOURCE_LABEL_VALUE = "v1"
REPOSITORY_VOLUME_GC_GRACE_SECONDS = 604_800
REPOSITORY_VOLUME_GC_LIMIT = 4
_REPOSITORY_VOLUME_LABELS = {
    BERTRAND_LABEL: BERTRAND_LABEL_MANAGED,
    REPO_VOLUME_CLAIM_LABEL: BERTRAND_LABEL_MANAGED,
    REPOSITORY_VOLUME_LABEL: REPOSITORY_VOLUME_LABEL_VALUE,
}

type _RepositoryVolumePhase = Literal["Initializing", "Ready", "Failed", "Retired"]
type _RepositoryMountPhase = Literal["Active", "Retired"]
type _RepositoryWorktreePhase = Literal["Active", "Retired"]
type _NonEmptyString = Annotated[str, Field(min_length=1)]


def _normalize_datetime(value: datetime | None) -> datetime | None:
    if value is None:
        return None
    if value.tzinfo is None:
        return value.replace(tzinfo=UTC)
    return value.astimezone(UTC)


def _datetime_payload(value: datetime | None) -> str | None:
    value = _normalize_datetime(value)
    return value.isoformat() if value is not None else None


def _serialize_datetime(
    value: datetime | None,
    _info: object,
) -> str | None:
    return _datetime_payload(value)


def repository_mount_alias_path(alias_path: str) -> str:
    """Validate a repository mount alias path for CRD storage.

    Parameters
    ----------
    alias_path : str
        Absolute host path used as a repository mount alias.

    Returns
    -------
    str
        Normalized alias path string suitable for durable mount metadata.

    Raises
    ------
    ValueError
        If the alias path is empty or not absolute.
    """
    alias_path = alias_path.strip()
    if not alias_path:
        msg = "repository mount alias path cannot be empty"
        raise ValueError(msg)
    if not alias_path.startswith("/"):
        msg = f"repository mount alias path must be absolute: {alias_path!r}"
        raise ValueError(msg)
    return alias_path


def repository_mount_name(repo_id: str, host_id: str, alias_path: str) -> str:
    """Return the deterministic name for a repository mount record.

    Parameters
    ----------
    repo_id : str
        Stable repository UUID.
    host_id : str
        Durable Bertrand host UUID.
    alias_path : str
        Absolute host path used as a repository mount alias.

    Returns
    -------
    str
        Kubernetes custom object name for the mount record.
    """
    repo_id = _check_uuid(repo_id)
    host_id = _check_uuid(host_id)
    alias_path = repository_mount_alias_path(alias_path)
    h = hashlib.sha256()
    for value in (repo_id, host_id, alias_path):
        encoded = value.encode("utf-8")
        h.update(len(encoded).to_bytes(8, "big"))
        h.update(encoded)
    return f"bertrand-repo-mount-{h.hexdigest()[:43]}"


def repository_worktree_path(worktree: str | PurePosixPath) -> str:
    """Validate and normalize a repository-relative worktree path.

    Parameters
    ----------
    worktree : str | PurePosixPath
        Repository-relative worktree path. Empty and `.` normalize to `.`.

    Returns
    -------
    str
        Normalized POSIX worktree path.

    Raises
    ------
    ValueError
        If the worktree path is absolute or escapes the repository root.
    """
    value = (
        worktree.as_posix() if isinstance(worktree, PurePosixPath) else str(worktree)
    )
    value = value.strip()
    if not value or value == ".":
        return "."
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        msg = f"repository worktree path must be relative: {worktree!r}"
        raise ValueError(msg)
    return path.as_posix()


def repository_worktree_name(repo_id: str, worktree_id: str) -> str:
    """Return the deterministic name for a repository worktree record.

    Parameters
    ----------
    repo_id : str
        Stable repository UUID.
    worktree_id : str
        Persistent worktree UUID.

    Returns
    -------
    str
        Kubernetes custom-object name for the worktree lifecycle record.
    """
    repo_id = _check_uuid(repo_id)
    worktree_id = _check_uuid(worktree_id)
    h = hashlib.sha256()
    for value in (repo_id, worktree_id):
        encoded = value.encode("utf-8")
        h.update(len(encoded).to_bytes(8, "big"))
        h.update(encoded)
    return f"bertrand-repo-worktree-{h.hexdigest()[:40]}"


def repo_volume_claim_name(repo_id: str) -> str:
    """Return the deterministic PVC name for a repository identity.

    Parameters
    ----------
    repo_id : str
        Repository UUID used to derive the managed claim name.

    Returns
    -------
    str
        Kubernetes PVC name for the repository volume.
    """
    repo_id = _check_uuid(repo_id)
    h = hashlib.sha256()
    encoded: bytes = repo_id.encode("utf-8")
    h.update(len(encoded).to_bytes(8, "big"))
    h.update(encoded)
    return f"bertrand-repo-{h.hexdigest()}"


def _assert_managed_repository_volume_claim(
    pvc: PersistentVolumeClaim,
    *,
    claim_name: str,
    repo_id: str,
    storage_class: str | None,
    require_rwx: bool,
) -> None:
    actual_name = pvc.name
    if actual_name != claim_name:
        msg = (
            f"cluster PVC for repo {repo_id!r} has non-deterministic claim name "
            f"{actual_name!r}, expected {claim_name!r}"
        )
        raise OSError(msg)
    labels = pvc.labels
    storage_class_name = pvc.storage_class_name
    access_modes = pvc.access_modes
    if (
        labels.get(BERTRAND_LABEL) != BERTRAND_LABEL_MANAGED
        or labels.get(REPO_VOLUME_CLAIM_LABEL) != BERTRAND_LABEL_MANAGED
    ):
        msg = (
            f"cluster PVC {claim_name!r} collides with Bertrand volume claim, but "
            "is not managed by Bertrand"
        )
        raise OSError(msg)
    actual_repo_id = labels.get(REPO_ID_LABEL)
    if actual_repo_id != repo_id:
        msg = (
            f"cluster PVC {claim_name!r} has mismatched repo identity label "
            f"{REPO_ID_LABEL!r}: expected {repo_id!r}, got {actual_repo_id!r}"
        )
        raise OSError(msg)
    if storage_class is not None and storage_class_name != storage_class:
        msg = (
            f"cluster PVC {claim_name!r} uses storage class "
            f"{storage_class_name!r}, expected {storage_class!r}"
        )
        raise OSError(msg)
    if require_rwx and "ReadWriteMany" not in access_modes:
        msg = f"cluster PVC {claim_name!r} must include ReadWriteMany access mode"
        raise OSError(msg)


async def ensure_repository_volume_claim(
    kube: Kube,
    *,
    repo_id: str,
    deadline: Deadline,
    size_request: str,
) -> PersistentVolumeClaim:
    """Ensure a deterministic, cluster-wide RWX claim exists.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : str
        Repository UUID used to derive the claim name and ownership labels.
    deadline : Deadline
        Maximum convergence budget in seconds.
    size_request : str
        Kubernetes storage request to apply to the claim.

    Returns
    -------
    PersistentVolumeClaim
        Converged repository volume claim.

    Raises
    ------
    OSError
        If the selected storage class or resulting PVC is incompatible.
    ValueError
        If the repository ID or storage request is invalid.
    """
    repo_id = _check_uuid(repo_id)
    size_request = size_request.strip()
    if not size_request:
        msg = "size request cannot be empty"
        raise ValueError(msg)
    claim_name = repo_volume_claim_name(repo_id)
    storage = await StorageClass.select(
        kube=kube,
        deadline=deadline,
        preferences=CEPHFS_STORAGE_CLASS_PREFERENCES,
        require_expansion=True,
    )
    if not storage.is_cephfs:
        msg = (
            f"storage class {storage.name!r} uses provisioner "
            f"{storage.provisioner!r}, but Bertrand repository volumes "
            "require a CephFS CSI provisioner"
        )
        raise OSError(msg)
    storage_class = storage.name
    pvc = await PersistentVolumeClaim.upsert(
        kube=kube,
        namespace=BERTRAND_NAMESPACE,
        name=claim_name,
        access_modes=("ReadWriteMany",),
        storage_class=storage_class,
        storage_request=size_request,
        labels={
            BERTRAND_LABEL: BERTRAND_LABEL_MANAGED,
            REPO_VOLUME_CLAIM_LABEL: BERTRAND_LABEL_MANAGED,
            REPO_ID_LABEL: repo_id,
        },
        deadline=deadline,
    )
    _assert_managed_repository_volume_claim(
        pvc,
        claim_name=claim_name,
        repo_id=repo_id,
        storage_class=storage_class,
        require_rwx=True,
    )
    return pvc


async def list_repository_volume_claims(
    kube: Kube,
    repo_id: str | None,
    *,
    deadline: Deadline,
) -> list[PersistentVolumeClaim]:
    """List repository volume claims currently present in the cluster.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : str | None
        Optional repository UUID used to filter the claim list.
    deadline : Deadline
        Maximum API budget in seconds.

    Returns
    -------
    list[PersistentVolumeClaim]
        Repository PVCs sorted by repository identity and claim name.

    Raises
    ------
    OSError
        If a discovered PVC is not a valid managed repository volume.
    """
    labels = {
        BERTRAND_LABEL: BERTRAND_LABEL_MANAGED,
        REPO_VOLUME_CLAIM_LABEL: BERTRAND_LABEL_MANAGED,
    }
    if repo_id is not None:
        repo_id = _check_uuid(repo_id)
        labels[REPO_ID_LABEL] = repo_id
    pvcs = await PersistentVolumeClaim.list(
        kube=kube,
        namespaces=(BERTRAND_NAMESPACE,),
        deadline=deadline,
        labels=labels,
    )
    out: list[PersistentVolumeClaim] = []
    for pvc in pvcs:
        labels = pvc.labels
        repo_id = labels.get(REPO_ID_LABEL, "")
        if not repo_id:
            msg = f"cluster PVC {pvc.name!r} is missing label {REPO_ID_LABEL!r}"
            raise OSError(msg)
        repo_id = _check_uuid(repo_id)
        _assert_managed_repository_volume_claim(
            pvc,
            claim_name=repo_volume_claim_name(repo_id),
            repo_id=repo_id,
            storage_class=None,
            require_rwx=False,
        )
        out.append(pvc)

    out.sort(key=lambda pvc: (pvc.labels.get(REPO_ID_LABEL, ""), pvc.name))
    return out


async def delete_repository_volume_claim(
    kube: Kube,
    *,
    pvc: PersistentVolumeClaim,
    deadline: Deadline,
    force: bool,
) -> None:
    """Delete a repository volume claim from the cluster.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    pvc : PersistentVolumeClaim
        Managed repository PVC to delete.
    deadline : Deadline
        Maximum deletion budget in seconds.
    force : bool
        Whether to skip the active-pod safety check.

    Raises
    ------
    OSError
        If the PVC is still in use by active pods.
    """
    repo_id = _check_uuid(pvc.labels.get(REPO_ID_LABEL, ""))
    if not force:
        pods = await Pod.list(
            kube=kube,
            namespaces=(BERTRAND_NAMESPACE,),
            deadline=deadline,
            labels={BERTRAND_LABEL: BERTRAND_LABEL_MANAGED, REPO_ID_LABEL: repo_id},
        )
        active = {
            claim_name
            for pod in pods
            if pod.is_active
            for claim_name in pod.persistent_volume_claim_names
        }
        active.discard("")
        name = pvc.name
        if name in active:
            msg = (
                f"cannot delete repository volume {name!r} while "
                "it is being used by active pods"
            )
            raise OSError(msg)

    await pvc.delete(
        kube,
        namespace=pvc.namespace,
        name=pvc.name,
        deadline=deadline,
    )


async def resolve_repository_volume_ceph_path(
    kube: Kube,
    *,
    pvc: PersistentVolumeClaim,
    deadline: Deadline,
) -> PosixPath:
    """Resolve a repo claim's CephFS path from bound PVC/PV metadata.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    pvc : PersistentVolumeClaim
        Managed repository PVC to inspect.
    deadline : Deadline
        Maximum resolution budget in seconds.

    Returns
    -------
    PosixPath
        CephFS backing path for the bound repository claim.

    Raises
    ------
    OSError
        If PVC/PV metadata cannot identify a valid CephFS path.
    """
    name = pvc.name
    namespace = pvc.namespace
    if not name:
        msg = "cannot resolve Ceph path for PVC with missing metadata.name"
        raise OSError(msg)
    if not namespace:
        msg = (
            f"cannot resolve Ceph path for PVC {name!r} with missing metadata.namespace"
        )
        raise OSError(msg)
    pvc = await pvc.wait_bound(kube=kube, deadline=deadline)
    volume_name = pvc.volume_name
    volume = await PersistentVolume.wait_present(
        kube=kube,
        deadline=deadline,
        name=volume_name,
    )
    driver = volume.csi_driver
    if not driver:
        msg = (
            f"PersistentVolume {volume_name!r} is not CSI-backed and cannot be "
            "mounted as a Ceph repository volume"
        )
        raise OSError(msg)
    if "cephfs" not in driver.lower():
        msg = (
            f"PersistentVolume {volume_name!r} uses CSI driver {driver!r}, "
            "expected a CephFS driver"
        )
        raise OSError(msg)
    ceph_path = volume.ceph_path
    if ceph_path is None:
        msg = (
            "repository PersistentVolume is missing CephFS path attributes "
            "(expected one of 'subvolumePath', 'rootPath', or 'path')"
        )
        raise OSError(msg)
    return ceph_path


class _RepositoryVolumeSpec(BaseModel):
    """Lifecycle spec for one managed Ceph repository volume."""

    model_config = ConfigDict(extra="forbid", frozen=True)
    repo_id: _NonEmptyString
    phase: _RepositoryVolumePhase = "Initializing"
    created_at: datetime
    last_seen_at: datetime
    retired_at: datetime | None = None
    last_gc_at: datetime | None = None
    last_error: str = ""

    _normalize_datetimes = field_validator(
        "created_at",
        "last_seen_at",
        "retired_at",
        "last_gc_at",
    )(_normalize_datetime)
    _serialize_datetimes = field_serializer(
        "created_at",
        "last_seen_at",
        "retired_at",
        "last_gc_at",
    )(_serialize_datetime)

    @property
    def labels(self) -> dict[str, str]:
        return {
            **_REPOSITORY_VOLUME_LABELS,
            REPO_ID_LABEL: self.repo_id,
            REPOSITORY_VOLUME_PHASE_LABEL: self.phase.lower(),
        }


class CephRepositoryMount(BaseModel):
    """Repository mount lifecycle entry embedded in `CephRepositoryState`."""

    model_config = ConfigDict(extra="forbid", frozen=True)
    name: _NonEmptyString
    repo_id: _NonEmptyString
    alias_path: _NonEmptyString
    host_id: _NonEmptyString
    node_name: str = ""
    phase: _RepositoryMountPhase = "Active"
    created_at: datetime
    last_seen_at: datetime
    retired_at: datetime | None = None
    last_error: str = ""

    _normalize_datetimes = field_validator(
        "created_at",
        "last_seen_at",
        "retired_at",
    )(_normalize_datetime)
    _serialize_datetimes = field_serializer(
        "created_at",
        "last_seen_at",
        "retired_at",
    )(_serialize_datetime)

    @property
    def namespace(self) -> str:
        """Return the namespace that owns this record."""
        return BERTRAND_NAMESPACE


class CephRepositoryWorktree(BaseModel):
    """Repository worktree lifecycle entry embedded in `CephRepositoryState`."""

    model_config = ConfigDict(extra="forbid", frozen=True)
    name: _NonEmptyString
    repo_id: _NonEmptyString
    worktree_id: _NonEmptyString
    worktree: str = "."
    phase: _RepositoryWorktreePhase = "Active"
    created_at: datetime
    last_seen_at: datetime
    retired_at: datetime | None = None
    last_error: str = ""

    @field_validator("repo_id", "worktree_id")
    @classmethod
    def _normalize_uuid(cls, value: str) -> str:
        return _check_uuid(value.strip())

    @field_validator("worktree")
    @classmethod
    def _normalize_worktree(cls, value: str) -> str:
        return repository_worktree_path(value)

    _normalize_datetimes = field_validator(
        "created_at",
        "last_seen_at",
        "retired_at",
    )(_normalize_datetime)
    _serialize_datetimes = field_serializer(
        "created_at",
        "last_seen_at",
        "retired_at",
    )(_serialize_datetime)

    @property
    def namespace(self) -> str:
        """Return the namespace that owns this record."""
        return BERTRAND_NAMESPACE


class _RepositoryStateStatus(BaseModel):
    """Collapsed repository lifecycle state."""

    model_config = ConfigDict(extra="forbid")
    mounts: dict[str, CephRepositoryMount] = Field(default_factory=dict)
    worktrees: dict[str, CephRepositoryWorktree] = Field(default_factory=dict)

    def with_mount(self, entry: CephRepositoryMount) -> _RepositoryStateStatus:
        """Return a copy with one mount entry replaced.

        Returns
        -------
        _RepositoryStateStatus
            Updated repository-state status.
        """
        return self.model_copy(update={"mounts": {**self.mounts, entry.name: entry}})

    def with_worktree(self, entry: CephRepositoryWorktree) -> _RepositoryStateStatus:
        """Return a copy with one worktree entry replaced.

        Returns
        -------
        _RepositoryStateStatus
            Updated repository-state status.
        """
        return self.model_copy(
            update={"worktrees": {**self.worktrees, entry.name: entry}},
        )


class CephRepositoryStateRecord(BaseModel):
    """Validated `CephRepositoryState` custom-resource payload."""

    model_config = ConfigDict(extra="forbid", frozen=True, populate_by_name=True)
    api_version: str = Field(alias="apiVersion")
    kind: Literal["CephRepositoryState"]
    metadata: CustomObjectMetadata
    spec: _RepositoryVolumeSpec
    status: _RepositoryStateStatus = Field(default_factory=_RepositoryStateStatus)

    @classmethod
    def parse_payload(cls, payload: object) -> CephRepositoryStateRecord:
        """Validate a Kubernetes custom object payload.

        Returns
        -------
        CephRepositoryStateRecord
            Validated repository state record.

        Raises
        ------
        OSError
            If the payload or embedded lifecycle maps are malformed.
        """
        try:
            record = cls.model_validate(payload)
        except ValidationError as err:
            msg = f"malformed {REPOSITORY_STATE_KIND} custom object: {err}"
            raise OSError(msg) from err

        repo_id = _check_uuid(record.spec.repo_id)
        expected_name = repo_volume_claim_name(repo_id)
        if record.name != expected_name:
            msg = (
                f"malformed {REPOSITORY_STATE_KIND} {record.name!r}: object name "
                f"does not match expected claim name {expected_name!r}"
            )
            raise OSError(msg)
        labels = record.metadata.labels
        if labels.get(REPO_ID_LABEL) != repo_id:
            msg = (
                f"malformed {REPOSITORY_STATE_KIND} {record.name!r}: repo label "
                f"{REPO_ID_LABEL!r} does not match spec repo_id {repo_id!r}"
            )
            raise OSError(msg)
        label_phase = labels.get(REPOSITORY_VOLUME_PHASE_LABEL, "").strip()
        expected_phase = record.spec.phase.lower()
        if label_phase != expected_phase:
            msg = (
                f"malformed {REPOSITORY_STATE_KIND} {record.name!r}: phase label "
                f"{label_phase!r} does not match spec phase {record.spec.phase!r}"
            )
            raise OSError(msg)
        for key, entry in record.status.mounts.items():
            _validate_repository_mount_entry(record, key, entry)
        for key, entry in record.status.worktrees.items():
            _validate_repository_worktree_entry(record, key, entry)
        return record

    @property
    def name(self) -> str:
        """Return the Kubernetes custom object name."""
        return self.metadata.name

    @property
    def namespace(self) -> str:
        """Return the namespace that owns this record."""
        return self.metadata.namespace

    @property
    def generation(self) -> int:
        """Return the Kubernetes metadata generation."""
        return self.metadata.generation

    @property
    def resource_version(self) -> str:
        """Return the Kubernetes resource version."""
        return self.metadata.resource_version

    @property
    def labels(self) -> dict[str, str]:
        """Return Kubernetes labels for this repository state object."""
        return self.metadata.labels

    @property
    def claim_name(self) -> str:
        """Return the deterministic repository PVC name."""
        return repo_volume_claim_name(self.spec.repo_id)


def _validate_repository_mount_entry(
    state: CephRepositoryStateRecord,
    key: str,
    entry: CephRepositoryMount,
) -> None:
    repo_id = _check_uuid(entry.repo_id)
    host_id = _check_uuid(entry.host_id)
    alias_path = repository_mount_alias_path(entry.alias_path)
    expected_name = repository_mount_name(repo_id, host_id, alias_path)
    if key != entry.name:
        msg = (
            f"malformed {REPOSITORY_STATE_KIND} {state.name!r}: mount map key "
            f"{key!r} does not match entry name {entry.name!r}"
        )
        raise OSError(msg)
    if entry.name != expected_name:
        msg = (
            f"malformed {REPOSITORY_STATE_KIND} {state.name!r}: mount entry "
            f"{entry.name!r} expected deterministic name {expected_name!r}"
        )
        raise OSError(msg)
    if repo_id != state.spec.repo_id:
        msg = (
            f"malformed {REPOSITORY_STATE_KIND} {state.name!r}: mount entry "
            f"{entry.name!r} repo_id {repo_id!r} does not match state "
            f"repo_id {state.spec.repo_id!r}"
        )
        raise OSError(msg)


def _validate_repository_worktree_entry(
    state: CephRepositoryStateRecord,
    key: str,
    entry: CephRepositoryWorktree,
) -> None:
    repo_id = _check_uuid(entry.repo_id)
    worktree_id = _check_uuid(entry.worktree_id)
    expected_name = repository_worktree_name(repo_id, worktree_id)
    if key != entry.name:
        msg = (
            f"malformed {REPOSITORY_STATE_KIND} {state.name!r}: worktree map key "
            f"{key!r} does not match entry name {entry.name!r}"
        )
        raise OSError(msg)
    if entry.name != expected_name:
        msg = (
            f"malformed {REPOSITORY_STATE_KIND} {state.name!r}: worktree entry "
            f"{entry.name!r} expected deterministic name {expected_name!r}"
        )
        raise OSError(msg)
    if repo_id != state.spec.repo_id:
        msg = (
            f"malformed {REPOSITORY_STATE_KIND} {state.name!r}: worktree entry "
            f"{entry.name!r} repo_id {repo_id!r} does not match state "
            f"repo_id {state.spec.repo_id!r}"
        )
        raise OSError(msg)


REPOSITORY_STATE_RESOURCE = CustomObjectResource[CephRepositoryStateRecord](
    group=REPOSITORY_VOLUME_GROUP,
    version=REPOSITORY_VOLUME_VERSION,
    kind=REPOSITORY_STATE_KIND,
    plural=REPOSITORY_STATE_PLURAL,
    labels=_REPOSITORY_VOLUME_LABELS,
    singular="cephrepositorystate",
    short_names=("crs",),
    payload_parser=CephRepositoryStateRecord.parse_payload,
    spec_model=_RepositoryVolumeSpec,
    spec_schema_overrides={
        "required": [
            "repo_id",
            "phase",
            "created_at",
            "last_seen_at",
            "last_error",
        ],
    },
    status_model=_RepositoryStateStatus,
)


@dataclass(frozen=True)
class _RepositoryVolumeGcInventory:
    buildkit_builds: tuple[BuildKitBuildRecord, ...]
    deployments: tuple[Deployment, ...]
    cronjobs: tuple[CronJob, ...]
    jobs: tuple[Job, ...]
    pods: tuple[Pod, ...]
    retained_snapshots: tuple[CustomObject, ...]

    @classmethod
    async def collect(
        cls,
        kube: Kube,
        *,
        deadline: Deadline,
    ) -> _RepositoryVolumeGcInventory:
        buildkit_task = asyncio.create_task(
            _list_buildkit_records(kube, deadline=deadline)
        )
        deployment_task = asyncio.create_task(
            Deployment.list(
                kube,
                namespaces=(BERTRAND_NAMESPACE,),
                labels={WORKLOAD_LABEL: WORKLOAD_LABEL_VALUE},
                deadline=deadline,
            )
        )
        cronjob_task = asyncio.create_task(
            CronJob.list(
                kube,
                namespaces=(BERTRAND_NAMESPACE,),
                labels={WORKLOAD_LABEL: WORKLOAD_LABEL_VALUE},
                deadline=deadline,
            )
        )
        job_task = asyncio.create_task(
            Job.list(
                kube,
                namespaces=(BERTRAND_NAMESPACE,),
                labels={WORKLOAD_LABEL: WORKLOAD_LABEL_VALUE},
                deadline=deadline,
            )
        )
        pod_task = asyncio.create_task(
            Pod.list(
                kube,
                namespaces=(BERTRAND_NAMESPACE,),
                deadline=deadline,
            )
        )
        snapshot_task = asyncio.create_task(
            VolumeSnapshot.list(
                kube,
                namespace=BERTRAND_NAMESPACE,
                labels={
                    BERTRAND_LABEL: BERTRAND_LABEL_MANAGED,
                    REPOSITORY_SNAPSHOT_LABEL: REPOSITORY_SNAPSHOT_LABEL_VALUE,
                    REPOSITORY_SNAPSHOT_PURPOSE_LABEL: (
                        REPOSITORY_SNAPSHOT_PURPOSE_RETAINED
                    ),
                },
                deadline=deadline,
            )
        )
        await asyncio.gather(
            buildkit_task,
            deployment_task,
            cronjob_task,
            job_task,
            pod_task,
            snapshot_task,
        )
        return cls(
            buildkit_builds=tuple(buildkit_task.result()),
            deployments=tuple(deployment_task.result()),
            cronjobs=tuple(cronjob_task.result()),
            jobs=tuple(job_task.result()),
            pods=tuple(pod_task.result()),
            retained_snapshots=tuple(snapshot_task.result()),
        )

    def active_buildkit_builds(self, repo_id: str) -> bool:
        repo_id = _check_uuid(repo_id)
        return any(
            build.spec.repo_id == repo_id and build.is_active
            for build in self.buildkit_builds
        )

    def workload_blocker(self, repo_id: str) -> str | None:
        repo_id = _check_uuid(repo_id)
        deployments = [
            item
            for item in self.deployments
            if item.labels.get(REPO_ID_LABEL) == repo_id
        ]
        if deployments:
            names = ", ".join(item.name for item in deployments[:3])
            return f"managed Deployments still reference repository {repo_id}: {names}"

        cronjobs = [
            item for item in self.cronjobs if item.labels.get(REPO_ID_LABEL) == repo_id
        ]
        if cronjobs:
            names = ", ".join(item.name for item in cronjobs[:3])
            return f"managed CronJobs still reference repository {repo_id}: {names}"

        jobs = [
            item
            for item in self.jobs
            if (
                item.labels.get(REPO_ID_LABEL) == repo_id
                and not item.is_complete
                and not item.is_failed
            )
        ]
        if jobs:
            names = ", ".join(item.name for item in jobs[:3])
            return f"active Jobs still reference repository {repo_id}: {names}"
        return None

    def active_image_blocker(self, repo_id: str) -> str | None:
        repo_id = _check_uuid(repo_id)
        active = [
            build
            for build in self.buildkit_builds
            if build.spec.repo_id == repo_id and build.image_phase == "Active"
        ]
        if active:
            names = ", ".join(build.name for build in active[:3])
            return (
                "active project image records still reference repository "
                f"{repo_id}: {names}"
            )
        return None

    def pod_blocker(self, record: CephRepositoryStateRecord) -> str | None:
        active_pods = [
            pod
            for pod in self.pods
            if (
                not pod.is_terminal
                and record.claim_name in pod.persistent_volume_claim_names
            )
        ]
        if active_pods:
            names = ", ".join(pod.name for pod in active_pods[:3])
            return (
                "active Pods still reference repository PVC "
                f"{record.claim_name}: {names}"
            )
        return None

    def has_retained_snapshot(self, repo_id: str) -> bool:
        repo_id = _check_uuid(repo_id)
        return any(
            snapshot.labels.get(REPO_ID_LABEL) == repo_id
            for snapshot in self.retained_snapshots
        )


async def _list_buildkit_records(
    kube: Kube,
    *,
    deadline: Deadline,
) -> list[BuildKitBuildRecord]:
    try:
        return await BUILDKIT_BUILD_RESOURCE.list(kube, deadline=deadline)
    except OSError as err:
        if isinstance(err, Kube.APIError) and err.missing_api_resource:
            return []
        raise


async def ensure_repository_volume_record(
    kube: Kube,
    *,
    repo_id: str,
    deadline: Deadline,
) -> CephRepositoryStateRecord:
    """Mark a managed repository volume as initializing or recently observed.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : str
        Stable repository UUID.
    deadline : Deadline
        Maximum convergence budget in seconds.

    Returns
    -------
    CephRepositoryStateRecord
        Lifecycle record for the repository volume.

    """
    repo_id = _check_uuid(repo_id)
    claim_name = repo_volume_claim_name(repo_id)
    await REPOSITORY_STATE_RESOURCE.ensure_crd(
        kube,
        deadline=deadline,
    )
    existing = await REPOSITORY_STATE_RESOURCE.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=claim_name,
        deadline=deadline,
    )
    now = datetime.now(UTC)
    spec = _RepositoryVolumeSpec(
        repo_id=repo_id,
        phase=(
            "Ready"
            if existing is not None and existing.spec.phase == "Ready"
            else "Initializing"
        ),
        created_at=existing.spec.created_at if existing is not None else now,
        last_seen_at=now,
        last_gc_at=None,
        last_error="",
    )
    return await REPOSITORY_STATE_RESOURCE.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=claim_name,
        spec=spec,
        labels=spec.labels,
        deadline=deadline,
    )


async def repository_volume_ready(
    kube: Kube,
    *,
    repo_id: str,
    deadline: Deadline,
) -> bool:
    """Return whether a repository ID already has a Ready lifecycle record.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : str
        Stable repository UUID.
    deadline : Deadline
        Maximum read budget in seconds.

    Returns
    -------
    bool
        True only when the repository lifecycle record exists and is in the `Ready`
        phase. Missing records, missing CRDs, `Initializing`, and `Failed` records are
        treated as not ready so init can adopt or retry them.

    Raises
    ------
    OSError
        If the lifecycle record cannot be read for any reason other than a missing
        custom-resource API.

    """
    repo_id = _check_uuid(repo_id)
    try:
        record = await REPOSITORY_STATE_RESOURCE.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=repo_volume_claim_name(repo_id),
            deadline=deadline,
        )
    except OSError as err:
        if isinstance(err, Kube.APIError) and err.missing_api_resource:
            return False
        raise
    return record is not None and record.spec.phase == "Ready"


async def mark_repository_volume_ready(
    kube: Kube,
    *,
    repo_id: str,
    deadline: Deadline,
) -> CephRepositoryStateRecord:
    """Mark a repository volume as finalized and safe for normal recovery.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : str
        Stable repository UUID.
    deadline : Deadline
        Maximum convergence budget in seconds.

    Returns
    -------
    CephRepositoryStateRecord
        Ready lifecycle record for the repository volume.

    """
    repo_id = _check_uuid(repo_id)
    claim_name = repo_volume_claim_name(repo_id)
    await REPOSITORY_STATE_RESOURCE.ensure_crd(
        kube,
        deadline=deadline,
    )
    existing = await REPOSITORY_STATE_RESOURCE.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=claim_name,
        deadline=deadline,
    )
    now = datetime.now(UTC)
    if existing is None:
        spec = _RepositoryVolumeSpec(
            repo_id=repo_id,
            phase="Ready",
            created_at=now,
            last_seen_at=now,
            last_error="",
        )
        return await REPOSITORY_STATE_RESOURCE.upsert(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=claim_name,
            spec=spec,
            labels=spec.labels,
            deadline=deadline,
        )

    spec = existing.spec.model_copy(
        update={
            "phase": "Ready",
            "last_seen_at": now,
            "last_error": "",
        },
    )
    return await REPOSITORY_STATE_RESOURCE.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=claim_name,
        spec=spec,
        labels=spec.labels,
        deadline=deadline,
    )


async def mark_repository_volume_failed(
    kube: Kube,
    *,
    repo_id: str,
    last_error: str,
    deadline: Deadline,
) -> CephRepositoryStateRecord | None:
    """Mark an incomplete repository volume as failed.

    Ready or retired records are left unchanged so a later failed config render does
    not poison a usable repository volume.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : str
        Stable repository UUID.
    last_error : str
        Diagnostic describing the failed initialization stage.
    deadline : Deadline
        Maximum convergence budget in seconds.

    Returns
    -------
    CephRepositoryStateRecord | None
        Updated failed record, existing ready/retired record, or None if the volume
        has not yet been recorded.
    """
    if deadline.remaining <= 0:
        return None
    repo_id = _check_uuid(repo_id)
    claim_name = repo_volume_claim_name(repo_id)
    await REPOSITORY_STATE_RESOURCE.ensure_crd(
        kube,
        deadline=deadline,
    )
    record = await REPOSITORY_STATE_RESOURCE.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=claim_name,
        deadline=deadline,
    )
    if record is None or record.spec.phase in ("Ready", "Retired"):
        return record

    spec = record.spec.model_copy(
        update={
            "phase": "Failed",
            "last_seen_at": datetime.now(UTC),
            "last_error": last_error,
        },
    )
    return await REPOSITORY_STATE_RESOURCE.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=claim_name,
        spec=spec,
        labels=spec.labels,
        deadline=deadline,
    )


async def ensure_repository_mount_record(
    kube: Kube,
    *,
    repo_id: str,
    host_id: str,
    alias_path: str,
    node_name: str,
    deadline: Deadline,
) -> CephRepositoryMount:
    """Mark a host repository mount alias as active and recently observed.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : str
        Stable repository UUID.
    host_id : str
        Durable Bertrand host UUID.
    alias_path : str
        Absolute host path used as the repository mount alias.
    node_name : str
        Best-effort local node name to store for diagnostics.
    deadline : Deadline
        Maximum convergence budget in seconds.

    Returns
    -------
    CephRepositoryMount
        Active lifecycle record for the host alias.

    """
    repo_id = _check_uuid(repo_id)
    host_id = _check_uuid(host_id)
    alias_path = repository_mount_alias_path(alias_path)
    claim_name = repo_volume_claim_name(repo_id)
    await REPOSITORY_STATE_RESOURCE.ensure_crd(
        kube,
        deadline=deadline,
    )
    state = await REPOSITORY_STATE_RESOURCE.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=claim_name,
        deadline=deadline,
    )
    if state is None:
        now = datetime.now(UTC)
        spec = _RepositoryVolumeSpec(
            repo_id=repo_id,
            phase="Initializing",
            created_at=now,
            last_seen_at=now,
            last_gc_at=None,
            last_error="",
        )
        state = await REPOSITORY_STATE_RESOURCE.upsert(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=claim_name,
            spec=spec,
            labels=spec.labels,
            deadline=deadline,
        )
    name = repository_mount_name(repo_id, host_id, alias_path)
    existing = state.status.mounts.get(name)
    now = datetime.now(UTC)
    created_at = existing.created_at if existing is not None else now
    entry = CephRepositoryMount(
        name=name,
        repo_id=repo_id,
        alias_path=alias_path,
        host_id=host_id,
        node_name=node_name.strip(),
        phase="Active",
        created_at=created_at,
        last_seen_at=now,
        retired_at=None,
        last_error="",
    )
    state = await REPOSITORY_STATE_RESOURCE.patch_status(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=state.name,
        status=state.status.with_mount(entry),
        deadline=deadline,
    )
    return state.status.mounts[name]


async def ensure_repository_worktree_record(
    kube: Kube,
    *,
    repo_id: str,
    worktree_id: str,
    worktree: str,
    deadline: Deadline,
) -> CephRepositoryWorktree:
    """Mark a repository worktree as active and recently observed.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : str
        Stable repository UUID.
    worktree_id : str
        Persistent worktree UUID stored in the worktree metadata directory.
    worktree : str
        Repository-relative worktree path.
    deadline : Deadline
        Maximum convergence budget in seconds.

    Returns
    -------
    CephRepositoryWorktree
        Active lifecycle record for the worktree.

    """
    repo_id = _check_uuid(repo_id)
    worktree_id = _check_uuid(worktree_id)
    worktree = repository_worktree_path(worktree)
    claim_name = repo_volume_claim_name(repo_id)
    await REPOSITORY_STATE_RESOURCE.ensure_crd(
        kube,
        deadline=deadline,
    )
    state = await REPOSITORY_STATE_RESOURCE.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=claim_name,
        deadline=deadline,
    )
    if state is None:
        now = datetime.now(UTC)
        spec = _RepositoryVolumeSpec(
            repo_id=repo_id,
            phase="Initializing",
            created_at=now,
            last_seen_at=now,
            last_gc_at=None,
            last_error="",
        )
        state = await REPOSITORY_STATE_RESOURCE.upsert(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=claim_name,
            spec=spec,
            labels=spec.labels,
            deadline=deadline,
        )
    name = repository_worktree_name(repo_id, worktree_id)
    existing = state.status.worktrees.get(name)
    now = datetime.now(UTC)
    created_at = existing.created_at if existing is not None else now
    entry = CephRepositoryWorktree(
        name=name,
        repo_id=repo_id,
        worktree_id=worktree_id,
        worktree=worktree,
        phase="Active",
        created_at=created_at,
        last_seen_at=now,
        retired_at=None,
        last_error="",
    )
    state = await REPOSITORY_STATE_RESOURCE.patch_status(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=state.name,
        status=state.status.with_worktree(entry),
        deadline=deadline,
    )
    return state.status.worktrees[name]


async def retire_repository_mount_record(
    kube: Kube,
    *,
    record: CephRepositoryMount,
    deadline: Deadline,
) -> CephRepositoryMount:
    """Retire one repository mount lifecycle record without deleting storage.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    record : CephRepositoryMount
        Mount lifecycle record to retire.
    deadline : Deadline
        Maximum request budget in seconds.

    Returns
    -------
    CephRepositoryMount
        Retired lifecycle record.

    """
    repo_id = _check_uuid(record.repo_id)
    claim_name = repo_volume_claim_name(repo_id)
    await REPOSITORY_STATE_RESOURCE.ensure_crd(
        kube,
        deadline=deadline,
    )
    retired_at = record.retired_at or datetime.now(UTC)
    entry = record.model_copy(
        update={
            "phase": "Retired",
            "last_seen_at": record.last_seen_at,
            "retired_at": retired_at,
            "last_error": "",
        }
    )
    state = await REPOSITORY_STATE_RESOURCE.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=claim_name,
        deadline=deadline,
    )
    if state is None:
        now = datetime.now(UTC)
        spec = _RepositoryVolumeSpec(
            repo_id=repo_id,
            phase="Initializing",
            created_at=now,
            last_seen_at=now,
            last_gc_at=None,
            last_error="",
        )
        state = await REPOSITORY_STATE_RESOURCE.upsert(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=claim_name,
            spec=spec,
            labels=spec.labels,
            deadline=deadline,
        )
    state = await REPOSITORY_STATE_RESOURCE.patch_status(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=state.name,
        status=state.status.with_mount(entry),
        deadline=deadline,
    )
    return state.status.mounts[record.name]


async def retire_repository_mount(
    kube: Kube,
    *,
    repo_id: str,
    host_id: str,
    alias_path: str,
    deadline: Deadline,
) -> CephRepositoryMount | None:
    """Retire one repository mount lifecycle record by identity.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : str
        Stable repository UUID.
    host_id : str
        Durable Bertrand host UUID.
    alias_path : str
        Absolute host path used as the repository mount alias.
    deadline : Deadline
        Maximum request budget in seconds.

    Returns
    -------
    CephRepositoryMount | None
        Retired lifecycle record, or None if the record does not exist.

    """
    repo_id = _check_uuid(repo_id)
    host_id = _check_uuid(host_id)
    alias_path = repository_mount_alias_path(alias_path)
    claim_name = repo_volume_claim_name(repo_id)
    await REPOSITORY_STATE_RESOURCE.ensure_crd(
        kube,
        deadline=deadline,
    )
    state = await REPOSITORY_STATE_RESOURCE.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=claim_name,
        deadline=deadline,
    )
    if state is None:
        return None
    record = state.status.mounts.get(
        repository_mount_name(repo_id, host_id, alias_path)
    )
    if record is None:
        return None
    retired_at = record.retired_at or datetime.now(UTC)
    entry = record.model_copy(
        update={
            "phase": "Retired",
            "last_seen_at": record.last_seen_at,
            "retired_at": retired_at,
            "last_error": "",
        }
    )
    state = await REPOSITORY_STATE_RESOURCE.patch_status(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=state.name,
        status=state.status.with_mount(entry),
        deadline=deadline,
    )
    return state.status.mounts[record.name]


async def delete_repository_snapshot_artifacts(
    kube: Kube,
    *,
    repo_id: str,
    deadline: Deadline,
) -> None:
    """Delete all snapshot resources associated with one repository.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : str
        Stable repository UUID.
    deadline : Deadline
        Maximum deletion budget in seconds.

    """
    repo_id = _check_uuid(repo_id)
    pvcs = await PersistentVolumeClaim.list(
        kube,
        namespaces=(BERTRAND_NAMESPACE,),
        labels={
            BERTRAND_LABEL: BERTRAND_LABEL_MANAGED,
            REPO_ID_LABEL: repo_id,
            REPOSITORY_BUILD_SOURCE_LABEL: REPOSITORY_BUILD_SOURCE_LABEL_VALUE,
        },
        deadline=deadline,
    )
    for pvc in sorted(pvcs, key=lambda item: item.name):
        await pvc.delete(
            kube,
            namespace=pvc.namespace,
            name=pvc.name,
            deadline=deadline,
        )
        await pvc.wait(
            kube,
            deadline=deadline,
            predicate=lambda live: live is None,
        )

    snapshots = await VolumeSnapshot.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        labels={
            BERTRAND_LABEL: BERTRAND_LABEL_MANAGED,
            REPO_ID_LABEL: repo_id,
            REPOSITORY_SNAPSHOT_LABEL: REPOSITORY_SNAPSHOT_LABEL_VALUE,
        },
        deadline=deadline,
    )
    for snapshot in sorted(snapshots, key=lambda item: item.name):
        await snapshot.delete(
            kube,
            namespace=snapshot.namespace,
            name=snapshot.name,
            deadline=deadline,
        )


async def delete_all_repository_volumes(
    kube: Kube,
    *,
    deadline: Deadline,
) -> list[CephRepositoryStateRecord]:
    """Delete all managed repository volumes during final cluster teardown.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    deadline : Deadline
        Maximum deletion budget in seconds.

    Returns
    -------
    list[CephRepositoryStateRecord]
        Repository state records deleted by this teardown pass.
    """
    await REPOSITORY_STATE_RESOURCE.ensure_crd(kube, deadline=deadline)
    records = await REPOSITORY_STATE_RESOURCE.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        deadline=deadline,
    )
    deleted: list[CephRepositoryStateRecord] = []
    for record in sorted(records, key=lambda item: item.name):
        repo_id = record.spec.repo_id
        await delete_repository_snapshot_artifacts(
            kube,
            repo_id=repo_id,
            deadline=deadline,
        )
        for pvc in await list_repository_volume_claims(
            kube,
            repo_id,
            deadline=deadline,
        ):
            await delete_repository_volume_claim(
                kube,
                pvc=pvc,
                deadline=deadline,
                force=True,
            )

        credentials = await RepoCredentials.get(repo_id, deadline=deadline)
        if credentials is not None:
            await credentials.delete(deadline=deadline)

        for worktree in sorted(
            record.status.worktrees.values(),
            key=lambda item: (item.worktree, item.worktree_id),
        ):
            await delete_capabilities_for_scope(
                kube,
                scope="worktree",
                scope_value=worktree.worktree_id,
                deadline=deadline,
            )
        await delete_capabilities_for_scope(
            kube,
            scope="repository",
            scope_value=repo_id,
            deadline=deadline,
        )
        await REPOSITORY_STATE_RESOURCE.delete(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=record.name,
            deadline=deadline,
        )
        deleted.append(record)
    return deleted


async def gc_repository_volumes(
    kube: Kube,
    *,
    deadline: Deadline,
    grace_seconds: int = REPOSITORY_VOLUME_GC_GRACE_SECONDS,
    limit: int = REPOSITORY_VOLUME_GC_LIMIT,
) -> list[CephRepositoryStateRecord]:
    """Delete eligible retired repository volumes and lifecycle records.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    deadline : Deadline
        Maximum GC budget in seconds.
    grace_seconds : int, optional
        Minimum time a record must remain retired before collection.
    limit : int, optional
        Maximum number of repository volumes to collect in this pass.

    Returns
    -------
    list[CephRepositoryStateRecord]
        Records collected during this GC pass.

    Raises
    ------
    ValueError
        If `grace_seconds` or `limit` is negative.
    """
    if grace_seconds < 0:
        msg = "repository volume GC grace_seconds must be non-negative"
        raise ValueError(msg)
    if limit < 0:
        msg = "repository volume GC limit must be non-negative"
        raise ValueError(msg)
    if limit == 0:
        return []
    now = datetime.now(UTC)
    grace = timedelta(seconds=grace_seconds)
    await REPOSITORY_STATE_RESOURCE.ensure_crd(
        kube,
        deadline=deadline,
    )
    records = await REPOSITORY_STATE_RESOURCE.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        labels={REPOSITORY_VOLUME_PHASE_LABEL: "retired"},
        deadline=deadline,
    )
    inventory: _RepositoryVolumeGcInventory | None = None
    collected: list[CephRepositoryStateRecord] = []

    for record in sorted(
        records,
        key=lambda item: (
            item.spec.retired_at or datetime.max.replace(tzinfo=UTC),
            item.name,
        ),
    ):
        if record.spec.phase != "Retired" or record.spec.retired_at is None:
            continue
        if now - record.spec.retired_at < grace:
            continue
        if len(collected) >= limit:
            break

        try:
            if inventory is None:
                inventory = await _RepositoryVolumeGcInventory.collect(
                    kube,
                    deadline=deadline,
                )
            if inventory.active_buildkit_builds(record.spec.repo_id):
                blocker = "active BuildKit builds still reference repository"
            else:
                blocker = None

            if blocker is None:
                active_mounts = [
                    item
                    for item in record.status.mounts.values()
                    if item.repo_id == record.spec.repo_id and item.phase == "Active"
                ]
                if active_mounts:
                    names = ", ".join(item.name for item in active_mounts[:3])
                    blocker = f"active repository mount records still exist: {names}"

            if blocker is None:
                active_worktrees = [
                    item
                    for item in record.status.worktrees.values()
                    if item.repo_id == record.spec.repo_id and item.phase == "Active"
                ]
                if active_worktrees:
                    names = ", ".join(item.worktree for item in active_worktrees[:3])
                    blocker = f"active repository worktree records still exist: {names}"

            if blocker is None:
                blocker = inventory.workload_blocker(record.spec.repo_id)

            if blocker is None:
                blocker = inventory.active_image_blocker(record.spec.repo_id)

            if blocker is None:
                blocker = inventory.pod_blocker(record)

            if blocker is None and inventory.has_retained_snapshot(record.spec.repo_id):
                blocker = "retained repository snapshots still exist"

            if blocker is not None:
                spec = record.spec.model_copy(
                    update={
                        "phase": "Retired",
                        "last_gc_at": now,
                        "last_error": blocker,
                    },
                )
                await REPOSITORY_STATE_RESOURCE.upsert(
                    kube,
                    namespace=BERTRAND_NAMESPACE,
                    name=record.name,
                    spec=spec,
                    labels=spec.labels,
                    deadline=deadline,
                )
                continue

            await delete_repository_snapshot_artifacts(
                kube,
                repo_id=record.spec.repo_id,
                deadline=deadline,
            )
            volumes = await list_repository_volume_claims(
                kube,
                record.spec.repo_id,
                deadline=deadline,
            )
            collection_error = None
            if len(volumes) > 1:
                names = ", ".join(sorted(volume.name for volume in volumes))
                collection_error = (
                    "repository identity maps to multiple cluster claims and cannot "
                    f"be garbage-collected safely for {record.spec.repo_id!r}: "
                    f"{names}"
                )
            elif volumes:
                pvc = volumes[0]
                repo_id = pvc.labels.get(REPO_ID_LABEL, "")
                if repo_id != record.spec.repo_id or pvc.name != record.claim_name:
                    collection_error = (
                        f"{REPOSITORY_STATE_KIND} {record.name!r} points to "
                        f"{record.spec.repo_id}/{record.claim_name}, but "
                        f"discovered PVC {repo_id}/{pvc.name}"
                    )
                else:
                    await delete_repository_volume_claim(
                        kube,
                        pvc=pvc,
                        deadline=deadline,
                        force=False,
                    )
            if collection_error is not None:
                spec = record.spec.model_copy(
                    update={
                        "phase": "Retired",
                        "last_gc_at": now,
                        "last_error": collection_error,
                    },
                )
                await REPOSITORY_STATE_RESOURCE.upsert(
                    kube,
                    namespace=BERTRAND_NAMESPACE,
                    name=record.name,
                    spec=spec,
                    labels=spec.labels,
                    deadline=deadline,
                )
                continue

            credentials = await RepoCredentials.get(
                record.spec.repo_id,
                deadline=deadline,
            )
            if credentials is not None:
                await credentials.delete(deadline=deadline)

            await _delete_repository_parented_records(
                kube,
                record=record,
                deadline=deadline,
            )
            await REPOSITORY_STATE_RESOURCE.delete(
                kube,
                namespace=BERTRAND_NAMESPACE,
                name=record.name,
                deadline=deadline,
            )
        except (OSError, TimeoutError, ValueError) as err:
            spec = record.spec.model_copy(
                update={
                    "phase": "Retired",
                    "last_gc_at": now,
                    "last_error": str(err),
                },
            )
            await REPOSITORY_STATE_RESOURCE.upsert(
                kube,
                namespace=BERTRAND_NAMESPACE,
                name=record.name,
                spec=spec,
                labels=spec.labels,
                deadline=deadline,
            )
            continue
        collected.append(record)
    return collected


async def next_repository_volume_gc_time(
    kube: Kube,
    *,
    deadline: Deadline,
    grace_seconds: int = REPOSITORY_VOLUME_GC_GRACE_SECONDS,
) -> datetime | None:
    """Return the next time retired repository volumes may be GC-eligible.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    deadline : Deadline
        Maximum request budget in seconds.
    grace_seconds : int, optional
        Minimum time a record must remain retired before collection.

    Returns
    -------
    datetime | None
        Earliest retirement grace boundary among retired records, or None when
        there are no retired records.

    Raises
    ------
    ValueError
        If `grace_seconds` is negative.
    """
    if grace_seconds < 0:
        msg = "repository volume GC grace_seconds must be non-negative"
        raise ValueError(msg)
    await REPOSITORY_STATE_RESOURCE.ensure_crd(kube, deadline=deadline)
    records = await REPOSITORY_STATE_RESOURCE.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        labels={REPOSITORY_VOLUME_PHASE_LABEL: "retired"},
        deadline=deadline,
    )
    if not records:
        return None
    now = datetime.now(UTC)
    boundaries = [
        record.spec.retired_at + timedelta(seconds=grace_seconds)
        for record in records
        if record.spec.retired_at is not None
    ]
    if len(boundaries) != len(records):
        return now
    return min(boundaries)


async def _delete_repository_parented_records(
    kube: Kube,
    *,
    record: CephRepositoryStateRecord,
    deadline: Deadline,
) -> None:
    for worktree in sorted(
        record.status.worktrees.values(),
        key=lambda item: (item.worktree, item.worktree_id),
    ):
        if worktree.phase == "Active":
            msg = (
                f"cannot delete repository volume {record.spec.repo_id}: active "
                f"worktree record {worktree.name!r} still exists"
            )
            raise OSError(msg)
        await delete_capabilities_for_scope(
            kube,
            scope="worktree",
            scope_value=worktree.worktree_id,
            deadline=deadline,
        )
    await delete_capabilities_for_scope(
        kube,
        scope="repository",
        scope_value=record.spec.repo_id,
        deadline=deadline,
    )
