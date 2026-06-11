"""Wrappers for the Kubernetes Job API and related execution operations."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass, replace
from typing import TYPE_CHECKING, Literal, Self

import kubernetes

from bertrand.env.git import Deadline, until

from .api.resource import (
    CreatableResource,
    KubeResource,
    Watchable,
    builtin_resource,
)
from .pod import Pod

if TYPE_CHECKING:
    import builtins
    from collections.abc import Awaitable, Callable, Mapping, Sequence
    from datetime import datetime

    from .api.client import Kube
    from .api.spec import PodTemplateSpec

JOB_WAIT_POLL_INTERVAL_SECONDS = 0.5
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
        "template": replace(pod_template, labels=template_labels)._manifest(),
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


@builtin_resource(api="batch", scope="namespaced", endpoint="job")
@dataclass(frozen=True)
class Job(
    KubeResource[kubernetes.client.V1Job],
    Watchable,
    CreatableResource,
):
    """General-purpose wrapper around one Kubernetes Job object.

    Parameters
    ----------
    _obj : kubernetes.client.V1Job
        Typed Kubernetes Job payload returned by the cluster API.

    Notes
    -----
    Jobs are one-off execution records. The public API intentionally exposes
    `create()` instead of `upsert()` because Job pod templates are effectively
    immutable once submitted.
    """

    _obj: kubernetes.client.V1Job

    @staticmethod
    def _manifest(
        *,
        namespace: str,
        name: str,
        labels: Mapping[str, str],
        pod_template: PodTemplateSpec,
        annotations: Mapping[str, str] | None,
        backoff_limit: int,
        ttl_seconds_after_finished: int | None,
        active_deadline_seconds: int | None,
        parallelism: int | None,
        completions: int | None,
        completion_mode: JobCompletionMode | None,
    ) -> dict[str, object]:
        return {
            "apiVersion": "batch/v1",
            "kind": "Job",
            "metadata": {
                "name": name,
                "namespace": namespace,
                "labels": dict(labels),
                "annotations": dict(annotations or {}),
            },
            "spec": _job_spec_manifest(
                labels=labels,
                pod_template=pod_template,
                backoff_limit=backoff_limit,
                ttl_seconds_after_finished=ttl_seconds_after_finished,
                active_deadline_seconds=active_deadline_seconds,
                parallelism=parallelism,
                completions=completions,
                completion_mode=completion_mode,
            ),
        }

    @classmethod
    async def create(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        labels: Mapping[str, str],
        pod_template: PodTemplateSpec,
        deadline: Deadline,
        backoff_limit: int = 0,
        ttl_seconds_after_finished: int | None = 3600,
        annotations: Mapping[str, str] | None = None,
        active_deadline_seconds: int | None = None,
        parallelism: int | None = None,
        completions: int | None = None,
        completion_mode: JobCompletionMode | None = None,
    ) -> Job:
        """Create one Kubernetes Job from intent-level fields.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the Job.
        name : str
            Job name to create.
        labels : Mapping[str, str]
            Labels to apply to the Job and pod template.
        pod_template : PodTemplateSpec
            Pod template to render into the Job.
        deadline : Deadline
            Maximum request budget in seconds. If infinite, wait indefinitely.
        backoff_limit : int, optional
            Kubernetes Job retry limit.
        ttl_seconds_after_finished : int | None, optional
            Optional TTL controller retention period for finished Jobs.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to `metadata.annotations`.
        active_deadline_seconds : int | None, optional
            Optional maximum Job runtime in seconds.
        parallelism : int | None, optional
            Optional maximum number of Pods the Job may run at once.
        completions : int | None, optional
            Optional number of successful Pod completions required.
        completion_mode : {"NonIndexed", "Indexed"} | None, optional
            Optional Kubernetes Job completion tracking mode.

        Returns
        -------
        Job
            Wrapped created Job.

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or the API call fails.
        """
        namespace = namespace.strip()
        name = name.strip()
        if not namespace or not name:
            msg = "Job create requires non-empty namespace and name"
            raise OSError(msg)
        _validate_job_execution(
            owner="Job",
            template_owner="Job",
            backoff_limit=backoff_limit,
            ttl_seconds_after_finished=ttl_seconds_after_finished,
            active_deadline_seconds=active_deadline_seconds,
            parallelism=parallelism,
            completions=completions,
            completion_mode=completion_mode,
        )

        manifest = cls._manifest(
            namespace=namespace,
            name=name,
            labels=labels,
            pod_template=pod_template,
            annotations=annotations,
            backoff_limit=backoff_limit,
            ttl_seconds_after_finished=ttl_seconds_after_finished,
            active_deadline_seconds=active_deadline_seconds,
            parallelism=parallelism,
            completions=completions,
            completion_mode=completion_mode,
        )
        return await cls.create_manifest(
            kube,
            namespace=namespace,
            name=name,
            manifest=manifest,
            deadline=deadline,
            malformed_message=(
                f"malformed Kubernetes Job payload while creating {name!r}"
            ),
        )

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

    @property
    def failed_condition(self) -> str | None:
        """Return the terminal failure message.

        Returns
        -------
        str | None
            Failure condition message or reason, if the Job has failed.
        """
        return self.failure_message

    async def pods(self, kube: Kube, *, deadline: Deadline) -> builtins.list[Pod]:
        """List Pods owned by this Job.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        list[Pod]
            Pods selected by Kubernetes' standard Job ownership labels.

        Raises
        ------
        OSError
            If this Job has incomplete Kubernetes metadata.
        """
        namespace = self.namespace
        name = self.name
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

    async def wait_complete(self, kube: Kube, *, deadline: Deadline) -> Job:
        """Wait until this Job succeeds or fails.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum wait time in seconds. Must be positive.

        Returns
        -------
        Job
            Fresh wrapper whose status reports at least one succeeded pod.

        Raises
        ------
        TimeoutError
            If the Job does not complete before `timeout`.
        OSError
            If this Job has incomplete Kubernetes metadata or fails.
        """
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = "cannot wait for Job completion with missing metadata.name/namespace"
            raise OSError(msg)
        current: Job = self

        async def complete(attempt_deadline: Deadline) -> Job:
            nonlocal current
            if current.succeeded > 0:
                return current
            failed_condition = current.failed_condition
            if failed_condition is not None:
                msg = f"Job {namespace}/{name} failed: {failed_condition}"
                raise OSError(msg)
            refreshed = await current.refresh(kube, deadline=attempt_deadline)
            if refreshed is None:
                msg = f"Job {namespace}/{name} disappeared while waiting for completion"
                raise OSError(msg)
            current = refreshed
            if current.succeeded > 0:
                return current
            failed_condition = current.failed_condition
            if failed_condition is not None:
                msg = f"Job {namespace}/{name} failed: {failed_condition}"
                raise OSError(msg)
            msg = f"Job {namespace}/{name} is not complete yet"
            raise TimeoutError(msg)

        try:
            return await until(
                complete,
                deadline=deadline,
                delay=JOB_WAIT_POLL_INTERVAL_SECONDS,
            )
        except TimeoutError as err:
            msg = f"timed out waiting for Job {namespace}/{name} completion"
            raise TimeoutError(msg) from err

    async def logs(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        tail_lines: int,
        failure_label: str,
        include_headers: bool = False,
    ) -> str:
        """Collect logs from pods owned by this Job.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum diagnostic budget in seconds.
        tail_lines : int
            Number of log lines to request from each pod.
        failure_label : str
            Human-readable label for diagnostic failures.
        include_headers : bool, optional
            Whether to prefix each pod's log chunk with `namespace/name`.

        Returns
        -------
        str
            Collected pod logs, or a diagnostic placeholder if logs cannot be read.
        """
        if deadline.remaining <= 0:
            return ""
        try:
            pods = await self.pods(kube, deadline=deadline)
            return await _pod_logs(
                kube,
                pods,
                deadline=deadline,
                tail_lines=tail_lines,
                include_headers=include_headers,
            )
        except (OSError, TimeoutError, ValueError) as err:
            return f"<failed to read {failure_label}: {err}>"

    async def pod_diagnostics(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        failure_label: str,
    ) -> str:
        """Collect status diagnostics from pods owned by this Job.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum diagnostic budget in seconds.
        failure_label : str
            Human-readable label for diagnostic failures.

        Returns
        -------
        str
            Newline-separated pod status diagnostics, or a diagnostic placeholder if
            pod status cannot be read.
        """
        if deadline.remaining <= 0:
            return ""
        try:
            pods = await self.pods(kube, deadline=deadline)
            return _pod_diagnostics(pods)
        except (OSError, TimeoutError, ValueError) as err:
            return f"<failed to read {failure_label}: {err}>"

    # TODO: I don't like `delete_quietly`, `wait_complete_with_diagnostics`, and  # noqa: FIX002
    # `run_observed` as names.  Can you think of better ones, or ideally ways to
    # eliminate these methods entirely and/or replace them with a more intuitive and
    # composable API?

    async def delete_quietly(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
        wait: bool = False,
    ) -> None:
        """Delete this Job, ignoring cleanup failures.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum cleanup budget in seconds.
        wait : bool, optional
            Whether to wait for the Job to disappear after deletion.
        """
        if deadline.remaining <= 0:
            return
        try:
            await self.delete(
                kube,
                deadline=deadline,
                propagation_policy="Foreground",
            )
            if wait:
                await self.wait_deleted(kube, deadline=deadline)
        except (OSError, TimeoutError):
            return

    async def wait_complete_with_diagnostics(
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
    ) -> Job:
        """Wait for this Job and enrich failures with logs and cleanup.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum completion budget in seconds.
        failure_context : str
            Failure message prefix used if the Job fails or times out.
        log_heading : str
            Heading inserted before collected diagnostic logs.
        log_failure_label : str
            Label used when diagnostic log collection itself fails.
        tail_lines : int
            Number of pod log lines to collect on failure.
        diagnostic_deadline : Deadline
            Maximum budget for failure log collection.
        cleanup_deadline : Deadline
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
            return await self.wait_complete(kube, deadline=deadline)
        except (OSError, TimeoutError) as err:
            logs = ""
            diagnostics = ""
            if diagnostic_deadline.remaining > 0:
                try:
                    pods = await self.pods(kube, deadline=diagnostic_deadline)
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
            await self.delete_quietly(kube, deadline=cleanup_deadline)
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

    async def run_observed(
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
            If the Job does not complete before `timeout`.
        OSError
            If the Job fails or disappears while waiting.
        """
        if observer is not None:
            await observer(self)
        try:
            await self.wait_complete_with_diagnostics(
                kube,
                deadline=deadline,
                failure_context=failure_context,
                log_heading=log_heading,
                log_failure_label=log_failure_label,
                tail_lines=tail_lines,
                diagnostic_deadline=diagnostic_deadline,
                cleanup_deadline=cleanup_deadline,
                include_log_headers=include_log_headers,
            )
        except TimeoutError:
            raise
        except OSError as err:
            raise OSError(str(err)) from err
        return await self.logs(
            kube,
            deadline=deadline,
            tail_lines=tail_lines,
            failure_label=log_failure_label,
            include_headers=include_log_headers,
        )
