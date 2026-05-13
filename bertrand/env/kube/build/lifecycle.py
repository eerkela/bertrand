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
from bertrand.env.kube.api import CustomResourceSpec, Kube
from bertrand.env.kube.build.refs import (
    DIGEST_REF_RE,
    channel_refs,
    digest_from_ref,
    digest_ref,
)
from bertrand.env.kube.build.repository import IMAGES, ImageRepository
from bertrand.env.kube.crd import CustomResourceClient, CustomResourceDefinition
from bertrand.env.kube.pod import Pod

if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence

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
PROJECT_IMAGE_CONFIG_ID = "BERTRAND_IMAGE_CONFIG_ID"
PROJECT_IMAGE_GC_GRACE_SECONDS = 86_400
PROJECT_IMAGE_GC_LIMIT = 16

type _ProjectImagePhase = Literal["Active", "Retired"]
type _NonEmptyString = Annotated[str, Field(min_length=1)]


@dataclass(frozen=True)
class ProjectImageIdentity:
    """Stable identity for one configured project image publication.

    Parameters
    ----------
    repo_id : str
        Stable repository UUID.
    worktree : str
        Repository-relative worktree identity.
    tag : str
        Configured image key from ``[tool.bertrand.image]``.
    env_id : str
        Deterministic capability environment UUID.
    config_id : str
        Deterministic hash of image configuration inputs.
    image : str
        Mutable internal image reference published by this identity.
    channels : tuple[str, ...]
        Project channel names to publish as moving aliases.
    """

    repo_id: str
    worktree: str
    tag: str
    env_id: str
    config_id: str
    image: str
    channels: tuple[str, ...]


class _ObjectMeta(BaseModel):
    """Validated subset of Kubernetes object metadata."""

    model_config = ConfigDict(extra="ignore")

    name: str = ""
    namespace: str = ""
    resource_version: str = Field(default="", alias="resourceVersion")
    labels: dict[str, str] = Field(default_factory=dict)


class _BertrandImageSpec(BaseModel):
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


class _BertrandImage(BaseModel):
    """Validated `BertrandImage` custom-resource payload."""

    model_config = ConfigDict(extra="forbid")

    api_version: str = Field(alias="apiVersion")
    kind: Literal["BertrandImage"]
    metadata: _ObjectMeta
    spec: _BertrandImageSpec


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
_PROJECT_IMAGE_SPEC = CustomResourceSpec(
    group=PROJECT_IMAGE_GROUP,
    version=PROJECT_IMAGE_VERSION,
    kind=PROJECT_IMAGE_KIND,
    plural=PROJECT_IMAGE_PLURAL,
    labels=_PROJECT_IMAGE_LABELS,
)
_PROJECT_IMAGE_CLIENT = CustomResourceClient(_PROJECT_IMAGE_SPEC)


