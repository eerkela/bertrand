"""Shared capability resolution and staging for kube runtime flows."""
from __future__ import annotations

import base64
import binascii
import json
import os
import re
import shutil
import sys
import uuid
from collections.abc import Mapping, Sequence
from pathlib import Path
from typing import Literal

from ..config import Bertrand
from ..config.core import TOMLKey
from ..run import BERTRAND_NAMESPACE, TIMEOUT, TOOLS_TMP_DIR, kubectl


def _capability_token(value: str) -> str:
    token = re.sub(r"[^a-z0-9_]+", "_", value.strip().lower()).strip("_")
    if not token:
        raise ValueError("capability ID cannot be empty")
    return token


def capability_secret_name(
    *,
    env_id: str,
    kind: Literal["ssh", "secret"],
    capability_id: str,
) -> str:
    """Format the cluster secret name for one build capability."""
    env_token = re.sub(r"[^a-z0-9]+", "", env_id.strip().lower())[:12]
    if not env_token:
        raise ValueError("environment ID cannot be empty")
    capability_token = _capability_token(capability_id).replace("_", "-")
    return f"bertrand-{env_token}-{kind}-{capability_token}"[:253]


async def _cluster_secret_data(
    *,
    name: str,
    timeout: float,
) -> dict[str, str] | None:
    result = await kubectl(
        [
            "get",
            "secret",
            name,
            "-o",
            "json",
        ],
        check=False,
        capture_output=True,
        timeout=timeout,
    )
    if result.returncode != 0:
        return None
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as err:
        raise OSError(
            f"cluster secret '{name}' returned malformed JSON payload"
        ) from err
    raw = payload.get("data", {})
    if not isinstance(raw, dict):
        raise OSError(f"cluster secret '{name}' is missing a valid data mapping")
    out: dict[str, str] = {}
    for key, value in raw.items():
        if isinstance(key, str) and isinstance(value, str):
            out[key] = value
    return out


def _decode_cluster_secret(
    *,
    name: str,
    data: Mapping[str, str],
    preferred_keys: Sequence[str],
) -> bytes:
    keys = [key for key in preferred_keys if key in data]
    if not keys and len(data) == 1:
        keys = list(data)
    if not keys:
        raise OSError(
            f"cluster secret '{name}' does not contain a recognized data key"
        )
    key = keys[0]
    try:
        return base64.b64decode(data[key], validate=True)
    except (binascii.Error, ValueError) as err:
        raise OSError(
            f"cluster secret '{name}' contains invalid base64 data for key '{key}'"
        ) from err


def device_env_var(capability_id: str) -> str:
    """Resolve env var used to source an optional/required device selector."""
    token = re.sub(r"[^A-Za-z0-9]+", "_", capability_id.strip().upper()).strip("_")
    if not token:
        raise ValueError("capability ID cannot be empty")
    return f"BERTRAND_DEVICE_{token}"


def cleanup_capability_dir(path: Path | None, *, suppress_errors: bool = True) -> None:
    """Remove staged capability files."""
    if path is None:
        return
    try:
        shutil.rmtree(path)
    except OSError:
        if not suppress_errors:
            raise


def _warn_optional(kind: str, capability_id: str) -> None:
    print(
        f"bertrand: optional {kind} capability '{capability_id}' was not found; "
        "continuing without it",
        file=sys.stderr,
    )


def _stage_payload(
    *,
    capability_dir: Path,
    category: Literal["secrets", "ssh"],
    capability_id: str,
    payload: bytes,
) -> Path:
    target = capability_dir / category / _capability_token(capability_id)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(payload)
    target.chmod(0o600)
    return target


async def _resolve_cluster_payload(
    *,
    env_id: str,
    timeout: float,
    kind: Literal["secret", "ssh"],
    capability_id: str,
    preferred_keys: Sequence[str],
) -> bytes | None:
    secret_name = capability_secret_name(
        env_id=env_id,
        kind=kind,
        capability_id=capability_id,
    )
    secret_data = await _cluster_secret_data(name=secret_name, timeout=timeout)
    if secret_data is None:
        return None
    return _decode_cluster_secret(
        name=secret_name,
        data=secret_data,
        preferred_keys=preferred_keys,
    )


async def build_capability_flags(
    *,
    env_id: str,
    tag: TOMLKey,
    build: Bertrand.Model.Build,
    timeout: float = TIMEOUT,
) -> tuple[list[str], Path | None]:
    """Resolve build capability flags and stage temporary payload files."""
    run_id = uuid.uuid4().hex
    capability_dir: Path | None = None
    flags: list[str] = []

    try:
        for req in build.secrets:
            payload = await _resolve_cluster_payload(
                env_id=env_id,
                timeout=timeout,
                kind="secret",
                capability_id=req.id,
                preferred_keys=("value", "secret", req.id),
            )
            if payload is None:
                if req.required:
                    secret_name = capability_secret_name(
                        env_id=env_id,
                        kind="secret",
                        capability_id=req.id,
                    )
                    raise OSError(
                        f"missing required build secret capability '{req.id}' "
                        f"(cluster secret '{secret_name}' not found in namespace "
                        f"'{BERTRAND_NAMESPACE}')"
                    )
                _warn_optional("secret", req.id)
                continue
            if capability_dir is None:
                capability_dir = (
                    TOOLS_TMP_DIR / "build-capabilities" / f"{run_id}.{uuid.uuid4().hex}"
                )
                capability_dir.mkdir(parents=True, exist_ok=True)
            target = _stage_payload(
                capability_dir=capability_dir,
                category="secrets",
                capability_id=req.id,
                payload=payload,
            )
            flags.extend(["--secret", f"id={req.id},src={target}"])

        for req in build.ssh:
            payload = await _resolve_cluster_payload(
                env_id=env_id,
                timeout=timeout,
                kind="ssh",
                capability_id=req.id,
                preferred_keys=("private_key", "id_rsa", "value", req.id),
            )
            if payload is None:
                if req.required:
                    secret_name = capability_secret_name(
                        env_id=env_id,
                        kind="ssh",
                        capability_id=req.id,
                    )
                    raise OSError(
                        f"missing required build ssh capability '{req.id}' "
                        f"(cluster secret '{secret_name}' not found in namespace "
                        f"'{BERTRAND_NAMESPACE}')"
                    )
                _warn_optional("ssh", req.id)
                continue
            if capability_dir is None:
                capability_dir = (
                    TOOLS_TMP_DIR / "build-capabilities" / f"{run_id}.{uuid.uuid4().hex}"
                )
                capability_dir.mkdir(parents=True, exist_ok=True)
            target = _stage_payload(
                capability_dir=capability_dir,
                category="ssh",
                capability_id=req.id,
                payload=payload,
            )
            flags.extend(["--ssh", f"{req.id}={target}"])

        for req in build.devices:
            env_var = device_env_var(req.id)
            selector = os.environ.get(env_var, "").strip()
            if not selector:
                if req.required:
                    raise OSError(
                        f"missing required build device capability '{req.id}' "
                        f"(set {env_var} to a CDI selector or device path)"
                    )
                _warn_optional("device", req.id)
                continue
            flags.extend(["--device", f"{selector}:{req.permissions}"])

        return flags, capability_dir
    except Exception:
        cleanup_capability_dir(capability_dir)
        raise
