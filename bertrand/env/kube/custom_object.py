"""Wrappers for Kubernetes runtime custom objects."""

from __future__ import annotations

import math
from collections.abc import AsyncIterator, Awaitable, Callable, Collection, Mapping
from dataclasses import dataclass, field
from datetime import UTC, datetime
from types import MappingProxyType
from typing import TYPE_CHECKING, Any, ClassVar, Literal, Self, cast

from pydantic import BaseModel, ConfigDict, Field, ValidationError

from bertrand.env.git import Deadline
from bertrand.env.kube.crd import CustomResourceDefinition

from .api._helpers import (
    _is_conflict,
    _is_missing_api_resource,
    _is_not_found,
    _label_selector,
    _wait_until_deleted,
)
from .api.watch import WatchEvent, WatchExpired
from .api.watch import watch as kube_watch

if TYPE_CHECKING:
    import builtins

    from .api.client import Kube


type CustomObjectScope = Literal["cluster", "namespaced"]
type _CustomObjectFragment = Mapping[str, object] | BaseModel


@dataclass(frozen=True)
class _CustomObjectSnapshot:
    items: list[CustomObject]
    resource_version: str


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
        Kubernetes resource version, parsed from ``resourceVersion``.
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
    """Generic wrapper around one Kubernetes custom object.

    Parameters
    ----------
    group : str
        API group that owns the custom object.
    version : str
        Served API version.
    plural : str
        Plural REST resource name.
    scope : {"cluster", "namespaced"}
        Kubernetes API scope.
    payload : Mapping[str, Any]
        Custom-object payload returned by Kubernetes.
    """

    group: str
    version: str
    plural: str
    scope: CustomObjectScope
    _payload: Mapping[str, Any] = field(repr=False)

    def __init__(
        self,
        *,
        group: str,
        version: str,
        plural: str,
        scope: CustomObjectScope,
        payload: Mapping[str, Any],
    ) -> None:
        """Initialize one custom-object wrapper.

        Parameters
        ----------
        group : str
            API group that owns the custom object.
        version : str
            Served API version.
        plural : str
            Plural REST resource name.
        scope : {"cluster", "namespaced"}
            Kubernetes API scope.
        payload : Mapping[str, Any]
            Custom-object payload returned by Kubernetes.
        """
        object.__setattr__(self, "group", group)
        object.__setattr__(self, "version", version)
        object.__setattr__(self, "plural", plural)
        object.__setattr__(self, "scope", scope)
        object.__setattr__(self, "_payload", payload)

    @classmethod
    def from_payload(
        cls,
        *,
        group: str,
        version: str,
        plural: str,
        scope: CustomObjectScope,
        payload: object,
    ) -> Self:
        """Create a custom-object wrapper from a Kubernetes payload.

        Parameters
        ----------
        group : str
            API group that owns the custom object.
        version : str
            Served API version.
        plural : str
            Plural REST resource name.
        scope : {"cluster", "namespaced"}
            Kubernetes API scope.
        payload : object
            Custom-object payload returned by Kubernetes.

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
            msg = f"malformed Kubernetes custom object payload for {plural}"
            raise OSError(msg)
        return cls(
            group=group,
            version=version,
            plural=plural,
            scope=scope,
            payload=cast("Mapping[str, Any]", payload),
        )

    @property
    def payload(self) -> Mapping[str, Any]:
        """Return this custom object's payload.

        Returns
        -------
        Mapping[str, Any]
            Read-only top-level view of the custom-object payload returned by
            Kubernetes.
        """
        return MappingProxyType(dict(self._payload))

    @property
    def metadata(self) -> Mapping[str, Any]:
        """Return this custom object's metadata.

        Returns
        -------
        Mapping[str, Any]
            Read-only view of `metadata`, or an empty mapping when unavailable.
        """
        metadata = self._payload.get("metadata", {})
        if not isinstance(metadata, Mapping):
            return MappingProxyType({})
        return MappingProxyType(cast("dict[str, Any]", dict(metadata)))

    @property
    def spec(self) -> Mapping[str, Any]:
        """Return this custom object's spec payload.

        Returns
        -------
        Mapping[str, Any]
            Read-only view of `spec`, or an empty mapping when unavailable.
        """
        spec = self._payload.get("spec", {})
        if not isinstance(spec, Mapping):
            return MappingProxyType({})
        return MappingProxyType(cast("dict[str, Any]", dict(spec)))

    @property
    def status(self) -> Mapping[str, Any]:
        """Return this custom object's status payload.

        Returns
        -------
        Mapping[str, Any]
            Read-only view of `status`, or an empty mapping when unavailable.
        """
        status = self._payload.get("status", {})
        if not isinstance(status, Mapping):
            return MappingProxyType({})
        return MappingProxyType(cast("dict[str, Any]", dict(status)))

    @property
    def name(self) -> str:
        """Return this custom object's name.

        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        return str(self.metadata.get("name") or "").strip()

    @property
    def namespace(self) -> str:
        """Return this custom object's namespace.

        Returns
        -------
        str
            Trimmed `metadata.namespace`, or an empty string when unavailable.
        """
        return str(self.metadata.get("namespace") or "").strip()

    @property
    def labels(self) -> Mapping[str, str]:
        """Return this custom object's labels.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.labels`, or an empty mapping when
            unavailable.
        """
        labels = self.metadata.get("labels", {})
        if not isinstance(labels, Mapping):
            return MappingProxyType({})
        return MappingProxyType({str(key): str(value) for key, value in labels.items()})

    @property
    def annotations(self) -> Mapping[str, str]:
        """Return this custom object's annotations.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.annotations`, or an empty mapping when
            unavailable.
        """
        annotations = self.metadata.get("annotations", {})
        if not isinstance(annotations, Mapping):
            return MappingProxyType({})
        return MappingProxyType(
            {str(key): str(value) for key, value in annotations.items()}
        )

    @property
    def resource_version(self) -> str:
        """Return this custom object's resource version.

        Returns
        -------
        str
            Kubernetes `metadata.resourceVersion`, or an empty string when
            unavailable.
        """
        return str(self.metadata.get("resourceVersion") or "").strip()

    @property
    def uid(self) -> str:
        """Return this custom object's UID.

        Returns
        -------
        str
            Kubernetes `metadata.uid`, or an empty string when unavailable.
        """
        return str(self.metadata.get("uid") or "").strip()

    @property
    def created_at(self) -> str:
        """Return this custom object's creation timestamp.

        Returns
        -------
        str
            Kubernetes `metadata.creationTimestamp`, or an empty string when
            unavailable.
        """
        return str(self.metadata.get("creationTimestamp") or "").strip()

    @property
    def created_at_utc(self) -> datetime | None:
        """Return this custom object's creation timestamp in UTC.

        Returns
        -------
        datetime | None
            Parsed Kubernetes `metadata.creationTimestamp`, or `None` when
            unavailable or malformed.
        """
        return _parse_kubernetes_datetime(self.created_at)

    @staticmethod
    def parse_utc_datetime(value: object) -> datetime | None:
        """Parse a Kubernetes timestamp-like value as UTC.

        Parameters
        ----------
        value : object
            Timestamp value to parse.

        Returns
        -------
        datetime | None
            Parsed UTC timestamp, or `None` when the input is empty or malformed.
        """
        return _parse_kubernetes_datetime(str(value or ""))


