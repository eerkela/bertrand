"""TODO"""
from __future__ import annotations

import time
from pathlib import Path

from ..kube import Environment
from ..run import nerdctl


async def bertrand_pause(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    deadline: float
) -> None:
    """Pause running Bertrand containers within an environment.

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

    Raises
    ------
    OSError
        If runtime pause operations fail.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")

    async with await Environment.load(worktree, timeout=deadline - time.time()) as env:
        ids = await _cli_containers(
            env,
            tag,
            status=("running", "restarting"),
            timeout=deadline - time.time()
        )
        if ids:
            await nerdctl(
                ["container", "pause", *ids],
                timeout=deadline - time.time()
            )

