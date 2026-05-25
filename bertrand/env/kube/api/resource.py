"""Shared Kubernetes resource client helpers."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Literal, Self, cast

from ._helpers import (
    _create_or_patch,
    _label_selector,
    _normalized_namespaces,
    _typed_list_items,
    _typed_payload,
    _validate_delete_status,
    _wait_until_deleted,
)
from .watch import watch as kube_watch

if TYPE_CHECKING:
    import builtins
    from collections.abc import AsyncIterator, Awaitable, Callable, Collection, Mapping

    from .client import Kube
    from .watch import WatchEvent

type ResourceScope = Literal["cluster", "namespaced"]


@dataclass(frozen=True)
class ResourceClient[PayloadT, WrapperT]:
    """Common operations for one typed Kubernetes resource.

    Parameters
    ----------
    scope : {"cluster", "namespaced"}
        Kubernetes resource scope. Cluster-scoped resources reject namespaces;
        namespaced resources require a namespace for single-object operations.
    kind : str
        Human-readable Kubernetes kind for diagnostics.
    expected : type[PayloadT]
        Kubernetes client payload type returned by single-object operations.
    list_type : type[object]
        Kubernetes client list payload type returned by list operations.
    wrapper : Callable[[PayloadT], WrapperT]
        Function that wraps a validated Kubernetes payload.
    read : Callable[[Kube, str | None, str, float | None], object]
        Function that reads a resource as ``(kube, namespace, name, timeout)``.
        Cluster-scoped endpoints ignore ``namespace``.
    list_all : Callable[[Kube, str | None, str | None, float | None], object]
        Function that lists this resource without a namespace constraint.
    list_namespace : Callable[[Kube, str, str | None, str | None, float | None],
            object] | None, optional
        Function that lists this resource in one namespace.
    create : Callable[[Kube, str | None, str, Mapping[str, object], float | None],
            object] | None, optional
        Function that creates a resource. Cluster-scoped endpoints ignore
        ``namespace``.
    patch : Callable[[Kube, str | None, str, Mapping[str, object], float | None],
            object] | None, optional
        Function that patches a resource. Cluster-scoped endpoints ignore
        ``namespace``.
    delete : Callable[[Kube, str | None, str, float | None], object] | None, optional
        Function that deletes a resource. Cluster-scoped endpoints ignore
        ``namespace``.
    watch_all : Callable[[Kube], Callable[..., object]] | None, optional
        Function returning the generated all-scope watch/list endpoint.
    watch_namespace : Callable[[Kube], Callable[..., object]] | None, optional
        Function returning the generated namespaced watch/list endpoint.
    """

    scope: ResourceScope
    kind: str
    expected: type[PayloadT]
    list_type: type[object]
    wrapper: Callable[[PayloadT], WrapperT]
    read: Callable[[Kube, str | None, str, float | None], object]
    list_all: Callable[[Kube, str | None, str | None, float | None], object]
    list_namespace: (
        Callable[[Kube, str, str | None, str | None, float | None], object] | None
    ) = None
    create: (
        Callable[[Kube, str | None, str, Mapping[str, object], float | None], object]
        | None
    ) = None
    patch: (
        Callable[[Kube, str | None, str, Mapping[str, object], float | None], object]
        | None
    ) = None
    delete: Callable[[Kube, str | None, str, float | None], object] | None = None
    watch_all: Callable[[Kube], Callable[..., object]] | None = None
    watch_namespace: Callable[[Kube], Callable[..., object]] | None = None

    async def get(
        self,
        kube: Kube,
        *,
        name: str,
        timeout: float,
        namespace: str | None = None,
    ) -> WrapperT | None:
        """Read one resource by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Resource name to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        namespace : str | None, optional
            Namespace that owns the resource. Required for namespaced resources
            and rejected for cluster-scoped resources.

        Returns
        -------
        WrapperT | None
            Wrapped Kubernetes object, or ``None`` when it does not exist.
        """
        namespace = self._single_namespace(namespace, action="read")
        label = self._label(namespace, name)
        payload = await kube.run(
            lambda request_timeout: self.read(kube, namespace, name, request_timeout),
            timeout=timeout,
            context=f"failed to read {self.kind} {label!r}",
        )
        if payload is None:
            return None
        return self.wrapper(_typed_payload(payload, self.expected, context=self.kind))

    async def list(
        self,
        kube: Kube,
        *,
        timeout: float,
        namespace: str | None = None,
        namespaces: Collection[str] | None = None,
        labels: Mapping[str, str] | None = None,
        field_selector: str | None = None,
    ) -> builtins.list[WrapperT]:
        """List resources with optional namespace and label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        namespace : str | None, optional
            Optional single namespace filter for namespaced resources.
        namespaces : Collection[str] | None, optional
            Optional namespace filters for namespaced resources. ``None`` queries
            all namespaces.
        labels : Mapping[str, str] | None, optional
            Optional exact-match label selector.
        field_selector : str | None, optional
            Raw Kubernetes field selector.

        Returns
        -------
        list[WrapperT]
            Wrapped Kubernetes resources matching the filters.
        """
        field_selector = field_selector.strip() if field_selector is not None else None
        if not field_selector:
            field_selector = None
        selected_namespaces = self._list_namespaces(
            namespace=namespace,
            namespaces=namespaces,
        )
        label_selector = _label_selector(labels)
        if self.scope == "cluster":
            payload = await kube.run(
                lambda request_timeout: self.list_all(
                    kube,
                    label_selector,
                    field_selector,
                    request_timeout,
                ),
                timeout=timeout,
                context=f"failed to list {self.kind}s",
                missing_ok=False,
            )
            return [
                self.wrapper(item)
                for item in _typed_list_items(
                    payload,
                    list_type=self.list_type,
                    item_type=self.expected,
                    list_context=self.kind,
                    item_context=self.kind,
                )
            ]
        if self.list_namespace is None:
            msg = f"{self.kind} does not define a namespaced list endpoint"
            raise NotImplementedError(msg)
        list_namespace = self.list_namespace
        normalized = _normalized_namespaces(selected_namespaces)
        payloads: list[object] = []
        if normalized is None:
            payload = await kube.run(
                lambda request_timeout: self.list_all(
                    kube,
                    label_selector,
                    field_selector,
                    request_timeout,
                ),
                timeout=timeout,
                context=f"failed to list {self.kind}s across all namespaces",
                missing_ok=False,
            )
            if payload is not None:
                payloads.append(payload)
        elif normalized:
            for selected in normalized:
                payload = await kube.run(
                    lambda request_timeout, selected=selected: list_namespace(
                        kube,
                        selected,
                        label_selector,
                        field_selector,
                        request_timeout,
                    ),
                    timeout=timeout,
                    context=f"failed to list {self.kind}s in namespace {selected!r}",
                    missing_ok=False,
                )
                if payload is not None:
                    payloads.append(payload)

        items: list[PayloadT] = []
        for payload in payloads:
            items.extend(
                _typed_list_items(
                    payload,
                    list_type=self.list_type,
                    item_type=self.expected,
                    list_context=self.kind,
                    item_context=self.kind,
                )
            )
        return [self.wrapper(item) for item in items]

    async def watch(
        self,
        kube: Kube,
        *,
        timeout: float,
        namespace: str | None = None,
        labels: Mapping[str, str] | None = None,
        field_selector: str | None = None,
        resource_version: str | None = None,
    ) -> AsyncIterator[WatchEvent[WrapperT]]:
        """Watch this resource type.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum watch budget in seconds. If infinite, wait indefinitely.
        namespace : str | None, optional
            Namespace to watch. If omitted for a namespaced resource, watches
            across all namespaces. Rejected for cluster-scoped resources.
        labels : Mapping[str, str] | None, optional
            Optional exact-match label selector.
        field_selector : str | None, optional
            Raw Kubernetes field selector.
        resource_version : str | None, optional
            Resource version to resume from.

        Yields
        ------
        WatchEvent[WrapperT]
            Typed watch events containing wrapped resources.
        """
        namespace = self._watch_namespace(namespace)
        if self.watch_all is None:
            msg = f"{self.kind} does not define a watch endpoint"
            raise NotImplementedError(msg)
        if self.scope == "cluster":
            async for event in kube_watch(
                self.watch_all(kube),
                wrapper=lambda payload: self.wrapper(
                    _typed_payload(
                        payload,
                        self.expected,
                        context=f"{self.kind} watch",
                    )
                ),
                timeout=timeout,
                context=f"failed to watch {self.kind}s",
                resource_version=resource_version,
                labels=labels,
                field_selector=field_selector,
            ):
                yield event
            return
        if self.watch_namespace is None:
            msg = f"{self.kind} does not define a namespaced watch endpoint"
            raise NotImplementedError(msg)
        api_kwargs: Mapping[str, object]
        if namespace is None:
            watch_fn = self.watch_all(kube)
            api_kwargs = {}
            context = f"failed to watch {self.kind}s across all namespaces"
        else:
            watch_fn = self.watch_namespace(kube)
            api_kwargs = {"namespace": namespace}
            context = f"failed to watch {self.kind}s in namespace {namespace!r}"
        async for event in kube_watch(
            watch_fn,
            wrapper=lambda payload: self.wrapper(
                _typed_payload(
                    payload,
                    self.expected,
                    context=f"{self.kind} watch",
                )
            ),
            timeout=timeout,
            context=context,
            resource_version=resource_version,
            labels=labels,
            field_selector=field_selector,
            api_kwargs=api_kwargs,
        ):
            yield event

    async def upsert(
        self,
        kube: Kube,
        *,
        name: str,
        manifest: Mapping[str, object],
        timeout: float,
        namespace: str | None = None,
    ) -> WrapperT:
        """Create or patch one resource from a manifest.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Resource name to create or patch.
        manifest : Mapping[str, object]
            Kubernetes manifest used for the create/patch operation.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        namespace : str | None, optional
            Namespace that owns the resource. Required for namespaced resources
            and rejected for cluster-scoped resources.

        Returns
        -------
        WrapperT
            Wrapped created or patched resource.
        """
        if self.create is None or self.patch is None:
            msg = f"{self.kind} does not define create/patch endpoints"
            raise NotImplementedError(msg)
        namespace = self._single_namespace(namespace, action="upsert")
        label = self._label(namespace, name)
        create = self.create
        patch = self.patch
        payload = await _create_or_patch(
            kube,
            timeout=timeout,
            create=lambda request_timeout: create(
                kube,
                namespace,
                name,
                manifest,
                request_timeout,
            ),
            patch=lambda request_timeout: patch(
                kube,
                namespace,
                name,
                manifest,
                request_timeout,
            ),
            create_context=f"failed to create {self.kind} {label}",
            patch_context=f"failed to patch {self.kind} {label}",
            expected=self.expected,
            payload_context=self.kind,
        )
        return self.wrapper(payload)

    async def delete_by_name(
        self,
        kube: Kube,
        *,
        name: str,
        timeout: float,
        namespace: str | None = None,
    ) -> None:
        """Delete one resource by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Resource name to delete.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        namespace : str | None, optional
            Namespace that owns the resource. Required for namespaced resources
            and rejected for cluster-scoped resources.
        """
        if self.delete is None:
            msg = f"{self.kind} does not define a delete endpoint"
            raise NotImplementedError(msg)
        namespace = self._single_namespace(namespace, action="delete")
        label = self._label(namespace, name)
        delete = self.delete
        payload = await kube.run(
            lambda request_timeout: delete(
                kube,
                namespace,
                name,
                request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete {self.kind} {label}",
        )
        _validate_delete_status(payload, label=f"{self.kind} {label}")

    async def wait_deleted(
        self,
        *,
        label: str,
        timeout: float,
        refresh: Callable[[float], Awaitable[object | None]],
    ) -> None:
        """Wait for a resource to disappear.

        Parameters
        ----------
        label : str
            Human-readable resource label for diagnostics.
        timeout : float
            Maximum wait budget in seconds.
        refresh : Callable[[float], Awaitable[object | None]]
            Callback that returns the live object or ``None``.
        """
        await _wait_until_deleted(label=label, timeout=timeout, refresh=refresh)

    def _single_namespace(self, namespace: str | None, *, action: str) -> str | None:
        namespace = namespace.strip() if namespace is not None else ""
        if self.scope == "cluster":
            if namespace:
                msg = f"{self.kind} is cluster-scoped; cannot {action} in a namespace"
                raise ValueError(msg)
            return None
        if not namespace:
            msg = f"{self.kind} {action} requires a namespace"
            raise ValueError(msg)
        return namespace

    def _watch_namespace(self, namespace: str | None) -> str | None:
        namespace = namespace.strip() if namespace is not None else ""
        if self.scope == "cluster":
            if namespace:
                msg = f"{self.kind} is cluster-scoped; cannot watch in a namespace"
                raise ValueError(msg)
            return None
        return namespace or None

    def _list_namespaces(
        self,
        *,
        namespace: str | None,
        namespaces: Collection[str] | None,
    ) -> Collection[str] | None:
        namespace = namespace.strip() if namespace is not None else ""
        if self.scope == "cluster":
            if namespace or namespaces is not None:
                msg = f"{self.kind} is cluster-scoped; cannot list by namespace"
                raise ValueError(msg)
            return None
        if namespace and namespaces is not None:
            msg = f"{self.kind} list accepts either namespace or namespaces, not both"
            raise ValueError(msg)
        return (namespace,) if namespace else namespaces

    def _label(self, namespace: str | None, name: str) -> str:
        return f"{namespace}/{name}" if namespace else name


def _resource_client[WrapperT](
    owner: type[WrapperT],
) -> ResourceClient[object, WrapperT]:
    factory = getattr(owner, "_client", None)
    if not callable(factory):
        msg = f"{owner.__name__} does not define a Kubernetes resource client"
        raise NotImplementedError(msg)
    return cast("ResourceClient[object, WrapperT]", factory())


class ClusterResourceMixin[PayloadT]:
    """Shared read/list/refresh helpers for cluster-scoped wrappers."""

    @classmethod
    async def get(cls, kube: Kube, *, name: str, timeout: float) -> Self | None:
        """Read one cluster-scoped Kubernetes resource by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Resource name to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Self | None
            Wrapped Kubernetes object, or ``None`` if it does not exist.
        """
        client = cast("ResourceClient[PayloadT, Self]", _resource_client(cls))
        return await client.get(kube, name=name, timeout=timeout)

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        timeout: float,
        labels: Mapping[str, str] | None = None,
        field_selector: str | None = None,
    ) -> builtins.list[Self]:
        """List cluster-scoped Kubernetes resources.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Optional exact-match label selector.
        field_selector : str | None, optional
            Raw Kubernetes field selector.

        Returns
        -------
        list[Self]
            Wrapped Kubernetes resources matching the filters.
        """
        client = cast("ResourceClient[PayloadT, Self]", _resource_client(cls))
        return await client.list(
            kube,
            timeout=timeout,
            labels=labels,
            field_selector=field_selector,
        )

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this resource by its metadata name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Self | None
            Fresh wrapper for the same object, or ``None`` if it no longer exists.
        """
        name = _resource_name(self, f"refresh {type(self).__name__}")
        return await type(self).get(kube, name=name, timeout=timeout)


