"""General Bertrand utilities shared between the external and internal CLIs."""
from __future__ import annotations

import json
import math
import subprocess
import sys
import time
from datetime import UTC, datetime
from typing import Protocol

from bertrand.env.git import CommandError, Deadline, TimeoutExpired


def emit_json(payload: object) -> None:
    """Print a stable, indented JSON payload.

    Parameters
    ----------
    payload : object
        JSON-serializable CLI payload.
    """
    print(json.dumps(payload, indent=2, sort_keys=True))


class CLICommand(Protocol):
    """A command closure that can be executed by the `cli()` helper method."""

    async def run(self, deadline: Deadline) -> None:
        """Run the command.

        Parameters
        ----------
        deadline : Deadline
            Command execution deadline derived from the CLI timeout budget.
        """


async def cli(op: CLICommand, *, timeout: float) -> None:
    """Run one async CLI command and optionally wrap timeout failures.

    Parameters
    ----------
    op : CLICommand
        Command closure to run, which must conform to the `CLICommand` protocol and
        capture any additional parameters it needs to run from the CLI argument parser.
    timeout : float
        CLI timeout used to set the `Deadline` for the command.

    Raises
    ------
    CommandError
        If the command raises an unexpected exception, whose text will be reported
        alongside `sys.argv` for better user feedback.
    TimeoutExpired
        If the command times out.  Bertrand will report the timeout error text as well
        as the start and stop times of the invoking command described by `sys.argv`.
    SystemExit
        If the command raises `SystemExit`, which is reported as-is.
    KeyboardInterrupt
        If the command raises `KeyboardInterrupt`, which is reported as-is.
    """
    if math.isnan(timeout) or timeout <= 0:
        msg = f"invalid timeout: {timeout} (must be > 0 seconds or inf)"
        raise CommandError(
            returncode=1,
            cmd=sys.argv,
            output=None,
            stderr=msg,
        )

    started = time.time()
    try:
        await op.run(Deadline(started + timeout))
    except (TimeoutError, TimeoutExpired) as err:
        start = datetime.fromtimestamp(started, UTC)
        stop = datetime.now(UTC)
        raise TimeoutExpired(
            cmd=sys.argv,
            timeout=timeout,
            output=None,
            stderr=f"started at: {start}\nstopped at: {stop}\n",
        ) from err
    except (SystemExit, KeyboardInterrupt, CommandError):
        raise
    except BaseException as err:
        if isinstance(err, subprocess.CalledProcessError):
            raise CommandError(
                returncode=err.returncode,
                cmd=err.cmd,
                output=err.output,
                stderr=err.stderr,
            ) from err
        raise CommandError(
            returncode=1,
            cmd=sys.argv,
            output=None,
            stderr=str(err),
        ) from err
