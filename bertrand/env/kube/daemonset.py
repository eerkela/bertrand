"""Wrappers for the Kubernetes DaemonSet API and related operations."""

from __future__ import annotations

from dataclasses import dataclass, replace
from types import MappingProxyType
from typing import TYPE_CHECKING, Self

import kubernetes

from .api.resource import (
    KubeResource,
    WatchEvent,
    _watch,
    namespaced_resource,
)

if TYPE_CHECKING:
    from collections.abc import AsyncIterator, Mapping

    from bertrand.env.git import Deadline

    from .api.client import Kube
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
    payload=kubernetes.client.V1DaemonSet,
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

    @classmethod
    async def watch(
        cls,
        kube: Kube,
        *,
        deadline: Deadline,
        namespace: str | None = None,
        labels: Mapping[str, str] | None = None,
        field_selector: str = "",
        resource_version: str = "",
    ) -> AsyncIterator[WatchEvent[Self]]:
        """Watch DaemonSets.

        Yields
        ------
        WatchEvent[DaemonSet]
            DaemonSet watch events.

        Raises
        ------
        OSError
            If Kubernetes returns a malformed DaemonSet watch payload.
        """
        namespace = namespace.strip() if namespace is not None else ""
        api = kubernetes.client.AppsV1Api(kube.client)
        if namespace:
            watch_fn = api.list_namespaced_daemon_set
            api_kwargs = {"namespace": namespace}
            context = f"failed to watch DaemonSets in namespace {namespace!r}"
        else:
            watch_fn = api.list_daemon_set_for_all_namespaces
            api_kwargs = {}
            context = "failed to watch DaemonSets across all namespaces"
        async for event in _watch(
            watch_fn,
            deadline=deadline,
            context=context,
            resource_version=resource_version,
            label_selector=(
                ",".join(f"{key}={value}" for key, value in labels.items())
                if labels
                else ""
            ),
            field_selector=field_selector,
            api_kwargs=api_kwargs,
        ):
            if not isinstance(event.object, kubernetes.client.V1DaemonSet):
                msg = "malformed Kubernetes DaemonSet watch payload"
                raise OSError(msg)
            yield WatchEvent(
                type=event.type,
                object=cls(_obj=event.object),
                resource_version=event.resource_version,
            )

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

    def has_available_pods(self, minimum: int = 1) -> bool:
        """Return whether this DaemonSet has enough available pods.

        Parameters
        ----------
        minimum : int, optional
            Minimum acceptable available pod count.

        Returns
        -------
        bool
            Whether `status.numberAvailable` is at least `minimum`.

        Raises
        ------
        ValueError
            If `minimum` is negative.
        """
        if minimum < 0:
            msg = "DaemonSet availability minimum cannot be negative"
            raise ValueError(msg)
        return self.number_available >= minimum

    def rollout_ready(self, minimum: int = 1) -> bool:
        """Return whether this DaemonSet rollout status is ready.

        Parameters
        ----------
        minimum : int, optional
            Minimum acceptable available pod count.

        Returns
        -------
        bool
            Whether the controller has observed the current generation, every desired
            pod is updated, and enough pods are available.

        Raises
        ------
        ValueError
            If `minimum` is negative.
        """
        if minimum < 0:
            msg = "DaemonSet rollout minimum cannot be negative"
            raise ValueError(msg)
        desired = self.desired_number_scheduled
        required = max(minimum, desired)
        generation_observed = (
            self.generation <= 0 or self.observed_generation >= self.generation
        )
        return (
            generation_observed
            and self.updated_number_scheduled >= desired
            and self.number_available >= required
        )

    async def wait_available(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        minimum: int = 1,
    ) -> DaemonSet:
        """Wait until this DaemonSet reports at least `minimum` available pods.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum wait budget in seconds. If infinite, wait indefinitely.
        minimum : int, optional
            Minimum available pod count required before returning.

        Returns
        -------
        DaemonSet
            Refreshed DaemonSet wrapper that satisfied the availability condition.

        Raises
        ------
        ValueError
            If `minimum` is negative.
        OSError
            If the DaemonSet disappears before satisfying the condition.
        """
        if minimum < 0:
            msg = "DaemonSet availability minimum cannot be negative"
            raise ValueError(msg)
        live = await self.wait(
            kube,
            deadline=deadline,
            predicate=lambda live: live is None or live.has_available_pods(minimum),
        )
        if live is None:
            msg = "DaemonSet disappeared while waiting for available pods"
            raise OSError(msg)
        return live

    async def wait_rollout(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        minimum: int = 1,
    ) -> DaemonSet:
        """Wait until this DaemonSet completes rollout.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum wait budget in seconds. If infinite, wait indefinitely.
        minimum : int, optional
            Minimum available pod count required before returning.

        Returns
        -------
        DaemonSet
            Refreshed DaemonSet wrapper that satisfied the rollout condition.

        Raises
        ------
        ValueError
            If `minimum` is negative.
        OSError
            If the DaemonSet disappears before satisfying the condition.
        """
        if minimum < 0:
            msg = "DaemonSet rollout minimum cannot be negative"
            raise ValueError(msg)
        target_generation = self.generation
        live = await self.wait(
            kube,
            deadline=deadline,
            predicate=lambda live: (
                live is None
                or (
                    (
                        target_generation <= 0
                        or live.observed_generation >= target_generation
                    )
                    and live.rollout_ready(minimum)
                )
            ),
        )
        if live is None:
            msg = "DaemonSet disappeared while waiting for rollout"
            raise OSError(msg)
        return live
