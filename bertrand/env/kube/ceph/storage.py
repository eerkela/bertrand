"""Ceph storage controller control plane composition."""

from __future__ import annotations

import asyncio
import os
import platform
import sys
from contextlib import suppress
from datetime import UTC, datetime
from typing import TYPE_CHECKING

from bertrand.env.git import BERTRAND_NAMESPACE, INFINITY
from bertrand.env.kube.api.client import (
    CLUSTER_REGISTRY_READY_LABEL,
    CLUSTER_REGISTRY_READY_VALUE,
    Kube,
)
from bertrand.env.kube.api.spec import (
    ContainerSpec,
    EnvVarSpec,
    PodTemplateSpec,
    PolicyRuleSpec,
    SecurityContextSpec,
    VolumeMountSpec,
    VolumeSpec,
)
from bertrand.env.kube.build.request import BUILDKIT_BUILD_GROUP, BUILDKIT_BUILD_PLURAL
from bertrand.env.kube.ceph.api import (
    BlockOSDSpec,
    CephCapacitySnapshot,
    add_block_osd,
    add_loop_osd,
    ceph_df,
    ceph_health,
    ceph_osds,
    host_free_bytes,
    parse_size_bytes,
    remove_osd,
    validate_block_osd_devices,
)
from bertrand.env.kube.ceph.capacity import (
    CEPH_CAPACITY_GROUP,
    STORAGE_ACTION_PLURAL,
    STORAGE_CONTROLLER_LABEL,
    STORAGE_CONTROLLER_LABEL_VALUE,
    STORAGE_CONTROLLER_LABELS,
    STORAGE_NODE_PLURAL,
    STORAGE_POLICY_PLURAL,
    CephStorageActionRecord,
    CephStorageNodeRecord,
    CephStoragePlanner,
    CephStoragePolicyRecord,
    StoragePlan,
    create_storage_actions,
    ensure_ceph_capacity_crds,
    ensure_default_storage_policy,
    list_storage_actions,
    list_storage_node_reports,
    patch_storage_action_status,
    patch_storage_policy_status,
    pending_storage_actions,
    read_storage_policy,
    storage_action_from_payload,
    storage_watch_targets,
    upsert_storage_node_report,
)
from bertrand.env.kube.ceph.snapshot import (
    ensure_repository_snapshot_support,
    maintain_repository_snapshots,
    next_repository_snapshot_time,
)
from bertrand.env.kube.ceph.volume import (
    REPOSITORY_MOUNT_PLURAL,
    REPOSITORY_VOLUME_PLURAL,
    ensure_repository_mount_crd,
    ensure_repository_volume_crd,
    gc_repository_volumes,
    next_repository_volume_gc_time,
)
from bertrand.env.kube.control import MaintenanceClock
from bertrand.env.kube.daemonset import DaemonSet
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.node import Node
from bertrand.env.kube.rbac import ClusterRole, ClusterRoleBinding
from bertrand.env.kube.service_account import ServiceAccount

if TYPE_CHECKING:
    from collections.abc import Collection, Mapping

    from bertrand.env.kube.custom_object import CustomObjectClient

STORAGE_CONTROLLER_SERVICE_ACCOUNT = "bertrand-ceph-storage-controller"
STORAGE_CONTROLLER_NAME = "bertrand-ceph-storage-controller"
STORAGE_AGENT_NAME = "bertrand-ceph-storage-agent"
STORAGE_WATCH_RESTART_DELAY_SECONDS = 1.0
STORAGE_CONTROLLER_DEFAULT_RECONCILE_SECONDS = 30.0
STORAGE_AGENT_SYNC_INTERVAL_SECONDS = 5.0
REPOSITORY_VOLUME_GC_EMPTY_CHECK_SECONDS = 3600.0
REPOSITORY_VOLUME_GC_READY_CHECK_SECONDS = 900.0
REPOSITORY_VOLUME_GC_FAILURE_RETRY_SECONDS = 300.0
REPOSITORY_VOLUME_GC_TIMEOUT_SECONDS = 60.0
REPOSITORY_SNAPSHOT_EMPTY_CHECK_SECONDS = 3600.0
REPOSITORY_SNAPSHOT_READY_CHECK_SECONDS = 900.0
REPOSITORY_SNAPSHOT_FAILURE_RETRY_SECONDS = 300.0
REPOSITORY_SNAPSHOT_TIMEOUT_SECONDS = 120.0

