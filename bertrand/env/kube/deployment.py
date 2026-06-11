"""Wrappers for the Kubernetes Deployment API and related operations."""

from __future__ import annotations

from dataclasses import dataclass, replace
from types import MappingProxyType
from typing import TYPE_CHECKING

import kubernetes

from .api.resource import (
    Creatable,
    Deletable,
    Listable,
    Patchable,
    Readable,
    Upsertable,
    Watchable,
    _resource_namespace_name,
    builtin_resource,
)

if TYPE_CHECKING:
    from collections.abc import Mapping

    from bertrand.env.git import Deadline

    from .api.client import Kube
    from .api.spec import DeploymentStrategyManifest, PodTemplateSpec


@builtin_resource(api="apps", scope="namespaced")
@dataclass(frozen=True)
class Deployment(
    Readable[kubernetes.client.V1Deployment],
    Listable[kubernetes.client.V1Deployment],
    Creatable[kubernetes.client.V1Deployment],
    Patchable[kubernetes.client.V1Deployment],
    Upsertable[kubernetes.client.V1Deployment],
    Deletable[kubernetes.client.V1Deployment],
    Watchable[kubernetes.client.V1Deployment],
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

    @staticmethod
    def _manifest(
        *,
        namespace: str,
        name: str,
        labels: Mapping[str, str],
        selector: Mapping[str, str],
        pod_template: PodTemplateSpec,
        replicas: int,
        annotations: Mapping[str, str] | None,
        strategy: DeploymentStrategyManifest | None,
        min_ready_seconds: int | None,
        progress_deadline_seconds: int | None,
        revision_history_limit: int | None,
        paused: bool | None,
    ) -> dict[str, object]:
        template_labels = dict(labels)
        template_labels.update(pod_template.labels)
        template_labels.update(selector)
        spec: dict[str, object] = {
            "replicas": replicas,
            "selector": {"matchLabels": dict(selector)},
            "template": replace(pod_template, labels=template_labels)._manifest(),
        }
        if strategy is not None:
            spec["strategy"] = dict(strategy)
        optional: dict[str, object | None] = {
            "minReadySeconds": min_ready_seconds,
            "progressDeadlineSeconds": progress_deadline_seconds,
            "revisionHistoryLimit": revision_history_limit,
            "paused": paused,
        }
        spec.update(
            {key: value for key, value in optional.items() if value is not None}
        )
        return {
            "apiVersion": "apps/v1",
            "kind": "Deployment",
            "metadata": {
                "name": name,
                "namespace": namespace,
                "labels": dict(labels),
                "annotations": dict(annotations or {}),
            },
            "spec": spec,
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
        replicas: int = 1,
        annotations: Mapping[str, str] | None = None,
        strategy: DeploymentStrategyManifest | None = None,
        min_ready_seconds: int | None = None,
        progress_deadline_seconds: int | None = None,
        revision_history_limit: int | None = None,
        paused: bool | None = None,
    ) -> Deployment:
        """Create or patch one Kubernetes Deployment from intent-level fields.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the Deployment.
        name : str
            Deployment name to create or patch.
        labels : Mapping[str, str]
            Labels to apply to the Deployment and pod template.
        selector : Mapping[str, str]
            Immutable pod selector labels for the Deployment.
        pod_template : PodTemplateSpec
            Pod template to render into the Deployment.
        deadline : Deadline
            Maximum request budget in seconds. If infinite, wait indefinitely.
        replicas : int, optional
            Desired replica count.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to `metadata.annotations`.
        strategy : DeploymentStrategyManifest | None, optional
            Optional Deployment rollout strategy.
        min_ready_seconds : int | None, optional
            Optional number of seconds a Pod must stay ready before availability.
        progress_deadline_seconds : int | None, optional
            Optional number of seconds before a rollout is considered stalled.
        revision_history_limit : int | None, optional
            Optional number of old ReplicaSets to retain.
        paused : bool | None, optional
            Optional flag controlling whether rollout progress is paused.

        Returns
        -------
        Deployment
            Wrapped created or patched Deployment.

        Raises
        ------
        ValueError
            If replica or rollout timing settings are invalid.
        OSError
            If Kubernetes returns malformed data or the API call fails.
        """
        namespace = namespace.strip()
        name = name.strip()
        if not namespace or not name:
            msg = "Deployment upsert requires non-empty namespace and name"
            raise OSError(msg)
        if replicas < 0:
            msg = "Deployment replicas cannot be negative"
            raise ValueError(msg)
        for label, value in (
            ("min ready seconds", min_ready_seconds),
            ("progress deadline seconds", progress_deadline_seconds),
            ("revision history limit", revision_history_limit),
        ):
            if value is not None and value < 0:
                msg = f"Deployment {label} cannot be negative"
                raise ValueError(msg)

        manifest = cls._manifest(
            namespace=namespace,
            name=name,
            labels=labels,
            selector=selector,
            pod_template=pod_template,
            replicas=replicas,
            annotations=annotations,
            strategy=strategy,
            min_ready_seconds=min_ready_seconds,
            progress_deadline_seconds=progress_deadline_seconds,
            revision_history_limit=revision_history_limit,
            paused=paused,
        )

        return await cls.upsert_manifest(
            kube,
            namespace=namespace,
            name=name,
            manifest=manifest,
            deadline=deadline,
        )

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
        """
        if minimum < 1:
            msg = "minimum available Deployment replicas must be positive"
            raise ValueError(msg)
        namespace, name = _resource_namespace_name(
            self, "wait for Deployment availability"
        )
        return await self.wait_until(
            kube,
            deadline=deadline,
            predicate=lambda live: live.available_replicas >= minimum,
            pending_message=f"Deployment {namespace}/{name} is not available yet",
            missing_message=(
                f"Deployment {namespace}/{name} disappeared while waiting for "
                "availability"
            ),
            timeout_message=(
                f"timed out waiting for Deployment {namespace}/{name} availability"
            ),
        )

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
        """
        if minimum < 1:
            msg = "minimum rolled out Deployment replicas must be positive"
            raise ValueError(msg)
        namespace, name = _resource_namespace_name(self, "wait for Deployment rollout")
        target_generation = self.generation
        return await self.wait_until(
            kube,
            deadline=deadline,
            predicate=lambda live: (
                (
                    target_generation <= 0
                    or live.observed_generation >= target_generation
                )
                and live.updated_replicas >= minimum
                and live.available_replicas >= minimum
            ),
            pending_message=(
                f"Deployment {namespace}/{name} rollout is not complete yet"
            ),
            missing_message=(
                f"Deployment {namespace}/{name} disappeared while waiting for rollout"
            ),
            timeout_message=(
                f"timed out waiting for Deployment {namespace}/{name} rollout"
            ),
        )

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
        namespace, name = _resource_namespace_name(self, "scale Deployment")
        payload = await kube.run(
            lambda request_timeout: kube.apps.patch_namespaced_deployment_scale(
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
