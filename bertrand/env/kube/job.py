"""Wrappers for the Kubernetes Job API and related execution operations."""

from __future__ import annotations

import asyncio
import contextlib
from dataclasses import dataclass, replace
from typing import TYPE_CHECKING, Literal, Self

import kubernetes

from .api.resource import (
    KubeResource,
    namespaced_resource,
)
from .pod import Pod

if TYPE_CHECKING:
    import builtins
    from collections.abc import (
        Awaitable,
        Callable,
        Mapping,
        Sequence,
    )
    from datetime import datetime

    from bertrand.env.git import Deadline

    from .api.client import Kube
    from .api.manifest import PodTemplateSpec

type JobCompletionMode = Literal["NonIndexed", "Indexed"]


async def _pod_logs(
    kube: Kube,
    pods: Sequence[Pod],
    *,
    deadline: Deadline,
    tail_lines: int,
    include_headers: bool,
) -> str:
    ordered = tuple(sorted(pods, key=lambda pod: (pod.namespace, pod.name)))
    if not ordered or deadline.remaining <= 0:
        return ""
    results = await asyncio.gather(
        *(
            pod.logs(
                kube,
                deadline=deadline,
                tail_lines=tail_lines,
            )
            for pod in ordered
        ),
        return_exceptions=True,
    )
    chunks: list[str] = []
    for pod, result in zip(ordered, results, strict=True):
        if isinstance(result, BaseException):
            raise result
        log = result.strip()
        if not log:
            continue
        if include_headers:
            chunks.append(f"--- {pod.namespace}/{pod.name} ---\n{log}")
        else:
            chunks.append(log)
    separator = "\n\n" if include_headers else "\n"
    return separator.join(chunks)


def _pod_diagnostics(pods: Sequence[Pod]) -> str:
    lines: list[str] = []
    for pod in sorted(pods, key=lambda item: (item.namespace, item.name)):
        lines.extend(pod.status_diagnostics)
    return "\n".join(lines)


async def _job_pods(
    job: Job,
    kube: Kube,
    *,
    deadline: Deadline,
) -> builtins.list[Pod]:
    namespace = job.namespace
    name = job.name
    if not namespace or not name:
        msg = "cannot list Job pods with missing metadata.name/namespace"
        raise OSError(msg)
    pods = await Pod.list(
        kube,
        namespaces=(namespace,),
        labels={"batch.kubernetes.io/job-name": name},
        deadline=deadline,
    )
    if pods:
        return pods
    return await Pod.list(
        kube,
        namespaces=(namespace,),
        labels={"job-name": name},
        deadline=deadline,
    )


def _validate_job_execution(
    *,
    owner: Literal["Job", "CronJob"],
    template_owner: Literal["Job", "CronJob Job"],
    backoff_limit: int,
    ttl_seconds_after_finished: int | None,
    active_deadline_seconds: int | None,
    parallelism: int | None,
    completions: int | None,
    completion_mode: JobCompletionMode | None,
) -> None:
    if backoff_limit < 0:
        msg = f"{owner} backoff limit cannot be negative"
        raise ValueError(msg)
    if ttl_seconds_after_finished is not None and ttl_seconds_after_finished < 0:
        msg = f"{template_owner} TTL cannot be negative"
        raise ValueError(msg)
    if active_deadline_seconds is not None and active_deadline_seconds < 0:
        msg = f"{template_owner} active deadline cannot be negative"
        raise ValueError(msg)
    if parallelism is not None and parallelism <= 0:
        msg = f"{template_owner} parallelism must be positive"
        raise ValueError(msg)
    if completions is not None and completions <= 0:
        msg = f"{template_owner} completions must be positive"
        raise ValueError(msg)
    if completion_mode is not None and completion_mode not in (
        "NonIndexed",
        "Indexed",
    ):
        msg = f"invalid {template_owner} completion mode: {completion_mode!r}"
        raise ValueError(msg)


