"""Internal Kubernetes workload run command implementation."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from bertrand.env.cli.external.run import run_configured_project
from bertrand.env.cli.internal._helper import live_project_context

if TYPE_CHECKING:
    from bertrand.env.git import Deadline


@dataclass(frozen=True, slots=True)
class InternalRunCommand:
    """Internal `bertrand run` command closure."""

    detach: bool
    tty: bool | None
    args: tuple[str, ...]

    async def __call__(self, deadline: Deadline) -> None:
        """Run the command.

        Raises
        ------
        ValueError
            If the requested foreground/TTY mode is invalid.
        """
        if self.detach and self.tty is True:
            msg = (
                "`bertrand run --detach --tty` is invalid because detached runs do not "
                "attach"
            )
            raise ValueError(msg)

        async with live_project_context("run", deadline=deadline) as (
            kube,
            config,
            repo_id,
        ):
            await run_configured_project(
                kube,
                config=config,
                repo_id=repo_id,
                detach=self.detach,
                tty=self.tty,
                args=self.args,
                ensure_build_crds=False,
                deadline=deadline,
            )
