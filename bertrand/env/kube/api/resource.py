"""Kubernetes generated-resource adapters and wrapper mixins."""

from __future__ import annotations

import asyncio
import math
from collections.abc import Mapping
from dataclasses import dataclass
from types import MappingProxyType
from typing import (
    TYPE_CHECKING,
    Any,
    Literal,
    Protocol,
    Self,
    cast,
    get_type_hints,
)

import kubernetes
from kubernetes.client.rest import ApiException

from bertrand.env.git import Deadline, until

from .client import Kube

if TYPE_CHECKING:
    import builtins
    from collections.abc import AsyncIterator, Callable, Collection
    from datetime import datetime

type ResourceScope = Literal["cluster", "namespaced"]
type BuiltinAPI = Literal[
    "core",
    "apps",
    "batch",
    "networking",
    "apiextensions",
    "rbac",
    "storage",
    "coordination",
]
type DeletionPropagationPolicy = Literal["Background", "Foreground", "Orphan"]
type WatchEventType = Literal["ADDED", "MODIFIED", "DELETED", "BOOKMARK", "ERROR"]
RESOURCE_WAIT_POLL_INTERVAL_SECONDS = 0.5


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
    """

    type: WatchEventType
    object: T
    resource_version: str


class WatchExpiredError(OSError):
    """Raised when Kubernetes can no longer serve a watch resource version."""


_WATCH_END = object()


@dataclass(frozen=True)
class _WatchEnvelope:
    type: WatchEventType
    object: object
    raw_object: Mapping[str, object]


def _watch_mapping(value: object, *, context: str, label: str) -> Mapping[str, object]:
    if not isinstance(value, Mapping):
        msg = f"{context} watch {label} is not a mapping"
        raise OSError(msg)
    out: dict[str, object] = {}
    for key, item in value.items():
        if not isinstance(key, str):
            msg = f"{context} watch {label} has non-string key {key!r}"
            raise OSError(msg)
        out[key] = item
    return out


def _watch_event_type(value: object, *, context: str) -> WatchEventType:
    event_type = str(value or "").strip()
    if event_type == "ADDED":
        return "ADDED"
    if event_type == "MODIFIED":
        return "MODIFIED"
    if event_type == "DELETED":
        return "DELETED"
    if event_type == "BOOKMARK":
        return "BOOKMARK"
    if event_type == "ERROR":
        return "ERROR"
    msg = f"{context} watch returned unknown event type {event_type!r}"
    raise OSError(msg)


def _watch_envelope(
    payload: object,
    *,
    context: str,
) -> _WatchEnvelope:
    # The Kubernetes client yields watch envelopes, not bare resources.  Keep
    # the generic parser aligned to that contract and let wrappers own objects.
    payload = _watch_mapping(payload, context=context, label="event payload")
    event_type = _watch_event_type(payload.get("type"), context=context)

    obj = payload.get("object")
    raw_object = payload.get("raw_object")
    if obj is None and raw_object is None:
        msg = f"{context} watch event is missing object payload"
        raise OSError(msg)
    if raw_object is None:
        if not isinstance(obj, Mapping):
            msg = f"{context} watch event is missing raw object payload"
            raise OSError(msg)
        raw_object = obj
    if obj is None:
        obj = raw_object
    return _WatchEnvelope(
        type=event_type,
        object=obj,
        raw_object=_watch_mapping(raw_object, context=context, label="raw object"),
    )


def _watch_event[T](
    payload: object,
    *,
    context: str,
    wrapper: Callable[[object], T],
) -> WatchEvent[T]:
    event = _watch_envelope(payload, context=context)
    if event.type == "ERROR":
        # Kubernetes reports watch failures as Status objects.  Convert them to
        # exceptions so callers can handle watch expiry separately from events.
        raw_code = event.raw_object.get("code")
        reason = str(event.raw_object.get("reason") or "").strip()
        message = str(event.raw_object.get("message") or "").strip()

        code: int | None = None
        if raw_code is not None:
            try:
                code = int(str(raw_code).strip())
            except ValueError:
                code = None

        detail = message or reason or str(event.raw_object).strip()
        if code == 410:
            # 410 means the requested resourceVersion is too old.  Callers can catch
            # this, re-list the collection, and resume from the fresh version.
            msg = f"{context} watch expired: {detail}"
            raise WatchExpiredError(msg)
        if detail:
            msg = f"{context} watch returned an error event: {detail}"
        else:
            msg = f"{context} watch returned an error event"
        raise OSError(msg)

    metadata = event.raw_object.get("metadata")
    resource_version = ""
    if isinstance(metadata, Mapping):
        metadata = _watch_mapping(metadata, context=context, label="object metadata")
        resource_version = str(metadata.get("resourceVersion") or "").strip()

    obj = wrapper(event.object)
    return WatchEvent(
        type=event.type,
        object=obj,
        resource_version=resource_version,
    )


async def _watch[T](
    fn: Callable[..., object],
    *,
    wrapper: Callable[[object], T],
    deadline: Deadline,
    context: str,
    resource_version: str = "",
    label_selector: str = "",
    field_selector: str = "",
    api_kwargs: Mapping[str, object] | None = None,
) -> AsyncIterator[WatchEvent[T]]:
    remaining = deadline.check(f"{context} watch timed out before it could start")

    # `Watch.stream()` forwards kwargs directly to the generated list endpoint -
    # normalize selectors and resourceVersion once so the stream starts cleanly
    kwargs = dict(api_kwargs or {})
    kwargs.update(
        {
            key: value
            for key, value in {
                "label_selector": label_selector.strip(),
                "field_selector": field_selector.strip(),
                "resource_version": resource_version.strip(),
            }.items()
            if value
        }
    )
    if not math.isinf(remaining):
        kwargs.setdefault("timeout_seconds", max(1, math.ceil(remaining)))

    # open a watch stream and asynchronously yield parsed events
    watcher = kubernetes.watch.Watch()
    iterator = watcher.stream(fn, **kwargs)
    try:
        while True:
            remaining = deadline.check(
                f"{context} watch timed out after {deadline.timeout} seconds"
            )
            try:
                # `Watch.stream()` is a blocking iterator; run it in a worker thread
                # and wait until the shared deadline.  Returning a sentinel lets the
                # async caller distinguish exhaustion from a normal `None` payload.
                payload = await asyncio.wait_for(
                    asyncio.to_thread(lambda: next(iterator, _WATCH_END)),
                    timeout=None if math.isinf(remaining) else remaining,
                )
                if payload is _WATCH_END:
                    return
            except TimeoutError as err:
                msg = f"{context} watch timed out after {deadline.timeout} seconds"
                raise TimeoutError(msg) from err
            except ApiException as err:
                detail = (err.body or err.reason or str(err)).strip()
                if err.status == 410:
                    # the API can also report stale resource versions as HTTP 410
                    # before yielding an ERROR event; surface both paths the same way
                    msg = f"{context} watch expired: {detail}"
                    raise WatchExpiredError(msg) from err
                msg = (
                    f"{context} watch failed with kubernetes API status {err.status}: "
                    f"{detail}"
                )
                raise OSError(msg) from err

            # parse the raw event payload and raise exceptions or yield normal values
            yield _watch_event(
                payload,
                context=context,
                wrapper=wrapper,
            )
    finally:
        watcher.stop()


def _label_selector(labels: Mapping[str, str] | None) -> str:
    if not labels:
        return ""
    return ",".join(f"{key}={value}" for key, value in labels.items())


def _normalized_namespaces(
    namespaces: Collection[str] | None,
) -> tuple[str, ...] | None:
    if namespaces is None:
        return None
    normalized = {namespace.strip() for namespace in namespaces}
    normalized.discard("")
    return tuple(sorted(normalized))


@dataclass(frozen=True)
class _BuiltinConfig:
    api: BuiltinAPI
    scope: ResourceScope
    payload: type[Any]
    read: str
    list: str
    list_all: str | None
    create: str
    patch: str
    delete: str


_BUILTIN_REGISTRY: dict[type[Any], _BuiltinConfig] = {}


def builtin_resource[BuiltinT: type[Any]](
    *,
    api: BuiltinAPI,
    scope: ResourceScope,
    endpoint: str,
) -> Callable[[BuiltinT], BuiltinT]:
    """Register Kubernetes generated-client metadata for a wrapper class.

    Parameters
    ----------
    api : BuiltinAPI
        Attribute on :class:`Kube` exposing the generated API family.
    scope : {"cluster", "namespaced"}
        Kubernetes API scope for the resource.
    endpoint : str
        Explicit generated-client endpoint stem for the resource.

    Returns
    -------
    Callable[[type[Any]], type[Any]]
        Class decorator that records endpoint metadata privately.

    Raises
    ------
    ValueError
        If the API family or scope is invalid.
    """
    if api not in (
        "core",
        "apps",
        "batch",
        "networking",
        "apiextensions",
        "rbac",
        "storage",
        "coordination",
    ):
        msg = f"unknown Kubernetes generated API family: {api!r}"
        raise ValueError(msg)
    if scope not in ("cluster", "namespaced"):
        msg = f"builtin resource scope must be 'cluster' or 'namespaced', got {scope!r}"
        raise ValueError(msg)
    endpoint = endpoint.strip()
    if not endpoint:
        msg = "builtin resource endpoint cannot be empty"
        raise ValueError(msg)

    def register(cls: BuiltinT) -> BuiltinT:
        payload = get_type_hints(cls).get("_obj")
        if not isinstance(payload, type):
            msg = f"{cls.__name__} must annotate _obj with a Kubernetes payload type"
            raise TypeError(msg)
        if scope == "namespaced":
            read = f"read_namespaced_{endpoint}"
            list_ = f"list_namespaced_{endpoint}"
            list_all = f"list_{endpoint}_for_all_namespaces"
            create = f"create_namespaced_{endpoint}"
            patch = f"patch_namespaced_{endpoint}"
            delete = f"delete_namespaced_{endpoint}"
        else:
            read = f"read_{endpoint}"
            list_ = f"list_{endpoint}"
            list_all = None
            create = f"create_{endpoint}"
            patch = f"patch_{endpoint}"
            delete = f"delete_{endpoint}"
        _BUILTIN_REGISTRY[cls] = _BuiltinConfig(
            api=api,
            scope=scope,
            payload=payload,
            read=read,
            list=list_,
            list_all=list_all,
            create=create,
            patch=patch,
            delete=delete,
        )
        return cls

    return register


class _HasObjectMeta(Protocol):
    @property
    def metadata(self) -> kubernetes.client.V1ObjectMeta | None: ...


class KubeResource[PayloadT: _HasObjectMeta]:
    """Base class for generated Kubernetes resource wrappers.

    Attributes
    ----------
    _obj : PayloadT
        Typed Kubernetes client model returned by the cluster API.
    """

    _obj: PayloadT

    def __init__(self, _obj: PayloadT) -> None:
        self._obj = _obj

    @classmethod
    def from_payload(cls, payload: PayloadT) -> Self:
        """Wrap one typed Kubernetes API payload.

        Parameters
        ----------
        payload : PayloadT
            Typed Kubernetes client model returned by the cluster API.

        Returns
        -------
        KubeResource
            Read-only wrapper around `payload`.
        """
        return cls(_obj=payload)

    @property
    def name(self) -> str:
        """Return the Kubernetes object name.

        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        metadata = self._obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def namespace(self) -> str:
        """Return the Kubernetes object namespace.

        Returns
        -------
        str
            Trimmed `metadata.namespace`, or an empty string when unavailable.
        """
        metadata = self._obj.metadata
        return (metadata.namespace or "").strip() if metadata is not None else ""

    @property
    def labels(self) -> Mapping[str, str]:
        """Return the Kubernetes object labels.

        Returns
        -------
        Mapping[str, str]
            Live read-only view of `metadata.labels`, or an empty mapping when
            unavailable.
        """
        metadata = self._obj.metadata
        if metadata is None or metadata.labels is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.labels)

    @property
    def annotations(self) -> Mapping[str, str]:
        """Return the Kubernetes object annotations.

        Returns
        -------
        Mapping[str, str]
            Live read-only view of `metadata.annotations`, or an empty mapping when
            unavailable.
        """
        metadata = self._obj.metadata
        if metadata is None or metadata.annotations is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.annotations)

    @property
    def resource_version(self) -> str:
        """Return the Kubernetes object resource version.

        Returns
        -------
        str
            Kubernetes `metadata.resourceVersion`, or an empty string when
            unavailable.
        """
        metadata = self._obj.metadata
        return (metadata.resource_version or "").strip() if metadata is not None else ""

    @property
    def uid(self) -> str:
        """Return the Kubernetes object UID.

        Returns
        -------
        str
            Kubernetes `metadata.uid`, or an empty string when unavailable.
        """
        metadata = self._obj.metadata
        return (metadata.uid or "").strip() if metadata is not None else ""

    @property
    def created_at(self) -> datetime | None:
        """Return the Kubernetes object creation timestamp.

        Returns
        -------
        datetime | None
            Kubernetes `metadata.creationTimestamp`, or `None` when unavailable.
        """
        metadata = self._obj.metadata
        return metadata.creation_timestamp if metadata is not None else None

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        name: str,
        deadline: Deadline,
        namespace: str | None = None,
        context: str | None = None,
    ) -> Self | None:
        """Read one resource by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Resource name.
        deadline : Deadline
            Maximum request budget in seconds.
        namespace : str | None, optional
            Namespace for namespaced resources.
        context : str | None, optional
            Error context override.

        Returns
        -------
        KubeResource | None
            Wrapped Kubernetes object, or `None` when absent.
        """
        namespace = _object_namespace(cls, namespace, action="read")
        label = f"{namespace}/{name}" if namespace else name
        config = _BUILTIN_REGISTRY[cls]
        payload = await _run_request(
            kube,
            api=config.api,
            method=config.read,
            namespace=namespace,
            name=name,
            deadline=deadline,
            context=context or f"failed to read {cls.__name__} {label!r}",
            missing_ok=True,
        )
        if payload is None:
            return None
        return _wrap_payload(cls, payload)

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        deadline: Deadline,
        namespace: str | None = None,
        namespaces: Collection[str] | None = None,
        labels: Mapping[str, str] | None = None,
        field_selector: str = "",
    ) -> builtins.list[Self]:
        """List resources with optional namespace and selector filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum request budget in seconds.
        namespace : str | None, optional
            Optional single namespace filter.
        namespaces : Collection[str] | None, optional
            Optional namespace filters for namespaced resources.
        labels : Mapping[str, str] | None, optional
            Optional exact-match label selector.
        field_selector : str, optional
            Raw Kubernetes field selector.

        Returns
        -------
        list[KubeResource]
            Wrapped Kubernetes objects matching the filters.

        Raises
        ------
        ValueError
            If namespace filters are invalid for the resource scope.
        RuntimeError
            If a namespaced resource cannot list across all namespaces.
        OSError
            If Kubernetes returns a malformed list payload.
        """
        config = _BUILTIN_REGISTRY[cls]
        namespace = namespace.strip() if namespace is not None else ""
        label_selector = _label_selector(labels)
        field_selector = field_selector.strip()
        if config.scope == "cluster":
            if namespace or namespaces is not None:
                msg = f"{cls.__name__} is cluster-scoped; cannot list by namespace"
                raise ValueError(msg)
            payloads = [
                await _run_request(
                    kube,
                    api=config.api,
                    method=config.list,
                    deadline=deadline,
                    label_selector=label_selector,
                    field_selector=field_selector,
                    context=f"failed to list {cls.__name__}s",
                    missing_ok=False,
                )
            ]
        else:
            if namespace and namespaces is not None:
                msg = (
                    f"{cls.__name__} list accepts either namespace or namespaces, "
                    "not both"
                )
                raise ValueError(msg)
            selected = (namespace,) if namespace else namespaces
            normalized = _normalized_namespaces(selected)
            if normalized is None:
                if config.list_all is None:
                    msg = f"{cls.__name__} does not support list across all namespaces"
                    raise RuntimeError(msg)
                payloads = [
                    await _run_request(
                        kube,
                        api=config.api,
                        method=config.list_all,
                        deadline=deadline,
                        label_selector=label_selector,
                        field_selector=field_selector,
                        context=f"failed to list {cls.__name__}s across all namespaces",
                        missing_ok=False,
                    )
                ]
            else:
                payloads = list(
                    await asyncio.gather(
                        *(
                            _run_request(
                                kube,
                                api=config.api,
                                method=config.list,
                                namespace=namespace,
                                deadline=deadline,
                                label_selector=label_selector,
                                field_selector=field_selector,
                                context=(
                                    f"failed to list {cls.__name__}s in namespace "
                                    f"{namespace!r}"
                                ),
                                missing_ok=False,
                            )
                            for namespace in normalized
                        )
                    )
                )

        items: builtins.list[Self] = []
        for payload in payloads:
            if payload is None:
                continue
            raw_items = getattr(payload, "items", None)
            if not isinstance(raw_items, list):
                msg = f"malformed Kubernetes {cls.__name__} list payload"
                raise OSError(msg)
            items.extend(_wrap_payload(cls, item) for item in raw_items)
        return items

    async def refresh(self, kube: Kube, *, deadline: Deadline) -> Self | None:
        """Re-read this resource by its metadata identity.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum request budget in seconds.

        Returns
        -------
        KubeResource | None
            Fresh wrapped object, or `None` when absent.

        Raises
        ------
        OSError
            If this resource has incomplete Kubernetes metadata.
        """
        if _BUILTIN_REGISTRY[type(self)].scope == "cluster":
            name = self.name
            if not name:
                msg = f"cannot refresh {type(self).__name__} with missing metadata.name"
                raise OSError(msg)
            return await type(self).get(kube, name=name, deadline=deadline)
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = (
                f"cannot refresh {type(self).__name__} with missing "
                "metadata.name/namespace"
            )
            raise OSError(msg)
        return await type(self).get(
            kube,
            namespace=namespace,
            name=name,
            deadline=deadline,
        )

    async def wait_until(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        predicate: Callable[[Self], bool],
        pending_message: str,
        missing_message: str,
        timeout_message: str,
        check_current: bool = False,
    ) -> Self:
        """Wait until this refreshed object satisfies `predicate`.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum wait budget.
        predicate : Callable[[KubeResource], bool]
            Predicate that returns `True` when the object is ready.
        pending_message : str
            Retry reason used while the object is still pending.
        missing_message : str
            Error raised if the object disappears.
        timeout_message : str
            Error raised if the wait expires.
        check_current : bool, optional
            Whether to check this resource before the first refresh.

        Returns
        -------
        KubeResource
            Fresh object satisfying `predicate`.

        Raises
        ------
        TimeoutError
            If the wait deadline expires.
        """
        current = self

        async def ready(attempt_deadline: Deadline) -> Self:
            nonlocal current
            if check_current and predicate(current):
                return current
            refreshed = await current.refresh(kube, deadline=attempt_deadline)
            if refreshed is None:
                raise OSError(missing_message)
            current = refreshed
            if predicate(current):
                return current
            raise TimeoutError(pending_message)

        try:
            return await until(
                ready,
                deadline=deadline,
                delay=RESOURCE_WAIT_POLL_INTERVAL_SECONDS,
            )
        except TimeoutError as err:
            raise TimeoutError(timeout_message) from err


