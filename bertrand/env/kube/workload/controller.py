"""Native Kubernetes controller materialization for Bertrand workloads."""

from __future__ import annotations

import uuid
from dataclasses import dataclass
from typing import TYPE_CHECKING, Literal, cast

from bertrand.env.git import BERTRAND_NAMESPACE, Deadline
from bertrand.env.kube.capability.device import upsert_resource_claim_templates
from bertrand.env.kube.cronjob import (
    CRON_JOB_RESOURCE,
    CronJob,
    CronJobConcurrencyPolicy,
)
from bertrand.env.kube.deployment import DEPLOYMENT_RESOURCE, Deployment
from bertrand.env.kube.job import JOB_RESOURCE, Job, JobCompletionMode
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
from bertrand.env.kube.pod import POD_RESOURCE, Pod
from bertrand.env.kube.workload.base import WorkloadIdentity, WorkloadPod

if TYPE_CHECKING:
    from collections.abc import Sequence

    from bertrand.env.config.bertrand import BertrandModel
    from bertrand.env.kube.api.client import Kube
    from bertrand.env.kube.api.resource import DeletionPropagationPolicy
    from bertrand.env.kube.api.spec import (
        DeploymentRollingUpdateManifest,
        DeploymentStrategyManifest,
    )

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
class WorkloadScaleResult:
    """Summary of a native workload scale operation.

    Parameters
    ----------
    workload : str
        Stable Kubernetes workload resource name.
    requested_replicas : int
        Requested logical workload replica count.
    deployment_replicas : int | None, optional
        Deployment replica count patched by the operation.
    cronjob_suspended : bool | None, optional
        CronJob suspension state patched by the operation.
    jobs_deleted : tuple[str, ...], optional
        Active managed Job names whose deletion was requested.
    pods_deleted : tuple[str, ...], optional
        Active managed Pod names whose deletion was requested directly.
    """

    workload: str
    requested_replicas: int
    deployment_replicas: int | None = None
    cronjob_suspended: bool | None = None
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
            self.deployment_replicas is not None
            or self.cronjob_suspended is not None
            or bool(self.jobs_deleted)
            or bool(self.pods_deleted)
        )


@dataclass(frozen=True)
class WorkloadRemoveResult:
    """Summary of a native workload removal operation.

    Parameters
    ----------
    workload : str
        Stable Kubernetes workload resource name.
    deployment_deleted : bool, optional
        Whether a managed Deployment deletion was requested.
    cronjob_deleted : bool, optional
        Whether a managed CronJob deletion was requested.
    jobs_deleted : tuple[str, ...], optional
        Active managed Job names whose deletion was requested.
    pods_deleted : tuple[str, ...], optional
        Active managed Pod names whose deletion was requested directly.
    images_retired : tuple[str, ...], optional
        Project image lifecycle record names retired by the project layer.
    """

    workload: str
    deployment_deleted: bool = False
    cronjob_deleted: bool = False
    jobs_deleted: tuple[str, ...] = ()
    pods_deleted: tuple[str, ...] = ()
    images_retired: tuple[str, ...] = ()

    @property
    def changed(self) -> bool:
        """Return whether any managed workload state was removed.

        Returns
        -------
        bool
            `True` when any Kubernetes object or image lifecycle record changed.
        """
        return (
            self.deployment_deleted
            or self.cronjob_deleted
            or bool(self.jobs_deleted)
            or bool(self.pods_deleted)
            or bool(self.images_retired)
        )


