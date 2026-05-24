"""Shared project-image BuildKit request model."""

from __future__ import annotations

import asyncio
import hashlib
import json
import uuid
from collections.abc import Mapping
from dataclasses import dataclass
from datetime import datetime  # noqa: TC003
from pathlib import Path
from typing import TYPE_CHECKING, Annotated, Literal

from pydantic import (
    BaseModel,
    ConfigDict,
    Field,
    ValidationError,
    ValidationInfo,
    field_validator,
)

from bertrand.env.config.core import _check_kube_name, _check_uuid
from bertrand.env.git import (
    BERTRAND_ENV,
    BERTRAND_NAMESPACE,
    REPO_ID_ENV,
    WORKTREE_ENV,
    WORKTREE_ID_ENV,
)
from bertrand.env.kube.crd import CustomResourceDefinition
from bertrand.env.kube.custom_object import (
    CustomObjectClient,
    CustomObjectMetadata,
    CustomObjectSpec,
)

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
BUILDKIT_BUILD_WAIT_POLL_SECONDS = 2.0
BUILDKIT_BUILD_LABELS = {
    BERTRAND_ENV: "1",
    BUILDKIT_BUILD_LABEL: BUILDKIT_BUILD_LABEL_VALUE,
}
PROJECT_IMAGE_CONFIG_ID = "BERTRAND_IMAGE_CONFIG_ID"

type BuildPullPolicy = Literal["missing", "always", "never"]
type BuildKitBuildPhase = Literal["Pending", "Running", "Succeeded", "Failed"]
type _NonEmptyString = Annotated[str, Field(min_length=1)]

_BUILDKIT_BUILD_SPEC = CustomObjectSpec(
    group=BUILDKIT_BUILD_GROUP,
    version=BUILDKIT_BUILD_VERSION,
    kind=BUILDKIT_BUILD_KIND,
    plural=BUILDKIT_BUILD_PLURAL,
    labels=BUILDKIT_BUILD_LABELS,
)
_BUILDKIT_BUILD_CLIENT = CustomObjectClient(_BUILDKIT_BUILD_SPEC)
_STRING_MAP_SCHEMA = {"type": "object", "additionalProperties": {"type": "string"}}
_BOOL_MAP_SCHEMA = {"type": "object", "additionalProperties": {"type": "boolean"}}
_BUILDKIT_BUILD_SPEC_SCHEMA = {
    "type": "object",
    "required": [
        "repo_id",
        "worktree",
        "worktree_id",
        "config_id",
        "image",
        "dockerfile",
        "pull",
    ],
    "properties": {
        "repo_id": {"type": "string", "minLength": 1},
        "worktree": {"type": "string", "minLength": 1},
        "worktree_id": {"type": "string", "minLength": 1},
        "config_id": {"type": "string", "minLength": 1},
        "image": {"type": "string", "minLength": 1},
        "dockerfile": {"type": "string", "minLength": 1},
        "build_args": _STRING_MAP_SCHEMA,
        "target": {"type": "string", "nullable": True},
        "pull": {"type": "string", "enum": ["missing", "always", "never"]},
        "secrets": _BOOL_MAP_SCHEMA,
        "ssh": _BOOL_MAP_SCHEMA,
        "devices": _BOOL_MAP_SCHEMA,
        "external_image": {"type": "string", "nullable": True},
        "auth_id": {"type": "string", "nullable": True},
    },
}
_BUILDKIT_BUILD_STATUS_SCHEMA = {
    "type": "object",
    "properties": {
        "phase": {
            "type": "string",
            "enum": ["Pending", "Running", "Succeeded", "Failed"],
        },
        "observedGeneration": {"type": "integer", "nullable": True},
        "started_at": {"type": "string", "format": "date-time", "nullable": True},
        "completed_at": {"type": "string", "format": "date-time", "nullable": True},
        "active_job": {"type": "string"},
        "active_platform": {"type": "string"},
        "external_digest_ref": {"type": "string"},
        "record_name": {"type": "string"},
        "message": {"type": "string"},
        "log_excerpt": {"type": "string"},
    },
}


