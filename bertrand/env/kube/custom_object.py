"""Wrappers for Kubernetes runtime custom objects."""

from __future__ import annotations

from collections.abc import AsyncIterator, Awaitable, Callable, Mapping
from dataclasses import dataclass, field
from datetime import UTC, datetime
from types import MappingProxyType
from typing import TYPE_CHECKING, Any, Literal, Self, cast

from pydantic import BaseModel, ConfigDict, Field

from bertrand.env.git import Deadline

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


@dataclass(frozen=True)
class CustomObjectSpec:
    """Intent-level Kubernetes custom-object specification.

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
        Default labels to apply to objects created through this spec.
    """

    group: str
    version: str
    kind: str
    plural: str
    scope: CustomObjectScope = "namespaced"
    labels: Mapping[str, str] = MappingProxyType({})

    @property
    def api_version(self) -> str:
        """Return the fully qualified Kubernetes API version.

        Returns
        -------
        str
            API version string in `group/version` form.
        """
        return f"{self.group}/{self.version}"


@dataclass(frozen=True)
class CustomObjectClient:
    """Bound helper for Kubernetes custom-object API operations.

    Parameters
    ----------
    spec : CustomObjectSpec
        Resource type handled by this client.
    """

    spec: CustomObjectSpec

    def _body(
        self,
        *,
        namespace: str | None,
        name: str,
        spec: Mapping[str, object],
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
    ) -> dict[str, object]:
        merged_labels = dict(self.spec.labels)
        merged_labels.update(labels or {})
        return {
            "apiVersion": self.spec.api_version,
            "kind": self.spec.kind,
            "metadata": self._metadata(
                name=name,
                namespace=namespace,
                labels=merged_labels,
                annotations=annotations,
            ),
            "spec": dict(spec),
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
        if self.spec.scope == "cluster":
            return kube.custom.get_cluster_custom_object, {
                "group": self.spec.group,
                "version": self.spec.version,
                "plural": self.spec.plural,
                "name": name,
            }
        return kube.custom.get_namespaced_custom_object, {
            "group": self.spec.group,
            "version": self.spec.version,
            "namespace": cast("str", namespace),
            "plural": self.spec.plural,
            "name": name,
        }

    def _list_endpoint(
        self,
        kube: Kube,
        *,
        namespace: str | None,
        label_selector: str | None,
    ) -> tuple[Callable[..., object], dict[str, object]]:
        if self.spec.scope == "cluster" or namespace is None:
            return kube.custom.list_cluster_custom_object, {
                "group": self.spec.group,
                "version": self.spec.version,
                "plural": self.spec.plural,
                "label_selector": label_selector,
            }
        return kube.custom.list_namespaced_custom_object, {
            "group": self.spec.group,
            "version": self.spec.version,
            "namespace": namespace,
            "plural": self.spec.plural,
            "label_selector": label_selector,
        }

    def _create_endpoint(
        self,
        kube: Kube,
        *,
        namespace: str | None,
        body: Mapping[str, object],
    ) -> tuple[Callable[..., object], dict[str, object]]:
        if self.spec.scope == "cluster":
            return kube.custom.create_cluster_custom_object, {
                "group": self.spec.group,
                "version": self.spec.version,
                "plural": self.spec.plural,
                "body": dict(body),
            }
        return kube.custom.create_namespaced_custom_object, {
            "group": self.spec.group,
            "version": self.spec.version,
            "namespace": cast("str", namespace),
            "plural": self.spec.plural,
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
        if self.spec.scope == "cluster":
            return kube.custom.patch_cluster_custom_object, {
                "group": self.spec.group,
                "version": self.spec.version,
                "plural": self.spec.plural,
                "name": name,
                "body": dict(body),
            }
        return kube.custom.patch_namespaced_custom_object, {
            "group": self.spec.group,
            "version": self.spec.version,
            "namespace": cast("str", namespace),
            "plural": self.spec.plural,
            "name": name,
            "body": dict(body),
        }

    def _patch_status_endpoint(
        self,
        kube: Kube,
        *,
        namespace: str | None,
        name: str,
        status: Mapping[str, object],
    ) -> tuple[Callable[..., object], dict[str, object]]:
        if self.spec.scope == "cluster":
            return kube.custom.patch_cluster_custom_object_status, {
                "group": self.spec.group,
                "version": self.spec.version,
                "plural": self.spec.plural,
                "name": name,
                "body": {"status": dict(status)},
            }
        return kube.custom.patch_namespaced_custom_object_status, {
            "group": self.spec.group,
            "version": self.spec.version,
            "namespace": cast("str", namespace),
            "plural": self.spec.plural,
            "name": name,
            "body": {"status": dict(status)},
        }

    def _delete_endpoint(
        self,
        kube: Kube,
        *,
        namespace: str | None,
        name: str,
    ) -> tuple[Callable[..., object], dict[str, object]]:
        if self.spec.scope == "cluster":
            return kube.custom.delete_cluster_custom_object, {
                "group": self.spec.group,
                "version": self.spec.version,
                "plural": self.spec.plural,
                "name": name,
            }
        return kube.custom.delete_namespaced_custom_object, {
            "group": self.spec.group,
            "version": self.spec.version,
            "namespace": cast("str", namespace),
            "plural": self.spec.plural,
            "name": name,
        }

    async def get(
        self,
        kube: Kube,
        *,
        name: str,
        timeout: float,
        namespace: str | None = None,
    ) -> CustomObject | None:
        """Read one custom object by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Custom object name to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        namespace : str | None, optional
            Namespace that owns the custom object. Required for namespaced custom
            objects and rejected for cluster-scoped custom objects.

        Returns
        -------
        CustomObject | None
            Wrapped custom object, or `None` if it does not exist.

        Raises
        ------
        OSError
            If the custom-object API kind is unavailable or the returned payload is
            malformed.
        """
        namespace = self._single_namespace(namespace, action="read")
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
                context=f"failed to read {self.spec.kind} {label}",
            )
        except OSError as err:
            if _is_not_found(err) and not _is_missing_api_resource(err):
                return None
            raise
        if payload is None:
            return None
        return self._wrap(payload)

    async def list(
        self,
        kube: Kube,
        *,
        timeout: float,
        labels: Mapping[str, str] | None = None,
        namespace: str | None = None,
    ) -> builtins.list[CustomObject]:
        """List custom objects with optional label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.
        namespace : str | None, optional
            Namespace filter for namespaced custom objects. If omitted for
            namespaced objects, lists across all namespaces. Rejected for
            cluster-scoped custom objects.

        Returns
        -------
        list[CustomObject]
            Wrapped custom objects matching the requested filters.
        """
        snapshot = await self._snapshot(
            kube,
            namespace=namespace,
            labels=labels,
            timeout=timeout,
        )
        return snapshot.items

    async def watch(
        self,
        kube: Kube,
        *,
        timeout: float,
        labels: Mapping[str, str] | None = None,
        namespace: str | None = None,
        resource_version: str | None = None,
        emit_initial: bool = False,
    ) -> AsyncIterator[WatchEvent[CustomObject]]:
        """Watch custom objects with relist-on-expiry recovery.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum watch budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.
        namespace : str | None, optional
            Namespace filter for namespaced custom objects. If omitted for
            namespaced objects, watches across all namespaces. Rejected for
            cluster-scoped custom objects.
        resource_version : str | None, optional
            Resource version to resume from. If omitted, the client lists once and
            watches from the list resource version.
        emit_initial : bool, optional
            Whether to emit listed objects as synthetic `"ADDED"` events before
            live watch events. Only applies when `resource_version` is omitted.

        Yields
        ------
        WatchEvent[CustomObject]
            Typed custom-object watch events.

        Raises
        ------
        TimeoutError
            If the watch exceeds the timeout budget.
        """
        if timeout <= 0:
            msg = f"{self.spec.kind} watch timeout must be non-negative"
            raise TimeoutError(msg)
        namespace = self._watch_namespace(namespace)
        deadline = Deadline.from_timeout(
            timeout,
            message=f"{self.spec.kind} watch timeout must be non-negative",
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
                    for item in snapshot.items:
                        yield WatchEvent(
                            type="ADDED",
                            object=item,
                            resource_version=item.resource_version or current_version,
                            raw_type="ADDED",
                        )
                    can_emit_initial = False

            remaining = deadline.remaining()
            if remaining <= 0:
                return
            watch_fn, api_kwargs, context = self._watch_endpoint(
                kube,
                namespace=namespace,
            )
            try:
                async for event in kube_watch(
                    watch_fn,
                    wrapper=self._wrap,
                    timeout=remaining,
                    context=context,
                    resource_version=current_version,
                    labels=labels,
                    api_kwargs=api_kwargs,
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
        spec: Mapping[str, object],
        timeout: float,
        namespace: str | None = None,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> CustomObject:
        """Create one custom object from intent-level fields.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Custom object name to create.
        spec : Mapping[str, object]
            Desired custom object `spec` payload.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        namespace : str | None, optional
            Namespace that owns the custom object. Required for namespaced custom
            objects and rejected for cluster-scoped custom objects.
        labels : Mapping[str, str] | None, optional
            Labels to merge over the resource defaults.
        annotations : Mapping[str, str] | None, optional
            Metadata annotations to apply.

        Returns
        -------
        CustomObject
            Wrapped created custom object.
        """
        namespace = self._single_namespace(namespace, action="create")
        return await self._create(
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

    async def create_manifest(
        self,
        kube: Kube,
        *,
        manifest: Mapping[str, object],
        timeout: float,
        namespace: str | None = None,
    ) -> CustomObject:
        """Create one custom object from a complete Kubernetes manifest.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        manifest : Mapping[str, object]
            Complete custom-object manifest.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        namespace : str | None, optional
            Namespace that owns the custom object. Required for namespaced custom
            objects and rejected for cluster-scoped custom objects.

        Returns
        -------
        CustomObject
            Wrapped created custom object.
        """
        namespace = self._single_namespace(namespace, action="create")
        self._require_manifest_name(manifest)
        return await self._create(
            kube,
            namespace=namespace,
            body=manifest,
            timeout=timeout,
        )

    async def upsert(
        self,
        kube: Kube,
        *,
        name: str,
        spec: Mapping[str, object],
        timeout: float,
        namespace: str | None = None,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> CustomObject:
        """Create or patch one custom object from intent fields.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Custom object name to create or patch.
        spec : Mapping[str, object]
            Desired custom object `spec` payload.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        namespace : str | None, optional
            Namespace that owns the custom object. Required for namespaced custom
            objects and rejected for cluster-scoped custom objects.
        labels : Mapping[str, str] | None, optional
            Labels to merge over the resource defaults.
        annotations : Mapping[str, str] | None, optional
            Metadata annotations to apply.

        Returns
        -------
        CustomObject
            Wrapped created or patched custom object.
        """
        namespace = self._single_namespace(namespace, action="upsert")
        return await self._upsert(
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

    async def patch_status(
        self,
        kube: Kube,
        *,
        name: str,
        status: Mapping[str, object],
        timeout: float,
        namespace: str | None = None,
    ) -> CustomObject:
        """Patch the status subresource for one custom object.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Custom object name to patch.
        status : Mapping[str, object]
            Desired `status` payload.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        namespace : str | None, optional
            Namespace that owns the custom object. Required for namespaced custom
            objects and rejected for cluster-scoped custom objects.

        Returns
        -------
        CustomObject
            Wrapped custom object returned by the status patch.
        """
        namespace = self._single_namespace(namespace, action="patch status")
        return await self._patch_status(
            kube,
            namespace=namespace,
            name=name,
            status=status,
            timeout=timeout,
        )

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
            Custom object name to delete.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        namespace : str | None, optional
            Namespace that owns the custom object. Required for namespaced custom
            objects and rejected for cluster-scoped custom objects.
        """
        namespace = self._single_namespace(namespace, action="delete")
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
            context=f"failed to delete {self.spec.kind} {label}",
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
        await _wait_until_deleted(
            label=label,
            timeout=timeout,
            refresh=refresh,
        )

    async def _snapshot(
        self,
        kube: Kube,
        *,
        timeout: float,
        labels: Mapping[str, str] | None = None,
        namespace: str | None = None,
    ) -> _CustomObjectSnapshot:
        namespace = self._watch_namespace(namespace)
        label_selector = _label_selector(labels)
        endpoint, api_kwargs = self._list_endpoint(
            kube,
            namespace=namespace,
            label_selector=label_selector,
        )
        context = (
            f"failed to list {self.spec.kind}s"
            if namespace is None
            else f"failed to list {self.spec.kind}s in namespace {namespace!r}"
        )
        payload = await self._request(
            kube,
            endpoint=endpoint,
            api_kwargs=api_kwargs,
            timeout=timeout,
            context=context,
        )
        if payload is None:
            msg = f"Kubernetes {self.spec.kind} list returned no payload"
            raise OSError(msg)
        if not isinstance(payload, Mapping):
            msg = f"malformed Kubernetes {self.spec.kind} list payload"
            raise OSError(msg)
        payload = cast("Mapping[str, object]", payload)
        metadata = payload.get("metadata", {})
        if not isinstance(metadata, Mapping):
            msg = f"malformed Kubernetes {self.spec.kind} list metadata"
            raise OSError(msg)
        metadata = cast("Mapping[str, object]", metadata)
        resource_version = str(metadata.get("resourceVersion") or "").strip()
        if not resource_version:
            msg = f"Kubernetes {self.spec.kind} list had no resourceVersion"
            raise OSError(msg)
        items = payload.get("items", [])
        if not isinstance(items, list):
            msg = f"malformed Kubernetes {self.spec.kind} list items"
            raise OSError(msg)
        out = [self._wrap(item) for item in items]
        return _CustomObjectSnapshot(items=out, resource_version=resource_version)

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
            context=f"failed to create {self.spec.kind} {label}",
        )
        return self._wrap(payload)

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
            context=f"failed to patch {self.spec.kind} {label}",
        )
        return self._wrap(payload)

    async def _patch_status(
        self,
        kube: Kube,
        *,
        namespace: str | None,
        name: str,
        status: Mapping[str, object],
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
            context=f"failed to patch {self.spec.kind} status {label}",
        )
        return self._wrap(payload)

    def _wrap(self, payload: object) -> CustomObject:
        return CustomObject.from_payload(
            group=self.spec.group,
            version=self.spec.version,
            plural=self.spec.plural,
            scope=self.spec.scope,
            payload=payload,
        )

    def _watch_endpoint(
        self,
        kube: Kube,
        *,
        namespace: str | None,
    ) -> tuple[Callable[..., object], Mapping[str, object], str]:
        if self.spec.scope == "cluster" or namespace is None:
            return (
                kube.custom.list_cluster_custom_object,
                {
                    "group": self.spec.group,
                    "version": self.spec.version,
                    "plural": self.spec.plural,
                },
                f"failed to watch {self.spec.kind}s",
            )
        return (
            kube.custom.list_namespaced_custom_object,
            {
                "group": self.spec.group,
                "version": self.spec.version,
                "namespace": namespace,
                "plural": self.spec.plural,
            },
            f"failed to watch {self.spec.kind}s in namespace {namespace!r}",
        )

    def _single_namespace(self, namespace: str | None, *, action: str) -> str | None:
        namespace = namespace.strip() if namespace is not None else ""
        if self.spec.scope == "cluster":
            if namespace:
                msg = (
                    f"{self.spec.kind} is cluster-scoped; cannot {action} in namespace"
                )
                raise ValueError(msg)
            return None
        if not namespace:
            msg = f"{self.spec.kind} {action} requires a namespace"
            raise ValueError(msg)
        return namespace

    def _watch_namespace(self, namespace: str | None) -> str | None:
        namespace = namespace.strip() if namespace is not None else ""
        if self.spec.scope == "cluster":
            if namespace:
                msg = f"{self.spec.kind} is cluster-scoped; cannot watch in namespace"
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
        if self.spec.scope == "namespaced":
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
            return self.spec.kind
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

    @classmethod
    def _from_object(cls, obj: CustomObject) -> Self:
        return cls(_obj=obj)

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
    """Non-owning descriptor for an externally installed custom-object API.

    Parameters
    ----------
    spec : CustomObjectSpec
        Custom-object API coordinates.
    parser : Callable[[CustomObject], T_co] | None, optional
        Wrapper factory for low-level Kubernetes objects. If omitted, the raw
        `CustomObject` is returned.
    """

    spec: CustomObjectSpec
    parser: Callable[[CustomObject], T_co] | None = None

    @property
    def client(self) -> CustomObjectClient:
        """Return the underlying low-level custom-object client.

        Returns
        -------
        CustomObjectClient
            Bound low-level client for this resource.
        """
        return CustomObjectClient(self.spec)

    async def get(
        self,
        kube: Kube,
        *,
        name: str,
        timeout: float,
        namespace: str | None = None,
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

        Returns
        -------
        T_co | None
            Wrapped object, or ``None`` if it does not exist.
        """
        obj = await self.client.get(
            kube,
            namespace=namespace,
            name=name,
            timeout=timeout,
        )
        return None if obj is None else self._wrap(obj)

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
        objects = await self.client.list(
            kube,
            namespace=namespace,
            labels=labels,
            timeout=timeout,
        )
        return [self._wrap(obj) for obj in objects]

    async def create(
        self,
        kube: Kube,
        *,
        name: str,
        spec: Mapping[str, object],
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
        spec : Mapping[str, object]
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
        obj = await self.client.create(
            kube,
            namespace=namespace,
            name=name,
            spec=spec,
            labels=labels,
            annotations=annotations,
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
        obj = await self.client.create_manifest(
            kube,
            namespace=namespace,
            manifest=manifest,
            timeout=timeout,
        )
        return self._wrap(obj)

    async def upsert(
        self,
        kube: Kube,
        *,
        name: str,
        spec: Mapping[str, object],
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
        spec : Mapping[str, object]
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
        obj = await self.client.upsert(
            kube,
            namespace=namespace,
            name=name,
            spec=spec,
            labels=labels,
            annotations=annotations,
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
        await self.client.delete_by_name(
            kube,
            namespace=namespace,
            name=name,
            timeout=timeout,
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
        await self.client.wait_deleted(label=label, timeout=timeout, refresh=refresh)

    def _wrap(self, obj: CustomObject) -> T_co:
        if self.parser is None:
            return cast("T_co", obj)
        return self.parser(obj)


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
