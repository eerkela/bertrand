"""Shared helpers for CLI command implementations."""

from __future__ import annotations

from contextlib import asynccontextmanager
from typing import TYPE_CHECKING

from bertrand.env.config.core import Config
from bertrand.env.git import Deadline, GitRepository, abspath
from bertrand.env.git.bertrand_git import warn
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.ceph.mount import (
    prune_repository_mounts,
    refresh_repository_alias_for_path,
)

if TYPE_CHECKING:
    from collections.abc import AsyncIterator
    from pathlib import Path


# TODO: _project_command_context is horrific.  We should probably just do this in
# main.py anyways, and pass those objects down to the command implementations instead
# of having them all import this same helper module, which should be entirely
# eliminated.

# -> We also absolutely do not want to open multiple kube client handlers per command
# invocation, since that's totally unnecessary and wasteful.


@asynccontextmanager
async def _project_command_context(
    target: Path,
    *,
    deadline: Deadline,
) -> AsyncIterator[tuple[Kube, GitRepository, Path, Config]]:
    with await Kube.host(deadline=deadline) as kube:
        repo, worktree = await resolve_project_worktree(
            kube,
            target,
            deadline=deadline,
        )
        config = await Config.load(
            worktree.path,
            kube=kube,
            repo=repo,
            deadline=deadline,
        )
        async with config.activate(deadline=deadline):
            yield kube, repo, worktree.path, config
        await prune_repository_mounts_quietly(kube, deadline=deadline)


async def resolve_project_worktree(
    kube: Kube,
    target: Path,
    *,
    deadline: Deadline,
) -> tuple[GitRepository, GitRepository.Worktree]:
    """Resolve a CLI project target to a repository and concrete worktree.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    target : Path
        User-provided repository or worktree path.
    deadline : Deadline
        Operation deadline for repository alias refresh.

    Returns
    -------
    tuple[GitRepository, GitRepository.Worktree]
        Resolved repository and concrete worktree. Repository-root targets use the
        worktree attached to HEAD.

    Raises
    ------
    OSError
        If no initialized repository is found or HEAD cannot identify a worktree.
    """
    repo, worktree = await resolve_project_scope(kube, target, deadline=deadline)
    if not await repo.exists(deadline=deadline):
        msg = f"no initialized Git repository found for target: {target}"
        raise OSError(msg)
    if worktree is not None:
        return repo, worktree
    head = await repo.head_worktree(deadline=deadline)
    if head is None:
        msg = (
            f"repository HEAD for {repo.root} must be attached to a local worktree; "
            "provide an explicit worktree path or attach HEAD to a branch before "
            "running this command."
        )
        raise OSError(msg)
    return repo, head


async def resolve_project_scope(
    kube: Kube,
    target: Path,
    *,
    deadline: Deadline,
) -> tuple[GitRepository, GitRepository.Worktree | None]:
    """Resolve a CLI project target without substituting repository roots through HEAD.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    target : Path
        User-provided repository or worktree path.
    deadline : Deadline
        Operation deadline for repository alias refresh.

    Returns
    -------
    tuple[GitRepository, GitRepository.Worktree | None]
        Resolved repository and explicit worktree, if the target points to one.
        Repository roots return None.
    """
    raw = abspath(target)
    await refresh_repository_alias_for_path(kube, raw, deadline=deadline)
    return await GitRepository.resolve(raw.resolve(), deadline=deadline)


async def prune_repository_mounts_quietly(
    kube: Kube,
    *,
    deadline: Deadline,
) -> None:
    """Run opportunistic repository mount pruning without masking command success.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    deadline : Deadline
        Operation deadline for mount pruning.
    """
    try:
        await prune_repository_mounts(kube, deadline=deadline)
    except (OSError, TimeoutError, ValueError) as err:
        warn(f"repository mount pruning did not converge: {err}")
