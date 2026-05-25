"""External CLI endpoint for editor-bounded Kubernetes dev sessions."""

from __future__ import annotations

from typing import TYPE_CHECKING

from bertrand.env.cli.external._helper import (
    _project_command_context,
)
from bertrand.env.cli.external.build import _publish_project_image
from bertrand.env.git import INFINITY
from bertrand.env.kube.dev import (
    CodeOpenBridge,
    create_project_dev_session,
    current_host_id,
    new_session_id,
)

if TYPE_CHECKING:
    from pathlib import Path


async def bertrand_code(
    target: Path,
    *,
    editor: str | None,
) -> None:
    """Open a host editor against a generated Kubernetes dev-session Pod.

    Parameters
    ----------
    target : Path
        Project repository or worktree path. Repository roots target the worktree
        attached to HEAD.
    editor : str | None
        Optional editor alias override forwarded to internal ``bertrand code``.

    Raises
    ------
    OSError
        If image build, dev-session creation, or mailbox bridging fails.
    ValueError
        If the editor override is empty.
    """
    session_id = new_session_id()
    host_id = current_host_id()
    async with _project_command_context(target, timeout=INFINITY) as context:
        publication = await _publish_project_image(
            context.kube,
            config=context.config,
            repo_id=context.config.repo.repo_id,
            timeout=INFINITY,
        )

        command = ["bertrand", "code", "--block"]
        if editor is not None:
            editor = editor.strip()
            if not editor:
                msg = "editor override must not be empty"
                raise ValueError(msg)
            command.extend(["--editor", editor])

        session = await create_project_dev_session(
            context.kube,
            config=context.config,
            repo_id=context.config.repo.repo_id,
            image_ref=publication.record.digest_ref,
            session_id=session_id,
            host_id=host_id,
            command=command,
            interactive=False,
            timeout=INFINITY,
        )
        async with CodeOpenBridge(context.kube, session_id=session_id, host_id=host_id):
            try:
                terminal = await session.pod.wait_terminal(
                    context.kube,
                    timeout=INFINITY,
                )
                if terminal.phase != "Succeeded":
                    log = await terminal.logs(
                        context.kube,
                        timeout=30,
                        container=session.primary_container,
                        tail_lines=200,
                    )
                    detail = log.strip()
                    msg = (
                        "`bertrand code` dev session exited with phase "
                        f"{terminal.phase}"
                    )
                    if detail:
                        msg = f"{msg}\n{detail}"
                    raise OSError(msg)
            finally:
                await session.delete(context.kube, timeout=INFINITY)
