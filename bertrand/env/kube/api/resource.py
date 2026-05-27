"""Shared Kubernetes resource descriptor helpers."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Any, ClassVar, Literal, Self, cast

from ._helpers import (
    _is_conflict,
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


class _DescriptorEngine:
    """Shared execution plumbing for Kubernetes resource descriptors."""

    scope: ResourceScope
    kind: str
    _cluster_namespace_phrase: ClassVar[str] = "in a namespace"
    _cluster_watch_phrase: ClassVar[str] = "in a namespace"

    async def _run(
        self,
        kube: Kube,
        fn: Callable[[float | None], object],
        *,
        timeout: float,
        context: str,
        missing_ok: bool = True,
    ) -> object | None:
        return await kube.run(
            fn,
            timeout=timeout,
            context=context,
            missing_ok=missing_ok,
        )

    async def _request(
        self,
        kube: Kube,
        *,
        endpoint: Callable[..., object],
        api_kwargs: Mapping[str, object],
        timeout: float,
        context: str,
        missing_ok: bool = False,
    ) -> object | None:
        return await self._run(
            kube,
            lambda request_timeout: endpoint(
                **api_kwargs,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=context,
            missing_ok=missing_ok,
        )

    async def wait_deleted(
        self,
        *,
        label: str,
        timeout: float,
        refresh: Callable[[float], Awaitable[object | None]],
    ) -> None:
        """Wait for a resource to disappear."""
        await _wait_until_deleted(label=label, timeout=timeout, refresh=refresh)

    async def get[WrapperT](
        self,
        kube: Kube,
        *,
        name: str,
        timeout: float,
        namespace: str | None = None,
        owner: Callable[..., WrapperT] | None = None,
        context: str | None = None,
    ) -> WrapperT | None:
        """Read one resource by name.

        Returns
        -------
        WrapperT | None
            Wrapped resource, or ``None`` when absent.

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or another API error.
        """
        namespace = self._single_namespace(namespace, action="read")
        label = self._object_label(name=name, namespace=namespace)
        try:
            payload = await self._run(
                kube,
                lambda request_timeout: self._read(
                    kube,
                    namespace,
                    name,
                    request_timeout,
                ),
                timeout=timeout,
                context=context or self._read_context(label),
                missing_ok=self._read_missing_ok(),
            )
        except OSError as err:
            if self._read_not_found(err):
                return None
            raise
        if payload is None:
            return None
        return self._wrap_payload(payload, owner=owner, context=self.kind)

    async def list[WrapperT](
        self,
        kube: Kube,
        *,
        timeout: float,
        namespace: str | None = None,
        namespaces: Collection[str] | None = None,
        labels: Mapping[str, str] | None = None,
        field_selector: str | None = None,
        owner: Callable[..., WrapperT] | None = None,
    ) -> builtins.list[WrapperT]:
        """List resources with optional namespace and label filtering.

        Returns
        -------
        list[WrapperT]
            Wrapped resources matching the selectors.
        """
        selected = self._list_namespaces(
            namespace=namespace,
            namespaces=namespaces,
        )
        payloads = await self._list_payloads(
            kube,
            timeout=timeout,
            namespaces=selected,
            label_selector=self._label_selector(self._selector(labels)),
            field_selector=self._field_selector(field_selector),
        )
        return self._wrap_list_payloads(payloads, owner=owner)

    async def watch_stream[WrapperT](
        self,
        kube: Kube,
        *,
        timeout: float,
        namespace: str | None = None,
        labels: Mapping[str, str] | None = None,
        field_selector: str | None = None,
        resource_version: str | None = None,
        owner: Callable[..., WrapperT] | None = None,
    ) -> AsyncIterator[WatchEvent[WrapperT]]:
        """Watch this resource type.

        Yields
        ------
        WatchEvent[WrapperT]
            Typed watch events containing wrapped resources.
        """
        namespace = self._watch_namespace(namespace)
        if not self._supports_watch():
            msg = f"{self.kind} does not define a watch endpoint"
            raise NotImplementedError(msg)
        watch_fn, api_kwargs, context = self._watch_request(
            kube,
            namespace=namespace,
        )
        async for event in kube_watch(
            watch_fn,
            wrapper=lambda payload: self._wrap_payload(
                payload,
                owner=owner,
                context=f"{self.kind} watch",
            ),
            timeout=timeout,
            context=context,
            resource_version=resource_version,
            labels=self._selector(labels),
            field_selector=field_selector,
            api_kwargs=api_kwargs,
        ):
            yield event

    async def create_manifest[WrapperT](
        self,
        kube: Kube,
        *,
        manifest: Mapping[str, object],
        timeout: float,
        namespace: str | None = None,
        owner: Callable[..., WrapperT] | None = None,
        name: str | None = None,
        context: str | None = None,
        missing_ok: bool = True,
        malformed_message: str | None = None,
    ) -> WrapperT:
        """Create one resource from a complete Kubernetes manifest.

        Returns
        -------
        WrapperT
            Wrapped created resource.
        """
        if not self._supports_create():
            msg = f"{self.kind} does not define a create endpoint"
            raise NotImplementedError(msg)
        namespace = self._single_namespace(namespace, action="create")
        label = self._manifest_label(manifest, namespace=namespace, name=name)
        payload = await self._run(
            kube,
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
        return self._wrap_payload(
            payload,
            owner=owner,
            context=self.kind,
            malformed_message=malformed_message,
        )

    async def _upsert_manifest[WrapperT](
        self,
        kube: Kube,
        *,
        name: str,
        manifest: Mapping[str, object],
        timeout: float,
        namespace: str | None = None,
        owner: Callable[..., WrapperT] | None = None,
    ) -> WrapperT:
        """Create or patch one resource from a complete Kubernetes manifest.

        Returns
        -------
        WrapperT
            Wrapped created or patched resource.

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or another API error.
        """
        if not self._supports_create() or not self._supports_patch():
            msg = f"{self.kind} does not define create/patch endpoints"
            raise NotImplementedError(msg)
        namespace = self._single_namespace(namespace, action="upsert")
        label = self._object_label(name=name, namespace=namespace)
        try:
            payload = await self._run(
                kube,
                lambda request_timeout: self._create(
                    kube,
                    namespace,
                    manifest,
                    request_timeout,
                ),
                timeout=timeout,
                context=f"failed to create {self.kind} {label}",
                missing_ok=False,
            )
        except OSError as err:
            if not _is_conflict(err):
                raise
            payload = await self._run(
                kube,
                lambda request_timeout: self._patch(
                    kube,
                    namespace,
                    name,
                    manifest,
                    request_timeout,
                ),
                timeout=timeout,
                context=f"failed to patch {self.kind} {label}",
                missing_ok=False,
            )
        return self._wrap_payload(payload, owner=owner, context=self.kind)

    async def delete_by_name(
        self,
        kube: Kube,
        *,
        name: str,
        timeout: float,
        namespace: str | None = None,
    ) -> None:
        """Delete one resource by name."""
        if not self._supports_delete():
            msg = f"{self.kind} does not define a delete endpoint"
            raise NotImplementedError(msg)
        namespace = self._single_namespace(namespace, action="delete")
        label = self._object_label(name=name, namespace=namespace)
        payload = await self._run(
            kube,
            lambda request_timeout: self._delete(
                kube,
                namespace,
                name,
                request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete {self.kind} {label}",
            missing_ok=self._delete_missing_ok(),
        )
        self._validate_delete_payload(payload, label=label)

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
                    context=self._list_all_context(all_namespaces=False),
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
                    context=self._list_all_context(all_namespaces=True),
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
        return await self._run(
            kube,
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
        return await self._run(
            kube,
            lambda request_timeout: self._list_namespace(
                kube,
                namespace,
                label_selector,
                field_selector,
                request_timeout,
            ),
            timeout=timeout,
            context=self._list_namespace_context(namespace),
            missing_ok=False,
        )

    def _single_namespace(self, namespace: str | None, *, action: str) -> str | None:
        namespace = self._default_namespace(namespace)
        namespace = namespace.strip() if namespace is not None else ""
        if self.scope == "cluster":
            if namespace:
                msg = (
                    f"{self.kind} is cluster-scoped; cannot {action} "
                    f"{self._cluster_namespace_phrase}"
                )
                raise ValueError(msg)
            return None
        if not namespace:
            msg = f"{self.kind} {action} requires a namespace"
            raise ValueError(msg)
        return namespace

    def _watch_namespace(self, namespace: str | None) -> str | None:
        namespace = self._default_namespace(namespace)
        namespace = namespace.strip() if namespace is not None else ""
        if self.scope == "cluster":
            if namespace:
                msg = (
                    f"{self.kind} is cluster-scoped; cannot watch "
                    f"{self._cluster_watch_phrase}"
                )
                raise ValueError(msg)
            return None
        return namespace or None

    def _list_namespaces(
        self,
        *,
        namespace: str | None,
        namespaces: Collection[str] | None,
    ) -> Collection[str] | None:
        namespace = self._default_namespace(namespace)
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

    def _selector(self, labels: Mapping[str, str] | None) -> Mapping[str, str] | None:
        defaults = getattr(self, "labels", {})
        if not defaults:
            return labels
        merged = dict(defaults)
        merged.update(labels or {})
        return merged

    def _label_selector(self, labels: Mapping[str, str] | None) -> str | None:
        return _label_selector(labels)

    def _object_label(self, *, name: str, namespace: str | None) -> str:
        return f"{namespace}/{name}" if namespace else name

    @staticmethod
    def _field_selector(field_selector: str | None) -> str | None:
        field_selector = field_selector.strip() if field_selector is not None else None
        return field_selector or None

    def _default_namespace(self, namespace: str | None) -> str | None:
        return namespace

    def _read_context(self, label: str) -> str:
        return f"failed to read {self.kind} {label!r}"

    def _read_missing_ok(self) -> bool:
        return True

    def _read_not_found(self, err: OSError) -> bool:
        del err
        return False

    def _list_all_context(self, *, all_namespaces: bool) -> str:
        if all_namespaces:
            return f"failed to list {self.kind}s across all namespaces"
        return f"failed to list {self.kind}s"

    def _list_namespace_context(self, namespace: str) -> str:
        return f"failed to list {self.kind}s in namespace {namespace!r}"

    def _manifest_label(
        self,
        manifest: Mapping[str, object],
        *,
        namespace: str | None,
        name: str | None,
    ) -> str:
        del manifest
        return self._object_label(name=name or self.kind, namespace=namespace)

    def _delete_missing_ok(self) -> bool:
        return True

    def _validate_delete_payload(self, payload: object | None, *, label: str) -> None:
        _validate_delete_status(payload, label=f"{self.kind} {label}")

    def _supports_create(self) -> bool:
        return bool(getattr(self, "create", True))

    def _supports_patch(self) -> bool:
        return bool(getattr(self, "patch", True))

    def _supports_delete(self) -> bool:
        return bool(getattr(self, "delete", True))

    def _supports_watch(self) -> bool:
        return bool(getattr(self, "watch", True))

    def _read(
        self,
        kube: Kube,
        namespace: str | None,
        name: str,
        request_timeout: float | None,
    ) -> object:
        raise NotImplementedError

    def _list_all(
        self,
        kube: Kube,
        label_selector: str | None,
        field_selector: str | None,
        request_timeout: float | None,
    ) -> object:
        raise NotImplementedError

    def _list_namespace(
        self,
        kube: Kube,
        namespace: str,
        label_selector: str | None,
        field_selector: str | None,
        request_timeout: float | None,
    ) -> object:
        raise NotImplementedError

    def _create(
        self,
        kube: Kube,
        namespace: str | None,
        manifest: Mapping[str, object],
        request_timeout: float | None,
    ) -> object:
        raise NotImplementedError

    def _patch(
        self,
        kube: Kube,
        namespace: str | None,
        name: str,
        manifest: Mapping[str, object],
        request_timeout: float | None,
    ) -> object:
        raise NotImplementedError

    def _delete(
        self,
        kube: Kube,
        namespace: str | None,
        name: str,
        request_timeout: float | None,
    ) -> object:
        raise NotImplementedError

    def _watch_request(
        self,
        kube: Kube,
        *,
        namespace: str | None,
    ) -> tuple[Callable[..., object], Mapping[str, object], str]:
        raise NotImplementedError

    def _wrap_payload[WrapperT](
        self,
        payload: object,
        *,
        owner: Callable[..., Any] | None,
        context: str,
        malformed_message: str | None = None,
    ) -> Any:
        raise NotImplementedError

    def _wrap_list_payloads(
        self,
        payloads: builtins.list[object | None],
        *,
        owner: Callable[..., Any] | None,
    ) -> builtins.list[Any]:
        raise NotImplementedError

    @staticmethod
    def _required_namespace(namespace: str | None, *, action: str) -> str:
        if namespace is None:
            msg = f"cannot {action} namespaced resource without namespace"
            raise RuntimeError(msg)
        return namespace


@dataclass(frozen=True)
class BuiltinResource[PayloadT](_DescriptorEngine):
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
        """Create or patch one generated Kubernetes resource.

        Returns
        -------
        WrapperT
            Wrapped created or patched resource.
        """
        return await self._upsert_manifest(
            kube,
            owner=owner,
            name=name,
            manifest=manifest,
            timeout=timeout,
            namespace=namespace,
        )

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
            kwargs["namespace"] = self._required_namespace(namespace, action="read")
        return self._method(kube, self._single_method("read"))(**kwargs)

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
            kwargs["namespace"] = self._required_namespace(namespace, action="create")
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
            kwargs["namespace"] = self._required_namespace(namespace, action="patch")
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
            kwargs["namespace"] = self._required_namespace(namespace, action="delete")
        return self._method(kube, self._single_method("delete"))(**kwargs)

    def _watch_all(self, kube: Kube) -> Callable[..., object]:
        return self._method(kube, self._list_all_method())

    def _watch_namespace_method(self, kube: Kube) -> Callable[..., object]:
        return self._method(kube, f"list_namespaced_{self.slug}")

    def _watch_request(
        self,
        kube: Kube,
        *,
        namespace: str | None,
    ) -> tuple[Callable[..., object], Mapping[str, object], str]:
        if self.scope == "cluster":
            return self._watch_all(kube), {}, f"failed to watch {self.kind}s"
        if namespace is None:
            return (
                self._watch_all(kube),
                {},
                f"failed to watch {self.kind}s across all namespaces",
            )
        return (
            self._watch_namespace_method(kube),
            {"namespace": namespace},
            f"failed to watch {self.kind}s in namespace {namespace!r}",
        )

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

    def _wrap_payload[WrapperT](
        self,
        payload: object,
        *,
        owner: Callable[..., WrapperT] | None,
        context: str,
        malformed_message: str | None = None,
    ) -> WrapperT:
        if owner is None:
            msg = f"{self.kind} wrapper owner is required"
            raise RuntimeError(msg)
        if malformed_message is not None and not isinstance(payload, self.expected):
            raise OSError(malformed_message)
        return owner(_obj=_typed_payload(payload, self.expected, context=context))

    def _wrap_list_payloads[WrapperT](
        self,
        payloads: builtins.list[object | None],
        *,
        owner: Callable[..., WrapperT] | None,
    ) -> builtins.list[WrapperT]:
        if owner is None:
            msg = f"{self.kind} wrapper owner is required"
            raise RuntimeError(msg)
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
