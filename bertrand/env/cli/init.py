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
    RepoMount,
    ensure_repo_credentials,
    secretfile,
)
from ..config import RESOURCE_NAMES, Config, Resource
from ..config.core import AbsolutePosixPath, NonEmpty, Trimmed, UUIDHex, _check_uuid
from ..kube import DEFAULT_VOLUME_SIZE, RepoVolume
from ..run import (
    BERTRAND_GROUP,
    METADATA_REPO_ID,
    REPO_DIR,
    REPO_MOUNT_EXT,
    STATE_DIR,
    TIMEOUT,
    CommandError,
    GitRepository,
    GroupStatus,
    Lock,
    User,
    assert_microceph_installed,
    assert_microk8s_installed,
    assert_nerdctl_installed,
    atomic_symlink,
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
)

# pylint: disable=unused-argument, missing-function-docstring, missing-return-doc
# pylint: disable=bare-except, broad-exception-caught


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
        timeout=None,
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


# TODO: repo checkpoints should probably be stored in the tmpfs RUN_DIR to make
# garbage collection easy.


REPO_CHECKPOINT_VERSION = 1
REPO_CHECKPOINT_DIR = STATE_DIR / "repo-conversions"


type RepoCheckpointPhase = Literal[
    "repo_id",
    "volume_ready",
    "mount_ready",
    "layout_ready",
    "cutover_ready",
    "complete",
]


class RepoCheckpoint(BaseModel):
    """Persistent resume checkpoint for repository convergence.

    Attributes
    ----------
    version : int
        Schema version for this checkpoint payload.
    source_path : str
        Absolute path where convergence started for this repository.
    source_git_dir : str
        Absolute git-common-dir path used as this checkpoint's stable key.
    source_dev : int | None
        Device ID fingerprint for stale-check detection.
    source_ino : int | None
        Inode fingerprint for stale-check detection.
    repo_id : str
        Repository UUID currently assigned to this convergence run.
    claim_name : str | None
        Resolved claim name in the cluster, when known.
    ceph_path : AbsolutePosixPath | None
        Resolved CephFS path for the repository claim, when known.
    phase : RepoCheckpointPhase
        Most recently completed convergence phase.
    """
    model_config = ConfigDict(extra="forbid")
    version: PositiveInt
    source_path: str
    source_git_dir: str
    source_dev: int | None = None
    source_ino: int | None = None
    repo_id: UUIDHex
    claim_name: str | None = None
    ceph_path: AbsolutePosixPath | None = None
    phase: RepoCheckpointPhase = "repo_id"


@dataclass
class RepoState:
    """In-memory convergence state shared by repository bootstrap stages.

    Attributes
    ----------
    path : Path
        Absolute target path passed to `bertrand init`.
    deadline : float | None
        Absolute monotonic deadline for repository convergence, or None if unbounded.
    repo : GitRepository
        Repository handle resolved from the target path.
    worktree : Path
        Relative worktree path resolved from the target path.
    repo_id : str | None
        Repository UUID assigned to this convergence run.
    volume : RepoVolume | None
        Repository PVC resolved or provisioned for `repo_id`.
    ceph_path : AbsolutePosixPath | None
        CephFS path resolved from the selected PVC/PV.
    credentials : RepoCredentials | None
        Repository-scoped Ceph credentials for host mounting.
    mount_alias : Path | None
        Host alias path currently used for mount convergence.
    new_repo : bool
        True when the target path does not currently resolve to an initialized
        repository.
    """
    path: Path
    deadline: float | None
    repo: GitRepository
    worktree: Path
    repo_id: UUIDHex | None = None
    volume: RepoVolume | None = None
    ceph_path: AbsolutePosixPath | None = None
    credentials: RepoCredentials | None = None
    mount_alias: Path | None = None
    new_repo: bool = False


def _remaining_timeout(deadline: float | None, now: float) -> float | None:
    """Compute stage-local timeout from an absolute convergence deadline."""
    if deadline is None:
        return None
    return max(0.0, deadline - now)


def _checkpoint_path(source_git_dir: Path) -> Path:
    """Return deterministic checkpoint path for a source git directory."""
    digest = hashlib.sha256(str(source_git_dir).encode("utf-8")).hexdigest()
    return REPO_CHECKPOINT_DIR / f"{digest}.json"


