"""Secret and SSH build-flag resolution for Kubernetes capabilities."""

from __future__ import annotations

import builtins
import hashlib
import shutil
import sys
import uuid
from pathlib import Path
from typing import Literal

from kubernetes import client as kube_client

from ...config.core import KubeName, _check_kube_name, _check_uuid
from ...run import BERTRAND_NAMESPACE, CACHE_DIR, atomic_write_bytes
from ..api import Kube
from ..secret import Secret

CAPABILITY_DIR = CACHE_DIR / "capabilities"
CAPABILITY_MANAGED_V1 = "bertrand.dev/capability-managed.v1"
CAPABILITY_ENV_ID_V1 = "bertrand.dev/capability-env-id.v1"
CAPABILITY_ID_V1 = "bertrand.dev/capability-id.v1"


def _secret_name(id: KubeName, env_id: str | None) -> KubeName:
    id = _check_kube_name(id)
    if env_id is not None:
        env_id = _check_uuid(env_id)

    parts: tuple[str, ...]
    if env_id is None:
        parts = ("shared", id)
    else:
        parts = (env_id, id)

    # include lengths before each token to keep hash input boundaries explicit
    digest = hashlib.sha256()
    for part in parts:
        encoded = part.encode("utf-8")
        digest.update(len(encoded).to_bytes(8, "big"))
        digest.update(encoded)

    return f"bertrand-secret-{digest.hexdigest()}"


def _build_flag(
    secret: Secret,
    *,
    mode: Literal["secret", "ssh"],
    id: KubeName,
    staged_dir: Path,
) -> tuple[str, str]:
    validated_id = _check_kube_name(id)
    target = _stage_secret_payload(staged=staged_dir, id=validated_id, payload=secret.value)
    if mode == "secret":
        return "--secret", f"id={validated_id},src={target}"
    return "--ssh", f"id={validated_id},src={target}"


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
        If no secret/ssh requests exist, returns `((), None)`.

    Raises
    ------
    TimeoutError
        If any Kubernetes request exceeds `timeout`.
    OSError
        If required secrets are missing, managed metadata is invalid, or payloads
        are malformed.
    ValueError
        If `env_id` is not a valid UUID.
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
    secret = await _resolve_secret(
        kube=kube,
        id=id,
        env_id=env_id,
        timeout=timeout,
    )
    if secret is None:
        if required:
            if mode == "secret":
                raise OSError(f"missing required secret: {id!r}")
            raise OSError(f"missing required ssh credential: {id!r}")
        if mode == "secret":
            print(
                f"bertrand: optional secret {id!r} was not found; continuing without it",
                file=sys.stderr,
            )
        else:
            print(
                f"bertrand: optional ssh credential {id!r} was not found; continuing without it",
                file=sys.stderr,
            )
        return []

    return builtins.list(_build_flag(secret, mode=mode, id=id, staged_dir=staged))


async def _resolve_secret(
    kube: Kube,
    *,
    id: KubeName,
    env_id: str,
    timeout: float,
) -> Secret | None:
    # env scope takes precedence over shared scope
    for scope in (env_id, None):
        name = _secret_name(id=id, env_id=scope)
        secret = await Secret.get(
            kube=kube,
            namespace=BERTRAND_NAMESPACE,
            timeout=timeout,
            name=name,
        )
        if secret is None:
            continue
        _assert_managed_secret(
            secret=secret,
            expected_name=name,
            id=id,
            env_id=scope,
        )
        return secret
    return None


def _assert_managed_secret(
    *,
    secret: Secret,
    expected_name: KubeName,
    id: KubeName,
    env_id: str | None,
) -> None:
    metadata = secret.obj.metadata or kube_client.V1ObjectMeta()
    name = (metadata.name or "").strip() or expected_name
    labels = metadata.labels or {}
    annotations = metadata.annotations or {}

    if labels.get(CAPABILITY_MANAGED_V1) != "true":
        raise OSError(
            f"cluster secret {name!r} collides with a Bertrand secret request but is unmanaged"
        )
    label_env = labels.get(CAPABILITY_ENV_ID_V1)
    expected_env = env_id or "shared"
    if label_env != expected_env:
        raise OSError(
            f"cluster secret {name!r} has mismatched {CAPABILITY_ENV_ID_V1!r}: "
            f"expected {expected_env!r}, got {label_env!r}"
        )
    annotation_id = annotations.get(CAPABILITY_ID_V1)
    if annotation_id is None:
        raise OSError(f"cluster secret {name!r} is missing annotation {CAPABILITY_ID_V1!r}")
    if _check_kube_name(annotation_id) != _check_kube_name(id):
        raise OSError(
            f"cluster secret {name!r} has mismatched {CAPABILITY_ID_V1!r}: "
            f"expected {id!r}, got {annotation_id!r}"
        )


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
