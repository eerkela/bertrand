"""Shared Kubernetes resource wrapper primitives."""

from __future__ import annotations

import asyncio
import math
from collections.abc import Callable, Collection, Mapping
from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING, Any, ClassVar, Literal, Protocol, Self, cast

import kubernetes
from kubernetes import client as kube_client
from kubernetes.client.rest import ApiException

from bertrand.env.git import EMPTY_MAPPING, Deadline, until
from bertrand.env.kube.api.client import Kube

if TYPE_CHECKING:
    import builtins
    from collections.abc import AsyncIterator, Awaitable
    from datetime import datetime

type DeletionPropagationPolicy = Literal["Background", "Foreground", "Orphan"]
RESOURCE_WAIT_POLL_INTERVAL_SECONDS = 0.5
_DELETION_PROPAGATION_POLICIES = frozenset({"Background", "Foreground", "Orphan"})


class _KubeManifest(Protocol):
    @property
    def name(self) -> str: ...
    @property
    def namespace(self) -> str | None: ...
    def manifest(self) -> Mapping[str, Any]: ...


class _HasObjectMeta(Protocol):
    @property
    def metadata(self) -> kube_client.V1ObjectMeta | None: ...


class _ListMeta(Protocol):
    @property
    def resource_version(self) -> str | None: ...


class _HasListMeta(Protocol):
    @property
    def metadata(self) -> _ListMeta | None: ...


class _KubeListPayload[PayloadT](_HasListMeta, Protocol):
    @property
    def items(self) -> list[PayloadT]: ...


class _ResourceAPI[PayloadT](Protocol):
    @property
    def kind(self) -> str: ...
    @property
    def namespaced(self) -> bool: ...
    @property
    def can_create(self) -> bool: ...
    @property
    def can_patch(self) -> bool: ...
    @property
    def can_delete(self) -> bool: ...

    def items(
        self,
        payload: _KubeListPayload[PayloadT],
    ) -> builtins.list[PayloadT]: ...
    def list_resource_version(self, payload: _KubeListPayload[PayloadT]) -> str: ...
    def name(self, payload: PayloadT) -> str: ...
    def namespace(self, payload: PayloadT) -> str: ...
    def labels(self, payload: PayloadT) -> Mapping[str, str]: ...
    def annotations(self, payload: PayloadT) -> Mapping[str, str]: ...
    def resource_version(self, payload: PayloadT) -> str: ...
    def uid(self, payload: PayloadT) -> str: ...
    def created_at(self, payload: PayloadT) -> datetime | None: ...
    async def read(
        self,
        kube: Kube,
        *,
        name: str,
        namespace: str | None,
        deadline: Deadline,
    ) -> PayloadT | None: ...
    async def list(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        namespace: str | None,
        namespaces: Collection[str] | None,
        label_selector: str,
        field_selector: str,
    ) -> builtins.list[_KubeListPayload[PayloadT]]: ...
    async def create(
        self,
        kube: Kube,
        *,
        name: str,
        namespace: str | None,
        body: Mapping[str, Any],
        deadline: Deadline,
    ) -> PayloadT: ...
    async def patch(
        self,
        kube: Kube,
        *,
        name: str,
        namespace: str | None,
        body: Mapping[str, object],
        deadline: Deadline,
        context: str,
    ) -> PayloadT: ...
    async def delete(
        self,
        kube: Kube,
        *,
        name: str,
        namespace: str | None,
        body: kube_client.V1DeleteOptions | None,
        deadline: Deadline,
    ) -> object | None: ...
    def watch_method(
        self,
        kube: Kube,
        *,
        namespace: str | None,
    ) -> tuple[Callable[..., Any], dict[str, Any], str]: ...


class WatchExpiredError(OSError):
    """Raised when Kubernetes can no longer serve a watch resource version."""


@dataclass(frozen=True)
class WatchEvent[T]:
    """Typed Kubernetes watch event.

    Parameters
    ----------
    type : WatchEvent.Type
        Normalized Kubernetes watch event type.
    object : T
        Wrapped Kubernetes resource object carried by the event.
    resource_version : str
        Resource version reported by the event object, or an empty string when
        unavailable.
    """

    type Type = Literal["ADDED", "MODIFIED", "DELETED", "BOOKMARK", "ERROR"]
    Types: ClassVar[Collection[Type]] = frozenset(
        ("ADDED", "MODIFIED", "DELETED", "BOOKMARK", "ERROR")
    )

    type: Type
    object: T
    resource_version: str


_WATCH_END = object()


