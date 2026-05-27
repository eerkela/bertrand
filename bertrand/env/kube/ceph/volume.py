"""CephFS-backed repository volume lifecycle helpers."""

from __future__ import annotations

import hashlib
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
from bertrand.env.git import BERTRAND_ENV, BERTRAND_NAMESPACE, REPO_ID_ENV, Deadline
from bertrand.env.kube.api._helpers import _is_missing_api_resource
from bertrand.env.kube.build.request import (
    BUILDKIT_BUILD_IMAGE_PHASE_LABEL,
    BUILDKIT_BUILD_REPO_LABEL,
    BUILDKIT_BUILD_RESOURCE,
    buildkit_build_label_hash,
    has_active_buildkit_builds,
)
from bertrand.env.kube.capability.base import delete_capabilities_for_scope
from bertrand.env.kube.ceph.auth import RepoCredentials
from bertrand.env.kube.cronjob import CronJob
from bertrand.env.kube.custom_object import (
    CustomObjectMetadata,
    CustomObjectResource,
)
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.job import Job
from bertrand.env.kube.pod import Pod
from bertrand.env.kube.snapshot import (
    VOLUME_SNAPSHOT_RESOURCE,
    delete_volume_snapshot,
)
from bertrand.env.kube.volume import (
    PersistentVolume,
    PersistentVolumeClaim,
    StorageClass,
)
from bertrand.env.kube.workload.base import (
    WORKLOAD_LABEL,
    WORKLOAD_LABEL_VALUE,
    WORKLOAD_REPO_LABEL,
)

if TYPE_CHECKING:
    from pathlib import PosixPath

    from bertrand.env.kube.api.client import Kube

REPO_VOLUME_ENV: str = "BERTRAND_REPO_VOLUME"
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
REPOSITORY_MOUNT_LABEL = "bertrand.dev/ceph-repository-mount"
REPOSITORY_MOUNT_LABEL_VALUE = "v1"
REPOSITORY_MOUNT_PHASE_LABEL = "bertrand.dev/ceph-repository-mount-phase"
REPOSITORY_MOUNT_PATH_HASH_LABEL = "bertrand.dev/ceph-repository-mount-path"
REPOSITORY_MOUNT_HOST_HASH_LABEL = "bertrand.dev/ceph-repository-mount-host"
REPOSITORY_WORKTREE_LABEL = "bertrand.dev/ceph-repository-worktree"
REPOSITORY_WORKTREE_LABEL_VALUE = "v1"
REPOSITORY_WORKTREE_PHASE_LABEL = "bertrand.dev/ceph-repository-worktree-phase"
REPOSITORY_WORKTREE_ID_HASH_LABEL = "bertrand.dev/ceph-repository-worktree-id"
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
    BERTRAND_ENV: "1",
    REPO_VOLUME_ENV: "1",
    REPOSITORY_VOLUME_LABEL: REPOSITORY_VOLUME_LABEL_VALUE,
}
_REPOSITORY_MOUNT_LABELS = {
    BERTRAND_ENV: "1",
    REPO_VOLUME_ENV: "1",
    REPOSITORY_MOUNT_LABEL: REPOSITORY_MOUNT_LABEL_VALUE,
}
_REPOSITORY_WORKTREE_LABELS = {
    BERTRAND_ENV: "1",
    REPO_VOLUME_ENV: "1",
    REPOSITORY_WORKTREE_LABEL: REPOSITORY_WORKTREE_LABEL_VALUE,
}

type _RepositoryVolumePhase = Literal[
    "Active",
    "Initializing",
    "Ready",
    "Failed",
    "Retired",
]
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


def repository_mount_path_hash(alias_path: str) -> str:
    """Return the Kubernetes-label-safe hash for a mount alias path.

    Parameters
    ----------
    alias_path : str
        Absolute host path used as a repository mount alias.

    Returns
    -------
    str
        Short SHA-256 hex digest used in mount record labels.
    """
    alias_path = repository_mount_alias_path(alias_path)
    return _hash_label(alias_path)


