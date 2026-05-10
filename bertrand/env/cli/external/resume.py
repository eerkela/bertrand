"""External CLI endpoint for resuming Bertrand containers."""

from __future__ import annotations

import time
from typing import TYPE_CHECKING

from bertrand.env.legacy.environment import Environment
from bertrand.env.legacy.nerdctl import nerdctl

from ._helper import _cli_containers

if TYPE_CHECKING:
    from pathlib import Path


async def bertrand_resume(
    worktree: Path, workload: str | None, tag: str | None, *, deadline: float
) -> None:
    """Resume paused Bertrand containers within an environment.

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

    """
    if workload is not None:
        msg = "kubernetes workloads are not yet supported"
        raise NotImplementedError(msg)

    async with await Environment.load(worktree, timeout=deadline - time.time()) as env:
        ids = await _cli_containers(
            env, tag, status=("paused",), timeout=deadline - time.time()
        )
        if ids:
            await nerdctl(
                ["container", "unpause", *ids], timeout=deadline - time.time()
            )
