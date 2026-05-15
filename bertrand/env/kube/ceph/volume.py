"""CephFS-backed repository volume lifecycle helpers."""

from __future__ import annotations

import asyncio
import hashlib
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta
from typing import TYPE_CHECKING, Annotated, Literal, Self

from pydantic import BaseModel, ConfigDict, Field, ValidationError, field_validator

from bertrand.env.config.core import _check_uuid
from bertrand.env.git import BERTRAND_ENV, BERTRAND_NAMESPACE, REPO_ID_ENV
from bertrand.env.kube.api.spec import CustomResourceSpec
from bertrand.env.kube.crd import (
    CustomObjectMetadata,
    CustomResourceClient,
    CustomResourceDefinition,
)
from bertrand.env.kube.pod import Pod
from bertrand.env.kube.volume import (
    PersistentVolume,
    PersistentVolumeClaim,
    StorageClass,
)

if TYPE_CHECKING:
    import builtins
    from collections.abc import Mapping
    from pathlib import PosixPath

    from bertrand.env.kube.api.client import Kube

REPO_VOLUME_ENV: str = "BERTRAND_REPO_VOLUME"
DEFAULT_VOLUME_SIZE = "16Mi"
CEPHFS_STORAGE_CLASS_PREFERENCES: tuple[str, ...] = ("cephfs", "rook-cephfs")
REPOSITORY_VOLUME_GROUP = "ceph.bertrand.dev"
REPOSITORY_VOLUME_VERSION = "v1alpha1"
REPOSITORY_VOLUME_KIND = "CephRepositoryVolume"
REPOSITORY_VOLUME_PLURAL = "cephrepositoryvolumes"
REPOSITORY_VOLUME_LABEL = "bertrand.dev/ceph-repository-volume"
REPOSITORY_VOLUME_LABEL_VALUE = "v1"
REPOSITORY_VOLUME_PHASE_LABEL = "bertrand.dev/ceph-repository-volume-phase"
REPOSITORY_VOLUME_GC_GRACE_SECONDS = 604_800
REPOSITORY_VOLUME_GC_LIMIT = 4
_REPOSITORY_VOLUME_LABELS = {
    BERTRAND_ENV: "1",
    REPO_VOLUME_ENV: "1",
    REPOSITORY_VOLUME_LABEL: REPOSITORY_VOLUME_LABEL_VALUE,
}

type _RepositoryVolumePhase = Literal["Active", "Retired"]
type _NonEmptyString = Annotated[str, Field(min_length=1)]


