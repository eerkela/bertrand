"""Shared project-image BuildKit request model."""

from __future__ import annotations

import asyncio
import hashlib
import json
import uuid
from collections.abc import Collection, Mapping
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta
from pathlib import Path
from types import MappingProxyType
from typing import TYPE_CHECKING, Annotated, Literal

from pydantic import (
    BaseModel,
    ConfigDict,
    Field,
    ValidationInfo,
    field_validator,
    model_validator,
)

from bertrand.env.config.core import _check_kube_name, _check_uuid
from bertrand.env.git import (
    BERTRAND_LABEL,
    BERTRAND_LABEL_IMAGE,
    BERTRAND_LABEL_MANAGED,
    BERTRAND_NAMESPACE,
    REPO_ID_LABEL,
    WORKTREE_ID_LABEL,
    Deadline,
    until,
)
from bertrand.env.kube.build.refs import DIGEST_REF_RE, digest_from_ref, digest_ref
from bertrand.env.kube.build.repository import delete_image_manifest
from bertrand.env.kube.custom_object import (
    CustomObjectMetadata,
    CustomObjectResource,
)
from bertrand.env.kube.pod import POD_RESOURCE, Pod

if TYPE_CHECKING:
    from collections.abc import Awaitable, Callable

    from bertrand.env.kube.api.client import Kube

BUILDKIT_BUILD_LABEL = "bertrand.dev/buildkit-build"
BUILDKIT_BUILD_LABEL_VALUE = "v1"
BUILDKIT_BUILD_GROUP = "build.bertrand.dev"
BUILDKIT_BUILD_VERSION = "v1alpha1"
BUILDKIT_BUILD_KIND = "BuildKitBuild"
BUILDKIT_BUILD_PLURAL = "buildkitbuilds"
BUILDKIT_BUILD_REPO_LABEL = "bertrand.dev/buildkit-build-repo"
BUILDKIT_BUILD_WORKTREE_LABEL = "bertrand.dev/buildkit-build-worktree"
BUILDKIT_BUILD_CONFIG_LABEL = "bertrand.dev/buildkit-build-config"
BUILDKIT_BUILD_IMAGE_PHASE_LABEL = "bertrand.dev/buildkit-image-phase"
BUILDKIT_BUILD_WAIT_POLL_SECONDS = 2.0
BUILDKIT_IMAGE_GC_GRACE_SECONDS = 86_400
BUILDKIT_IMAGE_GC_LIMIT = 16
BUILDKIT_BUILD_LABELS = {
    BERTRAND_LABEL: BERTRAND_LABEL_MANAGED,
    BUILDKIT_BUILD_LABEL: BUILDKIT_BUILD_LABEL_VALUE,
}
PROJECT_IMAGE_CONFIG_ID = "BERTRAND_IMAGE_CONFIG_ID"

type BuildPullPolicy = Literal["missing", "always", "never"]
type BuildKitBuildPhase = Literal["Pending", "Running", "Succeeded", "Failed"]
type BuildKitImagePhase = Literal["Pending", "Active", "Retired", "Collected"]
type _NonEmptyString = Annotated[str, Field(min_length=1)]


