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
from typing import Annotated, Literal, Self

from pydantic import BaseModel, ConfigDict, Field, PositiveInt

from ..ceph import (
    MountInfo,
    RepoCredentials,
    RepoMount,
)
from ..config import RESOURCE_NAMES, Config, Resource
from ..config.core import (
    AbsolutePath,
    AbsolutePosixPath,
    NonEmpty,
    Trimmed,
    UUIDHex,
    _check_uuid,
)
from ..kube import DEFAULT_VOLUME_SIZE, RepoVolume
from ..run import (
    BERTRAND_GROUP,
    METADATA_REPO_ID,
    REPO_DIR,
    REPO_MOUNT_EXT,
    RUN_DIR,
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


def _norm_path(path: Path) -> Path:
    return Path(os.path.abspath(path.expanduser()))


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
REPO_CHECKPOINT_VERSION = 1
REPO_CHECKPOINT_DIR = RUN_DIR / "init"


@dataclass
class RepoState:
    """In-memory convergence state shared by repository bootstrap stages.

    Attributes
    ----------
    target : AbsolutePath
        Absolute repository destination path where the managed mount alias should end
        up.  This is normalized from user input without resolving symlinks.
    deadline : float | None
        Absolute monotonic deadline for repository convergence, or None if unbounded.
    repo : GitRepository
        Repository handle resolved from the target path.
    worktree : Path
        Relative worktree path resolved from the target path.
    checkpoint : RepoState.Checkpoint
        Persistent checkpoint state for this repository convergence run.
    volume : RepoVolume | None
        Repository PVC resolved or provisioned for `repo_id`.
    credentials : RepoCredentials | None
        Repository-scoped Ceph credentials for host mounting.
    """
    class Checkpoint(BaseModel):
        """Persistent resume checkpoint for repository convergence."""
        model_config = ConfigDict(extra="forbid")
        version: PositiveInt
        root: AbsolutePath
        repo_id: UUIDHex
        git_head_ref: Annotated[str | None, Field(default=None)]
        git_head_oid: Annotated[str | None, Field(default=None)]
        git_refs_digest: Annotated[str | None, Field(default=None)]
        claim_name: Annotated[str | None, Field(default=None)]
        ceph_path: Annotated[AbsolutePosixPath | None, Field(default=None)]
        mount_alias: Annotated[AbsolutePath | None, Field(default=None)]
        cutover_staged_link: Annotated[AbsolutePath | None, Field(default=None)]
        cutover_backup_path: Annotated[AbsolutePath | None, Field(default=None)]

        @staticmethod
        def lock(root: AbsolutePath, timeout: float) -> Lock:
            """
            Parameters
            ----------
            root : AbsolutePath
                Repository root path to get the corresponding lock file for.
            timeout : float
                Timeout for acquiring the lock.

            Returns
            -------
            Lock
                A unique lock object for the given repository root, which can be
                acquired as an async context manager to coordinate exclusive access to
                the checkpoint file.
            """
            digest = hashlib.sha256(str(root).encode("utf-8"))
            return Lock(
                REPO_CHECKPOINT_DIR / f"{digest.hexdigest()}.lock",
                timeout=timeout,
                mode="local",
                privileges=INIT_LOCK_MODE,
            )

        @staticmethod
        def file(root: AbsolutePath) -> AbsolutePath:
            """
            Parameters
            ----------
            root : AbsolutePath
                Repository root path to get the corresponding checkpoint file for.

            Returns
            -------
            AbsolutePath
                An absolute path to the checkpoint file for the given repository root.
            """
            digest = hashlib.sha256(str(root).encode("utf-8"))
            return REPO_CHECKPOINT_DIR / f"{digest.hexdigest()}.json"

        @staticmethod
        async def _git_head_ref(repo: GitRepository) -> str | None:
            head = await repo.run(
                ["symbolic-ref", "--quiet", "HEAD"],
                check=False,
                capture_output=True,
            )
            if head.returncode == 0:
                ref = head.stdout.strip()
                if not ref:
                    raise ValueError("empty symbolic-ref output for HEAD")
                return ref
            if head.returncode == 1:
                return None
            raise CommandError(head.returncode, head.args, head.stdout, head.stderr)

        @staticmethod
        async def _git_head_oid(repo: GitRepository) -> str | None:
            head_oid = await repo.run(
                ["rev-parse", "--verify", "HEAD"],
                check=False,
                capture_output=True,
            )
            if head_oid.returncode == 0:
                out = head_oid.stdout.strip()
                oid = out.lower()
                if len(oid) != 40 or any(char not in "0123456789abcdef" for char in oid):
                    raise ValueError(f"invalid HEAD object ID: {out}")
                return oid
            err = head_oid.stderr.strip().lower()
            unborn_markers = (
                "needed a single revision",
                "unknown revision or path not in the working tree",
                "ambiguous argument 'head'",
            )
            if (
                head_oid.returncode == 128 and
                (not head_oid.stdout.strip() or any(marker in err for marker in unborn_markers))
            ):
                return None
            raise CommandError(head_oid.returncode, head_oid.args, head_oid.stdout, head_oid.stderr)

        @staticmethod
        async def _git_refs_digest(repo: GitRepository) -> str:
            refs = await repo.run(
                ["for-each-ref", "--format=%(refname)%00%(objectname)", "--sort=refname", "refs"],
                capture_output=True,
            )
            return hashlib.sha256(refs.stdout.encode("utf-8")).hexdigest()

        @classmethod
        async def load(cls, repo: GitRepository) -> Self:
            """Create or load a durable checkpoint for a given repository.

            Parameters
            ----------
            repo : GitRepository
                Git Repository handle for the target repo convergence.

            Returns
            -------
            RepoState.Checkpoint
                Loaded or newly created checkpoint for the target repository.
            """
            root = repo.root
            repo_id_file = root / METADATA_REPO_ID
            repo_id: UUIDHex | None = None
            if repo_id_file.is_file():
                try:
                    repo_id = _check_uuid(
                        repo_id_file.read_text(encoding="utf-8").strip()
                    )
                except ValueError:
                    repo_id = None
            if repo_id is None:
                repo_id = _check_uuid(uuid.uuid4().hex)
            git_head_ref = await cls._git_head_ref(repo)
            git_head_oid = await cls._git_head_oid(repo)
            git_refs_digest = await cls._git_refs_digest(repo)
            checkpoint = cls.file(root)
            if not checkpoint.exists():
                return cls.model_construct(
                    version=REPO_CHECKPOINT_VERSION,
                    root=root,
                    repo_id=repo_id,
                    git_head_ref=git_head_ref,
                    git_head_oid=git_head_oid,
                    git_refs_digest=git_refs_digest,
                )
            if not checkpoint.is_file():
                raise FileExistsError(
                    f"checkpoint path {checkpoint} exists but is not a file"
                )
            try:
                self: Self | None = cls.model_validate_json(
                    checkpoint.read_text(encoding="utf-8")
                )
                if (
                    self.version != REPO_CHECKPOINT_VERSION or
                    self.root != root or
                    self.git_head_ref != git_head_ref or
                    self.git_head_oid != git_head_oid or
                    self.git_refs_digest != git_refs_digest
                ):
                    self = None
            except (OSError, ValueError, TypeError):
                self = None
            if self is None:
                checkpoint.unlink(missing_ok=True)
                return cls.model_construct(
                    version=REPO_CHECKPOINT_VERSION,
                    root=root,
                    repo_id=repo_id,
                    git_head_ref=git_head_ref,
                    git_head_oid=git_head_oid,
                    git_refs_digest=git_refs_digest,
                )
            return self

        def dump(self) -> None:
            """Write the checkpoint state to disk.  This should be called after any
            mutation to the checkpoint fields to ensure progress is durable across
            crashes and resumptions.
            """
            atomic_write_text(
                self.file(self.root),
                json.dumps(
                    self.model_dump(mode="json"),
                    separators=(",", ":")
                ) + "\n",
                encoding="utf-8",
            )

    repo: GitRepository
    target: Path
    worktree: Path
    checkpoint: Checkpoint
    volume: RepoVolume | None
    credentials: RepoCredentials | None 
    deadline: float | None

    def __post_init__(self) -> None:
        self.target = _norm_path(self.target)


async def _ensure_repo_volume(
    state: RepoState,
    *,
    yes: bool,
    resources: set[Resource],
) -> None:
    """Converge repository volume allocation and deterministic claim reuse."""
    loop = asyncio.get_running_loop()

    # first, try to recover an existing claim for this deterministic repository ID
    volumes = await RepoVolume.get(
        state.checkpoint.repo_id,
        timeout=None if state.deadline is None else state.deadline - loop.time()
    )
    claimed: RepoVolume | None = None
    if state.checkpoint.claim_name is not None:
        claimed = next((
            volume for volume in volumes
            if volume.pvc.metadata.name == state.checkpoint.claim_name
        ), None)
        if claimed is None and volumes:
            # stale checkpoint claim_name: recover deterministically when possible
            if len(volumes) != 1:
                raise OSError(
                    "checkpointed claim_name no longer exists and repository identity "
                    f"{state.checkpoint.repo_id!r} maps to multiple cluster claims: "
                    f"{', '.join(sorted(volume.pvc.metadata.name for volume in volumes))}"
                )
            claimed = volumes[0]
    elif volumes:
        # fresh checkpoints after finalize lose claim_name; deterministic claim naming
        # should still leave exactly one candidate claim for the repo_id
        if len(volumes) != 1:
            names = ", ".join(sorted(volume.pvc.metadata.name for volume in volumes))
            raise OSError(
                "repository identity maps to multiple cluster claims, but no "
                f"checkpointed claim_name is available to disambiguate "
                f"{state.checkpoint.repo_id!r}: {names}"
            )
        claimed = volumes[0]

    # if we found a claim in the ceph cluster, verify that its internal mount point is
    # either unoccupied or maps to the expected Ceph path for this repository, in which
    # case we can safely reuse it
    if claimed is not None:
        ceph_path = await claimed.resolve_ceph_path(
            timeout=None if state.deadline is None else state.deadline - loop.time()
        )
        hidden_mount = REPO_DIR / state.checkpoint.repo_id / REPO_MOUNT_EXT
        mounted = MountInfo.search(hidden_mount)
        if mounted is not None and not (
            mounted.is_cephfs() and mounted.references_ceph_path(ceph_path)
        ):
            raise OSError(
                f"repository hidden mount {hidden_mount!r} is occupied by "
                f"{mounted.source!r}, expected Ceph source suffix ':{ceph_path}'"
            )
        state.volume = claimed
        state.checkpoint.claim_name = claimed.pvc.metadata.name
        state.checkpoint.ceph_path = ceph_path
        state.checkpoint.dump()
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

    # allocate via create(), then checkpoint results
    state.volume = await RepoVolume.create(
        repo_id=state.checkpoint.repo_id,
        timeout=None if state.deadline is None else state.deadline - loop.time(),
        size_request=DEFAULT_VOLUME_SIZE,
    )
    state.checkpoint.claim_name = state.volume.pvc.metadata.name
    state.checkpoint.ceph_path = await state.volume.resolve_ceph_path(
        timeout=None if state.deadline is None else state.deadline - loop.time()
    )
    state.checkpoint.dump()


async def _ensure_repo_credentials(
    state: RepoState,
    *,
    yes: bool,
    resources: set[Resource],
) -> None:
    """Ensure per-repository Ceph credentials exist, in order to allow mounting the
    volume on the host filesystem.
    """
    loop = asyncio.get_running_loop()
    ceph_path = state.checkpoint.ceph_path
    if ceph_path is None:
        raise OSError("repo volume must be resolved before credential convergence")

    # ensure that we have valid credentials for this repository, creating new ones if
    # necessary
    state.credentials = await RepoCredentials.create(
        repo_id=state.checkpoint.repo_id,
        ceph_path=ceph_path,
        timeout=None if state.deadline is None else state.deadline - loop.time(),
    )


async def _ensure_host_mount(
    state: RepoState,
    *,
    yes: bool,
    resources: set[Resource],
) -> None:
    """Ensure the repository volume is mounted on the host filesystem at a stable path,
    staging the mount until cutover.
    """
    loop = asyncio.get_running_loop()
    if state.checkpoint.ceph_path is None or state.credentials is None:
        raise OSError("host mount convergence requires resolved volume and credentials")

    # get location at which to place the repository symlink, staging changes until the
    # final cutover step to avoid disrupting any source repository
    mount_dir = REPO_DIR / state.checkpoint.repo_id / REPO_MOUNT_EXT
    if (
        (not state.target.exists() and not state.target.is_symlink()) or
        _alias_points_to(state.target, mount_dir)
    ):
        mount_alias = state.target
    else:
        staged = f".{state.target.name}.bertrand.mount.{state.checkpoint.repo_id}"
        mount_alias = state.target.parent / staged

    # mount the repository volume at the internal mount directory, then atomically
    # write a relocatable symlink to the target path, subject to staging.
    with state.credentials.secretfile() as ceph_secretfile:
        await RepoMount(
            repo_id=state.checkpoint.repo_id,
            ceph_path=state.checkpoint.ceph_path
        ).mount(
            mount_alias,
            timeout=None if state.deadline is None else state.deadline - loop.time(),
            monitors=state.credentials.monitors,
            ceph_user=state.credentials.user,
            ceph_secretfile=ceph_secretfile,
        )

    # checkpoint mount alias
    state.checkpoint.mount_alias = mount_alias
    state.checkpoint.dump()


# TODO: Ensuring bare worktrees should be radically simplified if possible, and handled
# in its own refactor pass.


def _alias_points_to(path: Path, target: Path) -> bool:
    if not path.is_symlink():
        return False
    try:
        current = path.readlink()
    except OSError:
        return False
    return current.is_absolute() and current == target


def _same_location(left: Path, right: Path) -> bool:
    left = Path(os.path.abspath(str(left.expanduser())))
    right = Path(os.path.abspath(str(right.expanduser())))
    if left == right:
        return True
    try:
        return left.resolve() == right.resolve()
    except (OSError, RuntimeError):
        return False


async def _ensure_bare_worktrees(
    state: RepoState,
    *,
    yes: bool,
    resources: set[Resource],
) -> None:
    """Converge repository to Bertrand's bare+worktree layout."""
    loop = asyncio.get_running_loop()
    deadline = state.deadline
    ceph_path = state.checkpoint.ceph_path
    mount_alias = state.checkpoint.mount_alias
    if ceph_path is None or mount_alias is None or state.credentials is None:
        raise OSError("bare-worktree convergence requires mounted repository credentials")
    destination_root = mount_alias
    destination_repo = GitRepository(destination_root / ".git")
    source_repo = state.repo
    if (
        not source_repo and
        state.checkpoint.cutover_backup_path is not None
    ):
        resumed_source = GitRepository(state.checkpoint.cutover_backup_path / ".git")
        if resumed_source:
            source_repo = resumed_source

    fresh_init = (
        not source_repo and
        state.checkpoint.cutover_backup_path is None
    )
    if fresh_init:
        if not destination_repo:
            branch = state.worktree.as_posix()
            await destination_repo.init(branch=branch, bare=True)
            await destination_repo.create_worktree(
                branch,
                target=destination_root / state.worktree,
                create_branch=True,
            )
        state.repo = destination_repo
        state.checkpoint.cutover_staged_link = None
        state.checkpoint.cutover_backup_path = None
        state.checkpoint.dump()
        return

    if source_repo is None:
        raise OSError(
            f"cannot resume repository conversion at {state.target}: source repository "
            "state is unavailable"
        )

    if not _same_location(destination_root, source_repo.root):
        if not confirm(
            "Bertrand needs to rewrite this repository into a managed bare/worktree "
            "layout and atomically replace the original path with a CephFS alias. "
            "Continue?\n[y/N] ",
            assume_yes=yes,
        ):
            raise PermissionError("repository layout conversion declined by user")
        if source_repo:
            clean = await run(
                ["git", "-C", str(source_repo.root), "status", "--porcelain"],
                capture_output=True,
                timeout=None if deadline is None else deadline - loop.time(),
            )
            if clean.stdout.strip():
                raise OSError(
                    f"cannot convert repository at {source_repo.root}: worktree has "
                    "uncommitted changes"
                )
        if not destination_repo:
            if not source_repo:
                raise OSError(
                    f"cannot resume repository conversion at {state.checkpoint.root}: "
                    "source repository state is unavailable"
                )
            await run(
                ["git", "clone", "--mirror", str(source_repo.root), str(destination_repo.git_dir)],
                capture_output=True,
                timeout=None if deadline is None else deadline - loop.time(),
            )
            destination_repo = GitRepository(destination_root / ".git")
        elif source_repo:
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
                timeout=None if deadline is None else deadline - loop.time(),
            )
        await destination_repo.sync_worktrees()

        original = state.target
        hidden_mount = REPO_DIR / state.checkpoint.repo_id / REPO_MOUNT_EXT
        staged_link = (
            state.checkpoint.cutover_staged_link
            if state.checkpoint.cutover_staged_link is not None
            else original.parent / f".{original.name}.bertrand.link.{state.checkpoint.repo_id}.tmp"
        )
        backup = (
            state.checkpoint.cutover_backup_path
            if state.checkpoint.cutover_backup_path is not None
            else original.parent / f".{original.name}.bertrand.backup.{state.checkpoint.repo_id}"
        )
        state.checkpoint.cutover_staged_link = staged_link
        state.checkpoint.cutover_backup_path = backup
        state.checkpoint.dump()

        if original.exists() and original.is_symlink() and _alias_points_to(original, hidden_mount):
            pass
        elif original.exists() or original.is_symlink():
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
                if (backup.exists() or backup.is_symlink()) and not original.exists():
                    backup.rename(original)
                staged_link.unlink(missing_ok=True)
                raise OSError(
                    f"failed to atomically swap repository path at {original}"
                ) from err
        elif backup.exists() or backup.is_symlink():
            if not staged_link.exists():
                atomic_symlink(hidden_mount, staged_link)
            elif not _alias_points_to(staged_link, hidden_mount):
                raise OSError(
                    f"staged link {staged_link} exists but does not target expected "
                    f"mount path {hidden_mount}"
                )
            staged_link.rename(original)
        else:
            raise OSError(
                f"cannot resume repository cutover at {original}: neither source path "
                "nor conversion backup is available"
            )

        with state.credentials.secretfile() as ceph_secretfile:
            await RepoMount(repo_id=state.checkpoint.repo_id, ceph_path=ceph_path).mount(
                original,
                timeout=None if deadline is None else deadline - loop.time(),
                monitors=state.credentials.monitors,
                ceph_user=state.credentials.user,
                ceph_secretfile=ceph_secretfile,
            )
        if destination_root != original:
            try:
                await RepoMount(repo_id=state.checkpoint.repo_id, ceph_path=ceph_path).unmount(
                    destination_root,
                    timeout=None if deadline is None else deadline - loop.time(),
                    force=False,
                    lazy=False,
                )
            except OSError:
                destination_root.unlink(missing_ok=True)
        if backup.is_symlink() or backup.is_file():
            try:
                backup.unlink()
            except OSError as err:
                print(
                    f"bertrand: warning: failed to delete conversion backup at {backup}: {err}",
                    file=sys.stderr,
                )
        elif backup.is_dir():
            try:
                shutil.rmtree(backup)
            except OSError as err:
                print(
                    f"bertrand: warning: failed to delete conversion backup at {backup}: {err}",
                    file=sys.stderr,
                )
        staged_link.unlink(missing_ok=True)
        state.repo = GitRepository(original / ".git")
        state.checkpoint.mount_alias = original
        state.checkpoint.dump()
    else:
        if destination_repo and await destination_repo.is_bare():
            await destination_repo.sync_worktrees()
            state.repo = destination_repo

    state.checkpoint.cutover_staged_link = None
    state.checkpoint.cutover_backup_path = None
    state.checkpoint.dump()


