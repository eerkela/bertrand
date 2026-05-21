"""Project-level native workload materialization."""

from __future__ import annotations

import asyncio
from typing import TYPE_CHECKING, Any, cast

from bertrand.env.config.bertrand import Bertrand
from bertrand.env.config.core import Config, _check_uuid
from bertrand.env.kube.build.lifecycle import (
    require_active_project_image,
    worktree_identity,
)
from bertrand.env.kube.build.project import project_image_build
from bertrand.env.kube.build.refs import digest_from_ref, digest_ref
from bertrand.env.kube.workload.base import WorkloadIdentity
from bertrand.env.kube.workload.config import workload_pod_from_config
from bertrand.env.kube.workload.controller import (
    StableWorkloadController,
    WorkloadKillResult,
    create_workload_job_run,
    ensure_workload_controller,
    kill_workload,
)

if TYPE_CHECKING:
    from collections.abc import Sequence

    from bertrand.env.kube.api.client import Kube
    from bertrand.env.kube.build.request import ProjectImageIdentity
    from bertrand.env.kube.job import Job


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
        If omitted, the current active `BertrandImage` lifecycle record supplies the
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
    bertrand = config.get(Bertrand)
    if bertrand is None or not bertrand.containers:
        return await ensure_workload_controller(
            kube,
            config=cast("Any", bertrand),
            workload=identity,
            timeout=timeout,
            primary_args=primary_args,
            interactive=interactive,
        )
    if bertrand.topology.kind == "job":
        return await ensure_workload_controller(
            kube,
            config=cast("Any", bertrand),
            workload=identity,
            timeout=timeout,
            primary_args=primary_args,
            interactive=interactive,
        )

    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    image_identity = project_image_build(config, repo_id=repo_id).identity
    image = await _project_workload_image_ref(
        kube,
        identity=image_identity,
        image_ref=image_ref,
        timeout=deadline - loop.time(),
    )
    workload = await workload_pod_from_config(
        kube,
        config=cast("Any", bertrand),
        repo_id=image_identity.repo_id,
        worktree=image_identity.worktree,
        env_id=image_identity.env_id,
        image=image,
        node=node,
        timeout=deadline - loop.time(),
    )
    return await ensure_workload_controller(
        kube,
        config=cast("Any", bertrand),
        workload=workload or identity,
        timeout=deadline - loop.time(),
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
        If omitted, the current active `BertrandImage` lifecycle record supplies the
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
    image = await _project_workload_image_ref(
        kube,
        identity=image_identity,
        image_ref=image_ref,
        timeout=deadline - loop.time(),
    )
    workload = await workload_pod_from_config(
        kube,
        config=cast("Any", bertrand),
        repo_id=image_identity.repo_id,
        worktree=image_identity.worktree,
        env_id=image_identity.env_id,
        image=image,
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
        primary_args=primary_args,
        interactive=interactive,
    )


async def kill_project_workload(
    kube: Kube,
    *,
    config: Config,
    repo_id: str,
    grace_period_seconds: int,
    timeout: float,
) -> WorkloadKillResult:
    """Stop active native workload processes for one project worktree.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    config : Config
        Active project configuration context.
    repo_id : str
        Stable repository UUID used for workload identity.
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
        If kill convergence cannot start before `timeout` expires.
    """
    if timeout <= 0:
        msg = "project workload kill timeout must be positive"
        raise TimeoutError(msg)
    _require_active_config(config)
    return await kill_workload(
        kube,
        identity=_project_workload_identity(config, repo_id=repo_id),
        grace_period_seconds=grace_period_seconds,
        timeout=timeout,
    )


def _project_workload_identity(config: Config, *, repo_id: str) -> WorkloadIdentity:
    repo_id = _check_uuid(repo_id)
    return WorkloadIdentity(
        repo_id=repo_id,
        worktree=worktree_identity(config.worktree),
    )


async def _project_workload_image_ref(
    kube: Kube,
    *,
    identity: ProjectImageIdentity,
    image_ref: str | None,
    timeout: float,
) -> str:
    if image_ref is not None:
        return _validate_project_workload_image_ref(image_ref, identity=identity)
    record = await require_active_project_image(
        kube,
        identity=identity,
        timeout=timeout,
    )
    return record.digest_ref


def _validate_project_workload_image_ref(
    image_ref: str,
    *,
    identity: ProjectImageIdentity,
) -> str:
    try:
        digest = digest_from_ref(image_ref, label="project workload image_ref")
        expected = digest_ref(identity.image, digest)
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
