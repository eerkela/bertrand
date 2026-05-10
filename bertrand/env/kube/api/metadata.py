"""Shared metadata bases for typed Kubernetes wrappers."""

from __future__ import annotations

from types import MappingProxyType
from typing import TYPE_CHECKING, Protocol

if TYPE_CHECKING:
    from collections.abc import Mapping
    from datetime import datetime

    import kubernetes


class _KubeObject(Protocol):
    @property
    def metadata(self) -> kubernetes.client.V1ObjectMeta | None: ...


class KubeMetadata[T: _KubeObject]:
    """Shared metadata view for typed Kubernetes API wrappers.

    This base is intended for wrapper classes that hold a typed Kubernetes client
    model in a private `_obj` field.  It centralizes read-only access to standard
    Kubernetes object metadata while keeping raw client models private.

    Attributes
    ----------
    _obj : T
        Typed Kubernetes client model with standard `metadata`.
    """

    _obj: T

    @property
    def name(self) -> str:
        """Return the Kubernetes object name.

        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        metadata = self._obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def labels(self) -> Mapping[str, str]:
        """Return the Kubernetes object labels.

        Returns
        -------
        Mapping[str, str]
            Live read-only view of `metadata.labels`, or an empty mapping when
            unavailable.
        """
        metadata = self._obj.metadata
        if metadata is None or metadata.labels is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.labels)

    @property
    def annotations(self) -> Mapping[str, str]:
        """Return the Kubernetes object annotations.

        Returns
        -------
        Mapping[str, str]
            Live read-only view of `metadata.annotations`, or an empty mapping when
            unavailable.
        """
        metadata = self._obj.metadata
        if metadata is None or metadata.annotations is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.annotations)

    @property
    def resource_version(self) -> str:
        """Return the Kubernetes object resource version.

        Returns
        -------
        str
            Kubernetes `metadata.resourceVersion`, or an empty string when
            unavailable.
        """
        metadata = self._obj.metadata
        return (metadata.resource_version or "").strip() if metadata is not None else ""

    @property
    def uid(self) -> str:
        """Return the Kubernetes object UID.

        Returns
        -------
        str
            Kubernetes `metadata.uid`, or an empty string when unavailable.
        """
        metadata = self._obj.metadata
        return (metadata.uid or "").strip() if metadata is not None else ""

    @property
    def created_at(self) -> datetime | None:
        """Return the Kubernetes object creation timestamp.

        Returns
        -------
        datetime | None
            Kubernetes `metadata.creationTimestamp`, or `None` when unavailable.
        """
        metadata = self._obj.metadata
        return metadata.creation_timestamp if metadata is not None else None

    def _object_label(
        self,
        name: str | None = None,
        namespace: str | None = None,
    ) -> str:
        namespace = (namespace or "").strip()
        name = (name or self.name).strip()
        if namespace and name:
            return f"{type(self).__name__} {namespace}/{name}"
        if name:
            return f"{type(self).__name__} {name}"
        return type(self).__name__

    def _require_name(self, action: str) -> str:
        name = self.name
        if not name:
            msg = f"cannot {action} with missing metadata.name"
            raise OSError(msg)
        return name


class NamespacedKubeMetadata[T: _KubeObject](KubeMetadata[T]):
    """Shared namespace-aware metadata view for typed Kubernetes wrappers.

    This base extends `KubeMetadata` for namespaced Kubernetes resources.

    Attributes
    ----------
    _obj : T
        Typed Kubernetes client model with standard `metadata.namespace`.
    """

    @property
    def namespace(self) -> str:
        """Return the Kubernetes object namespace.

        Returns
        -------
        str
            Trimmed `metadata.namespace`, or an empty string when unavailable.
        """
        metadata = self._obj.metadata
        return (metadata.namespace or "").strip() if metadata is not None else ""

    def _object_label(
        self,
        name: str | None = None,
        namespace: str | None = None,
    ) -> str:
        namespace = (namespace or self.namespace).strip()
        name = (name or self.name).strip()
        if namespace and name:
            return f"{type(self).__name__} {namespace}/{name}"
        if name:
            return f"{type(self).__name__} {name}"
        return type(self).__name__

    def _require_namespace_name(self, action: str) -> tuple[str, str]:
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = f"cannot {action} with missing metadata.name/namespace"
            raise OSError(msg)
        return namespace, name