async def _run_request(
    kube: Kube,
    *,
    api: BuiltinAPI,
    method: str,
    deadline: Deadline,
    context: str,
    missing_ok: bool,
    name: str | None = None,
    namespace: str | None = None,
    body: Mapping[str, object] | object | None = None,
    label_selector: str = "",
    field_selector: str = "",
) -> object | None:
    kwargs: dict[str, object] = {}
    if name is not None:
        kwargs["name"] = name
    if namespace is not None:
        kwargs["namespace"] = namespace
    if body is not None:
        kwargs["body"] = body
    if label_selector:
        kwargs["label_selector"] = label_selector
    if field_selector:
        kwargs["field_selector"] = field_selector
    endpoint = _endpoint(kube, api, method)
    return await kube.run(
        lambda request_timeout: endpoint(**kwargs, _request_timeout=request_timeout),
        deadline=deadline,
        context=context,
        missing_ok=missing_ok,
    )


def _endpoint(kube: Kube, api: BuiltinAPI, method: str) -> Callable[..., object]:
    return cast("Callable[..., object]", getattr(getattr(kube, api), method))


def _object_namespace(
    cls: type[Any],
    namespace: str | None,
    *,
    action: str,
) -> str | None:
    namespace = namespace.strip() if namespace is not None else ""
    if _BUILTIN_REGISTRY[cls].scope == "cluster":
        if namespace:
            msg = f"{cls.__name__} is cluster-scoped; cannot {action} in a namespace"
            raise ValueError(msg)
        return None
    if not namespace:
        msg = f"{cls.__name__} {action} requires a namespace"
        raise ValueError(msg)
    return namespace