def repository_mount_host_hash(host_id: str) -> str:
    """Return the Kubernetes-label-safe hash for a host identity.

    Parameters
    ----------
    host_id : str
        Durable Bertrand host UUID.

    Returns
    -------
    str
        Short SHA-256 hex digest used in mount record labels.
    """
    host_id = _check_uuid(host_id)
    return _hash_label(host_id)


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
        Repository-relative worktree path. Empty and ``.`` normalize to ``.``.

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


def repository_worktree_id_hash(worktree_id: str) -> str:
    """Return the Kubernetes-label-safe hash for a worktree UUID.

    Returns
    -------
    str
        Short SHA-256 hex digest for the worktree UUID.
    """
    return _hash_label(_check_uuid(worktree_id))


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


def _hash_label(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()[:40]


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
    if labels.get(BERTRAND_ENV) != "1" or labels.get(REPO_VOLUME_ENV) != "1":
        msg = (
            f"cluster PVC {claim_name!r} collides with Bertrand volume claim, but "
            "is not managed by Bertrand"
        )
        raise OSError(msg)
    actual_repo_id = labels.get(REPO_ID_ENV)
    if actual_repo_id != repo_id:
        msg = (
            f"cluster PVC {claim_name!r} has mismatched repo identity label "
            f"{REPO_ID_ENV!r}: expected {repo_id!r}, got {actual_repo_id!r}"
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
    timeout: float,
    size_request: str,
) -> PersistentVolumeClaim:
    """Ensure a deterministic, cluster-wide RWX claim exists.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : str
        Repository UUID used to derive the claim name and ownership labels.
    timeout : float
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
    deadline = Deadline.from_timeout(
        timeout,
        message="repository volume claim convergence timeout must be non-negative",
    )
    size_request = size_request.strip()
    if not size_request:
        msg = "size request cannot be empty"
        raise ValueError(msg)
    claim_name = repo_volume_claim_name(repo_id)
    storage = await StorageClass.select(
        kube=kube,
        timeout=deadline.remaining(),
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
            BERTRAND_ENV: "1",
            REPO_VOLUME_ENV: "1",
            REPO_ID_ENV: repo_id,
        },
        timeout=deadline.remaining(),
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
    timeout: float,
) -> list[PersistentVolumeClaim]:
    """List repository volume claims currently present in the cluster.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : str | None
        Optional repository UUID used to filter the claim list.
    timeout : float
        Maximum API budget in seconds.

    Returns
    -------
    list[PersistentVolumeClaim]
        Repository PVCs sorted by repository identity and claim name.

    Raises
    ------
    OSError
        If a discovered PVC is not a valid managed repository volume.
    TimeoutError
        If listing does not complete within the timeout.
    """
    labels = {BERTRAND_ENV: "1", REPO_VOLUME_ENV: "1"}
    if repo_id is not None:
        repo_id = _check_uuid(repo_id)
        labels[REPO_ID_ENV] = repo_id
    if timeout <= 0:
        msg = "timeout must be non-negative"
        raise TimeoutError(msg)
    pvcs = await PersistentVolumeClaim.list(
        kube=kube,
        namespaces=(BERTRAND_NAMESPACE,),
        timeout=timeout,
        labels=labels,
    )
    out: list[PersistentVolumeClaim] = []
    for pvc in pvcs:
        labels = pvc.labels
        repo_id = labels.get(REPO_ID_ENV, "")
        if not repo_id:
            msg = f"cluster PVC {pvc.name!r} is missing label {REPO_ID_ENV!r}"
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

    out.sort(key=lambda pvc: (pvc.labels.get(REPO_ID_ENV, ""), pvc.name))
    return out


async def delete_repository_volume_claim(
    kube: Kube,
    *,
    pvc: PersistentVolumeClaim,
    timeout: float,
    force: bool,
) -> None:
    """Delete a repository volume claim from the cluster.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    pvc : PersistentVolumeClaim
        Managed repository PVC to delete.
    timeout : float
        Maximum deletion budget in seconds.
    force : bool
        Whether to skip the active-pod safety check.

    Raises
    ------
    OSError
        If the PVC is still in use by active pods.
    """
    deadline = Deadline.from_timeout(
        timeout,
        message="repository volume deletion timeout must be non-negative",
    )
    repo_id = _check_uuid(pvc.labels.get(REPO_ID_ENV, ""))
    if not force:
        pods = await Pod.list(
            kube=kube,
            namespaces=(BERTRAND_NAMESPACE,),
            timeout=deadline.remaining(),
            labels={BERTRAND_ENV: "1", REPO_ID_ENV: repo_id},
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

    await pvc.delete(kube=kube, timeout=deadline.remaining())


async def resolve_repository_volume_ceph_path(
    kube: Kube,
    *,
    pvc: PersistentVolumeClaim,
    timeout: float,
) -> PosixPath:
    """Resolve a repo claim's CephFS path from bound PVC/PV metadata.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    pvc : PersistentVolumeClaim
        Managed repository PVC to inspect.
    timeout : float
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
    deadline = Deadline.from_timeout(
        timeout,
        message="repository volume Ceph path resolution timeout must be non-negative",
    )
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
    pvc = await pvc.wait_bound(kube=kube, timeout=deadline.remaining())
    volume_name = pvc.volume_name
    volume = await PersistentVolume.wait_present(
        kube=kube,
        timeout=deadline.remaining(),
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
    claim_name: _NonEmptyString
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

    @field_validator("phase")
    @classmethod
    def _normalize_phase(cls, value: _RepositoryVolumePhase) -> _RepositoryVolumePhase:
        if value == "Active":
            return "Ready"
        return value

    @property
    def labels(self) -> dict[str, str]:
        return {
            **_REPOSITORY_VOLUME_LABELS,
            REPO_ID_ENV: self.repo_id,
            REPOSITORY_VOLUME_PHASE_LABEL: self.phase.lower(),
        }


class CephRepositoryMount(BaseModel):
    """Repository mount lifecycle entry embedded in `CephRepositoryState`."""

    model_config = ConfigDict(extra="forbid", frozen=True)
    name: _NonEmptyString
    repo_id: _NonEmptyString
    claim_name: _NonEmptyString
    alias_path: _NonEmptyString
    alias_path_hash: _NonEmptyString
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
    def labels(self) -> dict[str, str]:
        """Return synthetic lifecycle labels for local filtering.

        Returns
        -------
        dict[str, str]
            Labels derived from the mount identity and phase.
        """
        return {
            **_REPOSITORY_MOUNT_LABELS,
            REPO_ID_ENV: self.repo_id,
            REPOSITORY_MOUNT_PATH_HASH_LABEL: self.alias_path_hash,
            REPOSITORY_MOUNT_HOST_HASH_LABEL: repository_mount_host_hash(self.host_id),
            REPOSITORY_MOUNT_PHASE_LABEL: self.phase.lower(),
        }

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
    def labels(self) -> dict[str, str]:
        """Return synthetic lifecycle labels for local filtering.

        Returns
        -------
        dict[str, str]
            Labels derived from the worktree identity and phase.
        """
        return {
            **_REPOSITORY_WORKTREE_LABELS,
            REPO_ID_ENV: self.repo_id,
            REPOSITORY_WORKTREE_ID_HASH_LABEL: repository_worktree_id_hash(
                self.worktree_id
            ),
            REPOSITORY_WORKTREE_PHASE_LABEL: self.phase.lower(),
        }

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
        expected_claim = repo_volume_claim_name(repo_id)
        if record.spec.claim_name != expected_claim:
            msg = (
                f"malformed {REPOSITORY_STATE_KIND} {record.name!r}: claim_name "
                f"{record.spec.claim_name!r} does not match expected claim "
                f"{expected_claim!r}"
            )
            raise OSError(msg)
        labels = record.metadata.labels
        if labels.get(REPO_ID_ENV) != repo_id:
            msg = (
                f"malformed {REPOSITORY_STATE_KIND} {record.name!r}: repo label "
                f"{REPO_ID_ENV!r} does not match spec repo_id {repo_id!r}"
            )
            raise OSError(msg)
        label_phase = labels.get(REPOSITORY_VOLUME_PHASE_LABEL, "").strip()
        expected_phase = record.spec.phase.lower()
        if expected_phase == "ready" and label_phase == "active":
            label_phase = "ready"
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
    expected_claim = repo_volume_claim_name(repo_id)
    if entry.claim_name != expected_claim:
        msg = (
            f"malformed {REPOSITORY_STATE_KIND} {state.name!r}: mount entry "
            f"{entry.name!r} claim_name {entry.claim_name!r} does not "
            f"match expected claim {expected_claim!r}"
        )
        raise OSError(msg)
    expected_path_hash = repository_mount_path_hash(alias_path)
    if entry.alias_path_hash != expected_path_hash:
        msg = (
            f"malformed {REPOSITORY_STATE_KIND} {state.name!r}: mount entry "
            f"{entry.name!r} alias_path_hash {entry.alias_path_hash!r} "
            f"does not match expected hash {expected_path_hash!r}"
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
            "claim_name",
            "phase",
            "created_at",
            "last_seen_at",
            "last_error",
        ],
    },
    status_model=_RepositoryStateStatus,
)


