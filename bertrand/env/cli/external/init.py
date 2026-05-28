"""Bootstrap Bertrand's shared host runtime.

Bertrand v1 uses the supported default MicroK8s snap as its shared Kubernetes
runtime and converges Rook-managed Ceph inside that cluster.  This module may
install or start MicroK8s when missing, but it never assumes exclusive snap
ownership.  It also generates or configures a project repository when requested.
"""

from __future__ import annotations

import contextlib
import hashlib
import os
import platform
import shutil
import sys
from dataclasses import dataclass
from importlib import resources as importlib_resources
from pathlib import Path
from typing import Any

from pydantic import ValidationError

from bertrand.env.config.core import RESOURCE_NAMES, Config, Resource
from bertrand.env.git import (
    BERTRAND_NAMESPACE,
    INFINITY,
    CommandError,
    Deadline,
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
    ensure_host_state,
)
from bertrand.env.kube.api.bootstrap import (
    assert_microk8s_installed,
    ensure_microk8s_kubeconfig,
    install_microk8s,
    start_microk8s,
)
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.build.controller import ensure_buildkit_build_controller
from bertrand.env.kube.build.daemon import ensure_buildkit_pool
from bertrand.env.kube.build.repository import (
    assert_image_repository_node_trust,
    current_buildkit_config_hash,
    ensure_image_repository,
    ensure_image_repository_node_trust,
)
from bertrand.env.kube.capability.device import ensure_dra_backend
from bertrand.env.kube.ceph.bootstrap import ensure_rook_ceph_base, wait_rook_ceph_ready
from bertrand.env.kube.ceph.mount import (
    ensure_repository_mount,
    finalize_repository_mount,
    prune_repository_mount_aliases,
    refresh_repository_alias_for_path,
    resurrect_repository_mount,
)
from bertrand.env.kube.ceph.storage import ensure_ceph_storage_controller
from bertrand.env.kube.ceph.volume import (
    DEFAULT_VOLUME_SIZE,
    list_repository_volume_claims,
    mark_repository_volume_failed,
)
from bertrand.env.kube.control import control_plane_image
from bertrand.env.kube.dev import ensure_dev_backend
from bertrand.env.kube.namespace import Namespace
from bertrand.env.kube.network.bootstrap import ensure_network_backend
from bertrand.env.kube.node_identity import ensure_local_bertrand_node

INIT_LOCK = Path("/tmp/bertrand-init.lock")
INIT_LOCK_MODE = 0o666
INIT_PREREQS = {
    "apt": {
        "getfacl": "acl",
        "setfacl": "acl",
        "groupadd": "passwd",
        "usermod": "passwd",
        "install": "coreutils",
        "mount.ceph": "ceph-common",
        "pvs": "lvm2",
        "vgs": "lvm2",
        "lvs": "lvm2",
        "losetup": "util-linux",
    },
    "dnf": {
        "getfacl": "acl",
        "setfacl": "acl",
        "groupadd": "shadow-utils",
        "usermod": "shadow-utils",
        "install": "coreutils",
        "mount.ceph": "ceph-common",
        "pvs": "lvm2",
        "vgs": "lvm2",
        "lvs": "lvm2",
        "losetup": "util-linux",
    },
    "yum": {
        "getfacl": "acl",
        "setfacl": "acl",
        "groupadd": "shadow-utils",
        "usermod": "shadow-utils",
        "install": "coreutils",
        "mount.ceph": "ceph-common",
        "pvs": "lvm2",
        "vgs": "lvm2",
        "lvs": "lvm2",
        "losetup": "util-linux",
    },
    "zypper": {
        "getfacl": "acl",
        "setfacl": "acl",
        "groupadd": "shadow",
        "usermod": "shadow",
        "install": "coreutils",
        "mount.ceph": "ceph-common",
        "pvs": "lvm2",
        "vgs": "lvm2",
        "lvs": "lvm2",
        "losetup": "util-linux",
    },
    "pacman": {
        "getfacl": "acl",
        "setfacl": "acl",
        "groupadd": "shadow",
        "usermod": "shadow",
        "install": "coreutils",
        "mount.ceph": "ceph",
        "pvs": "lvm2",
        "vgs": "lvm2",
        "lvs": "lvm2",
        "losetup": "util-linux",
    },
    "apk": {
        "getfacl": "acl",
        "setfacl": "acl",
        "groupadd": "shadow",
        "usermod": "shadow",
        "install": "coreutils",
        "mount.ceph": "ceph",
        "pvs": "lvm2",
        "vgs": "lvm2",
        "lvs": "lvm2",
        "losetup": "util-linux",
    },
}
INIT_CHECK_PREREQS = (
    ("getfacl", ("getfacl",)),
    ("setfacl", ("setfacl",)),
    ("groupadd", ("groupadd",)),
    ("usermod", ("usermod",)),
    ("install", ("install",)),
    ("mount.ceph", ("mount.ceph",)),
    ("pvs", ("pvs",)),
    ("vgs", ("vgs",)),
    ("lvs", ("lvs",)),
    ("losetup", ("losetup",)),
)
_INIT_REPO_STAGE_ERRORS: tuple[type[Exception], ...] = (
    OSError,
    RuntimeError,
    TimeoutError,
    ValueError,
    CommandError,
    ValidationError,
)
_INIT_FAILURE_MARK_ERRORS: tuple[type[Exception], ...] = (
    OSError,
    RuntimeError,
    TimeoutError,
    ValueError,
    ValidationError,
)


