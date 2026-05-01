"""Wrappers for the Kubernetes Pod API and related pod-scoped operations."""
from __future__ import annotations

import asyncio
import builtins
from collections.abc import Collection, Mapping
from dataclasses import dataclass
from types import MappingProxyType
from typing import Self

import kubernetes

from ..config.core import KubeName
from .api import Kube, _label_selector

POD_MIRROR_ANNOTATION = "kubernetes.io/config.mirror"
POD_SUPPORTED_CONTROLLER_KINDS = frozenset({
    "ReplicationController",
    "ReplicaSet",
    "StatefulSet",
    "DaemonSet",
    "Job",
})
POD_ACTIVE_PHASES = frozenset({"Pending", "Running", "Unknown"})
POD_TERMINAL_PHASES = frozenset({"Succeeded", "Failed"})
POD_WAIT_POLL_INTERVAL_SECONDS = 0.5


@dataclass(frozen=True)
class Pod:
    """General-purpose wrapper around one Kubernetes Pod object."""
    obj: kubernetes.client.V1Pod

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        timeout: float,
        name: KubeName,
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

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds the timeout budget.
        OSError
            If the Kubernetes API call fails or returns malformed data.
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
        if not isinstance(payload, kubernetes.client.V1Pod):
            raise OSError(
                f"malformed Kubernetes Pod payload for {name!r} in namespace "
                f"{namespace!r}"
            )
        return cls(obj=payload)

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

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds the timeout budget.
        OSError
            If the Kubernetes API call fails or returns malformed data.
        """
        label_selector = _label_selector(labels)
        payloads: builtins.list[kubernetes.client.V1PodList] = []

        # NOTE: Node-scoped filtering belongs to `Node.pods()` to keep ownership
        # boundaries explicit.  `Pod.list()` only handles namespace/label selectors.
        if namespaces is None:
            payload = await kube.run(
                lambda request_timeout: kube.core.list_pod_for_all_namespaces(
                    label_selector=label_selector,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context="failed to list pods across all namespaces",
            )
            if payload is not None:
                payloads.append(payload)
        else:
            normalized = {namespace.strip() for namespace in namespaces}
            normalized.discard("")
            if not normalized:
                return []
            for namespace in sorted(normalized):
                payload = await kube.run(
                    lambda request_timeout, namespace=namespace: kube.core.list_namespaced_pod(
                        namespace=namespace,
                        label_selector=label_selector,
                        _request_timeout=request_timeout,
                    ),
                    timeout=timeout,
                    context=f"failed to list pods in namespace {namespace!r}",
                )
                if payload is not None:
                    payloads.append(payload)

        out: builtins.list[Self] = []
        for payload in payloads:
            if not isinstance(payload, kubernetes.client.V1PodList):
                raise OSError("malformed Kubernetes Pod list payload")
            for item in payload.items or []:
                if not isinstance(item, kubernetes.client.V1Pod):
                    raise OSError("malformed Kubernetes Pod entry in list payload")
                out.append(cls(obj=item))
        return out

    @property
    def name(self) -> str:
        """
        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def namespace(self) -> str:
        """
        Returns
        -------
        str
            Trimmed `metadata.namespace`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.namespace or "").strip() if metadata is not None else ""

    @property
    def identity(self) -> tuple[str, str] | None:
        """
        Returns
        -------
        tuple[str, str] | None
            `(namespace, name)` when both are available, otherwise `None`.
        """
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            return None
        return namespace, name

    @property
    def labels(self) -> Mapping[str, str]:
        """
        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.labels`, or an empty mapping when unavailable.
        """
        metadata = self.obj.metadata
        if metadata is None or metadata.labels is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.labels)

    @property
    def annotations(self) -> Mapping[str, str]:
        """
        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.annotations`, or an empty mapping when
            unavailable.
        """
        metadata = self.obj.metadata
        if metadata is None or metadata.annotations is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.annotations)

    @property
    def phase(self) -> str:
        """
        Returns
        -------
        str
            Current pod phase value, or an empty string when unavailable.
        """
        status = self.obj.status
        return (status.phase or "").strip() if status is not None else ""

    @property
    def is_terminating(self) -> bool:
        """
        Returns
        -------
        bool
            `True` when `metadata.deletion_timestamp` is present.
        """
        metadata = self.obj.metadata
        return bool(metadata.deletion_timestamp) if metadata is not None else False

    @property
    def is_active(self) -> bool:
        """
        Returns
        -------
        bool
            `True` when the pod is not terminating and phase is one of
            `Pending|Running|Unknown`.
        """
        return not self.is_terminating and self.phase in POD_ACTIVE_PHASES

    @property
    def is_terminal(self) -> bool:
        """
        Returns
        -------
        bool
            `True` when pod phase is `Succeeded` or `Failed`.
        """
        return self.phase in POD_TERMINAL_PHASES

    @property
    def is_mirror(self) -> bool:
        """
        Returns
        -------
        bool
            `True` when this pod is a static mirror pod.
        """
        return POD_MIRROR_ANNOTATION in self.annotations

    @property
    def is_daemonset_controlled(self) -> bool:
        """
        Returns
        -------
        bool
            `True` when a controller owner-reference of kind `DaemonSet` exists.
        """
        metadata = self.obj.metadata
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
        metadata = self.obj.metadata
        owners = (metadata.owner_references or []) if metadata is not None else []
        for owner in owners:
            if (owner.kind or "").strip() not in kinds:
                continue
            if owner.controller is None or owner.controller:
                return True
        return False

    @property
    def uses_emptydir(self) -> bool:
        """
        Returns
        -------
        bool
            `True` when this pod spec includes at least one `emptyDir` volume.
        """
        spec = self.obj.spec
        for volume in (spec.volumes or []) if spec is not None else []:
            if volume.empty_dir is not None:
                return True
        return False

    @property
    def persistent_volume_claim_names(self) -> tuple[str, ...]:
        """
        Returns
        -------
        tuple[str, ...]
            Distinct PVC claim names referenced by this pod, preserving spec order.
        """
        spec = self.obj.spec
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

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this pod by identity.

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

        Raises
        ------
        OSError
            If this pod has no resolvable `(namespace, name)` identity.
        TimeoutError
            If the Kubernetes request exceeds the timeout budget.
        """
        identity = self.identity
        if identity is None:
            raise OSError("cannot refresh pod with missing metadata.name/namespace")
        namespace, name = identity
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
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.

        Raises
        ------
        OSError
            If this pod has no resolvable `(namespace, name)` identity.
        TimeoutError
            If deletion does not converge within `timeout`.
        """
        identity = self.identity
        if identity is None:
            raise OSError(
                "cannot wait for deletion of pod with missing metadata.name/namespace"
            )
        namespace, name = identity
        if timeout <= 0:
            raise TimeoutError(f"timed out waiting for pod {namespace}/{name} deletion")
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                raise TimeoutError(
                    f"timed out waiting for pod {namespace}/{name} deletion"
                )
            live = await self.refresh(kube, timeout=remaining)
            if live is None:
                return
            await asyncio.sleep(min(POD_WAIT_POLL_INTERVAL_SECONDS, remaining))

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
        OSError
            If this pod has no resolvable `(namespace, name)` identity, or if it is
            deleted before reaching a terminal phase.
        TimeoutError
            If terminal phase convergence does not complete within `timeout`.
        """
        identity = self.identity
        if identity is None:
            raise OSError(
                "cannot wait for terminal phase of pod with missing metadata.name/namespace"
            )
        namespace, name = identity
        if timeout <= 0:
            raise TimeoutError(
                f"timed out waiting for pod {namespace}/{name} terminal phase"
            )
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                raise TimeoutError(
                    f"timed out waiting for pod {namespace}/{name} terminal phase"
                )
            live = await self.refresh(kube, timeout=remaining)
            if live is None:
                raise OSError(
                    f"pod {namespace}/{name} was deleted before reaching a terminal phase"
                )
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

        Raises
        ------
        OSError
            If pod identity is incomplete or eviction fails.
        TimeoutError
            If the Kubernetes request exceeds the timeout budget.
        """
        identity = self.identity
        if identity is None:
            raise OSError("cannot evict pod with missing metadata.name/namespace")

        namespace, name = identity

        # NOTE: policy/v1 eviction is preferred over delete because it respects
        # PodDisruptionBudgets and communicates scheduling intent explicitly.
        # NOTE: we intentionally pass no grace override so workload-level
        # `terminationGracePeriodSeconds` stays authoritative.
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

        Raises
        ------
        OSError
            If this pod has no resolvable `(namespace, name)` identity.
        TimeoutError
            If the Kubernetes request exceeds the timeout budget.
        """
        identity = self.identity
        if identity is None:
            raise OSError("cannot delete pod with missing metadata.name/namespace")
        namespace, name = identity

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
        if payload is not None and not isinstance(
            payload,
            kubernetes.client.V1Status,
        ):
            raise OSError(
                f"malformed Kubernetes response while deleting pod {namespace}/{name}"
            )
