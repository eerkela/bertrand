"""MicroCeph bootstrap helpers for Bertrand's shared Ceph runtime.

Bertrand v1 targets the supported default MicroCeph snap.  Existing clusters are
allowed and treated as shared; Bertrand creates only its managed Ceph/Kubernetes
state and leaves destructive repository cleanup to future explicit commands.
"""

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
    """Install or refresh access to the default shared MicroCeph runtime.

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
            "Bertrand uses the default shared MicroCeph snap as its kubernetes "
            "storage backend. Would you like to install/refresh MicroCeph now at "
            f"channel {MICROCEPH_CHANNEL!r} (requires sudo)?\n[y/N] ",
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
                "MicroCeph installation completed, but the shared runtime is still "
                "not available. Check `snap list microceph` and `microceph --help` "
                "for diagnostics."
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
    """Ensure that the shared MicroCeph cluster is bootstrapped and ready.

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
            "or configure the shared runtime."
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


async def microceph_join_token(name: str, *, timeout: float) -> str:
    """Generate one MicroCeph join token from this cluster.

    Parameters
    ----------
    name : str
        Intended MicroCeph member name for the joining host.
    timeout : float
        Maximum command runtime in seconds.

    Returns
    -------
    str
        Sensitive MicroCeph join token.

    Raises
    ------
    OSError
        If MicroCeph is not ready or the token cannot be generated.
    TimeoutError
        If `timeout` is non-positive.
    ValueError
        If `name` is empty.
    """
    if timeout <= 0:
        msg = "MicroCeph cluster add timeout must be non-negative"
        raise TimeoutError(msg)
    name = name.strip()
    if not name:
        msg = "MicroCeph join token requires a non-empty node name"
        raise ValueError(msg)
    if not await microceph_cluster_ready(timeout=timeout):
        msg = "MicroCeph must be running before generating a join token"
        raise OSError(msg)
    result = await run(
        ["microceph", "cluster", "add", name],
        capture_output=True,
        timeout=timeout,
    )
    token = result.stdout.strip()
    if not token:
        msg = "MicroCeph cluster add returned an empty join token"
        raise OSError(msg)
    return token


async def join_microceph_cluster(
    token: str,
    *,
    microceph_ip: str | None = None,
    timeout: float,
) -> None:
    """Join the local shared MicroCeph runtime to an existing cluster.

    Parameters
    ----------
    token : str
        Sensitive MicroCeph join token produced by ``microceph cluster add``.
    microceph_ip : str | None, optional
        Optional bind address passed to ``--microceph-ip``.
    timeout : float
        Maximum join/readiness budget in seconds.

    Raises
    ------
    OSError
        If this host already appears to belong to a MicroCeph cluster or join fails.
    TimeoutError
        If `timeout` is non-positive.
    ValueError
        If `token` is empty.
    """
    if timeout <= 0:
        msg = "MicroCeph join timeout must be non-negative."
        raise TimeoutError(msg)
    token = token.strip()
    if not token:
        msg = "MicroCeph join token cannot be empty"
        raise ValueError(msg)
    if await microceph_cluster_ready(timeout=timeout):
        msg = (
            "local MicroCeph already reports a ready cluster; refusing to join it to "
            "another cluster.  Remove or reset the existing MicroCeph membership "
            "outside Bertrand if this host should join a different cluster."
        )
        raise OSError(msg)

    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    argv = ["microceph", "cluster", "join", token]
    if microceph_ip is not None:
        microceph_ip = microceph_ip.strip()
        if microceph_ip:
            argv.extend(["--microceph-ip", microceph_ip])
    async with HostLock(CEPH_LOCK_FILE, timeout=deadline - loop.time()):
        await run(argv, capture_output=True, timeout=deadline - loop.time())

        async def ready(remaining: float) -> None:
            if await microceph_cluster_ready(timeout=remaining):
                return
            msg = "MicroCeph has not joined the cluster yet"
            raise TimeoutError(msg)

        await until(
            ready,
            timeout=deadline - loop.time(),
            interval=0.5,
            action="waiting for MicroCeph cluster join",
        )


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
    """Converge shared MicroK8s rook-ceph integration with shared MicroCeph.

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
