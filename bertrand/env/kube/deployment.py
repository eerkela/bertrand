"""Wrappers for the Kubernetes Deployment API and related operations."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING, Self

import kubernetes

from .api import (
    ContainerSpec,
    ImagePullSecretSpec,
    Kube,
    PodSecurityContextSpec,
    TolerationSpec,
    VolumeSpec,
    WatchEvent,
    _label_selector,
    _pod_template_manifest,
)

DEPLOYMENT_WAIT_POLL_INTERVAL_SECONDS = 0.5

if TYPE_CHECKING:
    import builtins
    from collections.abc import AsyncIterator, Collection, Mapping
    from datetime import datetime


@dataclass(frozen=True)
class Deployment:
    """General-purpose wrapper around one Kubernetes Deployment object.

    Parameters
    ----------
    _obj : kubernetes.client.V1Deployment
        Typed Kubernetes Deployment payload returned by the cluster API.

    Notes
    -----
    The convergence API accepts intent-level fields and keeps raw Kubernetes
    manifests as an internal implementation detail.
    """

    _obj: kubernetes.client.V1Deployment

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        timeout: float,
        name: str,
    ) -> Self | None:
        """Read one Kubernetes Deployment by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the Deployment.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        name : str
            Deployment name to read.

        Returns
        -------
        Deployment | None
            Wrapped Kubernetes Deployment, or `None` if it does not exist.

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or the API call fails.
        """
        payload = await kube.run(
            lambda request_timeout: kube.apps.read_namespaced_deployment(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to read Deployment {name!r} in namespace {namespace!r}",
        )
        if payload is None:
            return None
        if not isinstance(payload, kubernetes.client.V1Deployment):
            msg = (
                f"malformed Kubernetes Deployment payload for {name!r} "
                f"in namespace {namespace!r}"
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
        """List Kubernetes Deployments with optional namespace and label filtering.

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
        list[Deployment]
            Wrapped Kubernetes Deployments matching the requested filters.

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or a list call fails.
        """
        label_selector = _label_selector(labels)
        payloads: builtins.list[kubernetes.client.V1DeploymentList] = []

        if namespaces is None:
            payload = await kube.run(
                lambda request_timeout: kube.apps.list_deployment_for_all_namespaces(
                    label_selector=label_selector,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context="failed to list Deployments across all namespaces",
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
                        kube.apps.list_namespaced_deployment(
                            namespace=namespace,
                            label_selector=label_selector,
                            _request_timeout=request_timeout,
                        )
                    ),
                    timeout=timeout,
                    context=f"failed to list Deployments in namespace {namespace!r}",
                )
                if payload is not None:
                    payloads.append(payload)

        out: builtins.list[Self] = []
        for payload in payloads:
            if not isinstance(payload, kubernetes.client.V1DeploymentList):
                msg = "malformed Kubernetes Deployment list payload"
                raise OSError(msg)
            for item in payload.items or []:
                if not isinstance(item, kubernetes.client.V1Deployment):
                    msg = "malformed Kubernetes Deployment entry in list payload"
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
        """Watch Kubernetes Deployments.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum watch budget in seconds. If infinite, wait indefinitely.
        namespace : str | None, optional
            Namespace to watch. If omitted, watches Deployments across all
            namespaces.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.
        field_selector : str | None, optional
            Raw Kubernetes field selector.
        resource_version : str | None, optional
            Resource version to watch from.

        Yields
        ------
        WatchEvent[Deployment]
            Typed watch events containing wrapped Deployments.
        """
        namespace = namespace.strip() if namespace is not None else ""
        if namespace:
            fn = kube.apps.list_namespaced_deployment
            api_kwargs: Mapping[str, object] = {"namespace": namespace}
            context = f"failed to watch Deployments in namespace {namespace!r}"
        else:
            fn = kube.apps.list_deployment_for_all_namespaces
            api_kwargs = {}
            context = "failed to watch Deployments across all namespaces"

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
        if not isinstance(payload, kubernetes.client.V1Deployment):
            msg = "malformed Kubernetes Deployment watch payload"
            raise OSError(msg)
        return cls(_obj=payload)

    @staticmethod
    def _manifest(
        *,
        namespace: str,
        name: str,
        labels: Mapping[str, str],
        selector: Mapping[str, str],
        containers: Collection[ContainerSpec],
        volumes: Collection[VolumeSpec],
        replicas: int,
        automount_service_account_token: bool,
        annotations: Mapping[str, str] | None,
        pod_annotations: Mapping[str, str] | None,
        service_account_name: str | None,
        node_selector: Mapping[str, str] | None,
        host_pid: bool | None,
        pod_security_context: PodSecurityContextSpec | Mapping[str, object] | None,
        tolerations: Collection[TolerationSpec],
        image_pull_secrets: Collection[ImagePullSecretSpec],
        priority_class_name: str | None,
        dns_policy: str | None,
        host_network: bool | None,
        termination_grace_period_seconds: int | None,
        strategy_type: str | None,
    ) -> dict[str, object]:
        template_labels = dict(labels)
        template_labels.update(selector)
        spec: dict[str, object] = {
            "replicas": replicas,
            "selector": {"matchLabels": dict(selector)},
            "template": _pod_template_manifest(
                labels=template_labels,
                pod_annotations=pod_annotations,
                containers=containers,
                volumes=volumes,
                automount_service_account_token=automount_service_account_token,
                service_account_name=service_account_name,
                node_selector=node_selector,
                host_pid=host_pid,
                pod_security_context=pod_security_context,
                tolerations=tolerations,
                image_pull_secrets=image_pull_secrets,
                priority_class_name=priority_class_name,
                dns_policy=dns_policy,
                host_network=host_network,
                termination_grace_period_seconds=(termination_grace_period_seconds),
            ),
        }
        if strategy_type is not None:
            strategy_type = strategy_type.strip()
            if strategy_type:
                strategy: dict[str, object] = {"type": strategy_type}
                if strategy_type == "Recreate":
                    strategy["rollingUpdate"] = None
                spec["strategy"] = strategy
        return {
            "apiVersion": "apps/v1",
            "kind": "Deployment",
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
        selector: Mapping[str, str],
        containers: Collection[ContainerSpec],
        volumes: Collection[VolumeSpec],
        timeout: float,
        replicas: int = 1,
        automount_service_account_token: bool = False,
        annotations: Mapping[str, str] | None = None,
        pod_annotations: Mapping[str, str] | None = None,
        service_account_name: str | None = None,
        node_selector: Mapping[str, str] | None = None,
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
        strategy_type: str | None = None,
    ) -> Self:
        """Create or patch one Kubernetes Deployment from intent-level fields.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the Deployment.
        name : str
            Deployment name to create or patch.
        labels : Mapping[str, str]
            Labels to apply to the Deployment and pod template.
        selector : Mapping[str, str]
            Immutable pod selector labels for the Deployment.
        containers : Collection[ContainerSpec]
            Pod containers to render into the Deployment template.
        volumes : Collection[VolumeSpec]
            Pod volumes to render into the Deployment template.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        replicas : int, optional
            Desired replica count.
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
        strategy_type : str | None, optional
            Optional Deployment rollout strategy type, such as `"Recreate"`.

        Returns
        -------
        Deployment
            Wrapped created or patched Deployment.

        Raises
        ------
        OSError
            If namespace/name are empty, or Kubernetes create/patch fails or returns
            malformed data.
        """
        namespace = namespace.strip()
        name = name.strip()
        if not namespace or not name:
            msg = "Deployment upsert requires non-empty namespace and name"
            raise OSError(msg)

        manifest = cls._manifest(
            namespace=namespace,
            name=name,
            labels=labels,
            selector=selector,
            containers=containers,
            volumes=volumes,
            replicas=replicas,
            automount_service_account_token=automount_service_account_token,
            annotations=annotations,
            pod_annotations=pod_annotations,
            service_account_name=service_account_name,
            node_selector=node_selector,
            host_pid=host_pid,
            pod_security_context=pod_security_context,
            tolerations=tolerations,
            image_pull_secrets=image_pull_secrets,
            priority_class_name=priority_class_name,
            dns_policy=dns_policy,
            host_network=host_network,
            termination_grace_period_seconds=termination_grace_period_seconds,
            strategy_type=strategy_type,
        )

        try:
            created = await kube.run(
                lambda request_timeout: kube.apps.create_namespaced_deployment(
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=f"failed to create Deployment {namespace}/{name}",
            )
        except OSError as err:
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise
        else:
            if not isinstance(created, kubernetes.client.V1Deployment):
                msg = f"malformed Kubernetes Deployment payload while creating {name!r}"
                raise OSError(msg)
            return cls(_obj=created)

        patched = await kube.run(
            lambda request_timeout: kube.apps.patch_namespaced_deployment(
                name=name,
                namespace=namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to patch Deployment {namespace}/{name}",
        )
        if not isinstance(patched, kubernetes.client.V1Deployment):
            msg = f"malformed Kubernetes Deployment payload while patching {name!r}"
            raise OSError(msg)
        return cls(_obj=patched)

    @property
    def name(self) -> str:
        """Return this Deployment's name.

        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        metadata = self._obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def namespace(self) -> str:
        """Return this Deployment's namespace.

        Returns
        -------
        str
            Trimmed `metadata.namespace`, or an empty string when unavailable.
        """
        metadata = self._obj.metadata
        return (metadata.namespace or "").strip() if metadata is not None else ""

    @property
    def labels(self) -> Mapping[str, str]:
        """Return this Deployment's labels.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.labels`, or an empty mapping when unavailable.
        """
        metadata = self._obj.metadata
        if metadata is None or metadata.labels is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.labels)

    @property
    def annotations(self) -> Mapping[str, str]:
        """Return this Deployment's annotations.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.annotations`, or an empty mapping when
            unavailable.
        """
        metadata = self._obj.metadata
        if metadata is None or metadata.annotations is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.annotations)

    @property
    def resource_version(self) -> str:
        """Return this Deployment's resource version.

        Returns
        -------
        str
            Kubernetes `metadata.resourceVersion`, or an empty string when
            unavailable.
        """
        metadata = self._obj.metadata
        return (metadata.resource_version or "").strip() if metadata is not None else ""

    @property
    def uid(self) -> str:
        """Return this Deployment's UID.

        Returns
        -------
        str
            Kubernetes `metadata.uid`, or an empty string when unavailable.
        """
        metadata = self._obj.metadata
        return (metadata.uid or "").strip() if metadata is not None else ""

    @property
    def created_at(self) -> datetime | None:
        """Return this Deployment's creation timestamp.

        Returns
        -------
        datetime | None
            Kubernetes `metadata.creationTimestamp`, or `None` when unavailable.
        """
        metadata = self._obj.metadata
        return metadata.creation_timestamp if metadata is not None else None

    @property
    def generation(self) -> int:
        """Return this Deployment's metadata generation.

        Returns
        -------
        int
            Kubernetes `metadata.generation`, or zero when unavailable.
        """
        metadata = self._obj.metadata
        return int(metadata.generation or 0) if metadata is not None else 0

    @property
    def replicas(self) -> int:
        """Return this Deployment's desired replica count.

        Returns
        -------
        int
            Desired replica count, or zero when unavailable.
        """
        spec = self._obj.spec
        return int(spec.replicas or 0) if spec is not None else 0

    @property
    def available_replicas(self) -> int:
        """Return this Deployment's available replica count.

        Returns
        -------
        int
            Available replica count, or zero when unavailable.
        """
        status = self._obj.status
        return int(status.available_replicas or 0) if status is not None else 0

    @property
    def ready_replicas(self) -> int:
        """Return this Deployment's ready replica count.

        Returns
        -------
        int
            Ready replica count, or zero when unavailable.
        """
        status = self._obj.status
        return int(status.ready_replicas or 0) if status is not None else 0

    @property
    def updated_replicas(self) -> int:
        """Return this Deployment's updated replica count.

        Returns
        -------
        int
            Updated replica count, or zero when unavailable.
        """
        status = self._obj.status
        return int(status.updated_replicas or 0) if status is not None else 0

    @property
    def unavailable_replicas(self) -> int:
        """Return this Deployment's unavailable replica count.

        Returns
        -------
        int
            Unavailable replica count, or zero when unavailable.
        """
        status = self._obj.status
        return int(status.unavailable_replicas or 0) if status is not None else 0

    @property
    def observed_generation(self) -> int:
        """Return the Deployment generation observed by the controller.

        Returns
        -------
        int
            Kubernetes `status.observedGeneration`, or zero when unavailable.
        """
        status = self._obj.status
        return int(status.observed_generation or 0) if status is not None else 0

    @property
    def selector(self) -> Mapping[str, str]:
        """Return this Deployment's selector labels.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `spec.selector.match_labels`, or an empty mapping when
            unavailable.
        """
        spec = self._obj.spec
        selector = spec.selector if spec is not None else None
        labels = selector.match_labels if selector is not None else None
        if labels is None:
            return MappingProxyType({})
        return MappingProxyType(labels)

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this Deployment by its metadata namespace and name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Deployment | None
            Fresh wrapper for the same Deployment, or `None` if it no longer exists.

        Raises
        ------
        OSError
            If this wrapper does not contain enough metadata to identify the
            Deployment, or if Kubernetes returns malformed data.
        """
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = "cannot refresh Deployment with missing metadata.name/namespace"
            raise OSError(msg)
        return await type(self).get(
            kube,
            namespace=namespace,
            timeout=timeout,
            name=name,
        )

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this Deployment from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Raises
        ------
        OSError
            If this wrapper does not contain enough metadata to identify the
            Deployment, if the delete request fails, or if Kubernetes returns
            malformed data.
        """
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = "cannot delete Deployment with missing metadata.name/namespace"
            raise OSError(msg)
        payload = await kube.run(
            lambda request_timeout: kube.apps.delete_namespaced_deployment(
                name=name,
                namespace=namespace,
                body=kubernetes.client.V1DeleteOptions(),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete Deployment {namespace}/{name}",
        )
        if payload is not None and not isinstance(payload, kubernetes.client.V1Status):
            msg = (
                "malformed Kubernetes response while deleting Deployment "
                f"{namespace}/{name}"
            )
            raise OSError(msg)

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this Deployment is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        Raises
        ------
        OSError
            If this wrapper does not contain enough metadata to identify the
            Deployment, or if a refresh request returns malformed data.
        TimeoutError
            If the Deployment still exists when `timeout` expires.
        """
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = (
                "cannot wait for Deployment deletion with missing "
                "metadata.name/namespace"
            )
            raise OSError(msg)
        if timeout <= 0:
            msg = f"timed out waiting for Deployment {namespace}/{name} deletion"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                msg = f"timed out waiting for Deployment {namespace}/{name} deletion"
                raise TimeoutError(msg)
            live = await self.refresh(kube, timeout=remaining)
            if live is None:
                return
            await asyncio.sleep(min(DEPLOYMENT_WAIT_POLL_INTERVAL_SECONDS, remaining))

    async def wait_available(
        self,
        kube: Kube,
        *,
        timeout: float,
        minimum: int = 1,
    ) -> Self:
        """Wait until this Deployment has at least `minimum` available replicas.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.
        minimum : int, optional
            Minimum acceptable `status.availableReplicas` value.

        Returns
        -------
        Deployment
            Fresh wrapper whose available replica count satisfies `minimum`.

        Raises
        ------
        ValueError
            If `minimum` is less than one.
        OSError
            If this wrapper is missing metadata or the Deployment disappears.
        TimeoutError
            If the Deployment does not become available before `timeout`.
        """
        if minimum < 1:
            msg = "minimum available Deployment replicas must be positive"
            raise ValueError(msg)
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = "cannot wait on Deployment with missing metadata.name/namespace"
            raise OSError(msg)
        if timeout <= 0:
            msg = f"timed out waiting for Deployment {namespace}/{name} availability"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        live: Self = self
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                msg = (
                    f"timed out waiting for Deployment {namespace}/{name} availability"
                )
                raise TimeoutError(msg)
            refreshed = await live.refresh(kube, timeout=remaining)
            if refreshed is None:
                msg = (
                    f"Deployment {namespace}/{name} disappeared while waiting for "
                    "availability"
                )
                raise OSError(msg)
            if refreshed.available_replicas >= minimum:
                return refreshed
            live = refreshed
            await asyncio.sleep(min(DEPLOYMENT_WAIT_POLL_INTERVAL_SECONDS, remaining))

    async def wait_rollout(
        self,
        kube: Kube,
        *,
        timeout: float,
        minimum: int = 1,
    ) -> Self:
        """Wait until this Deployment rolls out at least `minimum` replicas.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.
        minimum : int, optional
            Minimum acceptable updated and available replica count.

        Returns
        -------
        Deployment
            Fresh wrapper whose controller-observed generation and replica status
            indicate the rollout has completed for at least `minimum` replicas.

        Raises
        ------
        ValueError
            If `minimum` is less than one.
        OSError
            If this wrapper is missing metadata or the Deployment disappears.
        TimeoutError
            If the Deployment rollout does not complete before `timeout`.
        """
        if minimum < 1:
            msg = "minimum rolled out Deployment replicas must be positive"
            raise ValueError(msg)
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = "cannot wait on Deployment with missing metadata.name/namespace"
            raise OSError(msg)
        if timeout <= 0:
            msg = f"timed out waiting for Deployment {namespace}/{name} rollout"
            raise TimeoutError(msg)
        target_generation = self.generation
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        live: Self = self
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                msg = f"timed out waiting for Deployment {namespace}/{name} rollout"
                raise TimeoutError(msg)
            refreshed = await live.refresh(kube, timeout=remaining)
            if refreshed is None:
                msg = (
                    f"Deployment {namespace}/{name} disappeared while waiting for "
                    "rollout"
                )
                raise OSError(msg)
            generation_observed = (
                target_generation <= 0
                or refreshed.observed_generation >= target_generation
            )
            replicas_updated = refreshed.updated_replicas >= minimum
            replicas_available = refreshed.available_replicas >= minimum
            if generation_observed and replicas_updated and replicas_available:
                return refreshed
            live = refreshed
            await asyncio.sleep(min(DEPLOYMENT_WAIT_POLL_INTERVAL_SECONDS, remaining))

    async def scale(self, kube: Kube, *, replicas: int, timeout: float) -> Self:
        """Patch this Deployment's desired replica count.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        replicas : int
            Desired replica count. Must be non-negative.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Deployment
            Fresh wrapper after the scale patch is accepted.

        Raises
        ------
        ValueError
            If `replicas` is negative.
        OSError
            If this wrapper is missing metadata, Kubernetes rejects the patch, or
            the Deployment disappears during refresh.
        """
        if replicas < 0:
            msg = "Deployment replicas cannot be negative"
            raise ValueError(msg)
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = "cannot scale Deployment with missing metadata.name/namespace"
            raise OSError(msg)
        payload = await kube.run(
            lambda request_timeout: kube.apps.patch_namespaced_deployment_scale(
                name=name,
                namespace=namespace,
                body={"spec": {"replicas": replicas}},
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to scale Deployment {namespace}/{name}",
        )
        if payload is None:
            msg = f"Deployment {namespace}/{name} disappeared while scaling"
            raise OSError(msg)
        live = await type(self).get(
            kube,
            namespace=namespace,
            timeout=timeout,
            name=name,
        )
        if live is None:
            msg = f"Deployment {namespace}/{name} disappeared after scaling"
            raise OSError(msg)
        return live
