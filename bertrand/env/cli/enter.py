"""TODO"""
from __future__ import annotations

import sys
import time
from pathlib import Path

from ..config import DEFAULT_TAG, SHELLS, Bertrand
from ..kube import Environment
from ..rpc import start_rpc_sidecar, stop_rpc_sidecar
from ..run import NERDCTL_BIN, TIMEOUT, CommandError, nerdctl
from ._helper import _recover_spec


async def bertrand_enter(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    shell: str | None,
) -> None:
    """Replace the current process with an interactive shell inside the specified
    container, starting or rebuilding it as necessary.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.
    tag : str | None
        Optional tag to target; defaults to the configured default tag.
    shell : str | None
        Optional shell override. Must be recognized by `bertrand init`.

    Raises
    ------
    CommandError
        If stdin/stdout are not attached to a TTY.
    ValueError
        If the shell override is invalid.
    OSError
        If image/container/RPC sidecar orchestration fails.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")

    if not sys.stdin.isatty() or not sys.stdout.isatty():
        cmd = ["bertrand", "enter", _recover_spec(worktree, workload, tag)]
        if shell is not None:
            cmd.append(shell)
        raise CommandError(
            returncode=1,
            cmd=cmd,
            output="",
            stderr="'bertrand enter' requires both stdin and stdout to be a TTY."
        )

    async with await Environment.load(worktree, timeout=TIMEOUT) as env:
        bertrand = env.config.get(Bertrand)
        if not bertrand:
            raise OSError(
                f"Bertrand configuration is missing from the worktree config at "
                f"{worktree}.  This should never occur; if you see this message, "
                "try re-running `bertrand init` to regenerate your project "
                "configuration, or report an issue if the problem persists."
            )

        shell_cmd = SHELLS.get(bertrand.shell)
        if shell_cmd is None:
            raise ValueError(f"unrecognized shell: {bertrand.shell}")
        if shell is not None:
            shell_cmd = SHELLS.get(shell)
            if shell_cmd is None:
                raise ValueError(f"unrecognized shell override: {shell}")

        image = await env.build(tag or DEFAULT_TAG, quiet=False)
        container = await image.create(
            env.config,
            env.id,
            shell_cmd,
            quiet=False,
        )
        deadline = time.time() + min(env.lock.timeout, TIMEOUT)

        sidecar = await start_rpc_sidecar(
            container=container,
            container_bin=NERDCTL_BIN,
            deadline=deadline,
            strict=False,
            warn_context=(
                "bertrand: failed to start RPC sidecar; continuing without access to "
                "host RPC features"
            ),
        )

        try:
            await container.start(
                quiet=False,
                timeout=deadline - time.time(),
                attach=True,
                interactive=True,
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
