"""The external CLI endpoint for building Bertrand images."""

from __future__ import annotations

import asyncio
import contextlib
import math
import re
import sys
from typing import TYPE_CHECKING

from bertrand.env.cli.external._helper import (
    _project_command_context,
)
from bertrand.env.config.core import _check_kube_name
from bertrand.env.git import BERTRAND_NAMESPACE, Deadline
from bertrand.env.kube.build.controller import BUILDKIT_BUILD_CONTROLLER
from bertrand.env.kube.build.daemon import buildkit_pool_status
from bertrand.env.kube.build.project import (
    project_image_spec,
)
from bertrand.env.kube.build.refs import split_tagged_ref
from bertrand.env.kube.build.repository import (
    current_buildkit_config_hash,
    image_repository_maintenance_status,
    image_repository_status,
)
from bertrand.env.kube.build.request import (
    BUILDKIT_BUILD_RESOURCE,
    submit_buildkit_build,
    wait_buildkit_build,
)
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.job import Job

if TYPE_CHECKING:
    from pathlib import Path

    from bertrand.env.config.core import Config
    from bertrand.env.git import GitRepository
    from bertrand.env.kube.api.client import Kube
    from bertrand.env.kube.build.request import BuildKitBuildRecord, BuildKitBuildSpec


_GITHUB_HTTPS_REMOTE = re.compile(
    r"^https://github\.com/(?P<owner>[^/\s]+)/(?P<repo>[^/\s]+?)(?:\.git)?/?$"
)
_GITHUB_SSH_REMOTE = re.compile(
    r"^git@github\.com:(?P<owner>[^/\s]+)/(?P<repo>[^/\s]+?)(?:\.git)?$"
)
_GITHUB_SSH_URL_REMOTE = re.compile(
    r"^ssh://git@github\.com/(?P<owner>[^/\s]+)/(?P<repo>[^/\s]+?)(?:\.git)?/?$"
)
_BUILD_LOG_POLL_SECONDS = 2.0
_BUILD_LOG_READ_TIMEOUT_SECONDS = 5.0
_BUILD_LOG_TAIL_LINES = 240


