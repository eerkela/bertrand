"""Wrappers for the Kubernetes Deployment API and related operations."""

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

    from bertrand.env.git import Deadline

    from .api.client import Kube
    from .api.manifest import DeploymentStrategyManifest, PodTemplateSpec


@dataclass(frozen=True)
class DeploymentManifest:
    """Desired state for one Kubernetes Deployment."""

    namespace: str
    name: str
    labels: Mapping[str, str]
    selector: Mapping[str, str]
    pod_template: PodTemplateSpec
    replicas: int = 1
    annotations: Mapping[str, str] | None = None
    strategy: DeploymentStrategyManifest | None = None
    min_ready_seconds: int | None = None
    progress_deadline_seconds: int | None = None
    revision_history_limit: int | None = None
    paused: bool | None = None

    def manifest(self) -> Mapping[str, object]:
        """Render this desired state as a Kubernetes manifest.

        Returns
        -------
        Mapping[str, object]
            Kubernetes Deployment manifest payload.

        Raises
        ------
        ValueError
            If replica or rollout timing fields are negative.
        """
        if self.replicas < 0:
            msg = "Deployment replicas cannot be negative"
            raise ValueError(msg)
        for label, value in (
            ("min ready seconds", self.min_ready_seconds),
            ("progress deadline seconds", self.progress_deadline_seconds),
            ("revision history limit", self.revision_history_limit),
        ):
            if value is not None and value < 0:
                msg = f"Deployment {label} cannot be negative"
                raise ValueError(msg)
        template_labels = dict(self.labels)
        template_labels.update(self.pod_template.labels)
        template_labels.update(self.selector)
        spec: dict[str, object] = {
            "replicas": self.replicas,
            "selector": {"matchLabels": dict(self.selector)},
            "template": replace(
                self.pod_template,
                labels=template_labels,
            ).manifest(),
        }
        if self.strategy is not None:
            spec["strategy"] = dict(self.strategy)
        optional: dict[str, object | None] = {
            "minReadySeconds": self.min_ready_seconds,
            "progressDeadlineSeconds": self.progress_deadline_seconds,
            "revisionHistoryLimit": self.revision_history_limit,
            "paused": self.paused,
        }
        spec.update(
            {key: value for key, value in optional.items() if value is not None}
        )
        return {
            "apiVersion": "apps/v1",
            "kind": "Deployment",
            "metadata": {
                "name": self.name,
                "namespace": self.namespace,
                "labels": dict(self.labels),
                "annotations": dict(self.annotations or {}),
            },
            "spec": spec,
        }


