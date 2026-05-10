"""External CLI endpoint for monitoring Bertrand runtime objects."""

from __future__ import annotations

import time
from typing import TYPE_CHECKING

from bertrand.env.legacy.environment import Environment
from bertrand.env.legacy.nerdctl import nerdctl

from ._helper import _cli_containers, _parse_output_format

if TYPE_CHECKING:
    from pathlib import Path


async def bertrand_monitor(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    deadline: float,
    interval: int,
    output_format: str,
) -> None:
    """Gather resource utilization statistics for scoped Bertrand containers.

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
    interval : int
        Poll interval in seconds. Zero performs a single snapshot.
    output_format : str
        Output format: `json`, `table`, or `table <template>`.

    Raises
    ------
    ValueError
        If interval/format inputs are invalid.
    """
    if workload is not None:
        msg = "kubernetes workloads are not yet supported"
        raise NotImplementedError(msg)
    if interval < 0:
        msg = "interval must be non-negative"
        raise ValueError(msg)
    format_mode, table_template = _parse_output_format(output_format, allow_id=False)
    if format_mode == "json" and interval:
        msg = "cannot use 'json' and 'interval' together"
        raise ValueError(msg)

    async with await Environment.load(worktree, timeout=deadline - time.time()) as env:
        ids = await _cli_containers(env, tag, timeout=deadline - time.time())
        if not ids:
            if format_mode == "json":
                print("[]")
            return

        cmd = ["container", "stats"]
        if not interval:
            cmd.append("--no-stream")
        else:
            cmd.append(f"--interval={interval}")

        if format_mode == "json":
            cmd.append("--no-trunc")
            cmd.append("--format=json")
            cmd.extend(ids)
            await nerdctl(cmd, timeout=deadline - time.time())
        else:
            template = (
                table_template
                or "{{.Name}}\t{{.AVGCPU}}\t{{.CPUPerc}}\t{{.PIDs}}\t{{.MemUsage}}\t"
                "{{.NetIO}}\t{{.BlockIO}}"
            )
            cmd.append(f"--format=table {template}")
            cmd.extend(ids)
            await nerdctl(cmd, timeout=deadline - time.time())