@dataclass(frozen=True)
class RepoVolume:
    """Structured metadata for a CephFS-backed repository volume in the local cluster.

    Attributes
    ----------
    repo_id : str
        Stable repository identity used for volume naming and management.
    pvc : PersistentVolumeClaim
        Kubernetes PVC object representing the repository volume claim.
    """

    repo_id: str
    pvc: PersistentVolumeClaim

    @staticmethod
    def claim_name(repo_id: str) -> str:
        """Return the deterministic PVC name for a repository identity.

        Parameters
        ----------
        repo_id : str
            Repository UUID used to derive the managed claim name.

        Returns
        -------
        str
            Kubernetes PVC name for the repository volume.
        """
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
        actual_name = pvc.name
        if actual_name != claim_name:
            msg = (
                f"cluster PVC for repo {repo_id!r} has non-deterministic claim name "
                f"{actual_name!r}, expected {claim_name!r}"
            )
            raise OSError(msg)
        labels = pvc.labels
        storage_class_name = pvc.storage_class_name
        access_modes = pvc.access_modes
        if labels.get(BERTRAND_ENV) != "1" or labels.get(REPO_VOLUME_ENV) != "1":
            msg = (
                f"cluster PVC {claim_name!r} collides with Bertrand volume claim, but "
                "is not managed by Bertrand"
            )
            raise OSError(msg)
        actual_repo_id = labels.get(REPO_ID_ENV)
        if actual_repo_id != repo_id:
            msg = (
                f"cluster PVC {claim_name!r} has mismatched repo identity label "
                f"{REPO_ID_ENV!r}: expected {repo_id!r}, got {actual_repo_id!r}"
            )
            raise OSError(msg)
        if storage_class is not None and storage_class_name != storage_class:
            msg = (
                f"cluster PVC {claim_name!r} uses storage class "
                f"{storage_class_name!r}, expected {storage_class!r}"
            )
            raise OSError(msg)
        if require_rwx and "ReadWriteMany" not in access_modes:
            msg = f"cluster PVC {claim_name!r} must include ReadWriteMany access mode"
            raise OSError(msg)

    @classmethod
    async def ensure(
        cls,
        kube: Kube,
        *,
        repo_id: str,
        timeout: float,
        size_request: str,
    ) -> Self:
        """Ensure a deterministic, cluster-wide RWX claim exists.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        repo_id : str
            Repository UUID used to derive the claim name and ownership labels.
        timeout : float
            Maximum convergence budget in seconds.
        size_request : str
            Kubernetes storage request to apply to the claim.

        Returns
        -------
        RepoVolume
            Repository volume wrapper for the converged claim.

        Raises
        ------
        OSError
            If a selected storage class or existing PVC is incompatible.
        TimeoutError
            If `timeout` is non-positive.
        ValueError
            If `repo_id` or `size_request` is invalid.
        """
        repo_id = _check_uuid(repo_id)
        if timeout <= 0:
            msg = "timeout must be non-negative"
            raise TimeoutError(msg)
        size_request = size_request.strip()
        if not size_request:
            msg = "size request cannot be empty"
            raise ValueError(msg)
        claim_name = cls.claim_name(repo_id)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        storage = await StorageClass.select(
            kube=kube,
            timeout=deadline - loop.time(),
            preferences=CEPHFS_STORAGE_CLASS_PREFERENCES,
            require_expansion=True,
        )
        if not storage.is_cephfs:
            msg = (
                f"storage class {storage.name!r} uses provisioner "
                f"{storage.provisioner!r}, but Bertrand repository volumes "
                "require a CephFS CSI provisioner"
            )
            raise OSError(msg)
        storage_class = storage.name

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
    async def list(
        cls,
        kube: Kube,
        repo_id: str | None,
        *,
        timeout: float,
    ) -> builtins.list[Self]:
        """List repository volumes currently present in the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        repo_id : str | None
            Optional repository UUID to filter for.
        timeout : float
            Maximum list budget in seconds.

        Returns
        -------
        list[RepoVolume]
            Repository volumes sorted by repository identity and claim name.

        Raises
        ------
        OSError
            If a discovered PVC has invalid Bertrand repository ownership metadata.
        TimeoutError
            If `timeout` is non-positive.
        """
        labels = {BERTRAND_ENV: "1", REPO_VOLUME_ENV: "1"}
        if repo_id is not None:
            repo_id = _check_uuid(repo_id)
            labels[REPO_ID_ENV] = repo_id
        if timeout <= 0:
            msg = "timeout must be non-negative"
            raise TimeoutError(msg)
        pvcs = await PersistentVolumeClaim.list(
            kube=kube,
            namespaces=(BERTRAND_NAMESPACE,),
            timeout=timeout,
            labels=labels,
        )
        out: list[Self] = []
        for pvc in pvcs:
            labels = pvc.labels
            repo_id = labels.get(REPO_ID_ENV, "")
            if not repo_id:
                msg = f"cluster PVC {pvc.name!r} is missing label {REPO_ID_ENV!r}"
                raise OSError(msg)
            repo_id = _check_uuid(repo_id)
            cls._assert_managed_pvc(
                pvc,
                claim_name=cls.claim_name(repo_id),
                repo_id=repo_id,
                storage_class=None,
                require_rwx=False,
            )
            out.append(cls(repo_id=repo_id, pvc=pvc))

        out.sort(
            key=lambda m: (
                m.repo_id,
                m.pvc.name,
            )
        )
        return out

    async def delete(self, kube: Kube, *, timeout: float, force: bool) -> None:
        """Delete this repository volume claim from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum deletion budget in seconds.
        force : bool
            Whether to delete even when active pods reference the claim.

        Raises
        ------
        OSError
            If the claim is still active and `force` is False, or deletion fails.
        TimeoutError
            If `timeout` is non-positive.
        """
        if timeout <= 0:
            msg = "timeout must be non-negative"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
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
            name = self.pvc.name
            if name in active:
                msg = (
                    f"cannot delete repository volume {name!r} while "
                    "it is being used by active pods"
                )
                raise OSError(msg)

        await self.pvc.delete(kube=kube, timeout=deadline - loop.time())

    async def resolve_ceph_path(self, kube: Kube, *, timeout: float) -> PosixPath:
        """Resolve this repo claim's CephFS path from bound PVC/PV metadata.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum resolution budget in seconds.

        Returns
        -------
        PosixPath
            Absolute CephFS path backing this repository claim.

        Raises
        ------
        OSError
            If the PVC/PV binding is missing or does not expose a CephFS path.
        TimeoutError
            If `timeout` is non-positive.
        """
        if timeout <= 0:
            msg = "timeout must be non-negative"
            raise TimeoutError(msg)
        name = self.pvc.name
        namespace = self.pvc.namespace
        if not name:
            msg = "cannot resolve Ceph path for PVC with missing metadata.name"
            raise OSError(msg)
        if not namespace:
            msg = (
                f"cannot resolve Ceph path for PVC {name!r} with missing "
                "metadata.namespace"
            )
            raise OSError(msg)

        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
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

        driver = volume.csi_driver
        if not driver:
            msg = (
                f"PersistentVolume {volume_name!r} is not CSI-backed and cannot be "
                "mounted as a Ceph repository volume"
            )
            raise OSError(msg)
        if "cephfs" not in driver.lower():
            msg = (
                f"PersistentVolume {volume_name!r} uses CSI driver {driver!r}, "
                "expected a CephFS driver"
            )
            raise OSError(msg)

        ceph_path = volume.ceph_path
        if ceph_path is None:
            msg = (
                "repository PersistentVolume is missing CephFS path attributes "
                "(expected one of 'subvolumePath', 'rootPath', or 'path')"
            )
            raise OSError(msg)
        return ceph_path


