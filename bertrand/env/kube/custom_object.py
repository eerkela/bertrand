"""Wrappers and raw adapters for Kubernetes custom objects."""

from __future__ import annotations

import asyncio
import math
from collections.abc import AsyncIterator, Callable, Collection, Mapping
from dataclasses import dataclass, field
from datetime import UTC, datetime
from types import MappingProxyType
from typing import TYPE_CHECKING, Any, ClassVar, Literal, Self, TypeVar, cast

from kubernetes import client as kube_client
from pydantic import BaseModel, ConfigDict, Field

from bertrand.env.git import EMPTY_MAPPING, Deadline, until
from bertrand.env.kube.crd import (
    CustomResourceDefinition,
    CustomResourceDefinitionManifest,
)

from .api.client import Kube
from .api.resource import (
    RESOURCE_WAIT_POLL_INTERVAL_SECONDS,
    WatchEvent,
    _KubeListPayload,
    _KubeManifest,
    _ResourceAPI,
    watch_collection,
    watch_stream,
)

if TYPE_CHECKING:
    import builtins

type CustomObjectScope = Literal["cluster", "namespaced"]
type _CustomObjectFragment = Mapping[str, object] | BaseModel


def _normalized_namespaces(
    namespaces: Collection[str] | None,
) -> tuple[str, ...] | None:
    if namespaces is None:
        return None
    normalized = {namespace.strip() for namespace in namespaces}
    normalized.discard("")
    return tuple(sorted(normalized))


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
    def created_at(self) -> datetime | None:
        """Return this custom object's creation timestamp.

        Returns
        -------
        datetime | None
            Parsed UTC `metadata.creationTimestamp`, or `None` when unavailable or
            malformed.
        """
        return _parse_kubernetes_datetime(
            str(self.metadata.get("creationTimestamp") or "")
        )

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


@dataclass(frozen=True)
class _CustomListMeta:
    resource_version: str | None


@dataclass(frozen=True)
class _CustomListPayload:
    metadata: _CustomListMeta | None
    items: list[Mapping[str, Any]]


