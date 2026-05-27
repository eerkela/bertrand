"""Wrappers for Kubernetes storage APIs and related operations."""

from __future__ import annotations

import asyncio
import re
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
from pathlib import PosixPath
from types import MappingProxyType
from typing import TYPE_CHECKING, ClassVar, Self

import kubernetes

from bertrand.env.git import Deadline, until

from .api._helpers import (
    _is_conflict,
    _is_not_found,
    _validate_delete_status,
)
from .api.metadata import KubeMetadata, NamespacedKubeMetadata
from .api.resource import BuiltinResource, BuiltinResourceObject

if TYPE_CHECKING:
    from collections.abc import Collection, Mapping

    from .api.client import Kube

PVC_GROW_RETRIES = 4
VOLUME_WAIT_POLL_INTERVAL_SECONDS = 0.5
QUANTITY_RE = re.compile(r"^([0-9]+(?:\.[0-9]+)?)([A-Za-z]{0,2})$")
STORAGE_FACTORS: dict[str, Decimal] = {
    "": Decimal(1),
    "m": Decimal("0.001"),
    "k": Decimal(10) ** 3,
    "K": Decimal(10) ** 3,
    "M": Decimal(10) ** 6,
    "G": Decimal(10) ** 9,
    "T": Decimal(10) ** 12,
    "P": Decimal(10) ** 15,
    "E": Decimal(10) ** 18,
    "Ki": Decimal(2) ** 10,
    "Mi": Decimal(2) ** 20,
    "Gi": Decimal(2) ** 30,
    "Ti": Decimal(2) ** 40,
    "Pi": Decimal(2) ** 50,
    "Ei": Decimal(2) ** 60,
}


def _deadline_from_budget(seconds: float) -> Deadline:
    if seconds <= 0:
        return Deadline(
            expires_at=asyncio.get_running_loop().time(),
            timeout=seconds,
        )
    return Deadline.from_timeout(seconds, message="")


@dataclass(frozen=True)
class StorageClass(
    BuiltinResourceObject[kubernetes.client.V1StorageClass],
    KubeMetadata[kubernetes.client.V1StorageClass],
):
    """General-purpose wrapper around one Kubernetes StorageClass object.

    Parameters
    ----------
    _obj : kubernetes.client.V1StorageClass
        Typed Kubernetes StorageClass payload returned by the cluster API.
    """

    _obj: kubernetes.client.V1StorageClass

    resource: ClassVar[BuiltinResource[kubernetes.client.V1StorageClass]] = (
        BuiltinResource(
            scope="cluster",
            api="storage",
            kind="StorageClass",
            slug="storage_class",
            expected=kubernetes.client.V1StorageClass,
            list_type=kubernetes.client.V1StorageClassList,
        )
    )

    @classmethod
    async def select(
        cls,
        kube: Kube,
        *,
        timeout: float,
        preferences: Collection[str],
        require_expansion: bool = False,
    ) -> Self:
        """Select the first available StorageClass from a preference list.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        preferences : Collection[str]
            Ordered StorageClass names to try.
        require_expansion : bool, optional
            Whether the selected class must set `allowVolumeExpansion=true`.

        Returns
        -------
        StorageClass
            First available preferred StorageClass that satisfies the generic
            requirements.

        Raises
        ------
        ValueError
            If `preferences` is empty after normalization.
        OSError
            If Kubernetes returns malformed data or no preferred StorageClass
            satisfies the request.
        """
        normalized = tuple(
            name.strip() for name in preferences if name and name.strip()
        )
        if not normalized:
            msg = "storage class preferences cannot be empty"
            raise ValueError(msg)
        for name in normalized:
            storage = await cls.get(kube, timeout=timeout, name=name)
            if storage is None:
                continue
            if require_expansion and not storage.allow_volume_expansion:
                msg = f"storage class {name!r} must set allowVolumeExpansion=true"
                raise OSError(msg)
            return storage
        preferred = ", ".join(repr(name) for name in normalized)
        msg = f"no preferred StorageClass is available; expected one of {preferred}"
        raise OSError(msg)

    @property
    def provisioner(self) -> str:
        """Return the StorageClass provisioner.

        Returns
        -------
        str
            Trimmed provisioner name, or an empty string when unavailable.
        """
        return (self._obj.provisioner or "").strip()

    @property
    def allow_volume_expansion(self) -> bool:
        """Return whether volumes can expand dynamically.

        Returns
        -------
        bool
            Whether this StorageClass allows dynamic volume expansion.
        """
        return bool(self._obj.allow_volume_expansion)

    @property
    def is_cephfs(self) -> bool:
        """Return whether this StorageClass is CephFS-backed.

        Returns
        -------
        bool
            Whether this StorageClass uses a CephFS CSI provisioner.
        """
        provisioner = self.provisioner.lower()
        return "cephfs" in provisioner and "csi.ceph.com" in provisioner

    @property
    def parameters(self) -> Mapping[str, str]:
        """Return StorageClass parameters.

        Returns
        -------
        Mapping[str, str]
            StorageClass parameters, or an empty mapping when unavailable.
        """
        return MappingProxyType(self._obj.parameters or {})