class _RepositoryVolumeSpec(BaseModel):
    """Lifecycle spec for one managed Ceph repository volume."""

    model_config = ConfigDict(extra="forbid", frozen=True)
    repo_id: _NonEmptyString
    claim_name: _NonEmptyString
    phase: _RepositoryVolumePhase = "Active"
    created_at: datetime
    last_seen_at: datetime
    retired_at: datetime | None = None
    last_gc_at: datetime | None = None
    last_error: str = ""

    @field_validator("created_at", "last_seen_at", "retired_at", "last_gc_at")
    @classmethod
    def _normalize_datetime(cls, value: datetime | None) -> datetime | None:
        if value is None:
            return None
        if value.tzinfo is None:
            return value.replace(tzinfo=UTC)
        return value.astimezone(UTC)


class CephRepositoryVolumeRecord(BaseModel):
    """Read-only model for one `CephRepositoryVolume` custom object.

    Parameters
    ----------
    api_version : str
        Kubernetes API version reported by the custom object.
    kind : {"CephRepositoryVolume"}
        Kubernetes custom object kind.
    metadata : CustomObjectMetadata
        Kubernetes object metadata.
    spec : _RepositoryVolumeSpec
        Repository volume lifecycle spec.
    """

    model_config = ConfigDict(extra="forbid", frozen=True, populate_by_name=True)
    api_version: str = Field(alias="apiVersion")
    kind: Literal["CephRepositoryVolume"]
    metadata: CustomObjectMetadata
    spec: _RepositoryVolumeSpec

    @classmethod
    def from_payload(cls, payload: object) -> CephRepositoryVolumeRecord:
        """Validate a Kubernetes custom object payload.

        Parameters
        ----------
        payload : object
            Raw Kubernetes custom object payload.

        Returns
        -------
        CephRepositoryVolumeRecord
            Validated repository volume lifecycle record.

        Raises
        ------
        OSError
            If the payload is malformed or ownership metadata is inconsistent.
        """
        try:
            record = cls.model_validate(payload)
        except ValidationError as err:
            msg = f"malformed {REPOSITORY_VOLUME_KIND} custom object: {err}"
            raise OSError(msg) from err

        repo_id = _check_uuid(record.spec.repo_id)
        expected_claim = RepoVolume.claim_name(repo_id)
        if record.spec.claim_name != expected_claim:
            msg = (
                f"malformed {REPOSITORY_VOLUME_KIND} {record.name!r}: claim_name "
                f"{record.spec.claim_name!r} does not match expected claim "
                f"{expected_claim!r}"
            )
            raise OSError(msg)
        labels = record.metadata.labels
        if labels.get(REPO_ID_ENV) != repo_id:
            msg = (
                f"malformed {REPOSITORY_VOLUME_KIND} {record.name!r}: repo label "
                f"{REPO_ID_ENV!r} does not match spec repo_id {repo_id!r}"
            )
            raise OSError(msg)
        label_phase = labels.get(REPOSITORY_VOLUME_PHASE_LABEL, "").strip()
        if label_phase != record.spec.phase.lower():
            msg = (
                f"malformed {REPOSITORY_VOLUME_KIND} {record.name!r}: phase label "
                f"{label_phase!r} does not match spec phase {record.spec.phase!r}"
            )
            raise OSError(msg)
        return record

    @property
    def name(self) -> str:
        """Return the Kubernetes custom object name.

        Returns
        -------
        str
            Kubernetes custom object name.
        """
        return self.metadata.name

    @property
    def namespace(self) -> str:
        """Return the namespace that owns this record.

        Returns
        -------
        str
            Kubernetes namespace that owns this record.
        """
        return self.metadata.namespace

    @property
    def generation(self) -> int:
        """Return the Kubernetes metadata generation.

        Returns
        -------
        int
            Kubernetes metadata generation for this record.
        """
        return self.metadata.generation

    @property
    def resource_version(self) -> str:
        """Return the Kubernetes resource version.

        Returns
        -------
        str
            Kubernetes resource version for this record.
        """
        return self.metadata.resource_version

    @property
    def repo_id(self) -> str:
        """Return the managed repository identity.

        Returns
        -------
        str
            Repository UUID associated with this volume.
        """
        return self.spec.repo_id

    @property
    def claim_name(self) -> str:
        """Return the deterministic PVC claim name.

        Returns
        -------
        str
            Kubernetes PVC name for this repository volume.
        """
        return self.spec.claim_name

    @property
    def phase(self) -> _RepositoryVolumePhase:
        """Return the lifecycle phase.

        Returns
        -------
        {"Active", "Retired"}
            Repository volume lifecycle phase.
        """
        return self.spec.phase

    @property
    def created_at(self) -> datetime:
        """Return the lifecycle creation timestamp.

        Returns
        -------
        datetime
            Time this lifecycle record was first created.
        """
        return self.spec.created_at

    @property
    def last_seen_at(self) -> datetime:
        """Return the last successful convergence timestamp.

        Returns
        -------
        datetime
            Time this repository volume was last observed by init convergence.
        """
        return self.spec.last_seen_at

    @property
    def retired_at(self) -> datetime | None:
        """Return the retirement timestamp.

        Returns
        -------
        datetime | None
            Time this repository volume was retired, if retired.
        """
        return self.spec.retired_at

    @property
    def last_gc_at(self) -> datetime | None:
        """Return the last GC attempt timestamp.

        Returns
        -------
        datetime | None
            Time this repository volume was last considered by GC.
        """
        return self.spec.last_gc_at

    @property
    def last_error(self) -> str:
        """Return the last repository-volume GC error.

        Returns
        -------
        str
            Last non-fatal GC error recorded for this volume.
        """
        return self.spec.last_error


