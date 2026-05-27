"""Project-level native workload materialization."""

from __future__ import annotations

from dataclasses import replace
from typing import TYPE_CHECKING

from bertrand.env.config.bertrand import Bertrand, BertrandModel
from bertrand.env.config.core import Config, _check_uuid
from bertrand.env.git import Deadline, ensure_worktree_id
from bertrand.env.kube.build.project import project_image_spec
from bertrand.env.kube.build.refs import digest_from_ref, digest_ref
from bertrand.env.kube.build.request import (
    require_active_project_image,
    retire_project_images,
    worktree_identity,
)
from bertrand.env.kube.node_identity import resolve_host_id_for_node
from bertrand.env.kube.workload.base import WorkloadIdentity
from bertrand.env.kube.workload.config import workload_pod_from_config
from bertrand.env.kube.workload.controller import (
    StableWorkloadController,
    WorkloadRemoveResult,
    WorkloadScaleResult,
    create_workload_job_run,
    ensure_workload_controller,
    remove_workload,
    scale_workload,
)

if TYPE_CHECKING:
    from collections.abc import Sequence

    from bertrand.env.kube.api.client import Kube
    from bertrand.env.kube.build.request import BuildKitBuildSpec
    from bertrand.env.kube.job import Job
    from bertrand.env.kube.workload.base import WorkloadPod


