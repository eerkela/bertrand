"""Kubernetes generated-resource adapters and wrapper mixins."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Any, ClassVar, Literal, Self, cast

from bertrand.env.git import until

from ._helpers import (
    DeletionPropagationPolicy,
    _delete_options,
    _is_conflict,
    _label_selector,
    _normalized_namespaces,
    _typed_list_items,
    _typed_payload,
    _validate_delete_status,
    _wait_until_deleted,
)
from .watch import WatchEvent
from .watch import watch as kube_watch

if TYPE_CHECKING:
    import builtins
    from collections.abc import AsyncIterator, Awaitable, Callable, Collection, Mapping

    from .client import Kube

type ResourceScope = Literal["cluster", "namespaced"]
RESOURCE_WAIT_POLL_INTERVAL_SECONDS = 0.5


@dataclass(frozen=True)
class BuiltinResource[PayloadT]:
    """Raw adapter for one generated Kubernetes resource API.

    Parameters
    ----------
    scope : {"cluster", "namespaced"}
        Kubernetes resource scope.
    api : str
        Attribute on :class:`Kube` exposing the generated API family.
    kind : str
        Human-readable Kubernetes kind for diagnostics.
    slug : str
        Generated-client method slug, such as ``config_map`` or ``deployment``.
    expected : type[PayloadT]
        Kubernetes client payload type returned by single-object operations.
    list_type : type[object]
        Kubernetes client list payload type returned by list operations.
    can_create : bool, default=False
        Whether the generated API supports create for this resource.
    can_patch : bool, default=False
        Whether the generated API supports patch for this resource.
    can_delete : bool, default=False
        Whether the generated API supports delete for this resource.
    can_watch : bool, default=False
        Whether the generated API supports watch/list streaming.
    """

    scope: ResourceScope
    api: str
    kind: str
    slug: str
    expected: type[PayloadT]
    list_type: type[object]
    can_create: bool = False
    can_patch: bool = False
    can_delete: bool = False
    can_watch: bool = False

    async def get(
        self,
        kube: Kube,
        *,
        name: str,
        timeout: float,
        namespace: str | None = None,
        context: str | None = None,
    ) -> PayloadT | None:
        """Read one resource by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Resource name.
        timeout : float
            Maximum request budget in seconds.
        namespace : str | None, optional
            Namespace for namespaced resources.
        context : str | None, optional
            Error context override.

        Returns
        -------
        PayloadT | None
            Raw Kubernetes payload, or ``None`` when absent.
        """
        namespace = self._single_namespace(namespace, action="read")
        label = self._object_label(name=name, namespace=namespace)
        payload = await self._run(
            kube,
            lambda request_timeout: self._read(
                kube,
                namespace,
                name,
                request_timeout,
            ),
            timeout=timeout,
            context=context or f"failed to read {self.kind} {label!r}",
            missing_ok=True,
        )
        if payload is None:
            return None
        return _typed_payload(payload, self.expected, context=self.kind)

    async def list(
        self,
        kube: Kube,
        *,
        timeout: float,
        namespace: str | None = None,
        namespaces: Collection[str] | None = None,
        labels: Mapping[str, str] | None = None,
        field_selector: str | None = None,
    ) -> builtins.list[PayloadT]:
        """List resources with optional namespace and selector filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
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
        list[PayloadT]
            Raw Kubernetes payloads matching the filters.
        """
        selected = self._list_namespaces(namespace=namespace, namespaces=namespaces)
        payloads = await self._list_payloads(
            kube,
            timeout=timeout,
            namespaces=selected,
            label_selector=_label_selector(labels),
            field_selector=self._field_selector(field_selector),
        )
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
        return items

    async def watch(
        self,
        kube: Kube,
        *,
        timeout: float,
        namespace: str | None = None,
        labels: Mapping[str, str] | None = None,
        field_selector: str | None = None,
        resource_version: str | None = None,
    ) -> AsyncIterator[WatchEvent[PayloadT]]:
        """Watch this generated resource type.

        Yields
        ------
        WatchEvent[PayloadT]
            Typed watch events containing raw Kubernetes payloads.
        """
        namespace = self._watch_namespace(namespace)
        if not self.can_watch:
            msg = f"{self.kind} does not define a watch endpoint"
            raise NotImplementedError(msg)
        watch_fn, api_kwargs, context = self._watch_request(kube, namespace=namespace)
        async for event in kube_watch(
            watch_fn,
            wrapper=lambda payload: _typed_payload(
                payload,
                self.expected,
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

    async def create(
        self,
        kube: Kube,
        *,
        manifest: Mapping[str, object],
        timeout: float,
        namespace: str | None = None,
        name: str | None = None,
        context: str | None = None,
        malformed_message: str | None = None,
        missing_ok: bool = True,
    ) -> PayloadT:
        """Create one resource from a complete Kubernetes manifest.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        manifest : Mapping[str, object]
            Complete Kubernetes manifest.
        timeout : float
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
            Whether HTTP 404 should be converted to ``None`` before payload
            validation.

        Returns
        -------
        PayloadT
            Raw created Kubernetes payload.
        """
        if not self.can_create:
            msg = f"{self.kind} does not define a create endpoint"
            raise NotImplementedError(msg)
        namespace = self._single_namespace(namespace, action="create")
        label = self._object_label(name=name or self.kind, namespace=namespace)
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
        return self._typed(payload, malformed_message=malformed_message)

    async def upsert(
        self,
        kube: Kube,
        *,
        name: str,
        manifest: Mapping[str, object],
        timeout: float,
        namespace: str | None = None,
    ) -> PayloadT:
        """Create or patch one resource from a complete Kubernetes manifest.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Resource name.
        manifest : Mapping[str, object]
            Complete Kubernetes manifest.
        timeout : float
            Maximum request budget in seconds.
        namespace : str | None, optional
            Namespace for namespaced resources.

        Returns
        -------
        PayloadT
            Raw created or patched Kubernetes payload.

        Raises
        ------
        OSError
            If Kubernetes create or patch fails.
        """
        if not self.can_create or not self.can_patch:
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
        return self._typed(payload)

    async def delete_by_name(
        self,
        kube: Kube,
        *,
        name: str,
        timeout: float,
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
        timeout : float
            Maximum request budget in seconds.
        namespace : str | None, optional
            Namespace for namespaced resources.
        propagation_policy : {"Background", "Foreground", "Orphan"} | None, optional
            Optional Kubernetes deletion propagation policy.
        grace_period_seconds : int | None, optional
            Optional Kubernetes deletion grace period.
        """
        if not self.can_delete:
            msg = f"{self.kind} does not define a delete endpoint"
            raise NotImplementedError(msg)
        namespace = self._single_namespace(namespace, action="delete")
        label = self._object_label(name=name, namespace=namespace)
        delete_options = None
        if propagation_policy is not None or grace_period_seconds is not None:
            delete_options = _delete_options(
                kind=self.kind,
                propagation_policy=propagation_policy,
                grace_period_seconds=grace_period_seconds,
            )
        payload = await self._run(
            kube,
            lambda request_timeout: self._delete(
                kube,
                namespace,
                name,
                delete_options,
                request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete {self.kind} {label}",
            missing_ok=True,
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
            Human-readable resource label.
        timeout : float
            Maximum wait budget in seconds.
        refresh : Callable[[float], Awaitable[object | None]]
            Callback that returns the live object, or ``None`` when absent.
        """
        await _wait_until_deleted(label=label, timeout=timeout, refresh=refresh)

    async def _run(
        self,
        kube: Kube,
        fn: Callable[[float | None], object],
        *,
        timeout: float,
        context: str,
        missing_ok: bool,
    ) -> object | None:
        return await kube.run(
            fn,
            timeout=timeout,
            context=context,
            missing_ok=missing_ok,
        )

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
            context=f"failed to list {self.kind}s in namespace {namespace!r}",
            missing_ok=False,
        )

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
        delete_options: object | None,
        request_timeout: float | None,
    ) -> object:
        kwargs: dict[str, object] = {
            "name": name,
            "_request_timeout": request_timeout,
        }
        if delete_options is not None:
            kwargs["body"] = delete_options
        if self.scope == "namespaced":
            kwargs["namespace"] = self._required_namespace(namespace, action="delete")
        return self._method(kube, self._single_method("delete"))(**kwargs)

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

    def _list_all_context(self, *, all_namespaces: bool) -> str:
        if all_namespaces:
            return f"failed to list {self.kind}s across all namespaces"
        return f"failed to list {self.kind}s"

    def _object_label(self, *, name: str, namespace: str | None) -> str:
        return f"{namespace}/{name}" if namespace else name

    def _typed(
        self,
        payload: object,
        *,
        malformed_message: str | None = None,
    ) -> PayloadT:
        if not isinstance(payload, self.expected):
            if malformed_message is not None:
                raise OSError(malformed_message)
            msg = f"malformed Kubernetes {self.kind} payload"
            raise OSError(msg)
        return payload

    @staticmethod
    def _field_selector(field_selector: str | None) -> str | None:
        field_selector = field_selector.strip() if field_selector is not None else None
        return field_selector or None

    @staticmethod
    def _required_namespace(namespace: str | None, *, action: str) -> str:
        if namespace is None:
            msg = f"cannot {action} namespaced resource without namespace"
            raise RuntimeError(msg)
        return namespace


class BuiltinResourceObject[PayloadT]:
    """Base methods for wrappers backed by a :class:`BuiltinResource`.

    Attributes
    ----------
    resource : BuiltinResource[PayloadT]
        Raw descriptor for the generated Kubernetes API backing the wrapper.
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

        Returns
        -------
        Self | None
            Wrapped Kubernetes object, or ``None`` when absent.
        """
        payload = await cls.resource.get(
            kube,
            name=name,
            namespace=namespace,
            timeout=timeout,
        )
        wrapper = cast("Callable[..., Self]", cls)
        return None if payload is None else wrapper(_obj=payload)

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

        Returns
        -------
        list[Self]
            Wrapped Kubernetes resources matching the filters.
        """
        payloads = await cls.resource.list(
            kube,
            timeout=timeout,
            namespace=namespace,
            namespaces=namespaces,
            labels=labels,
            field_selector=field_selector,
        )
        wrapper = cast("Callable[..., Self]", cls)
        return [wrapper(_obj=payload) for payload in payloads]

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

        Yields
        ------
        WatchEvent[Self]
            Typed watch events containing wrapped resources.
        """
        async for event in cls.resource.watch(
            kube,
            timeout=timeout,
            namespace=namespace,
            labels=labels,
            field_selector=field_selector,
            resource_version=resource_version,
        ):
            yield WatchEvent(
                type=event.type,
                object=cast("Callable[..., Self]", cls)(_obj=event.object),
                resource_version=event.resource_version,
                raw_type=event.raw_type,
            )

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this resource by its metadata identity.

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

    async def delete(
        self,
        kube: Kube,
        *,
        timeout: float,
        propagation_policy: DeletionPropagationPolicy | None = "Background",
        grace_period_seconds: int | None = None,
    ) -> None:
        """Delete this resource from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds.
        propagation_policy : {"Background", "Foreground", "Orphan"} | None, optional
            Optional Kubernetes deletion propagation policy. Defaults to
            ``"Background"``.
        grace_period_seconds : int | None, optional
            Optional Kubernetes deletion grace period.
        """
        resource = type(self).resource
        if resource.scope == "cluster":
            name = _resource_name(self, f"delete {type(self).__name__}")
            await resource.delete_by_name(
                kube,
                name=name,
                timeout=timeout,
                propagation_policy=propagation_policy,
                grace_period_seconds=grace_period_seconds,
            )
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
            propagation_policy=propagation_policy,
            grace_period_seconds=grace_period_seconds,
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

    async def _wait_until(
        self,
        kube: Kube,
        *,
        timeout: float,
        predicate: Callable[[Self], bool],
        action: str,
        pending_message: str,
        missing_message: str,
        timeout_message: str,
        check_current: bool = False,
    ) -> Self:
        current: Self = self

        async def ready(remaining: float) -> Self:
            nonlocal current
            if check_current and predicate(current):
                return current
            refreshed = await current.refresh(kube, timeout=remaining)
            if refreshed is None:
                raise OSError(missing_message)
            current = refreshed
            if predicate(current):
                return current
            raise TimeoutError(pending_message)

        try:
            return await until(
                ready,
                timeout=timeout,
                interval=RESOURCE_WAIT_POLL_INTERVAL_SECONDS,
                action=action,
            )
        except TimeoutError as err:
            raise TimeoutError(timeout_message) from err


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
