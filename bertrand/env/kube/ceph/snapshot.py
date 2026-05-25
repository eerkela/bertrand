"""Ceph repository snapshot lifecycle and build-source helpers."""

from __future__ import annotations

import hashlib
import uuid
from contextlib import asynccontextmanager, suppress
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta
from typing import TYPE_CHECKING

from bertrand.env.config.core import _check_uuid
from bertrand.env.git import BERTRAND_ENV, BERTRAND_NAMESPACE, REPO_ID_ENV, Deadline
from bertrand.env.kube.api._helpers import _is_conflict
from bertrand.env.kube.ceph.refs import (
    CEPHFS_STORAGE_CLASS_PREFERENCES,
    REPOSITORY_BUILD_SOURCE_LABEL,
    REPOSITORY_BUILD_SOURCE_LABEL_VALUE,
    REPOSITORY_SNAPSHOT_LABEL,
    REPOSITORY_SNAPSHOT_LABEL_VALUE,
    REPOSITORY_SNAPSHOT_PURPOSE_BUILD,
    REPOSITORY_SNAPSHOT_PURPOSE_LABEL,
    REPOSITORY_SNAPSHOT_PURPOSE_RETAINED,
)
from bertrand.env.kube.snapshot import VolumeSnapshot, VolumeSnapshotClass
from bertrand.env.kube.volume import PersistentVolumeClaim, StorageClass

from .volume import (
    REPO_VOLUME_ENV,
    CephRepositoryVolumeRecord,
    RepoVolume,
    list_repository_volume_records,
)

if TYPE_CHECKING:
    from collections.abc import AsyncIterator, Collection, Mapping

    from bertrand.env.kube.api.client import Kube

REPOSITORY_SNAPSHOT_CLASS_LABEL = "bertrand.dev/ceph-repository-snapshot-class"
REPOSITORY_BUILD_REQUEST_LABEL = "bertrand.dev/buildkit-build-name"
REPOSITORY_SNAPSHOT_SOURCE_CLAIM_ANNOTATION = (
    "bertrand.dev/ceph-repository-source-claim"
)
REPOSITORY_BUILD_SOURCE_SNAPSHOT_ANNOTATION = (
    "bertrand.dev/ceph-repository-build-snapshot"
)
REPOSITORY_SNAPSHOT_INTERVAL_SECONDS = 86_400
REPOSITORY_SNAPSHOT_RETENTION_SECONDS = 1_209_600
REPOSITORY_SNAPSHOT_CREATE_LIMIT = 4
REPOSITORY_SNAPSHOT_DELETE_LIMIT = 8
REPOSITORY_BUILD_SOURCE_GC_LIMIT = 8
REPOSITORY_BUILD_SOURCE_MAX_AGE_SECONDS = 86_400
REPOSITORY_BUILD_SOURCE_CLEANUP_TIMEOUT_SECONDS = 30.0


@dataclass(frozen=True)
class _RepositorySnapshotInventory:
    retained: list[VolumeSnapshot]
    active_records: list[CephRepositoryVolumeRecord]
    by_repo: dict[str, list[VolumeSnapshot]]


async def ensure_repository_snapshot_support(
    kube: Kube,
    *,
    timeout: float,
) -> VolumeSnapshotClass:
    """Ensure repository snapshot primitives are usable.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum convergence budget in seconds.

    Returns
    -------
    VolumeSnapshotClass
        CephFS-compatible snapshot class selected for repository snapshots.

    Raises
    ------
    OSError
        If CephFS storage or snapshot support is unavailable.
    TimeoutError
        If `timeout` is non-positive or convergence exceeds the budget.
    """
    if timeout <= 0:
        msg = "repository snapshot support timeout must be non-negative"
        raise TimeoutError(msg)
    deadline = Deadline.from_timeout(timeout, message="timeout must be non-negative")
    storage = await StorageClass.select(
        kube,
        timeout=deadline.remaining(),
        preferences=CEPHFS_STORAGE_CLASS_PREFERENCES,
        require_expansion=True,
    )
    if not storage.is_cephfs:
        msg = (
            f"storage class {storage.name!r} uses provisioner "
            f"{storage.provisioner!r}, but repository snapshots require CephFS CSI"
        )
        raise OSError(msg)
    return await _ensure_snapshot_class(
        kube,
        storage=storage,
        timeout=deadline.remaining(),
    )


