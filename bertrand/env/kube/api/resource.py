"""Kubernetes generated-resource adapters and wrapper mixins."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
from typing import TYPE_CHECKING, Any, ClassVar, Literal, Self, cast

import kubernetes

from bertrand.env.git import Deadline, until

from .client import Kube
from .watch import WatchEvent
from .watch import watch as kube_watch

if TYPE_CHECKING:
    import builtins
    from collections.abc import AsyncIterator, Awaitable, Callable, Collection, Mapping

type ResourceScope = Literal["cluster", "namespaced"]
type DeletionPropagationPolicy = Literal["Background", "Foreground", "Orphan"]
type _BuiltinOperation = Literal["read", "list", "create", "patch", "delete"]
RESOURCE_WAIT_POLL_INTERVAL_SECONDS = 0.5


# TODO: I really want the resource model to be clearer and easier to understand, rather
# than committing to class variables that increase indirection in a non-intuitive,
# opaque way.  Mixins are good for centralizing boilerplate, but I hate the callback
# model and resource configuration as a class variable.  The only clean approaches I
# think we should entertain are inheritance and/or class decorators, if those would
# prove to be useful.


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


def _validate_delete_status(payload: object, *, label: str) -> None:
    if payload is not None and not isinstance(payload, kubernetes.client.V1Status):
        msg = f"malformed Kubernetes response while deleting {label}"
        raise OSError(msg)


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
        Generated-client method slug, such as `config_map` or `deployment`.
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

    async def get(
        self,
        kube: Kube,
        *,
        name: str,
        deadline: Deadline,
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
        deadline : Deadline
            Maximum request budget in seconds.
        namespace : str | None, optional
            Namespace for namespaced resources.
        context : str | None, optional
            Error context override.

        Returns
        -------
        PayloadT | None
            Raw Kubernetes payload, or `None` when absent.
        """
        namespace = self._single_namespace(namespace, action="read")
        label = self._object_label(name=name, namespace=namespace)
        payload = await self._run_request(
            kube,
            operation="read",
            namespace=namespace,
            name=name,
            deadline=deadline,
            context=context or f"failed to read {self.kind} {label!r}",
            missing_ok=True,
        )
        if payload is None:
            return None
        return self._typed(payload)

    async def list(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
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
        list[PayloadT]
            Raw Kubernetes payloads matching the filters.
        """
        selected = self._list_namespaces(namespace=namespace, namespaces=namespaces)
        payloads = await self._list_payloads(
            kube,
            deadline=deadline,
            namespaces=selected,
            label_selector=_label_selector(labels),
            field_selector=self._field_selector(field_selector),
        )
        items: builtins.list[PayloadT] = []
        for payload in payloads:
            items.extend(self._list_items(payload))
        return items

    async def watch(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
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
            wrapper=lambda payload: self._typed(payload, context=f"{self.kind} watch"),
            deadline=deadline,
            context=context,
            resource_version=resource_version,
            label_selector=_label_selector(labels),
            field_selector=field_selector,
            api_kwargs=api_kwargs,
        ):
            yield event

    async def create(
        self,
        kube: Kube,
        *,
        manifest: Mapping[str, object],
        deadline: Deadline,
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
        PayloadT
            Raw created Kubernetes payload.
        """
        if not self.can_create:
            msg = f"{self.kind} does not define a create endpoint"
            raise NotImplementedError(msg)
        namespace = self._single_namespace(namespace, action="create")
        label = self._object_label(name=name or self.kind, namespace=namespace)
        payload = await self._run_request(
            kube,
            operation="create",
            namespace=namespace,
            body=manifest,
            deadline=deadline,
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
        deadline: Deadline,
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
        deadline : Deadline
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
            payload = await self._run_request(
                kube,
                operation="create",
                namespace=namespace,
                body=manifest,
                deadline=deadline,
                context=f"failed to create {self.kind} {label}",
                missing_ok=False,
            )
        except OSError as err:
            if not isinstance(err, Kube.APIError) or err.status != 409:
                raise
            payload = await self._run_request(
                kube,
                operation="patch",
                namespace=namespace,
                name=name,
                body=manifest,
                deadline=deadline,
                context=f"failed to patch {self.kind} {label}",
                missing_ok=False,
            )
        return self._typed(payload)

    async def delete_by_name(
        self,
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
        payload = await self._run_request(
            kube,
            operation="delete",
            namespace=namespace,
            name=name,
            body=delete_options,
            deadline=deadline,
            context=f"failed to delete {self.kind} {label}",
            missing_ok=True,
        )
        _validate_delete_status(payload, label=f"{self.kind} {label}")

    async def wait_deleted(
        self,
        *,
        label: str,
        deadline: Deadline,
        refresh: Callable[[Deadline], Awaitable[object | None]],
    ) -> None:
        """Wait for a resource to disappear.

        Parameters
        ----------
        label : str
            Human-readable resource label.
        deadline : Deadline
            Deadline for the resource to be deleted.
        refresh : Callable[[Deadline], Awaitable[object | None]]
            Callback that returns the live object, or `None` when absent.

        Raises
        ------
        TimeoutError
            If the resource still exists when `deadline` expires.
        """

        async def deleted(attempt_deadline: Deadline) -> None:
            if await refresh(attempt_deadline) is None:
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

    async def _run_request(
        self,
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
            lambda request_timeout: self._request(
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

    async def _list_payloads(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        namespaces: Collection[str] | None,
        label_selector: str | None,
        field_selector: str | None,
    ) -> builtins.list[object | None]:
        if self.scope == "cluster":
            return [
                await self._run_request(
                    kube,
                    operation="list",
                    namespace=None,
                    deadline=deadline,
                    label_selector=label_selector,
                    field_selector=field_selector,
                    context=self._list_all_context(all_namespaces=False),
                    missing_ok=False,
                )
            ]
        normalized = _normalized_namespaces(namespaces)
        if normalized is None:
            return [
                await self._run_request(
                    kube,
                    operation="list",
                    namespace=None,
                    deadline=deadline,
                    label_selector=label_selector,
                    field_selector=field_selector,
                    context=self._list_all_context(all_namespaces=True),
                    missing_ok=False,
                )
            ]
        return list(
            await asyncio.gather(
                *(
                    self._run_request(
                        kube,
                        operation="list",
                        namespace=namespace,
                        deadline=deadline,
                        label_selector=label_selector,
                        field_selector=field_selector,
                        context=(
                            f"failed to list {self.kind}s in namespace {namespace!r}"
                        ),
                        missing_ok=False,
                    )
                    for namespace in normalized
                )
            )
        )

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

    def _request(
        self,
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
        if self.scope == "namespaced":
            if operation == "list":
                if namespace is not None:
                    kwargs["namespace"] = namespace
            else:
                kwargs["namespace"] = self._required_namespace(
                    namespace,
                    action=operation,
                )
        return self._method(kube, self._method_name(operation, namespace=namespace))(
            **kwargs
        )

    def _watch_request(
        self,
        kube: Kube,
        *,
        namespace: str | None,
    ) -> tuple[Callable[..., object], Mapping[str, object], str]:
        if self.scope == "cluster":
            return (
                self._method(kube, self._method_name("list", namespace=None)),
                {},
                f"failed to watch {self.kind}s",
            )
        if namespace is None:
            return (
                self._method(kube, self._method_name("list", namespace=None)),
                {},
                f"failed to watch {self.kind}s across all namespaces",
            )
        return (
            self._method(kube, self._method_name("list", namespace=namespace)),
            {"namespace": namespace},
            f"failed to watch {self.kind}s in namespace {namespace!r}",
        )

    def _method(self, kube: Kube, name: str) -> Callable[..., object]:
        api = getattr(kube, self.api)
        return cast("Callable[..., object]", getattr(api, name))

    def _method_name(
        self,
        operation: _BuiltinOperation,
        *,
        namespace: str | None,
    ) -> str:
        if operation == "list":
            if self.scope == "namespaced":
                if namespace is None:
                    return f"list_{self.slug}_for_all_namespaces"
                return f"list_namespaced_{self.slug}"
            return f"list_{self.slug}"
        if self.scope == "namespaced":
            return f"{operation}_namespaced_{self.slug}"
        return f"{operation}_{self.slug}"

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
        context: str | None = None,
    ) -> PayloadT:
        if not isinstance(payload, self.expected):
            if malformed_message is not None:
                raise OSError(malformed_message)
            msg = f"malformed Kubernetes {context or self.kind} payload"
            raise OSError(msg)
        return payload

    def _list_items(self, payload: object | None) -> builtins.list[PayloadT]:
        if payload is None:
            return []
        if not isinstance(payload, self.list_type):
            msg = f"malformed Kubernetes {self.kind} list payload"
            raise OSError(msg)

        out: builtins.list[PayloadT] = []
        for item in getattr(payload, "items", None) or []:
            if not isinstance(item, self.expected):
                msg = f"malformed Kubernetes {self.kind} entry in list payload"
                raise OSError(msg)
            out.append(item)
        return out

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
        deadline: Deadline,
        namespace: str | None = None,
    ) -> Self | None:
        """Read one Kubernetes resource by name.

        Returns
        -------
        Self | None
            Wrapped Kubernetes object, or `None` when absent.
        """
        payload = await cls.resource.get(
            kube,
            name=name,
            namespace=namespace,
            deadline=deadline,
        )
        wrapper = cast("Callable[..., Self]", cls)
        return None if payload is None else wrapper(_obj=payload)

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
        """List Kubernetes resources.

        Returns
        -------
        list[Self]
            Wrapped Kubernetes resources matching the filters.
        """
        payloads = await cls.resource.list(
            kube,
            deadline=deadline,
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
        deadline: Deadline,
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
            deadline=deadline,
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

    async def refresh(self, kube: Kube, *, deadline: Deadline) -> Self | None:
        """Re-read this resource by its metadata identity.

        Returns
        -------
        Self | None
            Fresh wrapper for the same object, or `None` if it no longer exists.
        """
        resource = type(self).resource
        if resource.scope == "cluster":
            name = _resource_name(self, f"refresh {type(self).__name__}")
            return await type(self).get(kube, name=name, deadline=deadline)
        namespace, name = _resource_namespace_name(
            self,
            f"refresh {type(self).__name__}",
        )
        return await type(self).get(
            kube,
            namespace=namespace,
            name=name,
            deadline=deadline,
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
            Optional Kubernetes deletion propagation policy. Defaults to
            `"Background"`.
        grace_period_seconds : int | None, optional
            Optional Kubernetes deletion grace period.
        """
        resource = type(self).resource
        if resource.scope == "cluster":
            name = _resource_name(self, f"delete {type(self).__name__}")
            await resource.delete_by_name(
                kube,
                name=name,
                deadline=deadline,
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
            deadline=deadline,
            propagation_policy=propagation_policy,
            grace_period_seconds=grace_period_seconds,
        )

    async def wait_deleted(self, kube: Kube, *, deadline: Deadline) -> None:
        """Wait until this resource is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
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
            deadline=deadline,
            refresh=lambda remaining: self.refresh(kube, deadline=remaining),
        )

    async def _wait_until(
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
        current: Self = self

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