class BuildKitBuildSpec(BaseModel):
    """Validated project-only `BuildKitBuild.spec` payload.

    Parameters
    ----------
    repo_id : str
        Stable repository UUID containing the project source PVC.
    worktree : str, optional
        Repository-volume subpath for the project worktree.
    worktree_id : str
        Persistent UUID shared by image identity and worktree capabilities.
    config_id : str
        Deterministic hash of project image configuration inputs.
    image : str
        Internal mutable image reference to publish.
    dockerfile : str
        Rendered Containerfile text.
    build_args : dict[str, str], optional
        Dockerfile build arguments.
    target : str | None, optional
        Optional target stage in a multi-stage Containerfile.
    pull : {'missing', 'always', 'never'}
        BuildKit base-image resolution policy.
    secrets : dict[str, bool], optional
        Secret capability requests keyed by capability ID.
    ssh : dict[str, bool], optional
        SSH capability requests keyed by capability ID.
    devices : dict[str, bool], optional
        DRA device capability requests keyed by capability ID.
    external_image : str | None, optional
        Optional external image reference to copy the assembled manifest to.
    auth_id : str | None, optional
        Secret capability ID containing Docker auth JSON for external publishing.
    """

    model_config = ConfigDict(extra="forbid", frozen=True)
    repo_id: _NonEmptyString
    worktree: _NonEmptyString = "."
    worktree_id: _NonEmptyString
    config_id: _NonEmptyString
    image: _NonEmptyString
    dockerfile: _NonEmptyString
    build_args: dict[str, str] = Field(default_factory=dict)
    target: str | None = None
    pull: BuildPullPolicy
    secrets: dict[_NonEmptyString, bool] = Field(default_factory=dict)
    ssh: dict[_NonEmptyString, bool] = Field(default_factory=dict)
    devices: dict[_NonEmptyString, bool] = Field(default_factory=dict)
    external_image: str | None = None
    auth_id: str | None = None

    @field_validator("repo_id", "worktree_id")
    @classmethod
    def _validate_uuid(cls, value: str) -> str:
        return _check_uuid(value.strip())

    @field_validator("worktree")
    @classmethod
    def _validate_worktree(cls, value: str) -> str:
        return _normalize_worktree(value)

    @field_validator("image", "config_id")
    @classmethod
    def _validate_trimmed_nonempty(cls, value: str, info: ValidationInfo) -> str:
        text = value.strip()
        if not text:
            msg = f"BuildKitBuild spec {info.field_name} cannot be empty"
            raise ValueError(msg)
        return text

    @field_validator("dockerfile")
    @classmethod
    def _validate_dockerfile(cls, value: str) -> str:
        if not value.strip():
            msg = "BuildKitBuild spec dockerfile cannot be empty"
            raise ValueError(msg)
        return value

    @field_validator("target", "external_image", "auth_id")
    @classmethod
    def _normalize_optional_text(
        cls,
        value: str | None,
        info: ValidationInfo,
    ) -> str | None:
        if value is None:
            return None
        text = value.strip()
        if not text:
            return None
        if info.field_name == "auth_id":
            return _check_kube_name(text)
        return text

    @field_validator("build_args")
    @classmethod
    def _normalize_build_args(cls, value: dict[str, str]) -> dict[str, str]:
        normalized: dict[str, str] = {}
        for key, item in value.items():
            name = key.strip()
            if not name:
                msg = "BuildKit build argument names cannot be empty"
                raise ValueError(msg)
            normalized[name] = item
        return dict(sorted(normalized.items()))

    @field_validator("secrets", "ssh", "devices", mode="before")
    @classmethod
    def _normalize_capability_requests(
        cls,
        value: object,
        info: ValidationInfo,
    ) -> dict[str, bool]:
        if value is None:
            return {}
        if not isinstance(value, Mapping):
            msg = f"BuildKit {info.field_name} requests must be a mapping"
            raise TypeError(msg)
        normalized: dict[str, bool] = {}
        for capability_id, required in value.items():
            checked = _check_kube_name(str(capability_id).strip())
            if checked in normalized:
                msg = f"duplicate BuildKit {info.field_name} capability ID: {checked!r}"
                raise ValueError(msg)
            if not isinstance(required, bool):
                msg = (
                    f"BuildKit {info.field_name} capability {checked!r} required "
                    "flag must be bool"
                )
                raise TypeError(msg)
            normalized[checked] = required
        return dict(sorted(normalized.items()))

    @property
    def request_labels(self) -> dict[str, str]:
        """Return Kubernetes labels applied to this build request.

        Returns
        -------
        dict[str, str]
            Labels used to identify and filter this build request.
        """
        labels = dict(BUILDKIT_BUILD_LABELS)
        labels.update(
            {
                BUILDKIT_BUILD_REPO_LABEL: buildkit_build_label_hash(self.repo_id),
                BUILDKIT_BUILD_WORKTREE_LABEL: buildkit_build_label_hash(
                    self.worktree_id
                ),
                BUILDKIT_BUILD_CONFIG_LABEL: buildkit_build_label_hash(self.config_id),
            }
        )
        return labels

    @property
    def image_labels(self) -> dict[str, str]:
        """Return image labels applied to BuildKit platform outputs.

        Returns
        -------
        dict[str, str]
            Image labels used by downstream runtime discovery.
        """
        return {
            BERTRAND_LABEL: BERTRAND_LABEL_IMAGE,
            REPO_ID_LABEL: self.repo_id,
            WORKTREE_ID_LABEL: self.worktree_id,
            PROJECT_IMAGE_CONFIG_ID: self.config_id,
        }