async def create_repository_snapshot(
    kube: Kube,
    *,
    repo_id: str,
    timeout: float,
) -> VolumeSnapshot:
    """Create one retained snapshot for a managed repository volume.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : str
        Stable repository UUID.
    timeout : float
        Maximum snapshot budget in seconds.

    Returns
    -------
    VolumeSnapshot
        Ready retained snapshot.
    """
    repo_id = _check_uuid(repo_id)
    return await _create_snapshot(
        kube,
        repo_id=repo_id,
        purpose=REPOSITORY_SNAPSHOT_PURPOSE_RETAINED,
        build_name=None,
        timeout=timeout,
    )


async def maintain_repository_snapshots(
    kube: Kube,
    *,
    timeout: float,
    interval_seconds: int = REPOSITORY_SNAPSHOT_INTERVAL_SECONDS,
    retention_seconds: int = REPOSITORY_SNAPSHOT_RETENTION_SECONDS,
    create_limit: int = REPOSITORY_SNAPSHOT_CREATE_LIMIT,
    delete_limit: int = REPOSITORY_SNAPSHOT_DELETE_LIMIT,
) -> None:
    """Create recent retained snapshots and delete expired retained snapshots.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum maintenance budget in seconds.
    interval_seconds : int, optional
        Minimum cadence between retained snapshots for each active repository.
    retention_seconds : int, optional
        Maximum retained snapshot age.
    create_limit : int, optional
        Maximum retained snapshots to create in this pass.
    delete_limit : int, optional
        Maximum expired snapshots to delete in this pass.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    ValueError
        If cadence or limit inputs are invalid.
    """
    if timeout <= 0:
        msg = "repository snapshot maintenance timeout must be non-negative"
        raise TimeoutError(msg)
    if interval_seconds <= 0 or retention_seconds <= 0:
        msg = "repository snapshot interval and retention must be positive"
        raise ValueError(msg)
    if create_limit < 0 or delete_limit < 0:
        msg = "repository snapshot maintenance limits must be non-negative"
        raise ValueError(msg)
    deadline = Deadline.from_timeout(timeout, message="timeout must be non-negative")
    await ensure_repository_snapshot_support(kube, timeout=deadline.remaining())

    now = datetime.now(UTC)
    inventory = await _snapshot_inventory(
        kube,
        timeout=deadline.remaining(),
    )
    deleted = await _delete_expired_retained_snapshots(
        kube,
        snapshots=inventory.retained,
        now=now,
        retention=timedelta(seconds=retention_seconds),
        limit=delete_limit,
        timeout=deadline.remaining(),
    )
    if deleted:
        inventory = await _snapshot_inventory(
            kube,
            timeout=deadline.remaining(),
        )

    created = 0
    fresh_boundary = now - timedelta(seconds=interval_seconds)
    for record in sorted(inventory.active_records, key=lambda item: item.repo_id):
        if created >= create_limit:
            break
        repo_snapshots = inventory.by_repo.get(record.repo_id, [])
        if any(
            snapshot.ready_to_use
            and snapshot.created_at is not None
            and snapshot.created_at >= fresh_boundary
            for snapshot in repo_snapshots
        ):
            continue
        await create_repository_snapshot(
            kube,
            repo_id=record.repo_id,
            timeout=deadline.remaining(),
        )
        created += 1


