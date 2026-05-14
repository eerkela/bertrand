"""Wrappers for the Kubernetes Node API and related operations."""

from __future__ import annotations

import asyncio
import os
import platform
from dataclasses import dataclass
from typing import TYPE_CHECKING, Literal, Self

import kubernetes

from .api.metadata import KubeMetadata
from .api.resource import ResourceClient
from .api.view import TaintView
from .pod import Pod

if TYPE_CHECKING:
    import builtins
    from collections.abc import AsyncIterator, Collection, Mapping

    from .api.client import Kube
    from .api.watch import WatchEvent

NODE_SYSTEM_NAMESPACES = frozenset(
    {
        "kube-system",
        "kube-public",
        "kube-node-lease",
    }
)
NODE_DRAIN_POLL_INTERVAL_SECONDS = 0.5
NODE_TAINT_KEY_NOT_READY = "node.kubernetes.io/not-ready"
NODE_TAINT_KEY_UNREACHABLE = "node.kubernetes.io/unreachable"
NODE_TAINT_KEY_MEMORY_PRESSURE = "node.kubernetes.io/memory-pressure"
NODE_TAINT_KEY_DISK_PRESSURE = "node.kubernetes.io/disk-pressure"
NODE_TAINT_KEY_UNSCHEDULABLE = "node.kubernetes.io/unschedulable"
NODE_ARCH_ALIASES = {
    "x86_64": "amd64",
    "amd64": "amd64",
    "aarch64": "arm64",
    "arm64": "arm64",
}


type TaintEffect = Literal["NoSchedule", "PreferNoSchedule", "NoExecute"]


