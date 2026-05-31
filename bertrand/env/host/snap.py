"""Host snap runtime helpers for Bertrand bootstrap code.

Bertrand v1 uses the supported default snap instances as a shared host runtime.
These helpers intentionally do not create parallel snap instances or assume that
Bertrand owns an installed snap package.
"""

from __future__ import annotations

import os
import shutil

from bertrand.env.git import (
    NO_DEADLINE,
    CommandError,
    can_escalate,
    confirm,
    install_packages,
    run,
    sudo,
)

# TODO: this whole module should be deletable after moving to a k3s-based runtime


async def snap_ready() -> bool:
    """Return whether the host snap command is available and usable.

    Returns
    -------
    bool
        True when `snap --version` succeeds.
    """
    if not shutil.which("snap"):
        return False
    return (
        await run(
            ["snap", "--version"],
            check=False,
            capture_output=True,
        )
    ).returncode == 0


async def ensure_snapd(
    package_manager: str,
    *,
    assume_yes: bool,
    component: str,
) -> None:
    """Install snapd when Bertrand needs a snap-backed runtime component.

    Parameters
    ----------
    package_manager : str
        Host package manager used by `install_packages`.
    assume_yes : bool
        Whether prompts should be answered yes automatically.
    component : str
        Human-readable component name for prompts and diagnostics.

    Raises
    ------
    PermissionError
        If installation is declined.
    OSError
        If snapd cannot be installed or is still unavailable afterwards.
    """
    if await snap_ready():
        return
    if not confirm(
        f"Bertrand requires 'snapd' to install {component}. Would you like to "
        f"install it now using {package_manager} (requires sudo)?\n[y/N] ",
        assume_yes=assume_yes,
    ):
        msg = "Installation declined by user."
        raise PermissionError(msg)

    try:
        await install_packages(
            package_manager,
            ["snapd"],
            yes=assume_yes,
            deadline=NO_DEADLINE,
        )
    except (CommandError, OSError, ValueError) as err:
        msg = (
            f"Bertrand uses a snap-based runtime path for {component}, but failed to "
            f"install 'snapd' via {package_manager!r}. This host is unsupported for "
            "the current Bertrand runtime installation model unless snapd can be "
            f"installed and made operational.\n{err}"
        )
        raise OSError(msg) from err
    if not await snap_ready():
        msg = (
            "Bertrand uses a snap-based runtime path, but 'snap' is still unavailable "
            "after installing snapd."
        )
        raise OSError(msg)


async def snap_package_installed(name: str) -> bool:
    """Return whether a snap package is installed.

    Parameters
    ----------
    name : str
        Snap package name.

    Returns
    -------
    bool
        True when `snap list <name>` succeeds.
    """
    if not await snap_ready():
        return False
    return (
        await run(["snap", "list", name], check=False, capture_output=True)
    ).returncode == 0


async def snap_package_ready(
    name: str,
    *,
    executable: str | None = None,
) -> bool:
    """Return whether a snap package and its command-line entry point are usable.

    Parameters
    ----------
    name : str
        Snap package name.
    executable : str | None, optional
        Command to probe with `--help`. Defaults to `name`.

    Returns
    -------
    bool
        True when the snap package is installed and the command probe succeeds.
    """
    command = executable or name
    if not await snap_package_installed(name) or not shutil.which(command):
        return False
    return (
        await run(
            [command, "--help"],
            check=False,
            capture_output=True,
        )
    ).returncode == 0


async def install_or_refresh_snap(
    name: str,
    *,
    channel: str,
    assume_yes: bool,
    classic: bool = False,
    component: str | None = None,
) -> None:
    """Install or refresh one snap package at a desired channel.

    Parameters
    ----------
    name : str
        Snap package name.
    channel : str
        Snap channel to install or refresh.
    assume_yes : bool
        Whether sudo should run non-interactively.
    classic : bool, optional
        Whether install should use classic confinement.
    component : str | None, optional
        Human-readable component name for diagnostics.

    Raises
    ------
    PermissionError
        If installation requires root privileges and sudo is unavailable.
    OSError
        If the snap command fails.
    """
    label = component or name
    if os.geteuid() != 0 and not can_escalate():
        msg = f"{label} installation requires root privileges; sudo not available."
        raise PermissionError(msg)
    if await snap_package_installed(name):
        cmd = ["snap", "refresh", name, "--channel", channel]
    else:
        cmd = ["snap", "install", name]
        if classic:
            cmd.append("--classic")
        cmd.extend(("--channel", channel))
    try:
        await run(sudo(cmd, non_interactive=assume_yes))
    except CommandError as err:
        msg = f"failed to install or refresh {label} snap package:\n{err}"
        raise OSError(msg) from err
