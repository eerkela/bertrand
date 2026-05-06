"""Wrappers for the Kubernetes Deployment API and related operations."""

from __future__ import annotations

import asyncio
import builtins
from collections.abc import Collection, Mapping
from dataclasses import dataclass
from types import MappingProxyType
from typing import Self

import kubernetes

from .api import ContainerSpec, Kube, VolumeSpec, _label_selector

DEPLOYMENT_WAIT_POLL_INTERVAL_SECONDS = 0.5


@dataclass(frozen=True)
class Deployment:
    """General-purpose wrapper around one Kubernetes Deployment object.

    Parameters
    ----------
    obj : kubernetes.client.V1Deployment
        Typed Kubernetes Deployment payload returned by the cluster API.

    Notes
    -----
    The convergence API accepts intent-level fields and keeps raw Kubernetes
    manifests as an internal implementation detail.
    """

    obj: kubernetes.client.V1Deployment

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
        TimeoutError
            If the Kubernetes request exceeds `timeout`.
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
            raise OSError(
                f"malformed Kubernetes Deployment payload for {name!r} in namespace {namespace!r}"
            )
        return cls(obj=payload)

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
        TimeoutError
            If any Kubernetes request exceeds `timeout`.
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
                raise OSError("malformed Kubernetes Deployment list payload")
            for item in payload.items or []:
                if not isinstance(item, kubernetes.client.V1Deployment):
                    raise OSError("malformed Kubernetes Deployment entry in list payload")
                out.append(cls(obj=item))
        return out

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
    ) -> dict[str, object]:
        return {
            "apiVersion": "apps/v1",
            "kind": "Deployment",
            "metadata": {
                "name": name,
                "namespace": namespace,
                "labels": dict(labels),
                "annotations": dict(annotations or {}),
            },
            "spec": {
                "replicas": replicas,
                "selector": {"matchLabels": dict(selector)},
                "template": {
                    "metadata": {"labels": dict(labels)},
                    "spec": {
                        "automountServiceAccountToken": automount_service_account_token,
                        "containers": [container.manifest() for container in containers],
                        "volumes": [volume.manifest() for volume in volumes],
                    },
                },
            },
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

        Returns
        -------
        Deployment
            Wrapped created or patched Deployment.

        Raises
        ------
        TimeoutError
            If any Kubernetes request exceeds `timeout`.
        OSError
            If Kubernetes create/patch fails or returns malformed data.
        """
        namespace = namespace.strip()
        name = name.strip()
        if not namespace or not name:
            raise OSError("Deployment upsert requires non-empty namespace and name")

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
            if not isinstance(created, kubernetes.client.V1Deployment):
                raise OSError(f"malformed Kubernetes Deployment payload while creating {name!r}")
            return cls(obj=created)
        except OSError as err:
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise

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
            raise OSError(f"malformed Kubernetes Deployment payload while patching {name!r}")
        return cls(obj=patched)

    @property
    def name(self) -> str:
        """
        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def namespace(self) -> str:
        """
        Returns
        -------
        str
            Trimmed `metadata.namespace`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.namespace or "").strip() if metadata is not None else ""

    @property
    def identity(self) -> tuple[str, str] | None:
        """
        Returns
        -------
        tuple[str, str] | None
            `(namespace, name)` when both are available, otherwise `None`.
        """
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            return None
        return namespace, name

    @property
    def labels(self) -> Mapping[str, str]:
        """
        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.labels`, or an empty mapping when unavailable.
        """
        metadata = self.obj.metadata
        if metadata is None or metadata.labels is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.labels)

    @property
    def annotations(self) -> Mapping[str, str]:
        """
        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.annotations`, or an empty mapping when
            unavailable.
        """
        metadata = self.obj.metadata
        if metadata is None or metadata.annotations is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.annotations)

    @property
    def replicas(self) -> int:
        """
        Returns
        -------
        int
            Desired replica count, or zero when unavailable.
        """
        spec = self.obj.spec
        return int(spec.replicas or 0) if spec is not None else 0

    @property
    def available_replicas(self) -> int:
        """
        Returns
        -------
        int
            Available replica count, or zero when unavailable.
        """
        status = self.obj.status
        return int(status.available_replicas or 0) if status is not None else 0

    @property
    def ready_replicas(self) -> int:
        """
        Returns
        -------
        int
            Ready replica count, or zero when unavailable.
        """
        status = self.obj.status
        return int(status.ready_replicas or 0) if status is not None else 0

    @property
    def selector(self) -> Mapping[str, str]:
        """
        Returns
        -------
        Mapping[str, str]
            Read-only view of `spec.selector.match_labels`, or an empty mapping when
            unavailable.
        """
        spec = self.obj.spec
        selector = spec.selector if spec is not None else None
        labels = selector.match_labels if selector is not None else None
        if labels is None:
            return MappingProxyType({})
        return MappingProxyType(labels)

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this Deployment by identity.

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
        TimeoutError
            If the Kubernetes request exceeds `timeout`.
        """
        identity = self.identity
        if identity is None:
            raise OSError("cannot refresh Deployment with missing metadata.name/namespace")
        namespace, name = identity
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
        TimeoutError
            If the Kubernetes request exceeds `timeout`.
        """
        identity = self.identity
        if identity is None:
            raise OSError("cannot delete Deployment with missing metadata.name/namespace")
        namespace, name = identity
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
            raise OSError(
                f"malformed Kubernetes response while deleting Deployment {namespace}/{name}"
            )

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
        identity = self.identity
        if identity is None:
            raise OSError("cannot wait for Deployment deletion with missing identity")
        namespace, name = identity
        if timeout <= 0:
            raise TimeoutError(f"timed out waiting for Deployment {namespace}/{name} deletion")
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                raise TimeoutError(f"timed out waiting for Deployment {namespace}/{name} deletion")
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
            If this wrapper is missing identity or the Deployment disappears.
        TimeoutError
            If the Deployment does not become available before `timeout`.
        """
        if minimum < 1:
            raise ValueError("minimum available Deployment replicas must be positive")
        identity = self.identity
        if identity is None:
            raise OSError("cannot wait on Deployment with missing metadata.name/namespace")
        namespace, name = identity
        if timeout <= 0:
            raise TimeoutError(f"timed out waiting for Deployment {namespace}/{name} availability")
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        live: Self = self
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                raise TimeoutError(
                    f"timed out waiting for Deployment {namespace}/{name} availability"
                )
            refreshed = await live.refresh(kube, timeout=remaining)
            if refreshed is None:
                raise OSError(
                    f"Deployment {namespace}/{name} disappeared while waiting for availability"
                )
            if refreshed.available_replicas >= minimum:
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
            If this wrapper is missing identity, Kubernetes rejects the patch, or
            the Deployment disappears during refresh.
        TimeoutError
            If the Kubernetes request exceeds `timeout`.
        """
        if replicas < 0:
            raise ValueError("Deployment replicas cannot be negative")
        identity = self.identity
        if identity is None:
            raise OSError("cannot scale Deployment with missing metadata.name/namespace")
        namespace, name = identity
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
            raise OSError(f"Deployment {namespace}/{name} disappeared while scaling")
        live = await type(self).get(
            kube,
            namespace=namespace,
            timeout=timeout,
            name=name,
        )
        if live is None:
            raise OSError(f"Deployment {namespace}/{name} disappeared after scaling")
        return live
