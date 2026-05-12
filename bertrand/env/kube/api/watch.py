"""Low-level Kubernetes watch streaming support."""

from __future__ import annotations

import asyncio
import math
from collections.abc import AsyncIterator, Callable, Iterator, Mapping
from contextlib import suppress
from dataclasses import dataclass
from typing import Literal, cast

import kubernetes
from kubernetes.client.rest import ApiException

from ._helpers import _label_selector, _typed_wrapper

type WatchEventType = Literal["ADDED", "MODIFIED", "DELETED", "BOOKMARK", "ERROR"]
_WATCH_EVENT_TYPES: frozenset[str] = frozenset(
    {"ADDED", "MODIFIED", "DELETED", "BOOKMARK", "ERROR"}
)


@dataclass(frozen=True)
class WatchEvent[T]:
    """Typed Kubernetes watch event.

    Parameters
    ----------
    type : WatchEventType
        Normalized Kubernetes watch event type.
    object : T
        Wrapped Kubernetes resource object carried by the event.
    resource_version : str
        Resource version reported by the event object, or an empty string when
        unavailable.
    raw_type : str
        Raw event type string received from Kubernetes.
    """

    type: WatchEventType
    object: T
    resource_version: str
    raw_type: str


class WatchExpired(OSError):  # noqa: N818
    """Raised when Kubernetes expires a watch resource version."""


@dataclass(frozen=True)
class _WatchEnd:
    pass


_WATCH_END = _WatchEnd()


def _next_watch_payload(iterator: Iterator[object]) -> object | _WatchEnd:
    try:
        return next(iterator)
    except StopIteration:
        return _WATCH_END


def _watch_resource_version(value: object) -> str:
    attr = getattr(value, "resource_version", None)
    if isinstance(attr, str):
        return attr.strip()
    if isinstance(value, Mapping):
        value = cast("Mapping[str, object]", value)
        metadata = value.get("metadata")
        if isinstance(metadata, Mapping):
            metadata = cast("Mapping[str, object]", metadata)
            return str(metadata.get("resourceVersion") or "").strip()
    return ""


def _watch_error_status(value: object) -> tuple[int | None, str]:
    if isinstance(value, Mapping):
        value = cast("Mapping[str, object]", value)
        raw_code = value.get("code")
        reason = str(value.get("reason") or "").strip()
        message = str(value.get("message") or "").strip()
    else:
        raw_code = getattr(value, "code", None)
        reason = str(getattr(value, "reason", "") or "").strip()
        message = str(getattr(value, "message", "") or "").strip()

    code: int | None = None
    if isinstance(raw_code, int):
        code = raw_code
    elif raw_code is not None:
        with suppress(ValueError):
            code = int(str(raw_code).strip())

    detail = message or reason or str(value).strip()
    return code, detail