_REPOSITORY_VOLUME_SPEC_SCHEMA = {
    "type": "object",
    "required": [
        "repo_id",
        "claim_name",
        "phase",
        "created_at",
        "last_seen_at",
        "last_error",
    ],
    "properties": {
        "repo_id": {"type": "string", "minLength": 1},
        "claim_name": {"type": "string", "minLength": 1},
        "phase": {"type": "string", "enum": ["Active", "Retired"]},
        "created_at": {"type": "string", "format": "date-time"},
        "last_seen_at": {"type": "string", "format": "date-time"},
        "retired_at": {"type": "string", "format": "date-time", "nullable": True},
        "last_gc_at": {"type": "string", "format": "date-time", "nullable": True},
        "last_error": {"type": "string"},
    },
}
_REPOSITORY_VOLUME_SPEC = CustomResourceSpec(
    group=REPOSITORY_VOLUME_GROUP,
    version=REPOSITORY_VOLUME_VERSION,
    kind=REPOSITORY_VOLUME_KIND,
    plural=REPOSITORY_VOLUME_PLURAL,
    labels=_REPOSITORY_VOLUME_LABELS,
)
_REPOSITORY_VOLUME_CLIENT = CustomResourceClient(_REPOSITORY_VOLUME_SPEC)


async def ensure_repository_volume_crd(kube: Kube, *, timeout: float) -> None:
    """Converge the repository-volume lifecycle CRD.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum convergence budget in seconds.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive or CRD establishment exceeds the budget.
    """
    if timeout <= 0:
        msg = "repository volume CRD timeout must be non-negative"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    crd = await CustomResourceDefinition.upsert(
        kube,
        group=REPOSITORY_VOLUME_GROUP,
        version=REPOSITORY_VOLUME_VERSION,
        plural=REPOSITORY_VOLUME_PLURAL,
        singular="cephrepositoryvolume",
        kind=REPOSITORY_VOLUME_KIND,
        short_names=("crv",),
        spec_schema=_REPOSITORY_VOLUME_SPEC_SCHEMA,
        labels=_REPOSITORY_VOLUME_LABELS,
        timeout=deadline - loop.time(),
    )
    await crd.wait_established(kube, timeout=deadline - loop.time())


