"""Shared helpers for CLI command implementations."""

from __future__ import annotations

from contextlib import asynccontextmanager
from pathlib import Path
from typing import TYPE_CHECKING

from bertrand.env.cli.util import warn
from bertrand.env.config.core import Config
from bertrand.env.git import GitRepository, abspath
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.ceph.mount import (
    prune_repository_mounts,
    refresh_repository_alias_for_path,
)

if TYPE_CHECKING:
    from collections.abc import AsyncIterator


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
    timeout: float,
) -> AsyncIterator[tuple[Kube, GitRepository, Path, Config]]:
    with await Kube.host(timeout=timeout) as kube:
        repo, worktree = await resolve_project_worktree(
            kube,
            target,
            timeout=timeout,
        )
        config = await Config.load(worktree, kube=kube, repo=repo, timeout=timeout)
        async with config:
            yield kube, repo, worktree, config
        await prune_repository_mounts_quietly(kube, timeout=timeout)


async def resolve_project_worktree(
    kube: Kube,
    target: Path,
    *,
    timeout: float,
) -> tuple[GitRepository, Path]:
    """Resolve a CLI project target to a repository and concrete worktree.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    target : Path
        User-provided repository or worktree path.
    timeout : float
        Maximum repository alias refresh budget.

    Returns
    -------
    tuple[GitRepository, Path]
        Resolved repository and concrete worktree path. Repository-root targets use
        the worktree attached to HEAD.

    Raises
    ------
    OSError
        If no initialized repository is found or HEAD cannot identify a worktree.
    """
    repo, worktree = await resolve_project_scope(kube, target, timeout=timeout)
    if not repo:
        msg = f"no initialized Git repository found for target: {target}"
        raise OSError(msg)
    if worktree != Path():
        return repo, repo.root / worktree
    head = await repo.head_worktree()
    if head is None:
        msg = (
            f"repository HEAD for {repo.root} must be attached to a local worktree; "
            "provide an explicit worktree path or attach HEAD to a branch before "
            "running this command."
        )
        raise OSError(msg)
    return repo, head.path


async def resolve_project_scope(
    kube: Kube,
    target: Path,
    *,
    timeout: float,
) -> tuple[GitRepository, Path]:
    """Resolve a CLI project target without substituting repository roots through HEAD.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    target : Path
        User-provided repository or worktree path.
    timeout : float
        Maximum repository alias refresh budget.

    Returns
    -------
    tuple[GitRepository, Path]
        Resolved repository and relative worktree path. Repository roots return
        `Path()`.
    """
    raw = abspath(target)
    await refresh_repository_alias_for_path(kube, raw, timeout=timeout)
    return await GitRepository.resolve(raw.resolve())


async def prune_repository_mounts_quietly(
    kube: Kube,
    *,
    timeout: float,
) -> None:
    """Run opportunistic repository mount pruning without masking command success.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum prune budget.
    """
    try:
        await prune_repository_mounts(kube, timeout=timeout)
    except (OSError, TimeoutError, ValueError) as err:
        warn(f"repository mount pruning did not converge: {err}")
