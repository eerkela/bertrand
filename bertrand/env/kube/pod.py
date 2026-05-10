"""Wrappers for the Kubernetes Pod API and related pod-scoped operations."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
from typing import TYPE_CHECKING, Self

import kubernetes

from .api import (
    Kube,
    NamespacedKubeMetadata,
    WatchEvent,
)
from .api._helpers import (
    _list_namespaced_items,
    _typed_payload,
    _validate_delete_status,
    _wait_until_deleted,
)
from .api.watch import (
    _watch_namespaced_resource,
)

if TYPE_CHECKING:
    import builtins
    from collections.abc import AsyncIterator, Collection, Mapping

POD_MIRROR_ANNOTATION = "kubernetes.io/config.mirror"
POD_SUPPORTED_CONTROLLER_KINDS = frozenset(
    {
        "ReplicationController",
        "ReplicaSet",
        "StatefulSet",
        "DaemonSet",
        "Job",
    }
)
POD_ACTIVE_PHASES = frozenset({"Pending", "Running", "Unknown"})
POD_TERMINAL_PHASES = frozenset({"Succeeded", "Failed"})
POD_WAIT_POLL_INTERVAL_SECONDS = 0.5


@dataclass(frozen=True)
class Pod(NamespacedKubeMetadata[kubernetes.client.V1Pod]):
    """General-purpose wrapper around one Kubernetes Pod object.

    Parameters
    ----------
    _obj : kubernetes.client.V1Pod
        Typed Kubernetes Pod payload returned by the cluster API.
    """

    _obj: kubernetes.client.V1Pod

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        timeout: float,
        name: str,
    ) -> Self | None:
        """Read one Kubernetes Pod by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            The namespace to search within.
        timeout : float
            The maximum time to wait for Kubernetes pod query in seconds.  If
            infinite, wait indefinitely.
        name : str
            Pod name to read.

        Returns
        -------
        Pod | None
            Validated Kubernetes pod wrapper, or `None` if the pod does not exist.
        """
        payload = await kube.run(
            lambda request_timeout: kube.core.read_namespaced_pod(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to read pod {name!r} in namespace {namespace!r}",
        )
        if payload is None:
            return None
        return cls(_obj=_typed_payload(payload, kubernetes.client.V1Pod, context="Pod"))

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        timeout: float,
        namespaces: Collection[str] | None = None,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[Self]:
        """List Kubernetes Pods with optional namespace and label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            The maximum time to wait for Kubernetes pod list queries in seconds.  If
            infinite, wait indefinitely.
        namespaces : Collection[str] | None, optional
            Optional namespace filters.  `None` queries all namespaces.  Otherwise,
            names are normalized (trimmed), deduplicated, and queried individually.
        labels : Mapping[str, str] | None, optional
            Optional label filters.

        Returns
        -------
        builtins.list[Pod]
            Validated Kubernetes pod wrappers.
        """
        return [
            cls(_obj=item)
            for item in await _list_namespaced_items(
                kube,
                timeout=timeout,
                namespaces=namespaces,
                labels=labels,
                list_all=lambda label_selector, request_timeout: (
                    kube.core.list_pod_for_all_namespaces(
                        label_selector=label_selector,
                        _request_timeout=request_timeout,
                    )
                ),
                list_namespace=lambda namespace, label_selector, request_timeout: (
                    kube.core.list_namespaced_pod(
                        namespace=namespace,
                        label_selector=label_selector,
                        _request_timeout=request_timeout,
                    )
                ),
                list_type=kubernetes.client.V1PodList,
                item_type=kubernetes.client.V1Pod,
                all_context="failed to list pods across all namespaces",
                namespace_context=lambda namespace: (
                    f"failed to list pods in namespace {namespace!r}"
                ),
                list_context="Pod",
                item_context="Pod",
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
        """Watch Kubernetes Pods.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum watch budget in seconds. If infinite, wait indefinitely.
        namespace : str | None, optional
            Namespace to watch. If omitted, watches Pods across all namespaces.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.
        field_selector : str | None, optional
            Raw Kubernetes field selector.
        resource_version : str | None, optional
            Resource version to watch from.

        Yields
        ------
        WatchEvent[Pod]
            Typed watch events containing wrapped Pods.
        """
        async for event in _watch_namespaced_resource(
            kube,
            expected=kubernetes.client.V1Pod,
            wrapper=lambda payload: cls(_obj=payload),
            timeout=timeout,
            namespace=namespace,
            resource_version=resource_version,
            labels=labels,
            field_selector=field_selector,
            watch_all=kube.core.list_pod_for_all_namespaces,
            watch_namespace=kube.core.list_namespaced_pod,
            all_context="failed to watch Pods across all namespaces",
            namespace_context=lambda namespace: (
                f"failed to watch Pods in namespace {namespace!r}"
            ),
            payload_context="Pod watch",
        ):
            yield event

    @property
    def phase(self) -> str:
        """Return the current Pod phase.

        Returns
        -------
        str
            Current pod phase value, or an empty string when unavailable.
        """
        status = self._obj.status
        return (status.phase or "").strip() if status is not None else ""

    @property
    def is_terminating(self) -> bool:
        """Return whether the Pod is terminating.

        Returns
        -------
        bool
            `True` when `metadata.deletion_timestamp` is present.
        """
        metadata = self._obj.metadata
        return bool(metadata.deletion_timestamp) if metadata is not None else False

    @property
    def is_active(self) -> bool:
        """Return whether the Pod is active.

        Returns
        -------
        bool
            `True` when the pod is not terminating and phase is one of
            `Pending|Running|Unknown`.
        """
        return not self.is_terminating and self.phase in POD_ACTIVE_PHASES

    @property
    def is_terminal(self) -> bool:
        """Return whether the Pod is terminal.

        Returns
        -------
        bool
            `True` when pod phase is `Succeeded` or `Failed`.
        """
        return self.phase in POD_TERMINAL_PHASES

    @property
    def is_mirror(self) -> bool:
        """Return whether the Pod is a static mirror pod.

        Returns
        -------
        bool
            `True` when this pod is a static mirror pod.
        """
        return POD_MIRROR_ANNOTATION in self.annotations

    @property
    def is_daemonset_controlled(self) -> bool:
        """Return whether the Pod is controlled by a DaemonSet.

        Returns
        -------
        bool
            `True` when a controller owner-reference of kind `DaemonSet` exists.
        """
        metadata = self._obj.metadata
        owners = (metadata.owner_references or []) if metadata is not None else []
        for owner in owners:
            if (owner.kind or "").strip() != "DaemonSet":
                continue
            if owner.controller is None or owner.controller:
                return True
        return False

    def has_supported_controller(
        self,
        kinds: frozenset[str] = POD_SUPPORTED_CONTROLLER_KINDS,
    ) -> bool:
        """Return whether this pod is controlled by a supported owner kind.

        Parameters
        ----------
        kinds : frozenset[str], optional
            Allowed owner kinds considered safe for drain orchestration.

        Returns
        -------
        bool
            `True` when a controller owner-reference matches one of `kinds`.
        """
        metadata = self._obj.metadata
        owners = (metadata.owner_references or []) if metadata is not None else []
        for owner in owners:
            if (owner.kind or "").strip() not in kinds:
                continue
            if owner.controller is None or owner.controller:
                return True
        return False

    @property
    def uses_emptydir(self) -> bool:
        """Return whether the Pod uses any `emptyDir` volume.

        Returns
        -------
        bool
            `True` when this pod spec includes at least one `emptyDir` volume.
        """
        spec = self._obj.spec
        for volume in (spec.volumes or []) if spec is not None else []:
            if volume.empty_dir is not None:
                return True
        return False

    @property
    def persistent_volume_claim_names(self) -> tuple[str, ...]:
        """Return referenced PersistentVolumeClaim names.

        Returns
        -------
        tuple[str, ...]
            Distinct PVC claim names referenced by this pod, preserving spec order.
        """
        spec = self._obj.spec
        seen: set[str] = set()
        out: builtins.list[str] = []
        for volume in (spec.volumes or []) if spec is not None else []:
            pvc = volume.persistent_volume_claim
            if pvc is None:
                continue
            claim_name = (pvc.claim_name or "").strip()
            if not claim_name or claim_name in seen:
                continue
            seen.add(claim_name)
            out.append(claim_name)
        return tuple(out)

    async def logs(
        self,
        kube: Kube,
        *,
        timeout: float,
        container: str | None = None,
        tail_lines: int | None = None,
    ) -> str:
        """Read this Pod's logs.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        container : str | None, optional
            Optional container name. If omitted, Kubernetes selects the default
            container when possible.
        tail_lines : int | None, optional
            Optional number of log lines to return from the end of the stream.

        Returns
        -------
        str
            Pod log text returned by Kubernetes.

        Raises
        ------
        ValueError
            If `tail_lines` is not positive.
        OSError
            If Kubernetes returns malformed log data.
        """
        namespace, name = self._require_namespace_name("read pod logs")
        if tail_lines is not None and tail_lines <= 0:
            msg = "pod log tail_lines must be positive"
            raise ValueError(msg)
        container = container.strip() if container is not None else None
        if container == "":
            container = None

        payload = await kube.run(
            lambda request_timeout: kube.core.read_namespaced_pod_log(
                name=name,
                namespace=namespace,
                container=container,
                tail_lines=tail_lines,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to read logs for pod {namespace}/{name}",
        )
        if payload is None:
            return ""
        if isinstance(payload, bytes):
            return payload.decode("utf-8", errors="replace")
        if not isinstance(payload, str):
            msg = f"malformed Kubernetes log payload for pod {namespace}/{name}"
            raise OSError(msg)
        return payload

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this pod by its metadata namespace and name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.

        Returns
        -------
        Pod | None
            Fresh pod wrapper, or `None` if the pod no longer exists.
        """
        namespace, name = self._require_namespace_name("refresh pod")
        return await type(self).get(
            kube,
            namespace=namespace,
            timeout=timeout,
            name=name,
        )

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this pod is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum runtime budget in seconds. If infinite, wait indefinitely.
        """
        namespace, name = self._require_namespace_name("wait for pod deletion")
        await _wait_until_deleted(
            label=self._object_label(name=name, namespace=namespace),
            timeout=timeout,
            refresh=lambda remaining: self.refresh(kube, timeout=remaining),
        )

    async def wait_terminal(self, kube: Kube, *, timeout: float) -> Self:
        """Wait until this pod reaches a terminal phase.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.

        Returns
        -------
        Pod
            Latest pod wrapper once phase converges to `Succeeded` or `Failed`.

        Raises
        ------
        TimeoutError
            If terminal phase convergence does not complete within `timeout`.
        OSError
            If the pod is deleted before reaching a terminal phase.
        """
        namespace, name = self._require_namespace_name("wait for pod terminal phase")
        if timeout <= 0:
            msg = f"timed out waiting for pod {namespace}/{name} terminal phase"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                msg = f"timed out waiting for pod {namespace}/{name} terminal phase"
                raise TimeoutError(msg)
            live = await self.refresh(kube, timeout=remaining)
            if live is None:
                msg = (
                    f"pod {namespace}/{name} was deleted before reaching a "
                    "terminal phase"
                )
                raise OSError(msg)
            if live.is_terminal:
                return live
            await asyncio.sleep(min(POD_WAIT_POLL_INTERVAL_SECONDS, remaining))

    async def evict(
        self,
        kube: Kube,
        *,
        timeout: float,
    ) -> None:
        """Evict this pod via the policy eviction subresource.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.
        """
        namespace, name = self._require_namespace_name("evict pod")

        # policy/v1 eviction is preferred over delete because it respects
        # PodDisruptionBudgets and communicates scheduling intent explicitly.
        body = kubernetes.client.V1Eviction(
            api_version="policy/v1",
            kind="Eviction",
            metadata=kubernetes.client.V1ObjectMeta(name=name, namespace=namespace),
            delete_options=None,
        )
        await kube.run(
            lambda request_timeout: kube.core.create_namespaced_pod_eviction(
                name=name,
                namespace=namespace,
                body=body,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to evict pod {namespace}/{name}",
        )

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this pod from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.
        """
        namespace, name = self._require_namespace_name("delete pod")

        payload = await kube.run(
            lambda request_timeout: kube.core.delete_namespaced_pod(
                name=name,
                namespace=namespace,
                body=kubernetes.client.V1DeleteOptions(),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete pod {namespace}/{name}",
        )
        _validate_delete_status(
            payload, label=self._object_label(name=name, namespace=namespace)
        )