async def ensure_workload_controller(
    kube: Kube,
    *,
    config: BertrandModel | None,
    workload: WorkloadInput,
    deadline: Deadline,
    primary_args: Sequence[str] | None = None,
    interactive: bool = False,
) -> StableWorkloadController | None:
    """Converge the stable Kubernetes controller for one workload topology.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    config : WorkloadConfig | None
        Validated Bertrand workload config. Missing config selects no workload.
    workload : WorkloadPod | WorkloadIdentity | None
        Workload pod intent for Deployment and CronJob topologies, or a stable
        identity for no-workload cleanup. Passing `None` makes no-workload cleanup
        a no-op because there is no resource name to target.
    deadline : Deadline
        Maximum convergence budget in seconds. If infinite, wait indefinitely.
    primary_args : Sequence[str] | None, optional
        Runtime arguments to append to the primary container command.
    interactive : bool, optional
        Whether the primary container should be rendered for stdin/TTY attachment.

    Returns
    -------
    Deployment | CronJob | None
        Converged stable controller, or `None` for Job/no-workload topology.

    """
    kind = _topology_kind(config)

    if kind == "deployment":
        return await _ensure_deployment_controller(
            kube,
            config=config,
            workload=workload,
            deadline=deadline,
            primary_args=primary_args,
            interactive=interactive,
        )

    if kind == "cronjob":
        return await _ensure_cronjob_controller(
            kube,
            config=config,
            workload=workload,
            deadline=deadline,
            primary_args=primary_args,
        )

    identity = _identity(workload)
    if identity is not None:
        await _delete_stable_resources(
            kube,
            identity=identity,
            deadline=deadline,
        )
    return None


async def _ensure_deployment_controller(
    kube: Kube,
    *,
    config: BertrandModel | None,
    workload: WorkloadInput,
    deadline: Deadline,
    primary_args: Sequence[str] | None,
    interactive: bool,
) -> Deployment:
    pod = _require_workload_pod(workload, kind="deployment")
    network = _require_network(config)
    rollout = _rollout(config)
    route_plan = await prepare_workload_http_routes(
        kube,
        network=network,
        workload=pod,
        deadline=deadline,
    )
    await ensure_workload_claim_templates(
        kube,
        workload=pod,
        deadline=deadline,
    )
    await _delete_cronjob(
        kube,
        identity=pod.identity,
        deadline=deadline,
    )
    await _ensure_deployment_network(
        kube,
        network=network,
        workload=pod,
        route_plan=route_plan,
        deadline=deadline,
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
        deadline=deadline,
    )


async def _ensure_cronjob_controller(
    kube: Kube,
    *,
    config: BertrandModel | None,
    workload: WorkloadInput,
    deadline: Deadline,
    primary_args: Sequence[str] | None,
) -> CronJob:
    pod = _require_workload_pod(workload, kind="cronjob")
    schedule = _require_schedule(config)
    execution = config.execution if config is not None else None
    await ensure_workload_claim_templates(
        kube,
        workload=pod,
        deadline=deadline,
    )
    await _delete_network_stack(
        kube,
        identity=pod.identity,
        deadline=deadline,
    )
    await _delete_deployment(
        kube,
        identity=pod.identity,
        deadline=deadline,
    )
    return await CronJob.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=pod.name,
        labels=pod.labels,
        pod_template=pod.pod_template(primary_args=primary_args),
        schedule=schedule.cron,
        backoff_limit=execution.retries if execution is not None else 0,
        ttl_seconds_after_finished=execution.ttl if execution is not None else None,
        active_deadline_seconds=execution.timeout if execution is not None else None,
        parallelism=execution.parallelism if execution is not None else 1,
        completions=execution.completions if execution is not None else None,
        completion_mode=_COMPLETION_MODE[
            execution.completion if execution is not None else "all"
        ],
        concurrency_policy=_concurrency_policy(schedule),
        suspend=False if schedule.suspend is None else schedule.suspend,
        starting_deadline_seconds=schedule.start_deadline,
        successful_jobs_history_limit=schedule.history.success,
        failed_jobs_history_limit=schedule.history.failure,
        time_zone=schedule.timezone,
        deadline=deadline,
    )


async def create_workload_job_run(
    kube: Kube,
    *,
    config: BertrandModel,
    workload: WorkloadPod,
    deadline: Deadline,
    primary_args: Sequence[str] | None = None,
    interactive: bool = False,
) -> Job:
    """Create one generated Kubernetes Job run for a Job-topology workload.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    config : WorkloadConfig
        Validated Bertrand workload config whose topology must be `"job"`.
    workload : WorkloadPod
        Workload pod intent to render into the generated Job.
    deadline : Deadline
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
    ValueError
        If `config` does not select Job topology.
    """
    if _topology_kind(config) != "job":
        msg = "generated workload Job runs require Job topology"
        raise ValueError(msg)

    return await _create_generated_workload_job(
        kube,
        config=config,
        workload=workload,
        deadline=deadline,
        primary_args=primary_args,
        interactive=interactive,
    )


