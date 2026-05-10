"""Private Kubernetes wrapper infrastructure."""

from __future__ import annotations

from typing import TYPE_CHECKING

import kubernetes

from bertrand.env.git import until

if TYPE_CHECKING:
    from collections.abc import Awaitable, Callable, Collection, Mapping

    from .client import Kube

KUBE_WAIT_POLL_INTERVAL_SECONDS = 0.5


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


def _typed_wrapper[T, W](
    payload: object,
    expected: type[T],
    *,
    wrapper: Callable[[T], W],
    context: str,
) -> W:
    return wrapper(_typed_payload(payload, expected, context=context))


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
    detail = str(err).lower()
    return "status 409" in detail or "already exists" in detail


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
        created = await kube.run(create, timeout=timeout, context=create_context)
    except OSError as err:
        if not _is_conflict(err):
            raise
    else:
        return _typed_payload(created, expected, context=payload_context)

    patched = await kube.run(patch, timeout=timeout, context=patch_context)
    return _typed_payload(patched, expected, context=payload_context)


def _normalized_namespaces(
    namespaces: Collection[str] | None,
) -> tuple[str, ...] | None:
    if namespaces is None:
        return None
    normalized = {namespace.strip() for namespace in namespaces}
    normalized.discard("")
    return tuple(sorted(normalized))


async def _list_cluster_items[T](
    kube: Kube,
    *,
    timeout: float,
    labels: Mapping[str, str] | None,
    list_items: Callable[[str | None, float | None], object],
    list_type: type[object],
    item_type: type[T],
    context: str,
    list_context: str,
    item_context: str,
) -> list[T]:
    label_selector = _label_selector(labels)
    payload = await kube.run(
        lambda request_timeout: list_items(label_selector, request_timeout),
        timeout=timeout,
        context=context,
    )
    return _typed_list_items(
        payload,
        list_type=list_type,
        item_type=item_type,
        list_context=list_context,
        item_context=item_context,
    )


async def _list_namespaced_items[T](
    kube: Kube,
    *,
    timeout: float,
    namespaces: Collection[str] | None,
    labels: Mapping[str, str] | None,
    list_all: Callable[[str | None, float | None], object],
    list_namespace: Callable[[str, str | None, float | None], object],
    list_type: type[object],
    item_type: type[T],
    all_context: str,
    namespace_context: Callable[[str], str],
    list_context: str,
    item_context: str,
) -> list[T]:
    label_selector = _label_selector(labels)
    normalized = _normalized_namespaces(namespaces)
    payloads: list[object] = []

    if normalized is None:
        payload = await kube.run(
            lambda request_timeout: list_all(label_selector, request_timeout),
            timeout=timeout,
            context=all_context,
        )
        if payload is not None:
            payloads.append(payload)
    elif not normalized:
        return []
    else:
        for namespace in normalized:
            payload = await kube.run(
                lambda request_timeout, namespace=namespace: list_namespace(
                    namespace,
                    label_selector,
                    request_timeout,
                ),
                timeout=timeout,
                context=namespace_context(namespace),
            )
            if payload is not None:
                payloads.append(payload)

    out: list[T] = []
    for payload in payloads:
        out.extend(
            _typed_list_items(
                payload,
                list_type=list_type,
                item_type=item_type,
                list_context=list_context,
                item_context=item_context,
            )
        )
    return out
