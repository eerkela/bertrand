"""TODO"""
from __future__ import annotations

import time
from pathlib import Path

from ..kube import Environment


async def bertrand_rm(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    deadline: float,
    force: bool,
) -> None:
    """Delete Bertrand entities on the system, scoped to images and containers within
    an environment.

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
        raise NotImplementedError("kubernetes workloads are not yet supported")

    async with await Environment.load(worktree, timeout=deadline - time.time()) as env:
        if tag is None:
            while env.images:
                _, image = env.images.popitem()
                env._json.retired.append(EnvironmentMetadata.RetiredImage(
                    force=force,
                    image=image,
                ))
        else:
            image = env.images.pop(tag)
            if image is not None:
                env._json.retired.append(EnvironmentMetadata.RetiredImage(
                    force=force,
                    image=image,
                ))
