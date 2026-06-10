"""Ceph repository snapshot lifecycle and build-source helpers."""

from __future__ import annotations

import hashlib
import uuid
from contextlib import asynccontextmanager, suppress
from datetime import UTC, datetime, timedelta
from typing import TYPE_CHECKING

from bertrand.env.config.core import _check_uuid
from bertrand.env.git import (
    BERTRAND_LABEL,
    BERTRAND_LABEL_MANAGED,
    BERTRAND_NAMESPACE,
    REPO_ID_LABEL,
    Deadline,
)
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.snapshot import (
    VOLUME_SNAPSHOT_CLASS_RESOURCE,
    VOLUME_SNAPSHOT_RESOURCE,
    create_volume_snapshot,
    create_volume_snapshot_class,
    delete_volume_snapshot,
    volume_snapshot_class_deletion_policy,
    volume_snapshot_class_driver,
    volume_snapshot_created_at,
    volume_snapshot_ready_to_use,
    wait_volume_snapshot_ready,
)
from bertrand.env.kube.volume import PersistentVolumeClaim, StorageClass

from .volume import (
    CEPHFS_STORAGE_CLASS_PREFERENCES,
    REPO_VOLUME_CLAIM_LABEL,
    REPOSITORY_BUILD_SOURCE_LABEL,
    REPOSITORY_BUILD_SOURCE_LABEL_VALUE,
    REPOSITORY_SNAPSHOT_LABEL,
    REPOSITORY_SNAPSHOT_LABEL_VALUE,
    REPOSITORY_SNAPSHOT_PURPOSE_BUILD,
    REPOSITORY_SNAPSHOT_PURPOSE_LABEL,
    REPOSITORY_SNAPSHOT_PURPOSE_RETAINED,
    REPOSITORY_STATE_RESOURCE,
    list_repository_volume_claims,
)

if TYPE_CHECKING:
    from collections.abc import AsyncIterator, Collection, Mapping

    from bertrand.env.kube.custom_object import CustomObject

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


async def ensure_repository_snapshot_support(
    kube: Kube,
    *,
    deadline: Deadline,
) -> CustomObject:
    """Ensure repository snapshot primitives are usable.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    deadline : Deadline
        Maximum convergence budget in seconds.

    Returns
    -------
    CustomObject
        CephFS-compatible snapshot class selected for repository snapshots.

    Raises
    ------
    OSError
        If CephFS storage or snapshot support is unavailable.
    """
    storage = await StorageClass.select(
        kube,
        deadline=deadline,
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
        deadline=deadline,
    )


async def create_repository_snapshot(
    kube: Kube,
    *,
    repo_id: str,
    deadline: Deadline,
) -> CustomObject:
    """Create one retained snapshot for a managed repository volume.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : str
        Stable repository UUID.
    deadline : Deadline
        Maximum snapshot budget in seconds.

    Returns
    -------
    CustomObject
        Ready retained snapshot.
    """
    repo_id = _check_uuid(repo_id)
    snapshot, _volume = await _create_snapshot(
        kube,
        repo_id=repo_id,
        purpose=REPOSITORY_SNAPSHOT_PURPOSE_RETAINED,
        build_name=None,
        deadline=deadline,
    )
    return snapshot