async def _read_repository_state(
    kube: Kube,
    *,
    repo_id: str,
    deadline: Deadline,
) -> CephRepositoryStateRecord | None:
    return await REPOSITORY_STATE_RESOURCE.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=repo_volume_claim_name(repo_id),
        timeout=deadline.remaining(),
    )


async def _upsert_repository_state(
    kube: Kube,
    *,
    spec: _RepositoryVolumeSpec,
    deadline: Deadline,
) -> CephRepositoryStateRecord:
    return await REPOSITORY_STATE_RESOURCE.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=spec.claim_name,
        spec=spec,
        labels=spec.labels,
        timeout=deadline.remaining(),
    )


async def _patch_repository_state(
    kube: Kube,
    *,
    state: CephRepositoryStateRecord,
    status: _RepositoryStateStatus,
    timeout: float,
) -> CephRepositoryStateRecord:
    return await REPOSITORY_STATE_RESOURCE.patch_status(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=state.name,
        status=status,
        timeout=timeout,
    )


async def _ensure_repository_state_record(
    kube: Kube,
    *,
    repo_id: str,
    deadline: Deadline,
) -> CephRepositoryStateRecord:
    repo_id = _check_uuid(repo_id)
    existing = await _read_repository_state(kube, repo_id=repo_id, deadline=deadline)
    now = datetime.now(UTC)
    created_at = existing.spec.created_at if existing is not None else now
    phase: _RepositoryVolumePhase = "Initializing"
    if existing is not None and existing.spec.phase == "Ready":
        phase = "Ready"
    spec = _RepositoryVolumeSpec(
        repo_id=repo_id,
        claim_name=repo_volume_claim_name(repo_id),
        phase=phase,
        created_at=created_at,
        last_seen_at=now,
        last_gc_at=None,
        last_error="",
    )
    return await _upsert_repository_state(kube, spec=spec, deadline=deadline)


