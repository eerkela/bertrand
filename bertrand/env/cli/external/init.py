"""Bootstrap Bertrand's host runtime.

This module also generates or configures a project repository when requested.
"""

from __future__ import annotations

import asyncio
import contextlib
import hashlib
import inspect
import json
import os
import platform
import shutil
import sys
from dataclasses import dataclass
from importlib import resources as importlib_resources
from pathlib import Path
from typing import TYPE_CHECKING, Annotated, Literal, Self

from pydantic import BaseModel, ConfigDict, PositiveInt, StringConstraints

from bertrand.env.config.core import RESOURCE_NAMES, Config, Resource
from bertrand.env.config.repository import resolve_repo_id
from bertrand.env.git import (
    BERTRAND_NAMESPACE,
    INFINITY,
    GitRepository,
    GroupStatus,
    HostLock,
    User,
    abspath,
    atomic_write_text,
    can_escalate,
    confirm,
    install_packages,
    run,
    symlink_points_to,
)
from bertrand.env.host import (
    BERTRAND_GROUP,
    REPO_DIR,
    REPO_MOUNT_EXT,
    RUN_DIR,
    STATE_DIR,
    ensure_host_state,
    host_state_backend_trustworthy,
)
from bertrand.env.kube.api import Kube
from bertrand.env.kube.api.bootstrap import (
    assert_microk8s_installed,
    ensure_microk8s_kubeconfig,
    install_microk8s,
    start_microk8s,
)
from bertrand.env.kube.build.controller import ensure_buildkit_build_controller
from bertrand.env.kube.build.daemon import BUILDKIT_POOL
from bertrand.env.kube.build.repository import IMAGES
from bertrand.env.kube.ceph.autoscale import ensure_ceph_autoscaler
from bertrand.env.kube.ceph.bootstrap import (
    assert_microceph_installed,
    install_microceph,
    link_kube_ceph,
    start_microceph,
)
from bertrand.env.kube.ceph.mount import (
    ensure_repository_mount,
    finalize_repository_mount,
    resurrect_repository_mount,
)
from bertrand.env.kube.ceph.volume import DEFAULT_VOLUME_SIZE, RepoVolume
from bertrand.env.kube.control import control_plane_image
from bertrand.env.kube.namespace import Namespace

if TYPE_CHECKING:
    from collections.abc import Awaitable, Callable


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


type _InitStage = Literal[
    "fresh",
    "detect_platform",
    "install_prereqs",
    "bootstrap_state_dir",
    "install_ceph_runtime",
    "install_kube_runtime",
    "installed",
]
type _InitText = Annotated[
    str,
    StringConstraints(strip_whitespace=True, min_length=1),
]


if TYPE_CHECKING:
    type _InitStep = Callable[[_InitState, _InitContext], Awaitable[None] | None]
    type _RepoStep = Callable[[_RepoState, _RepoContext], Awaitable[None]]


@dataclass(frozen=True)
class _InitContext:
    assume_yes: bool


class _InitState(BaseModel):
    model_config = ConfigDict(extra="forbid")
    version: PositiveInt
    stage: _InitStage = "fresh"
    user: _InitText | None = None
    uid: int | None = None
    gid: int | None = None
    package_manager: _InitText | None = None
    distro_id: _InitText | None = None
    distro_version: _InitText | None = None
    distro_codename: _InitText | None = None

    @staticmethod
    def backend_trustworthy() -> bool:
        """Return True when the shared init-state backend can be safely reused.

        Returns
        -------
        bool
            True if the backend is trustworthy and can be reused, False otherwise.
        """
        return host_state_backend_trustworthy()

    @classmethod
    def load(cls) -> Self:
        if not cls.backend_trustworthy():
            return cls(version=INIT_STATE_VERSION)
        if not INIT_STATE_FILE.exists() or not INIT_STATE_FILE.is_file():
            return cls(version=INIT_STATE_VERSION)
        try:
            data = json.loads(INIT_STATE_FILE.read_text(encoding="utf-8"))
            if not isinstance(data, dict):
                return cls(version=INIT_STATE_VERSION)
            self = cls.model_validate(data)
        except (OSError, TypeError, ValueError):
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


