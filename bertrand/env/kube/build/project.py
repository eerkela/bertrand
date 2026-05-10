"""Project configuration adapter for Bertrand's in-cluster BuildKit runtime."""

from __future__ import annotations

import asyncio
import hashlib
import json
import os
import re
import uuid
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import TYPE_CHECKING, Annotated, Literal, Protocol

import jinja2
import packaging.version
from pydantic import BaseModel, ConfigDict, Field, ValidationError

from bertrand.env.config import Bertrand
from bertrand.env.config.core import locate_template
from bertrand.env.git import (
    BERTRAND_ENV,
    BERTRAND_NAMESPACE,
    ENV_ID_ENV,
    IMAGE_TAG_ENV,
    INFINITY,
    REPO_ID_ENV,
    WORKTREE_ENV,
    WORKTREE_MOUNT,
)
from bertrand.env.kube.api import CustomResourceSpec, Kube
from bertrand.env.kube.build.job import BuildKitImageBuild, BuildKitImageResult
from bertrand.env.kube.build.repository import IMAGES, ImageRepository
from bertrand.env.kube.crd import CustomResourceClient, CustomResourceDefinition
from bertrand.env.kube.pod import Pod
from bertrand.env.version import VERSION

PROJECT_IMAGE_CONFIG_ID = "BERTRAND_IMAGE_CONFIG_ID"
PROJECT_IMAGE_ENV_NAMESPACE = uuid.UUID("36eb88bb-c284-4cb2-ab0a-57f5e850868a")
PROJECT_IMAGE_CONTEXT_PREFIX = "bertrand-project-image"
PROJECT_IMAGE_TAG = "current"
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
PROJECT_IMAGE_DIGEST_LABEL = "bertrand.dev/project-image-digest"
PROJECT_IMAGE_PHASE_LABEL = "bertrand.dev/project-image-phase"
PROJECT_IMAGE_REPO_ANNOTATION = "bertrand.dev/repo-id"
PROJECT_IMAGE_WORKTREE_ANNOTATION = "bertrand.dev/worktree"
PROJECT_IMAGE_TAG_ANNOTATION = "bertrand.dev/image-tag"
PROJECT_IMAGE_ENV_ANNOTATION = "bertrand.dev/env-id"
PROJECT_IMAGE_CONFIG_ANNOTATION = "bertrand.dev/image-config-id"
PROJECT_IMAGE_DIGEST_ANNOTATION = "bertrand.dev/image-digest"
PROJECT_IMAGE_GC_GRACE_SECONDS = 86_400
PROJECT_IMAGE_GC_LIMIT = 16
_PROJECT_IMAGE_COMPONENT_RE = re.compile(r"[^a-z0-9._-]+")
_PROJECT_IMAGE_DIGEST_RE = re.compile(r"^sha256:[0-9a-f]{64}$")

type _ProjectImagePhase = Literal["Active", "Retired"]
type _NonEmptyString = Annotated[str, Field(min_length=1)]

if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence

    from bertrand.env.config.core import Config, KubeName, OCIImageRef, TOMLKey


class _CapabilityRequest(Protocol):
    id: KubeName
    required: bool


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
    tag: _NonEmptyString
    env_id: _NonEmptyString
    image: _NonEmptyString
    digest_ref: _NonEmptyString
    digest: _NonEmptyString
    config_id: _NonEmptyString


class _BertrandImageStatus(BaseModel):
    """Observed lifecycle state for one published project image digest."""

    model_config = ConfigDict(extra="forbid")

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
    status: _BertrandImageStatus = Field(default_factory=_BertrandImageStatus)


