"""Native Kubernetes controller materialization for Bertrand workloads."""

from __future__ import annotations

import asyncio
import uuid
from dataclasses import dataclass
from typing import TYPE_CHECKING, Any, Literal, Protocol, cast

from bertrand.env.git import BERTRAND_NAMESPACE
from bertrand.env.kube.api.spec import DeploymentStrategySpec
from bertrand.env.kube.capability.device import upsert_resource_claim_templates
from bertrand.env.kube.cronjob import CronJob, CronJobConcurrencyPolicy
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.job import Job, JobCompletionMode
from bertrand.env.kube.network.workload import (
    delete_workload_http_routes,
    delete_workload_network_policy,
    delete_workload_service,
    ensure_workload_http_routes,
    ensure_workload_network_policy,
    ensure_workload_service,
    prepare_workload_http_routes,
    prune_workload_http_routes,
)
from bertrand.env.kube.pod import Pod
from bertrand.env.kube.workload.base import (
    WORKLOAD_ID_LABEL,
    WORKLOAD_LABEL,
    WORKLOAD_LABEL_VALUE,
    WorkloadIdentity,
    WorkloadPod,
)

if TYPE_CHECKING:
    from collections.abc import Sequence

    from bertrand.env.config.bertrand import WorkloadTopology
    from bertrand.env.kube.api.client import Kube

type StableWorkloadController = Deployment | CronJob
type WorkloadControllerKind = Literal["none", "job", "cronjob", "deployment"]
type WorkloadInput = WorkloadPod | WorkloadIdentity | None

_JOB_RUN_SUFFIX_CHARS = 8
_MAX_KUBE_NAME_CHARS = 63
_KILL_POD_POLL_SECONDS = 0.5
_KILL_POD_PROOF_SECONDS = 5.0
_SCHEDULE_CONCURRENCY: dict[str, CronJobConcurrencyPolicy] = {
    "allow": cast("CronJobConcurrencyPolicy", "Allow"),
    "forbid": cast("CronJobConcurrencyPolicy", "Forbid"),
    "replace": cast("CronJobConcurrencyPolicy", "Replace"),
}
_COMPLETION_MODE: dict[str, JobCompletionMode] = {
    "all": cast("JobCompletionMode", "NonIndexed"),
    "indexed": cast("JobCompletionMode", "Indexed"),
}


@dataclass(frozen=True)
class WorkloadKillResult:
    """Summary of a native workload kill operation.

    Parameters
    ----------
    workload : str
        Stable Kubernetes workload resource name.
    deployment_scaled : bool, optional
        Whether a managed Deployment was scaled to zero.
    cronjob_suspended : bool, optional
        Whether a managed CronJob was suspended.
    jobs_deleted : tuple[str, ...], optional
        Active managed Job names whose deletion was requested.
    pods_deleted : tuple[str, ...], optional
        Active managed Pod names whose deletion was requested directly.
    """

    workload: str
    deployment_scaled: bool = False
    cronjob_suspended: bool = False
    jobs_deleted: tuple[str, ...] = ()
    pods_deleted: tuple[str, ...] = ()

    @property
    def changed(self) -> bool:
        """Return whether any running workload state was changed.

        Returns
        -------
        bool
            `True` when a controller was disabled or running resources were
            deleted.
        """
        return (
            self.deployment_scaled
            or self.cronjob_suspended
            or bool(self.jobs_deleted)
            or bool(self.pods_deleted)
        )


class _ExecutionConfig(Protocol):
    restart: str
    retries: int
    timeout: int | None
    ttl: int | None
    parallelism: int
    completions: int | None
    completion: str


class _ScheduleHistoryConfig(Protocol):
    success: int | None
    failure: int | None


class _ScheduleConfig(Protocol):
    cron: str
    timezone: str | None
    concurrency: str
    start_deadline: int | None
    suspend: bool | None
    history: _ScheduleHistoryConfig


class _ScaleConfig(Protocol):
    replicas: int


class _RolloutConfig(Protocol):
    strategy: str
    max_surge: int | str | None
    max_unavailable: int | str | None
    min_ready: int | None
    timeout: int | None
    history: int | None
    paused: bool | None


class _NetworkConfig(Protocol):
    @property
    def policy(self) -> str: ...

    @property
    def routes(self) -> Sequence[object]: ...