async def ensure_repository_volume_record(
    kube: Kube,
    *,
    repo_id: str,
    timeout: float,
) -> CephRepositoryStateRecord:
    """Mark a managed repository volume as initializing or recently observed.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : str
        Stable repository UUID.
    timeout : float
        Maximum convergence budget in seconds.

    Returns
    -------
    CephRepositoryStateRecord
        Lifecycle record for the repository volume.

    """
    deadline = Deadline.from_timeout(
        timeout,
        message="repository volume lifecycle convergence timeout must be non-negative",
    )
    repo_id = _check_uuid(repo_id)
    await REPOSITORY_STATE_RESOURCE.ensure_crd(
        kube,
        timeout=deadline.remaining(),
    )
    return await _ensure_repository_state_record(
        kube,
        repo_id=repo_id,
        deadline=deadline,
    )


async def mark_repository_volume_ready(
    kube: Kube,
    *,
    repo_id: str,
    timeout: float,
) -> CephRepositoryStateRecord:
    """Mark a repository volume as finalized and safe for normal recovery.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : str
        Stable repository UUID.
    timeout : float
        Maximum convergence budget in seconds.

    Returns
    -------
    CephRepositoryStateRecord
        Ready lifecycle record for the repository volume.

    """
    deadline = Deadline.from_timeout(
        timeout,
        message="repository volume phase update timeout must be non-negative",
    )
    repo_id = _check_uuid(repo_id)
    await REPOSITORY_STATE_RESOURCE.ensure_crd(
        kube,
        timeout=deadline.remaining(),
    )
    existing = await _read_repository_state(kube, repo_id=repo_id, deadline=deadline)
    now = datetime.now(UTC)
    if existing is None:
        spec = _RepositoryVolumeSpec(
            repo_id=repo_id,
            claim_name=repo_volume_claim_name(repo_id),
            phase="Ready",
            created_at=now,
            last_seen_at=now,
            last_error="",
        )
        return await _upsert_repository_state(kube, spec=spec, deadline=deadline)

    spec = existing.spec.model_copy(
        update={
            "phase": "Ready",
            "last_seen_at": now,
            "last_error": "",
        },
    )
    return await _upsert_repository_state(kube, spec=spec, deadline=deadline)