@dataclass(frozen=True)
class Node(KubeMetadata[kubernetes.client.V1Node]):
    """General-purpose wrapper around one Kubernetes Node object.

    Parameters
    ----------
    _obj : kubernetes.client.V1Node
        Typed Kubernetes Node payload returned by the cluster API.

    Notes
    -----
    This wrapper exposes a small, typed surface for common node introspection and
    mutation operations so downstream modules can avoid reimplementing Kubernetes
    node-shape parsing.
    """

    _obj: kubernetes.client.V1Node

    @classmethod
    def _client(cls) -> ResourceClient[kubernetes.client.V1Node, Self]:
        return ResourceClient(
            scope="cluster",
            kind="Node",
            expected=kubernetes.client.V1Node,
            list_type=kubernetes.client.V1NodeList,
            wrapper=lambda payload: cls(_obj=payload),
            read=lambda kube, _namespace, name, request_timeout: kube.core.read_node(
                name=name,
                _request_timeout=request_timeout,
            ),
            list_all=lambda kube, label_selector, field_selector, request_timeout: (
                kube.core.list_node(
                    label_selector=label_selector,
                    field_selector=field_selector,
                    _request_timeout=request_timeout,
                )
            ),
            watch_all=lambda kube: kube.core.list_node,
        )

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        timeout: float,
        name: str,
    ) -> Self | None:
        """Read one Kubernetes Node by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            The maximum time to wait for Kubernetes node queries in seconds.  If
            infinite, wait indefinitely.
        name : str
            Node name to read.

        Returns
        -------
        Node | None
            Validated Kubernetes node wrapper, or `None` if the node does not exist.

        """
        return await cls._client().get(kube, name=name, timeout=timeout)

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

        """
        return await cls._client().list(kube, timeout=timeout, labels=labels)

    @classmethod
    async def watch(
        cls,
        kube: Kube,
        *,
        timeout: float,
        labels: Mapping[str, str] | None = None,
        resource_version: str | None = None,
    ) -> AsyncIterator[WatchEvent[Self]]:
        """Watch Kubernetes Nodes.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum watch budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.
        resource_version : str | None, optional
            Resource version to watch from.

        Yields
        ------
        WatchEvent[Node]
            Typed watch events containing wrapped Nodes.
        """
        async for event in cls._client().watch(
            kube,
            timeout=timeout,
            labels=labels,
            resource_version=resource_version,
        ):
            yield event

    @classmethod
    async def local(cls, kube: Kube, *, timeout: float) -> Self:
        """Resolve the Kubernetes Node for the current host.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Node
            Kubernetes node wrapper matching the current host identity.

        Raises
        ------
        OSError
            If no node can be matched, the matched node has no name, or multiple
            nodes make host identity ambiguous.
        """
        nodes = await cls.list(kube=kube, timeout=timeout)
        if not nodes:
            msg = "Kubernetes node list is empty"
            raise OSError(msg)

        hints = {
            platform.node().strip(),
            os.uname().nodename.strip() if hasattr(os, "uname") else "",
            os.environ.get("HOSTNAME", "").strip(),
        }
        hints.discard("")

        for node in nodes:
            if node.matches_identity(hints):
                if not node.name:
                    msg = "matched local Kubernetes node is missing metadata.name"
                    raise OSError(msg)
                return node

        if len(nodes) == 1:
            node = nodes[0]
            if not node.name:
                msg = "single Kubernetes node is missing metadata.name"
                raise OSError(msg)
            return node

        names = ", ".join(sorted(node.name for node in nodes if node.name))
        msg = (
            "unable to map host identity to a unique Kubernetes node name; "
            f"available nodes: {names}"
        )
        raise OSError(msg)

    @property
    def hostname(self) -> str:
        """Return the Kubernetes hostname label.

        Returns
        -------
        str
            Value of `kubernetes.io/hostname`, or an empty string when missing.
        """
        return self.labels.get("kubernetes.io/hostname", "").strip()

    @property
    def platform(self) -> str:
        """Return the node's OCI platform string.

        Returns
        -------
        str
            Canonical platform string such as ``"linux/amd64"``, or an empty
            string when the Kubernetes OS or architecture labels are missing.
        """
        labels = self.labels
        os_name = labels.get("kubernetes.io/os", "").strip().lower()
        arch = labels.get("kubernetes.io/arch", "").strip().lower()
        arch = NODE_ARCH_ALIASES.get(arch, arch)
        return f"{os_name}/{arch}" if os_name and arch else ""

    @property
    def is_build_eligible(self) -> bool:
        """Return whether this node can host a native BuildKit builder.

        Returns
        -------
        bool
            ``True`` when the node is ready, schedulable, Linux, and has a valid
            platform label pair.
        """
        return (
            self.is_ready and self.is_schedulable and self.platform.startswith("linux/")
        )

    @property
    def addresses(self) -> tuple[str, ...]:
        """Return reported Node addresses.

        Returns
        -------
        tuple[str, ...]
            All non-empty reported node addresses in Kubernetes API order.
        """
        status = self._obj.status
        out: builtins.list[str] = []
        for address in (status.addresses or []) if status is not None else []:
            value = (address.address or "").strip()
            if value:
                out.append(value)
        return tuple(out)

    @property
    def identity_values(self) -> frozenset[str]:
        """Return values that can identify this Node.

        Returns
        -------
        frozenset[str]
            Non-empty identity values that can refer to this node, including
            `metadata.name`, the Kubernetes hostname label, and reported addresses.
        """
        values = {self.name, self.hostname, *self.addresses}
        values.discard("")
        return frozenset(values)

    def matches_identity(self, hints: Collection[str]) -> bool:
        """Check whether this node matches any host identity hint.

        Parameters
        ----------
        hints : Collection[str]
            Candidate host identity strings such as hostnames or IP addresses.

        Returns
        -------
        bool
            `True` when any non-empty hint matches this node's identity values.
        """
        normalized = {hint.strip() for hint in hints if hint and hint.strip()}
        return bool(normalized & self.identity_values)

    @property
    def internal_ips(self) -> tuple[str, ...]:
        """Return reported internal IP addresses.

        Returns
        -------
        tuple[str, ...]
            All non-empty `InternalIP` addresses in reported node order.
        """
        status = self._obj.status
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
        """Return reported external IP addresses.

        Returns
        -------
        tuple[str, ...]
            All non-empty `ExternalIP` addresses in reported node order.
        """
        status = self._obj.status
        out: builtins.list[str] = []
        for address in (status.addresses or []) if status is not None else []:
            if (address.type or "").strip() != "ExternalIP":
                continue
            value = (address.address or "").strip()
            if value:
                out.append(value)
        return tuple(out)

    @property
    def roles(self) -> frozenset[str]:
        """Return Kubernetes role labels.

        Returns
        -------
        frozenset[str]
            Role names extracted from `node-role.kubernetes.io/*` keys.
        """
        out: set[str] = set()
        for key in self.labels:
            if key.startswith("node-role.kubernetes.io/"):
                role = key.removeprefix("node-role.kubernetes.io/").strip()
                out.add(role if role else "control-plane")
        return frozenset(out)

    @property
    def is_control_plane(self) -> bool:
        """Return whether this Node is a control-plane node.

        Returns
        -------
        bool
            `True` when this node has either control-plane or master role labels.
        """
        labels = self.labels
        return (
            "node-role.kubernetes.io/control-plane" in labels
            or "node-role.kubernetes.io/master" in labels
        )

    @property
    def is_ready(self) -> bool:
        """Return whether this Node currently reports Ready.

        Returns
        -------
        bool
            `True` when the `Ready` condition exists and has status `True`.
        """
        status = self._obj.status
        for condition in (status.conditions or []) if status is not None else []:
            if (condition.type or "").strip() != "Ready":
                continue
            return (condition.status or "").strip().lower() == "true"
        return False

    @property
    def is_schedulable(self) -> bool:
        """Return whether this Node accepts new pods.

        Returns
        -------
        bool
            `False` only when `spec.unschedulable` is explicitly true.
        """
        spec = self._obj.spec
        return not bool(spec.unschedulable) if spec is not None else True

    @property
    def taints(self) -> tuple[TaintView, ...]:
        """Return the Node taints.

        Returns
        -------
        tuple[TaintView, ...]
            Immutable snapshot of taint views, or an empty tuple when none exist.
        """
        spec = self._obj.spec
        if spec is None or spec.taints is None:
            return ()
        return tuple(
            TaintView(
                key=(taint.key or "").strip(),
                effect=(taint.effect or "").strip(),
                value=(taint.value or "").strip(),
            )
            for taint in spec.taints
        )

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
            msg = "cannot patch Kubernetes node with missing metadata.name"
            raise OSError(msg)
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
            msg = f"unable to patch Kubernetes node {name!r}: node not found"
            raise OSError(msg)
        if not isinstance(payload, kubernetes.client.V1Node):
            msg = f"malformed Kubernetes node patch response for {name!r}"
            raise OSError(msg)
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

        """
        labels = dict(self.labels)
        if label not in labels:
            return
        labels.pop(label, None)
        await self._patch(
            kube=kube,
            body={"metadata": {"labels": labels}},
            timeout=timeout,
            context=(
                f"failed to remove label {label!r} from Kubernetes node {self.name!r}"
            ),
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

        """
        await self._patch(
            kube=kube,
            body={"metadata": {"annotations": {key: value}}},
            timeout=timeout,
            context=(
                f"failed to set annotation {key!r} on Kubernetes node {self.name!r}"
            ),
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

        """
        annotations = dict(self.annotations)
        if key not in annotations:
            return
        annotations.pop(key, None)
        await self._patch(
            kube=kube,
            body={"metadata": {"annotations": annotations}},
            timeout=timeout,
            context=(
                f"failed to remove annotation {key!r} from Kubernetes node "
                f"{self.name!r}"
            ),
        )

    async def cordon(self, kube: Kube, *, timeout: float) -> None:
        """Mark this node unschedulable.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.

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

        """
        # normalize to one taint per (key, effect) pair so repeated calls converge
        # instead of duplicating entries
        taints = list(self.taints)
        normalized: builtins.list[TaintView] = []
        replaced = False
        for taint in taints:
            if taint.key == key and taint.effect == effect:
                if replaced:
                    continue
                replaced = True
                normalized.append(TaintView(key=key, effect=effect, value=value or ""))
                continue
            normalized.append(taint)
        if not replaced:
            normalized.append(TaintView(key=key, effect=effect, value=value or ""))
        payload = [
            {
                "key": taint.key,
                "effect": taint.effect,
                **({"value": taint.value} if taint.value else {}),
            }
            for taint in normalized
            if taint.key and taint.effect
        ]
        await self._patch(
            kube=kube,
            body={"spec": {"taints": payload}},
            timeout=timeout,
            context=(
                f"failed to set taint {key!r}/{effect!r} on Kubernetes node "
                f"{self.name!r}"
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

        """
        payload = [
            {
                "key": taint.key,
                "effect": taint.effect,
                **({"value": taint.value} if taint.value else {}),
            }
            for taint in self.taints
            if not (taint.key == key and (effect is None or taint.effect == effect))
            if taint.key and taint.effect
        ]
        await self._patch(
            kube=kube,
            body={"spec": {"taints": payload}},
            timeout=timeout,
            context=(
                f"failed to remove taint {key!r}/{effect or '*'} on Kubernetes "
                f"node {self.name!r}"
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
        OSError
            If this node has no name, or if the API payload is malformed.

        Notes
        -----
        Node-scoped pod selection is intentionally owned by `Node.pods()` so
        `Pod.list()` can remain namespace/label focused.
        """
        node_name = self.name
        if not node_name:
            msg = "cannot query pods for Kubernetes node with missing name"
            raise OSError(msg)
        return await Pod.list(
            kube,
            timeout=timeout,
            namespaces=namespaces,
            labels=labels,
            field_selector=f"spec.nodeName={node_name}",
        )

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
            msg = "node drain timeout must be non-negative"
            raise TimeoutError(msg)
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
            namespace = pod.namespace
            name = pod.name
            if not namespace or not name or not pod.is_active or pod.is_mirror:
                continue

            # DaemonSet pods are always skipped because they are managed to run
            # per-node; drain should not treat them as evictable workload pods
            if pod.is_daemonset_controlled or (
                namespace in NODE_SYSTEM_NAMESPACES and not force
            ):
                continue
            if pod.uses_emptydir and not force:
                blocked.append(
                    f"{namespace}/{name}: uses emptyDir volume "
                    "(set force=True to allow)"
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
            msg = (
                f"refusing to drain Kubernetes node {self.name!r} due to non-evictable "
                f"pods:\n{details}"
            )
            raise OSError(msg)

        # submit evictions first, then track completion separately so PDB retries and
        # convergence polling stay decoupled and predictable
        pending: set[tuple[str, str]] = set()
        for pod in candidates:
            namespace = pod.namespace
            name = pod.name
            if namespace and name:
                pending.add((namespace, name))
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
                        msg = (
                            f"timed out waiting for PDB eviction budget while draining "
                            f"node {self.name!r}"
                        )
                        raise TimeoutError(msg) from err
                    await asyncio.sleep(
                        min(NODE_DRAIN_POLL_INTERVAL_SECONDS, remaining)
                    )

        # eviction admission does not guarantee immediate termination, so we poll
        # node-local pod state until every targeted pod disappears
        while pending:
            remaining = deadline - loop.time()
            if remaining <= 0:
                remaining_pods = ", ".join(
                    f"{namespace}/{name}" for namespace, name in sorted(pending)
                )
                msg = (
                    f"timed out waiting for pod eviction convergence on node "
                    f"{self.name!r}; remaining: {remaining_pods}"
                )
                raise TimeoutError(msg)
            live: set[tuple[str, str]] = set()
            for pod in await self.pods(kube=kube, timeout=remaining):
                namespace = pod.namespace
                name = pod.name
                if namespace and name:
                    live.add((namespace, name))
            pending.intersection_update(live)
            if pending:
                # eviction admission is asynchronous, so we poll until all targeted
                # pods terminate or timeout
                await asyncio.sleep(
                    min(NODE_DRAIN_POLL_INTERVAL_SECONDS, deadline - loop.time())
                )
