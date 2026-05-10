"""MicroCeph host bootstrap helpers for Bertrand's Ceph runtime."""

from __future__ import annotations

import asyncio
import contextlib
import json
import shutil

from bertrand.env.git import (
    CommandError,
    GroupStatus,
    HostLock,
    TimeoutExpired,
    confirm,
    run,
    until,
)
from bertrand.env.host import RUN_DIR
from bertrand.env.host.snap import (
    ensure_snapd,
    install_or_refresh_snap,
    snap_package_ready,
)
from bertrand.env.kube.api.bootstrap import kubectl, microk8s_cluster_ready

MICROCEPH_CHANNEL = "quincy/stable"
MICROCEPH_GROUP = "microceph"
CEPH_LOCK_FILE = RUN_DIR / "microceph.lock"
KUBE_CEPH_LINK_LOCK_FILE = RUN_DIR / "kube-ceph-link.lock"


async def _microceph_ready() -> bool:
    return await snap_package_ready("microceph", executable="microceph")


async def install_microceph(
    *,
    package_manager: str,
    user: str,
    distro_id: str,
    assume_yes: bool,
) -> None:
    """Install or refresh MicroCeph runtime access.

    Parameters
    ----------
    package_manager : str
        Host package manager to use for installing dependencies.
    user : str
        Host username to configure for runtime group access.
    distro_id : str
        Host Linux distribution ID, retained for diagnostics.
    assume_yes : bool
        Whether to automatically answer yes to prompts.

    Raises
    ------
    PermissionError
        If installation requires root privileges and they are unavailable or declined.
    OSError
        If MicroCeph cannot be installed, found, or made ready.
    """
    _ = distro_id
    group = GroupStatus.get(user, MICROCEPH_GROUP)
    if await _microceph_ready():
        await group.activate(assume_yes=assume_yes)
        return

    await ensure_snapd(package_manager, assume_yes=assume_yes, component="MicroCeph")
    if not await _microceph_ready():
        if not confirm(
            "Bertrand requires MicroCeph as its kubernetes storage backend. Would "
            f"you like to install/refresh MicroCeph now at channel "
            f"{MICROCEPH_CHANNEL!r} (requires sudo)?\n[y/N] ",
            assume_yes=assume_yes,
        ):
            msg = "MicroCeph installation declined by user."
            raise PermissionError(msg)
        await install_or_refresh_snap(
            "microceph",
            channel=MICROCEPH_CHANNEL,
            assume_yes=assume_yes,
            component="MicroCeph",
        )
        if not await _microceph_ready():
            msg = (
                "MicroCeph installation completed, but the runtime is still not "
                "available. Check `snap list microceph` and `microceph --help` for "
                "diagnostics."
            )
            raise OSError(msg)

    await group.activate(assume_yes=assume_yes)


async def assert_microceph_installed(*, user: str) -> None:
    """Raise with actionable diagnostics when MicroCeph runtime is unusable.

    Parameters
    ----------
    user : str
        Host username to check for runtime group access.

    Raises
    ------
    OSError
        If MicroCeph is not installed, not usable, or group access is missing.
    """
    if not await _microceph_ready():
        msg = (
            "MicroCeph is installed but not usable after init bootstrap. Run "
            "`snap list microceph` and `microceph --help` for diagnostics."
        )
        raise OSError(msg)
    group = GroupStatus.get(user, MICROCEPH_GROUP)
    if not group.configured:
        msg = (
            f"user {user!r} is not in {MICROCEPH_GROUP!r}. Rerun `bertrand init` "
            "to configure MicroCeph access."
        )
        raise OSError(msg)


async def microceph_cluster_ready(*, timeout: float) -> bool:
    """Return whether the local MicroCeph cluster reports ready.

    Parameters
    ----------
    timeout : float
        Maximum readiness probe runtime in seconds.

    Returns
    -------
    bool
        True when MicroCeph and the Ceph CLI both report usable status.
    """
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    result = await run(
        ["microceph", "status"],
        check=False,
        capture_output=True,
        timeout=deadline - loop.time(),
    )
    if result.returncode != 0 or not shutil.which("microceph.ceph"):
        return False
    return (
        await run(
            ["microceph.ceph", "status", "--format", "json"],
            check=False,
            capture_output=True,
            timeout=deadline - loop.time(),
        )
    ).returncode == 0


async def start_microceph(*, timeout: float) -> None:
    """Ensure that a local MicroCeph cluster is bootstrapped and ready.

    Parameters
    ----------
    timeout : float
        Maximum startup/readiness budget in seconds.

    Raises
    ------
    TimeoutError
        If readiness checks do not succeed before `timeout`.
    OSError
        If MicroCeph is missing or cluster bootstrap fails.
    """
    if timeout <= 0:
        msg = "MicroCeph timeout must be non-negative."
        raise TimeoutError(msg)
    if not shutil.which("microceph"):
        msg = (
            "MicroCeph CLI was not found in PATH. Run `bertrand init` to install "
            "the managed runtime."
        )
        raise OSError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout

    if await microceph_cluster_ready(timeout=deadline - loop.time()):
        return

    try:
        async with HostLock(CEPH_LOCK_FILE, timeout=deadline - loop.time()):
            if await microceph_cluster_ready(timeout=deadline - loop.time()):
                return
            try:
                await run(
                    ["microceph", "cluster", "bootstrap"],
                    capture_output=True,
                    timeout=deadline - loop.time(),
                )
            except CommandError as err:
                out = f"{err.stdout}\n{err.stderr}".strip().lower()
                if not (
                    "already initialized" in out
                    or "already exists" in out
                    or "already part of a cluster" in out
                    or "already bootstrapped" in out
                ):
                    msg = f"failed to bootstrap MicroCeph cluster:\n{err}"
                    raise OSError(msg) from err

        async def ready(remaining: float) -> None:
            if await microceph_cluster_ready(timeout=remaining):
                return
            msg = "MicroCeph is not ready yet"
            raise TimeoutError(msg)

        try:
            await until(
                ready,
                timeout=deadline - loop.time(),
                interval=0.1,
                action="waiting for MicroCeph to become ready",
            )
        except TimeoutError as err:
            msg = (
                f"timed out waiting for MicroCeph to become ready after {timeout} "
                "seconds"
            )
            raise TimeoutError(msg) from err
        else:
            return
    except TimeoutExpired as err:
        msg = f"timed out waiting for MicroCeph to become ready after {timeout} seconds"
        raise TimeoutError(msg) from err
    except CommandError as err:
        msg = (
            "Failed to start MicroCeph. You may need to re-run `bertrand init` to "
            f"ensure proper setup and group membership.\n{err}"
        )
        raise OSError(msg) from err


