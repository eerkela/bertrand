"""Durable BuildKit build requests and their Kubernetes controller."""

from __future__ import annotations

import asyncio
import hashlib
import json
import sys
import uuid
from datetime import UTC, datetime
from typing import TYPE_CHECKING, Annotated, Literal

from pydantic import BaseModel, ConfigDict, Field, ValidationError

from bertrand.env.git import (
    BERTRAND_ENV,
    BERTRAND_NAMESPACE,
    ENV_ID_ENV,
    IMAGE_TAG_ENV,
    INFINITY,
    REPO_ID_ENV,
    WORKTREE_ENV,
)
from bertrand.env.kube.api import (
    CLUSTER_REGISTRY_READY_LABEL,
    CLUSTER_REGISTRY_READY_VALUE,
    ContainerSpec,
    CustomResourceSpec,
    Kube,
    PodTemplateSpec,
    PolicyRuleSpec,
)
from bertrand.env.kube.build.job import _ProjectBuildExecutor
from bertrand.env.kube.build.lifecycle import (
    PROJECT_IMAGE_CONFIG_ID,
    ProjectImageIdentity,
    ProjectImagePublication,
    ensure_project_image_crd,
)
from bertrand.env.kube.build.manifest import _publish_project_image_manifest
from bertrand.env.kube.crd import CustomResourceClient, CustomResourceDefinition
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.rbac import ClusterRole, ClusterRoleBinding
from bertrand.env.kube.service_account import ServiceAccount

if TYPE_CHECKING:
    from collections.abc import Awaitable, Callable, Mapping

    from bertrand.env.config.core import KubeName
    from bertrand.env.kube.job import Job


BUILDKIT_BUILD_GROUP = "build.bertrand.dev"
BUILDKIT_BUILD_VERSION = "v1alpha1"
BUILDKIT_BUILD_KIND = "BuildKitBuild"
BUILDKIT_BUILD_PLURAL = "buildkitbuilds"
BUILDKIT_BUILD_LABEL = "bertrand.dev/buildkit-build"
BUILDKIT_BUILD_LABEL_VALUE = "v1"
BUILDKIT_BUILD_REPO_LABEL = "bertrand.dev/buildkit-build-repo"
BUILDKIT_BUILD_WORKTREE_LABEL = "bertrand.dev/buildkit-build-worktree"
BUILDKIT_BUILD_TAG_LABEL = "bertrand.dev/buildkit-build-tag"
BUILDKIT_BUILD_CONFIG_LABEL = "bertrand.dev/buildkit-build-config"
BUILDKIT_BUILD_CONTROLLER = "bertrand-build-controller"
BUILDKIT_BUILD_SERVICE_ACCOUNT = "bertrand-build-controller"
BUILDKIT_BUILD_RECONCILE_SECONDS = 2.0
BUILDKIT_BUILD_WAIT_POLL_SECONDS = 2.0
BUILDKIT_BUILD_LOG_EXCERPT_CHARS = 4000

type _BuildKitBuildPhase = Literal["Pending", "Running", "Succeeded", "Failed"]
type _BuildNetworkMode = Literal["default", "none", "host"]
type _NonEmptyString = Annotated[str, Field(min_length=1)]


class _ObjectMeta(BaseModel):
    """Validated subset of Kubernetes object metadata."""

    model_config = ConfigDict(extra="ignore")
    name: str = ""
    namespace: str = ""
    generation: int = 0
    resource_version: str = Field(default="", alias="resourceVersion")
    labels: dict[str, str] = Field(default_factory=dict)


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
    network: _BuildNetworkMode
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
        network: _BuildNetworkMode,
        secrets: Mapping[str, bool],
        ssh: Mapping[str, bool],
        devices: Mapping[str, bool],
        external_image: str | None,
        auth_id: str | None,
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
        external_image : str | None
            Optional external manifest destination.
        auth_id : str | None
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
        labels = dict(_BUILDKIT_BUILD_LABELS)
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
    phase: _BuildKitBuildPhase = "Pending"
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