def _normalize_pvc_fields(
    *,
    operation: str,
    namespace: str,
    name: str,
    access_modes: Collection[str],
    storage_class: str,
    storage_request: str,
    validate_size: bool = True,
) -> tuple[str, str, tuple[str, ...], str, str]:
    namespace = namespace.strip()
    name = name.strip()
    storage_class = storage_class.strip()
    storage_request = storage_request.strip()
    modes = tuple(mode.strip() for mode in access_modes if mode and mode.strip())
    if not namespace or not name:
        msg = f"{operation} requires non-empty namespace and name"
        raise OSError(msg)
    if not modes:
        msg = f"{operation} requires at least one access mode"
        raise OSError(msg)
    if not storage_class:
        msg = f"{operation} requires a non-empty storage class"
        raise OSError(msg)
    if not storage_request:
        msg = f"{operation} requires a non-empty storage request"
        raise OSError(msg)
    if validate_size:
        parse_pvc_size(storage_request)
    return namespace, name, modes, storage_class, storage_request


@dataclass(frozen=True)
class PersistentVolumeClaim(
    BuiltinResourceObject[kubernetes.client.V1PersistentVolumeClaim],
    NamespacedKubeMetadata[kubernetes.client.V1PersistentVolumeClaim],
):
    """General-purpose wrapper around one Kubernetes PersistentVolumeClaim object.

    Parameters
    ----------
    _obj : kubernetes.client.V1PersistentVolumeClaim
        Typed Kubernetes PersistentVolumeClaim payload returned by the cluster API.

    Notes
    -----
    The convergence API creates missing claims and resizes existing claims upward to
    the requested storage quantity. Raw manifests remain an internal implementation
    detail.
    """

    _obj: kubernetes.client.V1PersistentVolumeClaim

    resource: ClassVar[BuiltinResource[kubernetes.client.V1PersistentVolumeClaim]] = (
        BuiltinResource(
            scope="namespaced",
            api="core",
            kind="PersistentVolumeClaim",
            slug="persistent_volume_claim",
            expected=kubernetes.client.V1PersistentVolumeClaim,
            list_type=kubernetes.client.V1PersistentVolumeClaimList,
            can_create=True,
        )
    )

    @staticmethod
    def _manifest(
        *,
        namespace: str,
        name: str,
        access_modes: Collection[str],
        storage_class: str,
        storage_request: str,
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
        data_source: Mapping[str, object] | None = None,
    ) -> dict[str, object]:
        spec: dict[str, object] = {
            "accessModes": list(access_modes),
            "storageClassName": storage_class,
            "resources": {
                "requests": {
                    "storage": storage_request,
                },
            },
        }
        if data_source is not None:
            spec["dataSource"] = dict(data_source)
        return {
            "apiVersion": "v1",
            "kind": "PersistentVolumeClaim",
            "metadata": {
                "name": name,
                "namespace": namespace,
                "labels": dict(labels or {}),
                "annotations": dict(annotations or {}),
            },
            "spec": spec,
        }

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        access_modes: Collection[str],
        storage_class: str,
        storage_request: str,
        timeout: float,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> Self:
        """Create a PVC if missing and converge its requested storage size.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the claim.
        name : str
            Claim name to create or update.
        access_modes : Collection[str]
            Required claim access modes, for example `"ReadWriteOnce"`.
        storage_class : str
            Required StorageClass name.
        storage_request : str
            Desired storage quantity. Existing claims grow upward but are not shrunk.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Labels to apply to `metadata.labels` and validate on existing claims.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to `metadata.annotations` and validate on existing
            claims.

        Returns
        -------
        PersistentVolumeClaim
            Wrapped created, existing, or resized claim.

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or the API call fails.
        """
        namespace, name, modes, storage_class, storage_request = _normalize_pvc_fields(
            operation="PVC upsert",
            namespace=namespace,
            name=name,
            access_modes=access_modes,
            storage_class=storage_class,
            storage_request=storage_request,
        )
        deadline = _deadline_from_budget(timeout)
        manifest = cls._manifest(
            namespace=namespace,
            name=name,
            access_modes=modes,
            storage_class=storage_class,
            storage_request=storage_request,
            labels=labels,
            annotations=annotations,
        )

        try:
            return cls(
                _obj=await cls.resource.create(
                    kube,
                    namespace=namespace,
                    name=name,
                    manifest=manifest,
                    timeout=deadline.remaining(),
                    malformed_message=(
                        "malformed Kubernetes PVC payload while creating "
                        f"{namespace}/{name}"
                    ),
                    missing_ok=False,
                )
            )
        except OSError as err:
            if not _is_conflict(err):
                raise

        live = await cls.get(
            kube,
            namespace=namespace,
            timeout=deadline.remaining(),
            name=name,
        )
        if live is None:
            msg = f"PVC {namespace}/{name} disappeared during upsert"
            raise OSError(msg)
        live._assert_upsert_compatible(
            storage_class=storage_class,
            access_modes=modes,
            labels=labels,
            annotations=annotations,
        )
        return await live._grow(
            storage_request,
            kube=kube,
            timeout=deadline.remaining(),
        )

    @classmethod
    async def create_from_snapshot(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        access_modes: Collection[str],
        storage_class: str,
        storage_request: str,
        snapshot_name: str,
        timeout: float,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> Self:
        """Create a PVC restored from a `VolumeSnapshot`.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the claim and snapshot.
        name : str
            Claim name to create.
        access_modes : Collection[str]
            Required claim access modes, for example `"ReadWriteMany"`.
        storage_class : str
            Required StorageClass name.
        storage_request : str
            Requested storage quantity for the restored claim.
        snapshot_name : str
            Source `VolumeSnapshot` name.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Labels to apply to `metadata.labels`.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to `metadata.annotations`.

        Returns
        -------
        PersistentVolumeClaim
            Wrapped restored claim.

        Raises
        ------
        OSError
            If intent fields are empty, creation fails, or Kubernetes returns a
            malformed payload.
        """
        namespace, name, modes, storage_class, storage_request = _normalize_pvc_fields(
            operation="snapshot PVC creation",
            namespace=namespace,
            name=name,
            access_modes=access_modes,
            storage_class=storage_class,
            storage_request=storage_request,
            validate_size=False,
        )
        snapshot_name = snapshot_name.strip()
        if not snapshot_name:
            msg = "snapshot PVC creation requires a non-empty snapshot name"
            raise OSError(msg)
        parse_pvc_size(storage_request)

        manifest = cls._manifest(
            namespace=namespace,
            name=name,
            access_modes=modes,
            storage_class=storage_class,
            storage_request=storage_request,
            labels=labels,
            annotations=annotations,
            data_source={
                "apiGroup": "snapshot.storage.k8s.io",
                "kind": "VolumeSnapshot",
                "name": snapshot_name,
            },
        )
        return cls(
            _obj=await cls.resource.create(
                kube,
                namespace=namespace,
                name=name,
                manifest=manifest,
                timeout=timeout,
                context=(
                    "failed to create PersistentVolumeClaim "
                    f"{namespace}/{name} from VolumeSnapshot "
                    f"{snapshot_name!r}"
                ),
                missing_ok=False,
                malformed_message=(
                    "malformed Kubernetes PVC payload while creating "
                    f"{namespace}/{name} from VolumeSnapshot "
                    f"{snapshot_name!r}"
                ),
            )
        )

    def _assert_upsert_compatible(
        self,
        *,
        storage_class: str,
        access_modes: Collection[str],
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
    ) -> None:
        namespace = self.namespace
        name = self.name
        if self.storage_class_name != storage_class:
            msg = (
                f"PVC {namespace}/{name} uses storage class "
                f"{self.storage_class_name!r}, expected {storage_class!r}"
            )
            raise OSError(msg)
        actual_modes = set(self.access_modes)
        for mode in access_modes:
            if mode not in actual_modes:
                msg = f"PVC {namespace}/{name} is missing access mode {mode!r}"
                raise OSError(msg)
        actual_labels = self.labels
        for key, value in (labels or {}).items():
            if actual_labels.get(key) != value:
                msg = (
                    f"PVC {namespace}/{name} has mismatched label {key!r}: "
                    f"expected {value!r}, got {actual_labels.get(key)!r}"
                )
                raise OSError(msg)
        actual_annotations = self.annotations
        for key, value in (annotations or {}).items():
            if actual_annotations.get(key) != value:
                msg = (
                    f"PVC {namespace}/{name} has mismatched annotation {key!r}: "
                    f"expected {value!r}, got {actual_annotations.get(key)!r}"
                )
                raise OSError(msg)

    @classmethod
    async def _resize_target(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        timeout: float,
    ) -> Self:
        live = await cls.get(
            kube,
            namespace=namespace,
            timeout=timeout,
            name=name,
        )
        if live is None:
            msg = f"PVC {name!r} disappeared during resize lifecycle"
            raise OSError(msg)
        return live

    async def _grow(
        self,
        requested: str,
        *,
        kube: Kube,
        timeout: float,
    ) -> Self:
        """Resize the PVC if current requested storage is below target.

        Parameters
        ----------
        requested : str
            Desired storage request.
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        PersistentVolumeClaim
            Fresh wrapper for the converged claim.

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or the API call fails.
        """
        new_size = parse_pvc_size(requested)
        namespace, name = self._require_namespace_name("resize PVC")
        patch = {"spec": {"resources": {"requests": {"storage": requested}}}}
        deadline = _deadline_from_budget(timeout)

        for attempt in range(PVC_GROW_RETRIES):
            live = await type(self)._resize_target(
                kube,
                namespace=namespace,
                timeout=deadline.remaining(),
                name=name,
            )
            current_size = parse_pvc_size(live.requested_storage)
            if current_size >= new_size:
                return live
            try:
                await kube.run(
                    lambda request_timeout: (
                        kube.core.patch_namespaced_persistent_volume_claim(
                            name=name,
                            namespace=namespace,
                            body=patch,
                            _request_timeout=request_timeout,
                        )
                    ),
                    timeout=deadline.remaining(),
                    context=f"failed to patch PVC {name!r} during resize lifecycle",
                    missing_ok=False,
                )
            except OSError as err:
                if _is_not_found(err):
                    msg = f"PVC {name!r} disappeared during resize lifecycle"
                    raise OSError(msg) from err
                if _is_conflict(err) and attempt + 1 < PVC_GROW_RETRIES:
                    continue
                raise

            live = await type(self)._resize_target(
                kube,
                namespace=namespace,
                timeout=deadline.remaining(),
                name=name,
            )
            current_size = parse_pvc_size(live.requested_storage)
            if current_size >= new_size:
                return live
            if attempt + 1 >= PVC_GROW_RETRIES:
                msg = (
                    f"PVC {name!r} did not converge to requested size {requested!r} "
                    f"after {PVC_GROW_RETRIES} attempts"
                )
                raise OSError(msg)
        msg = f"PVC {name!r} did not converge to requested size {requested!r}"
        raise OSError(msg)

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this PVC from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        namespace, name = self._require_namespace_name("delete PVC")
        payload = await kube.run(
            lambda request_timeout: kube.core.delete_namespaced_persistent_volume_claim(
                name=name,
                namespace=namespace,
                body=kubernetes.client.V1DeleteOptions(),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete PVC {namespace}/{name}",
        )
        _validate_delete_status(
            payload, label=self._object_label(name=name, namespace=namespace)
        )

    async def wait_bound(self, kube: Kube, *, timeout: float) -> Self:
        """Wait until this PVC reaches a bound state.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        Returns
        -------
        PersistentVolumeClaim
            Fresh wrapper whose phase is `Bound`.

        Raises
        ------
        TimeoutError
            If the claim does not bind before `timeout`.
        """
        namespace, name = self._require_namespace_name("wait for PVC binding")

        async def bound(remaining: float) -> Self:
            live = await self.refresh(kube, timeout=remaining)
            if live is None:
                msg = f"PVC {name!r} disappeared before binding"
                raise OSError(msg)
            if live.is_bound:
                return live
            msg = f"PVC {namespace}/{name} is not bound yet"
            raise TimeoutError(msg)

        try:
            return await until(
                bound,
                timeout=timeout,
                interval=VOLUME_WAIT_POLL_INTERVAL_SECONDS,
                action=f"waiting for PVC {namespace}/{name} binding",
            )
        except TimeoutError as err:
            msg = f"timed out waiting for PVC {namespace}/{name} binding"
            raise TimeoutError(msg) from err

    @property
    def phase(self) -> str:
        """Return the PersistentVolumeClaim phase.

        Returns
        -------
        str
            Trimmed claim phase string, or an empty string when unavailable.
        """
        status = self._obj.status
        return (status.phase or "").strip() if status is not None else ""

    @property
    def is_bound(self) -> bool:
        """Return whether this claim is bound.

        Returns
        -------
        bool
            Whether this claim is bound to a PersistentVolume.
        """
        return self.phase == "Bound" and bool(self.volume_name)

    @property
    def volume_name(self) -> str:
        """Return the bound PersistentVolume name.

        Returns
        -------
        str
            Trimmed bound `spec.volumeName`, or an empty string when unavailable.
        """
        spec = self._obj.spec
        return (spec.volume_name or "").strip() if spec is not None else ""

    @property
    def requested_storage(self) -> str:
        """Return the requested storage quantity.

        Returns
        -------
        str
            Requested storage quantity from `spec.resources.requests`.

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or the API call fails.
        """
        spec = self._obj.spec or kubernetes.client.V1PersistentVolumeClaimSpec()
        resources = spec.resources or kubernetes.client.V1VolumeResourceRequirements()
        requests = resources.requests or {}
        value = str(requests.get("storage") or "").strip()
        if not value:
            msg = "PVC does not expose a valid storage request quantity"
            raise OSError(msg)
        return value

    @property
    def storage_class_name(self) -> str:
        """Return the claim StorageClass name.

        Returns
        -------
        str
            Trimmed storage class name, or an empty string when unavailable.
        """
        spec = self._obj.spec
        return (spec.storage_class_name or "").strip() if spec is not None else ""

    @property
    def access_modes(self) -> tuple[str, ...]:
        """Return the claim access modes.

        Returns
        -------
        tuple[str, ...]
            Immutable access mode tuple, preserving API order.
        """
        spec = self._obj.spec
        modes = (spec.access_modes or []) if spec is not None else []
        return tuple(mode.strip() for mode in modes if mode and mode.strip())

    def has_access_mode(self, mode: str) -> bool:
        """Return whether this claim declares an access mode.

        Parameters
        ----------
        mode : str
            Access mode to check.

        Returns
        -------
        bool
            Whether `mode` is present in `spec.accessModes`.
        """
        return mode.strip() in self.access_modes


@dataclass(frozen=True)
class PersistentVolume(
    BuiltinResourceObject[kubernetes.client.V1PersistentVolume],
    KubeMetadata[kubernetes.client.V1PersistentVolume],
):
    """General-purpose wrapper around one Kubernetes PersistentVolume object.

    Parameters
    ----------
    _obj : kubernetes.client.V1PersistentVolume
        Typed Kubernetes PersistentVolume payload returned by the cluster API.
    """

    _obj: kubernetes.client.V1PersistentVolume

    resource: ClassVar[BuiltinResource[kubernetes.client.V1PersistentVolume]] = (
        BuiltinResource(
            scope="cluster",
            api="core",
            kind="PersistentVolume",
            slug="persistent_volume",
            expected=kubernetes.client.V1PersistentVolume,
            list_type=kubernetes.client.V1PersistentVolumeList,
        )
    )

    @classmethod
    async def wait_present(
        cls,
        kube: Kube,
        *,
        timeout: float,
        name: str,
    ) -> Self:
        """Wait until a PersistentVolume exists by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.
        name : str
            PersistentVolume name to wait for.

        Returns
        -------
        PersistentVolume
            Wrapped PersistentVolume once present.

        Raises
        ------
        TimeoutError
            If the volume is still absent when `timeout` expires.
        """

        async def present(remaining: float) -> Self:
            live = await cls.get(kube, timeout=remaining, name=name)
            if live is not None:
                return live
            msg = f"PersistentVolume {name!r} is not present yet"
            raise TimeoutError(msg)

        try:
            return await until(
                present,
                timeout=timeout,
                interval=VOLUME_WAIT_POLL_INTERVAL_SECONDS,
                action=f"waiting for PersistentVolume {name!r}",
            )
        except TimeoutError as err:
            msg = f"timed out waiting for PersistentVolume {name!r}"
            raise TimeoutError(msg) from err

    @property
    def phase(self) -> str:
        """Return the PersistentVolume phase.

        Returns
        -------
        str
            Trimmed PersistentVolume phase string, or an empty string when unavailable.
        """
        status = self._obj.status
        return (status.phase or "").strip() if status is not None else ""

    @property
    def claim_identity(self) -> tuple[str, str] | None:
        """Return the bound claim identity.

        Returns
        -------
        tuple[str, str] | None
            `(namespace, claim_name)` from the claim reference, or `None` if absent.
        """
        spec = self._obj.spec
        claim_ref = spec.claim_ref if spec is not None else None
        if claim_ref is None:
            return None
        namespace = (claim_ref.namespace or "").strip()
        claim_name = (claim_ref.name or "").strip()
        if not namespace or not claim_name:
            return None
        return namespace, claim_name

    @property
    def csi_driver(self) -> str:
        """Return the CSI driver name.

        Returns
        -------
        str
            Trimmed CSI driver string, or an empty string when unavailable.
        """
        spec = self._obj.spec
        csi = spec.csi if spec is not None else None
        return (csi.driver or "").strip() if csi is not None else ""

    @property
    def csi_volume_attributes(self) -> Mapping[str, str]:
        """Return CSI volume attributes.

        Returns
        -------
        Mapping[str, str]
            CSI volume attributes, or an empty mapping when unavailable.
        """
        spec = self._obj.spec
        csi = spec.csi if spec is not None else None
        if csi is None or csi.volume_attributes is None:
            return MappingProxyType({})
        return MappingProxyType(csi.volume_attributes)

    @property
    def ceph_path(self) -> PosixPath | None:
        """Return the CephFS backing path.

        Returns
        -------
        PosixPath | None
            CephFS backing path from CSI attributes, or `None` if unavailable.
        """
        for key in ("subvolumePath", "rootPath", "path"):
            value = self.csi_volume_attributes.get(key, "").strip()
            if not value:
                continue
            return PosixPath(value if value.startswith("/") else f"/{value}")
        return None


def parse_pvc_size(value: str) -> Decimal:
    """Parse a Kubernetes PVC request size string into a decimal byte value.

    Parameters
    ----------
    value : str
        Kubernetes storage quantity such as `"4Gi"` or `"500M"`.

    Returns
    -------
    Decimal
        Parsed quantity in bytes using Kubernetes decimal and binary suffix rules.

    Raises
    ------
    ValueError
        If `value` is not a supported Kubernetes storage quantity.
    """
    match = QUANTITY_RE.fullmatch(value.strip())
    if not match:
        msg = f"invalid Kubernetes PVC request size: {value!r}"
        raise ValueError(msg)
    number, suffix = match.groups()
    factor = STORAGE_FACTORS.get(suffix)
    if factor is None:
        msg = (
            f"invalid Kubernetes memory unit for PVC request: {suffix!r} (options are "
            f"{', '.join(repr(s) for s in STORAGE_FACTORS)})"
        )
        raise ValueError(msg)
    try:
        return Decimal(number) * factor
    except (InvalidOperation, ValueError) as err:
        msg = f"invalid Kubernetes memory quantity for PVC request: {value!r}"
        raise ValueError(msg) from err
