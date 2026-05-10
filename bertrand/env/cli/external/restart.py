"""External CLI endpoint for restarting Bertrand containers."""

from __future__ import annotations

import math
import sys
from typing import TYPE_CHECKING

from bertrand.env.legacy.container import Container
from bertrand.env.legacy.environment import Environment
from bertrand.env.legacy.nerdctl import TIMEOUT, nerdctl

from ._helper import _cli_containers, _recover_spec

if TYPE_CHECKING:
    from pathlib import Path


async def bertrand_restart(
    worktree: Path,
    workload: str | None,
    tag: str | None,
) -> None:
    """Restart running or paused Bertrand containers within an environment.

    If an image or container is out of date, it is rebuilt before restart.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.
    tag : str | None
        Optional member tag to scope the command.

    """
    if workload is not None:
        msg = "kubernetes workloads are not yet supported"
        raise NotImplementedError(msg)

    async with await Environment.load(worktree, timeout=TIMEOUT) as env:
        tags: list[str]
        tags = list(env.images) if tag is None else [tag]
        for current_tag in tags:
            containers = await Container.inspect(
                await _cli_containers(
                    env,
                    current_tag,
                    status=("running", "restarting", "paused"),
                    timeout=env.lock.timeout,
                )
            )
            if not containers:
                continue

            image = await env.build(current_tag, quiet=False)
            defer: list[list[str]] = []
            for container in containers:
                try:
                    if container.Image == image.id:
                        await nerdctl(
                            [
                                "container",
                                "restart",
                                "-t",
                                str(math.ceil(env.lock.timeout)),
                                container.Id,
                            ],
                            timeout=env.lock.timeout,
                        )
                    else:
                        await nerdctl(
                            [
                                "container",
                                "stop",
                                "-t",
                                str(math.ceil(env.lock.timeout)),
                                container.Id,
                            ],
                            timeout=env.lock.timeout,
                        )
                        if container.Path:
                            defer.append([container.Path, *container.Args])
                        else:
                            spec = _recover_spec(worktree, workload, current_tag)
                            print(
                                "bertrand: could not recover container argv during "
                                f"restart of {spec}: {container.Id}",
                                file=sys.stderr,
                            )
                except Exception as err:  # noqa: BLE001
                    spec = _recover_spec(worktree, workload, current_tag)
                    print(
                        f"bertrand: failed to stop container during restart of "
                        f"{spec}: {container.Id}\n{err}",
                        file=sys.stderr,
                    )

            for cmd in defer:
                container = await image.create(env.config, env.id, cmd, quiet=False)
                await container.start(
                    quiet=False,
                    timeout=env.lock.timeout,
                    attach=False,
                    interactive=False,
                )
