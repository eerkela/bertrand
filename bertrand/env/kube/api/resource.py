"""Kubernetes generated-resource adapters and wrapper mixins."""

from __future__ import annotations

import asyncio
import math
from collections.abc import Iterator, Mapping
from contextlib import suppress
from dataclasses import dataclass
from types import MappingProxyType
from typing import (
    TYPE_CHECKING,
    Any,
    Literal,
    Protocol,
    Self,
    TypeVar,
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
type _BuiltinOperation = Literal["read", "list", "create", "patch", "delete"]
RESOURCE_WAIT_POLL_INTERVAL_SECONDS = 0.5
_WATCH_EVENT_TYPES: frozenset[str] = frozenset(
    {"ADDED", "MODIFIED", "DELETED", "BOOKMARK", "ERROR"}
)


class _KubePayload(Protocol):
    @property
    def metadata(self) -> kubernetes.client.V1ObjectMeta | None: ...


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
    remaining: float,
    resource_version: str | None,
    label_selector: str | None,
    field_selector: str | None,
    api_kwargs: Mapping[str, object] | None,
) -> dict[str, object]:
    kwargs = dict(api_kwargs or {})
    if label_selector is not None:
        label_selector = label_selector.strip()
        if label_selector:
            kwargs["label_selector"] = label_selector
    if field_selector is not None:
        field_selector = field_selector.strip()
        if field_selector:
            kwargs["field_selector"] = field_selector
    if resource_version is not None:
        resource_version = resource_version.strip()
        if resource_version:
            kwargs["resource_version"] = resource_version
    if not math.isinf(remaining):
        kwargs.setdefault("timeout_seconds", max(1, math.ceil(remaining)))
    return kwargs


def _watch_api_exception_detail(err: ApiException) -> str:
    return (err.body or err.reason or str(err)).strip()


async def _read_watch_payload(
    iterator: Iterator[object],
    *,
    remaining: float,
    total_deadline: Deadline,
    context: str,
) -> object:
    try:
        return await asyncio.wait_for(
            asyncio.to_thread(_next_watch_payload, iterator),
            timeout=None if math.isinf(remaining) else remaining,
        )
    except TimeoutError as err:
        msg = f"{context} watch timed out after {total_deadline.timeout} seconds"
        raise TimeoutError(msg) from err


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
    )


