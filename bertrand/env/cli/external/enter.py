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
    async with _project_command_context(target, timeout=INFINITY) as context:
        bertrand = context.config.get(Bertrand)
        if bertrand is None:
            msg = f"missing Bertrand configuration for worktree: {context.worktree}"
            raise OSError(msg)
        shell_name = shell or bertrand.shell
        shell_cmd = SHELLS.get(shell_name)
        if shell_cmd is None:
            msg = f"unsupported shell override: {shell_name!r}"
            raise ValueError(msg)
        dev_shell_cmd = [DEV_SHELL_ENTRYPOINT.as_posix(), *shell_cmd]

        publication = await _publish_project_image(
            context.kube,
            config=context.config,
            repo_id=context.config.repo.repo_id,
            timeout=INFINITY,
        )
        session = await create_project_dev_session(
            context.kube,
            config=context.config,
            repo_id=context.config.repo.repo_id,
            image_ref=publication.record.digest_ref,
            session_id=session_id,
            host_id=host_id,
            command=dev_shell_cmd,
            interactive=True,
            timeout=INFINITY,
        )
        async with CodeOpenBridge(context.kube, session_id=session_id, host_id=host_id):
            try:
                pod = await session.wait_running(context.kube, timeout=INFINITY)
                await _attach_pod(
                    context.kube,
                    pod,
                    primary_container=session.primary_container,
                )
            finally:
                await session.delete(context.kube, timeout=INFINITY)