_STRING_MAP_SCHEMA = {"type": "object", "additionalProperties": {"type": "string"}}
_BOOL_MAP_SCHEMA = {"type": "object", "additionalProperties": {"type": "boolean"}}
_STRING_LIST_SCHEMA = {
    "type": "array",
    "items": {"type": "string", "minLength": 1},
    "uniqueItems": True,
}
_BUILDKIT_BUILD_SPEC_SCHEMA = {
    "type": "object",
    "required": [
        "repo_id",
        "worktree",
        "tag",
        "env_id",
        "config_id",
        "image",
        "dockerfile",
        "network",
        "channels",
    ],
    "properties": {
        "repo_id": {"type": "string", "minLength": 1},
        "worktree": {"type": "string", "minLength": 1},
        "tag": {"type": "string"},
        "env_id": {"type": "string", "minLength": 1},
        "config_id": {"type": "string", "minLength": 1},
        "image": {"type": "string", "minLength": 1},
        "dockerfile": {"type": "string", "minLength": 1},
        "build_args": _STRING_MAP_SCHEMA,
        "target": {"type": "string", "nullable": True},
        "network": {"type": "string", "enum": ["default", "none", "host"]},
        "secrets": _BOOL_MAP_SCHEMA,
        "ssh": _BOOL_MAP_SCHEMA,
        "devices": _BOOL_MAP_SCHEMA,
        "channels": _STRING_LIST_SCHEMA,
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
        "external_channel_digest_refs": _STRING_MAP_SCHEMA,
        "record_name": {"type": "string"},
        "message": {"type": "string"},
        "log_excerpt": {"type": "string"},
    },
}
_BUILDKIT_BUILD_LABELS = {
    BERTRAND_ENV: "1",
    BUILDKIT_BUILD_LABEL: BUILDKIT_BUILD_LABEL_VALUE,
}
_BUILDKIT_BUILD_SPEC = CustomResourceSpec(
    group=BUILDKIT_BUILD_GROUP,
    version=BUILDKIT_BUILD_VERSION,
    kind=BUILDKIT_BUILD_KIND,
    plural=BUILDKIT_BUILD_PLURAL,
    labels=_BUILDKIT_BUILD_LABELS,
)
_BUILDKIT_BUILD_CLIENT = CustomResourceClient(_BUILDKIT_BUILD_SPEC)


class BuildKitBuildRecord(BaseModel):
    """Read-only model for one `BuildKitBuild` custom object.

    Parameters
    ----------
    api_version : str
        Kubernetes API version reported by the custom object.
    kind : {"BuildKitBuild"}
        Kubernetes custom object kind.
    metadata : _ObjectMeta
        Kubernetes object metadata.
    spec : BuildKitBuildSpec
        Validated project build request spec.
    status : BuildKitBuildStatus
        Validated build status payload.
    """

    model_config = ConfigDict(extra="forbid", frozen=True, populate_by_name=True)

    api_version: str = Field(alias="apiVersion")
    kind: Literal["BuildKitBuild"]
    metadata: _ObjectMeta
    spec: BuildKitBuildSpec
    status: BuildKitBuildStatus = Field(default_factory=BuildKitBuildStatus)

    @classmethod
    def _from_payload(cls, payload: object) -> BuildKitBuildRecord:
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