@dataclass(frozen=True)
class _CustomResourceAPI:
    """Resource API binding for dynamic Kubernetes custom objects.

    Parameters
    ----------
    config : _CustomResourceConfig
        Custom resource declaration installed by `@custom_resource`.
    """

    config: _CustomResourceConfig

    @property
    def kind(self) -> str:
        """Return the diagnostic Kubernetes kind name."""
        return self.config.kind

    @property
    def namespaced(self) -> bool:
        """Return whether this custom resource is namespace-scoped."""
        return self.config.scope == "namespaced"

    @property
    def can_create(self) -> bool:
        """Return whether this binding supports generic create."""
        return True

    @property
    def can_patch(self) -> bool:
        """Return whether this binding supports generic patch."""
        return True

    @property
    def can_delete(self) -> bool:
        """Return whether this binding supports generic delete."""
        return True

    def items(
        self,
        payload: _KubeListPayload[Mapping[str, Any]],
    ) -> builtins.list[Mapping[str, Any]]:
        """Return items from one custom-object list payload.

        Parameters
        ----------
        payload : _KubeListPayload[Mapping[str, Any]]
            Validated custom-object list payload.

        Returns
        -------
        list[Mapping[str, Any]]
            Custom-object payload mappings from the list.
        """
        return payload.items

    def list_resource_version(
        self,
        payload: _KubeListPayload[Mapping[str, Any]],
    ) -> str:
        """Return a custom-object list payload's resource version.

        Parameters
        ----------
        payload : _KubeListPayload[Mapping[str, Any]]
            Validated custom-object list payload.

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

    def name(self, payload: Mapping[str, Any]) -> str:
        """Return a custom-object payload's metadata name.

        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        return str(self._metadata(payload).get("name") or "").strip()

    def namespace(self, payload: Mapping[str, Any]) -> str:
        """Return a custom-object payload's metadata namespace.

        Returns
        -------
        str
            Trimmed `metadata.namespace`, or an empty string when unavailable.
        """
        return str(self._metadata(payload).get("namespace") or "").strip()

    def labels(self, payload: Mapping[str, Any]) -> Mapping[str, str]:
        """Return a custom-object payload's labels.

        Returns
        -------
        Mapping[str, str]
            Read-only labels, or an empty mapping when unavailable.
        """
        labels = self._metadata(payload).get("labels", {})
        if not isinstance(labels, Mapping):
            return MappingProxyType({})
        return MappingProxyType({str(key): str(value) for key, value in labels.items()})

    def annotations(self, payload: Mapping[str, Any]) -> Mapping[str, str]:
        """Return a custom-object payload's annotations.

        Returns
        -------
        Mapping[str, str]
            Read-only annotations, or an empty mapping when unavailable.
        """
        annotations = self._metadata(payload).get("annotations", {})
        if not isinstance(annotations, Mapping):
            return MappingProxyType({})
        return MappingProxyType(
            {str(key): str(value) for key, value in annotations.items()}
        )

    def resource_version(self, payload: Mapping[str, Any]) -> str:
        """Return a custom-object payload's resource version.

        Returns
        -------
        str
            Trimmed `metadata.resourceVersion`, or an empty string when unavailable.
        """
        return str(self._metadata(payload).get("resourceVersion") or "").strip()

    def uid(self, payload: Mapping[str, Any]) -> str:
        """Return a custom-object payload's UID.

        Returns
        -------
        str
            Trimmed `metadata.uid`, or an empty string when unavailable.
        """
        return str(self._metadata(payload).get("uid") or "").strip()

    def created_at(self, payload: Mapping[str, Any]) -> datetime | None:
        """Return a custom-object payload's creation timestamp.

        Returns
        -------
        datetime | None
            Parsed UTC `metadata.creationTimestamp`, or `None` when unavailable or
            malformed.
        """
        return _parse_kubernetes_datetime(
            str(self._metadata(payload).get("creationTimestamp") or "")
        )

    async def read(
        self,
        kube: Kube,
        *,
        name: str,
        namespace: str | None,
        deadline: Deadline,
    ) -> Mapping[str, Any] | None:
        """Read one custom object by name.

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
        Mapping[str, Any] | None
            Custom-object payload, or `None` when the object is absent.

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or another API error.
        """
        namespace, name, label = self._identity(
            name=name,
            namespace=namespace,
            action="read",
        )
        api = kube_client.CustomObjectsApi(kube.client)
        try:
            if self.namespaced:
                payload = await kube.run(
                    lambda request_timeout: api.get_namespaced_custom_object(
                        group=self.config.group,
                        version=self.config.version,
                        namespace=cast("str", namespace),
                        plural=self.config.plural,
                        name=name,
                        _request_timeout=request_timeout,
                    ),
                    deadline=deadline,
                    context=f"failed to read {self.kind} {label}",
                    missing_ok=False,
                )
            else:
                payload = await kube.run(
                    lambda request_timeout: api.get_cluster_custom_object(
                        group=self.config.group,
                        version=self.config.version,
                        plural=self.config.plural,
                        name=name,
                        _request_timeout=request_timeout,
                    ),
                    deadline=deadline,
                    context=f"failed to read {self.kind} {label}",
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
        return self._payload(
            payload,
            label=f"custom object payload for {self.config.plural}",
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
    ) -> builtins.list[_KubeListPayload[Mapping[str, Any]]]:
        """List custom objects with rendered selector filters.

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
        list[_KubeListPayload[Mapping[str, Any]]]
            Validated custom-object list payloads.
        """
        selected = self.namespace_filter(
            action="list",
            namespace=namespace,
            namespaces=namespaces,
        )
        normalized = _normalized_namespaces(selected)
        api = kube_client.CustomObjectsApi(kube.client)
        field_selector = field_selector.strip()

        if self.config.scope == "cluster" or normalized is None:
            payload = await kube.run(
                lambda request_timeout: api.list_cluster_custom_object(
                    group=self.config.group,
                    version=self.config.version,
                    plural=self.config.plural,
                    label_selector=label_selector or None,
                    field_selector=field_selector or None,
                    _request_timeout=request_timeout,
                ),
                deadline=deadline,
                context=self.collection_context("list", None),
                missing_ok=False,
            )
            return [self._list_payload(payload)]

        if not normalized:
            return []
        payloads = await asyncio.gather(
            *(
                kube.run(
                    lambda request_timeout, namespace=namespace: (
                        api.list_namespaced_custom_object(
                            group=self.config.group,
                            version=self.config.version,
                            namespace=namespace,
                            plural=self.config.plural,
                            label_selector=label_selector or None,
                            field_selector=field_selector or None,
                            _request_timeout=request_timeout,
                        )
                    ),
                    deadline=deadline,
                    context=self.collection_context("list", namespace),
                    missing_ok=False,
                )
                for namespace in normalized
            )
        )
        return [self._list_payload(payload) for payload in payloads]

    async def create(
        self,
        kube: Kube,
        *,
        name: str,
        namespace: str | None,
        body: Mapping[str, Any],
        deadline: Deadline,
    ) -> Mapping[str, Any]:
        """Create one custom object from a manifest body.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Kubernetes object name.
        namespace : str | None
            Namespace for namespaced resources.
        body : Mapping[str, Any]
            Custom-object manifest body.
        deadline : Deadline
            Maximum request budget in seconds.

        Returns
        -------
        Mapping[str, Any]
            Created custom-object payload.
        """
        namespace, name, body = self.manifest_body(
            name=name,
            namespace=namespace,
            body=body,
            action="create",
        )
        label = self.object_label(name=name, namespace=namespace)
        api = kube_client.CustomObjectsApi(kube.client)
        if self.namespaced:
            payload = await kube.run(
                lambda request_timeout: api.create_namespaced_custom_object(
                    group=self.config.group,
                    version=self.config.version,
                    namespace=cast("str", namespace),
                    plural=self.config.plural,
                    body=dict(body),
                    _request_timeout=request_timeout,
                ),
                deadline=deadline,
                context=f"failed to create {self.kind} {label}",
                missing_ok=False,
            )
        else:
            payload = await kube.run(
                lambda request_timeout: api.create_cluster_custom_object(
                    group=self.config.group,
                    version=self.config.version,
                    plural=self.config.plural,
                    body=dict(body),
                    _request_timeout=request_timeout,
                ),
                deadline=deadline,
                context=f"failed to create {self.kind} {label}",
                missing_ok=False,
            )
        return self._payload(
            payload,
            label=f"custom object payload for {self.config.plural}",
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
    ) -> Mapping[str, Any]:
        """Patch one custom object.

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
        Mapping[str, Any]
            Patched custom-object payload.
        """
        namespace, name, _ = self._identity(
            name=name,
            namespace=namespace,
            action="patch",
        )
        api = kube_client.CustomObjectsApi(kube.client)
        if self.namespaced:
            payload = await kube.run(
                lambda request_timeout: api.patch_namespaced_custom_object(
                    group=self.config.group,
                    version=self.config.version,
                    namespace=cast("str", namespace),
                    plural=self.config.plural,
                    name=name,
                    body=dict(body),
                    _request_timeout=request_timeout,
                ),
                deadline=deadline,
                context=context,
                missing_ok=False,
            )
        else:
            payload = await kube.run(
                lambda request_timeout: api.patch_cluster_custom_object(
                    group=self.config.group,
                    version=self.config.version,
                    plural=self.config.plural,
                    name=name,
                    body=dict(body),
                    _request_timeout=request_timeout,
                ),
                deadline=deadline,
                context=context,
                missing_ok=False,
            )
        return self._payload(
            payload,
            label=f"custom object payload for {self.config.plural}",
        )

    async def patch_status(
        self,
        kube: Kube,
        *,
        name: str,
        namespace: str | None,
        body: Mapping[str, object],
        deadline: Deadline,
        context: str,
    ) -> Mapping[str, Any]:
        """Patch one custom object's status payload.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Kubernetes object name.
        namespace : str | None
            Namespace for namespaced resources.
        body : Mapping[str, object]
            Kubernetes status patch body.
        deadline : Deadline
            Maximum request budget in seconds.
        context : str
            Error context for the Kubernetes request.

        Returns
        -------
        Mapping[str, Any]
            Patched custom-object payload.
        """
        namespace, name, _ = self._identity(
            name=name,
            namespace=namespace,
            action="status patch",
        )
        api = kube_client.CustomObjectsApi(kube.client)
        if self.namespaced:
            payload = await kube.run(
                lambda request_timeout: api.patch_namespaced_custom_object_status(
                    group=self.config.group,
                    version=self.config.version,
                    namespace=cast("str", namespace),
                    plural=self.config.plural,
                    name=name,
                    body=dict(body),
                    _request_timeout=request_timeout,
                ),
                deadline=deadline,
                context=context,
                missing_ok=False,
            )
        else:
            payload = await kube.run(
                lambda request_timeout: api.patch_cluster_custom_object_status(
                    group=self.config.group,
                    version=self.config.version,
                    plural=self.config.plural,
                    name=name,
                    body=dict(body),
                    _request_timeout=request_timeout,
                ),
                deadline=deadline,
                context=context,
                missing_ok=False,
            )
        return self._payload(
            payload,
            label=f"custom object payload for {self.config.plural}",
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
        """Delete one custom object.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Kubernetes object name.
        namespace : str | None
            Namespace for namespaced resources.
        body : kubernetes.client.V1DeleteOptions | None
            Ignored for custom objects.
        deadline : Deadline
            Maximum request budget in seconds.

        Returns
        -------
        object | None
            Kubernetes deletion response, or `None` when absent.
        """
        del body
        namespace, name, label = self._identity(
            name=name,
            namespace=namespace,
            action="delete",
        )
        api = kube_client.CustomObjectsApi(kube.client)
        if self.namespaced:
            return await kube.run(
                lambda request_timeout: api.delete_namespaced_custom_object(
                    group=self.config.group,
                    version=self.config.version,
                    namespace=cast("str", namespace),
                    plural=self.config.plural,
                    name=name,
                    _request_timeout=request_timeout,
                ),
                deadline=deadline,
                context=f"failed to delete {self.kind} {label}",
                missing_ok=True,
            )
        return await kube.run(
            lambda request_timeout: api.delete_cluster_custom_object(
                group=self.config.group,
                version=self.config.version,
                plural=self.config.plural,
                name=name,
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
        """Return the list-style method used for custom-object watches.

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
        """
        selected = self.namespace_filter(action="watch", namespace=namespace)
        normalized = _normalized_namespaces(selected)
        namespace = None if normalized is None else next(iter(normalized), None)
        api = kube_client.CustomObjectsApi(kube.client)
        api_kwargs: dict[str, Any] = {
            "group": self.config.group,
            "version": self.config.version,
            "plural": self.config.plural,
        }
        if namespace is None:
            return (
                api.list_cluster_custom_object,
                api_kwargs,
                self.collection_context("watch", None),
            )
        api_kwargs["namespace"] = namespace
        return (
            api.list_namespaced_custom_object,
            api_kwargs,
            self.collection_context("watch", namespace),
        )

    def label_selector(self, labels: Mapping[str, str]) -> str:
        """Return caller labels merged with descriptor labels as a selector.

        Returns
        -------
        str
            Rendered Kubernetes label selector.
        """
        merged = dict(self.config.labels)
        merged.update(labels)
        return ",".join(f"{key}={value}" for key, value in merged.items())

    def namespace_filter(
        self,
        *,
        action: Literal["list", "watch"],
        namespace: str | None,
        namespaces: Collection[str] | None = None,
    ) -> Collection[str] | None:
        """Validate namespace collection filters for list and watch operations.

        Returns
        -------
        Collection[str] | None
            Normalized namespace selection, or `None` for all namespaces.

        Raises
        ------
        ValueError
            If namespace filters are invalid for this resource's scope.
        """
        namespace = self.default_namespace(namespace)
        namespace = namespace.strip() if namespace is not None else ""
        if self.config.scope == "cluster":
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

    def manifest_body(
        self,
        *,
        name: str,
        namespace: str | None,
        body: Mapping[str, Any],
        action: Literal["create", "upsert"],
    ) -> tuple[str | None, str, dict[str, object]]:
        """Validate and normalize a custom-object manifest body.

        Returns
        -------
        tuple[str | None, str, dict[str, object]]
            Namespace, name, and normalized manifest body.

        Raises
        ------
        OSError
            If the manifest identity or metadata is malformed.
        """
        name = name.strip()
        if not name:
            msg = f"{self.kind} {action} requires a name"
            raise OSError(msg)
        namespace = self._identity_namespace(namespace, action=action)
        raw = dict(body)
        metadata = raw.get("metadata")
        if not isinstance(metadata, Mapping):
            msg = f"{self.kind} manifest must define metadata"
            raise OSError(msg)
        metadata = dict(cast("Mapping[str, object]", metadata))
        body_name = str(metadata.get("name") or "").strip()
        if body_name != name:
            msg = (
                f"{self.kind} manifest metadata.name {body_name!r} does not match "
                f"intent name {name!r}"
            )
            raise OSError(msg)
        body_namespace = str(metadata.get("namespace") or "").strip()
        if self.config.scope == "cluster":
            if body_namespace:
                msg = f"{self.kind} manifest must not define metadata.namespace"
                raise OSError(msg)
            metadata.pop("namespace", None)
        elif body_namespace != namespace:
            msg = (
                f"{self.kind} manifest metadata.namespace {body_namespace!r} does "
                f"not match intent namespace {namespace!r}"
            )
            raise OSError(msg)
        labels = metadata.get("labels", {})
        if labels is None:
            labels = {}
        if not isinstance(labels, Mapping):
            msg = f"{self.kind} manifest metadata.labels must be a mapping"
            raise OSError(msg)
        merged_labels = dict(self.config.labels)
        merged_labels.update({str(key): str(value) for key, value in labels.items()})
        metadata["labels"] = merged_labels
        raw["metadata"] = metadata
        return namespace, name, raw

    def resource_identity(
        self,
        payload: Mapping[str, Any],
        *,
        action: str,
    ) -> tuple[str | None, str]:
        """Return a custom-object payload's required Kubernetes identity.

        Returns
        -------
        tuple[str | None, str]
            Namespace and name for namespaced resources, or `None` and name for
            cluster-scoped resources.

        Raises
        ------
        OSError
            If the payload metadata is missing required identity fields.
        """
        name = self.name(payload)
        namespace = self.namespace(payload)
        if self.config.scope == "cluster":
            if not name:
                msg = f"cannot {action} with missing metadata.name"
                raise OSError(msg)
            return None, name
        if not namespace or not name:
            msg = f"cannot {action} with missing metadata.name/namespace"
            raise OSError(msg)
        return namespace, name

    def object_label(self, *, name: str, namespace: str | None) -> str:
        """Return a human-readable custom-object identity label.

        Returns
        -------
        str
            `namespace/name` for namespaced resources, otherwise `name`.
        """
        return f"{namespace}/{name}" if namespace else name

    def collection_context(
        self,
        action: Literal["list", "watch"],
        namespace: str | None,
    ) -> str:
        """Return an error context for collection operations.

        Returns
        -------
        str
            Human-readable Kubernetes request context.
        """
        context = f"failed to {action} {self.kind}s"
        return context if namespace is None else f"{context} in namespace {namespace!r}"

    def _identity(
        self,
        *,
        name: str,
        namespace: str | None,
        action: str,
    ) -> tuple[str | None, str, str]:
        name = name.strip()
        namespace = self._identity_namespace(namespace, action=action)
        if not name:
            msg = f"{self.kind} {action} requires a name"
            raise OSError(msg)
        return namespace, name, self.object_label(name=name, namespace=namespace)

    def _identity_namespace(self, namespace: str | None, *, action: str) -> str | None:
        namespace = self.default_namespace(namespace)
        namespace = namespace.strip() if namespace is not None else ""
        if self.config.scope == "cluster":
            if namespace:
                msg = f"{self.kind} is cluster-scoped; cannot {action} in namespace"
                raise ValueError(msg)
            return None
        if not namespace:
            msg = f"{self.kind} {action} requires a namespace"
            raise ValueError(msg)
        return namespace

    def default_namespace(self, namespace: str | None) -> str | None:
        return namespace if namespace is not None else self.config.default_namespace

    def _metadata(self, payload: Mapping[str, Any]) -> Mapping[str, Any]:
        metadata = payload.get("metadata", {})
        if not isinstance(metadata, Mapping):
            return MappingProxyType({})
        return MappingProxyType(cast("dict[str, Any]", dict(metadata)))

    def _payload(self, payload: object, *, label: str) -> Mapping[str, Any]:
        if not isinstance(payload, Mapping):
            msg = f"malformed Kubernetes {label}"
            raise OSError(msg)
        return cast("Mapping[str, Any]", payload)

    def _list_payload(self, payload: object) -> _CustomListPayload:
        payload = self._payload(payload, label=f"{self.kind} list payload")
        metadata = payload.get("metadata", {})
        if not isinstance(metadata, Mapping):
            msg = f"malformed Kubernetes {self.kind} list metadata"
            raise OSError(msg)
        items = payload.get("items", [])
        if not isinstance(items, list):
            msg = f"malformed Kubernetes {self.kind} list items"
            raise OSError(msg)
        out = [
            self._payload(
                item,
                label=f"custom object payload for {self.config.plural}",
            )
            for item in items
        ]
        return _CustomListPayload(
            metadata=_CustomListMeta(
                str(cast("Mapping[str, object]", metadata).get("resourceVersion") or "")
            ),
            items=out,
        )


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
        config = _CustomResourceConfig(
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
        resource_cls = cast("type[CustomResource]", cls)
        resource_cls._resource_api = _CustomResourceAPI(config)
        return cls

    return register


class CustomResource(CustomObject):
    """Base class for class-owned Kubernetes custom-resource wrappers."""

    _resource_api: ClassVar[_ResourceAPI[Mapping[str, Any]] | None] = None

    @classmethod
    async def ensure_crd(cls, kube: Kube, *, deadline: Deadline) -> None:
        """Converge this custom resource's CRD and wait until it is established.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum convergence budget in seconds.

        Raises
        ------
        ValueError
            If this custom resource does not own a CRD.
        OSError
            If the CRD disappears before it reports established.
        """
        api = cls._api()
        config = api.config
        spec_schema = cls._spec_schema()
        if config.singular is None or spec_schema is None:
            msg = f"{config.kind} does not own a CRD"
            raise ValueError(msg)
        crd = await CustomResourceDefinition.upsert(
            kube,
            intent=CustomResourceDefinitionManifest(
                group=config.group,
                version=config.version,
                plural=config.plural,
                singular=config.singular,
                kind=config.kind,
                short_names=config.short_names,
                spec_schema=spec_schema,
                status_schema=cls._status_schema(),
                labels=config.labels,
                scope="Cluster" if config.scope == "cluster" else "Namespaced",
            ),
            deadline=deadline,
        )
        established = await crd.wait(
            kube,
            deadline=deadline,
            predicate=lambda live: live is None or live.is_established,
        )
        if established is None:
            msg = f"CustomResourceDefinition {crd.name!r} disappeared before ready"
            raise OSError(msg)

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        name: str,
        deadline: Deadline,
        namespace: str | None = None,
    ) -> Self | None:
        """Read one custom object by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Kubernetes object name.
        deadline : Deadline
            Maximum request budget in seconds.
        namespace : str | None, optional
            Namespace for namespaced resources.

        Returns
        -------
        CustomResource | None
            Wrapped custom object, or `None` when absent.

        """
        payload = await cls._api().read(
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
        """List custom objects with optional namespace and selector filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum request budget.
        namespace : str | None, optional
            Single namespace filter for namespaced resources.
        namespaces : Collection[str] | None, optional
            Multiple namespace filters for namespaced resources.
        labels : Mapping[str, str], optional
            Label filters to merge with resource labels.
        field_selector : Collection[str], optional
            Kubernetes field selector fragments to apply to the list request.

        Returns
        -------
        list[CustomResource]
            Wrapped custom objects matching the filters.

        """
        api = cls._api()
        label_selector = api.label_selector(labels)
        field_selector_text = ",".join(
            item.strip() for item in field_selector if item.strip()
        )
        payloads = await api.list(
            kube,
            deadline=deadline,
            namespace=namespace,
            namespaces=namespaces,
            label_selector=label_selector,
            field_selector=field_selector_text,
        )
        out: builtins.list[Self] = []
        for payload in payloads:
            out.extend(cls(item) for item in api.items(payload))
        return out

    @classmethod
    async def watch(
        cls,
        kube: Kube,
        *,
        deadline: Deadline,
        labels: Mapping[str, str] = EMPTY_MAPPING,
        namespace: str | None = None,
        field_selector: Collection[str] = (),
    ) -> AsyncIterator[WatchEvent[Self]]:
        """Watch custom objects and yield typed events.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum watch budget.
        labels : Mapping[str, str], optional
            Label filters to merge with resource labels for snapshot and stream
            requests.
        namespace : str | None, optional
            Namespace to watch. Omit for cluster-scoped resources or to watch all
            namespaces for namespaced resources.
        field_selector : Collection[str], optional
            Kubernetes field selector fragments to apply to the snapshot and stream
            requests.

        Yields
        ------
        WatchEvent[CustomResource]
            Typed custom-object watch events.
        """
        api = cls._api()
        label_selector = api.label_selector(labels)
        field_selector_text = ",".join(
            item.strip() for item in field_selector if item.strip()
        )
        list_method, api_kwargs, context = api.watch_method(kube, namespace=namespace)

        async def snapshot(attempt_deadline: Deadline) -> str:
            payload = await kube.run(
                lambda request_timeout: list_method(
                    **api_kwargs,
                    label_selector=label_selector or None,
                    field_selector=field_selector_text or None,
                    _request_timeout=request_timeout,
                ),
                deadline=attempt_deadline,
                context=context,
                missing_ok=False,
            )
            return api.list_resource_version(api._list_payload(payload))

        async def stream(
            resource_version: str,
            attempt_deadline: Deadline,
        ) -> AsyncIterator[WatchEvent[Self]]:
            async for event in watch_stream(
                list_method,
                deadline=attempt_deadline,
                context=context,
                resource_version=resource_version,
                label_selector=label_selector,
                field_selector=field_selector_text,
                api_kwargs=api_kwargs,
            ):
                payload = api._payload(
                    event.object,
                    label=f"custom object payload for {api.config.plural}",
                )
                yield WatchEvent(
                    type=event.type,
                    object=cls(payload),
                    resource_version=event.resource_version,
                )

        async for event in watch_collection(
            deadline=deadline,
            snapshot=snapshot,
            stream=stream,
        ):
            yield event

    @classmethod
    async def create(
        cls,
        kube: Kube,
        *,
        intent: _KubeManifest,
        deadline: Deadline,
    ) -> Self:
        """Create one custom object from a typed manifest intent.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        intent : _KubeManifest
            Push-side manifest intent to create.
        deadline : Deadline
            Maximum request budget in seconds.

        Returns
        -------
        CustomResource
            Wrapped created object.

        """
        api = cls._api()
        payload = await api.create(
            kube,
            name=intent.name.strip(),
            namespace=intent.namespace,
            body=intent.manifest(),
            deadline=deadline,
        )
        return cls(payload)

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        intent: _KubeManifest,
        deadline: Deadline,
    ) -> Self:
        """Create or patch one custom object from a typed manifest intent.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        intent : _KubeManifest
            Push-side manifest intent to create or patch.
        deadline : Deadline
            Maximum request budget in seconds.

        Returns
        -------
        CustomResource
            Wrapped created or patched object.

        Raises
        ------
        OSError
            If the manifest is malformed or the API request fails.
        """
        api = cls._api()
        namespace, name, body = api.manifest_body(
            name=intent.name,
            namespace=intent.namespace,
            body=intent.manifest(),
            action="upsert",
        )
        label = api.object_label(name=name, namespace=namespace)
        try:
            payload = await api.create(
                kube,
                name=name,
                namespace=namespace,
                body=body,
                deadline=deadline,
            )
        except OSError as err:
            if not isinstance(err, Kube.APIError) or err.status != 409:
                raise
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
        """Patch this custom object's metadata.

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
        CustomResource
            Fresh wrapper returned by Kubernetes after the metadata patch.

        Raises
        ------
        ValueError
            If no metadata updates are provided.
        """
        cls = type(self)
        api = cls._api()
        if not labels and not annotations:
            msg = f"{api.kind} metadata patch cannot be empty"
            raise ValueError(msg)
        namespace, name = api.resource_identity(
            self._payload,
            action="patch custom object",
        )
        label = api.object_label(name=name, namespace=namespace)

        body: dict[str, object] = {"metadata": {}}
        metadata = cast("dict[str, object]", body["metadata"])
        if labels:
            metadata["labels"] = dict(labels)
        if annotations:
            metadata["annotations"] = dict(annotations)

        payload = await api.patch(
            kube,
            name=name,
            namespace=namespace,
            body=body,
            deadline=deadline,
            context=f"failed to patch {api.kind} metadata {label}",
        )
        return type(self)(payload)

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

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Kubernetes object name.
        status : _CustomObjectFragment
            Status fragment to place under the Kubernetes `status` field.
        deadline : Deadline
            Maximum request budget in seconds.
        namespace : str | None, optional
            Namespace for namespaced resources.

        Returns
        -------
        CustomResource
            Wrapped object returned by Kubernetes.
        """
        api = cls._api()
        namespace, name, label = api._identity(
            name=name,
            namespace=namespace,
            action="patch status",
        )
        body = {"status": _custom_object_fragment(status)}
        payload = await api.patch_status(
            kube,
            name=name,
            namespace=namespace,
            body=body,
            deadline=deadline,
            context=f"failed to patch {api.kind} status {label}",
        )
        return cls(payload)

    async def delete(self, kube: Kube, *, deadline: Deadline) -> None:
        """Delete this custom object.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum request budget in seconds.

        """
        api = type(self)._api()
        namespace, name = api.resource_identity(
            self._payload,
            action="delete custom object",
        )
        await api.delete(
            kube,
            name=name,
            namespace=namespace,
            body=None,
            deadline=deadline,
        )

    async def refresh(self, kube: Kube, *, deadline: Deadline) -> Self | None:
        """Re-read this custom object by metadata identity.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum request budget in seconds.

        Returns
        -------
        CustomResource | None
            Fresh wrapped object, or `None` when absent.

        """
        namespace, name = (
            type(self)
            ._api()
            .resource_identity(
                self._payload,
                action="refresh custom object",
            )
        )
        return await type(self).get(
            kube,
            namespace=namespace,
            name=name,
            deadline=deadline,
        )

    async def wait(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        predicate: Callable[[Self | None], bool],
        check_current: bool = False,
    ) -> Self | None:
        """Wait until this custom-object identity satisfies `predicate`.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum wait budget.
        predicate : Callable[[CustomResource | None], bool]
            Predicate over the refreshed object. `None` represents deletion.
        check_current : bool, optional
            Whether to check this resource once before the first refresh.

        Returns
        -------
        CustomResource | None
            Refreshed custom object, or `None` when deletion satisfies the
            predicate.

        Raises
        ------
        TimeoutError
            If the wait deadline expires.
        """
        namespace, name = (
            type(self)
            ._api()
            .resource_identity(
                self._payload,
                action="wait for custom object",
            )
        )
        label = f"{type(self).__name__} {namespace}/{name}" if namespace else name
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
    def _api(cls) -> _CustomResourceAPI:
        api = cls._resource_api
        if api is None:
            msg = f"{cls.__name__} must be decorated with @custom_resource"
            raise TypeError(msg)
        return cast("_CustomResourceAPI", api)

    @classmethod
    def _spec_schema(cls) -> Mapping[str, object] | None:
        config = cls._api().config
        if config.spec_model is None:
            return None
        return _schema_from_model(
            config.spec_model,
            include_defaults=config.spec_schema_include_defaults,
            overrides=config.spec_schema_overrides,
        )

    @classmethod
    def _status_schema(cls) -> Mapping[str, object] | None:
        config = cls._api().config
        if config.status_model is None:
            return None
        return _schema_from_model(
            config.status_model,
            include_defaults=config.status_schema_include_defaults,
            overrides=config.status_schema_overrides,
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
