"""Project configuration adapter for Bertrand's in-cluster BuildKit runtime."""

from __future__ import annotations

import asyncio
import hashlib
import json
import re
import uuid
from dataclasses import dataclass
from typing import TYPE_CHECKING, Literal, Protocol

from bertrand.env.build_args import normalize_image_build_args
from bertrand.env.config import Bertrand, PyProject
from bertrand.env.config.bertrand import project_image_tag
from bertrand.env.git import INFINITY
from bertrand.env.kube.build.containerfile import project_containerfile
from bertrand.env.kube.build.controller import (
    BuildKitBuildRecord,
    submit_buildkit_build,
    wait_buildkit_build,
)
from bertrand.env.kube.build.lifecycle import (
    ProjectImageIdentity,
    ProjectImagePublication,
    ensure_project_image_crd,
    get_project_image,
    worktree_identity,
)
from bertrand.env.kube.build.repository import IMAGES

PROJECT_IMAGE_ENV_NAMESPACE = uuid.UUID("36eb88bb-c284-4cb2-ab0a-57f5e850868a")
_PROJECT_IMAGE_COMPONENT_RE = re.compile(r"[^a-z0-9._-]+")

if TYPE_CHECKING:
    from collections.abc import Awaitable, Callable, Mapping, Sequence

    from bertrand.env.config.core import Config, KubeName
    from bertrand.env.kube.api import Kube


class _CapabilityRequest(Protocol):
    id: KubeName
    required: bool


type _BuildNetworkMode = Literal["default", "none", "host"]


@dataclass(frozen=True)
class ProjectImageBuild:
    """Cluster-native image build request for one configured project image key.

    Parameters
    ----------
    identity : ProjectImageIdentity
        Stable publication identity shared by the request, manifest, and lifecycle
        record layers.
    dockerfile : str
        Rendered Containerfile text submitted with the durable request.
    build_args : dict[str, str]
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
    """

    identity: ProjectImageIdentity
    dockerfile: str
    build_args: dict[str, str]
    target: str | None
    network: _BuildNetworkMode
    secrets: Mapping[KubeName, bool]
    ssh: Mapping[KubeName, bool]
    devices: Mapping[KubeName, bool]

    async def publish(
        self,
        kube: Kube,
        *,
        timeout: float = INFINITY,
        external_image: str | None = None,
        auth_id: KubeName | None = None,
        on_update: Callable[[BuildKitBuildRecord], Awaitable[None]] | None = None,
    ) -> ProjectImagePublication:
        """Publish this project image manifest and update its lifecycle record.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float, optional
            Maximum runtime budget in seconds. If infinite, wait indefinitely.
        external_image : str | None, optional
            Optional external image reference to copy the assembled manifest to.
        auth_id : KubeName | None, optional
            Secret capability ID containing Docker auth JSON for the external
            registry.
        on_update : Callable[[BuildKitBuildRecord], Awaitable[None]] | None, optional
            Async callback invoked when the durable request status changes.

        Returns
        -------
        ProjectImagePublication
            Publication result backed by the active `BertrandImage` record.

        Raises
        ------
        TimeoutError
            If CRD convergence, image publication, or record updates exceed
            `timeout`.
        OSError
            If the durable build request fails or its lifecycle record is missing.
        """
        if timeout <= 0:
            msg = "project image publish timeout must be non-negative"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        request = await self.submit(
            kube,
            timeout=deadline - loop.time(),
            external_image=external_image,
            auth_id=auth_id,
        )
        request = await wait_buildkit_build(
            kube,
            name=request.name,
            timeout=deadline - loop.time(),
            on_update=on_update,
        )
        status = request.status
        if status.phase != "Succeeded":
            detail = status.message or f"BuildKitBuild {request.name!r} failed"
            logs = status.log_excerpt.strip()
            if logs:
                detail = f"{detail}\n\nBuild logs:\n{logs}"
            raise OSError(detail)
        if not status.record_name:
            msg = f"BuildKitBuild {request.name!r} succeeded without a record name"
            raise OSError(msg)
        record = await get_project_image(
            kube,
            name=status.record_name,
            timeout=deadline - loop.time(),
        )
        if record is None:
            msg = (
                f"BuildKitBuild {request.name!r} recorded missing project image "
                f"{status.record_name!r}"
            )
            raise OSError(msg)
        return ProjectImagePublication(
            record=record,
            external_digest_ref=status.external_digest_ref or None,
            external_channel_digest_refs=status.external_channel_digest_refs,
        )

    async def submit(
        self,
        kube: Kube,
        *,
        timeout: float = INFINITY,
        external_image: str | None = None,
        auth_id: KubeName | None = None,
    ) -> BuildKitBuildRecord:
        """Submit this project image as a durable BuildKit request.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float, optional
            Maximum submission budget in seconds. If infinite, wait indefinitely.
        external_image : str | None, optional
            Optional external image reference to copy the assembled manifest to.
        auth_id : KubeName | None, optional
            Secret capability ID containing Docker auth JSON for the external
            registry.

        Returns
        -------
        BuildKitBuildRecord
            Submitted durable build request.

        Raises
        ------
        TimeoutError
            If CRD convergence or request creation exceeds `timeout`.
        """
        if timeout <= 0:
            msg = "project image submit timeout must be non-negative"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        await ensure_project_image_crd(kube, timeout=deadline - loop.time())
        return await submit_buildkit_build(
            kube,
            identity=self.identity,
            dockerfile=self.dockerfile,
            build_args=self.build_args,
            target=self.target,
            network=self.network,
            secrets=self.secrets,
            ssh=self.ssh,
            devices=self.devices,
            timeout=deadline - loop.time(),
            external_image=external_image,
            auth_id=auth_id,
        )