async def next_repository_snapshot_time(
    kube: Kube,
    *,
    timeout: float,
    interval_seconds: int = REPOSITORY_SNAPSHOT_INTERVAL_SECONDS,
    retention_seconds: int = REPOSITORY_SNAPSHOT_RETENTION_SECONDS,
) -> datetime | None:
    """Return the next time repository snapshot maintenance may be useful.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum request budget in seconds.
    interval_seconds : int, optional
        Minimum retained snapshot cadence.
    retention_seconds : int, optional
        Maximum retained snapshot age.

    Returns
    -------
    datetime | None
        Earliest useful maintenance time, or `None` when no active repository or
        retained snapshot needs maintenance.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    ValueError
        If cadence inputs are invalid.
    """
    if timeout <= 0:
        msg = "repository snapshot scheduling timeout must be non-negative"
        raise TimeoutError(msg)
    if interval_seconds <= 0 or retention_seconds <= 0:
        msg = "repository snapshot interval and retention must be positive"
        raise ValueError(msg)
    deadline = Deadline.from_timeout(timeout, message="timeout must be non-negative")
    now = datetime.now(UTC)
    inventory = await _snapshot_inventory(
        kube,
        timeout=deadline.remaining(),
    )
    retention = timedelta(seconds=retention_seconds)
    if any(
        snapshot.created_at is not None and now - snapshot.created_at >= retention
        for snapshot in inventory.retained
    ):
        return now
    if not inventory.active_records:
        return None

    boundaries: list[datetime] = []
    interval = timedelta(seconds=interval_seconds)
    for record in inventory.active_records:
        ready = [
            snapshot.created_at
            for snapshot in inventory.by_repo.get(record.repo_id, [])
            if snapshot.ready_to_use and snapshot.created_at is not None
        ]
        if not ready:
            return now
        boundaries.append(max(ready) + interval)
    return min(boundaries) if boundaries else now


@asynccontextmanager
async def prepared_repository_build_source(
    kube: Kube,
    *,
    repo_id: str,
    build_name: str,
    timeout: float,
) -> AsyncIterator[str]:
    """Prepare a snapshot-restored PVC source for one BuildKit request.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : str
        Stable repository UUID.
    build_name : str
        Durable `BuildKitBuild` request name.
    timeout : float
        Maximum preparation budget in seconds.

    Yields
    ------
    str
        Temporary read-only source claim name.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    ValueError
        If `build_name` is empty.
    """
    if timeout <= 0:
        msg = "repository build snapshot preparation timeout must be non-negative"
        raise TimeoutError(msg)
    repo_id = _check_uuid(repo_id)
    build_name = build_name.strip()
    if not build_name:
        msg = "repository build snapshot requires a non-empty build name"
        raise ValueError(msg)
    deadline = Deadline.from_timeout(timeout, message="timeout must be non-negative")
    snapshot: VolumeSnapshot | None = None
    pvc: PersistentVolumeClaim | None = None
    try:
        snapshot = await _create_snapshot(
            kube,
            repo_id=repo_id,
            purpose=REPOSITORY_SNAPSHOT_PURPOSE_BUILD,
            build_name=build_name,
            timeout=deadline.remaining(),
        )
        volume = await _repository_volume(
            kube,
            repo_id=repo_id,
            timeout=deadline.remaining(),
        )
        pvc = await PersistentVolumeClaim.create_from_snapshot(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=_build_source_claim_name(repo_id=repo_id, build_name=build_name),
            access_modes=volume.pvc.access_modes or ("ReadWriteMany",),
            storage_class=volume.pvc.storage_class_name,
            storage_request=volume.pvc.requested_storage,
            snapshot_name=snapshot.name,
            labels=_build_source_labels(repo_id=repo_id, build_name=build_name),
            annotations={
                REPOSITORY_BUILD_SOURCE_SNAPSHOT_ANNOTATION: snapshot.name,
            },
            timeout=deadline.remaining(),
        )
        pvc = await pvc.wait_bound(kube, timeout=deadline.remaining())
        yield pvc.name
    finally:
        await _cleanup_build_source(kube, pvc=pvc, snapshot=snapshot)


