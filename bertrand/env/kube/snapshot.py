"""Kubernetes CSI snapshot custom-object wrappers."""

from __future__ import annotations

from collections.abc import Mapping
from typing import TYPE_CHECKING, Self, cast

from bertrand.env.git import Deadline, until
from bertrand.env.kube.custom_object import CustomResource, custom_resource

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


@custom_resource(
    group=SNAPSHOT_GROUP,
    version=SNAPSHOT_VERSION,
    kind=VOLUME_SNAPSHOT_CLASS_KIND,
    plural=VOLUME_SNAPSHOT_CLASS_PLURAL,
    scope="cluster",
)
class VolumeSnapshotClass(CustomResource):
    """Wrapper around one Kubernetes CSI VolumeSnapshotClass object."""

    @classmethod
    async def create(
        cls,
        kube: Kube,
        *,
        name: str,
        driver: str,
        deletion_policy: str,
        deadline: Deadline,
        parameters: Mapping[str, str] | None = None,
        labels: Mapping[str, str] | None = None,
    ) -> Self:
        """Create one `VolumeSnapshotClass`.

        Returns
        -------
        VolumeSnapshotClass
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
        return await cls.create_manifest(
            kube,
            manifest=manifest,
            deadline=deadline,
        )

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

    @classmethod
    async def create(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        source_claim: str,
        snapshot_class: str,
        deadline: Deadline,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> Self:
        """Create one snapshot from a source PersistentVolumeClaim.

        Returns
        -------
        VolumeSnapshot
            Created VolumeSnapshot.
        """
        return await super().create_spec(
            kube,
            namespace=namespace,
            name=name,
            spec={
                "volumeSnapshotClassName": snapshot_class,
                "source": {"persistentVolumeClaimName": source_claim},
            },
            labels=labels,
            annotations=annotations,
            deadline=deadline,
        )

    async def wait_ready(self, kube: Kube, *, deadline: Deadline) -> Self:
        """Wait until this VolumeSnapshot reports `readyToUse`.

        Returns
        -------
        VolumeSnapshot
            Fresh snapshot object whose status is ready.

        Raises
        ------
        TimeoutError
            If the snapshot is not ready before `deadline` expires.
        """
        namespace, name = self._require_namespace_name("wait for VolumeSnapshot")

        async def ready(attempt_deadline: Deadline) -> Self:
            live = await self.refresh(kube, deadline=attempt_deadline)
            if live is None:
                msg = f"VolumeSnapshot {namespace}/{name} disappeared before ready"
                raise OSError(msg)
            if live.error_message:
                msg = f"VolumeSnapshot {namespace}/{name} failed: {live.error_message}"
                raise OSError(msg)
            if live.ready_to_use:
                return live
            msg = f"VolumeSnapshot {namespace}/{name} is not ready yet"
            raise TimeoutError(msg)

        try:
            return await until(
                ready,
                deadline=deadline,
                delay=VOLUME_SNAPSHOT_WAIT_INTERVAL_SECONDS,
            )
        except TimeoutError as err:
            msg = f"timed out waiting for VolumeSnapshot {namespace}/{name}"
            raise TimeoutError(msg) from err

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