HOST_ROOT_VOLUME = "host-root"
HOST_ROOT_MOUNT = "/host"


async def _ensure_rbac(kube: Kube, *, deadline: float) -> None:
    loop = asyncio.get_running_loop()
    await ServiceAccount.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
        labels=STORAGE_CONTROLLER_LABELS,
        timeout=deadline - loop.time(),
    )
    await ClusterRole.upsert(
        kube,
        name=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
        labels=STORAGE_CONTROLLER_LABELS,
        rules=[
            PolicyRuleSpec(
                api_groups=[CEPH_CAPACITY_GROUP],
                resources=[
                    STORAGE_POLICY_PLURAL,
                    STORAGE_ACTION_PLURAL,
                    STORAGE_NODE_PLURAL,
                    REPOSITORY_MOUNT_PLURAL,
                    REPOSITORY_VOLUME_PLURAL,
                ],
                verbs=["get", "list", "watch", "create", "update", "patch"],
            ),
            PolicyRuleSpec(
                api_groups=[CEPH_CAPACITY_GROUP],
                resources=[
                    f"{STORAGE_POLICY_PLURAL}/status",
                    f"{STORAGE_ACTION_PLURAL}/status",
                    f"{STORAGE_NODE_PLURAL}/status",
                ],
                verbs=["get", "update", "patch"],
            ),
            PolicyRuleSpec(
                api_groups=[CEPH_CAPACITY_GROUP],
                resources=[REPOSITORY_MOUNT_PLURAL, REPOSITORY_VOLUME_PLURAL],
                verbs=["delete"],
            ),
            PolicyRuleSpec(
                api_groups=[BUILDKIT_BUILD_GROUP],
                resources=[BUILDKIT_BUILD_PLURAL],
                verbs=["get", "list", "watch"],
            ),
            PolicyRuleSpec(
                api_groups=[""],
                resources=["nodes"],
                verbs=["get", "list", "watch"],
            ),
            PolicyRuleSpec(
                api_groups=[""],
                resources=["pods"],
                verbs=["get", "list", "watch"],
            ),
            PolicyRuleSpec(
                api_groups=[""],
                resources=["persistentvolumeclaims"],
                verbs=["get", "list", "watch", "delete"],
            ),
            PolicyRuleSpec(
                api_groups=["snapshot.storage.k8s.io"],
                resources=["volumesnapshots"],
                verbs=["get", "list", "watch", "create", "delete"],
            ),
            PolicyRuleSpec(
                api_groups=["snapshot.storage.k8s.io"],
                resources=["volumesnapshotclasses"],
                verbs=["get", "list", "watch", "create"],
            ),
        ],
        timeout=deadline - loop.time(),
    )
    await ClusterRoleBinding.upsert(
        kube,
        name=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
        role_name=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
        service_account_name=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
        service_account_namespace=BERTRAND_NAMESPACE,
        labels=STORAGE_CONTROLLER_LABELS,
        timeout=deadline - loop.time(),
    )


async def _ensure_workloads(kube: Kube, *, image: str, deadline: float) -> None:
    loop = asyncio.get_running_loop()

    def container(role: str) -> ContainerSpec:
        return ContainerSpec(
            name=role,
            image=image,
            image_pull_policy="Always",
            args=[role],
            env=[EnvVarSpec.field_ref("NODE_NAME", field_path="spec.nodeName")],
            security_context=SecurityContextSpec(privileged=True, run_as_user=0),
            volume_mounts=[
                VolumeMountSpec(name=HOST_ROOT_VOLUME, mount_path=HOST_ROOT_MOUNT)
            ],
        )

    volumes = [
        VolumeSpec.host_path(HOST_ROOT_VOLUME, path="/", host_path_type="Directory")
    ]

    controller = await Deployment.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=STORAGE_CONTROLLER_NAME,
        labels={
            "app.kubernetes.io/name": STORAGE_CONTROLLER_NAME,
            "app.kubernetes.io/part-of": "bertrand",
            **STORAGE_CONTROLLER_LABELS,
        },
        selector={"app.kubernetes.io/name": STORAGE_CONTROLLER_NAME},
        pod_template=PodTemplateSpec(
            containers=[container("controller")],
            volumes=volumes,
            service_account_name=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
            automount_service_account_token=True,
            node_selector={CLUSTER_REGISTRY_READY_LABEL: CLUSTER_REGISTRY_READY_VALUE},
            host_pid=True,
        ),
        timeout=deadline - loop.time(),
    )
    await controller.wait_rollout(kube, timeout=deadline - loop.time())

    agent = await DaemonSet.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=STORAGE_AGENT_NAME,
        labels={
            "app.kubernetes.io/name": STORAGE_AGENT_NAME,
            "app.kubernetes.io/part-of": "bertrand",
            **STORAGE_CONTROLLER_LABELS,
        },
        selector={"app.kubernetes.io/name": STORAGE_AGENT_NAME},
        pod_template=PodTemplateSpec(
            containers=[container("agent")],
            volumes=volumes,
            service_account_name=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
            automount_service_account_token=True,
            node_selector={CLUSTER_REGISTRY_READY_LABEL: CLUSTER_REGISTRY_READY_VALUE},
            host_pid=True,
        ),
        timeout=deadline - loop.time(),
    )
    await agent.wait_rollout(kube, timeout=deadline - loop.time())


