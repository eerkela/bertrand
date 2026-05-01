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
    JSONValue,
)
from .api import Kube, _label_selector
from .pod import Pod

CACHE_VOLUME_ENV: str = "BERTRAND_CACHE_VOLUME"
REPO_VOLUME_ENV: str = "BERTRAND_REPO_VOLUME"
DEFAULT_VOLUME_SIZE = "16Mi"
REPO_STORAGE_CLASS_PREFERENCES: tuple[str, ...] = ("cephfs", "rook-cephfs")
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
EMPTY_MAPPING: Mapping[str, str] = {}


@dataclass(frozen=True)
class StorageClass:
    """General-purpose wrapper around one Kubernetes StorageClass object."""
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
        """List Kubernetes StorageClasses with optional label filtering."""
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

    @property
    def name(self) -> str:
        """Return trimmed `metadata.name`, or an empty string when unavailable."""
        metadata = self.obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def provisioner(self) -> str:
        """Return trimmed CSI provisioner name, or an empty string when unavailable."""
        return (self.obj.provisioner or "").strip()

    @property
    def allow_volume_expansion(self) -> bool:
        """Return whether this StorageClass allows dynamic volume expansion."""
        return bool(self.obj.allow_volume_expansion)

    @property
    def parameters(self) -> Mapping[str, str]:
        """Return StorageClass parameters, or an empty mapping when unavailable."""
        return self.obj.parameters or EMPTY_MAPPING


