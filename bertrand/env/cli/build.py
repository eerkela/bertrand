"""The external CLI endpoint for building Bertrand images."""
from __future__ import annotations

from pathlib import Path

from ..config import DEFAULT_TAG
from ..kube import Environment
from ..run import TIMEOUT


async def bertrand_build(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    quiet: bool,
) -> None:
    """Incrementally build Bertrand images within an environment.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable. If None, then the command
        targets tags in the environment's build matrix.
    tag : str | None
        Optional image tag to build. If omitted, the default tag is built.
    quiet : bool
        Whether to suppress build output from the container runtime.

    Notes
    -----
    This command does not materialize or start any containers; it only builds images,
    which corresponds to Ahead-of-Time (AoT) compilation of the container
    environment.

    Raises
    ------
    ValueError
        If the specified tag is invalid.
    OSError
        If build orchestration fails.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")
    if tag is None:
        tag = DEFAULT_TAG

    async with await Environment.load(worktree, timeout=TIMEOUT) as env:
        await env.build(tag, quiet=quiet)
