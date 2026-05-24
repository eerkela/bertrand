"""Project configuration adapter for Bertrand's in-cluster BuildKit runtime."""

from __future__ import annotations

import asyncio
import hashlib
import json
from dataclasses import dataclass
from typing import TYPE_CHECKING, Protocol

from bertrand.env.build_args import normalize_image_build_args
from bertrand.env.config.bertrand import Bertrand, project_image_tag
from bertrand.env.config.conan import ConanConfig
from bertrand.env.config.python import PyProject
from bertrand.env.git import INFINITY, ensure_worktree_id
from bertrand.env.kube.build.containerfile import project_containerfile
from bertrand.env.kube.build.lifecycle import (
    ProjectImagePublication,
    ensure_project_image_crd,
    get_project_image,
    worktree_identity,
)
from bertrand.env.kube.build.repository import IMAGES
from bertrand.env.kube.build.request import (
    BuildKitBuildRecord,
    BuildKitBuildSpec,
    ProjectImageIdentity,
    ensure_buildkit_build_crd,
    submit_buildkit_build,
    wait_buildkit_build,
)

if TYPE_CHECKING:
    from collections.abc import Awaitable, Callable, Mapping, Sequence
    from pathlib import Path

    from bertrand.env.config.core import Config, KubeName
    from bertrand.env.kube.api.client import Kube


class _CapabilityRequest(Protocol):
    id: KubeName
    required: bool


@dataclass(frozen=True)
class ProjectImageBuild:
    """Cluster-native image build request for the configured project image.

    Parameters
    ----------
    spec : BuildKitBuildSpec
        Durable project image build request submitted to the BuildKit controller.
    """

    spec: BuildKitBuildSpec

    @property
    def identity(self) -> ProjectImageIdentity:
        """Return the stable project image publication identity.

        Returns
        -------
        ProjectImageIdentity
            Identity shared by the request, manifest, and lifecycle record layers.
        """
        return self.spec.identity

    async def publish(
        self,
        kube: Kube,
        *,
        timeout: float = INFINITY,
        external_image: str | None = None,
        auth_id: KubeName | None = None,
        on_update: Callable[[BuildKitBuildRecord], Awaitable[None]] | None = None,
        ensure_crds: bool = True,
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
        ensure_crds : bool, optional
            Whether to converge the BuildKit/image lifecycle CRDs before submission.
            Disable only from in-cluster dev commands whose service account should
            not own CRD-definition writes.

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
            ensure_crds=ensure_crds,
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
        )

    async def submit(
        self,
        kube: Kube,
        *,
        timeout: float = INFINITY,
        external_image: str | None = None,
        auth_id: KubeName | None = None,
        ensure_crds: bool = True,
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
        ensure_crds : bool, optional
            Whether to converge the BuildKit/image lifecycle CRDs before submission.

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
        if ensure_crds:
            await ensure_buildkit_build_crd(kube, timeout=deadline - loop.time())
            await ensure_project_image_crd(kube, timeout=deadline - loop.time())
        spec = self.spec.with_external_publication(
            external_image=external_image,
            auth_id=auth_id,
        )
        return await submit_buildkit_build(
            kube,
            spec=spec,
            timeout=deadline - loop.time(),
        )


def project_image_build(
    config: Config,
    *,
    repo_id: str,
) -> ProjectImageBuild:
    """Translate project image config into a BuildKit build contract.

    Parameters
    ----------
    config : Config
        Active project configuration context.
    repo_id : str
        Stable repository UUID.

    Returns
    -------
    ProjectImageBuild
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
    image_config = bertrand.image

    worktree = worktree_identity(config.worktree)
    worktree_id = project_worktree_id(config.root)
    oci_tag = project_image_tag(pyproject.project.version)
    image = IMAGES.ref(
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
    identity = ProjectImageIdentity(
        repo_id=repo_id,
        worktree=worktree,
        worktree_id=worktree_id,
        config_id=config_id,
        image=image,
    )
    return ProjectImageBuild(
        spec=BuildKitBuildSpec.from_identity(
            identity=identity,
            dockerfile=dockerfile,
            build_args=_build_args(image_config.args),
            target=image_config.target,
            pull=image_config.pull,
            secrets=_capability_requests(image_config.secrets),
            ssh=_capability_requests(image_config.ssh),
            devices=_capability_requests(image_config.devices),
        )
    )


def project_worktree_id(worktree_root: Path) -> str:
    """Return the persistent worktree ID for one project worktree.

    Parameters
    ----------
    worktree_root : Path
        Absolute or relative checked-out worktree root.

    Returns
    -------
    str
        Stable UUID hex string stored at ``.bertrand/worktree_id``.
    """
    return ensure_worktree_id(worktree_root)


def _build_args(args: Mapping[str, object]) -> dict[str, str]:
    return normalize_image_build_args(dict(args))


def _capability_requests(
    requests: Sequence[_CapabilityRequest],
) -> dict[KubeName, bool]:
    return {request.id: request.required for request in requests}


def _config_id(
    *,
    repo_id: str,
    worktree_id: str,
    image_config: Bertrand.Model.Image,
    conan_config: ConanConfig.Model | None,
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
