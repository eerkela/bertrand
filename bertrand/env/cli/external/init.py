"""Bootstrap Bertrand's owned host runtime and project repositories."""

from __future__ import annotations

import contextlib
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
    STATE,
    CommandError,
    Deadline,
    GitRepository,
    State,
    abspath,
    atomic_write_text,
    confirm,
    run,
    symlink_points_to,
)
from bertrand.env.kube.api.client import (
    Kube,
    ensure_k0s_kubeconfig,
)
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
    refresh_repository_alias,
)
from bertrand.env.kube.ceph.storage import ensure_ceph_storage_controller
from bertrand.env.kube.ceph.volume import (
    DEFAULT_VOLUME_SIZE,
    list_repository_volume_claims,
    mark_repository_volume_failed,
    repository_volume_ready,
)
from bertrand.env.kube.control import control_plane_image
from bertrand.env.kube.dev import ensure_dev_backend
from bertrand.env.kube.namespace import Namespace
from bertrand.env.kube.network.bootstrap import ensure_network_backend
from bertrand.env.kube.node_identity import ensure_local_bertrand_node

INIT_REPO_STAGE_ERRORS: tuple[type[Exception], ...] = (
    OSError,
    RuntimeError,
    TimeoutError,
    ValueError,
    CommandError,
    ValidationError,
)
INIT_FAILURE_MARK_ERRORS: tuple[type[Exception], ...] = (
    OSError,
    RuntimeError,
    TimeoutError,
    ValueError,
    ValidationError,
)
MANAGED_GIT_HOOKS: tuple[tuple[Path, Path, bool], ...] = (
    (Path("reference_transaction.py"), Path("hooks/reference-transaction"), True),
    (Path("bertrand_git.py"), Path("hooks/bertrand_git.py"), False),
)
PROTECTED_DISABLE_RESOURCES: frozenset[str] = frozenset({"bertrand", "python"})


# TODO: all of this junk should be wayyy simpler


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
    repo_state: State.Repository
    target: Path
    worktree: GitRepository.Worktree | None
    repo_id: str
    mount_alias: Path | None
    enable: set[Resource[Any]]
    disable: set[Resource[Any]]
    yes: bool
    deadline: Deadline

    def __post_init__(self) -> None:
        self.target = abspath(self.target)


async def _ensure_repo_storage(state: _RepoState) -> None:
    volumes = await list_repository_volume_claims(
        state.kube,
        state.repo_id,
        deadline=state.deadline,
    )

    # no existing claim was found: prompt before converting an unmanaged repo
    if not volumes and await state.repo.exists(deadline=state.deadline):
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
        if not confirm(prompt, yes=state.yes):
            msg = "repository conversion declined by user"
            raise PermissionError(msg)

    state.mount_alias = await ensure_repository_mount(
        state.kube,
        repo_id=state.repo_id,
        deadline=state.deadline,
        size_request=DEFAULT_VOLUME_SIZE,
        target=state.target,
        volumes=volumes,
    )


async def _ensure_bare_worktrees(state: _RepoState) -> None:
    if state.mount_alias is None:
        msg = "bare-worktree convergence requires a mounted repository alias"
        raise OSError(msg)
    mount = state.repo_state.git
    target_branch = _target_worktree_branch(state)
    source_exists = await state.repo.exists(deadline=state.deadline)

    # new repository: initialize a bare history store and create the first worktree
    if not source_exists:
        await _ensure_new_bare_repository(
            mount,
            alias=state.mount_alias,
            deadline=state.deadline,
        )
        state.repo = mount
        return

    # conversion path: mirror all refs from source into destination, then converge
    # one worktree per local branch under refs/heads/*
    if state.repo != mount:
        await _mirror_source_repo(state, mount)

    # assert mounted directory is well-formed, then sync worktrees
    await _assert_bare_managed_repo(
        mount,
        alias=state.mount_alias,
        deadline=state.deadline,
    )
    await mount.sync_worktrees(deadline=state.deadline)
    state.repo = mount
    await _remap_target_worktree(state, target_branch=target_branch)


def _target_worktree_branch(state: _RepoState) -> str | None:
    # specific-worktree mode: capture source branch identity before any conversion so
    # we can remap to canonical converged worktree paths afterwards.
    if state.worktree is None:
        return None
    if state.worktree.branch is None:
        msg = (
            f"targeted worktree {state.worktree.path} has detached HEAD; please attach "
            "it to a branch before running `bertrand init`."
        )
        raise OSError(msg)
    return state.worktree.branch


