"""Ceph capacity controlplane for Bertrand-managed MicroCeph clusters.

This module defines a minimal grow-only storage autoscaler that:

1. publishes CRDs for autoscaler policy and node-scoped actions,
2. deploys a controller + daemonset agent pair into Kubernetes,
3. reconciles loop-backed OSD growth when Ceph usage crosses a threshold.

Notes
-----
This is intentionally a v1 surface: it only grows capacity, and never shrinks or
rebalances existing OSD devices.
"""
from __future__ import annotations

import asyncio
import hashlib
import json
import math
import os
import platform
import shutil
import subprocess
import sys
import uuid
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Annotated, Literal

from kubernetes import watch as kube_watch
from kubernetes.client.rest import ApiException
from pydantic import BaseModel, ConfigDict, Field, PositiveInt, ValidationError

from ...config.core import KubeName, NonEmpty, Trimmed
from ...run import (
    BERTRAND_ENV,
    BERTRAND_NAMESPACE,
    INFINITY,
    kubectl,
    run,
)
from ..api import (
    CLUSTER_REGISTRY_READY_LABEL,
    CLUSTER_REGISTRY_READY_VALUE,
    Kube,
)
from ..image import (
    ClusterImageBuild,
)
from ..node import Node

AUTOSCALE_GROUP = "ceph.bertrand.dev"
AUTOSCALE_VERSION = "v1alpha1"
AUTOSCALE_AUTOSCALER_KIND = "CephStorageAutoscaler"
AUTOSCALE_AUTOSCALER_PLURAL = "cephstorageautoscalers"
AUTOSCALE_ACTION_KIND = "CephStorageAction"
AUTOSCALE_ACTION_PLURAL = "cephstorageactions"
AUTOSCALE_DEFAULT_NAME = "default"
AUTOSCALE_SERVICE_ACCOUNT = "bertrand-ceph-autoscaler"
AUTOSCALE_CONTROLLER_NAME = "bertrand-ceph-autoscaler"
AUTOSCALE_AGENT_NAME = "bertrand-ceph-autoscaler-agent"
AUTOSCALE_LABEL = "bertrand.dev/ceph-autoscaler"
AUTOSCALE_LABEL_VALUE = "v1"
AUTOSCALE_IMAGE_REPO = "localhost:32000/bertrand/ceph-autoscaler"
AUTOSCALE_IMAGE_CONTEXT_PREFIX = "bertrand-ceph-autoscaler"
AUTOSCALE_LOOP_SIZE_RE = r"^[1-9][0-9]*[MGT]$"
AUTOSCALE_LOOP_SPEC_RE = r"^loop,[1-9][0-9]*[MGT],[1-9][0-9]*$"
AUTOSCALE_PHASES = ("Pending", "Running", "Succeeded", "Failed")


type Watermark = Annotated[float, Field(gt=0.0, lt=1.0)]
type LoopSize = Annotated[str, Field(pattern=AUTOSCALE_LOOP_SIZE_RE)]
type LoopSpec = Annotated[str, Field(pattern=AUTOSCALE_LOOP_SPEC_RE)]


@dataclass(frozen=True)
class CephCapacitySnapshot:
    """Raw Ceph capacity snapshot used by autoscaler reconciliation.

    Attributes
    ----------
    total_bytes : int
        Total raw cluster capacity in bytes.
    used_bytes : int
        Currently used raw cluster capacity in bytes.
    used_ratio : float
        Used/total ratio in [0, 1].
    """

    total_bytes: int
    used_bytes: int
    used_ratio: float


@dataclass(frozen=True)
class PlannedAction:
    """One planned growth action produced by controller reconciliation.

    Attributes
    ----------
    node_name : str
        Kubernetes node name where this action should run.
    loop_spec : str
        MicroCeph `disk add` loop-file spec (for example `loop,4G,1`).
    reason : str
        Human-readable rationale for enqueueing this action.
    """

    node_name: KubeName
    loop_spec: LoopSpec
    reason: NonEmpty[Trimmed]


@dataclass
class ControllerState:
    """Mutable controller loop state for watch+tick reconciliation.

    Attributes
    ----------
    actions_resource_version : str
        Last observed resourceVersion for action watch resumptions.
    round_robin_offset : int
        Monotonic node selection cursor for deterministic load distribution.
    """

    actions_resource_version: str = ""
    round_robin_offset: int = 0


class ObjectMeta(BaseModel):
    """Validated subset of Kubernetes object metadata."""

    model_config = ConfigDict(extra="ignore")
    name: Annotated[str, Field(default="")]
    namespace: Annotated[str, Field(default="")]
    generation: Annotated[int, Field(default=0)]
    resourceVersion: Annotated[str, Field(default="")]
    labels: Annotated[dict[str, str], Field(default_factory=dict)]