def _no_op(_state: _InitState, _context: _InitContext) -> None:
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


def _detect_platform(state: _InitState, _context: _InitContext) -> None:
    system = platform.system().lower()
    if system != "linux":
        msg = "Unsupported platform for package manager detection"
        raise OSError(msg)

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
        msg = "No supported package manager found"
        raise OSError(msg)

    # populate state
    user = User()
    state.user = user.name
    state.uid = user.uid
    state.gid = user.gid
    state.package_manager = manager
    state.distro_id = distro_id
    state.distro_version = version_id
    state.distro_codename = codename


async def _install_prereqs(state: _InitState, context: _InitContext) -> None:
    # fail fast if no escalation path is available for package installs
    if os.geteuid() != 0 and not can_escalate():
        msg = (
            "Bertrand requires root escalation to install host bootstrap dependencies, "
            "but neither 'sudo' nor 'doas' is available.  Install one of these tools "
            "or manually rerun `bertrand init` as root."
        )
        raise PermissionError(msg)
    if state.package_manager is None:
        msg = "Package manager is not detected; cannot install prerequisites."
        raise OSError(msg)

    # package mapping for bootstrap-required host tools across package managers
    packages = INIT_PREREQS.get(state.package_manager)
    if packages is None:
        msg = (
            "Unsupported package manager for prerequisite installation: "
            f"{state.package_manager!r}"
        )
        raise OSError(msg)

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
        assume_yes=context.assume_yes,
    ):
        msg = "Installation declined by user."
        raise PermissionError(msg)
    await install_packages(
        package_manager=state.package_manager,
        packages=sorted(missing),
        assume_yes=context.assume_yes,
        timeout=INFINITY,
    )

    # verify all required tools after installation
    unresolved: list[str] = [
        name
        for name, cmd in INIT_CHECK_PREREQS
        if not any(shutil.which(c) for c in cmd)
    ]
    if unresolved:
        msg = (
            "Prerequisite installation completed, but required host bootstrap tools "
            f"are still missing: {', '.join(unresolved)}."
        )
        raise OSError(msg)


async def _bootstrap_state_dir(state: _InitState, context: _InitContext) -> None:
    if state.user is None:
        msg = "init state user is missing; rerun `bertrand init`."
        raise OSError(msg)
    await ensure_host_state(
        user=state.user,
        assume_yes=context.assume_yes,
        timeout=INFINITY,
    )


async def _install_ceph_runtime(state: _InitState, context: _InitContext) -> None:
    if state.user is None:
        msg = "init state user is missing; rerun `bertrand init`."
        raise OSError(msg)
    if state.package_manager is None:
        msg = "Package manager is not detected; cannot install Ceph runtime."
        raise OSError(msg)
    if state.distro_id is None:
        msg = "Distro ID is not detected; cannot install Ceph runtime."
        raise OSError(msg)

    await install_microceph(
        user=state.user,
        package_manager=state.package_manager,
        distro_id=state.distro_id,
        assume_yes=context.assume_yes,
    )


async def _install_kube_runtime(state: _InitState, context: _InitContext) -> None:
    if state.user is None:
        msg = "init state user is missing; rerun `bertrand init`."
        raise OSError(msg)
    if state.package_manager is None:
        msg = "Package manager is not detected; cannot install Kubernetes runtime."
        raise OSError(msg)
    if state.distro_id is None:
        msg = "Distro ID is not detected; cannot install Kubernetes runtime."
        raise OSError(msg)

    await install_microk8s(
        package_manager=state.package_manager,
        user=state.user,
        distro_id=state.distro_id,
        assume_yes=context.assume_yes,
    )


