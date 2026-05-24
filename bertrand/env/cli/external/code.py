"""External CLI endpoint for editor-bounded Kubernetes dev sessions."""

from __future__ import annotations

from typing import TYPE_CHECKING

from bertrand.env.cli.external._helper import (
    prune_repository_mounts_quietly,
    resolve_project_worktree,
)
from bertrand.env.cli.external.build import BuildLogFollower, _assert_build_runtime
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
    with await Kube.host(timeout=INFINITY) as kube:
        repo, worktree = await resolve_project_worktree(
            kube,
            target,
            timeout=INFINITY,
        )
        config = await Config.load(worktree, kube=kube, repo=repo, timeout=INFINITY)
        async with config:
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

            command = ["bertrand", "code", "--block"]
            if editor is not None:
                editor = editor.strip()
                if not editor:
                    msg = "editor override must not be empty"
                    raise ValueError(msg)
                command.extend(["--editor", editor])

            session = await create_project_dev_session(
                kube,
                config=config,
                repo_id=config.repo.repo_id,
                image_ref=publication.record.digest_ref,
                session_id=session_id,
                host_id=host_id,
                command=command,
                interactive=False,
                timeout=INFINITY,
            )
            async with CodeOpenBridge(kube, session_id=session_id, host_id=host_id):
                try:
                    terminal = await session.pod.wait_terminal(kube, timeout=INFINITY)
                    if terminal.phase != "Succeeded":
                        log = await terminal.logs(
                            kube,
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
                    await session.delete(kube, timeout=INFINITY)
        await prune_repository_mounts_quietly(kube, timeout=INFINITY)