async def _create_generated_workload_job(
    kube: Kube,
    *,
    config: BertrandModel,
    workload: WorkloadPod,
    deadline: Deadline,
    primary_args: Sequence[str] | None,
    interactive: bool,
) -> Job:
    execution = config.execution
    await _delete_stable_resources(
        kube,
        identity=workload.identity,
        deadline=deadline,
    )
    await ensure_workload_claim_templates(
        kube,
        workload=workload,
        deadline=deadline,
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
        backoff_limit=execution.retries if execution is not None else 0,
        ttl_seconds_after_finished=execution.ttl if execution is not None else None,
        active_deadline_seconds=execution.timeout if execution is not None else None,
        parallelism=execution.parallelism if execution is not None else 1,
        completions=execution.completions if execution is not None else None,
        completion_mode=_COMPLETION_MODE[
            execution.completion if execution is not None else "all"
        ],
        deadline=deadline,
    )


async def scale_workload(
    kube: Kube,
    *,
    config: BertrandModel | None,
    identity: WorkloadIdentity,
    replicas: int,
    grace_period_seconds: int,
    deadline: Deadline,
) -> WorkloadScaleResult:
    """Scale active Kubernetes workload execution for one Bertrand identity.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    config : WorkloadConfig | None
        Validated Bertrand workload config used to infer topology semantics.
    identity : WorkloadIdentity
        Stable workload identity to scale.
    replicas : int
        Requested logical workload replica count.
    grace_period_seconds : int
        Kubernetes pod termination grace period.
    deadline : Deadline
        Maximum API-operation budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    WorkloadScaleResult
        Summary of controller and runtime resources affected by the scale request.

    Raises
    ------
    ValueError
        If `replicas` or `grace_period_seconds` is negative, or the topology cannot
        be scaled to the requested logical count.
    """
    if replicas < 0:
        msg = "workload scale replicas cannot be negative"
        raise ValueError(msg)
    if grace_period_seconds < 0:
        msg = "workload scale grace period cannot be negative"
        raise ValueError(msg)

    deployment_replicas: int | None = None
    cronjob_suspended: bool | None = None
    jobs_deleted: tuple[str, ...] = ()
    pods_deleted: tuple[str, ...] = ()

    kind = _topology_kind(config)
    if kind == "deployment":
        deployment_replicas = await _scale_deployment(
            kube,
            identity=identity,
            replicas=replicas,
            deadline=deadline,
        )
        jobs_deleted, pods_deleted = await _delete_active_execution_if_stopped(
            kube,
            identity=identity,
            replicas=replicas,
            grace_period_seconds=grace_period_seconds,
            deadline=deadline,
        )
    elif kind == "cronjob":
        if replicas > 1:
            msg = (
                "CronJob topology cannot scale above one logical replica; use "
                "`bertrand run` for immediate generated Job runs"
            )
            raise ValueError(msg)
        cronjob_suspended = await _set_cronjob_suspended(
            kube,
            identity=identity,
            suspend=replicas == 0,
            deadline=deadline,
        )
        jobs_deleted, pods_deleted = await _delete_active_execution_if_stopped(
            kube,
            identity=identity,
            replicas=replicas,
            grace_period_seconds=grace_period_seconds,
            deadline=deadline,
        )
    elif kind == "job":
        if replicas > 0:
            msg = (
                "Job topology does not have persistent replicas; use `bertrand run` "
                "to create a generated Job run"
            )
            raise ValueError(msg)
        jobs_deleted, pods_deleted = await _delete_active_execution(
            kube,
            identity=identity,
            grace_period_seconds=grace_period_seconds,
            deadline=deadline,
        )
    elif replicas > 0:
        msg = "cannot scale a project with no configured workload"
        raise ValueError(msg)

    return WorkloadScaleResult(
        workload=identity.name,
        requested_replicas=replicas,
        deployment_replicas=deployment_replicas,
        cronjob_suspended=cronjob_suspended,
        jobs_deleted=jobs_deleted,
        pods_deleted=pods_deleted,
    )