class _BuildKitBuildController:
    """In-cluster reconciler for durable BuildKit build requests."""

    async def run(self, *, timeout: float = INFINITY) -> None:
        """Run the controller reconciliation loop.

        Parameters
        ----------
        timeout : float, optional
            Maximum runtime budget in seconds. If infinite, run indefinitely.

        Raises
        ------
        TimeoutError
            If `timeout` is non-positive.
        """
        if timeout <= 0:
            msg = "BuildKit build controller timeout must be non-negative"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        with Kube.inside_cluster(namespace=BERTRAND_NAMESPACE) as kube:
            while loop.time() < deadline:
                try:
                    await self.reconcile_all(kube, deadline=deadline)
                except (OSError, TimeoutError, ValueError) as err:
                    print(
                        f"warning: BuildKit build reconciliation failed: {err}",
                        file=sys.stderr,
                        flush=True,
                    )
                remaining = deadline - loop.time()
                if remaining <= 0:
                    break
                await asyncio.sleep(min(BUILDKIT_BUILD_RECONCILE_SECONDS, remaining))

    async def reconcile_all(self, kube: Kube, *, deadline: float) -> None:
        """Reconcile every non-terminal `BuildKitBuild` in the Bertrand namespace.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : float
            Absolute event-loop deadline for this pass.
        """
        loop = asyncio.get_running_loop()
        objects = await _BUILDKIT_BUILD_CLIENT.list(
            kube,
            namespace=BERTRAND_NAMESPACE,
            labels={BUILDKIT_BUILD_LABEL: BUILDKIT_BUILD_LABEL_VALUE},
            timeout=deadline - loop.time(),
        )
        requests = [BuildKitBuildRecord._from_payload(obj.payload) for obj in objects]
        for request in sorted(requests, key=lambda item: item.metadata.name):
            if (
                request.status.observed_generation == request.metadata.generation
                and request.status.phase in ("Succeeded", "Failed")
            ):
                continue
            await self.reconcile(kube, request=request, deadline=deadline)

    async def reconcile(
        self,
        kube: Kube,
        *,
        request: BuildKitBuildRecord,
        deadline: float,
    ) -> None:
        """Reconcile one build request generation.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        request : BuildKitBuildRecord
            Build request to execute.
        deadline : float
            Absolute event-loop deadline for this reconciliation.
        """
        loop = asyncio.get_running_loop()
        try:
            await _patch_build_status(
                kube,
                name=request.metadata.name,
                phase="Running",
                generation=request.metadata.generation,
                started_at=datetime.now(UTC).isoformat(),
                completed_at=None,
                active_job="",
                active_platform="",
                external_digest_ref="",
                external_channel_digest_refs={},
                record_name="",
                message="BuildKit build is running",
                log_excerpt="",
                timeout=deadline - loop.time(),
            )
            platform_refs = await self._publish_platforms(
                kube,
                request=request,
                deadline=deadline,
            )
            publication = await self._publish_manifest(
                kube,
                request=request,
                platform_refs=platform_refs,
                deadline=deadline,
            )
            await _patch_build_status(
                kube,
                name=request.metadata.name,
                phase="Succeeded",
                generation=request.metadata.generation,
                completed_at=datetime.now(UTC).isoformat(),
                active_job="",
                active_platform="",
                external_digest_ref=publication.external_digest_ref or "",
                external_channel_digest_refs=publication.external_channel_digest_refs,
                record_name=publication.record.name,
                message="BuildKit build succeeded",
                log_excerpt="",
                timeout=deadline - loop.time(),
            )
        except (OSError, TimeoutError, ValueError) as err:
            await _patch_build_status(
                kube,
                name=request.metadata.name,
                phase="Failed",
                generation=request.metadata.generation,
                completed_at=datetime.now(UTC).isoformat(),
                active_job="",
                active_platform="",
                message=str(err).splitlines()[0][:240] if str(err) else "Build failed",
                log_excerpt=_log_excerpt(str(err)),
                timeout=deadline - loop.time(),
            )

    async def _publish_platforms(
        self,
        kube: Kube,
        *,
        request: BuildKitBuildRecord,
        deadline: float,
    ) -> dict[str, str]:
        spec = request.spec
        identity = spec.identity
        build = _ProjectBuildExecutor(
            image=spec.image,
            dockerfile=spec.dockerfile,
            repo_id=identity.repo_id,
            worktree=identity.worktree,
            build_args=dict(spec.build_args),
            image_labels=spec.image_labels,
            target=spec.target,
            network=spec.network,
            secrets=dict(spec.secrets),
            ssh=dict(spec.ssh),
            devices=dict(spec.devices),
        )
        loop = asyncio.get_running_loop()

        async def observe_job(platform: str, job: Job) -> None:
            await _patch_build_status(
                kube,
                name=request.metadata.name,
                phase="Running",
                generation=request.metadata.generation,
                active_job=job.name,
                active_platform=platform,
                message=f"BuildKit build is running for {platform}",
                timeout=deadline - loop.time(),
            )

        return await build.publish_platforms(
            kube,
            env_id=identity.env_id,
            job_observer=observe_job,
            timeout=deadline - loop.time(),
        )

    async def _publish_manifest(
        self,
        kube: Kube,
        *,
        request: BuildKitBuildRecord,
        platform_refs: Mapping[str, str],
        deadline: float,
    ) -> ProjectImagePublication:
        spec = request.spec
        identity = spec.identity
        loop = asyncio.get_running_loop()

        async def observe_job(job: Job) -> None:
            await _patch_build_status(
                kube,
                name=request.metadata.name,
                phase="Running",
                generation=request.metadata.generation,
                active_job=job.name,
                active_platform="",
                message="image manifest assembly is running",
                timeout=deadline - loop.time(),
            )

        return await _publish_project_image_manifest(
            kube,
            identity=identity,
            platform_refs=platform_refs,
            external_image=spec.external_image,
            auth_id=spec.auth_id,
            env_id=identity.env_id,
            job_observer=observe_job,
            timeout=deadline - loop.time(),
        )


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
        labels=_BUILDKIT_BUILD_LABELS,
        timeout=deadline - loop.time(),
    )
    await crd.wait_established(kube, timeout=deadline - loop.time())


