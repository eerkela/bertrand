"""Wrappers for the Kubernetes Deployment API and related operations."""

from __future__ import annotations

import asyncio
import builtins
from collections.abc import Collection, Mapping, Sequence
from dataclasses import dataclass
from types import MappingProxyType
from typing import Literal, Self, cast

import kubernetes

from .api import Kube, _label_selector

DEPLOYMENT_WAIT_POLL_INTERVAL_SECONDS = 0.5
type ContainerPortProtocol = Literal["TCP", "UDP", "SCTP"]
type ProbePort = int | str


def _manifest_identity(manifest: Mapping[str, object]) -> tuple[str, str]:
    metadata = manifest.get("metadata")
    if not isinstance(metadata, dict):
        raise OSError("Deployment manifest must define metadata")
    metadata = cast("dict[object, object]", metadata)
    namespace = str(metadata.get("namespace") or "").strip()
    name = str(metadata.get("name") or "").strip()
    if not namespace or not name:
        raise OSError("Deployment manifest must define metadata.namespace and metadata.name")
    return namespace, name


@dataclass(frozen=True)
class ContainerPort:
    """Intent-level container port specification."""
    name: str
    container_port: int
    protocol: ContainerPortProtocol = "TCP"

    def manifest(self) -> dict[str, object]:
        """Render this port as a Kubernetes container port manifest."""
        return {
            "name": self.name,
            "containerPort": self.container_port,
            "protocol": self.protocol,
        }


@dataclass(frozen=True)
class EnvVar:
    """Intent-level container environment variable."""
    name: str
    value: str

    def manifest(self) -> dict[str, object]:
        """Render this environment variable as a Kubernetes manifest."""
        return {"name": self.name, "value": self.value}


@dataclass(frozen=True)
class VolumeMount:
    """Intent-level container volume mount."""
    name: str
    mount_path: str
    read_only: bool | None = None

    def manifest(self) -> dict[str, object]:
        """Render this mount as a Kubernetes volume mount manifest."""
        payload: dict[str, object] = {
            "name": self.name,
            "mountPath": self.mount_path,
        }
        if self.read_only is not None:
            payload["readOnly"] = self.read_only
        return payload


@dataclass(frozen=True)
class Volume:
    """Intent-level pod volume."""
    name: str
    empty_dir_config: Mapping[str, object] | None = None
    config_map_name: str | None = None
    config_map_optional: bool | None = None
    persistent_volume_claim: str | None = None

    @classmethod
    def empty_dir(
        cls,
        name: str,
        config: Mapping[str, object] | None = None,
    ) -> Self:
        """Create an `emptyDir` volume specification."""
        return cls(name=name, empty_dir_config=dict(config or {}))

    @classmethod
    def config_map(
        cls,
        name: str,
        *,
        config_map_name: str,
        optional: bool | None = None,
    ) -> Self:
        """Create a ConfigMap-backed volume specification."""
        return cls(name=name, config_map_name=config_map_name, config_map_optional=optional)

    @classmethod
    def pvc(cls, name: str, *, claim_name: str) -> Self:
        """Create a PVC-backed volume specification."""
        return cls(name=name, persistent_volume_claim=claim_name)

    def manifest(self) -> dict[str, object]:
        """Render this volume as a Kubernetes manifest."""
        kinds = sum(
            value is not None
            for value in (
                self.empty_dir_config,
                self.config_map_name,
                self.persistent_volume_claim,
            )
        )
        if kinds != 1:
            raise ValueError("Deployment volume must define exactly one source")

        payload: dict[str, object] = {"name": self.name}
        if self.empty_dir_config is not None:
            payload["emptyDir"] = dict(self.empty_dir_config)
        elif self.config_map_name is not None:
            config_map: dict[str, object] = {"name": self.config_map_name}
            if self.config_map_optional is not None:
                config_map["optional"] = self.config_map_optional
            payload["configMap"] = config_map
        elif self.persistent_volume_claim is not None:
            payload["persistentVolumeClaim"] = {"claimName": self.persistent_volume_claim}
        return payload


