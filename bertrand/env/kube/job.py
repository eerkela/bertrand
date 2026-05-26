"""Wrappers for the Kubernetes Job API and related execution operations."""

from __future__ import annotations

from dataclasses import dataclass, replace
from typing import TYPE_CHECKING, ClassVar, Literal, Self

import kubernetes

from bertrand.env.git import until

from .api._helpers import (
    DeletionPropagationPolicy,
    _delete_options,
    _validate_delete_status,
)
from .api.metadata import NamespacedKubeMetadata
from .api.resource import BuiltinResource, BuiltinResourceObject
from .pod import Pod

if TYPE_CHECKING:
    import builtins
    from collections.abc import Mapping
    from datetime import datetime

    from .api.client import Kube
    from .api.spec import PodTemplateSpec

JOB_WAIT_POLL_INTERVAL_SECONDS = 0.5
type JobCompletionMode = Literal["NonIndexed", "Indexed"]


@dataclass(frozen=True)
class _JobExecutionFields:
    """Validated execution fields shared by Jobs and CronJob job templates."""

    backoff_limit: int
    ttl_seconds_after_finished: int | None
    active_deadline_seconds: int | None
    parallelism: int | None
    completions: int | None
    completion_mode: JobCompletionMode | None

    @classmethod
    def validate(
        cls,
        *,
        owner: Literal["Job", "CronJob"],
        template_owner: Literal["Job", "CronJob Job"],
        backoff_limit: int,
        ttl_seconds_after_finished: int | None,
        active_deadline_seconds: int | None,
        parallelism: int | None,
        completions: int | None,
        completion_mode: JobCompletionMode | None,
    ) -> Self:
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
        return cls(
            backoff_limit=backoff_limit,
            ttl_seconds_after_finished=ttl_seconds_after_finished,
            active_deadline_seconds=active_deadline_seconds,
            parallelism=parallelism,
            completions=completions,
            completion_mode=completion_mode,
        )


def _job_spec_manifest(
    *,
    labels: Mapping[str, str],
    pod_template: PodTemplateSpec,
    execution: _JobExecutionFields,
) -> dict[str, object]:
    template_labels = dict(labels)
    template_labels.update(pod_template.labels)
    if pod_template.restart_policy is None:
        pod_template = replace(pod_template, restart_policy="Never")
    spec: dict[str, object] = {
        "backoffLimit": execution.backoff_limit,
        "template": replace(pod_template, labels=template_labels)._manifest(),
    }
    if execution.ttl_seconds_after_finished is not None:
        spec["ttlSecondsAfterFinished"] = execution.ttl_seconds_after_finished
    if execution.active_deadline_seconds is not None:
        spec["activeDeadlineSeconds"] = execution.active_deadline_seconds
    if execution.parallelism is not None:
        spec["parallelism"] = execution.parallelism
    if execution.completions is not None:
        spec["completions"] = execution.completions
    if execution.completion_mode is not None:
        spec["completionMode"] = execution.completion_mode
    return spec


@dataclass(frozen=True)
class Job(
    BuiltinResourceObject[kubernetes.client.V1Job],
    NamespacedKubeMetadata[kubernetes.client.V1Job],
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

    resource: ClassVar[BuiltinResource[kubernetes.client.V1Job]] = (
        BuiltinResource.namespaced(
            api="batch",
            kind="Job",
            slug="job",
            expected=kubernetes.client.V1Job,
            list_type=kubernetes.client.V1JobList,
            create=True,
            watch=True,
        )
    )

    @staticmethod
    def _manifest(
        *,
        namespace: str,
        name: str,
        labels: Mapping[str, str],
        pod_template: PodTemplateSpec,
        annotations: Mapping[str, str] | None,
        execution: _JobExecutionFields,
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
                execution=execution,
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
        timeout: float,
        backoff_limit: int = 0,
        ttl_seconds_after_finished: int | None = 3600,
        annotations: Mapping[str, str] | None = None,
        active_deadline_seconds: int | None = None,
        parallelism: int | None = None,
        completions: int | None = None,
        completion_mode: JobCompletionMode | None = None,
    ) -> Self:
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
        timeout : float
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
        execution = _JobExecutionFields.validate(
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
            execution=execution,
        )
        return await cls.resource.create_manifest(
            kube,
            owner=cls,
            namespace=namespace,
            name=name,
            manifest=manifest,
            timeout=timeout,
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

    async def pods(self, kube: Kube, *, timeout: float) -> builtins.list[Pod]:
        """List Pods owned by this Job.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        list[Pod]
            Pods selected by Kubernetes' standard Job ownership labels.
        """
        namespace, name = self._require_namespace_name("list Job pods")
        pods = await Pod.list(
            kube,
            namespaces=(namespace,),
            labels={"batch.kubernetes.io/job-name": name},
            timeout=timeout,
        )
        if pods:
            return pods
        return await Pod.list(
            kube,
            namespaces=(namespace,),
            labels={"job-name": name},
            timeout=timeout,
        )

    async def delete(
        self,
        kube: Kube,
        *,
        timeout: float,
        propagation_policy: DeletionPropagationPolicy = "Background",
        grace_period_seconds: int | None = None,
    ) -> None:
        """Delete this Job from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        propagation_policy : {"Background", "Foreground", "Orphan"}, optional
            Kubernetes deletion propagation policy.
        grace_period_seconds : int | None, optional
            Optional Kubernetes deletion grace period.

        """
        namespace, name = self._require_namespace_name("delete Job")
        delete_options = _delete_options(
            kind="Job",
            propagation_policy=propagation_policy,
            grace_period_seconds=grace_period_seconds,
        )
        payload = await kube.run(
            lambda request_timeout: kube.batch.delete_namespaced_job(
                name=name,
                namespace=namespace,
                body=delete_options,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete Job {namespace}/{name}",
        )
        _validate_delete_status(
            payload, label=self._object_label(name=name, namespace=namespace)
        )

    async def wait_complete(self, kube: Kube, *, timeout: float) -> Self:
        """Wait until this Job succeeds or fails.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        Returns
        -------
        Job
            Fresh wrapper whose status reports at least one succeeded pod.

        Raises
        ------
        TimeoutError
            If the Job does not complete before `timeout`.
        """
        namespace, name = self._require_namespace_name("wait for Job completion")
        current: Self = self

        async def complete(remaining: float) -> Self:
            nonlocal current
            if current.succeeded > 0:
                return current
            failed_condition = current.failed_condition
            if failed_condition is not None:
                msg = f"Job {namespace}/{name} failed: {failed_condition}"
                raise OSError(msg)
            refreshed = await current.refresh(kube, timeout=remaining)
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
                timeout=timeout,
                interval=JOB_WAIT_POLL_INTERVAL_SECONDS,
                action=f"waiting for Job {namespace}/{name} completion",
            )
        except TimeoutError as err:
            msg = f"timed out waiting for Job {namespace}/{name} completion"
            raise TimeoutError(msg) from err