@dataclass(frozen=True)
class ProjectImageIdentity:
    """Stable project image identity shared by build and lifecycle layers.

    Parameters
    ----------
    repo_id : str
        Stable repository UUID containing the project source.
    worktree : str
        Repository-volume subpath for this worktree.
    worktree_id : str
        Persistent UUID for this concrete checkout instance.
    config_id : str
        Deterministic project image configuration fingerprint.
    image : str
        Internal mutable image reference.
    """

    repo_id: str
    worktree: str
    worktree_id: str
    config_id: str
    image: str


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

    @classmethod
    def from_identity(
        cls,
        *,
        identity: ProjectImageIdentity,
        dockerfile: str,
        build_args: Mapping[str, str],
        target: str | None,
        pull: BuildPullPolicy,
        secrets: Mapping[str, bool],
        ssh: Mapping[str, bool],
        devices: Mapping[str, bool],
        external_image: str | None = None,
        auth_id: str | None = None,
    ) -> BuildKitBuildSpec:
        """Create a request spec from one project image identity.

        Parameters
        ----------
        identity : ProjectImageIdentity
            Stable project image identity to build and publish.
        dockerfile : str
            Rendered Containerfile text.
        build_args : Mapping[str, str]
            Dockerfile build arguments.
        target : str | None
            Optional target stage in a multi-stage Containerfile.
        pull : {'missing', 'always', 'never'}
            BuildKit base-image resolution policy.
        secrets : Mapping[str, bool]
            Secret capability requests exposed to the build.
        ssh : Mapping[str, bool]
            SSH capability requests exposed to the build.
        devices : Mapping[str, bool]
            DRA device capability requests exposed to the build.
        external_image : str | None, optional
            Optional external manifest destination.
        auth_id : str | None, optional
            Secret capability ID containing Docker auth JSON for external publishing.

        Returns
        -------
        BuildKitBuildSpec
            Validated request spec.
        """
        return cls(
            repo_id=identity.repo_id,
            worktree=identity.worktree,
            worktree_id=identity.worktree_id,
            config_id=identity.config_id,
            image=identity.image,
            dockerfile=dockerfile,
            build_args=dict(sorted(build_args.items())),
            target=target,
            pull=pull,
            secrets=dict(sorted(secrets.items())),
            ssh=dict(sorted(ssh.items())),
            devices=dict(sorted(devices.items())),
            external_image=external_image,
            auth_id=auth_id,
        )

    def with_external_publication(
        self,
        *,
        external_image: str | None,
        auth_id: str | None,
    ) -> BuildKitBuildSpec:
        """Return this request with external publication fields replaced.

        Parameters
        ----------
        external_image : str | None
            Optional external manifest destination.
        auth_id : str | None
            Secret capability ID containing Docker auth JSON for external publishing.

        Returns
        -------
        BuildKitBuildSpec
            Validated request spec with the requested publication fields.
        """
        payload = self.model_dump(mode="python")
        payload.update(
            {
                "external_image": external_image,
                "auth_id": auth_id,
            }
        )
        return type(self).model_validate(payload)

    @property
    def identity(self) -> ProjectImageIdentity:
        """Return the project image identity encoded by this request.

        Returns
        -------
        ProjectImageIdentity
            Stable publication identity shared by build, manifest, and lifecycle
            record layers.
        """
        return ProjectImageIdentity(
            repo_id=self.repo_id,
            worktree=self.worktree,
            worktree_id=self.worktree_id,
            config_id=self.config_id,
            image=self.image,
        )

    @property
    def request_labels(self) -> dict[str, str]:
        """Return Kubernetes labels applied to this build request.

        Returns
        -------
        dict[str, str]
            Labels used to identify and filter this build request.
        """
        identity = self.identity
        labels = dict(BUILDKIT_BUILD_LABELS)
        labels.update(
            {
                BUILDKIT_BUILD_REPO_LABEL: _label_hash(identity.repo_id),
                BUILDKIT_BUILD_WORKTREE_LABEL: _label_hash(identity.worktree_id),
                BUILDKIT_BUILD_CONFIG_LABEL: _label_hash(identity.config_id),
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
        identity = self.identity
        return {
            BERTRAND_ENV: "1",
            REPO_ID_ENV: identity.repo_id,
            WORKTREE_ENV: identity.worktree,
            WORKTREE_ID_ENV: identity.worktree_id,
            PROJECT_IMAGE_CONFIG_ID: identity.config_id,
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
    external_digest_ref : str
        External digest ref emitted by manifest copy, if any.
    record_name : str
        `BertrandImage` lifecycle record written by project publication.
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
    external_digest_ref: str = ""
    record_name: str = ""
    message: str = ""
    log_excerpt: str = ""


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

    @classmethod
    def from_payload(cls, payload: object) -> BuildKitBuildRecord:
        """Validate a Kubernetes custom object payload.

        Parameters
        ----------
        payload : object
            Raw Kubernetes custom object payload.

        Returns
        -------
        BuildKitBuildRecord
            Validated build request record.

        Raises
        ------
        OSError
            If the payload is malformed.
        """
        try:
            return cls.model_validate(payload)
        except (TypeError, ValidationError) as err:
            msg = f"malformed {BUILDKIT_BUILD_KIND} custom object: {err}"
            raise OSError(msg) from err

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


def _label_hash(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()[:16]


async def ensure_buildkit_build_crd(kube: Kube, *, timeout: float) -> None:
    """Converge the durable BuildKit build request CRD.

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
        msg = "BuildKitBuild CRD timeout must be non-negative"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    crd = await CustomResourceDefinition.upsert(
        kube,
        group=BUILDKIT_BUILD_GROUP,
        version=BUILDKIT_BUILD_VERSION,
        plural=BUILDKIT_BUILD_PLURAL,
        singular="buildkitbuild",
        kind=BUILDKIT_BUILD_KIND,
        short_names=("bkbuild",),
        spec_schema=_BUILDKIT_BUILD_SPEC_SCHEMA,
        status_schema=_BUILDKIT_BUILD_STATUS_SCHEMA,
        labels=BUILDKIT_BUILD_LABELS,
        timeout=deadline - loop.time(),
    )
    await crd.wait_established(kube, timeout=deadline - loop.time())


async def submit_buildkit_build(
    kube: Kube,
    *,
    spec: BuildKitBuildSpec,
    timeout: float,
) -> BuildKitBuildRecord:
    """Create one durable project-image BuildKit request.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    spec : BuildKitBuildSpec
        Validated project image build request.
    timeout : float
        Maximum creation budget in seconds.

    Returns
    -------
    BuildKitBuildRecord
        Submitted build request.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive or request creation exceeds the budget.
    """
    if timeout <= 0:
        msg = "BuildKitBuild submit timeout must be non-negative"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    obj = await _BUILDKIT_BUILD_CLIENT.create(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=_buildkit_build_name(spec),
        spec=spec.model_dump(mode="json"),
        labels=spec.request_labels,
        timeout=deadline - loop.time(),
    )
    return BuildKitBuildRecord.from_payload(obj.payload)


async def get_buildkit_build(
    kube: Kube,
    *,
    name: str,
    timeout: float,
) -> BuildKitBuildRecord | None:
    """Read one durable BuildKit build request.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    name : str
        Build request name.
    timeout : float
        Maximum request budget in seconds.

    Returns
    -------
    BuildKitBuildRecord | None
        Build request, or `None` if it does not exist.
    """
    obj = await _BUILDKIT_BUILD_CLIENT.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=name,
        timeout=timeout,
    )
    if obj is None:
        return None
    return BuildKitBuildRecord.from_payload(obj.payload)


async def list_buildkit_builds(
    kube: Kube,
    *,
    timeout: float,
    labels: Mapping[str, str] | None = None,
    missing_ok: bool = False,
) -> list[BuildKitBuildRecord]:
    """List durable BuildKit build requests.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum request budget in seconds.
    labels : Mapping[str, str] | None, optional
        Additional exact-match labels to merge over the BuildKit request label.
    missing_ok : bool, optional
        Whether a missing CRD should be treated as an empty list.

    Returns
    -------
    list[BuildKitBuildRecord]
        Validated BuildKit build requests.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    OSError
        If Kubernetes listing fails or returns malformed objects.
    """
    if timeout <= 0:
        msg = "BuildKitBuild list timeout must be non-negative"
        raise TimeoutError(msg)
    selector = {BUILDKIT_BUILD_LABEL: BUILDKIT_BUILD_LABEL_VALUE}
    selector.update(labels or {})
    try:
        objects = await _BUILDKIT_BUILD_CLIENT.list(
            kube,
            namespace=BERTRAND_NAMESPACE,
            labels=selector,
            timeout=timeout,
        )
    except OSError as err:
        detail = str(err).lower()
        if missing_ok and ("not found" in detail or "status 404" in detail):
            return []
        raise
    return [BuildKitBuildRecord.from_payload(obj.payload) for obj in objects]


async def has_active_buildkit_builds(
    kube: Kube,
    *,
    timeout: float,
    repo_id: str | None = None,
) -> bool:
    """Return whether matching BuildKit requests may still mutate resources.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum request budget in seconds.
    repo_id : str | None, optional
        Stable repository UUID to filter by.

    Returns
    -------
    bool
        True when any current-generation or stale-generation matching request has
        not reached a terminal phase.
    """
    repo_id = _check_uuid(repo_id) if repo_id is not None else None
    labels = (
        {BUILDKIT_BUILD_REPO_LABEL: _label_hash(repo_id)}
        if repo_id is not None
        else None
    )
    for record in await list_buildkit_builds(
        kube,
        timeout=timeout,
        labels=labels,
        missing_ok=True,
    ):
        if repo_id is not None and record.spec.repo_id != repo_id:
            continue
        if record.is_active:
            return True
    return False


async def active_buildkit_build_names(kube: Kube, *, timeout: float) -> set[str]:
    """Return names for BuildKit requests that may still own resources.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum request budget in seconds.

    Returns
    -------
    set[str]
        Active BuildKit request names.
    """
    return {
        record.name
        for record in await list_buildkit_builds(
            kube,
            timeout=timeout,
            missing_ok=True,
        )
        if record.is_active
    }


async def wait_buildkit_build(
    kube: Kube,
    *,
    name: str,
    timeout: float,
    on_update: Callable[[BuildKitBuildRecord], Awaitable[None]] | None = None,
) -> BuildKitBuildRecord:
    """Wait for one durable BuildKit build request to reach a terminal phase.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    name : str
        Build request name.
    timeout : float
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
        If `timeout` is non-positive or expires before the request is terminal.
    OSError
        If the request disappears while waiting.
    """
    if timeout <= 0:
        msg = "BuildKitBuild wait timeout must be non-negative"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    seen_version = ""
    while True:
        remaining = deadline - loop.time()
        if remaining <= 0:
            msg = f"BuildKitBuild {name!r} did not finish before timeout"
            raise TimeoutError(msg)
        record = await get_buildkit_build(kube, name=name, timeout=remaining)
        if record is None:
            msg = f"BuildKitBuild {name!r} disappeared while waiting"
            raise OSError(msg)
        if record.resource_version != seen_version:
            seen_version = record.resource_version
            if on_update is not None:
                await on_update(record)
        if record.is_terminal:
            return record
        await asyncio.sleep(min(BUILDKIT_BUILD_WAIT_POLL_SECONDS, remaining))


async def patch_buildkit_build_status(
    kube: Kube,
    *,
    name: str,
    phase: BuildKitBuildPhase,
    generation: int,
    timeout: float,
    **updates: object,
) -> BuildKitBuildRecord:
    """Patch one BuildKit request status subresource.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    name : str
        Build request name.
    phase : {"Pending", "Running", "Succeeded", "Failed"}
        Status phase to record.
    generation : int
        Observed metadata generation.
    timeout : float
        Maximum patch budget in seconds.
    **updates : object
        Additional status fields to patch.

    Returns
    -------
    BuildKitBuildRecord
        Updated build request record.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    """
    if timeout <= 0:
        msg = "BuildKitBuild status patch timeout must be non-negative"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    payload: dict[str, object] = {}
    payload.update(updates)
    payload["phase"] = phase
    payload["observedGeneration"] = generation
    status = BuildKitBuildStatus.model_validate(payload).model_dump(
        mode="json",
        by_alias=True,
        exclude_unset=True,
    )
    obj = await _BUILDKIT_BUILD_CLIENT.patch_status(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=name,
        status=status,
        timeout=deadline - loop.time(),
    )
    return BuildKitBuildRecord.from_payload(obj.payload)


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