def _wrap_payload(
    cls: type[Any],
    payload: object,
    *,
    malformed_message: str | None = None,
    context: str | None = None,
) -> Any:
    if not isinstance(payload, _BUILTIN_REGISTRY[cls].payload):
        if malformed_message is not None:
            raise OSError(malformed_message)
        msg = f"malformed Kubernetes {context or cls.__name__} payload"
        raise OSError(msg)
    return cls.from_payload(payload)


class Watchable:
    """Mixin adding generated Kubernetes watch operations."""

    @classmethod
    async def watch(
        cls,
        kube: Kube,
        *,
        deadline: Deadline,
        namespace: str | None = None,
        labels: Mapping[str, str] | None = None,
        field_selector: str = "",
        resource_version: str = "",
    ) -> AsyncIterator[WatchEvent[Self]]:
        """Watch this generated resource type.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum watch budget. If infinite, wait indefinitely.
        namespace : str | None, optional
            Optional namespace for namespaced resources.
        labels : Mapping[str, str] | None, optional
            Optional exact-match label selector.
        field_selector : str, optional
            Raw Kubernetes field selector.
        resource_version : str, optional
            Resource version to watch from.

        Yields
        ------
        WatchEvent[KubeResource]
            Typed watch events containing wrapped Kubernetes objects.

        Raises
        ------
        ValueError
            If namespace filters are invalid for the resource scope.
        RuntimeError
            If a namespaced resource cannot watch across all namespaces.
        """
        config = _BUILTIN_REGISTRY[cls]
        namespace = namespace.strip() if namespace is not None else ""
        api_kwargs: Mapping[str, object] = {}
        if config.scope == "cluster":
            if namespace:
                msg = f"{cls.__name__} is cluster-scoped; cannot watch in a namespace"
                raise ValueError(msg)
            watch_fn = _endpoint(kube, config.api, config.list)
            context = f"failed to watch {cls.__name__}s"
        elif namespace:
            watch_fn = _endpoint(kube, config.api, config.list)
            api_kwargs = {"namespace": namespace}
            context = f"failed to watch {cls.__name__}s in namespace {namespace!r}"
        else:
            if config.list_all is None:
                msg = f"{cls.__name__} does not support watch across all namespaces"
                raise RuntimeError(msg)
            watch_fn = _endpoint(kube, config.api, config.list_all)
            context = f"failed to watch {cls.__name__}s across all namespaces"
        async for event in _watch(
            watch_fn,
            wrapper=lambda payload: _wrap_payload(
                cls, payload, context=f"{cls.__name__} watch"
            ),
            deadline=deadline,
            context=context,
            resource_version=resource_version,
            label_selector=_label_selector(labels),
            field_selector=field_selector.strip(),
            api_kwargs=api_kwargs,
        ):
            yield event