async def get_repository_volume_record(
    kube: Kube,
    *,
    repo_id: str,
    timeout: float,
) -> CephRepositoryVolumeRecord | None:
    """Read one repository-volume lifecycle record by repository ID.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : str
        Stable repository UUID.
    timeout : float
        Maximum request budget in seconds.

    Returns
    -------
    CephRepositoryVolumeRecord | None
        Repository-volume lifecycle record, or None if it does not exist.
    """
    repo_id = _check_uuid(repo_id)
    obj = await _REPOSITORY_VOLUME_CLIENT.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=RepoVolume.claim_name(repo_id),
        timeout=timeout,
    )
    if obj is None:
        return None
    return CephRepositoryVolumeRecord.from_payload(obj.payload)


async def list_repository_volume_records(
    kube: Kube,
    *,
    timeout: float,
    labels: Mapping[str, str] | None = None,
) -> list[CephRepositoryVolumeRecord]:
    """List repository-volume lifecycle records.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum request budget in seconds.
    labels : Mapping[str, str] | None, optional
        Optional exact-match label selector.

    Returns
    -------
    list[CephRepositoryVolumeRecord]
        Validated repository-volume lifecycle records.
    """
    objects = await _REPOSITORY_VOLUME_CLIENT.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        labels=labels,
        timeout=timeout,
    )
    return [CephRepositoryVolumeRecord.from_payload(obj.payload) for obj in objects]


async def ensure_repository_volume_record(
    kube: Kube,
    *,
    repo_id: str,
    timeout: float,
) -> CephRepositoryVolumeRecord:
    """Mark a managed repository volume as active and recently observed.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : str
        Stable repository UUID.
    timeout : float
        Maximum convergence budget in seconds.

    Returns
    -------
    CephRepositoryVolumeRecord
        Active lifecycle record for the repository volume.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive or convergence exceeds the budget.
    """
    if timeout <= 0:
        msg = "repository volume lifecycle convergence timeout must be non-negative"
        raise TimeoutError(msg)
    repo_id = _check_uuid(repo_id)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    await ensure_repository_volume_crd(kube, timeout=deadline - loop.time())
    existing = await get_repository_volume_record(
        kube,
        repo_id=repo_id,
        timeout=deadline - loop.time(),
    )
    now = datetime.now(UTC)
    created_at = existing.created_at if existing is not None else now
    obj = await _REPOSITORY_VOLUME_CLIENT.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=RepoVolume.claim_name(repo_id),
        spec=_repository_volume_spec_payload(
            repo_id=repo_id,
            phase="Active",
            created_at=created_at,
            last_seen_at=now,
            retired_at=None,
            last_gc_at=None,
            last_error="",
        ),
        labels=_repository_volume_labels(repo_id=repo_id, phase="Active"),
        timeout=deadline - loop.time(),
    )
    return CephRepositoryVolumeRecord.from_payload(obj.payload)


async def retire_repository_volume(
    kube: Kube,
    *,
    repo_id: str,
    timeout: float,
) -> CephRepositoryVolumeRecord:
    """Retire one repository volume lifecycle record without deleting storage.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : str
        Stable repository UUID.
    timeout : float
        Maximum request budget in seconds.

    Returns
    -------
    CephRepositoryVolumeRecord
        Retired lifecycle record.

    Raises
    ------
    OSError
        If no repository-volume lifecycle record exists.
    TimeoutError
        If `timeout` is non-positive or retirement exceeds the budget.
    """
    if timeout <= 0:
        msg = "repository volume retirement timeout must be non-negative"
        raise TimeoutError(msg)
    repo_id = _check_uuid(repo_id)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    await ensure_repository_volume_crd(kube, timeout=deadline - loop.time())
    record = await get_repository_volume_record(
        kube,
        repo_id=repo_id,
        timeout=deadline - loop.time(),
    )
    if record is None:
        msg = f"cannot retire untracked repository volume for {repo_id!r}"
        raise OSError(msg)
    retired_at = record.retired_at or datetime.now(UTC)
    return await _transition_repository_volume(
        kube,
        record=record,
        phase="Retired",
        last_seen_at=record.last_seen_at,
        retired_at=retired_at,
        last_gc_at=record.last_gc_at,
        last_error="",
        timeout=deadline - loop.time(),
    )


