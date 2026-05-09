"""Wrappers for the Kubernetes DaemonSet API and related operations."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING, Self

import kubernetes

from .api import (
    ContainerSpec,
    ImagePullSecretSpec,
    Kube,
    PodSecurityContextSpec,
    TolerationSpec,
    VolumeSpec,
    WatchEvent,
    _label_selector,
    _pod_template_manifest,
)

DAEMONSET_WAIT_POLL_INTERVAL_SECONDS = 0.5

if TYPE_CHECKING:
    import builtins
    from collections.abc import AsyncIterator, Collection, Mapping
    from datetime import datetime


@dataclass(frozen=True)
class DaemonSet:
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

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or the API call fails.
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
        if not isinstance(payload, kubernetes.client.V1DaemonSet):
            msg = (
                f"malformed Kubernetes DaemonSet payload for {name!r} "
                f"in namespace {namespace!r}"
            )
            raise OSError(msg)
        return cls(_obj=payload)

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

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or a list call fails.
        """
        label_selector = _label_selector(labels)
        payloads: builtins.list[kubernetes.client.V1DaemonSetList] = []
        if namespaces is None:
            payload = await kube.run(
                lambda request_timeout: kube.apps.list_daemon_set_for_all_namespaces(
                    label_selector=label_selector,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context="failed to list DaemonSets across all namespaces",
            )
            if payload is not None:
                payloads.append(payload)
        else:
            normalized = {namespace.strip() for namespace in namespaces}
            normalized.discard("")
            for namespace in sorted(normalized):
                payload = await kube.run(
                    lambda request_timeout, namespace=namespace: (
                        kube.apps.list_namespaced_daemon_set(
                            namespace=namespace,
                            label_selector=label_selector,
                            _request_timeout=request_timeout,
                        )
                    ),
                    timeout=timeout,
                    context=f"failed to list DaemonSets in namespace {namespace!r}",
                )
                if payload is not None:
                    payloads.append(payload)

        out: builtins.list[Self] = []
        for payload in payloads:
            if not isinstance(payload, kubernetes.client.V1DaemonSetList):
                msg = "malformed Kubernetes DaemonSet list payload"
                raise OSError(msg)
            for item in payload.items or []:
                if not isinstance(item, kubernetes.client.V1DaemonSet):
                    msg = "malformed Kubernetes DaemonSet entry in list payload"
                    raise OSError(msg)
                out.append(cls(_obj=item))
        return out

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
        namespace = namespace.strip() if namespace is not None else ""
        if namespace:
            fn = kube.apps.list_namespaced_daemon_set
            api_kwargs: Mapping[str, object] = {"namespace": namespace}
            context = f"failed to watch DaemonSets in namespace {namespace!r}"
        else:
            fn = kube.apps.list_daemon_set_for_all_namespaces
            api_kwargs = {}
            context = "failed to watch DaemonSets across all namespaces"

        async for event in kube.watch(
            fn,
            wrapper=cls._watch_payload,
            timeout=timeout,
            context=context,
            resource_version=resource_version,
            labels=labels,
            field_selector=field_selector,
            api_kwargs=api_kwargs,
        ):
            yield event

    @classmethod
    def _watch_payload(cls, payload: object) -> Self:
        if not isinstance(payload, kubernetes.client.V1DaemonSet):
            msg = "malformed Kubernetes DaemonSet watch payload"
            raise OSError(msg)
        return cls(_obj=payload)

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
        service_account_name: str | None,
        node_selector: Mapping[str, str] | None,
        host_pid: bool | None,
        pod_security_context: PodSecurityContextSpec | Mapping[str, object] | None,
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
        service_account_name: str | None = None,
        node_selector: Mapping[str, str] | None = None,
        host_pid: bool | None = None,
        pod_security_context: PodSecurityContextSpec
        | Mapping[str, object]
        | None = None,
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
        service_account_name : str | None, optional
            Optional pod service account name.
        node_selector : Mapping[str, str] | None, optional
            Optional pod node selector.
        host_pid : bool | None, optional
            Optional pod `hostPID` value.
        pod_security_context : PodSecurityContextSpec | Mapping | None, optional
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
            If namespace/name are empty, or Kubernetes create/patch fails or returns
            malformed data.
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
        try:
            created = await kube.run(
                lambda request_timeout: kube.apps.create_namespaced_daemon_set(
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=f"failed to create DaemonSet {namespace}/{name}",
            )
        except OSError as err:
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise
        else:
            if not isinstance(created, kubernetes.client.V1DaemonSet):
                msg = f"malformed Kubernetes DaemonSet payload while creating {name!r}"
                raise OSError(msg)
            return cls(_obj=created)

        patched = await kube.run(
            lambda request_timeout: kube.apps.patch_namespaced_daemon_set(
                name=name,
                namespace=namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to patch DaemonSet {namespace}/{name}",
        )
        if not isinstance(patched, kubernetes.client.V1DaemonSet):
            msg = f"malformed Kubernetes DaemonSet payload while patching {name!r}"
            raise OSError(msg)
        return cls(_obj=patched)

    @property
    def name(self) -> str:
        """Return this DaemonSet's name.

        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        metadata = self._obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def namespace(self) -> str:
        """Return this DaemonSet's namespace.

        Returns
        -------
        str
            Trimmed `metadata.namespace`, or an empty string when unavailable.
        """
        metadata = self._obj.metadata
        return (metadata.namespace or "").strip() if metadata is not None else ""

    @property
    def labels(self) -> Mapping[str, str]:
        """Return this DaemonSet's labels.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.labels`, or an empty mapping when unavailable.
        """
        metadata = self._obj.metadata
        if metadata is None or metadata.labels is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.labels)

    @property
    def annotations(self) -> Mapping[str, str]:
        """Return this DaemonSet's annotations.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.annotations`, or an empty mapping when
            unavailable.
        """
        metadata = self._obj.metadata
        if metadata is None or metadata.annotations is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.annotations)

    @property
    def resource_version(self) -> str:
        """Return this DaemonSet's resource version.

        Returns
        -------
        str
            Kubernetes `metadata.resourceVersion`, or an empty string when
            unavailable.
        """
        metadata = self._obj.metadata
        return (metadata.resource_version or "").strip() if metadata is not None else ""

    @property
    def uid(self) -> str:
        """Return this DaemonSet's UID.

        Returns
        -------
        str
            Kubernetes `metadata.uid`, or an empty string when unavailable.
        """
        metadata = self._obj.metadata
        return (metadata.uid or "").strip() if metadata is not None else ""

    @property
    def created_at(self) -> datetime | None:
        """Return this DaemonSet's creation timestamp.

        Returns
        -------
        datetime | None
            Kubernetes `metadata.creationTimestamp`, or `None` when unavailable.
        """
        metadata = self._obj.metadata
        return metadata.creation_timestamp if metadata is not None else None

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

        Raises
        ------
        OSError
            If this wrapper does not contain enough metadata to identify the
            DaemonSet, or if Kubernetes returns malformed data.
        """
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = "cannot refresh DaemonSet with missing metadata.name/namespace"
            raise OSError(msg)
        return await type(self).get(
            kube, namespace=namespace, name=name, timeout=timeout
        )

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this DaemonSet from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Raises
        ------
        OSError
            If this wrapper does not contain enough metadata to identify the
            DaemonSet, if the delete request fails, or if Kubernetes returns
            malformed data.
        """
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = "cannot delete DaemonSet with missing metadata.name/namespace"
            raise OSError(msg)
        payload = await kube.run(
            lambda request_timeout: kube.apps.delete_namespaced_daemon_set(
                name=name,
                namespace=namespace,
                body=kubernetes.client.V1DeleteOptions(),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete DaemonSet {namespace}/{name}",
        )
        if payload is not None and not isinstance(payload, kubernetes.client.V1Status):
            msg = (
                "malformed Kubernetes response while deleting DaemonSet "
                f"{namespace}/{name}"
            )
            raise OSError(msg)

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this DaemonSet is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        Raises
        ------
        OSError
            If this wrapper does not contain enough metadata to identify the
            DaemonSet, or if a refresh request returns malformed data.
        TimeoutError
            If the DaemonSet still exists when `timeout` expires.
        """
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = (
                "cannot wait for DaemonSet deletion with missing "
                "metadata.name/namespace"
            )
            raise OSError(msg)
        if timeout <= 0:
            msg = f"timed out waiting for DaemonSet {namespace}/{name} deletion"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                msg = f"timed out waiting for DaemonSet {namespace}/{name} deletion"
                raise TimeoutError(msg)
            live = await self.refresh(kube, timeout=remaining)
            if live is None:
                return
            await asyncio.sleep(min(DAEMONSET_WAIT_POLL_INTERVAL_SECONDS, remaining))

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
        OSError
            If this wrapper cannot be refreshed or disappears while waiting.
        """
        if minimum < 0:
            msg = "DaemonSet availability minimum cannot be negative"
            raise ValueError(msg)
        if timeout <= 0:
            msg = f"timed out waiting for DaemonSet {self.namespace}/{self.name}"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        current: Self = self
        while True:
            if current.number_available >= minimum:
                return current
            remaining = deadline - loop.time()
            if remaining <= 0:
                msg = f"timed out waiting for DaemonSet {self.namespace}/{self.name}"
                raise TimeoutError(msg)
            await asyncio.sleep(min(DAEMONSET_WAIT_POLL_INTERVAL_SECONDS, remaining))
            refreshed = await current.refresh(kube, timeout=deadline - loop.time())
            if refreshed is None:
                msg = f"DaemonSet {self.namespace}/{self.name} disappeared"
                raise OSError(msg)
            current = refreshed
