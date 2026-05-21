"""Shared helpers for CLI command implementations."""

from __future__ import annotations

from pathlib import Path

from bertrand.env.git import GitRepository


async def resolve_project_worktree(target: Path) -> tuple[GitRepository, Path]:
    """Resolve a CLI project target to a repository and concrete worktree.

    Parameters
    ----------
    target : Path
        User-provided repository or worktree path.

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
    repo, worktree = await GitRepository.resolve(target.expanduser().resolve())
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
