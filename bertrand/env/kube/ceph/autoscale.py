"""Ceph capacity autoscaler policy and runtime loops."""

from __future__ import annotations

import asyncio
import hashlib
import math
import re
import sys
from collections.abc import Collection, Mapping
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Any, Literal, cast

from ...run import BERTRAND_ENV, INFINITY
from ..api import Kube
from ..build import IMAGES, BuildKitImageBuild
from .autoscale_kube import (
    AUTOSCALE_ACTION_KIND,
    AUTOSCALE_DEFAULT_NAME,
    AUTOSCALE_LOOP_SIZE_RE,
    AUTOSCALE_LOOP_SPEC_RE,
    AUTOSCALE_PHASES,
    DEFAULT_AUTOSCALER_SPEC,
    RESOURCES,
)
from .microceph import CephCapacitySnapshot, add_loop_osd, ceph_df, host_free_bytes

AUTOSCALE_IMAGE_CONTEXT_PREFIX = "bertrand-ceph-autoscaler"
AUTOSCALE_NODE_REPORT_MAX_AGE_SECONDS = 120

type ActionPhase = Literal["Pending", "Running", "Succeeded", "Failed"]


@dataclass
class ControllerState:
    """Mutable controller loop state for deterministic action distribution."""

    round_robin_offset: int = 0


@dataclass(frozen=True)
class AutoscalerPolicy:
    """Autoscaler policy parsed from the singleton custom resource.

    Parameters
    ----------
    name : str
        Kubernetes object name.
    generation : int
        Kubernetes metadata generation for status observation.
    enabled : bool
        Whether controller planning should create new growth actions.
    high_watermark : float
        Used-capacity ratio that triggers growth.
    target_watermark : float
        Used-capacity ratio the controller tries to move toward.
    loop_size : str
        MicroCeph loop OSD size such as `"4G"`.
    max_actions_per_reconcile : int
        Maximum new actions allowed per reconcile pass, after in-flight actions.
    reconcile_interval_seconds : int
        Controller loop sleep interval.
    """

    name: str
    generation: int
    enabled: bool
    high_watermark: float
    target_watermark: float
    loop_size: str
    max_actions_per_reconcile: int
    reconcile_interval_seconds: int


@dataclass(frozen=True)
class StorageActionStatus:
    """Observed lifecycle state for one node-scoped autoscaler action."""

    phase: ActionPhase = "Pending"
    started_at: datetime | None = None
    finished_at: datetime | None = None
    message: str = ""
    worker_node: str = ""


@dataclass(frozen=True)
class StorageAction:
    """Node-scoped MicroCeph growth action parsed from a custom resource."""

    name: str
    policy_generation: int
    node_name: str
    loop_spec: str
    reason: str
    status: StorageActionStatus


@dataclass(frozen=True)
class StorageNodeReport:
    """Latest host-local capacity report emitted by one node agent."""

    name: str
    node_name: str
    free_bytes: int
    path: str
    heartbeat_at: datetime | None
    last_error: str = ""


@dataclass(frozen=True)
class PlannedAction:
    """One node-scoped MicroCeph growth action selected by policy planning."""

    node_name: str
    loop_spec: str
    reason: str

    def spec(self) -> dict[str, object]:
        """Render this planned action as custom-resource spec fields."""
        return {
            "node_name": self.node_name,
            "loop_spec": self.loop_spec,
            "reason": self.reason,
        }


def _now() -> datetime:
    return datetime.now(UTC)


def _now_iso() -> str:
    return _now().isoformat()


def _remaining(deadline: float) -> float:
    remaining = deadline - asyncio.get_running_loop().time()
    if remaining <= 0:
        raise TimeoutError("timed out while reconciling Ceph autoscaler")
    return remaining


