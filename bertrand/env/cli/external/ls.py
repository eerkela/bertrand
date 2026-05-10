"""External CLI endpoint for listing Bertrand runtime objects."""

from __future__ import annotations

import time
from typing import TYPE_CHECKING

from bertrand.env.git import BERTRAND_ENV, ENV_ID_ENV, IMAGE_TAG_ENV
from bertrand.env.legacy.environment import Environment
from bertrand.env.legacy.nerdctl import nerdctl

from ._helper import _cli_containers, _cli_images, _parse_output_format

if TYPE_CHECKING:
    from pathlib import Path


async def bertrand_ls(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    deadline: float,
    image: bool,
    output_format: str,
) -> None:
    """Gather status information for containers/images in a Bertrand environment.

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
        If True, list images. Otherwise, list containers.
    output_format : str
        Output format: `id`, `json`, `table`, or `table <template>`.
    """
    if workload is not None:
        msg = "kubernetes workloads are not yet supported"
        raise NotImplementedError(msg)
    format_mode, table_template = _parse_output_format(output_format, allow_id=True)

    async with await Environment.load(worktree, timeout=deadline - time.time()) as env:
        if format_mode == "id":
            if image:
                ids = await _cli_images(env, tag, timeout=deadline - time.time())
            else:
                ids = await _cli_containers(env, tag, timeout=deadline - time.time())
            for id_ in ids:
                print(id_)
            return

        if image:
            cmd = [
                "image",
                "ls",
                "-a",
                "--filter",
                f"label={BERTRAND_ENV}=1",
                "--filter",
                f"label={ENV_ID_ENV}={env.id}",
                "--filter",
                f"label={IMAGE_TAG_ENV}={tag}",
            ]
            if format_mode == "json":
                cmd.append("--no-trunc")
                cmd.append("--format=json")
            else:
                template = (
                    table_template
                    or "{{.Names}}\t{{.CreatedAt}}\t{{.Containers}}\t{{.ReadOnly}}\t"
                    "{{.Size}}\t{{.History}}"
                )
                cmd.append(f"--format=table {template}")
        else:
            cmd = [
                "container",
                "ls",
                "-a",
                "--size",
                "--filter",
                f"label={BERTRAND_ENV}=1",
                "--filter",
                f"label={ENV_ID_ENV}={env.id}",
                "--filter",
                f"label={IMAGE_TAG_ENV}={tag}",
            ]
            if format_mode == "json":
                cmd.append("--no-trunc")
                cmd.append("--format=json")
            else:
                template = (
                    table_template
                    or "{{.Names}}\t{{.CreatedAt}}\t{{.State}}\t{{.Command}}\t"
                    "{{.RunningFor}}\t{{.Status}}\t{{.Restarts}}\t{{.Size}}\t"
                    "{{.Mounts}}\t{{.Networks}}\t{{.Ports}}"
                )
                cmd.append(f"--format=table {template}")

        await nerdctl(cmd, timeout=deadline - time.time())