def _load_repo_checkpoint(path: Path) -> RepoCheckpoint | None:
    """Load a repository checkpoint from disk.

    Parameters
    ----------
    path : Path
        Checkpoint JSON path.

    Returns
    -------
    RepoCheckpoint | None
        Parsed checkpoint, or None if the file does not exist or is malformed.
    """
    if not path.exists() or not path.is_file():
        return None
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(data, dict):
            raise ValueError("checkpoint JSON must be an object")
        checkpoint = RepoCheckpoint.model_validate(data)
    except Exception:
        return None
    if checkpoint.version != REPO_CHECKPOINT_VERSION:
        return None
    return checkpoint


def _write_repo_checkpoint(path: Path, checkpoint: RepoCheckpoint) -> None:
    """Persist one repository checkpoint atomically."""
    atomic_write_text(
        path,
        json.dumps(checkpoint.model_dump(mode="json"), separators=(",", ":")) + "\n",
        encoding="utf-8",
    )


def _is_checkpoint_stale(
    checkpoint: RepoCheckpoint,
    *,
    source_path: Path,
    source_git_dir: Path,
    source_dev: int | None,
    source_ino: int | None,
) -> bool:
    """Return True when a checkpoint does not match current source identity."""
    if checkpoint.source_path != str(source_path):
        return True
    if checkpoint.source_git_dir != str(source_git_dir):
        return True
    if (
        checkpoint.source_dev is not None and
        source_dev is not None and
        checkpoint.source_dev != source_dev
    ):
        return True
    if (
        checkpoint.source_ino is not None and
        source_ino is not None and
        checkpoint.source_ino != source_ino
    ):
        return True
    return False


async def _resolve_repo(
    state: RepoState,
) -> tuple[RepoCheckpoint | None, Path, Path, int | None, int | None]:
    """Resolve repository target and load matching resume checkpoint.

    Parameters
    ----------
    state : RepoState
        Mutable convergence state to populate with resolved repository data.

    Returns
    -------
    tuple[RepoCheckpoint | None, Path, Path, int | None, int | None]
        Checkpoint (if valid), source path, source git-dir, source device ID, and
        source inode ID used for stale-check matching.
    """
    repo, worktree = await GitRepository.resolve(state.path)
    state.repo = repo
    state.worktree = worktree
    state.new_repo = not repo
    if state.new_repo and (state.path.exists() or state.path.is_symlink()):
        raise OSError(
            f"cannot initialize repository at {state.path!r}: path is already occupied "
            "by an existing filesystem entry"
        )

    source_path = repo.root if not state.new_repo else state.path
    source_git_dir = repo.git_dir
    source_dev: int | None = None
    source_ino: int | None = None
    stat_target = source_git_dir if source_git_dir.exists() else source_path
    if stat_target.exists():
        stat_info = stat_target.stat()
        source_dev = int(stat_info.st_dev)
        source_ino = int(stat_info.st_ino)

    checkpoint_path = _checkpoint_path(source_git_dir)
    checkpoint = _load_repo_checkpoint(checkpoint_path)
    if checkpoint is not None and _is_checkpoint_stale(
        checkpoint,
        source_path=source_path,
        source_git_dir=source_git_dir,
        source_dev=source_dev,
        source_ino=source_ino,
    ):
        checkpoint = None
    if checkpoint is not None and checkpoint.phase == "complete":
        checkpoint_path.unlink(missing_ok=True)
        checkpoint = None
    if checkpoint is not None:
        state.repo_id = checkpoint.repo_id
        state.ceph_path = checkpoint.ceph_path
    return checkpoint, checkpoint_path, source_path, source_dev, source_ino


