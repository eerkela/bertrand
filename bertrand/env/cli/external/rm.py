"""External CLI endpoint for removing native Bertrand workload topology."""

from __future__ import annotations

from typing import TYPE_CHECKING

from bertrand.env.cli.external._helper import _project_command_context
from bertrand.env.git import Deadline
from bertrand.env.kube.workload.project import remove_project_workload

if TYPE_CHECKING:
    from pathlib import Path

    from bertrand.env.kube.workload.controller import WorkloadRemoveResult


async def bertrand_rm(
    target: Path,
    *,
    grace_period_seconds: int = 10,
    deadline: Deadline,
) -> None:
    """Remove managed Kubernetes workload topology for a project target.

    Parameters
    ----------
    target : Path
        Project repository or worktree path. Repository roots target the worktree
        attached to HEAD.
    grace_period_seconds : int, optional
        Kubernetes pod termination grace period for active execution objects.
    deadline : Deadline
        Maximum Kubernetes API/convergence budget.

    Raises
    ------
    ValueError
        If `grace_period_seconds` is negative.

    Notes
    -----
    Repository PVCs, volume records, mount records, credentials, snapshots, and
    Ceph data are preserved.
    """
    if grace_period_seconds < 0:
        msg = "rm grace period cannot be negative"
        raise ValueError(msg)

    async with _project_command_context(target, deadline=deadline) as (
        kube,
        _repo,
        _worktree,
        config,
    ):
        result = await remove_project_workload(
            kube,
            config=config,
            repo_id=config.repo.id,
            grace_period_seconds=grace_period_seconds,
            deadline=deadline,
        )
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