async def _assert_installed(state: _InitState, _context: _InitContext) -> None:
    if state.user is None:
        msg = "init state user is missing; rerun `bertrand init`."
        raise OSError(msg)

    bertrand_group = GroupStatus.get(state.user, BERTRAND_GROUP)
    if not bertrand_group.configured:
        msg = (
            f"user {state.user!r} is not in {BERTRAND_GROUP!r}.  Rerun `bertrand init` "
            "to configure shared Bertrand host-state access."
        )
        raise OSError(msg)
    if not bertrand_group.active:
        msg = (
            f"user {state.user!r} is in {BERTRAND_GROUP!r}, but the current session "
            f"is not active in that group.  Run `newgrp {BERTRAND_GROUP}` or log out "
            "and back in, then rerun `bertrand init`."
        )
        raise OSError(msg)

    await assert_microceph_installed(user=state.user)
    await assert_microk8s_installed(user=state.user)


async def _run_init_step(
    step: _InitStep,
    state: _InitState,
    context: _InitContext,
) -> None:
    result = step(state, context)
    if inspect.isawaitable(result):
        await result


INIT_STAGES: tuple[tuple[_InitStage, _InitStep], ...] = (
    ("fresh", _no_op),
    ("detect_platform", _detect_platform),
    ("install_prereqs", _install_prereqs),
    ("bootstrap_state_dir", _bootstrap_state_dir),
    ("install_ceph_runtime", _install_ceph_runtime),
    ("install_kube_runtime", _install_kube_runtime),
    ("installed", _assert_installed),
)


@dataclass(frozen=True)
class _GitHook:
    source: Path
    destination: Path
    executable: bool


MANAGED_GIT_HOOKS: tuple[_GitHook, ...] = (
    _GitHook(
        source=Path("reference_transaction.py"),
        destination=Path("hooks/reference-transaction"),
        executable=True,
    ),
    _GitHook(
        source=Path("bertrand_git.py"),
        destination=Path("hooks/bertrand_git.py"),
        executable=False,
    ),
)
REPO_LOCK_DIR = RUN_DIR / "init"
PROTECTED_DISABLE_RESOURCES: frozenset[str] = frozenset({"bertrand", "python"})


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
                options = "\n".join(
                    f"    {option}" for option in sorted(RESOURCE_NAMES)
                )
                msg = f"unknown resource '{component}'.  Options are:\n{options}"
                raise ValueError(msg)
            parsed.add(resource)
    return parsed


@dataclass(frozen=True)
class _RepoContext:
    enable: set[Resource]
    disable: set[Resource]
    assume_yes: bool


@dataclass
class _RepoState:
    kube: Kube
    repo: GitRepository
    target: Path
    worktree: Path
    repo_id: str
    mount_alias: Path | None
    deadline: float

    def __post_init__(self) -> None:
        self.target = abspath(self.target)

    @classmethod
    def lock(cls, root: Path, timeout: float) -> HostLock:
        digest = hashlib.sha256(str(root).encode("utf-8"))
        return HostLock(
            REPO_LOCK_DIR / f"{digest.hexdigest()}.lock",
            timeout=timeout,
            privileges=INIT_LOCK_MODE,
        )


async def _ensure_repo_storage(
    state: _RepoState,
    context: _RepoContext,
) -> None:
    loop = asyncio.get_running_loop()
    volumes = await RepoVolume.list(
        state.kube,
        state.repo_id,
        timeout=state.deadline - loop.time(),
    )

    # no existing claim was found: prompt before converting an unmanaged repo
    if not volumes and state.repo:
        prompt = (
            "Bertrand found an existing unmanaged repository at "
            f"{state.repo.root}.  Do you want to convert it into a Bertrand "
            "repository?\n"
            "WARNING: this will delete all untracked files and convert the repository "
            "into a bare layout with isolated worktrees for each branch, in order to "
            "meet Bertrand's invariants.  Make sure to track or manually back up any "
            "important files in the repository before proceeding.  Additional "
            "information can be "
            "found in the Bertrand documentation, if needed.\nContinue? [y/N] "
        )
        if not confirm(prompt, assume_yes=context.assume_yes):
            msg = "repository conversion declined by user"
            raise PermissionError(msg)

    repository_mount = await ensure_repository_mount(
        state.kube,
        repo_id=state.repo_id,
        timeout=state.deadline - loop.time(),
        size_request=DEFAULT_VOLUME_SIZE,
        target=state.target,
        volumes=volumes,
    )
    state.mount_alias = repository_mount.alias