@namespaced_resource(
    api=kubernetes.client.AppsV1Api,
    payload=kubernetes.client.V1Deployment,
    read=kubernetes.client.AppsV1Api.read_namespaced_deployment,
    list=kubernetes.client.AppsV1Api.list_namespaced_deployment,
    list_all=kubernetes.client.AppsV1Api.list_deployment_for_all_namespaces,
    create=kubernetes.client.AppsV1Api.create_namespaced_deployment,
    patch=kubernetes.client.AppsV1Api.patch_namespaced_deployment,
    delete=kubernetes.client.AppsV1Api.delete_namespaced_deployment,
)
@dataclass(frozen=True)
class Deployment(
    KubeResource[kubernetes.client.V1Deployment, DeploymentManifest],
):
    """General-purpose wrapper around one Kubernetes Deployment object.

    Parameters
    ----------
    _obj : kubernetes.client.V1Deployment
        Typed Kubernetes Deployment payload returned by the cluster API.

    Notes
    -----
    The convergence API accepts intent-level fields and keeps raw Kubernetes
    manifests as an internal implementation detail.
    """

    _obj: kubernetes.client.V1Deployment

    @property
    def pod_annotations(self) -> Mapping[str, str]:
        """Return this Deployment's pod template annotations.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `spec.template.metadata.annotations`, or an empty
            mapping when unavailable.
        """
        spec = self._obj.spec
        template = spec.template if spec is not None else None
        metadata = template.metadata if template is not None else None
        if metadata is None or metadata.annotations is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.annotations)

    def container_env(self, name: str) -> Mapping[str, str]:
        """Return literal environment values for one pod-template container.

        Parameters
        ----------
        name : str
            Container name to inspect.

        Returns
        -------
        Mapping[str, str]
            Read-only mapping of literal environment values for the named
            container. Variables sourced from `valueFrom` are omitted.
        """
        target = name.strip()
        if not target:
            return MappingProxyType({})
        spec = self._obj.spec
        template = spec.template if spec is not None else None
        pod_spec = template.spec if template is not None else None
        for container in (pod_spec.containers or []) if pod_spec is not None else []:
            if (container.name or "").strip() != target:
                continue
            values = {
                item.name: item.value
                for item in container.env or []
                if item.name and item.value is not None
            }
            return MappingProxyType(values)
        return MappingProxyType({})

    @property
    def generation(self) -> int:
        """Return this Deployment's metadata generation.

        Returns
        -------
        int
            Kubernetes `metadata.generation`, or zero when unavailable.
        """
        metadata = self._obj.metadata
        return int(metadata.generation or 0) if metadata is not None else 0

    @property
    def replicas(self) -> int:
        """Return this Deployment's desired replica count.

        Returns
        -------
        int
            Desired replica count, or zero when unavailable.
        """
        spec = self._obj.spec
        return int(spec.replicas or 0) if spec is not None else 0

    @property
    def available_replicas(self) -> int:
        """Return this Deployment's available replica count.

        Returns
        -------
        int
            Available replica count, or zero when unavailable.
        """
        status = self._obj.status
        return int(status.available_replicas or 0) if status is not None else 0

    @property
    def ready_replicas(self) -> int:
        """Return this Deployment's ready replica count.

        Returns
        -------
        int
            Ready replica count, or zero when unavailable.
        """
        status = self._obj.status
        return int(status.ready_replicas or 0) if status is not None else 0

    @property
    def updated_replicas(self) -> int:
        """Return this Deployment's updated replica count.

        Returns
        -------
        int
            Updated replica count, or zero when unavailable.
        """
        status = self._obj.status
        return int(status.updated_replicas or 0) if status is not None else 0

    @property
    def unavailable_replicas(self) -> int:
        """Return this Deployment's unavailable replica count.

        Returns
        -------
        int
            Unavailable replica count, or zero when unavailable.
        """
        status = self._obj.status
        return int(status.unavailable_replicas or 0) if status is not None else 0

    @property
    def observed_generation(self) -> int:
        """Return the Deployment generation observed by the controller.

        Returns
        -------
        int
            Kubernetes `status.observedGeneration`, or zero when unavailable.
        """
        status = self._obj.status
        return int(status.observed_generation or 0) if status is not None else 0

    @property
    def selector(self) -> Mapping[str, str]:
        """Return this Deployment's selector labels.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `spec.selector.match_labels`, or an empty mapping when
            unavailable.
        """
        spec = self._obj.spec
        selector = spec.selector if spec is not None else None
        labels = selector.match_labels if selector is not None else None
        if labels is None:
            return MappingProxyType({})
        return MappingProxyType(labels)

    def has_available_replicas(self, minimum: int = 1) -> bool:
        """Return whether this Deployment has enough available replicas.

        Parameters
        ----------
        minimum : int, optional
            Minimum acceptable available replica count.

        Returns
        -------
        bool
            Whether `status.availableReplicas` is at least `minimum`.

        Raises
        ------
        ValueError
            If `minimum` is less than one.
        """
        if minimum < 1:
            msg = "minimum available Deployment replicas must be positive"
            raise ValueError(msg)
        return self.available_replicas >= minimum

    def rollout_ready(self, minimum: int = 1) -> bool:
        """Return whether this Deployment's rollout status is ready.

        Parameters
        ----------
        minimum : int, optional
            Minimum acceptable updated and available replica count.

        Returns
        -------
        bool
            Whether the controller has observed the current generation and at least
            `minimum` replicas are updated and available.

        Raises
        ------
        ValueError
            If `minimum` is less than one.
        """
        if minimum < 1:
            msg = "minimum rolled out Deployment replicas must be positive"
            raise ValueError(msg)
        generation_observed = (
            self.generation <= 0 or self.observed_generation >= self.generation
        )
        return (
            generation_observed
            and self.updated_replicas >= minimum
            and self.available_replicas >= minimum
        )

    async def wait_available(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        minimum: int = 1,
    ) -> Deployment:
        """Wait until this Deployment has at least `minimum` available replicas.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum wait time in seconds. Must be positive.
        minimum : int, optional
            Minimum acceptable `status.availableReplicas` value.

        Returns
        -------
        Deployment
            Fresh wrapper whose available replica count satisfies `minimum`.

        Raises
        ------
        ValueError
            If `minimum` is less than one.
        OSError
            If the Deployment disappears before satisfying the condition.
        """
        if minimum < 1:
            msg = "minimum available Deployment replicas must be positive"
            raise ValueError(msg)
        live = await self.wait(
            kube,
            deadline=deadline,
            predicate=lambda live: live is None
            or live.has_available_replicas(minimum),
        )
        if live is None:
            msg = "Deployment disappeared while waiting for available replicas"
            raise OSError(msg)
        return live

    async def wait_rollout(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        minimum: int = 1,
    ) -> Deployment:
        """Wait until this Deployment rolls out at least `minimum` replicas.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum wait time in seconds. Must be positive.
        minimum : int, optional
            Minimum acceptable updated and available replica count.

        Returns
        -------
        Deployment
            Fresh wrapper whose controller-observed generation and replica status
            indicate the rollout has completed for at least `minimum` replicas.

        Raises
        ------
        ValueError
            If `minimum` is less than one.
        OSError
            If the Deployment disappears before satisfying the condition.
        """
        if minimum < 1:
            msg = "minimum rolled out Deployment replicas must be positive"
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
                    and live.updated_replicas >= minimum
                    and live.available_replicas >= minimum
                )
            ),
        )
        if live is None:
            msg = "Deployment disappeared while waiting for rollout"
            raise OSError(msg)
        return live

    async def scale(
        self, kube: Kube, *, replicas: int, deadline: Deadline
    ) -> Deployment:
        """Patch this Deployment's desired replica count.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        replicas : int
            Desired replica count. Must be non-negative.
        deadline : Deadline
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Deployment
            Fresh wrapper after the scale patch is accepted.

        Raises
        ------
        ValueError
            If `replicas` is negative.
        OSError
            If Kubernetes returns malformed data or the API call fails.
        """
        if replicas < 0:
            msg = "Deployment replicas cannot be negative"
            raise ValueError(msg)
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = "cannot scale Deployment with missing metadata.name/namespace"
            raise OSError(msg)
        api = kubernetes.client.AppsV1Api(kube.client)
        payload = await kube.run(
            lambda request_timeout: api.patch_namespaced_deployment_scale(
                name=name,
                namespace=namespace,
                body={"spec": {"replicas": replicas}},
                _request_timeout=request_timeout,
            ),
            deadline=deadline,
            context=f"failed to scale Deployment {namespace}/{name}",
        )
        if payload is None:
            msg = f"Deployment {namespace}/{name} disappeared while scaling"
            raise OSError(msg)
        live = await Deployment.get(
            kube,
            namespace=namespace,
            deadline=deadline,
            name=name,
        )
        if live is None:
            msg = f"Deployment {namespace}/{name} disappeared after scaling"
            raise OSError(msg)
        return live