async def cleanup_orphaned_build_sources(
    kube: Kube,
    *,
    active_build_names: Collection[str],
    timeout: float,
    max_age_seconds: int = REPOSITORY_BUILD_SOURCE_MAX_AGE_SECONDS,
    limit: int = REPOSITORY_BUILD_SOURCE_GC_LIMIT,
) -> int:
    """Delete old build-purpose snapshot sources for inactive build requests.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    active_build_names : Collection[str]
        Build request names that must be preserved.
    timeout : float
        Maximum cleanup budget in seconds.
    max_age_seconds : int, optional
        Minimum age before inactive build sources are collected.
    limit : int, optional
        Maximum number of PVCs and snapshots to delete in this pass.

    Returns
    -------
    int
        Number of objects selected for deletion.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    ValueError
        If cleanup age or limit inputs are invalid.
    """
    if timeout <= 0:
        msg = "repository build-source cleanup timeout must be non-negative"
        raise TimeoutError(msg)
    if max_age_seconds < 0 or limit < 0:
        msg = "repository build-source cleanup age and limit must be non-negative"
        raise ValueError(msg)
    if limit == 0:
        return 0

    active = {name.strip() for name in active_build_names if name and name.strip()}
    now = datetime.now(UTC)
    max_age = timedelta(seconds=max_age_seconds)
    deadline = Deadline.from_timeout(timeout, message="timeout must be non-negative")
    deleted = 0

    pvcs = await PersistentVolumeClaim.list(
        kube,
        namespaces=(BERTRAND_NAMESPACE,),
        labels={
            BERTRAND_ENV: "1",
            REPOSITORY_BUILD_SOURCE_LABEL: REPOSITORY_BUILD_SOURCE_LABEL_VALUE,
        },
        timeout=deadline.remaining(),
    )
    for pvc in sorted(pvcs, key=lambda item: item.name):
        if deleted >= limit:
            break
        if not _build_source_orphaned(
            labels=pvc.labels,
            created_at=pvc.created_at,
            active_build_names=active,
            now=now,
            max_age=max_age,
        ):
            continue
        await pvc.delete(kube, timeout=deadline.remaining())
        deleted += 1

    snapshots = await VolumeSnapshot.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        labels=_snapshot_labels(purpose=REPOSITORY_SNAPSHOT_PURPOSE_BUILD),
        timeout=deadline.remaining(),
    )
    for snapshot in sorted(snapshots, key=lambda item: item.name):
        if deleted >= limit:
            break
        if not _build_source_orphaned(
            labels=snapshot.labels,
            created_at=snapshot.created_at,
            active_build_names=active,
            now=now,
            max_age=max_age,
        ):
            continue
        await snapshot.delete(kube, timeout=deadline.remaining())
        deleted += 1

    return deleted


async def _ensure_snapshot_class(
    kube: Kube,
    *,
    storage: StorageClass,
    timeout: float,
) -> VolumeSnapshotClass:
    if timeout <= 0:
        msg = "repository snapshot class timeout must be non-negative"
        raise TimeoutError(msg)
    deadline = Deadline.from_timeout(timeout, message="timeout must be non-negative")
    classes = await VolumeSnapshotClass.list(kube, timeout=deadline.remaining())
    matches = [
        item
        for item in classes
        if item.driver == storage.provisioner and item.deletion_policy == "Delete"
    ]
    if matches:
        return sorted(matches, key=lambda item: item.name)[0]

    name = _snapshot_class_name(storage.provisioner)
    existing = await VolumeSnapshotClass.get(
        kube,
        name=name,
        timeout=deadline.remaining(),
    )
    if existing is not None:
        _assert_snapshot_class(existing, driver=storage.provisioner)
        return existing
    try:
        return await VolumeSnapshotClass.create(
            kube,
            name=name,
            driver=storage.provisioner,
            deletion_policy="Delete",
            parameters=_snapshot_class_parameters(storage.parameters),
            labels={
                BERTRAND_ENV: "1",
                REPOSITORY_SNAPSHOT_CLASS_LABEL: "v1",
            },
            timeout=deadline.remaining(),
        )
    except OSError as err:
        if not _is_conflict(err):
            raise
    existing = await VolumeSnapshotClass.get(
        kube,
        name=name,
        timeout=deadline.remaining(),
    )
    if existing is None:
        msg = f"VolumeSnapshotClass {name!r} disappeared during convergence"
        raise OSError(msg)
    _assert_snapshot_class(existing, driver=storage.provisioner)
    return existing


