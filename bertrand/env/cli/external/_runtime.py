"""Small runtime and output helpers for the external CLI."""

from __future__ import annotations

import asyncio
import json
import math
import sys
import time
from datetime import UTC, datetime
from typing import TYPE_CHECKING

from bertrand.env.git import TimeoutExpired

if TYPE_CHECKING:
    from collections.abc import Coroutine, Sequence
    from typing import Any


def emit_json(payload: object) -> None:
    """Print a stable, indented JSON payload.

    Parameters
    ----------
    payload : object
        JSON-serializable CLI payload.
    """
    print(json.dumps(payload, indent=2, sort_keys=True))


def warn(message: str) -> None:
    """Print a Bertrand CLI warning to standard error.

    Parameters
    ----------
    message : str
        Warning text without the CLI prefix.
    """
    print(f"bertrand: warning: {message}", file=sys.stderr)


def validate_timeout(command: str, timeout: float) -> float:
    """Validate a positive command timeout.

    Parameters
    ----------
    command : str
        Command name used in the error message.
    timeout : float
        Timeout value in seconds. Infinite timeouts are allowed.

    Returns
    -------
    float
        The validated timeout.

    Raises
    ------
    OSError
        If the timeout is NaN or non-positive.
    """
    if math.isnan(timeout) or timeout <= 0:
        msg = f"invalid {command} timeout: {timeout} (must be > 0 seconds or inf)"
        raise OSError(msg)
    return timeout


def run_async(
    coro: Coroutine[Any, Any, None],
    *,
    cmd: Sequence[str] | None = None,
    timeout: float = 0.0,
    swallow_keyboard_interrupt: bool = False,
) -> None:
    """Run one async CLI command and optionally wrap timeout failures.

    Parameters
    ----------
    coro : Awaitable[None]
        Command coroutine to run.
    cmd : Sequence[str] | None, default None
        Command vector to attach to ``TimeoutExpired``. If omitted, timeout failures
        are re-raised unchanged.
    timeout : float, default 0.0
        Timeout recorded on ``TimeoutExpired`` when ``cmd`` is provided.
    swallow_keyboard_interrupt : bool, default False
        Whether to treat Ctrl-C as successful cancellation.

    Raises
    ------
    TimeoutExpired
        If the command times out and ``cmd`` is provided.
    TimeoutError
        If the command times out and ``cmd`` is omitted.
    KeyboardInterrupt
        If interrupted and ``swallow_keyboard_interrupt`` is false.
    """
    started = time.time()
    with asyncio.Runner() as runner:
        try:
            runner.run(coro)
        except KeyboardInterrupt:
            if swallow_keyboard_interrupt:
                return
            raise
        except (TimeoutError, TimeoutExpired) as err:
            if cmd is None:
                raise
            start = datetime.fromtimestamp(started, UTC)
            raise TimeoutExpired(
                cmd=list(cmd),
                timeout=timeout,
                output=None,
                stderr=f"started: {start}\nstopped: {datetime.now(UTC)}\n",
            ) from err