async def _ensure_bare_worktrees(
    state: _RepoState,
    _context: _RepoContext,
) -> None:
    if state.mount_alias is None:
        msg = "bare-worktree convergence requires a mounted repository alias"
        raise OSError(msg)
    mount = GitRepository(REPO_DIR / state.repo_id / REPO_MOUNT_EXT / ".git")
    loop = asyncio.get_running_loop()
    deadline = state.deadline
    target_branch: str | None = None

    # specific-worktree mode: capture source branch identity before any conversion so
    # we can remap to canonical converged worktree paths afterwards.
    if state.worktree != Path():
        source_worktree = state.repo.root / state.worktree
        match = next(
            (wt for wt in await state.repo.worktrees() if wt.path == source_worktree),
            None,
        )
        if match is None:
            msg = (
                f"targeted worktree {source_worktree} is not registered in source "
                f"repository {state.repo.root}"
            )
            raise OSError(msg)
        target_branch = match.branch
        if target_branch is None:
            msg = (
                f"targeted worktree {source_worktree} has detached HEAD; please attach "
                "it to a branch before running `bertrand init`."
            )
            raise OSError(msg)

    # new repository: initialize a bare history store and create the first worktree
    if not state.repo:  # source repo is uninitialized or missing
        branch = await GitRepository.default_branch()
        default_worktree = state.mount_alias / branch
        # destination repo is also uninitialized - create initial worktree
        if not mount:
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
            msg = f"managed destination repository at {state.mount_alias} must be bare"
            raise OSError(msg)
        state.repo = mount
        return

    # conversion path: mirror all refs from source into destination, then converge
    # one worktree per local branch under refs/heads/*
    if state.repo != mount:
        if await state.repo.dirty():
            msg = (
                f"cannot convert repository at {state.repo.root}: "
                "worktree has uncommitted changes"
            )
            raise OSError(msg)
        await mount.mirror_from(state.repo, timeout=deadline - loop.time())

    # assert mounted directory is well-formed, then sync worktrees
    if not mount:
        msg = f"managed destination repository does not exist at {state.mount_alias}"
        raise OSError(msg)
    if not await mount.is_bare():
        msg = f"managed destination repository at {state.mount_alias} must be bare"
        raise OSError(msg)
    await mount.sync_worktrees()
    state.repo = mount
    if target_branch is not None:
        if not any(wt.branch == target_branch for wt in await mount.worktrees()):
            msg = (
                f"failed to map targeted source worktree branch {target_branch!r} into "
                f"converged repository at {mount.root}"
            )
            raise OSError(msg)
        state.worktree = Path(target_branch)