async def ensure_ceph_storage_controller(
    kube: Kube, *, image: str, timeout: float
) -> None:
    """Converge Ceph storage controller CRDs, RBAC, and workloads in the local cluster.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    image : str
        Fully qualified storage controller image reference.
    timeout : float
        Maximum convergence budget in seconds.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive or convergence exceeds the budget.
    ValueError
        If `image` is empty.
    """
    if timeout <= 0:
        msg = "timeout must be non-negative"
        raise TimeoutError(msg)
    image = image.strip()
    if not image:
        msg = "control plane image reference cannot be empty"
        raise ValueError(msg)
    deadline = asyncio.get_running_loop().time() + timeout
    await ensure_ceph_capacity_crds(
        kube,
        timeout=deadline - asyncio.get_running_loop().time(),
    )
    await ensure_repository_volume_crd(
        kube,
        timeout=deadline - asyncio.get_running_loop().time(),
    )
    await ensure_repository_mount_crd(
        kube,
        timeout=deadline - asyncio.get_running_loop().time(),
    )
    await ensure_repository_snapshot_support(
        kube,
        timeout=deadline - asyncio.get_running_loop().time(),
    )
    await _ensure_rbac(kube, deadline=deadline)
    await ensure_default_storage_policy(
        kube,
        timeout=deadline - asyncio.get_running_loop().time(),
    )
    await _ensure_workloads(kube, image=image, deadline=deadline)


