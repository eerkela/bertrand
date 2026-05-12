"""The external CLI endpoint for building Bertrand images."""

from __future__ import annotations

import asyncio
import sys
from typing import TYPE_CHECKING

from bertrand.env.config import Bertrand
from bertrand.env.config.core import Config, _check_kube_name
from bertrand.env.config.repository import resolve_repo_id
from bertrand.env.git import INFINITY
from bertrand.env.kube.api import Kube
from bertrand.env.kube.build.daemon import BUILDKIT_POOL, BuildKitPoolStatus
from bertrand.env.kube.build.project import gc_project_images, project_image_build
from bertrand.env.kube.build.repository import (
    IMAGE_TAG_RE,
    IMAGES,
    ImageRepositoryStatus,
)

if TYPE_CHECKING:
    from pathlib import Path

    from bertrand.env.config.core import TOMLKey
    from bertrand.env.kube.build.manifest import ImageManifestResult
    from bertrand.env.kube.build.project import ProjectImageResult


# NOTE: Distributed native image build roadmap.
#
# Bertrand's final image build path should be manifest-oriented and native to the
# local Kubernetes cluster.  The command should read the whole project config,
# build every entry in `[tool.bertrand.image]`, publish a canonical manifest for
# each entry to the in-cluster registry, and optionally publish the same manifest
# to an external registry with `--publish`.
#
# Locked design decisions:
# - `bertrand build <worktree>` builds the entire image matrix.
# - `bertrand build` does not accept image tag suffixes such as `<worktree>:debug`.
#   If targeted builds ever return, they should use an explicit flag instead.
# - There is no `--all` flag because building the matrix is the default behavior.
# - `bertrand publish` should be removed from the active CLI once
#   `bertrand build --publish` provides the distribution path.
# - `[tool.bertrand.image.<name>]` names are image variant names, not aliases or
#   release channels.
# - Internal and external publishes use the same logical image tags:
#   `<internal-project-repo>:<name>` and `<external-repo>:<name>`.
# - External publishing is additive.  It never replaces the internal manifest or
#   the `BertrandImage` lifecycle record.
# - Registry authorization is provided by `--auth <capability-id>`, resolved as a
#   Secret-backed capability containing Docker `config.json` bytes.
# - BuildKit is still runtime infrastructure, not Bertrand domain state.  Keep the
#   native builder pool as wrapper/convergence code unless we explicitly decide to
#   introduce a CRD for asynchronous build-request state later.
#
# Target architecture:
# - The cluster's ready, schedulable node platforms define the default build
#   target set.  Foreign platforms and CI/cloud builders are future extensions.
# - BuildKit should become platform-aware.  Each platform build is executed by a
#   BuildKit daemon scheduled on a node of that same platform, not by relying on a
#   singleton daemon plus QEMU/cross-compilation.
# - Each image/platform build pushes a deterministic temporary internal ref.
# - A manifest assembly Job combines the platform refs into the canonical internal
#   manifest tag, and into the external tag when `--publish` is present.
# - `BertrandImage` records the canonical internal manifest digest and owned
#   platform digest refs.  External refs are distribution outputs and are not
#   owned by cluster GC.
#
# Implementation phases:
# 1. Builder pool foundation:
#    - hard-cut the singleton BuildKit convergence object to a per-node DaemonSet
#      builder pool;
#    - expose builder status: platform, node, endpoint, rollout, cache path,
#      config hash freshness, and CDI mount readiness;
#    - preserve registry config, rootful BuildKit, and CDI mounts;
#    - use node-local cache under `CACHE_DIR / "buildkit"` with registry cache as
#      the portable cross-node layer.
# 2. Platform discovery and selection:
#    - add Node wrapper helpers for canonical OS/architecture platform strings;
#    - derive target platforms from ready, schedulable nodes;
#    - choose exactly one ready BuildKit builder for each target platform;
#    - fail before building if any platform has no ready native builder.
# 3. Per-platform BuildKit jobs:
#    - extend the BuildKit job contract with one target platform, builder endpoint,
#      output refs, and optional Docker config auth;
#    - run one build Job per image/platform pair against the selected platform
#      builder;
#    - resolve CDI device capabilities against the selected builder's node;
#    - keep secret, SSH, cache, metadata capture, log tailing, and foreground
#      cleanup behavior intact.
# 4. Manifest assembly:
#    - add an in-cluster manifest assembly Job using `regctl`, with insecure HTTP
#      configuration scoped only to the internal registry Service host;
#    - assemble canonical internal manifests from all platform refs;
#    - push the same manifest externally when `--publish <repo>` is provided;
#    - mount Docker config auth for external registry access only when `--auth` is
#      provided.
# 5. Lifecycle and GC:
#    - write one `BertrandImage` record per canonical internal manifest, including
#      its owned temporary platform digest refs;
#    - mark superseded records for the same repo/worktree/image name as retired;
#    - extend bounded GC to delete retired canonical manifests and owned temporary
#      platform refs after grace, while preserving live-Pod protections;
#    - keep external registry retention outside Bertrand's cluster GC.
# 6. CLI cutover:
#    - make this command build every configured image entry by default;
#    - reject workload targeting and image suffix targeting with clear errors;
#    - add `--publish <repo>` and `--auth <capability-id>`;
#    - remove the active `bertrand publish` parser/handler;
#    - print a compact summary with image name, platforms, internal digest ref, and
#      optional external ref.
#
# Acceptance criteria:
# - every ready cluster platform is represented in each internal image manifest;
# - no worktree image metadata or IID files are written;
# - `BertrandImage` remains the internal lifecycle source of truth;
# - native builder selection, device capability locality, and manifest assembly are
#   deterministic and fail closed;
# - external publish failures fail the build, while internal GC failures stay
#   best-effort warnings.


