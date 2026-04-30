from __future__ import annotations

import asyncio
from collections.abc import Mapping
from dataclasses import dataclass
from types import MappingProxyType
from typing import Self

import kubernetes

from ..config.core import KubeName
from .api import Kube, Pod, _label_selector, _normalize_timeout

NODE_CONTROLLER_KINDS = frozenset({
    "ReplicationController",
    "ReplicaSet",
    "StatefulSet",
    "DaemonSet",
    "Job",
})
NODE_SYSTEM_NAMESPACES = frozenset({
    "kube-system",
    "kube-public",
    "kube-node-lease",
})
NODE_MIRROR_POD_ANNOTATION = "kubernetes.io/config.mirror"


@dataclass(frozen=True)
class Node:
    """General-purpose wrapper around one Kubernetes Node object.

    This wrapper exposes a small, typed surface for common node introspection and
    mutation operations so downstream modules can avoid reimplementing Kubernetes
    node-shape parsing.
    """
    obj: kubernetes.client.V1Node

    @classmethod
    async def query(
        cls,
        *,
        kube: Kube,
        timeout: float,
        name: KubeName | None = None,
        labels: Mapping[str, str] | None = None,
    ) -> list[Self]:
        """Load Kubernetes Nodes and validate their structure.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            The maximum time to wait for Kubernetes node queries in seconds.  If
            infinite, wait indefinitely.
        name : str | None, optional
            Optional node name.  When given, this performs an exact name lookup and
            returns either a one-item list or an empty list.
        labels : Mapping[str, str] | None, optional
            Optional label filters.  Only supported for list lookups.

        Returns
        -------
        list[Node]
            Validated Kubernetes node wrappers.  Name lookups return either one item
            or an empty list.

        Raises
        ------
        ValueError
            If both `name` and `labels` are provided.
        TimeoutError
            If the Kubernetes request exceeds the timeout budget.
        OSError
            If the Kubernetes API call fails or returns malformed data.
        """
        if name is not None and labels is not None:
            raise ValueError("node query cannot combine both name and labels filters")

        # NOTE: direct-name lookups map naturally to read-node semantics and preserve
        # 404 -> [] behavior through the shared `Kube.run()` boundary.
        if name is not None:
            payload = await kube.run(
                lambda: kube.core.read_node(
                    name=name,
                    _request_timeout=_normalize_timeout(timeout),
                ),
                timeout=timeout,
                context=f"failed to read Kubernetes node {name!r}",
            )
            if payload is None:
                return []
            if not isinstance(payload, kubernetes.client.V1Node):
                raise OSError(f"malformed Kubernetes node payload for {name!r}")
            return [cls(obj=payload)]

        # NOTE: selector-based discovery remains a plain list operation so callers can
        # compose higher-level filtering deterministically in Python.
        payload = await kube.run(
            lambda: kube.core.list_node(
                label_selector=_label_selector(labels),
                _request_timeout=_normalize_timeout(timeout),
            ),
            timeout=timeout,
            context="failed to list Kubernetes nodes",
        )
        if payload is None:
            return []
        if not isinstance(payload, kubernetes.client.V1NodeList):
            raise OSError("malformed Kubernetes node list payload")
        out: list[Self] = []
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
        out: list[str] = []
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
        out: list[str] = []
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
        # NOTE: the public accessor is read-only, so mutators take an explicit local
        # copy before patching Kubernetes state.
        labels = dict(self.labels)
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
        # NOTE: node-name validation is centralized here so all mutating helpers fail
        # consistently before sending partial patch requests.
        name = self.name
        if not name:
            raise OSError("cannot patch Kubernetes node with missing metadata.name")
        payload = await kube.run(
            lambda: kube.core.patch_node(
                name=name,
                body=body,
                _request_timeout=_normalize_timeout(timeout),
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
        *,
        kube: Kube,
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
        *,
        kube: Kube,
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
        # NOTE: the public accessor is read-only, so mutators take an explicit local
        # copy before patching Kubernetes state.
        labels = dict(self.labels)
        # NOTE: idempotent removal avoids unnecessary API writes when the key is
        # already absent.
        if label not in labels:
            return
        labels.pop(label, None)
        await self._patch(
            kube=kube,
            body={"metadata": {"labels": labels}},
            timeout=timeout,
            context=(
                f"failed to remove label {label!r} from Kubernetes node "
                f"{self.name!r}"
            ),
        )

    async def set_annotation(
        self,
        *,
        kube: Kube,
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
            context=(
                f"failed to set annotation {key!r} on Kubernetes node "
                f"{self.name!r}"
            ),
        )

    async def remove_annotation(
        self,
        *,
        kube: Kube,
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
        # NOTE: the public accessor is read-only, so mutators take an explicit local
        # copy before patching Kubernetes state.
        annotations = dict(self.annotations)
        # NOTE: idempotent removal avoids unnecessary API writes when the key is
        # already absent.
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

    async def cordon(self, *, kube: Kube, timeout: float) -> None:
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

    async def uncordon(self, *, kube: Kube, timeout: float) -> None:
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
        *,
        kube: Kube,
        key: str,
        effect: str,
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
        effect : str
            Taint effect.
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
        # NOTE: we normalize to one taint per (key, effect) pair so repeated calls
        # converge instead of duplicating entries.
        taints = list(self.taints)
        normalized: list[kubernetes.client.V1Taint] = []
        replaced = False
        for taint in taints:
            if (taint.key or "") == key and (taint.effect or "") == effect:
                if replaced:
                    continue
                replaced = True
                normalized.append(
                    kubernetes.client.V1Taint(
                        key=key,
                        effect=effect,
                        value=value,
                    )
                )
                continue
            normalized.append(taint)
        if not replaced:
            normalized.append(
                kubernetes.client.V1Taint(
                    key=key,
                    effect=effect,
                    value=value,
                )
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
                f"failed to set taint {key!r}/{effect!r} on Kubernetes node "
                f"{self!r}"
            ),
        )

    async def remove_taint(
        self,
        *,
        kube: Kube,
        key: str,
        effect: str | None,
        timeout: float,
    ) -> None:
        """Remove taints matching `key` and optional `effect`.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        key : str
            Taint key to remove.
        effect : str | None
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
        # NOTE: remove logic is list-comprehension based so the resulting taint set
        # is fully canonicalized in one patch.
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
        *,
        kube: Kube,
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> list[Pod]:
        """List pods currently scheduled on this node across all namespaces.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Optional pod label selector filters.

        Returns
        -------
        list[Pod]
            Pod wrappers for all matching pods assigned to this node.

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds the timeout budget.
        OSError
            If this node has no name, or if the API payload is malformed.
        """
        name = self.name
        if not name:
            raise OSError("cannot query pods for Kubernetes node with missing name")
        # NOTE: field-selector scoping keeps this query node-local at the API layer,
        # which avoids client-side filtering over cluster-wide pod lists.
        payload = await kube.run(
            lambda: kube.core.list_pod_for_all_namespaces(
                field_selector=f"spec.nodeName={name}",
                label_selector=_label_selector(labels),
                _request_timeout=_normalize_timeout(timeout),
            ),
            timeout=timeout,
            context=f"failed to list pods on Kubernetes node {name!r}",
        )
        if payload is None:
            return []
        if not isinstance(payload, kubernetes.client.V1PodList):
            raise OSError(f"malformed pod list payload for Kubernetes node {name!r}")
        out: list[Pod] = []
        for pod in payload.items or []:
            if not isinstance(pod, kubernetes.client.V1Pod):
                raise OSError(
                    f"malformed pod entry while listing Kubernetes node {name!r}"
                )
            out.append(Pod(obj=pod))
        return out

    async def evict_pod(
        self,
        pod: Pod,
        *,
        kube: Kube,
        timeout: float,
        grace_period_seconds: int | None = None,
    ) -> None:
        """Evict one pod scheduled on this node via the eviction subresource.

        Parameters
        ----------
        pod : Pod
            Pod to evict.
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.
        grace_period_seconds : int | None, optional
            Optional pod termination grace period override.

        Raises
        ------
        ValueError
            If `grace_period_seconds` is negative.
        TimeoutError
            If the Kubernetes request exceeds the timeout budget.
        OSError
            If pod identity is incomplete or eviction fails.
        """
        metadata = pod.obj.metadata
        name = (metadata.name or "") if metadata is not None else ""
        namespace = (metadata.namespace or "") if metadata is not None else ""
        if not name or not namespace:
            raise OSError("cannot evict pod with missing metadata.name/namespace")
        if grace_period_seconds is not None and grace_period_seconds < 0:
            raise ValueError("grace period must be non-negative when provided")

        # NOTE: policy/v1 eviction is preferred over delete because it respects
        # PodDisruptionBudgets and communicates scheduling intent explicitly.
        body = kubernetes.client.V1Eviction(
            api_version="policy/v1",
            kind="Eviction",
            metadata=kubernetes.client.V1ObjectMeta(name=name, namespace=namespace),
            delete_options=(
                kubernetes.client.V1DeleteOptions(
                    grace_period_seconds=grace_period_seconds
                ) if grace_period_seconds is not None else None
            ),
        )
        await kube.run(
            lambda: kube.core.create_namespaced_pod_eviction(
                name=name,
                namespace=namespace,
                body=body,
                _request_timeout=_normalize_timeout(timeout),
            ),
            timeout=timeout,
            context=f"failed to evict pod {namespace}/{name} from node {self.name!r}",
        )

    @staticmethod
    def _pod_identity(pod: Pod) -> tuple[str, str] | None:
        metadata = pod.obj.metadata
        name = (metadata.name or "") if metadata is not None else ""
        namespace = (metadata.namespace or "") if metadata is not None else ""
        if not name or not namespace:
            return None
        return namespace, name

    @staticmethod
    def _pod_is_mirror(pod: Pod) -> bool:
        metadata = pod.obj.metadata
        annotations = (metadata.annotations or {}) if metadata is not None else {}
        return NODE_MIRROR_POD_ANNOTATION in annotations

    @staticmethod
    def _pod_is_daemonset(pod: Pod) -> bool:
        metadata = pod.obj.metadata
        owners = (metadata.owner_references or []) if metadata is not None else []
        for owner in owners:
            if (owner.kind or "").strip() != "DaemonSet":
                continue
            if owner.controller is None or owner.controller:
                return True
        return False

    @staticmethod
    def _pod_has_supported_controller(pod: Pod) -> bool:
        metadata = pod.obj.metadata
        owners = (metadata.owner_references or []) if metadata is not None else []
        for owner in owners:
            kind = (owner.kind or "").strip()
            if kind not in NODE_CONTROLLER_KINDS:
                continue
            if owner.controller is None or owner.controller:
                return True
        return False

    @staticmethod
    def _pod_uses_emptydir(pod: Pod) -> bool:
        spec = pod.obj.spec
        for volume in (spec.volumes or []) if spec is not None else []:
            if volume.empty_dir is not None:
                return True
        return False

    async def drain(
        self,
        *,
        kube: Kube,
        timeout: float,
        ignore_daemonsets: bool = True,
        include_system_namespaces: bool = False,
        delete_emptydir_data: bool = False,
        force: bool = False,
        grace_period_seconds: int | None = None,
        poll_interval: float = 0.5,
    ) -> None:
        """Drain this node with kubectl-safe default policy.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.
        ignore_daemonsets : bool, optional
            Whether to skip DaemonSet-managed pods.
        include_system_namespaces : bool, optional
            Whether to include pods in Kubernetes system namespaces.
        delete_emptydir_data : bool, optional
            Whether to allow eviction of pods that use `emptyDir` volumes.
        force : bool, optional
            Whether to allow eviction of unmanaged pods without supported
            controller ownership.
        grace_period_seconds : int | None, optional
            Optional pod termination grace period override.
        poll_interval : float, optional
            Poll interval for PDB backpressure retries and convergence checks.

        Raises
        ------
        ValueError
            If `poll_interval` is not positive.
        TimeoutError
            If eviction admission or post-eviction convergence exceed timeout.
        OSError
            If the node cannot be drained safely under the selected policy.

        Notes
        -----
        This follows the safe posture of `kubectl drain`: cordon first, skip mirror
        pods, skip DaemonSets by default, respect PDB backpressure (429 retries),
        and require explicit opt-ins for potentially disruptive cases.
        """
        if timeout <= 0:
            raise TimeoutError("node drain timeout must be non-negative")
        if poll_interval <= 0:
            raise ValueError("node drain poll interval must be positive")
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout

        # NOTE: we cordon first to stop new placements while eviction converges.
        await self.cordon(kube=kube, timeout=deadline - loop.time())
        pods = await self.pods(kube=kube, timeout=deadline - loop.time())

        # NOTE: classification is explicit so refusal reasons are user-actionable
        # before any disruptive eviction requests are submitted.
        candidates: list[Pod] = []
        blocked: list[str] = []
        for pod in pods:
            identity = type(self)._pod_identity(pod)
            if identity is None:
                continue
            namespace, name = identity

            status = pod.obj.status
            phase = (status.phase or "").strip() if status is not None else ""
            if phase in {"Succeeded", "Failed"}:
                continue
            if type(self)._pod_is_mirror(pod):
                continue
            if type(self)._pod_is_daemonset(pod):
                if ignore_daemonsets:
                    continue
                blocked.append(
                    f"{namespace}/{name}: DaemonSet-managed pod "
                    "(set ignore_daemonsets=True to skip)"
                )
                continue
            if namespace in NODE_SYSTEM_NAMESPACES and not include_system_namespaces:
                continue
            if type(self)._pod_uses_emptydir(pod) and not delete_emptydir_data:
                blocked.append(
                    f"{namespace}/{name}: uses emptyDir volume "
                    "(set delete_emptydir_data=True to allow)"
                )
                continue
            if not type(self)._pod_has_supported_controller(pod) and not force:
                blocked.append(
                    f"{namespace}/{name}: unmanaged pod "
                    "(set force=True to allow)"
                )
                continue
            candidates.append(pod)

        if blocked:
            details = "\n".join(f"  - {line}" for line in sorted(blocked))
            raise OSError(
                f"refusing to drain Kubernetes node {self.name!r} due to "
                f"non-evictable pods:\n{details}"
            )

        # NOTE: we submit evictions first, then track completion separately so PDB
        # retries and convergence polling stay decoupled and predictable.
        pending = {
            identity
            for pod in candidates
            if (identity := type(self)._pod_identity(pod)) is not None
        }
        for pod in candidates:
            while True:
                try:
                    await self.evict_pod(
                        pod,
                        kube=kube,
                        timeout=deadline - loop.time(),
                        grace_period_seconds=grace_period_seconds,
                    )
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
                    await asyncio.sleep(min(poll_interval, remaining))

        # NOTE: eviction admission does not guarantee immediate termination, so we
        # poll node-local pod state until every targeted pod disappears.
        while pending:
            remaining = deadline - loop.time()
            if remaining <= 0:
                still_pending = ", ".join(
                    f"{namespace}/{name}" for namespace, name in sorted(pending)
                )
                raise TimeoutError(
                    f"timed out waiting for pod eviction convergence on node "
                    f"{self.name!r}; remaining: {still_pending}"
                )
            live = await self.pods(kube=kube, timeout=remaining)
            live_ids = {
                identity
                for pod in live
                if (identity := type(self)._pod_identity(pod)) is not None
            }
            pending.intersection_update(live_ids)
            if pending:
                await asyncio.sleep(min(poll_interval, deadline - loop.time()))
