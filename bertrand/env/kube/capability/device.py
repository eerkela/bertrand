"""Device build-flag helpers for Secret-backed capabilities."""

from __future__ import annotations

import builtins
import sys
from collections.abc import Iterable
from typing import Literal, Protocol, cast

from ...config.core import KubeName, _check_kube_name, _check_uuid
from ..api import Kube
from .base import Capability

type DevicePermission = Literal["r", "w", "m", "rw", "rm", "wm", "rwm"]

DEVICE_PERMISSIONS = frozenset({"r", "w", "m", "rw", "rm", "wm", "rwm"})


class _DeviceRequest(Protocol):
    id: KubeName
    required: bool
    permissions: str


async def build_device_flags(
    kube: Kube,
    *,
    env_id: str,
    build: object,
    timeout: float,
    node: KubeName | None = None,
) -> tuple[str, ...]:
    """Build device CLI flags for one build request.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    env_id : str
        Environment UUID used for env-first resolution.
    build : object
        Build configuration object with an optional `devices` iterable.
    timeout : float
        Maximum request budget in seconds. If infinite, wait indefinitely.
    node : KubeName | None, optional
        Optional parent Kubernetes node used for node-scoped capability lookup.

    Returns
    -------
    tuple[str, ...]
        Flattened `--device` CLI args for the configured device requests.

    Raises
    ------
    TimeoutError
        If any Kubernetes request exceeds `timeout`.
    OSError
        If a required device capability is missing, metadata is invalid, or a
        device selector is empty.
    ValueError
        If `env_id`, `node`, a request ID, or permissions are invalid.
    """
    raw_requests = getattr(build, "devices", None)
    if raw_requests is None:
        return ()
    requests = tuple(cast(Iterable[_DeviceRequest], raw_requests))
    if not requests:
        return ()

    validated_env = _check_uuid(env_id)
    validated_node = _check_kube_name(node) if node is not None else None
    seen: set[KubeName] = set()
    flags: builtins.list[str] = []
    for request in requests:
        id = _check_kube_name(request.id)
        if id in seen:
            raise ValueError(f"duplicate device capability ID: {id!r}")
        seen.add(id)

        permissions = str(request.permissions)
        if permissions not in DEVICE_PERMISSIONS:
            raise ValueError(
                f"invalid device permissions {permissions!r}; must be a non-empty "
                "combination of 'r', 'w', and 'm'"
            )

        flags.extend(
            await _resolve_device_flag(
                kube=kube,
                id=id,
                required=request.required,
                permissions=permissions,
                env_id=validated_env,
                node=validated_node,
                timeout=timeout,
            )
        )

    return tuple(flags)


async def _resolve_device_flag(
    kube: Kube,
    *,
    id: KubeName,
    required: bool,
    permissions: str,
    env_id: str,
    node: KubeName | None,
    timeout: float,
) -> builtins.list[str]:
    capability = await Capability.resolve(
        kube,
        kind="device",
        id=id,
        env_id=env_id,
        node=node,
        required=False,
        timeout=timeout,
    )
    if capability is None:
        if required:
            raise OSError(f"missing required device selector: {id!r}")
        print(
            f"bertrand: optional device selector {id!r} was not found; continuing without it",
            file=sys.stderr,
        )
        return []

    selector = capability.text.strip()
    if not selector:
        raise OSError(f"cluster device capability {id!r} payload cannot be empty")
    return ["--device", f"{selector}:{permissions}"]
