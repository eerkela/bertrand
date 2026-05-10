"""Legacy nerdctl cache-volume argument formatting helpers."""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import PosixPath
from typing import Any

from bertrand.env.config.core import RESOURCE_NAMES, Resource
from bertrand.env.legacy.nerdctl import nerdctl
from bertrand.env.git import BERTRAND_ENV, ENV_ID_ENV

CACHE_VOLUME_ENV: str = "BERTRAND_CACHE_VOLUME"
SANITIZE_RE = re.compile(r"[^a-zA-Z0-9._]+")


async def collect_mount_specs(config: Any, tag: str) -> list[tuple[str, PosixPath]]:
    """Collect and validate legacy resource cache mount specifications."""
    mounts: list[tuple[str, PosixPath]] = []
    target_owner: dict[str, str] = {}
    for name in sorted(config.resources):
        resource = RESOURCE_NAMES[name]
        try:
            declared = await resource.volumes(config, tag)
        except Exception as err:  # noqa: BLE001
            msg = f"failed to resolve cache volumes for resource {resource.name!r}: {err}"
            raise OSError(msg) from err
        if not isinstance(declared, list):
            msg = (
                f"volume hook for resource {resource.name!r} must return a list, "
                f"got {type(declared).__name__}"
            )
            raise OSError(msg)

        for raw in declared:
            if not isinstance(raw, Resource.Volume):
                msg = (
                    f"volume hook for resource {resource.name!r} must return "
                    f"`Resource.Volume` entries, got {type(raw).__name__}"
                )
                raise OSError(msg)
            target = raw.target
            if not target.is_absolute():
                msg = f"resource {resource.name!r} mount target must be absolute: {target}"
                raise OSError(msg)
            if any(part in (".", "..") for part in target.parts):
                msg = (
                    f"resource {resource.name!r} mount target cannot contain '.' or "
                    f"'..' segments: {target}"
                )
                raise OSError(msg)

            target_key = target.as_posix()
            owner = target_owner.setdefault(target_key, resource.name)
            if owner != resource.name:
                msg = (
                    f"volume target collision at {target_key!r} between resources "
                    f"{owner!r} and {resource.name!r}"
                )
                raise OSError(msg)

            try:
                payload = {"fingerprint": dict(raw.fingerprint), "target": target_key}
                text = json.dumps(
                    payload,
                    sort_keys=True,
                    separators=(",", ":"),
                    ensure_ascii=False,
                    allow_nan=False,
                )
                digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
            except ValueError as err:
                msg = (
                    f"resource {resource.name!r} mount {target_key!r} has invalid "
                    f"fingerprint payload: {err}"
                )
                raise OSError(msg) from err

            volume_name = SANITIZE_RE.sub(
                "-",
                f"bertrand-cache-{resource.name}-{digest[:20]}",
            ).strip("-")
            mounts.append((volume_name, target))

    mounts.sort()
    return mounts


async def format_volumes(config: Any, tag: str, env_id: str) -> list[str]:
    """Create or reuse legacy nerdctl cache volumes and render mount flags."""
    env_id = env_id.strip()
    if not env_id:
        msg = "environment ID cannot be empty when formatting cache volumes"
        raise ValueError(msg)

    mounts: list[str] = []
    for volume_name, volume_target in await collect_mount_specs(config, tag):
        await nerdctl(
            [
                "volume",
                "create",
                "--label",
                f"{BERTRAND_ENV}=1",
                "--label",
                f"{CACHE_VOLUME_ENV}=1",
                "--label",
                f"{ENV_ID_ENV}={env_id}",
                volume_name,
            ],
            check=False,
            capture_output=True,
        )
        mounts.extend(["--mount", f"type=volume,src={volume_name},dst={volume_target}"])
    return mounts