@dataclass(frozen=True)
class _HostRuntime:
    user: str
    package_manager: str


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


def _detect_host_runtime() -> _HostRuntime:
    system = platform.system().lower()
    if system != "linux":
        msg = "Unsupported platform for package manager detection"
        raise OSError(msg)

    os_release = _read_os_release()
    distro_id = (os_release.get("ID") or "").lower() or None
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
    return _HostRuntime(user=User().name, package_manager=manager)


async def _install_prereqs(runtime: _HostRuntime, *, assume_yes: bool) -> None:
    # fail fast if no escalation path is available for package installs
    if os.geteuid() != 0 and not can_escalate():
        msg = (
            "Bertrand requires root escalation to install host bootstrap dependencies, "
            "but neither 'sudo' nor 'doas' is available.  Install one of these tools "
            "or manually rerun `bertrand init` as root."
        )
        raise PermissionError(msg)

    # package mapping for bootstrap-required host tools across package managers
    packages = INIT_PREREQS.get(runtime.package_manager)
    if packages is None:
        msg = (
            "Unsupported package manager for prerequisite installation: "
            f"{runtime.package_manager!r}"
        )
        raise OSError(msg)

    # detect missing required bootstrap tools
    missing: set[str] = set()
    for tool, package in packages.items():
        if package in missing:
            continue
        if shutil.which(tool):
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
        msg = "Installation declined by user."
        raise PermissionError(msg)
    await install_packages(
        package_manager=runtime.package_manager,
        packages=sorted(missing),
        assume_yes=assume_yes,
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


async def _bootstrap_state_dir(runtime: _HostRuntime, *, assume_yes: bool) -> None:
    await ensure_host_state(
        user=runtime.user,
        assume_yes=assume_yes,
        timeout=INFINITY,
    )


async def _install_kube_runtime(runtime: _HostRuntime, *, assume_yes: bool) -> None:
    await install_microk8s(
        package_manager=runtime.package_manager,
        user=runtime.user,
        assume_yes=assume_yes,
    )


def _validate_shared_runtime_groups(runtime: _HostRuntime) -> None:
    for group, purpose in (
        (BERTRAND_GROUP, "shared Bertrand host-state access"),
        ("microk8s", "MicroK8s runtime access"),
    ):
        status = GroupStatus.get(runtime.user, group)
        if not status.configured:
            msg = (
                f"user {runtime.user!r} is not in {group!r}.  Rerun `bertrand init` "
                f"to configure {purpose}."
            )
            raise OSError(msg)
        if not status.active:
            msg = (
                f"user {runtime.user!r} is in {group!r}, but the current session is "
                f"not active in that group.  Run `newgrp {group}` or log out and "
                "back in, then rerun `bertrand init`."
            )
            raise OSError(msg)


async def _converge_host_runtime(*, assume_yes: bool) -> _HostRuntime:
    runtime = _detect_host_runtime()
    await _install_prereqs(runtime, assume_yes=assume_yes)
    await _bootstrap_state_dir(runtime, assume_yes=assume_yes)
    await _install_kube_runtime(runtime, assume_yes=assume_yes)
    _validate_shared_runtime_groups(runtime)
    await assert_microk8s_installed(user=runtime.user)
    return runtime


async def _ensure_shared_runtime(
    *,
    timeout: float,
    yes: bool,
    converge_cluster: bool,
) -> Deadline:
    msg = "host bootstrap timeout must be positive"
    deadline = Deadline.from_timeout(timeout, message=msg)
    async with HostLock(
        INIT_LOCK,
        timeout=deadline.remaining(),
        privileges=INIT_LOCK_MODE,
    ):
        await _converge_host_runtime(assume_yes=yes)
        if not converge_cluster:
            return deadline

        await _converge_host_cluster_runtime(deadline, start=True)
        return deadline


async def ensure_shared_runtime_installed(*, timeout: float, yes: bool) -> None:
    """Install shared host prerequisites without bootstrapping local clusters.

    Parameters
    ----------
    timeout : float
        Maximum host bootstrap budget in seconds.
    yes : bool
        Whether to auto-accept installation prompts.

    """
    await _ensure_shared_runtime(
        timeout=timeout,
        yes=yes,
        converge_cluster=False,
    )


MANAGED_GIT_HOOKS: tuple[tuple[Path, Path, bool], ...] = (
    (Path("reference_transaction.py"), Path("hooks/reference-transaction"), True),
    (Path("bertrand_git.py"), Path("hooks/bertrand_git.py"), False),
)
REPO_LOCK_DIR = RUN_DIR / "init"
PROTECTED_DISABLE_RESOURCES: frozenset[str] = frozenset({"bertrand", "python"})


def _parse_resource_specs(
    specs: list[str],
    *,
    for_disable: bool,
) -> set[Resource[Any]]:
    parsed: set[Resource[Any]] = set()
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


def _parse_repo_resource_plan(
    *,
    enable: list[str],
    disable: list[str],
) -> tuple[set[Resource[Any]], set[Resource[Any]]]:
    enabled: set[Resource[Any]] = {RESOURCE_NAMES["bertrand"]}
    enabled.update(_parse_resource_specs(enable, for_disable=False))
    disabled = _parse_resource_specs(disable, for_disable=True)
    protected = {
        name for name in PROTECTED_DISABLE_RESOURCES if RESOURCE_NAMES[name] in disabled
    }
    if protected:
        names = ", ".join(sorted(protected))
        msg = f"cannot disable required Bertrand resources: {names}"
        raise ValueError(msg)
    return enabled - disabled, disabled


@dataclass
class _RepoState:
    kube: Kube
    repo: GitRepository
    target: Path
    worktree: Path
    repo_id: str
    mount_alias: Path | None
    enable: set[Resource[Any]]
    disable: set[Resource[Any]]
    assume_yes: bool
    deadline: Deadline

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
) -> None:
    volumes = await list_repository_volume_claims(
        state.kube,
        state.repo_id,
        timeout=state.deadline.remaining(),
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
        if not confirm(prompt, assume_yes=state.assume_yes):
            msg = "repository conversion declined by user"
            raise PermissionError(msg)

    state.mount_alias = await ensure_repository_mount(
        state.kube,
        repo_id=state.repo_id,
        timeout=state.deadline.remaining(),
        size_request=DEFAULT_VOLUME_SIZE,
        target=state.target,
        volumes=volumes,
    )


async def _ensure_bare_worktrees(
    state: _RepoState,
) -> None:
    if state.mount_alias is None:
        msg = "bare-worktree convergence requires a mounted repository alias"
        raise OSError(msg)
    mount = GitRepository(REPO_DIR / state.repo_id / REPO_MOUNT_EXT / ".git")
    target_branch = await _target_worktree_branch(state)

    # new repository: initialize a bare history store and create the first worktree
    if not state.repo:  # source repo is uninitialized or missing
        await _ensure_new_bare_repository(mount, alias=state.mount_alias)
        state.repo = mount
        return

    # conversion path: mirror all refs from source into destination, then converge
    # one worktree per local branch under refs/heads/*
    if state.repo != mount:
        await _mirror_source_repo(state, mount)

    # assert mounted directory is well-formed, then sync worktrees
    await _assert_bare_managed_repo(mount, alias=state.mount_alias)
    await mount.sync_worktrees()
    state.repo = mount
    await _remap_target_worktree(state, target_branch=target_branch)


async def _target_worktree_branch(state: _RepoState) -> str | None:
    # specific-worktree mode: capture source branch identity before any conversion so
    # we can remap to canonical converged worktree paths afterwards.
    if state.worktree == Path():
        return None
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
    if match.branch is None:
        msg = (
            f"targeted worktree {source_worktree} has detached HEAD; please attach "
            "it to a branch before running `bertrand init`."
        )
        raise OSError(msg)
    return match.branch


async def _ensure_new_bare_repository(
    mount: GitRepository,
    *,
    alias: Path,
) -> None:
    branch = await GitRepository.default_branch()
    default_worktree = alias / branch
    # destination repo is also uninitialized - create initial worktree
    if not mount:
        await mount.init(branch=branch, bare=True)
        await mount.create_worktree(
            branch,
            target=default_worktree,
            create_branch=True,
        )
        return
    if await mount.is_bare():
        if not any(wt.branch == branch for wt in await mount.worktrees()):
            await mount.create_worktree(
                branch,
                target=default_worktree,
                create_branch=True,
            )
        await mount.sync_worktrees()
        return
    msg = f"managed destination repository at {alias} must be bare"
    raise OSError(msg)


async def _mirror_source_repo(state: _RepoState, mount: GitRepository) -> None:
    if await state.repo.dirty():
        msg = (
            f"cannot convert repository at {state.repo.root}: "
            "worktree has uncommitted changes"
        )
        raise OSError(msg)
    await mount.mirror_from(state.repo, timeout=state.deadline.remaining())


async def _assert_bare_managed_repo(mount: GitRepository, *, alias: Path) -> None:
    if not mount:
        msg = f"managed destination repository does not exist at {alias}"
        raise OSError(msg)
    if not await mount.is_bare():
        msg = f"managed destination repository at {alias} must be bare"
        raise OSError(msg)


async def _remap_target_worktree(
    state: _RepoState,
    *,
    target_branch: str | None,
) -> None:
    if target_branch is None:
        return
    if not any(wt.branch == target_branch for wt in await state.repo.worktrees()):
        msg = (
            f"failed to map targeted source worktree branch {target_branch!r} into "
            f"converged repository at {state.repo.root}"
        )
        raise OSError(msg)
    state.worktree = Path(target_branch)


async def _ensure_repo_hooks(
    state: _RepoState,
) -> None:
    # load managed hook payloads before install; this preserves fail-fast behavior if
    # packaged hook definitions are malformed.
    for source, destination, executable in MANAGED_GIT_HOOKS:
        stage = f"resolve managed hook for '{destination}'"
        marker = f"# bertrand-managed: {source}"
        try:
            # load hook from Bertrand package resources and verify shebang/marker
            expected: list[str] = []
            if executable:
                expected.append("#!/usr/bin/env python3")
            expected.append(marker)
            hook_text = (
                importlib_resources.files("bertrand.env")
                .joinpath(
                    "git",
                    source,
                )
                .read_text(encoding="utf-8")
            )
            if hook_text.splitlines()[: len(expected)] != expected:
                lines = "\n".join(expected)
                msg = f"packaged {source} must start with:\n{lines}"
                print(
                    f"bertrand: failed to {stage} in {state.repo.root}\n{msg}",
                    file=sys.stderr,
                )
                continue

            # do not clobber non-managed hooks
            stage = f"resolve existing git hook at '{destination}'"
            target = await state.repo.git_path(destination, cwd=state.repo.root)
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
            if executable:
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
) -> None:
    # render all targeted worktrees based on existing configuration (if any), then
    # apply enable/disable deltas before syncing artifacts.
    for worktree in await _config_render_targets(state):
        await _sync_worktree_config(state, worktree)


