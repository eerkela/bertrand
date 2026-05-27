"""Wrappers and raw adapters for Kubernetes custom objects."""

from __future__ import annotations

import math
from collections.abc import AsyncIterator, Awaitable, Callable, Collection, Mapping
from dataclasses import dataclass, field
from datetime import UTC, datetime
from types import MappingProxyType
from typing import TYPE_CHECKING, Any, Self, cast

from pydantic import BaseModel, ConfigDict, Field, ValidationError

from bertrand.env.git import Deadline
from bertrand.env.kube.crd import CustomResourceDefinition

from .api._helpers import (
    _is_conflict,
    _is_missing_api_resource,
    _is_not_found,
    _label_selector,
    _normalized_namespaces,
    _wait_until_deleted,
)
from .api.resource import ResourceScope
from .api.watch import WatchEvent, WatchExpired
from .api.watch import watch as kube_watch

if TYPE_CHECKING:
    import builtins

    from .api.client import Kube

type CustomObjectScope = ResourceScope
type _CustomObjectFragment = Mapping[str, object] | BaseModel


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
    """Generic read-only wrapper around one Kubernetes custom-object payload.

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
        """Initialize one custom-object wrapper."""
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
            Parsed UTC timestamp, or ``None`` when unavailable or malformed.
        """
        return _parse_kubernetes_datetime(str(value or ""))