class _WorkloadConfig(Protocol):
    @property
    def topology(self) -> WorkloadTopology: ...

    @property
    def execution(self) -> _ExecutionConfig | None: ...

    @property
    def schedule(self) -> _ScheduleConfig | None: ...

    @property
    def scale(self) -> _ScaleConfig | None: ...

    @property
    def rollout(self) -> _RolloutConfig | None: ...

    @property
    def network(self) -> _NetworkConfig: ...


async def ensure_workload_controller(
    kube: Kube,
    *,
    config: _WorkloadConfig | None,
    workload: WorkloadInput,
    timeout: float,
    primary_args: Sequence[str] | None = None,
    interactive: bool = False,
) -> StableWorkloadController | None:
    """Converge the stable Kubernetes controller for one workload topology.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    config : _WorkloadConfig | None
        Validated Bertrand workload config. Missing config selects no workload.
    workload : WorkloadPod | WorkloadIdentity | None
        Workload pod intent for Deployment and CronJob topologies, or a stable
        identity for no-workload cleanup. Passing ``None`` makes no-workload cleanup
        a no-op because there is no resource name to target.
    timeout : float
        Maximum convergence budget in seconds. If infinite, wait indefinitely.
    primary_args : Sequence[str] | None, optional
        Runtime arguments to append to the primary container command.
    interactive : bool, optional
        Whether the primary container should be rendered for stdin/TTY attachment.

    Returns
    -------
    Deployment | CronJob | None
        Converged stable controller, or ``None`` for Job/no-workload topology.

    Raises
    ------
    TimeoutError
        If convergence cannot start before `timeout` expires.
    """
    if timeout <= 0:
        msg = "workload controller convergence timeout must be positive"
        raise TimeoutError(msg)

    kind = _topology_kind(config)
    identity = _identity(workload)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout

    if kind == "deployment":
        pod = _require_workload_pod(workload, kind=kind)
        rollout = _rollout(config)
        route_plan = await prepare_workload_http_routes(
            kube,
            config=cast("Any", config),
            workload=pod,
            timeout=deadline - loop.time(),
        )
        await _ensure_claim_templates(
            kube,
            workload=pod,
            timeout=deadline - loop.time(),
        )
        await _delete_cronjob(
            kube, identity=pod.identity, timeout=deadline - loop.time()
        )
        await prune_workload_http_routes(
            kube,
            route_plan=route_plan,
            timeout=deadline - loop.time(),
        )
        await ensure_workload_network_policy(
            kube,
            config=cast("Any", config),
            workload=pod,
            timeout=deadline - loop.time(),
            route_plan=route_plan,
        )
        await ensure_workload_service(
            kube,
            workload=pod,
            timeout=deadline - loop.time(),
        )
        await ensure_workload_http_routes(
            kube,
            config=cast("Any", config),
            workload=pod,
            timeout=deadline - loop.time(),
            route_plan=route_plan,
        )
        return await Deployment.upsert(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=pod.name,
            labels=pod.labels,
            selector=pod.selector,
            pod_template=pod.pod_template(
                primary_args=primary_args,
                interactive=interactive,
                stdin_once=False,
            ),
            replicas=_replicas(config),
            strategy=_rollout_strategy(config),
            min_ready_seconds=rollout.min_ready if rollout is not None else None,
            progress_deadline_seconds=rollout.timeout if rollout is not None else None,
            revision_history_limit=rollout.history if rollout is not None else None,
            paused=rollout.paused if rollout is not None else None,
            timeout=deadline - loop.time(),
        )

    if kind == "cronjob":
        pod = _require_workload_pod(workload, kind=kind)
        schedule = _require_schedule(config)
        await _ensure_claim_templates(
            kube,
            workload=pod,
            timeout=deadline - loop.time(),
        )
        await delete_workload_http_routes(
            kube,
            identity=pod.identity,
            timeout=deadline - loop.time(),
        )
        await delete_workload_service(
            kube,
            identity=pod.identity,
            timeout=deadline - loop.time(),
        )
        await delete_workload_network_policy(
            kube,
            identity=pod.identity,
            timeout=deadline - loop.time(),
        )
        await _delete_deployment(
            kube,
            identity=pod.identity,
            timeout=deadline - loop.time(),
        )
        return await CronJob.upsert(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=pod.name,
            labels=pod.labels,
            pod_template=pod.pod_template(primary_args=primary_args),
            schedule=schedule.cron,
            backoff_limit=_backoff_limit(config),
            ttl_seconds_after_finished=_ttl_seconds_after_finished(config),
            active_deadline_seconds=_active_deadline_seconds(config),
            parallelism=_parallelism(config),
            completions=_completions(config),
            completion_mode=_completion_mode(config),
            concurrency_policy=_concurrency_policy(schedule),
            suspend=False if schedule.suspend is None else schedule.suspend,
            starting_deadline_seconds=schedule.start_deadline,
            successful_jobs_history_limit=schedule.history.success,
            failed_jobs_history_limit=schedule.history.failure,
            time_zone=schedule.timezone,
            timeout=deadline - loop.time(),
        )

    if kind == "job":
        if identity is not None:
            await _delete_stable_resources(
                kube,
                identity=identity,
                timeout=deadline - loop.time(),
            )
        return None

    if identity is not None:
        await _delete_stable_resources(
            kube,
            identity=identity,
            timeout=deadline - loop.time(),
        )
    return None


