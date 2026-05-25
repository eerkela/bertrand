"""Read-only wrappers for Kubernetes Events."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Self

from kubernetes import client as kube_client

from .api.metadata import NamespacedKubeMetadata
from .api.resource import NamespacedResourceMixin, NamespacedWatchMixin, ResourceClient
from .api.view import ObjectReference

if TYPE_CHECKING:
    from datetime import datetime


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
class Event(
    NamespacedWatchMixin[kube_client.EventsV1Event],
    NamespacedResourceMixin[kube_client.EventsV1Event],
    NamespacedKubeMetadata[kube_client.EventsV1Event],
):
    """Read-only wrapper around one Kubernetes Event object.

    Parameters
    ----------
    _obj : kube_client.EventsV1Event
        Typed Kubernetes Event payload returned by the cluster API.
    """

    _obj: kube_client.EventsV1Event

    @classmethod
    def _client(cls) -> ResourceClient[kube_client.EventsV1Event, Self]:
        return ResourceClient(
            scope="namespaced",
            kind="Event",
            expected=kube_client.EventsV1Event,
            list_type=kube_client.EventsV1EventList,
            wrapper=lambda payload: cls(_obj=payload),
            read=lambda kube, namespace, name, request_timeout: (
                kube.events.read_namespaced_event(
                    name=name,
                    namespace=namespace,
                    _request_timeout=request_timeout,
                )
            ),
            list_all=lambda kube, label_selector, field_selector, request_timeout: (
                kube.events.list_event_for_all_namespaces(
                    label_selector=label_selector,
                    field_selector=field_selector,
                    _request_timeout=request_timeout,
                )
            ),
            list_namespace=lambda kube, namespace, labels, fields, timeout: (
                kube.events.list_namespaced_event(
                    namespace=namespace,
                    label_selector=labels,
                    field_selector=fields,
                    _request_timeout=timeout,
                )
            ),
            watch_all=lambda kube: kube.events.list_event_for_all_namespaces,
            watch_namespace=lambda kube: kube.events.list_namespaced_event,
        )

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