def _watch_mapping(value: Any, *, context: str, label: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        msg = f"{context} watch {label} is not a mapping"
        raise OSError(msg)
    out: dict[str, Any] = {}
    for key, item in value.items():
        if not isinstance(key, str):
            msg = f"{context} watch {label} has non-string key {key!r}"
            raise OSError(msg)
        out[key] = item
    return out


def _parse_watch_event(payload: Any, *, context: str) -> WatchEvent[Any]:
    payload = _watch_mapping(payload, context=context, label="event payload")
    event_type = payload.get("type")
    if event_type not in WatchEvent.Types:
        msg = f"{context} watch returned unknown event type {event_type!r}"
        raise OSError(msg)
    event_type = cast("WatchEvent.Type", event_type)

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

    raw_object = _watch_mapping(raw_object, context=context, label="raw object")
    if event_type == "ERROR":
        raw_code = raw_object.get("code")
        reason = str(raw_object.get("reason") or "").strip()
        message = str(raw_object.get("message") or "").strip()

        code: int | None = None
        if raw_code is not None:
            try:
                code = int(str(raw_code).strip())
            except ValueError:
                code = None

        detail = message or reason or str(raw_object).strip()
        if code == 410:
            msg = f"{context} watch expired: {detail}"
            raise WatchExpiredError(msg)
        if detail:
            msg = f"{context} watch returned an error event: {detail}"
        else:
            msg = f"{context} watch returned an error event"
        raise OSError(msg)

    metadata = raw_object.get("metadata")
    resource_version = ""
    if isinstance(metadata, Mapping):
        metadata = _watch_mapping(metadata, context=context, label="object metadata")
        resource_version = str(metadata.get("resourceVersion") or "").strip()

    return WatchEvent(
        type=event_type,
        object=obj,
        resource_version=resource_version,
    )


async def watch_collection[T](
    *,
    deadline: Deadline,
    snapshot: Callable[[Deadline], Awaitable[str]],
    stream: Callable[[str, Deadline], AsyncIterator[WatchEvent[T]]],
) -> AsyncIterator[WatchEvent[T]]:
    """Watch a Kubernetes collection from snapshot through resumable streams.

    Parameters
    ----------
    deadline : Deadline
        Maximum watch budget shared by snapshots and stream attempts.
    snapshot : Callable[[Deadline], Awaitable[str]]
        Coroutine that lists the collection and returns its `resourceVersion`.
    stream : Callable[[str, Deadline], AsyncIterator[WatchEvent[T]]]
        Async iterator factory that streams events from a snapshot
        `resourceVersion`.

    Yields
    ------
    WatchEvent[T]
        Stream events from the watched collection.

    Raises
    ------
    TimeoutError
        If a snapshot or stream times out before the shared deadline expires.

    Notes
    -----
    `WatchExpiredError` from `stream` is handled by re-snapshotting the collection.
    Other exceptions from `snapshot` or `stream` propagate to the caller.
    """
    while True:
        if deadline.remaining <= 0:
            return
        resource_version = await snapshot(deadline)
        if deadline.remaining <= 0:
            return
        try:
            async for event in stream(resource_version, deadline):
                yield event
        except WatchExpiredError:
            continue
        except TimeoutError:
            if deadline.remaining <= 0:
                return
            raise
        else:
            return


async def watch_stream(
    fn: Callable[..., Any],
    *,
    deadline: Deadline,
    context: str,
    resource_version: str = "",
    label_selector: str = "",
    field_selector: str = "",
    api_kwargs: Mapping[str, Any] | None = None,
) -> AsyncIterator[WatchEvent[Any]]:
    """Stream raw Kubernetes watch events from a list-style API method.

    Parameters
    ----------
    fn : Callable[..., Any]
        Kubernetes list-style API method to stream with `Watch().stream()`.
    deadline : Deadline
        Maximum stream budget.
    context : str
        Human-readable operation context for error messages.
    resource_version : str, optional
        Internal Kubernetes `resourceVersion` to stream from.
    label_selector : str, optional
        Kubernetes label selector to pass to the stream request.
    field_selector : str, optional
        Kubernetes field selector to pass to the stream request.
    api_kwargs : Mapping[str, Any] | None, optional
        Extra keyword arguments required by the target API method.

    Yields
    ------
    WatchEvent[Any]
        Parsed Kubernetes watch events with untyped payload objects.

    Raises
    ------
    WatchExpiredError
        If Kubernetes reports that the requested resource version has expired.
    TimeoutError
        If the watch cannot complete before the deadline.
    OSError
        If Kubernetes returns malformed watch event payloads.
    """
    remaining = deadline.check(f"{context} watch timed out before it could start")
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

    watcher = kubernetes.watch.Watch()
    iterator = watcher.stream(fn, **kwargs)
    try:
        while True:
            remaining = deadline.check(
                f"{context} watch timed out after {deadline.timeout} seconds"
            )
            try:
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
                    msg = f"{context} watch expired: {detail}"
                    raise WatchExpiredError(msg) from err
                msg = (
                    f"{context} watch failed with kubernetes API status {err.status}: "
                    f"{detail}"
                )
                raise OSError(msg) from err
            yield _parse_watch_event(payload, context=context)
    finally:
        watcher.stop()


def _api[PayloadT](cls: type[KubeResource[PayloadT, Any]]) -> _ResourceAPI[PayloadT]:
    api = cls._resource_api
    if api is None:
        msg = f"{cls.__name__} is missing Kubernetes resource configuration"
        raise NotImplementedError(msg)
    return cast("_ResourceAPI[PayloadT]", api)


@dataclass(frozen=True)
class _BuiltinResourceAPI[PayloadT: _HasObjectMeta]:
    kind: str
    namespaced: bool
    api: type[Any]
    payload: type[PayloadT]
    read_method: Callable[..., Any]
    list_method: Callable[..., Any]
    list_all_method: Callable[..., Any] | None
    create_method: Callable[..., Any] | None
    patch_method: Callable[..., Any] | None
    delete_method: Callable[..., Any] | None

    @property
    def can_create(self) -> bool:
        """Return whether this binding supports generic create."""
        return self.create_method is not None

    @property
    def can_patch(self) -> bool:
        """Return whether this binding supports generic patch."""
        return self.patch_method is not None

    @property
    def can_delete(self) -> bool:
        """Return whether this binding supports generic delete."""
        return self.delete_method is not None

    def _identity(
        self,
        *,
        name: str,
        namespace: str | None,
        action: str,
    ) -> tuple[str, str, str]:
        name = name.strip()
        namespace = namespace.strip() if namespace is not None else ""
        if self.namespaced:
            if not namespace or not name:
                msg = f"{self.kind} {action} requires non-empty namespace and name"
                raise OSError(msg)
            return namespace, name, f"{namespace}/{name}"
        if namespace:
            msg = f"{self.kind} is cluster-scoped; cannot {action} by namespace"
            raise ValueError(msg)
        if not name:
            msg = f"{self.kind} {action} requires a non-empty name"
            raise OSError(msg)
        return "", name, name

    def items(
        self,
        payload: _KubeListPayload[PayloadT],
    ) -> builtins.list[PayloadT]:
        """Return items from one Kubernetes generated-model list payload.

        Parameters
        ----------
        payload : _KubeListPayload[PayloadT]
            Kubernetes list payload to read.

        Returns
        -------
        list[PayloadT]
            Kubernetes generated-model payload items.
        """
        return payload.items

    def list_resource_version(self, payload: _KubeListPayload[PayloadT]) -> str:
        """Return a generated-model list payload's resource version.

        Parameters
        ----------
        payload : _KubeListPayload[PayloadT]
            Kubernetes list payload returned by a generated API method.

        Returns
        -------
        str
            Trimmed list `metadata.resourceVersion`.

        Raises
        ------
        OSError
            If Kubernetes omits the list resource version.
        """
        metadata = payload.metadata
        resource_version = (
            str(metadata.resource_version or "").strip() if metadata is not None else ""
        )
        if not resource_version:
            msg = f"Kubernetes {self.kind} list had no resourceVersion"
            raise OSError(msg)
        return resource_version

    def name(self, payload: PayloadT) -> str:
        """Return a generated-model payload's metadata name.

        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        metadata = payload.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    def namespace(self, payload: PayloadT) -> str:
        """Return a generated-model payload's metadata namespace.

        Returns
        -------
        str
            Trimmed `metadata.namespace`, or an empty string when unavailable.
        """
        metadata = payload.metadata
        return (metadata.namespace or "").strip() if metadata is not None else ""

    def labels(self, payload: PayloadT) -> Mapping[str, str]:
        """Return a generated-model payload's metadata labels.

        Returns
        -------
        Mapping[str, str]
            Read-only `metadata.labels`, or an empty mapping when unavailable.
        """
        metadata = payload.metadata
        if metadata is None or metadata.labels is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.labels)

    def annotations(self, payload: PayloadT) -> Mapping[str, str]:
        """Return a generated-model payload's metadata annotations.

        Returns
        -------
        Mapping[str, str]
            Read-only `metadata.annotations`, or an empty mapping when unavailable.
        """
        metadata = payload.metadata
        if metadata is None or metadata.annotations is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.annotations)

    def resource_version(self, payload: PayloadT) -> str:
        """Return a generated-model payload's metadata resource version.

        Returns
        -------
        str
            Trimmed `metadata.resourceVersion`, or an empty string when unavailable.
        """
        metadata = payload.metadata
        return (metadata.resource_version or "").strip() if metadata is not None else ""

    def uid(self, payload: PayloadT) -> str:
        """Return a generated-model payload's metadata UID.

        Returns
        -------
        str
            Trimmed `metadata.uid`, or an empty string when unavailable.
        """
        metadata = payload.metadata
        return (metadata.uid or "").strip() if metadata is not None else ""

    def created_at(self, payload: PayloadT) -> datetime | None:
        """Return a generated-model payload's creation timestamp.

        Returns
        -------
        datetime | None
            Kubernetes `metadata.creationTimestamp`, or `None` when unavailable.
        """
        metadata = payload.metadata
        return metadata.creation_timestamp if metadata is not None else None

    async def read(
        self,
        kube: Kube,
        *,
        name: str,
        namespace: str | None,
        deadline: Deadline,
    ) -> PayloadT | None:
        """Read one Kubernetes generated-model resource by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Kubernetes object name.
        namespace : str | None
            Namespace for namespaced resources.
        deadline : Deadline
            Maximum request budget in seconds.

        Returns
        -------
        PayloadT | None
            Kubernetes generated-model payload, or `None` when missing.
        """
        namespace, name, label = self._identity(
            name=name,
            namespace=namespace,
            action="get",
        )
        api = self.api(kube.client)
        if self.namespaced:
            return cast(
                "PayloadT | None",
                await kube.run(
                    lambda request_timeout: self.read_method(
                        api,
                        name=name,
                        namespace=namespace,
                        _request_timeout=request_timeout,
                    ),
                    deadline=deadline,
                    context=f"failed to read {self.kind} {label}",
                    missing_ok=True,
                ),
            )
        return cast(
            "PayloadT | None",
            await kube.run(
                lambda request_timeout: self.read_method(
                    api,
                    name=name,
                    _request_timeout=request_timeout,
                ),
                deadline=deadline,
                context=f"failed to read {self.kind} {label}",
                missing_ok=True,
            ),
        )

    async def list(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        namespace: str | None,
        namespaces: Collection[str] | None,
        label_selector: str,
        field_selector: str,
    ) -> builtins.list[_KubeListPayload[PayloadT]]:
        """List Kubernetes generated-model resources.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum request budget in seconds.
        namespace : str | None
            Single namespace filter.
        namespaces : Collection[str] | None
            Multiple namespace filters.
        label_selector : str
            Rendered Kubernetes label selector.
        field_selector : str
            Rendered Kubernetes field selector.

        Returns
        -------
        list[_KubeListPayload[PayloadT]]
            Kubernetes generated-model list payloads.

        Raises
        ------
        ValueError
            If namespace filters are invalid for the resource scope.
        """
        api = self.api(kube.client)
        namespace = namespace.strip() if namespace is not None else ""
        if not self.namespaced:
            if namespace or namespaces is not None:
                msg = f"{self.kind} is cluster-scoped; cannot list by namespace"
                raise ValueError(msg)
            return [
                cast(
                    "_KubeListPayload[PayloadT]",
                    await kube.run(
                        lambda request_timeout: self.list_method(
                            api,
                            label_selector=label_selector or None,
                            field_selector=field_selector or None,
                            _request_timeout=request_timeout,
                        ),
                        deadline=deadline,
                        context=f"failed to list {self.kind} resources",
                        missing_ok=False,
                    ),
                )
            ]

        if namespace and namespaces is not None:
            msg = f"{self.kind} list accepts either namespace or namespaces, not both"
            raise ValueError(msg)
        selected = (namespace,) if namespace else namespaces
        if selected is None:
            list_all = self.list_all_method
            if list_all is None:
                msg = f"{self.kind} cannot be listed across all namespaces"
                raise NotImplementedError(msg)
            return [
                cast(
                    "_KubeListPayload[PayloadT]",
                    await kube.run(
                        lambda request_timeout: list_all(
                            api,
                            label_selector=label_selector or None,
                            field_selector=field_selector or None,
                            _request_timeout=request_timeout,
                        ),
                        deadline=deadline,
                        context=(
                            f"failed to list {self.kind} resources across namespaces"
                        ),
                        missing_ok=False,
                    ),
                )
            ]

        normalized = tuple(sorted({item.strip() for item in selected if item.strip()}))
        if not normalized:
            return []
        payloads = await asyncio.gather(
            *(
                kube.run(
                    lambda request_timeout, namespace=namespace: self.list_method(
                        api,
                        namespace=namespace,
                        label_selector=label_selector or None,
                        field_selector=field_selector or None,
                        _request_timeout=request_timeout,
                    ),
                    deadline=deadline,
                    context=(
                        f"failed to list {self.kind} resources in namespace "
                        f"{namespace!r}"
                    ),
                    missing_ok=False,
                )
                for namespace in normalized
            )
        )
        return [cast("_KubeListPayload[PayloadT]", payload) for payload in payloads]

    async def create(
        self,
        kube: Kube,
        *,
        name: str,
        namespace: str | None,
        body: Mapping[str, Any],
        deadline: Deadline,
    ) -> PayloadT:
        """Create one Kubernetes generated-model resource.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Kubernetes object name.
        namespace : str | None
            Namespace for namespaced resources.
        body : Mapping[str, Any]
            Kubernetes manifest body.
        deadline : Deadline
            Maximum request budget in seconds.

        Returns
        -------
        PayloadT
            Kubernetes generated-model payload.

        Raises
        ------
        NotImplementedError
            If this binding has no create method.
        """
        create = self.create_method
        if create is None:
            msg = f"{self.kind} does not implement create"
            raise NotImplementedError(msg)
        namespace, _, label = self._identity(
            name=name,
            namespace=namespace,
            action="create",
        )
        api = self.api(kube.client)
        if self.namespaced:
            return cast(
                "PayloadT",
                await kube.run(
                    lambda request_timeout: create(
                        api,
                        namespace=namespace,
                        body=body,
                        _request_timeout=request_timeout,
                    ),
                    deadline=deadline,
                    context=f"failed to create {self.kind} {label}",
                    missing_ok=False,
                ),
            )
        return cast(
            "PayloadT",
            await kube.run(
                lambda request_timeout: create(
                    api,
                    body=body,
                    _request_timeout=request_timeout,
                ),
                deadline=deadline,
                context=f"failed to create {self.kind} {label}",
                missing_ok=False,
            ),
        )

    async def patch(
        self,
        kube: Kube,
        *,
        name: str,
        namespace: str | None,
        body: Mapping[str, object],
        deadline: Deadline,
        context: str,
    ) -> PayloadT:
        """Patch one Kubernetes generated-model resource.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Kubernetes object name.
        namespace : str | None
            Namespace for namespaced resources.
        body : Mapping[str, object]
            Kubernetes patch body.
        deadline : Deadline
            Maximum request budget in seconds.
        context : str
            Error context for the Kubernetes request.

        Returns
        -------
        PayloadT
            Kubernetes generated-model payload.

        Raises
        ------
        NotImplementedError
            If this binding has no patch method.
        """
        patch = self.patch_method
        if patch is None:
            msg = f"{self.kind} does not implement patch"
            raise NotImplementedError(msg)
        namespace, name, _ = self._identity(
            name=name,
            namespace=namespace,
            action="patch",
        )
        api = self.api(kube.client)
        if self.namespaced:
            return cast(
                "PayloadT",
                await kube.run(
                    lambda request_timeout: patch(
                        api,
                        name=name,
                        namespace=namespace,
                        body=body,
                        _request_timeout=request_timeout,
                    ),
                    deadline=deadline,
                    context=context,
                    missing_ok=False,
                ),
            )
        return cast(
            "PayloadT",
            await kube.run(
                lambda request_timeout: patch(
                    api,
                    name=name,
                    body=body,
                    _request_timeout=request_timeout,
                ),
                deadline=deadline,
                context=context,
                missing_ok=False,
            ),
        )

    async def delete(
        self,
        kube: Kube,
        *,
        name: str,
        namespace: str | None,
        body: kube_client.V1DeleteOptions | None,
        deadline: Deadline,
    ) -> object | None:
        """Delete one Kubernetes generated-model resource.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Kubernetes object name.
        namespace : str | None
            Namespace for namespaced resources.
        body : kubernetes.client.V1DeleteOptions | None
            Kubernetes deletion options.
        deadline : Deadline
            Maximum request budget in seconds.

        Returns
        -------
        object | None
            Raw Kubernetes delete response.

        Raises
        ------
        NotImplementedError
            If this binding has no delete method.
        """
        delete = self.delete_method
        if delete is None:
            msg = f"{self.kind} does not implement delete"
            raise NotImplementedError(msg)
        namespace, name, label = self._identity(
            name=name,
            namespace=namespace,
            action="delete",
        )
        api = self.api(kube.client)
        if self.namespaced:
            return await kube.run(
                lambda request_timeout: delete(
                    api,
                    name=name,
                    namespace=namespace,
                    body=body,
                    _request_timeout=request_timeout,
                ),
                deadline=deadline,
                context=f"failed to delete {self.kind} {label}",
                missing_ok=True,
            )
        return await kube.run(
            lambda request_timeout: delete(
                api,
                name=name,
                body=body,
                _request_timeout=request_timeout,
            ),
            deadline=deadline,
            context=f"failed to delete {self.kind} {label}",
            missing_ok=True,
        )

    def watch_method(
        self,
        kube: Kube,
        *,
        namespace: str | None,
    ) -> tuple[Callable[..., Any], dict[str, Any], str]:
        """Return the list-style method used for watch snapshots and streams.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str | None
            Namespace to watch.

        Returns
        -------
        tuple[Callable[..., Any], dict[str, Any], str]
            List method, keyword arguments, and error context.

        Raises
        ------
        NotImplementedError
            If this binding cannot watch across all namespaces.
        ValueError
            If namespace is invalid for this resource scope.
        """
        namespace = namespace.strip() if namespace is not None else ""
        api = self.api(kube.client)
        if self.namespaced:
            if namespace:
                return (
                    lambda **kwargs: self.list_method(api, **kwargs),
                    {"namespace": namespace},
                    (
                        f"failed to watch {self.kind} resources in namespace "
                        f"{namespace!r}"
                    ),
                )
            list_all = self.list_all_method
            if list_all is None:
                msg = f"{self.kind} cannot be watched across all namespaces"
                raise NotImplementedError(msg)
            return (
                lambda **kwargs: list_all(api, **kwargs),
                {},
                f"failed to watch {self.kind} resources across namespaces",
            )
        if namespace:
            msg = f"{self.kind} is cluster-scoped; cannot watch by namespace"
            raise ValueError(msg)
        return (
            lambda **kwargs: self.list_method(api, **kwargs),
            {},
            f"failed to watch {self.kind} resources",
        )


@dataclass(frozen=True)
class KubeResource[PayloadT, ManifestT: _KubeManifest]:
    """Base class for Kubernetes generated-model wrappers.

    Attributes
    ----------
    _obj : PayloadT
        Typed Kubernetes client model returned by the cluster API.
    _resource_api : ClassVar[_ResourceAPI[PayloadT] | None]
        Abstract backend API for this resource type, shared by all instances.  These
        are constructed and assigned to each type via one of the `cluster_resource`,
        `namespaced_resource`, or `custom_resource` class decorators, respectively.
    """

    # NOTE: unfortunately, PEP 526 prevents us from annotating
    # `_resource_api: _ResourceAPI[PayloadT]` because `ClassVar[]` cannot contain
    # unresolved type variables.  Therefore, we have to erase the payload type and
    # recover it with a runtime cast in the `_api()` helper function, which recovers
    # full type information as a workaround.
    _resource_api: ClassVar[_ResourceAPI[Any] | None] = None
    _obj: PayloadT

    @property
    def name(self) -> str:
        """Return the Kubernetes object name.

        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        return _api(type(self)).name(self._obj)

    @property
    def namespace(self) -> str:
        """Return the Kubernetes object namespace.

        Returns
        -------
        str
            Trimmed `metadata.namespace`, or an empty string when unavailable.
        """
        return _api(type(self)).namespace(self._obj)

    @property
    def labels(self) -> Mapping[str, str]:
        """Return the Kubernetes object labels.

        Returns
        -------
        Mapping[str, str]
            Live read-only view of `metadata.labels`, or an empty mapping when
            unavailable.
        """
        return _api(type(self)).labels(self._obj)

    @property
    def annotations(self) -> Mapping[str, str]:
        """Return the Kubernetes object annotations.

        Returns
        -------
        Mapping[str, str]
            Live read-only view of `metadata.annotations`, or an empty mapping when
            unavailable.
        """
        return _api(type(self)).annotations(self._obj)

    @property
    def resource_version(self) -> str:
        """Return the Kubernetes object resource version.

        Returns
        -------
        str
            Kubernetes `metadata.resourceVersion`, or an empty string when
            unavailable.
        """
        return _api(type(self)).resource_version(self._obj)

    @property
    def uid(self) -> str:
        """Return the Kubernetes object UID.

        Returns
        -------
        str
            Kubernetes `metadata.uid`, or an empty string when unavailable.
        """
        return _api(type(self)).uid(self._obj)

    @property
    def created_at(self) -> datetime | None:
        """Return the Kubernetes object creation timestamp.

        Returns
        -------
        datetime | None
            Kubernetes `metadata.creationTimestamp`, or `None` when unavailable.
        """
        return _api(type(self)).created_at(self._obj)

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        name: str,
        deadline: Deadline,
        namespace: str | None = None,
    ) -> Self | None:
        """Read one Kubernetes resource by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Kubernetes object name.
        deadline : Deadline
            Maximum request budget in seconds.
        namespace : str | None, optional
            Namespace for namespaced resources. Cluster-scoped resources reject it.

        Returns
        -------
        KubeResource | None
            Wrapped resource, or `None` when absent.

        """
        api = _api(cls)
        payload = await api.read(
            kube,
            name=name,
            namespace=namespace,
            deadline=deadline,
        )
        if payload is None:
            return None
        return cls(payload)

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        deadline: Deadline,
        namespace: str | None = None,
        namespaces: Collection[str] | None = None,
        labels: Mapping[str, str] = EMPTY_MAPPING,
        field_selector: Collection[str] = (),
    ) -> builtins.list[Self]:
        """List Kubernetes resources with optional selector filters.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum request budget in seconds.
        namespace : str | None, optional
            Single namespace filter for namespaced resources.
        namespaces : Collection[str] | None, optional
            Multiple namespace filters for namespaced resources.
        labels : Mapping[str, str], optional
            Exact-match labels to convert into a Kubernetes label selector.
        field_selector : Collection[str], optional
            Kubernetes field selector fragments to apply to the list request.

        Returns
        -------
        list[KubeResource]
            Wrapped resources matching the filters.

        """
        field_selector = ",".join(
            item.strip() for item in field_selector if item.strip()
        )
        label_selector = ",".join(f"{key}={value}" for key, value in labels.items())
        api = _api(cls)
        payloads = await api.list(
            kube,
            deadline=deadline,
            namespace=namespace,
            namespaces=namespaces,
            label_selector=label_selector,
            field_selector=field_selector,
        )
        items: builtins.list[Self] = []
        for payload in payloads:
            items.extend(cls(item) for item in api.items(payload))
        return items

    @classmethod
    async def create(
        cls,
        kube: Kube,
        *,
        intent: ManifestT,
        deadline: Deadline,
    ) -> Self:
        """Create one Kubernetes resource from desired state.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        intent : ManifestT
            Desired resource state to render and create.
        deadline : Deadline
            Maximum request budget in seconds.

        Returns
        -------
        KubeResource
            Wrapped created resource.

        Raises
        ------
        NotImplementedError
            If this resource does not configure a create method.
        """
        api = _api(cls)
        if not api.can_create:
            msg = f"{api.kind} does not implement create"
            raise NotImplementedError(msg)
        name = intent.name.strip()
        namespace = intent.namespace
        body = intent.manifest()
        payload = await api.create(
            kube,
            name=name,
            namespace=namespace,
            body=body,
            deadline=deadline,
        )
        return cls(payload)

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        intent: ManifestT,
        deadline: Deadline,
    ) -> Self:
        """Create or patch one Kubernetes resource from desired state.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        intent : ManifestT
            Desired resource state to render and apply.
        deadline : Deadline
            Maximum request budget in seconds.

        Returns
        -------
        KubeResource
            Wrapped created or patched resource.

        Raises
        ------
        NotImplementedError
            If this resource does not configure create and patch methods.
        OSError
            If identity is incomplete, Kubernetes rejects the request, or Kubernetes
            returns malformed data.
        ValueError
            If namespace is supplied for a cluster-scoped resource.
        """
        api = _api(cls)
        if not api.can_create or not api.can_patch:
            msg = f"{api.kind} does not implement upsert"
            raise NotImplementedError(msg)
        name = intent.name.strip()
        namespace = intent.namespace
        namespace = namespace.strip() if namespace is not None else ""
        if not name:
            msg = f"{api.kind} upsert requires a non-empty name"
            raise OSError(msg)
        if api.namespaced:
            if not namespace:
                msg = f"{api.kind} upsert requires a non-empty namespace"
                raise OSError(msg)
            label = f"{namespace}/{name}"
        else:
            if namespace:
                msg = f"{api.kind} is cluster-scoped; cannot upsert by namespace"
                raise ValueError(msg)
            label = name

        try:
            return await cls.create(kube, intent=intent, deadline=deadline)
        except OSError as err:
            if not isinstance(err, Kube.APIError) or err.status != 409:
                raise
            body = intent.manifest()
            payload = await api.patch(
                kube,
                name=name,
                namespace=namespace,
                body=body,
                deadline=deadline,
                context=f"failed to patch {api.kind} {label}",
            )
        return cls(payload)

    async def patch(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        labels: Mapping[str, str | None] = EMPTY_MAPPING,
        annotations: Mapping[str, str | None] = EMPTY_MAPPING,
    ) -> Self:
        """Patch this Kubernetes resource's metadata.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum request budget in seconds.
        labels : Mapping[str, str | None], optional
            Label updates. `None` values delete labels.
        annotations : Mapping[str, str | None], optional
            Annotation updates. `None` values delete annotations.

        Returns
        -------
        KubeResource
            Fresh wrapper returned by Kubernetes after the metadata patch.

        Raises
        ------
        NotImplementedError
            If this resource has no configured patch operation.
        OSError
            If the resource identity is incomplete or Kubernetes returns malformed
            data.
        ValueError
            If no metadata updates are provided or namespace is invalid for this
            resource.
        """
        api = _api(type(self))
        if not api.can_patch:
            msg = f"{api.kind} does not implement patch"
            raise NotImplementedError(msg)
        if not labels and not annotations:
            msg = f"{api.kind} metadata patch cannot be empty"
            raise ValueError(msg)

        body: dict[str, object] = {"metadata": {}}
        metadata = cast("dict[str, object]", body["metadata"])
        if labels:
            metadata["labels"] = dict(labels)
        if annotations:
            metadata["annotations"] = dict(annotations)

        name = self.name
        namespace = self.namespace
        if api.namespaced:
            if not namespace or not name:
                msg = f"cannot patch {api.kind} with missing metadata.name/namespace"
                raise OSError(msg)
            label = f"{namespace}/{name}"
        else:
            if namespace:
                msg = f"{api.kind} is cluster-scoped; cannot patch by namespace"
                raise ValueError(msg)
            if not name:
                msg = f"cannot patch {api.kind} with missing metadata.name"
                raise OSError(msg)
            label = name
        payload = await api.patch(
            kube,
            name=name,
            namespace=namespace,
            body=body,
            deadline=deadline,
            context=f"failed to patch {api.kind} metadata {label}",
        )
        return type(self)(payload)

    async def refresh(self, kube: Kube, *, deadline: Deadline) -> Self | None:
        """Re-read this resource by its Kubernetes identity.

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
        api = _api(type(self))
        name = self.name
        namespace = self.namespace
        if api.namespaced:
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
        if not name:
            msg = f"cannot refresh {type(self).__name__} with missing metadata.name"
            raise OSError(msg)
        return await type(self).get(kube, name=name, deadline=deadline)

    async def delete(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        propagation_policy: DeletionPropagationPolicy | None = None,
        grace_period_seconds: int | None = None,
    ) -> None:
        """Delete this Kubernetes resource.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum request budget in seconds.
        propagation_policy : DeletionPropagationPolicy | None, optional
            Kubernetes deletion propagation policy.
        grace_period_seconds : int | None, optional
            Optional Kubernetes deletion grace period in seconds.

        Raises
        ------
        NotImplementedError
            If this resource has no configured delete operation.
        OSError
            If the resource identity is incomplete or Kubernetes returns malformed
            data.
        ValueError
            If namespace or deletion options are invalid for this resource.
        """
        api = _api(type(self))
        if not api.can_delete:
            msg = f"{api.kind} does not implement delete"
            raise NotImplementedError(msg)
        name = self.name
        namespace = self.namespace
        if (
            propagation_policy is not None
            and propagation_policy not in _DELETION_PROPAGATION_POLICIES
        ):
            msg = (
                f"invalid {api.kind} deletion propagation policy: "
                f"{propagation_policy!r}"
            )
            raise ValueError(msg)
        if grace_period_seconds is not None and grace_period_seconds < 0:
            msg = f"{api.kind} deletion grace period cannot be negative"
            raise ValueError(msg)
        body = (
            None
            if propagation_policy is None and grace_period_seconds is None
            else kube_client.V1DeleteOptions(
                grace_period_seconds=grace_period_seconds,
                propagation_policy=propagation_policy,
            )
        )
        if api.namespaced:
            if not namespace or not name:
                msg = f"cannot delete {api.kind} with missing metadata.name/namespace"
                raise OSError(msg)
            label = f"{namespace}/{name}"
        else:
            if namespace:
                msg = f"{api.kind} is cluster-scoped; cannot delete by namespace"
                raise ValueError(msg)
            if not name:
                msg = f"cannot delete {api.kind} with missing metadata.name"
                raise OSError(msg)
            label = name
        payload = await api.delete(
            kube,
            name=name,
            namespace=namespace,
            body=body,
            deadline=deadline,
        )
        if payload is not None and not isinstance(payload, kube_client.V1Status):
            msg = f"malformed Kubernetes response while deleting {api.kind} {label}"
            raise OSError(msg)

    async def wait(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        predicate: Callable[[Self | None], bool],
        check_current: bool = False,
    ) -> Self | None:
        """Wait until this resource identity satisfies `predicate`.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum wait budget.
        predicate : Callable[[KubeResource | None], bool]
            Predicate that returns `True` for the desired lifecycle state.  `None`
            means the resource no longer exists.
        check_current : bool, optional
            Whether to check this resource once before the first refresh.

        Returns
        -------
        KubeResource | None
            Fresh object or deletion state satisfying `predicate`.

        Raises
        ------
        TimeoutError
            If the wait deadline expires.
        """
        label = self.name or "<unknown>"
        namespace = self.namespace
        if namespace:
            label = f"{namespace}/{label}"
        label = f"{type(self).__name__} {label}"
        checked_current = False

        async def reached(attempt_deadline: Deadline) -> Self | None:
            nonlocal checked_current
            if check_current and not checked_current:
                checked_current = True
                if predicate(self):
                    return self
            current = await self.refresh(kube, deadline=attempt_deadline)
            if predicate(current):
                return current
            msg = f"{label} has not reached the expected state"
            raise TimeoutError(msg)

        try:
            return await until(
                reached,
                deadline=deadline,
                delay=RESOURCE_WAIT_POLL_INTERVAL_SECONDS,
            )
        except TimeoutError as err:
            msg = f"timed out waiting for {label}"
            raise TimeoutError(msg) from err

    @classmethod
    async def watch(
        cls,
        kube: Kube,
        *,
        deadline: Deadline,
        namespace: str | None = None,
        labels: Mapping[str, str] = EMPTY_MAPPING,
        field_selector: Collection[str] = (),
    ) -> AsyncIterator[WatchEvent[Self]]:
        """Watch Kubernetes resources with optional selector filters.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum watch budget.
        namespace : str | None, optional
            Namespace to watch. Omit for cluster-scoped resources or to watch all
            namespaces for namespaced resources.
        labels : Mapping[str, str], optional
            Label filters to apply to the snapshot and stream requests.
        field_selector : Collection[str], optional
            Kubernetes field selector fragments to apply to the snapshot and stream
            requests.

        Yields
        ------
        WatchEvent[KubeResource]
            Typed resource events.

        Notes
        -----
        Kubernetes list, stream, timeout, and malformed-payload errors propagate
        from the underlying snapshot and stream helpers.
        """
        api = _api(cls)
        namespace = namespace.strip() if namespace is not None else ""
        field_selector = ",".join(
            item.strip() for item in field_selector if item.strip()
        )
        label_selector = ",".join(f"{key}={value}" for key, value in labels.items())
        list_method, api_kwargs, context = api.watch_method(kube, namespace=namespace)

        async def snapshot(
            attempt_deadline: Deadline,
        ) -> str:
            payload = await kube.run(
                lambda request_timeout: list_method(
                    **api_kwargs,
                    label_selector=label_selector or None,
                    field_selector=field_selector or None,
                    _request_timeout=request_timeout,
                ),
                deadline=attempt_deadline,
                context=context,
                missing_ok=False,
            )
            return api.list_resource_version(
                cast("_KubeListPayload[PayloadT]", payload)
            )

        async def stream(
            resource_version: str,
            attempt_deadline: Deadline,
        ) -> AsyncIterator[WatchEvent[Self]]:
            async for event in watch_stream(
                lambda **kwargs: list_method(**api_kwargs, **kwargs),
                deadline=attempt_deadline,
                context=context,
                resource_version=resource_version,
                label_selector=label_selector,
                field_selector=field_selector,
            ):
                yield WatchEvent(
                    type=event.type,
                    object=cls(cast("PayloadT", event.object)),
                    resource_version=event.resource_version,
                )

        async for event in watch_collection(
            deadline=deadline,
            snapshot=snapshot,
            stream=stream,
        ):
            yield event


def cluster_resource[ResourceT: type[KubeResource[Any, Any]]](
    *,
    api: type[Any],
    payload: type[Any],
    read: Callable[..., Any],
    list: Callable[..., Any],  # noqa: A002
    create: Callable[..., Any] | None,
    patch: Callable[..., Any] | None,
    delete: Callable[..., Any] | None,
) -> Callable[[ResourceT], ResourceT]:
    """Configure a cluster-scoped built-in Kubernetes resource wrapper.

    Parameters
    ----------
    api : type[Any]
        Kubernetes generated API class bound to the shared transport.
    payload : type[Any]
        Kubernetes generated payload model expected from this resource.
    read : Callable[..., Any]
        Unbound generated API method that reads one resource by name.
    list : Callable[..., Any]
        Unbound generated API method that lists resources.
    create : Callable[..., Any] | None
        Unbound generated API method that creates one resource, or `None` when
        inherited upsert is not supported.
    patch : Callable[..., Any] | None
        Unbound generated API method that patches one resource, or `None` when
        inherited upsert is not supported.
    delete : Callable[..., Any] | None
        Unbound generated API method that deletes one resource by name, or `None`
        when deletion is not supported.

    Returns
    -------
    Callable
        Class decorator that installs the resource configuration.
    """

    def decorate(cls: ResourceT) -> ResourceT:
        cls._resource_api = _BuiltinResourceAPI(
            kind=cls.__name__,
            namespaced=False,
            api=api,
            payload=payload,
            read_method=read,
            list_method=list,
            list_all_method=None,
            create_method=create,
            patch_method=patch,
            delete_method=delete,
        )
        return cls

    return decorate


def namespaced_resource[ResourceT: type[KubeResource[Any, Any]]](
    *,
    api: type[Any],
    payload: type[Any],
    read: Callable[..., Any],
    list: Callable[..., Any],  # noqa: A002
    list_all: Callable[..., Any],
    create: Callable[..., Any] | None,
    patch: Callable[..., Any] | None,
    delete: Callable[..., Any] | None,
) -> Callable[[ResourceT], ResourceT]:
    """Configure a namespaced built-in Kubernetes resource wrapper.

    Parameters
    ----------
    api : type[Any]
        Kubernetes generated API class bound to the shared transport.
    payload : type[Any]
        Kubernetes generated payload model expected from this resource.
    read : Callable[..., Any]
        Unbound generated API method that reads one resource by namespace and name.
    list : Callable[..., Any]
        Unbound generated API method that lists resources in one namespace.
    list_all : Callable[..., Any]
        Unbound generated API method that lists resources across all namespaces.
    create : Callable[..., Any] | None
        Unbound generated API method that creates one namespaced resource, or `None`
        when inherited upsert is not supported.
    patch : Callable[..., Any] | None
        Unbound generated API method that patches one namespaced resource, or `None`
        when inherited upsert is not supported.
    delete : Callable[..., Any] | None
        Unbound generated API method that deletes one resource, or `None` when
        deletion is not supported.

    Returns
    -------
    Callable
        Class decorator that installs the resource configuration.
    """

    def decorate(cls: ResourceT) -> ResourceT:
        cls._resource_api = _BuiltinResourceAPI(
            kind=cls.__name__,
            namespaced=True,
            api=api,
            payload=payload,
            read_method=read,
            list_method=list,
            list_all_method=list_all,
            create_method=create,
            patch_method=patch,
            delete_method=delete,
        )
        return cls

    return decorate