async def create_workload_job_run(
    kube: Kube,
    *,
    config: _WorkloadConfig,
    workload: WorkloadPod,
    timeout: float,
    primary_args: Sequence[str] | None = None,
    interactive: bool = False,
) -> Job:
    """Create one generated Kubernetes Job run for a Job-topology workload.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    config : _WorkloadConfig
        Validated Bertrand workload config whose topology must be ``"job"``.
    workload : WorkloadPod
        Workload pod intent to render into the generated Job.
    timeout : float
        Maximum creation budget in seconds. If infinite, wait indefinitely.
    primary_args : Sequence[str] | None, optional
        Runtime arguments to append to the primary container command.
    interactive : bool, optional
        Whether the primary container should be rendered for stdin/TTY attachment.

    Returns
    -------
    Job
        Created generated Job run.

    Raises
    ------
    TimeoutError
        If creation cannot start before `timeout` expires.
    ValueError
        If `config` does not select Job topology.
    """
    if timeout <= 0:
        msg = "workload Job run creation timeout must be positive"
        raise TimeoutError(msg)
    if _topology_kind(config) != "job":
        msg = "generated workload Job runs require Job topology"
        raise ValueError(msg)

    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    await _delete_stable_resources(
        kube,
        identity=workload.identity,
        timeout=deadline - loop.time(),
    )
    await _ensure_claim_templates(
        kube,
        workload=workload,
        timeout=deadline - loop.time(),
    )
    return await Job.create(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=_job_run_name(workload.identity),
        labels=workload.labels,
        pod_template=workload.pod_template(
            primary_args=primary_args,
            interactive=interactive,
            stdin_once=interactive,
        ),
        backoff_limit=_backoff_limit(config),
        ttl_seconds_after_finished=_ttl_seconds_after_finished(config),
        active_deadline_seconds=_active_deadline_seconds(config),
        parallelism=_parallelism(config),
        completions=_completions(config),
        completion_mode=_completion_mode(config),
        timeout=deadline - loop.time(),
    )


async def kill_workload(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    grace_period_seconds: int,
    timeout: float,
) -> WorkloadKillResult:
    """Stop active Kubernetes workload processes for one Bertrand identity.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    identity : WorkloadIdentity
        Stable workload identity to stop.
    grace_period_seconds : int
        Kubernetes pod termination grace period.
    timeout : float
        Maximum API-operation budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    WorkloadKillResult
        Summary of controller and runtime resources affected by the operation.

    Raises
    ------
    TimeoutError
        If Kubernetes API work cannot start before `timeout` expires or active
        pods remain after the grace/proof window.
    ValueError
        If `grace_period_seconds` is negative.
    """
    if timeout <= 0:
        msg = "workload kill timeout must be positive"
        raise TimeoutError(msg)
    if grace_period_seconds < 0:
        msg = "workload kill grace period cannot be negative"
        raise ValueError(msg)

    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    deployment_scaled = await _scale_deployment_to_zero(
        kube,
        identity=identity,
        timeout=deadline - loop.time(),
    )
    cronjob_suspended = await _suspend_cronjob(
        kube,
        identity=identity,
        timeout=deadline - loop.time(),
    )
    jobs = await _active_workload_jobs(
        kube,
        identity=identity,
        timeout=deadline - loop.time(),
    )
    for job in jobs:
        await job.delete(
            kube,
            timeout=deadline - loop.time(),
            propagation_policy="Foreground",
            grace_period_seconds=grace_period_seconds,
        )

    pods = await _active_workload_pods(
        kube,
        identity=identity,
        timeout=deadline - loop.time(),
    )
    for pod in pods:
        await pod.delete(
            kube,
            timeout=deadline - loop.time(),
            grace_period_seconds=grace_period_seconds,
        )

    await _wait_workload_pods_stopped(
        kube,
        identity=identity,
        timeout=min(
            deadline - loop.time(),
            grace_period_seconds + _KILL_POD_PROOF_SECONDS,
        ),
    )
    return WorkloadKillResult(
        workload=identity.name,
        deployment_scaled=deployment_scaled,
        cronjob_suspended=cronjob_suspended,
        jobs_deleted=tuple(job.name for job in jobs),
        pods_deleted=tuple(pod.name for pod in pods),
    )