class ClusterMutableResourceMixin[PayloadT](ClusterResourceMixin[PayloadT]):
    """Shared delete helpers for cluster-scoped wrappers."""

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this resource from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        name = _resource_name(self, f"delete {type(self).__name__}")
        client = cast("ResourceClient[PayloadT, Self]", _resource_client(type(self)))
        await client.delete_by_name(kube, name=name, timeout=timeout)

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this resource is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait budget in seconds.
        """
        name = _resource_name(self, f"wait for {type(self).__name__} deletion")
        client = cast("ResourceClient[PayloadT, Self]", _resource_client(type(self)))
        await client.wait_deleted(
            label=_resource_label(self, name=name),
            timeout=timeout,
            refresh=lambda remaining: self.refresh(kube, timeout=remaining),
        )


class ClusterWatchMixin[PayloadT]:
    """Shared watch helper for cluster-scoped wrappers."""

    @classmethod
    async def watch(
        cls,
        kube: Kube,
        *,
        timeout: float,
        labels: Mapping[str, str] | None = None,
        field_selector: str | None = None,
        resource_version: str | None = None,
    ) -> AsyncIterator[WatchEvent[Self]]:
        """Watch cluster-scoped Kubernetes resources.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum watch budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Optional exact-match label selector.
        field_selector : str | None, optional
            Raw Kubernetes field selector.
        resource_version : str | None, optional
            Resource version to watch from.

        Yields
        ------
        WatchEvent[Self]
            Typed watch events containing wrapped resources.
        """
        client = cast("ResourceClient[PayloadT, Self]", _resource_client(cls))
        async for event in client.watch(
            kube,
            timeout=timeout,
            labels=labels,
            field_selector=field_selector,
            resource_version=resource_version,
        ):
            yield event


class NamespacedResourceMixin[PayloadT]:
    """Shared read/list/refresh helpers for namespaced wrappers."""

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        timeout: float,
    ) -> Self | None:
        """Read one namespaced Kubernetes resource by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the resource.
        name : str
            Resource name to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Self | None
            Wrapped Kubernetes object, or ``None`` if it does not exist.
        """
        client = cast("ResourceClient[PayloadT, Self]", _resource_client(cls))
        return await client.get(
            kube,
            namespace=namespace,
            name=name,
            timeout=timeout,
        )

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        timeout: float,
        namespaces: Collection[str] | None = None,
        labels: Mapping[str, str] | None = None,
        field_selector: str | None = None,
    ) -> builtins.list[Self]:
        """List namespaced Kubernetes resources.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        namespaces : Collection[str] | None, optional
            Optional namespace filters. ``None`` queries all namespaces.
        labels : Mapping[str, str] | None, optional
            Optional exact-match label selector.
        field_selector : str | None, optional
            Raw Kubernetes field selector.

        Returns
        -------
        list[Self]
            Wrapped Kubernetes resources matching the filters.
        """
        client = cast("ResourceClient[PayloadT, Self]", _resource_client(cls))
        return await client.list(
            kube,
            timeout=timeout,
            namespaces=namespaces,
            labels=labels,
            field_selector=field_selector,
        )

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this resource by its metadata namespace and name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Self | None
            Fresh wrapper for the same object, or ``None`` if it no longer exists.
        """
        namespace, name = _resource_namespace_name(
            self,
            f"refresh {type(self).__name__}",
        )
        return await type(self).get(
            kube,
            namespace=namespace,
            name=name,
            timeout=timeout,
        )