def _as_mapping(value: object, context: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise OSError(f"malformed {context}: expected object")
    return cast("Mapping[str, Any]", value)


def _object_section(obj: Mapping[str, Any], key: str) -> Mapping[str, Any]:
    value = obj.get(key, {})
    if value is None:
        return {}
    return _as_mapping(value, key)


def _string(value: object, *, default: str = "") -> str:
    if value is None:
        return default
    return str(value).strip()


def _integer(value: object, *, default: int = 0, minimum: int | None = None) -> int:
    if value is None:
        out = default
    elif isinstance(value, bool):
        raise OSError(f"expected integer, got {value!r}")
    elif isinstance(value, int):
        out = value
    elif isinstance(value, str):
        out = int(value)
    else:
        out = int(cast("Any", value))
    if minimum is not None and out < minimum:
        raise OSError(f"expected integer >= {minimum}, got {out}")
    return out


def _number(value: object, *, default: float) -> float:
    if value is None:
        return default
    if isinstance(value, bool):
        raise OSError(f"expected number, got {value!r}")
    if isinstance(value, int | float | str):
        return float(value)
    return float(cast("Any", value))


def _boolean(value: object, *, default: bool) -> bool:
    if value is None:
        return default
    if not isinstance(value, bool):
        raise OSError(f"expected boolean, got {value!r}")
    return value


def _timestamp(value: object) -> datetime | None:
    text = _string(value)
    if not text:
        return None
    parsed = datetime.fromisoformat(text.replace("Z", "+00:00"))
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=UTC)
    return parsed.astimezone(UTC)


def _metadata(obj: Mapping[str, Any]) -> Mapping[str, Any]:
    return _object_section(obj, "metadata")


def _spec(obj: Mapping[str, Any]) -> Mapping[str, Any]:
    return _object_section(obj, "spec")


def _status(obj: Mapping[str, Any]) -> Mapping[str, Any]:
    return _object_section(obj, "status")


def _parse_policy(obj: Mapping[str, Any]) -> AutoscalerPolicy:
    metadata = _metadata(obj)
    spec = dict(DEFAULT_AUTOSCALER_SPEC)
    spec.update(_spec(obj))
    name = _string(metadata.get("name")) or AUTOSCALE_DEFAULT_NAME
    loop_size = _string(spec.get("loop_size"), default="4G").upper()
    if not re.fullmatch(AUTOSCALE_LOOP_SIZE_RE, loop_size):
        raise OSError(f"invalid autoscaler loop_size: {loop_size!r}")
    high_watermark = _number(spec.get("high_watermark"), default=0.75)
    target_watermark = _number(spec.get("target_watermark"), default=0.65)
    if not 0.0 < high_watermark < 1.0:
        raise OSError(f"invalid autoscaler high_watermark: {high_watermark!r}")
    if not 0.0 < target_watermark < 1.0:
        raise OSError(f"invalid autoscaler target_watermark: {target_watermark!r}")
    return AutoscalerPolicy(
        name=name,
        generation=_integer(metadata.get("generation"), minimum=0),
        enabled=_boolean(spec.get("enabled"), default=True),
        high_watermark=high_watermark,
        target_watermark=target_watermark,
        loop_size=loop_size,
        max_actions_per_reconcile=_integer(
            spec.get("max_actions_per_reconcile"),
            default=3,
            minimum=1,
        ),
        reconcile_interval_seconds=_integer(
            spec.get("reconcile_interval_seconds"),
            default=30,
            minimum=1,
        ),
    )


def _parse_action_status(obj: Mapping[str, Any]) -> StorageActionStatus:
    status = _status(obj)
    phase = _string(status.get("phase"), default="Pending")
    if phase not in AUTOSCALE_PHASES:
        raise OSError(f"invalid {AUTOSCALE_ACTION_KIND} phase: {phase!r}")
    return StorageActionStatus(
        phase=cast("ActionPhase", phase),
        started_at=_timestamp(status.get("started_at")),
        finished_at=_timestamp(status.get("finished_at")),
        message=_string(status.get("message")),
        worker_node=_string(status.get("worker_node")),
    )


def _parse_action(obj: Mapping[str, Any]) -> StorageAction:
    metadata = _metadata(obj)
    spec = _spec(obj)
    name = _string(metadata.get("name"))
    if not name:
        raise OSError("malformed autoscaler action: missing metadata.name")
    loop_spec = _string(spec.get("loop_spec"))
    if not re.fullmatch(AUTOSCALE_LOOP_SPEC_RE, loop_spec):
        raise OSError(f"invalid autoscaler action loop_spec: {loop_spec!r}")
    node_name = _string(spec.get("node_name"))
    reason = _string(spec.get("reason"))
    if not node_name:
        raise OSError("malformed autoscaler action: missing spec.node_name")
    if not reason:
        raise OSError("malformed autoscaler action: missing spec.reason")
    return StorageAction(
        name=name,
        policy_generation=_integer(
            spec.get("policy_generation", spec.get("repo_generation")),
            minimum=0,
        ),
        node_name=node_name,
        loop_spec=loop_spec,
        reason=reason,
        status=_parse_action_status(obj),
    )


