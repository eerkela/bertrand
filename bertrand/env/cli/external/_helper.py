"""Shared helpers for CLI command implementations."""

from __future__ import annotations

from typing import TYPE_CHECKING

from bertrand.env.git import (
    ENV_ID_ENV,
    IMAGE_TAG_ENV,
)
from bertrand.env.legacy.nerdctl import ContainerState, nerdctl_ids

if TYPE_CHECKING:
    from pathlib import Path

    from bertrand.env.legacy.environment import Environment

# TODO: devolve these helpers to a separate helper.py module, or refactor to make them  # noqa: FIX002
# obsolete.
# -> Add them to `bertrand_git.py` if necessary, but we will only tackle this once we
# get back to refactoring the actual CLI.


async def _cli_containers(
    env: Environment,
    tag: str | None,
    *,
    status: tuple[ContainerState, ...] = ("created", "paused", "restarting", "running"),
    timeout: float,
) -> list[str]:
    """Resolve container IDs for an environment/tag scope and status filter.

    Parameters
    ----------
    env : Environment
        Loaded legacy environment metadata.
    tag : str | None
        Optional image tag to scope the runtime lookup.
    status : tuple[ContainerState, ...]
        Container states to include in the lookup.
    timeout : float
        Maximum time in seconds for the runtime lookup.

    Returns
    -------
    list[str]
        Matching container IDs.

    Raises
    ------
    KeyError
        If a requested tag is not known to the environment.
    """
    if tag is None:
        labels = {ENV_ID_ENV: env.id}
    elif tag not in env.images:
        msg = f"no image found for tag: '{tag}'"
        raise KeyError(msg)
    else:
        labels = {ENV_ID_ENV: env.id, IMAGE_TAG_ENV: tag}
    return await nerdctl_ids("container", labels=labels, status=status, timeout=timeout)


async def _cli_images(
    env: Environment,
    tag: str | None,
    *,
    timeout: float,
) -> list[str]:
    """Resolve image IDs for an environment/tag scope.

    Parameters
    ----------
    env : Environment
        Loaded legacy environment metadata.
    tag : str | None
        Optional image tag to scope the runtime lookup.
    timeout : float
        Maximum time in seconds for the runtime lookup.

    Returns
    -------
    list[str]
        Matching image IDs.
    """
    if tag is None:
        labels = {ENV_ID_ENV: env.id}
    else:
        labels = {ENV_ID_ENV: env.id, IMAGE_TAG_ENV: tag}
    return await nerdctl_ids("image", labels=labels, timeout=timeout)


def _recover_spec(worktree: Path, workload: str | None, tag: str | None) -> str:
    """Render a compact CLI target spec for diagnostics.

    Parameters
    ----------
    worktree : Path
        Worktree path from the parsed CLI target.
    workload : str | None
        Optional workload segment from the parsed CLI target.
    tag : str | None
        Optional tag segment from the parsed CLI target.

    Returns
    -------
    str
        Compact display form for command diagnostics.
    """
    spec = str(worktree)
    if workload:
        spec += f"@{workload}"
    if tag:
        spec += f":{tag}"
    return spec


def _parse_output_format(value: str, *, allow_id: bool) -> tuple[str, str | None]:
    """Parse `bertrand ls/monitor` format strings into mode and template.

    Parameters
    ----------
    value : str
        Raw CLI format string.
    allow_id : bool
        Whether the special `id` output mode is accepted.

    Returns
    -------
    tuple[str, str | None]
        Normalized mode and optional table template.

    Raises
    ------
    ValueError
        If the format string is empty or unsupported.
    """
    raw = value.strip()
    if not raw:
        msg = "format must not be empty"
        raise ValueError(msg)

    mode, _, tail = raw.partition(" ")
    mode = mode.strip().lower()
    template = tail.strip() or None
    if mode == "table":
        return mode, template
    if template is not None:
        msg = (
            "only table format accepts a template (expected: 'table' or "
            "'table <template>')"
        )
        raise ValueError(msg)
    if mode == "json":
        return mode, None
    if mode == "id":
        if not allow_id:
            msg = "format 'id' is only supported for the 'ls' command"
            raise ValueError(msg)
        return mode, None

    if allow_id:
        expected = "id, json, table, or table <template>"
    else:
        expected = "json, table, or table <template>"
    msg = f"invalid format: {raw!r} (expected {expected})"
    raise ValueError(msg)
