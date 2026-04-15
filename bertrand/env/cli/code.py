"""TODO"""
from __future__ import annotations

import asyncio
import time
from pathlib import Path

from ..config import DEFAULT_TAG
from ..kube import Environment
from ..rpc import start_rpc_sidecar, stop_rpc_sidecar
from ..run import NERDCTL_BIN, TIMEOUT, nerdctl


async def bertrand_code(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    editor: str | None,
) -> None:
    """Launch a host-side editor by running a blocking in-container `bertrand code`
    command in an ephemeral container, with a socket-coupled RPC sidecar.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.
    tag : str | None
        Optional tag to target; defaults to the configured default tag.
    editor : str | None
        Optional editor override alias forwarded to the in-container `bertrand code`
        command.

    Raises
    ------
    ValueError
        If editor input is invalid.
    OSError
        If image/container/RPC sidecar orchestration fails.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")
    if tag is None:
        tag = DEFAULT_TAG
    if editor is not None:
        editor = editor.strip()
        if not editor:
            raise ValueError("editor override must not be empty")

    async with await Environment.load(worktree, timeout=TIMEOUT) as env:
        image = await env.build(tag, quiet=False)
        cmd = ["bertrand", "code", "--block"]
        if editor is not None:
            cmd.extend(["--editor", editor])
        container = await image.create(
            env.config,
            env.id,
            cmd,
            quiet=False,
        )
        deadline = time.time() + min(env.lock.timeout, TIMEOUT)

        sidecar: asyncio.subprocess.Process | None = None
        try:
            sidecar = await start_rpc_sidecar(
                container=container,
                container_bin=NERDCTL_BIN,
                deadline=deadline,
                strict=True,
            )
            await container.start(
                quiet=False,
                timeout=deadline - time.time(),
                attach=False,
                interactive=False,
            )
            wait = await nerdctl(
                ["container", "wait", container.Id],
                capture_output=True,
                timeout=env.lock.timeout,
            )
            exit_code = wait.stdout.strip()
            if exit_code and exit_code != "0":
                raise OSError(
                    f"container exited with non-zero status while running "
                    f"'bertrand code': {exit_code}"
                )
        finally:
            await stop_rpc_sidecar(sidecar)
            await nerdctl(
                [
                    "container",
                    "rm",
                    "-f",
                    "-i",
                    "-v",
                    "--depend",
                    container.Id,
                ],
                check=False,
                capture_output=True,
            )
