"""Build-time secret and SSH helpers for Secret-backed capabilities."""

from __future__ import annotations

import builtins
import shutil
import sys
import uuid
from pathlib import Path
from typing import Literal

from ...config.core import KubeName, _check_kube_name, _check_uuid
from ...run import CACHE_DIR, atomic_write_bytes
from ..api import Kube
from .base import Capability

CAPABILITY_DIR = CACHE_DIR / "capabilities"


async def build_secret_flags(
    kube: Kube,
    *,
    env_id: str,
    build: object,
    timeout: float,
) -> tuple[tuple[str, ...], Path | None]:
    """Build secret and SSH CLI flags for one build request.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    env_id : str
        Environment UUID used for env-first then shared fallback lookups.
    build : object
        Build configuration object with optional `secrets` and `ssh` iterables.
    timeout : float
        Maximum request budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    tuple[tuple[str, ...], Path | None]
        A tuple containing emitted CLI args and the staged payload directory.
        If no secret or SSH requests exist, returns `((), None)`.

    Raises
    ------
    TimeoutError
        If any Kubernetes request exceeds `timeout`.
    OSError
        If required capabilities are missing, managed metadata is invalid, or
        payload staging fails.
    ValueError
        If `env_id` or a capability ID is invalid.
    """
    secrets = tuple(getattr(build, "secrets", ()))
    ssh = tuple(getattr(build, "ssh", ()))
    if not secrets and not ssh:
        return (), None

    validated_env = _check_uuid(env_id)
    staged = CAPABILITY_DIR / uuid.uuid4().hex
    flags: builtins.list[str] = []
    try:
        for request in secrets:
            flags.extend(
                await _resolve_build_flag(
                    kube=kube,
                    mode="secret",
                    id=request.id,
                    required=request.required,
                    env_id=validated_env,
                    timeout=timeout,
                    staged=staged,
                )
            )
        for request in ssh:
            flags.extend(
                await _resolve_build_flag(
                    kube=kube,
                    mode="ssh",
                    id=request.id,
                    required=request.required,
                    env_id=validated_env,
                    timeout=timeout,
                    staged=staged,
                )
            )
        return tuple(flags), staged
    except Exception:
        cleanup_secret_staged(staged)
        raise


def cleanup_secret_staged(path: Path | None) -> None:
    """Delete staged build-time secret payload files.

    Parameters
    ----------
    path : Path | None
        Staging directory path returned by :func:`build_secret_flags`. `None` is a
        no-op.

    Returns
    -------
    None
        This function returns `None`.
    """
    if path is None:
        return
    shutil.rmtree(path, ignore_errors=True)


async def _resolve_build_flag(
    kube: Kube,
    *,
    mode: Literal["secret", "ssh"],
    id: KubeName,
    required: bool,
    env_id: str,
    timeout: float,
    staged: Path,
) -> builtins.list[str]:
    validated_id = _check_kube_name(id)
    capability = await Capability.resolve(
        kube,
        kind=mode,
        id=validated_id,
        env_id=env_id,
        required=False,
        timeout=timeout,
    )
    if capability is None:
        if required:
            if mode == "secret":
                raise OSError(f"missing required secret: {validated_id!r}")
            raise OSError(f"missing required ssh credential: {validated_id!r}")
        if mode == "secret":
            print(
                f"bertrand: optional secret {validated_id!r} was not found; continuing without it",
                file=sys.stderr,
            )
        else:
            print(
                f"bertrand: optional ssh credential {validated_id!r} was not found; "
                "continuing without it",
                file=sys.stderr,
            )
        return []

    target = _stage_secret_payload(
        staged=staged,
        id=validated_id,
        payload=capability.payload,
    )
    if mode == "secret":
        return ["--secret", f"id={validated_id},src={target}"]
    return ["--ssh", f"id={validated_id},src={target}"]


def _stage_secret_payload(
    staged: Path,
    id: KubeName,
    payload: bytes,
) -> Path:
    target = staged / "secrets" / id
    target.parent.mkdir(parents=True, exist_ok=True)
    atomic_write_bytes(target, payload)
    target.chmod(0o600)
    return target