async def _ensure_repo_id(
    state: RepoState,
    *,
    checkpoint: RepoCheckpoint | None,
    checkpoint_path: Path,
    source_path: Path,
    source_git_dir: Path,
    source_dev: int | None,
    source_ino: int | None,
) -> RepoCheckpoint:
    """Ensure a repository UUID is available and persisted to checkpoint state."""
    repo_id_file = state.repo.root / METADATA_REPO_ID
    if repo_id_file.is_file():
        try:
            state.repo_id = _check_uuid(repo_id_file.read_text(encoding="utf-8").strip())
        except ValueError:
            state.repo_id = None
    if state.repo_id is None and checkpoint is not None:
        state.repo_id = checkpoint.repo_id
    if state.repo_id is None:
        state.repo_id = _check_uuid(uuid.uuid4().hex)
    checkpoint = RepoCheckpoint(
        version=REPO_CHECKPOINT_VERSION,
        source_path=str(source_path),
        source_git_dir=str(source_git_dir),
        source_dev=source_dev,
        source_ino=source_ino,
        repo_id=state.repo_id,
        claim_name=checkpoint.claim_name if checkpoint is not None else None,
        ceph_path=state.ceph_path,
        phase="repo_id",
    )
    _write_repo_checkpoint(checkpoint_path, checkpoint)
    return checkpoint


async def _ensure_repo_volume(
    state: RepoState,
    *,
    yes: bool,
    checkpoint: RepoCheckpoint,
    checkpoint_path: Path,
    source_path: Path,
    source_git_dir: Path,
    source_dev: int | None,
    source_ino: int | None,
) -> RepoCheckpoint:
    """Ensure repository volume claim and Ceph path converge for the current repo ID."""
    if state.repo_id is None:
        raise OSError("repository ID is missing before volume convergence")
    loop = asyncio.get_running_loop()
    deadline = state.deadline
    mount = None if state.new_repo else MountInfo.search(state.repo.root)

    while True:
        volumes = await RepoVolume.get(
            state.repo_id,
            timeout=_remaining_timeout(deadline, loop.time()),
        )
        matched_volume: RepoVolume | None = None
        matched_path: AbsolutePosixPath | None = None
        if mount is not None and mount.is_cephfs():
            for volume in volumes:
                try:
                    candidate = await volume.resolve_ceph_path(
                        timeout=_remaining_timeout(deadline, loop.time()),
                    )
                except (OSError, TimeoutError, CommandError):
                    continue
                if mount.references_ceph_path(candidate):
                    matched_volume = volume
                    matched_path = candidate
                    break

        if matched_volume is not None and matched_path is not None:
            state.volume = matched_volume
            state.ceph_path = matched_path
            break

        if volumes:
            # Existing claims for this repo_id do not match the source mount identity;
            # rotate to a fresh repo_id and retry claim convergence.
            state.repo_id = _check_uuid(uuid.uuid4().hex)
            checkpoint = RepoCheckpoint(
                version=REPO_CHECKPOINT_VERSION,
                source_path=str(source_path),
                source_git_dir=str(source_git_dir),
                source_dev=source_dev,
                source_ino=source_ino,
                repo_id=state.repo_id,
                claim_name=None,
                ceph_path=None,
                phase="repo_id",
            )
            _write_repo_checkpoint(checkpoint_path, checkpoint)
            continue

        if not state.new_repo and not confirm(
            "Bertrand found an existing unmanaged repository and needs to allocate a "
            "managed CephFS volume before conversion. Continue?\n[y/N] ",
            assume_yes=yes,
        ):
            raise PermissionError("repository conversion declined by user")

        state.volume = await RepoVolume.create(
            repo_id=state.repo_id,
            timeout=_remaining_timeout(deadline, loop.time()),
            size_request=DEFAULT_VOLUME_SIZE,
        )
        state.ceph_path = await state.volume.resolve_ceph_path(
            timeout=_remaining_timeout(deadline, loop.time()),
        )
        break

    checkpoint = RepoCheckpoint(
        version=REPO_CHECKPOINT_VERSION,
        source_path=str(source_path),
        source_git_dir=str(source_git_dir),
        source_dev=source_dev,
        source_ino=source_ino,
        repo_id=state.repo_id,
        claim_name=state.volume.pvc.metadata.name if state.volume is not None else None,
        ceph_path=state.ceph_path,
        phase="volume_ready",
    )
    _write_repo_checkpoint(checkpoint_path, checkpoint)
    return checkpoint


async def _ensure_repo_credentials(state: RepoState) -> None:
    """Ensure Ceph credentials exist for current repository identity."""
    if state.repo_id is None or state.ceph_path is None:
        raise OSError("repo volume must be resolved before credential convergence")
    loop = asyncio.get_running_loop()
    state.credentials = await ensure_repo_credentials(
        repo_id=state.repo_id,
        ceph_path=state.ceph_path,
        timeout=_remaining_timeout(state.deadline, loop.time()),
    )


