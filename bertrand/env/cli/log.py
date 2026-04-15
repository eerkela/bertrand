"""TODO"""
from __future__ import annotations

import time
from pathlib import Path

from ..kube import Environment
from ..run import nerdctl


async def bertrand_log(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    deadline: float,
    image: bool,
    since: str | None,
    until: str | None,
) -> None:
    """Print logs/history for scoped Bertrand targets.

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
    image : bool
        If True, show image history instead of container logs.
    since : str | None
        Lower bound for log time range (container logs mode only).
    until : str | None
        Upper bound for log time range (container logs mode only).

    Raises
    ------
    ValueError
        If `since`/`until` are used in image-history mode.
    OSError
        If runtime log/history operations fail.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")

    async with await Environment.load(worktree, timeout=deadline - time.time()) as env:
        if image:
            ids = await _cli_images(env, tag, timeout=deadline - time.time())
            cmd = [
                "image",
                "history",
                "--human",
                (
                    "--format=table {{.CreatedAt}}\t{{.CreatedSince}}\t{{.CreatedBy}}\t"
                    "{{.Size}}\t{{.Comment}}"
                ),
            ]
            if since is not None:
                raise ValueError("cannot use 'since' with image logs")
            if until is not None:
                raise ValueError("cannot use 'until' with image logs")
        else:
            ids = await _cli_containers(
                env,
                tag,
                timeout=deadline - time.time()
            )
            cmd = [
                "container",
                "logs",
                "--color",
                "--follow",
                "--names",
                "--timestamps",
            ]
            if since is not None:
                cmd.append("--since")
                cmd.append(since)
            if until is not None:
                cmd.append("--until")
                cmd.append(until)

        for id_ in ids:
            await nerdctl([*cmd, id_], timeout=deadline - time.time())
            print()