async def _create_snapshot(
    kube: Kube,
    *,
    repo_id: str,
    purpose: str,
    build_name: str | None,
    timeout: float,
) -> VolumeSnapshot:
    deadline = Deadline.from_timeout(timeout, message="timeout must be non-negative")
    volume = await _repository_volume(
        kube,
        repo_id=repo_id,
        timeout=deadline.remaining(),
    )
    await volume.pvc.wait_bound(kube, timeout=deadline.remaining())
    snapshot_class = await ensure_repository_snapshot_support(
        kube,
        timeout=deadline.remaining(),
    )
    snapshot = await VolumeSnapshot.create(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=_snapshot_name(repo_id=repo_id, purpose=purpose, build_name=build_name),
        source_claim=volume.pvc.name,
        snapshot_class=snapshot_class.name,
        labels=_snapshot_labels(
            repo_id=repo_id,
            purpose=purpose,
            build_name=build_name,
        ),
        annotations={
            REPOSITORY_SNAPSHOT_SOURCE_CLAIM_ANNOTATION: volume.pvc.name,
        },
        timeout=deadline.remaining(),
    )
    return await snapshot.wait_ready(kube, timeout=deadline.remaining())


async def _repository_volume(
    kube: Kube,
    *,
    repo_id: str,
    timeout: float,
) -> RepoVolume:
    volumes = await RepoVolume.list(kube, repo_id, timeout=timeout)
    if len(volumes) != 1:
        msg = (
            f"repository snapshot requires one managed repository PVC for "
            f"{repo_id!r}, found {len(volumes)}"
        )
        raise OSError(msg)
    return volumes[0]


async def _retained_snapshots(kube: Kube, *, timeout: float) -> list[VolumeSnapshot]:
    return await VolumeSnapshot.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        labels=_snapshot_labels(purpose=REPOSITORY_SNAPSHOT_PURPOSE_RETAINED),
        timeout=timeout,
    )


async def _snapshot_inventory(
    kube: Kube,
    *,
    timeout: float,
) -> _RepositorySnapshotInventory:
    deadline = Deadline.from_timeout(timeout, message="timeout must be non-negative")
    retained = await _retained_snapshots(kube, timeout=deadline.remaining())
    active_records = [
        record
        for record in await list_repository_volume_records(
            kube,
            timeout=deadline.remaining(),
        )
        if record.phase == "Ready"
    ]
    by_repo: dict[str, list[VolumeSnapshot]] = {}
    for snapshot in retained:
        repo_id = snapshot.labels.get(REPO_ID_ENV, "")
        if repo_id:
            by_repo.setdefault(repo_id, []).append(snapshot)
    return _RepositorySnapshotInventory(
        retained=retained,
        active_records=active_records,
        by_repo=by_repo,
    )


async def _delete_expired_retained_snapshots(
    kube: Kube,
    *,
    snapshots: Collection[VolumeSnapshot],
    now: datetime,
    retention: timedelta,
    limit: int,
    timeout: float,
) -> int:
    deadline = Deadline.from_timeout(timeout, message="timeout must be non-negative")
    deleted = 0
    for snapshot in sorted(snapshots, key=lambda item: item.name):
        if deleted >= limit:
            break
        created_at = snapshot.created_at
        if created_at is None or now - created_at < retention:
            continue
        await snapshot.delete(kube, timeout=deadline.remaining())
        deleted += 1
    return deleted


async def _cleanup_build_source(
    kube: Kube,
    *,
    pvc: PersistentVolumeClaim | None,
    snapshot: VolumeSnapshot | None,
) -> None:
    deadline = Deadline.from_timeout(
        REPOSITORY_BUILD_SOURCE_CLEANUP_TIMEOUT_SECONDS,
        message="repository build-source cleanup timeout must be non-negative",
    )
    if pvc is not None:
        with suppress(OSError, TimeoutError, ValueError):
            await pvc.delete(kube, timeout=deadline.remaining())
            await pvc.wait_deleted(kube, timeout=deadline.remaining())
    if snapshot is not None:
        with suppress(OSError, TimeoutError, ValueError):
            await snapshot.delete(kube, timeout=deadline.remaining())


