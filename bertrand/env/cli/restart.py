"""TODO"""
from __future__ import annotations

import math
import sys
from pathlib import Path

from ..kube import Container, Environment
from ..run import TIMEOUT, nerdctl


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

    Raises
    ------
    OSError
        If rebuild/restart operations fail.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")

    async with await Environment.load(worktree, timeout=TIMEOUT) as env:
        tags: list[str]
        if tag is None:
            tags = list(env.images)
        else:
            tags = [tag]
        for current_tag in tags:
            containers = await Container.inspect(await _cli_containers(
                env,
                current_tag,
                status=("running", "restarting", "paused"),
                timeout=env.lock.timeout
            ))
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
                                "-t", str(int(math.ceil(env.lock.timeout))),
                                container.Id
                            ],
                            timeout=env.lock.timeout
                        )
                    else:
                        await nerdctl(
                            [
                                "container",
                                "stop",
                                "-t", str(int(math.ceil(env.lock.timeout))),
                                container.Id
                            ],
                            timeout=env.lock.timeout
                        )
                        if container.Path:
                            defer.append([container.Path, *container.Args])
                        else:
                            print(
                                "bertrand: could not recover container argv during "
                                f"restart of {_recover_spec(worktree, workload, current_tag)}: "
                                f"{container.Id}",
                                file=sys.stderr
                            )
                except Exception as err:
                    print(
                        f"bertrand: failed to stop container during restart of "
                        f"{_recover_spec(worktree, workload, current_tag)}: {container.Id}\n"
                        f"{err}",
                        file=sys.stderr
                    )

            for cmd in defer:
                container = await image.create(env.config, env.id, cmd, quiet=False)
                await container.start(
                    quiet=False,
                    timeout=env.lock.timeout,
                    attach=False,
                    interactive=False,
                )