async def _ceph_csi_storage_classes(*, timeout: float) -> list[str]:
    try:
        result = await kubectl(
            ["get", "storageclass", "-o", "json"],
            capture_output=True,
            timeout=timeout,
        )
    except CommandError as err:
        msg = (
            "failed to query Kubernetes storage classes while linking MicroK8s to "
            f"MicroCeph:\n{err}"
        )
        raise OSError(msg) from err
    try:
        payload = json.loads(result.stdout)
    except (TypeError, ValueError) as err:
        msg = (
            "failed to parse storage class payload while linking MicroK8s to "
            f"MicroCeph: {err}"
        )
        raise OSError(msg) from err
    items = payload.get("items")
    if not isinstance(items, list):
        msg = "storage class payload is malformed: expected top-level 'items' list"
        raise OSError(msg)

    out: list[str] = []
    for item in items:
        if not isinstance(item, dict):
            continue
        metadata = item.get("metadata")
        name = ""
        if isinstance(metadata, dict):
            raw_name = metadata.get("name")
            if isinstance(raw_name, str):
                name = raw_name.strip()
        provisioner = item.get("provisioner")
        if (
            not isinstance(provisioner, str)
            or "csi.ceph.com" not in provisioner.strip().lower()
        ):
            continue
        if name:
            out.append(name)
    return sorted(set(out))


async def link_kube_ceph(*, timeout: float) -> None:
    """Converge MicroK8s rook-ceph integration with the local MicroCeph cluster.

    Parameters
    ----------
    timeout : float
        Maximum linkage convergence budget in seconds.

    Raises
    ------
    TimeoutError
        If linkage does not converge before `timeout`.
    OSError
        If runtimes are unavailable, addon/link commands fail, or storage classes
        do not materialize.
    """
    if timeout <= 0:
        msg = "kube-ceph link timeout must be non-negative."
        raise TimeoutError(msg)
    if not shutil.which("microk8s"):
        msg = "MicroK8s CLI was not found in PATH. Run `bertrand init` first."
        raise OSError(msg)
    if not shutil.which("microceph"):
        msg = "MicroCeph CLI was not found in PATH. Run `bertrand init` first."
        raise OSError(msg)

    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    async with HostLock(
        KUBE_CEPH_LINK_LOCK_FILE,
        timeout=deadline - loop.time(),
    ):
        if not await microk8s_cluster_ready(timeout=deadline - loop.time()):
            msg = "MicroK8s must be started before linking to MicroCeph."
            raise OSError(msg)
        if not await microceph_cluster_ready(timeout=deadline - loop.time()):
            msg = "MicroCeph must be started before linking to MicroK8s."
            raise OSError(msg)
        if await _ceph_csi_storage_classes(timeout=deadline - loop.time()):
            return

        try:
            await run(
                ["microk8s", "enable", "rook-ceph"],
                capture_output=True,
                timeout=deadline - loop.time(),
            )
        except CommandError as err:
            detail = f"{err.stdout}\n{err.stderr}".lower()
            if not (
                "already enabled" in detail
                or "is already enabled" in detail
                or "already exists" in detail
                or "alreadyexist" in detail
            ):
                msg = (
                    "failed to enable MicroK8s rook-ceph addon while linking to "
                    f"MicroCeph:\n{err}"
                )
                raise OSError(msg) from err

        try:
            await run(
                ["microk8s", "connect-external-ceph"],
                capture_output=True,
                timeout=deadline - loop.time(),
            )
        except CommandError as err:
            detail = f"{err.stdout}\n{err.stderr}".lower()
            if not (
                "already connected" in detail
                or "already imported" in detail
                or "already configured" in detail
                or "already exists" in detail
                or "alreadyexist" in detail
            ):
                msg = (
                    "failed to link MicroK8s rook-ceph to the external MicroCeph "
                    f"cluster.\n{err}"
                )
                raise OSError(msg) from err

        async def linked(remaining: float) -> None:
            if await _ceph_csi_storage_classes(timeout=remaining):
                return
            msg = "Ceph CSI storage classes are not available yet"
            raise TimeoutError(msg)

        with contextlib.suppress(TimeoutError):
            await until(
                linked,
                timeout=deadline - loop.time(),
                interval=0.1,
                action="waiting for Ceph CSI storage classes",
            )
            return

    msg = (
        "MicroK8s rook-ceph linkage completed, but no Ceph CSI storage classes were "
        "discovered."
    )
    raise OSError(msg)
