"""The external CLI endpoint for building Bertrand images."""

from __future__ import annotations

import asyncio
import sys
from typing import TYPE_CHECKING

from bertrand.env.config import DEFAULT_TAG
from bertrand.env.config.core import Config
from bertrand.env.config.repository import resolve_repo_id
from bertrand.env.git import INFINITY
from bertrand.env.kube.api import Kube
from bertrand.env.kube.build.daemon import BUILDKIT, BuildKitStatus
from bertrand.env.kube.build.project import gc_project_images, project_image_build
from bertrand.env.kube.build.repository import IMAGES, ImageRepositoryStatus

if TYPE_CHECKING:
    from pathlib import Path


async def bertrand_build(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    quiet: bool,
) -> None:
    """Incrementally build Bertrand images within an environment.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable. If None, then the command
        targets tags in the environment's build matrix.
    tag : str | None
        Optional image tag to build. If omitted, the default tag is built.
    quiet : bool
        Whether to suppress build output from the container runtime.

    Notes
    -----
    This command does not materialize or start any containers; it only builds images,
    which corresponds to Ahead-of-Time (AoT) compilation of the container
    environment.
    """
    if workload is not None:
        msg = "kubernetes workloads are not yet supported"
        raise NotImplementedError(msg)
    if tag is None:
        tag = DEFAULT_TAG

    with await Kube.host(timeout=INFINITY) as kube:
        config = await Config.load(worktree, kube=kube, timeout=INFINITY)
        async with config:
            await _assert_build_runtime(kube, timeout=INFINITY)
            repo_id = resolve_repo_id(config.repo)
            plan = project_image_build(config, tag, repo_id=repo_id)
            result = await plan.publish(kube, timeout=INFINITY)
            await _best_effort_gc(kube, quiet=quiet)

    if not quiet:
        print(f"image: {result.image}")
        print(f"digest: {result.digest_ref}")


async def _assert_build_runtime(kube: Kube, *, timeout: float) -> None:
    if timeout <= 0:
        msg = "image build runtime readiness timeout must be non-negative"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    registry = await IMAGES.status(kube, timeout=deadline - loop.time())
    buildkit = await BUILDKIT.status(
        kube,
        timeout=deadline - loop.time(),
        config_hash=IMAGES.buildkit_config_hash,
    )
    failures = [
        *_registry_readiness_failures(registry),
        *_buildkit_readiness_failures(buildkit),
    ]
    if failures:
        detail = "\n".join(f"- {failure}" for failure in failures)
        msg = (
            "Bertrand's image build runtime is not ready. Run `bertrand init` "
            f"to converge the Kubernetes build service.\n{detail}"
        )
        raise OSError(msg)


def _registry_readiness_failures(status: ImageRepositoryStatus) -> list[str]:
    failures: list[str] = []
    if not status.service_ready:
        failures.append("image registry Service is missing or has the wrong shape")
    if not status.rollout_ready:
        failures.append("image registry Deployment rollout is not ready")
    if not status.storage_ready:
        failures.append("image registry storage is not bound and ready")
    if not status.delete_enabled:
        failures.append("image registry manifest deletion is not enabled")
    if not status.config_current:
        failures.append("BuildKit registry routing config is stale")
    if not status.node_trust_ready:
        failures.append("one or more Kubernetes nodes do not trust the image registry")
    return failures


def _buildkit_readiness_failures(status: BuildKitStatus) -> list[str]:
    failures: list[str] = []
    if not status.service_ready:
        failures.append("BuildKit Service is missing or has the wrong shape")
    if not status.rollout_ready:
        failures.append("BuildKit Deployment rollout is not ready")
    if not status.config_current:
        failures.append("BuildKit pod template has stale registry config")
    if not status.storage_ready:
        failures.append("BuildKit cache storage is not bound and ready")
    return failures


async def _best_effort_gc(kube: Kube, *, quiet: bool) -> None:
    try:
        await gc_project_images(kube, timeout=INFINITY)
    except (OSError, TimeoutError, ValueError) as err:
        if not quiet:
            print(
                f"warning: project image garbage collection failed: {err}",
                file=sys.stderr,
            )