def _job_spec_manifest(
    *,
    labels: Mapping[str, str],
    pod_template: PodTemplateSpec,
    backoff_limit: int,
    ttl_seconds_after_finished: int | None,
    active_deadline_seconds: int | None,
    parallelism: int | None,
    completions: int | None,
    completion_mode: JobCompletionMode | None,
) -> dict[str, object]:
    template_labels = dict(labels)
    template_labels.update(pod_template.labels)
    if pod_template.restart_policy is None:
        pod_template = replace(pod_template, restart_policy="Never")
    spec: dict[str, object] = {
        "backoffLimit": backoff_limit,
        "template": replace(pod_template, labels=template_labels).manifest(),
    }
    if ttl_seconds_after_finished is not None:
        spec["ttlSecondsAfterFinished"] = ttl_seconds_after_finished
    if active_deadline_seconds is not None:
        spec["activeDeadlineSeconds"] = active_deadline_seconds
    if parallelism is not None:
        spec["parallelism"] = parallelism
    if completions is not None:
        spec["completions"] = completions
    if completion_mode is not None:
        spec["completionMode"] = completion_mode
    return spec


@dataclass(frozen=True)
class JobManifest:
    """Desired state for one Kubernetes Job."""

    namespace: str
    name: str
    labels: Mapping[str, str]
    pod_template: PodTemplateSpec
    annotations: Mapping[str, str] | None = None
    backoff_limit: int = 0
    ttl_seconds_after_finished: int | None = 3600
    active_deadline_seconds: int | None = None
    parallelism: int | None = None
    completions: int | None = None
    completion_mode: JobCompletionMode | None = None

    def manifest(self) -> Mapping[str, object]:
        """Render this desired state as a Kubernetes manifest.

        Returns
        -------
        Mapping[str, object]
            Kubernetes Job manifest payload.
        """
        _validate_job_execution(
            owner="Job",
            template_owner="Job",
            backoff_limit=self.backoff_limit,
            ttl_seconds_after_finished=self.ttl_seconds_after_finished,
            active_deadline_seconds=self.active_deadline_seconds,
            parallelism=self.parallelism,
            completions=self.completions,
            completion_mode=self.completion_mode,
        )
        return {
            "apiVersion": "batch/v1",
            "kind": "Job",
            "metadata": {
                "name": self.name,
                "namespace": self.namespace,
                "labels": dict(self.labels),
                "annotations": dict(self.annotations or {}),
            },
            "spec": _job_spec_manifest(
                labels=self.labels,
                pod_template=self.pod_template,
                backoff_limit=self.backoff_limit,
                ttl_seconds_after_finished=self.ttl_seconds_after_finished,
                active_deadline_seconds=self.active_deadline_seconds,
                parallelism=self.parallelism,
                completions=self.completions,
                completion_mode=self.completion_mode,
            ),
        }


