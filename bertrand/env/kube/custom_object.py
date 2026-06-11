"""Wrappers and raw adapters for Kubernetes custom objects."""

from __future__ import annotations

import asyncio
import math
from collections.abc import AsyncIterator, Awaitable, Callable, Collection, Mapping
from dataclasses import dataclass, field
from datetime import UTC, datetime
from types import MappingProxyType
from typing import TYPE_CHECKING, Any, Literal, Self, TypeVar, cast

from pydantic import BaseModel, ConfigDict, Field, ValidationError

from bertrand.env.git import Deadline, until
from bertrand.env.kube.crd import CustomResourceDefinition

from .api.client import Kube
from .api.resource import (
    RESOURCE_WAIT_POLL_INTERVAL_SECONDS,
    ResourceScope,
    WatchEvent,
    WatchExpiredError,
    _label_selector,
    _normalized_namespaces,
    _watch,
)

if TYPE_CHECKING:
    import builtins

type CustomObjectScope = ResourceScope
type _CustomObjectFragment = Mapping[str, object] | BaseModel
type _CustomOperation = Literal[
    "get",
    "list",
    "create",
    "patch",
    "patch_status",
    "delete",
]
type _CustomEndpoint = Callable[..., object]
_CUSTOM_ENDPOINTS: dict[_CustomOperation, tuple[str, str]] = {
    "get": ("get_cluster_custom_object", "get_namespaced_custom_object"),
    "list": ("list_cluster_custom_object", "list_namespaced_custom_object"),
    "create": ("create_cluster_custom_object", "create_namespaced_custom_object"),
    "patch": ("patch_cluster_custom_object", "patch_namespaced_custom_object"),
    "patch_status": (
        "patch_cluster_custom_object_status",
        "patch_namespaced_custom_object_status",
    ),
    "delete": ("delete_cluster_custom_object", "delete_namespaced_custom_object"),
}
_CustomResourceClass = TypeVar("_CustomResourceClass", bound=type[Any])


class CustomObjectMetadata(BaseModel):
    """Validated subset of Kubernetes custom-object metadata.

    Parameters
    ----------
    name : str
        Kubernetes object name.
    namespace : str
        Namespace that owns the object.
    generation : int
        Kubernetes metadata generation.
    resource_version : str
        Kubernetes resource version, parsed from `resourceVersion`.
    labels : dict[str, str]
        Kubernetes metadata labels.
    """

    model_config = ConfigDict(extra="ignore", frozen=True)
    name: str = ""
    namespace: str = ""
    generation: int = 0
    resource_version: str = Field(default="", alias="resourceVersion")
    labels: dict[str, str] = Field(default_factory=dict)


@dataclass(frozen=True, init=False)
class CustomObject:
    """Generic read-only wrapper around one Kubernetes custom-object payload.

    Parameters
    ----------
    payload : Mapping[str, Any]
        Custom-object payload returned by Kubernetes.
    """

    _payload: Mapping[str, Any] = field(repr=False)

    def __init__(self, payload: Mapping[str, Any]) -> None:
        """Initialize one custom-object wrapper."""
        object.__setattr__(self, "_payload", payload)

    @classmethod
    def from_payload(
        cls,
        payload: object,
        *,
        description: str = "custom object payload",
    ) -> Self:
        """Create a custom-object wrapper from a Kubernetes payload.

        Returns
        -------
        CustomObject
            Wrapped custom object.

        Raises
        ------
        OSError
            If the payload is malformed.
        """
        if not isinstance(payload, Mapping):
            msg = f"malformed Kubernetes {description}"
            raise OSError(msg)
        return cls(cast("Mapping[str, Any]", payload))

    @property
    def payload(self) -> Mapping[str, Any]:
        """Return a read-only top-level payload view."""
        return MappingProxyType(dict(self._payload))

    @property
    def metadata(self) -> Mapping[str, Any]:
        """Return a read-only metadata view."""
        metadata = self._payload.get("metadata", {})
        if not isinstance(metadata, Mapping):
            return MappingProxyType({})
        return MappingProxyType(cast("dict[str, Any]", dict(metadata)))

    @property
    def spec(self) -> Mapping[str, Any]:
        """Return a read-only spec view."""
        spec = self._payload.get("spec", {})
        if not isinstance(spec, Mapping):
            return MappingProxyType({})
        return MappingProxyType(cast("dict[str, Any]", dict(spec)))

    @property
    def status(self) -> Mapping[str, Any]:
        """Return a read-only status view."""
        status = self._payload.get("status", {})
        if not isinstance(status, Mapping):
            return MappingProxyType({})
        return MappingProxyType(cast("dict[str, Any]", dict(status)))

    @property
    def name(self) -> str:
        """Return this custom object's name."""
        return str(self.metadata.get("name") or "").strip()

    @property
    def namespace(self) -> str:
        """Return this custom object's namespace."""
        return str(self.metadata.get("namespace") or "").strip()

    @property
    def labels(self) -> Mapping[str, str]:
        """Return this custom object's labels."""
        labels = self.metadata.get("labels", {})
        if not isinstance(labels, Mapping):
            return MappingProxyType({})
        return MappingProxyType({str(key): str(value) for key, value in labels.items()})

    @property
    def annotations(self) -> Mapping[str, str]:
        """Return this custom object's annotations."""
        annotations = self.metadata.get("annotations", {})
        if not isinstance(annotations, Mapping):
            return MappingProxyType({})
        return MappingProxyType(
            {str(key): str(value) for key, value in annotations.items()}
        )

    @property
    def resource_version(self) -> str:
        """Return this custom object's resource version."""
        return str(self.metadata.get("resourceVersion") or "").strip()

    @property
    def uid(self) -> str:
        """Return this custom object's UID."""
        return str(self.metadata.get("uid") or "").strip()

    @property
    def created_at(self) -> str:
        """Return this custom object's creation timestamp."""
        return str(self.metadata.get("creationTimestamp") or "").strip()

    @property
    def created_at_utc(self) -> datetime | None:
        """Return this custom object's creation timestamp in UTC."""
        return _parse_kubernetes_datetime(self.created_at)

    @staticmethod
    def parse_utc_datetime(value: object) -> datetime | None:
        """Parse a Kubernetes timestamp-like value as UTC.

        Returns
        -------
        datetime | None
            Parsed UTC timestamp, or `None` when unavailable or malformed.
        """
        return _parse_kubernetes_datetime(str(value or ""))