async def maintain_repository_snapshots(
    kube: Kube,
    *,
    deadline: Deadline,
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
    deadline : Deadline
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
    ValueError
        If cadence or limit inputs are invalid.
    """
    if interval_seconds <= 0 or retention_seconds <= 0:
        msg = "repository snapshot interval and retention must be positive"
        raise ValueError(msg)
    if create_limit < 0 or delete_limit < 0:
        msg = "repository snapshot maintenance limits must be non-negative"
        raise ValueError(msg)
    await ensure_repository_snapshot_support(kube, deadline=deadline)

    now = datetime.now(UTC)
    retention = timedelta(seconds=retention_seconds)
    retained = await VOLUME_SNAPSHOT_RESOURCE.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        labels=_snapshot_labels(purpose=REPOSITORY_SNAPSHOT_PURPOSE_RETAINED),
        deadline=deadline,
    )
    active_records = [
        record
        for record in await REPOSITORY_STATE_RESOURCE.list(
            kube,
            namespace=BERTRAND_NAMESPACE,
            deadline=deadline,
        )
        if record.spec.phase == "Ready"
    ]
    deleted_names: set[str] = set()
    for snapshot in sorted(retained, key=lambda item: item.name):
        if len(deleted_names) >= delete_limit:
            break
        created_at = volume_snapshot_created_at(snapshot)
        if created_at is None or now - created_at < retention:
            continue
        await delete_volume_snapshot(kube, snapshot, deadline=deadline)
        deleted_names.add(snapshot.name)

    snapshots_by_repo: dict[str, list[CustomObject]] = {}
    for snapshot in retained:
        if snapshot.name in deleted_names:
            continue
        repo_id = snapshot.labels.get(REPO_ID_LABEL, "")
        if repo_id:
            snapshots_by_repo.setdefault(repo_id, []).append(snapshot)

    created = 0
    fresh_boundary = now - timedelta(seconds=interval_seconds)
    for record in sorted(active_records, key=lambda item: item.spec.repo_id):
        if created >= create_limit:
            break
        repo_snapshots = snapshots_by_repo.get(record.spec.repo_id, [])
        fresh = False
        for snapshot in repo_snapshots:
            created_at = volume_snapshot_created_at(snapshot)
            if (
                volume_snapshot_ready_to_use(snapshot)
                and created_at is not None
                and created_at >= fresh_boundary
            ):
                fresh = True
                break
        if fresh:
            continue
        await create_repository_snapshot(
            kube,
            repo_id=record.spec.repo_id,
            deadline=deadline,
        )
        created += 1


async def next_repository_snapshot_time(
    kube: Kube,
    *,
    deadline: Deadline,
    interval_seconds: int = REPOSITORY_SNAPSHOT_INTERVAL_SECONDS,
    retention_seconds: int = REPOSITORY_SNAPSHOT_RETENTION_SECONDS,
) -> datetime | None:
    """Return the next time repository snapshot maintenance may be useful.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    deadline : Deadline
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
    ValueError
        If cadence inputs are invalid.
    """
    if interval_seconds <= 0 or retention_seconds <= 0:
        msg = "repository snapshot interval and retention must be positive"
        raise ValueError(msg)
    now = datetime.now(UTC)
    retained = await VOLUME_SNAPSHOT_RESOURCE.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        labels=_snapshot_labels(purpose=REPOSITORY_SNAPSHOT_PURPOSE_RETAINED),
        deadline=deadline,
    )
    active_records = [
        record
        for record in await REPOSITORY_STATE_RESOURCE.list(
            kube,
            namespace=BERTRAND_NAMESPACE,
            deadline=deadline,
        )
        if record.spec.phase == "Ready"
    ]
    snapshots_by_repo: dict[str, list[CustomObject]] = {}
    for snapshot in retained:
        repo_id = snapshot.labels.get(REPO_ID_LABEL, "")
        if repo_id:
            snapshots_by_repo.setdefault(repo_id, []).append(snapshot)
    retention = timedelta(seconds=retention_seconds)
    for snapshot in retained:
        created_at = volume_snapshot_created_at(snapshot)
        if created_at is not None and now - created_at >= retention:
            return now
    if not active_records:
        return None

    boundaries: list[datetime] = []
    interval = timedelta(seconds=interval_seconds)
    for record in active_records:
        ready: list[datetime] = []
        for snapshot in snapshots_by_repo.get(record.spec.repo_id, []):
            created_at = volume_snapshot_created_at(snapshot)
            if volume_snapshot_ready_to_use(snapshot) and created_at is not None:
                ready.append(created_at)
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
    deadline: Deadline,
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
    deadline : Deadline
        Maximum preparation budget in seconds.

    Yields
    ------
    str
        Temporary read-only source claim name.

    Raises
    ------
    ValueError
        If `build_name` is empty.
    """
    repo_id = _check_uuid(repo_id)
    build_name = build_name.strip()
    if not build_name:
        msg = "repository build snapshot requires a non-empty build name"
        raise ValueError(msg)
    snapshot: CustomObject | None = None
    pvc: PersistentVolumeClaim | None = None
    try:
        snapshot, volume = await _create_snapshot(
            kube,
            repo_id=repo_id,
            purpose=REPOSITORY_SNAPSHOT_PURPOSE_BUILD,
            build_name=build_name,
            deadline=deadline,
        )
        pvc = await PersistentVolumeClaim.create_from_snapshot(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=_build_source_claim_name(repo_id=repo_id, build_name=build_name),
            access_modes=volume.access_modes or ("ReadWriteMany",),
            storage_class=volume.storage_class_name,
            storage_request=volume.requested_storage,
            snapshot_name=snapshot.name,
            labels=_build_source_labels(repo_id=repo_id, build_name=build_name),
            annotations={
                REPOSITORY_BUILD_SOURCE_SNAPSHOT_ANNOTATION: snapshot.name,
            },
            deadline=deadline,
        )
        pvc = await pvc.wait_bound(kube, deadline=deadline)
        yield pvc.name
    finally:
        await _cleanup_build_source(kube, pvc=pvc, snapshot=snapshot)


async def cleanup_orphaned_build_sources(
    kube: Kube,
    *,
    active_build_names: Collection[str],
    deadline: Deadline,
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
    deadline : Deadline
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
    ValueError
        If cleanup age or limit inputs are invalid.
    """
    if max_age_seconds < 0 or limit < 0:
        msg = "repository build-source cleanup age and limit must be non-negative"
        raise ValueError(msg)
    if limit == 0:
        return 0

    active = {name.strip() for name in active_build_names if name and name.strip()}
    now = datetime.now(UTC)
    max_age = timedelta(seconds=max_age_seconds)
    deleted = 0

    pvcs = await PersistentVolumeClaim.list(
        kube,
        namespaces=(BERTRAND_NAMESPACE,),
        labels={
            BERTRAND_LABEL: BERTRAND_LABEL_MANAGED,
            REPOSITORY_BUILD_SOURCE_LABEL: REPOSITORY_BUILD_SOURCE_LABEL_VALUE,
        },
        deadline=deadline,
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
        await pvc.delete(kube, deadline=deadline)
        deleted += 1

    snapshots = await VOLUME_SNAPSHOT_RESOURCE.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        labels=_snapshot_labels(purpose=REPOSITORY_SNAPSHOT_PURPOSE_BUILD),
        deadline=deadline,
    )
    for snapshot in sorted(snapshots, key=lambda item: item.name):
        if deleted >= limit:
            break
        if not _build_source_orphaned(
            labels=snapshot.labels,
            created_at=volume_snapshot_created_at(snapshot),
            active_build_names=active,
            now=now,
            max_age=max_age,
        ):
            continue
        await delete_volume_snapshot(kube, snapshot, deadline=deadline)
        deleted += 1

    return deleted


async def _ensure_snapshot_class(
    kube: Kube,
    *,
    storage: StorageClass,
    deadline: Deadline,
) -> CustomObject:
    classes = await VOLUME_SNAPSHOT_CLASS_RESOURCE.list(kube, deadline=deadline)
    matches = [
        item
        for item in classes
        if volume_snapshot_class_driver(item) == storage.provisioner
        and volume_snapshot_class_deletion_policy(item) == "Delete"
    ]
    if matches:
        return sorted(matches, key=lambda item: item.name)[0]

    name = _snapshot_class_name(storage.provisioner)
    existing = await VOLUME_SNAPSHOT_CLASS_RESOURCE.get(
        kube,
        name=name,
        deadline=deadline,
    )
    if existing is not None:
        _assert_snapshot_class(existing, driver=storage.provisioner)
        return existing
    try:
        return await create_volume_snapshot_class(
            kube,
            name=name,
            driver=storage.provisioner,
            deletion_policy="Delete",
            parameters=_snapshot_class_parameters(storage.parameters),
            labels={
                BERTRAND_LABEL: BERTRAND_LABEL_MANAGED,
                REPOSITORY_SNAPSHOT_CLASS_LABEL: "v1",
            },
            deadline=deadline,
        )
    except OSError as err:
        if not isinstance(err, Kube.APIError) or err.status != 409:
            raise
    existing = await VOLUME_SNAPSHOT_CLASS_RESOURCE.get(
        kube,
        name=name,
        deadline=deadline,
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
    deadline: Deadline,
) -> tuple[CustomObject, PersistentVolumeClaim]:
    volume = await _repository_volume_claim(
        kube,
        repo_id=repo_id,
        deadline=deadline,
    )
    await volume.wait_bound(kube, deadline=deadline)
    snapshot_class = await ensure_repository_snapshot_support(
        kube,
        deadline=deadline,
    )
    snapshot = await create_volume_snapshot(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=_snapshot_name(repo_id=repo_id, purpose=purpose, build_name=build_name),
        source_claim=volume.name,
        snapshot_class=snapshot_class.name,
        labels=_snapshot_labels(
            repo_id=repo_id,
            purpose=purpose,
            build_name=build_name,
        ),
        annotations={
            REPOSITORY_SNAPSHOT_SOURCE_CLAIM_ANNOTATION: volume.name,
        },
        deadline=deadline,
    )
    ready = await wait_volume_snapshot_ready(kube, snapshot, deadline=deadline)
    return ready, volume


async def _repository_volume_claim(
    kube: Kube,
    *,
    repo_id: str,
    deadline: Deadline,
) -> PersistentVolumeClaim:
    volumes = await list_repository_volume_claims(kube, repo_id, deadline=deadline)
    if len(volumes) != 1:
        msg = (
            f"repository snapshot requires one managed repository PVC for "
            f"{repo_id!r}, found {len(volumes)}"
        )
        raise OSError(msg)
    return volumes[0]


async def _cleanup_build_source(
    kube: Kube,
    *,
    pvc: PersistentVolumeClaim | None,
    snapshot: CustomObject | None,
) -> None:
    deadline = Deadline(REPOSITORY_BUILD_SOURCE_CLEANUP_TIMEOUT_SECONDS)
    if pvc is not None:
        with suppress(OSError, TimeoutError, ValueError):
            await pvc.delete(kube, deadline=deadline)
            await pvc.wait_deleted(kube, deadline=deadline)
    if snapshot is not None:
        with suppress(OSError, TimeoutError, ValueError):
            await delete_volume_snapshot(kube, snapshot, deadline=deadline)


def _snapshot_labels(
    *,
    purpose: str,
    repo_id: str | None = None,
    build_name: str | None = None,
) -> dict[str, str]:
    labels = {
        BERTRAND_LABEL: BERTRAND_LABEL_MANAGED,
        REPO_VOLUME_CLAIM_LABEL: BERTRAND_LABEL_MANAGED,
        REPOSITORY_SNAPSHOT_LABEL: REPOSITORY_SNAPSHOT_LABEL_VALUE,
        REPOSITORY_SNAPSHOT_PURPOSE_LABEL: purpose,
    }
    if repo_id is not None:
        labels[REPO_ID_LABEL] = _check_uuid(repo_id)
    if build_name is not None:
        labels[REPOSITORY_BUILD_REQUEST_LABEL] = build_name
    return labels


def _build_source_labels(*, repo_id: str, build_name: str) -> dict[str, str]:
    return {
        BERTRAND_LABEL: BERTRAND_LABEL_MANAGED,
        REPO_VOLUME_CLAIM_LABEL: BERTRAND_LABEL_MANAGED,
        REPO_ID_LABEL: _check_uuid(repo_id),
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


def _assert_snapshot_class(snapshot_class: CustomObject, *, driver: str) -> None:
    snapshot_driver = volume_snapshot_class_driver(snapshot_class)
    if snapshot_driver != driver:
        msg = (
            f"VolumeSnapshotClass {snapshot_class.name!r} uses driver "
            f"{snapshot_driver!r}, expected {driver!r}"
        )
        raise OSError(msg)
    if volume_snapshot_class_deletion_policy(snapshot_class) != "Delete":
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