async def _create_manifest(
    cls: type[Any],
    kube: Kube,
    *,
    manifest: Mapping[str, object],
    deadline: Deadline,
    namespace: str | None = None,
    name: str | None = None,
    context: str | None = None,
    malformed_message: str | None = None,
    missing_ok: bool = True,
) -> Any:
    namespace = _object_namespace(cls, namespace, action="create")
    label_name = name or cls.__name__
    label = f"{namespace}/{label_name}" if namespace else label_name
    config = _BUILTIN_REGISTRY[cls]
    payload = await _run_request(
        kube,
        api=config.api,
        method=config.create,
        namespace=namespace,
        body=manifest,
        deadline=deadline,
        context=context or f"failed to create {cls.__name__} {label}",
        missing_ok=missing_ok,
    )
    return _wrap_payload(cls, payload, malformed_message=malformed_message)


async def _patch_manifest(
    cls: type[Any],
    kube: Kube,
    *,
    name: str,
    manifest: Mapping[str, object],
    deadline: Deadline,
    namespace: str | None = None,
) -> Any:
    namespace = _object_namespace(cls, namespace, action="patch")
    label = f"{namespace}/{name}" if namespace else name
    config = _BUILTIN_REGISTRY[cls]
    payload = await _run_request(
        kube,
        api=config.api,
        method=config.patch,
        namespace=namespace,
        name=name,
        body=manifest,
        deadline=deadline,
        context=f"failed to patch {cls.__name__} {label}",
        missing_ok=False,
    )
    return _wrap_payload(cls, payload)