class BuildKitBuildStatus(BaseModel):
    """Read-only status for one durable BuildKit build request.

    Parameters
    ----------
    phase : {"Pending", "Running", "Succeeded", "Failed"}
        Coarse lifecycle phase.
    observed_generation : int | None
        Metadata generation last reconciled by the controller.
    started_at : datetime | None
        Time the controller started the current generation.
    completed_at : datetime | None
        Time the current generation reached a terminal phase.
    active_job : str
        Current BuildKit client Job name, if any.
    active_platform : str
        Native OCI platform currently being built, if any.
    internal_digest_ref : str
        Internal digest-pinned project image manifest ref, if published.
    external_digest_ref : str
        External digest ref emitted by manifest copy, if any.
    platform_images : dict[str, str]
        Platform-specific digest refs included in the project manifest.
    image_phase : {"Pending", "Active", "Retired", "Collected"}
        Project image lifecycle state for a successful build.
    published_at : datetime | None
        Time the project manifest was published.
    retired_at : datetime | None
        Time the project manifest was retired.
    last_gc_at : datetime | None
        Time the retired image was last considered by GC.
    image_last_error : str
        Last non-fatal image lifecycle error.
    message : str
        Concise human-readable status message.
    log_excerpt : str
        Failure or diagnostic log excerpt.
    """

    model_config = ConfigDict(extra="forbid", frozen=True, populate_by_name=True)
    phase: BuildKitBuildPhase = "Pending"
    observed_generation: int | None = Field(default=None, alias="observedGeneration")
    started_at: datetime | None = None
    completed_at: datetime | None = None
    active_job: str = ""
    active_platform: str = ""
    internal_digest_ref: str = ""
    external_digest_ref: str = ""
    platform_images: dict[str, str] = Field(default_factory=dict)
    image_phase: BuildKitImagePhase = "Pending"
    published_at: datetime | None = None
    retired_at: datetime | None = None
    last_gc_at: datetime | None = None
    image_last_error: str = ""
    message: str = ""
    log_excerpt: str = ""

    @field_validator("platform_images")
    @classmethod
    def _validate_platform_images(cls, value: dict[str, str]) -> dict[str, str]:
        return _validate_platform_images(
            platform_images=value,
            context="BuildKitBuild status",
            allow_empty=True,
        )

    def running(
        self,
        *,
        generation: int,
        message: str,
        active_job: str = "",
        active_platform: str = "",
        reset: bool = False,
    ) -> BuildKitBuildStatus:
        """Return status for an active controller reconciliation.

        Returns
        -------
        BuildKitBuildStatus
            Updated running status.
        """
        updates: dict[str, object] = {
            "phase": "Running",
            "observed_generation": generation,
            "active_job": active_job,
            "active_platform": active_platform,
            "message": message,
        }
        if reset:
            updates.update(
                {
                    "started_at": datetime.now(UTC),
                    "completed_at": None,
                    "internal_digest_ref": "",
                    "external_digest_ref": "",
                    "platform_images": {},
                    "image_phase": "Pending",
                    "published_at": None,
                    "retired_at": None,
                    "last_gc_at": None,
                    "image_last_error": "",
                    "log_excerpt": "",
                }
            )
        return type(self).model_validate({**self.model_dump(mode="python"), **updates})

    def succeeded(
        self,
        *,
        generation: int,
        completed_at: datetime,
        internal_digest_ref: str,
        external_digest_ref: str | None,
        platform_images: Mapping[str, str],
    ) -> BuildKitBuildStatus:
        """Return status for a successful image publication.

        Returns
        -------
        BuildKitBuildStatus
            Updated successful status.
        """
        return type(self).model_validate(
            {
                **self.model_dump(mode="python"),
                "phase": "Succeeded",
                "observed_generation": generation,
                "completed_at": completed_at,
                "active_job": "",
                "active_platform": "",
                "internal_digest_ref": internal_digest_ref,
                "external_digest_ref": external_digest_ref or "",
                "platform_images": dict(platform_images),
                "image_phase": "Active",
                "published_at": completed_at,
                "retired_at": None,
                "last_gc_at": None,
                "image_last_error": "",
                "message": "BuildKit build succeeded",
                "log_excerpt": "",
            }
        )

    def failed(
        self,
        *,
        generation: int,
        completed_at: datetime,
        message: str,
        log_excerpt: str,
    ) -> BuildKitBuildStatus:
        """Return status for a failed reconciliation.

        Returns
        -------
        BuildKitBuildStatus
            Updated failed status.
        """
        return type(self).model_validate(
            {
                **self.model_dump(mode="python"),
                "phase": "Failed",
                "observed_generation": generation,
                "completed_at": completed_at,
                "active_job": "",
                "active_platform": "",
                "internal_digest_ref": "",
                "external_digest_ref": "",
                "platform_images": {},
                "image_phase": "Pending",
                "published_at": None,
                "retired_at": None,
                "last_gc_at": None,
                "image_last_error": "",
                "message": message,
                "log_excerpt": log_excerpt,
            }
        )

    def with_image_phase(
        self,
        *,
        generation: int,
        image_phase: BuildKitImagePhase,
        retired_at: datetime | None = None,
        last_gc_at: datetime | None = None,
        image_last_error: str = "",
    ) -> BuildKitBuildStatus:
        """Return status with an updated project image lifecycle phase.

        Returns
        -------
        BuildKitBuildStatus
            Updated image lifecycle status.
        """
        return type(self).model_validate(
            {
                **self.model_dump(mode="python"),
                "observed_generation": generation,
                "image_phase": image_phase,
                "retired_at": retired_at,
                "last_gc_at": last_gc_at,
                "image_last_error": image_last_error,
            }
        )


