"""Cache volume orchestration helpers used by Bertrand runtime assembly.

This module provides a small public API for resolving resource-declared cache
mounts into deterministic volume names, formatting runtime mount arguments,
and cleaning stale cache volumes.

Notes
-----
All orchestration is intentionally backend-agnostic at the interface layer and
runtime-driven at execution time through managed `nerdctl` helpers.
"""
from __future__ import annotations

import hashlib
import json
import re
from pathlib import PosixPath
from typing import Any

from ..config.core import RESOURCE_NAMES, Resource
from ..run import BERTRAND_ENV, ENV_ID_ENV, nerdctl

CACHE_VOLUME_ENV: str = "BERTRAND_CACHE_VOLUME"
SANITIZE_RE = re.compile(r"[^a-zA-Z0-9._]+")


async def collect_mount_specs(config: Any, tag: str) -> list[tuple[str, PosixPath]]:
    """Collect and validate cache mount specifications for a build tag.

    Parameters
    ----------
    config : Any
        Active configuration context with resolved resources and registry.
    tag : str
        Active build tag used to query each resource's volume declarations.

    Returns
    -------
    list[tuple[str, PosixPath]]
        Deterministically ordered pairs of `(volume_name, target_path)`.

    Raises
    ------
    OSError
        If resource volume hooks fail, return invalid types, contain invalid
        targets, or produce non-serializable fingerprint payloads.

    Notes
    -----
    Names are derived as stable hashes over each volume's semantic fingerprint
    plus target path.  Target collisions across resources are rejected.
    """
    mounts: list[tuple[str, PosixPath]] = []
    target_owner: dict[str, str] = {}
    for name in sorted(config.resources):
        resource = RESOURCE_NAMES[name]
        try:
            declared = await resource.volumes(config, tag)
        except Exception as err:
            raise OSError(
                f"failed to resolve cache volumes for resource '{resource.name}': {err}"
            ) from err
        if not isinstance(declared, list):
            raise OSError(
                f"volume hook for resource '{resource.name}' must return a list, got "
                f"{type(declared).__name__}"
            )

        for raw in declared:
            if not isinstance(raw, Resource.Volume):
                raise OSError(
                    f"volume hook for resource '{resource.name}' must return "
                    f"`Resource.Volume` entries, got {type(raw).__name__}"
                )
            target = raw.target
            if not target.is_absolute():
                raise OSError(
                    f"resource '{resource.name}' mount target must be absolute: {target}"
                )
            if any(part in (".", "..") for part in target.parts):
                raise OSError(
                    f"resource '{resource.name}' mount target cannot contain '.' or '..' "
                    f"segments: {target}"
                )

            target_key = target.as_posix()
            owner = target_owner.setdefault(target_key, resource.name)
            if owner != resource.name:
                raise OSError(
                    f"volume target collision at '{target_key}' between resources "
                    f"'{owner}' and '{resource.name}'"
                )

            try:
                payload = {
                    "fingerprint": dict(raw.fingerprint),
                    "target": target_key,
                }
                text = json.dumps(
                    payload,
                    sort_keys=True,
                    separators=(",", ":"),
                    ensure_ascii=False,
                    allow_nan=False,
                )
                digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
            except ValueError as err:
                raise OSError(
                    f"resource '{resource.name}' mount '{target_key}' has invalid "
                    f"fingerprint payload: {err}"
                ) from err

            volume_name = SANITIZE_RE.sub(
                "-",
                f"bertrand-cache-{resource.name}-{digest[:20]}",
            ).strip("-")
            mounts.append((volume_name, target))

    mounts.sort()
    return mounts


async def format_volumes(config: Any, tag: str, env_id: str) -> list[str]:
    """Create/reuse cache volumes and emit runtime mount arguments.

    Parameters
    ----------
    config : Any
        Active configuration context.
    tag : str
        Active image/build tag.
    env_id : str
        Canonical environment UUID used for volume labels.

    Returns
    -------
    list[str]
        Flat argument list suitable for container create calls, formatted as
        repeated `["--mount", "type=volume,..."]` pairs.

    Raises
    ------
    ValueError
        If `env_id` is empty.
    OSError
        Propagated from `collect_mount_specs` validation failures.

    Notes
    -----
    The function is idempotent for existing volumes and emits deterministic
    mount ordering based on sorted mount specifications.
    """
    env_id = env_id.strip()
    if not env_id:
        raise ValueError("environment ID cannot be empty when formatting cache volumes")

    mounts: list[str] = []
    for volume_name, volume_target in await collect_mount_specs(config, tag):
        await nerdctl(
            [
                "volume",
                "create",
                "--label", f"{BERTRAND_ENV}=1",
                "--label", f"{CACHE_VOLUME_ENV}=1",
                "--label", f"{ENV_ID_ENV}={env_id}",
                volume_name,
            ],
            check=False,
            capture_output=True,
        )
        mounts.extend(["--mount", f"type=volume,src={volume_name},dst={volume_target}"])
    return mounts


async def gc_volumes(config: Any, env_id: str) -> None:
    """Garbage-collect stale labeled cache volumes for an environment.

    Parameters
    ----------
    config : Any
        Active configuration context.
    env_id : str
        Canonical environment UUID used to scope labeled cache volumes.

    Returns
    -------
    None
        This function is executed for side effects only.

    Raises
    ------
    ValueError
        If `env_id` is empty.
    OSError
        Propagated from `collect_mount_specs` when expected volume metadata is
        invalid.

    Notes
    -----
    This routine is best-effort against runtime command failures; if listing
    fails, cleanup is skipped.  Only volumes marked as dangling and carrying
    this environment's cache labels are candidates for removal.
    """
    env_id = env_id.strip()
    if not env_id:
        raise ValueError("environment ID cannot be empty when resolving cache volumes")
    bertrand = config.resources.get("bertrand")
    if bertrand is None:
        return

    expected = {
        volume_name
        for tag in bertrand.build
        for volume_name, _ in await collect_mount_specs(config, tag)
    }

    result = await nerdctl(
        [
            "volume",
            "ls",
            "-q",
            "--filter", f"label={BERTRAND_ENV}=1",
            "--filter", f"label={CACHE_VOLUME_ENV}=1",
            "--filter", f"label={ENV_ID_ENV}={env_id}",
            "--filter", "dangling=true",
        ],
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        return

    actual = {name.strip() for name in result.stdout.splitlines()}
    dangling = actual - expected
    dangling.discard("")
    if dangling:
        await nerdctl(
            ["volume", "rm", "-f", *sorted(dangling)],
            capture_output=True,
            check=False,
        )