async def ensure_project_workload_controller(
    kube: Kube,
    *,
    config: Config,
    repo_id: str,
    node: str | None = None,
    timeout: float,
    image_ref: str | None = None,
    primary_args: Sequence[str] | None = None,
    interactive: bool = False,
) -> StableWorkloadController | None:
    """Converge the stable Kubernetes workload selected by project config.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    config : Config
        Active project configuration context containing `[tool.bertrand]`.
    repo_id : str
        Stable repository UUID used for workload and image identity.
    node : str | None, optional
        Optional Kubernetes node name used for node-scoped capability resolution.
    timeout : float
        Maximum convergence budget in seconds. If infinite, wait indefinitely.
    image_ref : str | None, optional
        Optional digest-pinned project image reference from a just-completed build.
        If omitted, the current active `BuildKitBuild` lifecycle record supplies the
        immutable runtime image.
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
        msg = "project workload controller convergence timeout must be positive"
        raise TimeoutError(msg)
    _require_active_config(config)
    identity = _project_workload_identity(config, repo_id=repo_id)
    bertrand = _project_workload_config(config)
    if bertrand is None or not bertrand.containers:
        return await ensure_workload_controller(
            kube,
            config=bertrand,
            workload=identity,
            timeout=timeout,
            primary_args=primary_args,
            interactive=interactive,
        )
    if bertrand.topology.kind == "job":
        return await ensure_workload_controller(
            kube,
            config=bertrand,
            workload=identity,
            timeout=timeout,
            primary_args=primary_args,
            interactive=interactive,
        )

    deadline = Deadline.from_timeout(
        timeout,
        message="project workload controller convergence timeout must be positive",
    )
    workload = await _materialize_project_workload_pod(
        kube,
        config=config,
        workload_config=bertrand,
        repo_id=repo_id,
        node=node,
        image_ref=image_ref,
        deadline=deadline,
    )
    return await ensure_workload_controller(
        kube,
        config=bertrand,
        workload=workload or identity,
        timeout=deadline.remaining(),
        primary_args=primary_args,
        interactive=interactive,
    )


async def create_project_workload_job_run(
    kube: Kube,
    *,
    config: Config,
    repo_id: str,
    node: str | None = None,
    timeout: float,
    image_ref: str | None = None,
    primary_args: Sequence[str] | None = None,
    interactive: bool = False,
) -> Job:
    """Create one explicit Kubernetes Job run selected by project config.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    config : Config
        Active project configuration context containing `[tool.bertrand]`.
    repo_id : str
        Stable repository UUID used for workload and image identity.
    node : str | None, optional
        Optional Kubernetes node name used for node-scoped capability resolution.
    timeout : float
        Maximum creation budget in seconds. If infinite, wait indefinitely.
    image_ref : str | None, optional
        Optional digest-pinned project image reference from a just-completed build.
        If omitted, the current active `BuildKitBuild` lifecycle record supplies the
        immutable runtime image.
    primary_args : Sequence[str] | None, optional
        Runtime arguments to append to the primary container command.
    interactive : bool, optional
        Whether the primary container should be rendered for stdin/TTY attachment.

    Returns
    -------
    Job
        Created generated workload Job.

    Raises
    ------
    TimeoutError
        If creation cannot start before `timeout` expires.
    ValueError
        If project config does not select Job topology.
    """
    if timeout <= 0:
        msg = "project workload Job run creation timeout must be positive"
        raise TimeoutError(msg)
    _require_active_config(config)
    bertrand = _project_workload_config(config)
    if bertrand is None or not bertrand.containers:
        msg = "project workload Job runs require configured containers"
        raise ValueError(msg)
    if bertrand.topology.kind != "job":
        msg = "project workload Job runs require Job topology"
        raise ValueError(msg)

    deadline = Deadline.from_timeout(
        timeout,
        message="project workload Job run creation timeout must be positive",
    )
    workload = await _materialize_project_workload_pod(
        kube,
        config=config,
        workload_config=bertrand,
        repo_id=repo_id,
        node=node,
        image_ref=image_ref,
        deadline=deadline,
    )
    if workload is None:
        msg = "project workload Job runs require a rendered workload pod"
        raise ValueError(msg)
    return await create_workload_job_run(
        kube,
        config=bertrand,
        workload=workload,
        timeout=deadline.remaining(),
        primary_args=primary_args,
        interactive=interactive,
    )


async def scale_project_workload(
    kube: Kube,
    *,
    config: Config,
    repo_id: str,
    replicas: int,
    grace_period_seconds: int,
    timeout: float,
) -> WorkloadScaleResult:
    """Scale native workload execution for one project worktree.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    config : Config
        Active project configuration context.
    repo_id : str
        Stable repository UUID used for workload identity.
    replicas : int
        Requested logical workload replica count.
    grace_period_seconds : int
        Kubernetes pod termination grace period.
    timeout : float
        Maximum API-operation budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    WorkloadScaleResult
        Summary of controller and runtime resources affected by the operation.

    Raises
    ------
    TimeoutError
        If scale convergence cannot start before `timeout` expires.
    """
    if timeout <= 0:
        msg = "project workload scale timeout must be positive"
        raise TimeoutError(msg)
    _require_active_config(config)
    return await scale_workload(
        kube,
        config=_project_workload_config(config),
        identity=_project_workload_identity(config, repo_id=repo_id),
        replicas=replicas,
        grace_period_seconds=grace_period_seconds,
        timeout=timeout,
    )


async def remove_project_workload(
    kube: Kube,
    *,
    config: Config,
    repo_id: str,
    grace_period_seconds: int,
    timeout: float,
) -> WorkloadRemoveResult:
    """Remove native workload topology and retire worktree image records.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    config : Config
        Active project configuration context.
    repo_id : str
        Stable repository UUID used for workload and image identity.
    grace_period_seconds : int
        Kubernetes pod termination grace period.
    timeout : float
        Maximum API-operation budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    WorkloadRemoveResult
        Summary of workload resources removed and image records retired.

    Raises
    ------
    TimeoutError
        If removal cannot start before `timeout` expires.
    """
    if timeout <= 0:
        msg = "project workload removal timeout must be positive"
        raise TimeoutError(msg)
    _require_active_config(config)
    deadline = Deadline.from_timeout(
        timeout,
        message="project workload removal timeout must be positive",
    )
    identity = _project_workload_identity(config, repo_id=repo_id)
    result = await remove_workload(
        kube,
        identity=identity,
        grace_period_seconds=grace_period_seconds,
        timeout=deadline.remaining(),
    )
    retired = await retire_project_images(
        kube,
        repo_id=identity.repo_id,
        worktree_id=identity.worktree_id,
        timeout=deadline.remaining(),
    )
    return replace(result, images_retired=tuple(record.name for record in retired))


def _project_workload_identity(config: Config, *, repo_id: str) -> WorkloadIdentity:
    repo_id = _check_uuid(repo_id)
    return WorkloadIdentity(
        repo_id=repo_id,
        worktree_id=ensure_worktree_id(config.root),
        worktree=worktree_identity(config.worktree),
    )


def _project_workload_config(config: Config) -> BertrandModel | None:
    return config.get(Bertrand)


async def _materialize_project_workload_pod(
    kube: Kube,
    *,
    config: Config,
    workload_config: BertrandModel,
    repo_id: str,
    node: str | None,
    image_ref: str | None,
    deadline: Deadline,
) -> WorkloadPod | None:
    image_spec = project_image_spec(config, repo_id=repo_id)
    image = await _project_workload_image_ref(
        kube,
        spec=image_spec,
        image_ref=image_ref,
        timeout=deadline.remaining(),
    )
    return await _render_project_workload_pod(
        kube,
        workload_config=workload_config,
        image_spec=image_spec,
        image=image,
        node=node,
        deadline=deadline,
    )


async def _render_project_workload_pod(
    kube: Kube,
    *,
    workload_config: BertrandModel,
    image_spec: BuildKitBuildSpec,
    image: str,
    node: str | None,
    deadline: Deadline,
) -> WorkloadPod | None:
    host_id = await _project_workload_host_id(
        kube,
        config=workload_config,
        node=node,
        timeout=deadline.remaining(),
    )
    return await workload_pod_from_config(
        kube,
        config=workload_config,
        repo_id=image_spec.repo_id,
        worktree=image_spec.worktree,
        worktree_id=image_spec.worktree_id,
        image=image,
        host_id=host_id,
        timeout=deadline.remaining(),
    )


async def _project_workload_host_id(
    kube: Kube,
    *,
    config: BertrandModel,
    node: str | None,
    timeout: float,
) -> str | None:
    node_name = (node or config.node or "").strip()
    if not node_name:
        return None
    return await resolve_host_id_for_node(kube, node_name=node_name, timeout=timeout)


async def _project_workload_image_ref(
    kube: Kube,
    *,
    spec: BuildKitBuildSpec,
    image_ref: str | None,
    timeout: float,
) -> str:
    if image_ref is not None:
        return _validate_project_workload_image_ref(image_ref, spec=spec)
    record = await require_active_project_image(
        kube,
        spec=spec,
        timeout=timeout,
    )
    return record.digest_ref


def _validate_project_workload_image_ref(
    image_ref: str,
    *,
    spec: BuildKitBuildSpec,
) -> str:
    try:
        digest = digest_from_ref(image_ref, label="project workload image_ref")
        expected = digest_ref(spec.image, digest)
    except ValueError as err:
        msg = (
            "project workload image_ref must be a digest-pinned ref for the "
            f"configured project image repository: {image_ref!r}"
        )
        raise ValueError(msg) from err
    value = image_ref.strip()
    if value != expected:
        msg = (
            "project workload image_ref must be a digest-pinned ref for the "
            "configured project image repository: "
            f"expected {expected!r}, got {image_ref!r}"
        )
        raise ValueError(msg)
    return value


def _require_active_config(config: Config) -> None:
    if not isinstance(config, Config):
        msg = "project workload materialization requires an active Config instance"
        raise TypeError(msg)
    if not config:
        msg = "project workload materialization requires an active config context"
        raise RuntimeError(msg)
