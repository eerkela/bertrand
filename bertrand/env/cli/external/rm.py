"""External CLI endpoint for removing Bertrand runtime objects."""

from __future__ import annotations

import time
from typing import TYPE_CHECKING

from bertrand.env.legacy.environment import Environment
from bertrand.env.legacy.registry import EnvironmentMetadata

if TYPE_CHECKING:
    from pathlib import Path


async def bertrand_rm(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    deadline: float,
    force: bool,
) -> None:
    """Delete Bertrand runtime images and containers.

    This command is scoped to images and containers within a Bertrand environment.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.
    tag : str | None
        Optional member tag to scope the command.
    deadline : float
        Timestamp before which this command should complete, relative to the epoch.
    force : bool
        If True, containers/images are forcefully removed where applicable.

    Notes
    -----
    This command only deletes container-runtime artifacts; it never deletes the
    worktree itself.
    """
    if workload is not None:
        msg = "kubernetes workloads are not yet supported"
        raise NotImplementedError(msg)

    async with await Environment.load(worktree, timeout=deadline - time.time()) as env:
        if tag is None:
            while env.images:
                _, image = env.images.popitem()
                env._json.retired.append(
                    EnvironmentMetadata.RetiredImage(
                        force=force,
                        image=image,
                    )
                )
        else:
            image = env.images.pop(tag)
            if image is not None:
                env._json.retired.append(
                    EnvironmentMetadata.RetiredImage(
                        force=force,
                        image=image,
                    )
                )