async def _ensure_host_mount(
    state: RepoState,
    *,
    checkpoint: RepoCheckpoint,
    checkpoint_path: Path,
    source_path: Path,
    source_git_dir: Path,
    source_dev: int | None,
    source_ino: int | None,
) -> RepoCheckpoint:
    """Ensure host mount is attached for this repository identity."""
    if state.repo_id is None or state.ceph_path is None or state.credentials is None:
        raise OSError("host mount convergence requires repo ID, path, and credentials")
    loop = asyncio.get_running_loop()
    target = state.repo.root
    if target.exists() and not target.is_symlink():
        state.mount_alias = target.parent / f".{target.name}.bertrand.mount.{state.repo_id}"
    else:
        state.mount_alias = target
    with secretfile(state.credentials) as ceph_secretfile:
        await RepoMount(repo_id=state.repo_id, ceph_path=state.ceph_path).mount(
            state.mount_alias,
            timeout=_remaining_timeout(state.deadline, loop.time()),
            monitors=state.credentials.monitors,
            ceph_user=state.credentials.entity.removeprefix("client."),
            ceph_secretfile=ceph_secretfile,
        )
    checkpoint = RepoCheckpoint(
        version=REPO_CHECKPOINT_VERSION,
        source_path=str(source_path),
        source_git_dir=str(source_git_dir),
        source_dev=source_dev,
        source_ino=source_ino,
        repo_id=state.repo_id,
        claim_name=checkpoint.claim_name,
        ceph_path=state.ceph_path,
        phase="mount_ready",
    )
    _write_repo_checkpoint(checkpoint_path, checkpoint)
    return checkpoint


