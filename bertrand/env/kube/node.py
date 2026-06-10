"""Wrappers for the Kubernetes Node API and related operations."""

from __future__ import annotations

import os
import platform
from dataclasses import dataclass
from typing import TYPE_CHECKING, ClassVar, Literal, Self

import kubernetes

from .api.client import Kube
from .api.metadata import KubeMetadata
from .api.resource import BuiltinResource, BuiltinResourceObject
from .pod import Pod

if TYPE_CHECKING:
    import builtins
    from collections.abc import Collection, Mapping

    from bertrand.env.git import Deadline

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
class TaintView:
    """Read-only Kubernetes Node taint view.

    Parameters
    ----------
    key : str
        Taint key.
    effect : str
        Taint effect, such as `"NoSchedule"`.
    value : str
        Optional taint value.
    """

    key: str
    effect: str
    value: str = ""


@dataclass(frozen=True)
class Node(
    BuiltinResourceObject[kubernetes.client.V1Node],
    KubeMetadata[kubernetes.client.V1Node],
):
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

    resource: ClassVar[BuiltinResource[kubernetes.client.V1Node]] = BuiltinResource(
        scope="cluster",
        api="core",
        kind="Node",
        slug="node",
        expected=kubernetes.client.V1Node,
        list_type=kubernetes.client.V1NodeList,
        can_watch=True,
    )

    @classmethod
    async def local(cls, kube: Kube, *, deadline: Deadline) -> Self:
        """Resolve the Kubernetes Node for the current host.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
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
        nodes = await cls.list(kube=kube, deadline=deadline)
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
            Canonical platform string such as `"linux/amd64"`, or an empty
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
            `True` when the node is ready, schedulable, Linux, and has a valid
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
    def pod_cidrs(self) -> tuple[str, ...]:
        """Return Pod CIDRs assigned to this Node.

        Returns
        -------
        tuple[str, ...]
            Non-empty CIDRs from `spec.podCIDRs` and `spec.podCIDR`.
        """
        spec = self._obj.spec
        if spec is None:
            return ()
        values = list(spec.pod_cid_rs or ())
        if spec.pod_cidr:
            values.append(spec.pod_cidr)
        out: list[str] = []
        for value in values:
            cidr = (value or "").strip()
            if cidr and cidr not in out:
                out.append(cidr)
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
        deadline: Deadline,
        context: str,
    ) -> kubernetes.client.V1Node:
        name = self.name
        if not name:
            msg = "cannot patch Kubernetes node with missing metadata.name"
            raise OSError(msg)
        payload = await kube.run(
            lambda request_timeout: kube.core.patch_node(
                name=name,
                body=body,
                _request_timeout=request_timeout,
            ),
            deadline=deadline,
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
        deadline: Deadline,
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
        deadline : Deadline
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.

        """
        await self._patch(
            kube=kube,
            body={"metadata": {"labels": {label: value}}},
            deadline=deadline,
            context=f"failed to set label {label!r} on Kubernetes node {self.name!r}",
        )

    async def remove_label(
        self,
        kube: Kube,
        *,
        label: str,
        deadline: Deadline,
    ) -> None:
        """Remove one node label when present.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        label : str
            Label key to remove.
        deadline : Deadline
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.

        """
        labels = dict(self.labels)
        if label not in labels:
            return
        labels.pop(label, None)
        await self._patch(
            kube=kube,
            body={"metadata": {"labels": labels}},
            deadline=deadline,
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
        deadline: Deadline,
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
        deadline : Deadline
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.

        """
        await self._patch(
            kube=kube,
            body={"metadata": {"annotations": {key: value}}},
            deadline=deadline,
            context=(
                f"failed to set annotation {key!r} on Kubernetes node {self.name!r}"
            ),
        )

    async def remove_annotation(
        self,
        kube: Kube,
        *,
        key: str,
        deadline: Deadline,
    ) -> None:
        """Remove one node annotation when present.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        key : str
            Annotation key to remove.
        deadline : Deadline
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.

        """
        annotations = dict(self.annotations)
        if key not in annotations:
            return
        annotations.pop(key, None)
        await self._patch(
            kube=kube,
            body={"metadata": {"annotations": annotations}},
            deadline=deadline,
            context=(
                f"failed to remove annotation {key!r} from Kubernetes node "
                f"{self.name!r}"
            ),
        )

    async def cordon(self, kube: Kube, *, deadline: Deadline) -> None:
        """Mark this node unschedulable.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.

        """
        await self._patch(
            kube=kube,
            body={"spec": {"unschedulable": True}},
            deadline=deadline,
            context=f"failed to cordon Kubernetes node {self.name!r}",
        )

    async def uncordon(self, kube: Kube, *, deadline: Deadline) -> None:
        """Mark this node schedulable.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.

        """
        await self._patch(
            kube=kube,
            body={"spec": {"unschedulable": False}},
            deadline=deadline,
            context=f"failed to uncordon Kubernetes node {self.name!r}",
        )

    async def set_taint(
        self,
        kube: Kube,
        *,
        key: str,
        effect: TaintEffect,
        value: str | None,
        deadline: Deadline,
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
        deadline : Deadline
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
            deadline=deadline,
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
        deadline: Deadline,
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
        deadline : Deadline
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
            deadline=deadline,
            context=(
                f"failed to remove taint {key!r}/{effect or '*'} on Kubernetes "
                f"node {self.name!r}"
            ),
        )

    async def pods(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        labels: Mapping[str, str] | None = None,
        namespaces: Collection[str] | None = None,
    ) -> builtins.list[Pod]:
        """List pods scheduled onto this node across all namespaces.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
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
            deadline=deadline,
            namespaces=namespaces,
            labels=labels,
            field_selector=f"spec.nodeName={node_name}",
        )

    async def drain(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        force: bool = False,
    ) -> None:
        """Drain this node with safety-first defaults and one escalation flag.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.
        force : bool, optional
            Escalation toggle for disruptive evictions.  When False (default), drain
            skips system namespaces and blocks `emptyDir`/unmanaged pods.  When True,
            those pods are considered evictable.

        Raises
        ------
        OSError
            If the node cannot be drained safely under the selected policy.

        Notes
        -----
        This follows the safe posture of `kubectl drain`: cordon first, skip mirror
        pods, always skip DaemonSets, respect PDB backpressure (429 retries), and
        require explicit force for potentially disruptive cases.
        """
        await self.cordon(kube=kube, deadline=deadline)
        pods = await self.pods(kube=kube, deadline=deadline)
        candidates, blocked = _classify_node_drain_pods(pods, force=force)
        if blocked:
            raise OSError(_node_drain_blocked_message(self, blocked))

        pending = _node_drain_pending(candidates)
        await _evict_node_drain_candidates(self, kube, candidates, deadline=deadline)
        await _wait_node_drain_convergence(self, kube, pending, deadline=deadline)


