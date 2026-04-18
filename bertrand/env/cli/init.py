"""Bootstrap Bertrand's host runtime, then generate and/or configure a new project
repository, if requested.
"""
from __future__ import annotations

import asyncio
import grp
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

from ..ceph import RepoMount, ensure_repo_credentials, secretfile
from ..config import RESOURCE_NAMES, Config, Resource
from ..config.core import NonEmpty, Trimmed
from ..kube import DEFAULT_VOLUME_SIZE, RepoVolume
from ..run import (
    BERTRAND_GROUP,
    HOST_MOUNTS,
    REPO_ALIASES_EXT,
    REPO_DIR,
    REPO_LOCK_EXT,
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


async def _init_new_repository(
    repo: GitRepository,
    worktree: Path,
    *,
    deadline: float | None,
) -> tuple[GitRepository, Path, bool]:
    # generate unique repository ID to track this repository in the Ceph filesystem
    repo_id = uuid.uuid4().hex
    loop = asyncio.get_running_loop()

    # allocate CephFS volume for the repository
    volume = await RepoVolume.create(
        repo_id=repo_id,
        timeout=None if deadline is None else deadline - loop.time(),
        size_request=DEFAULT_VOLUME_SIZE,
    )

    # wait for the volume to be provisioned and retrieve its final CephFS path
    ceph_path = await volume.resolve_ceph_path(
        timeout=None if deadline is None else deadline - loop.time()
    )

    # generate per-repo credentials for that path
    credentials = await ensure_repo_credentials(
        repo_id=repo_id,
        ceph_path=ceph_path,
        timeout=None if deadline is None else deadline - loop.time(),
    )

    # mount the volume to the host and place a symlink at the requested path
    with secretfile(credentials) as ceph_secretfile:
        await RepoMount(repo_id=repo_id, ceph_path=ceph_path).mount(
            repo.root,
            timeout=None if deadline is None else deadline - loop.time(),
            monitors=credentials.monitors,
            ceph_user=credentials.entity.removeprefix("client."),
            ceph_secretfile=ceph_secretfile,
        )

    # write the repository ID to the mount path for later validation and recovery
    # TODO: atomic_write_text()

    # initialize a git repository and bare worktree in the new volume
    initial_branch = worktree.as_posix()
    await repo.init(branch=initial_branch, bare=True)
    await repo.create_worktree(
        initial_branch,
        target=repo.root / worktree,
        create_branch=True
    )
    return repo, worktree, True


async def _init_repository(
    path: Path,
    *,
    timeout: float | None,
) -> tuple[GitRepository, Path, bool]:
    loop = asyncio.get_running_loop()
    deadline = None if timeout is None else loop.time() + timeout

    # resolve repository and worktree target
    repo, worktree = await GitRepository.resolve(path)

    # if no repository exists at the target path, then we will initialize a new one
    # inside a CephFS volume, which is mounted to this host, and then symlinked to the
    # target path.
    if not repo:
        if path.exists() or path.is_symlink():
            raise OSError(
                f"cannot initialize repository at {path!r}: path is already "
                "occupied by an existing filesystem entry"
            )
        return await _init_new_repository(
            repo=repo,
            worktree=worktree,
            deadline=deadline,
        )

    # if a repo already exists, confirm that it points to a valid CephFS mount with a
    # valid repo_id file, or move it there if not

    # TODO: check symlink to a repo directory, presence in HOST_MOUNTS, and
    # .bertrand/repo_id file with a valid UUID that matches a PVC in the cluster.
    # If any of those checks fail, then we should continue like below, but move
    # the existing repository into a new CephFS volume, converting to a bare
    # layout and mirroring all branches.
    return repo, worktree, False


    

    # TODO: Ceph volume allocation happens here?

    


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
                    f"packaged {hook.source} must start with:\n{'\n'.join(expected)}"
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
    # TODO: this (rendering the initial worktree state from config) should happen in a
    # separate helper.  This function is solely concerned with making sure a repository
    # exists at the indicated path, and that it is mounted as a CephFS volume.  If it
    # previously was not, then we should move it into a cephFS volume in order to
    # normalize the repository layout.  That might also be when I convert to a
    # standardized, bare repository layout.

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
    async with config:  # init default values, load overrides, and validate all resources
        await config.sync(tag=None)  # render in-tree resources with validated config
        if new:  # make an initial commit if this is a new repository
            try:
                await run(
                    ["git", "add", "-A"],
                    cwd=repo.root / worktree,
                    capture_output=True
                )
                await run(
                    ["git", "commit", "--quiet", "-m", "Initial commit"],
                    cwd=repo.root / worktree,
                    capture_output=True,
                )
            except CommandError as err:
                print(
                    "bertrand: failed to create initial commit in "
                    f"{repo.root / worktree}\n{err}",
                    file=sys.stderr
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

    # start both clusters if they are not already running, and link them via rook-ceph
    await start_microceph(timeout=None if deadline is None else deadline - loop.time())
    await start_microk8s(timeout=None if deadline is None else deadline - loop.time())
    await link_kube_ceph(timeout=None if deadline is None else deadline - loop.time())

    # retrieve or allocate a repository volume for the target path, mount it to the
    # host system, and then ensure a git repository inside it with the proper hooks
    repo, worktree, new = await _init_repository(
        path,
        timeout=None if deadline is None else deadline - loop.time(),
    )
    await _install_git_hooks(repo)

    # TODO: render the initial worktree state for all configured resources.

    # TODO: make an initial commit with the rendered state