_PROJECT_IMAGE_SPEC_SCHEMA = {
    "type": "object",
    "required": [
        "repo_id",
        "worktree",
        "tag",
        "env_id",
        "image",
        "digest_ref",
        "digest",
        "config_id",
    ],
    "properties": {
        "repo_id": {"type": "string", "minLength": 1},
        "worktree": {"type": "string", "minLength": 1},
        "tag": {"type": "string", "minLength": 1},
        "env_id": {"type": "string", "minLength": 1},
        "image": {"type": "string", "minLength": 1},
        "digest_ref": {"type": "string", "minLength": 1},
        "digest": {"type": "string", "pattern": _PROJECT_IMAGE_DIGEST_RE.pattern},
        "config_id": {"type": "string", "minLength": 1},
    },
}
_PROJECT_IMAGE_STATUS_SCHEMA = {
    "type": "object",
    "properties": {
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
        Configured image tag from ``[tool.bertrand.image]``.
    env_id : str
        Deterministic capability environment UUID.
    image : str
        Mutable image reference published by the build.
    digest_ref : str
        Immutable digest-pinned image reference.
    digest : str
        OCI manifest digest reported by BuildKit.
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
        if label_phase != image.status.phase.lower():
            msg = (
                f"malformed {PROJECT_IMAGE_KIND} {image.metadata.name!r}: phase "
                f"label {label_phase!r} does not match status phase "
                f"{image.status.phase!r}"
            )
            raise OSError(msg)
        return cls(
            name=image.metadata.name,
            namespace=image.metadata.namespace,
            repo_id=image.spec.repo_id,
            worktree=image.spec.worktree,
            tag=image.spec.tag,
            env_id=image.spec.env_id,
            image=image.spec.image,
            digest_ref=image.spec.digest_ref,
            digest=image.spec.digest,
            config_id=image.spec.config_id,
            phase=image.status.phase,
            published_at=_utc_datetime(image.status.published_at),
            retired_at=_utc_datetime(image.status.retired_at),
            last_gc_at=_utc_datetime(image.status.last_gc_at),
            last_error=image.status.last_error,
        )


@dataclass(frozen=True)
class ProjectImageResult:
    """Result for one cluster-native project image publication.

    Parameters
    ----------
    plan : ProjectImageBuild
        Project image build plan that was executed.
    build : BuildKitImageResult
        BuildKit image publication result.
    record : ProjectImageRecord
        Active lifecycle record written for the published digest.
    """

    plan: ProjectImageBuild
    build: BuildKitImageResult
    record: ProjectImageRecord

    @property
    def image(self) -> str:
        """Return the mutable image reference.

        Returns
        -------
        str
            Mutable image reference published by BuildKit.
        """
        return self.build.image

    @property
    def digest(self) -> str:
        """Return the published image digest.

        Returns
        -------
        str
            OCI manifest digest reported by BuildKit.
        """
        return self.build.digest

    @property
    def digest_ref(self) -> str:
        """Return the immutable digest-pinned image reference.

        Returns
        -------
        str
            Image reference safe for future digest-pinned workload use.
        """
        return self.record.digest_ref


@dataclass(frozen=True)
class ProjectImageBuild:
    """Cluster-native image build plan for one configured project tag.

    Parameters
    ----------
    repo_id : str
        Stable repository UUID used to scope the image and capabilities.
    worktree : str
        Repository-relative worktree path, or ``"."`` for the repository root.
    tag : TOMLKey
        Configured image tag from ``[tool.bertrand.image]``.
    env_id : str
        Deterministic environment UUID used for capability resolution.
    config_id : str
        Deterministic hash of the image configuration inputs.
    image : OCIImageRef
        Mutable registry reference published by this build.
    build : BuildKitImageBuild
        Underlying BuildKit Job contract.
    """

    repo_id: str
    worktree: str
    tag: TOMLKey
    env_id: str
    config_id: str
    image: OCIImageRef
    build: BuildKitImageBuild

    async def publish(
        self,
        kube: Kube,
        *,
        timeout: float = INFINITY,
    ) -> ProjectImageResult:
        """Publish this project image and update its lifecycle record.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float, optional
            Maximum runtime budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        ProjectImageResult
            BuildKit publication result plus the active `BertrandImage` record.

        Raises
        ------
        TimeoutError
            If CRD convergence, image publication, or record updates exceed
            `timeout`.
        """
        if timeout <= 0:
            msg = "project image publish timeout must be non-negative"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        await ensure_project_image_crd(kube, timeout=deadline - loop.time())
        result = await self.build.publish(
            kube,
            timeout=deadline - loop.time(),
            env_id=self.env_id,
        )
        record = await _record_project_image(
            kube,
            plan=self,
            result=result,
            timeout=deadline - loop.time(),
        )
        return ProjectImageResult(plan=self, build=result, record=record)


def project_image_build(
    config: Config,
    tag: TOMLKey,
    *,
    repo_id: str,
) -> ProjectImageBuild:
    """Translate project image config into a BuildKit build contract.

    Parameters
    ----------
    config : Config
        Active project configuration context.
    tag : TOMLKey
        Image tag to build.
    repo_id : str
        Stable repository UUID.

    Returns
    -------
    ProjectImageBuild
        Build plan containing deterministic identity, target image, and BuildKit
        execution contract.

    Raises
    ------
    TypeError
        If `config` is not a configuration context.
    RuntimeError
        If the configuration context is not active.
    OSError
        If Bertrand configuration is missing or a custom Containerfile cannot be
        read.
    ValueError
        If the tag is unknown or the repository ID is invalid.
    """
    from bertrand.env.config.core import Config, _check_uuid

    if not isinstance(config, Config):
        msg = "project image builds require an active Config instance"
        raise TypeError(msg)
    if not config:
        msg = "project image builds require an active config context"
        raise RuntimeError(msg)

    repo_id = _check_uuid(repo_id)
    bertrand = config.get(Bertrand)
    if bertrand is None:
        msg = f"missing 'bertrand' configuration for environment at {config.root}"
        raise OSError(msg)
    image_config = bertrand.image.get(tag)
    if image_config is None:
        msg = f"unknown image tag '{tag}' for environment at {config.root}"
        raise ValueError(msg)

    worktree = _worktree_identity(config.worktree)
    env_id = _env_id(repo_id=repo_id, worktree=worktree)
    image = IMAGES.ref(
        "/".join(
            (
                "projects",
                repo_id,
                _image_component(worktree, fallback="root"),
                _image_component(tag, fallback="default"),
            )
        ),
        PROJECT_IMAGE_TAG,
    )
    dockerfile = _dockerfile(config.root, bertrand, tag, image_config)
    config_id = _config_id(
        repo_id=repo_id,
        worktree=worktree,
        tag=tag,
        image_config=image_config,
        dockerfile=dockerfile,
    )
    return ProjectImageBuild(
        repo_id=repo_id,
        worktree=worktree,
        tag=tag,
        env_id=env_id,
        config_id=config_id,
        image=image,
        build=BuildKitImageBuild(
            image=image,
            dockerfile=dockerfile,
            context_copies=((config.root, Path()),),
            context_prefix=PROJECT_IMAGE_CONTEXT_PREFIX,
            build_args=_build_args(image_config.args),
            build_labels={
                BERTRAND_ENV: "1",
                REPO_ID_ENV: repo_id,
                WORKTREE_ENV: worktree,
                IMAGE_TAG_ENV: tag,
                ENV_ID_ENV: env_id,
                PROJECT_IMAGE_CONFIG_ID: config_id,
            },
            target=image_config.target,
            secrets=_capability_requests(image_config.secrets),
            ssh=_capability_requests(image_config.ssh),
            devices=_capability_requests(image_config.devices),
        ),
    )


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
        status_schema=_PROJECT_IMAGE_STATUS_SCHEMA,
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
        Optional configured image tag filter. If omitted, active records for every
        tag in the repo/worktree identity are retired.

    Returns
    -------
    list[ProjectImageRecord]
        Records transitioned from `Active` to `Retired`.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive or retirement exceeds the budget.
    ValueError
        If `repo_id` or `tag` is invalid.
    """
    from bertrand.env.config.core import _check_uuid

    if timeout <= 0:
        msg = "project image retirement timeout must be non-negative"
        raise TimeoutError(msg)
    repo_id = _check_uuid(repo_id)
    worktree = _worktree_identity(worktree)
    if tag is not None:
        tag = tag.strip()
        if not tag:
            msg = "project image retirement tag cannot be empty"
            raise ValueError(msg)

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
    live_refs = await _active_pod_image_refs(kube, timeout=deadline - loop.time())
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
        ):
            continue
        try:
            await repository.delete_manifest(
                record.digest_ref,
                timeout=deadline - loop.time(),
            )
            await _PROJECT_IMAGE_CLIENT.delete(
                kube,
                namespace=BERTRAND_NAMESPACE,
                name=record.name,
                timeout=deadline - loop.time(),
            )
        except (OSError, ValueError) as err:
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


def _worktree_identity(worktree: Path | str) -> str:
    value = worktree.as_posix() if isinstance(worktree, Path) else str(worktree).strip()
    return value if value and value != "." else "."


async def _record_project_image(
    kube: Kube,
    *,
    plan: ProjectImageBuild,
    result: BuildKitImageResult,
    timeout: float,
) -> ProjectImageRecord:
    if timeout <= 0:
        msg = "project image record update timeout must be non-negative"
        raise TimeoutError(msg)
    digest = _image_digest(result.digest)
    digest_ref = _digest_ref(result.image, digest)
    now = datetime.now(UTC)
    record = await _upsert_project_image_record(
        kube,
        plan=plan,
        digest=digest,
        digest_ref=digest_ref,
        published_at=now,
        timeout=timeout,
    )
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    peers = await list_project_images(
        kube,
        labels=_identity_labels(
            repo_id=plan.repo_id,
            worktree=plan.worktree,
            tag=plan.tag,
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


async def _upsert_project_image_record(
    kube: Kube,
    *,
    plan: ProjectImageBuild,
    digest: str,
    digest_ref: str,
    published_at: datetime,
    timeout: float,
) -> ProjectImageRecord:
    name = _record_name(
        repo_id=plan.repo_id,
        worktree=plan.worktree,
        tag=plan.tag,
        digest=digest,
    )
    labels = _record_labels(
        repo_id=plan.repo_id,
        worktree=plan.worktree,
        tag=plan.tag,
        env_id=plan.env_id,
        config_id=plan.config_id,
        digest=digest,
        phase="Active",
    )
    annotations = {
        PROJECT_IMAGE_REPO_ANNOTATION: plan.repo_id,
        PROJECT_IMAGE_WORKTREE_ANNOTATION: plan.worktree,
        PROJECT_IMAGE_TAG_ANNOTATION: plan.tag,
        PROJECT_IMAGE_ENV_ANNOTATION: plan.env_id,
        PROJECT_IMAGE_CONFIG_ANNOTATION: plan.config_id,
        PROJECT_IMAGE_DIGEST_ANNOTATION: digest,
    }
    spec = {
        "repo_id": plan.repo_id,
        "worktree": plan.worktree,
        "tag": plan.tag,
        "env_id": plan.env_id,
        "image": plan.image,
        "digest_ref": digest_ref,
        "digest": digest,
        "config_id": plan.config_id,
    }
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    await _PROJECT_IMAGE_CLIENT.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=name,
        spec=spec,
        labels=labels,
        annotations=annotations,
        timeout=deadline - loop.time(),
    )
    return await _patch_project_image_status(
        kube,
        name=name,
        phase="Active",
        published_at=published_at,
        retired_at=None,
        last_gc_at=None,
        last_error="",
        timeout=deadline - loop.time(),
    )


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
    await _PROJECT_IMAGE_CLIENT.upsert(
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
            "digest": record.digest,
            "config_id": record.config_id,
        },
        labels=_record_labels(
            repo_id=record.repo_id,
            worktree=record.worktree,
            tag=record.tag,
            env_id=record.env_id,
            config_id=record.config_id,
            digest=record.digest,
            phase=phase,
        ),
        annotations={
            PROJECT_IMAGE_REPO_ANNOTATION: record.repo_id,
            PROJECT_IMAGE_WORKTREE_ANNOTATION: record.worktree,
            PROJECT_IMAGE_TAG_ANNOTATION: record.tag,
            PROJECT_IMAGE_ENV_ANNOTATION: record.env_id,
            PROJECT_IMAGE_CONFIG_ANNOTATION: record.config_id,
            PROJECT_IMAGE_DIGEST_ANNOTATION: record.digest,
        },
        timeout=deadline - loop.time(),
    )
    return await _patch_project_image_status(
        kube,
        name=record.name,
        phase=phase,
        published_at=record.published_at,
        retired_at=retired_at,
        last_gc_at=record.last_gc_at if last_gc_at is None else last_gc_at,
        last_error=last_error,
        timeout=deadline - loop.time(),
    )


async def _patch_project_image_status(
    kube: Kube,
    *,
    name: str,
    phase: _ProjectImagePhase,
    published_at: datetime | None,
    retired_at: datetime | None,
    last_gc_at: datetime | None,
    last_error: str,
    timeout: float,
) -> ProjectImageRecord:
    obj = await _PROJECT_IMAGE_CLIENT.patch_status(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=name,
        status={
            "phase": phase,
            "published_at": _datetime_payload(published_at),
            "retired_at": _datetime_payload(retired_at),
            "last_gc_at": _datetime_payload(last_gc_at),
            "last_error": last_error,
        },
        timeout=timeout,
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
) -> bool:
    if record.phase != "Retired" or record.retired_at is None:
        return False
    if now - record.retired_at < grace:
        return False
    return record.digest_ref not in live_refs and record.image not in live_refs


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
    digest: str,
    phase: _ProjectImagePhase,
) -> dict[str, str]:
    return {
        **_PROJECT_IMAGE_LABELS,
        **_identity_labels(repo_id=repo_id, worktree=worktree, tag=tag),
        PROJECT_IMAGE_ENV_LABEL: _label_hash(env_id),
        PROJECT_IMAGE_CONFIG_LABEL: _label_hash(config_id),
        PROJECT_IMAGE_DIGEST_LABEL: _label_hash(digest),
        PROJECT_IMAGE_PHASE_LABEL: phase.lower(),
    }


def _record_name(*, repo_id: str, worktree: str, tag: str, digest: str) -> str:
    payload = f"{repo_id}\0{worktree}\0{tag}\0{digest}".encode()
    return f"bertrand-image-{hashlib.sha256(payload).hexdigest()[:48]}"


def _label_hash(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()[:16]


def _image_digest(digest: str) -> str:
    digest = digest.strip()
    if not _PROJECT_IMAGE_DIGEST_RE.fullmatch(digest):
        msg = f"BuildKit returned unsupported image digest: {digest!r}"
        raise OSError(msg)
    return digest


def _digest_ref(image: str, digest: str) -> str:
    image = image.strip()
    if "@" in image:
        repository = image.rsplit("@", 1)[0]
    else:
        slash = image.rfind("/")
        colon = image.rfind(":")
        repository = image[:colon] if colon > slash else image
    if not repository:
        msg = f"cannot derive digest reference from image {image!r}"
        raise OSError(msg)
    return f"{repository}@{digest}"


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


def _env_id(*, repo_id: str, worktree: str) -> str:
    return uuid.uuid5(PROJECT_IMAGE_ENV_NAMESPACE, f"{repo_id}:{worktree}").hex


def _image_component(value: str, *, fallback: str) -> str:
    text = value.strip().lower()
    digest = hashlib.sha256(text.encode("utf-8")).hexdigest()[:12]
    text = _PROJECT_IMAGE_COMPONENT_RE.sub("-", text).strip("._-")
    if not text:
        text = fallback
    return f"{text[:48].rstrip('._-')}-{digest}"


def _dockerfile(
    root: Path,
    model: Bertrand.Model,
    tag: TOMLKey,
    image_config: Bertrand.Model.Image,
) -> str:
    if image_config.containerfile is not None:
        path = root / image_config.containerfile
        try:
            return path.read_text(encoding="utf-8")
        except OSError as err:
            msg = f"failed to read Containerfile for tag '{tag}': {path}"
            raise OSError(msg) from err
        except UnicodeDecodeError as err:
            msg = f"Containerfile for tag '{tag}' is not UTF-8 encoded: {path}"
            raise OSError(msg) from err
    return _render_containerfile(model, tag)


def _render_containerfile(model: Bertrand.Model, tag: TOMLKey) -> str:
    image_config = model.image.get(tag)
    if image_config is None:
        msg = f"unknown image tag '{tag}'"
        raise ValueError(msg)
    if image_config.containerfile is not None:
        msg = (
            f"cannot render generated Containerfile for tag '{tag}' when a custom "
            "`containerfile` is configured"
        )
        raise ValueError(msg)

    jinja = jinja2.Environment(
        autoescape=False,
        undefined=jinja2.StrictUndefined,
        keep_trailing_newline=True,
        trim_blocks=False,
        lstrip_blocks=False,
    )
    template = jinja.from_string(
        locate_template("core", "containerfile.v1").read_text(encoding="utf-8")
    )
    bertrand_version = packaging.version.parse(VERSION.bertrand)
    python_version = packaging.version.parse(VERSION.python)
    try:
        page_size_kib = os.sysconf("SC_PAGE_SIZE") // 1024
    except (AttributeError, ValueError, OSError):
        page_size_kib = 4
    return template.render(
        python_major=python_version.major,
        python_minor=python_version.minor,
        python_patch=python_version.micro,
        bertrand_major=bertrand_version.major,
        bertrand_minor=bertrand_version.minor,
        bertrand_patch=bertrand_version.micro,
        cpus=0,
        page_size_kib=page_size_kib,
        env_mount=str(WORKTREE_MOUNT),
        build_mounts=[],
        dependency_copies=_dependency_copy_specs(image_config.from_),
    )


def _dependency_copy_specs(from_images: Sequence[str]) -> list[dict[str, str]]:
    out: list[dict[str, str]] = []
    for index, image_ref in enumerate(from_images, start=1):
        token = re.sub(r"[^a-z0-9]+", "-", image_ref.lower()).strip("-")
        if not token:
            token = "dependency"
        token = token[:64].rstrip("-")
        target = f"/opt/bertrand/deps/{index:02d}-{token}"
        out.append({"image": image_ref, "target": target})
    return out


def _build_args(args: Mapping[str, object]) -> dict[str, str]:
    return {str(key): str(value) for key, value in args.items()}


def _capability_requests(
    requests: Sequence[_CapabilityRequest],
) -> dict[KubeName, bool]:
    return {request.id: request.required for request in requests}


def _config_id(
    *,
    repo_id: str,
    worktree: str,
    tag: str,
    image_config: Bertrand.Model.Image,
    dockerfile: str,
) -> str:
    payload = {
        "repo_id": repo_id,
        "worktree": worktree,
        "tag": tag,
        "image": image_config.model_dump(by_alias=True, mode="json"),
        "dockerfile_sha256": hashlib.sha256(dockerfile.encode("utf-8")).hexdigest(),
    }
    text = json.dumps(payload, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(text.encode("utf-8")).hexdigest()
