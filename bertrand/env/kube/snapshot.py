"""Wrappers for Kubernetes CSI snapshot custom resources."""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
from typing import TYPE_CHECKING, Self, cast

from bertrand.env.git import until
from bertrand.env.kube.custom_object import (
    CustomObject,
    CustomObjectClient,
    CustomObjectSpec,
)

if TYPE_CHECKING:
    import builtins
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

_VOLUME_SNAPSHOT_CLASS_SPEC = CustomObjectSpec(
    group=SNAPSHOT_GROUP,
    version=SNAPSHOT_VERSION,
    kind=VOLUME_SNAPSHOT_CLASS_KIND,
    plural=VOLUME_SNAPSHOT_CLASS_PLURAL,
    scope="cluster",
)
_VOLUME_SNAPSHOT_SPEC = CustomObjectSpec(
    group=SNAPSHOT_GROUP,
    version=SNAPSHOT_VERSION,
    kind=VOLUME_SNAPSHOT_KIND,
    plural=VOLUME_SNAPSHOT_PLURAL,
)
_VOLUME_SNAPSHOT_CLASS_CLIENT = CustomObjectClient(_VOLUME_SNAPSHOT_CLASS_SPEC)
_VOLUME_SNAPSHOT_CLIENT = CustomObjectClient(_VOLUME_SNAPSHOT_SPEC)


@dataclass(frozen=True)
class VolumeSnapshotClass:
    """Wrapper around one cluster-scoped `VolumeSnapshotClass`.

    Parameters
    ----------
    _obj : CustomObject
        Generic custom object returned by the snapshot API.
    """

    _obj: CustomObject

    @classmethod
    def _from_object(cls, obj: CustomObject) -> Self:
        return cls(_obj=obj)

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        name: str,
        timeout: float,
    ) -> Self | None:
        """Read one `VolumeSnapshotClass` by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Snapshot class name to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        VolumeSnapshotClass | None
            Wrapped snapshot class, or `None` if it does not exist.
        """
        obj = await _VOLUME_SNAPSHOT_CLASS_CLIENT.get(
            kube,
            name=name,
            timeout=timeout,
        )
        return None if obj is None else cls._from_object(obj)

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[Self]:
        """List `VolumeSnapshotClass` objects with optional labels.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Optional exact-match label selector.

        Returns
        -------
        list[VolumeSnapshotClass]
            Wrapped snapshot classes matching the selector.
        """
        objects = await _VOLUME_SNAPSHOT_CLASS_CLIENT.list(
            kube,
            labels=labels,
            timeout=timeout,
        )
        return [cls._from_object(obj) for obj in objects]

    @classmethod
    async def create(
        cls,
        kube: Kube,
        *,
        name: str,
        driver: str,
        deletion_policy: str,
        timeout: float,
        parameters: Mapping[str, str] | None = None,
        labels: Mapping[str, str] | None = None,
    ) -> Self:
        """Create one `VolumeSnapshotClass`.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Snapshot class name.
        driver : str
            CSI driver name.
        deletion_policy : str
            Kubernetes snapshot deletion policy.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        parameters : Mapping[str, str] | None, optional
            Driver-specific snapshot class parameters.
        labels : Mapping[str, str] | None, optional
            Labels to apply to the snapshot class.

        Returns
        -------
        VolumeSnapshotClass
            Wrapped created snapshot class.
        """
        manifest = {
            "apiVersion": SNAPSHOT_API_VERSION,
            "kind": VOLUME_SNAPSHOT_CLASS_KIND,
            "metadata": {"name": name, "labels": dict(labels or {})},
            "driver": driver,
            "deletionPolicy": deletion_policy,
            "parameters": dict(parameters or {}),
        }
        obj = await _VOLUME_SNAPSHOT_CLASS_CLIENT.create_manifest(
            kube,
            manifest=manifest,
            timeout=timeout,
        )
        return cls._from_object(obj)

    @property
    def name(self) -> str:
        """Return the snapshot class name.

        Returns
        -------
        str
            Kubernetes `metadata.name`, or an empty string when unavailable.
        """
        return self._obj.name

    @property
    def labels(self) -> Mapping[str, str]:
        """Return snapshot class labels.

        Returns
        -------
        Mapping[str, str]
            Read-only label mapping.
        """
        return self._obj.labels

    @property
    def driver(self) -> str:
        """Return the CSI driver name.

        Returns
        -------
        str
            Trimmed `driver` field.
        """
        return str(self._obj.payload.get("driver") or "").strip()

    @property
    def deletion_policy(self) -> str:
        """Return the snapshot deletion policy.

        Returns
        -------
        str
            Trimmed `deletionPolicy` field.
        """
        return str(self._obj.payload.get("deletionPolicy") or "").strip()