async def _ensure_new_bare_repository(
    mount: GitRepository,
    *,
    alias: Path,
    deadline: Deadline,
) -> None:
    branch = await GitRepository.default_branch(deadline=deadline)
    default_worktree = alias / branch
    # destination repo is also uninitialized - create initial worktree
    if not await mount.exists(deadline=deadline):
        await mount.init(branch=branch, bare=True, deadline=deadline)
        await mount.create_worktree(
            branch,
            target=default_worktree,
            create_branch=True,
            deadline=deadline,
        )
        return
    if await mount.bare(deadline=deadline):
        if not any(
            wt.branch == branch for wt in await mount.worktrees(deadline=deadline)
        ):
            await mount.create_worktree(
                branch,
                target=default_worktree,
                create_branch=True,
                deadline=deadline,
            )
        await mount.sync_worktrees(deadline=deadline)
        return
    msg = f"managed destination repository at {alias} must be bare"
    raise OSError(msg)


async def _mirror_source_repo(state: _RepoState, mount: GitRepository) -> None:
    if await state.repo.dirty(deadline=state.deadline):
        msg = (
            f"cannot convert repository at {state.repo.root}: "
            "worktree has uncommitted changes"
        )
        raise OSError(msg)
    await mount.mirror_from(state.repo, deadline=state.deadline)


async def _assert_bare_managed_repo(
    mount: GitRepository,
    *,
    alias: Path,
    deadline: Deadline,
) -> None:
    if not await mount.exists(deadline=deadline):
        msg = f"managed destination repository does not exist at {alias}"
        raise OSError(msg)
    if not await mount.bare(deadline=deadline):
        msg = f"managed destination repository at {alias} must be bare"
        raise OSError(msg)


async def _remap_target_worktree(
    state: _RepoState,
    *,
    target_branch: str | None,
) -> None:
    if target_branch is None:
        return
    match = next(
        (
            wt
            for wt in await state.repo.worktrees(deadline=state.deadline)
            if wt.branch == target_branch
        ),
        None,
    )
    if match is None:
        msg = (
            f"failed to map targeted source worktree branch {target_branch!r} into "
            f"converged repository at {state.repo.root}"
        )
        raise OSError(msg)
    state.worktree = match


async def _ensure_repo_hooks(state: _RepoState) -> None:
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


async def _render_config_artifacts(state: _RepoState) -> None:
    # render all targeted worktrees based on existing configuration (if any), then
    # apply enable/disable deltas before syncing artifacts.
    for worktree in await _config_render_targets(state):
        await _sync_worktree_config(state, worktree)


async def _config_render_targets(
    state: _RepoState,
) -> tuple[GitRepository.Worktree, ...]:
    if state.worktree is not None:
        return (state.worktree,)

    # repository-level targeting converges all branch-attached in-repo worktrees;
    # detached and out-of-tree worktrees are skipped with warnings.
    targets: list[GitRepository.Worktree] = []
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
        targets.append(worktree)
    if not targets:
        msg = "repository-wide config render found no eligible branch worktrees"
        raise OSError(msg)
    return tuple(targets)


async def _sync_worktree_config(
    state: _RepoState,
    worktree: GitRepository.Worktree,
) -> None:
    root = worktree.path
    relative = worktree.path.relative_to(state.repo.root)
    config = await Config.load(
        root,
        kube=state.kube,
        repo=state.repo,
        deadline=state.deadline,
    )
    config.resources.update({resource.name: None for resource in state.enable})
    config.init = Config.Init(
        repo=state.repo,
        worktree=relative,
    )
    async with config.activate(deadline=state.deadline):
        _remove_disabled_resource_artifacts(root, config, state.disable)
        await config.sync(deadline=state.deadline)


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


async def _make_initial_commit(state: _RepoState) -> None:
    worktree_path = await _initial_commit_worktree(state)
    if worktree_path is None:
        return
    await _commit_initial_changes(state, worktree_path)


async def _initial_commit_worktree(state: _RepoState) -> Path | None:
    if state.worktree is not None:
        return state.worktree.path

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
        deadline=state.deadline,
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
        deadline=state.deadline,
    )
    staged = await run(
        ["git", "diff", "--cached", "--quiet"],
        cwd=worktree_path,
        check=False,
        capture_output=True,
        deadline=state.deadline,
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
        deadline=state.deadline,
    )


async def _finalize(state: _RepoState) -> None:
    mount_alias = state.mount_alias
    if mount_alias is None:
        msg = "cannot finalize repository convergence without a mount alias"
        raise OSError(msg)
    target = state.target
    hidden_mount = state.repo_state.mount
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
            yes=state.yes,
        )

    state.mount_alias = await finalize_repository_mount(
        state.kube,
        repo_id=state.repo_id,
        target=target,
        alias=mount_alias,
        replace_existing=replace_existing,
        deadline=state.deadline,
    )
    state.repo_state = await refresh_repository_alias(
        state.kube,
        alias=state.mount_alias,
        repo=state.repo_state,
        deadline=state.deadline,
    )
    state.repo_id = state.repo_state.repo_id or state.repo_state.mount_id
    await prune_repository_mount_aliases(
        state.kube,
        repo_id=state.repo_id,
        deadline=state.deadline,
    )