async def _ensure_bare_worktrees(
    state: RepoState,
    *,
    yes: bool,
    checkpoint: RepoCheckpoint,
    checkpoint_path: Path,
    source_path: Path,
    source_git_dir: Path,
    source_dev: int | None,
    source_ino: int | None,
) -> RepoCheckpoint:
    """Converge repository to Bertrand's bare+worktree layout."""
    if state.mount_alias is None or state.repo_id is None or state.ceph_path is None:
        raise OSError("bare-worktree convergence requires mounted repository state")
    if state.credentials is None:
        raise OSError("bare-worktree convergence requires repository credentials")
    loop = asyncio.get_running_loop()
    deadline = state.deadline
    destination_root = state.mount_alias
    destination_repo = GitRepository(destination_root / ".git")
    source_repo = state.repo

    if state.new_repo:
        if not destination_repo:
            branch = state.worktree.as_posix()
            await destination_repo.init(branch=branch, bare=True)
            await destination_repo.create_worktree(
                branch,
                target=destination_root / state.worktree,
                create_branch=True,
            )
        state.repo = destination_repo
        checkpoint = RepoCheckpoint(
            version=REPO_CHECKPOINT_VERSION,
            source_path=str(source_path),
            source_git_dir=str(source_git_dir),
            source_dev=source_dev,
            source_ino=source_ino,
            repo_id=state.repo_id,
            claim_name=checkpoint.claim_name,
            ceph_path=state.ceph_path,
            phase="layout_ready",
        )
        _write_repo_checkpoint(checkpoint_path, checkpoint)
        return checkpoint

    if destination_root != source_repo.root:
        if not confirm(
            "Bertrand needs to rewrite this repository into a managed bare/worktree "
            "layout and atomically replace the original path with a CephFS alias. "
            "Continue?\n[y/N] ",
            assume_yes=yes,
        ):
            raise PermissionError("repository layout conversion declined by user")

        clean = await run(
            ["git", "-C", str(source_repo.root), "status", "--porcelain"],
            capture_output=True,
            timeout=_remaining_timeout(deadline, loop.time()),
        )
        if clean.stdout.strip():
            raise OSError(
                f"cannot convert repository at {source_repo.root}: worktree has "
                "uncommitted changes"
            )

        if not destination_repo:
            await run(
                ["git", "clone", "--mirror", str(source_repo.root), str(destination_repo.git_dir)],
                capture_output=True,
                timeout=_remaining_timeout(deadline, loop.time()),
            )
            destination_repo = GitRepository(destination_root / ".git")
        else:
            await run(
                [
                    "git",
                    "--git-dir", str(destination_repo.git_dir),
                    "fetch",
                    "--prune",
                    str(source_repo.root),
                    "+refs/*:refs/*",
                ],
                capture_output=True,
                timeout=_remaining_timeout(deadline, loop.time()),
            )
        await destination_repo.sync_worktrees()

        checkpoint = RepoCheckpoint(
            version=REPO_CHECKPOINT_VERSION,
            source_path=str(source_path),
            source_git_dir=str(source_git_dir),
            source_dev=source_dev,
            source_ino=source_ino,
            repo_id=state.repo_id,
            claim_name=checkpoint.claim_name,
            ceph_path=state.ceph_path,
            phase="cutover_ready",
        )
        _write_repo_checkpoint(checkpoint_path, checkpoint)

        hidden_mount = REPO_DIR / state.repo_id / REPO_MOUNT_EXT
        original = source_repo.root
        staged_link = original.parent / f".{original.name}.bertrand.link.{state.repo_id}.tmp"
        backup = original.parent / f".{original.name}.bertrand.backup.{state.repo_id}"
        if backup.exists() or backup.is_symlink():
            raise FileExistsError(
                f"cannot cut over repository at {original}: backup path already exists "
                f"({backup})"
            )
        atomic_symlink(hidden_mount, staged_link)
        original.rename(backup)
        try:
            staged_link.rename(original)
        except Exception as err:
            backup.rename(original)
            staged_link.unlink(missing_ok=True)
            raise OSError(f"failed to atomically swap repository path at {original}") from err

        # Re-run mount convergence so alias registration reflects the final path.
        with secretfile(state.credentials) as ceph_secretfile:
            await RepoMount(repo_id=state.repo_id, ceph_path=state.ceph_path).mount(
                original,
                timeout=_remaining_timeout(deadline, loop.time()),
                monitors=state.credentials.monitors,
                ceph_user=state.credentials.entity.removeprefix("client."),
                ceph_secretfile=ceph_secretfile,
            )
        if state.mount_alias != original:
            try:
                await RepoMount(repo_id=state.repo_id, ceph_path=state.ceph_path).unmount(
                    state.mount_alias,
                    timeout=_remaining_timeout(deadline, loop.time()),
                    force=False,
                    lazy=False,
                )
            except OSError:
                state.mount_alias.unlink(missing_ok=True)
        try:
            shutil.rmtree(backup)
        except OSError as err:
            print(
                f"bertrand: warning: failed to delete conversion backup at {backup}: {err}",
                file=sys.stderr,
            )
        state.mount_alias = original
        state.repo = GitRepository(original / ".git")
    else:
        if destination_repo and await destination_repo.is_bare():
            await destination_repo.sync_worktrees()
            state.repo = destination_repo

    checkpoint = RepoCheckpoint(
        version=REPO_CHECKPOINT_VERSION,
        source_path=str(source_path),
        source_git_dir=str(source_git_dir),
        source_dev=source_dev,
        source_ino=source_ino,
        repo_id=state.repo_id,
        claim_name=checkpoint.claim_name,
        ceph_path=state.ceph_path,
        phase="layout_ready",
    )
    _write_repo_checkpoint(checkpoint_path, checkpoint)
    return checkpoint


async def _ensure_repo_hooks(state: RepoState) -> None:
    """Write repository metadata and install managed git hooks."""
    if state.repo_id is None:
        raise OSError("repository ID is missing before hook convergence")
    atomic_write_text(state.repo.root / METADATA_REPO_ID, state.repo_id, encoding="utf-8")
    await _install_git_hooks(state.repo)


async def _install_git_hooks(repo: GitRepository) -> None:
    # check if repo is not initialized
    if not repo:
        print(f"bertrand: invalid git directory at {repo.git_dir}", file=sys.stderr)
        return

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
                    f"packaged {hook.source} must start with:\n"
                    f"{'\\n'.join(expected)}"
                )

            # do not clobber non-managed hooks
            stage = f"resolve existing git hook at '{hook.destination}'"
            target = await repo.git_path(hook.destination, cwd=repo.root)
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
            if hook.executable:
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