@dataclass(frozen=True)
class Probe:
    """Intent-level container health probe."""
    handler: Mapping[str, object]
    initial_delay_seconds: int | None = None
    period_seconds: int | None = None
    failure_threshold: int | None = None

    @classmethod
    def tcp(
        cls,
        *,
        port: ProbePort,
        initial_delay_seconds: int | None = None,
        period_seconds: int | None = None,
        failure_threshold: int | None = None,
    ) -> Self:
        """Create a TCP socket probe."""
        return cls(
            handler={"tcpSocket": {"port": port}},
            initial_delay_seconds=initial_delay_seconds,
            period_seconds=period_seconds,
            failure_threshold=failure_threshold,
        )

    @classmethod
    def http(
        cls,
        *,
        path: str,
        port: ProbePort,
        initial_delay_seconds: int | None = None,
        period_seconds: int | None = None,
        failure_threshold: int | None = None,
    ) -> Self:
        """Create an HTTP GET probe."""
        return cls(
            handler={"httpGet": {"path": path, "port": port}},
            initial_delay_seconds=initial_delay_seconds,
            period_seconds=period_seconds,
            failure_threshold=failure_threshold,
        )

    def manifest(self) -> dict[str, object]:
        """Render this probe as a Kubernetes manifest."""
        payload = dict(self.handler)
        if self.initial_delay_seconds is not None:
            payload["initialDelaySeconds"] = self.initial_delay_seconds
        if self.period_seconds is not None:
            payload["periodSeconds"] = self.period_seconds
        if self.failure_threshold is not None:
            payload["failureThreshold"] = self.failure_threshold
        return payload


@dataclass(frozen=True)
class Container:
    """Intent-level Deployment container specification."""
    name: str
    image: str
    image_pull_policy: str | None = None
    command: Sequence[str] | None = None
    args: Sequence[str] | None = None
    ports: Collection[ContainerPort] = ()
    env: Collection[EnvVar] = ()
    readiness_probe: Probe | None = None
    liveness_probe: Probe | None = None
    volume_mounts: Collection[VolumeMount] = ()
    security_context: Mapping[str, object] | None = None

    def manifest(self) -> dict[str, object]:
        """Render this container as a Kubernetes manifest."""
        payload: dict[str, object] = {
            "name": self.name,
            "image": self.image,
        }
        if self.image_pull_policy is not None:
            payload["imagePullPolicy"] = self.image_pull_policy
        if self.command is not None:
            payload["command"] = list(self.command)
        if self.args is not None:
            payload["args"] = list(self.args)
        if self.ports:
            payload["ports"] = [port.manifest() for port in self.ports]
        if self.env:
            payload["env"] = [var.manifest() for var in self.env]
        if self.readiness_probe is not None:
            payload["readinessProbe"] = self.readiness_probe.manifest()
        if self.liveness_probe is not None:
            payload["livenessProbe"] = self.liveness_probe.manifest()
        if self.volume_mounts:
            payload["volumeMounts"] = [mount.manifest() for mount in self.volume_mounts]
        if self.security_context is not None:
            payload["securityContext"] = dict(self.security_context)
        return payload


@dataclass(frozen=True)
class Deployment:
    """General-purpose wrapper around one Kubernetes Deployment object."""

    obj: kubernetes.client.V1Deployment

    @staticmethod
    def _manifest(
        *,
        namespace: str,
        name: str,
        labels: Mapping[str, str],
        selector: Mapping[str, str],
        containers: Collection[Container],
        volumes: Collection[Volume],
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
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        timeout: float,
        name: str,
    ) -> Self | None:
        """Read one Kubernetes Deployment by name."""
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
        """List Kubernetes Deployments with optional namespace and label filtering."""
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

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        labels: Mapping[str, str],
        selector: Mapping[str, str],
        containers: Collection[Container],
        volumes: Collection[Volume],
        timeout: float,
        replicas: int = 1,
        automount_service_account_token: bool = False,
        annotations: Mapping[str, str] | None = None,
    ) -> Self:
        """Create or patch one Kubernetes Deployment from intent-level fields."""
        return await cls.upsert_manifest(
            kube,
            manifest=cls._manifest(
                namespace=namespace,
                name=name,
                labels=labels,
                selector=selector,
                containers=containers,
                volumes=volumes,
                replicas=replicas,
                automount_service_account_token=automount_service_account_token,
                annotations=annotations,
            ),
            timeout=timeout,
        )

    @classmethod
    async def upsert_manifest(
        cls,
        kube: Kube,
        *,
        manifest: Mapping[str, object],
        timeout: float,
    ) -> Self:
        """Create or patch one Kubernetes Deployment manifest."""
        namespace, name = _manifest_identity(manifest)
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
        """Re-read this Deployment by identity."""
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
        """Delete this Deployment from the cluster."""
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
        """Wait until this Deployment is deleted from the cluster."""
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
        """Wait until this Deployment has at least `minimum` available replicas."""
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
        """Patch this Deployment's desired replica count."""
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