async def _watch[T](
    fn: Callable[..., object],
    *,
    wrapper: Callable[[object], T],
    deadline: Deadline,
    context: str,
    resource_version: str | None = None,
    label_selector: str | None = None,
    field_selector: str | None = None,
    api_kwargs: Mapping[str, object] | None = None,
) -> AsyncIterator[WatchEvent[T]]:
    remaining = deadline.check(f"{context} watch timed out before it could start")
    kwargs = _watch_kwargs(
        remaining=remaining,
        resource_version=resource_version,
        label_selector=label_selector,
        field_selector=field_selector,
        api_kwargs=api_kwargs,
    )
    watcher = kubernetes.watch.Watch()
    iterator = watcher.stream(fn, **kwargs)
    try:
        while True:
            remaining = deadline.check(
                f"{context} watch timed out after {deadline.timeout} seconds"
            )
            try:
                payload = await _read_watch_payload(
                    iterator,
                    remaining=remaining,
                    total_deadline=deadline,
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
            event = _watch_event(payload, context=context, wrapper=wrapper)
            if isinstance(event, WatchExpired):
                raise WatchExpired(str(event))
            if isinstance(event, OSError):
                raise OSError(str(event))
            yield event
    finally:
        watcher.stop()


def _label_selector(labels: Mapping[str, str] | None) -> str | None:
    if not labels:
        return None
    return ",".join(f"{key}={value}" for key, value in labels.items())


def _normalized_namespaces(
    namespaces: Collection[str] | None,
) -> tuple[str, ...] | None:
    if namespaces is None:
        return None
    normalized = {namespace.strip() for namespace in namespaces}
    normalized.discard("")
    return tuple(sorted(normalized))


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


@dataclass(frozen=True)
class _BuiltinConfig:
    api: BuiltinAPI
    scope: ResourceScope


_BUILTIN_REGISTRY: dict[type[Any], _BuiltinConfig] = {}
_BuiltinClass = TypeVar("_BuiltinClass", bound=type[Any])


def builtin_resource(
    *,
    api: BuiltinAPI,
    scope: ResourceScope,
) -> Callable[[_BuiltinClass], _BuiltinClass]:
    """Register Kubernetes generated-client metadata for a wrapper class.

    Parameters
    ----------
    api : BuiltinAPI
        Attribute on :class:`Kube` exposing the generated API family.
    scope : {"cluster", "namespaced"}
        Kubernetes API scope for the resource.

    Returns
    -------
    Callable[[type[Any]], type[Any]]
        Class decorator that records endpoint metadata privately.
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

    def register(cls: _BuiltinClass) -> _BuiltinClass:
        _BUILTIN_REGISTRY[cls] = _BuiltinConfig(
            api=api,
            scope=scope,
        )
        return cls

    return register


class KubeResource[PayloadT: _KubePayload]:
    """Base class for generated Kubernetes resource wrappers.

    Attributes
    ----------
    _obj : PayloadT
        Typed Kubernetes client model returned by the cluster API.
    """

    _obj: PayloadT

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
        wrapper = cast("Callable[..., Self]", cls)
        return wrapper(_obj=payload)

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


def _config(cls: type[Any]) -> _BuiltinConfig:
    try:
        return _BUILTIN_REGISTRY[cls]
    except KeyError as err:
        msg = f"{cls.__name__} must be decorated with @builtin_resource"
        raise TypeError(msg) from err


def _payload_type(cls: type[Any]) -> type[Any]:
    for candidate in cls.__mro__:
        annotations = getattr(candidate, "__annotations__", {})
        if "_obj" not in annotations:
            continue
        payload = get_type_hints(candidate).get("_obj")
        if isinstance(payload, type):
            return payload
    msg = f"{cls.__name__} must annotate _obj with a Kubernetes payload type"
    raise TypeError(msg)


def _snake_case(name: str) -> str:
    out: list[str] = []
    for index, char in enumerate(name):
        if char.isupper() and index and not name[index - 1].isupper():
            out.append("_")
        out.append(char.lower())
    return "".join(out)


# TODO: _method_name is doing snake case normalization and cross-referencing against
# the kubernetes API, which is not what we want.  We want simple, composable,
# deterministic, and maintainable approaches, which should not require this kind of
# reflection or any other mysterious, unmaintainable hacks.


def _method_name(
    cls: type[Any],
    operation: _BuiltinOperation,
    *,
    namespace: str | None,
) -> str:
    config = _config(cls)
    slug = _snake_case(cls.__name__)
    if operation == "list":
        if config.scope == "namespaced":
            if namespace is None:
                return f"list_{slug}_for_all_namespaces"
            return f"list_namespaced_{slug}"
        return f"list_{slug}"
    if config.scope == "namespaced":
        return f"{operation}_namespaced_{slug}"
    return f"{operation}_{slug}"


def _method(
    cls: type[Any],
    kube: Kube,
    operation: _BuiltinOperation,
    *,
    namespace: str | None,
) -> Callable[..., object]:
    api = getattr(kube, _config(cls).api)
    name = _method_name(cls, operation, namespace=namespace)
    return cast("Callable[..., object]", getattr(api, name))


async def _run_request(
    cls: type[Any],
    kube: Kube,
    *,
    operation: _BuiltinOperation,
    namespace: str | None,
    deadline: Deadline,
    context: str,
    missing_ok: bool,
    name: str | None = None,
    body: Mapping[str, object] | object | None = None,
    label_selector: str | None = None,
    field_selector: str | None = None,
) -> object | None:
    return await kube.run(
        lambda request_timeout: _request(
            cls,
            kube,
            operation=operation,
            namespace=namespace,
            name=name,
            body=body,
            label_selector=label_selector,
            field_selector=field_selector,
            request_timeout=request_timeout,
        ),
        deadline=deadline,
        context=context,
        missing_ok=missing_ok,
    )


def _request(
    cls: type[Any],
    kube: Kube,
    *,
    operation: _BuiltinOperation,
    namespace: str | None,
    name: str | None,
    body: Mapping[str, object] | object | None,
    label_selector: str | None,
    field_selector: str | None,
    request_timeout: float | None,
) -> object:
    kwargs: dict[str, object] = {"_request_timeout": request_timeout}
    if name is not None:
        kwargs["name"] = name
    if body is not None:
        kwargs["body"] = body
    if label_selector is not None:
        kwargs["label_selector"] = label_selector
    if field_selector is not None:
        kwargs["field_selector"] = field_selector
    if _config(cls).scope == "namespaced":
        if operation == "list":
            if namespace is not None:
                kwargs["namespace"] = namespace
        elif namespace is None:
            msg = f"cannot {operation} namespaced resource without namespace"
            raise RuntimeError(msg)
        else:
            kwargs["namespace"] = namespace
    return _method(cls, kube, operation, namespace=namespace)(**kwargs)


def _single_namespace(
    cls: type[Any],
    namespace: str | None,
    *,
    action: str,
) -> str | None:
    namespace = namespace.strip() if namespace is not None else ""
    if _config(cls).scope == "cluster":
        if namespace:
            msg = f"{cls.__name__} is cluster-scoped; cannot {action} in a namespace"
            raise ValueError(msg)
        return None
    if not namespace:
        msg = f"{cls.__name__} {action} requires a namespace"
        raise ValueError(msg)
    return namespace


def _list_namespaces(
    cls: type[Any],
    *,
    namespace: str | None,
    namespaces: Collection[str] | None,
) -> Collection[str] | None:
    namespace = namespace.strip() if namespace is not None else ""
    if _config(cls).scope == "cluster":
        if namespace or namespaces is not None:
            msg = f"{cls.__name__} is cluster-scoped; cannot list by namespace"
            raise ValueError(msg)
        return None
    if namespace and namespaces is not None:
        msg = f"{cls.__name__} list accepts either namespace or namespaces, not both"
        raise ValueError(msg)
    return (namespace,) if namespace else namespaces


async def _list_payloads(
    cls: type[Any],
    kube: Kube,
    *,
    deadline: Deadline,
    namespaces: Collection[str] | None,
    label_selector: str | None,
    field_selector: str | None,
) -> builtins.list[object | None]:
    if _config(cls).scope == "cluster":
        return [
            await _run_request(
                cls,
                kube,
                operation="list",
                namespace=None,
                deadline=deadline,
                label_selector=label_selector,
                field_selector=field_selector,
                context=_list_context(cls, all_namespaces=False),
                missing_ok=False,
            )
        ]
    normalized = _normalized_namespaces(namespaces)
    if normalized is None:
        return [
            await _run_request(
                cls,
                kube,
                operation="list",
                namespace=None,
                deadline=deadline,
                label_selector=label_selector,
                field_selector=field_selector,
                context=_list_context(cls, all_namespaces=True),
                missing_ok=False,
            )
        ]
    return list(
        await asyncio.gather(
            *(
                _run_request(
                    cls,
                    kube,
                    operation="list",
                    namespace=namespace,
                    deadline=deadline,
                    label_selector=label_selector,
                    field_selector=field_selector,
                    context=(
                        f"failed to list {cls.__name__}s in namespace {namespace!r}"
                    ),
                    missing_ok=False,
                )
                for namespace in normalized
            )
        )
    )


def _watch_namespace(cls: type[Any], namespace: str | None) -> str | None:
    namespace = namespace.strip() if namespace is not None else ""
    if _config(cls).scope == "cluster":
        if namespace:
            msg = f"{cls.__name__} is cluster-scoped; cannot watch in a namespace"
            raise ValueError(msg)
        return None
    return namespace or None


def _watch_request(
    cls: type[Any],
    kube: Kube,
    *,
    namespace: str | None,
) -> tuple[Callable[..., object], Mapping[str, object], str]:
    if _config(cls).scope == "cluster":
        return (
            _method(cls, kube, "list", namespace=None),
            {},
            f"failed to watch {cls.__name__}s",
        )
    if namespace is None:
        return (
            _method(cls, kube, "list", namespace=None),
            {},
            f"failed to watch {cls.__name__}s across all namespaces",
        )
    return (
        _method(cls, kube, "list", namespace=namespace),
        {"namespace": namespace},
        f"failed to watch {cls.__name__}s in namespace {namespace!r}",
    )


def _wrap_payload[ResourceT: KubeResource[Any]](
    cls: type[ResourceT],
    payload: object,
    *,
    malformed_message: str | None = None,
    context: str | None = None,
) -> ResourceT:
    if not isinstance(payload, _payload_type(cls)):
        if malformed_message is not None:
            raise OSError(malformed_message)
        msg = f"malformed Kubernetes {context or cls.__name__} payload"
        raise OSError(msg)
    return cls.from_payload(cast("Any", payload))


def _list_items(cls: type[Any], payload: object | None) -> builtins.list[Any]:
    if payload is None:
        return []
    items = getattr(payload, "items", None)
    if not isinstance(items, list):
        msg = f"malformed Kubernetes {cls.__name__} list payload"
        raise OSError(msg)

    expected = _payload_type(cls)
    out: builtins.list[Any] = []
    for item in items:
        if not isinstance(item, expected):
            msg = f"malformed Kubernetes {cls.__name__} entry in list payload"
            raise OSError(msg)
        out.append(item)
    return out


def _resource_name(resource: KubeResource[Any], action: str) -> str:
    name = resource.name
    if not name:
        msg = f"cannot {action} with missing metadata.name"
        raise OSError(msg)
    return name


def _resource_namespace_name(
    resource: KubeResource[Any],
    action: str,
) -> tuple[str, str]:
    namespace = resource.namespace
    name = resource.name
    if not namespace or not name:
        msg = f"cannot {action} with missing metadata.name/namespace"
        raise OSError(msg)
    return namespace, name


def _resource_label(
    resource: KubeResource[Any],
    *,
    name: str | None = None,
    namespace: str | None = None,
) -> str:
    namespace = (namespace or resource.namespace).strip()
    name = (name or resource.name).strip()
    if namespace and name:
        return f"{type(resource).__name__} {namespace}/{name}"
    if name:
        return f"{type(resource).__name__} {name}"
    return type(resource).__name__


def _name_label(*, name: str, namespace: str | None) -> str:
    return f"{namespace}/{name}" if namespace else name


def _list_context(cls: type[Any], *, all_namespaces: bool) -> str:
    if all_namespaces:
        return f"failed to list {cls.__name__}s across all namespaces"
    return f"failed to list {cls.__name__}s"


def _field_selector(field_selector: str | None) -> str | None:
    field_selector = field_selector.strip() if field_selector is not None else None
    return field_selector or None


class Readable[PayloadT: _KubePayload](KubeResource[PayloadT]):
    """Mixin adding generated Kubernetes read operations."""

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
        namespace = _single_namespace(cls, namespace, action="read")
        label = _name_label(name=name, namespace=namespace)
        payload = await _run_request(
            cls,
            kube,
            operation="read",
            namespace=namespace,
            name=name,
            deadline=deadline,
            context=context or f"failed to read {cls.__name__} {label!r}",
            missing_ok=True,
        )
        if payload is None:
            return None
        return _wrap_payload(cls, payload)

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
        """
        if _config(type(self)).scope == "cluster":
            name = _resource_name(self, f"refresh {type(self).__name__}")
            return await type(self).get(kube, name=name, deadline=deadline)
        namespace, name = _resource_namespace_name(
            self, f"refresh {type(self).__name__}"
        )
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


class Listable[PayloadT: _KubePayload](KubeResource[PayloadT]):
    """Mixin adding generated Kubernetes list operations."""

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        deadline: Deadline,
        namespace: str | None = None,
        namespaces: Collection[str] | None = None,
        labels: Mapping[str, str] | None = None,
        field_selector: str | None = None,
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
        field_selector : str | None, optional
            Raw Kubernetes field selector.

        Returns
        -------
        list[KubeResource]
            Wrapped Kubernetes objects matching the filters.
        """
        selected = _list_namespaces(cls, namespace=namespace, namespaces=namespaces)
        payloads = await _list_payloads(
            cls,
            kube,
            deadline=deadline,
            namespaces=selected,
            label_selector=_label_selector(labels),
            field_selector=_field_selector(field_selector),
        )
        items: builtins.list[Self] = []
        for payload in payloads:
            items.extend(_wrap_payload(cls, item) for item in _list_items(cls, payload))
        return items


class Watchable[PayloadT: _KubePayload](KubeResource[PayloadT]):
    """Mixin adding generated Kubernetes watch operations."""

    @classmethod
    async def watch(
        cls,
        kube: Kube,
        *,
        deadline: Deadline,
        namespace: str | None = None,
        labels: Mapping[str, str] | None = None,
        field_selector: str | None = None,
        resource_version: str | None = None,
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
        field_selector : str | None, optional
            Raw Kubernetes field selector.
        resource_version : str | None, optional
            Resource version to watch from.

        Yields
        ------
        WatchEvent[KubeResource]
            Typed watch events containing wrapped Kubernetes objects.
        """
        namespace = _watch_namespace(cls, namespace)
        watch_fn, api_kwargs, context = _watch_request(cls, kube, namespace=namespace)
        async for event in _watch(
            watch_fn,
            wrapper=lambda payload: _wrap_payload(
                cls, payload, context=f"{cls.__name__} watch"
            ),
            deadline=deadline,
            context=context,
            resource_version=resource_version,
            label_selector=_label_selector(labels),
            field_selector=field_selector,
            api_kwargs=api_kwargs,
        ):
            yield event


class Creatable[PayloadT: _KubePayload](KubeResource[PayloadT]):
    """Mixin adding generated Kubernetes create operations."""

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
        namespace = _single_namespace(cls, namespace, action="create")
        label = _name_label(name=name or cls.__name__, namespace=namespace)
        payload = await _run_request(
            cls,
            kube,
            operation="create",
            namespace=namespace,
            body=manifest,
            deadline=deadline,
            context=context or f"failed to create {cls.__name__} {label}",
            missing_ok=missing_ok,
        )
        return _wrap_payload(cls, payload, malformed_message=malformed_message)


class Patchable[PayloadT: _KubePayload](KubeResource[PayloadT]):
    """Mixin adding generated Kubernetes patch operations."""

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
        namespace = _single_namespace(cls, namespace, action="patch")
        label = _name_label(name=name, namespace=namespace)
        payload = await _run_request(
            cls,
            kube,
            operation="patch",
            namespace=namespace,
            name=name,
            body=manifest,
            deadline=deadline,
            context=f"failed to patch {cls.__name__} {label}",
            missing_ok=False,
        )
        return _wrap_payload(cls, payload)


class Upsertable[PayloadT: _KubePayload](KubeResource[PayloadT]):
    """Mixin adding generated Kubernetes create-or-patch operations."""

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

        Raises
        ------
        OSError
            If Kubernetes create/patch fails or returns malformed data.
        """
        namespace = _single_namespace(cls, namespace, action="upsert")
        label = _name_label(name=name, namespace=namespace)
        try:
            payload = await _run_request(
                cls,
                kube,
                operation="create",
                namespace=namespace,
                body=manifest,
                deadline=deadline,
                context=f"failed to create {cls.__name__} {label}",
                missing_ok=False,
            )
        except OSError as err:
            if not isinstance(err, Kube.APIError) or err.status != 409:
                raise
            payload = await _run_request(
                cls,
                kube,
                operation="patch",
                namespace=namespace,
                name=name,
                body=manifest,
                deadline=deadline,
                context=f"failed to patch {cls.__name__} {label}",
                missing_ok=False,
            )
        return _wrap_payload(cls, payload)


class Deletable[PayloadT: _KubePayload](KubeResource[PayloadT]):
    """Mixin adding generated Kubernetes delete operations."""

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
        namespace = _single_namespace(cls, namespace, action="delete")
        label = _name_label(name=name, namespace=namespace)
        delete_options = None
        if propagation_policy is not None or grace_period_seconds is not None:
            delete_options = _delete_options(
                kind=cls.__name__,
                propagation_policy=propagation_policy,
                grace_period_seconds=grace_period_seconds,
            )
        payload = await _run_request(
            cls,
            kube,
            operation="delete",
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
        if _config(type(self)).scope == "cluster":
            name = _resource_name(self, f"delete {type(self).__name__}")
            await type(self).delete_by_name(
                kube,
                name=name,
                deadline=deadline,
                propagation_policy=propagation_policy,
                grace_period_seconds=grace_period_seconds,
            )
            return
        namespace, name = _resource_namespace_name(
            self, f"delete {type(self).__name__}"
        )
        await type(self).delete_by_name(
            kube,
            namespace=namespace,
            name=name,
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

        Raises
        ------
        TimeoutError
            If the wait deadline expires before the resource disappears.
        """
        if _config(type(self)).scope == "cluster":
            name = _resource_name(self, f"wait for {type(self).__name__} deletion")
            namespace = None
        else:
            namespace, name = _resource_namespace_name(
                self,
                f"wait for {type(self).__name__} deletion",
            )
        label = _resource_label(self, name=name, namespace=namespace)

        async def deleted(attempt_deadline: Deadline) -> None:
            payload = await _run_request(
                type(self),
                kube,
                operation="read",
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
