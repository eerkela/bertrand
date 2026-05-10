"""Ceph capacity autoscaler controlplane composition."""

from __future__ import annotations

import asyncio
import hashlib
import math
import os
import platform
import sys
import uuid
from contextlib import suppress
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import TYPE_CHECKING, Annotated, Literal, cast

from pydantic import (
    BaseModel,
    ConfigDict,
    Field,
    PositiveInt,
    ValidationError,
    field_validator,
)

from bertrand.env.git import BERTRAND_ENV, BERTRAND_NAMESPACE, INFINITY
from bertrand.env.kube.api import (
    CLUSTER_REGISTRY_READY_LABEL,
    CLUSTER_REGISTRY_READY_VALUE,
    ContainerSpec,
    CustomResourceSpec,
    EnvVarSpec,
    Kube,
    PolicyRuleSpec,
    SecurityContextSpec,
    VolumeMountSpec,
    VolumeSpec,
)
from bertrand.env.kube.ceph.api import (
    LOOP_OSD_SIZE_PATTERN,
    LOOP_OSD_SPEC_PATTERN,
    CephCapacitySnapshot,
    LoopOSDSpec,
    add_loop_osd,
    ceph_df,
    host_free_bytes,
    parse_loop_osd_spec,
    parse_size_bytes,
)
from bertrand.env.kube.crd import CustomResourceClient, CustomResourceDefinition
from bertrand.env.kube.daemonset import DaemonSet
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.node import Node
from bertrand.env.kube.rbac import ClusterRole, ClusterRoleBinding
from bertrand.env.kube.service_account import ServiceAccount

if TYPE_CHECKING:
    from collections.abc import Collection, Mapping

    from bertrand.env.kube.build.job import BuildKitImageBuild

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
AUTOSCALE_IMAGE_CONTEXT_PREFIX = "bertrand-ceph-autoscaler"
AUTOSCALE_PHASES = ("Pending", "Running", "Succeeded", "Failed")
AUTOSCALE_NODE_REPORT_MAX_AGE_SECONDS = 120
AUTOSCALE_WATCH_RESTART_DELAY_SECONDS = 1.0
AUTOSCALE_CONTROLLER_DEFAULT_RECONCILE_SECONDS = 30.0
AUTOSCALE_AGENT_SYNC_INTERVAL_SECONDS = 5.0
HOST_ROOT_VOLUME = "host-root"
HOST_ROOT_MOUNT = "/host"
AUTOSCALE_LABELS = {BERTRAND_ENV: "1", AUTOSCALE_LABEL: AUTOSCALE_LABEL_VALUE}

type _Watermark = Annotated[float, Field(gt=0.0, lt=1.0)]
type _LoopSize = Annotated[str, Field(pattern=LOOP_OSD_SIZE_PATTERN)]
type _LoopSpec = Annotated[str, Field(pattern=LOOP_OSD_SPEC_PATTERN)]
type _ActionPhase = Literal["Pending", "Running", "Succeeded", "Failed"]


class _ObjectMeta(BaseModel):
    """Validated subset of Kubernetes object metadata."""

    model_config = ConfigDict(extra="ignore")

    name: str = ""
    namespace: str = ""
    generation: int = 0
    resource_version: str = Field(default="", alias="resourceVersion")
    labels: dict[str, str] = Field(default_factory=dict)


class _CephAutoscalerSpec(BaseModel):
    """Desired policy for Ceph capacity autoscaling."""

    model_config = ConfigDict(extra="forbid")

    enabled: bool = True
    high_watermark: _Watermark = 0.75
    target_watermark: _Watermark = 0.65
    loop_size: _LoopSize = "4G"
    max_actions_per_reconcile: PositiveInt = 3
    reconcile_interval_seconds: PositiveInt = 30

    @field_validator("loop_size")
    @classmethod
    def _validate_loop_size(cls, value: str) -> str:
        return LoopOSDSpec(size=value).size


