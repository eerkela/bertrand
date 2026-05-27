"""Kubernetes CSI snapshot custom-object helpers."""

from __future__ import annotations

from collections.abc import Mapping
from typing import TYPE_CHECKING, cast

from bertrand.env.git import until
from bertrand.env.kube.custom_object import CustomObject, CustomObjectResource

if TYPE_CHECKING:
    from datetime import datetime

    from .api.client import Kube

SNAPSHOT_GROUP = "snapshot.storage.k8s.io"
SNAPSHOT_VERSION = "v1"
SNAPSHOT_API_VERSION = f"{SNAPSHOT_GROUP}/{SNAPSHOT_VERSION}"
VOLUME_SNAPSHOT_KIND = "VolumeSnapshot"
VOLUME_SNAPSHOT_PLURAL = "volumesnapshots"
VOLUME_SNAPSHOT_CLASS_KIND = "VolumeSnapshotClass"
VOLUME_SNAPSHOT_CLASS_PLURAL = "volumesnapshotclasses"
VOLUME_SNAPSHOT_WAIT_INTERVAL_SECONDS = 0.5


VOLUME_SNAPSHOT_CLASS_RESOURCE = CustomObjectResource[CustomObject](
    group=SNAPSHOT_GROUP,
    version=SNAPSHOT_VERSION,
    kind=VOLUME_SNAPSHOT_CLASS_KIND,
    plural=VOLUME_SNAPSHOT_CLASS_PLURAL,
    scope="cluster",
)


VOLUME_SNAPSHOT_RESOURCE = CustomObjectResource[CustomObject](
    group=SNAPSHOT_GROUP,
    version=SNAPSHOT_VERSION,
    kind=VOLUME_SNAPSHOT_KIND,
    plural=VOLUME_SNAPSHOT_PLURAL,
)


async def create_volume_snapshot_class(
    kube: Kube,
    *,
    name: str,
    driver: str,
    deletion_policy: str,
    timeout: float,
    parameters: Mapping[str, str] | None = None,
    labels: Mapping[str, str] | None = None,
) -> CustomObject:
    """Create one `VolumeSnapshotClass`.

    Returns
    -------
    CustomObject
        Created snapshot class.
    """
    manifest = {
        "apiVersion": SNAPSHOT_API_VERSION,
        "kind": VOLUME_SNAPSHOT_CLASS_KIND,
        "metadata": {"name": name, "labels": dict(labels or {})},
        "driver": driver,
        "deletionPolicy": deletion_policy,
        "parameters": dict(parameters or {}),
    }
    return await VOLUME_SNAPSHOT_CLASS_RESOURCE.create_manifest(
        kube,
        manifest=manifest,
        timeout=timeout,
    )


def volume_snapshot_class_driver(snapshot_class: CustomObject) -> str:
    """Return the CSI driver name for one snapshot class.

    Returns
    -------
    str
        Trimmed `driver` field.
    """
    return str(snapshot_class.payload.get("driver") or "").strip()


def volume_snapshot_class_deletion_policy(snapshot_class: CustomObject) -> str:
    """Return the deletion policy for one snapshot class.

    Returns
    -------
    str
        Trimmed `deletionPolicy` field.
    """
    return str(snapshot_class.payload.get("deletionPolicy") or "").strip()


async def create_volume_snapshot(
    kube: Kube,
    *,
    namespace: str,
    name: str,
    source_claim: str,
    snapshot_class: str,
    timeout: float,
    labels: Mapping[str, str] | None = None,
    annotations: Mapping[str, str] | None = None,
) -> CustomObject:
    """Create one snapshot from a source PersistentVolumeClaim.

    Returns
    -------
    CustomObject
        Created VolumeSnapshot.
    """
    return await VOLUME_SNAPSHOT_RESOURCE.create(
        kube,
        namespace=namespace,
        name=name,
        spec={
            "volumeSnapshotClassName": snapshot_class,
            "source": {"persistentVolumeClaimName": source_claim},
        },
        labels=labels,
        annotations=annotations,
        timeout=timeout,
    )


async def delete_volume_snapshot(
    kube: Kube,
    snapshot: CustomObject,
    *,
    timeout: float,
) -> None:
    """Delete one VolumeSnapshot."""
    namespace, name = _require_volume_snapshot_namespace_name(
        snapshot, "delete VolumeSnapshot"
    )
    await VOLUME_SNAPSHOT_RESOURCE.delete_by_name(
        kube,
        namespace=namespace,
        name=name,
        timeout=timeout,
    )


