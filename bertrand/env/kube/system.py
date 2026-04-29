"""Host-side Kubernetes runtime convergence helpers for Bertrand."""
from __future__ import annotations

from ..run import CommandError, run

# TODO: maybe move this into run/bertrand_git.py if we don't end up adding anything
# else to this module.


async def enable_addon(name: str, *, timeout: float) -> None:
    """Enable a Kubernetes addon by name, if not already enabled.

    Parameters
    ----------
    name : str
        The name of the addon to enable.
    timeout : float
        Maximum runtime command timeout in seconds.  If infinite, wait indefinitely.

    Raises
    ------
    CommandError
        If the addon cannot be enabled.
    """
    try:
        await run(
            ["microk8s", "enable", name],
            capture_output=True,
            timeout=timeout,
        )
    except CommandError as err:
        detail = f"{err.stdout}\n{err.stderr}".strip().lower()
        if "already enabled" not in detail and "alreadyenabled" not in detail:
            raise
