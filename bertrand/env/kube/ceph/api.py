"""MicroCeph host command facade for Bertrand's Ceph runtime."""

from __future__ import annotations

import asyncio
import contextlib
import json
import os
import re
import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING

from bertrand.env.kube.api import kubectl
from bertrand.env.run import (
    INFINITY,
    RUN_DIR,
    CommandError,
    CompletedProcess,
    GroupStatus,
    Lock,
    TimeoutExpired,
    can_escalate,
    confirm,
    install_packages,
    run,
    sudo,
    until,
)

if TYPE_CHECKING:
    import subprocess
    from collections.abc import Mapping

MICROCEPH_HOST_ROOT = Path("/host")
MICROCEPH_LOOP_STORAGE_PATH = Path("/var/snap/microceph/common")
MICROCEPH_CHANNEL = "quincy/stable"
MICROCEPH_GROUP = "microceph"
CEPH_LOCK_FILE = RUN_DIR / "microceph.lock"
KUBE_CEPH_LINK_LOCK_FILE = RUN_DIR / "kube-ceph-link.lock"
LOOP_OSD_SIZE_PATTERN = r"^[1-9][0-9]*[MGT]$"
LOOP_OSD_SPEC_PATTERN = r"^loop,[1-9][0-9]*[MGT],[1-9][0-9]*$"


@dataclass(frozen=True)
class CephCapacitySnapshot:
    """Raw Ceph capacity snapshot used by autoscaler reconciliation.

    Attributes
    ----------
    total_bytes : int
        Total raw cluster capacity in bytes.
    used_bytes : int
        Currently used raw cluster capacity in bytes.
    used_ratio : float
        Used/total ratio in [0, 1].
    """

    total_bytes: int
    used_bytes: int
    used_ratio: float


@dataclass(frozen=True)
class NodeCapacitySnapshot:
    """Host-local capacity snapshot for one autoscaler agent node.

    Attributes
    ----------
    free_bytes : int
        Free bytes on the filesystem where MicroCeph loop files are stored.
    path : Path
        Host-visible path that was inspected.
    """

    free_bytes: int
    path: Path


@dataclass(frozen=True)
class LoopOSDSpec:
    """MicroCeph loop-backed OSD allocation request.

    Parameters
    ----------
    size : str
        Per-loop OSD size such as ``"4G"``.
    count : int, default 1
        Number of loop OSDs to allocate.
    """

    size: str
    count: int = 1

    def __post_init__(self) -> None:
        """Normalize and validate the allocation request.

        Raises
        ------
        ValueError
            If the requested loop size or count is invalid.
        """
        object.__setattr__(self, "size", _normalize_size(self.size))
        if (
            not isinstance(self.count, int)
            or isinstance(self.count, bool)
            or self.count <= 0
        ):
            msg = f"invalid MicroCeph loop OSD count: {self.count!r}"
            raise ValueError(msg)

    @property
    def bytes(self) -> int:
        """Return the total requested allocation size in bytes.

        Returns
        -------
        int
            Total requested bytes across all loop OSDs.
        """
        return parse_size_bytes(self.size) * self.count

    def render(self) -> str:
        """Render this allocation as a MicroCeph loop OSD spec string.

        Returns
        -------
        str
            MicroCeph ``disk add`` loop spec string.
        """
        return f"loop,{self.size},{self.count}"


def _container_path(path: Path) -> Path:
    if MICROCEPH_HOST_ROOT.is_dir():
        return MICROCEPH_HOST_ROOT / path.relative_to("/")
    return path


async def ceph(
    argv: list[str],
    *,
    check: bool = True,
    capture_output: bool | None = False,
    stdin: str | None = None,
    timeout: float = INFINITY,
    attempts: int = 1,
    delay: float = 0.1,
    cwd: Path | None = None,
    env: Mapping[str, str] | None = None,
) -> CompletedProcess:
    """Invoke the MicroCeph-backed Ceph CLI.

    Parameters
    ----------
    argv : list[str]
        Ceph arguments without the `microceph.ceph` prefix.
    check : bool, optional
        Whether nonzero command exits raise `CommandError`.
    capture_output : bool | None, optional
        Whether to capture, inherit, or tee subprocess output.
    stdin : str | None, optional
        Optional text to pass to command stdin.
    timeout : float, optional
        Maximum command runtime in seconds.
    attempts : int, optional
        Number of command attempts.
    delay : float, optional
        Delay between attempts in seconds.
    cwd : Path | None, optional
        Optional working directory.
    env : Mapping[str, str] | None, optional
        Optional environment overrides.

    Returns
    -------
    CompletedProcess
        Completed command result.
    """
    return await run(
        ["microceph.ceph", *argv],
        check=check,
        capture_output=capture_output,
        stdin=stdin,
        timeout=timeout,
        attempts=attempts,
        delay=delay,
        cwd=cwd,
        env=env,
    )