async def _render_worktree(
    repo: GitRepository,
    worktree: Path,
    *,
    resources: set[Resource],
    timeout: float | None,
) -> None:
    """Render all enabled resources into the selected repository worktree.

    Parameters
    ----------
    repo : GitRepository
        Repository whose worktree should receive generated artifacts.
    worktree : Path
        Worktree path relative to `repo.root`.
    resources : set[Resource]
        Resources selected by CLI flags for activation in this worktree.
    timeout : float | None
        Maximum render timeout in seconds.  If None, wait indefinitely.
    """

    # reconcile with existing configuration (if any)
    config = await Config.load(  # locate existing in-tree resources
        repo.root / worktree,
        repo=repo,
        timeout=timeout
    )
    config.resources.update({r.name: None for r in resources})  # merge any new resources from CLI
    config.init = Config.Init(
        repo=repo,
        worktree=worktree,
    )
    async with config:  # initialize defaults, load overrides, and validate resources
        await config.sync(tag=None)  # render in-tree resources with validated config


async def _render_config_artifacts(
    state: RepoState,
    *,
    resources: set[Resource],
) -> None:
    """Converge rendered configuration artifacts for the current worktree."""
    loop = asyncio.get_running_loop()
    await _render_worktree(
        state.repo,
        state.worktree,
        resources=resources,
        timeout=_remaining_timeout(state.deadline, loop.time()),
    )


async def _make_initial_commit(state: RepoState) -> None:
    """Create initial commit when repository is empty and staged diff is non-empty."""
    loop = asyncio.get_running_loop()
    worktree_path = state.repo.root / state.worktree
    head = await run(
        ["git", "rev-parse", "--verify", "HEAD"],
        cwd=worktree_path,
        check=False,
        capture_output=True,
        timeout=_remaining_timeout(state.deadline, loop.time()),
    )
    if head.returncode == 0:
        return  # repository already has commits
    if head.returncode != 128:
        raise OSError(
            "failed to determine whether repository already has commits:\n"
            f"{head.stdout}\n{head.stderr}".strip()
        )

    await run(
        ["git", "add", "-A"],
        cwd=worktree_path,
        capture_output=True,
        timeout=_remaining_timeout(state.deadline, loop.time()),
    )
    staged = await run(
        ["git", "diff", "--cached", "--quiet"],
        cwd=worktree_path,
        check=False,
        capture_output=True,
        timeout=_remaining_timeout(state.deadline, loop.time()),
    )
    if staged.returncode == 0:
        return  # nothing staged after render
    if staged.returncode != 1:
        raise OSError(
            "failed to inspect staged diff for initial commit:\n"
            f"{staged.stdout}\n{staged.stderr}".strip()
        )
    await run(
        ["git", "commit", "--quiet", "-m", "Initial commit"],
        cwd=worktree_path,
        capture_output=True,
        timeout=_remaining_timeout(state.deadline, loop.time()),
    )


async def _finalize(
    *,
    checkpoint: RepoCheckpoint,
    checkpoint_path: Path,
) -> None:
    """Mark convergence complete and remove resume checkpoint."""
    complete = checkpoint.model_copy(update={"phase": "complete"})
    _write_repo_checkpoint(checkpoint_path, complete)
    checkpoint_path.unlink(missing_ok=True)


