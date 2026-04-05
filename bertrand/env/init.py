"""Backend implementation for the `bertrand init` command, including host bootstrap
for MicroK8s runtime prerequisites and project initialization.
"""
from __future__ import annotations

import asyncio
import grp
import hashlib
import json
import os
import platform
import pwd
import signal
import shutil
import sys
import uuid
from collections.abc import Awaitable, Callable
from dataclasses import dataclass
from importlib import resources as importlib_resources
from pathlib import Path
from typing import Literal, Self

from pydantic import BaseModel, ConfigDict, PositiveInt

from .config import RESOURCE_NAMES, Config, Resource
from .config.core import NonEmpty, Trimmed
from .container import (
    BUILDCTL_BIN,
    BUILDKITD_BIN,
    BUILDKIT_LOG_FILE,
    BUILDKIT_PID_FILE,
    MICROK8S_CHANNEL,
    MICROK8S_GROUP,
    NERDCTL_BASE_URL,
    NERDCTL_BIN,
    NERDCTL_CHECKSUM,
    NERDCTL_INSTALL_DIR,
    NERDCTL_VERSION,
    NORMALIZE_ARCH,
    STATE_DIR,
    TOOLS_DIR,
    TOOLS_TMP_DIR,
    nerdctl,
)
from .run import (
    BERTRAND_ENV,
    LOCK_TIMEOUT,
    GitRepository,
    Lock,
    User,
    atomic_write_text,
    can_escalate,
    confirm,
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
    timeout: float = LOCK_TIMEOUT,
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
    async with Lock(INIT_LOCK, timeout=timeout):
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


# TODO: review cleanup logic after tying nerdctl into the new container.py runtime.


def _append_clean_error(
    errors: list[str],
    context: str,
    detail: str | Exception,
) -> None:
    message = str(detail).strip()
    if message:
        errors.append(f"{context}: {message}")
    else:
        errors.append(context)


def _command_error_detail(stdout: str, stderr: str, code: int) -> str:
    detail = stderr.strip() or stdout.strip()
    if detail:
        return detail
    return f"exit code {code}"


def _tail_text(path: Path, *, lines: int = 40) -> str:
    try:
        content = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return ""
    if lines < 1:
        return ""
    return "\n".join(content[-lines:])


def _chunks(values: list[str], size: int) -> list[list[str]]:
    if size < 1:
        raise ValueError("chunk size must be >= 1")
    return [values[i:i + size] for i in range(0, len(values), size)]


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


async def _buildkit_stop_best_effort(errors: list[str]) -> None:
    pid: int | None = None
    try:
        raw = BUILDKIT_PID_FILE.read_text(encoding="utf-8").strip()
        if raw:
            pid = int(raw)
    except FileNotFoundError:
        return
    except Exception as err:
        _append_clean_error(errors, "failed to read buildkit pid file", err)
        return

    if pid is None:
        return
    if not _pid_alive(pid):
        try:
            BUILDKIT_PID_FILE.unlink()
        except OSError:
            pass
        return

    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    except Exception as err:
        _append_clean_error(errors, f"failed to terminate buildkitd pid {pid}", err)

    deadline = asyncio.get_running_loop().time() + 5.0
    while _pid_alive(pid) and asyncio.get_running_loop().time() < deadline:
        await asyncio.sleep(0.1)

    if _pid_alive(pid):
        try:
            os.kill(pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        except Exception as err:
            _append_clean_error(errors, f"failed to kill buildkitd pid {pid}", err)
        await asyncio.sleep(0.1)

    if _pid_alive(pid):
        detail = _tail_text(BUILDKIT_LOG_FILE, lines=40)
        if detail:
            _append_clean_error(
                errors,
                f"buildkitd pid {pid} is still running after terminate/kill",
                f"tail of {BUILDKIT_LOG_FILE}:\n{detail}",
            )
        else:
            _append_clean_error(
                errors,
                f"buildkitd pid {pid} is still running after terminate/kill",
                "",
            )

    try:
        BUILDKIT_PID_FILE.unlink()
    except FileNotFoundError:
        pass
    except OSError:
        pass


async def _nerdctl_ids_by_label(
    mode: Literal["container", "image"],
    label_key: str,
    label_value: str,
    errors: list[str],
) -> list[str]:
    if mode == "container":
        cmd = [
            "container",
            "ls",
            "-a",
            "-q",
            "--filter",
            f"label={label_key}={label_value}",
        ]
    else:
        cmd = [
            "image",
            "ls",
            "-a",
            "-q",
            "--no-trunc",
            "--filter",
            f"label={label_key}={label_value}",
        ]
    try:
        result = await nerdctl(cmd, check=False, capture_output=True)
    except Exception as err:
        _append_clean_error(errors, f"failed to list {mode}s for cleanup", err)
        return []
    if result.returncode != 0:
        _append_clean_error(
            errors,
            f"failed to list {mode}s for cleanup",
            _command_error_detail(result.stdout, result.stderr, result.returncode),
        )
        return []
    seen: set[str] = set()
    out: list[str] = []
    for line in result.stdout.splitlines():
        id_ = line.strip()
        if not id_ or id_ in seen:
            continue
        seen.add(id_)
        out.append(id_)
    return out


def _extract_labeled_ids_from_inspect(
    payload: str,
    *,
    fallback_id: str | None,
    label_key: str,
    label_value: str,
) -> list[str]:
    data = json.loads(payload or "[]")
    if isinstance(data, dict):
        objects: list[object] = [data]
    elif isinstance(data, list):
        objects = data
    else:
        raise ValueError("inspect response was not a JSON object or array")

    out: list[str] = []
    for obj in objects:
        if not isinstance(obj, dict):
            continue
        labels = obj.get("Labels")
        if not isinstance(labels, dict):
            continue
        if labels.get(label_key) != label_value:
            continue
        candidate = obj.get("Name") or obj.get("ID") or obj.get("Id") or fallback_id
        if not isinstance(candidate, str):
            continue
        candidate = candidate.strip()
        if candidate:
            out.append(candidate)
    return out


async def _nerdctl_resources_by_label_via_inspect(
    mode: Literal["volume", "network"],
    label_key: str,
    label_value: str,
    errors: list[str],
) -> list[str]:
    try:
        listed = await nerdctl([mode, "ls", "-q"], check=False, capture_output=True)
    except Exception as err:
        _append_clean_error(errors, f"failed to list {mode}s for cleanup", err)
        return []
    if listed.returncode != 0:
        _append_clean_error(
            errors,
            f"failed to list {mode}s for cleanup",
            _command_error_detail(listed.stdout, listed.stderr, listed.returncode),
        )
        return []

    ids: list[str] = []
    seen: set[str] = set()
    for line in listed.stdout.splitlines():
        value = line.strip()
        if not value or value in seen:
            continue
        seen.add(value)
        ids.append(value)
    if not ids:
        return []

    selected: list[str] = []
    selected_seen: set[str] = set()
    for batch in _chunks(ids, 64):
        try:
            inspect = await nerdctl(
                [mode, "inspect", *batch],
                check=False,
                capture_output=True,
            )
        except Exception as err:
            _append_clean_error(
                errors,
                f"failed to inspect {mode} batch during cleanup",
                err,
            )
            inspect = None
        if inspect is not None and inspect.returncode == 0:
            try:
                matches = _extract_labeled_ids_from_inspect(
                    inspect.stdout,
                    fallback_id=None,
                    label_key=label_key,
                    label_value=label_value,
                )
            except Exception as err:
                _append_clean_error(
                    errors,
                    f"failed to parse {mode} inspect output during cleanup",
                    err,
                )
                matches = []
            for match in matches:
                if match in selected_seen:
                    continue
                selected_seen.add(match)
                selected.append(match)
            continue

        # batch inspect failed; fall back to single-resource inspection.
        for resource_id in batch:
            try:
                single = await nerdctl(
                    [mode, "inspect", resource_id],
                    check=False,
                    capture_output=True,
                )
            except Exception as err:
                _append_clean_error(
                    errors,
                    f"failed to inspect {mode} {resource_id}",
                    err,
                )
                continue
            if single.returncode != 0:
                _append_clean_error(
                    errors,
                    f"failed to inspect {mode} {resource_id}",
                    _command_error_detail(single.stdout, single.stderr, single.returncode),
                )
                continue
            try:
                matches = _extract_labeled_ids_from_inspect(
                    single.stdout,
                    fallback_id=resource_id,
                    label_key=label_key,
                    label_value=label_value,
                )
            except Exception as err:
                _append_clean_error(
                    errors,
                    f"failed to parse {mode} inspect output for {resource_id}",
                    err,
                )
                continue
            for match in matches:
                if match in selected_seen:
                    continue
                selected_seen.add(match)
                selected.append(match)
    return selected


async def _clean_batch_rm(
    mode: Literal["container", "image", "volume", "network"],
    ids: list[str],
    errors: list[str],
) -> None:
    if not ids:
        return
    if mode == "container":
        base = ["container", "rm", "-f", "-v"]
    elif mode == "image":
        base = ["image", "rm", "-f"]
    elif mode == "volume":
        base = ["volume", "rm", "-f"]
    else:
        base = ["network", "rm"]

    # first try a batched remove.
    try:
        bulk = await nerdctl([*base, *ids], check=False, capture_output=True)
    except Exception as err:
        _append_clean_error(errors, f"failed to remove {mode}s in batch", err)
        bulk = None
    if bulk is not None and bulk.returncode == 0:
        return

    # if batched remove failed, keep going one-by-one to maximize cleanup.
    for resource_id in ids:
        try:
            result = await nerdctl([*base, resource_id], check=False, capture_output=True)
        except Exception as err:
            _append_clean_error(errors, f"failed to remove {mode} {resource_id}", err)
            continue
        if result.returncode == 0:
            continue
        _append_clean_error(
            errors,
            f"failed to remove {mode} {resource_id}",
            _command_error_detail(result.stdout, result.stderr, result.returncode),
        )


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
        "This will remove Bertrand-managed containers, images, volumes, and networks "
        f"(label `{BERTRAND_ENV}=1`) and then delete local Bertrand state in "
        f"{STATE_DIR}.  It will not uninstall MicroK8s or revert host system "
        "settings.  Do you want to proceed? [y/N] ",
        assume_yes=assume_yes,
    ):
        raise OSError("Cleanup declined by user.")

    errors: list[str] = []

    # stop managed buildkit first so we don't leave stale daemon state behind.
    await _buildkit_stop_best_effort(errors)

    # remove runtime objects associated with Bertrand metadata labels.
    if NERDCTL_BIN.exists():
        containers = await _nerdctl_ids_by_label("container", BERTRAND_ENV, "1", errors)
        await _clean_batch_rm("container", containers, errors)

        images = await _nerdctl_ids_by_label("image", BERTRAND_ENV, "1", errors)
        await _clean_batch_rm("image", images, errors)

        volumes = await _nerdctl_resources_by_label_via_inspect(
            "volume",
            BERTRAND_ENV,
            "1",
            errors,
        )
        await _clean_batch_rm("volume", volumes, errors)

        networks = await _nerdctl_resources_by_label_via_inspect(
            "network",
            BERTRAND_ENV,
            "1",
            errors,
        )
        await _clean_batch_rm("network", networks, errors)
    else:
        _append_clean_error(
            errors,
            "skipped runtime object cleanup",
            f"managed nerdctl binary is missing at {NERDCTL_BIN}",
        )

    # delete the entire state directory last.
    try:
        shutil.rmtree(STATE_DIR)
    except FileNotFoundError:
        pass
    except Exception as err:
        _append_clean_error(errors, f"failed to remove {STATE_DIR}", err)

    if errors:
        details = "\n".join(f"- {entry}" for entry in errors)
        raise OSError(f"Bertrand cleanup completed with errors:\n{details}")