@dataclass(frozen=True)
class ProjectImageRecord:
    """Read-only lifecycle record for one published project image digest.

    Parameters
    ----------
    name : str
        Kubernetes custom object name.
    namespace : str
        Kubernetes namespace that owns the record.
    repo_id : str
        Stable repository UUID.
    worktree : str
        Repository-relative worktree identity.
    tag : str
        Configured image key from ``[tool.bertrand.image]``.
    env_id : str
        Deterministic capability environment UUID.
    image : str
        Mutable image reference published by the build.
    digest_ref : str
        Immutable digest-pinned image reference.
    digest : str
        OCI manifest digest derived from ``digest_ref``.
    platforms : tuple[str, ...]
        Platforms derived from ``platform_images``.
    platform_images : Mapping[str, str]
        Read-only mapping from platform to owned platform-image digest ref.
    channels : Mapping[str, str]
        Read-only mapping from channel name to internal mutable channel ref.
    config_id : str
        Deterministic hash of the image configuration inputs.
    phase : {"Active", "Retired"}
        Lifecycle phase used by bounded garbage collection.
    published_at : datetime | None
        Time the digest was published.
    retired_at : datetime | None
        Time the digest was retired, if any.
    last_gc_at : datetime | None
        Last time GC attempted to collect this record.
    last_error : str
        Last GC error message, if any.
    """

    name: str
    namespace: str
    repo_id: str
    worktree: str
    tag: str
    env_id: str
    image: str
    digest_ref: str
    digest: str
    platforms: tuple[str, ...]
    platform_images: Mapping[str, str]
    channels: Mapping[str, str]
    config_id: str
    phase: _ProjectImagePhase
    published_at: datetime | None
    retired_at: datetime | None
    last_gc_at: datetime | None
    last_error: str

    @classmethod
    def _from_payload(cls, payload: object) -> ProjectImageRecord:
        try:
            image = _BertrandImage.model_validate(payload)
        except ValidationError as err:
            msg = f"malformed {PROJECT_IMAGE_KIND} custom object: {err}"
            raise OSError(msg) from err
        label_phase = image.metadata.labels.get(PROJECT_IMAGE_PHASE_LABEL, "").strip()
        if label_phase != image.spec.phase.lower():
            msg = (
                f"malformed {PROJECT_IMAGE_KIND} {image.metadata.name!r}: phase "
                f"label {label_phase!r} does not match spec phase "
                f"{image.spec.phase!r}"
            )
            raise OSError(msg)
        if not DIGEST_REF_RE.fullmatch(image.spec.digest_ref):
            msg = (
                f"malformed {PROJECT_IMAGE_KIND} {image.metadata.name!r}: "
                f"unsupported digest ref {image.spec.digest_ref!r}"
            )
            raise OSError(msg)
        try:
            digest = digest_from_ref(image.spec.digest_ref)
        except ValueError as err:
            msg = f"malformed {PROJECT_IMAGE_KIND} {image.metadata.name!r}: {err}"
            raise OSError(msg) from err
        platform_images = MappingProxyType(
            dict(sorted(image.spec.platform_images.items()))
        )
        platforms = tuple(platform_images)
        channel_names = _normalize_channel_names(image.spec.channels)
        channels = MappingProxyType(channel_refs(image.spec.image, channel_names))
        _validate_platform_images(
            platform_images=platform_images,
            context=f"{PROJECT_IMAGE_KIND} {image.metadata.name!r}",
        )
        _validate_channels(
            channels=channels,
            context=f"{PROJECT_IMAGE_KIND} {image.metadata.name!r}",
        )
        return cls(
            name=image.metadata.name,
            namespace=image.metadata.namespace,
            repo_id=image.spec.repo_id,
            worktree=image.spec.worktree,
            tag=image.spec.tag,
            env_id=image.spec.env_id,
            image=image.spec.image,
            digest_ref=image.spec.digest_ref,
            digest=digest,
            platforms=platforms,
            platform_images=platform_images,
            channels=channels,
            config_id=image.spec.config_id,
            phase=image.spec.phase,
            published_at=_utc_datetime(image.spec.published_at),
            retired_at=_utc_datetime(image.spec.retired_at),
            last_gc_at=_utc_datetime(image.spec.last_gc_at),
            last_error=image.spec.last_error,
        )

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
    return [ProjectImageRecord._from_payload(obj.payload) for obj in objects]


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
    return ProjectImageRecord._from_payload(obj.payload)


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
        labels=_identity_labels(repo_id=repo_id, worktree=worktree, tag=tag),
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
            await _PROJECT_IMAGE_CLIENT.delete(
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
        labels=_identity_labels(
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
    labels = _record_labels(
        repo_id=identity.repo_id,
        worktree=identity.worktree,
        tag=identity.tag,
        env_id=identity.env_id,
        config_id=identity.config_id,
        phase="Active",
    )
    spec = {
        "repo_id": identity.repo_id,
        "worktree": identity.worktree,
        "tag": identity.tag,
        "env_id": identity.env_id,
        "image": identity.image,
        "digest_ref": digest_ref,
        "platform_images": dict(platform_images),
        "channels": list(channel_names),
        "config_id": identity.config_id,
        "phase": "Active",
        "published_at": _datetime_payload(published_at),
        "retired_at": None,
        "last_gc_at": None,
        "last_error": "",
    }
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    obj = await _PROJECT_IMAGE_CLIENT.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=name,
        spec=spec,
        labels=labels,
        timeout=deadline - loop.time(),
    )
    return ProjectImageRecord._from_payload(obj.payload)


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
    obj = await _PROJECT_IMAGE_CLIENT.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=record.name,
        spec={
            "repo_id": record.repo_id,
            "worktree": record.worktree,
            "tag": record.tag,
            "env_id": record.env_id,
            "image": record.image,
            "digest_ref": record.digest_ref,
            "platform_images": dict(record.platform_images),
            "channels": list(record.channels),
            "config_id": record.config_id,
            "phase": phase,
            "published_at": _datetime_payload(record.published_at),
            "retired_at": _datetime_payload(retired_at),
            "last_gc_at": _datetime_payload(
                record.last_gc_at if last_gc_at is None else last_gc_at
            ),
            "last_error": last_error,
        },
        labels=_record_labels(
            repo_id=record.repo_id,
            worktree=record.worktree,
            tag=record.tag,
            env_id=record.env_id,
            config_id=record.config_id,
            phase=phase,
        ),
        timeout=deadline - loop.time(),
    )
    return ProjectImageRecord._from_payload(obj.payload)


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


def _identity_labels(
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


def _record_labels(
    *,
    repo_id: str,
    worktree: str,
    tag: str,
    env_id: str,
    config_id: str,
    phase: _ProjectImagePhase,
) -> dict[str, str]:
    return {
        **_PROJECT_IMAGE_LABELS,
        **_identity_labels(repo_id=repo_id, worktree=worktree, tag=tag),
        PROJECT_IMAGE_ENV_LABEL: _label_hash(env_id),
        PROJECT_IMAGE_CONFIG_LABEL: _label_hash(config_id),
        PROJECT_IMAGE_PHASE_LABEL: phase.lower(),
    }


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