@dataclass(frozen=True)
class CustomObjectResource[T_co]:
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
        Context used when wrapping Pydantic validation errors as ``OSError``.
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
            If the timeout is non-positive or establishment exceeds the budget.
        ValueError
            If this descriptor does not own a CRD.
        """
        spec_schema = self._spec_schema()
        if self.singular is None or spec_schema is None:
            msg = f"{self.kind} descriptor does not own a CRD"
            raise ValueError(msg)
        message = f"{self.kind} CRD timeout must be non-negative"
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
            labels=self.labels,
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

        Returns
        -------
        T_co | None
            Wrapped custom object, or ``None`` when absent.

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or another API error.
        """
        namespace = self._single_namespace(namespace, action="read")
        label = self._object_label(name=name, namespace=namespace)
        try:
            if self.scope == "cluster":
                payload = await kube.run(
                    lambda request_timeout: kube.custom.get_cluster_custom_object(
                        group=self.group,
                        version=self.version,
                        plural=self.plural,
                        name=name,
                        _request_timeout=request_timeout,
                    ),
                    timeout=timeout,
                    context=context or f"failed to read {self.kind} {label}",
                    missing_ok=False,
                )
            else:
                payload = await kube.run(
                    lambda request_timeout: kube.custom.get_namespaced_custom_object(
                        group=self.group,
                        version=self.version,
                        namespace=cast("str", namespace),
                        plural=self.plural,
                        name=name,
                        _request_timeout=request_timeout,
                    ),
                    timeout=timeout,
                    context=context or f"failed to read {self.kind} {label}",
                    missing_ok=False,
                )
        except OSError as err:
            if _is_not_found(err) and not _is_missing_api_resource(err):
                return None
            raise
        return self._wrap_object_payload(payload)

    async def list(
        self,
        kube: Kube,
        *,
        timeout: float,
        namespace: str | None = None,
        namespaces: Collection[str] | None = None,
        labels: Mapping[str, str] | None = None,
        field_selector: str | None = None,
    ) -> builtins.list[T_co]:
        """List custom objects with optional namespace and label filtering.

        Returns
        -------
        list[T_co]
            Wrapped custom objects matching the filters.
        """
        selected = self._list_namespaces(namespace=namespace, namespaces=namespaces)
        payloads = await self._list_payloads(
            kube,
            timeout=timeout,
            namespaces=selected,
            label_selector=_label_selector(self._selector(labels)),
            field_selector=field_selector,
        )
        return self._wrap_list_payloads(payloads)

    async def watch(
        self,
        kube: Kube,
        *,
        timeout: float,
        labels: Mapping[str, str] | None = None,
        namespace: str | None = None,
        resource_version: str | None = None,
        emit_initial: bool = False,
        field_selector: str | None = None,
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
        namespace = self._watch_namespace(namespace)
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
                items, current_version = await self._snapshot(
                    kube,
                    namespace=namespace,
                    labels=labels,
                    field_selector=field_selector,
                    timeout=remaining,
                )
                if can_emit_initial:
                    for event in self._initial_watch_events(
                        items,
                        resource_version=current_version,
                    ):
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
                    field_selector=field_selector,
                ):
                    if event.resource_version:
                        current_version = event.resource_version
                    yield event
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

        Returns
        -------
        T_co
            Wrapped created object.
        """
        namespace = self._single_namespace(namespace, action="create")
        return await self.create_manifest(
            kube,
            manifest=self._body(
                namespace=namespace,
                name=name,
                spec=spec,
                labels=labels,
                annotations=annotations,
            ),
            timeout=timeout,
            namespace=namespace,
        )

    async def create_manifest(
        self,
        kube: Kube,
        *,
        manifest: Mapping[str, object],
        timeout: float,
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
        namespace = self._single_namespace(namespace, action="create")
        label = self._manifest_label(manifest, namespace=namespace, name=name)
        if self.scope == "cluster":
            payload = await kube.run(
                lambda request_timeout: kube.custom.create_cluster_custom_object(
                    group=self.group,
                    version=self.version,
                    plural=self.plural,
                    body=dict(manifest),
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=context or f"failed to create {self.kind} {label}",
                missing_ok=True,
            )
        else:
            payload = await kube.run(
                lambda request_timeout: kube.custom.create_namespaced_custom_object(
                    group=self.group,
                    version=self.version,
                    namespace=cast("str", namespace),
                    plural=self.plural,
                    body=dict(manifest),
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=context or f"failed to create {self.kind} {label}",
                missing_ok=True,
            )
        return self._wrap_object_payload(payload)

    async def upsert(
        self,
        kube: Kube,
        *,
        name: str,
        timeout: float,
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
            If neither ``spec`` nor ``manifest`` is provided, or the API call fails.
        """
        namespace = self._single_namespace(namespace, action="upsert")
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
            if self.scope == "cluster":
                payload = await kube.run(
                    lambda request_timeout: kube.custom.create_cluster_custom_object(
                        group=self.group,
                        version=self.version,
                        plural=self.plural,
                        body=body,
                        _request_timeout=request_timeout,
                    ),
                    timeout=timeout,
                    context=f"failed to create {self.kind} {label}",
                    missing_ok=False,
                )
            else:
                payload = await kube.run(
                    lambda request_timeout: kube.custom.create_namespaced_custom_object(
                        group=self.group,
                        version=self.version,
                        namespace=cast("str", namespace),
                        plural=self.plural,
                        body=body,
                        _request_timeout=request_timeout,
                    ),
                    timeout=timeout,
                    context=f"failed to create {self.kind} {label}",
                    missing_ok=False,
                )
        except OSError as err:
            if not _is_conflict(err):
                raise
            if self.scope == "cluster":
                payload = await kube.run(
                    lambda request_timeout: kube.custom.patch_cluster_custom_object(
                        group=self.group,
                        version=self.version,
                        plural=self.plural,
                        name=name,
                        body=body,
                        _request_timeout=request_timeout,
                    ),
                    timeout=timeout,
                    context=f"failed to patch {self.kind} {label}",
                    missing_ok=False,
                )
            else:
                payload = await kube.run(
                    lambda request_timeout: kube.custom.patch_namespaced_custom_object(
                        group=self.group,
                        version=self.version,
                        namespace=cast("str", namespace),
                        plural=self.plural,
                        name=name,
                        body=body,
                        _request_timeout=request_timeout,
                    ),
                    timeout=timeout,
                    context=f"failed to patch {self.kind} {label}",
                    missing_ok=False,
                )
        return self._wrap_object_payload(payload)

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

        Returns
        -------
        T_co
            Wrapped patched object.
        """
        namespace = self._single_namespace(namespace, action="patch")
        label = self._object_label(name=name, namespace=namespace)
        if self.scope == "cluster":
            payload = await kube.run(
                lambda request_timeout: kube.custom.patch_cluster_custom_object(
                    group=self.group,
                    version=self.version,
                    plural=self.plural,
                    name=name,
                    body=dict(body),
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=context or f"failed to patch {self.kind} {label}",
                missing_ok=False,
            )
        else:
            payload = await kube.run(
                lambda request_timeout: kube.custom.patch_namespaced_custom_object(
                    group=self.group,
                    version=self.version,
                    namespace=cast("str", namespace),
                    plural=self.plural,
                    name=name,
                    body=dict(body),
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=context or f"failed to patch {self.kind} {label}",
                missing_ok=False,
            )
        return self._wrap_object_payload(payload)

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
        namespace = self._single_namespace(namespace, action="patch status")
        label = self._object_label(name=name, namespace=namespace)
        body = {"status": _custom_object_fragment(status)}
        if self.scope == "cluster":
            payload = await kube.run(
                lambda request_timeout: kube.custom.patch_cluster_custom_object_status(
                    group=self.group,
                    version=self.version,
                    plural=self.plural,
                    name=name,
                    body=body,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=f"failed to patch {self.kind} status {label}",
                missing_ok=False,
            )
        else:
            payload = await kube.run(
                lambda request_timeout: (
                    kube.custom.patch_namespaced_custom_object_status(
                        group=self.group,
                        version=self.version,
                        namespace=cast("str", namespace),
                        plural=self.plural,
                        name=name,
                        body=body,
                        _request_timeout=request_timeout,
                    )
                ),
                timeout=timeout,
                context=f"failed to patch {self.kind} status {label}",
                missing_ok=False,
            )
        return self._wrap_object_payload(payload)

    async def delete_by_name(
        self,
        kube: Kube,
        *,
        name: str,
        timeout: float,
        namespace: str | None = None,
    ) -> None:
        """Delete one custom object by name."""
        namespace = self._single_namespace(namespace, action="delete")
        label = self._object_label(name=name, namespace=namespace)
        if self.scope == "cluster":
            await kube.run(
                lambda request_timeout: kube.custom.delete_cluster_custom_object(
                    group=self.group,
                    version=self.version,
                    plural=self.plural,
                    name=name,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=f"failed to delete {self.kind} {label}",
                missing_ok=True,
            )
        else:
            await kube.run(
                lambda request_timeout: kube.custom.delete_namespaced_custom_object(
                    group=self.group,
                    version=self.version,
                    namespace=cast("str", namespace),
                    plural=self.plural,
                    name=name,
                    _request_timeout=request_timeout,
                ),
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
        """Wait until a custom object disappears."""
        await _wait_until_deleted(
            label=label,
            timeout=timeout,
            refresh=refresh,
        )

    async def _watch_once(
        self,
        kube: Kube,
        *,
        timeout: float,
        namespace: str | None,
        labels: Mapping[str, str] | None,
        resource_version: str | None,
        field_selector: str | None,
    ) -> AsyncIterator[WatchEvent[T_co]]:
        if self.scope == "cluster" or namespace is None:
            endpoint = kube.custom.list_cluster_custom_object
            api_kwargs = {
                "group": self.group,
                "version": self.version,
                "plural": self.plural,
            }
            context = f"failed to watch {self.kind}s"
        else:
            endpoint = kube.custom.list_namespaced_custom_object
            api_kwargs = {
                "group": self.group,
                "version": self.version,
                "namespace": namespace,
                "plural": self.plural,
            }
            context = f"failed to watch {self.kind}s in namespace {namespace!r}"
        async for event in kube_watch(
            endpoint,
            wrapper=self._wrap_object_payload,
            timeout=timeout,
            context=context,
            resource_version=resource_version,
            labels=labels,
            field_selector=field_selector,
            api_kwargs=api_kwargs,
        ):
            yield event

    async def _snapshot(
        self,
        kube: Kube,
        *,
        timeout: float,
        labels: Mapping[str, str] | None = None,
        namespace: str | None = None,
        field_selector: str | None = None,
    ) -> tuple[builtins.list[CustomObject], str]:
        namespace = self._watch_namespace(namespace)
        payloads = await self._list_payloads(
            kube,
            namespaces=(namespace,) if namespace else None,
            label_selector=_label_selector(labels),
            field_selector=field_selector,
            timeout=timeout,
        )
        payload = payloads[0] if payloads else None
        if payload is None:
            msg = f"Kubernetes {self.kind} list returned no payload"
            raise OSError(msg)
        if not isinstance(payload, Mapping):
            msg = f"malformed Kubernetes {self.kind} list payload"
            raise OSError(msg)
        snapshot = cast("Mapping[str, object]", payload)
        return self._snapshot_items(snapshot), self._snapshot_resource_version(snapshot)

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
        items: builtins.list[CustomObject],
        *,
        resource_version: str,
    ) -> tuple[WatchEvent[T_co], ...]:
        return tuple(
            WatchEvent(
                type="ADDED",
                object=self._wrap(item),
                resource_version=item.resource_version or resource_version,
                raw_type="ADDED",
            )
            for item in items
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
        field_selector = field_selector.strip() if field_selector is not None else ""
        field_selector = field_selector or None
        if self.scope == "cluster":
            return [
                await kube.run(
                    lambda request_timeout: kube.custom.list_cluster_custom_object(
                        group=self.group,
                        version=self.version,
                        plural=self.plural,
                        label_selector=label_selector,
                        field_selector=field_selector,
                        _request_timeout=request_timeout,
                    ),
                    timeout=timeout,
                    context=f"failed to list {self.kind}s",
                    missing_ok=False,
                )
            ]
        normalized = _normalized_namespaces(namespaces)
        if normalized is None:
            return [
                await kube.run(
                    lambda request_timeout: kube.custom.list_cluster_custom_object(
                        group=self.group,
                        version=self.version,
                        plural=self.plural,
                        label_selector=label_selector,
                        field_selector=field_selector,
                        _request_timeout=request_timeout,
                    ),
                    timeout=timeout,
                    context=f"failed to list {self.kind}s",
                    missing_ok=False,
                )
            ]
        return [
            await kube.run(
                lambda request_timeout, namespace=namespace: (
                    kube.custom.list_namespaced_custom_object(
                        group=self.group,
                        version=self.version,
                        namespace=namespace,
                        plural=self.plural,
                        label_selector=label_selector,
                        field_selector=field_selector,
                        _request_timeout=request_timeout,
                    )
                ),
                timeout=timeout,
                context=f"failed to list {self.kind}s in namespace {namespace!r}",
                missing_ok=False,
            )
            for namespace in normalized
        ]

    def _single_namespace(self, namespace: str | None, *, action: str) -> str | None:
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

    def _watch_namespace(self, namespace: str | None) -> str | None:
        namespace = self._default_namespace(namespace)
        namespace = namespace.strip() if namespace is not None else ""
        if self.scope == "cluster":
            if namespace:
                msg = f"{self.kind} is cluster-scoped; cannot watch in namespace"
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

    def _wrap_list_payloads(
        self,
        payloads: builtins.list[object | None],
    ) -> builtins.list[T_co]:
        out: builtins.list[T_co] = []
        for payload in payloads:
            if payload is None:
                msg = f"Kubernetes {self.kind} list returned no payload"
                raise OSError(msg)
            if not isinstance(payload, Mapping):
                msg = f"malformed Kubernetes {self.kind} list payload"
                raise OSError(msg)
            out.extend(
                self._wrap(item)
                for item in self._snapshot_items(cast("Mapping[str, object]", payload))
            )
        return out

    def _wrap_object_payload(self, payload: object) -> T_co:
        return self._wrap(self._wrap_object(payload))

    def _wrap_object(self, payload: object) -> CustomObject:
        return CustomObject.from_payload(
            group=self.group,
            version=self.version,
            plural=self.plural,
            scope=self.scope,
            payload=payload,
        )

    def _wrap(self, obj: CustomObject) -> T_co:
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
