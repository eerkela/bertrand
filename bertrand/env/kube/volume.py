"""Persistent Volume Claim (PVC) requests for Bertrand's environment bootstrapping
and caching mechanisms.
"""

from __future__ import annotations

import asyncio
import builtins
import hashlib
import json
import re
from collections.abc import Collection, Mapping
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
from pathlib import PosixPath
from typing import Self

import kubernetes

from ..config import RESOURCE_NAMES, Bertrand, Config, Resource
from ..config.core import KUBE_SANITIZE_RE, AbsolutePosixPath, KubeName, _check_uuid
from ..run import (
    BERTRAND_ENV,
    BERTRAND_NAMESPACE,
    ENV_ID_ENV,
    REPO_ID_ENV,
)
from .api import Kube, _label_selector
from .pod import Pod

CACHE_VOLUME_ENV: str = "BERTRAND_CACHE_VOLUME"
REPO_VOLUME_ENV: str = "BERTRAND_REPO_VOLUME"
DEFAULT_VOLUME_SIZE = "16Mi"
CEPHFS_STORAGE_CLASS_PREFERENCES: tuple[str, ...] = ("cephfs", "rook-cephfs")
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


@dataclass(frozen=True)
class StorageClass:
    """General-purpose wrapper around one Kubernetes StorageClass object.

    Parameters
    ----------
    obj : kubernetes.client.V1StorageClass
        Typed Kubernetes StorageClass payload returned by the cluster API.
    """

    obj: kubernetes.client.V1StorageClass

    @classmethod
    async def get(
        cls,
        *,
        kube: Kube,
        timeout: float,
        name: KubeName,
    ) -> Self | None:
        """Read one Kubernetes StorageClass by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.
        name : str
            StorageClass name to read.

        Returns
        -------
        StorageClass | None
            Validated wrapper, or `None` if missing.
        """
        payload = await kube.run(
            lambda request_timeout: kube.storage.read_storage_class(
                name=name,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to read StorageClass {name!r}",
        )
        if payload is None:
            return None
        if not isinstance(payload, kubernetes.client.V1StorageClass):
            raise OSError(f"malformed Kubernetes StorageClass payload for {name!r}")
        return cls(obj=payload)

    @classmethod
    async def list(
        cls,
        *,
        kube: Kube,
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[Self]:
        """List Kubernetes StorageClasses with optional label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.

        Returns
        -------
        list[StorageClass]
            Wrapped Kubernetes StorageClasses matching the requested filters.

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds `timeout`.
        OSError
            If Kubernetes returns malformed data or the list call fails.
        """
        payload = await kube.run(
            lambda request_timeout: kube.storage.list_storage_class(
                label_selector=_label_selector(labels),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context="failed to list Kubernetes StorageClasses",
        )
        if payload is None:
            return []
        if not isinstance(payload, kubernetes.client.V1StorageClassList):
            raise OSError("malformed Kubernetes StorageClass list payload")
        out: builtins.list[Self] = []
        for item in payload.items or []:
            if not isinstance(item, kubernetes.client.V1StorageClass):
                raise OSError("malformed Kubernetes StorageClass entry in list payload")
            out.append(cls(obj=item))
        return out

    @classmethod
    async def select(
        cls,
        *,
        kube: Kube,
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
        TimeoutError
            If any Kubernetes request exceeds `timeout`.
        OSError
            If no preferred StorageClass exists, expansion is required but missing,
            or Kubernetes returns malformed data.
        """
        normalized = tuple(name.strip() for name in preferences if name and name.strip())
        if not normalized:
            raise ValueError("storage class preferences cannot be empty")
        for name in normalized:
            storage = await cls.get(kube=kube, timeout=timeout, name=name)
            if storage is None:
                continue
            if require_expansion and not storage.allow_volume_expansion:
                raise OSError(f"storage class {name!r} must set allowVolumeExpansion=true")
            return storage
        preferred = ", ".join(repr(name) for name in normalized)
        raise OSError(f"no preferred StorageClass is available; expected one of {preferred}")

    @property
    def name(self) -> str:
        """
        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def provisioner(self) -> str:
        """
        Returns
        -------
        str
            Trimmed provisioner name, or an empty string when unavailable.
        """
        return (self.obj.provisioner or "").strip()

    @property
    def allow_volume_expansion(self) -> bool:
        """
        Returns
        -------
        bool
            Whether this StorageClass allows dynamic volume expansion.
        """
        return bool(self.obj.allow_volume_expansion)

    @property
    def is_cephfs(self) -> bool:
        """
        Returns
        -------
        bool
            Whether this StorageClass uses a CephFS CSI provisioner.
        """
        provisioner = self.provisioner.lower()
        return "cephfs" in provisioner and "csi.ceph.com" in provisioner

    @property
    def parameters(self) -> Mapping[str, str]:
        """
        Returns
        -------
        Mapping[str, str]
            StorageClass parameters, or an empty mapping when unavailable.
        """
        return self.obj.parameters or {}


@dataclass(frozen=True)
class PersistentVolumeClaim:
    """General-purpose wrapper around one Kubernetes PersistentVolumeClaim object.

    Parameters
    ----------
    obj : kubernetes.client.V1PersistentVolumeClaim
        Typed Kubernetes PersistentVolumeClaim payload returned by the cluster API.

    Notes
    -----
    The convergence API creates missing claims and resizes existing claims upward to
    the requested storage quantity. Raw manifests remain an internal implementation
    detail.
    """

    obj: kubernetes.client.V1PersistentVolumeClaim

    @classmethod
    async def get(
        cls,
        *,
        kube: Kube,
        namespace: str,
        timeout: float,
        name: KubeName,
    ) -> Self | None:
        """Read one Kubernetes PersistentVolumeClaim by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the claim.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        name : KubeName
            Claim name to read.

        Returns
        -------
        PersistentVolumeClaim | None
            Wrapped Kubernetes claim, or `None` if it does not exist.

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds `timeout`.
        OSError
            If Kubernetes returns malformed data or the API call fails.
        """
        payload = await kube.run(
            lambda request_timeout: kube.core.read_namespaced_persistent_volume_claim(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to read PVC {name!r} in namespace {namespace!r}",
        )
        if payload is None:
            return None
        if not isinstance(payload, kubernetes.client.V1PersistentVolumeClaim):
            raise OSError(
                f"malformed Kubernetes PVC payload for {name!r} in namespace {namespace!r}"
            )
        return cls(obj=payload)

    @classmethod
    async def list(
        cls,
        *,
        kube: Kube,
        timeout: float,
        namespaces: Collection[str] | None = None,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[Self]:
        """List Kubernetes PersistentVolumeClaims by namespace and label selectors.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.
        namespaces : Collection[str] | None, optional
            Optional namespace filters.  `None` queries all namespaces.  Otherwise,
            names are normalized (trimmed), deduplicated, and queried individually.
        labels : Mapping[str, str] | None, optional
            Optional label filters.
        """
        label_selector = _label_selector(labels)
        payloads: builtins.list[kubernetes.client.V1PersistentVolumeClaimList] = []
        if namespaces is None:
            payload = await kube.run(
                lambda request_timeout: kube.core.list_persistent_volume_claim_for_all_namespaces(
                    label_selector=label_selector,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context="failed to list PVCs across all namespaces",
            )
            if payload is not None:
                payloads.append(payload)
        else:
            normalized = {namespace.strip() for namespace in namespaces}
            normalized.discard("")
            if not normalized:
                return []
            for namespace in sorted(normalized):
                payload = await kube.run(
                    lambda request_timeout, namespace=namespace: (
                        kube.core.list_namespaced_persistent_volume_claim(
                            namespace=namespace,
                            label_selector=label_selector,
                            _request_timeout=request_timeout,
                        )
                    ),
                    timeout=timeout,
                    context=f"failed to list PVCs in namespace {namespace!r}",
                )
                if payload is not None:
                    payloads.append(payload)

        out: builtins.list[Self] = []
        for payload in payloads:
            if not isinstance(payload, kubernetes.client.V1PersistentVolumeClaimList):
                raise OSError("malformed Kubernetes PersistentVolumeClaim list payload")
            for item in payload.items or []:
                if not isinstance(item, kubernetes.client.V1PersistentVolumeClaim):
                    raise OSError(
                        "malformed Kubernetes PersistentVolumeClaim entry in list payload"
                    )
                out.append(cls(obj=item))
        return out

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
    ) -> dict[str, object]:
        return {
            "apiVersion": "v1",
            "kind": "PersistentVolumeClaim",
            "metadata": {
                "name": name,
                "namespace": namespace,
                "labels": dict(labels or {}),
                "annotations": dict(annotations or {}),
            },
            "spec": {
                "accessModes": list(access_modes),
                "storageClassName": storage_class,
                "resources": {
                    "requests": {
                        "storage": storage_request,
                    },
                },
            },
        }

    @classmethod
    async def upsert(
        cls,
        *,
        kube: Kube,
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
        ValueError
            If `storage_request` is not a valid Kubernetes quantity.
        TimeoutError
            If any Kubernetes request exceeds `timeout`.
        OSError
            If intent fields are empty, immutable existing claim fields differ, or
            Kubernetes create/patch/read operations fail.
        """
        namespace = namespace.strip()
        name = name.strip()
        storage_class = storage_class.strip()
        storage_request = storage_request.strip()
        modes = tuple(mode.strip() for mode in access_modes if mode and mode.strip())
        if not namespace or not name:
            raise OSError("PVC upsert requires non-empty namespace and name")
        if not modes:
            raise OSError("PVC upsert requires at least one access mode")
        if not storage_class:
            raise OSError("PVC upsert requires a non-empty storage class")
        if not storage_request:
            raise OSError("PVC upsert requires a non-empty storage request")
        parse_pvc_size(storage_request)

        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
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
            payload = await kube.run(
                lambda request_timeout: kube.core.create_namespaced_persistent_volume_claim(
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                timeout=deadline - loop.time(),
                context=f"failed to create PersistentVolumeClaim {namespace}/{name}",
            )
            if not isinstance(payload, kubernetes.client.V1PersistentVolumeClaim):
                raise OSError(f"malformed Kubernetes PVC payload while creating {namespace}/{name}")
            return cls(obj=payload)
        except OSError as err:
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise

        live = await cls.get(
            kube=kube,
            namespace=namespace,
            timeout=deadline - loop.time(),
            name=name,
        )
        if live is None:
            raise OSError(f"PVC {namespace}/{name} disappeared during upsert")
        live._assert_upsert_compatible(
            storage_class=storage_class,
            access_modes=modes,
            labels=labels,
            annotations=annotations,
        )
        return await live._grow(storage_request, kube=kube, timeout=deadline - loop.time())

    def _assert_upsert_compatible(
        self,
        *,
        storage_class: str,
        access_modes: Collection[str],
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
    ) -> None:
        identity = self.identity
        namespace, name = identity if identity is not None else ("", "")
        if self.storage_class_name != storage_class:
            raise OSError(
                f"PVC {namespace}/{name} uses storage class "
                f"{self.storage_class_name!r}, expected {storage_class!r}"
            )
        actual_modes = set(self.access_modes)
        for mode in access_modes:
            if mode not in actual_modes:
                raise OSError(f"PVC {namespace}/{name} is missing access mode {mode!r}")
        actual_labels = self.labels
        for key, value in (labels or {}).items():
            if actual_labels.get(key) != value:
                raise OSError(
                    f"PVC {namespace}/{name} has mismatched label {key!r}: "
                    f"expected {value!r}, got {actual_labels.get(key)!r}"
                )
        actual_annotations = self.annotations
        for key, value in (annotations or {}).items():
            if actual_annotations.get(key) != value:
                raise OSError(
                    f"PVC {namespace}/{name} has mismatched annotation {key!r}: "
                    f"expected {value!r}, got {actual_annotations.get(key)!r}"
                )

    async def _grow(
        self,
        requested: str,
        *,
        kube: Kube,
        timeout: float,
    ) -> Self:
        """Resize the PVC if current requested storage is below target."""
        new_size = parse_pvc_size(requested)
        identity = self.identity
        if identity is None:
            raise OSError("cannot resize PVC with missing metadata.name/namespace")
        namespace, name = identity
        patch = {"spec": {"resources": {"requests": {"storage": requested}}}}
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout

        for attempt in range(PVC_GROW_RETRIES):
            live = await type(self).get(
                kube=kube,
                namespace=namespace,
                timeout=deadline - loop.time(),
                name=name,
            )
            if live is None:
                raise OSError(f"PVC {name!r} disappeared during resize lifecycle")
            current_size = parse_pvc_size(live.requested_storage)
            if current_size >= new_size:
                return live
            try:
                await kube.run(
                    lambda request_timeout: kube.core.patch_namespaced_persistent_volume_claim(
                        name=name,
                        namespace=namespace,
                        body=patch,
                        _request_timeout=request_timeout,
                    ),
                    timeout=deadline - loop.time(),
                    context=f"failed to patch PVC {name!r} during resize lifecycle",
                )
            except OSError as err:
                detail = str(err).lower()
                if "status 404" in detail or "not found" in detail:
                    raise OSError(f"PVC {name!r} disappeared during resize lifecycle") from err
                if (
                    "status 409" in detail
                    or "conflict" in detail
                    or "the object has been modified" in detail
                ) and attempt + 1 < PVC_GROW_RETRIES:
                    continue
                raise

            live = await type(self).get(
                kube=kube,
                namespace=namespace,
                timeout=deadline - loop.time(),
                name=name,
            )
            if live is None:
                raise OSError(f"PVC {name!r} disappeared during resize lifecycle")
            current_size = parse_pvc_size(live.requested_storage)
            if current_size >= new_size:
                return live
            if attempt + 1 >= PVC_GROW_RETRIES:
                raise OSError(
                    f"PVC {name!r} did not converge to requested size {requested!r} "
                    f"after {PVC_GROW_RETRIES} attempts"
                )
        raise OSError(f"PVC {name!r} did not converge to requested size {requested!r}")

    async def delete(self, *, kube: Kube, timeout: float) -> None:
        """Delete this PVC from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Raises
        ------
        OSError
            If this wrapper is missing identity or the delete request fails.
        TimeoutError
            If the Kubernetes request exceeds `timeout`.
        """
        identity = self.identity
        if identity is None:
            raise OSError("cannot delete PVC with missing metadata.name/namespace")
        namespace, name = identity
        await kube.run(
            lambda request_timeout: kube.core.delete_namespaced_persistent_volume_claim(
                name=name,
                namespace=namespace,
                body=kubernetes.client.V1DeleteOptions(),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete PVC {name!r}",
        )

    async def refresh(self, *, kube: Kube, timeout: float) -> Self | None:
        """Re-read this PVC by identity.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        PersistentVolumeClaim | None
            Fresh wrapper for the same claim, or `None` if it no longer exists.

        Raises
        ------
        OSError
            If this wrapper is missing identity or Kubernetes returns malformed data.
        TimeoutError
            If the Kubernetes request exceeds `timeout`.
        """
        identity = self.identity
        if identity is None:
            raise OSError("cannot refresh PVC with missing metadata.name/namespace")
        namespace, name = identity
        return await type(self).get(
            kube=kube,
            namespace=namespace,
            timeout=timeout,
            name=name,
        )

    async def wait_bound(self, *, kube: Kube, timeout: float) -> Self:
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
        OSError
            If this wrapper is missing identity or the claim disappears.
        TimeoutError
            If the claim does not bind before `timeout`.
        """
        identity = self.identity
        if identity is None:
            raise OSError("cannot wait for bound state of PVC with missing metadata.name/namespace")
        namespace, name = identity
        if timeout <= 0:
            raise TimeoutError(f"timed out waiting for PVC {namespace}/{name} binding")
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                raise TimeoutError(f"timed out waiting for PVC {namespace}/{name} binding")
            live = await self.refresh(kube=kube, timeout=remaining)
            if live is None:
                raise OSError(f"PVC {name!r} disappeared before binding")
            if live.is_bound:
                return live
            await asyncio.sleep(min(VOLUME_WAIT_POLL_INTERVAL_SECONDS, remaining))

    async def wait_deleted(self, *, kube: Kube, timeout: float) -> None:
        """Wait until this PVC is deleted.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        Raises
        ------
        OSError
            If this wrapper is missing identity or Kubernetes returns malformed data.
        TimeoutError
            If the claim still exists when `timeout` expires.
        """
        identity = self.identity
        if identity is None:
            raise OSError("cannot wait for deletion of PVC with missing metadata.name/namespace")
        namespace, name = identity
        if timeout <= 0:
            raise TimeoutError(f"timed out waiting for PVC {namespace}/{name} deletion")
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                raise TimeoutError(f"timed out waiting for PVC {namespace}/{name} deletion")
            live = await self.refresh(kube=kube, timeout=remaining)
            if live is None:
                return
            await asyncio.sleep(min(VOLUME_WAIT_POLL_INTERVAL_SECONDS, remaining))

    @property
    def name(self) -> str:
        """
        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def namespace(self) -> str:
        """
        Returns
        -------
        str
            Trimmed `metadata.namespace`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.namespace or "").strip() if metadata is not None else ""

    @property
    def identity(self) -> tuple[str, str] | None:
        """
        Returns
        -------
        tuple[str, str] | None
            `(namespace, name)` when both components are available, otherwise `None`.
        """
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            return None
        return namespace, name

    @property
    def labels(self) -> Mapping[str, str]:
        """
        Returns
        -------
        Mapping[str, str]
            Claim labels, or an empty mapping when unavailable.
        """
        metadata = self.obj.metadata
        return metadata.labels or {} if metadata is not None else {}

    @property
    def annotations(self) -> Mapping[str, str]:
        """
        Returns
        -------
        Mapping[str, str]
            Claim annotations, or an empty mapping when unavailable.
        """
        metadata = self.obj.metadata
        return metadata.annotations or {} if metadata is not None else {}

    @property
    def phase(self) -> str:
        """
        Returns
        -------
        str
            Trimmed claim phase string, or an empty string when unavailable.
        """
        status = self.obj.status
        return (status.phase or "").strip() if status is not None else ""

    @property
    def is_bound(self) -> bool:
        """
        Returns
        -------
        bool
            Whether this claim is bound to a PersistentVolume.
        """
        return self.phase == "Bound" and bool(self.volume_name)

    @property
    def volume_name(self) -> str:
        """
        Returns
        -------
        str
            Trimmed bound `spec.volumeName`, or an empty string when unavailable.
        """
        spec = self.obj.spec
        return (spec.volume_name or "").strip() if spec is not None else ""

    @property
    def requested_storage(self) -> str:
        """
        Returns
        -------
        str
            Requested storage quantity from `spec.resources.requests`.

        Raises
        ------
        OSError
            If the claim does not expose a storage request.
        """
        spec = self.obj.spec or kubernetes.client.V1PersistentVolumeClaimSpec()
        resources = spec.resources or kubernetes.client.V1VolumeResourceRequirements()
        requests = resources.requests or {}
        value = str(requests.get("storage") or "").strip()
        if not value:
            raise OSError("PVC does not expose a valid storage request quantity")
        return value

    @property
    def storage_class_name(self) -> str:
        """
        Returns
        -------
        str
            Trimmed storage class name, or an empty string when unavailable.
        """
        spec = self.obj.spec
        return (spec.storage_class_name or "").strip() if spec is not None else ""

    @property
    def access_modes(self) -> tuple[str, ...]:
        """
        Returns
        -------
        tuple[str, ...]
            Immutable access mode tuple, preserving API order.
        """
        spec = self.obj.spec
        modes = (spec.access_modes or []) if spec is not None else []
        return tuple(mode.strip() for mode in modes if mode and mode.strip())


@dataclass(frozen=True)
class PersistentVolume:
    """General-purpose wrapper around one Kubernetes PersistentVolume object.

    Parameters
    ----------
    obj : kubernetes.client.V1PersistentVolume
        Typed Kubernetes PersistentVolume payload returned by the cluster API.
    """

    obj: kubernetes.client.V1PersistentVolume

    @classmethod
    async def get(
        cls,
        *,
        kube: Kube,
        timeout: float,
        name: KubeName,
    ) -> Self | None:
        """Read one Kubernetes PersistentVolume by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        name : KubeName
            PersistentVolume name to read.

        Returns
        -------
        PersistentVolume | None
            Wrapped Kubernetes PersistentVolume, or `None` if it does not exist.

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds `timeout`.
        OSError
            If Kubernetes returns malformed data or the API call fails.
        """
        payload = await kube.run(
            lambda request_timeout: kube.core.read_persistent_volume(
                name=name,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to read PersistentVolume {name!r}",
        )
        if payload is None:
            return None
        if not isinstance(payload, kubernetes.client.V1PersistentVolume):
            raise OSError(f"malformed Kubernetes PersistentVolume payload for {name!r}")
        return cls(obj=payload)

    @classmethod
    async def list(
        cls,
        *,
        kube: Kube,
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[Self]:
        """List Kubernetes PersistentVolumes with optional label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.

        Returns
        -------
        list[PersistentVolume]
            Wrapped Kubernetes PersistentVolumes matching the requested filters.

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds `timeout`.
        OSError
            If Kubernetes returns malformed data or the list call fails.
        """
        payload = await kube.run(
            lambda request_timeout: kube.core.list_persistent_volume(
                label_selector=_label_selector(labels),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context="failed to list Kubernetes PersistentVolumes",
        )
        if payload is None:
            return []
        if not isinstance(payload, kubernetes.client.V1PersistentVolumeList):
            raise OSError("malformed Kubernetes PersistentVolume list payload")
        out: builtins.list[Self] = []
        for item in payload.items or []:
            if not isinstance(item, kubernetes.client.V1PersistentVolume):
                raise OSError("malformed Kubernetes PersistentVolume entry in list payload")
            out.append(cls(obj=item))
        return out

    async def refresh(self, *, kube: Kube, timeout: float) -> Self | None:
        """Re-read this PersistentVolume by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        PersistentVolume | None
            Fresh wrapper for the same volume, or `None` if it no longer exists.

        Raises
        ------
        OSError
            If this wrapper is missing `metadata.name` or Kubernetes returns malformed
            data.
        TimeoutError
            If the Kubernetes request exceeds `timeout`.
        """
        name = self.name
        if not name:
            raise OSError("cannot refresh PersistentVolume with missing metadata.name")
        return await type(self).get(kube=kube, timeout=timeout, name=name)

    @classmethod
    async def wait_present(
        cls,
        *,
        kube: Kube,
        timeout: float,
        name: KubeName,
    ) -> Self:
        """Wait until a PersistentVolume exists by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.
        name : KubeName
            PersistentVolume name to wait for.

        Returns
        -------
        PersistentVolume
            Wrapped PersistentVolume once present.

        Raises
        ------
        TimeoutError
            If the volume is still absent when `timeout` expires.
        OSError
            If Kubernetes returns malformed data.
        """
        if timeout <= 0:
            raise TimeoutError(f"timed out waiting for PersistentVolume {name!r}")
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                raise TimeoutError(f"timed out waiting for PersistentVolume {name!r}")
            live = await cls.get(kube=kube, timeout=remaining, name=name)
            if live is not None:
                return live
            await asyncio.sleep(min(VOLUME_WAIT_POLL_INTERVAL_SECONDS, remaining))

    @property
    def name(self) -> str:
        """
        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def phase(self) -> str:
        """
        Returns
        -------
        str
            Trimmed PersistentVolume phase string, or an empty string when unavailable.
        """
        status = self.obj.status
        return (status.phase or "").strip() if status is not None else ""

    @property
    def claim_identity(self) -> tuple[str, str] | None:
        """
        Returns
        -------
        tuple[str, str] | None
            `(namespace, claim_name)` from the claim reference, or `None` if absent.
        """
        spec = self.obj.spec
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
        """
        Returns
        -------
        str
            Trimmed CSI driver string, or an empty string when unavailable.
        """
        spec = self.obj.spec
        csi = spec.csi if spec is not None else None
        return (csi.driver or "").strip() if csi is not None else ""

    @property
    def csi_volume_attributes(self) -> Mapping[str, str]:
        """
        Returns
        -------
        Mapping[str, str]
            CSI volume attributes, or an empty mapping when unavailable.
        """
        spec = self.obj.spec
        csi = spec.csi if spec is not None else None
        return csi.volume_attributes or {} if csi is not None else {}

    @property
    def ceph_path(self) -> PosixPath | None:
        """
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
        raise ValueError(f"invalid Kubernetes PVC request size: {value!r}")
    number, suffix = match.groups()
    factor = STORAGE_FACTORS.get(suffix)
    if factor is None:
        raise ValueError(
            f"invalid Kubernetes memory unit for PVC request: {suffix!r} (options are "
            f"{', '.join(repr(s) for s in STORAGE_FACTORS)})"
        )
    try:
        return Decimal(number) * factor
    except (InvalidOperation, ValueError) as err:
        raise ValueError(f"invalid Kubernetes memory quantity for PVC request: {value!r}") from err


@dataclass(frozen=True)
class CacheVolume:
    """Structured metadata for a cache volume declaration from a resource."""

    name: KubeName
    target: AbsolutePosixPath

    @classmethod
    async def from_config(cls, config: Config, tag: str, env_id: str) -> list[Self]:
        """Collect and validate cache volume specifications for a build tag.

        Parameters
        ----------
        config : Config
            Active configuration context with resolved resources and registry.
        tag : str
            Active build tag used to query each resource's volume declarations.
        env_id : str
            Canonical environment UUID used for volume name derivation and collision
            checks.

        Returns
        -------
        list[CacheVolume]
            Deterministically ordered list of `CacheVolume` objects.  Each object
            contains the volume name, which can be used to identify the corresponding
            PVC in the cluster, and the target path, which is the absolute container
            path where the volume should be mounted. This information can be written to
            `Containerfile` and workload specs.

        Raises
        ------
        OSError
            If resource volume hooks fail, return invalid types, contain invalid
            targets, or produce non-serializable fingerprint payloads.

        Notes
        -----
        Names are derived as stable hashes over each volume's semantic fingerprint plus
        target path. Target collisions across resources are rejected.
        """
        env_id = _check_uuid(env_id)
        volumes: list[Self] = []
        target_owner: dict[str, str] = {}

        # scan over all resources associated with this environment
        for name in sorted(config.resources):
            resource = RESOURCE_NAMES[name]
            try:
                declared = await resource.volumes(config, tag)
            except Exception as err:
                raise OSError(
                    f"failed to resolve cache volumes for resource '{resource.name}': {err}"
                ) from err
            if not isinstance(declared, list):
                raise OSError(
                    f"volume hook for resource '{resource.name}' must return a list, "
                    f"got {type(declared).__name__}"
                )

            # collect volume requests for each resource and check for collisions
            for raw in declared:
                if not isinstance(raw, Resource.Volume):
                    raise OSError(
                        f"volume hook for resource '{resource.name}' must return "
                        f"`Resource.Volume` entries, got {type(raw).__name__}"
                    )
                target = raw.target
                if not target.is_absolute():
                    raise OSError(
                        f"resource '{resource.name}' mount target must be absolute: {target}"
                    )
                if any(part in (".", "..") for part in target.parts):
                    raise OSError(
                        f"resource '{resource.name}' mount target cannot contain '.' "
                        f"or '..' segments: {target}"
                    )
                target_key = target.as_posix()
                owner = target_owner.setdefault(target_key, resource.name)
                if owner != resource.name:
                    raise OSError(
                        f"volume target collision at '{target_key}' between resources "
                        f"'{owner}' and '{resource.name}'"
                    )

                # compute semantic hash
                try:
                    payload = {
                        "env_id": env_id,
                        "fingerprint": dict(raw.fingerprint),
                        "target": target_key,
                    }
                    text = json.dumps(
                        payload,
                        sort_keys=True,
                        separators=(",", ":"),
                        ensure_ascii=False,
                        allow_nan=False,
                    )
                    digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
                except (TypeError, ValueError) as err:
                    raise OSError(
                        f"resource '{resource.name}' mount '{target_key}' has invalid "
                        f"fingerprint payload: {err}"
                    ) from err

                # derive a stable volume name
                volume_name = KUBE_SANITIZE_RE.sub(
                    "-",
                    f"bertrand-cache-{resource.name}-{digest}",
                ).strip("-")
                volumes.append(cls(name=volume_name, target=target))

        volumes.sort(key=lambda mv: (mv.name, mv.target))
        return volumes

    @staticmethod
    def _assert_managed_cache(
        pvc: PersistentVolumeClaim,
        *,
        claim_name: str,
        env_id: str,
        storage_class: str | None,
        require_rwo: bool,
    ) -> None:
        meta = pvc.obj.metadata
        labels = (meta.labels or {}) if meta is not None else {}
        spec = pvc.obj.spec
        storage_class_name = spec.storage_class_name if spec is not None else None
        access_modes = (spec.access_modes or []) if spec is not None else []
        if labels.get(BERTRAND_ENV) != "1" or labels.get(CACHE_VOLUME_ENV) != "1":
            raise OSError(
                f"cluster PVC {claim_name!r} has missing required labels "
                f"{BERTRAND_ENV!r}=1 and {CACHE_VOLUME_ENV!r}=1"
            )
        actual_env_id = labels.get(ENV_ID_ENV)
        if actual_env_id != env_id:
            raise OSError(
                f"cluster PVC {claim_name!r} has mismatched environment identity label "
                f"{ENV_ID_ENV!r}: expected {env_id!r}, got {actual_env_id!r}"
            )
        if storage_class is not None and storage_class_name != storage_class:
            raise OSError(
                f"cluster PVC {claim_name!r} uses storage class "
                f"{storage_class_name!r}, expected {storage_class!r}"
            )
        if require_rwo and "ReadWriteOnce" not in access_modes:
            raise OSError(f"cluster PVC {claim_name!r} must include ReadWriteOnce access mode")

    @classmethod
    async def ensure(
        cls,
        config: Config,
        tag: str,
        env_id: str,
        *,
        timeout: float,
        storage_class: str,
        size_request: str,
    ) -> None:
        """Ensure deterministic cache PVCs exist for one build tag.

        Parameters
        ----------
        config : Config
            Active configuration context.
        tag : str
            Active build tag used to resolve requested cache volumes.
        env_id : str
            Canonical environment UUID used for managed PVC labels.
        timeout : float
            Maximum runtime command timeout in seconds.  If infinite, wait
            indefinitely.
        storage_class : str
            StorageClass name used for claim creation and validation.
        size_request : str
            Requested PVC storage quantity for new claims and resize checks.

        Returns
        -------
        None
            This function executes for side effects only.

        Raises
        ------
        ValueError
            If `env_id`, `storage_class`, or `size_request` is empty, or if any of the
            PVC payloads fail validation checks.
        TimeoutError
            If `timeout` is negative or if any kube API calls exceed the timeout.
        CommandError
            If any kube API call fails.

        Notes
        -----
        This function assumes kube API reachability is ensured by the caller. It does not
        call `start_microk8s()`.
        """
        env_id = _check_uuid(env_id)
        if timeout <= 0:
            raise TimeoutError("timeout must be non-negative")
        storage_class = storage_class.strip()
        if not storage_class:
            raise ValueError("storage class cannot be empty")
        size_request = size_request.strip()
        if not size_request:
            raise ValueError("size request cannot be empty")
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        with await Kube.host(timeout=deadline - loop.time()) as kube:
            # get PVC storage class and assert that it supports volume expansion
            storage = await StorageClass.select(
                kube=kube,
                timeout=deadline - loop.time(),
                preferences=(storage_class,),
                require_expansion=True,
            )
            storage_name = storage.name

            # get/create PVCs for each of this tag's cache volumes
            for volume in await CacheVolume.from_config(config, tag, env_id):
                pvc = await PersistentVolumeClaim.upsert(
                    kube=kube,
                    namespace=BERTRAND_NAMESPACE,
                    name=volume.name,
                    access_modes=("ReadWriteOnce",),
                    storage_class=storage_name,
                    storage_request=size_request,
                    labels={
                        BERTRAND_ENV: "1",
                        CACHE_VOLUME_ENV: "1",
                        ENV_ID_ENV: env_id,
                    },
                    timeout=deadline - loop.time(),
                )
                cls._assert_managed_cache(
                    pvc,
                    claim_name=volume.name,
                    env_id=env_id,
                    storage_class=storage_class,
                    require_rwo=True,
                )

    @classmethod
    async def gc(cls, config: Config, env_id: str, *, timeout: float) -> None:
        """Garbage-collect stale labeled cache PVCs for an environment.

        Parameters
        ----------
        config : Config
            Active configuration context.
        env_id : str
            Canonical environment UUID used to scope labeled cache PVCs.
        timeout : float
            Maximum runtime command timeout in seconds.  If infinite, wait
            indefinitely.

        Returns
        -------
        None
            This function executes for side effects only.

        Raises
        ------
        ValueError
            If `env_id` is empty or any PVC payloads fail validation checks.
        TimeoutError
            If `timeout` is negative or if any kube API calls exceed the timeout.
        CommandError
            If any kube API call fails.

        Notes
        -----
        Only Bertrand-labeled cache PVCs for this environment are candidates. Claims
        currently referenced by active pods are never deleted.
        """
        env_id = _check_uuid(env_id)
        if timeout <= 0:
            raise TimeoutError("timeout must be non-negative")

        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        bertrand = config.get(Bertrand)
        if bertrand is None:
            return
        with await Kube.host(timeout=deadline - loop.time()) as kube:
            # get all PVCs associated with this environment
            actual = await PersistentVolumeClaim.list(
                kube=kube,
                namespaces=(BERTRAND_NAMESPACE,),
                timeout=deadline - loop.time(),
                labels={BERTRAND_ENV: "1", CACHE_VOLUME_ENV: "1", ENV_ID_ENV: env_id},
            )
            if not actual:
                return  # no volumes to clean up

            # get PVCs with active pods
            active = {
                claim_name
                for pod in await Pod.list(
                    kube=kube,
                    namespaces=(BERTRAND_NAMESPACE,),
                    timeout=deadline - loop.time(),
                    labels={BERTRAND_ENV: "1", ENV_ID_ENV: env_id},
                )
                if pod.is_active
                for claim_name in pod.persistent_volume_claim_names
            }
            active.discard("")

            # get expected PVCs for this environment based on current semantic hash
            expected = {
                volume.name
                for tag in bertrand.build
                for volume in await CacheVolume.from_config(config, tag, env_id)
            }

            # delete actual claims whose names are not in the expected and active sets
            stale = [
                pvc
                for pvc in actual
                if (
                    pvc.obj.metadata is not None
                    and pvc.obj.metadata.name
                    and pvc.obj.metadata.name not in expected
                    and pvc.obj.metadata.name not in active
                )
            ]
            for pvc in stale:
                metadata = pvc.obj.metadata
                if metadata is None or not metadata.name:
                    continue
                claim_name = metadata.name
                cls._assert_managed_cache(
                    pvc,
                    claim_name=claim_name,
                    env_id=env_id,
                    storage_class=None,
                    require_rwo=False,
                )
                await pvc.delete(kube=kube, timeout=deadline - loop.time())


async def format_volumes(config: Config, tag: str, env_id: str) -> list[str]:
    """Render legacy `nerdctl` volume flags for configured cache volumes."""
    flags: list[str] = []
    for volume in await CacheVolume.from_config(config, tag, env_id):
        flags.extend(["-v", f"{volume.name}:{volume.target.as_posix()}"])
    return flags


@dataclass(frozen=True)
class RepoVolume:
    """Structured metadata for a CephFS-backed repository volume in the local cluster.

    One of these volumes will be created whenever Bertrand initializes a new project.
    The resulting volume will be mounted to a private directory on the host system,
    and a symlink to the mounted directory will be placed at the initialized path.
    The volume itself will always store a single git repository together with one or
    more worktrees, along with any extra metadata needed to manage the volume itself.

    Attributes
    ----------
    repo_id : str
        Stable repository identity used for volume naming and management.  This is a
        UUID hex string that is generated during repository initialization and stored
        in the repository's metadata for the lifetime of the volume.
    pvc : PersistentVolumeClaim
        Kubernetes PVC object representing the claim for this repository volume in the
        cluster.
    """

    repo_id: str
    pvc: PersistentVolumeClaim

    @staticmethod
    def _kube_name(repo_id: str) -> str:
        repo_id = _check_uuid(repo_id)
        h = hashlib.sha256()
        encoded: bytes = repo_id.encode("utf-8")
        h.update(len(encoded).to_bytes(8, "big"))
        h.update(encoded)
        return f"bertrand-repo-{h.hexdigest()}"

    @staticmethod
    def _assert_managed_pvc(
        pvc: PersistentVolumeClaim,
        *,
        claim_name: str,
        repo_id: str,
        storage_class: str | None,
        require_rwx: bool,
    ) -> None:
        meta = pvc.obj.metadata
        actual_name = meta.name if meta is not None else None
        if actual_name != claim_name:
            raise OSError(
                f"cluster PVC for repo {repo_id!r} has non-deterministic claim name "
                f"{actual_name!r}, expected {claim_name!r}"
            )
        labels = (meta.labels or {}) if meta is not None else {}
        spec = pvc.obj.spec
        storage_class_name = spec.storage_class_name if spec is not None else None
        access_modes = (spec.access_modes or []) if spec is not None else []
        if labels.get(BERTRAND_ENV) != "1" or labels.get(REPO_VOLUME_ENV) != "1":
            raise OSError(
                f"cluster PVC {claim_name!r} collides with Bertrand volume claim, but "
                "is not managed by Bertrand"
            )
        actual_repo_id = labels.get(REPO_ID_ENV)
        if actual_repo_id != repo_id:
            raise OSError(
                f"cluster PVC {claim_name!r} has mismatched repo identity label "
                f"{REPO_ID_ENV!r}: expected {repo_id!r}, got {actual_repo_id!r}"
            )
        if storage_class is not None and storage_class_name != storage_class:
            raise OSError(
                f"cluster PVC {claim_name!r} uses storage class "
                f"{storage_class_name!r}, expected {storage_class!r}"
            )
        if require_rwx and "ReadWriteMany" not in access_modes:
            raise OSError(f"cluster PVC {claim_name!r} must include ReadWriteMany access mode")

    @classmethod
    async def create(
        cls,
        repo_id: str,
        *,
        timeout: float,
        size_request: str,
    ) -> Self:
        """Ensure a deterministic, cluster-wide RWX claim exists for one repository
        identity.

        Parameters
        ----------
        repo_id : str
            Stable, caller-provided repository identity used for deterministic claim
            names.
        timeout : float
            Maximum runtime command timeout in seconds.  If infinite, wait
            indefinitely.
        size_request : str
            Requested storage quantity for initial creation and resize checks.

        Returns
        -------
        RepoVolume
            Structured metadata for the created/grown repository volume claim.

        Raises
        ------
        ValueError
            If `repo_id` or `size_request` is empty, or if the PVC payload fails
            validation checks.
        TimeoutError
            If `timeout` is negative or if any kube API calls exceed the timeout.
        CommandError
            If any kube API call fails.
        """
        repo_id = _check_uuid(repo_id)
        if timeout <= 0:
            raise TimeoutError("timeout must be non-negative")
        size_request = size_request.strip()
        if not size_request:
            raise ValueError("size request cannot be empty")
        claim_name = cls._kube_name(repo_id)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        with await Kube.host(timeout=deadline - loop.time()) as kube:
            # select the preferred CephFS storage class in deterministic order
            storage = await StorageClass.select(
                kube=kube,
                timeout=deadline - loop.time(),
                preferences=CEPHFS_STORAGE_CLASS_PREFERENCES,
                require_expansion=True,
            )
            if not storage.is_cephfs:
                raise OSError(
                    f"storage class {storage.name!r} uses provisioner "
                    f"{storage.provisioner!r}, but Bertrand repository volumes require a "
                    "CephFS CSI provisioner"
                )
            storage_class = storage.name

            # get existing PVC or create a new one if missing
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
                timeout=deadline - loop.time(),
            )

            cls._assert_managed_pvc(
                pvc,
                claim_name=claim_name,
                repo_id=repo_id,
                storage_class=storage_class,
                require_rwx=True,
            )

            return cls(repo_id=repo_id, pvc=pvc)

    @classmethod
    async def get(cls, repo_id: str | None, *, timeout: float) -> list[Self]:
        """List repository volumes currently present in the cluster.

        Parameters
        ----------
        repo_id : str | None
            If provided, filter the repository volumes by this specific repository ID.
        timeout : float
            Maximum runtime command timeout in seconds.  If infinite, wait
            indefinitely.

        Returns
        -------
        list[RepoVolume]
            Structured metadata for each repository volume claim found in the cluster
            that matches the filters.
        """
        labels = {BERTRAND_ENV: "1", REPO_VOLUME_ENV: "1"}
        if repo_id is not None:
            repo_id = _check_uuid(repo_id)
            labels[REPO_ID_ENV] = repo_id
        if timeout <= 0:
            raise TimeoutError("timeout must be non-negative")
        with await Kube.host(timeout=timeout) as kube:
            # get matching PVCs
            pvcs = await PersistentVolumeClaim.list(
                kube=kube,
                namespaces=(BERTRAND_NAMESPACE,),
                timeout=timeout,
                labels=labels,
            )
            out: list[Self] = []
            for pvc in pvcs:
                meta = pvc.obj.metadata
                labels = (meta.labels or {}) if meta is not None else {}
                repo_id = labels.get(REPO_ID_ENV, "")
                if not repo_id:
                    raise OSError(
                        "cluster PVC "
                        f"{(meta.name if meta is not None else '')!r} is missing "
                        f"label {REPO_ID_ENV!r}"
                    )
                repo_id = _check_uuid(repo_id)
                cls._assert_managed_pvc(
                    pvc,
                    claim_name=cls._kube_name(repo_id),
                    repo_id=repo_id,
                    storage_class=None,
                    require_rwx=False,
                )
                out.append(cls(repo_id=repo_id, pvc=pvc))

            # deterministically order the output
            out.sort(
                key=lambda m: (
                    m.repo_id,
                    (m.pvc.obj.metadata.name if m.pvc.obj.metadata is not None else ""),
                )
            )
            return out

    async def delete(self, *, timeout: float, force: bool) -> None:
        """Delete this repository volume claim from the cluster.

        Parameters
        ----------
        timeout : float
            Maximum runtime command timeout in seconds.  If infinite, wait
            indefinitely.
        force : bool
            If True, delete the claim even if it is currently referenced by active pods.
        """
        if timeout <= 0:
            raise TimeoutError("timeout must be non-negative")
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        with await Kube.host(timeout=deadline - loop.time()) as kube:
            # check for running pods associated with this pvc, unless overridden
            if not force:
                pods = await Pod.list(
                    kube=kube,
                    namespaces=(BERTRAND_NAMESPACE,),
                    timeout=deadline - loop.time(),
                    labels={BERTRAND_ENV: "1", REPO_ID_ENV: self.repo_id},
                )
                active = {
                    claim_name
                    for pod in pods
                    if pod.is_active
                    for claim_name in pod.persistent_volume_claim_names
                }
                active.discard("")
                meta = self.pvc.obj.metadata
                name = meta.name if meta is not None else None
                if name in active:
                    raise OSError(
                        f"cannot delete repository volume {name!r} while "
                        "it is being used by active pods"
                    )

            # delete the volume, ignoring race conditions
            await self.pvc.delete(kube=kube, timeout=deadline - loop.time())

    async def resolve_ceph_path(self, *, timeout: float) -> PosixPath:
        """Resolve this repo claim's CephFS path from its bound PVC/PV metadata.

        Parameters
        ----------
        timeout : float
            Maximum runtime command timeout in seconds.  If infinite, wait
            indefinitely.

        Returns
        -------
        PosixPath
            Absolute path to the claimed CephFS subvolume within the Ceph filesystem,
            as specified in the PV's CSI volume attributes.
        """
        if timeout <= 0:
            raise TimeoutError("timeout must be non-negative")
        meta = self.pvc.obj.metadata
        name = (meta.name or "") if meta is not None else ""
        namespace = (meta.namespace or "") if meta is not None else ""
        if not name:
            raise OSError("cannot resolve Ceph path for PVC with missing metadata.name")
        if not namespace:
            raise OSError(
                f"cannot resolve Ceph path for PVC {name!r} with missing metadata.namespace"
            )

        # wait until the PV is bound with the expected CSI attributes
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        with await Kube.host(timeout=deadline - loop.time()) as kube:
            pvc = await self.pvc.wait_bound(
                kube=kube,
                timeout=deadline - loop.time(),
            )
            volume_name = pvc.volume_name
            volume = await PersistentVolume.wait_present(
                kube=kube,
                timeout=deadline - loop.time(),
                name=volume_name,
            )

            # confirm CSI driver with cephfs backend
            driver = volume.csi_driver
            if not driver:
                raise OSError(
                    f"PersistentVolume {volume_name!r} is not CSI-backed and cannot be "
                    "mounted as a Ceph repository volume"
                )
            if "cephfs" not in driver.lower():
                raise OSError(
                    f"PersistentVolume {volume_name!r} uses CSI driver {driver!r}, "
                    "expected a CephFS driver"
                )

            ceph_path = volume.ceph_path
            if ceph_path is None:
                raise OSError(
                    "repository PersistentVolume is missing CephFS path attributes "
                    "(expected one of 'subvolumePath', 'rootPath', or 'path')"
                )
            return ceph_path