def _parse_node_report(obj: Mapping[str, Any]) -> StorageNodeReport:
    metadata = _metadata(obj)
    spec = _spec(obj)
    status = _status(obj)
    node_name = _string(spec.get("node_name"))
    if not node_name:
        raise OSError("malformed autoscaler node report: missing spec.node_name")
    return StorageNodeReport(
        name=_string(metadata.get("name")) or node_name,
        node_name=node_name,
        free_bytes=_integer(status.get("free_bytes"), minimum=0),
        path=_string(status.get("path")),
        heartbeat_at=_timestamp(status.get("heartbeat_at")),
        last_error=_string(status.get("last_error")),
    )


def _parse_loop_size_bytes(size: str) -> int:
    size = size.strip().upper()
    if len(size) < 2:
        raise ValueError(f"invalid loop size: {size!r}")
    number = int(size[:-1])
    unit = size[-1]
    scale = {"M": 2**20, "G": 2**30, "T": 2**40}.get(unit)
    if number <= 0 or scale is None:
        raise ValueError(f"invalid loop size: {size!r}")
    return number * scale


def _autoscaler_image_ref() -> str:
    h = hashlib.sha256()
    for path in (
        Path(__file__).resolve(),
        Path(__file__).with_name("autoscale_kube.py").resolve(),
        Path(__file__).with_name("microceph.py").resolve(),
    ):
        payload = path.read_bytes()
        h.update(len(payload).to_bytes(8, "big"))
        h.update(payload)
    return IMAGES.ref("ceph-autoscaler", f"v1-{h.hexdigest()[:12]}")


def _autoscale_dockerfile() -> str:
    return (
        "FROM python:3.12-slim\n"
        "WORKDIR /opt/bertrand\n"
        "ENV PYTHONUNBUFFERED=1\n"
        "ENV PYTHONPATH=/opt/bertrand\n"
        "COPY bertrand /opt/bertrand/bertrand\n"
        "RUN python -m pip install --no-cache-dir 'kubernetes>=32,<35'\n"
        "ENTRYPOINT ['python', '-m', 'bertrand.env.kube.ceph.autoscale']\n"
    ).replace("'", '"')


def ceph_capacity_controlplane_image_build() -> BuildKitImageBuild:
    """Return the autoscaler controlplane image build contract."""
    repo_root = Path(__file__).resolve().parents[4]
    return BuildKitImageBuild(
        image=_autoscaler_image_ref(),
        dockerfile=_autoscale_dockerfile(),
        context_copies=((repo_root / "bertrand", Path("bertrand")),),
        context_prefix=AUTOSCALE_IMAGE_CONTEXT_PREFIX,
        build_labels={BERTRAND_ENV: "1"},
    )


async def ensure_ceph_capacity_controlplane(*, image: str, timeout: float) -> None:
    """Converge Ceph autoscaler CRDs, RBAC, and workloads in the local cluster."""
    with await Kube.host(timeout=timeout) as kube:
        await RESOURCES.ensure(kube, image=image, timeout=timeout)


def _action_counts(actions: Collection[StorageAction]) -> dict[str, int]:
    counts = {phase: 0 for phase in AUTOSCALE_PHASES}
    for action in actions:
        counts[action.status.phase] += 1
    return counts


def _eligible_nodes(
    *,
    ready_nodes: Collection[str],
    reports: Collection[StorageNodeReport],
    loop_bytes: int,
) -> list[str]:
    ready = frozenset(ready_nodes)
    now = _now()
    eligible: list[str] = []
    for report in reports:
        if report.node_name not in ready or report.heartbeat_at is None:
            continue
        if (now - report.heartbeat_at).total_seconds() > AUTOSCALE_NODE_REPORT_MAX_AGE_SECONDS:
            continue
        slots = report.free_bytes // loop_bytes
        eligible.extend([report.node_name] * min(slots, 32))
    return sorted(eligible)