@dataclass(frozen=True)
class PersistentVolumeClaim:
    """General-purpose wrapper around one Kubernetes PersistentVolumeClaim object."""
    obj: kubernetes.client.V1PersistentVolumeClaim

    @staticmethod
    def _requested_storage(spec: kubernetes.client.V1PersistentVolumeClaimSpec) -> str:
        resources = spec.resources or kubernetes.client.V1VolumeResourceRequirements()
        requests = resources.requests or {}
        value = str(requests.get("storage") or "").strip()
        if not value:
            raise OSError("PVC does not expose a valid storage request quantity")
        return value

    @classmethod
    async def get(
        cls,
        *,
        kube: Kube,
        namespace: str,
        timeout: float,
        name: KubeName,
    ) -> Self | None:
        """Read one Kubernetes PersistentVolumeClaim by name."""
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
                f"malformed Kubernetes PVC payload for {name!r} in namespace "
                f"{namespace!r}"
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

    @classmethod
    async def create(
        cls,
        data: dict[str, JSONValue],
        *,
        kube: Kube,
        timeout: float,
    ) -> Self:
        """Create a Kubernetes PersistentVolumeClaim from a manifest payload."""
        metadata = data.get("metadata")
        name = ""
        namespace = ""
        if isinstance(metadata, Mapping):
            metadata_fields: dict[str, object] = {}
            for key, value in metadata.items():
                if isinstance(key, str):
                    metadata_fields[key] = value
            name = str(metadata_fields.get("name") or "").strip()
            namespace = str(metadata_fields.get("namespace") or "").strip()
        if not namespace:
            raise OSError("PVC creation payload must define metadata.namespace")

        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        try:
            payload = await kube.run(
                lambda request_timeout: kube.core.create_namespaced_persistent_volume_claim(
                    namespace=namespace,
                    body=data,
                    _request_timeout=request_timeout,
                ),
                timeout=deadline - loop.time(),
                context="failed to create PersistentVolumeClaim",
            )
            if payload is None:
                raise OSError("kubernetes returned empty payload during PVC creation")
            if not isinstance(payload, kubernetes.client.V1PersistentVolumeClaim):
                raise OSError("malformed Kubernetes payload during PVC creation")
            return cls(obj=payload)
        except OSError as err:
            text = str(err).lower()
            if "status 409" not in text and "already exists" not in text:
                raise

        if name and namespace:
            existing = await cls.get(
                kube=kube,
                namespace=namespace,
                timeout=deadline - loop.time(),
                name=name,
            )
            if existing is not None:
                return existing
        raise OSError(
            "kubernetes accepted PVC creation, but no valid PVC payload was returned"
        )

    async def grow(
        self,
        requested: str,
        *,
        kube: Kube,
        timeout: float,
    ) -> None:
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
                return
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
                    raise OSError(
                        f"PVC {name!r} disappeared during resize lifecycle"
                    ) from err
                if (
                    "status 409" in detail or
                    "conflict" in detail or
                    "the object has been modified" in detail
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
                return
            if attempt + 1 >= PVC_GROW_RETRIES:
                raise OSError(
                    f"PVC {name!r} did not converge to requested size {requested!r} "
                    f"after {PVC_GROW_RETRIES} attempts"
                )

    async def delete(self, *, kube: Kube, timeout: float) -> None:
        """Delete this PVC from the cluster."""
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
        """Re-read this PVC by identity."""
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
        """Wait until this PVC reaches a bound state."""
        identity = self.identity
        if identity is None:
            raise OSError(
                "cannot wait for bound state of PVC with missing metadata.name/namespace"
            )
        namespace, name = identity
        if timeout <= 0:
            raise TimeoutError(f"timed out waiting for PVC {namespace}/{name} binding")
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                raise TimeoutError(
                    f"timed out waiting for PVC {namespace}/{name} binding"
                )
            live = await self.refresh(kube=kube, timeout=remaining)
            if live is None:
                raise OSError(f"PVC {name!r} disappeared before binding")
            if live.is_bound:
                return live
            await asyncio.sleep(min(VOLUME_WAIT_POLL_INTERVAL_SECONDS, remaining))

    async def wait_deleted(self, *, kube: Kube, timeout: float) -> None:
        """Wait until this PVC is deleted."""
        identity = self.identity
        if identity is None:
            raise OSError(
                "cannot wait for deletion of PVC with missing metadata.name/namespace"
            )
        namespace, name = identity
        if timeout <= 0:
            raise TimeoutError(f"timed out waiting for PVC {namespace}/{name} deletion")
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                raise TimeoutError(
                    f"timed out waiting for PVC {namespace}/{name} deletion"
                )
            live = await self.refresh(kube=kube, timeout=remaining)
            if live is None:
                return
            await asyncio.sleep(min(VOLUME_WAIT_POLL_INTERVAL_SECONDS, remaining))

    @property
    def name(self) -> str:
        """Return trimmed `metadata.name`, or an empty string when unavailable."""
        metadata = self.obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def namespace(self) -> str:
        """Return trimmed `metadata.namespace`, or an empty string when unavailable."""
        metadata = self.obj.metadata
        return (metadata.namespace or "").strip() if metadata is not None else ""

    @property
    def identity(self) -> tuple[str, str] | None:
        """Return `(namespace, name)` identity when both components are available."""
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            return None
        return namespace, name

    @property
    def phase(self) -> str:
        """Return trimmed PVC phase string, or an empty string when unavailable."""
        status = self.obj.status
        return (status.phase or "").strip() if status is not None else ""

    @property
    def is_bound(self) -> bool:
        """Return whether this PVC phase is `Bound` and has `spec.volume_name`."""
        return self.phase == "Bound" and bool(self.volume_name)

    @property
    def volume_name(self) -> str:
        """Return trimmed `spec.volume_name`, or an empty string when unavailable."""
        spec = self.obj.spec
        return (spec.volume_name or "").strip() if spec is not None else ""

    @property
    def requested_storage(self) -> str:
        """Return requested storage quantity string from `spec.resources.requests`."""
        spec = self.obj.spec or kubernetes.client.V1PersistentVolumeClaimSpec()
        return type(self)._requested_storage(spec)

    @property
    def storage_class_name(self) -> str:
        """Return trimmed storage class name, or an empty string when unavailable."""
        spec = self.obj.spec
        return (spec.storage_class_name or "").strip() if spec is not None else ""

    @property
    def access_modes(self) -> tuple[str, ...]:
        """Return immutable access mode tuple, preserving API order."""
        spec = self.obj.spec
        modes = (spec.access_modes or []) if spec is not None else []
        return tuple(mode.strip() for mode in modes if mode and mode.strip())


@dataclass(frozen=True)
class PersistentVolume:
    """General-purpose wrapper around one Kubernetes PersistentVolume object."""
    obj: kubernetes.client.V1PersistentVolume

    @classmethod
    async def get(
        cls,
        *,
        kube: Kube,
        timeout: float,
        name: KubeName,
    ) -> Self | None:
        """Read one Kubernetes PersistentVolume by name."""
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
        """List Kubernetes PersistentVolumes with optional label filtering."""
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
        """Re-read this PersistentVolume by name."""
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
        """Wait until a PersistentVolume exists by name."""
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
        """Return trimmed `metadata.name`, or an empty string when unavailable."""
        metadata = self.obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def phase(self) -> str:
        """Return trimmed PersistentVolume phase string, or empty string."""
        status = self.obj.status
        return (status.phase or "").strip() if status is not None else ""

    @property
    def claim_identity(self) -> tuple[str, str] | None:
        """Return `(namespace, claim_name)` from claim ref, or `None` if absent."""
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
        """Return trimmed CSI driver string, or empty string when unavailable."""
        spec = self.obj.spec
        csi = spec.csi if spec is not None else None
        return (csi.driver or "").strip() if csi is not None else ""

    @property
    def csi_volume_attributes(self) -> Mapping[str, str]:
        """Return CSI volume attributes, or an empty mapping when unavailable."""
        spec = self.obj.spec
        csi = spec.csi if spec is not None else None
        return csi.volume_attributes or EMPTY_MAPPING if csi is not None else EMPTY_MAPPING

    @property
    def ceph_path(self) -> PosixPath | None:
        """Return CephFS backing path from CSI attributes, or `None` if unavailable."""
        for key in ("subvolumePath", "rootPath", "path"):
            value = self.csi_volume_attributes.get(key, "").strip()
            if not value:
                continue
            return PosixPath(value if value.startswith("/") else f"/{value}")
        return None


def parse_pvc_size(value: str) -> Decimal:
    """Parse a Kubernetes PVC request size string into a Decimal value."""
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
        raise ValueError(
            f"invalid Kubernetes memory quantity for PVC request: {value!r}"
        ) from err


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
                    f"failed to resolve cache volumes for resource '{resource.name}': "
                    f"{err}"
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
                        f"resource '{resource.name}' mount target must be absolute: "
                        f"{target}"
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
                    digest = hashlib.sha256(
                        text.encode("utf-8")
                    ).hexdigest()
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
            raise OSError(
                f"cluster PVC {claim_name!r} must include ReadWriteOnce access mode"
            )

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
            # get PVC storage class and assert that it supports volume expansion for
            # dynamic resizing
            storage = await StorageClass.get(
                kube=kube,
                timeout=deadline - loop.time(),
                name=storage_class,
            )
            if storage is None:
                raise OSError(
                    f"required {storage_class!r} StorageClass is not available; cache PVC "
                    "provisioning cannot proceed"
                )
            if not storage.allow_volume_expansion:
                raise OSError(
                    f"{storage_class!r} StorageClass must set 'allowVolumeExpansion=true' "
                    "for Bertrand cache PVC resizing"
                )
            storage_name = storage.name

            # get/create PVCs for each of this tag's cache volumes
            for volume in await CacheVolume.from_config(config, tag, env_id):
                pvc = await PersistentVolumeClaim.get(
                    kube=kube,
                    namespace=BERTRAND_NAMESPACE,
                    timeout=deadline - loop.time(),
                    name=volume.name,
                )
                if pvc is None:  # create a new volume with the requested size
                    pvc = await PersistentVolumeClaim.create({
                        "apiVersion": "v1",
                        "kind": "PersistentVolumeClaim",
                        "metadata": {
                            "name": volume.name,
                            "namespace": BERTRAND_NAMESPACE,
                            "labels": {
                                BERTRAND_ENV: "1",
                                CACHE_VOLUME_ENV: "1",
                                ENV_ID_ENV: env_id,
                            },
                        },
                        "spec": {
                            "accessModes": ["ReadWriteOnce"],
                            "storageClassName": storage_name,
                            "resources": {
                                "requests": {
                                    "storage": size_request,
                                },
                            },
                        },
                    }, kube=kube, timeout=deadline - loop.time())

                cls._assert_managed_cache(
                    pvc,
                    claim_name=volume.name,
                    env_id=env_id,
                    storage_class=storage_class,
                    require_rwo=True,
                )

                # try to grow existing volume if necessary
                await pvc.grow(
                    size_request,
                    kube=kube,
                    timeout=deadline - loop.time(),
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
                pvc for pvc in actual if (
                    pvc.obj.metadata is not None and
                    pvc.obj.metadata.name and
                    pvc.obj.metadata.name not in expected and
                    pvc.obj.metadata.name not in active
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
            raise OSError(
                f"cluster PVC {claim_name!r} must include ReadWriteMany access mode"
            )

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
            storage_class: str | None = None
            storage: StorageClass | None = None
            for candidate in REPO_STORAGE_CLASS_PREFERENCES:
                storage = await StorageClass.get(
                    kube=kube,
                    timeout=deadline - loop.time(),
                    name=candidate,
                )
                if storage is not None:
                    storage_class = candidate
                    break
            if storage_class is None or storage is None:
                preferred = ", ".join(repr(name) for name in REPO_STORAGE_CLASS_PREFERENCES)
                raise OSError(
                    "required CephFS storage class is not available; expected one of "
                    f"{preferred}.  Ensure MicroK8s is linked to MicroCeph."
                )

            # assert that the selected class supports dynamic resizing and CephFS CSI
            if not storage.allow_volume_expansion:
                raise OSError(
                    f"storage class {storage_class!r} must set allowVolumeExpansion=true "
                    "for Bertrand repository volume resizing"
                )
            provisioner = storage.provisioner.lower()
            if "cephfs" not in provisioner or "csi.ceph.com" not in provisioner:
                raise OSError(
                    f"storage class {storage_class!r} uses provisioner "
                    f"{storage.provisioner!r}, but Bertrand repository volumes require a "
                    "CephFS CSI provisioner (for example 'rook-ceph.cephfs.csi.ceph.com').  "
                    "Ensure MicroK8s is linked to MicroCeph and that the preferred "
                    "CephFS storage class is configured correctly."
                )

            # get existing PVC or create a new one if missing
            pvc = await PersistentVolumeClaim.get(
                kube=kube,
                namespace=BERTRAND_NAMESPACE,
                timeout=deadline - loop.time(),
                name=claim_name,
            )
            if pvc is None:
                pvc = await PersistentVolumeClaim.create({
                    "apiVersion": "v1",
                    "kind": "PersistentVolumeClaim",
                    "metadata": {
                        "name": claim_name,
                        "namespace": BERTRAND_NAMESPACE,
                        "labels": {
                            BERTRAND_ENV: "1",
                            REPO_VOLUME_ENV: "1",
                            REPO_ID_ENV: repo_id,
                        },
                    },
                    "spec": {
                        "accessModes": ["ReadWriteMany"],
                        "storageClassName": storage_class,
                        "resources": {
                            "requests": {
                                "storage": size_request,
                            },
                        },
                    },
                }, kube=kube, timeout=deadline - loop.time())

            cls._assert_managed_pvc(
                pvc,
                claim_name=claim_name,
                repo_id=repo_id,
                storage_class=storage_class,
                require_rwx=True,
            )

            # try to grow existing volume if necessary
            await pvc.grow(
                size_request,
                kube=kube,
                timeout=deadline - loop.time(),
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
            out.sort(key=lambda m: (
                m.repo_id,
                (m.pvc.obj.metadata.name if m.pvc.obj.metadata is not None else ""),
            ))
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
                f"cannot resolve Ceph path for PVC {name!r} with missing "
                "metadata.namespace"
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
