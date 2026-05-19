"""Project-level native workload materialization."""

from __future__ import annotations

import asyncio
from typing import TYPE_CHECKING, Any, cast

from bertrand.env.config.bertrand import Bertrand
from bertrand.env.config.core import Config, _check_uuid
from bertrand.env.kube.build.lifecycle import worktree_identity
from bertrand.env.kube.build.project import project_image_build
from bertrand.env.kube.workload.base import WorkloadIdentity
from bertrand.env.kube.workload.config import workload_pod_from_config
from bertrand.env.kube.workload.controller import (
    StableWorkloadController,
    create_workload_job_run,
    ensure_workload_controller,
)

if TYPE_CHECKING:
    from bertrand.env.kube.api.client import Kube
    from bertrand.env.kube.job import Job


async def ensure_project_workload_controller(
    kube: Kube,
    *,
    config: Config,
    repo_id: str,
    node: str | None = None,
    timeout: float,
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
    bertrand = config.get(Bertrand)
    if bertrand is None or not bertrand.containers:
        return await ensure_workload_controller(
            kube,
            config=cast("Any", bertrand),
            workload=identity,
            timeout=timeout,
        )

    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    image_identity = project_image_build(config, repo_id=repo_id).identity
    workload = await workload_pod_from_config(
        kube,
        config=cast("Any", bertrand),
        repo_id=image_identity.repo_id,
        worktree=image_identity.worktree,
        env_id=image_identity.env_id,
        image=image_identity.image,
        node=node,
        timeout=deadline - loop.time(),
    )
    return await ensure_workload_controller(
        kube,
        config=cast("Any", bertrand),
        workload=workload or identity,
        timeout=deadline - loop.time(),
    )


async def create_project_workload_job_run(
    kube: Kube,
    *,
    config: Config,
    repo_id: str,
    node: str | None = None,
    timeout: float,
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
    bertrand = config.get(Bertrand)
    if bertrand is None or not bertrand.containers:
        msg = "project workload Job runs require configured containers"
        raise ValueError(msg)
    if bertrand.topology.kind != "job":
        msg = "project workload Job runs require Job topology"
        raise ValueError(msg)

    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    image_identity = project_image_build(config, repo_id=repo_id).identity
    workload = await workload_pod_from_config(
        kube,
        config=cast("Any", bertrand),
        repo_id=image_identity.repo_id,
        worktree=image_identity.worktree,
        env_id=image_identity.env_id,
        image=image_identity.image,
        node=node,
        timeout=deadline - loop.time(),
    )
    if workload is None:
        msg = "project workload Job runs require a rendered workload pod"
        raise ValueError(msg)
    return await create_workload_job_run(
        kube,
        config=cast("Any", bertrand),
        workload=workload,
        timeout=deadline - loop.time(),
    )


def _project_workload_identity(config: Config, *, repo_id: str) -> WorkloadIdentity:
    repo_id = _check_uuid(repo_id)
    return WorkloadIdentity(
        repo_id=repo_id,
        worktree=worktree_identity(config.worktree),
    )


def _require_active_config(config: Config) -> None:
    if not isinstance(config, Config):
        msg = "project workload materialization requires an active Config instance"
        raise TypeError(msg)
    if not config:
        msg = "project workload materialization requires an active config context"
        raise RuntimeError(msg)
