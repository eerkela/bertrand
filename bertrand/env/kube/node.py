"""Wrappers for the Kubernetes Node API and related operations."""
from __future__ import annotations

import asyncio
import builtins
from collections.abc import Collection, Mapping
from dataclasses import dataclass
from types import MappingProxyType
from typing import Literal, Self

import kubernetes

from ..config.core import KubeName
from .api import Kube, _label_selector
from .pod import Pod

NODE_SYSTEM_NAMESPACES = frozenset({
    "kube-system",
    "kube-public",
    "kube-node-lease",
})
NODE_DRAIN_POLL_INTERVAL_SECONDS = 0.5
NODE_TAINT_KEY_NOT_READY = "node.kubernetes.io/not-ready"
NODE_TAINT_KEY_UNREACHABLE = "node.kubernetes.io/unreachable"
NODE_TAINT_KEY_MEMORY_PRESSURE = "node.kubernetes.io/memory-pressure"
NODE_TAINT_KEY_DISK_PRESSURE = "node.kubernetes.io/disk-pressure"
NODE_TAINT_KEY_UNSCHEDULABLE = "node.kubernetes.io/unschedulable"


type TaintEffect = Literal["NoSchedule", "PreferNoSchedule", "NoExecute"]


@dataclass(frozen=True)
class Node:
    """General-purpose wrapper around one Kubernetes Node object.

    This wrapper exposes a small, typed surface for common node introspection and
    mutation operations so downstream modules can avoid reimplementing Kubernetes
    node-shape parsing.
    """
    obj: kubernetes.client.V1Node

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        timeout: float,
        name: KubeName,
    ) -> Self | None:
        """Read one Kubernetes Node by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            The maximum time to wait for Kubernetes node queries in seconds.  If
            infinite, wait indefinitely.
        name : str | None, optional
            Node name to read.

        Returns
        -------
        Node | None
            Validated Kubernetes node wrapper, or `None` if the node does not exist.

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds the timeout budget.
        OSError
            If the Kubernetes API call fails or returns malformed data.
        """
        payload = await kube.run(
            lambda timeout: kube.core.read_node(
                name=name,
                _request_timeout=timeout,
            ),
            timeout=timeout,
            context=f"failed to read Kubernetes node {name!r}",
        )
        if payload is None:
            return None
        if not isinstance(payload, kubernetes.client.V1Node):
            raise OSError(f"malformed Kubernetes node payload for {name!r}")
        return cls(obj=payload)

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[Self]:
        """List Kubernetes Nodes with optional label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            The maximum time to wait for Kubernetes node list queries in seconds.  If
            infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Optional label filters.

        Returns
        -------
        builtins.list[Node]
            Validated Kubernetes node wrappers.

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds the timeout budget.
        OSError
            If the Kubernetes API call fails or returns malformed data.
        """
        payload = await kube.run(
            lambda timeout: kube.core.list_node(
                label_selector=_label_selector(labels),
                _request_timeout=timeout,
            ),
            timeout=timeout,
            context="failed to list Kubernetes nodes",
        )
        if payload is None:
            return []
        if not isinstance(payload, kubernetes.client.V1NodeList):
            raise OSError("malformed Kubernetes node list payload")
        out: builtins.list[Self] = []
        for item in payload.items or []:
            if not isinstance(item, kubernetes.client.V1Node):
                raise OSError("malformed Kubernetes node entry in list payload")
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
        if metadata is None:
            return ""
        return (metadata.name or "").strip()

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
    def hostname(self) -> str:
        """
        Returns
        -------
        str
            Value of `kubernetes.io/hostname`, or an empty string when missing.
        """
        return self.labels.get("kubernetes.io/hostname", "").strip()

    @property
    def internal_ips(self) -> tuple[str, ...]:
        """
        Returns
        -------
        tuple[str, ...]
            All non-empty `InternalIP` addresses in reported node order.
        """
        status = self.obj.status
        out: builtins.list[str] = []
        for address in (status.addresses or []) if status is not None else []:
            if (address.type or "").strip() != "InternalIP":
                continue
            value = (address.address or "").strip()
            if value:
                out.append(value)
        return tuple(out)

    @property
    def external_ips(self) -> tuple[str, ...]:
        """
        Returns
        -------
        tuple[str, ...]
            All non-empty `ExternalIP` addresses in reported node order.
        """
        status = self.obj.status
        out: builtins.list[str] = []
        for address in (status.addresses or []) if status is not None else []:
            if (address.type or "").strip() != "ExternalIP":
                continue
            value = (address.address or "").strip()
            if value:
                out.append(value)
        return tuple(out)

    @property
    def roles(self) -> set[str]:
        """
        Returns
        -------
        set[str]
            Role names extracted from `node-role.kubernetes.io/*` keys.
        """
        out: set[str] = set()
        for key in self.labels:
            if key.startswith("node-role.kubernetes.io/"):
                role = key.removeprefix("node-role.kubernetes.io/").strip()
                out.add(role if role else "control-plane")
        return out

    @property
    def is_control_plane(self) -> bool:
        """
        Returns
        -------
        bool
            `True` when this node has either control-plane or master role labels.
        """
        labels = self.labels
        return (
            "node-role.kubernetes.io/control-plane" in labels or
            "node-role.kubernetes.io/master" in labels
        )

    @property
    def is_ready(self) -> bool:
        """
        Returns
        -------
        bool
            `True` when the `Ready` condition exists and has status `True`.
        """
        status = self.obj.status
        for condition in (status.conditions or []) if status is not None else []:
            if (condition.type or "").strip() != "Ready":
                continue
            return (condition.status or "").strip().lower() == "true"
        return False

    @property
    def is_schedulable(self) -> bool:
        """
        Returns
        -------
        bool
            `False` only when `spec.unschedulable` is explicitly true.
        """
        spec = self.obj.spec
        return not bool(spec.unschedulable) if spec is not None else True

    @property
    def taints(self) -> tuple[kubernetes.client.V1Taint, ...]:
        """
        Returns
        -------
        tuple[kubernetes.client.V1Taint, ...]
            Immutable snapshot of taints, or an empty tuple when none exist.
        """
        spec = self.obj.spec
        if spec is None or spec.taints is None:
            return ()
        return tuple(spec.taints)

    async def _patch(
        self,
        *,
        kube: Kube,
        body: dict[str, object],
        timeout: float,
        context: str,
    ) -> kubernetes.client.V1Node:
        name = self.name
        if not name:
            raise OSError("cannot patch Kubernetes node with missing metadata.name")
        payload = await kube.run(
            lambda timeout: kube.core.patch_node(
                name=name,
                body=body,
                _request_timeout=timeout,
            ),
            timeout=timeout,
            context=context,
        )
        if payload is None:
            raise OSError(f"unable to patch Kubernetes node {name!r}: node not found")
        if not isinstance(payload, kubernetes.client.V1Node):
            raise OSError(f"malformed Kubernetes node patch response for {name!r}")
        return payload

    async def set_label(
        self,
        kube: Kube,
        *,
        label: str,
        value: str,
        timeout: float,
    ) -> None:
        """Apply or overwrite one node label.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        label : str
            Label key to apply.
        value : str
            Label value to apply.
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.

        Raises
        ------
        TimeoutError
            If the patch operation exceeds the timeout budget.
        OSError
            If the Kubernetes patch call fails.
        """
        await self._patch(
            kube=kube,
            body={"metadata": {"labels": {label: value}}},
            timeout=timeout,
            context=f"failed to set label {label!r} on Kubernetes node {self.name!r}",
        )

    async def remove_label(
        self,
        kube: Kube,
        *,
        label: str,
        timeout: float,
    ) -> None:
        """Remove one node label when present.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        label : str
            Label key to remove.
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.

        Raises
        ------
        TimeoutError
            If the patch operation exceeds the timeout budget.
        OSError
            If the Kubernetes patch call fails.
        """
        labels = dict(self.labels)
        if label not in labels:
            return
        labels.pop(label, None)
        await self._patch(
            kube=kube,
            body={"metadata": {"labels": labels}},
            timeout=timeout,
            context=f"failed to remove label {label!r} from Kubernetes node {self.name!r}",
        )

    async def set_annotation(
        self,
        kube: Kube,
        *,
        key: str,
        value: str,
        timeout: float,
    ) -> None:
        """Apply or overwrite one node annotation.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        key : str
            Annotation key to apply.
        value : str
            Annotation value to apply.
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.

        Raises
        ------
        TimeoutError
            If the patch operation exceeds the timeout budget.
        OSError
            If the Kubernetes patch call fails.
        """
        await self._patch(
            kube=kube,
            body={"metadata": {"annotations": {key: value}}},
            timeout=timeout,
            context=f"failed to set annotation {key!r} on Kubernetes node {self.name!r}",
        )

    async def remove_annotation(
        self,
        kube: Kube,
        *,
        key: str,
        timeout: float,
    ) -> None:
        """Remove one node annotation when present.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        key : str
            Annotation key to remove.
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.

        Raises
        ------
        TimeoutError
            If the patch operation exceeds the timeout budget.
        OSError
            If the Kubernetes patch call fails.
        """
        annotations = dict(self.annotations)
        if key not in annotations:
            return
        annotations.pop(key, None)
        await self._patch(
            kube=kube,
            body={"metadata": {"annotations": annotations}},
            timeout=timeout,
            context=f"failed to remove annotation {key!r} from Kubernetes node {self.name!r}",
        )

    async def cordon(self, kube: Kube, *, timeout: float) -> None:
        """Mark this node unschedulable.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.

        Raises
        ------
        TimeoutError
            If the patch operation exceeds the timeout budget.
        OSError
            If the Kubernetes patch call fails.
        """
        await self._patch(
            kube=kube,
            body={"spec": {"unschedulable": True}},
            timeout=timeout,
            context=f"failed to cordon Kubernetes node {self.name!r}",
        )

    async def uncordon(self, kube: Kube, *, timeout: float) -> None:
        """Mark this node schedulable.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.

        Raises
        ------
        TimeoutError
            If the patch operation exceeds the timeout budget.
        OSError
            If the Kubernetes patch call fails.
        """
        await self._patch(
            kube=kube,
            body={"spec": {"unschedulable": False}},
            timeout=timeout,
            context=f"failed to uncordon Kubernetes node {self.name!r}",
        )

    async def set_taint(
        self,
        kube: Kube,
        *,
        key: str,
        effect: TaintEffect,
        value: str | None,
        timeout: float,
    ) -> None:
        """Upsert one node taint by `(key, effect)`.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        key : str
            Taint key.
        effect : TaintEffect
            Taint effect (`NoSchedule`, `PreferNoSchedule`, or `NoExecute`).
        value : str | None
            Optional taint value.
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.

        Raises
        ------
        TimeoutError
            If the patch operation exceeds the timeout budget.
        OSError
            If the Kubernetes patch call fails.
        """
        # normalize to one taint per (key, effect) pair so repeated calls converge
        # instead of duplicating entries
        taints = list(self.taints)
        normalized: builtins.list[kubernetes.client.V1Taint] = []
        replaced = False
        for taint in taints:
            if (taint.key or "") == key and (taint.effect or "") == effect:
                if replaced:
                    continue
                replaced = True
                normalized.append(
                    kubernetes.client.V1Taint(key=key, effect=effect, value=value)
                )
                continue
            normalized.append(taint)
        if not replaced:
            normalized.append(
                kubernetes.client.V1Taint(key=key, effect=effect, value=value)
            )
        payload = [
            {
                "key": (taint.key or ""),
                "effect": (taint.effect or ""),
                **({"value": taint.value} if taint.value else {}),
            }
            for taint in normalized
            if (taint.key or "").strip() and (taint.effect or "").strip()
        ]
        await self._patch(
            kube=kube,
            body={"spec": {"taints": payload}},
            timeout=timeout,
            context=(
                f"failed to set taint {key!r}/{effect!r} on Kubernetes node {self!r}"
            ),
        )

    async def remove_taint(
        self,
        kube: Kube,
        *,
        key: str,
        effect: TaintEffect | None,
        timeout: float,
    ) -> None:
        """Remove taints matching `key` and optional `effect`.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        key : str
            Taint key to remove.
        effect : TaintEffect | None
            Optional effect filter.  If omitted, all effects for `key` are removed.
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.

        Raises
        ------
        TimeoutError
            If the patch operation exceeds the timeout budget.
        OSError
            If the Kubernetes patch call fails.
        """
        payload = [
            {
                "key": (taint.key or ""),
                "effect": (taint.effect or ""),
                **({"value": taint.value} if taint.value else {}),
            }
            for taint in self.taints
            if not (
                (taint.key or "") == key and
                (effect is None or (taint.effect or "") == effect)
            )
            if (taint.key or "").strip() and (taint.effect or "").strip()
        ]
        await self._patch(
            kube=kube,
            body={"spec": {"taints": payload}},
            timeout=timeout,
            context=(
                f"failed to remove taint {key!r}/{effect or '*'} on Kubernetes node "
                f"{self.name!r}"
            ),
        )

    async def pods(
        self,
        kube: Kube,
        *,
        timeout: float,
        labels: Mapping[str, str] | None = None,
        namespaces: Collection[str] | None = None,
    ) -> builtins.list[Pod]:
        """List pods scheduled onto this node across all namespaces.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Optional pod label selector filters.
        namespaces : Collection[str] | None, optional
            Optional namespace filter.  If omitted, query all namespaces.  If
            provided, names are normalized (trimmed), deduplicated, and queried
            individually.  An explicitly empty filter resolves to no results.

        Returns
        -------
        builtins.list[Pod]
            Pod wrappers for all matching pods assigned to this node.

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds the timeout budget.
        OSError
            If this node has no name, or if the API payload is malformed.
        """
        node_name = self.name
        if not node_name:
            raise OSError("cannot query pods for Kubernetes node with missing name")
        label_selector = _label_selector(labels)

        # if no namespace filter is given, do a single cluster-wide query
        payloads: builtins.list[kubernetes.client.V1PodList] = []
        if namespaces is None:
            payload = await kube.run(
                lambda timeout: kube.core.list_pod_for_all_namespaces(
                    field_selector=f"spec.nodeName={node_name}",
                    label_selector=label_selector,
                    _request_timeout=timeout,
                ),
                timeout=timeout,
                context=f"failed to list pods on Kubernetes node {node_name!r}",
            )
            if payload is not None:
                payloads.append(payload)

        # otherwise, query each namespace individually and aggregate results
        else:
            _namespaces = {namespace.strip() for namespace in namespaces}
            _namespaces.discard("")
            for namespace in _namespaces:
                payload = await kube.run(
                    lambda timeout, namespace=namespace: kube.core.list_namespaced_pod(
                        namespace=namespace,
                        field_selector=f"spec.nodeName={node_name}",
                        label_selector=label_selector,
                        _request_timeout=timeout,
                    ),
                    timeout=timeout,
                    context=(
                        f"failed to list pods in namespace {namespace!r} on "
                        f"Kubernetes node {node_name!r}"
                    ),
                )
                if payload is not None:
                    payloads.append(payload)

        # verify each payload and convert to Pod wrappers
        out: builtins.list[Pod] = []
        for payload in payloads:
            if not isinstance(payload, kubernetes.client.V1PodList):
                raise OSError(
                    f"malformed pod list payload for Kubernetes node {node_name!r}"
                )
            for item in payload.items or []:
                if not isinstance(item, kubernetes.client.V1Pod):
                    raise OSError(
                        f"malformed pod entry while listing Kubernetes node "
                        f"{node_name!r}"
                    )
                out.append(Pod(obj=item))
        return out

    async def drain(
        self,
        kube: Kube,
        *,
        timeout: float,
        force: bool = False,
    ) -> None:
        """Drain this node with safety-first defaults and one escalation flag.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.
        force : bool, optional
            Escalation toggle for disruptive evictions.  When False (default), drain
            skips system namespaces and blocks `emptyDir`/unmanaged pods.  When True,
            those pods are considered evictable.

        Raises
        ------
        TimeoutError
            If eviction admission or post-eviction convergence exceed timeout.
        OSError
            If the node cannot be drained safely under the selected policy.

        Notes
        -----
        This follows the safe posture of `kubectl drain`: cordon first, skip mirror
        pods, always skip DaemonSets, respect PDB backpressure (429 retries), and
        require explicit force for potentially disruptive cases.
        """
        if timeout <= 0:
            raise TimeoutError("node drain timeout must be non-negative")
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout

        # cordon first to stop new placements while eviction converges
        await self.cordon(kube=kube, timeout=deadline - loop.time())
        pods = await self.pods(kube=kube, timeout=deadline - loop.time())

        # classification is explicit so refusal reasons are user-actionable before any
        # disruptive eviction requests are submitted
        candidates: builtins.list[Pod] = []
        blocked: builtins.list[str] = []
        for pod in pods:
            identity = pod.identity
            if identity is None or not pod.is_active or pod.is_mirror:
                continue
            namespace, name = identity

            # DaemonSet pods are always skipped because they are managed to run
            # per-node; drain should not treat them as evictable workload pods
            if pod.is_daemonset_controlled or (
                namespace in NODE_SYSTEM_NAMESPACES and
                not force
            ):
                continue
            if pod.uses_emptydir and not force:
                blocked.append(
                    f"{namespace}/{name}: uses emptyDir volume (set force=True to allow)"
                )
                continue
            if not pod.has_supported_controller() and not force:
                blocked.append(
                    f"{namespace}/{name}: unmanaged pod (set force=True to allow)"
                )
                continue
            candidates.append(pod)
        if blocked:
            details = "\n".join(f"  - {line}" for line in sorted(blocked))
            raise OSError(
                f"refusing to drain Kubernetes node {self.name!r} due to non-evictable "
                f"pods:\n{details}"
            )

        # submit evictions first, then track completion separately so PDB retries and
        # convergence polling stay decoupled and predictable
        pending: set[tuple[str, str]] = set()
        for pod in candidates:
            identity = pod.identity
            if identity is not None:
                pending.add(identity)
        for pod in candidates:
            while True:
                try:
                    # we intentionally pass no grace override so each individual
                    # workload's terminationGracePeriodSeconds remains authoritative
                    await pod.evict(kube, timeout=deadline - loop.time())
                    break
                except OSError as err:
                    detail = str(err).lower()
                    if "status 429" not in detail and "too many requests" not in detail:
                        raise
                    remaining = deadline - loop.time()
                    if remaining <= 0:
                        raise TimeoutError(
                            f"timed out waiting for PDB eviction budget while draining "
                            f"node {self.name!r}"
                        ) from err
                    await asyncio.sleep(
                        min(NODE_DRAIN_POLL_INTERVAL_SECONDS, remaining)
                    )

        # eviction admission does not guarantee immediate termination, so we poll
        # node-local pod state until every targeted pod disappears
        while pending:
            remaining = deadline - loop.time()
            if remaining <= 0:
                raise TimeoutError(
                    f"timed out waiting for pod eviction convergence on node "
                    f"{self.name!r}; remaining: {', '.join(
                        f'{namespace}/{name}' for namespace, name in sorted(pending)
                    )}"
                )
            live: set[tuple[str, str]] = set()
            for pod in await self.pods(kube=kube, timeout=remaining):
                identity = pod.identity
                if identity is not None:
                    live.add(identity)
            pending.intersection_update(live)
            if pending:
                # eviction admission is asynchronous, so we poll until all targeted
                # pods terminate or timeout
                await asyncio.sleep(
                    min(NODE_DRAIN_POLL_INTERVAL_SECONDS, deadline - loop.time())
                )