class BuildKitBuildRecord(BaseModel):
    """Read-only model for one `BuildKitBuild` custom object.

    Parameters
    ----------
    api_version : str
        Kubernetes API version reported by the custom object.
    kind : {"BuildKitBuild"}
        Kubernetes custom object kind.
    metadata : CustomObjectMetadata
        Kubernetes object metadata.
    spec : BuildKitBuildSpec
        Validated project build request spec.
    status : BuildKitBuildStatus
        Validated build status payload.
    """

    model_config = ConfigDict(extra="forbid", frozen=True, populate_by_name=True)
    api_version: str = Field(alias="apiVersion")
    kind: Literal["BuildKitBuild"]
    metadata: CustomObjectMetadata
    spec: BuildKitBuildSpec
    status: BuildKitBuildStatus = Field(default_factory=BuildKitBuildStatus)

    @model_validator(mode="after")
    def _validate_image_lifecycle(self) -> BuildKitBuildRecord:
        if self.image_phase == "Pending":
            return self
        if self.status.phase != "Succeeded" or not self.is_reconciled:
            msg = (
                f"{BUILDKIT_BUILD_KIND} {self.name!r}: image lifecycle phase "
                "requires a reconciled successful build"
            )
            raise ValueError(msg)
        if not DIGEST_REF_RE.fullmatch(self.digest_ref):
            msg = (
                f"{BUILDKIT_BUILD_KIND} {self.name!r}: unsupported digest ref "
                f"{self.digest_ref!r}"
            )
            raise ValueError(msg)
        try:
            digest = digest_from_ref(self.digest_ref)
        except ValueError as err:
            msg = f"{BUILDKIT_BUILD_KIND} {self.name!r}: {err}"
            raise ValueError(msg) from err
        expected_digest_ref = digest_ref(self.image, digest)
        if self.digest_ref != expected_digest_ref:
            msg = (
                f"{BUILDKIT_BUILD_KIND} {self.name!r}: digest ref "
                f"{self.digest_ref!r} does not match image {self.image!r}"
            )
            raise ValueError(msg)
        _validate_platform_images(
            platform_images=self.platform_images,
            context=f"{BUILDKIT_BUILD_KIND} {self.name!r}",
        )
        return self

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
        """Return the namespace that owns this build request.

        Returns
        -------
        str
            Kubernetes namespace that owns this build request.
        """
        return self.metadata.namespace

    @property
    def generation(self) -> int:
        """Return the Kubernetes metadata generation.

        Returns
        -------
        int
            Kubernetes metadata generation for this build request.
        """
        return self.metadata.generation

    @property
    def resource_version(self) -> str:
        """Return the Kubernetes resource version.

        Returns
        -------
        str
            Kubernetes resource version used by wait loops.
        """
        return self.metadata.resource_version

    @property
    def is_reconciled(self) -> bool:
        """Return whether status reflects this record generation.

        Returns
        -------
        bool
            Whether the controller has observed this custom object's current
            metadata generation.
        """
        return self.status.observed_generation == self.metadata.generation

    @property
    def is_terminal(self) -> bool:
        """Return whether this build request is terminal.

        Returns
        -------
        bool
            True when status belongs to the current generation and phase is
            `Succeeded` or `Failed`.
        """
        return self.is_reconciled and self.status.phase in ("Succeeded", "Failed")

    @property
    def is_active(self) -> bool:
        """Return whether this build request can still mutate build resources.

        Returns
        -------
        bool
            True when this request is stale, pending, or running.
        """
        return not self.is_terminal

    @property
    def image(self) -> str:
        """Return the mutable project image reference."""
        return self.spec.image

    @property
    def digest_ref(self) -> str:
        """Return the immutable digest-pinned project image reference."""
        return self.status.internal_digest_ref

    @property
    def digest(self) -> str:
        """Return the project image manifest digest."""
        return digest_from_ref(self.digest_ref)

    @property
    def platforms(self) -> tuple[str, ...]:
        """Return platforms included in this publication."""
        return tuple(self.platform_images)

    @property
    def platform_images(self) -> Mapping[str, str]:
        """Return platform-specific digest refs for this publication."""
        return MappingProxyType(dict(sorted(self.status.platform_images.items())))

    @property
    def config_id(self) -> str:
        """Return the project image configuration identity."""
        return self.spec.config_id

    @property
    def image_phase(self) -> BuildKitImagePhase:
        """Return this build's project image lifecycle phase."""
        return self.status.image_phase

    @property
    def published_at(self) -> datetime | None:
        """Return the publication timestamp normalized to UTC."""
        return _utc_datetime(self.status.published_at)

    @property
    def retired_at(self) -> datetime | None:
        """Return the retirement timestamp normalized to UTC."""
        return _utc_datetime(self.status.retired_at)

    @property
    def last_gc_at(self) -> datetime | None:
        """Return the last image GC attempt timestamp normalized to UTC."""
        return _utc_datetime(self.status.last_gc_at)

    @property
    def image_last_error(self) -> str:
        """Return the last non-fatal image lifecycle error."""
        return self.status.image_last_error


