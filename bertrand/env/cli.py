"""CLI runtime bridge for host bootstrap and environment lifecycle commands.

This module exposes Bertrand's external command handlers, including persistent host
bootstrap (`bertrand init` / `bertrand clean`) and runtime build/container operations
that target the local MicroK8s + nerdctl control plane.
"""
from __future__ import annotations

import asyncio
import grp
import hashlib
import json
import math
import os
import platform
import pwd
import re
import shutil
import signal
import sys
import time
import uuid
from collections.abc import Awaitable, Callable, Sequence
from dataclasses import dataclass
from importlib import resources as importlib_resources
from pathlib import Path
from typing import Literal, Self

from pydantic import BaseModel, ConfigDict, PositiveInt

from .config import DEFAULT_TAG, SHELLS, Bertrand, Config, PyProject, RESOURCE_NAMES, Resource
from .config.core import NonEmpty, Trimmed
from .kube.container import Container, start_rpc_sidecar, stop_rpc_sidecar
from .kube.environment import Environment
from .kube.image import Image
from .kube.registry import EnvironmentMetadata
from .rpc import RPC_TIMEOUT
from .run import (
    BERTRAND_ENV,
    BUILDCTL_BIN,
    BUILDKIT_PID_FILE,
    BUILDKITD_BIN,
    MICROK8S_CHANNEL,
    MICROK8S_GROUP,
    NERDCTL_BASE_URL,
    NERDCTL_BIN,
    NERDCTL_CHECKSUM,
    NERDCTL_INSTALL_DIR,
    NERDCTL_VERSION,
    NORMALIZE_ARCH,
    ENV_ID_ENV,
    IMAGE_ID_ENV,
    IMAGE_TAG_ENV,
    STATE_DIR,
    TIMEOUT,
    TOOLS_DIR,
    TOOLS_TMP_DIR,
    CommandError,
    GitRepository,
    Lock,
    User,
    atomic_write_text,
    can_escalate,
    confirm,
    nerdctl,
    nerdctl_ids,
    run,
    sudo,
)

# pylint: disable=unused-argument, missing-function-docstring, missing-return-doc
# pylint: disable=bare-except, broad-exception-caught


##################################
####    PERSISTENT INSTALL    ####
##################################


INIT_LOCK = STATE_DIR / "init.lock"
INIT_STATE_FILE = STATE_DIR / "init.state.json"
INIT_STATE_VERSION: int = 1


type InitStage = Literal[
    "fresh",
    "detect_platform",
    "install_microk8s",
    "add_to_microk8s_group",
    "install_nerdctl",
    "ready",
]


class InitState(BaseModel):
    """Persistent state for host bootstrap progression in `bertrand init`."""
    model_config = ConfigDict(extra="forbid")
    version: PositiveInt
    stage: InitStage = "fresh"
    user: NonEmpty[Trimmed] | None = None
    uid: int | None = None
    gid: int | None = None
    package_manager: NonEmpty[Trimmed] | None = None
    distro_id: NonEmpty[Trimmed] | None = None
    distro_version: NonEmpty[Trimmed] | None = None
    distro_codename: NonEmpty[Trimmed] | None = None

    @classmethod
    def load(cls) -> Self:
        STATE_DIR.mkdir(parents=True, exist_ok=True)
        if not INIT_STATE_FILE.exists():
            self = cls(version=INIT_STATE_VERSION)
            self.dump()
            return self
        try:
            data = json.loads(INIT_STATE_FILE.read_text(encoding="utf-8"))
            if not isinstance(data, dict):
                raise ValueError("init state JSON must be an object")
            self = cls.model_validate(data)
        except Exception:
            self = cls(version=INIT_STATE_VERSION)
            self.dump()
            return self
        if self.version != INIT_STATE_VERSION:
            self = cls(version=INIT_STATE_VERSION)
            self.dump()
        return self

    def dump(self) -> None:
        STATE_DIR.mkdir(parents=True, exist_ok=True)
        atomic_write_text(
            INIT_STATE_FILE,
            json.dumps(self.model_dump(mode="json"), separators=(",", ":")) + "\n",
            encoding="utf-8",
            private=True,
        )


async def _no_op(state: InitState, assume_yes: bool) -> None:
    return


@dataclass(frozen=True)
class _PackageSpec:
    install: list[str]
    refresh: list[str] | None
    yes_install: list[str]
    yes_refresh: list[str]
    noninteractive_env: dict[str, str] | None


_INSTALL_SPECS: dict[str, _PackageSpec] = {
    "apt": _PackageSpec(
        install=["apt-get", "install"],
        refresh=["apt-get", "update"],
        yes_install=["-y"],
        yes_refresh=[],
        noninteractive_env={"DEBIAN_FRONTEND": "noninteractive"},
    ),
    "dnf": _PackageSpec(
        install=["dnf", "install"],
        refresh=["dnf", "makecache"],
        yes_install=["-y"],
        yes_refresh=["-y"],
        noninteractive_env=None,
    ),
    "yum": _PackageSpec(
        install=["yum", "install"],
        refresh=["yum", "makecache"],
        yes_install=["-y"],
        yes_refresh=["-y"],
        noninteractive_env=None,
    ),
    "zypper": _PackageSpec(
        install=["zypper", "install"],
        refresh=["zypper", "refresh"],
        yes_install=["--non-interactive"],
        yes_refresh=["--non-interactive"],
        noninteractive_env=None,
    ),
    "pacman": _PackageSpec(
        install=["pacman", "-S"],
        refresh=["pacman", "-Sy"],
        yes_install=["--noconfirm"],
        yes_refresh=[],
        noninteractive_env=None,
    ),
    "apk": _PackageSpec(
        install=["apk", "add"],
        refresh=["apk", "update"],
        yes_install=["--no-interactive"],
        yes_refresh=["--no-interactive"],
        noninteractive_env=None,
    ),
}


def _resolve_package_spec(state: InitState) -> tuple[str, _PackageSpec]:
    if state.package_manager is None:
        raise ValueError("package manager was not initialized in install state")
    spec = _INSTALL_SPECS.get(state.package_manager)
    if spec is None:
        supported = ", ".join(sorted(_INSTALL_SPECS))
        raise ValueError(
            f"unsupported package manager '{state.package_manager}' (supported: {supported})"
        )
    if not shutil.which(spec.install[0]):
        raise FileNotFoundError(
            f"package manager '{state.package_manager}' not found: {spec.install[0]}"
        )
    if spec.refresh is not None and not shutil.which(spec.refresh[0]):
        raise FileNotFoundError(f"refresh command not found: {spec.refresh[0]}")
    return state.package_manager, spec


