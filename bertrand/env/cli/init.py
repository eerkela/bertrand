"""Bootstrap Bertrand's host runtime, then generate and/or configure a new project
repository, if requested.
"""
from __future__ import annotations

import asyncio
import grp
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import uuid
from collections.abc import Awaitable, Callable
from dataclasses import dataclass
from importlib import resources as importlib_resources
from pathlib import Path
from typing import Literal, Self

from pydantic import BaseModel, ConfigDict, PositiveInt

from ..ceph import (
    MountInfo,
    RepoCredentials,
)
from ..config import RESOURCE_NAMES, Config, Resource
from ..config.core import (
    AbsolutePosixPath,
    NonEmpty,
    Trimmed,
    UUIDHex,
    _check_uuid,
)
from ..kube import DEFAULT_VOLUME_SIZE, RepoVolume
from ..run import (
    BERTRAND_GROUP,
    INFINITY,
    METADATA_REPO_ID,
    REPO_DIR,
    REPO_MOUNT_EXT,
    RUN_DIR,
    STATE_DIR,
    GitRepository,
    GroupStatus,
    Lock,
    User,
    abspath,
    assert_microceph_installed,
    assert_microk8s_installed,
    assert_nerdctl_installed,
    atomic_write_text,
    can_escalate,
    confirm,
    ensure_bertrand_state,
    install_microceph,
    install_microk8s,
    install_nerdctl,
    install_packages,
    link_kube_ceph,
    run,
    start_microceph,
    start_microk8s,
    symlink_points_to,
)

##################################
####    PERSISTENT INSTALL    ####
##################################


INIT_LOCK = Path("/tmp/bertrand-init.lock")
INIT_LOCK_MODE = 0o666
INIT_STATE_FILE = STATE_DIR / "init.state.json"
INIT_STATE_VERSION: int = 1
INIT_PREREQS = {
    "apt": {
        "tar": "tar",
        "curl": "curl",
        "getfacl": "acl",
        "setfacl": "acl",
        "groupadd": "passwd",
        "usermod": "passwd",
        "install": "coreutils",
    },
    "dnf": {
        "tar": "tar",
        "curl": "curl",
        "acl": "acl",
        "groupadd": "shadow-utils",
        "usermod": "shadow-utils",
        "install": "coreutils",
    },
    "yum": {
        "tar": "tar",
        "curl": "curl",
        "acl": "acl",
        "groupadd": "shadow-utils",
        "usermod": "shadow-utils",
        "install": "coreutils",
    },
    "zypper": {
        "tar": "tar",
        "curl": "curl",
        "acl": "acl",
        "groupadd": "shadow",
        "usermod": "shadow",
        "install": "coreutils",
    },
    "pacman": {
        "tar": "tar",
        "curl": "curl",
        "acl": "acl",
        "groupadd": "shadow",
        "usermod": "shadow",
        "install": "coreutils",
    },
    "apk": {
        "tar": "tar",
        "curl": "curl",
        "acl": "acl",
        "groupadd": "shadow",
        "usermod": "shadow",
        "install": "coreutils",
    },
}
INIT_CHECK_PREREQS = (
    ("tar", ("tar",)),
    ("curl/wget", ("curl", "wget")),
    ("getfacl", ("getfacl",)),
    ("setfacl", ("setfacl",)),
    ("groupadd", ("groupadd",)),
    ("usermod", ("usermod",)),
    ("install", ("install",)),
)


