"""The external CLI endpoint for building Bertrand images."""

from __future__ import annotations

import asyncio
import re
import sys
from pathlib import Path
from typing import TYPE_CHECKING

from bertrand.env.config import Bertrand
from bertrand.env.config.core import Config, _check_kube_name
from bertrand.env.config.repository import resolve_repo_id
from bertrand.env.git import INFINITY, GitRepository
from bertrand.env.kube.api import Kube
from bertrand.env.kube.build.daemon import BUILDKIT_POOL, BuildKitPoolStatus
from bertrand.env.kube.build.lifecycle import gc_project_images
from bertrand.env.kube.build.project import project_image_build
from bertrand.env.kube.build.repository import (
    IMAGE_TAG_RE,
    IMAGES,
    ImageRepositoryStatus,
)

if TYPE_CHECKING:
    from bertrand.env.kube.build.manifest import ImageManifestResult
    from bertrand.env.kube.build.project import ProjectImageResult


_GITHUB_HTTPS_REMOTE = re.compile(
    r"^https://github\.com/(?P<owner>[^/\s]+)/(?P<repo>[^/\s]+?)(?:\.git)?/?$"
)
_GITHUB_SSH_REMOTE = re.compile(
    r"^git@github\.com:(?P<owner>[^/\s]+)/(?P<repo>[^/\s]+?)(?:\.git)?$"
)
_GITHUB_SSH_URL_REMOTE = re.compile(
    r"^ssh://git@github\.com/(?P<owner>[^/\s]+)/(?P<repo>[^/\s]+?)(?:\.git)?/?$"
)


async def bertrand_build(
    target: Path,
    *,
    publish: str | None = None,
    auth: str | None = None,
    quiet: bool,
) -> None:
    """Build all configured Bertrand images through the Kubernetes BuildKit pool.

    Parameters
    ----------
    target : Path
        Project repository or worktree path.
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
    if auth is not None:
        auth = auth.strip()
        if not auth:
            msg = "--auth cannot be empty"
            raise ValueError(msg)
        if publish is None:
            msg = "--auth requires --publish"
            raise ValueError(msg)
        auth = _check_kube_name(auth)

    repo, worktree = await _resolve_build_target(target)
    results: list[tuple[str, ProjectImageResult]] = []
    with await Kube.host(timeout=INFINITY) as kube:
        config = await Config.load(worktree, kube=kube, repo=repo, timeout=INFINITY)
        async with config:
            await _assert_build_runtime(kube, timeout=INFINITY)
            publish_repo = await _publish_repository(config.repo, publish)
            repo_id = resolve_repo_id(config.repo)
            tags = _image_tags(config)
            for tag in tags:
                plan = project_image_build(config, tag, repo_id=repo_id)
                external_image = (
                    _external_image(publish_repo, plan.oci_tag)
                    if publish_repo is not None
                    else None
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


async def _resolve_build_target(target: Path) -> tuple[GitRepository, Path]:
    repo, worktree = await GitRepository.resolve(target.expanduser().resolve())
    if not repo:
        msg = f"no initialized Git repository found for build target: {target}"
        raise OSError(msg)
    if worktree != Path():
        return repo, repo.root / worktree
    head = await repo.head_worktree()
    if head is None:
        msg = (
            f"repository HEAD for {repo.root} must be attached to a local worktree; "
            "provide an explicit worktree path or attach HEAD to a branch before "
            "running `bertrand build`."
        )
        raise OSError(msg)
    return repo, head.path


def _image_tags(config: Config) -> tuple[str, ...]:
    bertrand = config.get(Bertrand)
    if bertrand is None:
        msg = f"missing 'bertrand' configuration for environment at {config.root}"
        raise OSError(msg)
    if not bertrand.image:
        msg = f"environment at {config.root} does not define any images"
        raise OSError(msg)
    return tuple(sorted(bertrand.image))


async def _publish_repository(repo: GitRepository, publish: str | None) -> str | None:
    if publish is None:
        return None
    if publish.strip():
        return _normalize_publish_repository(publish)
    return await _infer_ghcr_repository(repo)


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


async def _infer_ghcr_repository(repo: GitRepository) -> str:
    remotes = await _github_remotes(repo)
    origin = remotes.get("origin")
    if origin is not None:
        if len(origin) == 1:
            owner, name = next(iter(origin))
            return f"ghcr.io/{owner}/{name}".lower()
        choices = ", ".join(
            f"github.com/{owner}/{name}" for owner, name in sorted(origin)
        )
        msg = f"Git remote 'origin' has ambiguous GitHub repositories: {choices}"
        raise ValueError(msg)

    choices = {identity for identities in remotes.values() for identity in identities}
    if len(choices) == 1:
        owner, name = next(iter(choices))
        return f"ghcr.io/{owner}/{name}".lower()
    if not choices:
        msg = (
            "--publish could not infer a GHCR repository because this Git repository "
            "has no GitHub remotes; pass --publish ghcr.io/owner/repo explicitly."
        )
        raise ValueError(msg)
    formatted = ", ".join(
        f"github.com/{owner}/{name}" for owner, name in sorted(choices)
    )
    msg = (
        "--publish found multiple GitHub repositories and cannot choose one: "
        f"{formatted}; pass --publish ghcr.io/owner/repo explicitly."
    )
    raise ValueError(msg)


async def _github_remotes(
    repo: GitRepository,
) -> dict[str, frozenset[tuple[str, str]]]:
    result = await repo.run(["remote", "-v"], capture_output=True)
    out: dict[str, set[tuple[str, str]]] = {}
    for raw in result.stdout.splitlines():
        parts = raw.split()
        if len(parts) < 2:
            continue
        name, url = parts[0], parts[1]
        identity = _github_remote_identity(url)
        if identity is not None:
            out.setdefault(name, set()).add(identity)
    return {name: frozenset(identities) for name, identities in out.items()}


def _github_remote_identity(url: str) -> tuple[str, str] | None:
    for pattern in (_GITHUB_HTTPS_REMOTE, _GITHUB_SSH_REMOTE, _GITHUB_SSH_URL_REMOTE):
        match = pattern.fullmatch(url.strip())
        if match is not None:
            owner = match.group("owner")
            repo = match.group("repo")
            if owner and repo:
                return owner, repo
    return None


def _external_image(repo: str, tag: str) -> str:
    if not IMAGE_TAG_RE.fullmatch(tag):
        msg = f"derived image tag is not a valid OCI tag: {tag!r}"
        raise ValueError(msg)
    return f"{repo}:{tag}"


def _print_results(results: list[tuple[str, ProjectImageResult]]) -> None:
    for index, (tag, result) in enumerate(results):
        if index:
            print()
        print(f"image: {_display_image_key(tag)}")
        print(f"  internal: {result.image}")
        print(f"  digest: {result.digest_ref}")
        print(f"  platforms: {', '.join(result.record.platforms)}")
        if result.manifest.channel_digest_refs:
            print("  channels:")
            for name, ref in result.manifest.channel_digest_refs.items():
                print(f"    {name}: {ref}")
        external = _external_digest(result.manifest)
        if external is not None:
            print(f"  external: {external}")
        if result.manifest.external_channel_digest_refs:
            print("  external channels:")
            for name, ref in result.manifest.external_channel_digest_refs.items():
                print(f"    {name}: {ref}")


def _external_digest(result: ImageManifestResult) -> str | None:
    if result.external_digest_ref is None:
        return None
    return result.external_digest_ref


def _display_image_key(tag: str) -> str:
    return '""' if tag == "" else tag


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