class CephStorageAutoscalerSpec(BaseModel):
    """Policy contract for autoscaler behavior.

    Parameters
    ----------
    enabled : bool, optional
        Whether controller reconcile should enqueue growth actions.
    high_watermark : float, optional
        Utilization ratio at or above which growth should trigger.
    target_watermark : float, optional
        Utilization ratio to target after adding new loop-backed OSDs.
    loop_size : str, optional
        Loop-file size suffix passed to MicroCeph (`M`, `G`, or `T` units).
    max_actions_per_reconcile : int, optional
        Hard cap on number of actions generated in one reconcile cycle.
    reconcile_interval_seconds : int, optional
        Tick interval between reconciliations.
    """

    model_config = ConfigDict(extra="forbid")
    enabled: bool = True
    high_watermark: Watermark = 0.75
    target_watermark: Watermark = 0.65
    loop_size: LoopSize = "4G"
    max_actions_per_reconcile: PositiveInt = 3
    reconcile_interval_seconds: PositiveInt = 30


class CephStorageAutoscalerStatus(BaseModel):
    """Observed status emitted by controller reconcile cycles.

    Attributes
    ----------
    observedGeneration : int | None
        Most recent autoscaler generation processed by controller loop.
    total_bytes : int | None
        Last observed Ceph raw capacity in bytes.
    used_bytes : int | None
        Last observed Ceph used raw bytes.
    used_ratio : float | None
        Last observed used/total ratio in [0, 1].
    pending_actions : int
        Number of queued `CephStorageAction` objects in `Pending` phase.
    running_actions : int
        Number of queued `CephStorageAction` objects in `Running` phase.
    succeeded_actions : int
        Number of queued `CephStorageAction` objects in `Succeeded` phase.
    failed_actions : int
        Number of queued `CephStorageAction` objects in `Failed` phase.
    last_reconciled_at : datetime | None
        Timestamp of the last controller reconcile completion.
    last_error : str
        Last reconciliation error message, if any.
    """

    model_config = ConfigDict(extra="forbid")
    observedGeneration: Annotated[int | None, Field(default=None)] = None
    total_bytes: Annotated[int | None, Field(default=None)] = None
    used_bytes: Annotated[int | None, Field(default=None)] = None
    used_ratio: Annotated[float | None, Field(default=None)] = None
    pending_actions: Annotated[int, Field(default=0, ge=0)] = 0
    running_actions: Annotated[int, Field(default=0, ge=0)] = 0
    succeeded_actions: Annotated[int, Field(default=0, ge=0)] = 0
    failed_actions: Annotated[int, Field(default=0, ge=0)] = 0
    last_reconciled_at: Annotated[datetime | None, Field(default=None)] = None
    last_error: Annotated[str, Field(default="")] = ""


class CephStorageAutoscaler(BaseModel):
    """Validated payload for `CephStorageAutoscaler` custom resources.

    Attributes
    ----------
    apiVersion : str
        Kubernetes API version string for this custom resource.
    kind : Literal[\"CephStorageAutoscaler\"]
        Kubernetes kind identifier for this custom resource.
    metadata : ObjectMeta
        Kubernetes object metadata subset.
    spec : CephStorageAutoscalerSpec
        Desired autoscaling policy.
    status : CephStorageAutoscalerStatus | None
        Observed controller status for this policy object.
    """

    model_config = ConfigDict(extra="forbid")
    apiVersion: str
    kind: Literal["CephStorageAutoscaler"]
    metadata: ObjectMeta
    spec: CephStorageAutoscalerSpec = Field(default_factory=CephStorageAutoscalerSpec)
    status: CephStorageAutoscalerStatus | None = None


class CephStorageActionSpec(BaseModel):
    """Desired node-side execution contract for one growth action.

    Attributes
    ----------
    repo_generation : int
        Controller generation marker copied from autoscaler at enqueue time.
    node_name : str
        Target Kubernetes node name where this action should run.
    loop_spec : str
        MicroCeph loop specification passed to `microceph disk add`.
    reason : str
        Human-readable enqueue reason from controller reconciliation.
    """

    model_config = ConfigDict(extra="forbid")
    repo_generation: Annotated[int, Field(ge=0)]
    node_name: KubeName
    loop_spec: LoopSpec
    reason: NonEmpty[Trimmed]


class CephStorageActionStatus(BaseModel):
    """Observed lifecycle state for one `CephStorageAction` object.

    Attributes
    ----------
    phase : Literal[\"Pending\", \"Running\", \"Succeeded\", \"Failed\"]
        Current action lifecycle phase.
    started_at : datetime | None
        Timestamp when a node agent claimed this action.
    finished_at : datetime | None
        Timestamp when action reached terminal phase.
    message : str
        Node-agent diagnostic message.
    worker_node : str
        Node name that processed this action.
    """

    model_config = ConfigDict(extra="forbid")
    phase: Literal["Pending", "Running", "Succeeded", "Failed"] = "Pending"
    started_at: datetime | None = None
    finished_at: datetime | None = None
    message: str = ""
    worker_node: str = ""


class CephStorageAction(BaseModel):
    """Validated payload for `CephStorageAction` custom resources.

    Attributes
    ----------
    apiVersion : str
        Kubernetes API version string for this custom resource.
    kind : Literal[\"CephStorageAction\"]
        Kubernetes kind identifier for this custom resource.
    metadata : ObjectMeta
        Kubernetes object metadata subset.
    spec : CephStorageActionSpec
        Desired node-side execution contract.
    status : CephStorageActionStatus | None
        Observed node-agent execution state.
    """

    model_config = ConfigDict(extra="forbid")
    apiVersion: str
    kind: Literal["CephStorageAction"]
    metadata: ObjectMeta
    spec: CephStorageActionSpec
    status: CephStorageActionStatus | None = None


