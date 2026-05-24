"""External CLI endpoint for scaling native Bertrand workloads."""

from __future__ import annotations

from typing import TYPE_CHECKING

from bertrand.env.cli.external._helper import (
    prune_repository_mounts_quietly,
    resolve_project_worktree,
)
from bertrand.env.config.core import Config
from bertrand.env.git import INFINITY
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.workload.project import scale_project_workload

if TYPE_CHECKING:
    from pathlib import Path

    from bertrand.env.kube.workload.controller import WorkloadScaleResult


async def bertrand_scale(
    target: Path,
    *,
    replicas: int,
    grace_period_seconds: int = 10,
    timeout: float = INFINITY,
) -> None:
    """Scale active Kubernetes workload execution for a project target.

    Parameters
    ----------
    target : Path
        Project repository or worktree path. Repository roots target the worktree
        attached to HEAD.
    replicas : int
        Requested logical workload replica count.
    grace_period_seconds : int, optional
        Kubernetes pod termination grace period when scale-to-zero deletes active
        execution objects.
    timeout : float, optional
        Maximum Kubernetes API/convergence budget.

    Raises
    ------
    ValueError
        If `replicas` or `grace_period_seconds` is negative.
    TimeoutError
        If the operation cannot start before `timeout` expires.
    """
    if replicas < 0:
        msg = "scale replicas cannot be negative"
        raise ValueError(msg)
    if grace_period_seconds < 0:
        msg = "scale grace period cannot be negative"
        raise ValueError(msg)
    if timeout <= 0:
        msg = "scale timeout must be positive"
        raise TimeoutError(msg)

    with await Kube.host(timeout=timeout) as kube:
        repo, worktree = await resolve_project_worktree(
            kube,
            target,
            timeout=timeout,
        )
        config = await Config.load(worktree, kube=kube, repo=repo, timeout=timeout)
        async with config:
            result = await scale_project_workload(
                kube,
                config=config,
                repo_id=config.repo.repo_id,
                replicas=replicas,
                grace_period_seconds=grace_period_seconds,
                timeout=timeout,
            )
        await prune_repository_mounts_quietly(kube, timeout=timeout)
    _print_scale_result(result)


def _print_scale_result(result: WorkloadScaleResult) -> None:
    if not result.changed:
        print(f"workload: {result.workload} already at requested scale")
        return
    if result.deployment_replicas is not None:
        print(f"deployment: {result.workload} scaled to {result.deployment_replicas}")
    if result.cronjob_suspended is not None:
        action = "suspended" if result.cronjob_suspended else "resumed"
        print(f"cronjob: {result.workload} {action}")
    if result.jobs_deleted:
        print(f"jobs: deleted {', '.join(result.jobs_deleted)}")
    if result.pods_deleted:
        print(f"pods: terminating {', '.join(result.pods_deleted)}")
