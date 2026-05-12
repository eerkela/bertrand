"""Wrappers for the Kubernetes DaemonSet API and related operations."""

from __future__ import annotations

from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING, Self

import kubernetes

from bertrand.env.git import until

from .api import (
    ContainerSpec,
    ImagePullSecretSpec,
    Kube,
    NamespacedKubeMetadata,
    PodSecurityContextSpec,
    TolerationSpec,
    VolumeSpec,
    WatchEvent,
)
from .api._helpers import (
    _create_or_patch,
    _list_namespaced_items,
    _typed_payload,
    _validate_delete_status,
    _wait_until_deleted,
)
from .api._render import (
    _pod_template_manifest,
)
from .api.watch import (
    _watch_namespaced_resource,
)

DAEMONSET_WAIT_POLL_INTERVAL_SECONDS = 0.5

if TYPE_CHECKING:
    import builtins
    from collections.abc import AsyncIterator, Collection, Mapping


@dataclass(frozen=True)
class DaemonSet(NamespacedKubeMetadata[kubernetes.client.V1DaemonSet]):
    """General-purpose wrapper around one Kubernetes DaemonSet object."""

    _obj: kubernetes.client.V1DaemonSet

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
        payload = await kube.run(
            lambda request_timeout: kube.apps.read_namespaced_daemon_set(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to read DaemonSet {name!r} in namespace {namespace!r}",
        )
        if payload is None:
            return None
        return cls(
            _obj=_typed_payload(
                payload,
                kubernetes.client.V1DaemonSet,
                context="DaemonSet",
            )
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
        return [
            cls(_obj=item)
            for item in await _list_namespaced_items(
                kube,
                timeout=timeout,
                namespaces=namespaces,
                labels=labels,
                list_all=lambda label_selector, request_timeout: (
                    kube.apps.list_daemon_set_for_all_namespaces(
                        label_selector=label_selector,
                        _request_timeout=request_timeout,
                    )
                ),
                list_namespace=lambda namespace, label_selector, request_timeout: (
                    kube.apps.list_namespaced_daemon_set(
                        namespace=namespace,
                        label_selector=label_selector,
                        _request_timeout=request_timeout,
                    )
                ),
                list_type=kubernetes.client.V1DaemonSetList,
                item_type=kubernetes.client.V1DaemonSet,
                all_context="failed to list DaemonSets across all namespaces",
                namespace_context=lambda namespace: (
                    f"failed to list DaemonSets in namespace {namespace!r}"
                ),
                list_context="DaemonSet",
                item_context="DaemonSet",
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
        async for event in _watch_namespaced_resource(
            expected=kubernetes.client.V1DaemonSet,
            wrapper=lambda payload: cls(_obj=payload),
            timeout=timeout,
            namespace=namespace,
            resource_version=resource_version,
            labels=labels,
            field_selector=field_selector,
            watch_all=kube.apps.list_daemon_set_for_all_namespaces,
            watch_namespace=kube.apps.list_namespaced_daemon_set,
            all_context="failed to watch DaemonSets across all namespaces",
            namespace_context=lambda namespace: (
                f"failed to watch DaemonSets in namespace {namespace!r}"
            ),
            payload_context="DaemonSet watch",
        ):
            yield event

    @staticmethod
    def _manifest(
        *,
        namespace: str,
        name: str,
        labels: Mapping[str, str],
        selector: Mapping[str, str],
        containers: Collection[ContainerSpec],
        volumes: Collection[VolumeSpec],
        automount_service_account_token: bool,
        annotations: Mapping[str, str] | None,
        pod_annotations: Mapping[str, str] | None,
        service_account_name: str | None,
        node_selector: Mapping[str, str] | None,
        host_pid: bool | None,
        pod_security_context: PodSecurityContextSpec | None,
        tolerations: Collection[TolerationSpec],
        image_pull_secrets: Collection[ImagePullSecretSpec],
        priority_class_name: str | None,
        dns_policy: str | None,
        host_network: bool | None,
        termination_grace_period_seconds: int | None,
    ) -> dict[str, object]:
        template_labels = dict(labels)
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
                    labels=template_labels,
                    pod_annotations=pod_annotations,
                    containers=containers,
                    volumes=volumes,
                    automount_service_account_token=automount_service_account_token,
                    service_account_name=service_account_name,
                    node_selector=node_selector,
                    host_pid=host_pid,
                    pod_security_context=pod_security_context,
                    tolerations=tolerations,
                    image_pull_secrets=image_pull_secrets,
                    priority_class_name=priority_class_name,
                    dns_policy=dns_policy,
                    host_network=host_network,
                    termination_grace_period_seconds=(termination_grace_period_seconds),
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
        containers: Collection[ContainerSpec],
        volumes: Collection[VolumeSpec],
        timeout: float,
        automount_service_account_token: bool = False,
        annotations: Mapping[str, str] | None = None,
        pod_annotations: Mapping[str, str] | None = None,
        service_account_name: str | None = None,
        node_selector: Mapping[str, str] | None = None,
        host_pid: bool | None = None,
        pod_security_context: PodSecurityContextSpec | None = None,
        tolerations: Collection[TolerationSpec] = (),
        image_pull_secrets: Collection[ImagePullSecretSpec] = (),
        priority_class_name: str | None = None,
        dns_policy: str | None = None,
        host_network: bool | None = None,
        termination_grace_period_seconds: int | None = None,
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
        containers : Collection[ContainerSpec]
            Pod containers to render into the DaemonSet template.
        volumes : Collection[VolumeSpec]
            Pod volumes to render into the DaemonSet template.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        automount_service_account_token : bool, optional
            Whether pods should automount the default service-account token.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to `metadata.annotations`.
        pod_annotations : Mapping[str, str] | None, optional
            Annotations to apply to pod template `metadata.annotations`.
        service_account_name : str | None, optional
            Optional pod service account name.
        node_selector : Mapping[str, str] | None, optional
            Optional pod node selector.
        host_pid : bool | None, optional
            Optional pod `hostPID` value.
        pod_security_context : PodSecurityContextSpec | None, optional
            Optional pod security context.
        tolerations : Collection[TolerationSpec], optional
            Optional pod tolerations.
        image_pull_secrets : Collection[ImagePullSecretSpec], optional
            Optional image pull Secret references.
        priority_class_name : str | None, optional
            Optional pod priority class name.
        dns_policy : str | None, optional
            Optional pod DNS policy.
        host_network : bool | None, optional
            Optional pod `hostNetwork` value.
        termination_grace_period_seconds : int | None, optional
            Optional pod termination grace period in seconds.

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
            containers=containers,
            volumes=volumes,
            automount_service_account_token=automount_service_account_token,
            annotations=annotations,
            pod_annotations=pod_annotations,
            service_account_name=service_account_name,
            node_selector=node_selector,
            host_pid=host_pid,
            pod_security_context=pod_security_context,
            tolerations=tolerations,
            image_pull_secrets=image_pull_secrets,
            priority_class_name=priority_class_name,
            dns_policy=dns_policy,
            host_network=host_network,
            termination_grace_period_seconds=termination_grace_period_seconds,
        )
        payload = await _create_or_patch(
            kube,
            timeout=timeout,
            create=lambda request_timeout: kube.apps.create_namespaced_daemon_set(
                namespace=namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            patch=lambda request_timeout: kube.apps.patch_namespaced_daemon_set(
                name=name,
                namespace=namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            create_context=f"failed to create DaemonSet {namespace}/{name}",
            patch_context=f"failed to patch DaemonSet {namespace}/{name}",
            expected=kubernetes.client.V1DaemonSet,
            payload_context="DaemonSet",
        )
        return cls(_obj=payload)

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
        payload = await kube.run(
            lambda request_timeout: kube.apps.delete_namespaced_daemon_set(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete DaemonSet {namespace}/{name}",
        )
        _validate_delete_status(
            payload, label=self._object_label(name=name, namespace=namespace)
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
        await _wait_until_deleted(
            label=self._object_label(name=name, namespace=namespace),
            timeout=timeout,
            refresh=lambda remaining: self.refresh(kube, timeout=remaining),
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
