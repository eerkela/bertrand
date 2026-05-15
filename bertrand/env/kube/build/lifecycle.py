"""Project image lifecycle records and bounded registry garbage collection."""

from __future__ import annotations

import asyncio
import hashlib
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta
from pathlib import Path
from types import MappingProxyType
from typing import TYPE_CHECKING, Annotated, Literal

from pydantic import BaseModel, ConfigDict, Field, ValidationError

from bertrand.env.git import BERTRAND_ENV, BERTRAND_NAMESPACE
from bertrand.env.kube.build.refs import (
    DIGEST_REF_RE,
    channel_refs,
    digest_from_ref,
    digest_ref,
)
from bertrand.env.kube.build.repository import IMAGES, ImageRepository
from bertrand.env.kube.crd import CustomResourceDefinition
from bertrand.env.kube.custom_object import (
    CustomObjectClient,
    CustomObjectMetadata,
    CustomObjectSpec,
)
from bertrand.env.kube.pod import Pod

if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence

    from bertrand.env.kube.api.client import Kube
    from bertrand.env.kube.build.request import ProjectImageIdentity

PROJECT_IMAGE_GROUP = "build.bertrand.dev"
PROJECT_IMAGE_VERSION = "v1alpha1"
PROJECT_IMAGE_KIND = "BertrandImage"
PROJECT_IMAGE_PLURAL = "bertrandimages"
PROJECT_IMAGE_LABEL = "bertrand.dev/project-image"
PROJECT_IMAGE_LABEL_VALUE = "v1"
PROJECT_IMAGE_REPO_LABEL = "bertrand.dev/project-image-repo"
PROJECT_IMAGE_WORKTREE_LABEL = "bertrand.dev/project-image-worktree"
PROJECT_IMAGE_TAG_LABEL = "bertrand.dev/project-image-tag"
PROJECT_IMAGE_ENV_LABEL = "bertrand.dev/project-image-env"
PROJECT_IMAGE_CONFIG_LABEL = "bertrand.dev/project-image-config"
PROJECT_IMAGE_PHASE_LABEL = "bertrand.dev/project-image-phase"
PROJECT_IMAGE_GC_GRACE_SECONDS = 86_400
PROJECT_IMAGE_GC_LIMIT = 16

type _ProjectImagePhase = Literal["Active", "Retired"]
type _NonEmptyString = Annotated[str, Field(min_length=1)]


class _ProjectImageSpec(BaseModel):
    """Desired identity for one published project image digest."""

    model_config = ConfigDict(extra="forbid")
    repo_id: _NonEmptyString
    worktree: _NonEmptyString
    tag: str
    env_id: _NonEmptyString
    image: _NonEmptyString
    digest_ref: _NonEmptyString
    platform_images: dict[_NonEmptyString, _NonEmptyString]
    channels: list[_NonEmptyString] = Field(default_factory=list)
    config_id: _NonEmptyString
    phase: _ProjectImagePhase = "Active"
    published_at: datetime | None = None
    retired_at: datetime | None = None
    last_gc_at: datetime | None = None
    last_error: str = ""

    @classmethod
    def published(
        cls,
        *,
        identity: ProjectImageIdentity,
        digest_ref: str,
        platform_images: Mapping[str, str],
        channel_names: Sequence[str],
        published_at: datetime,
    ) -> _ProjectImageSpec:
        return cls(
            repo_id=identity.repo_id,
            worktree=identity.worktree,
            tag=identity.tag,
            env_id=identity.env_id,
            image=identity.image,
            digest_ref=digest_ref,
            platform_images=dict(sorted(platform_images.items())),
            channels=sorted(channel_names),
            config_id=identity.config_id,
            phase="Active",
            published_at=published_at,
            retired_at=None,
            last_gc_at=None,
            last_error="",
        )

    @staticmethod
    def identity_labels(
        *,
        repo_id: str,
        worktree: str,
        tag: str | None,
    ) -> dict[str, str]:
        labels = {
            PROJECT_IMAGE_REPO_LABEL: _label_hash(repo_id),
            PROJECT_IMAGE_WORKTREE_LABEL: _label_hash(worktree),
        }
        if tag is not None:
            labels[PROJECT_IMAGE_TAG_LABEL] = _label_hash(tag)
        return labels

    @property
    def labels(self) -> dict[str, str]:
        return {
            **_PROJECT_IMAGE_LABELS,
            **self.identity_labels(
                repo_id=self.repo_id,
                worktree=self.worktree,
                tag=self.tag,
            ),
            PROJECT_IMAGE_ENV_LABEL: _label_hash(self.env_id),
            PROJECT_IMAGE_CONFIG_LABEL: _label_hash(self.config_id),
            PROJECT_IMAGE_PHASE_LABEL: self.phase.lower(),
        }

    def payload(self) -> dict[str, object]:
        return {
            "repo_id": self.repo_id,
            "worktree": self.worktree,
            "tag": self.tag,
            "env_id": self.env_id,
            "image": self.image,
            "digest_ref": self.digest_ref,
            "platform_images": dict(sorted(self.platform_images.items())),
            "channels": sorted(self.channels),
            "config_id": self.config_id,
            "phase": self.phase,
            "published_at": _datetime_payload(self.published_at),
            "retired_at": _datetime_payload(self.retired_at),
            "last_gc_at": _datetime_payload(self.last_gc_at),
            "last_error": self.last_error,
        }