class ActionList(BaseModel):
    """Validated subset of action list payload."""

    model_config = ConfigDict(extra="ignore")
    items: list[CephStorageAction] = Field(default_factory=list)
    metadata: ObjectMeta = Field(default_factory=lambda: ObjectMeta(
        name="",
        namespace="",
        generation=0,
        resourceVersion="",
        labels={},
    ))


def _now_iso() -> str:
    return datetime.now(UTC).isoformat()


def _remaining(deadline: float) -> float:
    remaining = deadline - asyncio.get_running_loop().time()
    if remaining <= 0:
        raise TimeoutError("timed out while converging Ceph autoscaler controlplane")
    return remaining


def _parse_loop_size_bytes(size: str) -> int:
    size = size.strip().upper()
    if len(size) < 2:
        raise ValueError(f"invalid loop size: {size!r}")
    unit = size[-1]
    number = int(size[:-1])
    if number <= 0:
        raise ValueError(f"invalid loop size: {size!r}")
    scale = {"M": 2**20, "G": 2**30, "T": 2**40}.get(unit)
    if scale is None:
        raise ValueError(f"invalid loop size unit in {size!r}; expected M/G/T")
    return number * scale


def _autoscaler_image_ref() -> str:
    # NOTE: deterministic image tags avoid unnecessary churn when init runs
    # repeatedly on the same code revision.
    this_file = Path(__file__).resolve()
    digest = hashlib.sha256(this_file.read_bytes()).hexdigest()[:12]
    return f"{AUTOSCALE_IMAGE_REPO}:v1-{digest}"


def _autoscaler_crd_manifest() -> dict:
    return {
        "apiVersion": "apiextensions.k8s.io/v1",
        "kind": "CustomResourceDefinition",
        "metadata": {"name": f"{AUTOSCALE_AUTOSCALER_PLURAL}.{AUTOSCALE_GROUP}"},
        "spec": {
            "group": AUTOSCALE_GROUP,
            "scope": "Namespaced",
            "names": {
                "plural": AUTOSCALE_AUTOSCALER_PLURAL,
                "singular": "cephstorageautoscaler",
                "kind": AUTOSCALE_AUTOSCALER_KIND,
                "shortNames": ["csa"],
            },
            "versions": [{
                "name": AUTOSCALE_VERSION,
                "served": True,
                "storage": True,
                "schema": {
                    "openAPIV3Schema": {
                        "type": "object",
                        "properties": {
                            "spec": {
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
                            "status": {
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
                                    "last_reconciled_at": {
                                        "type": "string",
                                        "format": "date-time",
                                    },
                                    "last_error": {"type": "string"},
                                },
                            },
                        },
                    },
                },
                "subresources": {"status": {}},
            }],
        },
    }