async def _ensure_claim_templates(
    kube: Kube,
    *,
    workload: WorkloadPod,
    timeout: float,
) -> None:
    if not workload.resource_claim_templates:
        return
    await upsert_resource_claim_templates(
        kube,
        namespace=BERTRAND_NAMESPACE,
        intents=workload.resource_claim_templates,
        labels=workload.labels,
        timeout=timeout,
    )


async def _delete_stable_resources(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    timeout: float,
) -> None:
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    await delete_workload_http_routes(
        kube,
        identity=identity,
        timeout=deadline - loop.time(),
    )
    await delete_workload_service(
        kube,
        identity=identity,
        timeout=deadline - loop.time(),
    )
    await delete_workload_network_policy(
        kube,
        identity=identity,
        timeout=deadline - loop.time(),
    )
    await _delete_deployment(kube, identity=identity, timeout=deadline - loop.time())
    await _delete_cronjob(kube, identity=identity, timeout=deadline - loop.time())


async def _scale_deployment_to_zero(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    timeout: float,
) -> bool:
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    deployment = await Deployment.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=identity.name,
        timeout=deadline - loop.time(),
    )
    _assert_managed(deployment, identity=identity, kind="Deployment")
    if deployment is None or deployment.replicas == 0:
        return False
    await deployment.scale(kube, replicas=0, timeout=deadline - loop.time())
    return True


async def _suspend_cronjob(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    timeout: float,
) -> bool:
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    cronjob = await CronJob.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=identity.name,
        timeout=deadline - loop.time(),
    )
    _assert_managed(cronjob, identity=identity, kind="CronJob")
    if cronjob is None:
        return False
    if cronjob.suspended:
        return False
    await cronjob.suspend(kube, suspend=True, timeout=deadline - loop.time())
    return True


async def _active_workload_jobs(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    timeout: float,
) -> tuple[Job, ...]:
    jobs = await Job.list(
        kube,
        namespaces=(BERTRAND_NAMESPACE,),
        labels=_runtime_labels(identity),
        timeout=timeout,
    )
    active = tuple(job for job in jobs if not job.is_complete and not job.is_failed)
    for job in active:
        _assert_managed(job, identity=identity, kind="Job")
    return active


async def _active_workload_pods(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    timeout: float,
) -> tuple[Pod, ...]:
    pods = await Pod.list(
        kube,
        namespaces=(BERTRAND_NAMESPACE,),
        labels=_runtime_labels(identity),
        timeout=timeout,
    )
    active = tuple(pod for pod in pods if not pod.is_terminal)
    for pod in active:
        _assert_managed(pod, identity=identity, kind="Pod")
    return active


async def _wait_workload_pods_stopped(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    timeout: float,
) -> None:
    if timeout <= 0:
        msg = f"timed out waiting for workload {identity.name} pods to stop"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    while True:
        remaining = deadline - loop.time()
        if remaining <= 0:
            pods = await _active_workload_pods(
                kube,
                identity=identity,
                timeout=_KILL_POD_POLL_SECONDS,
            )
            diagnostics = "\n".join(
                line for pod in pods for line in pod.status_diagnostics
            )
            detail = f"\n\nPod status:\n{diagnostics}" if diagnostics else ""
            msg = f"timed out waiting for workload {identity.name} pods to stop{detail}"
            raise TimeoutError(msg)
        pods = await _active_workload_pods(
            kube,
            identity=identity,
            timeout=remaining,
        )
        if not pods:
            return
        await asyncio.sleep(min(_KILL_POD_POLL_SECONDS, remaining))


def _runtime_labels(identity: WorkloadIdentity) -> dict[str, str]:
    labels = {WORKLOAD_LABEL: WORKLOAD_LABEL_VALUE}
    labels.update(identity.selector)
    return labels


