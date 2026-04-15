"""TODO"""
from __future__ import annotations

import asyncio
import os
import shutil
import signal
import sys

from ..run import (
    BERTRAND_ENV,
    BUILDKIT_PID_FILE,
    NERDCTL_BIN,
    STATE_DIR,
    TIMEOUT,
    confirm,
    nerdctl,
    nerdctl_ids,
)


def _pid_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    except OSError:
        return False
    return True


async def bertrand_clean(*, assume_yes: bool) -> None:
    """Clean Bertrand-managed runtime objects and local state on the host.

    Parameters
    ----------
    assume_yes : bool
        Whether to auto-accept prompts during cleanup.

    Raises
    ------
    OSError
        If cleanup is declined by the user, or if cleanup finished with failures.
    """
    if not confirm(
        "This will remove Bertrand-managed containers, images, volumes, and "
        f"networks (label `{BERTRAND_ENV}=1`) and then delete local Bertrand state in "
        f"{STATE_DIR}.  It will not uninstall MicroK8s or revert host system "
        "settings.  Do you want to proceed?\n[y/N] ",
        assume_yes=assume_yes,
    ):
        raise OSError("Cleanup declined by user.")

    # stop managed buildkit first so we don't leave stale daemon state behind
    try:
        raw = BUILDKIT_PID_FILE.read_text(encoding="utf-8").strip()
        if raw:
            pid = int(raw)
            if _pid_alive(pid):
                # try to terminate gracefully
                deadline = asyncio.get_running_loop().time() + TIMEOUT
                os.kill(pid, signal.SIGTERM)
                while _pid_alive(pid) and asyncio.get_running_loop().time() < deadline:
                    await asyncio.sleep(0.1)

                # kill if still alive
                if _pid_alive(pid):
                    os.kill(pid, signal.SIGKILL)

        BUILDKIT_PID_FILE.unlink(missing_ok=True)
    except Exception as err:
        print(
            f"bertrand: failed to stop buildkitd during cleanup: {err}",
            file=sys.stderr
        )

    # remove runtime objects associated with Bertrand metadata labels
    if NERDCTL_BIN.exists():
        chunk_size = 64  # chunks of 64 to avoid arg limits
        try:
            containers = await nerdctl_ids(
                "container",
                {BERTRAND_ENV: "1"}
            )
            for i in range(0, len(containers), chunk_size):
                await nerdctl(
                    ["container", "rm", "-f", "-i", *containers[i:i + chunk_size]],
                    check=False,
                )
        except Exception as err:
            print(f"bertrand: failed to clean containers:\n{err}", file=sys.stderr)

        try:
            images = await nerdctl_ids(
                "image",
                {BERTRAND_ENV: "1"}
            )
            for i in range(0, len(images), chunk_size):
                await nerdctl(
                    ["image", "rm", "-f", "-i", *images[i:i + chunk_size]],
                    check=False,
                )
        except Exception as err:
            print(f"bertrand: failed to clean images:\n{err}", file=sys.stderr)

        try:
            volumes = await nerdctl_ids(
                "volume",
                {BERTRAND_ENV: "1"}
            )
            for i in range(0, len(volumes), chunk_size):
                await nerdctl(
                    ["volume", "rm", "-f", *volumes[i:i + chunk_size]],
                    check=False,
                )
        except Exception as err:
            print(f"bertrand: failed to clean volumes:\n{err}", file=sys.stderr)

        try:
            networks = await nerdctl_ids(
                "network",
                {BERTRAND_ENV: "1"},
            )
            for i in range(0, len(networks), chunk_size):
                await nerdctl(
                    ["network", "rm", "-f", *networks[i:i + chunk_size]],
                    check=False,
                )
        except Exception as err:
            print(f"bertrand: failed to clean networks:\n{err}", file=sys.stderr)

    # delete the state directory to remove pinned binaries
    shutil.rmtree(STATE_DIR)