type InitStage = Literal[
    "fresh",
    "detect_platform",
    "install_prereqs",
    "bootstrap_state_dir",
    "install_ceph_runtime",
    "install_kube_runtime",
    "install_nerdctl",
    "installed",
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

    @staticmethod
    def backend_trustworthy() -> bool:
        """Return True when the shared init-state backend can be safely reused.

        Returns
        -------
        bool
            True if the backend is trustworthy and can be reused, False otherwise.
        """
        if (
            not shutil.which("setfacl") or
            not shutil.which("getfacl") or
            not STATE_DIR.is_dir() or
            STATE_DIR.is_symlink()
        ):
            return False
        try:
            group_info = grp.getgrnam(BERTRAND_GROUP)
            stat_info = STATE_DIR.stat()
        except (KeyError, OSError):
            return False
        if (
            stat_info.st_uid != 0 or
            stat_info.st_gid != group_info.gr_gid or
            (stat_info.st_mode & 0o7777) != 0o2770
        ):
            return False
        result = subprocess.run(
            ["getfacl", "-cp", str(STATE_DIR)],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
        if result.returncode != 0:
            return False
        acl_lines = {line.strip() for line in result.stdout.splitlines() if line.strip()}
        access = f"group:{BERTRAND_GROUP}:rwx"
        default = f"default:group:{BERTRAND_GROUP}:rwx"
        return access in acl_lines and default in acl_lines

    @classmethod
    def load(cls) -> Self:
        if not cls.backend_trustworthy():
            return cls(version=INIT_STATE_VERSION)
        if not INIT_STATE_FILE.exists() or not INIT_STATE_FILE.is_file():
            return cls(version=INIT_STATE_VERSION)
        try:
            data = json.loads(INIT_STATE_FILE.read_text(encoding="utf-8"))
            if not isinstance(data, dict):
                raise ValueError("init state JSON must be an object")
            self = cls.model_validate(data)
        except Exception:
            return cls(version=INIT_STATE_VERSION)
        if self.version != INIT_STATE_VERSION:
            return cls(version=INIT_STATE_VERSION)
        return self

    def dump(self) -> None:
        atomic_write_text(
            INIT_STATE_FILE,
            json.dumps(self.model_dump(mode="json"), separators=(",", ":")) + "\n",
            encoding="utf-8",
        )


async def _no_op(state: InitState, assume_yes: bool) -> None:
    return


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


async def _install_prereqs(state: InitState, assume_yes: bool) -> None:
    # fail fast if no escalation path is available for package installs
    if os.geteuid() != 0 and not can_escalate():
        raise PermissionError(
            "Bertrand requires root escalation to install host bootstrap dependencies, "
            "but neither 'sudo' nor 'doas' is available.  Install one of these tools "
            "or manually rerun `bertrand init` as root."
        )
    if state.package_manager is None:
        raise OSError("Package manager is not detected; cannot install prerequisites.")

    # package mapping for bootstrap-required host tools across supported package managers
    packages = INIT_PREREQS.get(state.package_manager)
    if packages is None:
        raise OSError(
            f"Unsupported package manager for prerequisite installation: "
            f"{state.package_manager!r}"
        )

    # detect missing required bootstrap tools
    missing: set[str] = set()
    for tool, package in packages.items():
        if package in missing:
            continue
        if tool == "curl/wget":
            if shutil.which("curl") or shutil.which("wget"):
                continue
        elif shutil.which(tool):
            continue
        missing.add(package)
    if not missing:
        return

    # install missing tools
    if not confirm(
        "Bertrand requires host bootstrap tools to configure runtime "
        f"dependencies and shared state (missing: {', '.join(missing)}).  Would "
        "you like Bertrand to install missing packages now (requires sudo)?\n[y/N] ",
        assume_yes=assume_yes,
    ):
        raise PermissionError("Installation declined by user.")
    await install_packages(
        package_manager=state.package_manager,
        packages=sorted(missing),
        assume_yes=assume_yes,
        timeout=INFINITY,
    )

    # verify all required tools after installation
    unresolved: list[str] = [
        name for name, cmd in INIT_CHECK_PREREQS
        if not any(shutil.which(c) for c in cmd)
    ]
    if unresolved:
        raise OSError(
            "Prerequisite installation completed, but required host bootstrap tools "
            f"are still missing: {', '.join(unresolved)}."
        )


async def _bootstrap_state_dir(state: InitState, assume_yes: bool) -> None:
    if state.user is None:
        raise OSError("init state user is missing; rerun `bertrand init`.")
    await ensure_bertrand_state(
        user=state.user,
        assume_yes=assume_yes,
        timeout=INFINITY,
    )


async def _install_ceph_runtime(state: InitState, assume_yes: bool) -> None:
    if state.user is None:
        raise OSError("init state user is missing; rerun `bertrand init`.")
    if state.package_manager is None:
        raise OSError("Package manager is not detected; cannot install Ceph runtime.")
    if state.distro_id is None:
        raise OSError("Distro ID is not detected; cannot install Ceph runtime.")

    await install_microceph(
        user=state.user,
        package_manager=state.package_manager,
        distro_id=state.distro_id,
        assume_yes=assume_yes,
    )


async def _install_kube_runtime(state: InitState, assume_yes: bool) -> None:
    if state.user is None:
        raise OSError("init state user is missing; rerun `bertrand init`.")
    if state.package_manager is None:
        raise OSError("Package manager is not detected; cannot install Kubernetes runtime.")
    if state.distro_id is None:
        raise OSError("Distro ID is not detected; cannot install Kubernetes runtime.")

    await install_microk8s(
        package_manager=state.package_manager,
        user=state.user,
        distro_id=state.distro_id,
        assume_yes=assume_yes,
    )


async def _install_nerdctl(state: InitState, assume_yes: bool) -> None:
    await install_nerdctl()


async def _assert_installed(state: InitState, assume_yes: bool) -> None:
    if state.user is None:
        raise OSError("init state user is missing; rerun `bertrand init`.")

    bertrand_group = GroupStatus.get(state.user, BERTRAND_GROUP)
    if not bertrand_group.configured:
        raise OSError(
            f"user {state.user!r} is not in {BERTRAND_GROUP!r}.  Rerun `bertrand init` "
            "to configure shared Bertrand host-state access."
        )
    if not bertrand_group.active:
        raise OSError(
            f"user {state.user!r} is in {BERTRAND_GROUP!r}, but the current session "
            f"is not active in that group.  Run `newgrp {BERTRAND_GROUP}` or log out "
            "and back in, then rerun `bertrand init`."
        )

    await assert_microceph_installed(user=state.user)
    await assert_microk8s_installed(user=state.user)
    await assert_nerdctl_installed()


INIT_STAGES: tuple[tuple[InitStage, Callable[[InitState, bool], Awaitable[None]]], ...] = (
    ("fresh", _no_op),
    ("detect_platform", _detect_platform),
    ("install_prereqs", _install_prereqs),
    ("bootstrap_state_dir", _bootstrap_state_dir),
    ("install_ceph_runtime", _install_ceph_runtime),
    ("install_kube_runtime", _install_kube_runtime),
    ("install_nerdctl", _install_nerdctl),
    ("installed", _assert_installed),
)


############################
####    PROJECT INIT    ####
############################


@dataclass(frozen=True)
class GitHook:
    """Specifies a git hook to be installed into project repositories during
    initialization.

    Attributes
    ----------
    source : Path
        Relative path to the hook payload, starting from `bertrand.env.run`.
    destination : Path
        Relative path to the target hook location, starting from the repository's
        `.git/` directory (e.g. `hooks/pre-commit`).
    executable : bool
        Whether the installed hook should have executable permissions set.
    """
    source: Path
    destination: Path
    executable: bool


MANAGED_GIT_HOOKS: tuple[GitHook, ...] = (
    GitHook(
        source=Path("reference_transaction.py"),
        destination=Path("hooks/reference-transaction"),
        executable=True,
    ),
    GitHook(
        source=Path("bertrand_git.py"),
        destination=Path("hooks/bertrand_git.py"),
        executable=False,
    ),
)
REPO_LOCK_DIR = RUN_DIR / "init"
REPO_ID_NAMESPACE = uuid.UUID("7b1506f4-4a3f-4b46-94bb-471e0f59d1a0")
PROTECTED_DISABLE_RESOURCES: frozenset[str] = frozenset({"bertrand", "python"})


def _resolve_repo_id(repo: GitRepository) -> UUIDHex:
    repo_id_file = repo.root / METADATA_REPO_ID
    if repo_id_file.is_file():
        try:
            return _check_uuid(repo_id_file.read_text(encoding="utf-8").strip())
        except (OSError, ValueError):
            pass
    return uuid.uuid5(REPO_ID_NAMESPACE, repo.root.as_posix()).hex


async def _resurrect_mount(
    path: Path,
    *,
    timeout: float,
) -> tuple[UUIDHex, MountInfo] | None:
    if timeout <= 0:
        raise TimeoutError("timed out before repository mount resurrection started")
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout

    # search for managed bertrand symlink alias pointing to a mounted volume
    repo_id, mount = MountInfo.resolve(path)
    if repo_id is None:
        return None
    if mount is not None:
        return repo_id, mount

    # managed alias ancestry is authoritative: if the mount is missing, recover it
    # from cluster state or fail closed to avoid silently drifting ownership
    volumes = await RepoVolume.get(
        repo_id,
        timeout=deadline - loop.time(),
    )
    if not volumes:
        raise OSError(
            "repository alias ancestry was detected for repository "
            f"{repo_id}, but no matching cluster claim exists"
        )
    if len(volumes) != 1:
        names = ", ".join(sorted(volume.pvc.metadata.name for volume in volumes))
        raise OSError(
            "repository alias ancestry maps to multiple cluster claims and cannot be "
            f"disambiguated safely for {repo_id!r}: {names}"
        )

    # resolve Ceph path + credentials for the claim, then regenerate the local mount
    volume = volumes[0]
    ceph_path = await volume.resolve_ceph_path(timeout=deadline - loop.time())
    credentials = await RepoCredentials.create(
        repo_id=repo_id,
        ceph_path=ceph_path,
        timeout=deadline - loop.time(),
    )
    with credentials.secretfile() as ceph_secretfile:
        mount = await MountInfo.mount(
            repo_id=repo_id,
            ceph_path=ceph_path,
            timeout=deadline - loop.time(),
            monitors=credentials.monitors,
            ceph_user=credentials.user,
            ceph_secretfile=ceph_secretfile,
        )
    return repo_id, mount


def _parse_resource_specs(
    specs: list[str],
    *,
    for_disable: bool,
) -> set[Resource]:
    parsed: set[Resource] = set()
    protected = {RESOURCE_NAMES[name] for name in PROTECTED_DISABLE_RESOURCES}
    for spec in specs:
        for component in spec.split(","):
            name = component.strip()
            if name.lower() == "all":
                all_resources = set(RESOURCE_NAMES.values())
                if for_disable:
                    parsed.update(all_resources - protected)
                else:
                    parsed.update(all_resources)
                continue
            resource = RESOURCE_NAMES.get(name)
            if resource is None:
                raise ValueError(
                    f"unknown resource '{component}'.  Options are:\n"
                    f"{'\n'.join(f'    {option}' for option in sorted(RESOURCE_NAMES))}"
                )
            parsed.add(resource)
    return parsed


@dataclass
class RepoState:
    """In-memory convergence state shared by repository bootstrap stages.

    Attributes
    ----------
    target : AbsolutePath
        Absolute repository destination path where the managed mount alias should end
        up.  This is normalized from user input without resolving symlinks.
    deadline : float
        Absolute monotonic deadline for repository convergence, or infinite if
        unbounded.
    repo : GitRepository
        Repository handle resolved from the target path.
    worktree : Path
        Relative worktree path resolved from the target path.  A value of "."
        indicates repository-level targeting, while non-empty paths target a
        specific worktree.
    repo_id : UUIDHex
        Stable repository identity used for deterministic volume and mount recovery.
    volume : RepoVolume | None
        Repository PVC resolved or provisioned for `repo_id`.
    ceph_path : PosixPath | None
        Absolute CephFS path for the repository volume.
    mount_alias : Path | None
        Current host alias path selected for this convergence run.
    credentials : RepoCredentials | None
        Repository-scoped Ceph credentials for host mounting.
    """
    repo: GitRepository
    target: Path
    worktree: Path
    repo_id: UUIDHex
    volume: RepoVolume | None
    ceph_path: AbsolutePosixPath | None
    mount_alias: Path | None
    credentials: RepoCredentials | None
    deadline: float

    def __post_init__(self) -> None:
        self.target = abspath(self.target)

    @classmethod
    def lock(cls, root: Path, timeout: float) -> Lock:
        """
        Parameters
        ----------
        root : AbsolutePath
            Repository root path to get the corresponding lock file for.
        timeout : float
            Timeout for acquiring the lock.  If infinite, wait indefinitely.

        Returns
        -------
        Lock
            A unique lock object for the given repository root, which can be
            acquired as an async context manager to prevent race conditions for the
            same repository across concurrent `bertrand init` runs.
        """
        digest = hashlib.sha256(str(root).encode("utf-8"))
        return Lock(
            REPO_LOCK_DIR / f"{digest.hexdigest()}.lock",
            timeout=timeout,
            mode="local",
            privileges=INIT_LOCK_MODE,
        )


async def _ensure_repo_volume(
    state: RepoState,
    enable: set[Resource],
    disable: set[Resource],
    yes: bool,
) -> None:
    """Converge repository volume allocation and deterministic claim reuse."""
    loop = asyncio.get_running_loop()

    # first, try to recover an existing claim for this deterministic repository ID
    volumes = await RepoVolume.get(
        state.repo_id,
        timeout=state.deadline - loop.time()
    )
    claimed: RepoVolume | None = None
    if volumes:
        # deterministic claim naming should leave exactly one candidate claim for the
        # repository identity
        if len(volumes) != 1:
            names = ", ".join(sorted(volume.pvc.metadata.name for volume in volumes))
            raise OSError(
                "repository identity maps to multiple cluster claims and cannot be "
                f"disambiguated safely for {state.repo_id!r}: {names}"
            )
        claimed = volumes[0]

    # if we found a claim in the ceph cluster, verify that its internal mount point is
    # either unoccupied or maps to the expected Ceph path for this repository, in which
    # case we can safely reuse it
    if claimed is not None:
        ceph_path = await claimed.resolve_ceph_path(
            timeout=state.deadline - loop.time()
        )
        hidden_mount = REPO_DIR / state.repo_id / REPO_MOUNT_EXT
        mounted = MountInfo.search(hidden_mount)
        if mounted is not None and not (
            mounted.ceph_path is not None and
            mounted.ceph_path == ceph_path
        ):
            raise OSError(
                f"repository hidden mount {hidden_mount!r} is occupied by "
                f"{mounted.source!r}, expected Ceph source suffix ':{ceph_path}'"
            )
        state.volume = claimed
        state.ceph_path = ceph_path
        return

    # no existing claim was found: prompt before converting an unmanaged repo
    if state.repo and not confirm(
        "Bertrand found an existing unmanaged repository at "
        f"{state.repo.root}.  Do you want to convert it into a Bertrand repository?\n"
        "WARNING: this will delete all untracked files and convert the repository into "
        "a bare layout with isolated worktrees for each branch, in order to meet "
        "Bertrand's invariants.  Make sure to track or manually back up any important "
        "files in the repository before proceeding.  Additional information can be "
        "found in the Bertrand documentation, if needed.\nContinue? [y/N] ",
        assume_yes=yes,
    ):
        raise PermissionError("repository conversion declined by user")

    # allocate via create(), then persist in-memory resolved volume state
    state.volume = await RepoVolume.create(
        repo_id=state.repo_id,
        timeout=state.deadline - loop.time(),
        size_request=DEFAULT_VOLUME_SIZE,
    )
    state.ceph_path = await state.volume.resolve_ceph_path(
        timeout=state.deadline - loop.time()
    )


async def _ensure_repo_credentials(
    state: RepoState,
    enable: set[Resource],
    disable: set[Resource],
    yes: bool,
) -> None:
    """Ensure per-repository Ceph credentials exist, in order to allow mounting the
    volume on the host filesystem.
    """
    loop = asyncio.get_running_loop()
    ceph_path = state.ceph_path
    if ceph_path is None:
        raise OSError("repo volume must be resolved before credential convergence")

    # ensure that we have valid credentials for this repository, creating new ones if
    # necessary
    state.credentials = await RepoCredentials.create(
        repo_id=state.repo_id,
        ceph_path=ceph_path,
        timeout=state.deadline - loop.time(),
    )


async def _ensure_host_mount(
    state: RepoState,
    enable: set[Resource],
    disable: set[Resource],
    yes: bool,
) -> None:
    """Ensure the repository volume is mounted on the host filesystem at a stable path,
    staging the mount until cutover.
    """
    loop = asyncio.get_running_loop()
    if state.ceph_path is None or state.credentials is None:
        raise OSError("host mount convergence requires resolved volume and credentials")

    # get location at which to place the repository symlink, staging changes until the
    # final cutover step to avoid disrupting any source repository
    mount_dir = REPO_DIR / state.repo_id / REPO_MOUNT_EXT
    staged_alias = (
        state.target.parent / f".{state.target.name}.bertrand.mount.{state.repo_id}"
    )
    target_occupied = state.target.exists() or state.target.is_symlink()
    if not target_occupied or symlink_points_to(state.target, mount_dir):
        mount_alias = state.target
    else:
        mount_alias = staged_alias

    # mount the repository volume at the internal mount directory, then atomically
    # write a relocatable symlink to the target path, subject to staging.
    with state.credentials.secretfile() as ceph_secretfile:
        mount = await MountInfo.mount(
            repo_id=state.repo_id,
            ceph_path=state.ceph_path,
            timeout=state.deadline - loop.time(),
            monitors=state.credentials.monitors,
            ceph_user=state.credentials.user,
            ceph_secretfile=ceph_secretfile,
        )
    async with mount.aliases(timeout=state.deadline - loop.time(), gc=True) as aliases:
        aliases.link(mount_alias)

    # record the repository ID in the newly-mounted volume
    atomic_write_text(
        mount_dir / METADATA_REPO_ID,
        state.repo_id,
        encoding="utf-8",
    )

    state.mount_alias = mount_alias


async def _ensure_bare_worktrees(
    state: RepoState,
    enable: set[Resource],
    disable: set[Resource],
    yes: bool,
) -> None:
    """Converge repository to Bertrand's bare+worktree layout.  If we're converting an
    existing repository, then we first mirror all refs into the new repository, and
    then check out each local branch into a separate worktree, in order to preserve the
    existing structure and commit history.  Note that this will effectively delete all
    untracked files in the source repository, so users should make sure to back up or
    track any important files before proceeding with convergence.
    """
    if state.mount_alias is None:
        raise OSError("bare-worktree convergence requires a mounted repository alias")
    mount = GitRepository(
        REPO_DIR / state.repo_id / REPO_MOUNT_EXT / ".git"
    )
    loop = asyncio.get_running_loop()
    deadline = state.deadline
    target_branch: str | None = None

    # specific-worktree mode: capture source branch identity before any conversion so
    # we can remap to canonical converged worktree paths afterwards.
    if state.worktree != Path("."):
        source_worktree = state.repo.root / state.worktree
        match = next(
            (wt for wt in await state.repo.worktrees() if wt.path == source_worktree),
            None
        )
        if match is None:
            raise OSError(
                f"targeted worktree {source_worktree} is not registered in source "
                f"repository {state.repo.root}"
            )
        target_branch = match.branch
        if target_branch is None:
            raise OSError(
                f"targeted worktree {source_worktree} has detached HEAD; please attach "
                "it to a branch before running `bertrand init`."
            )

    # new repository: initialize a bare history store and create the first worktree
    if not state.repo:  # source repo is uninitialized or missing
        branch = await GitRepository.default_branch()
        default_worktree = state.mount_alias / branch
        if not mount:  # destination repo is also uninitialized - create initial worktree
            await mount.init(branch=branch, bare=True)
            await mount.create_worktree(
                branch,
                target=default_worktree,
                create_branch=True,
            )
        elif await mount.is_bare():
            if not any(wt.branch == branch for wt in await mount.worktrees()):
                await mount.create_worktree(
                    branch,
                    target=default_worktree,
                    create_branch=True,
                )
            await mount.sync_worktrees()
        else:
            raise OSError(
                f"managed destination repository at {state.mount_alias} "
                "must be bare"
            )
        state.repo = mount
        return

    # conversion path: mirror all refs from source into destination, then converge
    # one worktree per local branch under refs/heads/*
    if state.repo != mount:
        if await state.repo.dirty():
            raise OSError(
                f"cannot convert repository at {state.repo.root}: worktree has "
                "uncommitted changes"
            )
        await mount.mirror_from(state.repo, timeout=deadline - loop.time())

    # assert mounted directory is well-formed, then sync worktrees
    if not mount:
        raise OSError(
            "managed destination repository does not exist at "
            f"{state.mount_alias}"
        )
    if not await mount.is_bare():
        raise OSError(
            f"managed destination repository at {state.mount_alias} must be bare"
        )
    await mount.sync_worktrees()
    state.repo = mount
    if target_branch is not None:
        if not any(wt.branch == target_branch for wt in await mount.worktrees()):
            raise OSError(
                f"failed to map targeted source worktree branch {target_branch!r} into "
                f"converged repository at {mount.root}"
            )
        state.worktree = Path(target_branch)


async def _ensure_repo_hooks(
    state: RepoState,
    enable: set[Resource],
    disable: set[Resource],
    yes: bool,
) -> None:
    """Write the managed git hooks into the repository's .git/hooks directory, if they
    don't already exist or are outdated.
    """
    # load managed hook payloads before install; this preserves fail-fast behavior if
    # packaged hook definitions are malformed.
    for hook in MANAGED_GIT_HOOKS:
        stage = f"resolve managed hook for '{hook.destination}'"
        marker = f"# bertrand-managed: {hook.source}"
        try:
            # load hook from Bertrand package resources and verify shebang/marker
            expected: list[str] = []
            if hook.executable:
                expected.append("#!/usr/bin/env python3")
            expected.append(marker)
            hook_text = importlib_resources.files("bertrand.env").joinpath(
                "run",
                hook.source,
            ).read_text(encoding="utf-8")
            if hook_text.splitlines()[:len(expected)] != expected:
                raise ValueError(
                    f"packaged {hook.source} must start with:\n{'\n'.join(expected)}"
                )

            # do not clobber non-managed hooks
            stage = f"resolve existing git hook at '{hook.destination}'"
            target = await state.repo.git_path(
                hook.destination,
                cwd=state.repo.root
            )
            if target.exists():
                if not target.is_file():
                    raise FileNotFoundError(f"git hook path is not a file: {target}")
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
            if hook.executable:
                stage = f"set executable permissions on git hook {target}"
                try:
                    target.chmod(0o755)
                except OSError:
                    pass
        except Exception as err:
            print(
                f"bertrand: failed to {stage} in {state.repo.root}\n{err}",
                file=sys.stderr
            )


async def _render_config_artifacts(
    state: RepoState,
    enable: set[Resource],
    disable: set[Resource],
    yes: bool,
) -> None:
    """Converge rendered configuration artifacts for targeted worktrees."""
    loop = asyncio.get_running_loop()
    render_targets: list[Path] = []

    # repository-level targeting converges all branch-attached in-repo worktrees;
    # detached and out-of-tree worktrees are skipped with warnings.
    if state.worktree == Path("."):
        for worktree in sorted(await state.repo.worktrees(), key=lambda wt: wt.path.as_posix()):
            if worktree.branch is None:
                print(
                    f"bertrand: skipping detached worktree during repository-wide "
                    f"render: {worktree.path}",
                    file=sys.stderr,
                )
                continue
            if not worktree.path.is_relative_to(state.repo.root):
                print(
                    f"bertrand: skipping out-of-tree worktree during repository-wide "
                    f"render: {worktree.path}",
                    file=sys.stderr,
                )
                continue
            render_targets.append(worktree.path.relative_to(state.repo.root))
    else:
        render_targets.append(state.worktree)
    if not render_targets:
        raise OSError("repository-wide config render found no eligible branch worktrees")

    # render all targeted worktrees based on existing configuration (if any), then
    # apply enable/disable deltas before syncing artifacts.
    for worktree in render_targets:
        root = state.repo.root / worktree
        config = await Config.load(
            root,
            repo=state.repo,
            timeout=state.deadline - loop.time()
        )
        config.resources.update({resource.name: None for resource in enable})
        config.init = Config.Init(
            repo=state.repo,
            worktree=worktree,
        )
        async with config:
            for resource in disable:
                config.resources.pop(resource.name, None)
                for relative in sorted(resource.paths, key=lambda item: item.as_posix()):
                    path = root / relative
                    if not path.exists() and not path.is_symlink():
                        continue
                    elif path.is_dir() and not path.is_symlink():
                        shutil.rmtree(path, ignore_errors=True)
                    else:
                        path.unlink(missing_ok=True)
            await config.sync(tag=None)


async def _make_initial_commit(
    state: RepoState,
    enable: set[Resource],
    disable: set[Resource],
    yes: bool,
) -> None:
    """Create initial commit when repository is empty and staged diff is non-empty."""
    loop = asyncio.get_running_loop()
    worktree_path: Path | None = None

    # repository-level target: use the HEAD or first branch-attached worktree for
    # commit operations
    if state.worktree == Path("."):
        branch_targets = {
            wt.branch: wt.path.relative_to(state.repo.root)
            for wt in await state.repo.worktrees()
            if wt.branch is not None and wt.path.is_relative_to(state.repo.root)
        }
        head = await state.repo.head_branch()
        if head is not None and head in branch_targets:
            worktree_path = state.repo.root / branch_targets[head]
        elif branch_targets:
            worktree_path = state.repo.root / sorted(
                branch_targets.values(),
                key=lambda value: value.as_posix()
            )[0]
    else:
        worktree_path = state.repo.root / state.worktree

    # don't commit unless we found a valid worktree path to commit within
    if worktree_path is None:
        return

    # only commit if there are no existing commits
    head = await run(
        ["git", "rev-parse", "--verify", "HEAD"],
        cwd=worktree_path,
        check=False,
        capture_output=True,
        timeout=state.deadline - loop.time(),
    )
    if head.returncode == 0:
        return  # repository already has commits
    if head.returncode != 128:
        raise OSError(
            f"failed to determine whether repository already has commits:\n{head}"
        )

    # stage changes and only commit if there are differences
    await run(
        ["git", "add", "-A"],
        cwd=worktree_path,
        capture_output=True,
        timeout=state.deadline - loop.time(),
    )
    staged = await run(
        ["git", "diff", "--cached", "--quiet"],
        cwd=worktree_path,
        check=False,
        capture_output=True,
        timeout=state.deadline - loop.time(),
    )
    if staged.returncode == 0:
        return  # nothing staged after render
    if staged.returncode != 1:
        raise OSError(f"failed to inspect staged diff for initial commit:\n{staged}")

    # make commit
    await run(
        ["git", "commit", "--quiet", "-m", "Initial commit"],
        cwd=worktree_path,
        capture_output=True,
        timeout=state.deadline - loop.time(),
    )


async def _finalize(
    state: RepoState,
    enable: set[Resource],
    disable: set[Resource],
    yes: bool,
) -> None:
    """Complete any pending path cutover, finishing the repository bootstrap process."""
    mount_alias = state.mount_alias
    if mount_alias is None:
        raise OSError("cannot finalize repository convergence without a mount alias")
    target = state.target
    hidden_mount = REPO_DIR / state.repo_id / REPO_MOUNT_EXT
    staged_alias = target.parent / f".{target.name}.bertrand.mount.{state.repo_id}"
    swap_path = target.parent / f".{target.name}.bertrand.swap.{state.repo_id}"

    # already converged: just validate the final alias target.
    if mount_alias == target:
        if not symlink_points_to(target, hidden_mount):
            raise OSError(
                f"cannot finalize repository at {target}: alias path {target} does "
                f"not target expected mount path {hidden_mount}"
            )

    # interrupted swap: either destination is already converged or staged alias needs
    # promotion into place
    elif swap_path.exists() or swap_path.is_symlink():
        if target.exists() or target.is_symlink():
            if not symlink_points_to(target, hidden_mount):
                raise OSError(
                    f"cannot resume repository cutover at {target}: alias path {target} "
                    f"does not target expected mount path {hidden_mount}"
                )
        else:
            if not mount_alias.exists() and not mount_alias.is_symlink():
                raise OSError(
                    f"cannot resume repository cutover at {target}: neither staged "
                    "alias nor destination path exists while swap path is present"
                )
            if not symlink_points_to(mount_alias, hidden_mount):
                raise OSError(
                    f"cannot resume repository cutover at {target}: alias path "
                    f"{mount_alias} does not target expected mount path {hidden_mount}"
                )
            mount_alias.rename(target)

    # destination occupied: two-step atomic swap with rollback
    elif target.exists() or target.is_symlink():
        if not mount_alias.exists() and not mount_alias.is_symlink():
            raise OSError(
                f"cannot cut over repository at {target}: staged alias does not exist "
                f"({mount_alias})"
            )
        if not symlink_points_to(mount_alias, hidden_mount):
            raise OSError(
                f"cannot cut over repository at {target}: alias path {mount_alias} "
                f"does not target expected mount path {hidden_mount}"
            )
        if not confirm(
            "Bertrand needs to atomically replace this path with a managed "
            "CephFS repository alias to complete conversion. Continue?\n[y/N] ",
            assume_yes=yes,
        ):
            raise PermissionError("repository cutover declined by user")
        target.rename(swap_path)
        try:
            mount_alias.rename(target)
        except Exception as err:
            if (
                (swap_path.exists() or swap_path.is_symlink()) and
                not target.exists() and
                not target.is_symlink()
            ):
                swap_path.rename(target)
            raise OSError(f"failed to atomically swap repository path at {target}") from err

    # destination empty: promote staged alias directly
    else:
        if not mount_alias.exists() and not mount_alias.is_symlink():
            raise OSError(
                f"cannot finalize repository cutover at {target}: staged alias does "
                f"not exist ({mount_alias})"
            )
        if not symlink_points_to(mount_alias, hidden_mount):
            raise OSError(
                f"cannot finalize repository cutover at {target}: alias path "
                f"{mount_alias} does not target expected mount path {hidden_mount}"
            )
        mount_alias.rename(target)

    # final invariant: destination path must be a managed alias to hidden mount
    if not symlink_points_to(target, hidden_mount):
        raise OSError(
            f"repository cutover failed for destination path {target}: alias path "
            f"{target} does not target expected mount path {hidden_mount}"
        )

    # clean up displaced target after successful cutover (best effort)
    if swap_path.exists() or swap_path.is_symlink():
        try:
            if not swap_path.is_symlink() and swap_path.is_dir():
                shutil.rmtree(swap_path)
            else:
                swap_path.unlink()
        except OSError as err:
            print(
                f"bertrand: warning: failed to delete conversion swap path at "
                f"{swap_path}: {err}",
                file=sys.stderr,
            )

    # remove stale staged alias left behind by interrupted swaps once destination is
    # converged to the managed mount alias
    if mount_alias != target and (mount_alias.exists() or mount_alias.is_symlink()):
        if symlink_points_to(mount_alias, hidden_mount):
            loop = asyncio.get_running_loop()
            # Alias-state operations do not need live mount-table lookups.
            mount = MountInfo(mount_point=hidden_mount)
            async with mount.aliases(timeout=state.deadline - loop.time(), gc=True) as aliases:
                aliases.unlink(mount_alias)
    if (
        staged_alias != target and
        staged_alias != mount_alias and
        (staged_alias.exists() or staged_alias.is_symlink()) and
        symlink_points_to(staged_alias, hidden_mount)
    ):
        loop = asyncio.get_running_loop()
        # Alias-state operations do not need live mount-table lookups.
        mount = MountInfo(mount_point=hidden_mount)
        async with mount.aliases(timeout=state.deadline - loop.time(), gc=True) as aliases:
            aliases.unlink(staged_alias)
    state.mount_alias = target


REPO_STAGES: tuple[Callable[
    [RepoState, set[Resource], set[Resource], bool],
    Awaitable[None]
], ...] = (
    _ensure_repo_volume,
    _ensure_repo_credentials,
    _ensure_host_mount,
    _ensure_bare_worktrees,
    _ensure_repo_hooks,
    _render_config_artifacts,
    _make_initial_commit,
    _finalize,
)


###################
####    CLI    ####
###################


async def bertrand_init(
    path: Path | None,
    *,
    timeout: float,
    enable: list[str],
    disable: list[str],
    yes: bool,
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
    timeout : float
        Time (in seconds) to wait for the repository to become available with the
        expected configuration.  If infinite, then wait indefinitely.  Note that this
        does not apply to any host bootstrapping stages, which may require user
        confirmation, and are only run once per host (not per repository).
    enable : list[str]
        List of resources to enable at the resolved worktree.  Each component is a
        comma-separated list of resource names or aliases, which are resolved to their
        corresponding, unique `Resource` implementations.
    disable : list[str]
        List of resources to disable at the resolved worktree.  Each component is a
        comma-separated list of resource names or aliases, which are resolved to their
        corresponding, unique `Resource` implementations.
    yes : bool
        Whether to auto-accept prompts during host bootstrap stages.

    Raises
    ------
    OSError
        If Git is not found, or the project root repository is invalid.
    ValueError
        If any resource names in `enable`/`disable` are invalid, or if required
        core resources are disabled.
    """
    if path is None and (enable or disable):
        raise OSError(
            "Cannot globally enable or disable resources without a worktree.  Please "
            "specify at least a repository path to configure resources within it."
        )
    if timeout <= 0:
        raise TimeoutError("timed out before checking host bootstrap")

    # bootstrap host runtime control plane (persistent, system-wide)
    async with Lock(
        INIT_LOCK,
        timeout=timeout,
        mode="local",
        privileges=INIT_LOCK_MODE
    ):
        state = InitState.load()
        index = next(
            (i for i, (stage, _) in enumerate(INIT_STAGES) if stage == state.stage),
            0
        )
        if index == len(INIT_STAGES) - 1:
            try:
                await _assert_installed(state, yes)
            except OSError:  # reported as finished, but runtime is not actually installed
                index = 0
                state = InitState(version=INIT_STATE_VERSION)
                if InitState.backend_trustworthy():
                    state.dump()
        for stage, step in INIT_STAGES[index:]:
            await step(state, yes)
            state.stage = stage
            if InitState.backend_trustworthy():
                state.dump()

        if state.user is None:
            raise OSError("init state user is missing; rerun `bertrand init`.")
        for group, purpose in (
            (BERTRAND_GROUP, "shared Bertrand host-state access"),
            ("microceph", "MicroCeph CLI/storage access"),
            ("microk8s", "MicroK8s runtime access"),
        ):
            status = GroupStatus.get(state.user, group)
            if not status.configured:
                raise OSError(
                    f"user {state.user!r} is not in {group!r}.  Rerun `bertrand init` "
                    f"to configure {purpose}."
                )
            if not status.active:
                raise OSError(
                    f"user {state.user!r} is in {group!r}, but the current session is "
                    f"not active in that group.  Run `newgrp {group}` or log out and "
                    "back in, then rerun `bertrand init`."
                )

        # start both clusters if they are not already running, and link them via rook-ceph
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        await start_microceph(timeout=deadline - loop.time())
        await start_microk8s(timeout=deadline - loop.time())
        await link_kube_ceph(timeout=deadline - loop.time())

    # if no project root is provided, then we're done
    if path is None:
        return

    # fail fast if required tools are missing, and validate resource convergence input
    enabled: set[Resource] = {RESOURCE_NAMES["bertrand"]}
    enabled.update(_parse_resource_specs(enable, for_disable=False))
    disabled = _parse_resource_specs(disable, for_disable=True)
    protected = {
        name
        for name in PROTECTED_DISABLE_RESOURCES
        if RESOURCE_NAMES[name] in disabled
    }
    if protected:
        raise ValueError(
            f"cannot disable required Bertrand resources: {', '.join(sorted(protected))}"
        )
    enabled -= disabled

    # resolve path to parent git repository and relative worktree
    if not shutil.which("git"):
        raise OSError(
            "Bertrand requires 'git' to initialize a project repository, but it "
            "was not found in PATH."
        )
    raw_path = abspath(path)

    # run managed-alias resurrection before git path resolution so repository
    # discovery sees the recovered hidden mount layout if one was detached
    resurrected = await _resurrect_mount(
        raw_path,
        timeout=deadline - loop.time(),
    )
    recovered_repo_id: UUIDHex | None = None
    if resurrected is not None:
        recovered_repo_id, _ = resurrected

    # TODO: I think I can avoid a redundant scan of the parent directories by refining
    # the `_resurrect_mount` hook and related helper in mount.py

    # search for parent Git repository on target path, and then identify the ancestor
    # symlink that points to it, if any
    repo, worktree = await GitRepository.resolve(raw_path.resolve())
    target: Path | None = None
    for candidate in (raw_path, *raw_path.parents):
        try:
            if candidate.resolve() == repo.root:
                target = candidate
                break
        except OSError:
            pass
    if target is None:
        raise OSError(
            f"failed to derive repository destination path from {raw_path}; no "
            f"ancestor resolves to repository root {repo.root}"
        )

    # synchronize uniquely for each repository path to limit global init lock contention
    async with RepoState.lock(repo.root, timeout=deadline - loop.time()):
        if repo and await repo.dirty():
            raise OSError(
                f"repository at {repo.root} has uncommitted changes; please commit or "
                "stash them before calling `bertrand init`."
            )

        # resolve deterministic repository identity from managed metadata if available,
        # otherwise derive from the canonical repository root.
        repo_id = recovered_repo_id or _resolve_repo_id(repo)
        state = RepoState(
            repo=repo,
            target=target,
            worktree=worktree,
            repo_id=repo_id,
            volume=None,
            ceph_path=None,
            mount_alias=None,
            credentials=None,
            deadline=deadline,
        )

        # execute all idempotent convergence stages in sequence, allowing recovery from
        # previous runs
        for stage in REPO_STAGES:
            await stage(state, enabled, disabled, yes)