async def refresh_volume_snapshot(
    kube: Kube,
    snapshot: CustomObject,
    *,
    timeout: float,
) -> CustomObject | None:
    """Re-read one VolumeSnapshot.

    Returns
    -------
    CustomObject | None
        Fresh snapshot object, or `None` when deleted.
    """
    namespace, name = _require_volume_snapshot_namespace_name(
        snapshot, "refresh VolumeSnapshot"
    )
    return await VOLUME_SNAPSHOT_RESOURCE.get(
        kube,
        namespace=namespace,
        name=name,
        timeout=timeout,
    )


async def wait_volume_snapshot_ready(
    kube: Kube,
    snapshot: CustomObject,
    *,
    timeout: float,
) -> CustomObject:
    """Wait until one VolumeSnapshot reports `readyToUse`.

    Returns
    -------
    CustomObject
        Fresh snapshot object whose status is ready.

    Raises
    ------
    TimeoutError
        If readiness does not complete before `timeout`.
    """
    namespace, name = _require_volume_snapshot_namespace_name(
        snapshot, "wait for VolumeSnapshot"
    )

    async def ready(remaining: float) -> CustomObject:
        live = await refresh_volume_snapshot(kube, snapshot, timeout=remaining)
        if live is None:
            msg = f"VolumeSnapshot {namespace}/{name} disappeared before ready"
            raise OSError(msg)
        error = volume_snapshot_error_message(live)
        if error:
            msg = f"VolumeSnapshot {namespace}/{name} failed: {error}"
            raise OSError(msg)
        if volume_snapshot_ready_to_use(live):
            return live
        msg = f"VolumeSnapshot {namespace}/{name} is not ready yet"
        raise TimeoutError(msg)

    try:
        return await until(
            ready,
            timeout=timeout,
            interval=VOLUME_SNAPSHOT_WAIT_INTERVAL_SECONDS,
            action=f"waiting for VolumeSnapshot {namespace}/{name}",
        )
    except TimeoutError as err:
        msg = f"timed out waiting for VolumeSnapshot {namespace}/{name}"
        raise TimeoutError(msg) from err


async def wait_volume_snapshot_deleted(
    kube: Kube,
    snapshot: CustomObject,
    *,
    timeout: float,
) -> None:
    """Wait until one VolumeSnapshot is deleted."""
    namespace, name = _require_volume_snapshot_namespace_name(
        snapshot, "wait for VolumeSnapshot"
    )
    await VOLUME_SNAPSHOT_RESOURCE.wait_deleted(
        label=f"VolumeSnapshot {namespace}/{name}",
        timeout=timeout,
        refresh=lambda remaining: refresh_volume_snapshot(
            kube, snapshot, timeout=remaining
        ),
    )


def volume_snapshot_created_at(snapshot: CustomObject) -> datetime | None:
    """Return the snapshot creation timestamp.

    Returns
    -------
    datetime | None
        Snapshot status creation time, metadata creation time, or `None`.
    """
    value = snapshot.status.get("creationTime")
    if isinstance(value, str):
        return snapshot.parse_utc_datetime(value)
    return snapshot.created_at_utc


def volume_snapshot_ready_to_use(snapshot: CustomObject) -> bool:
    """Return whether one snapshot is ready to use.

    Returns
    -------
    bool
        Value of `status.readyToUse`.
    """
    return bool(snapshot.status.get("readyToUse"))


def volume_snapshot_error_message(snapshot: CustomObject) -> str:
    """Return the snapshot controller error message.

    Returns
    -------
    str
        Snapshot status error message, or an empty string.
    """
    error = snapshot.status.get("error")
    if not isinstance(error, Mapping):
        return ""
    error = cast("Mapping[str, object]", error)
    return str(error.get("message") or "").strip()


def volume_snapshot_source_claim(snapshot: CustomObject) -> str:
    """Return the source PersistentVolumeClaim name.

    Returns
    -------
    str
        Source PVC name, or an empty string when unavailable.
    """
    source = snapshot.spec.get("source")
    if not isinstance(source, Mapping):
        return ""
    source = cast("Mapping[str, object]", source)
    return str(source.get("persistentVolumeClaimName") or "").strip()


def volume_snapshot_class_name(snapshot: CustomObject) -> str:
    """Return the snapshot class name.

    Returns
    -------
    str
        `spec.volumeSnapshotClassName`, or an empty string.
    """
    return str(snapshot.spec.get("volumeSnapshotClassName") or "").strip()


def _require_volume_snapshot_namespace_name(
    snapshot: CustomObject, action: str
) -> tuple[str, str]:
    namespace = snapshot.namespace
    name = snapshot.name
    if not namespace or not name:
        msg = f"cannot {action} with missing metadata.name/namespace"
        raise OSError(msg)
    return namespace, name