def project_image_build(
    config: Config,
    tag: str,
    *,
    repo_id: str,
) -> ProjectImageBuild:
    """Translate project image config into a BuildKit build contract.

    Parameters
    ----------
    config : Config
        Active project configuration context.
    tag : str
        Image key to build.
    repo_id : str
        Stable repository UUID.

    Returns
    -------
    ProjectImageBuild
        Build request containing deterministic identity, target image, and BuildKit
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
    pyproject = config.get(PyProject)
    if pyproject is None:
        msg = f"missing 'pyproject' configuration for environment at {config.root}"
        raise OSError(msg)
    image_config = bertrand.image.get(tag)
    if image_config is None:
        msg = f"unknown image key '{tag}' for environment at {config.root}"
        raise ValueError(msg)

    worktree = worktree_identity(config.worktree)
    env_id = _env_id(repo_id=repo_id, worktree=worktree)
    oci_tag = project_image_tag(pyproject.project.version, tag)
    image = IMAGES.ref(
        "/".join(
            (
                "projects",
                repo_id,
                _image_component(worktree, fallback="root"),
            )
        ),
        oci_tag,
    )
    dockerfile = project_containerfile(
        config.root,
        bertrand,
        tag,
        image_config,
    )
    config_id = _config_id(
        repo_id=repo_id,
        worktree=worktree,
        tag=tag,
        image_config=image_config,
        dockerfile=dockerfile,
    )
    channels = tuple(
        sorted(
            name for name, channel in bertrand.channel.items() if channel.image == tag
        )
    )
    identity = ProjectImageIdentity(
        repo_id=repo_id,
        worktree=worktree,
        tag=tag,
        env_id=env_id,
        config_id=config_id,
        image=image,
        channels=channels,
    )
    return ProjectImageBuild(
        identity=identity,
        dockerfile=dockerfile,
        build_args=_build_args(image_config.args),
        target=image_config.target,
        network=image_config.network,
        secrets=_capability_requests(image_config.secrets),
        ssh=_capability_requests(image_config.ssh),
        devices=_capability_requests(image_config.devices),
    )


def _env_id(*, repo_id: str, worktree: str) -> str:
    return uuid.uuid5(PROJECT_IMAGE_ENV_NAMESPACE, f"{repo_id}:{worktree}").hex


def _image_component(value: str, *, fallback: str) -> str:
    text = value.strip().lower()
    digest = hashlib.sha256(text.encode("utf-8")).hexdigest()[:12]
    text = _PROJECT_IMAGE_COMPONENT_RE.sub("-", text).strip("._-")
    if not text:
        text = fallback
    return f"{text[:48].rstrip('._-')}-{digest}"


def _build_args(args: Mapping[str, object]) -> dict[str, str]:
    return normalize_image_build_args(dict(args))


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