@dataclass(frozen=True)
class VolumeSnapshot:
    """Wrapper around one namespaced `VolumeSnapshot`.

    Parameters
    ----------
    _obj : CustomObject
        Generic custom object returned by the snapshot API.
    """

    _obj: CustomObject

    @classmethod
    def _from_object(cls, obj: CustomObject) -> Self:
        return cls(_obj=obj)

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        timeout: float,
    ) -> Self | None:
        """Read one `VolumeSnapshot` by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the snapshot.
        name : str
            Snapshot name.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        VolumeSnapshot | None
            Wrapped snapshot, or `None` if it does not exist.
        """
        obj = await _VOLUME_SNAPSHOT_CLIENT.get(
            kube,
            namespace=namespace,
            name=name,
            timeout=timeout,
        )
        return None if obj is None else cls._from_object(obj)

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        namespace: str,
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[Self]:
        """List namespaced `VolumeSnapshot` objects.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace to query.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Optional exact-match label selector.

        Returns
        -------
        list[VolumeSnapshot]
            Wrapped snapshots matching the selector.
        """
        objects = await _VOLUME_SNAPSHOT_CLIENT.list(
            kube,
            namespace=namespace,
            labels=labels,
            timeout=timeout,
        )
        return [cls._from_object(obj) for obj in objects]

    @classmethod
    async def create(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        source_claim: str,
        snapshot_class: str,
        timeout: float,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> Self:
        """Create one snapshot from a source PersistentVolumeClaim.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the snapshot and source claim.
        name : str
            Snapshot name.
        source_claim : str
            Source PersistentVolumeClaim name.
        snapshot_class : str
            VolumeSnapshotClass name to use.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Labels to apply to the snapshot.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to the snapshot.

        Returns
        -------
        VolumeSnapshot
            Wrapped created snapshot.
        """
        obj = await _VOLUME_SNAPSHOT_CLIENT.create(
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
        return cls._from_object(obj)

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this snapshot.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        namespace, name = self._require_namespace_name("delete VolumeSnapshot")
        await _VOLUME_SNAPSHOT_CLIENT.delete_by_name(
            kube,
            namespace=namespace,
            name=name,
            timeout=timeout,
        )

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this snapshot.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        VolumeSnapshot | None
            Fresh wrapper for the same snapshot, or `None` if deleted.
        """
        namespace, name = self._require_namespace_name("refresh VolumeSnapshot")
        return await type(self).get(
            kube,
            namespace=namespace,
            name=name,
            timeout=timeout,
        )

    async def wait_ready(self, kube: Kube, *, timeout: float) -> Self:
        """Wait until the snapshot reports `readyToUse`.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait budget in seconds.

        Returns
        -------
        VolumeSnapshot
            Fresh snapshot wrapper whose status is ready.

        Raises
        ------
        TimeoutError
            If readiness does not complete before `timeout`.
        """
        namespace, name = self._require_namespace_name("wait for VolumeSnapshot")

        async def ready(remaining: float) -> Self:
            live = await self.refresh(kube, timeout=remaining)
            if live is None:
                msg = f"VolumeSnapshot {namespace}/{name} disappeared before ready"
                raise OSError(msg)
            error = live.error_message
            if error:
                msg = f"VolumeSnapshot {namespace}/{name} failed: {error}"
                raise OSError(msg)
            if live.ready_to_use:
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

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this snapshot is deleted.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait budget in seconds.
        """
        namespace, name = self._require_namespace_name("wait for VolumeSnapshot")
        await _VOLUME_SNAPSHOT_CLIENT.wait_deleted(
            label=f"VolumeSnapshot {namespace}/{name}",
            timeout=timeout,
            refresh=lambda remaining: self.refresh(kube, timeout=remaining),
        )

    @property
    def name(self) -> str:
        """Return the snapshot name.

        Returns
        -------
        str
            Kubernetes `metadata.name`, or an empty string when unavailable.
        """
        return self._obj.name

    @property
    def namespace(self) -> str:
        """Return the snapshot namespace.

        Returns
        -------
        str
            Kubernetes `metadata.namespace`, or an empty string when unavailable.
        """
        return self._obj.namespace

    @property
    def labels(self) -> Mapping[str, str]:
        """Return snapshot labels.

        Returns
        -------
        Mapping[str, str]
            Read-only snapshot label mapping.
        """
        return self._obj.labels

    @property
    def created_at(self) -> datetime | None:
        """Return the snapshot creation timestamp.

        Returns
        -------
        datetime | None
            Snapshot status creation time, metadata creation time, or `None`.
        """
        value = self._obj.status.get("creationTime")
        if isinstance(value, str):
            return self._obj.parse_utc_datetime(value)
        return self._obj.created_at_utc

    @property
    def ready_to_use(self) -> bool:
        """Return whether this snapshot is ready to use.

        Returns
        -------
        bool
            Value of `status.readyToUse`.
        """
        return bool(self._obj.status.get("readyToUse"))

    @property
    def error_message(self) -> str:
        """Return the snapshot controller error message.

        Returns
        -------
        str
            Snapshot status error message, or an empty string.
        """
        error = self._obj.status.get("error")
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
        source = self._obj.spec.get("source")
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
        return str(self._obj.spec.get("volumeSnapshotClassName") or "").strip()

    def _require_namespace_name(self, action: str) -> tuple[str, str]:
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = f"cannot {action} with missing metadata.name/namespace"
            raise OSError(msg)
        return namespace, name
