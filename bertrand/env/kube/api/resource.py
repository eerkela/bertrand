"""Shared Kubernetes resource client helpers."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from ._helpers import (
    _create_or_patch,
    _list_cluster_items,
    _list_namespaced_items,
    _typed_payload,
    _validate_delete_status,
    _wait_until_deleted,
)
from .watch import _watch_cluster_resource, _watch_namespaced_resource

if TYPE_CHECKING:
    import builtins
    from collections.abc import AsyncIterator, Awaitable, Callable, Collection, Mapping

    from .client import Kube
    from .watch import WatchEvent


@dataclass(frozen=True)
class NamespacedResourceClient[PayloadT, WrapperT]:
    """Common operations for one typed namespaced Kubernetes resource.

    Parameters
    ----------
    kind : str
        Human-readable Kubernetes kind for diagnostics.
    expected : type[PayloadT]
        Kubernetes client payload type returned by single-object operations.
    list_type : type[object]
        Kubernetes client list payload type returned by list operations.
    wrapper : Callable[[PayloadT], WrapperT]
        Function that wraps a validated Kubernetes payload.
    read : Callable[[Kube, str, str, float | None], object]
        Function that reads a resource as ``(kube, namespace, name, timeout)``.
    list_all : Callable[[Kube, str | None, str | None, float | None], object]
        Function that lists this resource across all namespaces.
    list_namespace : Callable[[Kube, str, str | None, str | None, float | None],
            object]
        Function that lists this resource in one namespace.
    create : Callable[[Kube, str, str, Mapping[str, object], float | None], object] |
            None, optional
        Function that creates a resource as ``(kube, namespace, name, manifest,
        timeout)``.
    patch : Callable[[Kube, str, str, Mapping[str, object], float | None], object] |
            None, optional
        Function that patches a resource as ``(kube, namespace, name, manifest,
        timeout)``.
    delete : Callable[[Kube, str, str, float | None], object] | None, optional
        Function that deletes a resource as ``(kube, namespace, name, timeout)``.
    watch_all : Callable[[Kube], Callable[..., object]] | None, optional
        Function returning the generated all-namespaces watch/list endpoint.
    watch_namespace : Callable[[Kube], Callable[..., object]] | None, optional
        Function returning the generated namespaced watch/list endpoint.
    """

    kind: str
    expected: type[PayloadT]
    list_type: type[object]
    wrapper: Callable[[PayloadT], WrapperT]
    read: Callable[[Kube, str, str, float | None], object]
    list_all: Callable[[Kube, str | None, str | None, float | None], object]
    list_namespace: Callable[
        [Kube, str, str | None, str | None, float | None],
        object,
    ]
    create: (
        Callable[[Kube, str, str, Mapping[str, object], float | None], object] | None
    ) = None
    patch: (
        Callable[[Kube, str, str, Mapping[str, object], float | None], object] | None
    ) = None
    delete: Callable[[Kube, str, str, float | None], object] | None = None
    watch_all: Callable[[Kube], Callable[..., object]] | None = None
    watch_namespace: Callable[[Kube], Callable[..., object]] | None = None

    async def get(
        self,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        timeout: float,
    ) -> WrapperT | None:
        """Read one resource by namespace and name.

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
        WrapperT | None
            Wrapped Kubernetes object, or ``None`` when it does not exist.
        """
        payload = await kube.run(
            lambda request_timeout: self.read(kube, namespace, name, request_timeout),
            timeout=timeout,
            context=(f"failed to read {self.kind} {name!r} in namespace {namespace!r}"),
        )
        if payload is None:
            return None
        return self.wrapper(_typed_payload(payload, self.expected, context=self.kind))

    async def list(
        self,
        kube: Kube,
        *,
        timeout: float,
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
        namespaces : Collection[str] | None, optional
            Optional namespace filters. ``None`` queries all namespaces.
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
        return [
            self.wrapper(item)
            for item in await _list_namespaced_items(
                kube,
                timeout=timeout,
                namespaces=namespaces,
                labels=labels,
                list_all=lambda label_selector, request_timeout: self.list_all(
                    kube,
                    label_selector,
                    field_selector,
                    request_timeout,
                ),
                list_namespace=(
                    lambda namespace, label_selector, request_timeout: (
                        self.list_namespace(
                            kube,
                            namespace,
                            label_selector,
                            field_selector,
                            request_timeout,
                        )
                    )
                ),
                list_type=self.list_type,
                item_type=self.expected,
                all_context=f"failed to list {self.kind}s across all namespaces",
                namespace_context=lambda namespace: (
                    f"failed to list {self.kind}s in namespace {namespace!r}"
                ),
                list_context=self.kind,
                item_context=self.kind,
            )
        ]

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
            Namespace to watch. If omitted, watches across all namespaces.
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
        if self.watch_all is None or self.watch_namespace is None:
            msg = f"{self.kind} does not define watch endpoints"
            raise NotImplementedError(msg)
        async for event in _watch_namespaced_resource(
            expected=self.expected,
            wrapper=self.wrapper,
            timeout=timeout,
            namespace=namespace,
            resource_version=resource_version,
            labels=labels,
            field_selector=field_selector,
            watch_all=self.watch_all(kube),
            watch_namespace=self.watch_namespace(kube),
            all_context=f"failed to watch {self.kind}s across all namespaces",
            namespace_context=lambda namespace: (
                f"failed to watch {self.kind}s in namespace {namespace!r}"
            ),
            payload_context=f"{self.kind} watch",
        ):
            yield event

    async def upsert(
        self,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        manifest: Mapping[str, object],
        timeout: float,
    ) -> WrapperT:
        """Create or patch one resource from a manifest.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the resource.
        name : str
            Resource name to create or patch.
        manifest : Mapping[str, object]
            Kubernetes manifest used for the create/patch operation.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        WrapperT
            Wrapped created or patched resource.
        """
        if self.create is None or self.patch is None:
            msg = f"{self.kind} does not define create/patch endpoints"
            raise NotImplementedError(msg)
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
            create_context=f"failed to create {self.kind} {namespace}/{name}",
            patch_context=f"failed to patch {self.kind} {namespace}/{name}",
            expected=self.expected,
            payload_context=self.kind,
        )
        return self.wrapper(payload)

    async def delete_by_name(
        self,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        timeout: float,
    ) -> None:
        """Delete one resource by namespace and name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the resource.
        name : str
            Resource name to delete.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        if self.delete is None:
            msg = f"{self.kind} does not define a delete endpoint"
            raise NotImplementedError(msg)
        delete = self.delete
        payload = await kube.run(
            lambda request_timeout: delete(
                kube,
                namespace,
                name,
                request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete {self.kind} {namespace}/{name}",
        )
        _validate_delete_status(payload, label=f"{self.kind} {namespace}/{name}")

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


@dataclass(frozen=True)
class ClusterResourceClient[PayloadT, WrapperT]:
    """Common operations for one typed cluster-scoped Kubernetes resource.

    Parameters
    ----------
    kind : str
        Human-readable Kubernetes kind for diagnostics.
    expected : type[PayloadT]
        Kubernetes client payload type returned by single-object operations.
    list_type : type[object]
        Kubernetes client list payload type returned by list operations.
    wrapper : Callable[[PayloadT], WrapperT]
        Function that wraps a validated Kubernetes payload.
    read : Callable[[Kube, str, float | None], object]
        Function that reads a resource as ``(kube, name, timeout)``.
    list_items : Callable[[Kube, str | None, str | None, float | None], object]
        Function that lists this resource with optional label and field selectors.
    create : Callable[[Kube, str, Mapping[str, object], float | None], object] |
            None, optional
        Function that creates a resource as ``(kube, name, manifest, timeout)``.
    patch : Callable[[Kube, str, Mapping[str, object], float | None], object] |
            None, optional
        Function that patches a resource as ``(kube, name, manifest, timeout)``.
    delete : Callable[[Kube, str, float | None], object] | None, optional
        Function that deletes a resource as ``(kube, name, timeout)``.
    watch_items : Callable[[Kube], Callable[..., object]] | None, optional
        Function returning the generated cluster-scoped watch/list endpoint.
    """

    kind: str
    expected: type[PayloadT]
    list_type: type[object]
    wrapper: Callable[[PayloadT], WrapperT]
    read: Callable[[Kube, str, float | None], object]
    list_items: Callable[[Kube, str | None, str | None, float | None], object]
    create: Callable[[Kube, str, Mapping[str, object], float | None], object] | None = (
        None
    )
    patch: Callable[[Kube, str, Mapping[str, object], float | None], object] | None = (
        None
    )
    delete: Callable[[Kube, str, float | None], object] | None = None
    watch_items: Callable[[Kube], Callable[..., object]] | None = None

    async def get(
        self,
        kube: Kube,
        *,
        name: str,
        timeout: float,
    ) -> WrapperT | None:
        """Read one cluster-scoped resource by name.

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
        WrapperT | None
            Wrapped Kubernetes object, or ``None`` when it does not exist.
        """
        payload = await kube.run(
            lambda request_timeout: self.read(kube, name, request_timeout),
            timeout=timeout,
            context=f"failed to read {self.kind} {name!r}",
        )
        if payload is None:
            return None
        return self.wrapper(_typed_payload(payload, self.expected, context=self.kind))

    async def list(
        self,
        kube: Kube,
        *,
        timeout: float,
        labels: Mapping[str, str] | None = None,
        field_selector: str | None = None,
    ) -> builtins.list[WrapperT]:
        """List cluster-scoped resources with optional label filtering.

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
        list[WrapperT]
            Wrapped Kubernetes resources matching the filters.
        """
        field_selector = field_selector.strip() if field_selector is not None else None
        if not field_selector:
            field_selector = None
        return [
            self.wrapper(item)
            for item in await _list_cluster_items(
                kube,
                timeout=timeout,
                labels=labels,
                list_items=lambda label_selector, request_timeout: self.list_items(
                    kube,
                    label_selector,
                    field_selector,
                    request_timeout,
                ),
                list_type=self.list_type,
                item_type=self.expected,
                context=f"failed to list {self.kind}s",
                list_context=self.kind,
                item_context=self.kind,
            )
        ]

    async def watch(
        self,
        kube: Kube,
        *,
        timeout: float,
        labels: Mapping[str, str] | None = None,
        field_selector: str | None = None,
        resource_version: str | None = None,
    ) -> AsyncIterator[WatchEvent[WrapperT]]:
        """Watch this cluster-scoped resource type.

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
            Resource version to resume from.

        Yields
        ------
        WatchEvent[WrapperT]
            Typed watch events containing wrapped resources.
        """
        if self.watch_items is None:
            msg = f"{self.kind} does not define a watch endpoint"
            raise NotImplementedError(msg)
        async for event in _watch_cluster_resource(
            expected=self.expected,
            wrapper=self.wrapper,
            timeout=timeout,
            labels=labels,
            field_selector=field_selector,
            resource_version=resource_version,
            watch_fn=self.watch_items(kube),
            context=f"failed to watch {self.kind}s",
            payload_context=f"{self.kind} watch",
        ):
            yield event

    async def upsert(
        self,
        kube: Kube,
        *,
        name: str,
        manifest: Mapping[str, object],
        timeout: float,
    ) -> WrapperT:
        """Create or patch one cluster-scoped resource from a manifest.

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

        Returns
        -------
        WrapperT
            Wrapped created or patched resource.
        """
        if self.create is None or self.patch is None:
            msg = f"{self.kind} does not define create/patch endpoints"
            raise NotImplementedError(msg)
        create = self.create
        patch = self.patch
        payload = await _create_or_patch(
            kube,
            timeout=timeout,
            create=lambda request_timeout: create(
                kube,
                name,
                manifest,
                request_timeout,
            ),
            patch=lambda request_timeout: patch(
                kube,
                name,
                manifest,
                request_timeout,
            ),
            create_context=f"failed to create {self.kind} {name}",
            patch_context=f"failed to patch {self.kind} {name}",
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
    ) -> None:
        """Delete one cluster-scoped resource by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Resource name to delete.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        if self.delete is None:
            msg = f"{self.kind} does not define a delete endpoint"
            raise NotImplementedError(msg)
        delete = self.delete
        payload = await kube.run(
            lambda request_timeout: delete(kube, name, request_timeout),
            timeout=timeout,
            context=f"failed to delete {self.kind} {name}",
        )
        _validate_delete_status(payload, label=f"{self.kind} {name}")

    async def wait_deleted(
        self,
        *,
        label: str,
        timeout: float,
        refresh: Callable[[float], Awaitable[object | None]],
    ) -> None:
        """Wait for a cluster-scoped resource to disappear.

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
