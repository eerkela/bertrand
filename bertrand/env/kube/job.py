"""Wrappers for the Kubernetes Job API and related execution operations."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
from typing import TYPE_CHECKING, Literal, Self

import kubernetes

from .api import (
    ContainerSpec,
    ImagePullSecretSpec,
    Kube,
    NamespacedKubeMetadata,
    PodSecurityContextSpec,
    TolerationSpec,
    VolumeSpec,
    WatchEvent,
    _label_selector,
    _pod_template_manifest,
    _validate_delete_status,
    _wait_until_deleted,
)

if TYPE_CHECKING:
    import builtins
    from collections.abc import AsyncIterator, Collection, Mapping
    from datetime import datetime

    from bertrand.env.kube.pod import Pod

JOB_WAIT_POLL_INTERVAL_SECONDS = 0.5
type RestartPolicy = Literal["Never", "OnFailure"]
type DeletionPropagationPolicy = Literal["Background", "Foreground", "Orphan"]


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

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or the API call fails.
        """
        payload = await kube.run(
            lambda request_timeout: kube.batch.read_namespaced_job(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to read Job {name!r} in namespace {namespace!r}",
        )
        if payload is None:
            return None
        if not isinstance(payload, kubernetes.client.V1Job):
            msg = (
                f"malformed Kubernetes Job payload for {name!r} in namespace "
                f"{namespace!r}"
            )
            raise OSError(msg)
        return cls(_obj=payload)

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

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or a list call fails.
        """
        label_selector = _label_selector(labels)
        payloads: builtins.list[kubernetes.client.V1JobList] = []

        if namespaces is None:
            payload = await kube.run(
                lambda request_timeout: kube.batch.list_job_for_all_namespaces(
                    label_selector=label_selector,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context="failed to list Jobs across all namespaces",
            )
            if payload is not None:
                payloads.append(payload)
        else:
            normalized = {namespace.strip() for namespace in namespaces}
            normalized.discard("")
            if not normalized:
                return []
            for namespace in sorted(normalized):
                payload = await kube.run(
                    lambda request_timeout, namespace=namespace: (
                        kube.batch.list_namespaced_job(
                            namespace=namespace,
                            label_selector=label_selector,
                            _request_timeout=request_timeout,
                        )
                    ),
                    timeout=timeout,
                    context=f"failed to list Jobs in namespace {namespace!r}",
                )
                if payload is not None:
                    payloads.append(payload)

        out: builtins.list[Self] = []
        for payload in payloads:
            if not isinstance(payload, kubernetes.client.V1JobList):
                msg = "malformed Kubernetes Job list payload"
                raise OSError(msg)
            for item in payload.items or []:
                if not isinstance(item, kubernetes.client.V1Job):
                    msg = "malformed Kubernetes Job entry in list payload"
                    raise OSError(msg)
                out.append(cls(_obj=item))
        return out

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
        namespace = namespace.strip() if namespace is not None else ""
        if namespace:
            fn = kube.batch.list_namespaced_job
            api_kwargs: Mapping[str, object] = {"namespace": namespace}
            context = f"failed to watch Jobs in namespace {namespace!r}"
        else:
            fn = kube.batch.list_job_for_all_namespaces
            api_kwargs = {}
            context = "failed to watch Jobs across all namespaces"

        async for event in kube.watch(
            fn,
            wrapper=cls._watch_payload,
            timeout=timeout,
            context=context,
            resource_version=resource_version,
            labels=labels,
            field_selector=field_selector,
            api_kwargs=api_kwargs,
        ):
            yield event

    @classmethod
    def _watch_payload(cls, payload: object) -> Self:
        if not isinstance(payload, kubernetes.client.V1Job):
            msg = "malformed Kubernetes Job watch payload"
            raise OSError(msg)
        return cls(_obj=payload)

    @staticmethod
    def _manifest(
        *,
        namespace: str,
        name: str,
        labels: Mapping[str, str],
        containers: Collection[ContainerSpec],
        volumes: Collection[VolumeSpec],
        restart_policy: RestartPolicy,
        backoff_limit: int,
        ttl_seconds_after_finished: int | None,
        automount_service_account_token: bool,
        annotations: Mapping[str, str] | None,
        pod_annotations: Mapping[str, str] | None,
        service_account_name: str | None,
        node_selector: Mapping[str, str] | None,
        node_name: str | None,
        host_pid: bool | None,
        pod_security_context: PodSecurityContextSpec | Mapping[str, object] | None,
        tolerations: Collection[TolerationSpec],
        image_pull_secrets: Collection[ImagePullSecretSpec],
        priority_class_name: str | None,
        dns_policy: str | None,
        host_network: bool | None,
        termination_grace_period_seconds: int | None,
    ) -> dict[str, object]:
        spec: dict[str, object] = {
            "backoffLimit": backoff_limit,
            "template": _pod_template_manifest(
                labels=labels,
                pod_annotations=pod_annotations,
                containers=containers,
                volumes=volumes,
                automount_service_account_token=automount_service_account_token,
                service_account_name=service_account_name,
                node_selector=node_selector,
                host_pid=host_pid,
                restart_policy=restart_policy,
                pod_security_context=pod_security_context,
                tolerations=tolerations,
                image_pull_secrets=image_pull_secrets,
                priority_class_name=priority_class_name,
                dns_policy=dns_policy,
                host_network=host_network,
                termination_grace_period_seconds=termination_grace_period_seconds,
                node_name=node_name,
            ),
        }
        if ttl_seconds_after_finished is not None:
            spec["ttlSecondsAfterFinished"] = ttl_seconds_after_finished

        return {
            "apiVersion": "batch/v1",
            "kind": "Job",
            "metadata": {
                "name": name,
                "namespace": namespace,
                "labels": dict(labels),
                "annotations": dict(annotations or {}),
            },
            "spec": spec,
        }

    @classmethod
    async def create(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        labels: Mapping[str, str],
        containers: Collection[ContainerSpec],
        volumes: Collection[VolumeSpec],
        timeout: float,
        restart_policy: RestartPolicy = "Never",
        backoff_limit: int = 0,
        ttl_seconds_after_finished: int | None = 3600,
        automount_service_account_token: bool = False,
        annotations: Mapping[str, str] | None = None,
        pod_annotations: Mapping[str, str] | None = None,
        service_account_name: str | None = None,
        node_selector: Mapping[str, str] | None = None,
        node_name: str | None = None,
        host_pid: bool | None = None,
        pod_security_context: PodSecurityContextSpec
        | Mapping[str, object]
        | None = None,
        tolerations: Collection[TolerationSpec] = (),
        image_pull_secrets: Collection[ImagePullSecretSpec] = (),
        priority_class_name: str | None = None,
        dns_policy: str | None = None,
        host_network: bool | None = None,
        termination_grace_period_seconds: int | None = None,
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
        containers : Collection[ContainerSpec]
            Pod containers to render into the Job template.
        volumes : Collection[VolumeSpec]
            Pod volumes to render into the Job template.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        restart_policy : {"Never", "OnFailure"}, optional
            Pod restart policy.
        backoff_limit : int, optional
            Kubernetes Job retry limit.
        ttl_seconds_after_finished : int | None, optional
            Optional TTL controller retention period for finished Jobs.
        automount_service_account_token : bool, optional
            Whether pods should automount the default service-account token.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to `metadata.annotations`.
        pod_annotations : Mapping[str, str] | None, optional
            Annotations to apply to pod template `metadata.annotations`.
        service_account_name : str | None, optional
            Optional pod service account name.
        node_selector : Mapping[str, str] | None, optional
            Optional pod node selector.
        node_name : str | None, optional
            Optional exact node name for host-local execution.
        host_pid : bool | None, optional
            Optional pod `hostPID` value.
        pod_security_context : PodSecurityContextSpec | Mapping | None, optional
            Optional pod security context.
        tolerations : Collection[TolerationSpec], optional
            Optional pod tolerations.
        image_pull_secrets : Collection[ImagePullSecretSpec], optional
            Optional image pull Secret references.
        priority_class_name : str | None, optional
            Optional pod priority class name.
        dns_policy : str | None, optional
            Optional pod DNS policy.
        host_network : bool | None, optional
            Optional pod `hostNetwork` value.
        termination_grace_period_seconds : int | None, optional
            Optional pod termination grace period in seconds.

        Returns
        -------
        Job
            Wrapped created Job.

        Raises
        ------
        OSError
            If Kubernetes create fails or returns malformed data.
        ValueError
            If retry or TTL settings are invalid.
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
            containers=containers,
            volumes=volumes,
            restart_policy=restart_policy,
            backoff_limit=backoff_limit,
            ttl_seconds_after_finished=ttl_seconds_after_finished,
            automount_service_account_token=automount_service_account_token,
            annotations=annotations,
            pod_annotations=pod_annotations,
            service_account_name=service_account_name,
            node_selector=node_selector,
            node_name=node_name,
            host_pid=host_pid,
            pod_security_context=pod_security_context,
            tolerations=tolerations,
            image_pull_secrets=image_pull_secrets,
            priority_class_name=priority_class_name,
            dns_policy=dns_policy,
            host_network=host_network,
            termination_grace_period_seconds=termination_grace_period_seconds,
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

        Raises
        ------
        TimeoutError
            If the Job still exists when `timeout` expires.
        """
        namespace, name = self._require_namespace_name("wait for Job deletion")
        try:
            await _wait_until_deleted(
                label=self._object_label(name=name, namespace=namespace),
                timeout=timeout,
                refresh=lambda remaining: self.refresh(kube, timeout=remaining),
            )
        except TimeoutError as err:
            raise TimeoutError(str(err)) from err

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
        OSError
            If the Job fails or disappears while waiting.
        """
        namespace, name = self._require_namespace_name("wait for Job completion")
        if timeout <= 0:
            msg = f"timed out waiting for Job {namespace}/{name} completion"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        current: Self = self
        while True:
            if current.succeeded > 0:
                return current
            failed_condition = current.failed_condition
            if failed_condition is not None:
                msg = f"Job {namespace}/{name} failed: {failed_condition}"
                raise OSError(msg)

            remaining = deadline - loop.time()
            if remaining <= 0:
                msg = f"timed out waiting for Job {namespace}/{name} completion"
                raise TimeoutError(msg)
            await asyncio.sleep(min(JOB_WAIT_POLL_INTERVAL_SECONDS, remaining))
            refreshed = await current.refresh(kube, timeout=deadline - loop.time())
            if refreshed is None:
                msg = f"Job {namespace}/{name} disappeared while waiting for completion"
                raise OSError(msg)
            current = refreshed
