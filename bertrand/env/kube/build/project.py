"""Project configuration adapter for Bertrand's in-cluster BuildKit runtime."""

from __future__ import annotations

import hashlib
import json
from typing import TYPE_CHECKING

from bertrand.env.build_args import normalize_image_build_args
from bertrand.env.config.bertrand import Bertrand, BertrandModel, project_image_tag
from bertrand.env.config.conan import ConanConfig, ConanConfigModel
from bertrand.env.config.core import Config, _check_uuid
from bertrand.env.config.python import PyProject
from bertrand.env.git import GitRepository
from bertrand.env.kube.build.containerfile import project_containerfile
from bertrand.env.kube.build.repository import image_repository_ref
from bertrand.env.kube.build.request import (
    BuildKitBuildSpec,
    worktree_identity,
)

if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence

    from bertrand.env.config.core import KubeName
    from bertrand.env.kube.workload.capability import CapabilityRequest


def project_image_spec(
    config: Config,
    *,
    repo_id: str,
    external_image: str | None = None,
    auth_id: KubeName | None = None,
) -> BuildKitBuildSpec:
    """Translate project image config into a BuildKit build contract.

    Parameters
    ----------
    config : Config
        Active project configuration context.
    repo_id : str
        Stable repository UUID.
    external_image : str | None, optional
        Optional external image ref for manifest publication.
    auth_id : KubeName | None, optional
        Optional Secret capability ID for external registry auth.

    Returns
    -------
    BuildKitBuildSpec
        Build request containing stable identity, target image, and BuildKit execution
        contract.

    Raises
    ------
    TypeError
        If `config` is not a configuration context.
    RuntimeError
        If the configuration context is not active.
    OSError
        If Bertrand configuration is missing or a custom Containerfile cannot be
        read.
    """
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
    image_config = bertrand.image

    worktree = worktree_identity(config.worktree)
    worktree_id = GitRepository.Worktree(repo=config.repo, path=config.root).id
    oci_tag = project_image_tag(pyproject.project.version)
    image = image_repository_ref(
        "/".join(
            (
                "projects",
                repo_id,
                worktree_id,
            )
        ),
        oci_tag,
    )
    dockerfile = project_containerfile(
        config.root,
        bertrand,
        image_config,
    )
    config_id = _config_id(
        repo_id=repo_id,
        worktree_id=worktree_id,
        image_config=image_config,
        conan_config=config.get(ConanConfig),
        dockerfile=dockerfile,
    )
    return BuildKitBuildSpec(
        repo_id=repo_id,
        worktree=worktree,
        worktree_id=worktree_id,
        config_id=config_id,
        image=image,
        dockerfile=dockerfile,
        build_args=_build_args(image_config.args),
        target=image_config.target,
        pull=image_config.pull,
        secrets=_capability_requests(image_config.secrets),
        ssh=_capability_requests(image_config.ssh),
        devices=_capability_requests(image_config.devices),
        external_image=external_image,
        auth_id=auth_id,
    )


def _build_args(args: Mapping[str, object]) -> dict[str, str]:
    return normalize_image_build_args(dict(args))


def _capability_requests(
    requests: Sequence[CapabilityRequest],
) -> dict[KubeName, bool]:
    return {request.id: request.required for request in requests}


def _config_id(
    *,
    repo_id: str,
    worktree_id: str,
    image_config: BertrandModel.Image,
    conan_config: ConanConfigModel | None,
    dockerfile: str,
) -> str:
    payload = {
        "repo_id": repo_id,
        "worktree_id": worktree_id,
        "image": image_config.model_dump(by_alias=True, mode="json"),
        "conan": (
            None
            if conan_config is None
            else conan_config.model_dump(by_alias=True, mode="json")
        ),
        "dockerfile_sha256": hashlib.sha256(dockerfile.encode("utf-8")).hexdigest(),
    }
    text = json.dumps(payload, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(text.encode("utf-8")).hexdigest()