async def _ensure_repo_hooks(
    state: _RepoState,
    _context: _RepoContext,
) -> None:
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
            hook_text = (
                importlib_resources.files("bertrand.env")
                .joinpath(
                    "git",
                    hook.source,
                )
                .read_text(encoding="utf-8")
            )
            if hook_text.splitlines()[: len(expected)] != expected:
                lines = "\n".join(expected)
                msg = f"packaged {hook.source} must start with:\n{lines}"
                print(
                    f"bertrand: failed to {stage} in {state.repo.root}\n{msg}",
                    file=sys.stderr,
                )
                continue

            # do not clobber non-managed hooks
            stage = f"resolve existing git hook at '{hook.destination}'"
            target = await state.repo.git_path(hook.destination, cwd=state.repo.root)
            if target.exists():
                if not target.is_file():
                    msg = f"git hook path is not a file: {target}"
                    print(
                        f"bertrand: failed to {stage} in {state.repo.root}\n{msg}",
                        file=sys.stderr,
                    )
                    continue
                existing = target.read_text(encoding="utf-8")
                if existing == hook_text:
                    continue
                if existing.splitlines()[: len(expected)] != expected:
                    print(
                        f"existing git hook at {target} is not managed by Bertrand; "
                        f"skipping to avoid clobbering user-managed hook.",
                        file=sys.stderr,
                    )
                    continue

            # install hook into git directory
            stage = f"write git hook to {target}"
            atomic_write_text(target, hook_text, encoding="utf-8")
            if hook.executable:
                stage = f"set executable permissions on git hook {target}"
                with contextlib.suppress(OSError):
                    target.chmod(0o755)
        except (OSError, ValueError) as err:
            print(
                f"bertrand: failed to {stage} in {state.repo.root}\n{err}",
                file=sys.stderr,
            )


async def _render_config_artifacts(
    state: _RepoState,
    context: _RepoContext,
) -> None:
    loop = asyncio.get_running_loop()
    render_targets: list[Path] = []

    # repository-level targeting converges all branch-attached in-repo worktrees;
    # detached and out-of-tree worktrees are skipped with warnings.
    if state.worktree == Path():
        for worktree in sorted(
            await state.repo.worktrees(),
            key=lambda wt: wt.path.as_posix(),
        ):
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
        msg = "repository-wide config render found no eligible branch worktrees"
        raise OSError(msg)

    # render all targeted worktrees based on existing configuration (if any), then
    # apply enable/disable deltas before syncing artifacts.
    for worktree in render_targets:
        root = state.repo.root / worktree
        config = await Config.load(
            root,
            kube=state.kube,
            repo=state.repo,
            timeout=state.deadline - loop.time(),
        )
        config.resources.update({resource.name: None for resource in context.enable})
        config.init = Config.Init(
            repo=state.repo,
            worktree=worktree,
        )
        async with config:
            for resource in context.disable:
                config.resources.pop(resource.name, None)
                for relative in sorted(
                    resource.paths,
                    key=lambda item: item.as_posix(),
                ):
                    path = root / relative
                    if not path.exists() and not path.is_symlink():
                        continue
                    if path.is_dir() and not path.is_symlink():
                        shutil.rmtree(path, ignore_errors=True)
                    else:
                        path.unlink(missing_ok=True)
            await config.sync(tag=None)


async def _make_initial_commit(
    state: _RepoState,
    _context: _RepoContext,
) -> None:
    loop = asyncio.get_running_loop()
    worktree_path: Path | None = None

    # repository-level target: use the HEAD or first branch-attached worktree for
    # commit operations
    if state.worktree == Path():
        branch_targets = {
            wt.branch: wt.path.relative_to(state.repo.root)
            for wt in await state.repo.worktrees()
            if wt.branch is not None and wt.path.is_relative_to(state.repo.root)
        }
        head = await state.repo.head_branch()
        if head is not None and head in branch_targets:
            worktree_path = state.repo.root / branch_targets[head]
        elif branch_targets:
            worktree_path = (
                state.repo.root
                / sorted(
                    branch_targets.values(),
                    key=lambda value: value.as_posix(),
                )[0]
            )
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
        msg = f"failed to determine whether repository already has commits:\n{head}"
        raise OSError(msg)

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
        msg = f"failed to inspect staged diff for initial commit:\n{staged}"
        raise OSError(msg)

    # make commit
    await run(
        ["git", "commit", "--quiet", "-m", "Initial commit"],
        cwd=worktree_path,
        capture_output=True,
        timeout=state.deadline - loop.time(),
    )