def _classify_node_drain_pods(
    pods: Collection[Pod],
    *,
    force: bool,
) -> tuple[list[Pod], list[str]]:
    candidates: list[Pod] = []
    blocked: list[str] = []
    for pod in pods:
        namespace = pod.namespace
        name = pod.name
        if not namespace or not name or not pod.is_active or pod.is_mirror:
            continue
        if pod.is_daemonset_controlled or (
            namespace in NODE_SYSTEM_NAMESPACES and not force
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
    return candidates, blocked


def _node_drain_blocked_message(node: Node, blocked: list[str]) -> str:
    details = "\n".join(f"  - {line}" for line in sorted(blocked))
    return (
        f"refusing to drain Kubernetes node {node.name!r} due to non-evictable "
        f"pods:\n{details}"
    )


def _node_drain_pending(candidates: Collection[Pod]) -> set[tuple[str, str]]:
    pending: set[tuple[str, str]] = set()
    for pod in candidates:
        namespace = pod.namespace
        name = pod.name
        if namespace and name:
            pending.add((namespace, name))
    return pending


async def _evict_node_drain_candidates(
    node: Node,
    kube: Kube,
    candidates: Collection[Pod],
    *,
    deadline: Deadline,
) -> None:
    for pod in candidates:
        while True:
            try:
                await pod.evict(kube, deadline=deadline)
                break
            except OSError as err:
                if not isinstance(err, Kube.APIError) or err.status != 429:
                    raise
                remaining = deadline.remaining
                if remaining <= 0:
                    msg = (
                        f"timed out waiting for PDB eviction budget while draining "
                        f"node {node.name!r}"
                    )
                    raise TimeoutError(msg) from err
                await deadline.sleep(NODE_DRAIN_POLL_INTERVAL_SECONDS)


async def _wait_node_drain_convergence(
    node: Node,
    kube: Kube,
    pending: set[tuple[str, str]],
    *,
    deadline: Deadline,
) -> None:
    while pending:
        remaining = deadline.remaining
        if remaining <= 0:
            remaining_pods = ", ".join(
                f"{namespace}/{name}" for namespace, name in sorted(pending)
            )
            msg = (
                f"timed out waiting for pod eviction convergence on node "
                f"{node.name!r}; remaining: {remaining_pods}"
            )
            raise TimeoutError(msg)
        live = {
            (pod.namespace, pod.name)
            for pod in await node.pods(kube=kube, deadline=deadline)
            if pod.namespace and pod.name
        }
        pending.intersection_update(live)
        if pending:
            await deadline.sleep(NODE_DRAIN_POLL_INTERVAL_SECONDS)