async def _delete_active_execution_if_stopped(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    replicas: int,
    grace_period_seconds: int,
    deadline: Deadline,
) -> tuple[tuple[str, ...], tuple[str, ...]]:
    if replicas != 0:
        return (), ()
    return await _delete_active_execution(
        kube,
        identity=identity,
        grace_period_seconds=grace_period_seconds,
        deadline=deadline,
    )


async def remove_workload(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    grace_period_seconds: int,
    deadline: Deadline,
) -> WorkloadRemoveResult:
    """Remove managed Kubernetes workload topology for one Bertrand identity.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    identity : WorkloadIdentity
        Stable workload identity to remove.
    grace_period_seconds : int
        Kubernetes pod termination grace period for active execution objects.
    deadline : Deadline
        Maximum API-operation budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    WorkloadRemoveResult
        Summary of managed workload resources removed by the operation.

    Raises
    ------
    ValueError
        If `grace_period_seconds` is negative.
    """
    if grace_period_seconds < 0:
        msg = "workload removal grace period cannot be negative"
        raise ValueError(msg)

    await _delete_network_stack(
        kube,
        identity=identity,
        deadline=deadline,
    )
    deployment_deleted = await _delete_deployment(
        kube,
        identity=identity,
        deadline=deadline,
        propagation_policy="Foreground",
        grace_period_seconds=grace_period_seconds,
    )
    cronjob_deleted = await _delete_cronjob(
        kube,
        identity=identity,
        deadline=deadline,
        propagation_policy="Orphan",
        grace_period_seconds=grace_period_seconds,
    )
    jobs_deleted, pods_deleted = await _delete_active_execution(
        kube,
        identity=identity,
        grace_period_seconds=grace_period_seconds,
        deadline=deadline,
    )
    return WorkloadRemoveResult(
        workload=identity.name,
        deployment_deleted=deployment_deleted,
        cronjob_deleted=cronjob_deleted,
        jobs_deleted=jobs_deleted,
        pods_deleted=pods_deleted,
    )


async def ensure_workload_claim_templates(
    kube: Kube,
    *,
    workload: WorkloadPod,
    deadline: Deadline,
) -> None:
    """Converge DRA claim templates required by a rendered workload pod.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    workload : WorkloadPod
        Rendered workload pod intent.
    deadline : Deadline
        Maximum convergence budget in seconds.
    """
    if not workload.resource_claim_capabilities_by_container:
        return
    for (
        container_name,
        capability_ids,
    ) in workload.resource_claim_capabilities_by_container.items():
        await upsert_resource_claim_templates(
            kube,
            namespace=BERTRAND_NAMESPACE,
            owner=workload.name,
            capability_ids=capability_ids,
            container_name=container_name,
            labels=workload.labels,
            deadline=deadline,
        )


async def _ensure_deployment_network(
    kube: Kube,
    *,
    network: BertrandModel.Network,
    workload: WorkloadPod,
    route_plan: dict[str, tuple[BertrandModel.Network.Route, int]],
    deadline: Deadline,
) -> None:
    await prune_workload_http_routes(
        kube,
        identity=workload.identity,
        route_plan=route_plan,
        deadline=deadline,
    )
    await ensure_workload_network_policy(
        kube,
        network=network,
        workload=workload,
        deadline=deadline,
        route_plan=route_plan,
    )
    await ensure_workload_service(
        kube,
        workload=workload,
        deadline=deadline,
    )
    await ensure_workload_http_routes(
        kube,
        network=network,
        workload=workload,
        deadline=deadline,
        route_plan=route_plan,
    )