async def gc_repository_volumes(
    kube: Kube,
    *,
    timeout: float,
    grace_seconds: int = REPOSITORY_VOLUME_GC_GRACE_SECONDS,
    limit: int = REPOSITORY_VOLUME_GC_LIMIT,
) -> list[CephRepositoryVolumeRecord]:
    """Delete eligible retired repository volumes and lifecycle records.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum GC budget in seconds.
    grace_seconds : int, optional
        Minimum time a record must remain retired before collection.
    limit : int, optional
        Maximum number of repository volumes to collect in this pass.

    Returns
    -------
    list[CephRepositoryVolumeRecord]
        Records collected during this GC pass.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive or GC exceeds the budget.
    ValueError
        If `grace_seconds` or `limit` is negative.
    """
    from bertrand.env.kube.build.controller import has_active_buildkit_builds
    from bertrand.env.kube.ceph.auth import RepoCredentials

    if timeout <= 0:
        msg = "repository volume GC timeout must be non-negative"
        raise TimeoutError(msg)
    if grace_seconds < 0:
        msg = "repository volume GC grace_seconds must be non-negative"
        raise ValueError(msg)
    if limit < 0:
        msg = "repository volume GC limit must be non-negative"
        raise ValueError(msg)
    if limit == 0:
        return []

    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    records = await list_repository_volume_records(
        kube,
        labels={REPOSITORY_VOLUME_PHASE_LABEL: "retired"},
        timeout=deadline - loop.time(),
    )
    now = datetime.now(UTC)
    collected: list[CephRepositoryVolumeRecord] = []
    grace = timedelta(seconds=grace_seconds)
    for record in sorted(records, key=_repository_volume_gc_sort_key):
        if len(collected) >= limit:
            break
        if not _repository_volume_gc_eligible(record, now=now, grace=grace):
            continue
        try:
            if await has_active_buildkit_builds(
                kube,
                repo_id=record.repo_id,
                timeout=deadline - loop.time(),
            ):
                continue
            volumes = await RepoVolume.list(
                kube,
                record.repo_id,
                timeout=deadline - loop.time(),
            )
            if len(volumes) > 1:
                _raise_ambiguous_repository_claims(
                    repo_id=record.repo_id,
                    volumes=volumes,
                )
            if volumes:
                _assert_record_matches_volume(record, volumes[0])
                await volumes[0].delete(
                    kube,
                    timeout=deadline - loop.time(),
                    force=False,
                )
            credentials = await RepoCredentials.get(
                record.repo_id,
                timeout=deadline - loop.time(),
            )
            if credentials is not None:
                await credentials.delete(timeout=deadline - loop.time())
            await _REPOSITORY_VOLUME_CLIENT.delete(
                kube,
                namespace=BERTRAND_NAMESPACE,
                name=record.name,
                timeout=deadline - loop.time(),
            )
        except (OSError, TimeoutError, ValueError) as err:
            await _transition_repository_volume(
                kube,
                record=record,
                phase="Retired",
                last_seen_at=record.last_seen_at,
                retired_at=record.retired_at,
                last_gc_at=now,
                last_error=str(err),
                timeout=deadline - loop.time(),
            )
            continue
        collected.append(record)
    return collected


