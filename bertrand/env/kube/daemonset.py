"""Wrappers for the Kubernetes DaemonSet API and related operations."""

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

DAEMONSET_WAIT_POLL_INTERVAL_SECONDS = 0.5

if TYPE_CHECKING:
    import builtins
    from collections.abc import AsyncIterator, Collection, Mapping

    from .api.client import Kube
    from .api.spec import PodTemplateSpec
    from .api.watch import WatchEvent


@dataclass(frozen=True)
class DaemonSet(NamespacedKubeMetadata[kubernetes.client.V1DaemonSet]):
    """General-purpose wrapper around one Kubernetes DaemonSet object."""

    _obj: kubernetes.client.V1DaemonSet

    @classmethod
    def _client(
        cls,
    ) -> ResourceClient[kubernetes.client.V1DaemonSet, Self]:
        return ResourceClient(
            scope="namespaced",
            kind="DaemonSet",
            expected=kubernetes.client.V1DaemonSet,
            list_type=kubernetes.client.V1DaemonSetList,
            wrapper=lambda payload: cls(_obj=payload),
            read=lambda kube, namespace, name, request_timeout: (
                kube.apps.read_namespaced_daemon_set(
                    name=name,
                    namespace=namespace,
                    _request_timeout=request_timeout,
                )
            ),
            list_all=lambda kube, label_selector, field_selector, request_timeout: (
                kube.apps.list_daemon_set_for_all_namespaces(
                    label_selector=label_selector,
                    field_selector=field_selector,
                    _request_timeout=request_timeout,
                )
            ),
            list_namespace=lambda kube, namespace, labels, fields, timeout: (
                kube.apps.list_namespaced_daemon_set(
                    namespace=namespace,
                    label_selector=labels,
                    field_selector=fields,
                    _request_timeout=timeout,
                )
            ),
            create=lambda kube, namespace, _name, manifest, request_timeout: (
                kube.apps.create_namespaced_daemon_set(
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                )
            ),
            patch=lambda kube, namespace, name, manifest, request_timeout: (
                kube.apps.patch_namespaced_daemon_set(
                    name=name,
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                )
            ),
            delete=lambda kube, namespace, name, request_timeout: (
                kube.apps.delete_namespaced_daemon_set(
                    name=name,
                    namespace=namespace,
                    _request_timeout=request_timeout,
                )
            ),
            watch_all=lambda kube: kube.apps.list_daemon_set_for_all_namespaces,
            watch_namespace=lambda kube: kube.apps.list_namespaced_daemon_set,
        )

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        timeout: float,
    ) -> Self | None:
        """Read one Kubernetes DaemonSet by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the DaemonSet.
        name : str
            DaemonSet name to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        DaemonSet | None
            Wrapped Kubernetes DaemonSet, or `None` if it does not exist.
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
        """List Kubernetes DaemonSets with optional namespace and label filtering.

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
        list[DaemonSet]
            Wrapped Kubernetes DaemonSets matching the requested filters.
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
        """Watch Kubernetes DaemonSets.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum watch budget in seconds. If infinite, wait indefinitely.
        namespace : str | None, optional
            Namespace to watch. If omitted, watches DaemonSets across all namespaces.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.
        field_selector : str | None, optional
            Raw Kubernetes field selector.
        resource_version : str | None, optional
            Resource version to watch from.

        Yields
        ------
        WatchEvent[DaemonSet]
            Typed watch events containing wrapped DaemonSets.
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
                "template": _pod_template_manifest(
                    replace(pod_template, labels=template_labels)
                ),
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
        timeout: float,
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
        timeout : float
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
        return await cls._client().upsert(
            kube,
            namespace=namespace,
            name=name,
            manifest=manifest,
            timeout=timeout,
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

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this DaemonSet by name and namespace.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        DaemonSet | None
            Fresh wrapper for the same DaemonSet, or `None` if it no longer exists.
        """
        namespace, name = self._require_namespace_name("refresh DaemonSet")
        return await type(self).get(
            kube,
            namespace=namespace,
            timeout=timeout,
            name=name,
        )

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this DaemonSet from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        namespace, name = self._require_namespace_name("delete DaemonSet")
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
        """Wait until this DaemonSet is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        """
        namespace, name = self._require_namespace_name("wait for DaemonSet deletion")
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
        """Wait until this DaemonSet reports at least `minimum` available pods.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
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
        TimeoutError
            If the DaemonSet does not become available before `timeout`.
        """
        if minimum < 0:
            msg = "DaemonSet availability minimum cannot be negative"
            raise ValueError(msg)
        namespace, name = self._require_namespace_name(
            "wait for DaemonSet availability"
        )
        current: Self = self

        async def available(remaining: float) -> Self:
            nonlocal current
            refreshed = await current.refresh(kube, timeout=remaining)
            if refreshed is None:
                msg = (
                    f"DaemonSet {namespace}/{name} disappeared while waiting for "
                    "availability"
                )
                raise OSError(msg)
            if refreshed.has_available_pods(minimum):
                return refreshed
            current = refreshed
            msg = f"DaemonSet {namespace}/{name} is not available yet"
            raise TimeoutError(msg)

        try:
            return await until(
                available,
                timeout=timeout,
                interval=DAEMONSET_WAIT_POLL_INTERVAL_SECONDS,
                action=f"waiting for DaemonSet {namespace}/{name} availability",
            )
        except TimeoutError as err:
            msg = f"timed out waiting for DaemonSet {namespace}/{name} availability"
            raise TimeoutError(msg) from err

    async def wait_rollout(
        self,
        kube: Kube,
        *,
        timeout: float,
        minimum: int = 1,
    ) -> Self:
        """Wait until this DaemonSet completes rollout.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
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
        TimeoutError
            If the DaemonSet does not complete rollout before `timeout`.
        """
        if minimum < 0:
            msg = "DaemonSet rollout minimum cannot be negative"
            raise ValueError(msg)
        namespace, name = self._require_namespace_name("wait for DaemonSet rollout")
        target_generation = self.generation
        current: Self = self

        async def rolled_out(remaining: float) -> Self:
            nonlocal current
            refreshed = await current.refresh(kube, timeout=remaining)
            if refreshed is None:
                msg = (
                    f"DaemonSet {namespace}/{name} disappeared while waiting for "
                    "rollout"
                )
                raise OSError(msg)
            if (
                target_generation <= 0
                or refreshed.observed_generation >= target_generation
            ) and refreshed.rollout_ready(minimum):
                return refreshed
            current = refreshed
            msg = f"DaemonSet {namespace}/{name} rollout is not complete yet"
            raise TimeoutError(msg)

        try:
            return await until(
                rolled_out,
                timeout=timeout,
                interval=DAEMONSET_WAIT_POLL_INTERVAL_SECONDS,
                action=f"waiting for DaemonSet {namespace}/{name} rollout",
            )
        except TimeoutError as err:
            msg = f"timed out waiting for DaemonSet {namespace}/{name} rollout"
            raise TimeoutError(msg) from err