def _plan_actions(
    *,
    policy: AutoscalerPolicy,
    capacity: CephCapacitySnapshot,
    actions: Collection[StorageAction],
    eligible_nodes: list[str],
    state: ControllerState,
) -> list[PlannedAction]:
    if not policy.enabled or not eligible_nodes or capacity.used_ratio < policy.high_watermark:
        return []
    loop_bytes = _parse_loop_size_bytes(policy.loop_size)
    target_used = policy.target_watermark * capacity.total_bytes
    deficit = capacity.used_bytes - target_used
    if deficit <= 0:
        return []

    counts = _action_counts(actions)
    in_flight = counts["Pending"] + counts["Running"]
    budget = policy.max_actions_per_reconcile - in_flight
    if budget <= 0:
        return []

    desired = math.ceil(deficit / loop_bytes)
    count = max(0, min(desired, budget, len(eligible_nodes)))
    planned: list[PlannedAction] = []
    for index in range(count):
        node = eligible_nodes[(state.round_robin_offset + index) % len(eligible_nodes)]
        planned.append(
            PlannedAction(
                node_name=node,
                loop_spec=f"loop,{policy.loop_size},1",
                reason=(
                    "cluster usage "
                    f"{capacity.used_ratio:.2%} >= high watermark {policy.high_watermark:.2%}"
                ),
            )
        )
    if eligible_nodes:
        state.round_robin_offset = (state.round_robin_offset + count) % len(eligible_nodes)
    return planned


async def _patch_policy_status(
    kube: Kube,
    *,
    policy: AutoscalerPolicy,
    capacity: CephCapacitySnapshot | None,
    counts: Mapping[str, int],
    error: str,
    timeout: float,
) -> None:
    await RESOURCES.patch_policy_status(
        kube,
        status={
            "observedGeneration": policy.generation,
            "total_bytes": capacity.total_bytes if capacity is not None else None,
            "used_bytes": capacity.used_bytes if capacity is not None else None,
            "used_ratio": capacity.used_ratio if capacity is not None else None,
            "pending_actions": counts.get("Pending", 0),
            "running_actions": counts.get("Running", 0),
            "succeeded_actions": counts.get("Succeeded", 0),
            "failed_actions": counts.get("Failed", 0),
            "last_reconciled_at": _now_iso(),
            "last_error": error,
        },
        timeout=timeout,
    )


async def run_ceph_capacity_controller(*, timeout: float = INFINITY) -> None:
    """Run controller reconciliation loop for Ceph autoscaling actions."""
    if timeout <= 0:
        raise TimeoutError("controller timeout must be non-negative")
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    state = ControllerState()
    with Kube.inside_cluster() as kube:
        while True:
            interval = 30.0
            try:
                policy = _parse_policy(
                    await RESOURCES.get_policy(kube, timeout=_remaining(deadline))
                )
                interval = float(policy.reconcile_interval_seconds)
                actions = [
                    _parse_action(obj)
                    for obj in await RESOURCES.list_actions(kube, timeout=_remaining(deadline))
                ]
                reports = [
                    _parse_node_report(obj)
                    for obj in await RESOURCES.list_node_reports(kube, timeout=_remaining(deadline))
                ]
                ready_nodes = await RESOURCES.list_ready_nodes(kube, timeout=_remaining(deadline))
                capacity = await ceph_df(timeout=_remaining(deadline))
                loop_bytes = _parse_loop_size_bytes(policy.loop_size)
                planned = _plan_actions(
                    policy=policy,
                    capacity=capacity,
                    actions=actions,
                    eligible_nodes=_eligible_nodes(
                        ready_nodes=ready_nodes,
                        reports=reports,
                        loop_bytes=loop_bytes,
                    ),
                    state=state,
                )
                if planned:
                    await RESOURCES.create_actions(
                        kube,
                        policy_generation=policy.generation,
                        actions=[action.spec() for action in planned],
                        timeout=_remaining(deadline),
                    )
                    actions = [
                        _parse_action(obj)
                        for obj in await RESOURCES.list_actions(kube, timeout=_remaining(deadline))
                    ]
                await _patch_policy_status(
                    kube,
                    policy=policy,
                    capacity=capacity,
                    counts=_action_counts(actions),
                    error="",
                    timeout=_remaining(deadline),
                )
            except asyncio.CancelledError:
                raise
            except Exception as err:
                try:
                    policy = _parse_policy(
                        await RESOURCES.get_policy(kube, timeout=_remaining(deadline))
                    )
                    await _patch_policy_status(
                        kube,
                        policy=policy,
                        capacity=None,
                        counts={phase: 0 for phase in AUTOSCALE_PHASES},
                        error=str(err),
                        timeout=_remaining(deadline),
                    )
                except Exception:
                    pass
            await asyncio.sleep(min(interval, _remaining(deadline)))


