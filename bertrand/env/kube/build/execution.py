"""Shared execution helpers for short-lived Kubernetes build Jobs."""

from __future__ import annotations

from typing import TYPE_CHECKING

from bertrand.env.git import Deadline

if TYPE_CHECKING:
    from collections.abc import Awaitable, Callable

    from bertrand.env.kube.api.client import Kube
    from bertrand.env.kube.job import Job


async def job_logs(
    kube: Kube,
    job: Job,
    *,
    timeout: float,
    tail_lines: int,
    failure_label: str,
    include_headers: bool = False,
) -> str:
    """Collect logs from pods owned by one Job.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    job : Job
        Job whose pods should be inspected.
    timeout : float
        Maximum diagnostic budget in seconds.
    tail_lines : int
        Number of log lines to request from each pod.
    failure_label : str
        Human-readable label for diagnostic failures.
    include_headers : bool, optional
        Whether to prefix each pod's log chunk with ``namespace/name``.

    Returns
    -------
    str
        Collected pod logs, or a diagnostic placeholder if logs cannot be read.
    """
    if timeout <= 0:
        return ""
    try:
        deadline = Deadline.from_timeout(
            timeout, message="timeout must be non-negative"
        )
        pods = await job.pods(kube, timeout=deadline.remaining())
        chunks: list[str] = []
        for pod in pods:
            remaining = deadline.remaining()
            if remaining <= 0:
                break
            log = await pod.logs(
                kube,
                timeout=remaining,
                tail_lines=tail_lines,
            )
            log = log.strip()
            if not log:
                continue
            if include_headers:
                chunks.append(f"--- {pod.namespace}/{pod.name} ---\n{log}")
            else:
                chunks.append(log)
        separator = "\n\n" if include_headers else "\n"
        return separator.join(chunks)
    except (OSError, TimeoutError, ValueError) as err:
        return f"<failed to read {failure_label}: {err}>"


async def job_pod_diagnostics(
    kube: Kube,
    job: Job,
    *,
    timeout: float,
    failure_label: str,
) -> str:
    """Collect status diagnostics from pods owned by one Job.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    job : Job
        Job whose pods should be inspected.
    timeout : float
        Maximum diagnostic budget in seconds.
    failure_label : str
        Human-readable label for diagnostic failures.

    Returns
    -------
    str
        Newline-separated pod status diagnostics, or a diagnostic placeholder if
        pod status cannot be read.
    """
    if timeout <= 0:
        return ""
    try:
        pods = await job.pods(kube, timeout=timeout)
        lines: list[str] = []
        for pod in pods:
            lines.extend(pod.status_diagnostics)
        return "\n".join(lines)
    except (OSError, TimeoutError, ValueError) as err:
        return f"<failed to read {failure_label}: {err}>"


async def delete_job(
    kube: Kube,
    job: Job,
    *,
    timeout: float,
    wait: bool = False,
) -> None:
    """Delete one Job, ignoring cleanup failures.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    job : Job
        Job to delete.
    timeout : float
        Maximum cleanup budget in seconds.
    wait : bool, optional
        Whether to wait for the Job to disappear after deletion.
    """
    if timeout <= 0:
        return
    try:
        deadline = Deadline.from_timeout(
            timeout, message="timeout must be non-negative"
        )
        await job.delete(
            kube,
            timeout=deadline.remaining(),
            propagation_policy="Foreground",
        )
        if wait:
            await job.wait_deleted(kube, timeout=deadline.remaining())
    except (OSError, TimeoutError):
        return