async def _converge_repository(
    path: Path,
    *,
    resources: set[Resource],
    timeout: float | None,
    yes: bool,
) -> tuple[GitRepository, Path, bool]:
    """Run repository convergence stages in a fixed forward order.

    Parameters
    ----------
    path : Path
        Absolute target path passed to `bertrand init`.
    resources : set[Resource]
        Resource set selected by CLI flags.
    timeout : float | None
        Maximum total convergence timeout in seconds.  If None, wait indefinitely.
    yes : bool
        Whether confirmation prompts should be auto-accepted.

    Returns
    -------
    tuple[GitRepository, Path, bool]
        Converged repository object, selected worktree, and whether the repository was
        newly initialized in this run.
    """
    loop = asyncio.get_running_loop()
    deadline = None if timeout is None else loop.time() + timeout
    state = RepoState(
        path=path,
        deadline=deadline,
        repo=GitRepository(path / ".git"),
        worktree=Path("."),
    )

    # resolve_repo
    checkpoint, checkpoint_path, source_path, source_dev, source_ino = await _resolve_repo(state)

    # ensure_repo_id
    checkpoint = await _ensure_repo_id(
        state,
        checkpoint=checkpoint,
        checkpoint_path=checkpoint_path,
        source_path=source_path,
        source_git_dir=state.repo.git_dir,
        source_dev=source_dev,
        source_ino=source_ino,
    )

    # ensure_repo_volume
    checkpoint = await _ensure_repo_volume(
        state,
        yes=yes,
        checkpoint=checkpoint,
        checkpoint_path=checkpoint_path,
        source_path=source_path,
        source_git_dir=state.repo.git_dir,
        source_dev=source_dev,
        source_ino=source_ino,
    )

    # ensure_repo_credentials
    await _ensure_repo_credentials(state)

    # ensure_host_mount
    checkpoint = await _ensure_host_mount(
        state,
        checkpoint=checkpoint,
        checkpoint_path=checkpoint_path,
        source_path=source_path,
        source_git_dir=state.repo.git_dir,
        source_dev=source_dev,
        source_ino=source_ino,
    )

    # ensure_bare_worktrees
    checkpoint = await _ensure_bare_worktrees(
        state,
        yes=yes,
        checkpoint=checkpoint,
        checkpoint_path=checkpoint_path,
        source_path=source_path,
        source_git_dir=state.repo.git_dir,
        source_dev=source_dev,
        source_ino=source_ino,
    )

    # ensure_repo_hooks
    await _ensure_repo_hooks(state)

    # render_config_artifacts
    await _render_config_artifacts(state, resources=resources)

    # make_initial_commit
    await _make_initial_commit(state)

    # finalize
    await _finalize(checkpoint=checkpoint, checkpoint_path=checkpoint_path)
    return state.repo, state.worktree, state.new_repo


###################
####    CLI    ####
###################


# TODO: I should make sure that any user group (microk8s, microceph, bertrand) is
# not just configured, but active for the current user, and warn consistently, outside
# of the main init loop, so that it always warns on every init until those privileges
# are activated.


async def bertrand_init(
    path: Path | None,
    *,
    timeout: float | None,
    enable: list[str],
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
    timeout : float | None
        Time (in seconds) to wait for the repository to become available with the
        expected configuration.  If None, then wait indefinitely.  Note that this does
        not apply to any host bootstrapping stages, which may require user confirmation,
        and are only run once per host (not per repository).
    enable : list[str]
        List of resources to enable at the resolved worktree.  Each component is a
        comma-separated list of resource names or aliases, which are resolved to their
        corresponding, unique `Resource` implementations.
    yes : bool
        Whether to auto-accept prompts during host bootstrap stages.

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
    if timeout is not None and timeout <= 0:
        raise TimeoutError("timed out before checking host bootstrap")

    # bootstrap runtime control plane if needed
    async with Lock(
        INIT_LOCK,
        timeout=TIMEOUT,
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

        # run any unfinished stages
        for stage, step in INIT_STAGES[index:]:
            await step(state, yes)
            state.stage = stage
            if InitState.backend_trustworthy():
                state.dump()

    # if no project root is provided, then we're done
    if path is None:
        return
    path = path.expanduser().resolve()
    loop = asyncio.get_running_loop()
    deadline = None if timeout is None else loop.time() + timeout

    # fail fast if required tools are missing, and validate the resources to enable at
    # the worktree path
    if not shutil.which("git"):
        raise OSError(
            "Bertrand requires 'git' to initialize a project repository, but it was "
            "not found in PATH."
        )
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

    # serialize runtime + repository convergence so checkpoint updates have one writer.
    async with Lock(
        INIT_LOCK,
        timeout=TIMEOUT,
        mode="local",
        privileges=INIT_LOCK_MODE,
    ):
        # start both clusters if they are not already running, and link them via rook-ceph
        await start_microceph(timeout=None if deadline is None else deadline - loop.time())
        await start_microk8s(timeout=None if deadline is None else deadline - loop.time())
        await link_kube_ceph(timeout=None if deadline is None else deadline - loop.time())

        # converge repository state onto Bertrand's managed CephFS-backed layout
        await _converge_repository(
            path,
            resources=resources,
            timeout=None if deadline is None else deadline - loop.time(),
            yes=yes,
        )