_PROJECT_IMAGE_SPEC_SCHEMA = {
    "type": "object",
    "required": [
        "repo_id",
        "worktree",
        "tag",
        "env_id",
        "image",
        "digest_ref",
        "platform_images",
        "config_id",
        "phase",
        "published_at",
    ],
    "properties": {
        "repo_id": {"type": "string", "minLength": 1},
        "worktree": {"type": "string", "minLength": 1},
        "tag": {"type": "string"},
        "env_id": {"type": "string", "minLength": 1},
        "image": {"type": "string", "minLength": 1},
        "digest_ref": {
            "type": "string",
            "minLength": 1,
            "pattern": DIGEST_REF_RE.pattern,
        },
        "platform_images": {
            "type": "object",
            "minProperties": 1,
            "additionalProperties": {
                "type": "string",
                "pattern": DIGEST_REF_RE.pattern,
            },
        },
        "channels": {
            "type": "array",
            "items": {"type": "string", "minLength": 1},
            "uniqueItems": True,
        },
        "config_id": {"type": "string", "minLength": 1},
        "phase": {"type": "string", "enum": ["Active", "Retired"]},
        "published_at": {"type": "string", "format": "date-time", "nullable": True},
        "retired_at": {"type": "string", "format": "date-time", "nullable": True},
        "last_gc_at": {"type": "string", "format": "date-time", "nullable": True},
        "last_error": {"type": "string"},
    },
}
_PROJECT_IMAGE_LABELS = {
    BERTRAND_ENV: "1",
    PROJECT_IMAGE_LABEL: PROJECT_IMAGE_LABEL_VALUE,
}
_PROJECT_IMAGE_SPEC = CustomObjectSpec(
    group=PROJECT_IMAGE_GROUP,
    version=PROJECT_IMAGE_VERSION,
    kind=PROJECT_IMAGE_KIND,
    plural=PROJECT_IMAGE_PLURAL,
    labels=_PROJECT_IMAGE_LABELS,
)
_PROJECT_IMAGE_CLIENT = CustomObjectClient(_PROJECT_IMAGE_SPEC)


