"""External CLI endpoint for entering Kubernetes dev-session Pods."""

from __future__ import annotations

import sys
from typing import TYPE_CHECKING

from bertrand.env.cli.external._helper import (
    _project_command_context,
)
from bertrand.env.cli.external.build import _publish_project_image
from bertrand.env.cli.external.run import _attach_pod
from bertrand.env.config.bertrand import SHELLS, Bertrand
from bertrand.env.config.vscode import DEV_SHELL_ENTRYPOINT
from bertrand.env.git import INFINITY
from bertrand.env.kube.dev import (
    code_open_bridge,
    create_project_dev_session,
    current_host_id,
    new_session_id,
    wait_dev_session_running,
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
    async with _project_command_context(target, timeout=INFINITY) as (
        kube,
        _repo,
        worktree,
        config,
    ):
        bertrand = config.get(Bertrand)
        if bertrand is None:
            msg = f"missing Bertrand configuration for worktree: {worktree}"
            raise OSError(msg)
        shell_name = shell or bertrand.shell
        shell_cmd = SHELLS.get(shell_name)
        if shell_cmd is None:
            msg = f"unsupported shell override: {shell_name!r}"
            raise ValueError(msg)
        dev_shell_cmd = [DEV_SHELL_ENTRYPOINT.as_posix(), *shell_cmd]

        publication = await _publish_project_image(
            kube,
            config=config,
            repo_id=config.repo.repo_id,
            timeout=INFINITY,
        )
        pod, primary_container = await create_project_dev_session(
            kube,
            config=config,
            repo_id=config.repo.repo_id,
            image_ref=publication.digest_ref,
            session_id=session_id,
            host_id=host_id,
            command=dev_shell_cmd,
            interactive=True,
            timeout=INFINITY,
        )
        async with code_open_bridge(kube, session_id=session_id, host_id=host_id):
            try:
                running = await wait_dev_session_running(
                    kube,
                    pod,
                    primary_container=primary_container,
                    timeout=INFINITY,
                )
                await _attach_pod(
                    kube,
                    running,
                    primary_container=primary_container,
                )
            finally:
                await pod.delete(kube, timeout=INFINITY, grace_period_seconds=1)
