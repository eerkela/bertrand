"""Shared helpers for CLI command implementations."""
from __future__ import annotations

from pathlib import Path

from ..kube import Environment
from ..run import (
    ENV_ID_ENV,
    IMAGE_TAG_ENV,
    ContainerState,
    nerdctl_ids,
)

# TODO: devolve these helpers to a separate helper.py module, or refactor to make them
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
    """Resolve container IDs for an environment/tag scope and status filter."""
    if tag is None:
        labels = {ENV_ID_ENV: env.id}
    elif tag not in env.images:
        raise KeyError(f"no image found for tag: '{tag}'")
    else:
        labels = {ENV_ID_ENV: env.id, IMAGE_TAG_ENV: tag}
    return await nerdctl_ids(
        "container",
        labels=labels,
        status=status,
        timeout=timeout
    )


async def _cli_images(
    env: Environment,
    tag: str | None,
    *,
    timeout: float,
) -> list[str]:
    """Resolve image IDs for an environment/tag scope."""
    if tag is None:
        labels = {ENV_ID_ENV: env.id}
    else:
        labels = {ENV_ID_ENV: env.id, IMAGE_TAG_ENV: tag}
    return await nerdctl_ids("image", labels=labels, timeout=timeout)


def _recover_spec(worktree: Path, workload: str | None, tag: str | None) -> str:
    """Render a compact CLI target spec for diagnostics."""
    spec = str(worktree)
    if workload:
        spec += f"@{workload}"
    if tag:
        spec += f":{tag}"
    return spec


def _parse_output_format(value: str, *, allow_id: bool) -> tuple[str, str | None]:
    """Parse `bertrand ls/monitor` format strings into mode + template."""
    raw = value.strip()
    if not raw:
        raise ValueError("format must not be empty")

    mode, _, tail = raw.partition(" ")
    mode = mode.strip().lower()
    template = tail.strip() or None
    if mode == "table":
        return mode, template
    if template is not None:
        raise ValueError(
            "only table format accepts a template (expected: 'table' or "
            "'table <template>')"
        )
    if mode == "json":
        return mode, None
    if mode == "id":
        if not allow_id:
            raise ValueError("format 'id' is only supported for the 'ls' command")
        return mode, None

    if allow_id:
        expected = "id, json, table, or table <template>"
    else:
        expected = "json, table, or table <template>"
    raise ValueError(f"invalid format: {raw!r} (expected {expected})")