def _worker_node_name() -> str:
    return sys.argv[2].strip() if len(sys.argv) > 2 else ""


def _node_name() -> str:
    import os
    import platform

    return os.environ.get("NODE_NAME", "").strip() or _worker_node_name() or platform.node().strip()


def _node_report_status() -> dict[str, object]:
    try:
        snapshot = host_free_bytes()
        return {
            "free_bytes": snapshot.free_bytes,
            "path": snapshot.path.as_posix(),
            "heartbeat_at": _now_iso(),
            "last_error": "",
        }
    except Exception as err:
        return {
            "free_bytes": 0,
            "path": "",
            "heartbeat_at": _now_iso(),
            "last_error": str(err),
        }


async def _agent_pending_actions(
    kube: Kube,
    *,
    node_name: str,
    timeout: float,
) -> list[StorageAction]:
    actions = [_parse_action(obj) for obj in await RESOURCES.list_actions(kube, timeout=timeout)]
    pending = [
        action
        for action in actions
        if action.node_name == node_name and action.status.phase == "Pending"
    ]
    pending.sort(key=lambda action: action.name)
    return pending


async def _patch_action_status(
    kube: Kube,
    *,
    action: StorageAction,
    phase: ActionPhase,
    message: str,
    node_name: str,
    timeout: float,
    started: bool = False,
    finished: bool = False,
) -> None:
    status: dict[str, object] = {
        "phase": phase,
        "message": message,
        "worker_node": node_name,
    }
    if started:
        status["started_at"] = _now_iso()
    if finished:
        status["finished_at"] = _now_iso()
    await RESOURCES.patch_action_status(
        kube,
        name=action.name,
        status=status,
        timeout=timeout,
    )


async def run_ceph_capacity_agent(*, timeout: float = INFINITY) -> None:
    """Run node agent loop for node reports and queued growth actions."""
    if timeout <= 0:
        raise TimeoutError("agent timeout must be non-negative")
    node_name = _node_name()
    if not node_name:
        raise OSError("Ceph autoscaler agent could not resolve NODE_NAME")
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    with Kube.inside_cluster() as kube:
        while True:
            await RESOURCES.upsert_node_report(
                kube,
                node_name=node_name,
                status=_node_report_status(),
                timeout=_remaining(deadline),
            )
            for action in await _agent_pending_actions(
                kube,
                node_name=node_name,
                timeout=_remaining(deadline),
            ):
                try:
                    await _patch_action_status(
                        kube,
                        action=action,
                        phase="Running",
                        message="action claimed by node agent",
                        node_name=node_name,
                        timeout=_remaining(deadline),
                        started=True,
                    )
                    await add_loop_osd(action.loop_spec, timeout=_remaining(deadline))
                    await _patch_action_status(
                        kube,
                        action=action,
                        phase="Succeeded",
                        message="microceph disk add completed",
                        node_name=node_name,
                        timeout=_remaining(deadline),
                        finished=True,
                    )
                except asyncio.CancelledError:
                    raise
                except Exception as err:
                    await _patch_action_status(
                        kube,
                        action=action,
                        phase="Failed",
                        message=str(err),
                        node_name=node_name,
                        timeout=_remaining(deadline),
                        finished=True,
                    )
            await asyncio.sleep(min(5.0, _remaining(deadline)))


def main(argv: list[str] | None = None) -> int:
    """Entry point for controlplane container role dispatch."""
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
            runner.run(run_ceph_capacity_controller())
        else:
            runner.run(run_ceph_capacity_agent())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