async def _finalize(
    state: _RepoState,
    context: _RepoContext,
) -> None:
    mount_alias = state.mount_alias
    if mount_alias is None:
        msg = "cannot finalize repository convergence without a mount alias"
        raise OSError(msg)
    target = state.target
    hidden_mount = REPO_DIR / state.repo_id / REPO_MOUNT_EXT
    swap_path = target.parent / f".{target.name}.bertrand.swap.{state.repo_id}"
    replace_existing = False
    if (
        mount_alias != target
        and not (swap_path.exists() or swap_path.is_symlink())
        and (target.exists() or target.is_symlink())
        and not symlink_points_to(target, hidden_mount)
    ):
        replace_existing = confirm(
            "Bertrand needs to atomically replace this path with a managed "
            "CephFS repository alias to complete conversion. Continue?\n[y/N] ",
            assume_yes=context.assume_yes,
        )

    loop = asyncio.get_running_loop()
    state.mount_alias = await finalize_repository_mount(
        repo_id=state.repo_id,
        target=target,
        alias=mount_alias,
        replace_existing=replace_existing,
        timeout=state.deadline - loop.time(),
    )


REPO_STAGES: tuple[_RepoStep, ...] = (
    _ensure_repo_storage,
    _ensure_bare_worktrees,
    _ensure_repo_hooks,
    _render_config_artifacts,
    _make_initial_commit,
    _finalize,
)


async def _converge_build_runtime(kube: Kube, *, timeout: float) -> None:
    if timeout <= 0:
        msg = "build runtime timeout must be non-negative"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    await Namespace.upsert(
        kube,
        name=BERTRAND_NAMESPACE,
        timeout=deadline - loop.time(),
    )
    await IMAGES.ensure(kube, timeout=deadline - loop.time())
    await IMAGES.ensure_node_trust(kube, timeout=deadline - loop.time())
    await IMAGES.assert_node_trust(kube, timeout=deadline - loop.time())
    await BUILDKIT_POOL.ensure(
        kube,
        timeout=deadline - loop.time(),
        config_hash=await IMAGES.current_buildkit_config_hash(
            kube,
            timeout=deadline - loop.time(),
        ),
    )
    await ensure_buildkit_build_controller(
        kube,
        image=control_plane_image(),
        timeout=deadline - loop.time(),
    )


