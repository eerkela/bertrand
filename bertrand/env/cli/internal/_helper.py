"""Shared helpers for internal CLI command implementations."""

from __future__ import annotations

from contextlib import asynccontextmanager
from typing import TYPE_CHECKING

from bertrand.env.config.core import Config
from bertrand.env.git import (
    REPO_MOUNT,
    WORKTREE_MOUNT,
    Deadline,
    GitRepository,
    inside_container,
    inside_image,
)
from bertrand.env.kube.api.client import Kube

if TYPE_CHECKING:
    from collections.abc import AsyncIterator


@asynccontextmanager
async def live_project_context(
    command: str,
    *,
    deadline: Deadline,
) -> AsyncIterator[tuple[Kube, Config, str]]:
    """Open the current dev Pod's project config using in-cluster credentials.

    Parameters
    ----------
    command : str
        User-facing command name used in diagnostics.
    deadline : Deadline
        Operation deadline for config metadata locking.

    Yields
    ------
    tuple[Kube, Config, str]
        In-cluster Kubernetes client, active config context, and repository UUID.

    Raises
    ------
    RuntimeError
        If the command is not running inside a live Bertrand dev/workload Pod.
    """
    if not inside_container():
        msg = f"`bertrand {command}` requires a live Bertrand dev Pod context"
        raise RuntimeError(msg)

    repo = GitRepository(git_dir=REPO_MOUNT / ".git")
    repo_id = repo.id
    with Kube.internal() as kube:
        config = await Config.load(
            WORKTREE_MOUNT,
            kube=kube,
            repo=repo,
            deadline=deadline,
        )
        async with config.activate(deadline=deadline):
            yield kube, config, repo_id


@asynccontextmanager
async def image_build_context(
    command: str,
    *,
    deadline: Deadline,
) -> AsyncIterator[Config]:
    """Open a standalone image-build config.

    Parameters
    ----------
    command : str
        User-facing command name used in diagnostics.
    deadline : Deadline
        Operation deadline for config parsing and artifact rendering.

    Yields
    ------
    Config
        Active parsed image-build configuration.

    Raises
    ------
    RuntimeError
        If the command is not running during a Bertrand image build.
    """
    if not inside_image() or inside_container():
        msg = f"`bertrand {command}` requires a Bertrand image-build context"
        raise RuntimeError(msg)

    config = await Config.load(WORKTREE_MOUNT, kube=None, deadline=deadline)
    async with config.activate(deadline=deadline):
        yield config
