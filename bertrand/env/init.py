"""Backend implementation for the `bertrand init` command, including host bootstrap
for rootless container operation and project initialization.
"""
from __future__ import annotations

import json
import os
import platform
import re
import shlex
import shutil
import sys
from dataclasses import dataclass
from importlib import resources as importlib_resources
from pathlib import Path
from typing import Awaitable, Callable, Literal, Self

from pydantic import BaseModel, ConfigDict, PositiveInt

from .config import Config, NonEmpty, Trimmed
from .container import STATE_DIR, TIMEOUT, podman_cmd, podman_ids
from .run import (
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


type InitStage = Literal[
    "fresh",
    "detect_platform",
    "install_container_cli",
    "enable_user_namespaces",
    "provision_subids",
    "delegate_controllers",
    "ready",
]


INIT_LOCK = STATE_DIR / "init.lock"
INIT_STATE_FILE = STATE_DIR / "init.state.json"
INIT_STATE_VERSION: int = 1


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


###############################
####    DETECT PLATFORM    ####
###############################


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


###################################
####    INSTALL OCI RUNTIME    ####
###################################


async def _podman_ready() -> bool:
    if not shutil.which("podman"):
        return False
    result = await run(
        ["podman", "info", "--format", "json"],
        check=False,
        capture_output=True
    )
    return result.returncode == 0


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


async def _install_container_cli(state: InitState, assume_yes: bool) -> None:
    if await _podman_ready():
        return
    if os.name != "posix":
        raise OSError("package manager operations require a POSIX system.")
    if state.package_manager is None:
        raise ValueError("package manager was not initialized in install state")
    spec = _INSTALL_SPECS.get(state.package_manager)
    if spec is None:
        supported = ", ".join(sorted(_INSTALL_SPECS))
        raise ValueError(
            f"unsupported package manager '{state.package_manager}' (supported: {supported})"
        )

    # prompt for installation
    if not confirm(
        "Bertrand requires 'podman' to manage rootless containers.  Would you like to "
        f"install it now using {state.package_manager} (requires sudo).\n"
        "[y/N] ",
        assume_yes=assume_yes,
    ):
        raise OSError("Installation declined by user.")

    # ensure we can elevate if needed
    if os.geteuid() != 0 and not can_escalate():
        raise PermissionError(
            "package installation requires root privileges; sudo not available."
        )
    if not shutil.which(spec.install[0]):
        raise FileNotFoundError(
            f"package manager '{state.package_manager}' not found: {spec.install[0]}"
        )

    # define required packages according to podman documentation
    packages = [
        "podman",
        "slirp4netns",
        "passt",
        "fuse-overlayfs",
    ]
    if state.package_manager == "apt":
        packages.append("uidmap")
    elif state.package_manager == "dnf":
        packages.append("shadow-utils")

    # define environment for non-interactive installation if needed
    env: dict[str, str] | None = None
    if assume_yes and spec.noninteractive_env:
        env = os.environ.copy()
        env.update(spec.noninteractive_env)

    # refresh package index
    if spec.refresh is not None:
        if not shutil.which(spec.refresh[0]):
            raise FileNotFoundError(f"refresh command not found: {spec.refresh[0]}")
        cmd = spec.refresh.copy()
        if assume_yes:
            cmd.extend(spec.yes_refresh)
        await run(cmd, env=env)

    # install packages
    cmd = spec.install.copy()
    if assume_yes:
        cmd.extend(spec.yes_install)
    cmd.extend(packages)
    await run(cmd, env=env)

    # confirm success
    if not await _podman_ready():
        raise OSError(
            "Installation completed, but 'podman' is still not found.  Please "
            "investigate the issue and ensure the required packages are installed."
        )


########################################
####    ROOTLESS USER NAMESPACES    ####
########################################


def _read_proc_sys(path: str) -> int | None:
    p = Path("/proc/sys") / path
    try:
        return int(p.read_text(encoding="utf-8").strip())
    except (OSError, ValueError):
        return None


async def _enable_user_namespaces(state: InitState, assume_yes: bool) -> None:
    if os.name != "posix":
        raise OSError("User management operations require a POSIX system.")

    # check current status
    needed = 15000  # recommended by podman
    unpriv = _read_proc_sys("kernel/unprivileged_userns_clone")
    maxns = _read_proc_sys("user/max_user_namespaces")
    if (unpriv is None or unpriv != 0) and (maxns is None or maxns >= needed):
        return

    # prompt for sudo if needed
    if os.geteuid() != 0 and not can_escalate():
        raise PermissionError(
            "Enabling user namespaces requires root privileges; no sudo available."
        )
    if not confirm(
        "Rootless containers require unprivileged user namespaces to be enabled on "
        "the host system.  This may require sudo privileges.\n"
        "Do you want to proceed? [y/N] ",
        assume_yes=assume_yes
    ):
        raise OSError("User declined to enable unprivileged user namespaces.")

    # enable unprivileged user namespaces
    if unpriv == 0:
        await run(sudo(
            ["sysctl", "-w", "kernel.unprivileged_userns_clone=1"],
            non_interactive=assume_yes
        ))
    if maxns is not None and maxns < needed:
        await run(sudo(
            ["sysctl", "-w", f"user.max_user_namespaces={needed}"],
            non_interactive=assume_yes
        ))

    # verify success
    unpriv = _read_proc_sys("kernel/unprivileged_userns_clone")
    maxns = _read_proc_sys("user/max_user_namespaces")
    if (unpriv == 0) or (maxns is not None and maxns < needed):
        raise OSError("Failed to enable unprivileged user namespaces.")


###################################################
####    ROOTLESS SUBUID/SUBGID PROVISIONING    ####
###################################################


SUBID_REGEX = re.compile(r"^(?P<user>[^:]+):(?P<start>\d+):(?P<count>\d+)\s*$")


@dataclass(frozen=True)
class SubIDRange:
    """A range of subordinate IDs for a user.

    Attributes
    ----------
    user : str
        The username.
    start : int
        The starting subordinate ID.
    count : int
        The number of subordinate IDs in the range.
    """
    user: str
    start: int
    count: int

    @property
    def end(self) -> int:
        """
        Returns
        -------
        int
            The first subordinate ID after the end of the range.
        """
        return self.start + self.count


def _read_subid_file(path: Path) -> list[SubIDRange]:
    if not path.exists():
        return []
    out: list[SubIDRange] = []
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        m = SUBID_REGEX.match(line)
        if not m:
            continue
        out.append(SubIDRange(
            user=m.group("user"),
            start=int(m.group("start")),
            count=int(m.group("count")),
        ))
    return out


def _choose_non_overlapping_subid_start(ranges: list[SubIDRange], needed: int) -> int:
    base = 100000
    max_end = max((r.end for r in ranges), default=base)
    start = max(base, max_end)
    rem = start % needed
    if rem:
        start += (needed - rem)
    return start


async def _append_subid(
    path: Path,
    user: str,
    start: int,
    count: int,
    *,
    assume_yes: bool
) -> None:
    line = f"{user}:{start}:{count}"
    quoted_line = shlex.quote(line)
    quoted_path = shlex.quote(str(path))
    cmd = (
        "set -euo pipefail; "
        f"touch {quoted_path}; "
        "if command -v flock >/dev/null 2>&1; then "
        f"  exec 9>>{quoted_path}; flock -x 9; "
        "fi; "
        f"grep -F -x -q {quoted_line} {quoted_path} || echo {quoted_line} >> {quoted_path}"
    )
    await run(sudo(["sh", "-lc", cmd], non_interactive=assume_yes))


async def _provision_subids(state: InitState, assume_yes: bool) -> None:
    if os.name != "posix":
        raise OSError("User management operations require a POSIX system.")
    if state.user is None:
        raise ValueError("State user is not set")

    # check whether there are already enough subuids/subgids
    needed = 65536  # recommended by podman
    subuid_path = Path("/etc/subuid")
    subgid_path = Path("/etc/subgid")
    uid_ranges = _read_subid_file(subuid_path)
    gid_ranges = _read_subid_file(subgid_path)
    if (
        any(r.user == state.user and r.count >= needed for r in uid_ranges) and
        any(r.user == state.user and r.count >= needed for r in gid_ranges)
    ):
        return

    # prompt for sudo if needed
    if os.geteuid() != 0 and not can_escalate():
        raise PermissionError(
            "Subuid/subgid provisioning requires root privileges; no sudo available."
        )
    if not confirm(
        "Rootless containers require subordinate UID/GID ranges (>= 65536) in "
        "/etc/subuid and /etc/subgid.  Bertrand can configure this, but may "
        "require sudo privileges to do so.\n"
        "Do you want to proceed? [y/N] ",
        assume_yes=assume_yes
    ):
        raise OSError("User declined to provision subordinate UID/GID ranges.")

    # choose non-overlapping ranges and append to files
    start_uid = _choose_non_overlapping_subid_start(uid_ranges, needed)
    start_gid = _choose_non_overlapping_subid_start(gid_ranges, needed)
    await _append_subid(subuid_path, state.user, start_uid, needed, assume_yes=assume_yes)
    await _append_subid(subgid_path, state.user, start_gid, needed, assume_yes=assume_yes)

    # verify success
    uid_ranges = _read_subid_file(subuid_path)
    gid_ranges = _read_subid_file(subgid_path)
    if (
        not any(r.user == state.user and r.count >= needed for r in uid_ranges) or
        not any(r.user == state.user and r.count >= needed for r in gid_ranges)
    ):
        raise OSError("Failed to provision subuid/subgid ranges correctly.")


######################################################################
####    CONTROLLER DELEGATION (for cgroups v2 resource limits)    ####
######################################################################


def _cgroup_v2_controllers() -> set[str] | None:
    path = Path("/sys/fs/cgroup/cgroup.controllers")
    try:
        text = path.read_text(encoding="utf-8").strip()
    except OSError:
        return None
    return set(text.split()) if text else set()


async def _systemd_version() -> int | None:
    cp = await run(["systemctl", "--version"], check=False, capture_output=True)
    if cp.returncode != 0:
        return None
    line = ""
    if cp.stdout:
        line = cp.stdout.splitlines()[0].strip()
    match = re.search(r"systemd\s+(\d+)", line)
    if not match:
        return None
    return int(match.group(1))


def _dropin_delegate_controllers(path: Path) -> set[str]:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return set()
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("Delegate="):
            value = line.split("=", 1)[1].strip()
            return set(value.split()) if value else set()
    return set()


async def _delegate_controllers(state: InitState, assume_yes: bool) -> None:
    if os.name != "posix":
        raise OSError("systemd operations require a POSIX system.")
    if not shutil.which("systemctl"):
        raise OSError("systemctl not found")

    # check if cgroups v2 is in use and controllers are visible
    root_controllers = _cgroup_v2_controllers()
    if root_controllers is None:
        return

    # check if controllers are already delegated to the user slice
    required = {"cpu", "io", "memory", "pids"}
    systemd_version = await _systemd_version()
    if "cpuset" in root_controllers:
        if systemd_version is not None and systemd_version >= 244:
            required.add("cpuset")
        else:
            print(
                "bertrand: cpuset controller detected, but systemd < 244; "
                "skipping cpuset delegation.",
                file=sys.stderr
            )
    path = Path(
        f"/sys/fs/cgroup/user.slice/user-{state.uid}.slice/"
        f"user@{state.uid}.service/cgroup.controllers"
    )
    try:
        delegated_text = path.read_text(encoding="utf-8").strip()
        delegated = set(delegated_text.split()) if delegated_text else set()
    except OSError:
        delegated = set()
    if required.issubset(delegated):
        return

    # check for existing drop-in configuration that hasn't taken effect yet
    dropin_path = Path("/etc/systemd/system/user@.service.d/delegate.conf")
    if required.issubset(_dropin_delegate_controllers(dropin_path)):
        print(
            "bertrand: controller delegation is configured but may require "
            "logging out and back in to take effect.",
            file=sys.stderr
        )
        return

    # ensure we can elevate if needed
    if os.geteuid() != 0 and not can_escalate():
        raise PermissionError(
            "Configuring controller delegation requires root privileges; no sudo "
            "available."
        )
    if not confirm(
        "Enforcing resource limits for rootless containers requires "
        "cgroup controllers to be delegated to user sessions.  Bertrand "
        "can configure this, but may require sudo privileges to do so.\n"
        "Do you want to proceed? [y/N] ",
        assume_yes=assume_yes
    ):
        return  # skip if user declines

    # write drop-in configuration
    controllers = sorted(required)
    dropin_dir = Path("/etc/systemd/system/user@.service.d")
    await run(sudo(["mkdir", "-p", str(dropin_dir)], non_interactive=assume_yes))

    # write delegate.conf via sudo
    delegate_path = dropin_dir / "delegate.conf"
    contents = "[Service]\nDelegate=" + " ".join(controllers) + "\n"
    quoted_path = shlex.quote(str(delegate_path))
    await run(
        sudo(["sh", "-lc", f"cat > {quoted_path}"], non_interactive=assume_yes),
        input=contents
    )

    # reload system daemon to pick up drop-in
    await run(sudo(["systemctl", "daemon-reload"], non_interactive=assume_yes))

    # verify success
    if required.issubset(_dropin_delegate_controllers(dropin_path)):
        print(
            "bertrand: controller delegation updated. You may need to log out and "
            "back in for changes to take effect.",
            file=sys.stderr
        )
    else:
        print(
            "bertrand: controller delegation could not be configured; some resource "
            "limits may be unavailable.",
            file=sys.stderr
        )


##################################
####    PERSISTENT INSTALL    ####
##################################


async def _assert_container_cli_ready(state: InitState, assume_yes: bool) -> None:
    if not await _podman_ready():
        cmd = "bertrand init --yes" if assume_yes else "bertrand init"
        raise OSError(
            "podman is installed but not usable for rootless operation after init "
            "bootstrap. Verify host prerequisites (user namespaces, subuid/subgid, and "
            "controller delegation) and retry.  Run `podman info` for diagnostics. "
            f"(last command: `{cmd}`)"
        )


INIT_STAGES: tuple[tuple[InitStage, Callable[[InitState, bool], Awaitable[None]]], ...] = (
    ("fresh", _no_op),
    ("detect_platform", _detect_platform),
    ("install_container_cli", _install_container_cli),
    ("enable_user_namespaces", _enable_user_namespaces),
    ("provision_subids", _provision_subids),
    ("delegate_controllers", _delegate_controllers),
    ("ready", _assert_container_cli_ready),
)


#########################
####    GIT HOOKS    ####
#########################


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


async def _install_git_hooks(root: Path) -> None:
    # check if git is available
    if not shutil.which("git"):
        return

    # check if repo is not initialized
    git_dir = root / ".git"
    if not git_dir.exists():
        return
    repo = GitRepository(git_dir)
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
            target = await repo.git_path(destination, cwd=root)
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
            print(f"bertrand: failed to {stage} in {root}\n{err}", file=sys.stderr)


###################
####    CLI    ####
###################


async def bertrand_init(
    project_root: Path | None,
    *,
    profile: str | None,
    capabilities: set[str] | None,
    yes: bool,
) -> None:
    """Initialize host prerequisites and optionally bootstrap an environment root.

    Parameters
    ----------
    project_root : Path | None
        Optional project root path.  If None, only host bootstrap stages are run.
    profile : str | None
        Optional environment layout profile for `Config.init`.
    capabilities : set[str] | None
        Optional capability set for `Config.init`.
    yes : bool
        Whether to auto-accept prompts during host bootstrap stages.

    Raises
    ------
    OSError
        If Git is not found, or the project root repository is invalid.
    """
    # install container runtime if needed
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    async with Lock(INIT_LOCK, timeout=TIMEOUT):
        state = InitState.load()
        index = next(
            (i for i, (stage, _) in enumerate(INIT_STAGES) if stage == state.stage),
            0
        )
        if index == len(INIT_STAGES) - 1 and not await _podman_ready():
            index = 0
            state = InitState(version=INIT_STATE_VERSION)
            state.dump()

        # run any unfinished stages
        for stage, step in INIT_STAGES[index:]:
            await step(state, yes)
            state.stage = stage
            state.dump()

    # if no project root is provided, then we're done
    if project_root is None:
        return

    # determine whether the directory at the given root is already a valid git
    # repository
    if not shutil.which("git"):
        raise OSError("git is required for path-scoped initialization")
    repo = GitRepository(project_root / ".git")

    # if the repository did not previously exist or is invalid, initialize it
    if not repo:
        await repo.init(bare=True)
        worktree = repo.git_dir.parent / "main"
        await repo.create_worktree("main", target=worktree, create_branch=True)
        await Config.init(worktree, profile, capabilities)
        try:
            await run(["git", "add", "-A"], cwd=worktree, capture_output=True)
            await run(
                ["git", "commit", "--quiet", "-m", "Initial commit"],
                cwd=worktree,
                capture_output=True,
            )
        except Exception as err:
            print(
                f"bertrand: failed to create initial commit in {worktree}\n{err}",
                file=sys.stderr
            )

    # if the repository is bare, then we should try to converge its worktrees to match
    # the current branches
    if await repo.is_bare():
        if not await repo.supports_relative_paths():
            raise OSError(
                "git worktree relative path support is required for bare repository "
                "mode, but this git version does not support '--relative-paths' for "
                "worktree creation and move operations."
            )
        await repo.sync_worktrees()

    # install/update git hooks for the repository
    await _install_git_hooks(repo.git_dir.parent)


async def bertrand_clean(*, assume_yes: bool) -> None:
    """Best-effort cleanup of Bertrand-managed runtime artifacts on the host.

    Parameters
    ----------
    assume_yes : bool
        Whether to auto-accept prompts during cleanup.

    Raises
    ------
    OSError
        If cleanup is declined by the user.
    """
    if not confirm(
        "This will attempt to remove all containers, images, and volumes created by "
        f"Bertrand, and delete all local state stored in {STATE_DIR}.  It will not "
        "uninstall podman or revert any host system settings.  Do you want to proceed? "
        "[y/N] ",
        assume_yes=assume_yes,
    ):
        raise OSError("Cleanup declined by user.")

    if shutil.which("podman"):
        try:
            containers = await podman_ids("container", {})
            if containers:
                await podman_cmd(
                    ["container", "rm", "-f", "-i", *containers],
                    check=False
                )
        except Exception as err:
            print(f"bertrand: failed to clean containers:\n{err}", file=sys.stderr)

        try:
            images = await podman_ids("image", {})
            if images:
                await podman_cmd(
                    ["image", "rm", "-f", "-i", *images],
                    check=False
                )
        except Exception as err:
            print(f"bertrand: failed to clean images:\n{err}", file=sys.stderr)

        try:
            volumes = await podman_ids("volume", {})
            if volumes:
                await podman_cmd(
                    ["volume", "rm", "-f", "-i", *volumes],
                    check=False
                )
        except Exception as err:
            print(f"bertrand: failed to clean volumes:\n{err}", file=sys.stderr)

    # delete the entire state directory
    shutil.rmtree(STATE_DIR, ignore_errors=True)