class ProjectImageRecord(BaseModel):
    """Read-only lifecycle record for one published project image digest.

    Parameters
    ----------
    api_version : str
        Kubernetes API version reported by the custom object.
    kind : {"BertrandImage"}
        Kubernetes custom object kind.
    metadata : CustomObjectMetadata
        Kubernetes object metadata.
    spec : _ProjectImageSpec
        Project image lifecycle spec.
    """

    model_config = ConfigDict(extra="forbid", frozen=True, populate_by_name=True)
    api_version: str = Field(alias="apiVersion")
    kind: Literal["BertrandImage"]
    metadata: CustomObjectMetadata
    spec: _ProjectImageSpec

    @classmethod
    def from_payload(cls, payload: object) -> ProjectImageRecord:
        """Validate a Kubernetes custom object payload.

        Parameters
        ----------
        payload : object
            Raw Kubernetes custom object payload.

        Returns
        -------
        ProjectImageRecord
            Validated project image lifecycle record.

        Raises
        ------
        OSError
            If the payload is malformed.
        """
        try:
            record = cls.model_validate(payload)
        except ValidationError as err:
            msg = f"malformed {PROJECT_IMAGE_KIND} custom object: {err}"
            raise OSError(msg) from err
        label_phase = record.metadata.labels.get(PROJECT_IMAGE_PHASE_LABEL, "").strip()
        if label_phase != record.spec.phase.lower():
            msg = (
                f"malformed {PROJECT_IMAGE_KIND} {record.name!r}: phase label "
                f"{label_phase!r} does not match spec phase {record.spec.phase!r}"
            )
            raise OSError(msg)
        if not DIGEST_REF_RE.fullmatch(record.spec.digest_ref):
            msg = (
                f"malformed {PROJECT_IMAGE_KIND} {record.name!r}: unsupported "
                f"digest ref {record.spec.digest_ref!r}"
            )
            raise OSError(msg)
        try:
            _ = record.digest
        except ValueError as err:
            msg = f"malformed {PROJECT_IMAGE_KIND} {record.name!r}: {err}"
            raise OSError(msg) from err
        _validate_platform_images(
            platform_images=record.platform_images,
            context=f"{PROJECT_IMAGE_KIND} {record.name!r}",
        )
        _validate_channels(
            channels=record.channels,
            context=f"{PROJECT_IMAGE_KIND} {record.name!r}",
        )
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
    def repo_id(self) -> str:
        """Return the stable repository identity.

        Returns
        -------
        str
            Stable repository UUID.
        """
        return self.spec.repo_id

    @property
    def worktree(self) -> str:
        """Return the repository worktree identity.

        Returns
        -------
        str
            Repository-relative worktree identity.
        """
        return self.spec.worktree

    @property
    def tag(self) -> str:
        """Return the configured project image key.

        Returns
        -------
        str
            Configured image key from ``[tool.bertrand.image]``.
        """
        return self.spec.tag

    @property
    def env_id(self) -> str:
        """Return the deterministic capability environment identity.

        Returns
        -------
        str
            Deterministic capability environment UUID.
        """
        return self.spec.env_id

    @property
    def image(self) -> str:
        """Return the mutable image reference.

        Returns
        -------
        str
            Mutable image reference published by the build.
        """
        return self.spec.image

    @property
    def digest_ref(self) -> str:
        """Return the immutable digest-pinned image reference.

        Returns
        -------
        str
            Digest-pinned image reference for the published manifest.
        """
        return self.spec.digest_ref

    @property
    def digest(self) -> str:
        """Return the OCI manifest digest.

        Returns
        -------
        str
            OCI manifest digest derived from ``digest_ref``.
        """
        return digest_from_ref(self.spec.digest_ref)

    @property
    def platforms(self) -> tuple[str, ...]:
        """Return platforms included in this publication.

        Returns
        -------
        tuple[str, ...]
            Platforms derived from ``platform_images``.
        """
        return tuple(self.platform_images)

    @property
    def platform_images(self) -> Mapping[str, str]:
        """Return platform-specific digest refs.

        Returns
        -------
        Mapping[str, str]
            Read-only mapping from platform to owned platform-image digest ref.
        """
        return MappingProxyType(dict(sorted(self.spec.platform_images.items())))

    @property
    def channels(self) -> Mapping[str, str]:
        """Return internal mutable channel refs.

        Returns
        -------
        Mapping[str, str]
            Read-only mapping from channel name to internal mutable channel ref.
        """
        channel_names = _normalize_channel_names(self.spec.channels)
        return MappingProxyType(channel_refs(self.spec.image, channel_names))

    @property
    def config_id(self) -> str:
        """Return the project image configuration identity.

        Returns
        -------
        str
            Deterministic hash of the image configuration inputs.
        """
        return self.spec.config_id

    @property
    def phase(self) -> _ProjectImagePhase:
        """Return the lifecycle phase.

        Returns
        -------
        {"Active", "Retired"}
            Lifecycle phase used by bounded garbage collection.
        """
        return self.spec.phase

    @property
    def published_at(self) -> datetime | None:
        """Return the publication timestamp.

        Returns
        -------
        datetime | None
            Time the digest was published.
        """
        return _utc_datetime(self.spec.published_at)

    @property
    def retired_at(self) -> datetime | None:
        """Return the retirement timestamp.

        Returns
        -------
        datetime | None
            Time the digest was retired, if retired.
        """
        return _utc_datetime(self.spec.retired_at)

    @property
    def last_gc_at(self) -> datetime | None:
        """Return the last GC attempt timestamp.

        Returns
        -------
        datetime | None
            Time this record was last considered by GC.
        """
        return _utc_datetime(self.spec.last_gc_at)

    @property
    def last_error(self) -> str:
        """Return the last project-image GC error.

        Returns
        -------
        str
            Last non-fatal GC error recorded for this image.
        """
        return self.spec.last_error

    @property
    def channel_digest_refs(self) -> Mapping[str, str]:
        """Return internal digest-pinned channel refs.

        Returns
        -------
        Mapping[str, str]
            Read-only channel digest refs derived from this record's published
            digest.
        """
        return MappingProxyType(
            {name: digest_ref(ref, self.digest) for name, ref in self.channels.items()}
        )

    def _lifecycle_spec(
        self,
        *,
        phase: _ProjectImagePhase,
        retired_at: datetime | None,
        last_gc_at: datetime | None,
        last_error: str,
    ) -> _ProjectImageSpec:
        return _ProjectImageSpec(
            repo_id=self.repo_id,
            worktree=self.worktree,
            tag=self.tag,
            env_id=self.env_id,
            image=self.image,
            digest_ref=self.digest_ref,
            platform_images=dict(self.platform_images),
            channels=list(self.spec.channels),
            config_id=self.config_id,
            phase=phase,
            published_at=self.published_at,
            retired_at=retired_at,
            last_gc_at=last_gc_at,
            last_error=last_error,
        )


