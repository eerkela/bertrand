"""Wrappers for the Kubernetes CronJob API."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Literal

import kubernetes

from .api.resource import (
    KubeResource,
    namespaced_resource,
)
from .job import JobCompletionMode, _job_spec_manifest, _validate_job_execution

if TYPE_CHECKING:
    from collections.abc import Mapping
    from datetime import datetime

    from bertrand.env.git import Deadline

    from .api.client import Kube
    from .api.manifest import PodTemplateSpec

type CronJobConcurrencyPolicy = Literal["Allow", "Forbid", "Replace"]


@dataclass(frozen=True)
class CronJobManifest:
    """Desired state for one Kubernetes CronJob."""

    namespace: str
    name: str
    labels: Mapping[str, str]
    pod_template: PodTemplateSpec
    schedule: str
    annotations: Mapping[str, str] | None = None
    backoff_limit: int = 0
    ttl_seconds_after_finished: int | None = 3600
    concurrency_policy: CronJobConcurrencyPolicy = "Forbid"
    suspend: bool | None = None
    starting_deadline_seconds: int | None = None
    successful_jobs_history_limit: int | None = None
    failed_jobs_history_limit: int | None = None
    time_zone: str | None = None
    active_deadline_seconds: int | None = None
    parallelism: int | None = None
    completions: int | None = None
    completion_mode: JobCompletionMode | None = None

    def manifest(self) -> Mapping[str, object]:
        """Render this desired state as a Kubernetes manifest.

        Returns
        -------
        Mapping[str, object]
            Kubernetes CronJob manifest payload.

        Raises
        ------
        ValueError
            If schedule is empty or history/deadline fields are negative.
        """
        schedule = self.schedule.strip()
        if not schedule:
            msg = "CronJob schedule cannot be empty"
            raise ValueError(msg)
        for label, value in (
            ("starting deadline", self.starting_deadline_seconds),
            ("successful jobs history limit", self.successful_jobs_history_limit),
            ("failed jobs history limit", self.failed_jobs_history_limit),
        ):
            if value is not None and value < 0:
                msg = f"CronJob {label} cannot be negative"
                raise ValueError(msg)
        _validate_job_execution(
            owner="CronJob",
            template_owner="CronJob Job",
            backoff_limit=self.backoff_limit,
            ttl_seconds_after_finished=self.ttl_seconds_after_finished,
            active_deadline_seconds=self.active_deadline_seconds,
            parallelism=self.parallelism,
            completions=self.completions,
            completion_mode=self.completion_mode,
        )
        spec: dict[str, object] = {
            "schedule": schedule,
            "concurrencyPolicy": self.concurrency_policy,
            "jobTemplate": {
                "metadata": {
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
            },
        }
        optional: dict[str, object | None] = {
            "suspend": self.suspend,
            "startingDeadlineSeconds": self.starting_deadline_seconds,
            "successfulJobsHistoryLimit": self.successful_jobs_history_limit,
            "failedJobsHistoryLimit": self.failed_jobs_history_limit,
            "timeZone": self.time_zone,
        }
        spec.update(
            {key: value for key, value in optional.items() if value is not None}
        )
        return {
            "apiVersion": "batch/v1",
            "kind": "CronJob",
            "metadata": {
                "name": self.name,
                "namespace": self.namespace,
                "labels": dict(self.labels),
                "annotations": dict(self.annotations or {}),
            },
            "spec": spec,
        }


@namespaced_resource(
    api=kubernetes.client.BatchV1Api,
    payload=kubernetes.client.V1CronJob,
    read=kubernetes.client.BatchV1Api.read_namespaced_cron_job,
    list=kubernetes.client.BatchV1Api.list_namespaced_cron_job,
    list_all=kubernetes.client.BatchV1Api.list_cron_job_for_all_namespaces,
    create=kubernetes.client.BatchV1Api.create_namespaced_cron_job,
    patch=kubernetes.client.BatchV1Api.patch_namespaced_cron_job,
    delete=kubernetes.client.BatchV1Api.delete_namespaced_cron_job,
)
@dataclass(frozen=True)
class CronJob(
    KubeResource[kubernetes.client.V1CronJob, CronJobManifest],
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
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = "cannot suspend CronJob with missing metadata.name/namespace"
            raise OSError(msg)
        api = kubernetes.client.BatchV1Api(kube.client)
        payload = await kube.run(
            lambda request_timeout: api.patch_namespaced_cron_job(
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