async def bertrand_build(
    worktree: Path,
    *,
    publish: str | None = None,
    auth: str | None = None,
    quiet: bool,
) -> None:
    """Build all configured Bertrand images through the Kubernetes BuildKit pool.

    Parameters
    ----------
    worktree : Path
        Project worktree path.
    publish : str | None, optional
        Optional external OCI repository root to publish manifests to.
    auth : str | None, optional
        Optional Secret-backed capability ID containing Docker auth JSON for the
        external registry.
    quiet : bool
        Whether to suppress build summary output.

    Raises
    ------
    ValueError
        If external publish or auth inputs are invalid.

    Notes
    -----
    This command does not materialize or start any containers. It publishes image
    manifests and lifecycle records only.
    """
    publish = _normalize_publish_repository(publish)
    if auth is not None:
        auth = auth.strip()
        if not auth:
            msg = "--auth cannot be empty"
            raise ValueError(msg)
        if publish is None:
            msg = "--auth requires --publish"
            raise ValueError(msg)
        auth = _check_kube_name(auth)

    results: list[tuple[TOMLKey, ProjectImageResult]] = []
    with await Kube.host(timeout=INFINITY) as kube:
        config = await Config.load(worktree, kube=kube, timeout=INFINITY)
        async with config:
            await _assert_build_runtime(kube, timeout=INFINITY)
            repo_id = resolve_repo_id(config.repo)
            tags = _image_tags(config)
            for tag in tags:
                plan = project_image_build(config, tag, repo_id=repo_id)
                external_image = (
                    _external_image(publish, tag) if publish is not None else None
                )
                result = await plan.publish(
                    kube,
                    timeout=INFINITY,
                    external_image=external_image,
                    auth_id=auth,
                )
                results.append((tag, result))
            await _best_effort_gc(kube, quiet=quiet)

    if not quiet:
        _print_results(results)


def _image_tags(config: Config) -> tuple[TOMLKey, ...]:
    bertrand = config.get(Bertrand)
    if bertrand is None:
        msg = f"missing 'bertrand' configuration for environment at {config.root}"
        raise OSError(msg)
    if not bertrand.image:
        msg = f"environment at {config.root} does not define any images"
        raise OSError(msg)
    return tuple(sorted(bertrand.image))


def _normalize_publish_repository(repo: str | None) -> str | None:
    if repo is None:
        return None
    value = repo.strip().strip("/").lower()
    if not value:
        msg = "--publish requires a non-empty OCI repository"
        raise ValueError(msg)
    if "://" in value:
        msg = "--publish expects an OCI repository without a URL scheme"
        raise ValueError(msg)
    if "@" in value:
        msg = "--publish expects a repository, not a digest reference"
        raise ValueError(msg)
    slash = value.rfind("/")
    colon = value.rfind(":")
    if slash < 1:
        msg = "--publish expects a repository path such as 'ghcr.io/owner/repo'"
        raise ValueError(msg)
    if colon > slash:
        msg = "--publish expects a repository, not a tagged image reference"
        raise ValueError(msg)
    return value


def _external_image(repo: str, tag: str) -> str:
    if not IMAGE_TAG_RE.fullmatch(tag):
        msg = f"image config key is not a valid OCI tag: {tag!r}"
        raise ValueError(msg)
    return f"{repo}:{tag}"


def _print_results(results: list[tuple[TOMLKey, ProjectImageResult]]) -> None:
    for index, (tag, result) in enumerate(results):
        if index:
            print()
        print(f"image: {tag}")
        print(f"  internal: {result.image}")
        print(f"  digest: {result.digest_ref}")
        print(f"  platforms: {', '.join(result.record.platforms)}")
        external = _external_digest(result.manifest)
        if external is not None:
            print(f"  external: {external}")


def _external_digest(result: ImageManifestResult) -> str | None:
    if result.external_digest_ref is None:
        return None
    return result.external_digest_ref


async def _assert_build_runtime(kube: Kube, *, timeout: float) -> None:
    if timeout <= 0:
        msg = "image build runtime readiness timeout must be non-negative"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    registry = await IMAGES.status(kube, timeout=deadline - loop.time())
    buildkit = await BUILDKIT_POOL.status(
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


def _buildkit_readiness_failures(status: BuildKitPoolStatus) -> list[str]:
    failures: list[str] = []
    if not status.daemonset_present:
        failures.append("BuildKit DaemonSet is missing")
    if not status.rollout_ready:
        failures.append("BuildKit DaemonSet rollout is not ready")
    if not status.config_current:
        failures.append("BuildKit DaemonSet has stale registry config")
    if not status.ready_builders:
        failures.append("BuildKit has no ready builder pods")
    if status.missing_platforms:
        platforms = ", ".join(status.missing_platforms)
        failures.append(f"BuildKit has no ready builder for platform(s): {platforms}")
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
