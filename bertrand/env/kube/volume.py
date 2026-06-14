"""Wrappers for Kubernetes storage APIs and related operations."""

from __future__ import annotations

import re
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
from pathlib import PosixPath
from types import MappingProxyType
from typing import TYPE_CHECKING, Never

import kubernetes

from .api.client import Kube
from .api.resource import (
    KubeResource,
    cluster_resource,
    namespaced_resource,
)

if TYPE_CHECKING:
    from collections.abc import Collection, Mapping

    from bertrand.env.git import Deadline

PVC_GROW_RETRIES = 4
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


@cluster_resource(
    api=kubernetes.client.StorageV1Api,
    read=kubernetes.client.StorageV1Api.read_storage_class,
    list=kubernetes.client.StorageV1Api.list_storage_class,
    create=None,
    patch=kubernetes.client.StorageV1Api.patch_storage_class,
    delete=None,
)
@dataclass(frozen=True)
class StorageClass(
    KubeResource[kubernetes.client.V1StorageClass, Never],
):
    """General-purpose wrapper around one Kubernetes StorageClass object.

    Parameters
    ----------
    _obj : kubernetes.client.V1StorageClass
        Typed Kubernetes StorageClass payload returned by the cluster API.
    """

    _obj: kubernetes.client.V1StorageClass

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
class PersistentVolumeClaimManifest:
    """Desired state for one Kubernetes PersistentVolumeClaim."""

    namespace: str
    name: str
    access_modes: Collection[str]
    storage_class: str
    storage_request: str
    labels: Mapping[str, str] | None = None
    annotations: Mapping[str, str] | None = None
    data_source: Mapping[str, object] | None = None

    def manifest(self) -> Mapping[str, object]:
        """Render this desired state as a Kubernetes manifest.

        Returns
        -------
        Mapping[str, object]
            Kubernetes PersistentVolumeClaim manifest payload.
        """
        spec: dict[str, object] = {
            "accessModes": list(self.access_modes),
            "storageClassName": self.storage_class,
            "resources": {
                "requests": {
                    "storage": self.storage_request,
                },
            },
        }
        if self.data_source is not None:
            spec["dataSource"] = dict(self.data_source)
        return {
            "apiVersion": "v1",
            "kind": "PersistentVolumeClaim",
            "metadata": {
                "name": self.name,
                "namespace": self.namespace,
                "labels": dict(self.labels or {}),
                "annotations": dict(self.annotations or {}),
            },
            "spec": spec,
        }


@namespaced_resource(
    api=kubernetes.client.CoreV1Api,
    read=kubernetes.client.CoreV1Api.read_namespaced_persistent_volume_claim,
    list=kubernetes.client.CoreV1Api.list_namespaced_persistent_volume_claim,
    list_all=kubernetes.client.CoreV1Api.list_persistent_volume_claim_for_all_namespaces,
    create=kubernetes.client.CoreV1Api.create_namespaced_persistent_volume_claim,
    patch=kubernetes.client.CoreV1Api.patch_namespaced_persistent_volume_claim,
    delete=kubernetes.client.CoreV1Api.delete_namespaced_persistent_volume_claim,
)
@dataclass(frozen=True)
class PersistentVolumeClaim(
    KubeResource[
        kubernetes.client.V1PersistentVolumeClaim,
        PersistentVolumeClaimManifest,
    ],
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

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        intent: PersistentVolumeClaimManifest,
        deadline: Deadline,
    ) -> PersistentVolumeClaim:
        """Create a PVC if missing and converge its requested storage size.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        intent : PersistentVolumeClaimManifest
            Desired claim state. Snapshot data sources are not supported by this
            convergence API.
        deadline : Deadline
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        PersistentVolumeClaim
            Wrapped created, existing, or resized claim.

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or the API call fails.
        """
        if intent.data_source is not None:
            msg = "PVC upsert does not support data sources"
            raise OSError(msg)
        namespace, name, modes, storage_class, storage_request = _normalize_pvc_fields(
            operation="PVC upsert",
            namespace=intent.namespace,
            name=intent.name,
            access_modes=intent.access_modes,
            storage_class=intent.storage_class,
            storage_request=intent.storage_request,
        )
        try:
            return await cls.create(
                kube,
                intent=PersistentVolumeClaimManifest(
                    namespace=namespace,
                    name=name,
                    access_modes=modes,
                    storage_class=storage_class,
                    storage_request=storage_request,
                    labels=intent.labels,
                    annotations=intent.annotations,
                ),
                deadline=deadline,
            )
        except OSError as err:
            if not isinstance(err, Kube.APIError) or err.status != 409:
                raise

        live = await PersistentVolumeClaim.get(
            kube,
            namespace=namespace,
            deadline=deadline,
            name=name,
        )
        if live is None:
            msg = f"PVC {namespace}/{name} disappeared during upsert"
            raise OSError(msg)
        live._assert_upsert_compatible(
            storage_class=storage_class,
            access_modes=modes,
            labels=intent.labels,
            annotations=intent.annotations,
        )
        return await live._grow(
            storage_request,
            kube=kube,
            deadline=deadline,
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
        deadline: Deadline,
    ) -> PersistentVolumeClaim:
        live = await PersistentVolumeClaim.get(
            kube,
            namespace=namespace,
            deadline=deadline,
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
        deadline: Deadline,
    ) -> PersistentVolumeClaim:
        """Resize the PVC if current requested storage is below target.

        Parameters
        ----------
        requested : str
            Desired storage request.
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
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
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = "cannot resize PVC with missing metadata.name/namespace"
            raise OSError(msg)
        patch = {"spec": {"resources": {"requests": {"storage": requested}}}}
        for attempt in range(PVC_GROW_RETRIES):
            live = await type(self)._resize_target(
                kube,
                namespace=namespace,
                deadline=deadline,
                name=name,
            )
            current_size = parse_pvc_size(live.requested_storage)
            if current_size >= new_size:
                return live
            try:
                await kube.run(
                    lambda request_timeout: (
                        kubernetes.client.CoreV1Api(kube.client).patch_namespaced_persistent_volume_claim(
                            name=name,
                            namespace=namespace,
                            body=patch,
                            _request_timeout=request_timeout,
                        )
                    ),
                    deadline=deadline,
                    context=f"failed to patch PVC {name!r} during resize lifecycle",
                    missing_ok=False,
                )
            except OSError as err:
                if isinstance(err, Kube.APIError) and err.status == 404:
                    msg = f"PVC {name!r} disappeared during resize lifecycle"
                    raise OSError(msg) from err
                if (
                    isinstance(err, Kube.APIError)
                    and err.status == 409
                    and attempt + 1 < PVC_GROW_RETRIES
                ):
                    continue
                raise

            live = await type(self)._resize_target(
                kube,
                namespace=namespace,
                deadline=deadline,
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


@cluster_resource(
    api=kubernetes.client.CoreV1Api,
    read=kubernetes.client.CoreV1Api.read_persistent_volume,
    list=kubernetes.client.CoreV1Api.list_persistent_volume,
    create=None,
    patch=kubernetes.client.CoreV1Api.patch_persistent_volume,
    delete=None,
)
@dataclass(frozen=True)
class PersistentVolume(
    KubeResource[kubernetes.client.V1PersistentVolume, Never],
):
    """General-purpose wrapper around one Kubernetes PersistentVolume object.

    Parameters
    ----------
    _obj : kubernetes.client.V1PersistentVolume
        Typed Kubernetes PersistentVolume payload returned by the cluster API.
    """

    _obj: kubernetes.client.V1PersistentVolume

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