async def _upsert_manifest(
    cls: type[Any],
    kube: Kube,
    *,
    name: str,
    manifest: Mapping[str, object],
    deadline: Deadline,
    namespace: str | None = None,
) -> Any:
    namespace = _object_namespace(cls, namespace, action="upsert")
    label = f"{namespace}/{name}" if namespace else name
    try:
        return await _create_manifest(
            cls,
            kube,
            manifest=manifest,
            deadline=deadline,
            namespace=namespace,
            name=name,
            context=f"failed to create {cls.__name__} {label}",
            missing_ok=False,
        )
    except OSError as err:
        if not isinstance(err, Kube.APIError) or err.status != 409:
            raise
        return await _patch_manifest(
            cls,
            kube,
            name=name,
            manifest=manifest,
            deadline=deadline,
            namespace=namespace,
        )


async def _delete_by_name(
    cls: type[Any],
    kube: Kube,
    *,
    name: str,
    deadline: Deadline,
    namespace: str | None = None,
    propagation_policy: DeletionPropagationPolicy | None = None,
    grace_period_seconds: int | None = None,
) -> None:
    namespace = _object_namespace(cls, namespace, action="delete")
    label = f"{namespace}/{name}" if namespace else name
    delete_options = None
    if propagation_policy is not None or grace_period_seconds is not None:
        if propagation_policy is not None and propagation_policy not in (
            "Background",
            "Foreground",
            "Orphan",
        ):
            msg = (
                f"invalid {cls.__name__} deletion propagation policy: "
                f"{propagation_policy!r}"
            )
            raise ValueError(msg)
        if grace_period_seconds is not None and grace_period_seconds < 0:
            msg = f"{cls.__name__} deletion grace period cannot be negative"
            raise ValueError(msg)
        delete_options = kubernetes.client.V1DeleteOptions(
            grace_period_seconds=grace_period_seconds,
            propagation_policy=propagation_policy,
        )
    config = _BUILTIN_REGISTRY[cls]
    payload = await _run_request(
        kube,
        api=config.api,
        method=config.delete,
        namespace=namespace,
        name=name,
        body=delete_options,
        deadline=deadline,
        context=f"failed to delete {cls.__name__} {label}",
        missing_ok=True,
    )
    if payload is not None and not isinstance(payload, kubernetes.client.V1Status):
        msg = f"malformed Kubernetes response while deleting {cls.__name__} {label}"
        raise OSError(msg)


