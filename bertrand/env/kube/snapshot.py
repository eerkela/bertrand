"""Kubernetes CSI snapshot custom-object wrappers."""

from __future__ import annotations

from collections.abc import Mapping
from typing import TYPE_CHECKING, cast

from bertrand.env.kube.custom_object import CustomResource, custom_resource

if TYPE_CHECKING:
    from datetime import datetime

SNAPSHOT_GROUP = "snapshot.storage.k8s.io"
SNAPSHOT_VERSION = "v1"
SNAPSHOT_API_VERSION = f"{SNAPSHOT_GROUP}/{SNAPSHOT_VERSION}"
VOLUME_SNAPSHOT_KIND = "VolumeSnapshot"
VOLUME_SNAPSHOT_PLURAL = "volumesnapshots"
VOLUME_SNAPSHOT_CLASS_KIND = "VolumeSnapshotClass"
VOLUME_SNAPSHOT_CLASS_PLURAL = "volumesnapshotclasses"


@custom_resource(
    group=SNAPSHOT_GROUP,
    version=SNAPSHOT_VERSION,
    kind=VOLUME_SNAPSHOT_CLASS_KIND,
    plural=VOLUME_SNAPSHOT_CLASS_PLURAL,
    scope="cluster",
)
class VolumeSnapshotClass(CustomResource):
    """Wrapper around one Kubernetes CSI VolumeSnapshotClass object."""

    @property
    def driver(self) -> str:
        """Return the CSI driver name.

        Returns
        -------
        str
            Trimmed `driver` field.
        """
        return str(self.payload.get("driver") or "").strip()

    @property
    def deletion_policy(self) -> str:
        """Return the deletion policy.

        Returns
        -------
        str
            Trimmed `deletionPolicy` field.
        """
        return str(self.payload.get("deletionPolicy") or "").strip()


@custom_resource(
    group=SNAPSHOT_GROUP,
    version=SNAPSHOT_VERSION,
    kind=VOLUME_SNAPSHOT_KIND,
    plural=VOLUME_SNAPSHOT_PLURAL,
)
class VolumeSnapshot(CustomResource):
    """Wrapper around one Kubernetes CSI VolumeSnapshot object."""

    @property
    def snapshot_created_at(self) -> datetime | None:
        """Return the snapshot creation timestamp.

        Returns
        -------
        datetime | None
            Snapshot status creation time, metadata creation time, or `None`.
        """
        value = self.status.get("creationTime")
        if isinstance(value, str):
            return self.parse_utc_datetime(value)
        return self.created_at_utc

    @property
    def ready_to_use(self) -> bool:
        """Return whether this snapshot is ready to use.

        Returns
        -------
        bool
            Value of `status.readyToUse`.
        """
        return bool(self.status.get("readyToUse"))

    @property
    def error_message(self) -> str:
        """Return the snapshot controller error message.

        Returns
        -------
        str
            Snapshot status error message, or an empty string.
        """
        error = self.status.get("error")
        if not isinstance(error, Mapping):
            return ""
        error = cast("Mapping[str, object]", error)
        return str(error.get("message") or "").strip()

    @property
    def source_claim(self) -> str:
        """Return the source PersistentVolumeClaim name.

        Returns
        -------
        str
            Source PVC name, or an empty string when unavailable.
        """
        source = self.spec.get("source")
        if not isinstance(source, Mapping):
            return ""
        source = cast("Mapping[str, object]", source)
        return str(source.get("persistentVolumeClaimName") or "").strip()

    @property
    def snapshot_class_name(self) -> str:
        """Return the snapshot class name.

        Returns
        -------
        str
            `spec.volumeSnapshotClassName`, or an empty string.
        """
        return str(self.spec.get("volumeSnapshotClassName") or "").strip()