class _CephAutoscalerStatus(BaseModel):
    """Observed status emitted by the Ceph capacity controller."""

    model_config = ConfigDict(extra="forbid")

    observed_generation: int | None = Field(default=None, alias="observedGeneration")
    total_bytes: int | None = None
    used_bytes: int | None = None
    used_ratio: float | None = None
    pending_actions: int = 0
    running_actions: int = 0
    succeeded_actions: int = 0
    failed_actions: int = 0
    last_reconciled_at: datetime | None = None
    last_error: str = ""


class _CephAutoscaler(BaseModel):
    """Validated `CephStorageAutoscaler` custom-resource payload."""

    model_config = ConfigDict(extra="forbid")

    api_version: str = Field(alias="apiVersion")
    kind: Literal["CephStorageAutoscaler"]
    metadata: _ObjectMeta
    spec: _CephAutoscalerSpec = Field(default_factory=_CephAutoscalerSpec)
    status: _CephAutoscalerStatus | None = None


class _CephStorageActionSpec(BaseModel):
    """Desired node-local growth action contract."""

    model_config = ConfigDict(extra="forbid")

    policy_generation: Annotated[int, Field(ge=0)]
    node_name: Annotated[str, Field(min_length=1)]
    loop_spec: _LoopSpec
    reason: Annotated[str, Field(min_length=1)]

    @field_validator("loop_spec")
    @classmethod
    def _validate_loop_spec(cls, value: str) -> str:
        return parse_loop_osd_spec(value).render()


class _CephStorageActionStatus(BaseModel):
    """Observed lifecycle state for one node-local growth action."""

    model_config = ConfigDict(extra="forbid")

    phase: _ActionPhase = "Pending"
    started_at: datetime | None = None
    finished_at: datetime | None = None
    message: str = ""
    worker_node: str = ""


class _CephStorageAction(BaseModel):
    """Validated `CephStorageAction` custom-resource payload."""

    model_config = ConfigDict(extra="forbid")

    api_version: str = Field(alias="apiVersion")
    kind: Literal["CephStorageAction"]
    metadata: _ObjectMeta
    spec: _CephStorageActionSpec
    status: _CephStorageActionStatus = Field(default_factory=_CephStorageActionStatus)


class _CephStorageNodeSpec(BaseModel):
    """Desired identity contract for one node capacity report."""

    model_config = ConfigDict(extra="forbid")

    node_name: Annotated[str, Field(min_length=1)]


class _CephStorageNodeStatus(BaseModel):
    """Observed host-local capacity state reported by one node agent."""

    model_config = ConfigDict(extra="forbid")

    free_bytes: Annotated[int, Field(ge=0)] = 0
    path: str = ""
    heartbeat_at: datetime | None = None
    last_error: str = ""


class _CephStorageNode(BaseModel):
    """Validated `CephStorageNode` custom-resource payload."""

    model_config = ConfigDict(extra="forbid")

    api_version: str = Field(alias="apiVersion")
    kind: Literal["CephStorageNode"]
    metadata: _ObjectMeta
    spec: _CephStorageNodeSpec
    status: _CephStorageNodeStatus | None = None


@dataclass(frozen=True)
class _PlannedAction:
    """One node-scoped MicroCeph growth action selected by policy planning."""

    node_name: str
    loop_spec: str
    reason: str