async def _converge_cluster_runtime(kube: Kube, *, timeout: float) -> None:
    if timeout <= 0:
        msg = "cluster runtime timeout must be non-negative"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    await _converge_build_runtime(kube, timeout=deadline - loop.time())
    await ensure_ceph_autoscaler(
        kube,
        image=control_plane_image(),
        timeout=deadline - loop.time(),
    )


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
    TimeoutError
        If bootstrap or convergence cannot start before the deadline expires.
    ValueError
        If any resource names in `enable`/`disable` are invalid, or if required
        core resources are disabled.
    """
    if path is None and (enable or disable):
        msg = (
            "Cannot globally enable or disable resources without a worktree.  Please "
            "specify at least a repository path to configure resources within it."
        )
        raise OSError(msg)
    if timeout <= 0:
        msg = "timed out before checking host bootstrap"
        raise TimeoutError(msg)

    # bootstrap host runtime control plane (persistent, system-wide)
    async with HostLock(
        INIT_LOCK,
        timeout=timeout,
        privileges=INIT_LOCK_MODE,
    ):
        context = _InitContext(assume_yes=yes)
        state = _InitState.load()
        index = next(
            (i for i, (stage, _) in enumerate(INIT_STAGES) if stage == state.stage),
            0,
        )
        if index == len(INIT_STAGES) - 1:
            try:
                await _assert_installed(state, context)
            # reported as finished, but runtime is not actually installed
            except OSError:
                index = 0
                state = _InitState(version=INIT_STATE_VERSION)
                if _InitState.backend_trustworthy():
                    state.dump()
        for stage, step in INIT_STAGES[index:]:
            await _run_init_step(step, state, context)
            state.stage = stage
            if _InitState.backend_trustworthy():
                state.dump()

        if state.user is None:
            msg = "init state user is missing; rerun `bertrand init`."
            raise OSError(msg)
        for group, purpose in (
            (BERTRAND_GROUP, "shared Bertrand host-state access"),
            ("microceph", "MicroCeph CLI/storage access"),
            ("microk8s", "MicroK8s runtime access"),
        ):
            status = GroupStatus.get(state.user, group)
            if not status.configured:
                msg = (
                    f"user {state.user!r} is not in {group!r}.  Rerun `bertrand init` "
                    f"to configure {purpose}."
                )
                raise OSError(msg)
            if not status.active:
                msg = (
                    f"user {state.user!r} is in {group!r}, but the current session is "
                    f"not active in that group.  Run `newgrp {group}` or log out and "
                    "back in, then rerun `bertrand init`."
                )
                raise OSError(msg)

        # Start both clusters and link them via Rook-Ceph.
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        await start_microceph(timeout=deadline - loop.time())
        await start_microk8s(timeout=deadline - loop.time())
        await ensure_microk8s_kubeconfig(timeout=deadline - loop.time())
        await link_kube_ceph(timeout=deadline - loop.time())

        # bootstrap internal kubernetes runtime control plane
        with await Kube.host(timeout=deadline - loop.time()) as kube:
            await _converge_cluster_runtime(kube, timeout=deadline - loop.time())

    # if no project root is provided, then we're done
    if path is None:
        return

    # fail fast if required tools are missing, and validate resource convergence input
    enabled: set[Resource] = {RESOURCE_NAMES["bertrand"]}
    enabled.update(_parse_resource_specs(enable, for_disable=False))
    disabled = _parse_resource_specs(disable, for_disable=True)
    protected = {
        name for name in PROTECTED_DISABLE_RESOURCES if RESOURCE_NAMES[name] in disabled
    }
    if protected:
        names = ", ".join(sorted(protected))
        msg = f"cannot disable required Bertrand resources: {names}"
        raise ValueError(msg)
    enabled -= disabled

    # resolve path to parent git repository and relative worktree
    if not shutil.which("git"):
        msg = (
            "Bertrand requires 'git' to initialize a project repository, but it "
            "was not found in PATH."
        )
        raise OSError(msg)
    raw_path = abspath(path)

    with await Kube.host(timeout=deadline - loop.time()) as kube:
        # run managed-alias resurrection before git path resolution so repository
        # discovery sees the recovered hidden mount layout if one was detached
        resurrected = await resurrect_repository_mount(
            kube,
            raw_path,
            timeout=deadline - loop.time(),
        )
        recovered_repo_id: str | None = None
        if resurrected is not None:
            recovered_repo_id, _ = resurrected

        # search for parent Git repository on target path, and then identify the
        # ancestor symlink that points to it, if any
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
            msg = (
                f"failed to derive repository destination path from {raw_path}; no "
                f"ancestor resolves to repository root {repo.root}"
            )
            raise OSError(msg)

        # synchronize uniquely for each repository path to limit global init lock
        # contention
        async with _RepoState.lock(repo.root, timeout=deadline - loop.time()):
            if repo and await repo.dirty():
                msg = (
                    f"repository at {repo.root} has uncommitted changes; please "
                    "commit or stash them before calling `bertrand init`."
                )
                raise OSError(msg)

            # resolve deterministic repository identity from managed metadata if
            # available, otherwise derive from the canonical repository root.
            repo_id = recovered_repo_id or resolve_repo_id(repo)
            repo_context = _RepoContext(
                enable=enabled,
                disable=disabled,
                assume_yes=yes,
            )
            state = _RepoState(
                kube=kube,
                repo=repo,
                target=target,
                worktree=worktree,
                repo_id=repo_id,
                mount_alias=None,
                deadline=deadline,
            )

            # execute all idempotent convergence stages in sequence, allowing recovery
            # from previous runs
            for stage in REPO_STAGES:
                await stage(state, repo_context)