async def ensure_buildkit_build_controller(
    kube: Kube,
    *,
    image: str,
    timeout: float,
) -> None:
    """Converge the in-cluster BuildKit build controller.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    image : str
        Bertrand control-plane image containing this package and runtime
        dependencies.
    timeout : float
        Maximum convergence budget in seconds.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive or rollout exceeds the budget.
    ValueError
        If the controller image reference is empty.
    """
    image = image.strip()
    if not image:
        msg = "BuildKit build controller image cannot be empty"
        raise ValueError(msg)
    if timeout <= 0:
        msg = "BuildKit build controller timeout must be non-negative"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    await ensure_buildkit_build_crd(kube, timeout=deadline - loop.time())
    await ensure_project_image_crd(kube, timeout=deadline - loop.time())
    await ServiceAccount.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=BUILDKIT_BUILD_SERVICE_ACCOUNT,
        labels=_BUILDKIT_BUILD_LABELS,
        timeout=deadline - loop.time(),
    )
    await ClusterRole.upsert(
        kube,
        name=BUILDKIT_BUILD_CONTROLLER,
        labels=_BUILDKIT_BUILD_LABELS,
        rules=_controller_rules(),
        timeout=deadline - loop.time(),
    )
    await ClusterRoleBinding.upsert(
        kube,
        name=BUILDKIT_BUILD_CONTROLLER,
        role_name=BUILDKIT_BUILD_CONTROLLER,
        service_account_name=BUILDKIT_BUILD_SERVICE_ACCOUNT,
        service_account_namespace=BERTRAND_NAMESPACE,
        labels=_BUILDKIT_BUILD_LABELS,
        timeout=deadline - loop.time(),
    )
    deployment = await Deployment.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=BUILDKIT_BUILD_CONTROLLER,
        labels=_BUILDKIT_BUILD_LABELS,
        selector={BUILDKIT_BUILD_LABEL: BUILDKIT_BUILD_LABEL_VALUE},
        replicas=1,
        pod_template=PodTemplateSpec(
            containers=[
                ContainerSpec(
                    name="controller",
                    image=image,
                    image_pull_policy="IfNotPresent",
                    command=["python", "-m", "bertrand.env.kube.build.controller"],
                )
            ],
            service_account_name=BUILDKIT_BUILD_SERVICE_ACCOUNT,
            automount_service_account_token=True,
            node_selector={
                CLUSTER_REGISTRY_READY_LABEL: CLUSTER_REGISTRY_READY_VALUE,
            },
        ),
        timeout=deadline - loop.time(),
    )
    await deployment.wait_rollout(kube, timeout=deadline - loop.time())