async def watch[T](
    fn: Callable[..., object],
    *,
    wrapper: Callable[[object], T],
    timeout: float,
    context: str,
    resource_version: str | None = None,
    labels: Mapping[str, str] | None = None,
    field_selector: str | None = None,
    api_kwargs: Mapping[str, object] | None = None,
) -> AsyncIterator[WatchEvent[T]]:
    """Stream typed Kubernetes watch events across the sync/async boundary.

    Parameters
    ----------
    fn : Callable[..., object]
        Generated Kubernetes list/watch API function.
    wrapper : Callable[[object], T]
        Callback that validates and wraps each raw event object.
    timeout : float
        Maximum watch budget in seconds. If infinite, wait indefinitely.
    context : str
        Human-readable context for timeout and API error messages.
    resource_version : str | None, optional
        Resource version to watch from.
    labels : Mapping[str, str] | None, optional
        Optional label selector key/value pairs.
    field_selector : str | None, optional
        Raw Kubernetes field selector.
    api_kwargs : Mapping[str, object] | None, optional
        Additional keyword arguments for the generated API function.

    Yields
    ------
    WatchEvent[T]
        Typed watch events containing wrapped Kubernetes objects.

    Raises
    ------
    TimeoutError
        If the watch exceeds the timeout budget.
    WatchExpired
        If Kubernetes returns HTTP 410 for an expired resource version.
    OSError
        If Kubernetes returns malformed watch data or any non-410 API failure.
    """
    if timeout <= 0:
        msg = f"{context} watch timed out before request could start"
        raise TimeoutError(msg)

    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    kwargs = dict(api_kwargs or {})
    label_selector = _label_selector(labels)
    if label_selector is not None:
        kwargs["label_selector"] = label_selector
    if field_selector is not None:
        field_selector = field_selector.strip()
        if field_selector:
            kwargs["field_selector"] = field_selector
    if resource_version is not None:
        resource_version = resource_version.strip()
        if resource_version:
            kwargs["resource_version"] = resource_version
    if not math.isinf(timeout):
        kwargs.setdefault("timeout_seconds", max(1, math.ceil(timeout)))

    watcher = kubernetes.watch.Watch()
    iterator = watcher.stream(fn, **kwargs)
    try:
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                msg = f"{context} watch timed out after {timeout} seconds"
                raise TimeoutError(msg)
            try:
                payload = await asyncio.wait_for(
                    asyncio.to_thread(_next_watch_payload, iterator),
                    timeout=None if math.isinf(remaining) else remaining,
                )
            except TimeoutError as err:
                msg = f"{context} watch timed out after {timeout} seconds"
                raise TimeoutError(msg) from err
            except ApiException as err:
                if err.status == 410:
                    detail = (err.body or err.reason or str(err)).strip()
                    msg = f"{context} watch expired: {detail}"
                    raise WatchExpired(msg) from err
                detail = (err.body or err.reason or str(err)).strip()
                msg = (
                    f"{context} watch failed with kubernetes API status "
                    f"{err.status}: {detail}"
                )
                raise OSError(msg) from err

            if payload is _WATCH_END:
                return
            if not isinstance(payload, Mapping):
                msg = f"{context} watch returned malformed event payload"
                raise OSError(msg)
            payload = cast("Mapping[str, object]", payload)

            raw_type = str(payload.get("type") or "").strip()
            if raw_type not in _WATCH_EVENT_TYPES:
                msg = f"{context} watch returned unknown event type {raw_type!r}"
                raise OSError(msg)
            event_type = cast("WatchEventType", raw_type)

            raw_object = payload.get("object")
            if raw_object is None:
                raw_object = payload.get("raw_object")
            if raw_object is None:
                msg = f"{context} watch event is missing object payload"
                raise OSError(msg)
            if event_type == "ERROR":
                code, detail = _watch_error_status(raw_object)
                if code == 410:
                    msg = f"{context} watch expired: {detail}"
                    raise WatchExpired(msg)
                if detail:
                    msg = f"{context} watch returned an error event: {detail}"
                else:
                    msg = f"{context} watch returned an error event"
                raise OSError(msg)
            obj = wrapper(raw_object)
            yield WatchEvent(
                type=event_type,
                object=obj,
                resource_version=_watch_resource_version(obj)
                or _watch_resource_version(raw_object),
                raw_type=raw_type,
            )
    finally:
        watcher.stop()


async def _watch_cluster_resource[T, W](
    *,
    timeout: float,
    labels: Mapping[str, str] | None,
    field_selector: str | None,
    resource_version: str | None,
    watch_fn: Callable[..., object],
    expected: type[T],
    wrapper: Callable[[T], W],
    context: str,
    payload_context: str,
) -> AsyncIterator[WatchEvent[W]]:
    async for event in watch(
        watch_fn,
        wrapper=lambda payload: _typed_wrapper(
            payload,
            expected,
            wrapper=wrapper,
            context=payload_context,
        ),
        timeout=timeout,
        context=context,
        resource_version=resource_version,
        labels=labels,
        field_selector=field_selector,
    ):
        yield event


async def _watch_namespaced_resource[T, W](
    *,
    timeout: float,
    namespace: str | None,
    labels: Mapping[str, str] | None,
    field_selector: str | None,
    resource_version: str | None,
    watch_all: Callable[..., object],
    watch_namespace: Callable[..., object],
    expected: type[T],
    wrapper: Callable[[T], W],
    all_context: str,
    namespace_context: Callable[[str], str],
    payload_context: str,
) -> AsyncIterator[WatchEvent[W]]:
    namespace = namespace.strip() if namespace is not None else ""
    if namespace:
        fn = watch_namespace
        api_kwargs: Mapping[str, object] = {"namespace": namespace}
        context = namespace_context(namespace)
    else:
        fn = watch_all
        api_kwargs = {}
        context = all_context

    async for event in watch(
        fn,
        wrapper=lambda payload: _typed_wrapper(
            payload,
            expected,
            wrapper=wrapper,
            context=payload_context,
        ),
        timeout=timeout,
        context=context,
        resource_version=resource_version,
        labels=labels,
        field_selector=field_selector,
        api_kwargs=api_kwargs,
    ):
        yield event