async def _config_render_targets(state: _RepoState) -> tuple[Path, ...]:
    if state.worktree != Path():
        return (state.worktree,)

    # repository-level targeting converges all branch-attached in-repo worktrees;
    # detached and out-of-tree worktrees are skipped with warnings.
    targets: list[Path] = []
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
        targets.append(worktree.path.relative_to(state.repo.root))
    if not targets:
        msg = "repository-wide config render found no eligible branch worktrees"
        raise OSError(msg)
    return tuple(targets)


async def _sync_worktree_config(
    state: _RepoState,
    worktree: Path,
) -> None:
    root = state.repo.root / worktree
    config = await Config.load(
        root,
        kube=state.kube,
        repo=state.repo,
        timeout=state.deadline.remaining(),
    )
    config.resources.update({resource.name: None for resource in state.enable})
    config.init = Config.Init(
        repo=state.repo,
        worktree=worktree,
    )
    async with config:
        _remove_disabled_resource_artifacts(root, config, state.disable)
        await config.sync()


def _remove_disabled_resource_artifacts(
    root: Path,
    config: Config,
    disabled: set[Resource[Any]],
) -> None:
    for resource in disabled:
        config.resources.pop(resource.name, None)
        for relative in sorted(resource.paths, key=lambda item: item.as_posix()):
            path = root / relative
            if not path.exists() and not path.is_symlink():
                continue
            if path.is_dir() and not path.is_symlink():
                shutil.rmtree(path, ignore_errors=True)
            else:
                path.unlink(missing_ok=True)


