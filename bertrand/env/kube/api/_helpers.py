"""Private Kubernetes wrapper infrastructure."""

from __future__ import annotations

from typing import TYPE_CHECKING, Literal

import kubernetes

from bertrand.env.git import until
from bertrand.env.kube.api.client import KubeApiError

if TYPE_CHECKING:
    from collections.abc import Awaitable, Callable, Collection, Mapping

    from .client import Kube

KUBE_WAIT_POLL_INTERVAL_SECONDS = 0.5
type DeletionPropagationPolicy = Literal["Background", "Foreground", "Orphan"]


def _delete_options(
    *,
    kind: str,
    propagation_policy: DeletionPropagationPolicy | None = None,
    grace_period_seconds: int | None = None,
) -> kubernetes.client.V1DeleteOptions:
    if propagation_policy is not None and propagation_policy not in (
        "Background",
        "Foreground",
        "Orphan",
    ):
        msg = f"invalid {kind} deletion propagation policy: {propagation_policy!r}"
        raise ValueError(msg)
    if grace_period_seconds is not None and grace_period_seconds < 0:
        msg = f"{kind} deletion grace period cannot be negative"
        raise ValueError(msg)
    return kubernetes.client.V1DeleteOptions(
        grace_period_seconds=grace_period_seconds,
        propagation_policy=propagation_policy,
    )


def _validate_delete_status(payload: object, *, label: str) -> None:
    if payload is not None and not isinstance(payload, kubernetes.client.V1Status):
        msg = f"malformed Kubernetes response while deleting {label}"
        raise OSError(msg)


async def _wait_until_deleted(
    *,
    label: str,
    timeout: float,
    refresh: Callable[[float], Awaitable[object | None]],
) -> None:
    async def deleted(remaining: float) -> None:
        live = await refresh(remaining)
        if live is None:
            return
        msg = f"{label} still exists"
        raise TimeoutError(msg)

    try:
        await until(
            deleted,
            timeout=timeout,
            interval=KUBE_WAIT_POLL_INTERVAL_SECONDS,
            action=f"waiting for {label} deletion",
        )
    except TimeoutError as err:
        msg = f"timed out waiting for {label} deletion"
        raise TimeoutError(msg) from err


def _label_selector(labels: Mapping[str, str] | None) -> str | None:
    if labels is None:
        return None
    if not labels:
        return None
    return ",".join(f"{k}={v}" for k, v in labels.items())


def _typed_payload[T](payload: object, expected: type[T], *, context: str) -> T:
    if not isinstance(payload, expected):
        msg = f"malformed Kubernetes {context} payload"
        raise OSError(msg)
    return payload


def _typed_list_items[T](
    payload: object | None,
    *,
    list_type: type[object],
    item_type: type[T],
    list_context: str,
    item_context: str,
) -> list[T]:
    if payload is None:
        return []
    if not isinstance(payload, list_type):
        msg = f"malformed Kubernetes {list_context} list payload"
        raise OSError(msg)

    out: list[T] = []
    for item in getattr(payload, "items", None) or []:
        if not isinstance(item, item_type):
            msg = f"malformed Kubernetes {item_context} entry in list payload"
            raise OSError(msg)
        out.append(item)
    return out


def _is_conflict(err: OSError) -> bool:
    return isinstance(err, KubeApiError) and err.status == 409


def _is_not_found(err: OSError) -> bool:
    return isinstance(err, KubeApiError) and err.status == 404


def _is_too_many_requests(err: OSError) -> bool:
    return isinstance(err, KubeApiError) and err.status == 429


def _is_missing_api_resource(err: OSError) -> bool:
    if not _is_not_found(err):
        return False
    detail = err.detail.lower() if isinstance(err, KubeApiError) else str(err).lower()
    return "the server could not find the requested resource" in detail


async def _create_or_patch[T](
    kube: Kube,
    *,
    timeout: float,
    create: Callable[[float | None], object],
    patch: Callable[[float | None], object],
    create_context: str,
    patch_context: str,
    expected: type[T],
    payload_context: str,
) -> T:
    try:
        created = await kube.run(
            create,
            timeout=timeout,
            context=create_context,
            missing_ok=False,
        )
    except OSError as err:
        if not _is_conflict(err):
            raise
    else:
        return _typed_payload(created, expected, context=payload_context)

    patched = await kube.run(
        patch,
        timeout=timeout,
        context=patch_context,
        missing_ok=False,
    )
    return _typed_payload(patched, expected, context=payload_context)


def _normalized_namespaces(
    namespaces: Collection[str] | None,
) -> tuple[str, ...] | None:
    if namespaces is None:
        return None
    normalized = {namespace.strip() for namespace in namespaces}
    normalized.discard("")
    return tuple(sorted(normalized))
