"""TODO"""
from __future__ import annotations

import time
from pathlib import Path

from ..kube import Environment
from ..run import nerdctl


async def bertrand_top(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    deadline: float
) -> None:
    """Display running processes for scoped Bertrand containers.

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
        If runtime top operations fail.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")

    async with await Environment.load(worktree, timeout=deadline - time.time()) as env:
        ids = await _cli_containers(
            env,
            tag,
            timeout=deadline - time.time()
        )
        for id_ in ids:
            await nerdctl(
                ["container", "top", id_],
                timeout=deadline - time.time(),
            )
            print()
