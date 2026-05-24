"""External CLI endpoint for removing native Bertrand workload topology."""

from __future__ import annotations

from typing import TYPE_CHECKING

from bertrand.env.cli.external._helper import (
    prune_repository_mounts_quietly,
    resolve_project_worktree,
)
from bertrand.env.config.core import Config
from bertrand.env.git import INFINITY
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.workload.project import remove_project_workload

if TYPE_CHECKING:
    from pathlib import Path

    from bertrand.env.kube.workload.controller import WorkloadRemoveResult


async def bertrand_rm(
    target: Path,
    *,
    grace_period_seconds: int = 10,
    timeout: float = INFINITY,
) -> None:
    """Remove managed Kubernetes workload topology for a project target.

    Parameters
    ----------
    target : Path
        Project repository or worktree path. Repository roots target the worktree
        attached to HEAD.
    grace_period_seconds : int, optional
        Kubernetes pod termination grace period for active execution objects.
    timeout : float, optional
        Maximum Kubernetes API/convergence budget.

    Raises
    ------
    ValueError
        If `grace_period_seconds` is negative.
    TimeoutError
        If the operation cannot start before `timeout` expires.

    Notes
    -----
    Repository PVCs, volume records, mount records, credentials, snapshots, and
    Ceph data are preserved.
    """
    if grace_period_seconds < 0:
        msg = "rm grace period cannot be negative"
        raise ValueError(msg)
    if timeout <= 0:
        msg = "rm timeout must be positive"
        raise TimeoutError(msg)

    with await Kube.host(timeout=timeout) as kube:
        repo, worktree = await resolve_project_worktree(
            kube,
            target,
            timeout=timeout,
        )
        config = await Config.load(worktree, kube=kube, repo=repo, timeout=timeout)
        async with config:
            result = await remove_project_workload(
                kube,
                config=config,
                repo_id=config.repo.repo_id,
                grace_period_seconds=grace_period_seconds,
                timeout=timeout,
            )
        await prune_repository_mounts_quietly(kube, timeout=timeout)
    _print_rm_result(result)


def _print_rm_result(result: WorkloadRemoveResult) -> None:
    if not result.changed:
        print(f"workload: {result.workload} already removed")
        return
    if result.deployment_deleted:
        print(f"deployment: {result.workload} deleted")
    if result.cronjob_deleted:
        print(f"cronjob: {result.workload} deleted")
    if result.jobs_deleted:
        print(f"jobs: deleted {', '.join(result.jobs_deleted)}")
    if result.pods_deleted:
        print(f"pods: terminating {', '.join(result.pods_deleted)}")
    if result.images_retired:
        print(f"images: retired {len(result.images_retired)} record(s)")
