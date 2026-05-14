"""External CLI endpoint for starting Bertrand containers."""

from __future__ import annotations

from typing import TYPE_CHECKING

from bertrand.env.config.bertrand import DEFAULT_TAG
from bertrand.env.legacy.environment import Environment
from bertrand.env.legacy.nerdctl import TIMEOUT

if TYPE_CHECKING:
    from collections.abc import Sequence
    from pathlib import Path


async def bertrand_start(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    cmd: Sequence[str],
) -> None:
    """Start Bertrand containers within an environment.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.
    tag : str | None
        Optional tag to target; defaults to the configured default tag.
    cmd : Sequence[str]
        Optional command to override the default container entry point.

    """
    if workload is not None:
        msg = "kubernetes workloads are not yet supported"
        raise NotImplementedError(msg)
    if tag is None:
        tag = DEFAULT_TAG

    async with await Environment.load(worktree, timeout=TIMEOUT) as env:
        image = await env.build(tag, quiet=False)
        container = await image.create(env.config, env.id, cmd, quiet=False)
        await container.start(
            quiet=False,
            timeout=env.lock.timeout,
            attach=False,
            interactive=False,
        )