@dataclass(frozen=True)
class _CustomObjectAPI[T_co]:
    """Raw adapter for an installed or Bertrand-owned custom-object API.

    Parameters
    ----------
    group : str
        Kubernetes API group that owns the custom object.
    version : str
        Served API version.
    kind : str
        Kubernetes kind name.
    plural : str
        Plural REST resource name.
    scope : {"cluster", "namespaced"}, default="namespaced"
        Kubernetes API scope.
    labels : Mapping[str, str], optional
        Default labels to apply to created objects and list selectors.
    payload_parser : Callable[[object], T_co] | None, optional
        Payload validator for record-oriented resources.
    payload_error_context : str | None, optional
        Context used when wrapping Pydantic validation errors as `OSError`.
    """

    group: str
    version: str
    kind: str
    plural: str
    scope: CustomObjectScope = "namespaced"
    labels: Mapping[str, str] = MappingProxyType({})
    payload_parser: Callable[[object], T_co] | None = None
    payload_error_context: str | None = None
    singular: str | None = None
    spec_model: type[BaseModel] | None = None
    spec_schema_overrides: Mapping[str, object] | None = None
    spec_schema_include_defaults: bool = False
    short_names: Collection[str] = ()
    status_model: type[BaseModel] | None = None
    status_schema_overrides: Mapping[str, object] | None = None
    status_schema_include_defaults: bool = False
    default_namespace: str | None = None

    def __post_init__(self) -> None:
        """Validate descriptor parser and schema configuration.

        Raises
        ------
        ValueError
            If parser or schema options are inconsistent.
        """
        if self.payload_error_context is not None and self.payload_parser is None:
            msg = f"{self.kind} descriptor error context requires a payload parser"
            raise ValueError(msg)
        if self.spec_schema_overrides is not None and self.spec_model is None:
            msg = f"{self.kind} descriptor spec schema overrides require a model"
            raise ValueError(msg)
        if self.status_schema_overrides is not None and self.status_model is None:
            msg = f"{self.kind} descriptor status schema overrides require a model"
            raise ValueError(msg)

    @property
    def api_version(self) -> str:
        """Return the fully qualified Kubernetes API version."""
        return f"{self.group}/{self.version}"

    async def ensure_crd(self, kube: Kube, *, deadline: Deadline) -> None:
        """Converge this descriptor's CRD and wait until it is established.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum convergence budget in seconds.

        Raises
        ------
        ValueError
            If this descriptor does not own a CRD.
        """
        spec_schema = self._spec_schema()
        if self.singular is None or spec_schema is None:
            msg = f"{self.kind} descriptor does not own a CRD"
            raise ValueError(msg)
        crd = await CustomResourceDefinition.upsert(
            kube,
            group=self.group,
            version=self.version,
            plural=self.plural,
            singular=self.singular,
            kind=self.kind,
            short_names=self.short_names,
            spec_schema=spec_schema,
            status_schema=self._status_schema(),
            labels=self.labels,
            scope="Cluster" if self.scope == "cluster" else "Namespaced",
            deadline=deadline,
        )
        await crd.wait_established(kube, deadline=deadline)

    async def get(
        self,
        kube: Kube,
        *,
        name: str,
        deadline: Deadline,
        namespace: str | None = None,
        context: str | None = None,
    ) -> T_co | None:
        """Read one custom object by name.

        Returns
        -------
        T_co | None
            Wrapped custom object, or `None` when absent.

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or another API error.
        """
        namespace = self._object_namespace(namespace, action="read")
        label = self._object_label(name=name, namespace=namespace)
        try:
            payload = await self._run_custom(
                kube,
                "get",
                namespace=namespace,
                name=name,
                deadline=deadline,
                context=context or f"failed to read {self.kind} {label}",
                missing_ok=False,
            )
        except OSError as err:
            if (
                isinstance(err, Kube.APIError)
                and err.status == 404
                and not err.missing_api_resource
            ):
                return None
            raise
        return self._wrap_payload(payload)

    async def list(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        namespace: str | None = None,
        namespaces: Collection[str] | None = None,
        labels: Mapping[str, str] | None = None,
        field_selector: str = "",
    ) -> builtins.list[T_co]:
        """List custom objects with optional namespace and label filtering.

        Returns
        -------
        list[T_co]
            Wrapped custom objects matching the filters.
        """
        selected = self._namespace_filter(
            action="list",
            namespace=namespace,
            namespaces=namespaces,
        )
        payloads = await self._list_payloads(
            kube,
            deadline=deadline,
            namespaces=selected,
            label_selector=_label_selector(self._selector(labels)),
            field_selector=field_selector.strip(),
        )
        out: builtins.list[T_co] = []
        for payload in payloads:
            out.extend(
                self._wrap_payload(item.payload) for item in self._list_items(payload)
            )
        return out

    async def watch(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        labels: Mapping[str, str] | None = None,
        namespace: str | None = None,
        resource_version: str = "",
        emit_initial: bool = False,
        field_selector: str = "",
    ) -> AsyncIterator[WatchEvent[T_co]]:
        """Watch custom objects and yield typed events.

        Yields
        ------
        WatchEvent[T_co]
            Typed custom-object watch events.

        """
        selected = self._namespace_filter(action="watch", namespace=namespace)
        normalized = _normalized_namespaces(selected)
        namespace = None if normalized is None else next(iter(normalized), None)
        labels = self._selector(labels)
        field_selector = field_selector.strip()
        current_version = resource_version.strip()
        can_emit_initial = emit_initial and not current_version
        while True:
            remaining = deadline.remaining
            if remaining <= 0:
                return
            if not current_version:
                items, current_version = await self._snapshot(
                    kube,
                    namespace=namespace,
                    labels=labels,
                    field_selector=field_selector,
                    deadline=deadline,
                )
                if can_emit_initial:
                    for event in self._initial_watch_events(
                        items,
                        resource_version=current_version,
                    ):
                        yield event
                    can_emit_initial = False

            remaining = deadline.remaining
            if remaining <= 0:
                return
            try:
                async for event in self._watch_once(
                    kube,
                    namespace=namespace,
                    labels=labels,
                    deadline=deadline,
                    resource_version=current_version,
                    field_selector=field_selector,
                ):
                    if event.resource_version:
                        current_version = event.resource_version
                    yield event
            except WatchExpiredError:
                current_version = ""
            else:
                return

    async def create(
        self,
        kube: Kube,
        *,
        name: str,
        spec: _CustomObjectFragment,
        deadline: Deadline,
        namespace: str | None = None,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> T_co:
        """Create one custom object from intent fields.

        Returns
        -------
        T_co
            Wrapped created object.
        """
        namespace = self._object_namespace(namespace, action="create")
        return await self.create_manifest(
            kube,
            manifest=self._body(
                namespace=namespace,
                name=name,
                spec=spec,
                labels=labels,
                annotations=annotations,
            ),
            deadline=deadline,
            namespace=namespace,
        )

    async def create_manifest(
        self,
        kube: Kube,
        *,
        manifest: Mapping[str, object],
        deadline: Deadline,
        namespace: str | None = None,
        name: str | None = None,
        context: str | None = None,
    ) -> T_co:
        """Create one custom object from a complete Kubernetes manifest.

        Returns
        -------
        T_co
            Wrapped created object.
        """
        namespace = self._object_namespace(namespace, action="create")
        label = self._manifest_label(manifest, namespace=namespace, name=name)
        payload = await self._run_custom(
            kube,
            "create",
            namespace=namespace,
            body=dict(manifest),
            deadline=deadline,
            context=context or f"failed to create {self.kind} {label}",
            missing_ok=True,
        )
        return self._wrap_payload(payload)

    async def upsert(
        self,
        kube: Kube,
        *,
        name: str,
        deadline: Deadline,
        namespace: str | None = None,
        spec: _CustomObjectFragment | None = None,
        manifest: Mapping[str, object] | None = None,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> T_co:
        """Create or patch one custom object from intent fields.

        Returns
        -------
        T_co
            Wrapped created or patched object.

        Raises
        ------
        OSError
            If neither `spec` nor `manifest` is provided, or the API call fails.
        """
        namespace = self._object_namespace(namespace, action="upsert")
        if manifest is None:
            if spec is None:
                msg = "custom object upsert requires spec or manifest"
                raise OSError(msg)
            manifest = self._body(
                namespace=namespace,
                name=name,
                spec=spec,
                labels=labels,
                annotations=annotations,
            )
        label = self._object_label(name=name, namespace=namespace)
        body = dict(manifest)
        try:
            payload = await self._run_custom(
                kube,
                "create",
                namespace=namespace,
                body=body,
                deadline=deadline,
                context=f"failed to create {self.kind} {label}",
                missing_ok=False,
            )
        except OSError as err:
            if not isinstance(err, Kube.APIError) or err.status != 409:
                raise
            payload = await self._run_custom(
                kube,
                "patch",
                namespace=namespace,
                name=name,
                body=body,
                deadline=deadline,
                context=f"failed to patch {self.kind} {label}",
                missing_ok=False,
            )
        return self._wrap_payload(payload)

    async def patch(
        self,
        kube: Kube,
        *,
        name: str,
        body: Mapping[str, object],
        deadline: Deadline,
        namespace: str | None = None,
        context: str | None = None,
    ) -> T_co:
        """Patch one custom object with a raw Kubernetes patch body.

        Returns
        -------
        T_co
            Wrapped patched object.
        """
        namespace = self._object_namespace(namespace, action="patch")
        label = self._object_label(name=name, namespace=namespace)
        payload = await self._run_custom(
            kube,
            "patch",
            namespace=namespace,
            name=name,
            body=dict(body),
            deadline=deadline,
            context=context or f"failed to patch {self.kind} {label}",
            missing_ok=False,
        )
        return self._wrap_payload(payload)

    async def patch_status(
        self,
        kube: Kube,
        *,
        name: str,
        status: _CustomObjectFragment,
        deadline: Deadline,
        namespace: str | None = None,
    ) -> T_co:
        """Patch one custom-object status payload.

        Returns
        -------
        T_co
            Wrapped object returned by Kubernetes.
        """
        namespace = self._object_namespace(namespace, action="patch status")
        label = self._object_label(name=name, namespace=namespace)
        body = {"status": _custom_object_fragment(status)}
        payload = await self._run_custom(
            kube,
            "patch_status",
            namespace=namespace,
            name=name,
            body=body,
            deadline=deadline,
            context=f"failed to patch {self.kind} status {label}",
            missing_ok=False,
        )
        return self._wrap_payload(payload)

    async def delete_by_name(
        self,
        kube: Kube,
        *,
        name: str,
        deadline: Deadline,
        namespace: str | None = None,
    ) -> None:
        """Delete one custom object by name."""
        namespace = self._object_namespace(namespace, action="delete")
        label = self._object_label(name=name, namespace=namespace)
        await self._run_custom(
            kube,
            "delete",
            namespace=namespace,
            name=name,
            deadline=deadline,
            context=f"failed to delete {self.kind} {label}",
            missing_ok=True,
        )

    async def wait_deleted(
        self,
        *,
        label: str,
        deadline: Deadline,
        refresh: Callable[[Deadline], Awaitable[object | None]],
    ) -> None:
        """Wait until a custom object disappears.

        Raises
        ------
        TimeoutError
            If the object still exists when `deadline` expires.
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

    async def _watch_once(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        namespace: str | None,
        labels: Mapping[str, str] | None,
        resource_version: str,
        field_selector: str,
    ) -> AsyncIterator[WatchEvent[T_co]]:
        endpoint = self._custom_endpoint(kube, "list", namespace=namespace)
        api_kwargs = self._custom_kwargs(
            namespace=namespace,
            name=None,
            body=None,
            label_selector="",
            field_selector="",
        )
        async for event in _watch(
            endpoint,
            wrapper=self._wrap_payload,
            deadline=deadline,
            context=self._collection_context("watch", namespace),
            resource_version=resource_version,
            label_selector=_label_selector(labels),
            field_selector=field_selector,
            api_kwargs=api_kwargs,
        ):
            yield event

    async def _snapshot(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        labels: Mapping[str, str] | None = None,
        namespace: str | None = None,
        field_selector: str = "",
    ) -> tuple[builtins.list[CustomObject], str]:
        payloads = await self._list_payloads(
            kube,
            namespaces=(namespace,) if namespace else None,
            label_selector=_label_selector(labels),
            field_selector=field_selector,
            deadline=deadline,
        )
        payload = payloads[0] if payloads else None
        if payload is None:
            msg = f"Kubernetes {self.kind} list returned no payload"
            raise OSError(msg)
        if not isinstance(payload, Mapping):
            msg = f"malformed Kubernetes {self.kind} list payload"
            raise OSError(msg)
        snapshot = cast("Mapping[str, object]", payload)
        return self._list_items(snapshot), self._snapshot_resource_version(snapshot)

    def _snapshot_resource_version(self, payload: Mapping[str, object]) -> str:
        metadata = payload.get("metadata", {})
        if not isinstance(metadata, Mapping):
            msg = f"malformed Kubernetes {self.kind} list metadata"
            raise OSError(msg)
        metadata = cast("Mapping[str, object]", metadata)
        resource_version = str(metadata.get("resourceVersion") or "").strip()
        if not resource_version:
            msg = f"Kubernetes {self.kind} list had no resourceVersion"
            raise OSError(msg)
        return resource_version

    def _list_items(
        self,
        payload: object,
    ) -> builtins.list[CustomObject]:
        if payload is None:
            msg = f"Kubernetes {self.kind} list returned no payload"
            raise OSError(msg)
        if not isinstance(payload, Mapping):
            msg = f"malformed Kubernetes {self.kind} list payload"
            raise OSError(msg)
        payload = cast("Mapping[str, object]", payload)
        items = payload.get("items", [])
        if not isinstance(items, list):
            msg = f"malformed Kubernetes {self.kind} list items"
            raise OSError(msg)
        return [
            CustomObject.from_payload(
                item,
                description=f"custom object payload for {self.plural}",
            )
            for item in items
        ]

    def _initial_watch_events(
        self,
        items: builtins.list[CustomObject],
        *,
        resource_version: str,
    ) -> tuple[WatchEvent[T_co], ...]:
        return tuple(
            WatchEvent(
                type="ADDED",
                object=self._wrap_payload(item.payload),
                resource_version=item.resource_version or resource_version,
            )
            for item in items
        )

    async def _list_payloads(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        namespaces: Collection[str] | None,
        label_selector: str,
        field_selector: str,
    ) -> builtins.list[object | None]:
        field_selector = field_selector.strip()
        normalized = _normalized_namespaces(namespaces)
        if self.scope == "cluster" or normalized is None:
            return [
                await self._run_custom(
                    kube,
                    "list",
                    namespace=None,
                    label_selector=label_selector,
                    field_selector=field_selector,
                    deadline=deadline,
                    context=self._collection_context("list", None),
                    missing_ok=False,
                )
            ]
        return list(
            await asyncio.gather(
                *(
                    self._run_custom(
                        kube,
                        "list",
                        namespace=namespace,
                        label_selector=label_selector,
                        field_selector=field_selector,
                        deadline=deadline,
                        context=self._collection_context("list", namespace),
                        missing_ok=False,
                    )
                    for namespace in normalized
                )
            )
        )

    def _object_namespace(self, namespace: str | None, *, action: str) -> str | None:
        namespace = self._default_namespace(namespace)
        namespace = namespace.strip() if namespace is not None else ""
        if self.scope == "cluster":
            if namespace:
                msg = f"{self.kind} is cluster-scoped; cannot {action} in namespace"
                raise ValueError(msg)
            return None
        if not namespace:
            msg = f"{self.kind} {action} requires a namespace"
            raise ValueError(msg)
        return namespace

    def _namespace_filter(
        self,
        *,
        action: Literal["list", "watch"],
        namespace: str | None,
        namespaces: Collection[str] | None = None,
    ) -> Collection[str] | None:
        namespace = self._default_namespace(namespace)
        namespace = namespace.strip() if namespace is not None else ""
        if self.scope == "cluster":
            if namespace or namespaces is not None:
                relation = "in namespace" if action == "watch" else "by namespace"
                msg = f"{self.kind} is cluster-scoped; cannot {action} {relation}"
                raise ValueError(msg)
            return None
        if namespace and namespaces is not None:
            msg = (
                f"{self.kind} {action} accepts either namespace or namespaces, not both"
            )
            raise ValueError(msg)
        return (namespace,) if namespace else namespaces

    def _body(
        self,
        *,
        namespace: str | None,
        name: str,
        spec: _CustomObjectFragment,
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
    ) -> dict[str, object]:
        merged_labels = dict(self.labels)
        merged_labels.update(labels or {})
        return {
            "apiVersion": self.api_version,
            "kind": self.kind,
            "metadata": self._metadata(
                name=name,
                namespace=namespace,
                labels=merged_labels,
                annotations=annotations,
            ),
            "spec": _custom_object_fragment(spec),
        }

    async def _run_custom(
        self,
        kube: Kube,
        operation: _CustomOperation,
        *,
        deadline: Deadline,
        context: str,
        missing_ok: bool,
        namespace: str | None = None,
        name: str | None = None,
        body: Mapping[str, object] | None = None,
        label_selector: str = "",
        field_selector: str = "",
    ) -> object | None:
        endpoint = self._custom_endpoint(kube, operation, namespace=namespace)
        api_kwargs = self._custom_kwargs(
            namespace=namespace,
            name=name,
            body=body,
            label_selector=label_selector,
            field_selector=field_selector,
        )
        return await kube.run(
            lambda request_timeout: endpoint(
                **api_kwargs,
                _request_timeout=request_timeout,
            ),
            deadline=deadline,
            context=context,
            missing_ok=missing_ok,
        )

    def _custom_endpoint(
        self,
        kube: Kube,
        operation: _CustomOperation,
        *,
        namespace: str | None,
    ) -> _CustomEndpoint:
        cluster_method, namespaced_method = _CUSTOM_ENDPOINTS[operation]
        use_cluster = self.scope == "cluster" or (
            operation == "list" and namespace is None
        )
        method = cluster_method if use_cluster else namespaced_method
        return cast("_CustomEndpoint", getattr(kube.custom, method))

    def _custom_kwargs(
        self,
        *,
        namespace: str | None,
        name: str | None,
        body: Mapping[str, object] | None,
        label_selector: str,
        field_selector: str,
    ) -> dict[str, object]:
        kwargs: dict[str, object] = {
            "group": self.group,
            "version": self.version,
            "plural": self.plural,
        }
        if namespace is not None:
            kwargs["namespace"] = namespace
        if name is not None:
            kwargs["name"] = name
        if body is not None:
            kwargs["body"] = body
        if label_selector:
            kwargs["label_selector"] = label_selector
        if field_selector:
            kwargs["field_selector"] = field_selector
        return kwargs

    def _wrap_payload(self, payload: object) -> T_co:
        obj = CustomObject.from_payload(
            payload,
            description=f"custom object payload for {self.plural}",
        )
        if self.payload_parser is None:
            return cast("T_co", obj)
        try:
            return self.payload_parser(obj.payload)
        except (TypeError, ValidationError) as err:
            if self.payload_error_context is None:
                raise
            msg = f"malformed {self.payload_error_context}: {err}"
            raise OSError(msg) from err

    def _spec_schema(self) -> Mapping[str, object] | None:
        if self.spec_model is None:
            return None
        return _schema_from_model(
            self.spec_model,
            include_defaults=self.spec_schema_include_defaults,
            overrides=self.spec_schema_overrides,
        )

    def _status_schema(self) -> Mapping[str, object] | None:
        if self.status_model is None:
            return None
        return _schema_from_model(
            self.status_model,
            include_defaults=self.status_schema_include_defaults,
            overrides=self.status_schema_overrides,
        )

    def _selector(self, labels: Mapping[str, str] | None) -> Mapping[str, str] | None:
        if not self.labels:
            return labels
        merged = dict(self.labels)
        merged.update(labels or {})
        return merged

    def _default_namespace(self, namespace: str | None) -> str | None:
        return namespace if namespace is not None else self.default_namespace

    def _object_label(self, *, name: str, namespace: str | None) -> str:
        return f"{namespace}/{name}" if namespace else name

    def _collection_context(
        self,
        action: Literal["list", "watch"],
        namespace: str | None,
    ) -> str:
        context = f"failed to {action} {self.kind}s"
        return context if namespace is None else f"{context} in namespace {namespace!r}"

    def _manifest_label(
        self,
        manifest: Mapping[str, object],
        *,
        namespace: str | None,
        name: str | None,
    ) -> str:
        self._require_manifest_name(manifest)
        metadata = cast("Mapping[str, object]", manifest["metadata"])
        object_name = name or str(metadata.get("name") or "").strip()
        return self._object_label(name=object_name, namespace=namespace)

    def _metadata(
        self,
        *,
        name: str,
        namespace: str | None,
        labels: Mapping[str, str],
        annotations: Mapping[str, str] | None,
    ) -> dict[str, object]:
        metadata: dict[str, object] = {
            "name": name,
            "labels": dict(labels),
            "annotations": dict(annotations or {}),
        }
        if self.scope == "namespaced":
            metadata["namespace"] = namespace
        return metadata

    @staticmethod
    def _require_manifest_name(body: Mapping[str, object]) -> None:
        metadata = body.get("metadata")
        if not isinstance(metadata, Mapping):
            msg = "custom object manifest must define metadata"
            raise OSError(msg)
        metadata = cast("Mapping[str, object]", metadata)
        name = str(metadata.get("name") or "").strip()
        if not name:
            msg = "custom object manifest must define metadata.name"
            raise OSError(msg)


@dataclass(frozen=True)
class _CustomResourceConfig:
    group: str
    version: str
    kind: str
    plural: str
    scope: CustomObjectScope = "namespaced"
    labels: Mapping[str, str] = field(default_factory=lambda: MappingProxyType({}))
    singular: str | None = None
    spec_model: type[BaseModel] | None = None
    spec_schema_overrides: Mapping[str, object] | None = None
    spec_schema_include_defaults: bool = False
    short_names: tuple[str, ...] = ()
    status_model: type[BaseModel] | None = None
    status_schema_overrides: Mapping[str, object] | None = None
    status_schema_include_defaults: bool = False
    default_namespace: str | None = None


_CUSTOM_RESOURCE_REGISTRY: dict[type[Any], _CustomResourceConfig] = {}


def custom_resource(
    *,
    group: str,
    version: str,
    plural: str,
    scope: CustomObjectScope = "namespaced",
    kind: str | None = None,
    labels: Mapping[str, str] | None = None,
    singular: str | None = None,
    spec_model: type[BaseModel] | None = None,
    spec_schema_overrides: Mapping[str, object] | None = None,
    spec_schema_include_defaults: bool = False,
    short_names: Collection[str] = (),
    status_model: type[BaseModel] | None = None,
    status_schema_overrides: Mapping[str, object] | None = None,
    status_schema_include_defaults: bool = False,
    default_namespace: str | None = None,
) -> Callable[[_CustomResourceClass], _CustomResourceClass]:
    """Register Kubernetes custom-resource metadata for a wrapper class.

    Parameters
    ----------
    group : str
        Kubernetes API group that owns the custom resource.
    version : str
        Served API version.
    plural : str
        Plural REST resource name.
    scope : {"cluster", "namespaced"}, optional
        Kubernetes API scope.
    kind : str | None, optional
        Kubernetes kind name. Defaults to the decorated class name.
    labels : Mapping[str, str] | None, optional
        Default labels to apply to created objects and list selectors.
    singular : str | None, optional
        Singular resource name for CRD ownership. When omitted, the wrapper can use
        an existing API but cannot converge its CRD.
    spec_model : type[BaseModel] | None, optional
        Pydantic model used to generate a CRD `spec` schema.
    spec_schema_overrides : Mapping[str, object] | None, optional
        Schema overrides merged into the generated `spec` schema.
    spec_schema_include_defaults : bool, optional
        Whether generated `spec` schema fragments keep Pydantic defaults.
    short_names : Collection[str], optional
        Optional CRD short names.
    status_model : type[BaseModel] | None, optional
        Pydantic model used to generate a CRD `status` schema.
    status_schema_overrides : Mapping[str, object] | None, optional
        Schema overrides merged into the generated `status` schema.
    status_schema_include_defaults : bool, optional
        Whether generated `status` schema fragments keep Pydantic defaults.
    default_namespace : str | None, optional
        Namespace used by namespaced resources when callers omit one.

    Returns
    -------
    Callable[[type[Any]], type[Any]]
        Class decorator that records the resource metadata privately.

    Raises
    ------
    ValueError
        If the declaration has an invalid scope or missing identity fields.
    """
    group = group.strip()
    version = version.strip()
    plural = plural.strip()
    raw_scope = scope.strip()
    if raw_scope not in ("cluster", "namespaced"):
        msg = (
            "custom resource scope must be 'cluster' or 'namespaced', "
            f"got {raw_scope!r}"
        )
        raise ValueError(msg)
    resource_scope = raw_scope
    if not group or not version or not plural:
        msg = (
            "custom resource declarations require non-empty group, version, and plural"
        )
        raise ValueError(msg)

    def register(cls: _CustomResourceClass) -> _CustomResourceClass:
        resource_kind = (kind or cls.__name__).strip()
        if not resource_kind:
            msg = "custom resource declarations require non-empty kind"
            raise ValueError(msg)
        _CUSTOM_RESOURCE_REGISTRY[cls] = _CustomResourceConfig(
            group=group,
            version=version,
            kind=resource_kind,
            plural=plural,
            scope=resource_scope,
            labels=MappingProxyType(dict(labels or {})),
            singular=None if singular is None else singular.strip() or None,
            spec_model=spec_model,
            spec_schema_overrides=(
                None
                if spec_schema_overrides is None
                else MappingProxyType(dict(spec_schema_overrides))
            ),
            spec_schema_include_defaults=spec_schema_include_defaults,
            short_names=tuple(name.strip() for name in short_names if name.strip()),
            status_model=status_model,
            status_schema_overrides=(
                None
                if status_schema_overrides is None
                else MappingProxyType(dict(status_schema_overrides))
            ),
            status_schema_include_defaults=status_schema_include_defaults,
            default_namespace=(
                None if default_namespace is None else default_namespace.strip() or None
            ),
        )
        return cls

    return register


class CustomResource(CustomObject):
    """Base class for class-owned Kubernetes custom-object wrappers."""

    @classmethod
    async def ensure_crd(cls, kube: Kube, *, deadline: Deadline) -> None:
        """Converge this custom resource's CRD and wait until it is established."""
        await cls._api().ensure_crd(kube, deadline=deadline)

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
        """Read one custom object by name.

        Returns
        -------
        CustomResource | None
            Wrapped custom object, or `None` when absent.
        """
        return await cls._api().get(
            kube,
            name=name,
            deadline=deadline,
            namespace=namespace,
            context=context,
        )

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
        """List custom objects with optional namespace and label filtering.

        Returns
        -------
        list[CustomResource]
            Wrapped custom objects matching the filters.
        """
        return await cls._api().list(
            kube,
            deadline=deadline,
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
        deadline: Deadline,
        labels: Mapping[str, str] | None = None,
        namespace: str | None = None,
        resource_version: str = "",
        emit_initial: bool = False,
        field_selector: str = "",
    ) -> AsyncIterator[WatchEvent[Self]]:
        """Watch custom objects and yield typed events.

        Yields
        ------
        WatchEvent[CustomResource]
            Typed custom-object watch events.
        """
        async for event in cls._api().watch(
            kube,
            deadline=deadline,
            labels=labels,
            namespace=namespace,
            resource_version=resource_version,
            emit_initial=emit_initial,
            field_selector=field_selector,
        ):
            yield event

    @classmethod
    async def create_spec(
        cls,
        kube: Kube,
        *,
        name: str,
        spec: _CustomObjectFragment,
        deadline: Deadline,
        namespace: str | None = None,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> Self:
        """Create one custom object from intent fields.

        Returns
        -------
        CustomResource
            Wrapped created object.
        """
        return await cls._api().create(
            kube,
            name=name,
            spec=spec,
            deadline=deadline,
            namespace=namespace,
            labels=labels,
            annotations=annotations,
        )

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
    ) -> Self:
        """Create one custom object from a complete Kubernetes manifest.

        Returns
        -------
        CustomResource
            Wrapped created object.
        """
        return await cls._api().create_manifest(
            kube,
            manifest=manifest,
            deadline=deadline,
            namespace=namespace,
            name=name,
            context=context,
        )

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        name: str,
        deadline: Deadline,
        namespace: str | None = None,
        spec: _CustomObjectFragment | None = None,
        manifest: Mapping[str, object] | None = None,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> Self:
        """Create or patch one custom object from intent fields.

        Returns
        -------
        CustomResource
            Wrapped created or patched object.
        """
        return await cls._api().upsert(
            kube,
            name=name,
            deadline=deadline,
            namespace=namespace,
            spec=spec,
            manifest=manifest,
            labels=labels,
            annotations=annotations,
        )

    @classmethod
    async def patch(
        cls,
        kube: Kube,
        *,
        name: str,
        body: Mapping[str, object],
        deadline: Deadline,
        namespace: str | None = None,
        context: str | None = None,
    ) -> Self:
        """Patch one custom object with a raw Kubernetes patch body.

        Returns
        -------
        CustomResource
            Wrapped patched object.
        """
        return await cls._api().patch(
            kube,
            name=name,
            body=body,
            deadline=deadline,
            namespace=namespace,
            context=context,
        )

    @classmethod
    async def patch_status(
        cls,
        kube: Kube,
        *,
        name: str,
        status: _CustomObjectFragment,
        deadline: Deadline,
        namespace: str | None = None,
    ) -> Self:
        """Patch one custom-object status payload.

        Returns
        -------
        CustomResource
            Wrapped object returned by Kubernetes.
        """
        return await cls._api().patch_status(
            kube,
            name=name,
            status=status,
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
    ) -> None:
        """Delete one custom object by name."""
        await cls._api().delete_by_name(
            kube,
            name=name,
            deadline=deadline,
            namespace=namespace,
        )

    async def refresh(self, kube: Kube, *, deadline: Deadline) -> Self | None:
        """Re-read this custom object by metadata identity.

        Returns
        -------
        CustomResource | None
            Fresh wrapped object, or `None` when absent.
        """
        namespace, name = self._require_resource_identity("refresh custom object")
        return await type(self).get(
            kube,
            namespace=namespace,
            name=name,
            deadline=deadline,
        )

    async def delete(self, kube: Kube, *, deadline: Deadline) -> None:
        """Delete this custom object from the cluster."""
        namespace, name = self._require_resource_identity("delete custom object")
        await type(self).delete_by_name(
            kube,
            namespace=namespace,
            name=name,
            deadline=deadline,
        )

    async def wait_deleted(self, kube: Kube, *, deadline: Deadline) -> None:
        """Wait until this custom object is deleted."""
        namespace, name = self._require_resource_identity("wait for custom object")
        label = f"{type(self).__name__} {namespace}/{name}" if namespace else name
        await (
            type(self)
            ._api()
            .wait_deleted(
                label=label,
                deadline=deadline,
                refresh=lambda remaining: self.refresh(kube, deadline=remaining),
            )
        )

    def _require_resource_identity(self, action: str) -> tuple[str | None, str]:
        if type(self)._config().scope == "cluster":
            return None, self._require_name(action)
        return self._require_namespace_name(action)

    def _require_name(self, action: str) -> str:
        name = self.name
        if not name:
            msg = f"cannot {action} with missing metadata.name"
            raise OSError(msg)
        return name

    def _require_namespace_name(self, action: str) -> tuple[str, str]:
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = f"cannot {action} with missing metadata.name/namespace"
            raise OSError(msg)
        return namespace, name

    @classmethod
    def _config(cls) -> _CustomResourceConfig:
        try:
            return _CUSTOM_RESOURCE_REGISTRY[cls]
        except KeyError as err:
            msg = f"{cls.__name__} must be decorated with @custom_resource"
            raise TypeError(msg) from err

    @classmethod
    def _api(cls) -> _CustomObjectAPI[Self]:
        config = cls._config()
        return _CustomObjectAPI(
            group=config.group,
            version=config.version,
            kind=config.kind,
            plural=config.plural,
            scope=config.scope,
            labels=config.labels,
            payload_parser=cls.from_payload,
            payload_error_context=f"{config.kind} payload",
            singular=config.singular,
            spec_model=config.spec_model,
            spec_schema_overrides=config.spec_schema_overrides,
            spec_schema_include_defaults=config.spec_schema_include_defaults,
            short_names=config.short_names,
            status_model=config.status_model,
            status_schema_overrides=config.status_schema_overrides,
            status_schema_include_defaults=config.status_schema_include_defaults,
            default_namespace=config.default_namespace,
        )


def _custom_object_fragment(fragment: _CustomObjectFragment) -> dict[str, object]:
    if isinstance(fragment, BaseModel):
        return cast(
            "dict[str, object]",
            fragment.model_dump(mode="json", by_alias=True),
        )
    return dict(fragment)


def _schema_from_model(
    model: type[BaseModel],
    *,
    include_defaults: bool,
    overrides: Mapping[str, object] | None,
) -> Mapping[str, object]:
    raw = model.model_json_schema(mode="validation", by_alias=True)
    normalized = _normalize_schema_fragment(
        raw,
        defs=raw.get("$defs", {}),
        include_defaults=include_defaults,
    )
    if not isinstance(normalized, Mapping):
        msg = f"{model.__name__} did not produce an object schema"
        raise TypeError(msg)
    schema = cast("dict[str, object]", dict(normalized))
    if overrides is not None:
        _merge_schema_overrides(schema, overrides)
    return schema


def _normalize_schema_fragment(
    fragment: object,
    *,
    defs: object,
    include_defaults: bool,
) -> object:
    if isinstance(fragment, Mapping):
        fragment_mapping = cast("Mapping[str, object]", fragment)
        if "$ref" in fragment_mapping:
            definitions = cast("Mapping[str, object]", defs)
            ref = str(fragment_mapping["$ref"])
            name = ref.removeprefix("#/$defs/")
            replacement = dict(cast("Mapping[str, object]", definitions[name]))
            replacement.update(
                {
                    str(key): value
                    for key, value in fragment_mapping.items()
                    if str(key) != "$ref"
                }
            )
            return _normalize_schema_fragment(
                replacement,
                defs=defs,
                include_defaults=include_defaults,
            )

        any_of = fragment_mapping.get("anyOf")
        if isinstance(any_of, list):
            variants = [
                item
                for item in any_of
                if not (
                    isinstance(item, Mapping)
                    and cast("Mapping[str, object]", item).get("type") == "null"
                )
            ]
            if len(variants) == 1 and len(variants) != len(any_of):
                nullable = _normalize_schema_fragment(
                    variants[0],
                    defs=defs,
                    include_defaults=include_defaults,
                )
                if isinstance(nullable, Mapping):
                    schema = dict(cast("Mapping[str, object]", nullable))
                    schema["nullable"] = True
                    for key, value in fragment_mapping.items():
                        key = str(key)
                        if key in {"anyOf", "title", "description", "$defs", "$schema"}:
                            continue
                        if key == "default" and not include_defaults:
                            continue
                        schema[key] = _normalize_schema_fragment(
                            value,
                            defs=defs,
                            include_defaults=include_defaults,
                        )
                    return schema

        schema: dict[str, object] = {}
        for key, value in fragment_mapping.items():
            key = str(key)
            if key in {"title", "description", "$defs", "$schema", "propertyNames"}:
                continue
            if key == "default" and not include_defaults:
                continue
            if key == "additionalProperties" and value is False:
                continue
            schema[key] = _normalize_schema_fragment(
                value,
                defs=defs,
                include_defaults=include_defaults,
            )
        if schema.get("type") == "integer" and schema.get("exclusiveMinimum") == 0:
            schema.pop("exclusiveMinimum", None)
            schema["minimum"] = 1
        exclusive_minimum = schema.get("exclusiveMinimum")
        exclusive_maximum = schema.get("exclusiveMaximum")
        if (
            schema.get("type") == "number"
            and isinstance(exclusive_minimum, int | float)
            and isinstance(exclusive_maximum, int | float)
            and math.isclose(float(exclusive_minimum), 0.0)
            and math.isclose(float(exclusive_maximum), 1.0)
        ):
            schema.pop("exclusiveMinimum", None)
            schema.pop("exclusiveMaximum", None)
            schema["minimum"] = 0
            schema["maximum"] = 1
        return schema
    if isinstance(fragment, list):
        return [
            _normalize_schema_fragment(
                item,
                defs=defs,
                include_defaults=include_defaults,
            )
            for item in fragment
        ]
    return fragment


def _merge_schema_overrides(
    schema: dict[str, object],
    overrides: Mapping[str, object],
) -> None:
    for key, value in overrides.items():
        if (
            key == "properties"
            and isinstance(value, Mapping)
            and isinstance(schema.get(key), Mapping)
        ):
            properties = cast("dict[str, object]", schema[key])
            for prop, prop_schema in value.items():
                properties[str(prop)] = prop_schema
            continue
        existing = schema.get(key)
        if isinstance(existing, dict) and isinstance(value, Mapping):
            nested = cast("dict[str, object]", existing)
            _merge_schema_overrides(nested, cast("Mapping[str, object]", value))
            continue
        schema[key] = value


def _parse_kubernetes_datetime(value: str) -> datetime | None:
    value = value.strip()
    if not value:
        return None
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=UTC)
    return parsed.astimezone(UTC)
