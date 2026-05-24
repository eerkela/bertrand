"""Shared helpers for internal CLI command implementations."""

from __future__ import annotations

import os
from contextlib import asynccontextmanager
from dataclasses import dataclass
from typing import TYPE_CHECKING

from bertrand.env.config.core import Config, _check_uuid
from bertrand.env.git import (
    INFINITY,
    PROJECT_MOUNT,
    REPO_ID_ENV,
    WORKTREE_MOUNT,
    GitRepository,
    inside_container,
    inside_image,
)
from bertrand.env.kube.api.client import Kube

if TYPE_CHECKING:
    from collections.abc import AsyncIterator


@dataclass(frozen=True)
class LiveProjectContext:
    """Active project context for commands running inside a dev Pod.

    Parameters
    ----------
    kube : Kube
        In-cluster Kubernetes API client for the dev Pod service account.
    config : Config
        Active parsed project configuration.
    repo_id : str
        Stable repository UUID injected into the workload environment.
    """

    kube: Kube
    config: Config
    repo_id: str


@asynccontextmanager
async def live_project_context(
    command: str,
    *,
    timeout: float = INFINITY,
) -> AsyncIterator[LiveProjectContext]:
    """Open the current dev Pod's project config using in-cluster credentials.

    Parameters
    ----------
    command : str
        User-facing command name used in diagnostics.
    timeout : float, optional
        Maximum config lock budget in seconds. If infinite, wait indefinitely.

    Yields
    ------
    LiveProjectContext
        In-cluster Kubernetes client and active config context.

    Raises
    ------
    RuntimeError
        If the command is not running inside a live Bertrand dev/workload Pod.
    """
    if not inside_container():
        msg = f"`bertrand {command}` requires a live Bertrand dev Pod context"
        raise RuntimeError(msg)

    repo_id = current_repo_id(command)
    with Kube.inside_cluster() as kube:
        async with await Config.load(
            WORKTREE_MOUNT,
            kube=kube,
            repo=GitRepository(git_dir=PROJECT_MOUNT / ".git"),
            timeout=timeout,
        ) as config:
            yield LiveProjectContext(kube=kube, config=config, repo_id=repo_id)


@asynccontextmanager
async def image_build_context(
    command: str,
    *,
    timeout: float = INFINITY,
) -> AsyncIterator[Config]:
    """Open a standalone image-build snapshot config.

    Parameters
    ----------
    command : str
        User-facing command name used in diagnostics.
    timeout : float, optional
        Stored operation budget for artifact rendering.

    Yields
    ------
    Config
        Active parsed snapshot configuration.

    Raises
    ------
    RuntimeError
        If the command is not running during a Bertrand image build.
    """
    if not inside_image() or inside_container():
        msg = f"`bertrand {command}` requires a Bertrand image-build context"
        raise RuntimeError(msg)

    async with await Config.load_snapshot(WORKTREE_MOUNT, timeout=timeout) as config:
        yield config


def current_repo_id(command: str) -> str:
    """Return the repository UUID injected into the current dev Pod.

    Parameters
    ----------
    command : str
        User-facing command name used in diagnostics.

    Returns
    -------
    str
        Normalized repository UUID hex string.

    Raises
    ------
    RuntimeError
        If the environment does not identify the current repository.
    """
    raw = os.environ.get(REPO_ID_ENV, "").strip()
    if not raw:
        msg = f"`bertrand {command}` requires {REPO_ID_ENV} in the dev Pod environment"
        raise RuntimeError(msg)
    return _check_uuid(raw)