async def _snap_ready() -> bool:
    if not shutil.which("snap"):
        return False
    return (
        await run(["snap", "--version"], check=False, capture_output=True)
    ).returncode == 0


async def _install_snap(
    package_manager: str,
    *,
    assume_yes: bool,
    component: str,
) -> None:
    if await _snap_ready():
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
            assume_yes=assume_yes,
            timeout=INFINITY,
        )
    except (CommandError, TimeoutExpired, OSError, ValueError, PermissionError) as err:
        msg = (
            f"Bertrand uses a snap-based runtime path for {component}, but failed to "
            f"install 'snapd' via {package_manager!r}. This host is unsupported for "
            f"the current runtime installation model.\n{err}"
        )
        raise OSError(msg) from err
    if not await _snap_ready():
        msg = "snap is still unavailable after installing snapd"
        raise OSError(msg)


async def _microceph_installed() -> bool:
    if not shutil.which("snap"):
        return False
    return (
        await run(["snap", "list", "microceph"], check=False, capture_output=True)
    ).returncode == 0


async def _microceph_ready() -> bool:
    if not await _microceph_installed() or not shutil.which("microceph"):
        return False
    return (
        await run(["microceph", "--help"], check=False, capture_output=True)
    ).returncode == 0


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

    await _install_snap(package_manager, assume_yes=assume_yes, component="MicroCeph")
    if not await _microceph_ready():
        if not confirm(
            "Bertrand requires MicroCeph as its kubernetes storage backend. Would "
            "you like to install/refresh MicroCeph now at channel "
            f"{MICROCEPH_CHANNEL!r} (requires sudo)?\n[y/N] ",
            assume_yes=assume_yes,
        ):
            msg = "MicroCeph installation declined by user."
            raise PermissionError(msg)
        if os.geteuid() != 0 and not can_escalate():
            msg = "MicroCeph installation requires root privileges; sudo not available."
            raise PermissionError(msg)
        if await _microceph_installed():
            cmd = ["snap", "refresh", "microceph", "--channel", MICROCEPH_CHANNEL]
        else:
            cmd = [
                "snap",
                "install",
                "microceph",
                "--classic",
                "--channel",
                MICROCEPH_CHANNEL,
            ]
        await run(sudo(cmd, non_interactive=assume_yes))
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


async def _microceph_cluster_ready(*, timeout: float) -> bool:
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

    if await _microceph_cluster_ready(timeout=deadline - loop.time()):
        return

    try:
        async with Lock(CEPH_LOCK_FILE, timeout=deadline - loop.time(), mode="local"):
            if await _microceph_cluster_ready(timeout=deadline - loop.time()):
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
            if await _microceph_cluster_ready(timeout=remaining):
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


async def _microk8s_cluster_ready(*, timeout: float) -> bool:
    return (
        await run(
            ["microk8s", "kubectl", "get", "--raw=/readyz"],
            check=False,
            capture_output=True,
            timeout=timeout,
        )
    ).returncode == 0


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
    async with Lock(
        KUBE_CEPH_LINK_LOCK_FILE,
        timeout=deadline - loop.time(),
        mode="local",
    ):
        if not await _microk8s_cluster_ready(timeout=deadline - loop.time()):
            msg = "MicroK8s must be started before linking to MicroCeph."
            raise OSError(msg)
        if not await _microceph_cluster_ready(timeout=deadline - loop.time()):
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


def _normalize_size(size: str) -> str:
    normalized = size.strip().upper()
    if not re.fullmatch(LOOP_OSD_SIZE_PATTERN, normalized):
        msg = f"invalid MicroCeph loop OSD size: {size!r}"
        raise ValueError(msg)
    return normalized


def parse_size_bytes(size: str) -> int:
    """Parse a MicroCeph size string into bytes.

    Parameters
    ----------
    size : str
        Size string using ``M``, ``G``, or ``T`` suffixes.

    Returns
    -------
    int
        Parsed byte count.

    """
    normalized = _normalize_size(size)
    scale = {"M": 2**20, "G": 2**30, "T": 2**40}[normalized[-1]]
    return int(normalized[:-1]) * scale