async def _make_initial_commit(
    state: _RepoState,
) -> None:
    worktree_path = await _initial_commit_worktree(state)
    if worktree_path is None:
        return
    await _commit_initial_changes(state, worktree_path)


async def _initial_commit_worktree(state: _RepoState) -> Path | None:
    if state.worktree != Path():
        return state.repo.root / state.worktree

    # repository-level target: use the HEAD or first branch-attached worktree for
    # commit operations.
    branch_targets = {
        wt.branch: wt.path.relative_to(state.repo.root)
        for wt in await state.repo.worktrees()
        if wt.branch is not None and wt.path.is_relative_to(state.repo.root)
    }
    head = await state.repo.head_branch()
    if head is not None and head in branch_targets:
        return state.repo.root / branch_targets[head]
    if not branch_targets:
        return None
    return (
        state.repo.root
        / sorted(
            branch_targets.values(),
            key=lambda value: value.as_posix(),
        )[0]
    )


async def _commit_initial_changes(state: _RepoState, worktree_path: Path) -> None:
    # only commit if there are no existing commits
    head = await run(
        ["git", "rev-parse", "--verify", "HEAD"],
        cwd=worktree_path,
        check=False,
        capture_output=True,
        timeout=state.deadline.remaining(),
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
        timeout=state.deadline.remaining(),
    )
    staged = await run(
        ["git", "diff", "--cached", "--quiet"],
        cwd=worktree_path,
        check=False,
        capture_output=True,
        timeout=state.deadline.remaining(),
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
        timeout=state.deadline.remaining(),
    )