# TODO: rendering hooks and config artifacts is complicated, especially if I'm going
# to add a layered `bertrand init` command, where not providing a repository path
# bootstraps the host environment, but does not create a new project.  Providing a
# repository path converges it toward the intended ceph + bare worktree layout, and
# renders the configured state in all branches at once.  Providing a particular branch
# worktree converges the repository-level layout, but only renders the configured state
# in that one worktree, leaving other branches unaffected.


async def _ensure_repo_hooks(
    state: RepoState,
    *,
    yes: bool,
    resources: set[Resource],
) -> None:
    """Write repository metadata and install managed git hooks."""
    atomic_write_text(
        state.repo.root / METADATA_REPO_ID,
        state.checkpoint.repo_id,
        encoding="utf-8",
    )

    # check if repo is not initialized
    if not state.repo:
        print(f"bertrand: invalid git directory at {state.repo.git_dir}", file=sys.stderr)
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
            target = await state.repo.git_path(
                hook.destination,
                cwd=state.repo.root
            )
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
                f"bertrand: failed to {stage} in {state.repo.root}\n{err}",
                file=sys.stderr
            )


async def _render_config_artifacts(
    state: RepoState,
    *,
    yes: bool,
    resources: set[Resource],
) -> None:
    """Converge rendered configuration artifacts for the current worktree."""
    loop = asyncio.get_running_loop()

    # reconcile with existing configuration (if any)
    config = await Config.load(  # locate existing in-tree resources
        state.repo.root / state.worktree,
        repo=state.repo,
        timeout=None if state.deadline is None else state.deadline - loop.time()
    )
    config.resources.update({r.name: None for r in resources})  # merge any new resources from CLI
    config.init = Config.Init(
        repo=state.repo,
        worktree=state.worktree,
    )
    async with config:  # initialize defaults, load overrides, and validate resources
        await config.sync(tag=None)  # render in-tree resources with validated config