async def _install_packages(
    state: InitState,
    *,
    packages: list[str],
    assume_yes: bool,
) -> None:
    if os.name != "posix":
        raise OSError("package manager operations require a POSIX system.")
    manager, spec = _resolve_package_spec(state)
    if os.geteuid() != 0 and not can_escalate():
        raise PermissionError(
            f"package installation using '{manager}' requires root privileges; "
            "sudo not available."
        )

    # set noninteractive env vars if needed
    env: dict[str, str] | None = None
    if assume_yes and spec.noninteractive_env:
        env = os.environ.copy()
        env.update(spec.noninteractive_env)

    # refresh package lists if supported and requested
    if spec.refresh is not None:
        cmd = spec.refresh.copy()
        if assume_yes:
            cmd.extend(spec.yes_refresh)
        await run(sudo(cmd, non_interactive=assume_yes), env=env)

    # install requested packages
    cmd = spec.install.copy()
    if assume_yes:
        cmd.extend(spec.yes_install)
    cmd.extend(packages)
    await run(sudo(cmd, non_interactive=assume_yes), env=env)


def _read_os_release() -> dict[str, str]:
    path = Path("/etc/os-release")
    data: dict[str, str] = {}
    if not path.exists():
        return data
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        data[k.strip()] = v.strip().strip('"').strip("'")
    return data


async def _detect_platform(state: InitState, assume_yes: bool) -> None:
    system = platform.system().lower()
    if system != "linux":
        raise OSError("Unsupported platform for package manager detection")

    # read /etc/os-release for distro info
    os_release = _read_os_release()
    distro_id = (os_release.get("ID") or "").lower() or None
    version_id = os_release.get("VERSION_ID") or None
    codename = os_release.get("UBUNTU_CODENAME") or os_release.get("VERSION_CODENAME")

    # detect package manager
    manager: str | None = None
    if distro_id in {"debian", "ubuntu"} and shutil.which("apt-get"):
        manager = "apt"
    elif shutil.which("dnf"):
        manager = "dnf"
    elif shutil.which("yum"):
        manager = "yum"
    elif shutil.which("zypper"):
        manager = "zypper"
    elif shutil.which("pacman"):
        manager = "pacman"
    elif shutil.which("apk"):
        manager = "apk"
    if manager is None:
        raise OSError("No supported package manager found")

    # populate state
    user = User()
    state.user = user.name
    state.uid = user.uid
    state.gid = user.gid
    state.package_manager = manager
    state.distro_id = distro_id
    state.distro_version = version_id
    state.distro_codename = codename


def _digest_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        while True:
            chunk = f.read(1024 * 1024)  # 1 MiB chunks
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


async def _install_prereq_utils(state: InitState, assume_yes: bool) -> None:
    missing: list[str] = []
    if not shutil.which("tar"):
        missing.append("tar")
    if not shutil.which("curl") and not shutil.which("wget"):
        missing.append("curl")
    if not missing:
        return

    if not confirm(
        "Bertrand requires extra host tools to install pinned nerdctl artifacts "
        f"({', '.join(missing)}).  Install now (requires sudo)?\n[y/N] ",
        assume_yes=assume_yes,
    ):
        raise OSError("Installation declined by user.")
    await _install_packages(state, packages=missing, assume_yes=assume_yes)

    if not shutil.which("tar"):
        raise OSError("Installation completed, but 'tar' is still not available.")
    if not shutil.which("curl") and not shutil.which("wget"):
        raise OSError(
            "Installation completed, but neither 'curl' nor 'wget' is available."
        )