async def _finalize(
    state: _RepoState,
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
            assume_yes=state.assume_yes,
        )

    state.mount_alias = await finalize_repository_mount(
        state.kube,
        repo_id=state.repo_id,
        target=target,
        alias=mount_alias,
        replace_existing=replace_existing,
        timeout=state.deadline.remaining(),
    )
    await refresh_repository_alias_for_path(
        state.kube,
        state.mount_alias,
        timeout=state.deadline.remaining(),
    )
    await prune_repository_mount_aliases(
        state.kube,
        repo_id=state.repo_id,
        timeout=state.deadline.remaining(),
    )


async def _converge_build_runtime(kube: Kube, *, timeout: float) -> None:
    msg = "build runtime timeout must be positive"
    deadline = Deadline.from_timeout(timeout, message=msg)
    await Namespace.upsert(
        kube,
        name=BERTRAND_NAMESPACE,
        timeout=deadline.remaining(),
    )
    await ensure_image_repository(kube, timeout=deadline.remaining())
    await ensure_image_repository_node_trust(kube, timeout=deadline.remaining())
    await assert_image_repository_node_trust(kube, timeout=deadline.remaining())
    await ensure_buildkit_pool(
        kube,
        timeout=deadline.remaining(),
        config_hash=await current_buildkit_config_hash(
            kube,
            timeout=deadline.remaining(),
        ),
    )
    await ensure_dra_backend(
        kube,
        image=control_plane_image(),
        timeout=deadline.remaining(),
    )
    await ensure_buildkit_build_controller(
        kube,
        image=control_plane_image(),
        timeout=deadline.remaining(),
    )


async def _converge_cluster_runtime(kube: Kube, *, timeout: float) -> None:
    msg = "cluster runtime timeout must be positive"
    deadline = Deadline.from_timeout(timeout, message=msg)
    await ensure_local_bertrand_node(kube, timeout=deadline.remaining())
    await ensure_rook_ceph_base(kube, timeout=deadline.remaining())
    await _converge_build_runtime(kube, timeout=deadline.remaining())
    await ensure_dev_backend(kube, timeout=deadline.remaining())
    await ensure_network_backend(kube, timeout=deadline.remaining())
    await ensure_ceph_storage_controller(
        kube,
        image=control_plane_image(),
        timeout=deadline.remaining(),
    )
    await wait_rook_ceph_ready(kube, timeout=deadline.remaining())


async def _converge_host_cluster_runtime(
    deadline: Deadline,
    *,
    start: bool,
) -> None:
    if start:
        await start_microk8s(timeout=deadline.remaining())
    await ensure_microk8s_kubeconfig(timeout=deadline.remaining())
    with await Kube.host(timeout=deadline.remaining()) as kube:
        await _converge_cluster_runtime(kube, timeout=deadline.remaining())


