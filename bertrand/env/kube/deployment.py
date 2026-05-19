"""Wrappers for the Kubernetes Deployment API and related operations."""

from __future__ import annotations

from dataclasses import dataclass, replace
from types import MappingProxyType
from typing import TYPE_CHECKING, Self

import kubernetes

from bertrand.env.git import until

from .api._render import (
    _pod_template_manifest,
)
from .api.metadata import NamespacedKubeMetadata
from .api.resource import ResourceClient

DEPLOYMENT_WAIT_POLL_INTERVAL_SECONDS = 0.5

if TYPE_CHECKING:
    import builtins
    from collections.abc import AsyncIterator, Collection, Mapping

    from .api.client import Kube
    from .api.spec import DeploymentStrategySpec, PodTemplateSpec
    from .api.watch import WatchEvent


@dataclass(frozen=True)
class Deployment(NamespacedKubeMetadata[kubernetes.client.V1Deployment]):
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

    @classmethod
    def _client(
        cls,
    ) -> ResourceClient[kubernetes.client.V1Deployment, Self]:
        return ResourceClient(
            scope="namespaced",
            kind="Deployment",
            expected=kubernetes.client.V1Deployment,
            list_type=kubernetes.client.V1DeploymentList,
            wrapper=lambda payload: cls(_obj=payload),
            read=lambda kube, namespace, name, request_timeout: (
                kube.apps.read_namespaced_deployment(
                    name=name,
                    namespace=namespace,
                    _request_timeout=request_timeout,
                )
            ),
            list_all=lambda kube, label_selector, field_selector, request_timeout: (
                kube.apps.list_deployment_for_all_namespaces(
                    label_selector=label_selector,
                    field_selector=field_selector,
                    _request_timeout=request_timeout,
                )
            ),
            list_namespace=lambda kube, namespace, labels, fields, timeout: (
                kube.apps.list_namespaced_deployment(
                    namespace=namespace,
                    label_selector=labels,
                    field_selector=fields,
                    _request_timeout=timeout,
                )
            ),
            create=lambda kube, namespace, _name, manifest, request_timeout: (
                kube.apps.create_namespaced_deployment(
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                )
            ),
            patch=lambda kube, namespace, name, manifest, request_timeout: (
                kube.apps.patch_namespaced_deployment(
                    name=name,
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                )
            ),
            delete=lambda kube, namespace, name, request_timeout: (
                kube.apps.delete_namespaced_deployment(
                    name=name,
                    namespace=namespace,
                    _request_timeout=request_timeout,
                )
            ),
            watch_all=lambda kube: kube.apps.list_deployment_for_all_namespaces,
            watch_namespace=lambda kube: kube.apps.list_namespaced_deployment,
        )

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        timeout: float,
        name: str,
    ) -> Self | None:
        """Read one Kubernetes Deployment by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the Deployment.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        name : str
            Deployment name to read.

        Returns
        -------
        Deployment | None
            Wrapped Kubernetes Deployment, or `None` if it does not exist.
        """
        return await cls._client().get(
            kube,
            namespace=namespace,
            name=name,
            timeout=timeout,
        )

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        timeout: float,
        namespaces: Collection[str] | None = None,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[Self]:
        """List Kubernetes Deployments with optional namespace and label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        namespaces : Collection[str] | None, optional
            Optional namespace filters. `None` queries all namespaces. Otherwise,
            entries are trimmed, deduplicated, and queried individually.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.

        Returns
        -------
        list[Deployment]
            Wrapped Kubernetes Deployments matching the requested filters.
        """
        return await cls._client().list(
            kube,
            timeout=timeout,
            namespaces=namespaces,
            labels=labels,
        )

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
        """Watch Kubernetes Deployments.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum watch budget in seconds. If infinite, wait indefinitely.
        namespace : str | None, optional
            Namespace to watch. If omitted, watches Deployments across all
            namespaces.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.
        field_selector : str | None, optional
            Raw Kubernetes field selector.
        resource_version : str | None, optional
            Resource version to watch from.

        Yields
        ------
        WatchEvent[Deployment]
            Typed watch events containing wrapped Deployments.
        """
        async for event in cls._client().watch(
            kube,
            timeout=timeout,
            namespace=namespace,
            labels=labels,
            field_selector=field_selector,
            resource_version=resource_version,
        ):
            yield event

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
        strategy: DeploymentStrategySpec | None,
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
            "template": _pod_template_manifest(
                replace(pod_template, labels=template_labels)
            ),
        }
        if strategy is not None:
            strategy_type = strategy.kind.strip()
            if strategy_type:
                payload: dict[str, object] = {"type": strategy_type}
                if strategy_type == "Recreate":
                    payload["rollingUpdate"] = None
                else:
                    rolling_update: dict[str, object] = {}
                    if strategy.max_surge is not None:
                        rolling_update["maxSurge"] = strategy.max_surge
                    if strategy.max_unavailable is not None:
                        rolling_update["maxUnavailable"] = strategy.max_unavailable
                    if rolling_update:
                        payload["rollingUpdate"] = rolling_update
                spec["strategy"] = payload
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
        timeout: float,
        replicas: int = 1,
        annotations: Mapping[str, str] | None = None,
        strategy: DeploymentStrategySpec | None = None,
        min_ready_seconds: int | None = None,
        progress_deadline_seconds: int | None = None,
        revision_history_limit: int | None = None,
        paused: bool | None = None,
    ) -> Self:
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
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        replicas : int, optional
            Desired replica count.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to `metadata.annotations`.
        strategy : DeploymentStrategySpec | None, optional
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

        return await cls._client().upsert(
            kube,
            namespace=namespace,
            name=name,
            manifest=manifest,
            timeout=timeout,
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

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this Deployment by its metadata namespace and name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Deployment | None
            Fresh wrapper for the same Deployment, or `None` if it no longer exists.
        """
        namespace, name = self._require_namespace_name("refresh Deployment")
        return await type(self).get(
            kube,
            namespace=namespace,
            timeout=timeout,
            name=name,
        )

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this Deployment from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        namespace, name = self._require_namespace_name("delete Deployment")
        await (
            type(self)
            ._client()
            .delete_by_name(
                kube,
                namespace=namespace,
                name=name,
                timeout=timeout,
            )
        )

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this Deployment is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        """
        namespace, name = self._require_namespace_name("wait for Deployment deletion")
        await (
            type(self)
            ._client()
            .wait_deleted(
                label=self._object_label(name=name, namespace=namespace),
                timeout=timeout,
                refresh=lambda remaining: self.refresh(kube, timeout=remaining),
            )
        )

    async def wait_available(
        self,
        kube: Kube,
        *,
        timeout: float,
        minimum: int = 1,
    ) -> Self:
        """Wait until this Deployment has at least `minimum` available replicas.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
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
        TimeoutError
            If the Deployment does not become available before `timeout`.
        """
        if minimum < 1:
            msg = "minimum available Deployment replicas must be positive"
            raise ValueError(msg)
        namespace, name = self._require_namespace_name(
            "wait for Deployment availability"
        )
        live: Self = self

        async def available(remaining: float) -> Self:
            nonlocal live
            refreshed = await live.refresh(kube, timeout=remaining)
            if refreshed is None:
                msg = (
                    f"Deployment {namespace}/{name} disappeared while waiting for "
                    "availability"
                )
                raise OSError(msg)
            if refreshed.available_replicas >= minimum:
                return refreshed
            live = refreshed
            msg = f"Deployment {namespace}/{name} is not available yet"
            raise TimeoutError(msg)

        try:
            return await until(
                available,
                timeout=timeout,
                interval=DEPLOYMENT_WAIT_POLL_INTERVAL_SECONDS,
                action=f"waiting for Deployment {namespace}/{name} availability",
            )
        except TimeoutError as err:
            msg = f"timed out waiting for Deployment {namespace}/{name} availability"
            raise TimeoutError(msg) from err

    async def wait_rollout(
        self,
        kube: Kube,
        *,
        timeout: float,
        minimum: int = 1,
    ) -> Self:
        """Wait until this Deployment rolls out at least `minimum` replicas.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
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
        TimeoutError
            If the Deployment rollout does not complete before `timeout`.
        """
        if minimum < 1:
            msg = "minimum rolled out Deployment replicas must be positive"
            raise ValueError(msg)
        namespace, name = self._require_namespace_name("wait for Deployment rollout")
        target_generation = self.generation
        live: Self = self

        async def rolled_out(remaining: float) -> Self:
            nonlocal live
            refreshed = await live.refresh(kube, timeout=remaining)
            if refreshed is None:
                msg = (
                    f"Deployment {namespace}/{name} disappeared while waiting for "
                    "rollout"
                )
                raise OSError(msg)
            generation_observed = (
                target_generation <= 0
                or refreshed.observed_generation >= target_generation
            )
            replicas_updated = refreshed.updated_replicas >= minimum
            replicas_available = refreshed.available_replicas >= minimum
            if generation_observed and replicas_updated and replicas_available:
                return refreshed
            live = refreshed
            msg = f"Deployment {namespace}/{name} rollout is not complete yet"
            raise TimeoutError(msg)

        try:
            return await until(
                rolled_out,
                timeout=timeout,
                interval=DEPLOYMENT_WAIT_POLL_INTERVAL_SECONDS,
                action=f"waiting for Deployment {namespace}/{name} rollout",
            )
        except TimeoutError as err:
            msg = f"timed out waiting for Deployment {namespace}/{name} rollout"
            raise TimeoutError(msg) from err

    async def scale(self, kube: Kube, *, replicas: int, timeout: float) -> Self:
        """Patch this Deployment's desired replica count.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        replicas : int
            Desired replica count. Must be non-negative.
        timeout : float
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
        namespace, name = self._require_namespace_name("scale Deployment")
        payload = await kube.run(
            lambda request_timeout: kube.apps.patch_namespaced_deployment_scale(
                name=name,
                namespace=namespace,
                body={"spec": {"replicas": replicas}},
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to scale Deployment {namespace}/{name}",
        )
        if payload is None:
            msg = f"Deployment {namespace}/{name} disappeared while scaling"
            raise OSError(msg)
        live = await type(self).get(
            kube,
            namespace=namespace,
            timeout=timeout,
            name=name,
        )
        if live is None:
            msg = f"Deployment {namespace}/{name} disappeared after scaling"
            raise OSError(msg)
        return live