_AUTOSCALER_SPEC_SCHEMA = {
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
            "pattern": LOOP_OSD_SIZE_PATTERN,
            "default": "4G",
        },
        "max_actions_per_reconcile": {"type": "integer", "minimum": 1, "default": 3},
        "reconcile_interval_seconds": {"type": "integer", "minimum": 1, "default": 30},
    },
}
_AUTOSCALER_STATUS_SCHEMA = {
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
}
_ACTION_SPEC_SCHEMA = {
    "type": "object",
    "required": ["policy_generation", "node_name", "loop_spec", "reason"],
    "properties": {
        "policy_generation": {"type": "integer", "minimum": 0},
        "node_name": {"type": "string", "minLength": 1},
        "loop_spec": {"type": "string", "pattern": LOOP_OSD_SPEC_PATTERN},
        "reason": {"type": "string", "minLength": 1},
    },
}
_ACTION_STATUS_SCHEMA = {
    "type": "object",
    "properties": {
        "phase": {"type": "string", "enum": list(AUTOSCALE_PHASES)},
        "started_at": {"type": "string", "format": "date-time"},
        "finished_at": {"type": "string", "format": "date-time"},
        "message": {"type": "string"},
        "worker_node": {"type": "string"},
    },
}
_NODE_REPORT_SPEC_SCHEMA = {
    "type": "object",
    "required": ["node_name"],
    "properties": {"node_name": {"type": "string", "minLength": 1}},
}
_NODE_REPORT_STATUS_SCHEMA = {
    "type": "object",
    "properties": {
        "free_bytes": {"type": "integer", "minimum": 0},
        "path": {"type": "string"},
        "heartbeat_at": {"type": "string", "format": "date-time"},
        "last_error": {"type": "string"},
    },
}

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
_AUTOSCALER_CLIENT = CustomResourceClient(AUTOSCALER)
_ACTION_CLIENT = CustomResourceClient(ACTION)
_NODE_REPORT_CLIENT = CustomResourceClient(NODE_REPORT)


async def _ensure_crd(
    kube: Kube,
    *,
    plural: str,
    singular: str,
    kind: str,
    short_names: Collection[str],
    spec_schema: Mapping[str, object],
    status_schema: Mapping[str, object],
    deadline: float,
) -> None:
    loop = asyncio.get_running_loop()
    crd = await CustomResourceDefinition.upsert(
        kube,
        group=AUTOSCALE_GROUP,
        version=AUTOSCALE_VERSION,
        plural=plural,
        singular=singular,
        kind=kind,
        short_names=short_names,
        spec_schema=spec_schema,
        status_schema=status_schema,
        labels=AUTOSCALE_LABELS,
        timeout=deadline - loop.time(),
    )
    await crd.wait_established(kube, timeout=deadline - loop.time())


async def _ensure_rbac(kube: Kube, *, deadline: float) -> None:
    loop = asyncio.get_running_loop()
    await ServiceAccount.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=AUTOSCALE_SERVICE_ACCOUNT,
        labels=AUTOSCALE_LABELS,
        timeout=deadline - loop.time(),
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
        timeout=deadline - loop.time(),
    )
    await ClusterRoleBinding.upsert(
        kube,
        name=AUTOSCALE_SERVICE_ACCOUNT,
        role_name=AUTOSCALE_SERVICE_ACCOUNT,
        service_account_name=AUTOSCALE_SERVICE_ACCOUNT,
        service_account_namespace=BERTRAND_NAMESPACE,
        labels=AUTOSCALE_LABELS,
        timeout=deadline - loop.time(),
    )


async def _ensure_default_policy(kube: Kube, *, deadline: float) -> None:
    loop = asyncio.get_running_loop()
    await _AUTOSCALER_CLIENT.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=AUTOSCALE_DEFAULT_NAME,
        spec=cast("dict[str, object]", _CephAutoscalerSpec().model_dump(mode="json")),
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
        name=AUTOSCALE_CONTROLLER_NAME,
        labels={
            "app.kubernetes.io/name": AUTOSCALE_CONTROLLER_NAME,
            "app.kubernetes.io/part-of": "bertrand",
            **AUTOSCALE_LABELS,
        },
        selector={"app.kubernetes.io/name": AUTOSCALE_CONTROLLER_NAME},
        containers=[container("controller")],
        volumes=volumes,
        service_account_name=AUTOSCALE_SERVICE_ACCOUNT,
        automount_service_account_token=True,
        node_selector={CLUSTER_REGISTRY_READY_LABEL: CLUSTER_REGISTRY_READY_VALUE},
        host_pid=True,
        timeout=deadline - loop.time(),
    )
    await controller.wait_rollout(kube, timeout=deadline - loop.time())

    agent = await DaemonSet.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=AUTOSCALE_AGENT_NAME,
        labels={
            "app.kubernetes.io/name": AUTOSCALE_AGENT_NAME,
            "app.kubernetes.io/part-of": "bertrand",
            **AUTOSCALE_LABELS,
        },
        selector={"app.kubernetes.io/name": AUTOSCALE_AGENT_NAME},
        containers=[container("agent")],
        volumes=volumes,
        service_account_name=AUTOSCALE_SERVICE_ACCOUNT,
        automount_service_account_token=True,
        node_selector={CLUSTER_REGISTRY_READY_LABEL: CLUSTER_REGISTRY_READY_VALUE},
        host_pid=True,
        timeout=deadline - loop.time(),
    )
    await agent.wait_rollout(kube, timeout=deadline - loop.time())


