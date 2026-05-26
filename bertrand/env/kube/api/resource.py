"""Shared Kubernetes resource descriptor helpers."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Any, ClassVar, Literal, Self, cast

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
class BuiltinResource[PayloadT]:
    """Descriptor for one generated Kubernetes resource API.

    Parameters
    ----------
    scope : {"cluster", "namespaced"}
        Kubernetes resource scope.
    api : str
        Attribute on `Kube` exposing the generated API family.
    kind : str
        Human-readable Kubernetes kind for diagnostics.
    slug : str
        Generated-client method slug, such as ``config_map`` or ``deployment``.
    expected : type[PayloadT]
        Kubernetes client payload type returned by single-object operations.
    list_type : type[object]
        Kubernetes client list payload type returned by list operations.
    create : bool, default=False
        Whether the generated API supports create for this resource.
    patch : bool, default=False
        Whether the generated API supports patch for this resource.
    delete : bool, default=False
        Whether the generated API supports delete for this resource.
    watch : bool, default=False
        Whether the generated API supports watch/list streaming.
    """

    scope: ResourceScope
    api: str
    kind: str
    slug: str
    expected: type[PayloadT]
    list_type: type[object]
    create: bool = False
    patch: bool = False
    delete: bool = False
    watch: bool = False

    @classmethod
    def cluster(
        cls,
        *,
        api: str,
        kind: str,
        slug: str,
        expected: type[PayloadT],
        list_type: type[object],
        create: bool = False,
        patch: bool = False,
        delete: bool = False,
        watch: bool = False,
    ) -> BuiltinResource[PayloadT]:
        """Build a descriptor for a cluster-scoped generated resource.

        Returns
        -------
        BuiltinResource[PayloadT]
            Descriptor wired to standard generated Kubernetes endpoints.
        """
        return cls(
            scope="cluster",
            api=api,
            kind=kind,
            slug=slug,
            expected=expected,
            list_type=list_type,
            create=create,
            patch=patch,
            delete=delete,
            watch=watch,
        )

    @classmethod
    def namespaced(
        cls,
        *,
        api: str,
        kind: str,
        slug: str,
        expected: type[PayloadT],
        list_type: type[object],
        create: bool = False,
        patch: bool = False,
        delete: bool = False,
        watch: bool = False,
    ) -> BuiltinResource[PayloadT]:
        """Build a descriptor for a namespaced generated resource.

        Returns
        -------
        BuiltinResource[PayloadT]
            Descriptor wired to standard generated Kubernetes endpoints.
        """
        return cls(
            scope="namespaced",
            api=api,
            kind=kind,
            slug=slug,
            expected=expected,
            list_type=list_type,
            create=create,
            patch=patch,
            delete=delete,
            watch=watch,
        )

    async def get[WrapperT](
        self,
        kube: Kube,
        *,
        owner: Callable[..., WrapperT],
        name: str,
        timeout: float,
        namespace: str | None = None,
    ) -> WrapperT | None:
        """Read one resource by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        owner : Callable[..., WrapperT]
            Wrapper constructor accepting ``_obj``.
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
            lambda request_timeout: self._read(
                kube,
                namespace,
                name,
                request_timeout,
            ),
            timeout=timeout,
            context=f"failed to read {self.kind} {label!r}",
        )
        if payload is None:
            return None
        return self._wrap(payload, owner, context=self.kind)

    async def list[WrapperT](
        self,
        kube: Kube,
        *,
        owner: Callable[..., WrapperT],
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
        owner : Callable[..., WrapperT]
            Wrapper constructor accepting ``_obj``.
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
        selected = self._list_namespaces(
            namespace=namespace,
            namespaces=namespaces,
        )
        payloads = await self._list_payloads(
            kube,
            timeout=timeout,
            namespaces=selected,
            label_selector=_label_selector(labels),
            field_selector=self._field_selector(field_selector),
        )
        return self._wrap_list_payloads(payloads, owner)

    async def watch_stream[WrapperT](
        self,
        kube: Kube,
        *,
        owner: Callable[..., WrapperT],
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
        owner : Callable[..., WrapperT]
            Wrapper constructor accepting ``_obj``.
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
        if not self.watch:
            msg = f"{self.kind} does not define a watch endpoint"
            raise NotImplementedError(msg)
        if self.scope == "cluster":
            async for event in kube_watch(
                self._watch_all(kube),
                wrapper=lambda payload: self._wrap(
                    payload,
                    owner,
                    context=f"{self.kind} watch",
                ),
                timeout=timeout,
                context=f"failed to watch {self.kind}s",
                resource_version=resource_version,
                labels=labels,
                field_selector=field_selector,
            ):
                yield event
            return

        api_kwargs: Mapping[str, object]
        if namespace is None:
            watch_fn = self._watch_all(kube)
            api_kwargs = {}
            context = f"failed to watch {self.kind}s across all namespaces"
        else:
            watch_fn = self._watch_namespace_method(kube)
            api_kwargs = {"namespace": namespace}
            context = f"failed to watch {self.kind}s in namespace {namespace!r}"
        async for event in kube_watch(
            watch_fn,
            wrapper=lambda payload: self._wrap(
                payload,
                owner,
                context=f"{self.kind} watch",
            ),
            timeout=timeout,
            context=context,
            resource_version=resource_version,
            labels=labels,
            field_selector=field_selector,
            api_kwargs=api_kwargs,
        ):
            yield event

    async def create_manifest[WrapperT](
        self,
        kube: Kube,
        *,
        owner: Callable[..., WrapperT],
        name: str,
        manifest: Mapping[str, object],
        timeout: float,
        namespace: str | None = None,
        context: str | None = None,
        missing_ok: bool = True,
        malformed_message: str | None = None,
    ) -> WrapperT:
        """Create one resource from a manifest.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        owner : Callable[..., WrapperT]
            Wrapper constructor accepting ``_obj``.
        name : str
            Resource name to create.
        manifest : Mapping[str, object]
            Kubernetes manifest used for the create operation.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        namespace : str | None, optional
            Namespace that owns the resource. Required for namespaced resources
            and rejected for cluster-scoped resources.
        context : str | None, optional
            Exact request context for specialized diagnostics.
        missing_ok : bool, default=True
            Whether HTTP 404 should be returned as ``None`` before malformed
            payload validation.
        malformed_message : str | None, optional
            Exact malformed-payload error to preserve at specialized call sites.

        Returns
        -------
        WrapperT
            Wrapped created resource.

        Raises
        ------
        OSError
            If Kubernetes returns malformed data.
        """
        if not self.create:
            msg = f"{self.kind} does not define a create endpoint"
            raise NotImplementedError(msg)
        namespace = self._single_namespace(namespace, action="create")
        label = self._label(namespace, name)
        payload = await kube.run(
            lambda request_timeout: self._create(
                kube,
                namespace,
                manifest,
                request_timeout,
            ),
            timeout=timeout,
            context=context or f"failed to create {self.kind} {label}",
            missing_ok=missing_ok,
        )
        if not isinstance(payload, self.expected):
            msg = malformed_message or f"malformed Kubernetes {self.kind} payload"
            raise OSError(msg)
        return owner(_obj=payload)

    async def upsert[WrapperT](
        self,
        kube: Kube,
        *,
        owner: Callable[..., WrapperT],
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
        owner : Callable[..., WrapperT]
            Wrapper constructor accepting ``_obj``.
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
        if not self.create or not self.patch:
            msg = f"{self.kind} does not define create/patch endpoints"
            raise NotImplementedError(msg)
        namespace = self._single_namespace(namespace, action="upsert")
        label = self._label(namespace, name)
        payload = await _create_or_patch(
            kube,
            timeout=timeout,
            create=lambda request_timeout: self._create(
                kube,
                namespace,
                manifest,
                request_timeout,
            ),
            patch=lambda request_timeout: self._patch(
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
        return owner(_obj=payload)

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
        if not self.delete:
            msg = f"{self.kind} does not define a delete endpoint"
            raise NotImplementedError(msg)
        namespace = self._single_namespace(namespace, action="delete")
        label = self._label(namespace, name)
        payload = await kube.run(
            lambda request_timeout: self._delete(
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

    def _read(
        self,
        kube: Kube,
        namespace: str | None,
        name: str,
        request_timeout: float | None,
    ) -> object:
        kwargs: dict[str, object] = {
            "name": name,
            "_request_timeout": request_timeout,
        }
        if self.scope == "namespaced":
            kwargs["namespace"] = self._namespace(namespace, action="read")
        return self._method(kube, self._single_method("read"))(**kwargs)

    async def _list_payloads(
        self,
        kube: Kube,
        *,
        timeout: float,
        namespaces: Collection[str] | None,
        label_selector: str | None,
        field_selector: str | None,
    ) -> builtins.list[object | None]:
        if self.scope == "cluster":
            return [
                await self._list_all_payload(
                    kube,
                    timeout=timeout,
                    label_selector=label_selector,
                    field_selector=field_selector,
                    context=f"failed to list {self.kind}s",
                )
            ]
        normalized = _normalized_namespaces(namespaces)
        if normalized is None:
            return [
                await self._list_all_payload(
                    kube,
                    timeout=timeout,
                    label_selector=label_selector,
                    field_selector=field_selector,
                    context=f"failed to list {self.kind}s across all namespaces",
                )
            ]
        return [
            await self._list_namespace_payload(
                kube,
                namespace=namespace,
                timeout=timeout,
                label_selector=label_selector,
                field_selector=field_selector,
            )
            for namespace in normalized
        ]

    async def _list_all_payload(
        self,
        kube: Kube,
        *,
        timeout: float,
        label_selector: str | None,
        field_selector: str | None,
        context: str,
    ) -> object | None:
        return await kube.run(
            lambda request_timeout: self._list_all(
                kube,
                label_selector,
                field_selector,
                request_timeout,
            ),
            timeout=timeout,
            context=context,
            missing_ok=False,
        )

    async def _list_namespace_payload(
        self,
        kube: Kube,
        *,
        namespace: str,
        timeout: float,
        label_selector: str | None,
        field_selector: str | None,
    ) -> object | None:
        return await kube.run(
            lambda request_timeout: self._list_namespace(
                kube,
                namespace,
                label_selector,
                field_selector,
                request_timeout,
            ),
            timeout=timeout,
            context=f"failed to list {self.kind}s in namespace {namespace!r}",
            missing_ok=False,
        )

    def _list_all(
        self,
        kube: Kube,
        label_selector: str | None,
        field_selector: str | None,
        request_timeout: float | None,
    ) -> object:
        return self._method(kube, self._list_all_method())(
            label_selector=label_selector,
            field_selector=field_selector,
            _request_timeout=request_timeout,
        )

    def _list_namespace(
        self,
        kube: Kube,
        namespace: str,
        label_selector: str | None,
        field_selector: str | None,
        request_timeout: float | None,
    ) -> object:
        return self._method(kube, f"list_namespaced_{self.slug}")(
            namespace=namespace,
            label_selector=label_selector,
            field_selector=field_selector,
            _request_timeout=request_timeout,
        )

    def _create(
        self,
        kube: Kube,
        namespace: str | None,
        manifest: Mapping[str, object],
        request_timeout: float | None,
    ) -> object:
        kwargs: dict[str, object] = {
            "body": manifest,
            "_request_timeout": request_timeout,
        }
        if self.scope == "namespaced":
            kwargs["namespace"] = self._namespace(namespace, action="create")
        return self._method(kube, self._single_method("create"))(**kwargs)

    def _patch(
        self,
        kube: Kube,
        namespace: str | None,
        name: str,
        manifest: Mapping[str, object],
        request_timeout: float | None,
    ) -> object:
        kwargs: dict[str, object] = {
            "name": name,
            "body": manifest,
            "_request_timeout": request_timeout,
        }
        if self.scope == "namespaced":
            kwargs["namespace"] = self._namespace(namespace, action="patch")
        return self._method(kube, self._single_method("patch"))(**kwargs)

    def _delete(
        self,
        kube: Kube,
        namespace: str | None,
        name: str,
        request_timeout: float | None,
    ) -> object:
        kwargs: dict[str, object] = {
            "name": name,
            "_request_timeout": request_timeout,
        }
        if self.scope == "namespaced":
            kwargs["namespace"] = self._namespace(namespace, action="delete")
        return self._method(kube, self._single_method("delete"))(**kwargs)

    def _watch_all(self, kube: Kube) -> Callable[..., object]:
        return self._method(kube, self._list_all_method())

    def _watch_namespace_method(self, kube: Kube) -> Callable[..., object]:
        return self._method(kube, f"list_namespaced_{self.slug}")

    def _method(self, kube: Kube, name: str) -> Callable[..., object]:
        api = getattr(kube, self.api)
        return cast("Callable[..., object]", getattr(api, name))

    def _single_method(self, action: str) -> str:
        if self.scope == "namespaced":
            return f"{action}_namespaced_{self.slug}"
        return f"{action}_{self.slug}"

    def _list_all_method(self) -> str:
        if self.scope == "namespaced":
            return f"list_{self.slug}_for_all_namespaces"
        return f"list_{self.slug}"

    def _wrap[WrapperT](
        self,
        payload: object,
        owner: Callable[..., WrapperT],
        *,
        context: str,
    ) -> WrapperT:
        return owner(_obj=_typed_payload(payload, self.expected, context=context))

    def _wrap_list_payloads[WrapperT](
        self,
        payloads: builtins.list[object | None],
        owner: Callable[..., WrapperT],
    ) -> builtins.list[WrapperT]:
        items: builtins.list[PayloadT] = []
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
        return [owner(_obj=item) for item in items]

    @staticmethod
    def _field_selector(field_selector: str | None) -> str | None:
        field_selector = field_selector.strip() if field_selector is not None else None
        return field_selector or None

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

    @staticmethod
    def _namespace(namespace: str | None, *, action: str) -> str:
        if namespace is None:
            msg = f"cannot {action} namespaced resource without namespace"
            raise RuntimeError(msg)
        return namespace


class BuiltinResourceObject[PayloadT]:
    """Base methods for wrappers backed by a `BuiltinResource` descriptor.

    Attributes
    ----------
    resource : BuiltinResource[PayloadT]
        Descriptor for the generated Kubernetes API backing the wrapper.
    """

    resource: ClassVar[BuiltinResource[Any]]

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        name: str,
        timeout: float,
        namespace: str | None = None,
    ) -> Self | None:
        """Read one Kubernetes resource by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Resource name to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        namespace : str | None, optional
            Namespace for namespaced resources.

        Returns
        -------
        Self | None
            Wrapped Kubernetes object, or ``None`` if it does not exist.
        """
        return await cls.resource.get(
            kube,
            owner=cls,
            name=name,
            namespace=namespace,
            timeout=timeout,
        )

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        timeout: float,
        namespace: str | None = None,
        namespaces: Collection[str] | None = None,
        labels: Mapping[str, str] | None = None,
        field_selector: str | None = None,
    ) -> builtins.list[Self]:
        """List Kubernetes resources.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
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
        list[Self]
            Wrapped Kubernetes resources matching the filters.
        """
        return await cls.resource.list(
            kube,
            owner=cls,
            timeout=timeout,
            namespace=namespace,
            namespaces=namespaces,
            labels=labels,
            field_selector=field_selector,
        )

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
        """Watch Kubernetes resources.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum watch budget in seconds. If infinite, wait indefinitely.
        namespace : str | None, optional
            Optional namespace filter.
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
        async for event in cls.resource.watch_stream(
            kube,
            owner=cls,
            timeout=timeout,
            namespace=namespace,
            labels=labels,
            field_selector=field_selector,
            resource_version=resource_version,
        ):
            yield event

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this resource by its metadata identity.

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
        resource = type(self).resource
        if resource.scope == "cluster":
            name = _resource_name(self, f"refresh {type(self).__name__}")
            return await type(self).get(kube, name=name, timeout=timeout)
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

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this resource from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        resource = type(self).resource
        if resource.scope == "cluster":
            name = _resource_name(self, f"delete {type(self).__name__}")
            await resource.delete_by_name(kube, name=name, timeout=timeout)
            return
        namespace, name = _resource_namespace_name(
            self,
            f"delete {type(self).__name__}",
        )
        await resource.delete_by_name(
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
        resource = type(self).resource
        if resource.scope == "cluster":
            name = _resource_name(
                self,
                f"wait for {type(self).__name__} deletion",
            )
            namespace = None
        else:
            namespace, name = _resource_namespace_name(
                self,
                f"wait for {type(self).__name__} deletion",
            )
        await resource.wait_deleted(
            label=_resource_label(self, name=name, namespace=namespace),
            timeout=timeout,
            refresh=lambda remaining: self.refresh(kube, timeout=remaining),
        )


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