async def _delete_deployment(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    timeout: float,
) -> None:
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    deployment = await Deployment.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=identity.name,
        timeout=deadline - loop.time(),
    )
    _assert_managed(deployment, identity=identity, kind="Deployment")
    if deployment is not None:
        await deployment.delete(kube, timeout=deadline - loop.time())


async def _delete_cronjob(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    timeout: float,
) -> None:
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    cronjob = await CronJob.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=identity.name,
        timeout=deadline - loop.time(),
    )
    _assert_managed(cronjob, identity=identity, kind="CronJob")
    if cronjob is not None:
        await cronjob.delete(kube, timeout=deadline - loop.time())


def _assert_managed(
    resource: StableWorkloadController | Job | Pod | None,
    *,
    identity: WorkloadIdentity,
    kind: str,
) -> None:
    if resource is None:
        return
    labels = resource.labels
    expected = {
        WORKLOAD_LABEL: WORKLOAD_LABEL_VALUE,
        WORKLOAD_ID_LABEL: identity.workload_id,
    }
    expected.update(identity.selector)
    if all(labels.get(key) == value for key, value in expected.items()):
        return
    msg = (
        f"{kind} {BERTRAND_NAMESPACE}/{identity.name} exists but is not managed by "
        "this Bertrand workload"
    )
    raise OSError(msg)


def _topology_kind(config: _WorkloadConfig | None) -> WorkloadControllerKind:
    if config is None:
        return "none"
    kind = config.topology.kind
    if kind in ("none", "job", "cronjob", "deployment"):
        return kind
    msg = f"unsupported workload topology: {kind!r}"
    raise ValueError(msg)


def _identity(workload: WorkloadInput) -> WorkloadIdentity | None:
    if isinstance(workload, WorkloadPod):
        return workload.identity
    return workload


def _require_workload_pod(workload: WorkloadInput, *, kind: str) -> WorkloadPod:
    if isinstance(workload, WorkloadPod):
        return workload
    msg = f"{kind} topology requires a workload pod intent"
    raise ValueError(msg)


def _require_schedule(config: _WorkloadConfig | None) -> _ScheduleConfig:
    if config is not None and config.schedule is not None:
        return config.schedule
    msg = "CronJob topology requires schedule config"
    raise ValueError(msg)


def _execution(config: _WorkloadConfig | None) -> _ExecutionConfig | None:
    return config.execution if config is not None else None


def _rollout(config: _WorkloadConfig | None) -> _RolloutConfig | None:
    return config.rollout if config is not None else None


def _replicas(config: _WorkloadConfig | None) -> int:
    if config is None or config.scale is None:
        return 1
    return config.scale.replicas


def _backoff_limit(config: _WorkloadConfig | None) -> int:
    execution = _execution(config)
    return execution.retries if execution is not None else 0


def _ttl_seconds_after_finished(config: _WorkloadConfig | None) -> int | None:
    execution = _execution(config)
    return execution.ttl if execution is not None else None


def _active_deadline_seconds(config: _WorkloadConfig | None) -> int | None:
    execution = _execution(config)
    return execution.timeout if execution is not None else None


def _parallelism(config: _WorkloadConfig | None) -> int:
    execution = _execution(config)
    return execution.parallelism if execution is not None else 1


def _completions(config: _WorkloadConfig | None) -> int | None:
    execution = _execution(config)
    return execution.completions if execution is not None else None


def _completion_mode(config: _WorkloadConfig | None) -> JobCompletionMode:
    execution = _execution(config)
    value = execution.completion if execution is not None else "all"
    return _COMPLETION_MODE[value]


def _concurrency_policy(schedule: _ScheduleConfig) -> CronJobConcurrencyPolicy:
    return _SCHEDULE_CONCURRENCY[schedule.concurrency]


def _rollout_strategy(config: _WorkloadConfig | None) -> DeploymentStrategySpec | None:
    rollout = _rollout(config)
    if rollout is None:
        return None
    if rollout.strategy == "recreate":
        return DeploymentStrategySpec.recreate()
    return DeploymentStrategySpec.rolling_update(
        max_surge=rollout.max_surge,
        max_unavailable=rollout.max_unavailable,
    )


def _job_run_name(identity: WorkloadIdentity) -> str:
    suffix = uuid.uuid4().hex[:_JOB_RUN_SUFFIX_CHARS]
    prefix_chars = _MAX_KUBE_NAME_CHARS - len(suffix) - 1
    prefix = identity.name[:prefix_chars].rstrip("-")
    return f"{prefix}-{suffix}"
