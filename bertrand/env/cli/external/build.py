"""The external CLI endpoint for building Bertrand images."""

from __future__ import annotations

import asyncio
import contextlib
import re
import sys
from typing import TYPE_CHECKING

from bertrand.env.cli.external._helper import resolve_project_worktree
from bertrand.env.config.core import Config, _check_kube_name
from bertrand.env.git import BERTRAND_NAMESPACE, INFINITY
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.build.controller import BUILDKIT_BUILD_CONTROLLER
from bertrand.env.kube.build.daemon import BUILDKIT_POOL
from bertrand.env.kube.build.execution import job_logs
from bertrand.env.kube.build.project import project_image_build
from bertrand.env.kube.build.refs import split_tagged_ref
from bertrand.env.kube.build.repository import IMAGES
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.job import Job

if TYPE_CHECKING:
    from pathlib import Path

    from bertrand.env.git import GitRepository
    from bertrand.env.kube.build.lifecycle import ProjectImagePublication
    from bertrand.env.kube.build.request import BuildKitBuildRecord


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
    target: Path,
    *,
    publish: str | None = None,
    auth: str | None = None,
    quiet: bool,
    detach: bool = False,
) -> None:
    """Build the configured Bertrand image through the Kubernetes BuildKit service.

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

    repo, worktree = await resolve_project_worktree(target)
    result: ProjectImagePublication | None = None
    request_name: str | None = None
    with await Kube.host(timeout=INFINITY) as kube:
        config = await Config.load(worktree, kube=kube, repo=repo, timeout=INFINITY)
        async with config:
            await _assert_build_runtime(kube, timeout=INFINITY)
            publish_repo = await _publish_repository(config.repo, publish)
            repo_id = config.repo.repo_id
            build = project_image_build(config, repo_id=repo_id)
            external_image = (
                _external_image(publish_repo, build.identity.image)
                if publish_repo is not None
                else None
            )
            if detach:
                request = await build.submit(
                    kube,
                    timeout=INFINITY,
                    external_image=external_image,
                    auth_id=auth,
                )
                request_name = request.name
            else:
                follower = None if quiet else BuildLogFollower(kube)
                try:
                    on_update = None if follower is None else follower.update
                    result = await build.publish(
                        kube,
                        timeout=INFINITY,
                        external_image=external_image,
                        auth_id=auth,
                        on_update=on_update,
                    )
                finally:
                    if follower is not None:
                        await follower.close()

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


def _print_result(result: ProjectImagePublication) -> None:
    record = result.record
    print(f"image: {record.image}")
    print(f"  digest: {record.digest_ref}")
    print(f"  platforms: {', '.join(record.platforms)}")
    if result.external_digest_ref is not None:
        print(f"  external: {result.external_digest_ref}")


def _print_request(name: str) -> None:
    print(f"build: {name}")


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
        config_hash=await IMAGES.current_buildkit_config_hash(
            kube,
            timeout=deadline - loop.time(),
        ),
    )
    controller = await Deployment.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=BUILDKIT_BUILD_CONTROLLER,
        timeout=deadline - loop.time(),
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


class BuildLogFollower:
    """Best-effort active BuildKit Job log follower for synchronous CLI builds.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context used to read Jobs and pod logs.
    """

    def __init__(self, kube: Kube) -> None:
        self._kube = kube
        self._job_name = ""
        self._task: asyncio.Task[None] | None = None
        self._printed: dict[str, str] = {}
        self._maintenance_notice = ""

    async def update(self, record: BuildKitBuildRecord) -> None:
        """Follow the current active Job from a BuildKit request status update.

        Parameters
        ----------
        record : BuildKitBuildRecord
            Build request status snapshot.
        """
        await self._set_job(record.status.active_job)
        if not record.status.active_job.strip() and record.is_active:
            await self._maybe_print_maintenance()

    async def close(self) -> None:
        """Stop any active log-following task."""
        await self._set_job("")

    async def _set_job(self, name: str) -> None:
        name = name.strip()
        if name == self._job_name:
            return
        if self._task is not None:
            self._task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self._task
        self._job_name = name
        self._task = None
        if name:
            self._task = asyncio.create_task(self._run(name))

    async def _run(self, name: str) -> None:
        while True:
            try:
                job = await Job.get(
                    self._kube,
                    namespace=BERTRAND_NAMESPACE,
                    name=name,
                    timeout=_BUILD_LOG_READ_TIMEOUT_SECONDS,
                )
                if job is not None:
                    logs = await job_logs(
                        self._kube,
                        job,
                        timeout=_BUILD_LOG_READ_TIMEOUT_SECONDS,
                        tail_lines=_BUILD_LOG_TAIL_LINES,
                        failure_label="build Job logs",
                        include_headers=True,
                    )
                    self._print_new_logs(name, logs)
            except (OSError, TimeoutError, ValueError):
                pass
            await asyncio.sleep(_BUILD_LOG_POLL_SECONDS)

    def _print_new_logs(self, name: str, logs: str) -> None:
        logs = logs.strip()
        if not logs:
            return
        previous = self._printed.get(name, "")
        if logs == previous:
            return
        if previous and logs.startswith(previous):
            chunk = logs[len(previous) :].lstrip("\n")
        else:
            chunk = logs
        self._printed[name] = logs
        if not chunk:
            return
        print(chunk, file=sys.stderr, flush=True)

    async def _maybe_print_maintenance(self) -> None:
        try:
            status = await IMAGES.maintenance_status(
                self._kube,
                timeout=_BUILD_LOG_READ_TIMEOUT_SECONDS,
            )
        except (OSError, TimeoutError, ValueError):
            return
        if not status.active:
            self._maintenance_notice = ""
            return
        message = status.message or "image registry maintenance is running"
        if message == self._maintenance_notice:
            return
        self._maintenance_notice = message
        print(message, file=sys.stderr, flush=True)
