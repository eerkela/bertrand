"""Wrappers for the Kubernetes DaemonSet API and related operations."""

from __future__ import annotations

from dataclasses import dataclass, replace
from types import MappingProxyType
from typing import TYPE_CHECKING

import kubernetes

from .api.resource import (
    KubeResource,
    namespaced_resource,
)

if TYPE_CHECKING:
    from collections.abc import Mapping

    from .api.manifest import PodTemplateSpec


@dataclass(frozen=True)
class DaemonSetManifest:
    """Desired state for one Kubernetes DaemonSet."""

    namespace: str
    name: str
    labels: Mapping[str, str]
    selector: Mapping[str, str]
    pod_template: PodTemplateSpec
    annotations: Mapping[str, str] | None = None

    def manifest(self) -> Mapping[str, object]:
        """Render this desired state as a Kubernetes manifest.

        Returns
        -------
        Mapping[str, object]
            Kubernetes DaemonSet manifest payload.
        """
        template_labels = dict(self.labels)
        template_labels.update(self.pod_template.labels)
        template_labels.update(self.selector)
        return {
            "apiVersion": "apps/v1",
            "kind": "DaemonSet",
            "metadata": {
                "name": self.name,
                "namespace": self.namespace,
                "labels": dict(self.labels),
                "annotations": dict(self.annotations or {}),
            },
            "spec": {
                "selector": {"matchLabels": dict(self.selector)},
                "template": replace(
                    self.pod_template,
                    labels=template_labels,
                ).manifest(),
            },
        }


@namespaced_resource(
    api=kubernetes.client.AppsV1Api,
    read=kubernetes.client.AppsV1Api.read_namespaced_daemon_set,
    list=kubernetes.client.AppsV1Api.list_namespaced_daemon_set,
    list_all=kubernetes.client.AppsV1Api.list_daemon_set_for_all_namespaces,
    create=kubernetes.client.AppsV1Api.create_namespaced_daemon_set,
    patch=kubernetes.client.AppsV1Api.patch_namespaced_daemon_set,
    delete=kubernetes.client.AppsV1Api.delete_namespaced_daemon_set,
)
@dataclass(frozen=True)
class DaemonSet(
    KubeResource[kubernetes.client.V1DaemonSet, DaemonSetManifest],
):
    """General-purpose wrapper around one Kubernetes DaemonSet object."""

    _obj: kubernetes.client.V1DaemonSet

    @property
    def generation(self) -> int:
        """Return this DaemonSet's metadata generation.

        Returns
        -------
        int
            Kubernetes `metadata.generation`, or zero when unavailable.
        """
        metadata = self._obj.metadata
        return int(metadata.generation or 0) if metadata is not None else 0

    @property
    def number_available(self) -> int:
        """Return this DaemonSet's available pod count.

        Returns
        -------
        int
            Available DaemonSet pod count, or zero when unavailable.
        """
        status = self._obj.status
        return int(status.number_available or 0) if status is not None else 0

    @property
    def desired_number_scheduled(self) -> int:
        """Return this DaemonSet's desired scheduled pod count.

        Returns
        -------
        int
            Desired scheduled pod count, or zero when unavailable.
        """
        status = self._obj.status
        return int(status.desired_number_scheduled or 0) if status is not None else 0

    @property
    def current_number_scheduled(self) -> int:
        """Return this DaemonSet's current scheduled pod count.

        Returns
        -------
        int
            Current scheduled pod count, or zero when unavailable.
        """
        status = self._obj.status
        return int(status.current_number_scheduled or 0) if status is not None else 0

    @property
    def number_ready(self) -> int:
        """Return this DaemonSet's ready pod count.

        Returns
        -------
        int
            Ready DaemonSet pod count, or zero when unavailable.
        """
        status = self._obj.status
        return int(status.number_ready or 0) if status is not None else 0

    @property
    def updated_number_scheduled(self) -> int:
        """Return this DaemonSet's updated scheduled pod count.

        Returns
        -------
        int
            Updated scheduled pod count, or zero when unavailable.
        """
        status = self._obj.status
        return int(status.updated_number_scheduled or 0) if status is not None else 0

    @property
    def number_unavailable(self) -> int:
        """Return this DaemonSet's unavailable pod count.

        Returns
        -------
        int
            Unavailable DaemonSet pod count, or zero when unavailable.
        """
        status = self._obj.status
        return int(status.number_unavailable or 0) if status is not None else 0

    @property
    def observed_generation(self) -> int:
        """Return the DaemonSet generation observed by the controller.

        Returns
        -------
        int
            Kubernetes `status.observedGeneration`, or zero when unavailable.
        """
        status = self._obj.status
        return int(status.observed_generation or 0) if status is not None else 0

    @property
    def pod_annotations(self) -> Mapping[str, str]:
        """Return pod-template annotations.

        Returns
        -------
        Mapping[str, str]
            Read-only pod-template annotations, or an empty mapping when absent.
        """
        spec = self._obj.spec
        template = spec.template if spec is not None else None
        metadata = template.metadata if template is not None else None
        annotations = metadata.annotations if metadata is not None else None
        return MappingProxyType(dict(annotations or {}))
