"""External CLI endpoint for showing Bertrand container processes."""

from __future__ import annotations

import time
from typing import TYPE_CHECKING

from bertrand.env.legacy.environment import Environment
from bertrand.env.legacy.nerdctl import nerdctl

from ._helper import _cli_containers

if TYPE_CHECKING:
    from pathlib import Path


async def bertrand_top(
    worktree: Path, workload: str | None, tag: str | None, *, deadline: float
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

    """
    if workload is not None:
        msg = "kubernetes workloads are not yet supported"
        raise NotImplementedError(msg)

    async with await Environment.load(worktree, timeout=deadline - time.time()) as env:
        ids = await _cli_containers(env, tag, timeout=deadline - time.time())
        for id_ in ids:
            await nerdctl(
                ["container", "top", id_],
                timeout=deadline - time.time(),
            )
            print()