async def submit_buildkit_build(
    kube: Kube,
    *,
    identity: ProjectImageIdentity,
    dockerfile: str,
    build_args: Mapping[str, str],
    target: str | None,
    network: _BuildNetworkMode,
    secrets: Mapping[KubeName, bool],
    ssh: Mapping[KubeName, bool],
    devices: Mapping[KubeName, bool],
    timeout: float,
    external_image: str | None = None,
    auth_id: KubeName | None = None,
) -> BuildKitBuildRecord:
    """Create one durable project-image BuildKit request.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
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
    secrets : Mapping[KubeName, bool]
        Secret capability requests exposed to the build.
    ssh : Mapping[KubeName, bool]
        SSH capability requests exposed to the build.
    devices : Mapping[KubeName, bool]
        CDI device capability requests exposed to the build.
    timeout : float
        Maximum creation budget in seconds.
    external_image : str | None, optional
        Optional external manifest destination.
    auth_id : KubeName | None, optional
        Secret capability ID containing Docker auth JSON for external publishing.

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
    await ensure_buildkit_build_crd(kube, timeout=deadline - loop.time())
    spec = BuildKitBuildSpec.from_identity(
        identity=identity,
        dockerfile=dockerfile,
        build_args=build_args,
        target=target,
        network=network,
        secrets=secrets,
        ssh=ssh,
        devices=devices,
        external_image=external_image,
        auth_id=auth_id,
    )
    obj = await _BUILDKIT_BUILD_CLIENT.create(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=_buildkit_build_name(spec),
        spec=spec.model_dump(mode="json"),
        labels=spec.request_labels,
        timeout=deadline - loop.time(),
    )
    return BuildKitBuildRecord._from_payload(obj.payload)


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
    return BuildKitBuildRecord._from_payload(obj.payload)


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
        if record.status.phase in ("Succeeded", "Failed"):
            return record
        await asyncio.sleep(min(BUILDKIT_BUILD_WAIT_POLL_SECONDS, remaining))


async def run_buildkit_build_controller(*, timeout: float = INFINITY) -> None:
    """Run the in-cluster durable BuildKit build controller.

    Parameters
    ----------
    timeout : float, optional
        Maximum runtime budget in seconds. If infinite, run indefinitely.
    """
    await _BuildKitBuildController().run(timeout=timeout)


def main() -> int:
    """Run the BuildKit build controller module entrypoint.

    Returns
    -------
    int
        Process exit status.
    """
    try:
        asyncio.run(run_buildkit_build_controller())
    except KeyboardInterrupt:
        return 0
    return 0


def _controller_rules() -> tuple[PolicyRuleSpec, ...]:
    return (
        PolicyRuleSpec(
            api_groups=[BUILDKIT_BUILD_GROUP],
            resources=[
                BUILDKIT_BUILD_PLURAL,
                f"{BUILDKIT_BUILD_PLURAL}/status",
                "bertrandimages",
            ],
            verbs=["get", "list", "watch", "create", "update", "patch", "delete"],
        ),
        PolicyRuleSpec(
            api_groups=["batch"],
            resources=["jobs"],
            verbs=["get", "list", "watch", "create", "delete"],
        ),
        PolicyRuleSpec(
            api_groups=[""],
            resources=["pods", "pods/log", "configmaps", "secrets", "nodes"],
            verbs=["get", "list", "watch", "create", "update", "patch", "delete"],
        ),
    )


def _buildkit_build_name(spec: BuildKitBuildSpec) -> str:
    text = json.dumps(
        spec.model_dump(mode="json"),
        sort_keys=True,
        separators=(",", ":"),
    )
    digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
    return f"bertrand-build-{digest[:16]}-{uuid.uuid4().hex[:8]}"


async def _patch_build_status(
    kube: Kube,
    *,
    name: str,
    phase: _BuildKitBuildPhase,
    generation: int,
    timeout: float,
    **updates: object,
) -> BuildKitBuildRecord:
    if timeout <= 0:
        msg = "BuildKitBuild status patch timeout must be non-negative"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    payload: dict[str, object] = {}
    payload.update(updates)
    payload["phase"] = phase
    payload["observedGeneration"] = generation
    payload = BuildKitBuildStatus.model_validate(payload).model_dump(
        mode="json",
        by_alias=True,
        exclude_unset=True,
    )
    obj = await _BUILDKIT_BUILD_CLIENT.patch_status(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=name,
        status=payload,
        timeout=deadline - loop.time(),
    )
    return BuildKitBuildRecord._from_payload(obj.payload)


def _log_excerpt(text: str) -> str:
    lines = text.strip().splitlines()
    excerpt = "\n".join(lines[-80:])
    if len(excerpt) > BUILDKIT_BUILD_LOG_EXCERPT_CHARS:
        return excerpt[-BUILDKIT_BUILD_LOG_EXCERPT_CHARS:]
    return excerpt


def _label_hash(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()[:16]


if __name__ == "__main__":
    raise SystemExit(main())
