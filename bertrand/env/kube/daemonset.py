"""Wrappers for the Kubernetes DaemonSet API and related operations."""

from __future__ import annotations

from dataclasses import dataclass, replace
from types import MappingProxyType
from typing import TYPE_CHECKING, ClassVar, Self

import kubernetes

from bertrand.env.git import Deadline

from .api.metadata import NamespacedKubeMetadata
from .api.resource import BuiltinResource, BuiltinResourceObject

if TYPE_CHECKING:
    from collections.abc import Mapping

    from .api.client import Kube
    from .api.spec import PodTemplateSpec


@dataclass(frozen=True)
class DaemonSet(
    BuiltinResourceObject[kubernetes.client.V1DaemonSet],
    NamespacedKubeMetadata[kubernetes.client.V1DaemonSet],
):
    """General-purpose wrapper around one Kubernetes DaemonSet object."""

    _obj: kubernetes.client.V1DaemonSet

    resource: ClassVar[BuiltinResource[kubernetes.client.V1DaemonSet]] = (
        BuiltinResource(
            scope="namespaced",
            api="apps",
            kind="DaemonSet",
            slug="daemon_set",
            expected=kubernetes.client.V1DaemonSet,
            list_type=kubernetes.client.V1DaemonSetList,
            can_create=True,
            can_patch=True,
            can_delete=True,
            can_watch=True,
        )
    )

    @staticmethod
    def _manifest(
        *,
        namespace: str,
        name: str,
        labels: Mapping[str, str],
        selector: Mapping[str, str],
        pod_template: PodTemplateSpec,
        annotations: Mapping[str, str] | None,
    ) -> dict[str, object]:
        template_labels = dict(labels)
        template_labels.update(pod_template.labels)
        template_labels.update(selector)
        return {
            "apiVersion": "apps/v1",
            "kind": "DaemonSet",
            "metadata": {
                "name": name,
                "namespace": namespace,
                "labels": dict(labels),
                "annotations": dict(annotations or {}),
            },
            "spec": {
                "selector": {"matchLabels": dict(selector)},
                "template": replace(pod_template, labels=template_labels)._manifest(),
            },
        }

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        labels: Mapping[str, str],
        selector: Mapping[str, str],
        pod_template: PodTemplateSpec,
        deadline: Deadline,
        annotations: Mapping[str, str] | None = None,
    ) -> Self:
        """Create or patch one Kubernetes DaemonSet from intent-level fields.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the DaemonSet.
        name : str
            DaemonSet name to create or patch.
        labels : Mapping[str, str]
            Labels to apply to the DaemonSet and pod template.
        selector : Mapping[str, str]
            Immutable pod selector labels for the DaemonSet.
        pod_template : PodTemplateSpec
            Pod template to render into the DaemonSet.
        deadline : Deadline
            Maximum request budget in seconds. If infinite, wait indefinitely.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to `metadata.annotations`.

        Returns
        -------
        DaemonSet
            Wrapped created or patched DaemonSet.

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or the API call fails.
        """
        namespace = namespace.strip()
        name = name.strip()
        if not namespace or not name:
            msg = "DaemonSet upsert requires non-empty namespace and name"
            raise OSError(msg)
        manifest = cls._manifest(
            namespace=namespace,
            name=name,
            labels=labels,
            selector=selector,
            pod_template=pod_template,
            annotations=annotations,
        )
        return cls(
            _obj=await cls.resource.upsert(
                kube,
                namespace=namespace,
                name=name,
                manifest=manifest,
                deadline=deadline,
            )
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
    ) -> Self:
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
        """
        if minimum < 0:
            msg = "DaemonSet availability minimum cannot be negative"
            raise ValueError(msg)
        namespace, name = self._require_namespace_name(
            "wait for DaemonSet availability"
        )
        return await self._wait_until(
            kube,
            deadline=deadline,
            predicate=lambda live: live.has_available_pods(minimum),
            pending_message=f"DaemonSet {namespace}/{name} is not available yet",
            missing_message=(
                f"DaemonSet {namespace}/{name} disappeared while waiting for "
                "availability"
            ),
            timeout_message=(
                f"timed out waiting for DaemonSet {namespace}/{name} availability"
            ),
        )

    async def wait_rollout(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        minimum: int = 1,
    ) -> Self:
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
        """
        if minimum < 0:
            msg = "DaemonSet rollout minimum cannot be negative"
            raise ValueError(msg)
        namespace, name = self._require_namespace_name("wait for DaemonSet rollout")
        target_generation = self.generation
        return await self._wait_until(
            kube,
            deadline=deadline,
            predicate=lambda live: (
                (
                    target_generation <= 0
                    or live.observed_generation >= target_generation
                )
                and live.rollout_ready(minimum)
            ),
            pending_message=f"DaemonSet {namespace}/{name} rollout is not complete yet",
            missing_message=(
                f"DaemonSet {namespace}/{name} disappeared while waiting for rollout"
            ),
            timeout_message=(
                f"timed out waiting for DaemonSet {namespace}/{name} rollout"
            ),
        )
