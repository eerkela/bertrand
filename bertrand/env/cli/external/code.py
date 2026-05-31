"""External CLI endpoint for editor-bounded Kubernetes dev sessions."""

from __future__ import annotations

from typing import TYPE_CHECKING

from bertrand.env.cli.external._helper import (
    _project_command_context,
)
from bertrand.env.cli.external.build import _publish_project_image
from bertrand.env.git import Deadline
from bertrand.env.kube.dev import (
    code_open_bridge,
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
    deadline: Deadline,
) -> None:
    """Open a host editor against a generated Kubernetes dev-session Pod.

    Parameters
    ----------
    target : Path
        Project repository or worktree path. Repository roots target the worktree
        attached to HEAD.
    editor : str | None
        Optional editor alias override forwarded to internal `bertrand code`.
    deadline : Deadline
        Command execution deadline.

    Raises
    ------
    OSError
        If image build, dev-session creation, or mailbox bridging fails.
    ValueError
        If the editor override is empty.
    """
    session_id = new_session_id()
    host_id = current_host_id()
    async with _project_command_context(target, deadline=deadline) as (
        kube,
        _repo,
        _worktree,
        config,
    ):
        publication = await _publish_project_image(
            kube,
            config=config,
            repo_id=config.repo.id,
            deadline=deadline,
        )

        command = ["bertrand", "code", "--block"]
        if editor is not None:
            editor = editor.strip()
            if not editor:
                msg = "editor override must not be empty"
                raise ValueError(msg)
            command.extend(["--editor", editor])

        pod, primary_container = await create_project_dev_session(
            kube,
            config=config,
            repo_id=config.repo.id,
            image_ref=publication.digest_ref,
            session_id=session_id,
            host_id=host_id,
            command=command,
            interactive=False,
            deadline=deadline,
        )
        async with code_open_bridge(kube, session_id=session_id, host_id=host_id):
            try:
                terminal = await pod.wait_terminal(
                    kube,
                    deadline=deadline,
                )
                if terminal.phase != "Succeeded":
                    log = await terminal.logs(
                        kube,
                        deadline=Deadline(30),
                        container=primary_container,
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
                await pod.delete(
                    kube,
                    deadline=deadline,
                    grace_period_seconds=1,
                )