async def wait_job_complete(
    kube: Kube,
    job: Job,
    *,
    timeout: float,
    failure_context: str,
    log_heading: str,
    log_failure_label: str,
    tail_lines: int,
    diagnostic_timeout: float,
    cleanup_timeout: float,
    include_log_headers: bool = False,
) -> Job:
    """Wait for one Job and enrich failures with logs and cleanup.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    job : Job
        Job to wait on.
    timeout : float
        Maximum completion budget in seconds.
    failure_context : str
        Failure message prefix used if the Job fails or times out.
    log_heading : str
        Heading inserted before collected diagnostic logs.
    log_failure_label : str
        Label used when diagnostic log collection itself fails.
    tail_lines : int
        Number of pod log lines to collect on failure.
    diagnostic_timeout : float
        Maximum budget for failure log collection.
    cleanup_timeout : float
        Maximum budget for failed Job cleanup.
    include_log_headers : bool, optional
        Whether diagnostic logs should include pod headers.

    Returns
    -------
    Job
        Refreshed Job wrapper that completed successfully.

    Raises
    ------
    TimeoutError
        If the Job does not complete before `timeout`.
    OSError
        If the Job fails or disappears while waiting.
    """
    try:
        return await job.wait_complete(kube, timeout=timeout)
    except (OSError, TimeoutError) as err:
        logs = ""
        diagnostics = ""
        if diagnostic_timeout > 0:
            deadline = Deadline.from_timeout(
                diagnostic_timeout,
                message="diagnostic timeout must be non-negative",
            )
            logs = await job_logs(
                kube,
                job,
                timeout=deadline.remaining(),
                tail_lines=tail_lines,
                failure_label=log_failure_label,
                include_headers=include_log_headers,
            )
            diagnostics = await job_pod_diagnostics(
                kube,
                job,
                timeout=deadline.remaining(),
                failure_label="Job pod status diagnostics",
            )
        await delete_job(kube, job, timeout=cleanup_timeout)
        msg = f"{failure_context}: {err}"
        diagnostics = diagnostics.strip()
        if diagnostics:
            msg = f"{msg}\n\nPod status:\n{diagnostics}"
        logs = logs.strip()
        if logs:
            msg = f"{msg}\n\n{log_heading}:\n{logs}"
        if isinstance(err, TimeoutError):
            raise TimeoutError(msg) from err
        raise OSError(msg) from err


async def run_observed_job(
    kube: Kube,
    job: Job,
    *,
    timeout: float,
    failure_context: str,
    log_heading: str,
    log_failure_label: str,
    tail_lines: int,
    diagnostic_timeout: float,
    cleanup_timeout: float,
    include_log_headers: bool = False,
    observer: Callable[[Job], Awaitable[None]] | None = None,
) -> str:
    """Observe, wait for, and collect logs from one short-lived Job.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    job : Job
        Job to observe and wait on.
    timeout : float
        Maximum completion and success-log budget in seconds.
    failure_context : str
        Failure message prefix used if the Job fails or times out.
    log_heading : str
        Heading inserted before collected diagnostic logs.
    log_failure_label : str
        Label used when diagnostic or success log collection itself fails.
    tail_lines : int
        Number of pod log lines to collect.
    diagnostic_timeout : float
        Maximum budget for failure log collection.
    cleanup_timeout : float
        Maximum budget for failed Job cleanup.
    include_log_headers : bool, optional
        Whether collected logs should include pod headers.
    observer : Callable[[Job], Awaitable[None]] | None, optional
        Callback invoked after the Job is created and before waiting begins.

    Returns
    -------
    str
        Success logs collected from the completed Job.

    Raises
    ------
    TimeoutError
        If the Job does not complete before `timeout`.
    OSError
        If the Job fails or disappears while waiting.
    """
    if timeout <= 0:
        msg = "observed Job timeout must be non-negative"
        raise TimeoutError(msg)
    deadline = Deadline.from_timeout(
        timeout, message="timeout must be non-negative"
    )
    if observer is not None:
        await observer(job)
    try:
        await wait_job_complete(
            kube,
            job,
            timeout=deadline.remaining(),
            failure_context=failure_context,
            log_heading=log_heading,
            log_failure_label=log_failure_label,
            tail_lines=tail_lines,
            diagnostic_timeout=diagnostic_timeout,
            cleanup_timeout=cleanup_timeout,
            include_log_headers=include_log_headers,
        )
    except TimeoutError:
        raise
    except OSError as err:
        raise OSError(str(err)) from err
    return await job_logs(
        kube,
        job,
        timeout=deadline.remaining(),
        tail_lines=tail_lines,
        failure_label=log_failure_label,
        include_headers=include_log_headers,
    )