async def next_repository_volume_gc_time(
    kube: Kube,
    *,
    timeout: float,
    grace_seconds: int = REPOSITORY_VOLUME_GC_GRACE_SECONDS,
) -> datetime | None:
    """Return the next time retired repository volumes may be GC-eligible.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum request budget in seconds.
    grace_seconds : int, optional
        Minimum time a record must remain retired before collection.

    Returns
    -------
    datetime | None
        Earliest retirement grace boundary among retired records, or None when
        there are no retired records.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    ValueError
        If `grace_seconds` is negative.
    """
    if timeout <= 0:
        msg = "repository volume GC scheduling timeout must be non-negative"
        raise TimeoutError(msg)
    if grace_seconds < 0:
        msg = "repository volume GC grace_seconds must be non-negative"
        raise ValueError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    records = await list_repository_volume_records(
        kube,
        labels={REPOSITORY_VOLUME_PHASE_LABEL: "retired"},
        timeout=deadline - loop.time(),
    )
    if not records:
        return None
    now = datetime.now(UTC)
    boundaries = [
        record.retired_at + timedelta(seconds=grace_seconds)
        for record in records
        if record.retired_at is not None
    ]
    if len(boundaries) != len(records):
        return now
    return min(boundaries)


async def _transition_repository_volume(
    kube: Kube,
    *,
    record: CephRepositoryVolumeRecord,
    phase: _RepositoryVolumePhase,
    last_seen_at: datetime,
    retired_at: datetime | None,
    last_gc_at: datetime | None,
    last_error: str,
    timeout: float,
) -> CephRepositoryVolumeRecord:
    if timeout <= 0:
        msg = "repository volume lifecycle transition timeout must be non-negative"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    obj = await _REPOSITORY_VOLUME_CLIENT.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=record.name,
        spec=_repository_volume_spec_payload(
            repo_id=record.repo_id,
            phase=phase,
            created_at=record.created_at,
            last_seen_at=last_seen_at,
            retired_at=retired_at,
            last_gc_at=last_gc_at,
            last_error=last_error,
        ),
        labels=_repository_volume_labels(repo_id=record.repo_id, phase=phase),
        timeout=deadline - loop.time(),
    )
    return CephRepositoryVolumeRecord.from_payload(obj.payload)


def _repository_volume_spec_payload(
    *,
    repo_id: str,
    phase: _RepositoryVolumePhase,
    created_at: datetime,
    last_seen_at: datetime,
    retired_at: datetime | None,
    last_gc_at: datetime | None,
    last_error: str,
) -> dict[str, object]:
    return {
        "repo_id": repo_id,
        "claim_name": RepoVolume.claim_name(repo_id),
        "phase": phase,
        "created_at": _datetime_payload(created_at),
        "last_seen_at": _datetime_payload(last_seen_at),
        "retired_at": _datetime_payload(retired_at),
        "last_gc_at": _datetime_payload(last_gc_at),
        "last_error": last_error,
    }


def _repository_volume_labels(
    *,
    repo_id: str,
    phase: _RepositoryVolumePhase,
) -> dict[str, str]:
    return {
        **_REPOSITORY_VOLUME_LABELS,
        REPO_ID_ENV: repo_id,
        REPOSITORY_VOLUME_PHASE_LABEL: phase.lower(),
    }


def _repository_volume_gc_eligible(
    record: CephRepositoryVolumeRecord,
    *,
    now: datetime,
    grace: timedelta,
) -> bool:
    if record.phase != "Retired" or record.retired_at is None:
        return False
    return now - record.retired_at >= grace


def _repository_volume_gc_sort_key(
    record: CephRepositoryVolumeRecord,
) -> tuple[datetime, str]:
    return (
        record.retired_at or datetime.max.replace(tzinfo=UTC),
        record.name,
    )


def _assert_record_matches_volume(
    record: CephRepositoryVolumeRecord,
    volume: RepoVolume,
) -> None:
    if volume.repo_id != record.repo_id or volume.pvc.name != record.claim_name:
        msg = (
            f"{REPOSITORY_VOLUME_KIND} {record.name!r} points to "
            f"{record.repo_id}/{record.claim_name}, but discovered PVC "
            f"{volume.repo_id}/{volume.pvc.name}"
        )
        raise OSError(msg)


def _raise_ambiguous_repository_claims(
    *,
    repo_id: str,
    volumes: list[RepoVolume],
) -> None:
    names = ", ".join(sorted(volume.pvc.name for volume in volumes))
    msg = (
        "repository identity maps to multiple cluster claims and cannot be "
        f"garbage-collected safely for {repo_id!r}: {names}"
    )
    raise OSError(msg)


def _datetime_payload(value: datetime | None) -> str | None:
    if value is None:
        return None
    if value.tzinfo is None:
        value = value.replace(tzinfo=UTC)
    return value.astimezone(UTC).isoformat()