class CephStorageController:
    """Controller role for cluster-wide Ceph capacity planning."""

    def __init__(self) -> None:
        self._planner = CephStoragePlanner()
        self._repository_volume_gc = MaintenanceClock()
        self._repository_snapshot = MaintenanceClock()

    async def _watch(
        self,
        kube: Kube,
        *,
        client: CustomObjectClient,
        wake: asyncio.Event,
        deadline: float,
        context: str,
    ) -> None:
        loop = asyncio.get_running_loop()
        while True:
            try:
                async for _event in client.watch(
                    kube,
                    namespace=BERTRAND_NAMESPACE,
                    labels={STORAGE_CONTROLLER_LABEL: STORAGE_CONTROLLER_LABEL_VALUE},
                    timeout=deadline - loop.time(),
                ):
                    wake.set()
                wake.set()
                await asyncio.sleep(
                    min(STORAGE_WATCH_RESTART_DELAY_SECONDS, deadline - loop.time())
                )
            except asyncio.CancelledError:
                raise
            except (OSError, RuntimeError, ValueError) as err:
                print(
                    "bertrand: warning: Ceph storage controller "
                    f"{context} watch failed: {err}",
                    file=sys.stderr,
                )
                wake.set()
                await asyncio.sleep(
                    min(STORAGE_WATCH_RESTART_DELAY_SECONDS, deadline - loop.time())
                )

    async def _ready_nodes(self, kube: Kube, *, timeout: float) -> list[str]:
        """List Kubernetes nodes that are ready for Bertrand registry pulls.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds.

        Returns
        -------
        list[str]
            Ready node names sorted in deterministic order.
        """
        nodes = await Node.list(
            kube,
            labels={CLUSTER_REGISTRY_READY_LABEL: CLUSTER_REGISTRY_READY_VALUE},
            timeout=timeout,
        )
        return sorted(node.name for node in nodes if node.name and node.is_ready)

    async def _plan_storage_actions(
        self,
        kube: Kube,
        *,
        policy: CephStoragePolicyRecord,
        actions: Collection[CephStorageActionRecord],
        reports: Collection[CephStorageNodeRecord],
        capacity: CephCapacitySnapshot,
        deadline: float,
    ) -> StoragePlan:
        loop = asyncio.get_running_loop()
        loop_bytes = parse_size_bytes(policy.spec.loop_size)
        ready_nodes = await self._ready_nodes(kube, timeout=deadline - loop.time())
        eligible_nodes = self._planner.eligible_nodes(
            ready_nodes=ready_nodes,
            reports=reports,
            loop_bytes=loop_bytes,
        )
        planned = self._planner.plan_grow_actions(
            policy=policy,
            capacity=capacity,
            actions=actions,
            eligible_nodes=eligible_nodes,
            loop_bytes=loop_bytes,
        )
        inventory = self._planner.osd_inventory(actions)
        managed_osd_count = len(inventory.loop_osd_ids)
        shrink_candidates = []
        last_shrink_at = self._planner.last_shrink_at(actions)
        if (
            not planned
            and policy.spec.enabled
            and policy.spec.shrink_enabled
            and capacity.used_ratio < policy.spec.low_watermark
        ):
            health = await ceph_health(timeout=deadline - loop.time())
            if health.clean:
                shrink_candidates = self._planner.managed_osds(
                    actions=actions,
                    osds=await ceph_osds(timeout=deadline - loop.time()),
                )
                planned = self._planner.plan_shrink_action(
                    policy=policy,
                    capacity=capacity,
                    actions=actions,
                    candidates=shrink_candidates,
                )
        return StoragePlan(
            actions=planned,
            managed_osd_count=managed_osd_count,
            loop_osd_count=len(inventory.loop_osd_ids),
            block_osd_count=len(inventory.block_osd_ids),
            elastic_bytes=inventory.elastic_bytes,
            durable_bytes=inventory.durable_bytes,
            shrink_candidate_count=len(shrink_candidates),
            last_shrink_at=last_shrink_at,
        )

    def _status_payload(
        self,
        *,
        capacity: CephCapacitySnapshot,
        actions: Collection[CephStorageActionRecord],
        plan: StoragePlan,
    ) -> dict[str, object]:
        counts = self._planner.action_counts(actions)
        return {
            "total_bytes": capacity.total_bytes,
            "used_bytes": capacity.used_bytes,
            "used_ratio": capacity.used_ratio,
            "pending_actions": counts.get("Pending", 0),
            "running_actions": counts.get("Running", 0),
            "succeeded_actions": counts.get("Succeeded", 0),
            "failed_actions": counts.get("Failed", 0),
            "managed_osds": plan.managed_osd_count,
            "loop_osds": plan.loop_osd_count,
            "block_osds": plan.block_osd_count,
            "elastic_bytes": plan.elastic_bytes,
            "durable_bytes": plan.durable_bytes,
            "block_preferred": plan.block_osd_count > 0,
            "shrink_candidates": plan.shrink_candidate_count,
            "last_shrink_at": (
                plan.last_shrink_at.isoformat()
                if plan.last_shrink_at is not None
                else None
            ),
            "last_reconciled_at": datetime.now(UTC).isoformat(),
            "last_error": "",
        }

    @staticmethod
    def _error_status_payload(error: str) -> dict[str, object]:
        return {
            "total_bytes": None,
            "used_bytes": None,
            "used_ratio": None,
            "pending_actions": 0,
            "running_actions": 0,
            "succeeded_actions": 0,
            "failed_actions": 0,
            "managed_osds": 0,
            "loop_osds": 0,
            "block_osds": 0,
            "elastic_bytes": 0,
            "durable_bytes": 0,
            "block_preferred": False,
            "shrink_candidates": 0,
            "last_shrink_at": None,
            "last_reconciled_at": datetime.now(UTC).isoformat(),
            "last_error": error,
        }

    async def reconcile(self, kube: Kube, *, deadline: float) -> float:
        """Run one controller reconciliation pass and return the next interval.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : float
            Absolute event-loop deadline for this controller run.

        Returns
        -------
        float
            Delay in seconds before the next reconciliation pass.
        """
        loop = asyncio.get_running_loop()
        policy = await read_storage_policy(kube, timeout=deadline - loop.time())
        actions = await list_storage_actions(kube, timeout=deadline - loop.time())
        reports = await list_storage_node_reports(
            kube,
            timeout=deadline - loop.time(),
        )
        capacity = await ceph_df(timeout=deadline - loop.time())
        plan = await self._plan_storage_actions(
            kube,
            policy=policy,
            actions=actions,
            reports=reports,
            capacity=capacity,
            deadline=deadline,
        )
        if plan.actions:
            await create_storage_actions(
                kube,
                policy_generation=policy.metadata.generation,
                actions=plan.actions,
                timeout=deadline - loop.time(),
            )
            actions = await list_storage_actions(kube, timeout=deadline - loop.time())
            inventory = self._planner.osd_inventory(actions)
            plan = StoragePlan(
                actions=plan.actions,
                managed_osd_count=len(inventory.loop_osd_ids),
                loop_osd_count=len(inventory.loop_osd_ids),
                block_osd_count=len(inventory.block_osd_ids),
                elastic_bytes=inventory.elastic_bytes,
                durable_bytes=inventory.durable_bytes,
                shrink_candidate_count=(
                    0
                    if any(action.operation == "shrink" for action in plan.actions)
                    else plan.shrink_candidate_count
                ),
                last_shrink_at=self._planner.last_shrink_at(actions),
            )
        await patch_storage_policy_status(
            kube,
            policy=policy,
            status=self._status_payload(
                capacity=capacity,
                actions=actions,
                plan=plan,
            ),
            timeout=deadline - loop.time(),
        )
        return float(policy.spec.reconcile_interval_seconds)

    async def _maybe_repository_volume_gc(
        self,
        kube: Kube,
        *,
        deadline: float,
    ) -> None:
        now = datetime.now(UTC)
        pass_deadline = self._repository_volume_gc.pass_deadline(
            now,
            loop_deadline=deadline,
            timeout=REPOSITORY_VOLUME_GC_TIMEOUT_SECONDS,
        )
        if pass_deadline is None:
            return
        loop = asyncio.get_running_loop()

        try:
            next_gc = await next_repository_volume_gc_time(
                kube,
                timeout=pass_deadline - loop.time(),
            )
            if next_gc is None:
                self._repository_volume_gc.schedule_after(
                    REPOSITORY_VOLUME_GC_EMPTY_CHECK_SECONDS
                )
                return
            if next_gc > now:
                self._repository_volume_gc.schedule_at(next_gc)
                return

            await gc_repository_volumes(
                kube,
                timeout=pass_deadline - loop.time(),
            )
            self._repository_volume_gc.schedule_after(
                REPOSITORY_VOLUME_GC_READY_CHECK_SECONDS
            )
        except (OSError, TimeoutError, ValueError) as err:
            self._repository_volume_gc.schedule_after(
                REPOSITORY_VOLUME_GC_FAILURE_RETRY_SECONDS
            )
            print(
                f"bertrand: warning: repository volume garbage collection failed: "
                f"{err}",
                file=sys.stderr,
            )

    async def _maybe_repository_snapshot_maintenance(
        self,
        kube: Kube,
        *,
        deadline: float,
    ) -> None:
        now = datetime.now(UTC)
        pass_deadline = self._repository_snapshot.pass_deadline(
            now,
            loop_deadline=deadline,
            timeout=REPOSITORY_SNAPSHOT_TIMEOUT_SECONDS,
        )
        if pass_deadline is None:
            return
        loop = asyncio.get_running_loop()

        try:
            next_snapshot = await next_repository_snapshot_time(
                kube,
                timeout=pass_deadline - loop.time(),
            )
            if next_snapshot is None:
                self._repository_snapshot.schedule_after(
                    REPOSITORY_SNAPSHOT_EMPTY_CHECK_SECONDS
                )
                return
            if next_snapshot > now:
                self._repository_snapshot.schedule_at(next_snapshot)
                return

            await maintain_repository_snapshots(
                kube,
                timeout=pass_deadline - loop.time(),
            )
            self._repository_snapshot.schedule_after(
                REPOSITORY_SNAPSHOT_READY_CHECK_SECONDS
            )
        except (OSError, TimeoutError, ValueError) as err:
            self._repository_snapshot.schedule_after(
                REPOSITORY_SNAPSHOT_FAILURE_RETRY_SECONDS
            )
            print(
                f"bertrand: warning: repository snapshot maintenance failed: {err}",
                file=sys.stderr,
            )

    async def _patch_error(self, kube: Kube, *, error: str, deadline: float) -> None:
        """Best-effort status patch for reconciliation failures."""
        loop = asyncio.get_running_loop()
        policy = await read_storage_policy(kube, timeout=deadline - loop.time())
        await patch_storage_policy_status(
            kube,
            policy=policy,
            status=self._error_status_payload(error),
            timeout=deadline - loop.time(),
        )

    async def run(self, *, timeout: float = INFINITY) -> None:
        """Run the controller loop until cancelled or timed out.

        Parameters
        ----------
        timeout : float, default=INFINITY
            Maximum controller runtime in seconds.

        Raises
        ------
        TimeoutError
            If `timeout` is non-positive or the loop exceeds the budget.
        asyncio.CancelledError
            If the surrounding task is cancelled.
        """
        if timeout <= 0:
            msg = "controller timeout must be positive"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        wake = asyncio.Event()
        wake.set()
        with Kube.inside_cluster() as kube:
            async with asyncio.TaskGroup() as group:
                for client, context in storage_watch_targets():
                    group.create_task(
                        self._watch(
                            kube,
                            client=client,
                            wake=wake,
                            deadline=deadline,
                            context=context,
                        )
                    )
                interval = STORAGE_CONTROLLER_DEFAULT_RECONCILE_SECONDS
                while True:
                    if not wake.is_set():
                        wait_timeout = min(interval, deadline - loop.time())
                        with suppress(TimeoutError):
                            await asyncio.wait_for(
                                wake.wait(),
                                timeout=wait_timeout,
                            )
                    wake.clear()
                    interval = STORAGE_CONTROLLER_DEFAULT_RECONCILE_SECONDS
                    error: str | None = None
                    try:
                        interval = await self.reconcile(kube, deadline=deadline)
                    except asyncio.CancelledError:
                        raise
                    except TimeoutError as err:
                        if deadline - loop.time() <= 0:
                            raise
                        error = str(err)
                    except (OSError, ValueError, RuntimeError) as err:
                        error = str(err)
                    if error is not None:
                        with suppress(OSError, TimeoutError, ValueError):
                            await self._patch_error(
                                kube,
                                error=error,
                                deadline=deadline,
                            )
                    await self._maybe_repository_volume_gc(kube, deadline=deadline)
                    await self._maybe_repository_snapshot_maintenance(
                        kube,
                        deadline=deadline,
                    )