async def _delete_network_stack(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    deadline: Deadline,
) -> None:
    await delete_workload_http_routes(
        kube,
        identity=identity,
        deadline=deadline,
    )
    await delete_workload_service(
        kube,
        identity=identity,
        deadline=deadline,
    )
    await delete_workload_network_policy(
        kube,
        identity=identity,
        deadline=deadline,
    )


async def _delete_stable_resources(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    deadline: Deadline,
) -> None:
    await _delete_network_stack(
        kube,
        identity=identity,
        deadline=deadline,
    )
    await _delete_deployment(kube, identity=identity, deadline=deadline)
    await _delete_cronjob(kube, identity=identity, deadline=deadline)


async def _scale_deployment(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    replicas: int,
    deadline: Deadline,
) -> int | None:
    deployment = await DEPLOYMENT_RESOURCE.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=identity.name,
        deadline=deadline,
    )
    _assert_managed(deployment, identity=identity, kind="Deployment")
    if deployment is None:
        if replicas == 0:
            return None
        msg = (
            f"cannot scale missing Deployment {BERTRAND_NAMESPACE}/{identity.name}; "
            "run `bertrand run` first"
        )
        raise OSError(msg)
    if deployment.replicas == replicas:
        return None
    await deployment.scale(kube, replicas=replicas, deadline=deadline)
    return replicas


async def _set_cronjob_suspended(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    suspend: bool,
    deadline: Deadline,
) -> bool | None:
    cronjob = await CRON_JOB_RESOURCE.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=identity.name,
        deadline=deadline,
    )
    _assert_managed(cronjob, identity=identity, kind="CronJob")
    if cronjob is None:
        if suspend:
            return None
        msg = (
            f"cannot resume missing CronJob {BERTRAND_NAMESPACE}/{identity.name}; "
            "run `bertrand run` first"
        )
        raise OSError(msg)
    if cronjob.suspended == suspend:
        return None
    await cronjob.suspend(kube, suspend=suspend, deadline=deadline)
    return suspend


async def _delete_active_execution(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    grace_period_seconds: int,
    deadline: Deadline,
) -> tuple[tuple[str, ...], tuple[str, ...]]:
    jobs = await _active_workload_jobs(
        kube,
        identity=identity,
        deadline=deadline,
    )
    for job in jobs:
        await JOB_RESOURCE.delete(
            kube,
            job,
            deadline=deadline,
            propagation_policy="Foreground",
            grace_period_seconds=grace_period_seconds,
        )
    pods = await _active_workload_pods(
        kube,
        identity=identity,
        deadline=deadline,
    )
    for pod in pods:
        await POD_RESOURCE.delete(
            kube,
            pod,
            deadline=deadline,
            grace_period_seconds=grace_period_seconds,
        )
    remaining = deadline.remaining
    if remaining <= 0:
        return tuple(job.name for job in jobs), tuple(pod.name for pod in pods)
    await _wait_workload_pods_stopped(
        kube,
        identity=identity,
        deadline=Deadline(
            min(grace_period_seconds + _KILL_POD_PROOF_SECONDS, remaining)
        ),
    )
    return tuple(job.name for job in jobs), tuple(pod.name for pod in pods)


async def _active_workload_jobs(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    deadline: Deadline,
) -> tuple[Job, ...]:
    jobs = await JOB_RESOURCE.list(
        kube,
        namespaces=(BERTRAND_NAMESPACE,),
        labels=identity.managed_selector,
        deadline=deadline,
    )
    active = tuple(job for job in jobs if not job.is_complete and not job.is_failed)
    for job in active:
        _assert_managed(job, identity=identity, kind="Job")
    return active


async def _active_workload_pods(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    deadline: Deadline,
) -> tuple[Pod, ...]:
    pods = await POD_RESOURCE.list(
        kube,
        namespaces=(BERTRAND_NAMESPACE,),
        labels=identity.managed_selector,
        deadline=deadline,
    )
    active = tuple(pod for pod in pods if not pod.is_terminal)
    for pod in active:
        _assert_managed(pod, identity=identity, kind="Pod")
    return active


