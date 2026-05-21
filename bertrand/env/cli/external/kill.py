"""External CLI endpoint for killing native Bertrand workloads."""

from __future__ import annotations

from typing import TYPE_CHECKING

from bertrand.env.cli.external._helper import resolve_project_worktree
from bertrand.env.config.core import Config
from bertrand.env.git import INFINITY
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.workload.project import kill_project_workload

if TYPE_CHECKING:
    from pathlib import Path

    from bertrand.env.kube.workload.controller import WorkloadKillResult


async def bertrand_kill(
    target: Path,
    *,
    grace_period_seconds: int = 10,
) -> None:
    """Kill active Kubernetes workload processes for a project target.

    Parameters
    ----------
    target : Path
        Project repository or worktree path. Repository roots target the worktree
        attached to HEAD.
    grace_period_seconds : int, optional
        Kubernetes pod termination grace period. Kubelet handles normal process
        termination and forceful kill after this window expires.

    Raises
    ------
    ValueError
        If `grace_period_seconds` is negative.
    """
    if grace_period_seconds < 0:
        msg = "kill timeout must be non-negative"
        raise ValueError(msg)

    repo, worktree = await resolve_project_worktree(target)
    with await Kube.host(timeout=INFINITY) as kube:
        config = await Config.load(worktree, kube=kube, repo=repo, timeout=INFINITY)
        async with config:
            result = await kill_project_workload(
                kube,
                config=config,
                repo_id=config.repo.repo_id,
                grace_period_seconds=grace_period_seconds,
                timeout=INFINITY,
            )
    _print_kill_result(result)


def _print_kill_result(result: WorkloadKillResult) -> None:
    if not result.changed:
        print("workload: nothing running")
        return
    if result.deployment_scaled:
        print(f"deployment: {result.workload} scaled to 0")
    if result.cronjob_suspended:
        print(f"cronjob: {result.workload} suspended")
    if result.jobs_deleted:
        print(f"jobs: deleted {', '.join(result.jobs_deleted)}")
    if result.pods_deleted:
        print(f"pods: terminating {', '.join(result.pods_deleted)}")
