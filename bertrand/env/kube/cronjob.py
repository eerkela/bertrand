"""Wrappers for the Kubernetes CronJob API."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Literal, Self

import kubernetes

from .api.metadata import NamespacedKubeMetadata
from .api.resource import ResourceClient
from .job import JobCompletionMode, _job_spec_manifest

if TYPE_CHECKING:
    import builtins
    from collections.abc import AsyncIterator, Collection, Mapping
    from datetime import datetime

    from .api.client import Kube
    from .api.spec import PodTemplateSpec
    from .api.watch import WatchEvent

type CronJobConcurrencyPolicy = Literal["Allow", "Forbid", "Replace"]


@dataclass(frozen=True)
class CronJob(NamespacedKubeMetadata[kubernetes.client.V1CronJob]):
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

    @classmethod
    def _client(cls) -> ResourceClient[kubernetes.client.V1CronJob, Self]:
        return ResourceClient(
            scope="namespaced",
            kind="CronJob",
            expected=kubernetes.client.V1CronJob,
            list_type=kubernetes.client.V1CronJobList,
            wrapper=lambda payload: cls(_obj=payload),
            read=lambda kube, namespace, name, request_timeout: (
                kube.batch.read_namespaced_cron_job(
                    name=name,
                    namespace=namespace,
                    _request_timeout=request_timeout,
                )
            ),
            list_all=lambda kube, label_selector, field_selector, request_timeout: (
                kube.batch.list_cron_job_for_all_namespaces(
                    label_selector=label_selector,
                    field_selector=field_selector,
                    _request_timeout=request_timeout,
                )
            ),
            list_namespace=lambda kube, namespace, labels, fields, timeout: (
                kube.batch.list_namespaced_cron_job(
                    namespace=namespace,
                    label_selector=labels,
                    field_selector=fields,
                    _request_timeout=timeout,
                )
            ),
            create=lambda kube, namespace, _name, manifest, request_timeout: (
                kube.batch.create_namespaced_cron_job(
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                )
            ),
            patch=lambda kube, namespace, name, manifest, request_timeout: (
                kube.batch.patch_namespaced_cron_job(
                    name=name,
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                )
            ),
            delete=lambda kube, namespace, name, request_timeout: (
                kube.batch.delete_namespaced_cron_job(
                    name=name,
                    namespace=namespace,
                    _request_timeout=request_timeout,
                )
            ),
            watch_all=lambda kube: kube.batch.list_cron_job_for_all_namespaces,
            watch_namespace=lambda kube: kube.batch.list_namespaced_cron_job,
        )

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        timeout: float,
    ) -> Self | None:
        """Read one Kubernetes CronJob by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the CronJob.
        name : str
            CronJob name to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        CronJob | None
            Wrapped Kubernetes CronJob, or ``None`` if it does not exist.
        """
        return await cls._client().get(
            kube,
            namespace=namespace,
            name=name,
            timeout=timeout,
        )

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        timeout: float,
        namespaces: Collection[str] | None = None,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[Self]:
        """List Kubernetes CronJobs with optional namespace and label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        namespaces : Collection[str] | None, optional
            Optional namespace filters. ``None`` queries all namespaces.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.

        Returns
        -------
        list[CronJob]
            Wrapped Kubernetes CronJobs matching the requested filters.
        """
        return await cls._client().list(
            kube,
            timeout=timeout,
            namespaces=namespaces,
            labels=labels,
        )

    @classmethod
    async def watch(
        cls,
        kube: Kube,
        *,
        timeout: float,
        namespace: str | None = None,
        labels: Mapping[str, str] | None = None,
        field_selector: str | None = None,
        resource_version: str | None = None,
    ) -> AsyncIterator[WatchEvent[Self]]:
        """Watch Kubernetes CronJobs.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum watch budget in seconds. If infinite, wait indefinitely.
        namespace : str | None, optional
            Namespace to watch. If omitted, watches CronJobs across all namespaces.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.
        field_selector : str | None, optional
            Raw Kubernetes field selector.
        resource_version : str | None, optional
            Resource version to watch from.

        Yields
        ------
        WatchEvent[CronJob]
            Typed watch events containing wrapped CronJobs.
        """
        async for event in cls._client().watch(
            kube,
            timeout=timeout,
            namespace=namespace,
            labels=labels,
            field_selector=field_selector,
            resource_version=resource_version,
        ):
            yield event

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
        concurrency_policy: CronJobConcurrencyPolicy,
        suspend: bool | None,
        starting_deadline_seconds: int | None,
        successful_jobs_history_limit: int | None,
        failed_jobs_history_limit: int | None,
        time_zone: str | None,
        active_deadline_seconds: int | None,
        parallelism: int | None,
        completions: int | None,
        completion_mode: JobCompletionMode | None,
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
        timeout: float,
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
    ) -> Self:
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
        timeout : float
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
        if backoff_limit < 0:
            msg = "CronJob backoff limit cannot be negative"
            raise ValueError(msg)
        if ttl_seconds_after_finished is not None and ttl_seconds_after_finished < 0:
            msg = "CronJob Job TTL cannot be negative"
            raise ValueError(msg)
        for label, value in (
            ("starting deadline", starting_deadline_seconds),
            ("successful jobs history limit", successful_jobs_history_limit),
            ("failed jobs history limit", failed_jobs_history_limit),
        ):
            if value is not None and value < 0:
                msg = f"CronJob {label} cannot be negative"
                raise ValueError(msg)
        if active_deadline_seconds is not None and active_deadline_seconds < 0:
            msg = "CronJob Job active deadline cannot be negative"
            raise ValueError(msg)
        if parallelism is not None and parallelism <= 0:
            msg = "CronJob Job parallelism must be positive"
            raise ValueError(msg)
        if completions is not None and completions <= 0:
            msg = "CronJob Job completions must be positive"
            raise ValueError(msg)
        if completion_mode is not None and completion_mode not in (
            "NonIndexed",
            "Indexed",
        ):
            msg = f"invalid CronJob Job completion mode: {completion_mode!r}"
            raise ValueError(msg)

        manifest = cls._manifest(
            namespace=namespace,
            name=name,
            labels=labels,
            pod_template=pod_template,
            schedule=schedule,
            annotations=annotations,
            backoff_limit=backoff_limit,
            ttl_seconds_after_finished=ttl_seconds_after_finished,
            concurrency_policy=concurrency_policy,
            suspend=suspend,
            starting_deadline_seconds=starting_deadline_seconds,
            successful_jobs_history_limit=successful_jobs_history_limit,
            failed_jobs_history_limit=failed_jobs_history_limit,
            time_zone=time_zone,
            active_deadline_seconds=active_deadline_seconds,
            parallelism=parallelism,
            completions=completions,
            completion_mode=completion_mode,
        )
        return await cls._client().upsert(
            kube,
            namespace=namespace,
            name=name,
            manifest=manifest,
            timeout=timeout,
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

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this CronJob by its metadata namespace and name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        CronJob | None
            Fresh wrapper for the same CronJob, or ``None`` if it no longer exists.
        """
        namespace, name = self._require_namespace_name("refresh CronJob")
        return await type(self).get(
            kube,
            namespace=namespace,
            name=name,
            timeout=timeout,
        )

    async def suspend(self, kube: Kube, *, suspend: bool, timeout: float) -> Self:
        """Patch this CronJob's suspend state.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        suspend : bool
            Desired value for Kubernetes `spec.suspend`.
        timeout : float
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
        namespace, name = self._require_namespace_name("suspend CronJob")
        payload = await kube.run(
            lambda request_timeout: kube.batch.patch_namespaced_cron_job(
                name=name,
                namespace=namespace,
                body={"spec": {"suspend": suspend}},
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to patch CronJob {namespace}/{name} suspend state",
        )
        if not isinstance(payload, kubernetes.client.V1CronJob):
            msg = f"malformed Kubernetes CronJob payload while patching {name!r}"
            raise OSError(msg)
        return type(self)(_obj=payload)

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this CronJob from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        namespace, name = self._require_namespace_name("delete CronJob")
        await (
            type(self)
            ._client()
            .delete_by_name(
                kube,
                namespace=namespace,
                name=name,
                timeout=timeout,
            )
        )

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this CronJob is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.
        """
        namespace, name = self._require_namespace_name("wait for CronJob deletion")
        await (
            type(self)
            ._client()
            .wait_deleted(
                label=self._object_label(name=name, namespace=namespace),
                timeout=timeout,
                refresh=lambda remaining: self.refresh(kube, timeout=remaining),
            )
        )
