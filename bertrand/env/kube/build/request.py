"""Shared project-image BuildKit request model."""

from __future__ import annotations

import hashlib
from dataclasses import dataclass
from datetime import datetime  # noqa: TC003
from typing import TYPE_CHECKING, Annotated, Literal

from pydantic import BaseModel, ConfigDict, Field, ValidationError

from bertrand.env.git import (
    BERTRAND_ENV,
    ENV_ID_ENV,
    IMAGE_TAG_ENV,
    REPO_ID_ENV,
    WORKTREE_ENV,
)
from bertrand.env.kube.crd import CustomObjectMetadata  # noqa: TC001

if TYPE_CHECKING:
    from collections.abc import Mapping

BUILDKIT_BUILD_LABEL = "bertrand.dev/buildkit-build"
BUILDKIT_BUILD_LABEL_VALUE = "v1"
BUILDKIT_BUILD_KIND = "BuildKitBuild"
BUILDKIT_BUILD_REPO_LABEL = "bertrand.dev/buildkit-build-repo"
BUILDKIT_BUILD_WORKTREE_LABEL = "bertrand.dev/buildkit-build-worktree"
BUILDKIT_BUILD_TAG_LABEL = "bertrand.dev/buildkit-build-tag"
BUILDKIT_BUILD_CONFIG_LABEL = "bertrand.dev/buildkit-build-config"
BUILDKIT_BUILD_LABELS = {
    BERTRAND_ENV: "1",
    BUILDKIT_BUILD_LABEL: BUILDKIT_BUILD_LABEL_VALUE,
}
PROJECT_IMAGE_CONFIG_ID = "BERTRAND_IMAGE_CONFIG_ID"

type BuildNetworkMode = Literal["default", "none", "host"]
type BuildKitBuildPhase = Literal["Pending", "Running", "Succeeded", "Failed"]
type _NonEmptyString = Annotated[str, Field(min_length=1)]


@dataclass(frozen=True)
class ProjectImageIdentity:
    """Stable project image identity shared by build and lifecycle layers.

    Parameters
    ----------
    repo_id : str
        Stable repository UUID containing the project source.
    worktree : str
        Repository-volume subpath for this worktree.
    tag : str
        Project image configuration key.
    env_id : str
        Deterministic environment UUID for this project image.
    config_id : str
        Deterministic project image configuration fingerprint.
    image : str
        Internal mutable image reference.
    channels : tuple[str, ...]
        Mutable channel names this image should publish.
    """

    repo_id: str
    worktree: str
    tag: str
    env_id: str
    config_id: str
    image: str
    channels: tuple[str, ...]


class BuildKitBuildSpec(BaseModel):
    """Validated project-only `BuildKitBuild.spec` payload.

    Parameters
    ----------
    repo_id : str
        Stable repository UUID containing the project source PVC.
    worktree : str, optional
        Repository-volume subpath for the project worktree.
    tag : str
        Configured project image key.
    env_id : str
        Deterministic capability environment UUID.
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
    network : {'default', 'none', 'host'}
        BuildKit network mode applied to build-time `RUN` instructions.
    secrets : dict[str, bool], optional
        Secret capability requests keyed by capability ID.
    ssh : dict[str, bool], optional
        SSH capability requests keyed by capability ID.
    devices : dict[str, bool], optional
        CDI device capability requests keyed by capability ID.
    channels : list[str], optional
        Mutable channel names to publish after the build.
    external_image : str | None, optional
        Optional external image reference to copy the assembled manifest to.
    auth_id : str | None, optional
        Secret capability ID containing Docker auth JSON for external publishing.
    """

    model_config = ConfigDict(extra="forbid", frozen=True)

    repo_id: _NonEmptyString
    worktree: _NonEmptyString = "."
    tag: str
    env_id: _NonEmptyString
    config_id: _NonEmptyString
    image: _NonEmptyString
    dockerfile: _NonEmptyString
    build_args: dict[str, str] = Field(default_factory=dict)
    target: str | None = None
    network: BuildNetworkMode
    secrets: dict[_NonEmptyString, bool] = Field(default_factory=dict)
    ssh: dict[_NonEmptyString, bool] = Field(default_factory=dict)
    devices: dict[_NonEmptyString, bool] = Field(default_factory=dict)
    channels: list[_NonEmptyString] = Field(default_factory=list)
    external_image: str | None = None
    auth_id: str | None = None

    @classmethod
    def from_identity(
        cls,
        *,
        identity: ProjectImageIdentity,
        dockerfile: str,
        build_args: Mapping[str, str],
        target: str | None,
        network: BuildNetworkMode,
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
        network : {'default', 'none', 'host'}
            BuildKit network mode applied to build-time `RUN` instructions.
        secrets : Mapping[str, bool]
            Secret capability requests exposed to the build.
        ssh : Mapping[str, bool]
            SSH capability requests exposed to the build.
        devices : Mapping[str, bool]
            CDI device capability requests exposed to the build.
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
            tag=identity.tag,
            env_id=identity.env_id,
            config_id=identity.config_id,
            image=identity.image,
            dockerfile=dockerfile,
            build_args=dict(sorted(build_args.items())),
            target=target,
            network=network,
            secrets=dict(sorted(secrets.items())),
            ssh=dict(sorted(ssh.items())),
            devices=dict(sorted(devices.items())),
            channels=sorted({channel.strip() for channel in identity.channels}),
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
            tag=self.tag,
            env_id=self.env_id,
            config_id=self.config_id,
            image=self.image,
            channels=tuple(sorted({channel.strip() for channel in self.channels})),
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
                BUILDKIT_BUILD_WORKTREE_LABEL: _label_hash(identity.worktree),
                BUILDKIT_BUILD_TAG_LABEL: _label_hash(identity.tag),
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
            IMAGE_TAG_ENV: identity.tag,
            ENV_ID_ENV: identity.env_id,
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
    external_channel_digest_refs : dict[str, str]
        External digest-pinned channel refs.
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
    external_channel_digest_refs: dict[str, str] = Field(default_factory=dict)
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
        except ValidationError as err:
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


def _label_hash(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()[:16]