async def _delete(
    resource: Any,
    kube: Kube,
    *,
    deadline: Deadline,
    propagation_policy: DeletionPropagationPolicy | None = "Background",
    grace_period_seconds: int | None = None,
) -> None:
    if _BUILTIN_REGISTRY[type(resource)].scope == "cluster":
        name = resource.name
        if not name:
            msg = f"cannot delete {type(resource).__name__} with missing metadata.name"
            raise OSError(msg)
        await _delete_by_name(
            type(resource),
            kube,
            name=name,
            deadline=deadline,
            propagation_policy=propagation_policy,
            grace_period_seconds=grace_period_seconds,
        )
        return
    namespace = resource.namespace
    name = resource.name
    if not namespace or not name:
        msg = (
            f"cannot delete {type(resource).__name__} with missing "
            "metadata.name/namespace"
        )
        raise OSError(msg)
    await _delete_by_name(
        type(resource),
        kube,
        namespace=namespace,
        name=name,
        deadline=deadline,
        propagation_policy=propagation_policy,
        grace_period_seconds=grace_period_seconds,
    )


async def _wait_deleted(
    resource: Any,
    kube: Kube,
    *,
    deadline: Deadline,
) -> None:
    if _BUILTIN_REGISTRY[type(resource)].scope == "cluster":
        name = resource.name
        if not name:
            msg = (
                f"cannot wait for {type(resource).__name__} deletion with missing "
                "metadata.name"
            )
            raise OSError(msg)
        namespace = None
    else:
        namespace = resource.namespace
        name = resource.name
        if not namespace or not name:
            msg = (
                f"cannot wait for {type(resource).__name__} deletion with missing "
                "metadata.name/namespace"
            )
            raise OSError(msg)
    label = (
        f"{type(resource).__name__} {namespace}/{name}"
        if namespace
        else (f"{type(resource).__name__} {name}")
    )
    config = _BUILTIN_REGISTRY[type(resource)]

    async def deleted(attempt_deadline: Deadline) -> None:
        payload = await _run_request(
            kube,
            api=config.api,
            method=config.read,
            namespace=namespace,
            name=name,
            deadline=attempt_deadline,
            context=f"failed to read {label} while waiting for deletion",
            missing_ok=True,
        )
        if payload is None:
            return
        msg = f"{label} still exists"
        raise TimeoutError(msg)

    try:
        await until(
            deleted,
            deadline=deadline,
            delay=RESOURCE_WAIT_POLL_INTERVAL_SECONDS,
        )
    except TimeoutError as err:
        msg = f"timed out waiting for {label} deletion"
        raise TimeoutError(msg) from err


