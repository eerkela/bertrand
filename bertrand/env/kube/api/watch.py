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

from bertrand.env.git import Deadline

from ._helpers import _label_selector

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


_WATCH_END = object()


def _next_watch_payload(iterator: Iterator[object]) -> object:
    try:
        return next(iterator)
    except StopIteration:
        return _WATCH_END


def _watch_resource_version(value: object) -> str:
    attr = getattr(value, "resource_version", None)
    if isinstance(attr, str):
        return attr.strip()
    metadata_attr = getattr(value, "metadata", None)
    if metadata_attr is not None:
        metadata_version = getattr(metadata_attr, "resource_version", None)
        if isinstance(metadata_version, str):
            return metadata_version.strip()
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


def _watch_kwargs(
    *,
    timeout: float,
    resource_version: str | None,
    labels: Mapping[str, str] | None,
    field_selector: str | None,
    api_kwargs: Mapping[str, object] | None,
) -> dict[str, object]:
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
    return kwargs


def _watch_api_exception_detail(err: ApiException) -> str:
    return (err.body or err.reason or str(err)).strip()


async def _read_watch_payload(
    iterator: Iterator[object],
    *,
    remaining: float,
    timeout: float,
    context: str,
) -> object:
    try:
        return await asyncio.wait_for(
            asyncio.to_thread(_next_watch_payload, iterator),
            timeout=None if math.isinf(remaining) else remaining,
        )
    except TimeoutError as err:
        msg = f"{context} watch timed out after {timeout} seconds"
        raise TimeoutError(msg) from err


def _watch_event[T](
    payload: object,
    *,
    context: str,
    wrapper: Callable[[object], T],
) -> WatchEvent[T] | OSError:
    if not isinstance(payload, Mapping):
        msg = f"{context} watch returned malformed event payload"
        return OSError(msg)
    payload = cast("Mapping[str, object]", payload)

    raw_type = str(payload.get("type") or "").strip()
    if raw_type not in _WATCH_EVENT_TYPES:
        msg = f"{context} watch returned unknown event type {raw_type!r}"
        return OSError(msg)
    event_type = cast("WatchEventType", raw_type)

    raw_object = payload.get("object")
    if raw_object is None:
        raw_object = payload.get("raw_object")
    if raw_object is None:
        msg = f"{context} watch event is missing object payload"
        return OSError(msg)
    if event_type == "ERROR":
        return _watch_error_event_exception(raw_object, context=context)
    obj = wrapper(raw_object)
    return WatchEvent(
        type=event_type,
        object=obj,
        resource_version=_watch_resource_version(obj)
        or _watch_resource_version(raw_object),
        raw_type=raw_type,
    )


def _watch_error_event_exception(raw_object: object, *, context: str) -> OSError:
    code, detail = _watch_error_status(raw_object)
    if code == 410:
        msg = f"{context} watch expired: {detail}"
        return WatchExpired(msg)
    if detail:
        msg = f"{context} watch returned an error event: {detail}"
    else:
        msg = f"{context} watch returned an error event"
    return OSError(msg)


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
    deadline = Deadline.from_timeout(
        timeout,
        message=f"{context} watch timed out before request could start",
    )
    kwargs = _watch_kwargs(
        timeout=timeout,
        resource_version=resource_version,
        labels=labels,
        field_selector=field_selector,
        api_kwargs=api_kwargs,
    )
    watcher = kubernetes.watch.Watch()
    iterator = watcher.stream(fn, **kwargs)
    try:
        while True:
            remaining = deadline.check(
                f"{context} watch timed out after {timeout} seconds"
            )
            try:
                payload = await _read_watch_payload(
                    iterator,
                    remaining=remaining,
                    timeout=timeout,
                    context=context,
                )
            except ApiException as err:
                detail = _watch_api_exception_detail(err)
                if err.status == 410:
                    msg = f"{context} watch expired: {detail}"
                    raise WatchExpired(msg) from err
                msg = (
                    f"{context} watch failed with kubernetes API status "
                    f"{err.status}: {detail}"
                )
                raise OSError(msg) from err
            if payload is _WATCH_END:
                return
            event = _watch_event(
                payload,
                context=context,
                wrapper=wrapper,
            )
            if isinstance(event, WatchExpired):
                raise WatchExpired(str(event))
            if isinstance(event, OSError):
                raise OSError(str(event))
            yield event
    finally:
        watcher.stop()