async def _mark_repo_failure(
    kube: Kube,
    *,
    repo_id: str,
    err: BaseException,
    deadline: Deadline,
) -> None:
    remaining = deadline.remaining
    if remaining <= 0:
        return
    with contextlib.suppress(*INIT_FAILURE_MARK_ERRORS):
        await mark_repository_volume_failed(
            kube,
            repo_id=repo_id,
            last_error=str(err),
            deadline=deadline,
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
) -> tuple[GitRepository, GitRepository.Worktree | None, Path, State.Repository | None]:
    raw_path = abspath(path)

    managed = STATE.resolve_alias(raw_path)
    if managed is not None:
        alias, repo_state = managed
        repo_state = await refresh_repository_alias(
            kube,
            alias=alias,
            repo=repo_state,
            deadline=deadline,
        )
        repo, worktree = await GitRepository.resolve(
            repo_state.path(raw_path.relative_to(alias)),
            deadline=deadline,
        )
        return repo, worktree, alias, repo_state

    # No managed alias was found, so init is allowed to interpret the target as a new
    # repository initialization or unmanaged repository conversion.
    repo, worktree = await GitRepository.resolve(raw_path.resolve(), deadline=deadline)
    return (
        repo,
        worktree,
        _repository_destination_path(raw_path, repo),
        None,
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


async def _repository_identity(
    kube: Kube,
    repo: GitRepository,
    *,
    repo_state: State.Repository | None,
    deadline: Deadline,
) -> tuple[str, GitRepository, State.Repository]:
    """Resolve the stable repository ID and repair local catalogue drift.

    Returns
    -------
    tuple[str, GitRepository, State.Repository]
        Stable repository UUID and the repository object to converge.
    """
    if repo_state is not None:
        repaired = await repo_state.normalize(deadline=deadline)
        repo_id = repaired.repo_id or repaired.mount_id
        return repo_id, repaired.git, repaired

    metadata_id = repo.metadata_id
    if metadata_id is None or await repository_volume_ready(
        kube,
        repo_id=metadata_id,
        deadline=deadline,
    ):
        repo_id = repo.path_id
    else:
        repo_id = metadata_id
    repo_state = STATE.repo(repo_id)
    return repo_id, repo, repo_state


async def _converge_repository(
    kube: Kube,
    *,
    repo: GitRepository,
    worktree: GitRepository.Worktree | None,
    target: Path,
    repo_state: State.Repository | None,
    enable: set[Resource[Any]],
    disable: set[Resource[Any]],
    deadline: Deadline,
    yes: bool,
) -> None:
    repo_id, repo, repo_state = await _repository_identity(
        kube,
        repo,
        repo_state=repo_state,
        deadline=deadline,
    )
    repo_lock = repo_state.lock
    await repo_lock.lock(deadline)
    try:
        await _assert_repo_clean(repo, deadline=deadline)
        state = _RepoState(
            kube=kube,
            repo=repo,
            repo_state=repo_state,
            target=target,
            worktree=worktree,
            repo_id=repo_id,
            mount_alias=None,
            enable=enable,
            disable=disable,
            yes=yes,
            deadline=deadline,
        )
        try:
            await _ensure_repo_storage(state)
            await _ensure_bare_worktrees(state)
            await _ensure_repo_hooks(state)
            await _render_config_artifacts(state)
            await _make_initial_commit(state)
            await _finalize(state)
        except INIT_REPO_STAGE_ERRORS as err:
            await _mark_repo_failure(
                state.kube,
                repo_id=state.repo_id,
                err=err,
                deadline=state.deadline,
            )
            raise
    finally:
        await repo_lock.unlock(ignore_errors=True)


async def _assert_repo_clean(repo: GitRepository, *, deadline: Deadline) -> None:
    if await repo.exists(deadline=deadline) and await repo.dirty(deadline=deadline):
        msg = (
            f"repository at {repo.root} has uncommitted changes; please "
            "commit or stash them before calling `bertrand init`."
        )
        raise OSError(msg)


@dataclass(frozen=True, slots=True)
class ExternalInit:
    """A command closure that bootstraps host prerequisites and project repositories.

    Attributes
    ----------
    path : Path | str | None
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
    disable : list[str]
        List of resources to disable at the resolved worktree.  Each component is a
        comma-separated list of resource names or aliases, which are resolved to their
        corresponding, unique `Resource` implementations.
    yes : bool
        Whether to auto-accept prompts during host bootstrap stages.
    """

    path: Path | str | None
    enable: list[str]
    disable: list[str]
    yes: bool

    async def _bootstrap_control_plane(
        self,
        kube: Kube,
        deadline: Deadline,
    ) -> None:
        await ensure_local_bertrand_node(kube, deadline=deadline)
        await ensure_rook_ceph_base(kube, deadline=deadline)
        await Namespace.upsert(
            kube,
            name=BERTRAND_NAMESPACE,
            deadline=deadline,
        )
        await ensure_image_repository(kube, deadline=deadline)
        await ensure_image_repository_node_trust(kube=kube, deadline=deadline)
        # TODO: the assertion should be built into
        # `ensure_image_repository_node_trust` rather than repeated here  It may
        # also be beneficial to centralize it in a single `ensure_image_repository`
        # and then `ensure_buildkit`, etc.
        await assert_image_repository_node_trust(kube, deadline=deadline)
        await ensure_buildkit_pool(
            kube,
            deadline=deadline,
            config_hash=await current_buildkit_config_hash(
                kube,
                deadline=deadline,
            ),
        )
        await ensure_buildkit_build_controller(
            kube,
            image=control_plane_image(),
            deadline=deadline,
        )
        await ensure_dra_backend(
            kube,
            image=control_plane_image(),
            deadline=deadline,
        )
        await ensure_dev_backend(kube, deadline=deadline)
        await ensure_network_backend(kube, deadline=deadline)
        await ensure_ceph_storage_controller(
            kube,
            image=control_plane_image(),
            deadline=deadline,
        )
        await wait_rook_ceph_ready(kube, deadline=deadline)

    async def __call__(self, deadline: Deadline) -> None:
        """Run the command.

        Parameters
        ----------
        deadline : Deadline
            Deadline for the entire init operation, including host bootstrapping and
            repository convergence.

        Raises
        ------
        OSError
            If Git is not found, or the project root repository is invalid.
        ValueError
            If any resource names in `enable`/`disable` are invalid, or if required
            core resources are disabled.
        """
        raw_path = self.path
        path = None if raw_path is None else abspath(Path(raw_path))
        if path is None and (self.enable or self.disable):
            msg = (
                "Cannot globally enable or disable resources without a worktree.  "
                "Please specify at least a repository path to configure resources "
                "within it."
            )
            raise OSError(msg)

        # idempotently bootstrap host cluster infrastructure
        ignore_errors = False
        kube: Kube | None = None
        await STATE.init(deadline=deadline, yes=self.yes)
        try:
            await Kube.init(deadline=deadline, yes=self.yes)
            ensure_k0s_kubeconfig(deadline=deadline)
            kube = Kube.external()
            await self._bootstrap_control_plane(kube=kube, deadline=deadline)
            # TODO: acquire the repo lock within this context, then include a following
            # try/catch block that transitions from the cluster lock to the repository
            # lock
        except:
            ignore_errors = True
            if kube is not None:
                kube.close()
            raise
        finally:
            await STATE.lock.unlock(ignore_errors=ignore_errors)

        # finish bootstrapping cluster control plane, then converge repository volume
        # under a separate lock to reduce contention
        try:
            # if no repository root is provided, then we're done
            if path is None:
                return

            # TODO: continue init refactor from here

            # fail fast if required tools are missing, and validate convergence input
            try:
                enabled, disabled = _parse_repo_resource_plan(
                    enable=self.enable,
                    disable=self.disable,
                )
            except ValueError as err:
                raise ValueError(*err.args) from None
            _ensure_git_available()

            repo, worktree, target, repo_state = await _resolve_repo_target(
                kube,
                path,
                deadline=deadline,
            )
            await _converge_repository(
                kube,
                repo=repo,
                worktree=worktree,
                target=target,
                repo_state=repo_state,
                enable=enabled,
                disable=disabled,
                deadline=deadline,
                yes=self.yes,
            )
        finally:
            if kube is not None:
                kube.close()


async def ensure_shared_runtime_installed(
    *,
    deadline: Deadline,
    yes: bool,
) -> None:
    """Install host prerequisites without starting or joining the k0s cluster."""
    await STATE.init(yes=yes, deadline=deadline)
    await STATE.lock.unlock()


async def _converge_host_cluster_runtime(
    deadline: Deadline,
    *,
    start: bool,
) -> None:
    """Converge the local cluster control plane after host runtime installation."""
    if start:
        await Kube.init(deadline=deadline, yes=False)
    ensure_k0s_kubeconfig(deadline=deadline)
    runtime = ExternalInit(path=None, enable=[], disable=[], yes=False)
    with Kube.external() as kube:
        await runtime._bootstrap_control_plane(kube, deadline)
