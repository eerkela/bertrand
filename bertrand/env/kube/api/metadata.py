"""Shared metadata view for typed Kubernetes wrappers."""

from __future__ import annotations

from types import MappingProxyType
from typing import TYPE_CHECKING, Protocol, Self, cast

if TYPE_CHECKING:
    from collections.abc import Callable, Mapping
    from datetime import datetime

    import kubernetes


class _KubePayload(Protocol):
    @property
    def metadata(self) -> kubernetes.client.V1ObjectMeta | None: ...


class KubeObject[PayloadT: _KubePayload]:
    """Read-only metadata view for typed Kubernetes API payloads.

    Attributes
    ----------
    _obj : PayloadT
        Typed Kubernetes client model with standard `metadata`.
    """

    _obj: PayloadT

    @classmethod
    def from_payload(cls, payload: PayloadT) -> Self:
        """Wrap one typed Kubernetes API payload.

        Parameters
        ----------
        payload : PayloadT
            Typed Kubernetes client model returned by the cluster API.

        Returns
        -------
        KubeObject
            Read-only wrapper around `payload`.
        """
        wrapper = cast("Callable[..., Self]", cls)
        return wrapper(_obj=payload)

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
    def namespace(self) -> str:
        """Return the Kubernetes object namespace.

        Returns
        -------
        str
            Trimmed `metadata.namespace`, or an empty string when unavailable.
        """
        metadata = self._obj.metadata
        return (metadata.namespace or "").strip() if metadata is not None else ""

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
        namespace = (namespace or self.namespace).strip()
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

    def _require_namespace_name(self, action: str) -> tuple[str, str]:
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = f"cannot {action} with missing metadata.name/namespace"
            raise OSError(msg)
        return namespace, name
