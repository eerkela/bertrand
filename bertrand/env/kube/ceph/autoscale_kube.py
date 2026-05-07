"""Kubernetes resource contract for the Ceph capacity autoscaler."""

from __future__ import annotations

import asyncio
import uuid
from collections.abc import Collection, Mapping
from dataclasses import dataclass
from typing import Any, cast

from ...run import BERTRAND_ENV, BERTRAND_NAMESPACE
from ..api import (
    CLUSTER_REGISTRY_READY_LABEL,
    CLUSTER_REGISTRY_READY_VALUE,
    ContainerSpec,
    EnvVarSpec,
    Kube,
    VolumeMountSpec,
    VolumeSpec,
)
from ..crd import CustomResourceDefinition
from ..custom import CustomResourceClient, CustomResourceSpec
from ..daemonset import DaemonSet
from ..deployment import Deployment
from ..node import Node
from ..rbac import ClusterRole, ClusterRoleBinding, PolicyRuleSpec
from ..service_account import ServiceAccount

AUTOSCALE_GROUP = "ceph.bertrand.dev"
AUTOSCALE_VERSION = "v1alpha1"
AUTOSCALE_AUTOSCALER_KIND = "CephStorageAutoscaler"
AUTOSCALE_AUTOSCALER_PLURAL = "cephstorageautoscalers"
AUTOSCALE_ACTION_KIND = "CephStorageAction"
AUTOSCALE_ACTION_PLURAL = "cephstorageactions"
AUTOSCALE_NODE_KIND = "CephStorageNode"
AUTOSCALE_NODE_PLURAL = "cephstoragenodes"
AUTOSCALE_DEFAULT_NAME = "default"
AUTOSCALE_SERVICE_ACCOUNT = "bertrand-ceph-autoscaler"
AUTOSCALE_CONTROLLER_NAME = "bertrand-ceph-autoscaler"
AUTOSCALE_AGENT_NAME = "bertrand-ceph-autoscaler-agent"
AUTOSCALE_LABEL = "bertrand.dev/ceph-autoscaler"
AUTOSCALE_LABEL_VALUE = "v1"
AUTOSCALE_LOOP_SIZE_RE = r"^[1-9][0-9]*[MGT]$"
AUTOSCALE_LOOP_SPEC_RE = r"^loop,[1-9][0-9]*[MGT],[1-9][0-9]*$"
AUTOSCALE_PHASES = ("Pending", "Running", "Succeeded", "Failed")
HOST_ROOT_VOLUME = "host-root"
HOST_ROOT_MOUNT = "/host"
DEFAULT_AUTOSCALER_SPEC = {
    "enabled": True,
    "high_watermark": 0.75,
    "target_watermark": 0.65,
    "loop_size": "4G",
    "max_actions_per_reconcile": 3,
    "reconcile_interval_seconds": 30,
}

AUTOSCALE_LABELS = {BERTRAND_ENV: "1", AUTOSCALE_LABEL: AUTOSCALE_LABEL_VALUE}
AUTOSCALER = CustomResourceSpec(
    group=AUTOSCALE_GROUP,
    version=AUTOSCALE_VERSION,
    kind=AUTOSCALE_AUTOSCALER_KIND,
    plural=AUTOSCALE_AUTOSCALER_PLURAL,
    labels=AUTOSCALE_LABELS,
)
ACTION = CustomResourceSpec(
    group=AUTOSCALE_GROUP,
    version=AUTOSCALE_VERSION,
    kind=AUTOSCALE_ACTION_KIND,
    plural=AUTOSCALE_ACTION_PLURAL,
    labels=AUTOSCALE_LABELS,
)
NODE_REPORT = CustomResourceSpec(
    group=AUTOSCALE_GROUP,
    version=AUTOSCALE_VERSION,
    kind=AUTOSCALE_NODE_KIND,
    plural=AUTOSCALE_NODE_PLURAL,
    labels=AUTOSCALE_LABELS,
)


def _remaining(deadline: float) -> float:
    remaining = deadline - asyncio.get_running_loop().time()
    if remaining <= 0:
        raise TimeoutError("timed out while converging Ceph autoscaler resources")
    return remaining


def _workload_labels(name: str) -> dict[str, str]:
    return {
        "app.kubernetes.io/name": name,
        "app.kubernetes.io/part-of": "bertrand",
        **AUTOSCALE_LABELS,
    }


