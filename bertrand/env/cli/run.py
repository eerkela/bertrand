"""TODO"""
from __future__ import annotations

from collections.abc import Sequence
from pathlib import Path

from ..config import DEFAULT_TAG
from ..kube import Environment
from ..run import TIMEOUT


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

    Raises
    ------
    ValueError
        If tag or command input is invalid.
    OSError
        If image build or container lifecycle startup fails.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")
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