def ceph_autoscaler_image_build() -> BuildKitImageBuild:
    """Return the autoscaler controlplane image build contract.

    Returns
    -------
    BuildKitImageBuild
        Build contract for the Ceph autoscaler controller/agent image.
    """
    from bertrand.env.kube.build.job import BuildKitImageBuild
    from bertrand.env.kube.build.repository import IMAGES

    repo_root = Path(__file__).resolve().parents[4]
    h = hashlib.sha256()
    for path in (
        Path(__file__).resolve(),
        Path(__file__).with_name("api.py").resolve(),
        repo_root / "bertrand/env/kube/api.py",
        repo_root / "bertrand/env/kube/crd.py",
        repo_root / "bertrand/env/kube/daemonset.py",
        repo_root / "bertrand/env/kube/deployment.py",
        repo_root / "bertrand/env/kube/node.py",
        repo_root / "bertrand/env/kube/rbac.py",
        repo_root / "bertrand/env/kube/service_account.py",
        repo_root / "bertrand/env/git/__init__.py",
        repo_root / "bertrand/env/git/bertrand_git.py",
    ):
        payload = path.read_bytes()
        h.update(len(payload).to_bytes(8, "big"))
        h.update(payload)
    image = IMAGES.ref("ceph-autoscaler", f"v1-{h.hexdigest()[:12]}")
    return BuildKitImageBuild(
        image=image,
        dockerfile="\n".join(
            (
                "FROM python:3.12-slim",
                "WORKDIR /opt/bertrand",
                "ENV PYTHONUNBUFFERED=1",
                "ENV PYTHONPATH=/opt/bertrand",
                "COPY bertrand /opt/bertrand/bertrand",
                "RUN python -m pip install --no-cache-dir "
                "'pydantic>=2,<3' 'kubernetes>=32,<35'",
                "ENTRYPOINT [\"python\", \"-m\", \"bertrand.env.kube.ceph.autoscale\"]",
            )
        )
        + "\n",
        context_copies=((repo_root / "bertrand", Path("bertrand")),),
        context_prefix=AUTOSCALE_IMAGE_CONTEXT_PREFIX,
        build_labels={BERTRAND_ENV: "1"},
    )