async def _wait_workload_pods_stopped(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    deadline: Deadline,
) -> None:
    while True:
        remaining = deadline.remaining
        if remaining <= 0:
            pods = await _active_workload_pods(
                kube,
                identity=identity,
                deadline=Deadline(_KILL_POD_POLL_SECONDS),
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
            deadline=deadline,
        )
        if not pods:
            return
        await deadline.sleep(_KILL_POD_POLL_SECONDS)


async def _delete_deployment(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    deadline: Deadline,
    propagation_policy: DeletionPropagationPolicy = "Background",
    grace_period_seconds: int | None = None,
) -> bool:
    deployment = await DEPLOYMENT_RESOURCE.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=identity.name,
        deadline=deadline,
    )
    _assert_managed(deployment, identity=identity, kind="Deployment")
    if deployment is None:
        return False
    await DEPLOYMENT_RESOURCE.delete(
        kube,
        deployment,
        deadline=deadline,
        propagation_policy=propagation_policy,
        grace_period_seconds=grace_period_seconds,
    )
    return True


async def _delete_cronjob(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    deadline: Deadline,
    propagation_policy: DeletionPropagationPolicy = "Background",
    grace_period_seconds: int | None = None,
) -> bool:
    cronjob = await CRON_JOB_RESOURCE.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=identity.name,
        deadline=deadline,
    )
    _assert_managed(cronjob, identity=identity, kind="CronJob")
    if cronjob is None:
        return False
    await CRON_JOB_RESOURCE.delete(
        kube,
        cronjob,
        deadline=deadline,
        propagation_policy=propagation_policy,
        grace_period_seconds=grace_period_seconds,
    )
    return True


def _assert_managed(
    resource: StableWorkloadController | Job | Pod | None,
    *,
    identity: WorkloadIdentity,
    kind: str,
) -> None:
    if resource is None:
        return
    labels = resource.labels
    expected = identity.managed_selector
    if all(labels.get(key) == value for key, value in expected.items()):
        return
    msg = (
        f"{kind} {BERTRAND_NAMESPACE}/{identity.name} exists but is not managed by "
        "this Bertrand workload"
    )
    raise OSError(msg)


def _topology_kind(config: BertrandModel | None) -> WorkloadControllerKind:
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


def _require_schedule(config: BertrandModel | None) -> BertrandModel.Schedule:
    if config is not None and config.schedule is not None:
        return config.schedule
    msg = "CronJob topology requires schedule config"
    raise ValueError(msg)


def _require_network(config: BertrandModel | None) -> BertrandModel.Network:
    if config is not None:
        return config.network
    msg = "workload network convergence requires workload config"
    raise ValueError(msg)


def _rollout(config: BertrandModel | None) -> BertrandModel.Rollout | None:
    return config.rollout if config is not None else None


def _replicas(config: BertrandModel | None) -> int:
    if config is None or config.scale is None:
        return 1
    return config.scale.replicas


def _concurrency_policy(schedule: BertrandModel.Schedule) -> CronJobConcurrencyPolicy:
    return _SCHEDULE_CONCURRENCY[schedule.concurrency]


def _rollout_strategy(
    config: BertrandModel | None,
) -> DeploymentStrategyManifest | None:
    rollout = _rollout(config)
    if rollout is None:
        return None
    if rollout.strategy == "recreate":
        return {"type": "Recreate", "rollingUpdate": None}
    strategy: DeploymentStrategyManifest = {"type": "RollingUpdate"}
    rolling_update: DeploymentRollingUpdateManifest = {}
    if rollout.max_surge is not None:
        rolling_update["maxSurge"] = rollout.max_surge
    if rollout.max_unavailable is not None:
        rolling_update["maxUnavailable"] = rollout.max_unavailable
    if rolling_update:
        strategy["rollingUpdate"] = rolling_update
    return strategy


def _job_run_name(identity: WorkloadIdentity) -> str:
    suffix = uuid.uuid4().hex[:_JOB_RUN_SUFFIX_CHARS]
    prefix_chars = _MAX_KUBE_NAME_CHARS - len(suffix) - 1
    prefix = identity.name[:prefix_chars].rstrip("-")
    return f"{prefix}-{suffix}"
