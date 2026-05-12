"""Project configuration adapter for Bertrand's in-cluster BuildKit runtime."""

from __future__ import annotations

import asyncio
import hashlib
import json
import re
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING, Protocol

from bertrand.env.config import Bertrand, PyProject
from bertrand.env.config.bertrand import project_image_tag
from bertrand.env.git import (
    BERTRAND_ENV,
    ENV_ID_ENV,
    IMAGE_TAG_ENV,
    INFINITY,
    REPO_ID_ENV,
    WORKTREE_ENV,
)
from bertrand.env.kube.build.containerfile import project_containerfile
from bertrand.env.kube.build.job import BuildKitImageBuild
from bertrand.env.kube.build.lifecycle import (
    ProjectImagePublication,
    ensure_project_image_crd,
    worktree_identity,
)
from bertrand.env.kube.build.manifest import publish_image_manifest
from bertrand.env.kube.build.repository import IMAGES

PROJECT_IMAGE_CONFIG_ID = "BERTRAND_IMAGE_CONFIG_ID"
PROJECT_IMAGE_ENV_NAMESPACE = uuid.UUID("36eb88bb-c284-4cb2-ab0a-57f5e850868a")
PROJECT_IMAGE_CONTEXT_PREFIX = "bertrand-project-image"
_PROJECT_IMAGE_COMPONENT_RE = re.compile(r"[^a-z0-9._-]+")

if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence

    from bertrand.env.config.core import Config, KubeName, OCIImageRef
    from bertrand.env.kube.api import Kube


class _CapabilityRequest(Protocol):
    id: KubeName
    required: bool


@dataclass(frozen=True)
class ProjectImageBuild:
    """Cluster-native image build plan for one configured project image key.

    Parameters
    ----------
    repo_id : str
        Stable repository UUID used to scope the image and capabilities.
    worktree : str
        Repository-relative worktree path, or ``"."`` for the repository root.
    tag : str
        Configured image key from ``[tool.bertrand.image]``.
    env_id : str
        Deterministic environment UUID used for capability resolution.
    config_id : str
        Deterministic hash of the image configuration inputs.
    oci_tag : str
        Derived OCI tag used for internal and external registry references.
    image : OCIImageRef
        Mutable registry reference published by this build.
    channels : tuple[str, ...]
        Moving channel tags to publish as aliases of this image.
    build : BuildKitImageBuild
        Underlying BuildKit Job contract.
    """

    repo_id: str
    worktree: str
    tag: str
    env_id: str
    config_id: str
    oci_tag: str
    image: OCIImageRef
    channels: tuple[str, ...]
    build: BuildKitImageBuild

    async def publish(
        self,
        kube: Kube,
        *,
        timeout: float = INFINITY,
        external_image: str | None = None,
        auth_id: KubeName | None = None,
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

        Returns
        -------
        ProjectImagePublication
            Publication result backed by the active `BertrandImage` record.

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
        platform_results = await self.build.publish_platforms(
            kube,
            env_id=self.env_id,
            timeout=deadline - loop.time(),
        )
        return await publish_image_manifest(
            kube,
            plan=self,
            platform_results=platform_results,
            timeout=deadline - loop.time(),
            external_image=external_image,
            auth_id=auth_id,
            env_id=self.env_id,
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
    dockerfile = project_containerfile(config.root, bertrand, tag, image_config)
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
    return ProjectImageBuild(
        repo_id=repo_id,
        worktree=worktree,
        tag=tag,
        env_id=env_id,
        config_id=config_id,
        oci_tag=oci_tag,
        image=image,
        channels=channels,
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
