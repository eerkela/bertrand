"""Legacy build capability flag helpers for old nerdctl image builds."""

from __future__ import annotations

import shutil
import sys
import uuid
from typing import TYPE_CHECKING, Any, Literal

from bertrand.env.config.core import KubeName, _check_kube_name, _check_uuid
from bertrand.env.git import atomic_write_bytes
from bertrand.env.host import CACHE_DIR
from bertrand.env.kube.capability.base import Capability

if TYPE_CHECKING:
    from pathlib import Path

    from bertrand.env.kube.api import Kube

CAPABILITY_DIR = CACHE_DIR / "capabilities"


def _stage_secret_payload(
    staged: Path,
    capability_id: KubeName,
    payload: bytes,
) -> Path:
    target = staged / "secrets" / capability_id
    target.parent.mkdir(parents=True, exist_ok=True)
    atomic_write_bytes(target, payload)
    target.chmod(0o600)
    return target


def cleanup_secret_staged(path: Path | None) -> None:
    """Delete staged legacy build-time secret payload files."""
    if path is None:
        return
    shutil.rmtree(path, ignore_errors=True)


async def _resolve_build_flag(
    kube: Kube,
    *,
    mode: Literal["secret", "ssh"],
    capability_id: KubeName,
    required: bool,
    env_id: str,
    timeout: float,
    staged: Path,
) -> list[str]:
    capability = await Capability.resolve(
        kube,
        kind=mode,
        capability_id=capability_id,
        env_id=env_id,
        required=required,
        timeout=timeout,
    )
    if capability is None:
        print(
            f"bertrand: optional {mode} {capability_id!r} was not found; "
            "continuing without it",
            file=sys.stderr,
        )
        return []

    target = _stage_secret_payload(
        staged,
        capability_id=_check_kube_name(capability_id),
        payload=capability.payload,
    )
    if mode == "secret":
        return ["--secret", f"id={capability_id},src={target}"]
    return ["--ssh", f"id={capability_id},src={target}"]


async def build_secret_flags(
    kube: Kube,
    *,
    env_id: str,
    build: object,
    timeout: float,
) -> tuple[tuple[str, ...], Path | None]:
    """Build legacy secret and SSH CLI flags for one nerdctl build request.

    Returns
    -------
    tuple[tuple[str, ...], Path | None]
        CLI flags and an optional staged payload directory to clean up.
    """
    secrets: tuple[Any, ...] = tuple(getattr(build, "secrets", ()))
    ssh: tuple[Any, ...] = tuple(getattr(build, "ssh", ()))
    if not secrets and not ssh:
        return (), None

    validated_env = _check_uuid(env_id)
    staged = CAPABILITY_DIR / uuid.uuid4().hex
    flags: list[str] = []
    try:
        for request in secrets:
            flags.extend(
                await _resolve_build_flag(
                    kube,
                    mode="secret",
                    capability_id=request.id,
                    required=request.required,
                    env_id=validated_env,
                    timeout=timeout,
                    staged=staged,
                )
            )
        for request in ssh:
            flags.extend(
                await _resolve_build_flag(
                    kube,
                    mode="ssh",
                    capability_id=request.id,
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


class DeviceConfigMap:
    """Legacy device flag resolver kept for old nerdctl build commands."""

    @classmethod
    async def build_flags(
        cls,
        kube: Kube,
        *,
        env_id: str,
        build: object,
        timeout: float,
    ) -> tuple[str, ...]:
        """Build legacy device CLI flags for one build request.

        Returns
        -------
        tuple[str, ...]
            Legacy `--device` CLI flags.

        Raises
        ------
        ValueError
            If a request declares a duplicate device capability ID.
        """
        requests: tuple[Any, ...] = tuple(getattr(build, "devices", ()))
        if not requests:
            return ()

        validated_env = _check_uuid(env_id)
        seen: set[KubeName] = set()
        flags: list[str] = []
        for request in requests:
            capability_id = _check_kube_name(request.id)
            if capability_id in seen:
                msg = f"duplicate device capability ID: {capability_id!r}"
                raise ValueError(msg)
            seen.add(capability_id)

            capability = await Capability.resolve_device(
                kube,
                capability_id=capability_id,
                env_id=validated_env,
                required=request.required,
                timeout=timeout,
            )
            if capability is None:
                print(
                    f"bertrand: optional device selector {capability_id!r} was not "
                    "found; continuing without it",
                    file=sys.stderr,
                )
                continue
            flags.extend(
                [
                    "--device",
                    capability.selector,
                ]
            )

        return tuple(flags)