async def mark_repository_volume_failed(
    kube: Kube,
    *,
    repo_id: str,
    last_error: str,
    timeout: float,
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
    timeout : float
        Maximum convergence budget in seconds.

    Returns
    -------
    CephRepositoryStateRecord | None
        Updated failed record, existing ready/retired record, or None if the volume
        has not yet been recorded.
    """
    if timeout <= 0:
        return None
    repo_id = _check_uuid(repo_id)
    deadline = Deadline.from_timeout(
        timeout,
        message="repository volume failed phase update timeout must be non-negative",
    )
    await REPOSITORY_STATE_RESOURCE.ensure_crd(
        kube,
        timeout=deadline.remaining(),
    )
    record = await _read_repository_state(kube, repo_id=repo_id, deadline=deadline)
    if record is None or record.spec.phase in ("Ready", "Retired"):
        return record

    spec = record.spec.model_copy(
        update={
            "phase": "Failed",
            "last_seen_at": datetime.now(UTC),
            "last_error": last_error,
        },
    )
    return await _upsert_repository_state(kube, spec=spec, deadline=deadline)


async def ensure_repository_mount_record(
    kube: Kube,
    *,
    repo_id: str,
    host_id: str,
    alias_path: str,
    node_name: str,
    timeout: float,
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
    timeout : float
        Maximum convergence budget in seconds.

    Returns
    -------
    CephRepositoryMount
        Active lifecycle record for the host alias.

    """
    deadline = Deadline.from_timeout(
        timeout,
        message="repository mount lifecycle convergence timeout must be non-negative",
    )
    repo_id = _check_uuid(repo_id)
    host_id = _check_uuid(host_id)
    alias_path = repository_mount_alias_path(alias_path)
    await REPOSITORY_STATE_RESOURCE.ensure_crd(
        kube,
        timeout=deadline.remaining(),
    )
    state = await _read_repository_state(kube, repo_id=repo_id, deadline=deadline)
    if state is None:
        state = await _ensure_repository_state_record(
            kube,
            repo_id=repo_id,
            deadline=deadline,
        )
    name = repository_mount_name(repo_id, host_id, alias_path)
    existing = state.status.mounts.get(name)
    now = datetime.now(UTC)
    created_at = existing.created_at if existing is not None else now
    entry = CephRepositoryMount(
        name=name,
        repo_id=repo_id,
        claim_name=repo_volume_claim_name(repo_id),
        alias_path=alias_path,
        alias_path_hash=repository_mount_path_hash(alias_path),
        host_id=host_id,
        node_name=node_name.strip(),
        phase="Active",
        created_at=created_at,
        last_seen_at=now,
        retired_at=None,
        last_error="",
    )
    state = await _patch_repository_state(
        kube,
        state=state,
        status=state.status.with_mount(entry),
        timeout=deadline.remaining(),
    )
    return state.status.mounts[name]


async def ensure_repository_worktree_record(
    kube: Kube,
    *,
    repo_id: str,
    worktree_id: str,
    worktree: str,
    timeout: float,
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
    timeout : float
        Maximum convergence budget in seconds.

    Returns
    -------
    CephRepositoryWorktree
        Active lifecycle record for the worktree.

    """
    deadline = Deadline.from_timeout(
        timeout,
        message=(
            "repository worktree lifecycle convergence timeout must be non-negative"
        ),
    )
    repo_id = _check_uuid(repo_id)
    worktree_id = _check_uuid(worktree_id)
    worktree = repository_worktree_path(worktree)
    await REPOSITORY_STATE_RESOURCE.ensure_crd(
        kube,
        timeout=deadline.remaining(),
    )
    state = await _read_repository_state(kube, repo_id=repo_id, deadline=deadline)
    if state is None:
        state = await _ensure_repository_state_record(
            kube,
            repo_id=repo_id,
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
    state = await _patch_repository_state(
        kube,
        state=state,
        status=state.status.with_worktree(entry),
        timeout=deadline.remaining(),
    )
    return state.status.worktrees[name]


async def retire_repository_mount_record(
    kube: Kube,
    *,
    record: CephRepositoryMount,
    timeout: float,
) -> CephRepositoryMount:
    """Retire one repository mount lifecycle record without deleting storage.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    record : CephRepositoryMount
        Mount lifecycle record to retire.
    timeout : float
        Maximum request budget in seconds.

    Returns
    -------
    CephRepositoryMount
        Retired lifecycle record.

    """
    deadline = Deadline.from_timeout(
        timeout,
        message="repository mount retirement timeout must be non-negative",
    )
    repo_id = _check_uuid(record.repo_id)
    await REPOSITORY_STATE_RESOURCE.ensure_crd(
        kube,
        timeout=deadline.remaining(),
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
    state = await _read_repository_state(kube, repo_id=repo_id, deadline=deadline)
    if state is None:
        state = await _ensure_repository_state_record(
            kube,
            repo_id=repo_id,
            deadline=deadline,
        )
    state = await _patch_repository_state(
        kube,
        state=state,
        status=state.status.with_mount(entry),
        timeout=deadline.remaining(),
    )
    return state.status.mounts[record.name]


async def retire_repository_mount(
    kube: Kube,
    *,
    repo_id: str,
    host_id: str,
    alias_path: str,
    timeout: float,
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
    timeout : float
        Maximum request budget in seconds.

    Returns
    -------
    CephRepositoryMount | None
        Retired lifecycle record, or None if the record does not exist.

    """
    deadline = Deadline.from_timeout(
        timeout,
        message="repository mount retirement timeout must be non-negative",
    )
    repo_id = _check_uuid(repo_id)
    host_id = _check_uuid(host_id)
    alias_path = repository_mount_alias_path(alias_path)
    await REPOSITORY_STATE_RESOURCE.ensure_crd(
        kube,
        timeout=deadline.remaining(),
    )
    state = await _read_repository_state(kube, repo_id=repo_id, deadline=deadline)
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
    state = await _patch_repository_state(
        kube,
        state=state,
        status=state.status.with_mount(entry),
        timeout=deadline.remaining(),
    )
    return state.status.mounts[record.name]


async def delete_repository_snapshot_artifacts(
    kube: Kube,
    *,
    repo_id: str,
    timeout: float,
) -> None:
    """Delete all snapshot resources associated with one repository.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : str
        Stable repository UUID.
    timeout : float
        Maximum deletion budget in seconds.

    """
    deadline = Deadline.from_timeout(
        timeout,
        message="repository snapshot artifact deletion timeout must be non-negative",
    )
    repo_id = _check_uuid(repo_id)
    pvcs = await PersistentVolumeClaim.list(
        kube,
        namespaces=(BERTRAND_NAMESPACE,),
        labels={
            BERTRAND_ENV: "1",
            REPO_ID_ENV: repo_id,
            REPOSITORY_BUILD_SOURCE_LABEL: REPOSITORY_BUILD_SOURCE_LABEL_VALUE,
        },
        timeout=deadline.remaining(),
    )
    for pvc in sorted(pvcs, key=lambda item: item.name):
        await pvc.delete(kube, timeout=deadline.remaining())
        await pvc.wait_deleted(kube, timeout=deadline.remaining())

    snapshots = await VOLUME_SNAPSHOT_RESOURCE.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        labels={
            BERTRAND_ENV: "1",
            REPO_ID_ENV: repo_id,
            REPOSITORY_SNAPSHOT_LABEL: REPOSITORY_SNAPSHOT_LABEL_VALUE,
        },
        timeout=deadline.remaining(),
    )
    for snapshot in sorted(snapshots, key=lambda item: item.name):
        await delete_volume_snapshot(kube, snapshot, timeout=deadline.remaining())


async def gc_repository_volumes(
    kube: Kube,
    *,
    timeout: float,
    grace_seconds: int = REPOSITORY_VOLUME_GC_GRACE_SECONDS,
    limit: int = REPOSITORY_VOLUME_GC_LIMIT,
) -> list[CephRepositoryStateRecord]:
    """Delete eligible retired repository volumes and lifecycle records.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
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
    deadline = Deadline.from_timeout(
        timeout,
        message="repository volume GC timeout must be non-negative",
    )
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
        timeout=deadline.remaining(),
    )
    records = await REPOSITORY_STATE_RESOURCE.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        labels={REPOSITORY_VOLUME_PHASE_LABEL: "retired"},
        timeout=deadline.remaining(),
    )
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
            if await has_active_buildkit_builds(
                kube,
                repo_id=record.spec.repo_id,
                timeout=deadline.remaining(),
            ):
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
                blocker = await _repository_workload_resource_blocker(
                    kube,
                    repo_id=record.spec.repo_id,
                    timeout=deadline.remaining(),
                )

            if blocker is None:
                blocker = await _repository_image_record_blocker(
                    kube,
                    repo_id=record.spec.repo_id,
                    timeout=deadline.remaining(),
                )

            if blocker is None:
                pods = await Pod.list(
                    kube,
                    namespaces=(BERTRAND_NAMESPACE,),
                    timeout=deadline.remaining(),
                )
                active_pods = [
                    pod
                    for pod in pods
                    if (
                        not pod.is_terminal
                        and record.spec.claim_name in pod.persistent_volume_claim_names
                    )
                ]
                if active_pods:
                    names = ", ".join(pod.name for pod in active_pods[:3])
                    blocker = (
                        "active Pods still reference repository PVC "
                        f"{record.spec.claim_name}: {names}"
                    )

            if blocker is None and await _has_retained_repository_snapshots(
                kube,
                repo_id=record.spec.repo_id,
                timeout=deadline.remaining(),
            ):
                blocker = "retained repository snapshots still exist"

            if blocker is not None:
                spec = record.spec.model_copy(
                    update={
                        "phase": "Retired",
                        "last_gc_at": now,
                        "last_error": blocker,
                    },
                )
                await _upsert_repository_state(kube, spec=spec, deadline=deadline)
                continue

            await delete_repository_snapshot_artifacts(
                kube,
                repo_id=record.spec.repo_id,
                timeout=deadline.remaining(),
            )
            volumes = await list_repository_volume_claims(
                kube,
                record.spec.repo_id,
                timeout=deadline.remaining(),
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
                repo_id = pvc.labels.get(REPO_ID_ENV, "")
                if repo_id != record.spec.repo_id or pvc.name != record.spec.claim_name:
                    collection_error = (
                        f"{REPOSITORY_STATE_KIND} {record.name!r} points to "
                        f"{record.spec.repo_id}/{record.spec.claim_name}, but "
                        f"discovered PVC {repo_id}/{pvc.name}"
                    )
                else:
                    await delete_repository_volume_claim(
                        kube,
                        pvc=pvc,
                        timeout=deadline.remaining(),
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
                await _upsert_repository_state(kube, spec=spec, deadline=deadline)
                continue

            credentials = await RepoCredentials.get(
                record.spec.repo_id,
                timeout=deadline.remaining(),
            )
            if credentials is not None:
                await credentials.delete(timeout=deadline.remaining())

            await _delete_repository_parented_records(
                kube,
                record=record,
                timeout=deadline.remaining(),
            )
            await REPOSITORY_STATE_RESOURCE.delete_by_name(
                kube,
                namespace=BERTRAND_NAMESPACE,
                name=record.name,
                timeout=deadline.remaining(),
            )
        except (OSError, TimeoutError, ValueError) as err:
            spec = record.spec.model_copy(
                update={
                    "phase": "Retired",
                    "last_gc_at": now,
                    "last_error": str(err),
                },
            )
            await _upsert_repository_state(kube, spec=spec, deadline=deadline)
            continue
        collected.append(record)
    return collected


async def next_repository_volume_gc_time(
    kube: Kube,
    *,
    timeout: float,
    grace_seconds: int = REPOSITORY_VOLUME_GC_GRACE_SECONDS,
) -> datetime | None:
    """Return the next time retired repository volumes may be GC-eligible.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
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
    deadline = Deadline.from_timeout(
        timeout,
        message="repository volume GC scheduling timeout must be non-negative",
    )
    if grace_seconds < 0:
        msg = "repository volume GC grace_seconds must be non-negative"
        raise ValueError(msg)
    await REPOSITORY_STATE_RESOURCE.ensure_crd(kube, timeout=deadline.remaining())
    records = await REPOSITORY_STATE_RESOURCE.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        labels={REPOSITORY_VOLUME_PHASE_LABEL: "retired"},
        timeout=deadline.remaining(),
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


async def _repository_workload_resource_blocker(
    kube: Kube,
    *,
    repo_id: str,
    timeout: float,
) -> str | None:
    repo_id = _check_uuid(repo_id)
    labels = {
        WORKLOAD_LABEL: WORKLOAD_LABEL_VALUE,
        WORKLOAD_REPO_LABEL: hashlib.sha256(repo_id.encode()).hexdigest()[:16],
    }
    deadline = Deadline.from_timeout(
        timeout,
        message="repository workload blocker scan timeout must be non-negative",
    )
    deployments = await Deployment.list(
        kube,
        namespaces=(BERTRAND_NAMESPACE,),
        labels=labels,
        timeout=deadline.remaining(),
    )
    if deployments:
        names = ", ".join(item.name for item in deployments[:3])
        return f"managed Deployments still reference repository {repo_id}: {names}"
    cronjobs = await CronJob.list(
        kube,
        namespaces=(BERTRAND_NAMESPACE,),
        labels=labels,
        timeout=deadline.remaining(),
    )
    if cronjobs:
        names = ", ".join(item.name for item in cronjobs[:3])
        return f"managed CronJobs still reference repository {repo_id}: {names}"
    jobs = await Job.list(
        kube,
        namespaces=(BERTRAND_NAMESPACE,),
        labels=labels,
        timeout=deadline.remaining(),
    )
    active_jobs = [job for job in jobs if not job.is_complete and not job.is_failed]
    if active_jobs:
        names = ", ".join(item.name for item in active_jobs[:3])
        return f"active Jobs still reference repository {repo_id}: {names}"
    return None


async def _repository_image_record_blocker(
    kube: Kube,
    *,
    repo_id: str,
    timeout: float,
) -> str | None:
    repo_id = _check_uuid(repo_id)
    try:
        builds = await BUILDKIT_BUILD_RESOURCE.list(
            kube,
            labels={
                BUILDKIT_BUILD_REPO_LABEL: buildkit_build_label_hash(repo_id),
                BUILDKIT_BUILD_IMAGE_PHASE_LABEL: "active",
            },
            timeout=timeout,
        )
    except OSError as err:
        if _is_missing_api_resource(err):
            builds = []
        else:
            raise
    active = [
        build
        for build in builds
        if build.spec.repo_id == repo_id and build.image_phase == "Active"
    ]
    if active:
        names = ", ".join(build.name for build in active[:3])
        return (
            "active project image records still reference repository "
            f"{repo_id}: {names}"
        )
    return None


async def _delete_repository_parented_records(
    kube: Kube,
    *,
    record: CephRepositoryStateRecord,
    timeout: float,
) -> None:
    deadline = Deadline.from_timeout(
        timeout,
        message="repository parented record cleanup timeout must be non-negative",
    )
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
            timeout=deadline.remaining(),
        )
    await delete_capabilities_for_scope(
        kube,
        scope="repository",
        scope_value=record.spec.repo_id,
        timeout=deadline.remaining(),
    )


async def _has_retained_repository_snapshots(
    kube: Kube,
    *,
    repo_id: str,
    timeout: float,
) -> bool:
    snapshots = await VOLUME_SNAPSHOT_RESOURCE.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        labels={
            BERTRAND_ENV: "1",
            REPO_ID_ENV: _check_uuid(repo_id),
            REPOSITORY_SNAPSHOT_LABEL: REPOSITORY_SNAPSHOT_LABEL_VALUE,
            REPOSITORY_SNAPSHOT_PURPOSE_LABEL: REPOSITORY_SNAPSHOT_PURPOSE_RETAINED,
        },
        timeout=timeout,
    )
    return bool(snapshots)