def _selector(name: str) -> dict[str, str]:
    return {"app.kubernetes.io/name": name}


def _controller_container(image: str, role: str) -> ContainerSpec:
    return ContainerSpec(
        name=role,
        image=image,
        image_pull_policy="Always",
        args=[role],
        env=[EnvVarSpec.field_ref("NODE_NAME", field_path="spec.nodeName")],
        security_context={"privileged": True, "runAsUser": 0},
        volume_mounts=[VolumeMountSpec(name=HOST_ROOT_VOLUME, mount_path=HOST_ROOT_MOUNT)],
    )


def _pod_volumes() -> list[VolumeSpec]:
    return [VolumeSpec.host_path(HOST_ROOT_VOLUME, path="/", host_path_type="Directory")]


@dataclass(frozen=True)
class CephAutoscalerResources:
    """Kubernetes resource client for the Ceph capacity autoscaler.

    Parameters
    ----------
    namespace : str, optional
        Namespace that owns autoscaler CRs, RBAC subjects, and workloads.
    """

    namespace: str = BERTRAND_NAMESPACE

    @property
    def policy(self) -> CustomResourceClient:
        """
        Returns
        -------
        CustomResourceClient
            Bound custom-resource client for autoscaler policy objects.
        """
        return AUTOSCALER.client()

    @property
    def actions(self) -> CustomResourceClient:
        """
        Returns
        -------
        CustomResourceClient
            Bound custom-resource client for autoscaler action objects.
        """
        return ACTION.client()

    @property
    def node_reports(self) -> CustomResourceClient:
        """
        Returns
        -------
        CustomResourceClient
            Bound custom-resource client for autoscaler node report objects.
        """
        return NODE_REPORT.client()

    async def ensure(self, kube: Kube, *, image: str, timeout: float) -> None:
        """Converge autoscaler CRDs, RBAC, and workloads."""
        if timeout <= 0:
            raise TimeoutError("timeout must be non-negative")
        image = image.strip()
        if not image:
            raise ValueError("controlplane image reference cannot be empty")
        deadline = asyncio.get_running_loop().time() + timeout

        for kwargs in (
            {
                "plural": AUTOSCALE_AUTOSCALER_PLURAL,
                "singular": "cephstorageautoscaler",
                "kind": AUTOSCALE_AUTOSCALER_KIND,
                "short_names": ["csa"],
                "spec_schema": {
                    "type": "object",
                    "properties": {
                        "enabled": {"type": "boolean", "default": True},
                        "high_watermark": {
                            "type": "number",
                            "minimum": 0,
                            "maximum": 1,
                            "default": 0.75,
                        },
                        "target_watermark": {
                            "type": "number",
                            "minimum": 0,
                            "maximum": 1,
                            "default": 0.65,
                        },
                        "loop_size": {
                            "type": "string",
                            "pattern": AUTOSCALE_LOOP_SIZE_RE,
                            "default": "4G",
                        },
                        "max_actions_per_reconcile": {
                            "type": "integer",
                            "minimum": 1,
                            "default": 3,
                        },
                        "reconcile_interval_seconds": {
                            "type": "integer",
                            "minimum": 1,
                            "default": 30,
                        },
                    },
                },
                "status_schema": {
                    "type": "object",
                    "properties": {
                        "observedGeneration": {"type": "integer"},
                        "total_bytes": {"type": "integer"},
                        "used_bytes": {"type": "integer"},
                        "used_ratio": {"type": "number"},
                        "pending_actions": {"type": "integer"},
                        "running_actions": {"type": "integer"},
                        "succeeded_actions": {"type": "integer"},
                        "failed_actions": {"type": "integer"},
                        "last_reconciled_at": {"type": "string", "format": "date-time"},
                        "last_error": {"type": "string"},
                    },
                },
            },
            {
                "plural": AUTOSCALE_ACTION_PLURAL,
                "singular": "cephstorageaction",
                "kind": AUTOSCALE_ACTION_KIND,
                "short_names": ["csact"],
                "spec_schema": {
                    "type": "object",
                    "required": ["policy_generation", "node_name", "loop_spec", "reason"],
                    "properties": {
                        "policy_generation": {"type": "integer", "minimum": 0},
                        "node_name": {"type": "string", "minLength": 1},
                        "loop_spec": {"type": "string", "pattern": AUTOSCALE_LOOP_SPEC_RE},
                        "reason": {"type": "string", "minLength": 1},
                    },
                },
                "status_schema": {
                    "type": "object",
                    "properties": {
                        "phase": {"type": "string", "enum": list(AUTOSCALE_PHASES)},
                        "started_at": {"type": "string", "format": "date-time"},
                        "finished_at": {"type": "string", "format": "date-time"},
                        "message": {"type": "string"},
                        "worker_node": {"type": "string"},
                    },
                },
            },
            {
                "plural": AUTOSCALE_NODE_PLURAL,
                "singular": "cephstoragenode",
                "kind": AUTOSCALE_NODE_KIND,
                "short_names": ["csnode"],
                "spec_schema": {
                    "type": "object",
                    "required": ["node_name"],
                    "properties": {"node_name": {"type": "string", "minLength": 1}},
                },
                "status_schema": {
                    "type": "object",
                    "properties": {
                        "free_bytes": {"type": "integer", "minimum": 0},
                        "path": {"type": "string"},
                        "heartbeat_at": {"type": "string", "format": "date-time"},
                        "last_error": {"type": "string"},
                    },
                },
            },
        ):
            crd = await CustomResourceDefinition.upsert(
                kube,
                group=AUTOSCALE_GROUP,
                version=AUTOSCALE_VERSION,
                labels=AUTOSCALE_LABELS,
                timeout=_remaining(deadline),
                **cast("Any", kwargs),
            )
            await crd.wait_established(kube, timeout=_remaining(deadline))

        await ServiceAccount.upsert(
            kube,
            namespace=self.namespace,
            name=AUTOSCALE_SERVICE_ACCOUNT,
            labels=AUTOSCALE_LABELS,
            timeout=_remaining(deadline),
        )
        await ClusterRole.upsert(
            kube,
            name=AUTOSCALE_SERVICE_ACCOUNT,
            labels=AUTOSCALE_LABELS,
            rules=[
                PolicyRuleSpec(
                    api_groups=[AUTOSCALE_GROUP],
                    resources=[
                        AUTOSCALE_AUTOSCALER_PLURAL,
                        AUTOSCALE_ACTION_PLURAL,
                        AUTOSCALE_NODE_PLURAL,
                    ],
                    verbs=["get", "list", "watch", "create", "update", "patch"],
                ),
                PolicyRuleSpec(
                    api_groups=[AUTOSCALE_GROUP],
                    resources=[
                        f"{AUTOSCALE_AUTOSCALER_PLURAL}/status",
                        f"{AUTOSCALE_ACTION_PLURAL}/status",
                        f"{AUTOSCALE_NODE_PLURAL}/status",
                    ],
                    verbs=["get", "update", "patch"],
                ),
                PolicyRuleSpec(
                    api_groups=[""],
                    resources=["nodes"],
                    verbs=["get", "list", "watch"],
                ),
            ],
            timeout=_remaining(deadline),
        )
        await ClusterRoleBinding.upsert(
            kube,
            name=AUTOSCALE_SERVICE_ACCOUNT,
            role_name=AUTOSCALE_SERVICE_ACCOUNT,
            service_account_name=AUTOSCALE_SERVICE_ACCOUNT,
            service_account_namespace=self.namespace,
            labels=AUTOSCALE_LABELS,
            timeout=_remaining(deadline),
        )
        await self.policy.upsert(
            kube,
            namespace=self.namespace,
            name=AUTOSCALE_DEFAULT_NAME,
            spec=DEFAULT_AUTOSCALER_SPEC,
            timeout=_remaining(deadline),
        )

        controller = await Deployment.upsert(
            kube,
            namespace=self.namespace,
            name=AUTOSCALE_CONTROLLER_NAME,
            labels=_workload_labels(AUTOSCALE_CONTROLLER_NAME),
            selector=_selector(AUTOSCALE_CONTROLLER_NAME),
            containers=[_controller_container(image, "controller")],
            volumes=_pod_volumes(),
            service_account_name=AUTOSCALE_SERVICE_ACCOUNT,
            automount_service_account_token=True,
            node_selector={CLUSTER_REGISTRY_READY_LABEL: CLUSTER_REGISTRY_READY_VALUE},
            host_pid=True,
            timeout=_remaining(deadline),
        )
        await controller.wait_available(kube, timeout=_remaining(deadline))

        agent = await DaemonSet.upsert(
            kube,
            namespace=self.namespace,
            name=AUTOSCALE_AGENT_NAME,
            labels=_workload_labels(AUTOSCALE_AGENT_NAME),
            selector=_selector(AUTOSCALE_AGENT_NAME),
            containers=[_controller_container(image, "agent")],
            volumes=_pod_volumes(),
            service_account_name=AUTOSCALE_SERVICE_ACCOUNT,
            automount_service_account_token=True,
            node_selector={CLUSTER_REGISTRY_READY_LABEL: CLUSTER_REGISTRY_READY_VALUE},
            host_pid=True,
            timeout=_remaining(deadline),
        )
        await agent.wait_available(kube, timeout=_remaining(deadline))

    async def get_policy(self, kube: Kube, *, timeout: float) -> Mapping[str, Any]:
        """Read the singleton autoscaler policy object."""
        obj = await self.policy.get(
            kube,
            namespace=self.namespace,
            name=AUTOSCALE_DEFAULT_NAME,
            timeout=timeout,
        )
        if obj is None:
            raise OSError(f"{AUTOSCALE_AUTOSCALER_KIND} {AUTOSCALE_DEFAULT_NAME!r} is missing")
        return obj.obj

    async def list_actions(self, kube: Kube, *, timeout: float) -> list[Mapping[str, Any]]:
        """List autoscaler action custom resources."""
        return [
            obj.obj
            for obj in await self.actions.list(
                kube,
                namespace=self.namespace,
                labels={AUTOSCALE_LABEL: AUTOSCALE_LABEL_VALUE},
                timeout=timeout,
            )
        ]

    async def list_node_reports(self, kube: Kube, *, timeout: float) -> list[Mapping[str, Any]]:
        """List autoscaler node-report custom resources."""
        return [
            obj.obj
            for obj in await self.node_reports.list(
                kube,
                namespace=self.namespace,
                labels={AUTOSCALE_LABEL: AUTOSCALE_LABEL_VALUE},
                timeout=timeout,
            )
        ]

    async def list_ready_nodes(self, kube: Kube, *, timeout: float) -> list[str]:
        """List Kubernetes nodes that can pull Bertrand registry images."""
        nodes = await Node.list(
            kube,
            labels={CLUSTER_REGISTRY_READY_LABEL: CLUSTER_REGISTRY_READY_VALUE},
            timeout=timeout,
        )
        return sorted(node.name for node in nodes if node.name and node.is_ready)

    async def create_actions(
        self,
        kube: Kube,
        *,
        policy_generation: int,
        actions: Collection[Mapping[str, object]],
        timeout: float,
    ) -> None:
        """Create node-scoped growth action custom resources."""
        for action in actions:
            await self.actions.create(
                kube,
                namespace=self.namespace,
                name=f"{AUTOSCALE_DEFAULT_NAME}-{uuid.uuid4().hex[:12]}",
                spec={
                    "policy_generation": policy_generation,
                    "node_name": action["node_name"],
                    "loop_spec": action["loop_spec"],
                    "reason": action["reason"],
                },
                timeout=timeout,
            )

    async def patch_policy_status(
        self,
        kube: Kube,
        *,
        status: Mapping[str, object],
        timeout: float,
    ) -> None:
        """Patch singleton autoscaler policy status."""
        await self.policy.patch_status(
            kube,
            namespace=self.namespace,
            name=AUTOSCALE_DEFAULT_NAME,
            status=status,
            timeout=timeout,
        )

    async def upsert_node_report(
        self,
        kube: Kube,
        *,
        node_name: str,
        status: Mapping[str, object],
        timeout: float,
    ) -> None:
        """Upsert one node report and patch its observed status."""
        await self.node_reports.upsert(
            kube,
            namespace=self.namespace,
            name=node_name,
            spec={"node_name": node_name},
            timeout=timeout,
        )
        await self.node_reports.patch_status(
            kube,
            namespace=self.namespace,
            name=node_name,
            status=status,
            timeout=timeout,
        )

    async def patch_action_status(
        self,
        kube: Kube,
        *,
        name: str,
        status: Mapping[str, object],
        timeout: float,
    ) -> None:
        """Patch one autoscaler action status."""
        await self.actions.patch_status(
            kube,
            namespace=self.namespace,
            name=name,
            status=status,
            timeout=timeout,
        )


RESOURCES = CephAutoscalerResources()