def buildkit_build_label_hash(value: str) -> str:
    """Return the Kubernetes-label-safe hash used by BuildKit selectors.

    Returns
    -------
    str
        Short SHA-256 hex digest used in BuildKit labels.
    """
    return hashlib.sha256(value.encode("utf-8")).hexdigest()[:16]


BUILDKIT_BUILD_RESOURCE = CustomObjectResource[BuildKitBuildRecord](
    group=BUILDKIT_BUILD_GROUP,
    version=BUILDKIT_BUILD_VERSION,
    kind=BUILDKIT_BUILD_KIND,
    plural=BUILDKIT_BUILD_PLURAL,
    labels=BUILDKIT_BUILD_LABELS,
    singular="buildkitbuild",
    short_names=("bkbuild",),
    payload_parser=BuildKitBuildRecord.model_validate,
    payload_error_context=f"{BUILDKIT_BUILD_KIND} custom object",
    spec_model=BuildKitBuildSpec,
    spec_schema_overrides={
        "required": [
            "repo_id",
            "worktree",
            "worktree_id",
            "config_id",
            "image",
            "dockerfile",
            "pull",
        ],
    },
    status_model=BuildKitBuildStatus,
    default_namespace=BERTRAND_NAMESPACE,
)


@dataclass(frozen=True)
class _ProjectImageGcInventory:
    records: tuple[BuildKitBuildRecord, ...]
    pods: tuple[Pod, ...]

    @classmethod
    async def collect(
        cls,
        kube: Kube,
        *,
        deadline: Deadline,
    ) -> _ProjectImageGcInventory:
        await BUILDKIT_BUILD_RESOURCE.ensure_crd(kube, deadline=deadline)
        records_task = asyncio.create_task(
            BUILDKIT_BUILD_RESOURCE.list(kube, deadline=deadline)
        )
        pods_task = asyncio.create_task(POD_RESOURCE.list(kube, deadline=deadline))
        await asyncio.gather(records_task, pods_task)
        return cls(
            records=tuple(records_task.result()),
            pods=tuple(pods_task.result()),
        )

    def retired_records(self) -> tuple[BuildKitBuildRecord, ...]:
        return tuple(
            sorted(
                (record for record in self.records if record.image_phase == "Retired"),
                key=lambda item: (
                    item.retired_at or datetime.max.replace(tzinfo=UTC),
                    item.name,
                ),
            )
        )

    def active_digest_refs(self) -> set[str]:
        refs: set[str] = set()
        for record in self.records:
            if record.image_phase != "Active":
                continue
            for ref in (record.digest_ref, *record.platform_images.values()):
                if ref:
                    refs.add(ref)
        return refs

    def live_image_refs(self) -> set[str]:
        refs: set[str] = set()
        for pod in self.pods:
            if pod.is_active:
                refs.update(pod.image_refs)
        return refs


async def submit_buildkit_build(
    kube: Kube,
    *,
    spec: BuildKitBuildSpec,
    deadline: Deadline,
) -> BuildKitBuildRecord:
    """Create one durable project-image BuildKit request.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    spec : BuildKitBuildSpec
        Validated project image build request.
    deadline : Deadline
        Maximum creation budget in seconds.

    Returns
    -------
    BuildKitBuildRecord
        Submitted build request.

    """
    return await BUILDKIT_BUILD_RESOURCE.create(
        kube,
        name=_buildkit_build_name(spec),
        spec=spec,
        labels=spec.request_labels,
        deadline=deadline,
    )


