"""Internal Kubernetes workload run command implementation."""

from __future__ import annotations

import asyncio
from typing import TYPE_CHECKING

from bertrand.env.cli.external.run import run_configured_project
from bertrand.env.cli.internal._helper import live_project_context

if TYPE_CHECKING:
    import argparse


def bertrand_run(args: argparse.Namespace) -> None:
    """Execute the internal `bertrand run` command.

    Parameters
    ----------
    args : argparse.Namespace
        Parsed internal CLI arguments.

    Raises
    ------
    ValueError
        If the requested foreground/TTY mode is invalid.
    """
    if args.detach and args.tty is True:
        msg = (
            "`bertrand run --detach --tty` is invalid because detached runs do not "
            "attach"
        )
        raise ValueError(msg)

    asyncio.run(_bertrand_run_async(args))


async def _bertrand_run_async(args: argparse.Namespace) -> None:
    async with live_project_context("run") as (kube, config, repo_id):
        await run_configured_project(
            kube,
            config=config,
            repo_id=repo_id,
            detach=args.detach,
            tty=args.tty,
            args=tuple(args.args),
            ensure_build_crds=False,
        )
