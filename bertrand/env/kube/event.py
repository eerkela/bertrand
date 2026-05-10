"""Read-only wrappers for Kubernetes Events."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Self

from kubernetes import client as kube_client

from .api import (
    NamespacedKubeMetadata,
    ObjectReference,
)
from .api._helpers import (
    _list_namespaced_items,
    _typed_payload,
)
from .api.watch import (
    _watch_namespaced_resource,
)

if TYPE_CHECKING:
    import builtins
    from collections.abc import AsyncIterator, Collection, Mapping
    from datetime import datetime

    from .api import Kube, WatchEvent


def _object_identity(
    ref: kube_client.V1ObjectReference | None,
) -> ObjectReference | None:
    if ref is None:
        return None
    kind = (ref.kind or "").strip()
    namespace = (ref.namespace or "").strip()
    name = (ref.name or "").strip()
    if not kind and not namespace and not name:
        return None
    return ObjectReference(
        kind=kind,
        namespace=namespace,
        name=name,
        api_version=(ref.api_version or "").strip(),
        uid=(ref.uid or "").strip(),
        resource_version=(ref.resource_version or "").strip(),
    )


@dataclass(frozen=True)
class Event(NamespacedKubeMetadata[kube_client.EventsV1Event]):
    """Read-only wrapper around one Kubernetes Event object.

    Parameters
    ----------
    _obj : kube_client.EventsV1Event
        Typed Kubernetes Event payload returned by the cluster API.
    """

    _obj: kube_client.EventsV1Event

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        timeout: float,
    ) -> Self | None:
        """Read one Kubernetes Event by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the Event.
        name : str
            Event name to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Event | None
            Wrapped Kubernetes Event, or `None` if it does not exist.
        """
        payload = await kube.run(
            lambda request_timeout: kube.events.read_namespaced_event(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to read Event {name!r} in namespace {namespace!r}",
        )
        if payload is None:
            return None
        return cls(
            _obj=_typed_payload(payload, kube_client.EventsV1Event, context="Event")
        )

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        timeout: float,
        namespaces: Collection[str] | None = None,
        labels: Mapping[str, str] | None = None,
        field_selector: str | None = None,
    ) -> builtins.list[Self]:
        """List Kubernetes Events with optional filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        namespaces : Collection[str] | None, optional
            Optional namespace filters. `None` queries all namespaces.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.
        field_selector : str | None, optional
            Raw Kubernetes field selector.

        Returns
        -------
        list[Event]
            Wrapped Events matching the requested filters.
        """
        normalized_field_selector = (
            field_selector.strip() if field_selector is not None else None
        )
        return [
            cls(_obj=item)
            for item in await _list_namespaced_items(
                kube,
                timeout=timeout,
                namespaces=namespaces,
                labels=labels,
                list_all=lambda label_selector, request_timeout: (
                    kube.events.list_event_for_all_namespaces(
                        label_selector=label_selector,
                        field_selector=normalized_field_selector or None,
                        _request_timeout=request_timeout,
                    )
                ),
                list_namespace=lambda namespace, label_selector, request_timeout: (
                    kube.events.list_namespaced_event(
                        namespace=namespace,
                        label_selector=label_selector,
                        field_selector=normalized_field_selector or None,
                        _request_timeout=request_timeout,
                    )
                ),
                list_type=kube_client.EventsV1EventList,
                item_type=kube_client.EventsV1Event,
                all_context="failed to list Events across all namespaces",
                namespace_context=lambda namespace: (
                    f"failed to list Events in namespace {namespace!r}"
                ),
                list_context="Event",
                item_context="Event",
            )
        ]

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
        """Watch Kubernetes Events.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum watch budget in seconds. If infinite, wait indefinitely.
        namespace : str | None, optional
            Namespace to watch. If omitted, watches across all namespaces.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.
        field_selector : str | None, optional
            Raw Kubernetes field selector.
        resource_version : str | None, optional
            Resource version to watch from.

        Yields
        ------
        WatchEvent[Event]
            Typed watch events containing wrapped Events.
        """
        async for event in _watch_namespaced_resource(
            kube,
            expected=kube_client.EventsV1Event,
            wrapper=lambda payload: cls(_obj=payload),
            timeout=timeout,
            namespace=namespace,
            resource_version=resource_version,
            labels=labels,
            field_selector=field_selector,
            watch_all=kube.events.list_event_for_all_namespaces,
            watch_namespace=kube.events.list_namespaced_event,
            all_context="failed to watch Events across all namespaces",
            namespace_context=lambda namespace: (
                f"failed to watch Events in namespace {namespace!r}"
            ),
            payload_context="Event watch",
        ):
            yield event

    @property
    def reason(self) -> str:
        """Return the Event reason.

        Returns
        -------
        str
            Event reason, or an empty string when unavailable.
        """
        return (self._obj.reason or "").strip()

    @property
    def type(self) -> str:
        """Return the Event type.

        Returns
        -------
        str
            Event type, such as `"Normal"` or `"Warning"`, or an empty string when
            unavailable.
        """
        return (self._obj.type or "").strip()

    @property
    def action(self) -> str:
        """Return the Event action.

        Returns
        -------
        str
            Event action, or an empty string when unavailable.
        """
        return (self._obj.action or "").strip()

    @property
    def note(self) -> str:
        """Return the Event note.

        Returns
        -------
        str
            Human-readable Event note, or an empty string when unavailable.
        """
        return (self._obj.note or "").strip()

    @property
    def reporting_controller(self) -> str:
        """Return the Event reporting controller.

        Returns
        -------
        str
            Event `reportingController`, or an empty string when unavailable.
        """
        return (self._obj.reporting_controller or "").strip()

    @property
    def reporting_instance(self) -> str:
        """Return the Event reporting instance.

        Returns
        -------
        str
            Event `reportingInstance`, or an empty string when unavailable.
        """
        return (self._obj.reporting_instance or "").strip()

    @property
    def event_time(self) -> datetime | None:
        """Return the Event timestamp.

        Returns
        -------
        datetime | None
            Event `eventTime`, or `None` when unavailable.
        """
        return self._obj.event_time

    @property
    def regarding_identity(self) -> ObjectReference | None:
        """Return the primary object identity for this Event.

        Returns
        -------
        ObjectReference | None
            Reference from `regarding`, or `None` when the Event has no object
            reference.
        """
        return _object_identity(self._obj.regarding)

    @property
    def related_identity(self) -> ObjectReference | None:
        """Return the secondary object identity for this Event.

        Returns
        -------
        ObjectReference | None
            Reference from `related`, or `None` when the Event has no related object
            reference.
        """
        return _object_identity(self._obj.related)
