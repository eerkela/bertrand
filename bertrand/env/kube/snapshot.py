"""Kubernetes CSI snapshot custom-object wrappers."""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
from typing import TYPE_CHECKING, cast

from bertrand.env.git import EMPTY_MAPPING
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


@dataclass(frozen=True)
class VolumeSnapshotClassManifest:
    """Push-side manifest for a Kubernetes CSI VolumeSnapshotClass.

    Parameters
    ----------
    name : str
        Kubernetes VolumeSnapshotClass name.
    driver : str
        CSI driver name.
    deletion_policy : str
        Snapshot deletion policy.
    parameters : Mapping[str, object]
        CSI driver parameters.
    labels : Mapping[str, str]
        Metadata labels to apply.
    """

    name: str
    driver: str
    deletion_policy: str = "Delete"
    parameters: Mapping[str, object] = EMPTY_MAPPING
    labels: Mapping[str, str] = EMPTY_MAPPING

    @property
    def namespace(self) -> None:
        """Return `None` because VolumeSnapshotClass is cluster-scoped."""
        return None

    def manifest(self) -> Mapping[str, object]:
        """Render the Kubernetes VolumeSnapshotClass manifest.

        Returns
        -------
        Mapping[str, object]
            Complete Kubernetes custom-object manifest.
        """
        return {
            "apiVersion": SNAPSHOT_API_VERSION,
            "kind": VOLUME_SNAPSHOT_CLASS_KIND,
            "metadata": {
                "name": self.name,
                "labels": dict(self.labels),
            },
            "driver": self.driver,
            "deletionPolicy": self.deletion_policy,
            "parameters": dict(self.parameters),
        }


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


@dataclass(frozen=True)
class VolumeSnapshotManifest:
    """Push-side manifest for a Kubernetes CSI VolumeSnapshot.

    Parameters
    ----------
    namespace : str
        Namespace that owns the VolumeSnapshot.
    name : str
        Kubernetes VolumeSnapshot name.
    snapshot_class_name : str
        VolumeSnapshotClass used by the snapshot.
    source_claim : str
        Source PersistentVolumeClaim name.
    labels : Mapping[str, str]
        Metadata labels to apply.
    annotations : Mapping[str, str]
        Metadata annotations to apply.
    """

    namespace: str
    name: str
    snapshot_class_name: str
    source_claim: str
    labels: Mapping[str, str] = EMPTY_MAPPING
    annotations: Mapping[str, str] = EMPTY_MAPPING

    def manifest(self) -> Mapping[str, object]:
        """Render the Kubernetes VolumeSnapshot manifest.

        Returns
        -------
        Mapping[str, object]
            Complete Kubernetes custom-object manifest.
        """
        return {
            "apiVersion": SNAPSHOT_API_VERSION,
            "kind": VOLUME_SNAPSHOT_KIND,
            "metadata": {
                "namespace": self.namespace,
                "name": self.name,
                "labels": dict(self.labels),
                "annotations": dict(self.annotations),
            },
            "spec": {
                "volumeSnapshotClassName": self.snapshot_class_name,
                "source": {"persistentVolumeClaimName": self.source_claim},
            },
        }


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