class NamespacedMutableResourceMixin[PayloadT](NamespacedResourceMixin[PayloadT]):
    """Shared delete helpers for namespaced wrappers."""

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this resource from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        namespace, name = _resource_namespace_name(
            self,
            f"delete {type(self).__name__}",
        )
        client = cast("ResourceClient[PayloadT, Self]", _resource_client(type(self)))
        await client.delete_by_name(
            kube,
            namespace=namespace,
            name=name,
            timeout=timeout,
        )

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this resource is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait budget in seconds.
        """
        namespace, name = _resource_namespace_name(
            self,
            f"wait for {type(self).__name__} deletion",
        )
        client = cast("ResourceClient[PayloadT, Self]", _resource_client(type(self)))
        await client.wait_deleted(
            label=_resource_label(self, name=name, namespace=namespace),
            timeout=timeout,
            refresh=lambda remaining: self.refresh(kube, timeout=remaining),
        )


class NamespacedWatchMixin[PayloadT]:
    """Shared watch helper for namespaced wrappers."""

    @classmethod
    async def watch(
        cls,
        kube: Kube,
        *,
        timeout: float,
        namespace: str | None = None,
        labels: Mapping[str, str] | None = None,
        field_selector: str | None = None,
        resource_version: str | None = None,
    ) -> AsyncIterator[WatchEvent[Self]]:
        """Watch namespaced Kubernetes resources.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum watch budget in seconds. If infinite, wait indefinitely.
        namespace : str | None, optional
            Namespace to watch. If omitted, watches across all namespaces.
        labels : Mapping[str, str] | None, optional
            Optional exact-match label selector.
        field_selector : str | None, optional
            Raw Kubernetes field selector.
        resource_version : str | None, optional
            Resource version to watch from.

        Yields
        ------
        WatchEvent[Self]
            Typed watch events containing wrapped resources.
        """
        client = cast("ResourceClient[PayloadT, Self]", _resource_client(cls))
        async for event in client.watch(
            kube,
            timeout=timeout,
            namespace=namespace,
            labels=labels,
            field_selector=field_selector,
            resource_version=resource_version,
        ):
            yield event


def _resource_name(resource: object, action: str) -> str:
    name = str(getattr(resource, "name", "") or "").strip()
    if not name:
        msg = f"cannot {action} with missing metadata.name"
        raise OSError(msg)
    return name


def _resource_namespace_name(resource: object, action: str) -> tuple[str, str]:
    namespace = str(getattr(resource, "namespace", "") or "").strip()
    name = str(getattr(resource, "name", "") or "").strip()
    if not namespace or not name:
        msg = f"cannot {action} with missing metadata.name/namespace"
        raise OSError(msg)
    return namespace, name


def _resource_label(
    resource: object,
    *,
    name: str,
    namespace: str | None = None,
) -> str:
    label = getattr(resource, "_object_label", None)
    if callable(label):
        return str(label(name=name, namespace=namespace))
    if namespace:
        return f"{type(resource).__name__} {namespace}/{name}"
    return f"{type(resource).__name__} {name}"
