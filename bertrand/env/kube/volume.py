"""Wrappers for Kubernetes storage APIs and related operations."""

from __future__ import annotations

import asyncio
import re
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
from pathlib import PosixPath
from types import MappingProxyType
from typing import TYPE_CHECKING, Self

import kubernetes

from .api import Kube, _label_selector

if TYPE_CHECKING:
    import builtins
    from collections.abc import Collection, Mapping

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
        kube: Kube,
        *,
        timeout: float,
        name: str,
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

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or the API call fails.
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
            msg = f"malformed Kubernetes StorageClass payload for {name!r}"
            raise OSError(msg)
        return cls(obj=payload)

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
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
            msg = "malformed Kubernetes StorageClass list payload"
            raise OSError(msg)
        out: builtins.list[Self] = []
        for item in payload.items or []:
            if not isinstance(item, kubernetes.client.V1StorageClass):
                msg = "malformed Kubernetes StorageClass entry in list payload"
                raise OSError(msg)
            out.append(cls(obj=item))
        return out

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
            If no preferred StorageClass exists, expansion is required but missing,
            or Kubernetes returns malformed data.
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
    def name(self) -> str:
        """Return the StorageClass name.

        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def provisioner(self) -> str:
        """Return the StorageClass provisioner.

        Returns
        -------
        str
            Trimmed provisioner name, or an empty string when unavailable.
        """
        return (self.obj.provisioner or "").strip()

    @property
    def allow_volume_expansion(self) -> bool:
        """Return whether volumes can expand dynamically.

        Returns
        -------
        bool
            Whether this StorageClass allows dynamic volume expansion.
        """
        return bool(self.obj.allow_volume_expansion)

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
        return MappingProxyType(self.obj.parameters or {})


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
        kube: Kube,
        *,
        namespace: str,
        timeout: float,
        name: str,
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
        name : str
            Claim name to read.

        Returns
        -------
        PersistentVolumeClaim | None
            Wrapped Kubernetes claim, or `None` if it does not exist.

        Raises
        ------
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
            msg = (
                f"malformed Kubernetes PVC payload for {name!r} in namespace "
                f"{namespace!r}"
            )
            raise OSError(msg)
        return cls(obj=payload)

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
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

        Returns
        -------
        list[PersistentVolumeClaim]
            Wrapped Kubernetes PersistentVolumeClaims matching the requested filters.

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or a list call fails.
        """
        label_selector = _label_selector(labels)
        payloads: builtins.list[kubernetes.client.V1PersistentVolumeClaimList] = []
        if namespaces is None:
            payload = await kube.run(
                lambda request_timeout: (
                    kube.core.list_persistent_volume_claim_for_all_namespaces(
                        label_selector=label_selector,
                        _request_timeout=request_timeout,
                    )
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
                msg = "malformed Kubernetes PersistentVolumeClaim list payload"
                raise OSError(msg)
            for item in payload.items or []:
                if not isinstance(item, kubernetes.client.V1PersistentVolumeClaim):
                    msg = (
                        "malformed Kubernetes PersistentVolumeClaim entry in "
                        "list payload"
                    )
                    raise OSError(msg)
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
            If intent fields are empty, immutable existing claim fields differ, or
            Kubernetes create/patch/read operations fail.
        """
        namespace = namespace.strip()
        name = name.strip()
        storage_class = storage_class.strip()
        storage_request = storage_request.strip()
        modes = tuple(mode.strip() for mode in access_modes if mode and mode.strip())
        if not namespace or not name:
            msg = "PVC upsert requires non-empty namespace and name"
            raise OSError(msg)
        if not modes:
            msg = "PVC upsert requires at least one access mode"
            raise OSError(msg)
        if not storage_class:
            msg = "PVC upsert requires a non-empty storage class"
            raise OSError(msg)
        if not storage_request:
            msg = "PVC upsert requires a non-empty storage request"
            raise OSError(msg)
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
                lambda request_timeout: (
                    kube.core.create_namespaced_persistent_volume_claim(
                        namespace=namespace,
                        body=manifest,
                        _request_timeout=request_timeout,
                    )
                ),
                timeout=deadline - loop.time(),
                context=f"failed to create PersistentVolumeClaim {namespace}/{name}",
            )
        except OSError as err:
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise
        else:
            if not isinstance(payload, kubernetes.client.V1PersistentVolumeClaim):
                msg = (
                    "malformed Kubernetes PVC payload while creating "
                    f"{namespace}/{name}"
                )
                raise OSError(msg)
            return cls(obj=payload)

        live = await cls.get(
            kube,
            namespace=namespace,
            timeout=deadline - loop.time(),
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
            timeout=deadline - loop.time(),
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
            If the claim cannot be identified or resize convergence fails.
        """
        new_size = parse_pvc_size(requested)
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = "cannot resize PVC with missing metadata.name/namespace"
            raise OSError(msg)
        patch = {"spec": {"resources": {"requests": {"storage": requested}}}}
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout

        for attempt in range(PVC_GROW_RETRIES):
            live = await type(self).get(
                kube,
                namespace=namespace,
                timeout=deadline - loop.time(),
                name=name,
            )
            if live is None:
                msg = f"PVC {name!r} disappeared during resize lifecycle"
                raise OSError(msg)
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
                    timeout=deadline - loop.time(),
                    context=f"failed to patch PVC {name!r} during resize lifecycle",
                )
            except OSError as err:
                detail = str(err).lower()
                if "status 404" in detail or "not found" in detail:
                    msg = f"PVC {name!r} disappeared during resize lifecycle"
                    raise OSError(msg) from err
                if (
                    "status 409" in detail
                    or "conflict" in detail
                    or "the object has been modified" in detail
                ) and attempt + 1 < PVC_GROW_RETRIES:
                    continue
                raise

            live = await type(self).get(
                kube,
                namespace=namespace,
                timeout=deadline - loop.time(),
                name=name,
            )
            if live is None:
                msg = f"PVC {name!r} disappeared during resize lifecycle"
                raise OSError(msg)
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

        Raises
        ------
        OSError
            If this wrapper is missing metadata or the delete request fails.
        """
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = "cannot delete PVC with missing metadata.name/namespace"
            raise OSError(msg)
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

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this PVC by its metadata namespace and name.

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
            If this wrapper is missing metadata or Kubernetes returns malformed data.
        """
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = "cannot refresh PVC with missing metadata.name/namespace"
            raise OSError(msg)
        return await type(self).get(
            kube,
            namespace=namespace,
            timeout=timeout,
            name=name,
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
        OSError
            If this wrapper is missing metadata or the claim disappears.
        TimeoutError
            If the claim does not bind before `timeout`.
        """
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = (
                "cannot wait for bound state of PVC with missing "
                "metadata.name/namespace"
            )
            raise OSError(msg)
        if timeout <= 0:
            msg = f"timed out waiting for PVC {namespace}/{name} binding"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                msg = f"timed out waiting for PVC {namespace}/{name} binding"
                raise TimeoutError(msg)
            live = await self.refresh(kube, timeout=remaining)
            if live is None:
                msg = f"PVC {name!r} disappeared before binding"
                raise OSError(msg)
            if live.is_bound:
                return live
            await asyncio.sleep(min(VOLUME_WAIT_POLL_INTERVAL_SECONDS, remaining))

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
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
            If this wrapper is missing metadata or Kubernetes returns malformed data.
        TimeoutError
            If the claim still exists when `timeout` expires.
        """
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = "cannot wait for deletion of PVC with missing metadata.name/namespace"
            raise OSError(msg)
        if timeout <= 0:
            msg = f"timed out waiting for PVC {namespace}/{name} deletion"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                msg = f"timed out waiting for PVC {namespace}/{name} deletion"
                raise TimeoutError(msg)
            live = await self.refresh(kube, timeout=remaining)
            if live is None:
                return
            await asyncio.sleep(min(VOLUME_WAIT_POLL_INTERVAL_SECONDS, remaining))

    @property
    def name(self) -> str:
        """Return the PersistentVolumeClaim name.

        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def namespace(self) -> str:
        """Return the PersistentVolumeClaim namespace.

        Returns
        -------
        str
            Trimmed `metadata.namespace`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.namespace or "").strip() if metadata is not None else ""

    @property
    def labels(self) -> Mapping[str, str]:
        """Return the PersistentVolumeClaim labels.

        Returns
        -------
        Mapping[str, str]
            Claim labels, or an empty mapping when unavailable.
        """
        metadata = self.obj.metadata
        if metadata is None or metadata.labels is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.labels)

    @property
    def annotations(self) -> Mapping[str, str]:
        """Return the PersistentVolumeClaim annotations.

        Returns
        -------
        Mapping[str, str]
            Claim annotations, or an empty mapping when unavailable.
        """
        metadata = self.obj.metadata
        if metadata is None or metadata.annotations is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.annotations)

    @property
    def phase(self) -> str:
        """Return the PersistentVolumeClaim phase.

        Returns
        -------
        str
            Trimmed claim phase string, or an empty string when unavailable.
        """
        status = self.obj.status
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
        spec = self.obj.spec
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
            If the claim does not expose a storage request.
        """
        spec = self.obj.spec or kubernetes.client.V1PersistentVolumeClaimSpec()
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
        spec = self.obj.spec
        return (spec.storage_class_name or "").strip() if spec is not None else ""

    @property
    def access_modes(self) -> tuple[str, ...]:
        """Return the claim access modes.

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
        kube: Kube,
        *,
        timeout: float,
        name: str,
    ) -> Self | None:
        """Read one Kubernetes PersistentVolume by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        name : str
            PersistentVolume name to read.

        Returns
        -------
        PersistentVolume | None
            Wrapped Kubernetes PersistentVolume, or `None` if it does not exist.

        Raises
        ------
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
            msg = f"malformed Kubernetes PersistentVolume payload for {name!r}"
            raise OSError(msg)
        return cls(obj=payload)

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
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
            msg = "malformed Kubernetes PersistentVolume list payload"
            raise OSError(msg)
        out: builtins.list[Self] = []
        for item in payload.items or []:
            if not isinstance(item, kubernetes.client.V1PersistentVolume):
                msg = "malformed Kubernetes PersistentVolume entry in list payload"
                raise OSError(msg)
            out.append(cls(obj=item))
        return out

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
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
        """
        name = self.name
        if not name:
            msg = "cannot refresh PersistentVolume with missing metadata.name"
            raise OSError(msg)
        return await type(self).get(kube, timeout=timeout, name=name)

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
        if timeout <= 0:
            msg = f"timed out waiting for PersistentVolume {name!r}"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                msg = f"timed out waiting for PersistentVolume {name!r}"
                raise TimeoutError(msg)
            live = await cls.get(kube, timeout=remaining, name=name)
            if live is not None:
                return live
            await asyncio.sleep(min(VOLUME_WAIT_POLL_INTERVAL_SECONDS, remaining))

    @property
    def name(self) -> str:
        """Return the PersistentVolume name.

        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def phase(self) -> str:
        """Return the PersistentVolume phase.

        Returns
        -------
        str
            Trimmed PersistentVolume phase string, or an empty string when unavailable.
        """
        status = self.obj.status
        return (status.phase or "").strip() if status is not None else ""

    @property
    def claim_identity(self) -> tuple[str, str] | None:
        """Return the bound claim identity.

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
        """Return the CSI driver name.

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
        """Return CSI volume attributes.

        Returns
        -------
        Mapping[str, str]
            CSI volume attributes, or an empty mapping when unavailable.
        """
        spec = self.obj.spec
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