async def _download_file(url: str, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    if shutil.which("curl"):
        await run(
            [
                "curl",
                "-fL",
                "--retry",
                "3",
                "--output",
                str(target),
                url,
            ]
        )
        return
    if shutil.which("wget"):
        await run(
            [
                "wget",
                "--tries=3",
                "--output-document",
                str(target),
                url,
            ]
        )
        return
    raise OSError("No download tool available (expected curl or wget).")


async def _snap_ready() -> bool:
    if not shutil.which("snap"):
        return False
    result = await run(
        ["snap", "--version"],
        check=False,
        capture_output=True,
    )
    return result.returncode == 0


async def _microk8s_installed() -> bool:
    if not await _snap_ready():
        return False
    result = await run(
        ["snap", "list", "microk8s"],
        check=False,
        capture_output=True
    )
    return result.returncode == 0


async def _microk8s_ready() -> bool:
    if not await _microk8s_installed() or not shutil.which("microk8s"):
        return False
    result = await run(
        ["microk8s", "--help"],
        check=False,
        capture_output=True,
    )
    return result.returncode == 0


async def _install_microk8s(state: InitState, assume_yes: bool) -> None:
    if await _microk8s_ready():
        return
    if state.distro_id not in {"ubuntu", "debian"} or state.package_manager != "apt":
        distro = state.distro_id or "unknown"
        manager = state.package_manager or "unknown"
        raise OSError(
            "MicroK8s bootstrap currently supports Ubuntu/Debian hosts using apt "
            f"(detected distro={distro!r}, package_manager={manager!r})."
        )

    # install snapd if needed
    if not await _snap_ready():
        if not confirm(
            "Bertrand requires 'snapd' to install MicroK8s.  Would you like to "
            "install it now using apt (requires sudo)?\n"
            "[y/N] ",
            assume_yes=assume_yes,
        ):
            raise OSError("Installation declined by user.")
        await _install_packages(
            state,
            packages=["snapd"],
            assume_yes=assume_yes,
        )
        if not await _snap_ready():
            raise OSError(
                "Installation completed, but 'snap' is still not found.  Please "
                "investigate the issue and ensure snapd is installed correctly."
            )

    # install/update microK8s
    if not confirm(
        "Bertrand requires MicroK8s as its runtime control plane.  Would you like to "
        f"install/refresh MicroK8s now at channel '{MICROK8S_CHANNEL}' (requires sudo)?\n"
        "[y/N] ",
        assume_yes=assume_yes,
    ):
        raise OSError("Installation declined by user.")
    if os.geteuid() != 0 and not can_escalate():
        raise PermissionError(
            "MicroK8s installation requires root privileges; sudo not available."
        )
    if await _microk8s_installed():
        cmd = ["snap", "refresh", "microk8s", "--channel", MICROK8S_CHANNEL]
    else:
        cmd = [
            "snap",
            "install",
            "microk8s",
            "--classic",
            "--channel",
            MICROK8S_CHANNEL
        ]
    await run(sudo(cmd, non_interactive=assume_yes))

    # confirm success
    if not await _microk8s_ready():
        raise OSError(
            "MicroK8s installation completed, but the runtime is still not available.  "
            "Please check `snap list microk8s` and `microk8s --help` for diagnostics."
        )


def _microk8s_group_status(user: str) -> tuple[bool, bool]:
    try:
        group = grp.getgrnam(MICROK8S_GROUP)
    except KeyError:
        return False, False

    try:
        primary_gid = pwd.getpwnam(user).pw_gid
    except KeyError:
        primary_gid = None

    configured = user in group.gr_mem or primary_gid == group.gr_gid
    active = group.gr_gid in os.getgroups() or os.getegid() == group.gr_gid
    return configured, active


async def _add_to_microk8s_group(state: InitState, assume_yes: bool) -> None:
    if not await _microk8s_installed():
        raise OSError("MicroK8s must be installed before configuring socket access.")
    if state.user is None:
        raise ValueError("init state user is missing; run platform detection first.")

    configured, active = _microk8s_group_status(state.user)
    if configured and active:
        return

    # create group if it doesn't exist
    if not configured:
        if not confirm(
            f"Bertrand needs user '{state.user}' in the '{MICROK8S_GROUP}' "
            "group to access the MicroK8s containerd socket.  Add this membership now "
            "(requires sudo)?\n[y/N] ",
            assume_yes=assume_yes,
        ):
            raise OSError("MicroK8s group membership update declined by user.")
        if os.geteuid() != 0 and not can_escalate():
            raise PermissionError(
                "Updating MicroK8s group membership requires root privileges; "
                "sudo not available."
            )
        await run(
            sudo(
                ["usermod", "-a", "-G", MICROK8S_GROUP, state.user],
                non_interactive=assume_yes,
            )
        )
        configured, active = _microk8s_group_status(state.user)
        if not configured:
            raise OSError(
                f"Failed to add user '{state.user}' to group '{MICROK8S_GROUP}'."
            )

    # if we had to update group membership, then the user needs to log out and back in
    # to finish setup
    if not active:
        print(
            f"bertrand: added {state.user!r} to the {MICROK8S_GROUP!r} group, but "
            f"sudo is still required in this session.  Run `newgrp {MICROK8S_GROUP}` "
            "or log out and back in to pick up the new group privileges.",
            file=sys.stderr
        )


def _managed_toolchain_ready() -> bool:
    required = (
        NERDCTL_BIN,
        BUILDCTL_BIN,
        BUILDKITD_BIN,
    )
    return all(path.exists() for path in required)


async def _install_nerdctl(state: InitState, assume_yes: bool) -> None:
    if _managed_toolchain_ready():
        return
    if state.distro_id not in {"ubuntu", "debian"} or state.package_manager != "apt":
        distro = state.distro_id or "unknown"
        manager = state.package_manager or "unknown"
        raise OSError(
            "Pinned nerdctl bootstrap currently supports Ubuntu/Debian hosts using apt "
            f"(detected distro={distro!r}, package_manager={manager!r})."
        )

    # confirm arch is supported and get checksum
    arch = NORMALIZE_ARCH.get(platform.machine().strip().lower())
    if not arch:
        raise OSError(
            "Unsupported CPU architecture for pinned nerdctl artifact: "
            f"{platform.machine()!r} (supported: {sorted(NORMALIZE_ARCH)})"
        )
    archive_name = f"nerdctl-full-{NERDCTL_VERSION}-linux-{arch}.tar.gz"
    archive_path = TOOLS_TMP_DIR / archive_name
    archive_url = f"{NERDCTL_BASE_URL}/{archive_name}"
    expected_sha = NERDCTL_CHECKSUM[arch]

    # install prereq utils (targ, curl, etc.)
    await _install_prereq_utils(state, assume_yes)

    # download and verify pinned archive
    TOOLS_TMP_DIR.mkdir(parents=True, exist_ok=True)
    needs_download = True
    if archive_path.exists() and _digest_file(archive_path) == expected_sha:
        needs_download = False
    if needs_download:
        await _download_file(archive_url, archive_path)
        actual_sha = _digest_file(archive_path)
        if actual_sha != expected_sha:
            try:
                archive_path.unlink()
            except OSError:
                pass
            raise OSError(
                f"Checksum mismatch for {archive_name}: expected {expected_sha}, "
                f"got {actual_sha}."
            )

    # extract archive to final location
    TOOLS_DIR.mkdir(parents=True, exist_ok=True)
    staged = TOOLS_DIR / f".nerdctl-{uuid.uuid4().hex}.tmp"
    if staged.exists():
        shutil.rmtree(staged, ignore_errors=True)
    staged.mkdir(parents=True, exist_ok=True)
    try:
        await run(["tar", "-xzf", str(archive_path), "-C", str(staged)])
        if not (staged / "bin" / "nerdctl").exists():
            raise OSError(
                "Pinned nerdctl archive extracted successfully, but expected binary "
                f"was not found at {(staged / 'bin' / 'nerdctl')}."
            )
        if NERDCTL_INSTALL_DIR.exists():
            shutil.rmtree(NERDCTL_INSTALL_DIR, ignore_errors=True)
        staged.replace(NERDCTL_INSTALL_DIR)
    finally:
        if staged.exists():
            shutil.rmtree(staged, ignore_errors=True)

    # confirm success
    if not _managed_toolchain_ready():
        raise OSError(
            "Managed nerdctl toolchain installation completed, but required binaries "
            "are still missing."
        )


async def _runtime_ready(state: InitState) -> bool:
    if state.user is None or not await _microk8s_ready():
        return False

    configured, active = _microk8s_group_status(state.user)
    if not configured or not active or not _managed_toolchain_ready():
        return False

    nerdctl_result = await nerdctl(
        ["info"],
        check=False,
        capture_output=True,
    )
    return nerdctl_result.returncode == 0


async def _assert_runtime_ready(state: InitState, assume_yes: bool) -> None:
    if state.user is None:
        raise ValueError("init state user is missing; rerun `bertrand init`.")
    if not await _microk8s_ready():
        raise OSError(
            "MicroK8s is installed but not usable after init bootstrap.  Run "
            "`snap list microk8s` and `microk8s --help` for diagnostics."
        )

    configured, active = _microk8s_group_status(state.user)
    if not configured:
        raise OSError(
            f"user '{state.user}' is not in '{MICROK8S_GROUP}'.  Rerun `bertrand init` "
            "to configure MicroK8s containerd socket access."
        )
    if not active:
        raise OSError(
            f"user '{state.user}' was added to '{MICROK8S_GROUP}', but the current "
            "session has not picked up the new group membership.  Log out and back in, "
            "then rerun `bertrand init`."
        )
    if not _managed_toolchain_ready():
        raise OSError(
            "Managed nerdctl/BuildKit toolchain is incomplete.  Rerun `bertrand init`."
        )

    nerdctl_result = await nerdctl(
        ["info"],
        check=False,
        capture_output=True,
    )
    if nerdctl_result.returncode != 0:
        detail = nerdctl_result.stderr.strip() or nerdctl_result.stdout.strip()
        raise OSError(
            "Managed nerdctl command failed to access the MicroK8s containerd daemon."
            f"{f' Details: {detail}' if detail else ''}"
        )


INIT_STAGES: tuple[tuple[InitStage, Callable[[InitState, bool], Awaitable[None]]], ...] = (
    ("fresh", _no_op),
    ("detect_platform", _detect_platform),
    ("install_microk8s", _install_microk8s),
    ("add_to_microk8s_group", _add_to_microk8s_group),
    ("install_nerdctl", _install_nerdctl),
    ("ready", _assert_runtime_ready),
)


############################
####    PROJECT INIT    ####
############################


MANAGED_HOOKS: tuple[tuple[str, str, bool], ...] = (
    (
        "reference_transaction.py",         # source path relative to `bertrand.env.run`
        "hooks/reference-transaction",      # target git path
        True,                               # executable
    ),
    (
        "bertrand_git.py",
        "hooks/bertrand_git.py",
        False,
    ),
)


async def _init_repository(
    path: Path,
    *,
    resources: set[Resource],
    timeout: float,
) -> tuple[GitRepository, Path]:
    # resolve repository and worktree target
    repo, worktree = await GitRepository.resolve(path)
    path = repo.root / worktree
    new = not repo
    if new:
        initial_branch = worktree.as_posix()
        await repo.init(branch=initial_branch, bare=True)
        await repo.create_worktree(
            initial_branch,
            target=repo.root / worktree,
            create_branch=True
        )

    # reconcile with existing configuration (if any)
    config = await Config.load(  # locate existing in-tree resources
        path,
        repo=repo,
        timeout=timeout
    )
    config.resources.update({r.name: None for r in resources})  # merge any new resources from CLI
    config.init = Config.Init(
        repo=repo,
        worktree=worktree,
    )
    async with config:  # init default values, load overrides, and validate all resources
        await config.sync(tag=None)  # render in-tree resources with validated config

        # make an initial commit if this is a new repository
        if new:
            try:
                await run(["git", "add", "-A"], cwd=path, capture_output=True)
                await run(
                    ["git", "commit", "--quiet", "-m", "Initial commit"],
                    cwd=path,
                    capture_output=True,
                )
            except Exception as err:
                print(
                    f"bertrand: failed to create initial commit in {path}\n{err}",
                    file=sys.stderr
                )
        return repo, worktree


async def _install_git_hooks(repo: GitRepository) -> None:
    # check if repo is not initialized
    if not repo:
        print(f"bertrand: invalid git directory at {repo.git_dir}", file=sys.stderr)
        return

    # load managed hook payloads before install; this preserves fail-fast behavior if
    # packaged hook definitions are malformed.
    for source, destination, executable in MANAGED_HOOKS:
        stage = f"resolve managed hook for '{destination}'"
        marker = f"# bertrand-managed: {source}"
        try:
            # load hook from Bertrand package resources and verify shebang/marker
            expected: list[str] = []
            if executable:
                expected.append("#!/usr/bin/env python3")
            expected.append(marker)
            hook_text = importlib_resources.files("bertrand.env").joinpath(
                "run",
                source,
            ).read_text(encoding="utf-8")
            if hook_text.splitlines()[:len(expected)] != expected:
                raise ValueError(
                    f"packaged {source} must start with:\n{'\n'.join(expected)}"
                )

            # do not clobber non-managed hooks
            stage = f"resolve existing git hook at '{destination}'"
            target = await repo.git_path(destination, cwd=repo.root)
            if target.exists():
                if not target.is_file():
                    raise OSError(f"git hook path is not a file: {target}")
                existing = target.read_text(encoding="utf-8")
                if existing == hook_text:
                    continue
                if existing.splitlines()[:len(expected)] != expected:
                    print(
                        f"existing git hook at {target} is not managed by Bertrand; "
                        f"skipping to avoid clobbering user-managed hook.",
                        file=sys.stderr
                    )
                    continue

            # install hook into git directory
            stage = f"write git hook to {target}"
            atomic_write_text(target, hook_text, encoding="utf-8")
            if executable:
                stage = f"set executable permissions on git hook {target}"
                try:
                    target.chmod(0o755)
                except OSError:
                    pass
        except Exception as err:
            print(
                f"bertrand: failed to {stage} in {repo.root}\n{err}",
                file=sys.stderr
            )


###################
####    CLI    ####
###################


async def bertrand_init(
    path: Path | None,
    *,
    enable: list[str],
    yes: bool,
    timeout: float = TIMEOUT,
) -> None:
    """Initialize host prerequisites and optionally bootstrap an environment root.

    Parameters
    ----------
    path : Path | None
        Optional project/environment root path.  If None, only host bootstrap stages
        are run.  Otherwise, the specified path is resolved, normalizing symlinks and
        ensuring an absolute path, and then its ancestors are traversed to find a valid
        Git repository.  If no repository is found, then a new bare repository will be
        initialized at the indicated path, and an isolated worktree will be created to
        hold its default branch.
    enable : list[str]
        List of resources to enable at the resolved worktree.  Each component is a
        comma-separated list of resource names or aliases, which are resolved to their
        corresponding, unique `Resource` implementations.
    yes : bool
        Whether to auto-accept prompts during host bootstrap stages.
    timeout : float
        Timeout in seconds to wait when acquiring locks for the initialization target.
        This applies to both the init lock for the host bootstrap stages and the
        environment lock for rendering worktree resources.  It does not restrict the
        runtime of either of these stages, only the time spent waiting to start them.

    Raises
    ------
    OSError
        If Git is not found, or the project root repository is invalid.
    ValueError
        If any resource names in `enable` are invalid.
    """
    if path is None and enable:
        raise OSError(
            "Cannot enable resources without a worktree.  Please specify a path to "
            "initialize the project repository and enable resources within it."
        )

    # install runtime control plane if needed
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    async with Lock(INIT_LOCK, timeout=timeout, mode="local"):
        state = InitState.load()
        index = next(
            (i for i, (stage, _) in enumerate(INIT_STAGES) if stage == state.stage),
            0
        )
        if index == len(INIT_STAGES) - 1 and not await _runtime_ready(state):
            index = 0
            state = InitState(version=INIT_STATE_VERSION)
            state.dump()

        # run any unfinished stages
        for stage, step in INIT_STAGES[index:]:
            await step(state, yes)
            state.stage = stage
            state.dump()

    # if no project root is provided, then we're done
    if path is None:
        return
    if not shutil.which("git"):
        raise OSError(
            "Bertrand requires 'git' to initialize a project repository, but it was "
            "not found in PATH."
        )
    path = path.expanduser().resolve()

    # identify the resources to enable at the worktree path
    resources: set[Resource] = {RESOURCE_NAMES["bertrand"]}
    for spec in enable:
        for component in spec.split(","):
            r = RESOURCE_NAMES.get(component.strip())
            if r is None:
                raise ValueError(
                    f"unknown resource '{component}' - Options are:\n"
                    f"{'\n'.join(f'    {name}' for name in sorted(RESOURCE_NAMES))}"
                )
            resources.add(r)

    # initialize git repository if needed, then install/update git hooks within it
    repo, _ = await _init_repository(path, resources=resources, timeout=timeout)
    await _install_git_hooks(repo)


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


def _normalize_version(value: str) -> str:
    """Normalize a semantic version token, removing an optional `v` prefix."""
    out = value.strip()
    if not out:
        raise ValueError("version cannot be empty")
    if out.startswith("v") and len(out) > 1:
        out = out[1:]
    if not out:
        raise ValueError("version cannot be empty")
    return out


def _normalize_arch(value: str) -> str:
    """Normalize architecture aliases to Bertrand's canonical architecture labels."""
    arch = value.strip().lower()
    if not arch:
        raise ValueError("architecture cannot be empty")
    return NORMALIZE_ARCH.get(
        arch,
        re.sub(r"[^a-z0-9._-]+", "-", arch).strip("-")
    )


def _parse_manifest_arches(value: str | None) -> list[str]:
    """Parse and normalize comma-separated manifest architecture inputs."""
    if value is None:
        raise ValueError("--manifest requires --manifest-arches")
    raw = value.strip()
    if not raw:
        raise ValueError("--manifest-arches cannot be empty")
    out: list[str] = []
    seen: set[str] = set()
    for token in raw.split(","):
        arch = _normalize_arch(token)
        if not arch:
            raise ValueError(f"invalid architecture in --manifest-arches: {token!r}")
        if arch in seen:
            continue
        seen.add(arch)
        out.append(arch)
    if not out:
        raise ValueError("--manifest-arches must include at least one architecture")
    return out


async def _cli_containers(
    env: Environment,
    tag: str | None,
    *,
    status: tuple[str, ...] = ("created", "paused", "restarting", "running"),
    timeout: float,
) -> list[str]:
    """Resolve container IDs for an environment/tag scope and status filter."""
    if tag is None:
        labels = {ENV_ID_ENV: env.id}
    elif tag not in env.images:
        raise KeyError(f"no image found for tag: '{tag}'")
    else:
        labels = {ENV_ID_ENV: env.id, IMAGE_TAG_ENV: tag}
    return await nerdctl_ids(
        "container",
        labels=labels,
        status=status,
        timeout=timeout
    )


async def _cli_images(
    env: Environment,
    tag: str | None,
    *,
    timeout: float,
) -> list[str]:
    """Resolve image IDs for an environment/tag scope."""
    if tag is None:
        labels = {ENV_ID_ENV: env.id}
    else:
        labels = {ENV_ID_ENV: env.id, IMAGE_TAG_ENV: tag}
    return await nerdctl_ids("image", labels=labels, timeout=timeout)


def _recover_spec(worktree: Path, workload: str | None, tag: str | None) -> str:
    """Render a compact CLI target spec for diagnostics."""
    spec = str(worktree)
    if workload:
        spec += f"@{workload}"
    if tag:
        spec += f":{tag}"
    return spec


def _parse_output_format(value: str, *, allow_id: bool) -> tuple[str, str | None]:
    """Parse `bertrand ls/monitor` format strings into mode + template."""
    raw = value.strip()
    if not raw:
        raise ValueError("format must not be empty")

    mode, _, tail = raw.partition(" ")
    mode = mode.strip().lower()
    template = tail.strip() or None
    if mode == "table":
        return mode, template
    if template is not None:
        raise ValueError(
            "only table format accepts a template (expected: 'table' or "
            "'table <template>')"
        )
    if mode == "json":
        return mode, None
    if mode == "id":
        if not allow_id:
            raise ValueError("format 'id' is only supported for the 'ls' command")
        return mode, None

    if allow_id:
        expected = "id, json, table, or table <template>"
    else:
        expected = "json, table, or table <template>"
    raise ValueError(f"invalid format: {raw!r} (expected {expected})")


async def bertrand_build(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    quiet: bool,
) -> None:
    """Incrementally build Bertrand images within an environment.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable. If None, then the command
        targets tags in the environment's build matrix.
    tag : str | None
        Optional image tag to build. If omitted, the default tag is built.
    quiet : bool
        Whether to suppress build output from the container runtime.

    Notes
    -----
    This command does not materialize or start any containers; it only builds images,
    which corresponds to Ahead-of-Time (AoT) compilation of the container
    environment.

    Raises
    ------
    ValueError
        If the specified tag is invalid.
    OSError
        If build orchestration fails.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")
    if tag is None:
        tag = DEFAULT_TAG

    async with await Environment.load(worktree, timeout=TIMEOUT) as env:
        await env.build(tag, quiet=quiet)


async def bertrand_publish(
    worktree: Path,
    *,
    repo: str,
    version: str | None,
    manifest: bool,
    manifest_arches: str | None,
) -> str | None:
    """Build and publish Bertrand images for all declared tags in an environment.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    repo : str
        Remote OCI repository where tags/manifests should be published.
    version : str | None
        Optional release version to enforce. Accepts `X.Y.Z` or `vX.Y.Z`.
    manifest : bool
        If True, assemble and publish multi-arch manifests only.
    manifest_arches : str | None
        Comma-separated architectures for manifest assembly. Required when
        `manifest=True`.

    Returns
    -------
    str | None
        Normalized host architecture in build mode (`manifest=False`), otherwise
        None.

    Raises
    ------
    ValueError
        If repository/version/manifest architecture inputs are invalid.
    OSError
        If publish prerequisites fail or runtime publish operations fail.
    """
    repo = repo.strip().lower()
    if not repo:
        raise ValueError("OCI repository must be non-empty when provided")

    async with await Environment.load(worktree, timeout=TIMEOUT) as env:
        bertrand = env.config.get(Bertrand)
        python = env.config.get(PyProject)
        if python is None:
            raise OSError("could not determine project version for publish")
        if bertrand is None:
            raise OSError("could not determine configured tags for publish")
        if not bertrand.build:
            raise OSError("publish requires at least one configured tag")

        project_version = _normalize_version(python.project.version)
        publish_version = project_version
        if version is not None:
            publish_version = _normalize_version(version)
            if publish_version != project_version:
                raise OSError(
                    f"publish version '{version}' does not match project version "
                    f"'{project_version}'"
                )

        if not manifest:
            arch = _normalize_arch((await nerdctl(
                ["info", "--format", "{{.Host.Arch}}"],
                capture_output=True,
            )).stdout)
            if not arch:
                raise OSError(
                    "could not determine host architecture from `nerdctl info` output"
                )

            built: dict[str, Image] = {}
            for current_tag in bertrand.build:
                try:
                    built[current_tag] = await env.build(current_tag, quiet=False)
                except Exception as err:
                    raise OSError(
                        f"failed to build tag '{current_tag}' for publish"
                    ) from err

            for current_tag in bertrand.build:
                suffix = "" if current_tag == DEFAULT_TAG else f"-{current_tag}"
                image = built[current_tag]
                ref = f"{repo}:{publish_version}{suffix}-{arch}"
                await nerdctl(["tag", image.id, ref], capture_output=True)
                await nerdctl(["push", ref], attempts=3, capture_output=True)
            return arch

        arches = _parse_manifest_arches(manifest_arches)
        for current_tag in bertrand.build:
            suffix = "" if current_tag == DEFAULT_TAG else f"-{current_tag}"
            manifest_ref = f"{repo}:{publish_version}{suffix}"
            source_refs = [f"{manifest_ref}-{arch}" for arch in arches]
            for ref in source_refs:
                await nerdctl(
                    ["manifest", "inspect", f"docker://{ref}"],
                    attempts=3,
                    capture_output=True,
                )
            try:
                await nerdctl(
                    ["manifest", "rm", manifest_ref],
                    check=False,
                    capture_output=True,
                )
                await nerdctl(
                    ["manifest", "create", manifest_ref],
                    capture_output=True
                )
                for ref in source_refs:
                    await nerdctl(
                        ["manifest", "add", manifest_ref, f"docker://{ref}"],
                        attempts=3,
                        capture_output=True
                    )
                await nerdctl(
                    ["manifest", "push", "--all", manifest_ref, f"docker://{manifest_ref}"],
                    attempts=3,
                    capture_output=True
                )
            finally:
                await nerdctl(
                    ["manifest", "rm", manifest_ref],
                    check=False,
                    capture_output=True,
                )
        return None


async def bertrand_start(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    cmd: Sequence[str],
) -> None:
    """Start Bertrand containers within an environment.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.
    tag : str | None
        Optional tag to target; defaults to the configured default tag.
    cmd : Sequence[str]
        Optional command to override the default container entry point.

    Raises
    ------
    ValueError
        If tag or command input is invalid.
    OSError
        If image build or container lifecycle startup fails.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")
    if tag is None:
        tag = DEFAULT_TAG

    async with await Environment.load(worktree, timeout=TIMEOUT) as env:
        image = await env.build(tag, quiet=False)
        container = await image.create(env.config, env.id, cmd, quiet=False)
        await container.start(
            quiet=False,
            timeout=env.lock.timeout,
            attach=False,
            interactive=False,
        )


async def bertrand_code(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    editor: str | None,
) -> None:
    """Launch a host-side editor by running a blocking in-container `bertrand code`
    command in an ephemeral container, with a socket-coupled RPC sidecar.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.
    tag : str | None
        Optional tag to target; defaults to the configured default tag.
    editor : str | None
        Optional editor override alias forwarded to the in-container `bertrand code`
        command.

    Raises
    ------
    ValueError
        If editor input is invalid.
    OSError
        If image/container/RPC sidecar orchestration fails.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")
    if tag is None:
        tag = DEFAULT_TAG
    if editor is not None:
        editor = editor.strip()
        if not editor:
            raise ValueError("editor override must not be empty")

    async with await Environment.load(worktree, timeout=TIMEOUT) as env:
        image = await env.build(tag, quiet=False)
        cmd = ["bertrand", "code", "--block"]
        if editor is not None:
            cmd.extend(["--editor", editor])
        container = await image.create(
            env.config,
            env.id,
            cmd,
            quiet=False,
        )
        deadline = time.monotonic() + min(env.lock.timeout, RPC_TIMEOUT)

        sidecar: asyncio.subprocess.Process | None = None
        try:
            sidecar = await start_rpc_sidecar(
                container=container,
                container_bin=NERDCTL_BIN,
                deadline=deadline,
                strict=True,
            )
            await container.start(
                quiet=False,
                timeout=deadline - time.monotonic(),
                attach=False,
                interactive=False,
            )
            wait = await nerdctl(
                ["container", "wait", container.Id],
                capture_output=True,
                timeout=env.lock.timeout,
            )
            exit_code = wait.stdout.strip()
            if exit_code and exit_code != "0":
                raise OSError(
                    f"container exited with non-zero status while running "
                    f"'bertrand code': {exit_code}"
                )
        finally:
            await stop_rpc_sidecar(sidecar)
            await nerdctl(
                [
                    "container",
                    "rm",
                    "-f",
                    "-i",
                    "-v",
                    "--depend",
                    container.Id,
                ],
                check=False,
                capture_output=True,
            )


async def bertrand_enter(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    shell: str | None,
) -> None:
    """Replace the current process with an interactive shell inside the specified
    container, starting or rebuilding it as necessary.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.
    tag : str | None
        Optional tag to target; defaults to the configured default tag.
    shell : str | None
        Optional shell override. Must be recognized by `bertrand init`.

    Raises
    ------
    CommandError
        If stdin/stdout are not attached to a TTY.
    ValueError
        If the shell override is invalid.
    OSError
        If image/container/RPC sidecar orchestration fails.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")

    if not sys.stdin.isatty() or not sys.stdout.isatty():
        cmd = ["bertrand", "enter", _recover_spec(worktree, workload, tag)]
        if shell is not None:
            cmd.append(shell)
        raise CommandError(
            returncode=1,
            cmd=cmd,
            output="",
            stderr="'bertrand enter' requires both stdin and stdout to be a TTY."
        )

    async with await Environment.load(worktree, timeout=TIMEOUT) as env:
        bertrand = env.config.get(Bertrand)
        if not bertrand:
            raise OSError(
                f"Bertrand configuration is missing from the worktree config at "
                f"{worktree}.  This should never occur; if you see this message, "
                "try re-running `bertrand init` to regenerate your project "
                "configuration, or report an issue if the problem persists."
            )

        shell_cmd = SHELLS.get(bertrand.shell)
        if shell_cmd is None:
            raise ValueError(f"unrecognized shell: {bertrand.shell}")
        if shell is not None:
            shell_cmd = SHELLS.get(shell)
            if shell_cmd is None:
                raise ValueError(f"unrecognized shell override: {shell}")

        image = await env.build(tag or DEFAULT_TAG, quiet=False)
        container = await image.create(
            env.config,
            env.id,
            shell_cmd,
            quiet=False,
        )
        deadline = time.monotonic() + min(env.lock.timeout, RPC_TIMEOUT)

        sidecar = await start_rpc_sidecar(
            container=container,
            container_bin=NERDCTL_BIN,
            deadline=deadline,
            strict=False,
            warn_context=(
                "bertrand: failed to start RPC sidecar; continuing without access to "
                "host RPC features"
            ),
        )

        try:
            await container.start(
                quiet=False,
                timeout=deadline - time.monotonic(),
                attach=True,
                interactive=True,
            )
        finally:
            await stop_rpc_sidecar(sidecar)
            await nerdctl(
                [
                    "container",
                    "rm",
                    "-f",
                    "-i",
                    "-v",
                    "--depend",
                    container.Id,
                ],
                check=False,
                capture_output=True,
            )


async def bertrand_stop(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    deadline: float,
) -> None:
    """Stop running Bertrand containers within an environment.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.
    tag : str | None
        Optional member tag to scope the command.
    deadline : float
        Timestamp before which this command should complete, relative to the epoch.

    Raises
    ------
    OSError
        If runtime stop operations fail.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")

    async with await Environment.load(worktree, timeout=deadline - time.time()) as env:
        ids = await _cli_containers(
            env,
            tag,
            status=("running", "restarting", "paused"),
            timeout=deadline - time.time()
        )
        if ids:
            timeout = deadline - time.time()
            await nerdctl(
                [
                    "container",
                    "stop",
                    "-t", str(int(math.ceil(timeout))),
                    *ids
                ],
                timeout=timeout
            )


async def bertrand_pause(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    deadline: float
) -> None:
    """Pause running Bertrand containers within an environment.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.
    tag : str | None
        Optional member tag to scope the command.
    deadline : float
        Timestamp before which this command should complete, relative to the epoch.

    Raises
    ------
    OSError
        If runtime pause operations fail.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")

    async with await Environment.load(worktree, timeout=deadline - time.time()) as env:
        ids = await _cli_containers(
            env,
            tag,
            status=("running", "restarting"),
            timeout=deadline - time.time()
        )
        if ids:
            await nerdctl(
                ["container", "pause", *ids],
                timeout=deadline - time.time()
            )


async def bertrand_resume(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    deadline: float
) -> None:
    """Resume paused Bertrand containers within an environment.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.
    tag : str | None
        Optional member tag to scope the command.
    deadline : float
        Timestamp before which this command should complete, relative to the epoch.

    Raises
    ------
    OSError
        If runtime unpause operations fail.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")

    async with await Environment.load(worktree, timeout=deadline - time.time()) as env:
        ids = await _cli_containers(
            env,
            tag,
            status=("paused",),
            timeout=deadline - time.time()
        )
        if ids:
            await nerdctl(
                ["container", "unpause", *ids],
                timeout=deadline - time.time()
            )


async def bertrand_restart(
    worktree: Path,
    workload: str | None,
    tag: str | None,
) -> None:
    """Restart running or paused Bertrand containers within an environment.

    If an image or container is out of date, it is rebuilt before restart.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.
    tag : str | None
        Optional member tag to scope the command.

    Raises
    ------
    OSError
        If rebuild/restart operations fail.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")

    async with await Environment.load(worktree, timeout=TIMEOUT) as env:
        tags: list[str]
        if tag is None:
            tags = list(env.images)
        else:
            tags = [tag]
        for current_tag in tags:
            containers = await Container.inspect(await _cli_containers(
                env,
                current_tag,
                status=("running", "restarting", "paused"),
                timeout=env.lock.timeout
            ))
            if not containers:
                continue

            image = await env.build(current_tag, quiet=False)
            defer: list[list[str]] = []
            for container in containers:
                try:
                    if container.Image == image.id:
                        await nerdctl(
                            [
                                "container",
                                "restart",
                                "-t", str(int(math.ceil(env.lock.timeout))),
                                container.Id
                            ],
                            timeout=env.lock.timeout
                        )
                    else:
                        await nerdctl(
                            [
                                "container",
                                "stop",
                                "-t", str(int(math.ceil(env.lock.timeout))),
                                container.Id
                            ],
                            timeout=env.lock.timeout
                        )
                        if container.Path:
                            defer.append([container.Path, *container.Args])
                        else:
                            print(
                                "bertrand: could not recover container argv during "
                                f"restart of {_recover_spec(worktree, workload, current_tag)}: "
                                f"{container.Id}",
                                file=sys.stderr
                            )
                except Exception as err:
                    print(
                        f"bertrand: failed to stop container during restart of "
                        f"{_recover_spec(worktree, workload, current_tag)}: {container.Id}\n"
                        f"{err}",
                        file=sys.stderr
                    )

            for cmd in defer:
                container = await image.create(env.config, env.id, cmd, quiet=False)
                await container.start(
                    quiet=False,
                    timeout=env.lock.timeout,
                    attach=False,
                    interactive=False,
                )


async def bertrand_rm(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    deadline: float,
    force: bool,
) -> None:
    """Delete Bertrand entities on the system, scoped to images and containers within
    an environment.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.
    tag : str | None
        Optional member tag to scope the command.
    deadline : float
        Timestamp before which this command should complete, relative to the epoch.
    force : bool
        If True, containers/images are forcefully removed where applicable.

    Notes
    -----
    This command only deletes container-runtime artifacts; it never deletes the
    worktree itself.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")

    async with await Environment.load(worktree, timeout=deadline - time.time()) as env:
        if tag is None:
            while env.images:
                _, image = env.images.popitem()
                env._json.retired.append(EnvironmentMetadata.RetiredImage(
                    force=force,
                    image=image,
                ))
        else:
            image = env.images.pop(tag)
            if image is not None:
                env._json.retired.append(EnvironmentMetadata.RetiredImage(
                    force=force,
                    image=image,
                ))


async def bertrand_ls(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    deadline: float,
    image: bool,
    format: str,
) -> None:
    """Gather status information for containers/images in a Bertrand environment.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.
    tag : str | None
        Optional member tag to scope the command.
    deadline : float
        Timestamp before which this command should complete, relative to the epoch.
    image : bool
        If True, list images. Otherwise, list containers.
    format : str
        Output format: `id`, `json`, `table`, or `table <template>`.

    Raises
    ------
    ValueError
        If format input is invalid.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")
    format_mode, table_template = _parse_output_format(
        format,
        allow_id=True
    )

    async with await Environment.load(worktree, timeout=deadline - time.time()) as env:
        if format_mode == "id":
            if image:
                ids = await _cli_images(
                    env,
                    tag,
                    timeout=deadline - time.time()
                )
            else:
                ids = await _cli_containers(
                    env,
                    tag,
                    timeout=deadline - time.time()
                )
            for id_ in ids:
                print(id_)
            return

        if image:
            cmd = [
                "image",
                "ls",
                "-a",
                "--filter", f"label={BERTRAND_ENV}=1",
                "--filter", f"label={ENV_ID_ENV}={env.id}",
                "--filter", f"label={IMAGE_TAG_ENV}={tag}",
            ]
            if format_mode == "json":
                cmd.append("--no-trunc")
                cmd.append("--format=json")
            else:
                template = (
                    table_template or
                    "{{.Names}}\t{{.CreatedAt}}\t{{.Containers}}\t{{.ReadOnly}}\t"
                    "{{.Size}}\t{{.History}}"
                )
                cmd.append(f"--format=table {template}")
        else:
            cmd = [
                "container",
                "ls",
                "-a",
                "--size",
                "--filter", f"label={BERTRAND_ENV}=1",
                "--filter", f"label={ENV_ID_ENV}={env.id}",
                "--filter", f"label={IMAGE_TAG_ENV}={tag}",
            ]
            if format_mode == "json":
                cmd.append("--no-trunc")
                cmd.append("--format=json")
            else:
                template = (
                    table_template or
                    "{{.Names}}\t{{.CreatedAt}}\t{{.State}}\t{{.Command}}\t"
                    "{{.RunningFor}}\t{{.Status}}\t{{.Restarts}}\t{{.Size}}\t"
                    "{{.Mounts}}\t{{.Networks}}\t{{.Ports}}"
                )
                cmd.append(f"--format=table {template}")

        await nerdctl(cmd, timeout=deadline - time.time())


async def bertrand_monitor(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    deadline: float,
    interval: int,
    format: str,
) -> None:
    """Gather resource utilization statistics for scoped Bertrand containers.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.
    tag : str | None
        Optional member tag to scope the command.
    deadline : float
        Timestamp before which this command should complete, relative to the epoch.
    interval : int
        Poll interval in seconds. Zero performs a single snapshot.
    format : str
        Output format: `json`, `table`, or `table <template>`.

    Raises
    ------
    ValueError
        If interval/format inputs are invalid.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")
    if interval < 0:
        raise ValueError("interval must be non-negative")
    format_mode, table_template = _parse_output_format(
        format,
        allow_id=False
    )
    if format_mode == "json" and interval:
        raise ValueError("cannot use 'json' and 'interval' together")

    async with await Environment.load(worktree, timeout=deadline - time.time()) as env:
        ids = await _cli_containers(env, tag, timeout=deadline - time.time())
        if not ids:
            if format_mode == "json":
                print("[]")
            return

        cmd = ["container", "stats"]
        if not interval:
            cmd.append("--no-stream")
        else:
            cmd.append(f"--interval={interval}")

        if format_mode == "json":
            cmd.append("--no-trunc")
            cmd.append("--format=json")
            cmd.extend(ids)
            await nerdctl(cmd, timeout=deadline - time.time())
        else:
            template = (
                table_template or
                "{{.Name}}\t{{.AVGCPU}}\t{{.CPUPerc}}\t{{.PIDs}}\t{{.MemUsage}}\t"
                "{{.NetIO}}\t{{.BlockIO}}"
            )
            cmd.append(f"--format=table {template}")
            cmd.extend(ids)
            await nerdctl(cmd, timeout=deadline - time.time())


async def bertrand_top(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    deadline: float
) -> None:
    """Display running processes for scoped Bertrand containers.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.
    tag : str | None
        Optional member tag to scope the command.
    deadline : float
        Timestamp before which this command should complete, relative to the epoch.

    Raises
    ------
    OSError
        If runtime top operations fail.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")

    async with await Environment.load(worktree, timeout=deadline - time.time()) as env:
        ids = await _cli_containers(
            env,
            tag,
            timeout=deadline - time.time()
        )
        for id_ in ids:
            await nerdctl(
                ["container", "top", id_],
                timeout=deadline - time.time(),
            )
            print()


async def bertrand_log(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    deadline: float,
    image: bool,
    since: str | None,
    until: str | None,
) -> None:
    """Print logs/history for scoped Bertrand targets.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.
    tag : str | None
        Optional member tag to scope the command.
    deadline : float
        Timestamp before which this command should complete, relative to the epoch.
    image : bool
        If True, show image history instead of container logs.
    since : str | None
        Lower bound for log time range (container logs mode only).
    until : str | None
        Upper bound for log time range (container logs mode only).

    Raises
    ------
    ValueError
        If `since`/`until` are used in image-history mode.
    OSError
        If runtime log/history operations fail.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")

    async with await Environment.load(worktree, timeout=deadline - time.time()) as env:
        if image:
            ids = await _cli_images(env, tag, timeout=deadline - time.time())
            cmd = [
                "image",
                "history",
                "--human",
                (
                    "--format=table {{.CreatedAt}}\t{{.CreatedSince}}\t{{.CreatedBy}}\t"
                    "{{.Size}}\t{{.Comment}}"
                ),
            ]
            if since is not None:
                raise ValueError("cannot use 'since' with image logs")
            if until is not None:
                raise ValueError("cannot use 'until' with image logs")
        else:
            ids = await _cli_containers(
                env,
                tag,
                timeout=deadline - time.time()
            )
            cmd = [
                "container",
                "logs",
                "--color",
                "--follow",
                "--names",
                "--timestamps",
            ]
            if since is not None:
                cmd.append("--since")
                cmd.append(since)
            if until is not None:
                cmd.append("--until")
                cmd.append(until)

        for id_ in ids:
            await nerdctl([*cmd, id_], timeout=deadline - time.time())
            print()