async def require_active_project_image(
    kube: Kube,
    *,
    spec: BuildKitBuildSpec,
    deadline: Deadline,
) -> BuildKitBuildRecord:
    """Return the active image-bearing build for one exact project image identity.

    Returns
    -------
    BuildKitBuildRecord
        Active successful build record matching the requested spec identity.

    Raises
    ------
    OSError
        If no active image exists or lifecycle invariants are violated.
    """
    await BUILDKIT_BUILD_RESOURCE.ensure_crd(kube, deadline=deadline)
    records = await BUILDKIT_BUILD_RESOURCE.list(
        kube,
        labels={
            BUILDKIT_BUILD_REPO_LABEL: buildkit_build_label_hash(spec.repo_id),
            BUILDKIT_BUILD_WORKTREE_LABEL: buildkit_build_label_hash(spec.worktree_id),
            BUILDKIT_BUILD_IMAGE_PHASE_LABEL: "active",
        },
        deadline=deadline,
    )
    records = [
        record
        for record in records
        if record.spec.repo_id == spec.repo_id
        and record.spec.worktree_id == spec.worktree_id
    ]
    active = [record for record in records if record.image_phase == "Active"]
    matching = [record for record in active if record.config_id == spec.config_id]
    if len(matching) > 1:
        names = ", ".join(sorted(record.name for record in matching))
        msg = (
            "project image lifecycle invariant violated: multiple active "
            f"{BUILDKIT_BUILD_KIND} records match current config "
            f"{spec.config_id!r}: {names}"
        )
        raise OSError(msg)
    if matching:
        record = matching[0]
        if record.spec.worktree_id != spec.worktree_id or record.image != spec.image:
            msg = (
                "project image lifecycle invariant violated: active "
                f"{BUILDKIT_BUILD_KIND} {record.name!r} matches config "
                f"{spec.config_id!r} but does not match the current image identity"
            )
            raise OSError(msg)
        expected_digest_ref = digest_ref(spec.image, record.digest)
        if record.digest_ref != expected_digest_ref:
            msg = (
                "project image lifecycle invariant violated: active "
                f"{BUILDKIT_BUILD_KIND} {record.name!r} points at "
                f"{record.digest_ref!r}, expected {expected_digest_ref!r}"
            )
            raise OSError(msg)
        return record

    workload_label = f"{spec.repo_id}:{spec.worktree_id}"
    if active:
        detail = ", ".join(
            sorted(f"{record.name}(config={record.config_id})" for record in active)
        )
        msg = (
            f"active project image for {workload_label} is stale for current image "
            f"config {spec.config_id!r}; run `bertrand build` before "
            f"materializing the workload. Active records: {detail}"
        )
        raise OSError(msg)

    msg = (
        f"no active project image has been published for {workload_label}; run "
        "`bertrand build` before materializing the workload"
    )
    raise OSError(msg)


async def retire_project_images(
    kube: Kube,
    *,
    repo_id: str,
    worktree_id: str,
    deadline: Deadline,
    exclude_names: Collection[str] = (),
) -> list[BuildKitBuildRecord]:
    """Retire active project images without deleting registry manifests.

    Returns
    -------
    list[BuildKitBuildRecord]
        Build records transitioned from active to retired.

    """
    repo_id = _check_uuid(repo_id)
    worktree_id = _check_uuid(worktree_id)
    await BUILDKIT_BUILD_RESOURCE.ensure_crd(kube, deadline=deadline)
    records = await BUILDKIT_BUILD_RESOURCE.list(
        kube,
        labels={
            BUILDKIT_BUILD_REPO_LABEL: buildkit_build_label_hash(repo_id),
            BUILDKIT_BUILD_WORKTREE_LABEL: buildkit_build_label_hash(worktree_id),
            BUILDKIT_BUILD_IMAGE_PHASE_LABEL: "active",
        },
        deadline=deadline,
    )
    now = datetime.now(UTC)
    excluded = set(exclude_names)
    retired: list[BuildKitBuildRecord] = []
    for record in sorted(records, key=lambda item: item.name):
        if (
            record.spec.repo_id != repo_id
            or record.spec.worktree_id != worktree_id
            or record.image_phase != "Active"
            or record.name in excluded
        ):
            continue
        retired.append(
            await patch_buildkit_build_status(
                kube,
                record=record,
                status=record.status.with_image_phase(
                    generation=record.generation,
                    image_phase="Retired",
                    retired_at=record.retired_at or now,
                    image_last_error="",
                ),
                deadline=deadline,
            )
        )
    return retired


