"""Wrappers for the Kubernetes Job API and related execution operations."""

from __future__ import annotations

from dataclasses import dataclass, replace
from typing import TYPE_CHECKING, Literal, Self

import kubernetes

from bertrand.env.git import until

from .api._helpers import (
    _validate_delete_status,
)
from .api._render import (
    _pod_template_manifest,
)
from .api.metadata import NamespacedKubeMetadata
from .api.resource import ResourceClient

if TYPE_CHECKING:
    import builtins
    from collections.abc import AsyncIterator, Collection, Mapping
    from datetime import datetime

    from bertrand.env.kube.pod import Pod

    from .api.client import Kube
    from .api.spec import PodTemplateSpec
    from .api.watch import WatchEvent

JOB_WAIT_POLL_INTERVAL_SECONDS = 0.5
type DeletionPropagationPolicy = Literal["Background", "Foreground", "Orphan"]


def _job_spec_manifest(
    *,
    labels: Mapping[str, str],
    pod_template: PodTemplateSpec,
    backoff_limit: int,
    ttl_seconds_after_finished: int | None,
) -> dict[str, object]:
    template_labels = dict(labels)
    template_labels.update(pod_template.labels)
    if pod_template.restart_policy is None:
        pod_template = replace(pod_template, restart_policy="Never")
    spec: dict[str, object] = {
        "backoffLimit": backoff_limit,
        "template": _pod_template_manifest(
            replace(pod_template, labels=template_labels),
        ),
    }
    if ttl_seconds_after_finished is not None:
        spec["ttlSecondsAfterFinished"] = ttl_seconds_after_finished
    return spec


@dataclass(frozen=True)
class Job(NamespacedKubeMetadata[kubernetes.client.V1Job]):
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

    @classmethod
    def _client(cls) -> ResourceClient[kubernetes.client.V1Job, Self]:
        return ResourceClient(
            scope="namespaced",
            kind="Job",
            expected=kubernetes.client.V1Job,
            list_type=kubernetes.client.V1JobList,
            wrapper=lambda payload: cls(_obj=payload),
            read=lambda kube, namespace, name, request_timeout: (
                kube.batch.read_namespaced_job(
                    name=name,
                    namespace=namespace,
                    _request_timeout=request_timeout,
                )
            ),
            list_all=lambda kube, label_selector, field_selector, request_timeout: (
                kube.batch.list_job_for_all_namespaces(
                    label_selector=label_selector,
                    field_selector=field_selector,
                    _request_timeout=request_timeout,
                )
            ),
            list_namespace=lambda kube, namespace, labels, fields, timeout: (
                kube.batch.list_namespaced_job(
                    namespace=namespace,
                    label_selector=labels,
                    field_selector=fields,
                    _request_timeout=timeout,
                )
            ),
            watch_all=lambda kube: kube.batch.list_job_for_all_namespaces,
            watch_namespace=lambda kube: kube.batch.list_namespaced_job,
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
        """Read one Kubernetes Job by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the Job.
        name : str
            Job name to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Job | None
            Wrapped Kubernetes Job, or `None` if it does not exist.
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
        """List Kubernetes Jobs with optional namespace and label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        namespaces : Collection[str] | None, optional
            Optional namespace filters. `None` queries all namespaces. Otherwise,
            entries are trimmed, deduplicated, and queried individually.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.

        Returns
        -------
        list[Job]
            Wrapped Kubernetes Jobs matching the requested filters.
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
        """Watch Kubernetes Jobs.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum watch budget in seconds. If infinite, wait indefinitely.
        namespace : str | None, optional
            Namespace to watch. If omitted, watches Jobs across all namespaces.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.
        field_selector : str | None, optional
            Raw Kubernetes field selector.
        resource_version : str | None, optional
            Resource version to watch from.

        Yields
        ------
        WatchEvent[Job]
            Typed watch events containing wrapped Jobs.
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
        backoff_limit: int,
        ttl_seconds_after_finished: int | None,
        annotations: Mapping[str, str] | None,
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

        Returns
        -------
        Job
            Wrapped created Job.

        Raises
        ------
        ValueError
            If retry or TTL settings are invalid.
        OSError
            If Kubernetes returns malformed data or the API call fails.
        """
        namespace = namespace.strip()
        name = name.strip()
        if not namespace or not name:
            msg = "Job create requires non-empty namespace and name"
            raise OSError(msg)
        if backoff_limit < 0:
            msg = "Job backoff limit cannot be negative"
            raise ValueError(msg)
        if ttl_seconds_after_finished is not None and ttl_seconds_after_finished < 0:
            msg = "Job TTL cannot be negative"
            raise ValueError(msg)

        manifest = cls._manifest(
            namespace=namespace,
            name=name,
            labels=labels,
            pod_template=pod_template,
            backoff_limit=backoff_limit,
            ttl_seconds_after_finished=ttl_seconds_after_finished,
            annotations=annotations,
        )
        created = await kube.run(
            lambda request_timeout: kube.batch.create_namespaced_job(
                namespace=namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to create Job {namespace}/{name}",
        )
        if not isinstance(created, kubernetes.client.V1Job):
            msg = f"malformed Kubernetes Job payload while creating {name!r}"
            raise OSError(msg)
        return cls(_obj=created)

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
        from bertrand.env.kube.pod import Pod

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

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this Job by its metadata namespace and name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Job | None
            Fresh wrapper for the same Job, or `None` if it no longer exists.
        """
        namespace, name = self._require_namespace_name("refresh Job")
        return await type(self).get(
            kube,
            namespace=namespace,
            name=name,
            timeout=timeout,
        )

    async def delete(
        self,
        kube: Kube,
        *,
        timeout: float,
        propagation_policy: DeletionPropagationPolicy = "Background",
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

        Raises
        ------
        ValueError
            If `propagation_policy` is invalid.
        """
        namespace, name = self._require_namespace_name("delete Job")
        if propagation_policy not in ("Background", "Foreground", "Orphan"):
            msg = f"invalid Job deletion propagation policy: {propagation_policy!r}"
            raise ValueError(msg)
        payload = await kube.run(
            lambda request_timeout: kube.batch.delete_namespaced_job(
                name=name,
                namespace=namespace,
                body=kubernetes.client.V1DeleteOptions(
                    propagation_policy=propagation_policy,
                ),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete Job {namespace}/{name}",
        )
        _validate_delete_status(
            payload, label=self._object_label(name=name, namespace=namespace)
        )

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this Job is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        """
        namespace, name = self._require_namespace_name("wait for Job deletion")
        await (
            type(self)
            ._client()
            .wait_deleted(
                label=self._object_label(name=name, namespace=namespace),
                timeout=timeout,
                refresh=lambda remaining: self.refresh(kube, timeout=remaining),
            )
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
