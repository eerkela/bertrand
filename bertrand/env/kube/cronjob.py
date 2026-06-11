"""Wrappers for the Kubernetes CronJob API."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Literal

import kubernetes

from .api.resource import (
    Creatable,
    Deletable,
    Listable,
    Patchable,
    Readable,
    Upsertable,
    Watchable,
    _resource_namespace_name,
    builtin_resource,
)
from .job import JobCompletionMode, _job_spec_manifest, _validate_job_execution

if TYPE_CHECKING:
    from collections.abc import Mapping
    from datetime import datetime

    from bertrand.env.git import Deadline

    from .api.client import Kube
    from .api.spec import PodTemplateSpec

type CronJobConcurrencyPolicy = Literal["Allow", "Forbid", "Replace"]


@builtin_resource(api="batch", scope="namespaced")
@dataclass(frozen=True)
class CronJob(
    Readable[kubernetes.client.V1CronJob],
    Listable[kubernetes.client.V1CronJob],
    Creatable[kubernetes.client.V1CronJob],
    Patchable[kubernetes.client.V1CronJob],
    Upsertable[kubernetes.client.V1CronJob],
    Deletable[kubernetes.client.V1CronJob],
    Watchable[kubernetes.client.V1CronJob],
):
    """General-purpose wrapper around one Kubernetes CronJob object.

    Parameters
    ----------
    _obj : kubernetes.client.V1CronJob
        Typed Kubernetes CronJob payload returned by the cluster API.

    Notes
    -----
    The convergence API accepts intent-level fields and keeps raw Kubernetes
    manifests as an internal implementation detail.
    """

    _obj: kubernetes.client.V1CronJob

    @staticmethod
    def _manifest(
        *,
        namespace: str,
        name: str,
        labels: Mapping[str, str],
        pod_template: PodTemplateSpec,
        schedule: str,
        annotations: Mapping[str, str] | None,
        backoff_limit: int,
        ttl_seconds_after_finished: int | None,
        active_deadline_seconds: int | None,
        parallelism: int | None,
        completions: int | None,
        completion_mode: JobCompletionMode | None,
        concurrency_policy: CronJobConcurrencyPolicy,
        suspend: bool | None,
        starting_deadline_seconds: int | None,
        successful_jobs_history_limit: int | None,
        failed_jobs_history_limit: int | None,
        time_zone: str | None,
    ) -> dict[str, object]:
        spec: dict[str, object] = {
            "schedule": schedule,
            "concurrencyPolicy": concurrency_policy,
            "jobTemplate": {
                "metadata": {
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
            },
        }
        optional: dict[str, object | None] = {
            "suspend": suspend,
            "startingDeadlineSeconds": starting_deadline_seconds,
            "successfulJobsHistoryLimit": successful_jobs_history_limit,
            "failedJobsHistoryLimit": failed_jobs_history_limit,
            "timeZone": time_zone,
        }
        spec.update(
            {key: value for key, value in optional.items() if value is not None}
        )

        return {
            "apiVersion": "batch/v1",
            "kind": "CronJob",
            "metadata": {
                "name": name,
                "namespace": namespace,
                "labels": dict(labels),
                "annotations": dict(annotations or {}),
            },
            "spec": spec,
        }

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        labels: Mapping[str, str],
        pod_template: PodTemplateSpec,
        schedule: str,
        deadline: Deadline,
        annotations: Mapping[str, str] | None = None,
        backoff_limit: int = 0,
        ttl_seconds_after_finished: int | None = 3600,
        concurrency_policy: CronJobConcurrencyPolicy = "Forbid",
        suspend: bool | None = None,
        starting_deadline_seconds: int | None = None,
        successful_jobs_history_limit: int | None = None,
        failed_jobs_history_limit: int | None = None,
        time_zone: str | None = None,
        active_deadline_seconds: int | None = None,
        parallelism: int | None = None,
        completions: int | None = None,
        completion_mode: JobCompletionMode | None = None,
    ) -> CronJob:
        """Create or patch one Kubernetes CronJob from intent-level fields.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the CronJob.
        name : str
            CronJob name to create or patch.
        labels : Mapping[str, str]
            Labels to apply to the CronJob, Job template, and Pod template.
        pod_template : PodTemplateSpec
            Pod template to render into the CronJob's Job template.
        schedule : str
            Cron schedule string.
        deadline : Deadline
            Maximum request budget in seconds. If infinite, wait indefinitely.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to the CronJob and Job template.
        backoff_limit : int, optional
            Kubernetes Job retry limit for created Jobs.
        ttl_seconds_after_finished : int | None, optional
            Optional TTL controller retention period for finished Jobs.
        concurrency_policy : {"Allow", "Forbid", "Replace"}, optional
            Kubernetes CronJob concurrency policy.
        suspend : bool | None, optional
            Whether to suspend future scheduled Jobs.
        starting_deadline_seconds : int | None, optional
            Optional deadline for starting missed Jobs.
        successful_jobs_history_limit : int | None, optional
            Optional number of successful Jobs to retain.
        failed_jobs_history_limit : int | None, optional
            Optional number of failed Jobs to retain.
        time_zone : str | None, optional
            Optional IANA time zone name for schedule interpretation.
        active_deadline_seconds : int | None, optional
            Optional maximum runtime in seconds for Jobs created by this CronJob.
        parallelism : int | None, optional
            Optional maximum concurrent Pods for Jobs created by this CronJob.
        completions : int | None, optional
            Optional successful Pod completions required for each Job.
        completion_mode : {"NonIndexed", "Indexed"} | None, optional
            Optional Job completion tracking mode for created Jobs.

        Returns
        -------
        CronJob
            Wrapped created or patched CronJob.

        Raises
        ------
        ValueError
            If retry, TTL, or schedule settings are invalid.
        OSError
            If Kubernetes returns malformed data or the API call fails.
        """
        namespace = namespace.strip()
        name = name.strip()
        schedule = schedule.strip()
        if not namespace or not name:
            msg = "CronJob upsert requires non-empty namespace and name"
            raise OSError(msg)
        if not schedule:
            msg = "CronJob schedule cannot be empty"
            raise ValueError(msg)
        for label, value in (
            ("starting deadline", starting_deadline_seconds),
            ("successful jobs history limit", successful_jobs_history_limit),
            ("failed jobs history limit", failed_jobs_history_limit),
        ):
            if value is not None and value < 0:
                msg = f"CronJob {label} cannot be negative"
                raise ValueError(msg)
        _validate_job_execution(
            owner="CronJob",
            template_owner="CronJob Job",
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
            schedule=schedule,
            annotations=annotations,
            backoff_limit=backoff_limit,
            ttl_seconds_after_finished=ttl_seconds_after_finished,
            active_deadline_seconds=active_deadline_seconds,
            parallelism=parallelism,
            completions=completions,
            completion_mode=completion_mode,
            concurrency_policy=concurrency_policy,
            suspend=suspend,
            starting_deadline_seconds=starting_deadline_seconds,
            successful_jobs_history_limit=successful_jobs_history_limit,
            failed_jobs_history_limit=failed_jobs_history_limit,
            time_zone=time_zone,
        )
        return await cls.upsert_manifest(
            kube,
            namespace=namespace,
            name=name,
            manifest=manifest,
            deadline=deadline,
        )

    @property
    def active(self) -> int:
        """Return the active Job reference count.

        Returns
        -------
        int
            Active Job reference count, or zero when unavailable.
        """
        status = self._obj.status
        return len(status.active or []) if status is not None else 0

    @property
    def suspended(self) -> bool:
        """Return whether future CronJob schedules are suspended.

        Returns
        -------
        bool
            `True` when Kubernetes `spec.suspend` is truthy.
        """
        spec = self._obj.spec
        return bool(spec.suspend) if spec is not None else False

    @property
    def last_schedule_time(self) -> datetime | None:
        """Return the last schedule timestamp.

        Returns
        -------
        datetime | None
            Last schedule time, if reported by Kubernetes.
        """
        status = self._obj.status
        return status.last_schedule_time if status is not None else None

    @property
    def last_successful_time(self) -> datetime | None:
        """Return the last successful Job timestamp.

        Returns
        -------
        datetime | None
            Last successful time, if reported by Kubernetes.
        """
        status = self._obj.status
        return status.last_successful_time if status is not None else None

    async def suspend(
        self, kube: Kube, *, suspend: bool, deadline: Deadline
    ) -> CronJob:
        """Patch this CronJob's suspend state.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        suspend : bool
            Desired value for Kubernetes `spec.suspend`.
        deadline : Deadline
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        CronJob
            Fresh wrapper after Kubernetes accepts the suspend patch.

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or the CronJob disappears after
            patching.
        """
        namespace, name = _resource_namespace_name(self, "suspend CronJob")
        payload = await kube.run(
            lambda request_timeout: kube.batch.patch_namespaced_cron_job(
                name=name,
                namespace=namespace,
                body={"spec": {"suspend": suspend}},
                _request_timeout=request_timeout,
            ),
            deadline=deadline,
            context=f"failed to patch CronJob {namespace}/{name} suspend state",
        )
        if not isinstance(payload, kubernetes.client.V1CronJob):
            msg = f"malformed Kubernetes CronJob payload while patching {name!r}"
            raise OSError(msg)
        return type(self)(_obj=payload)