def parse_loop_osd_spec(value: str) -> LoopOSDSpec:
    """Parse a MicroCeph loop OSD spec string.

    Parameters
    ----------
    value : str
        Loop OSD spec such as ``"loop,4G,1"``.

    Returns
    -------
    LoopOSDSpec
        Parsed allocation request.

    Raises
    ------
    ValueError
        If `value` does not use the supported MicroCeph loop OSD spec format.
    """
    parts = [part.strip() for part in value.strip().split(",")]
    if len(parts) != 3 or parts[0] != "loop":
        msg = f"invalid MicroCeph loop OSD spec: {value!r}"
        raise ValueError(msg)
    try:
        count = int(parts[2])
    except ValueError as err:
        msg = f"invalid MicroCeph loop OSD count: {parts[2]!r}"
        raise ValueError(msg) from err
    return LoopOSDSpec(size=parts[1], count=count)


async def host_command(
    argv: list[str],
    *,
    timeout: float,
) -> subprocess.CompletedProcess[str]:
    """Run a MicroCeph command in host or privileged-container context.

    Parameters
    ----------
    argv : list[str]
        Command vector to execute.
    timeout : float
        Maximum command runtime in seconds.

    Returns
    -------
    subprocess.CompletedProcess[str]
        Completed command result with captured text streams.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    """
    if timeout <= 0:
        msg = "timeout must be non-negative"
        raise TimeoutError(msg)
    if MICROCEPH_HOST_ROOT.is_dir() and not shutil.which(argv[0]):
        cmd = ["chroot", str(MICROCEPH_HOST_ROOT), *argv]
    else:
        cmd = argv
    return await run(
        cmd,
        check=False,
        capture_output=True,
        timeout=timeout,
    )


async def ceph_df(*, timeout: float) -> CephCapacitySnapshot:
    """Inspect raw Ceph cluster capacity using MicroCeph's Ceph CLI.

    Parameters
    ----------
    timeout : float
        Maximum command runtime in seconds.

    Returns
    -------
    CephCapacitySnapshot
        Parsed raw capacity snapshot.

    Raises
    ------
    OSError
        If the MicroCeph command fails or returns malformed capacity data.
    """
    result = await host_command(
        ["microceph.ceph", "df", "--format", "json"], timeout=timeout
    )
    if result.returncode != 0:
        msg = f"failed to inspect Ceph capacity:\n{result}"
        raise OSError(msg)
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as err:
        msg = f"ceph df returned malformed JSON: {err}"
        raise OSError(msg) from err

    stats = payload.get("stats", {}) if isinstance(payload, dict) else {}
    total = int(stats.get("total_bytes") or stats.get("total_space") or 0)
    used = int(
        stats.get("total_used_bytes")
        or stats.get("total_used_raw_bytes")
        or stats.get("total_used")
        or 0
    )
    if total <= 0:
        msg = f"ceph df reported invalid total capacity: {total}"
        raise OSError(msg)
    if used < 0:
        msg = f"ceph df reported invalid used capacity: {used}"
        raise OSError(msg)
    return CephCapacitySnapshot(
        total_bytes=total, used_bytes=used, used_ratio=used / total
    )


def host_free_bytes(path: Path = MICROCEPH_LOOP_STORAGE_PATH) -> NodeCapacitySnapshot:
    """Inspect free host bytes for MicroCeph loop-backed OSD storage.

    Parameters
    ----------
    path : Path, default MICROCEPH_LOOP_STORAGE_PATH
        Host path whose filesystem free space should be inspected.

    Returns
    -------
    NodeCapacitySnapshot
        Free-space snapshot for the requested host path.
    """
    target = _container_path(path)
    usage = shutil.disk_usage(target)
    return NodeCapacitySnapshot(free_bytes=int(usage.free), path=path)


async def add_loop_osd(loop_spec: LoopOSDSpec | str, *, timeout: float) -> None:
    """Add one or more loop-backed OSDs through MicroCeph.

    Parameters
    ----------
    loop_spec : LoopOSDSpec | str
        Loop OSD allocation request.
    timeout : float
        Maximum command runtime in seconds.

    Raises
    ------
    OSError
        If MicroCeph rejects the loop OSD allocation.
    """
    spec = (
        loop_spec
        if isinstance(loop_spec, LoopOSDSpec)
        else parse_loop_osd_spec(loop_spec)
    )
    rendered = spec.render()
    result = await host_command(["microceph", "disk", "add", rendered], timeout=timeout)
    if result.returncode != 0:
        msg = f"microceph disk add failed for {rendered}:\n{result}"
        raise OSError(msg)