@dataclass(frozen=True)
class ProjectImagePublication:
    """Result for one cluster-native project image publication.

    Parameters
    ----------
    record : ProjectImageRecord
        Active lifecycle record written for the published digest.
    external_digest_ref : str | None, optional
        External digest-pinned image reference reported after copy.
    external_channel_digest_refs : Mapping[str, str], optional
        Read-only mapping from external channel name to digest-pinned ref.
    """

    record: ProjectImageRecord
    external_digest_ref: str | None = None
    external_channel_digest_refs: Mapping[str, str] = MappingProxyType({})


async def ensure_project_image_crd(kube: Kube, *, timeout: float) -> None:
    """Converge the project image lifecycle CRD.

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
        msg = "project image CRD timeout must be non-negative"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    crd = await CustomResourceDefinition.upsert(
        kube,
        group=PROJECT_IMAGE_GROUP,
        version=PROJECT_IMAGE_VERSION,
        plural=PROJECT_IMAGE_PLURAL,
        singular="bertrandimage",
        kind=PROJECT_IMAGE_KIND,
        short_names=("bimg",),
        spec_schema=_PROJECT_IMAGE_SPEC_SCHEMA,
        labels=_PROJECT_IMAGE_LABELS,
        timeout=deadline - loop.time(),
    )
    await crd.wait_established(kube, timeout=deadline - loop.time())


async def list_project_images(
    kube: Kube,
    *,
    timeout: float,
    labels: Mapping[str, str] | None = None,
) -> list[ProjectImageRecord]:
    """List project image lifecycle records.

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
    list[ProjectImageRecord]
        Validated project image lifecycle records.
    """
    objects = await _PROJECT_IMAGE_CLIENT.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        labels=labels,
        timeout=timeout,
    )
    return [ProjectImageRecord.from_payload(obj.payload) for obj in objects]


async def get_project_image(
    kube: Kube,
    *,
    name: str,
    timeout: float,
) -> ProjectImageRecord | None:
    """Read one project image lifecycle record by name.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    name : str
        Lifecycle record name.
    timeout : float
        Maximum request budget in seconds.

    Returns
    -------
    ProjectImageRecord | None
        Project image record, or `None` if it does not exist.
    """
    obj = await _PROJECT_IMAGE_CLIENT.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=name,
        timeout=timeout,
    )
    if obj is None:
        return None
    return ProjectImageRecord.from_payload(obj.payload)


async def retire_project_images(
    kube: Kube,
    *,
    repo_id: str,
    worktree: str,
    timeout: float,
    tag: str | None = None,
) -> list[ProjectImageRecord]:
    """Retire active project image records without deleting registry manifests.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : str
        Stable repository UUID.
    worktree : str
        Repository-relative worktree identity, or ``"."`` for the repository root.
    timeout : float
        Maximum request budget in seconds.
    tag : str | None, optional
        Optional configured image key filter. If omitted, active records for every
        key in the repo/worktree identity are retired.

    Returns
    -------
    list[ProjectImageRecord]
        Records transitioned from `Active` to `Retired`.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive or retirement exceeds the budget.
    """
    from bertrand.env.config.core import _check_uuid

    if timeout <= 0:
        msg = "project image retirement timeout must be non-negative"
        raise TimeoutError(msg)
    repo_id = _check_uuid(repo_id)
    worktree = worktree_identity(worktree)
    if tag is not None:
        tag = tag.strip()

    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    await ensure_project_image_crd(kube, timeout=deadline - loop.time())
    records = await list_project_images(
        kube,
        labels=_ProjectImageSpec.identity_labels(
            repo_id=repo_id,
            worktree=worktree,
            tag=tag,
        ),
        timeout=deadline - loop.time(),
    )
    now = datetime.now(UTC)
    retired: list[ProjectImageRecord] = []
    for record in sorted(records, key=lambda item: (item.tag, item.name)):
        if record.phase != "Active":
            continue
        retired.append(
            await _transition_project_image(
                kube,
                record=record,
                phase="Retired",
                retired_at=record.retired_at or now,
                last_error="",
                timeout=deadline - loop.time(),
            )
        )
    return retired


async def gc_project_images(
    kube: Kube,
    *,
    timeout: float,
    grace_seconds: int = PROJECT_IMAGE_GC_GRACE_SECONDS,
    limit: int = PROJECT_IMAGE_GC_LIMIT,
    repository: ImageRepository = IMAGES,
) -> list[ProjectImageRecord]:
    """Delete eligible retired project image manifests and records.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum GC budget in seconds.
    grace_seconds : int, optional
        Minimum time a record must remain retired before collection.
    limit : int, optional
        Maximum number of records to delete in this pass.
    repository : ImageRepository, optional
        Registry storage plane used for manifest deletion.

    Returns
    -------
    list[ProjectImageRecord]
        Records collected during this GC pass.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive or GC exceeds the budget.
    ValueError
        If `grace_seconds` or `limit` is negative.
    """
    if timeout <= 0:
        msg = "project image GC timeout must be non-negative"
        raise TimeoutError(msg)
    if grace_seconds < 0:
        msg = "project image GC grace_seconds must be non-negative"
        raise ValueError(msg)
    if limit < 0:
        msg = "project image GC limit must be non-negative"
        raise ValueError(msg)
    if limit == 0:
        return []

    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    await ensure_project_image_crd(kube, timeout=deadline - loop.time())
    records = await list_project_images(
        kube,
        labels={PROJECT_IMAGE_PHASE_LABEL: "retired"},
        timeout=deadline - loop.time(),
    )
    active_records = await list_project_images(
        kube,
        labels={PROJECT_IMAGE_PHASE_LABEL: "active"},
        timeout=deadline - loop.time(),
    )
    live_refs = await _active_pod_image_refs(kube, timeout=deadline - loop.time())
    active_digest_refs = _active_record_digest_refs(active_records)
    now = datetime.now(UTC)
    collected: list[ProjectImageRecord] = []
    for record in sorted(records, key=_gc_sort_key):
        if len(collected) >= limit:
            break
        if not _gc_eligible(
            record,
            now=now,
            grace=timedelta(seconds=grace_seconds),
            live_refs=live_refs,
            active_digest_refs=active_digest_refs,
        ):
            continue
        try:
            for digest_ref in _record_digest_refs(record):
                await repository.delete_manifest(
                    digest_ref,
                    timeout=deadline - loop.time(),
                )
            await _PROJECT_IMAGE_CLIENT.delete_by_name(
                kube,
                namespace=BERTRAND_NAMESPACE,
                name=record.name,
                timeout=deadline - loop.time(),
            )
        except (OSError, TimeoutError, ValueError) as err:
            await _transition_project_image(
                kube,
                record=record,
                phase="Retired",
                retired_at=record.retired_at,
                last_gc_at=now,
                last_error=str(err),
                timeout=deadline - loop.time(),
            )
            continue
        collected.append(record)
    return collected


async def next_project_image_gc_time(
    kube: Kube,
    *,
    timeout: float,
    grace_seconds: int = PROJECT_IMAGE_GC_GRACE_SECONDS,
) -> datetime | None:
    """Return the next time retired project images may be GC-eligible.

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
        Earliest retirement grace boundary among retired records, or `None` when
        there are no retired records.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    ValueError
        If `grace_seconds` is negative.

    Notes
    -----
    This is a cheap scheduling hint only.  Manifest deletion safety remains centralized
    in :func:`gc_project_images`.
    """
    if timeout <= 0:
        msg = "project image GC scheduling timeout must be non-negative"
        raise TimeoutError(msg)
    if grace_seconds < 0:
        msg = "project image GC scheduling grace_seconds must be non-negative"
        raise ValueError(msg)

    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    await ensure_project_image_crd(kube, timeout=deadline - loop.time())
    records = await list_project_images(
        kube,
        labels={PROJECT_IMAGE_PHASE_LABEL: "retired"},
        timeout=deadline - loop.time(),
    )
    if not records:
        return None

    now = datetime.now(UTC)
    grace = timedelta(seconds=grace_seconds)
    next_times = [
        record.retired_at + grace if record.retired_at is not None else now
        for record in records
    ]
    return min(next_times)


async def record_project_image(
    kube: Kube,
    *,
    identity: ProjectImageIdentity,
    image_digest_ref: str,
    platform_images: Mapping[str, str],
    channel_digest_refs: Mapping[str, str],
    timeout: float,
) -> ProjectImageRecord:
    """Record a published project image manifest and retire superseded peers.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    identity : ProjectImageIdentity
        Executed project image identity.
    image_digest_ref : str
        Immutable digest-pinned ref for the project image.
    platform_images : Mapping[str, str]
        Digest-pinned platform refs that were included in the manifest.
    channel_digest_refs : Mapping[str, str]
        Digest-pinned internal channel refs emitted by the manifest job.
    timeout : float
        Maximum record update budget in seconds.

    Returns
    -------
    ProjectImageRecord
        Active lifecycle record for the published manifest.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    OSError
        If the manifest result is inconsistent with the project image identity.
    """
    if timeout <= 0:
        msg = "project image record update timeout must be non-negative"
        raise TimeoutError(msg)
    try:
        digest = digest_from_ref(image_digest_ref)
    except ValueError as err:
        raise OSError(str(err)) from err
    expected_digest_ref = digest_ref(identity.image, digest)
    if image_digest_ref != expected_digest_ref:
        msg = (
            "project image manifest result digest ref does not match identity image "
            f"and digest: {image_digest_ref!r}"
        )
        raise OSError(msg)
    platform_images = dict(platform_images)
    channel_names = _normalize_channel_names(identity.channels)
    channels = channel_refs(identity.image, channel_names)
    _validate_channel_digests(
        channels=channels,
        digest=digest,
        actual=channel_digest_refs,
    )
    _validate_platform_images(
        platform_images=platform_images,
        context=f"project image {identity.tag!r}",
    )
    now = datetime.now(UTC)
    record = await _upsert_project_image_record(
        kube,
        identity=identity,
        digest=digest,
        digest_ref=expected_digest_ref,
        platform_images=platform_images,
        channel_names=channel_names,
        published_at=now,
        timeout=timeout,
    )
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    peers = await list_project_images(
        kube,
        labels=_ProjectImageSpec.identity_labels(
            repo_id=identity.repo_id,
            worktree=identity.worktree,
            tag=identity.tag,
        ),
        timeout=deadline - loop.time(),
    )
    for peer in peers:
        if peer.name == record.name or peer.phase != "Active":
            continue
        await _transition_project_image(
            kube,
            record=peer,
            phase="Retired",
            retired_at=peer.retired_at or now,
            last_error="",
            timeout=deadline - loop.time(),
        )
    return record


def worktree_identity(worktree: Path | str) -> str:
    """Normalize a repository worktree identity.

    Parameters
    ----------
    worktree : Path | str
        Repository-relative worktree path.

    Returns
    -------
    str
        Normalized worktree identity, using ``"."`` for the repository root.
    """
    value = worktree.as_posix() if isinstance(worktree, Path) else str(worktree).strip()
    return value if value and value != "." else "."


async def _upsert_project_image_record(
    kube: Kube,
    *,
    identity: ProjectImageIdentity,
    digest: str,
    digest_ref: str,
    platform_images: Mapping[str, str],
    channel_names: Sequence[str],
    published_at: datetime,
    timeout: float,
) -> ProjectImageRecord:
    name = _record_name(
        repo_id=identity.repo_id,
        worktree=identity.worktree,
        tag=identity.tag,
        digest=digest,
    )
    spec = _ProjectImageSpec.published(
        identity=identity,
        digest_ref=digest_ref,
        platform_images=platform_images,
        channel_names=channel_names,
        published_at=published_at,
    )
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    obj = await _PROJECT_IMAGE_CLIENT.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=name,
        spec=spec.payload(),
        labels=spec.labels,
        timeout=deadline - loop.time(),
    )
    return ProjectImageRecord.from_payload(obj.payload)


async def _transition_project_image(
    kube: Kube,
    *,
    record: ProjectImageRecord,
    phase: _ProjectImagePhase,
    retired_at: datetime | None,
    last_error: str,
    last_gc_at: datetime | None = None,
    timeout: float,
) -> ProjectImageRecord:
    if timeout <= 0:
        msg = "project image phase transition timeout must be non-negative"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    spec = record._lifecycle_spec(
        phase=phase,
        retired_at=retired_at,
        last_gc_at=record.last_gc_at if last_gc_at is None else last_gc_at,
        last_error=last_error,
    )
    obj = await _PROJECT_IMAGE_CLIENT.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=record.name,
        spec=spec.payload(),
        labels=spec.labels,
        timeout=deadline - loop.time(),
    )
    return ProjectImageRecord.from_payload(obj.payload)


async def _active_pod_image_refs(kube: Kube, *, timeout: float) -> frozenset[str]:
    pods = await Pod.list(kube, timeout=timeout)
    refs: set[str] = set()
    for pod in pods:
        if pod.is_active:
            refs.update(pod.image_refs)
    return frozenset(refs)


def _gc_eligible(
    record: ProjectImageRecord,
    *,
    now: datetime,
    grace: timedelta,
    live_refs: frozenset[str],
    active_digest_refs: frozenset[str],
) -> bool:
    if record.phase != "Retired" or record.retired_at is None:
        return False
    if now - record.retired_at < grace:
        return False
    if not set(_record_digest_refs(record)).isdisjoint(active_digest_refs):
        return False
    return _record_image_refs(record).isdisjoint(live_refs)


def _active_record_digest_refs(records: Sequence[ProjectImageRecord]) -> frozenset[str]:
    refs: set[str] = set()
    for record in records:
        if record.phase == "Active":
            refs.update(_record_digest_refs(record))
    return frozenset(refs)


def _record_image_refs(record: ProjectImageRecord) -> frozenset[str]:
    return frozenset(
        (record.image, *record.channels.values(), *_record_digest_refs(record))
    )


def _record_digest_refs(record: ProjectImageRecord) -> tuple[str, ...]:
    seen: set[str] = set()
    refs: list[str] = []
    for ref in (record.digest_ref, *record.platform_images.values()):
        if ref not in seen:
            seen.add(ref)
            refs.append(ref)
    return tuple(refs)


def _gc_sort_key(record: ProjectImageRecord) -> tuple[datetime, str]:
    return (
        record.retired_at or datetime.max.replace(tzinfo=UTC),
        record.name,
    )


def _record_name(*, repo_id: str, worktree: str, tag: str, digest: str) -> str:
    payload = f"{repo_id}\0{worktree}\0{tag}\0{digest}".encode()
    return f"bertrand-image-{hashlib.sha256(payload).hexdigest()[:48]}"


def _label_hash(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()[:16]


def _validate_platform_images(
    *,
    platform_images: Mapping[str, str],
    context: str,
) -> None:
    if not platform_images:
        msg = f"{context} must define at least one platform image"
        raise OSError(msg)
    for platform, image_ref in platform_images.items():
        if not platform.strip():
            msg = f"{context} defines an empty platform"
            raise OSError(msg)
        if not DIGEST_REF_RE.fullmatch(image_ref):
            msg = (
                f"{context} platform {platform!r} has invalid digest ref: {image_ref!r}"
            )
            raise OSError(msg)


def _normalize_channel_names(channels: Sequence[str]) -> tuple[str, ...]:
    normalized: set[str] = set()
    for channel in channels:
        name = channel.strip()
        if not name:
            msg = "project image channel name cannot be empty"
            raise OSError(msg)
        normalized.add(name)
    return tuple(sorted(normalized))


def _validate_channels(*, channels: Mapping[str, str], context: str) -> None:
    for name, ref in channels.items():
        if not name.strip():
            msg = f"{context} defines an empty channel name"
            raise OSError(msg)
        if not ref.strip():
            msg = f"{context} channel {name!r} has an empty image ref"
            raise OSError(msg)


def _validate_channel_digests(
    *,
    channels: Mapping[str, str],
    digest: str,
    actual: Mapping[str, str],
) -> None:
    expected = {name: digest_ref(ref, digest) for name, ref in channels.items()}
    normalized = dict(actual)
    if normalized != expected:
        msg = (
            "project image manifest result channel digest refs do not match "
            f"published channels: {normalized!r} != {expected!r}"
        )
        raise OSError(msg)


def _datetime_payload(value: datetime | None) -> str | None:
    value = _utc_datetime(value)
    if value is None:
        return None
    return value.isoformat().replace("+00:00", "Z")


def _utc_datetime(value: datetime | None) -> datetime | None:
    if value is None:
        return None
    if value.tzinfo is None:
        return value.replace(tzinfo=UTC)
    return value.astimezone(UTC)