class CreatableResource:
    """Surface for resources that support create and delete."""

    @classmethod
    async def create_manifest(
        cls,
        kube: Kube,
        *,
        manifest: Mapping[str, object],
        deadline: Deadline,
        namespace: str | None = None,
        name: str | None = None,
        context: str | None = None,
        malformed_message: str | None = None,
        missing_ok: bool = True,
    ) -> Self:
        """Create one resource from a complete Kubernetes manifest.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        manifest : Mapping[str, object]
            Complete Kubernetes manifest.
        deadline : Deadline
            Maximum request budget in seconds.
        namespace : str | None, optional
            Namespace for namespaced resources.
        name : str | None, optional
            Name used in diagnostics.
        context : str | None, optional
            Error context override.
        malformed_message : str | None, optional
            Payload validation message override.
        missing_ok : bool, optional
            Whether HTTP 404 should be converted to `None` before payload
            validation.

        Returns
        -------
        KubeResource
            Wrapped created Kubernetes object.
        """
        return await _create_manifest(
            cls,
            kube,
            manifest=manifest,
            deadline=deadline,
            namespace=namespace,
            name=name,
            context=context,
            malformed_message=malformed_message,
            missing_ok=missing_ok,
        )

    @classmethod
    async def delete_by_name(
        cls,
        kube: Kube,
        *,
        name: str,
        deadline: Deadline,
        namespace: str | None = None,
        propagation_policy: DeletionPropagationPolicy | None = None,
        grace_period_seconds: int | None = None,
    ) -> None:
        """Delete one resource by name."""
        await _delete_by_name(
            cls,
            kube,
            name=name,
            deadline=deadline,
            namespace=namespace,
            propagation_policy=propagation_policy,
            grace_period_seconds=grace_period_seconds,
        )

    async def delete(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        propagation_policy: DeletionPropagationPolicy | None = "Background",
        grace_period_seconds: int | None = None,
    ) -> None:
        """Delete this resource from the cluster."""
        await _delete(
            self,
            kube,
            deadline=deadline,
            propagation_policy=propagation_policy,
            grace_period_seconds=grace_period_seconds,
        )

    async def wait_deleted(self, kube: Kube, *, deadline: Deadline) -> None:
        """Wait for this resource to disappear."""
        await _wait_deleted(self, kube, deadline=deadline)