async def _mark_repo_failure(
    kube: Kube,
    *,
    repo_id: str,
    err: BaseException,
    deadline: Deadline,
) -> None:
    remaining = deadline.remaining()
    if remaining <= 0:
        return
    with contextlib.suppress(*_INIT_FAILURE_MARK_ERRORS):
        await mark_repository_volume_failed(
            kube,
            repo_id=repo_id,
            last_error=str(err),
            timeout=remaining,
        )


def _ensure_git_available() -> None:
    if shutil.which("git"):
        return
    msg = (
        "Bertrand requires 'git' to initialize a project repository, but it "
        "was not found in PATH."
    )
    raise OSError(msg)


async def _resolve_repo_target(
    kube: Kube,
    path: Path,
    *,
    deadline: Deadline,
) -> tuple[GitRepository, Path, Path, str | None]:
    raw_path = abspath(path)

    # run managed-alias resurrection before git path resolution so repository
    # discovery sees the recovered hidden mount layout if one was detached
    resurrected = await resurrect_repository_mount(
        kube,
        raw_path,
        timeout=deadline.remaining(),
    )
    recovered_repo_id = resurrected[0] if resurrected is not None else None

    # search for parent Git repository on target path, and then identify the
    # ancestor symlink that points to it, if any
    repo, worktree = await GitRepository.resolve(raw_path.resolve())
    return (
        repo,
        worktree,
        _repository_destination_path(raw_path, repo),
        recovered_repo_id,
    )


def _repository_destination_path(raw_path: Path, repo: GitRepository) -> Path:
    for candidate in (raw_path, *raw_path.parents):
        try:
            if candidate.resolve() == repo.root:
                return candidate
        except OSError:
            pass
    msg = (
        f"failed to derive repository destination path from {raw_path}; no "
        f"ancestor resolves to repository root {repo.root}"
    )
    raise OSError(msg)


async def _converge_repository(
    kube: Kube,
    *,
    repo: GitRepository,
    worktree: Path,
    target: Path,
    recovered_repo_id: str | None,
    enable: set[Resource[Any]],
    disable: set[Resource[Any]],
    deadline: Deadline,
    yes: bool,
) -> None:
    # synchronize uniquely for each repository path to limit global init lock
    # contention
    async with _RepoState.lock(repo.root, timeout=deadline.remaining()):
        await _assert_repo_clean(repo)
        state = _RepoState(
            kube=kube,
            repo=repo,
            target=target,
            worktree=worktree,
            repo_id=recovered_repo_id or repo.repo_id,
            mount_alias=None,
            enable=enable,
            disable=disable,
            assume_yes=yes,
            deadline=deadline,
        )
        try:
            await _ensure_repo_storage(state)
            await _ensure_bare_worktrees(state)
            await _ensure_repo_hooks(state)
            await _render_config_artifacts(state)
            await _make_initial_commit(state)
            await _finalize(state)
        except _INIT_REPO_STAGE_ERRORS as err:
            await _mark_repo_failure(
                state.kube,
                repo_id=state.repo_id,
                err=err,
                deadline=state.deadline,
            )
            raise


async def _assert_repo_clean(repo: GitRepository) -> None:
    if repo and await repo.dirty():
        msg = (
            f"repository at {repo.root} has uncommitted changes; please "
            "commit or stash them before calling `bertrand init`."
        )
        raise OSError(msg)


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
        msg = (
            "Cannot globally enable or disable resources without a worktree.  Please "
            "specify at least a repository path to configure resources within it."
        )
        raise OSError(msg)
    deadline = await _ensure_shared_runtime(
        timeout=timeout,
        yes=yes,
        converge_cluster=True,
    )

    # if no project root is provided, then we're done
    if path is None:
        return

    # fail fast if required tools are missing, and validate resource convergence input
    try:
        enabled, disabled = _parse_repo_resource_plan(enable=enable, disable=disable)
    except ValueError as err:
        raise ValueError(*err.args) from None
    _ensure_git_available()

    with await Kube.host(timeout=deadline.remaining()) as kube:
        repo, worktree, target, recovered_repo_id = await _resolve_repo_target(
            kube,
            path,
            deadline=deadline,
        )
        await _converge_repository(
            kube,
            repo=repo,
            worktree=worktree,
            target=target,
            recovered_repo_id=recovered_repo_id,
            enable=enabled,
            disable=disabled,
            deadline=deadline,
            yes=yes,
        )