@namespaced_resource(
    api=kubernetes.client.BatchV1Api,
    payload=kubernetes.client.V1Job,
    read=kubernetes.client.BatchV1Api.read_namespaced_job,
    list=kubernetes.client.BatchV1Api.list_namespaced_job,
    list_all=kubernetes.client.BatchV1Api.list_job_for_all_namespaces,
    create=kubernetes.client.BatchV1Api.create_namespaced_job,
    patch=kubernetes.client.BatchV1Api.patch_namespaced_job,
    delete=kubernetes.client.BatchV1Api.delete_namespaced_job,
)
@dataclass(frozen=True)
class Job(
    KubeResource[kubernetes.client.V1Job, JobManifest],
):
    """General-purpose wrapper around one Kubernetes Job object.

    Parameters
    ----------
    _obj : kubernetes.client.V1Job
        Typed Kubernetes Job payload returned by the cluster API.

    Notes
    -----
    Jobs are one-off execution records. The public API intentionally exposes
    inherited `create()` instead of `upsert()` because Job pod templates are
    effectively immutable once submitted.
    """

    _obj: kubernetes.client.V1Job

    @property
    def active(self) -> int:
        """Return the active pod count.

        Returns
        -------
        int
            Active pod count, or zero when unavailable.
        """
        status = self._obj.status
        return int(status.active or 0) if status is not None else 0

    @property
    def succeeded(self) -> int:
        """Return the succeeded pod count.

        Returns
        -------
        int
            Succeeded pod count, or zero when unavailable.
        """
        status = self._obj.status
        return int(status.succeeded or 0) if status is not None else 0

    @property
    def failed(self) -> int:
        """Return the failed pod count.

        Returns
        -------
        int
            Failed pod count, or zero when unavailable.
        """
        status = self._obj.status
        return int(status.failed or 0) if status is not None else 0

    @property
    def completion_time(self) -> datetime | None:
        """Return the Job completion timestamp.

        Returns
        -------
        datetime | None
            Job completion timestamp, if reported by Kubernetes.
        """
        status = self._obj.status
        return status.completion_time if status is not None else None

    @property
    def start_time(self) -> datetime | None:
        """Return the Job start timestamp.

        Returns
        -------
        datetime | None
            Job start timestamp, if reported by Kubernetes.
        """
        status = self._obj.status
        return status.start_time if status is not None else None

    @property
    def is_complete(self) -> bool:
        """Return whether the Job reports a complete condition.

        Returns
        -------
        bool
            `True` when Kubernetes reports a successful completion condition.
        """
        status = self._obj.status
        for condition in (status.conditions or []) if status is not None else []:
            if condition.type == "Complete" and condition.status == "True":
                return True
        return False

    @property
    def is_failed(self) -> bool:
        """Return whether the Job reports a failed condition.

        Returns
        -------
        bool
            `True` when Kubernetes reports a failed condition.
        """
        status = self._obj.status
        for condition in (status.conditions or []) if status is not None else []:
            if condition.type == "Failed" and condition.status == "True":
                return True
        return False

    @property
    def failure_message(self) -> str | None:
        """Return the terminal Job failure message.

        Returns
        -------
        str | None
            Failure condition message or reason, if the Job has failed.
        """
        status = self._obj.status
        for condition in (status.conditions or []) if status is not None else []:
            if condition.type == "Failed" and condition.status == "True":
                return condition.message or condition.reason or "Job failed"
        return None

    async def run(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        failure_context: str,
        log_heading: str,
        log_failure_label: str,
        tail_lines: int,
        diagnostic_deadline: Deadline,
        cleanup_deadline: Deadline,
        include_log_headers: bool = False,
        observer: Callable[[Self], Awaitable[None]] | None = None,
    ) -> str:
        """Observe, wait for, and collect logs from this short-lived Job.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum completion and success-log budget in seconds.
        failure_context : str
            Failure message prefix used if the Job fails or times out.
        log_heading : str
            Heading inserted before collected diagnostic logs.
        log_failure_label : str
            Label used when diagnostic or success log collection itself fails.
        tail_lines : int
            Number of pod log lines to collect.
        diagnostic_deadline : Deadline
            Maximum budget for failure log collection.
        cleanup_deadline : Deadline
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
            If the Job does not complete before `deadline`.
        OSError
            If the Job fails or disappears while waiting.
        """
        if observer is not None:
            await observer(self)
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = "cannot run Job with missing metadata.name/namespace"
            raise OSError(msg)

        def complete(live: Job | None) -> bool:
            if live is None:
                msg = f"Job {namespace}/{name} disappeared while waiting for completion"
                raise OSError(msg)
            failure = live.failure_message
            if failure is not None:
                msg = f"Job {namespace}/{name} failed: {failure}"
                raise OSError(msg)
            return live.succeeded > 0

        try:
            await self.wait(
                kube,
                deadline=deadline,
                predicate=complete,
                check_current=True,
            )
        except (OSError, TimeoutError) as err:
            if isinstance(err, TimeoutError):
                msg = f"timed out waiting for Job {namespace}/{name} completion"
                err = TimeoutError(msg)
            logs = ""
            diagnostics = ""
            if diagnostic_deadline.remaining > 0:
                try:
                    pods = await _job_pods(
                        self,
                        kube,
                        deadline=diagnostic_deadline,
                    )
                    diagnostics = _pod_diagnostics(pods)
                    try:
                        logs = await _pod_logs(
                            kube,
                            pods,
                            deadline=diagnostic_deadline,
                            tail_lines=tail_lines,
                            include_headers=include_log_headers,
                        )
                    except (OSError, TimeoutError, ValueError) as log_err:
                        logs = f"<failed to read {log_failure_label}: {log_err}>"
                except (OSError, TimeoutError, ValueError) as diagnostic_err:
                    logs = f"<failed to read {log_failure_label}: {diagnostic_err}>"
                    diagnostics = (
                        f"<failed to read Job pod status diagnostics: {diagnostic_err}>"
                    )
            if cleanup_deadline.remaining > 0:
                with contextlib.suppress(OSError, TimeoutError):
                    await self.delete(
                        kube,
                        deadline=cleanup_deadline,
                        propagation_policy="Foreground",
                    )
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

        if deadline.remaining <= 0:
            return ""
        try:
            pods = await _job_pods(self, kube, deadline=deadline)
            return await _pod_logs(
                kube,
                deadline=deadline,
                pods=pods,
                tail_lines=tail_lines,
                include_headers=include_log_headers,
            )
        except (OSError, TimeoutError, ValueError) as err:
            return f"<failed to read {log_failure_label}: {err}>"