async def gc_project_images(
    kube: Kube,
    *,
    deadline: Deadline,
    grace_seconds: int = BUILDKIT_IMAGE_GC_GRACE_SECONDS,
    limit: int = BUILDKIT_IMAGE_GC_LIMIT,
) -> list[BuildKitBuildRecord]:
    """Delete eligible retired project image manifests and mark builds collected.

    Returns
    -------
    list[BuildKitBuildRecord]
        Retired build records transitioned to collected.

    Raises
    ------
    ValueError
        If `grace_seconds` or `limit` is negative.
    """
    if grace_seconds < 0:
        msg = "project image GC grace_seconds must be non-negative"
        raise ValueError(msg)
    if limit < 0:
        msg = "project image GC limit must be non-negative"
        raise ValueError(msg)
    if limit == 0:
        return []

    inventory = await _ProjectImageGcInventory.collect(
        kube,
        deadline=deadline,
    )
    active_digest_refs = inventory.active_digest_refs()
    live_refs = inventory.live_image_refs()
    now = datetime.now(UTC)
    grace = timedelta(seconds=grace_seconds)
    collected: list[BuildKitBuildRecord] = []
    for record in inventory.retired_records():
        if len(collected) >= limit:
            break

        retired_at = record.retired_at
        if retired_at is None or now - retired_at < grace:
            continue

        digest_refs: list[str] = []
        seen_refs: set[str] = set()
        for ref in (record.digest_ref, *record.platform_images.values()):
            if ref and ref not in seen_refs:
                digest_refs.append(ref)
                seen_refs.add(ref)
        if seen_refs & active_digest_refs:
            continue
        if {record.image, *digest_refs} & live_refs:
            continue

        try:
            for image_ref in digest_refs:
                await delete_image_manifest(
                    image_ref,
                    deadline=deadline,
                )
            collected.append(
                await patch_buildkit_build_status(
                    kube,
                    record=record,
                    status=record.status.with_image_phase(
                        generation=record.generation,
                        image_phase="Collected",
                        retired_at=record.retired_at,
                        last_gc_at=now,
                        image_last_error="",
                    ),
                    deadline=deadline,
                )
            )
        except (OSError, TimeoutError, ValueError) as err:
            await patch_buildkit_build_status(
                kube,
                record=record,
                status=record.status.with_image_phase(
                    generation=record.generation,
                    image_phase="Retired",
                    retired_at=record.retired_at,
                    last_gc_at=now,
                    image_last_error=str(err),
                ),
                deadline=deadline,
            )
            continue
    return collected


async def next_project_image_gc_time(
    kube: Kube,
    *,
    deadline: Deadline,
    grace_seconds: int = BUILDKIT_IMAGE_GC_GRACE_SECONDS,
) -> datetime | None:
    """Return the next time retired project images may be GC-eligible.

    Returns
    -------
    datetime | None
        Earliest GC eligibility time, or `None` when no retired images exist.

    Raises
    ------
    ValueError
        If `grace_seconds` is negative.
    """
    if grace_seconds < 0:
        msg = "project image GC scheduling grace_seconds must be non-negative"
        raise ValueError(msg)

    await BUILDKIT_BUILD_RESOURCE.ensure_crd(kube, deadline=deadline)
    records = await BUILDKIT_BUILD_RESOURCE.list(
        kube,
        labels={BUILDKIT_BUILD_IMAGE_PHASE_LABEL: "retired"},
        deadline=deadline,
    )
    if not records:
        return None

    now = datetime.now(UTC)
    grace = timedelta(seconds=grace_seconds)
    next_times = [
        record.retired_at + grace if record.retired_at is not None else now
        for record in records
        if record.image_phase == "Retired"
    ]
    return min(next_times) if next_times else None


async def wait_buildkit_build(
    kube: Kube,
    *,
    name: str,
    deadline: Deadline,
    on_update: Callable[[BuildKitBuildRecord], Awaitable[None]] | None = None,
) -> BuildKitBuildRecord:
    """Wait for one durable BuildKit build request to reach a terminal phase.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    name : str
        Build request name.
    deadline : Deadline
        Maximum wait budget in seconds.
    on_update : Callable[[BuildKitBuildRecord], Awaitable[None]] | None, optional
        Async callback invoked when the observed resource version changes.

    Returns
    -------
    BuildKitBuildRecord
        Terminal build request record.

    Raises
    ------
    TimeoutError
        If `deadline` expires before the request is terminal.
    """
    seen_version = ""

    async def terminal(attempt_deadline: Deadline) -> BuildKitBuildRecord:
        nonlocal seen_version
        record = await BUILDKIT_BUILD_RESOURCE.get(
            kube,
            name=name,
            deadline=attempt_deadline,
        )
        if record is None:
            msg = f"BuildKitBuild {name!r} disappeared while waiting"
            raise OSError(msg)
        if record.resource_version != seen_version:
            if on_update is not None:
                await on_update(record)
            seen_version = record.resource_version
        if record.is_terminal:
            return record
        msg = f"BuildKitBuild {name!r} is not terminal yet"
        raise TimeoutError(msg)

    try:
        return await until(
            terminal,
            deadline=deadline,
            delay=BUILDKIT_BUILD_WAIT_POLL_SECONDS,
        )
    except TimeoutError as err:
        msg = f"BuildKitBuild {name!r} did not finish before timeout"
        raise TimeoutError(msg) from err