async def _make_initial_commit(
    state: RepoState,
    *,
    yes: bool,
    resources: set[Resource],
) -> None:
    """Create initial commit when repository is empty and staged diff is non-empty."""
    loop = asyncio.get_running_loop()
    worktree_path = state.repo.root / state.worktree
    head = await run(
        ["git", "rev-parse", "--verify", "HEAD"],
        cwd=worktree_path,
        check=False,
        capture_output=True,
        timeout=None if state.deadline is None else state.deadline - loop.time(),
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
        timeout=None if state.deadline is None else state.deadline - loop.time(),
    )
    staged = await run(
        ["git", "diff", "--cached", "--quiet"],
        cwd=worktree_path,
        check=False,
        capture_output=True,
        timeout=None if state.deadline is None else state.deadline - loop.time(),
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
        timeout=None if state.deadline is None else state.deadline - loop.time(),
    )


# TODO: try to delete `_finalize` if all it's doing is just unlinking the checkpoint
# file.


async def _finalize(
    state: RepoState,
    *,
    yes: bool,
    resources: set[Resource],
) -> None:
    """Mark convergence complete and remove resume checkpoint."""
    state.checkpoint.file(state.checkpoint.root).unlink(missing_ok=True)


REPO_STAGES: tuple[Callable[..., Awaitable[None]], ...] = (
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

    # bootstrap host runtime control plane (persistent, system-wide)
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
        for stage, step in INIT_STAGES[index:]:
            await step(state, yes)
            state.stage = stage
            if InitState.backend_trustworthy():
                state.dump()

        # start both clusters if they are not already running, and link them via rook-ceph
        loop = asyncio.get_running_loop()
        deadline = None if timeout is None else loop.time() + timeout
        await start_microceph(timeout=None if deadline is None else deadline - loop.time())
        await start_microk8s(timeout=None if deadline is None else deadline - loop.time())
        await link_kube_ceph(timeout=None if deadline is None else deadline - loop.time())

    # if no project root is provided, then we're done
    if path is None:
        return

    # fail fast if required tools are missing, and validate the resources to enable
    # at the worktree path
    resources: set[Resource] = {RESOURCE_NAMES["bertrand"]}
    for spec in enable:
        for component in spec.split(","):
            r = RESOURCE_NAMES.get(component.strip())
            if r is None:
                raise ValueError(
                    f"unknown resource '{component}'.  Options are:\n"
                    f"{'\n'.join(f'    {name}' for name in sorted(RESOURCE_NAMES))}"
                )
            resources.add(r)

    # resolve path to parent git repository and relative worktree
    if not shutil.which("git"):
        raise OSError(
            "Bertrand requires 'git' to initialize a project repository, but it "
            "was not found in PATH."
        )
    raw_path = _norm_path(path)
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
    async with RepoState.Checkpoint.lock(
        repo.root,
        timeout=TIMEOUT if deadline is None else deadline - loop.time()
    ):
        if repo and await repo.dirty():
            raise OSError(
                f"repository at {repo.root} has uncommitted changes; please commit or "
                "stash them before calling `bertrand init`."
            )

        # create or load checkpoint for this repository path
        checkpoint = await RepoState.Checkpoint.load(repo)
        state = RepoState(
            repo=repo,
            target=target,
            worktree=worktree,
            checkpoint=checkpoint,
            volume=None,
            credentials=None,
            deadline=deadline,
        )

        # execute all idempotent convergence stages in sequence, allowing recovery from
        # previous runs
        for stage in REPO_STAGES:
            await stage(state, yes=yes, resources=resources)