class DeclarativeResource:
    """Surface for resources managed through create, patch, upsert, and delete."""

    @classmethod
    async def create_manifest(
        cls,
        kube: Kube,
        *,
        manifest: Mapping[str, object],
        deadline: Deadline,
        namespace: str | None = None,
        name: str | None = None,
        context: str | None = None,
        malformed_message: str | None = None,
        missing_ok: bool = True,
    ) -> Self:
        """Create one resource from a complete Kubernetes manifest.

        Returns
        -------
        KubeResource
            Wrapped created Kubernetes object.
        """
        return await _create_manifest(
            cls,
            kube,
            manifest=manifest,
            deadline=deadline,
            namespace=namespace,
            name=name,
            context=context,
            malformed_message=malformed_message,
            missing_ok=missing_ok,
        )

    @classmethod
    async def patch_manifest(
        cls,
        kube: Kube,
        *,
        name: str,
        manifest: Mapping[str, object],
        deadline: Deadline,
        namespace: str | None = None,
    ) -> Self:
        """Patch one resource from a complete Kubernetes manifest.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Resource name.
        manifest : Mapping[str, object]
            Complete Kubernetes manifest.
        deadline : Deadline
            Maximum request budget in seconds.
        namespace : str | None, optional
            Namespace for namespaced resources.

        Returns
        -------
        KubeResource
            Wrapped patched Kubernetes object.
        """
        return await _patch_manifest(
            cls,
            kube,
            name=name,
            manifest=manifest,
            deadline=deadline,
            namespace=namespace,
        )

    @classmethod
    async def upsert_manifest(
        cls,
        kube: Kube,
        *,
        name: str,
        manifest: Mapping[str, object],
        deadline: Deadline,
        namespace: str | None = None,
    ) -> Self:
        """Create or patch one resource from a complete Kubernetes manifest.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Resource name.
        manifest : Mapping[str, object]
            Complete Kubernetes manifest.
        deadline : Deadline
            Maximum request budget in seconds.
        namespace : str | None, optional
            Namespace for namespaced resources.

        Returns
        -------
        KubeResource
            Wrapped created or patched Kubernetes object.

        """
        return await _upsert_manifest(
            cls,
            kube,
            name=name,
            manifest=manifest,
            deadline=deadline,
            namespace=namespace,
        )

    @classmethod
    async def delete_by_name(
        cls,
        kube: Kube,
        *,
        name: str,
        deadline: Deadline,
        namespace: str | None = None,
        propagation_policy: DeletionPropagationPolicy | None = None,
        grace_period_seconds: int | None = None,
    ) -> None:
        """Delete one resource by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Resource name.
        deadline : Deadline
            Maximum request budget in seconds.
        namespace : str | None, optional
            Namespace for namespaced resources.
        propagation_policy : {"Background", "Foreground", "Orphan"} | None, optional
            Optional Kubernetes deletion propagation policy.
        grace_period_seconds : int | None, optional
            Optional Kubernetes deletion grace period.

        """
        await _delete_by_name(
            cls,
            kube,
            name=name,
            deadline=deadline,
            namespace=namespace,
            propagation_policy=propagation_policy,
            grace_period_seconds=grace_period_seconds,
        )

    async def delete(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        propagation_policy: DeletionPropagationPolicy | None = "Background",
        grace_period_seconds: int | None = None,
    ) -> None:
        """Delete this resource from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum request budget in seconds.
        propagation_policy : {"Background", "Foreground", "Orphan"} | None, optional
            Optional Kubernetes deletion propagation policy.
        grace_period_seconds : int | None, optional
            Optional Kubernetes deletion grace period.

        """
        await _delete(
            self,
            kube,
            deadline=deadline,
            propagation_policy=propagation_policy,
            grace_period_seconds=grace_period_seconds,
        )

    async def wait_deleted(self, kube: Kube, *, deadline: Deadline) -> None:
        """Wait for this resource to disappear.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Deadline for the resource to be deleted.

        """
        await _wait_deleted(self, kube, deadline=deadline)