class CephStorageAgent:
    """DaemonSet agent role for node-local Ceph capacity mutation."""

    def __init__(self, *, node_name: str | None = None) -> None:
        self.node_name = node_name or self.resolve_node_name()

    @staticmethod
    def resolve_node_name() -> str:
        """Resolve the Kubernetes node name for this agent process.

        Returns
        -------
        str
            Resolved Kubernetes node name.

        Raises
        ------
        OSError
            If no node name can be inferred from the process environment.
        """
        name = os.environ.get("NODE_NAME", "").strip()
        if name:
            return name
        name = sys.argv[2].strip() if len(sys.argv) > 2 else ""
        if name:
            return name
        name = platform.node().strip()
        if name:
            return name
        msg = "Ceph storage controller agent could not resolve NODE_NAME"
        raise OSError(msg)

    async def _watch_actions(
        self,
        kube: Kube,
        *,
        wake: asyncio.Event,
        deadline: float,
    ) -> None:
        loop = asyncio.get_running_loop()
        action_client: CustomObjectClient | None = None
        for client, context in storage_watch_targets():
            if context == STORAGE_ACTION_PLURAL:
                action_client = client
                break
        if action_client is None:
            msg = "Ceph storage action watch target is unavailable"
            raise RuntimeError(msg)
        while True:
            try:
                async for event in action_client.watch(
                    kube,
                    namespace=BERTRAND_NAMESPACE,
                    labels={STORAGE_CONTROLLER_LABEL: STORAGE_CONTROLLER_LABEL_VALUE},
                    timeout=deadline - loop.time(),
                    emit_initial=True,
                ):
                    try:
                        action = storage_action_from_payload(event.object.payload)
                    except OSError as err:
                        print(
                            "bertrand: warning: Ceph storage controller action watch "
                            f"saw malformed payload: {err}",
                            file=sys.stderr,
                        )
                        wake.set()
                        continue
                    if (
                        action.spec.node_name == self.node_name
                        and action.status.phase == "Pending"
                    ):
                        wake.set()
                wake.set()
                await asyncio.sleep(
                    min(STORAGE_WATCH_RESTART_DELAY_SECONDS, deadline - loop.time())
                )
            except asyncio.CancelledError:
                raise
            except (OSError, RuntimeError, ValueError) as err:
                print(
                    "bertrand: warning: Ceph storage controller action watch "
                    f"failed: {err}",
                    file=sys.stderr,
                )
                wake.set()
                await asyncio.sleep(
                    min(STORAGE_WATCH_RESTART_DELAY_SECONDS, deadline - loop.time())
                )

    async def _upsert_node_report(self, kube: Kube, *, timeout: float) -> None:
        """Report current host free capacity for this node."""
        try:
            snapshot = host_free_bytes()
            status = {
                "free_bytes": snapshot.free_bytes,
                "path": snapshot.path.as_posix(),
                "heartbeat_at": datetime.now(UTC).isoformat(),
                "last_error": "",
            }
        except OSError as err:
            status = {
                "free_bytes": 0,
                "path": "",
                "heartbeat_at": datetime.now(UTC).isoformat(),
                "last_error": str(err),
            }
        await upsert_storage_node_report(
            kube,
            node_name=self.node_name,
            status=status,
            timeout=timeout,
        )

    async def _pending_actions(
        self, kube: Kube, *, timeout: float
    ) -> list[CephStorageActionRecord]:
        """List pending actions assigned to this node.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds.

        Returns
        -------
        list[CephStorageActionRecord]
            Pending actions targeting this agent's node.
        """
        return await pending_storage_actions(
            kube,
            node_name=self.node_name,
            timeout=timeout,
        )

    async def _patch_action(
        self,
        kube: Kube,
        *,
        action: CephStorageActionRecord,
        status: Mapping[str, object],
        timeout: float,
    ) -> None:
        """Patch the status for one assigned action."""
        await patch_storage_action_status(
            kube,
            action=action,
            status=status,
            timeout=timeout,
        )

    @staticmethod
    def _grow_loop_spec(action: CephStorageActionRecord) -> str:
        if action.spec.loop_spec is None:
            msg = "grow action is missing loop_spec"
            raise ValueError(msg)
        return action.spec.loop_spec

    @staticmethod
    def _shrink_osd_id(action: CephStorageActionRecord) -> int:
        if action.spec.osd_id is None:
            msg = "shrink action is missing osd_id"
            raise ValueError(msg)
        return action.spec.osd_id

    @staticmethod
    def _block_osd_spec(action: CephStorageActionRecord) -> BlockOSDSpec:
        if action.spec.device is None:
            msg = "add-block action is missing device"
            raise ValueError(msg)
        return BlockOSDSpec(
            device=action.spec.device,
            wal_device=action.spec.wal_device,
            db_device=action.spec.db_device,
            encrypt=action.spec.encrypt,
            wipe=action.spec.wipe,
        )

    async def _execute_action(
        self, kube: Kube, *, action: CephStorageActionRecord, deadline: float
    ) -> None:
        """Claim and execute one pending action on this node.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        action : CephStorageActionRecord
            Pending action assigned to this node.
        deadline : float
            Absolute event-loop deadline for this agent run.

        Raises
        ------
        asyncio.CancelledError
            If the surrounding task is cancelled.
        """
        loop = asyncio.get_running_loop()
        try:
            await self._patch_action(
                kube,
                action=action,
                status={
                    "phase": "Running",
                    "message": "action claimed by node agent",
                    "worker_node": self.node_name,
                    "started_at": datetime.now(UTC).isoformat(),
                },
                timeout=deadline - loop.time(),
            )
            if action.spec.operation == "grow":
                osd_ids = await add_loop_osd(
                    self._grow_loop_spec(action),
                    timeout=deadline - loop.time(),
                )
                await self._patch_action(
                    kube,
                    action=action,
                    status={
                        "phase": "Succeeded",
                        "message": "microceph disk add completed",
                        "worker_node": self.node_name,
                        "created_osd_ids": list(osd_ids),
                        "osd_origin": "autoscaled-loop",
                        "osd_quality": "elastic",
                        "finished_at": datetime.now(UTC).isoformat(),
                    },
                    timeout=deadline - loop.time(),
                )
                return
            if action.spec.operation == "add-block":
                spec = self._block_osd_spec(action)
                inspections = await validate_block_osd_devices(
                    spec,
                    timeout=deadline - loop.time(),
                )
                osd_ids = await add_block_osd(
                    spec,
                    timeout=deadline - loop.time(),
                )
                source_devices = [report.path for report in inspections]
                source_device_bytes = inspections[0].size_bytes if inspections else 0
                await self._patch_action(
                    kube,
                    action=action,
                    status={
                        "phase": "Succeeded",
                        "message": "microceph block disk add completed",
                        "worker_node": self.node_name,
                        "created_osd_ids": list(osd_ids),
                        "osd_origin": "manual-block",
                        "osd_quality": "durable",
                        "source_devices": source_devices,
                        "source_device_bytes": source_device_bytes,
                        "finished_at": datetime.now(UTC).isoformat(),
                    },
                    timeout=deadline - loop.time(),
                )
                return
            osd_id = self._shrink_osd_id(action)
            await remove_osd(osd_id, timeout=deadline - loop.time())
            await self._patch_action(
                kube,
                action=action,
                status={
                    "phase": "Succeeded",
                    "message": f"microceph disk remove completed for osd.{osd_id}",
                    "worker_node": self.node_name,
                    "removed_osd_ids": [osd_id],
                    "finished_at": datetime.now(UTC).isoformat(),
                },
                timeout=deadline - loop.time(),
            )
        except asyncio.CancelledError:
            raise
        except (OSError, TimeoutError, ValueError, RuntimeError) as err:
            await self._patch_action(
                kube,
                action=action,
                status={
                    "phase": "Failed",
                    "message": str(err),
                    "diagnostics": str(err),
                    "worker_node": self.node_name,
                    "finished_at": datetime.now(UTC).isoformat(),
                },
                timeout=deadline - loop.time(),
            )

    async def sync(self, kube: Kube, *, deadline: float) -> None:
        """Run one node-agent synchronization pass.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : float
            Absolute event-loop deadline for this synchronization pass.
        """
        loop = asyncio.get_running_loop()
        await self._upsert_node_report(kube, timeout=deadline - loop.time())
        for action in await self._pending_actions(kube, timeout=deadline - loop.time()):
            await self._execute_action(kube, action=action, deadline=deadline)

    async def run(self, *, timeout: float = INFINITY) -> None:
        """Run the node agent loop until cancelled or timed out.

        Parameters
        ----------
        timeout : float, default=INFINITY
            Maximum agent runtime in seconds.

        Raises
        ------
        TimeoutError
            If `timeout` is non-positive or the loop exceeds the budget.
        """
        if timeout <= 0:
            msg = "agent timeout must be positive"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        wake = asyncio.Event()
        wake.set()
        with Kube.inside_cluster() as kube:
            async with asyncio.TaskGroup() as group:
                group.create_task(
                    self._watch_actions(kube, wake=wake, deadline=deadline)
                )
                while True:
                    if not wake.is_set():
                        wait_timeout = min(
                            STORAGE_AGENT_SYNC_INTERVAL_SECONDS,
                            deadline - loop.time(),
                        )
                        with suppress(TimeoutError):
                            await asyncio.wait_for(
                                wake.wait(),
                                timeout=wait_timeout,
                            )
                    wake.clear()
                    await self.sync(kube, deadline=deadline)


async def run_ceph_storage_controller(*, timeout: float = INFINITY) -> None:
    """Run controller reconciliation loop for Ceph storage actions."""
    await CephStorageController().run(timeout=timeout)


async def run_ceph_storage_agent(*, timeout: float = INFINITY) -> None:
    """Run node agent loop for node reports and queued growth actions."""
    await CephStorageAgent().run(timeout=timeout)


def main(argv: list[str] | None = None) -> int:
    """Entry point for control plane container role dispatch.

    Parameters
    ----------
    argv : list[str] | None, optional
        Command-line arguments without the executable name. If `None`, use
        `sys.argv[1:]`.

    Returns
    -------
    int
        Process exit code.
    """
    if argv is None:
        argv = sys.argv[1:]
    role = argv[0].strip().lower() if argv else "controller"
    if role not in {"controller", "agent"}:
        print(
            "usage: python -m bertrand.env.kube.ceph.storage [controller|agent]",
            file=sys.stderr,
        )
        return 2
    with asyncio.Runner() as runner:
        if role == "controller":
            runner.run(CephStorageController().run(timeout=INFINITY))
        else:
            runner.run(CephStorageAgent().run(timeout=INFINITY))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