async def bertrand_build(
    *,
    kube: Kube,
    deadline: Deadline,
    path: Path,
    publish: str | None = None,
    auth: str | None = None,
    quiet: bool,
    detach: bool = False,
) -> None:
    """Build the configured Bertrand image through the Kubernetes BuildKit service.

    Parameters
    ----------
    kube : Kube
        Kubernetes client instance.
    deadline : Deadline
        Deadline for the build operation.
    path : Path
        Project repository or worktree path.
    publish : str | None, optional
        Optional external OCI repository root to publish manifests to.
    auth : str | None, optional
        Optional Secret-backed capability ID containing Docker auth JSON for the
        external registry.
    quiet : bool
        Whether to suppress build summary output.
    detach : bool, optional
        Whether to submit durable build requests without waiting for publication.

    Raises
    ------
    RuntimeError
        If detached submission or synchronous publication returns no result.
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

    result: BuildKitBuildRecord | None = None
    request_name: str | None = None
    async with _project_command_context(path, timeout=math.inf) as (
        kube,
        _repo,
        _worktree,
        config,
    ):
        publish_repo = await _publish_repository(config.repo, publish)
        repo_id = config.repo.repo_id
        if detach:
            await _assert_build_runtime(kube, timeout=math.inf)
            spec = project_image_spec(config, repo_id=repo_id)
            external_image = (
                _external_image(publish_repo, spec.image)
                if publish_repo is not None
                else None
            )
            spec = _publication_spec(spec, external_image=external_image, auth_id=auth)
            await BUILDKIT_BUILD_RESOURCE.ensure_crd(kube, timeout=math.inf)
            request = await submit_buildkit_build(
                kube,
                spec=spec,
                timeout=math.inf,
            )
            request_name = request.name
        else:
            result = await _publish_project_image(
                kube,
                config=config,
                repo_id=repo_id,
                timeout=math.inf,
                external_repository=publish_repo,
                auth_id=auth,
                quiet=quiet,
            )

    if not quiet:
        if detach:
            if request_name is None:
                msg = "detached image build did not return a request name"
                raise RuntimeError(msg)
            _print_request(request_name)
        else:
            if result is None:
                msg = "image build completed without a publication result"
                raise RuntimeError(msg)
            _print_result(result)


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


def _external_image(repo: str, image: str) -> str:
    _, tag = split_tagged_ref(image, label="internal project image")
    return f"{repo}:{tag}"


async def _publish_project_image(
    kube: Kube,
    *,
    config: Config,
    repo_id: str,
    timeout: float,
    external_repository: str | None = None,
    auth_id: str | None = None,
    quiet: bool = False,
    ensure_crds: bool = True,
) -> BuildKitBuildRecord:
    deadline = Deadline.from_timeout(
        timeout,
        message="project image publication timeout must be positive",
    )
    await _assert_build_runtime(kube, timeout=deadline.remaining())
    spec = project_image_spec(config, repo_id=repo_id)
    external_image = (
        _external_image(external_repository, spec.image)
        if external_repository is not None
        else None
    )
    spec = _publication_spec(spec, external_image=external_image, auth_id=auth_id)
    job_name = ""
    task: asyncio.Task[None] | None = None
    printed: dict[str, str] = {}
    maintenance_notice = ""

    def print_new_logs(name: str, logs: str) -> None:
        logs = logs.strip()
        if not logs:
            return
        previous = printed.get(name, "")
        if logs == previous:
            return
        if previous and logs.startswith(previous):
            chunk = logs[len(previous) :].lstrip("\n")
        else:
            chunk = logs
        printed[name] = logs
        if chunk:
            print(chunk, file=sys.stderr, flush=True)

    async def follow_build_logs(name: str) -> None:
        while True:
            try:
                job = await Job.get(
                    kube,
                    namespace=BERTRAND_NAMESPACE,
                    name=name,
                    timeout=_BUILD_LOG_READ_TIMEOUT_SECONDS,
                )
                if job is not None:
                    logs = await job.logs(
                        kube,
                        timeout=_BUILD_LOG_READ_TIMEOUT_SECONDS,
                        tail_lines=_BUILD_LOG_TAIL_LINES,
                        failure_label="build Job logs",
                        include_headers=True,
                    )
                    print_new_logs(name, logs)
            except (OSError, TimeoutError, ValueError):
                pass
            await asyncio.sleep(_BUILD_LOG_POLL_SECONDS)

    async def set_job(name: str) -> None:
        nonlocal job_name, task
        name = name.strip()
        if name == job_name:
            return
        if task is not None:
            task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await task
        job_name = name
        task = asyncio.create_task(follow_build_logs(name)) if name else None

    async def maybe_print_maintenance() -> None:
        nonlocal maintenance_notice
        try:
            status = await image_repository_maintenance_status(
                kube,
                timeout=_BUILD_LOG_READ_TIMEOUT_SECONDS,
            )
        except (OSError, TimeoutError, ValueError):
            return
        if not status.active:
            maintenance_notice = ""
            return
        message = status.message or "image registry maintenance is running"
        if message == maintenance_notice:
            return
        maintenance_notice = message
        print(message, file=sys.stderr, flush=True)

    async def update_logs(record: BuildKitBuildRecord) -> None:
        await set_job(record.status.active_job)
        if not record.status.active_job.strip() and record.is_active:
            await maybe_print_maintenance()

    try:
        if ensure_crds:
            await BUILDKIT_BUILD_RESOURCE.ensure_crd(
                kube,
                timeout=deadline.remaining(),
            )
        request = await submit_buildkit_build(
            kube,
            spec=spec,
            timeout=deadline.remaining(),
        )
        result = await wait_buildkit_build(
            kube,
            name=request.name,
            timeout=deadline.remaining(),
            on_update=None if quiet else update_logs,
        )
        return _require_successful_build(result)
    finally:
        await set_job("")


def _publication_spec(
    spec: BuildKitBuildSpec,
    *,
    external_image: str | None,
    auth_id: str | None,
) -> BuildKitBuildSpec:
    return type(spec).model_validate(
        {
            **spec.model_dump(mode="python"),
            "external_image": external_image,
            "auth_id": auth_id,
        }
    )


def _require_successful_build(record: BuildKitBuildRecord) -> BuildKitBuildRecord:
    if record.status.phase == "Succeeded" and record.digest_ref:
        return record
    detail = record.status.message or "BuildKit build did not publish an image"
    if record.status.log_excerpt:
        detail = f"{detail}\n{record.status.log_excerpt}"
    raise OSError(detail)


def _print_result(result: BuildKitBuildRecord) -> None:
    print(f"image: {result.image}")
    print(f"  digest: {result.digest_ref}")
    print(f"  platforms: {', '.join(result.platforms)}")
    if result.status.external_digest_ref:
        print(f"  external: {result.status.external_digest_ref}")


def _print_request(name: str) -> None:
    print(f"build: {name}")


async def _assert_build_runtime(kube: Kube, *, timeout: float) -> None:
    deadline = Deadline.from_timeout(
        timeout,
        message="image build runtime readiness timeout must be positive",
    )
    registry = await image_repository_status(kube, timeout=deadline.remaining())
    buildkit = await buildkit_pool_status(
        kube,
        timeout=deadline.remaining(),
        config_hash=await current_buildkit_config_hash(
            kube,
            timeout=deadline.remaining(),
        ),
    )
    controller = await Deployment.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=BUILDKIT_BUILD_CONTROLLER,
        timeout=deadline.remaining(),
    )
    failures = [*registry.failures, *buildkit.failures]
    if controller is None:
        failures.append("BuildKit build controller Deployment is missing")
    elif not controller.rollout_ready(minimum=1):
        failures.append("BuildKit build controller rollout is not ready")
    if failures:
        detail = "\n".join(f"- {failure}" for failure in failures)
        msg = (
            "Bertrand's image build runtime is not ready. Run `bertrand init` "
            f"to converge the Kubernetes build service.\n{detail}"
        )
        raise OSError(msg)