@dataclass(frozen=True)
class CustomObjectWrapper:
    """Base wrapper around one Kubernetes custom object.

    Parameters
    ----------
    _obj : CustomObject
        Generic custom object returned by Kubernetes.
    """

    _obj: CustomObject
    resource: ClassVar[CustomObjectResource[Any]]

    @classmethod
    def _from_object(cls, obj: CustomObject) -> Self:
        return cls(_obj=obj)

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        name: str,
        timeout: float,
        namespace: str | None = None,
    ) -> Self | None:
        """Read one wrapped custom object by name.

        Returns
        -------
        Self | None
            Wrapped object, or `None` when absent.
        """
        result = await cls.resource.get(
            kube,
            namespace=namespace,
            name=name,
            timeout=timeout,
        )
        return cast("Self | None", result)

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        timeout: float,
        labels: Mapping[str, str] | None = None,
        namespace: str | None = None,
    ) -> builtins.list[Self]:
        """List wrapped custom objects with optional label filtering.

        Returns
        -------
        list[Self]
            Wrapped objects matching the selector.
        """
        result = await cls.resource.list(
            kube,
            namespace=namespace,
            labels=labels,
            timeout=timeout,
        )
        return cast("list[Self]", result)

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this custom object.

        Returns
        -------
        Self | None
            Fresh wrapper, or `None` when deleted.

        Raises
        ------
        OSError
            If this wrapper does not carry enough metadata to refresh itself.
        """
        if self._obj.scope == "namespaced" and not self.namespace:
            msg = f"cannot refresh {self._obj.plural} object without namespace"
            raise OSError(msg)
        if not self.name:
            msg = f"cannot refresh {self._obj.plural} object without name"
            raise OSError(msg)
        return await type(self).get(
            kube,
            namespace=self.namespace or None,
            name=self.name,
            timeout=timeout,
        )

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this custom object by name.

        Raises
        ------
        OSError
            If this wrapper does not carry enough metadata to delete itself.
        """
        if self._obj.scope == "namespaced" and not self.namespace:
            msg = f"cannot delete {self._obj.plural} object without namespace"
            raise OSError(msg)
        if not self.name:
            msg = f"cannot delete {self._obj.plural} object without name"
            raise OSError(msg)
        await type(self).resource.delete_by_name(
            kube,
            namespace=self.namespace or None,
            name=self.name,
            timeout=timeout,
        )

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this custom object is deleted."""
        label = f"{self._obj.plural} {self.namespace}/{self.name}"
        if not self.namespace:
            label = f"{self._obj.plural} {self.name}"
        await type(self).resource.wait_deleted(
            label=label,
            timeout=timeout,
            refresh=lambda remaining: self.refresh(kube, timeout=remaining),
        )

    @property
    def name(self) -> str:
        """Return the Kubernetes object name.

        Returns
        -------
        str
            Kubernetes ``metadata.name``.
        """
        return self._obj.name

    @property
    def namespace(self) -> str:
        """Return the Kubernetes object namespace.

        Returns
        -------
        str
            Kubernetes ``metadata.namespace``, or an empty string for cluster-scoped
            objects.
        """
        return self._obj.namespace

    @property
    def labels(self) -> Mapping[str, str]:
        """Return Kubernetes object labels.

        Returns
        -------
        Mapping[str, str]
            Read-only Kubernetes labels.
        """
        return self._obj.labels


@dataclass(frozen=True)
class CustomObjectResource[T_co]:
    """Descriptor for an externally installed or Bertrand-owned custom-object API.

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
    scope : {"cluster", "namespaced"}, optional
        Kubernetes API scope.
    labels : Mapping[str, str], optional
        Default labels to apply to created objects and list selectors.
    parser : Callable[[CustomObject], T_co] | None, optional
        Wrapper factory for low-level Kubernetes objects. If omitted, the raw
        `CustomObject` is returned.
    payload_parser : Callable[[object], T_co] | None, optional
        Payload validator for record-oriented resources.
    payload_error_context : str | None, optional
        Context used when wrapping Pydantic payload validation errors as
        `OSError`.
    """

    group: str
    version: str
    kind: str
    plural: str
    scope: CustomObjectScope = "namespaced"
    labels: Mapping[str, str] = MappingProxyType({})
    parser: Callable[[CustomObject], T_co] | None = None
    payload_parser: Callable[[object], T_co] | None = None
    payload_error_context: str | None = None
    singular: str | None = None
    spec_schema: Mapping[str, object] | None = None
    spec_model: type[BaseModel] | None = None
    spec_schema_overrides: Mapping[str, object] | None = None
    spec_schema_include_defaults: bool = False
    short_names: Collection[str] = ()
    status_schema: Mapping[str, object] | None = None
    status_model: type[BaseModel] | None = None
    status_schema_overrides: Mapping[str, object] | None = None
    status_schema_include_defaults: bool = False
    crd_labels: Mapping[str, str] | None = None
    crd_timeout_message: str | None = None
    default_namespace: str | None = None

    def __post_init__(self) -> None:
        """Validate descriptor parser configuration.

        Raises
        ------
        ValueError
            If mutually exclusive parser options are configured together.
        """
        if self.parser is not None and self.payload_parser is not None:
            msg = f"{self.kind} descriptor cannot define both parsers"
            raise ValueError(msg)
        if self.payload_error_context is not None and self.payload_parser is None:
            msg = f"{self.kind} descriptor error context requires a payload parser"
            raise ValueError(msg)
        if self.spec_schema is not None and self.spec_model is not None:
            msg = f"{self.kind} descriptor cannot define both spec schema sources"
            raise ValueError(msg)
        if self.status_schema is not None and self.status_model is not None:
            msg = f"{self.kind} descriptor cannot define both status schema sources"
            raise ValueError(msg)
        if self.spec_schema_overrides is not None and self.spec_model is None:
            msg = f"{self.kind} descriptor spec schema overrides require a model"
            raise ValueError(msg)
        if self.status_schema_overrides is not None and self.status_model is None:
            msg = f"{self.kind} descriptor status schema overrides require a model"
            raise ValueError(msg)

    @property
    def api_version(self) -> str:
        """Return the fully qualified Kubernetes API version.

        Returns
        -------
        str
            API version string in `group/version` form.
        """
        return f"{self.group}/{self.version}"

    async def ensure_crd(self, kube: Kube, *, timeout: float) -> None:
        """Converge this descriptor's CRD and wait until it is established.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum convergence budget in seconds.

        Raises
        ------
        TimeoutError
            If `timeout` is non-positive or establishment exceeds the budget.
        ValueError
            If this descriptor does not own a CRD.
        """
        spec_schema = self._spec_schema()
        if self.singular is None or spec_schema is None:
            msg = f"{self.kind} descriptor does not own a CRD"
            raise ValueError(msg)
        message = self.crd_timeout_message or (
            f"{self.kind} CRD timeout must be non-negative"
        )
        if timeout <= 0:
            raise TimeoutError(message)
        deadline = Deadline.from_timeout(timeout, message=message)
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
            labels=self.crd_labels or self.labels,
            scope="Cluster" if self.scope == "cluster" else "Namespaced",
            timeout=deadline.remaining(),
        )
        await crd.wait_established(kube, timeout=deadline.remaining())

    async def get(
        self,
        kube: Kube,
        *,
        name: str,
        timeout: float,
        namespace: str | None = None,
        context: str | None = None,
    ) -> T_co | None:
        """Read one custom object by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Custom object name.
        timeout : float
            Maximum request budget in seconds.
        namespace : str | None, optional
            Namespace that owns the custom object.
        context : str | None, optional
            Kubernetes request diagnostic override.

        Returns
        -------
        T_co | None
            Wrapped object, or ``None`` if it does not exist.

        Raises
        ------
        OSError
            If the custom-object API kind is unavailable or the returned payload is
            malformed.
        """
        namespace = self._single_namespace(self._namespace(namespace), action="read")
        label = self._object_label(name=name, namespace=namespace)
        endpoint, api_kwargs = self._read_endpoint(
            kube,
            namespace=namespace,
            name=name,
        )
        try:
            payload = await self._request(
                kube,
                endpoint=endpoint,
                api_kwargs=api_kwargs,
                timeout=timeout,
                context=context or f"failed to read {self.kind} {label}",
            )
        except OSError as err:
            if _is_not_found(err) and not _is_missing_api_resource(err):
                return None
            raise
        if payload is None:
            return None
        return self._wrap(self._wrap_object(payload))

    async def list(
        self,
        kube: Kube,
        *,
        timeout: float,
        labels: Mapping[str, str] | None = None,
        namespace: str | None = None,
    ) -> builtins.list[T_co]:
        """List custom objects with optional labels.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds.
        labels : Mapping[str, str] | None, optional
            Exact-match label selector.
        namespace : str | None, optional
            Optional namespace filter.

        Returns
        -------
        list[T_co]
            Wrapped objects returned by Kubernetes.
        """
        snapshot = await self._snapshot(
            kube,
            namespace=self._namespace(namespace),
            labels=self._selector(labels),
            timeout=timeout,
        )
        return [self._wrap(obj) for obj in snapshot.items]

    async def watch(
        self,
        kube: Kube,
        *,
        timeout: float,
        labels: Mapping[str, str] | None = None,
        namespace: str | None = None,
        resource_version: str | None = None,
        emit_initial: bool = False,
    ) -> AsyncIterator[WatchEvent[T_co]]:
        """Watch custom objects and yield typed events.

        Yields
        ------
        WatchEvent[T_co]
            Typed custom-object watch events.

        Raises
        ------
        TimeoutError
            If the watch timeout is non-positive.
        """
        if timeout <= 0:
            msg = f"{self.kind} watch timeout must be non-negative"
            raise TimeoutError(msg)
        namespace = self._watch_namespace(self._namespace(namespace))
        labels = self._selector(labels)
        deadline = Deadline.from_timeout(
            timeout,
            message=f"{self.kind} watch timeout must be non-negative",
        )
        current_version = resource_version.strip() if resource_version else ""
        can_emit_initial = emit_initial and not current_version
        while True:
            remaining = deadline.remaining()
            if remaining <= 0:
                return
            if not current_version:
                snapshot = await self._snapshot(
                    kube,
                    namespace=namespace,
                    labels=labels,
                    timeout=remaining,
                )
                current_version = snapshot.resource_version
                if can_emit_initial:
                    for event in self._initial_watch_events(snapshot):
                        yield event
                    can_emit_initial = False

            remaining = deadline.remaining()
            if remaining <= 0:
                return
            try:
                async for event in self._watch_once(
                    kube,
                    namespace=namespace,
                    labels=labels,
                    timeout=remaining,
                    resource_version=current_version,
                ):
                    if event.resource_version:
                        current_version = event.resource_version
                    yield WatchEvent(
                        type=event.type,
                        object=self._wrap(event.object),
                        resource_version=event.resource_version,
                        raw_type=event.raw_type,
                    )
            except WatchExpired:
                current_version = ""
            else:
                return

    async def create(
        self,
        kube: Kube,
        *,
        name: str,
        spec: _CustomObjectFragment,
        timeout: float,
        namespace: str | None = None,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> T_co:
        """Create one custom object from intent fields.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Custom object name.
        spec : Mapping[str, object] | BaseModel
            Desired custom object ``spec`` payload.
        timeout : float
            Maximum request budget in seconds.
        namespace : str | None, optional
            Namespace that owns the custom object.
        labels : Mapping[str, str] | None, optional
            Labels to apply.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply.

        Returns
        -------
        T_co
            Wrapped created object.
        """
        namespace = self._single_namespace(self._namespace(namespace), action="create")
        obj = await self._create(
            kube,
            namespace=namespace,
            body=self._body(
                namespace=namespace,
                name=name,
                spec=spec,
                labels=labels,
                annotations=annotations,
            ),
            timeout=timeout,
        )
        return self._wrap(obj)

    async def create_manifest(
        self,
        kube: Kube,
        *,
        manifest: Mapping[str, object],
        timeout: float,
        namespace: str | None = None,
    ) -> T_co:
        """Create one custom object from a complete manifest.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        manifest : Mapping[str, object]
            Complete Kubernetes manifest.
        timeout : float
            Maximum request budget in seconds.
        namespace : str | None, optional
            Namespace that owns the custom object.

        Returns
        -------
        T_co
            Wrapped created object.
        """
        namespace = self._single_namespace(self._namespace(namespace), action="create")
        self._require_manifest_name(manifest)
        obj = await self._create(
            kube,
            namespace=namespace,
            body=manifest,
            timeout=timeout,
        )
        return self._wrap(obj)

    async def upsert(
        self,
        kube: Kube,
        *,
        name: str,
        spec: _CustomObjectFragment,
        timeout: float,
        namespace: str | None = None,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> T_co:
        """Create or patch one custom object from intent fields.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Custom object name.
        spec : Mapping[str, object] | BaseModel
            Desired custom object ``spec`` payload.
        timeout : float
            Maximum request budget in seconds.
        namespace : str | None, optional
            Namespace that owns the custom object.
        labels : Mapping[str, str] | None, optional
            Labels to apply.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply.

        Returns
        -------
        T_co
            Wrapped created or patched object.
        """
        namespace = self._single_namespace(self._namespace(namespace), action="upsert")
        obj = await self._upsert(
            kube,
            namespace=namespace,
            body=self._body(
                namespace=namespace,
                name=name,
                spec=spec,
                labels=labels,
                annotations=annotations,
            ),
            timeout=timeout,
        )
        return self._wrap(obj)

    async def patch(
        self,
        kube: Kube,
        *,
        name: str,
        body: Mapping[str, object],
        timeout: float,
        namespace: str | None = None,
        context: str | None = None,
    ) -> T_co:
        """Patch one custom object with a raw Kubernetes patch body.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Custom object name.
        body : Mapping[str, object]
            Raw Kubernetes patch body.
        timeout : float
            Maximum request budget in seconds.
        namespace : str | None, optional
            Namespace that owns the custom object.
        context : str | None, optional
            Kubernetes request diagnostic override.

        Returns
        -------
        T_co
            Wrapped object returned by Kubernetes.
        """
        namespace = self._single_namespace(self._namespace(namespace), action="patch")
        obj = await self._patch(
            kube,
            namespace=namespace,
            name=name,
            body=body,
            context=context,
            timeout=timeout,
        )
        return self._wrap(obj)

    async def patch_status(
        self,
        kube: Kube,
        *,
        name: str,
        status: _CustomObjectFragment,
        timeout: float,
        namespace: str | None = None,
    ) -> T_co:
        """Patch one custom-object status payload.

        Returns
        -------
        T_co
            Wrapped object returned by Kubernetes.
        """
        namespace = self._single_namespace(
            self._namespace(namespace),
            action="patch status",
        )
        obj = await self._patch_status(
            kube,
            namespace=namespace,
            name=name,
            status=status,
            timeout=timeout,
        )
        return self._wrap(obj)

    async def delete_by_name(
        self,
        kube: Kube,
        *,
        name: str,
        timeout: float,
        namespace: str | None = None,
    ) -> None:
        """Delete one custom object by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Custom object name.
        timeout : float
            Maximum request budget in seconds.
        namespace : str | None, optional
            Namespace that owns the custom object.
        """
        namespace = self._single_namespace(self._namespace(namespace), action="delete")
        label = self._object_label(name=name, namespace=namespace)
        endpoint, api_kwargs = self._delete_endpoint(
            kube,
            namespace=namespace,
            name=name,
        )
        await self._request(
            kube,
            endpoint=endpoint,
            api_kwargs=api_kwargs,
            timeout=timeout,
            context=f"failed to delete {self.kind} {label}",
            missing_ok=True,
        )

    async def wait_deleted(
        self,
        *,
        label: str,
        timeout: float,
        refresh: Callable[[float], Awaitable[object | None]],
    ) -> None:
        """Wait for a custom object to disappear.

        Parameters
        ----------
        label : str
            Human-readable resource label for diagnostics.
        timeout : float
            Maximum wait budget in seconds.
        refresh : Callable[[float], Awaitable[object | None]]
            Callback that returns the live object or `None`.
        """
        await _wait_until_deleted(label=label, timeout=timeout, refresh=refresh)

    async def _snapshot(
        self,
        kube: Kube,
        *,
        timeout: float,
        labels: Mapping[str, str] | None = None,
        namespace: str | None = None,
    ) -> _CustomObjectSnapshot:
        namespace = self._watch_namespace(namespace)
        payload = await self._snapshot_payload(
            kube,
            namespace=namespace,
            labels=labels,
            timeout=timeout,
        )
        return _CustomObjectSnapshot(
            items=self._snapshot_items(payload),
            resource_version=self._snapshot_resource_version(payload),
        )

    async def _snapshot_payload(
        self,
        kube: Kube,
        *,
        timeout: float,
        labels: Mapping[str, str] | None,
        namespace: str | None,
    ) -> Mapping[str, object]:
        endpoint, api_kwargs = self._list_endpoint(
            kube,
            namespace=namespace,
            label_selector=_label_selector(labels),
        )
        payload = await self._request(
            kube,
            endpoint=endpoint,
            api_kwargs=api_kwargs,
            timeout=timeout,
            context=self._snapshot_context(namespace),
        )
        if payload is None:
            msg = f"Kubernetes {self.kind} list returned no payload"
            raise OSError(msg)
        if not isinstance(payload, Mapping):
            msg = f"malformed Kubernetes {self.kind} list payload"
            raise OSError(msg)
        return cast("Mapping[str, object]", payload)

    def _snapshot_context(self, namespace: str | None) -> str:
        if namespace is None:
            return f"failed to list {self.kind}s"
        return f"failed to list {self.kind}s in namespace {namespace!r}"

    def _snapshot_resource_version(self, payload: Mapping[str, object]) -> str:
        metadata = self._snapshot_metadata(payload)
        resource_version = str(metadata.get("resourceVersion") or "").strip()
        if not resource_version:
            msg = f"Kubernetes {self.kind} list had no resourceVersion"
            raise OSError(msg)
        return resource_version

    def _snapshot_metadata(
        self,
        payload: Mapping[str, object],
    ) -> Mapping[str, object]:
        metadata = payload.get("metadata", {})
        if not isinstance(metadata, Mapping):
            msg = f"malformed Kubernetes {self.kind} list metadata"
            raise OSError(msg)
        return cast("Mapping[str, object]", metadata)

    def _snapshot_items(
        self,
        payload: Mapping[str, object],
    ) -> builtins.list[CustomObject]:
        items = payload.get("items", [])
        if not isinstance(items, list):
            msg = f"malformed Kubernetes {self.kind} list items"
            raise OSError(msg)
        return [self._wrap_object(item) for item in items]

    def _initial_watch_events(
        self,
        snapshot: _CustomObjectSnapshot,
    ) -> tuple[WatchEvent[T_co], ...]:
        return tuple(
            WatchEvent(
                type="ADDED",
                object=self._wrap(item),
                resource_version=item.resource_version or snapshot.resource_version,
                raw_type="ADDED",
            )
            for item in snapshot.items
        )

    async def _watch_once(
        self,
        kube: Kube,
        *,
        timeout: float,
        namespace: str | None,
        labels: Mapping[str, str] | None,
        resource_version: str,
    ) -> AsyncIterator[WatchEvent[CustomObject]]:
        watch_fn, api_kwargs, context = self._watch_endpoint(
            kube,
            namespace=namespace,
        )
        async for event in kube_watch(
            watch_fn,
            wrapper=self._wrap_object,
            timeout=timeout,
            context=context,
            resource_version=resource_version,
            labels=labels,
            api_kwargs=api_kwargs,
        ):
            yield event

    async def _create(
        self,
        kube: Kube,
        *,
        namespace: str | None,
        body: Mapping[str, object],
        timeout: float,
    ) -> CustomObject:
        label = self._manifest_label(body, namespace=namespace)
        endpoint, api_kwargs = self._create_endpoint(
            kube,
            namespace=namespace,
            body=body,
        )
        payload = await self._request(
            kube,
            endpoint=endpoint,
            api_kwargs=api_kwargs,
            timeout=timeout,
            context=f"failed to create {self.kind} {label}",
        )
        return self._wrap_object(payload)

    async def _upsert(
        self,
        kube: Kube,
        *,
        namespace: str | None,
        body: Mapping[str, object],
        timeout: float,
    ) -> CustomObject:
        metadata = body.get("metadata")
        if not isinstance(metadata, Mapping):
            msg = "custom object body must define metadata"
            raise OSError(msg)
        metadata = cast("Mapping[str, object]", metadata)
        name = str(metadata.get("name") or "").strip()
        if not name:
            msg = "custom object body must define metadata.name"
            raise OSError(msg)
        try:
            return await self._create(
                kube,
                namespace=namespace,
                body=body,
                timeout=timeout,
            )
        except OSError as err:
            if not _is_conflict(err):
                raise
        return await self._patch(
            kube,
            namespace=namespace,
            name=name,
            body=body,
            timeout=timeout,
        )

    async def _patch(
        self,
        kube: Kube,
        *,
        namespace: str | None,
        name: str,
        body: Mapping[str, object],
        timeout: float,
        context: str | None = None,
    ) -> CustomObject:
        label = self._object_label(name=name, namespace=namespace)
        endpoint, api_kwargs = self._patch_endpoint(
            kube,
            namespace=namespace,
            name=name,
            body=body,
        )
        payload = await self._request(
            kube,
            endpoint=endpoint,
            api_kwargs=api_kwargs,
            timeout=timeout,
            context=context or f"failed to patch {self.kind} {label}",
        )
        return self._wrap_object(payload)

    async def _patch_status(
        self,
        kube: Kube,
        *,
        namespace: str | None,
        name: str,
        status: _CustomObjectFragment,
        timeout: float,
    ) -> CustomObject:
        label = self._object_label(name=name, namespace=namespace)
        endpoint, api_kwargs = self._patch_status_endpoint(
            kube,
            namespace=namespace,
            name=name,
            status=status,
        )
        payload = await self._request(
            kube,
            endpoint=endpoint,
            api_kwargs=api_kwargs,
            timeout=timeout,
            context=f"failed to patch {self.kind} status {label}",
        )
        return self._wrap_object(payload)

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
        return await kube.run(
            lambda request_timeout: endpoint(
                **api_kwargs,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=context,
            missing_ok=missing_ok,
        )

    def _read_endpoint(
        self,
        kube: Kube,
        *,
        namespace: str | None,
        name: str,
    ) -> tuple[Callable[..., object], dict[str, object]]:
        if self.scope == "cluster":
            return kube.custom.get_cluster_custom_object, {
                "group": self.group,
                "version": self.version,
                "plural": self.plural,
                "name": name,
            }
        return kube.custom.get_namespaced_custom_object, {
            "group": self.group,
            "version": self.version,
            "namespace": cast("str", namespace),
            "plural": self.plural,
            "name": name,
        }

    def _list_endpoint(
        self,
        kube: Kube,
        *,
        namespace: str | None,
        label_selector: str | None,
    ) -> tuple[Callable[..., object], dict[str, object]]:
        if self.scope == "cluster" or namespace is None:
            return kube.custom.list_cluster_custom_object, {
                "group": self.group,
                "version": self.version,
                "plural": self.plural,
                "label_selector": label_selector,
            }
        return kube.custom.list_namespaced_custom_object, {
            "group": self.group,
            "version": self.version,
            "namespace": namespace,
            "plural": self.plural,
            "label_selector": label_selector,
        }

    def _create_endpoint(
        self,
        kube: Kube,
        *,
        namespace: str | None,
        body: Mapping[str, object],
    ) -> tuple[Callable[..., object], dict[str, object]]:
        if self.scope == "cluster":
            return kube.custom.create_cluster_custom_object, {
                "group": self.group,
                "version": self.version,
                "plural": self.plural,
                "body": dict(body),
            }
        return kube.custom.create_namespaced_custom_object, {
            "group": self.group,
            "version": self.version,
            "namespace": cast("str", namespace),
            "plural": self.plural,
            "body": dict(body),
        }

    def _patch_endpoint(
        self,
        kube: Kube,
        *,
        namespace: str | None,
        name: str,
        body: Mapping[str, object],
    ) -> tuple[Callable[..., object], dict[str, object]]:
        if self.scope == "cluster":
            return kube.custom.patch_cluster_custom_object, {
                "group": self.group,
                "version": self.version,
                "plural": self.plural,
                "name": name,
                "body": dict(body),
            }
        return kube.custom.patch_namespaced_custom_object, {
            "group": self.group,
            "version": self.version,
            "namespace": cast("str", namespace),
            "plural": self.plural,
            "name": name,
            "body": dict(body),
        }

    def _patch_status_endpoint(
        self,
        kube: Kube,
        *,
        namespace: str | None,
        name: str,
        status: _CustomObjectFragment,
    ) -> tuple[Callable[..., object], dict[str, object]]:
        if self.scope == "cluster":
            return kube.custom.patch_cluster_custom_object_status, {
                "group": self.group,
                "version": self.version,
                "plural": self.plural,
                "name": name,
                "body": {"status": _custom_object_fragment(status)},
            }
        return kube.custom.patch_namespaced_custom_object_status, {
            "group": self.group,
            "version": self.version,
            "namespace": cast("str", namespace),
            "plural": self.plural,
            "name": name,
            "body": {"status": _custom_object_fragment(status)},
        }

    def _delete_endpoint(
        self,
        kube: Kube,
        *,
        namespace: str | None,
        name: str,
    ) -> tuple[Callable[..., object], dict[str, object]]:
        if self.scope == "cluster":
            return kube.custom.delete_cluster_custom_object, {
                "group": self.group,
                "version": self.version,
                "plural": self.plural,
                "name": name,
            }
        return kube.custom.delete_namespaced_custom_object, {
            "group": self.group,
            "version": self.version,
            "namespace": cast("str", namespace),
            "plural": self.plural,
            "name": name,
        }

    def _watch_endpoint(
        self,
        kube: Kube,
        *,
        namespace: str | None,
    ) -> tuple[Callable[..., object], Mapping[str, object], str]:
        if self.scope == "cluster" or namespace is None:
            return (
                kube.custom.list_cluster_custom_object,
                {
                    "group": self.group,
                    "version": self.version,
                    "plural": self.plural,
                },
                f"failed to watch {self.kind}s",
            )
        return (
            kube.custom.list_namespaced_custom_object,
            {
                "group": self.group,
                "version": self.version,
                "namespace": namespace,
                "plural": self.plural,
            },
            f"failed to watch {self.kind}s in namespace {namespace!r}",
        )

    def _wrap_object(self, payload: object) -> CustomObject:
        return CustomObject.from_payload(
            group=self.group,
            version=self.version,
            plural=self.plural,
            scope=self.scope,
            payload=payload,
        )

    def _wrap(self, obj: CustomObject) -> T_co:
        if self.parser is None:
            if self.payload_parser is None:
                return cast("T_co", obj)
            try:
                return self.payload_parser(obj.payload)
            except (TypeError, ValidationError) as err:
                if self.payload_error_context is None:
                    raise
                msg = f"malformed {self.payload_error_context}: {err}"
                raise OSError(msg) from err
        if self.payload_parser is not None:
            msg = f"{self.kind} descriptor cannot define both parsers"
            raise ValueError(msg)
        return self.parser(obj)

    def _spec_schema(self) -> Mapping[str, object] | None:
        if self.spec_schema is not None:
            return self.spec_schema
        if self.spec_model is None:
            return None
        return _schema_from_model(
            self.spec_model,
            include_defaults=self.spec_schema_include_defaults,
            overrides=self.spec_schema_overrides,
        )

    def _status_schema(self) -> Mapping[str, object] | None:
        if self.status_schema is not None:
            return self.status_schema
        if self.status_model is None:
            return None
        return _schema_from_model(
            self.status_model,
            include_defaults=self.status_schema_include_defaults,
            overrides=self.status_schema_overrides,
        )

    def _namespace(self, namespace: str | None) -> str | None:
        return namespace if namespace is not None else self.default_namespace

    def _selector(self, labels: Mapping[str, str] | None) -> Mapping[str, str] | None:
        if not self.labels:
            return labels
        merged = dict(self.labels)
        merged.update(labels or {})
        return merged

    def _single_namespace(self, namespace: str | None, *, action: str) -> str | None:
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

    def _watch_namespace(self, namespace: str | None) -> str | None:
        namespace = namespace.strip() if namespace is not None else ""
        if self.scope == "cluster":
            if namespace:
                msg = f"{self.kind} is cluster-scoped; cannot watch in namespace"
                raise ValueError(msg)
            return None
        return namespace or None

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

    def _manifest_label(
        self,
        body: Mapping[str, object],
        *,
        namespace: str | None,
    ) -> str:
        metadata = body.get("metadata")
        if not isinstance(metadata, Mapping):
            return self.kind
        metadata = cast("Mapping[str, object]", metadata)
        name = str(metadata.get("name") or "").strip()
        return self._object_label(name=name, namespace=namespace)

    def _require_manifest_name(self, body: Mapping[str, object]) -> None:
        metadata = body.get("metadata")
        if not isinstance(metadata, Mapping):
            msg = "custom object manifest must define metadata"
            raise OSError(msg)
        metadata = cast("Mapping[str, object]", metadata)
        name = str(metadata.get("name") or "").strip()
        if not name:
            msg = "custom object manifest must define metadata.name"
            raise OSError(msg)

    def _object_label(self, *, name: str, namespace: str | None) -> str:
        return f"{namespace}/{name}" if namespace else name


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