async def patch_buildkit_build_status(
    kube: Kube,
    *,
    record: BuildKitBuildRecord,
    status: BuildKitBuildStatus,
    deadline: Deadline,
) -> BuildKitBuildRecord:
    """Patch one BuildKit request status subresource.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    record : BuildKitBuildRecord
        Build request snapshot to patch.
    status : BuildKitBuildStatus
        Complete status payload to apply.
    deadline : Deadline
        Maximum patch budget in seconds.

    Returns
    -------
    BuildKitBuildRecord
        Updated build request record.
    """
    updated = await BUILDKIT_BUILD_RESOURCE.patch_status(
        kube,
        name=record.name,
        status=status.model_dump(mode="json", by_alias=True),
        deadline=deadline,
    )
    image_phase_label = (
        status.image_phase.lower() if status.image_phase != "Pending" else None
    )
    if (
        updated.metadata.labels.get(BUILDKIT_BUILD_IMAGE_PHASE_LABEL)
        == image_phase_label
    ):
        return updated
    return await BUILDKIT_BUILD_RESOURCE.patch(
        kube,
        name=updated.name,
        body={
            "metadata": {
                "labels": {BUILDKIT_BUILD_IMAGE_PHASE_LABEL: image_phase_label}
            }
        },
        deadline=deadline,
        context=f"patch {BUILDKIT_BUILD_KIND} image lifecycle labels",
    )


def worktree_identity(worktree: Path | str) -> str:
    """Normalize a repository worktree path for image identity labels.

    Returns
    -------
    str
        Normalized repository-relative worktree identity.

    Raises
    ------
    ValueError
        If the worktree identity is absolute or escapes the repository root.
    """
    value = worktree.as_posix() if isinstance(worktree, Path) else str(worktree).strip()
    if not value or value == ".":
        return "."
    path = Path(value)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        msg = f"repository worktree identity must be a relative subpath: {worktree!r}"
        raise ValueError(msg)
    return path.as_posix()


def _buildkit_build_name(spec: BuildKitBuildSpec) -> str:
    text = json.dumps(
        spec.model_dump(mode="json"),
        sort_keys=True,
        separators=(",", ":"),
    )
    digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
    return f"bertrand-build-{digest[:16]}-{uuid.uuid4().hex[:8]}"


def _normalize_worktree(worktree: str) -> str:
    value = worktree.strip()
    if not value or value == ".":
        return "."
    path = Path(value)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        msg = f"BuildKit PVC worktree must be a relative subpath: {worktree!r}"
        raise ValueError(msg)
    return path.as_posix()


def _validate_platform_images(
    *,
    platform_images: Mapping[str, str],
    context: str,
    allow_empty: bool = False,
) -> dict[str, str]:
    if not platform_images and not allow_empty:
        msg = f"{context}: project image platform map cannot be empty"
        raise ValueError(msg)
    validated: dict[str, str] = {}
    for raw_platform, raw_ref in platform_images.items():
        platform = raw_platform.strip()
        if not platform:
            msg = f"{context}: project image platform cannot be empty"
            raise ValueError(msg)
        if platform in validated:
            msg = f"{context}: duplicate project image platform {platform!r}"
            raise ValueError(msg)
        ref = raw_ref.strip()
        if not DIGEST_REF_RE.fullmatch(ref):
            msg = f"{context}: unsupported platform image ref {ref!r}"
            raise ValueError(msg)
        try:
            digest_from_ref(ref)
        except ValueError as err:
            msg = f"{context}: {err}"
            raise ValueError(msg) from err
        validated[platform] = ref
    return dict(sorted(validated.items()))


def _utc_datetime(value: datetime | None) -> datetime | None:
    if value is None:
        return None
    if value.tzinfo is None:
        return value.replace(tzinfo=UTC)
    return value.astimezone(UTC)
