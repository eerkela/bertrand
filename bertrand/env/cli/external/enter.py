"""External CLI endpoint for entering Kubernetes dev-session Pods."""

from __future__ import annotations

import sys
from typing import TYPE_CHECKING

from bertrand.env.cli.external._helper import (
    prune_repository_mounts_quietly,
    resolve_project_worktree,
)
from bertrand.env.cli.external.build import BuildLogFollower, _assert_build_runtime
from bertrand.env.cli.external.run import _attach_pod
from bertrand.env.config.bertrand import SHELLS, Bertrand
from bertrand.env.config.core import Config
from bertrand.env.git import INFINITY
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.build.project import project_image_build
from bertrand.env.kube.dev import (
    CodeOpenBridge,
    create_project_dev_session,
    current_host_id,
    new_session_id,
)

if TYPE_CHECKING:
    from pathlib import Path


async def bertrand_enter(
    target: Path,
    *,
    shell: str | None,
) -> None:
    """Open an interactive shell inside a Kubernetes dev-session Pod.

    Parameters
    ----------
    target : Path
        Project repository or worktree path. Repository roots target the worktree
        attached to HEAD.
    shell : str | None
        Optional shell alias override. Must be recognized by Bertrand config.

    Raises
    ------
    OSError
        If stdin/stdout are not TTYs or dev-session orchestration fails.
    ValueError
        If the shell alias is unsupported.
    """
    if not sys.stdin.isatty() or not sys.stdout.isatty():
        msg = "`bertrand enter` requires both stdin and stdout to be a TTY."
        raise OSError(msg)

    session_id = new_session_id()
    host_id = current_host_id()
    with await Kube.host(timeout=INFINITY) as kube:
        repo, worktree = await resolve_project_worktree(
            kube,
            target,
            timeout=INFINITY,
        )
        config = await Config.load(worktree, kube=kube, repo=repo, timeout=INFINITY)
        async with config:
            bertrand = config.get(Bertrand)
            if bertrand is None:
                msg = f"missing Bertrand configuration for worktree: {worktree}"
                raise OSError(msg)
            shell_name = shell or bertrand.shell
            shell_cmd = SHELLS.get(shell_name)
            if shell_cmd is None:
                msg = f"unsupported shell override: {shell_name!r}"
                raise ValueError(msg)

            await _assert_build_runtime(kube, timeout=INFINITY)
            build = project_image_build(config, repo_id=config.repo.repo_id)
            follower = BuildLogFollower(kube)
            try:
                publication = await build.publish(
                    kube,
                    timeout=INFINITY,
                    on_update=follower.update,
                )
            finally:
                await follower.close()

            session = await create_project_dev_session(
                kube,
                config=config,
                repo_id=config.repo.repo_id,
                image_ref=publication.record.digest_ref,
                session_id=session_id,
                host_id=host_id,
                command=shell_cmd,
                interactive=True,
                timeout=INFINITY,
            )
            async with CodeOpenBridge(kube, session_id=session_id, host_id=host_id):
                try:
                    pod = await session.wait_running(kube, timeout=INFINITY)
                    await _attach_pod(
                        kube,
                        pod,
                        primary_container=session.primary_container,
                    )
                finally:
                    await session.delete(kube, timeout=INFINITY)
        await prune_repository_mounts_quietly(kube, timeout=INFINITY)