def _action_crd_manifest() -> dict:
    return {
        "apiVersion": "apiextensions.k8s.io/v1",
        "kind": "CustomResourceDefinition",
        "metadata": {"name": f"{AUTOSCALE_ACTION_PLURAL}.{AUTOSCALE_GROUP}"},
        "spec": {
            "group": AUTOSCALE_GROUP,
            "scope": "Namespaced",
            "names": {
                "plural": AUTOSCALE_ACTION_PLURAL,
                "singular": "cephstorageaction",
                "kind": AUTOSCALE_ACTION_KIND,
                "shortNames": ["csact"],
            },
            "versions": [{
                "name": AUTOSCALE_VERSION,
                "served": True,
                "storage": True,
                "schema": {
                    "openAPIV3Schema": {
                        "type": "object",
                        "properties": {
                            "spec": {
                                "type": "object",
                                "required": ["repo_generation", "node_name", "loop_spec", "reason"],
                                "properties": {
                                    "repo_generation": {"type": "integer", "minimum": 0},
                                    "node_name": {"type": "string", "minLength": 1},
                                    "loop_spec": {
                                        "type": "string",
                                        "pattern": AUTOSCALE_LOOP_SPEC_RE,
                                    },
                                    "reason": {"type": "string", "minLength": 1},
                                },
                            },
                            "status": {
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
                    },
                },
                "subresources": {"status": {}},
            }],
        },
    }


def _service_account_manifest() -> dict:
    return {
        "apiVersion": "v1",
        "kind": "ServiceAccount",
        "metadata": {
            "name": AUTOSCALE_SERVICE_ACCOUNT,
            "namespace": BERTRAND_NAMESPACE,
            "labels": {AUTOSCALE_LABEL: AUTOSCALE_LABEL_VALUE},
        },
    }


def _cluster_role_manifest() -> dict:
    return {
        "apiVersion": "rbac.authorization.k8s.io/v1",
        "kind": "ClusterRole",
        "metadata": {
            "name": AUTOSCALE_SERVICE_ACCOUNT,
            "labels": {AUTOSCALE_LABEL: AUTOSCALE_LABEL_VALUE},
        },
        "rules": [
            {
                "apiGroups": [AUTOSCALE_GROUP],
                "resources": [AUTOSCALE_AUTOSCALER_PLURAL, AUTOSCALE_ACTION_PLURAL],
                "verbs": ["get", "list", "watch", "create", "update", "patch"],
            },
            {
                "apiGroups": [AUTOSCALE_GROUP],
                "resources": [
                    f"{AUTOSCALE_AUTOSCALER_PLURAL}/status",
                    f"{AUTOSCALE_ACTION_PLURAL}/status",
                ],
                "verbs": ["get", "update", "patch"],
            },
            {
                "apiGroups": [""],
                "resources": ["nodes"],
                "verbs": ["get", "list", "watch", "patch"],
            },
        ],
    }


def _cluster_role_binding_manifest() -> dict:
    return {
        "apiVersion": "rbac.authorization.k8s.io/v1",
        "kind": "ClusterRoleBinding",
        "metadata": {
            "name": AUTOSCALE_SERVICE_ACCOUNT,
            "labels": {AUTOSCALE_LABEL: AUTOSCALE_LABEL_VALUE},
        },
        "roleRef": {
            "apiGroup": "rbac.authorization.k8s.io",
            "kind": "ClusterRole",
            "name": AUTOSCALE_SERVICE_ACCOUNT,
        },
        "subjects": [{
            "kind": "ServiceAccount",
            "name": AUTOSCALE_SERVICE_ACCOUNT,
            "namespace": BERTRAND_NAMESPACE,
        }],
    }


def _controller_manifest(image: str) -> dict:
    return {
        "apiVersion": "apps/v1",
        "kind": "Deployment",
        "metadata": {
            "name": AUTOSCALE_CONTROLLER_NAME,
            "namespace": BERTRAND_NAMESPACE,
            "labels": {AUTOSCALE_LABEL: AUTOSCALE_LABEL_VALUE},
        },
        "spec": {
            "replicas": 1,
            "selector": {"matchLabels": {"app": AUTOSCALE_CONTROLLER_NAME}},
            "template": {
                "metadata": {
                    "labels": {
                        "app": AUTOSCALE_CONTROLLER_NAME,
                        AUTOSCALE_LABEL: AUTOSCALE_LABEL_VALUE,
                    }
                },
                "spec": {
                    "serviceAccountName": AUTOSCALE_SERVICE_ACCOUNT,
                    "nodeSelector": {
                        CLUSTER_REGISTRY_READY_LABEL: CLUSTER_REGISTRY_READY_VALUE,
                    },
                    "hostPID": True,
                    "containers": [{
                        "name": "controller",
                        "image": image,
                        "imagePullPolicy": "Always",
                        "args": ["controller"],
                        "env": [{
                            "name": "NODE_NAME",
                            "valueFrom": {"fieldRef": {"fieldPath": "spec.nodeName"}},
                        }],
                        "securityContext": {
                            "privileged": True,
                            "runAsUser": 0,
                        },
                        "volumeMounts": [{
                            "name": "host-root",
                            "mountPath": "/host",
                        }],
                    }],
                    "volumes": [{
                        "name": "host-root",
                        "hostPath": {"path": "/", "type": "Directory"},
                    }],
                },
            },
        },
    }


def _agent_manifest(image: str) -> dict:
    return {
        "apiVersion": "apps/v1",
        "kind": "DaemonSet",
        "metadata": {
            "name": AUTOSCALE_AGENT_NAME,
            "namespace": BERTRAND_NAMESPACE,
            "labels": {AUTOSCALE_LABEL: AUTOSCALE_LABEL_VALUE},
        },
        "spec": {
            "selector": {"matchLabels": {"app": AUTOSCALE_AGENT_NAME}},
            "template": {
                "metadata": {
                    "labels": {
                        "app": AUTOSCALE_AGENT_NAME,
                        AUTOSCALE_LABEL: AUTOSCALE_LABEL_VALUE,
                    },
                },
                "spec": {
                    "serviceAccountName": AUTOSCALE_SERVICE_ACCOUNT,
                    "nodeSelector": {
                        CLUSTER_REGISTRY_READY_LABEL: CLUSTER_REGISTRY_READY_VALUE,
                    },
                    "hostPID": True,
                    "containers": [{
                        "name": "agent",
                        "image": image,
                        "imagePullPolicy": "Always",
                        "args": ["agent"],
                        "env": [{
                            "name": "NODE_NAME",
                            "valueFrom": {"fieldRef": {"fieldPath": "spec.nodeName"}},
                        }],
                        "securityContext": {
                            "privileged": True,
                            "runAsUser": 0,
                        },
                        "volumeMounts": [{
                            "name": "host-root",
                            "mountPath": "/host",
                        }],
                    }],
                    "volumes": [{
                        "name": "host-root",
                        "hostPath": {"path": "/", "type": "Directory"},
                    }],
                },
            },
        },
    }


def _default_autoscaler_manifest() -> dict:
    return {
        "apiVersion": f"{AUTOSCALE_GROUP}/{AUTOSCALE_VERSION}",
        "kind": AUTOSCALE_AUTOSCALER_KIND,
        "metadata": {
            "name": AUTOSCALE_DEFAULT_NAME,
            "namespace": BERTRAND_NAMESPACE,
            "labels": {AUTOSCALE_LABEL: AUTOSCALE_LABEL_VALUE},
        },
        "spec": CephStorageAutoscalerSpec().model_dump(mode="json"),
    }


async def _apply_manifest(manifest: dict, *, timeout: float) -> None:
    await kubectl(
        ["apply", "-f", "-"],
        input=json.dumps(manifest, separators=(",", ":")),
        timeout=timeout,
    )


def _autoscale_dockerfile() -> str:
    return (
        "FROM python:3.12-slim\n"
        "WORKDIR /opt/bertrand\n"
        "ENV PYTHONUNBUFFERED=1\n"
        "ENV PYTHONPATH=/opt/bertrand\n"
        "COPY bertrand /opt/bertrand/bertrand\n"
        "RUN python -m pip install --no-cache-dir 'pydantic>=2,<3'\n"
        "ENTRYPOINT ['python', '-m', 'bertrand.env.kube.ceph.autoscale']\n"
    ).replace("'", '"')


def ceph_capacity_controlplane_image_build() -> ClusterImageBuild:
    """Return the autoscaler controlplane image build contract.

    Returns
    -------
    ClusterImageBuild
        Declarative cluster image build request for Ceph autoscaler workloads.
    """
    repo_root = Path(__file__).resolve().parents[4]
    return ClusterImageBuild(
        image=_autoscaler_image_ref(),
        dockerfile=_autoscale_dockerfile(),
        context_copies=((repo_root / "bertrand", Path("bertrand")),),
        context_prefix=AUTOSCALE_IMAGE_CONTEXT_PREFIX,
        build_flags=("--progress=plain",),
        build_labels={BERTRAND_ENV: "1"},
    )


async def ensure_ceph_capacity_controlplane(*, image: str, timeout: float) -> None:
    """Converge Ceph autoscaler CRDs and workloads in the local cluster.

    Parameters
    ----------
    image : str
        Pre-published controlplane image reference to deploy.
    timeout : float
        Maximum runtime budget in seconds for this convergence pass.  If infinite,
        wait indefinitely.

    Returns
    -------
    None
        This function executes for side effects only.

    Raises
    ------
    OSError
        If convergence cannot complete safely.
    TimeoutError
        If timeout is exhausted.

    Notes
    -----
    This is intentionally a grow-only v1.  It deploys controller + node agents and a
    singleton autoscaler policy object, but does not implement shrink semantics.
    """

    if timeout <= 0:
        raise TimeoutError("timeout must be non-negative")
    image = image.strip()
    if not image:
        raise ValueError("controlplane image reference cannot be empty")
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout

    manifests = (
        _autoscaler_crd_manifest(),
        _action_crd_manifest(),
        _service_account_manifest(),
        _cluster_role_manifest(),
        _cluster_role_binding_manifest(),
        _controller_manifest(image),
        _agent_manifest(image),
        _default_autoscaler_manifest(),
    )
    for manifest in manifests:
        await _apply_manifest(manifest, timeout=_remaining(deadline))


async def _host_ceph_command(argv: list[str], *, timeout: float) -> subprocess.CompletedProcess[str]:
    if timeout <= 0:
        raise TimeoutError("timeout must be non-negative")

    # NOTE: controller/agent pods run inside containers that may not ship microceph
    # binaries.  We chroot into /host in those contexts to execute the host command.
    if Path("/host").is_dir() and not shutil.which(argv[0]):
        cmd = ["chroot", "/host", *argv]
    else:
        cmd = argv

    result = await run(
        cmd,
        check=False,
        capture_output=True,
        timeout=timeout,
    )
    return result


async def _ceph_capacity(*, timeout: float) -> CephCapacitySnapshot:
    result = await _host_ceph_command(["microceph.ceph", "df", "--format", "json"], timeout=timeout)
    if result.returncode != 0:
        raise OSError(f"failed to inspect ceph capacity:\n{result}")
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as err:
        raise OSError(f"ceph df returned malformed JSON: {err}") from err

    # NOTE: ceph JSON keys changed across releases; we accept modern fields first and
    # then legacy-compatible fallbacks.
    stats = payload.get("stats", {}) if isinstance(payload, dict) else {}
    total = int(
        stats.get("total_bytes")
        or stats.get("total_space")
        or 0
    )
    used = int(
        stats.get("total_used_bytes")
        or stats.get("total_used_raw_bytes")
        or stats.get("total_used")
        or 0
    )
    if total <= 0:
        raise OSError(f"ceph df reported invalid total capacity: {total}")
    if used < 0:
        raise OSError(f"ceph df reported invalid used capacity: {used}")
    return CephCapacitySnapshot(total_bytes=total, used_bytes=used, used_ratio=used / total)


async def _controller_read_autoscaler(api: Kube, *, timeout: float) -> CephStorageAutoscaler:
    payload = await api.run(
        lambda request_timeout: api.custom.get_namespaced_custom_object(
            group=AUTOSCALE_GROUP,
            version=AUTOSCALE_VERSION,
            namespace=api.namespace,
            plural=AUTOSCALE_AUTOSCALER_PLURAL,
            name=AUTOSCALE_DEFAULT_NAME,
            _request_timeout=request_timeout,
        ),
        timeout=timeout,
        context=(
            f"failed to read {AUTOSCALE_AUTOSCALER_KIND} {AUTOSCALE_DEFAULT_NAME!r} "
            f"in namespace {api.namespace!r}"
        ),
    )
    if payload is None:
        raise OSError(
            f"{AUTOSCALE_AUTOSCALER_KIND} {AUTOSCALE_DEFAULT_NAME!r} is missing in "
            f"namespace {api.namespace!r}"
        )
    try:
        return CephStorageAutoscaler.model_validate(payload)
    except ValidationError as err:
        raise OSError(f"malformed autoscaler payload: {err}") from err


async def _controller_list_actions(api: Kube, *, timeout: float) -> ActionList:
    payload = await api.run(
        lambda request_timeout: api.custom.list_namespaced_custom_object(
            group=AUTOSCALE_GROUP,
            version=AUTOSCALE_VERSION,
            namespace=api.namespace,
            plural=AUTOSCALE_ACTION_PLURAL,
            _request_timeout=request_timeout,
        ),
        timeout=timeout,
        context=(
            f"failed to list {AUTOSCALE_ACTION_KIND} objects in namespace "
            f"{api.namespace!r}"
        ),
    )
    try:
        return ActionList.model_validate(payload)
    except ValidationError as err:
        raise OSError(f"malformed action list payload: {err}") from err


async def _controller_list_nodes(api: Kube, *, timeout: float) -> list[Node]:
    return await Node.list(kube=api, timeout=timeout)


def _eligible_nodes(nodes: list[Node]) -> list[str]:
    return sorted(
        node.name
        for node in nodes
        if node.name and
        node.labels.get(CLUSTER_REGISTRY_READY_LABEL) == CLUSTER_REGISTRY_READY_VALUE
    )


def _plan_actions(
    *,
    autoscaler: CephStorageAutoscaler,
    capacity: CephCapacitySnapshot,
    nodes: list[str],
    state: ControllerState,
) -> list[PlannedAction]:
    spec = autoscaler.spec
    if not spec.enabled:
        return []
    if not nodes:
        return []
    if capacity.used_ratio < spec.high_watermark:
        return []

    loop_bytes = _parse_loop_size_bytes(spec.loop_size)
    if loop_bytes <= 0:
        return []

    target_used = spec.target_watermark * capacity.total_bytes
    deficit = capacity.used_bytes - target_used
    if deficit <= 0:
        return []

    desired = math.ceil(deficit / loop_bytes)
    count = max(0, min(desired, spec.max_actions_per_reconcile))
    if count == 0:
        return []

    # NOTE: deterministic round-robin distribution avoids action hotspots while still
    # keeping scheduling deterministic for debugging.
    planned: list[PlannedAction] = []
    for index in range(count):
        node = nodes[(state.round_robin_offset + index) % len(nodes)]
        planned.append(PlannedAction(
            node_name=node,
            loop_spec=f"loop,{spec.loop_size},1",
            reason=(
                "cluster usage "
                f"{capacity.used_ratio:.2%} >= high watermark {spec.high_watermark:.2%}"
            ),
        ))
    state.round_robin_offset = (state.round_robin_offset + count) % len(nodes)
    return planned


async def _controller_create_actions(
    api: Kube,
    *,
    autoscaler: CephStorageAutoscaler,
    actions: list[PlannedAction],
    timeout: float,
) -> None:
    # NOTE: controller enqueues intent objects instead of mutating disks directly so
    # host-level mutation always runs on the target node with local failure reporting.
    for action in actions:
        manifest = {
            "apiVersion": f"{AUTOSCALE_GROUP}/{AUTOSCALE_VERSION}",
            "kind": AUTOSCALE_ACTION_KIND,
            "metadata": {
                "name": f"{AUTOSCALE_DEFAULT_NAME}-{uuid.uuid4().hex[:12]}",
                "namespace": api.namespace,
                "labels": {AUTOSCALE_LABEL: AUTOSCALE_LABEL_VALUE},
            },
            "spec": {
                "repo_generation": autoscaler.metadata.generation,
                "node_name": action.node_name,
                "loop_spec": action.loop_spec,
                "reason": action.reason,
            },
            "status": {"phase": "Pending"},
        }
        await api.run(
            lambda request_timeout, manifest=manifest: api.custom.create_namespaced_custom_object(
                group=AUTOSCALE_GROUP,
                version=AUTOSCALE_VERSION,
                namespace=api.namespace,
                plural=AUTOSCALE_ACTION_PLURAL,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=(
                f"failed to create {AUTOSCALE_ACTION_KIND} in namespace "
                f"{api.namespace!r}"
            ),
        )


async def _controller_patch_status(
    api: Kube,
    *,
    autoscaler: CephStorageAutoscaler,
    capacity: CephCapacitySnapshot | None,
    counts: dict[str, int],
    error: str,
    timeout: float,
) -> None:
    status = {
        "status": {
            "observedGeneration": autoscaler.metadata.generation,
            "total_bytes": capacity.total_bytes if capacity else None,
            "used_bytes": capacity.used_bytes if capacity else None,
            "used_ratio": capacity.used_ratio if capacity else None,
            "pending_actions": counts.get("Pending", 0),
            "running_actions": counts.get("Running", 0),
            "succeeded_actions": counts.get("Succeeded", 0),
            "failed_actions": counts.get("Failed", 0),
            "last_reconciled_at": _now_iso(),
            "last_error": error,
        }
    }
    await api.run(
        lambda request_timeout: api.custom.patch_namespaced_custom_object_status(
            group=AUTOSCALE_GROUP,
            version=AUTOSCALE_VERSION,
            namespace=api.namespace,
            plural=AUTOSCALE_AUTOSCALER_PLURAL,
            name=AUTOSCALE_DEFAULT_NAME,
            body=status,
            _request_timeout=request_timeout,
        ),
        timeout=timeout,
        context=(
            f"failed to patch status for {AUTOSCALE_AUTOSCALER_KIND} "
            f"{AUTOSCALE_DEFAULT_NAME!r}"
        ),
    )


async def _controller_watch_actions(
    api: Kube,
    *,
    state: ControllerState,
    timeout: float,
) -> None:
    if timeout <= 0:
        return
    timeout_seconds = max(1, int(math.ceil(timeout)))

    def _watch() -> str:
        watcher = kube_watch.Watch()
        latest_rv = state.actions_resource_version
        kwargs: dict[str, object] = {
            "group": AUTOSCALE_GROUP,
            "version": AUTOSCALE_VERSION,
            "namespace": api.namespace,
            "plural": AUTOSCALE_ACTION_PLURAL,
            "timeout_seconds": timeout_seconds,
            "_request_timeout": None if math.isinf(timeout) else timeout,
        }
        if state.actions_resource_version:
            kwargs["resource_version"] = state.actions_resource_version

        try:
            for event in watcher.stream(
                api.custom.list_namespaced_custom_object,
                **kwargs,
            ):
                if not isinstance(event, dict):
                    continue
                obj = event.get("object")
                if not isinstance(obj, dict):
                    continue
                metadata = obj.get("metadata")
                if not isinstance(metadata, dict):
                    continue
                rv = str(metadata.get("resourceVersion", "")).strip()
                if rv:
                    latest_rv = rv
            return latest_rv
        finally:
            watcher.stop()

    try:
        latest_rv = await asyncio.wait_for(
            asyncio.to_thread(_watch),
            timeout=None if math.isinf(timeout) else timeout,
        )
    except asyncio.TimeoutError:
        return
    except ApiException as err:
        if err.status == 410:
            # NOTE: watch streams can expire between cycles; reset and relist.
            state.actions_resource_version = ""
        return
    except OSError:
        return
    except Exception:
        return

    if latest_rv:
        state.actions_resource_version = latest_rv


async def run_ceph_capacity_controller(*, timeout: float = INFINITY) -> None:
    """Run controller reconciliation loop for Ceph autoscaling actions.

    Parameters
    ----------
    timeout : float, optional
        Maximum runtime for this loop.  Defaults to infinity.

    Returns
    -------
    None
        This function runs until cancelled or timeout is reached.

    Raises
    ------
    OSError
        If malformed managed state or unrecoverable API errors are encountered.
    """

    if timeout <= 0:
        raise TimeoutError("controller timeout must be non-negative")
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    state = ControllerState()
    with Kube.inside_cluster() as api:
        # NOTE: watch+tick hybrid ensures bounded convergence latency while still reacting
        # quickly to action updates.
        while True:
            interval = 30.0
            try:
                autoscaler = await _controller_read_autoscaler(api, timeout=_remaining(deadline))
                interval = float(autoscaler.spec.reconcile_interval_seconds)
                actions = await _controller_list_actions(api, timeout=_remaining(deadline))
                nodes = await _controller_list_nodes(api, timeout=_remaining(deadline))
                capacity = await _ceph_capacity(timeout=_remaining(deadline))

                counts = {phase: 0 for phase in AUTOSCALE_PHASES}
                for action in actions.items:
                    phase = (action.status.phase if action.status is not None else "Pending")
                    if phase in counts:
                        counts[phase] += 1
                    rv = action.metadata.resourceVersion.strip()
                    if rv:
                        state.actions_resource_version = rv

                planned = _plan_actions(
                    autoscaler=autoscaler,
                    capacity=capacity,
                    nodes=_eligible_nodes(nodes),
                    state=state,
                )
                if planned:
                    await _controller_create_actions(
                        api,
                        autoscaler=autoscaler,
                        actions=planned,
                        timeout=_remaining(deadline),
                    )
                    counts["Pending"] += len(planned)

                await _controller_patch_status(
                    api,
                    autoscaler=autoscaler,
                    capacity=capacity,
                    counts=counts,
                    error="",
                    timeout=_remaining(deadline),
                )
            except asyncio.CancelledError:
                raise
            except Exception as err:  # pylint: disable=broad-exception-caught
                # fail-closed status updates make malformed managed state visible to
                # operators instead of silently masking reconciliation drift.
                try:
                    autoscaler = await _controller_read_autoscaler(api, timeout=_remaining(deadline))
                    await _controller_patch_status(
                        api,
                        autoscaler=autoscaler,
                        capacity=None,
                        counts={phase: 0 for phase in AUTOSCALE_PHASES},
                        error=str(err),
                        timeout=_remaining(deadline),
                    )
                except Exception:
                    pass

            await _controller_watch_actions(
                api,
                state=state,
                timeout=min(interval, _remaining(deadline)),
            )


async def _agent_list_actions(api: Kube, *, timeout: float) -> list[CephStorageAction]:
    actions = await _controller_list_actions(api, timeout=timeout)
    node = os.environ.get("NODE_NAME", "").strip() or platform.node().strip()

    pending: list[CephStorageAction] = []
    for action in actions.items:
        phase = action.status.phase if action.status is not None else "Pending"
        if action.spec.node_name != node:
            continue
        if phase != "Pending":
            continue
        pending.append(action)
    pending.sort(key=lambda item: item.metadata.name)
    return pending


async def _agent_patch_action_status(
    api: Kube,
    *,
    action: CephStorageAction,
    phase: Literal["Pending", "Running", "Succeeded", "Failed"],
    message: str,
    timeout: float,
    started: bool = False,
    finished: bool = False,
) -> None:
    patch: dict[str, dict[str, str]] = {
        "status": {
            "phase": phase,
            "message": message,
            "worker_node": os.environ.get("NODE_NAME", "").strip() or platform.node().strip(),
        }
    }
    if started:
        patch["status"]["started_at"] = _now_iso()
    if finished:
        patch["status"]["finished_at"] = _now_iso()
    await api.run(
        lambda request_timeout: api.custom.patch_namespaced_custom_object_status(
            group=AUTOSCALE_GROUP,
            version=AUTOSCALE_VERSION,
            namespace=api.namespace,
            plural=AUTOSCALE_ACTION_PLURAL,
            name=action.metadata.name,
            body=patch,
            _request_timeout=request_timeout,
        ),
        timeout=timeout,
        context=(
            f"failed to patch status for {AUTOSCALE_ACTION_KIND} "
            f"{action.metadata.name!r}"
        ),
    )


async def _agent_execute(action: CephStorageAction, *, timeout: float) -> None:
    result = await _host_ceph_command(
        ["microceph", "disk", "add", action.spec.loop_spec],
        timeout=timeout,
    )
    if result.returncode != 0:
        raise OSError(f"microceph disk add failed for {action.spec.loop_spec}:\n{result}")


async def run_ceph_capacity_agent(*, timeout: float = INFINITY) -> None:
    """Run node agent loop for executing queued `CephStorageAction` resources.

    Parameters
    ----------
    timeout : float, optional
        Maximum runtime for this loop.  Defaults to infinity.

    Returns
    -------
    None
        This function runs until cancelled or timeout is reached.
    """

    if timeout <= 0:
        raise TimeoutError("agent timeout must be non-negative")
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    with Kube.inside_cluster() as api:
        while True:
            pending = await _agent_list_actions(api, timeout=_remaining(deadline))
            for action in pending:
                try:
                    await _agent_patch_action_status(
                        api,
                        action=action,
                        phase="Running",
                        message="action claimed by node agent",
                        timeout=_remaining(deadline),
                        started=True,
                    )
                    await _agent_execute(action, timeout=_remaining(deadline))
                    await _agent_patch_action_status(
                        api,
                        action=action,
                        phase="Succeeded",
                        message="microceph disk add completed",
                        timeout=_remaining(deadline),
                        finished=True,
                    )
                except asyncio.CancelledError:
                    raise
                except Exception as err:  # pylint: disable=broad-exception-caught
                    await _agent_patch_action_status(
                        api,
                        action=action,
                        phase="Failed",
                        message=str(err),
                        timeout=_remaining(deadline),
                        finished=True,
                    )

            await asyncio.sleep(min(5.0, _remaining(deadline)))


def main(argv: list[str] | None = None) -> int:
    """Entry point for controlplane container role dispatch.

    Parameters
    ----------
    argv : list[str] | None, optional
        Optional CLI arguments.  Defaults to `sys.argv[1:]`.

    Returns
    -------
    int
        POSIX-style exit status code.
    """

    if argv is None:
        argv = sys.argv[1:]
    if not argv:
        role = "controller"
    else:
        role = argv[0].strip().lower()

    if role not in {"controller", "agent"}:
        print(
            "usage: python -m bertrand.env.kube.ceph.autoscale [controller|agent]",
            file=sys.stderr,
        )
        return 2

    # NOTE: split roles keep mutation authority explicit.  Controller enqueues intents;
    # agent performs host-level disk mutation for node-local execution.
    with asyncio.Runner() as runner:
        if role == "controller":
            runner.run(run_ceph_capacity_controller())
        else:
            runner.run(run_ceph_capacity_agent())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