def _snapshot_labels(
    *,
    purpose: str,
    repo_id: str | None = None,
    build_name: str | None = None,
) -> dict[str, str]:
    labels = {
        BERTRAND_ENV: "1",
        REPO_VOLUME_ENV: "1",
        REPOSITORY_SNAPSHOT_LABEL: REPOSITORY_SNAPSHOT_LABEL_VALUE,
        REPOSITORY_SNAPSHOT_PURPOSE_LABEL: purpose,
    }
    if repo_id is not None:
        labels[REPO_ID_ENV] = _check_uuid(repo_id)
    if build_name is not None:
        labels[REPOSITORY_BUILD_REQUEST_LABEL] = build_name
    return labels


def _build_source_labels(*, repo_id: str, build_name: str) -> dict[str, str]:
    return {
        BERTRAND_ENV: "1",
        REPO_VOLUME_ENV: "1",
        REPO_ID_ENV: _check_uuid(repo_id),
        REPOSITORY_BUILD_SOURCE_LABEL: REPOSITORY_BUILD_SOURCE_LABEL_VALUE,
        REPOSITORY_BUILD_REQUEST_LABEL: build_name,
    }


def _snapshot_name(
    *,
    repo_id: str,
    purpose: str,
    build_name: str | None,
) -> str:
    timestamp = datetime.now(UTC).strftime("%Y%m%d%H%M%S")
    if purpose == REPOSITORY_SNAPSHOT_PURPOSE_BUILD and build_name:
        digest = hashlib.sha256(f"{repo_id}:{build_name}".encode()).hexdigest()[:16]
        return f"bertrand-build-snap-{digest}-{uuid.uuid4().hex[:8]}"
    digest = hashlib.sha256(repo_id.encode()).hexdigest()[:16]
    return f"bertrand-repo-snap-{digest}-{timestamp}-{uuid.uuid4().hex[:8]}"


def _build_source_claim_name(*, repo_id: str, build_name: str) -> str:
    digest = hashlib.sha256(f"{repo_id}:{build_name}".encode()).hexdigest()[:16]
    return f"bertrand-build-src-{digest}-{uuid.uuid4().hex[:8]}"


def _snapshot_class_name(driver: str) -> str:
    digest = hashlib.sha256(driver.encode()).hexdigest()[:12]
    return f"bertrand-cephfs-snap-{digest}"


def _snapshot_class_parameters(parameters: Mapping[str, str]) -> dict[str, str]:
    out: dict[str, str] = {}
    cluster_id = parameters.get("clusterID")
    if cluster_id:
        out["clusterID"] = cluster_id
    for source, target in (
        (
            "csi.storage.k8s.io/provisioner-secret-name",
            "csi.storage.k8s.io/snapshotter-secret-name",
        ),
        (
            "csi.storage.k8s.io/provisioner-secret-namespace",
            "csi.storage.k8s.io/snapshotter-secret-namespace",
        ),
        (
            "csi.storage.k8s.io/snapshotter-secret-name",
            "csi.storage.k8s.io/snapshotter-secret-name",
        ),
        (
            "csi.storage.k8s.io/snapshotter-secret-namespace",
            "csi.storage.k8s.io/snapshotter-secret-namespace",
        ),
    ):
        value = parameters.get(source)
        if value:
            out[target] = value
    return out


def _assert_snapshot_class(snapshot_class: VolumeSnapshotClass, *, driver: str) -> None:
    if snapshot_class.driver != driver:
        msg = (
            f"VolumeSnapshotClass {snapshot_class.name!r} uses driver "
            f"{snapshot_class.driver!r}, expected {driver!r}"
        )
        raise OSError(msg)
    if snapshot_class.deletion_policy != "Delete":
        msg = (
            f"VolumeSnapshotClass {snapshot_class.name!r} must use "
            "deletionPolicy=Delete"
        )
        raise OSError(msg)


def _build_source_orphaned(
    *,
    labels: Mapping[str, str],
    created_at: datetime | None,
    active_build_names: Collection[str],
    now: datetime,
    max_age: timedelta,
) -> bool:
    if labels.get(REPOSITORY_BUILD_REQUEST_LABEL, "") in active_build_names:
        return False
    if created_at is None:
        return True
    if created_at.tzinfo is None:
        created_at = created_at.replace(tzinfo=UTC)
    return now - created_at.astimezone(UTC) >= max_age