async def ensure_ceph_autoscaler(kube: Kube, *, image: str, timeout: float) -> None:
    """Converge Ceph autoscaler CRDs, RBAC, and workloads in the local cluster.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    image : str
        Fully qualified autoscaler image reference.
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
        msg = "controlplane image reference cannot be empty"
        raise ValueError(msg)
    deadline = asyncio.get_running_loop().time() + timeout
    await _ensure_crd(
        kube,
        plural=AUTOSCALE_AUTOSCALER_PLURAL,
        singular="cephstorageautoscaler",
        kind=AUTOSCALE_AUTOSCALER_KIND,
        short_names=["csa"],
        spec_schema=_AUTOSCALER_SPEC_SCHEMA,
        status_schema=_AUTOSCALER_STATUS_SCHEMA,
        deadline=deadline,
    )
    await _ensure_crd(
        kube,
        plural=AUTOSCALE_ACTION_PLURAL,
        singular="cephstorageaction",
        kind=AUTOSCALE_ACTION_KIND,
        short_names=["csact"],
        spec_schema=_ACTION_SPEC_SCHEMA,
        status_schema=_ACTION_STATUS_SCHEMA,
        deadline=deadline,
    )
    await _ensure_crd(
        kube,
        plural=AUTOSCALE_NODE_PLURAL,
        singular="cephstoragenode",
        kind=AUTOSCALE_NODE_KIND,
        short_names=["csnode"],
        spec_schema=_NODE_REPORT_SPEC_SCHEMA,
        status_schema=_NODE_REPORT_STATUS_SCHEMA,
        deadline=deadline,
    )
    await _ensure_rbac(kube, deadline=deadline)
    await _ensure_default_policy(kube, deadline=deadline)
    await _ensure_workloads(kube, image=image, deadline=deadline)


class CephAutoscalerController:
    """Controller role for cluster-wide Ceph capacity planning."""

    def __init__(self) -> None:
        self._offset = 0

    async def _watch(
        self,
        kube: Kube,
        *,
        client: CustomResourceClient,
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
                    labels={AUTOSCALE_LABEL: AUTOSCALE_LABEL_VALUE},
                    timeout=deadline - loop.time(),
                ):
                    wake.set()
                wake.set()
                await asyncio.sleep(
                    min(AUTOSCALE_WATCH_RESTART_DELAY_SECONDS, deadline - loop.time())
                )
            except asyncio.CancelledError:
                raise
            except (OSError, RuntimeError, ValueError) as err:
                print(
                    f"bertrand: warning: Ceph autoscaler {context} watch failed: {err}",
                    file=sys.stderr,
                )
                wake.set()
                await asyncio.sleep(
                    min(AUTOSCALE_WATCH_RESTART_DELAY_SECONDS, deadline - loop.time())
                )

    @staticmethod
    def _action_counts(
        actions: Collection[_CephStorageAction],
    ) -> dict[str, int]:
        counts: dict[str, int] = dict.fromkeys(AUTOSCALE_PHASES, 0)
        for action in actions:
            counts[action.status.phase] += 1
        return counts

    @staticmethod
    def _eligible_nodes(
        *,
        ready_nodes: Collection[str],
        reports: Collection[_CephStorageNode],
        loop_bytes: int,
    ) -> list[str]:
        ready = frozenset(ready_nodes)
        now = datetime.now(UTC)
        eligible: list[str] = []
        for report in reports:
            status = report.status
            if report.spec.node_name not in ready or status is None:
                continue
            heartbeat = status.heartbeat_at
            if heartbeat is None:
                continue
            if heartbeat.tzinfo is None:
                heartbeat = heartbeat.replace(tzinfo=UTC)
            heartbeat = heartbeat.astimezone(UTC)
            if (
                now - heartbeat
            ).total_seconds() > AUTOSCALE_NODE_REPORT_MAX_AGE_SECONDS:
                continue
            slots = status.free_bytes // loop_bytes
            eligible.extend([report.spec.node_name] * min(slots, 32))
        return sorted(eligible)

    def _plan_actions(
        self,
        *,
        policy: _CephAutoscaler,
        capacity: CephCapacitySnapshot,
        actions: Collection[_CephStorageAction],
        eligible_nodes: list[str],
    ) -> list[_PlannedAction]:
        spec = policy.spec
        if (
            not spec.enabled
            or not eligible_nodes
            or capacity.used_ratio < spec.high_watermark
        ):
            return []
        loop_bytes = parse_size_bytes(spec.loop_size)
        target_used = spec.target_watermark * capacity.total_bytes
        deficit = capacity.used_bytes - target_used
        if deficit <= 0:
            return []

        counts = self._action_counts(actions)
        in_flight = counts["Pending"] + counts["Running"]
        budget = spec.max_actions_per_reconcile - in_flight
        if budget <= 0:
            return []

        desired = math.ceil(deficit / loop_bytes)
        count = max(0, min(desired, budget, len(eligible_nodes)))
        planned: list[_PlannedAction] = []
        for index in range(count):
            node = eligible_nodes[(self._offset + index) % len(eligible_nodes)]
            planned.append(
                _PlannedAction(
                    node_name=node,
                    loop_spec=LoopOSDSpec(size=spec.loop_size).render(),
                    reason=(
                        "cluster usage "
                        f"{capacity.used_ratio:.2%} >= high watermark "
                        f"{spec.high_watermark:.2%}"
                    ),
                )
            )
        if eligible_nodes:
            self._offset = (self._offset + count) % len(eligible_nodes)
        return planned

    async def _read_policy(self, kube: Kube, *, timeout: float) -> _CephAutoscaler:
        """Read and validate the singleton autoscaler policy.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds.

        Returns
        -------
        _CephAutoscaler
            Validated singleton autoscaler policy.

        Raises
        ------
        OSError
            If the singleton policy resource does not exist.
        """
        obj = await _AUTOSCALER_CLIENT.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=AUTOSCALE_DEFAULT_NAME,
            timeout=timeout,
        )
        if obj is None:
            msg = f"{AUTOSCALE_AUTOSCALER_KIND} {AUTOSCALE_DEFAULT_NAME!r} is missing"
            raise OSError(msg)
        return _CephAutoscaler.model_validate(obj.payload)

    async def _list_actions(
        self, kube: Kube, *, timeout: float
    ) -> list[_CephStorageAction]:
        """List and validate autoscaler action resources.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds.

        Returns
        -------
        list[_CephStorageAction]
            Validated action resources.
        """
        objects = await _ACTION_CLIENT.list(
            kube,
            namespace=BERTRAND_NAMESPACE,
            labels={AUTOSCALE_LABEL: AUTOSCALE_LABEL_VALUE},
            timeout=timeout,
        )
        return [_CephStorageAction.model_validate(obj.payload) for obj in objects]

    async def _list_node_reports(
        self, kube: Kube, *, timeout: float
    ) -> list[_CephStorageNode]:
        """List and validate node capacity report resources.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds.

        Returns
        -------
        list[_CephStorageNode]
            Validated node capacity report resources.
        """
        objects = await _NODE_REPORT_CLIENT.list(
            kube,
            namespace=BERTRAND_NAMESPACE,
            labels={AUTOSCALE_LABEL: AUTOSCALE_LABEL_VALUE},
            timeout=timeout,
        )
        return [_CephStorageNode.model_validate(obj.payload) for obj in objects]

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

    async def _create_actions(
        self,
        kube: Kube,
        *,
        policy_generation: int,
        actions: Collection[_PlannedAction],
        timeout: float,
    ) -> None:
        """Create node-scoped growth action resources."""
        for action in actions:
            await _ACTION_CLIENT.create(
                kube,
                namespace=BERTRAND_NAMESPACE,
                name=f"{AUTOSCALE_DEFAULT_NAME}-{uuid.uuid4().hex[:12]}",
                spec={
                    "policy_generation": policy_generation,
                    "node_name": action.node_name,
                    "loop_spec": action.loop_spec,
                    "reason": action.reason,
                },
                timeout=timeout,
            )

    async def _patch_status(
        self,
        kube: Kube,
        *,
        policy: _CephAutoscaler,
        status: Mapping[str, object],
        timeout: float,
    ) -> None:
        """Patch the singleton autoscaler status."""
        payload = {"observedGeneration": policy.metadata.generation, **dict(status)}
        await _AUTOSCALER_CLIENT.patch_status(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=AUTOSCALE_DEFAULT_NAME,
            status=payload,
            timeout=timeout,
        )

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
        policy = await self._read_policy(kube, timeout=deadline - loop.time())
        actions = await self._list_actions(kube, timeout=deadline - loop.time())
        reports = await self._list_node_reports(kube, timeout=deadline - loop.time())
        capacity = await ceph_df(timeout=deadline - loop.time())
        planned = self._plan_actions(
            policy=policy,
            capacity=capacity,
            actions=actions,
            eligible_nodes=self._eligible_nodes(
                ready_nodes=await self._ready_nodes(
                    kube,
                    timeout=deadline - loop.time(),
                ),
                reports=reports,
                loop_bytes=parse_size_bytes(policy.spec.loop_size),
            ),
        )
        if planned:
            await self._create_actions(
                kube,
                policy_generation=policy.metadata.generation,
                actions=planned,
                timeout=deadline - loop.time(),
            )
            actions = await self._list_actions(kube, timeout=deadline - loop.time())
        counts = self._action_counts(actions)
        await self._patch_status(
            kube,
            policy=policy,
            status={
                "total_bytes": capacity.total_bytes,
                "used_bytes": capacity.used_bytes,
                "used_ratio": capacity.used_ratio,
                "pending_actions": counts.get("Pending", 0),
                "running_actions": counts.get("Running", 0),
                "succeeded_actions": counts.get("Succeeded", 0),
                "failed_actions": counts.get("Failed", 0),
                "last_reconciled_at": datetime.now(UTC).isoformat(),
                "last_error": "",
            },
            timeout=deadline - loop.time(),
        )
        return float(policy.spec.reconcile_interval_seconds)

    async def _patch_error(self, kube: Kube, *, error: str, deadline: float) -> None:
        """Best-effort status patch for reconciliation failures."""
        loop = asyncio.get_running_loop()
        policy = await self._read_policy(kube, timeout=deadline - loop.time())
        await self._patch_status(
            kube,
            policy=policy,
            status={
                "total_bytes": None,
                "used_bytes": None,
                "used_ratio": None,
                "pending_actions": 0,
                "running_actions": 0,
                "succeeded_actions": 0,
                "failed_actions": 0,
                "last_reconciled_at": datetime.now(UTC).isoformat(),
                "last_error": error,
            },
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
                group.create_task(
                    self._watch(
                        kube,
                        client=_AUTOSCALER_CLIENT,
                        wake=wake,
                        deadline=deadline,
                        context=AUTOSCALE_AUTOSCALER_PLURAL,
                    )
                )
                group.create_task(
                    self._watch(
                        kube,
                        client=_ACTION_CLIENT,
                        wake=wake,
                        deadline=deadline,
                        context=AUTOSCALE_ACTION_PLURAL,
                    )
                )
                group.create_task(
                    self._watch(
                        kube,
                        client=_NODE_REPORT_CLIENT,
                        wake=wake,
                        deadline=deadline,
                        context=AUTOSCALE_NODE_PLURAL,
                    )
                )
                interval = AUTOSCALE_CONTROLLER_DEFAULT_RECONCILE_SECONDS
                while True:
                    if not wake.is_set():
                        wait_timeout = min(interval, deadline - loop.time())
                        with suppress(TimeoutError):
                            await asyncio.wait_for(
                                wake.wait(),
                                timeout=wait_timeout,
                            )
                    wake.clear()
                    interval = AUTOSCALE_CONTROLLER_DEFAULT_RECONCILE_SECONDS
                    try:
                        interval = await self.reconcile(kube, deadline=deadline)
                    except asyncio.CancelledError:
                        raise
                    except ValidationError as err:
                        with suppress(OSError, TimeoutError, ValueError):
                            await self._patch_error(
                                kube,
                                error=(
                                    "malformed autoscaler custom resource payload: "
                                    f"{err}"
                                ),
                                deadline=deadline,
                            )
                    except TimeoutError as err:
                        if deadline - loop.time() <= 0:
                            raise
                        with suppress(OSError, TimeoutError, ValueError):
                            await self._patch_error(
                                kube,
                                error=str(err),
                                deadline=deadline,
                            )
                    except (OSError, ValueError, RuntimeError) as err:
                        with suppress(OSError, TimeoutError, ValueError):
                            await self._patch_error(
                                kube,
                                error=str(err),
                                deadline=deadline,
                            )


class CephAutoscalerAgent:
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
        msg = "Ceph autoscaler agent could not resolve NODE_NAME"
        raise OSError(msg)

    async def _watch_actions(
        self,
        kube: Kube,
        *,
        wake: asyncio.Event,
        deadline: float,
    ) -> None:
        loop = asyncio.get_running_loop()
        while True:
            try:
                async for event in _ACTION_CLIENT.watch(
                    kube,
                    namespace=BERTRAND_NAMESPACE,
                    labels={AUTOSCALE_LABEL: AUTOSCALE_LABEL_VALUE},
                    timeout=deadline - loop.time(),
                    emit_initial=True,
                ):
                    try:
                        action = _CephStorageAction.model_validate(event.object.payload)
                    except ValidationError as err:
                        print(
                            "bertrand: warning: Ceph autoscaler action watch "
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
                    min(AUTOSCALE_WATCH_RESTART_DELAY_SECONDS, deadline - loop.time())
                )
            except asyncio.CancelledError:
                raise
            except (OSError, RuntimeError, ValueError) as err:
                print(
                    f"bertrand: warning: Ceph autoscaler action watch failed: {err}",
                    file=sys.stderr,
                )
                wake.set()
                await asyncio.sleep(
                    min(AUTOSCALE_WATCH_RESTART_DELAY_SECONDS, deadline - loop.time())
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
        await _NODE_REPORT_CLIENT.upsert(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=self.node_name,
            spec={"node_name": self.node_name},
            timeout=timeout,
        )
        await _NODE_REPORT_CLIENT.patch_status(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=self.node_name,
            status=status,
            timeout=timeout,
        )

    async def _pending_actions(
        self, kube: Kube, *, timeout: float
    ) -> list[_CephStorageAction]:
        """List pending actions assigned to this node.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds.

        Returns
        -------
        list[_CephStorageAction]
            Pending actions targeting this agent's node.
        """
        objects = await _ACTION_CLIENT.list(
            kube,
            namespace=BERTRAND_NAMESPACE,
            labels={AUTOSCALE_LABEL: AUTOSCALE_LABEL_VALUE},
            timeout=timeout,
        )
        actions = [_CephStorageAction.model_validate(obj.payload) for obj in objects]
        pending = [
            action
            for action in actions
            if action.spec.node_name == self.node_name
            and action.status.phase == "Pending"
        ]
        pending.sort(key=lambda action: action.metadata.name)
        return pending

    async def _patch_action(
        self,
        kube: Kube,
        *,
        action: _CephStorageAction,
        status: Mapping[str, object],
        timeout: float,
    ) -> None:
        """Patch the status for one assigned action."""
        await _ACTION_CLIENT.patch_status(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=action.metadata.name,
            status=status,
            timeout=timeout,
        )

    async def _execute_action(
        self, kube: Kube, *, action: _CephStorageAction, deadline: float
    ) -> None:
        """Claim and execute one pending action on this node.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        action : _CephStorageAction
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
            await add_loop_osd(action.spec.loop_spec, timeout=deadline - loop.time())
            await self._patch_action(
                kube,
                action=action,
                status={
                    "phase": "Succeeded",
                    "message": "microceph disk add completed",
                    "worker_node": self.node_name,
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
                            AUTOSCALE_AGENT_SYNC_INTERVAL_SECONDS,
                            deadline - loop.time(),
                        )
                        with suppress(TimeoutError):
                            await asyncio.wait_for(
                                wake.wait(),
                                timeout=wait_timeout,
                            )
                    wake.clear()
                    await self.sync(kube, deadline=deadline)


async def run_ceph_autoscaler_controller(*, timeout: float = INFINITY) -> None:
    """Run controller reconciliation loop for Ceph autoscaling actions."""
    await CephAutoscalerController().run(timeout=timeout)


async def run_ceph_autoscaler_agent(*, timeout: float = INFINITY) -> None:
    """Run node agent loop for node reports and queued growth actions."""
    await CephAutoscalerAgent().run(timeout=timeout)


def main(argv: list[str] | None = None) -> int:
    """Entry point for controlplane container role dispatch.

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
            "usage: python -m bertrand.env.kube.ceph.autoscale [controller|agent]",
            file=sys.stderr,
        )
        return 2
    with asyncio.Runner() as runner:
        if role == "controller":
            runner.run(CephAutoscalerController().run(timeout=INFINITY))
        else:
            runner.run(CephAutoscalerAgent().run(timeout=INFINITY))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
