"""Wrappers for the Kubernetes DaemonSet API and related operations."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING, Self

import kubernetes

from .api import ContainerSpec, Kube, ProbeSpec, VolumeSpec, _label_selector

DAEMONSET_WAIT_POLL_INTERVAL_SECONDS = 0.5

if TYPE_CHECKING:
    import builtins
    from collections.abc import Collection, Mapping


def _probe_manifest(probe: ProbeSpec) -> dict[str, object]:
    payload = dict(probe.handler)
    if probe.initial_delay_seconds is not None:
        payload["initialDelaySeconds"] = probe.initial_delay_seconds
    if probe.period_seconds is not None:
        payload["periodSeconds"] = probe.period_seconds
    if probe.failure_threshold is not None:
        payload["failureThreshold"] = probe.failure_threshold
    return payload


def _container_manifest(container: ContainerSpec) -> dict[str, object]:
    payload: dict[str, object] = {
        "name": container.name,
        "image": container.image,
    }
    if container.image_pull_policy is not None:
        payload["imagePullPolicy"] = container.image_pull_policy
    if container.command is not None:
        payload["command"] = list(container.command)
    if container.args is not None:
        payload["args"] = list(container.args)
    if container.ports:
        payload["ports"] = [
            {
                "name": port.name,
                "containerPort": port.container_port,
                "protocol": port.protocol,
            }
            for port in container.ports
        ]
    if container.env:
        env: list[dict[str, object]] = []
        for var in container.env:
            sources = sum(value is not None for value in (var.value, var.field_path))
            if sources != 1:
                msg = "environment variable must define exactly one source"
                raise ValueError(msg)
            item: dict[str, object] = {"name": var.name}
            if var.value is not None:
                item["value"] = var.value
            elif var.field_path is not None:
                item["valueFrom"] = {"fieldRef": {"fieldPath": var.field_path}}
            env.append(item)
        payload["env"] = env
    if container.readiness_probe is not None:
        payload["readinessProbe"] = _probe_manifest(container.readiness_probe)
    if container.liveness_probe is not None:
        payload["livenessProbe"] = _probe_manifest(container.liveness_probe)
    if container.volume_mounts:
        payload["volumeMounts"] = [
            {
                key: value
                for key, value in {
                    "name": mount.name,
                    "mountPath": mount.mount_path,
                    "readOnly": mount.read_only,
                }.items()
                if value is not None
            }
            for mount in container.volume_mounts
        ]
    if container.security_context is not None:
        payload["securityContext"] = dict(container.security_context)
    return payload


def _volume_manifest(volume: VolumeSpec) -> dict[str, object]:
    kinds = sum(
        value is not None
        for value in (
            volume.empty_dir_config,
            volume.config_map_name,
            volume.persistent_volume_claim,
            volume.host_path_path,
        )
    )
    if kinds != 1:
        msg = "Kubernetes volume must define exactly one source"
        raise ValueError(msg)

    payload: dict[str, object] = {"name": volume.name}
    if volume.empty_dir_config is not None:
        payload["emptyDir"] = dict(volume.empty_dir_config)
    elif volume.config_map_name is not None:
        config_map: dict[str, object] = {"name": volume.config_map_name}
        if volume.config_map_optional is not None:
            config_map["optional"] = volume.config_map_optional
        payload["configMap"] = config_map
    elif volume.persistent_volume_claim is not None:
        payload["persistentVolumeClaim"] = {"claimName": volume.persistent_volume_claim}
    elif volume.host_path_path is not None:
        host_path: dict[str, object] = {"path": volume.host_path_path}
        if volume.host_path_type is not None:
            host_path["type"] = volume.host_path_type
        payload["hostPath"] = host_path
    return payload


def _pod_template_manifest(
    *,
    labels: Mapping[str, str],
    containers: Collection[ContainerSpec],
    volumes: Collection[VolumeSpec],
    automount_service_account_token: bool,
    service_account_name: str | None,
    node_selector: Mapping[str, str] | None,
    host_pid: bool | None,
) -> dict[str, object]:
    spec: dict[str, object] = {
        "automountServiceAccountToken": automount_service_account_token,
        "containers": [_container_manifest(container) for container in containers],
        "volumes": [_volume_manifest(volume) for volume in volumes],
    }
    if service_account_name is not None:
        service_account_name = service_account_name.strip()
        if service_account_name:
            spec["serviceAccountName"] = service_account_name
    if node_selector:
        spec["nodeSelector"] = dict(node_selector)
    if host_pid is not None:
        spec["hostPID"] = host_pid
    return {
        "metadata": {"labels": dict(labels)},
        "spec": spec,
    }


@dataclass(frozen=True)
class DaemonSet:
    """General-purpose wrapper around one Kubernetes DaemonSet object."""

    obj: kubernetes.client.V1DaemonSet

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        timeout: float,
    ) -> Self | None:
        """Read one Kubernetes DaemonSet by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the DaemonSet.
        name : str
            DaemonSet name to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        DaemonSet | None
            Wrapped Kubernetes DaemonSet, or `None` if it does not exist.

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or the API call fails.
        """
        payload = await kube.run(
            lambda request_timeout: kube.apps.read_namespaced_daemon_set(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to read DaemonSet {name!r} in namespace {namespace!r}",
        )
        if payload is None:
            return None
        if not isinstance(payload, kubernetes.client.V1DaemonSet):
            msg = (
                f"malformed Kubernetes DaemonSet payload for {name!r} "
                f"in namespace {namespace!r}"
            )
            raise OSError(msg)
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
        """List Kubernetes DaemonSets with optional namespace and label filtering.

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
        list[DaemonSet]
            Wrapped Kubernetes DaemonSets matching the requested filters.

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or a list call fails.
        """
        label_selector = _label_selector(labels)
        payloads: builtins.list[kubernetes.client.V1DaemonSetList] = []
        if namespaces is None:
            payload = await kube.run(
                lambda request_timeout: kube.apps.list_daemon_set_for_all_namespaces(
                    label_selector=label_selector,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context="failed to list DaemonSets across all namespaces",
            )
            if payload is not None:
                payloads.append(payload)
        else:
            normalized = {namespace.strip() for namespace in namespaces}
            normalized.discard("")
            for namespace in sorted(normalized):
                payload = await kube.run(
                    lambda request_timeout, namespace=namespace: (
                        kube.apps.list_namespaced_daemon_set(
                            namespace=namespace,
                            label_selector=label_selector,
                            _request_timeout=request_timeout,
                        )
                    ),
                    timeout=timeout,
                    context=f"failed to list DaemonSets in namespace {namespace!r}",
                )
                if payload is not None:
                    payloads.append(payload)

        out: builtins.list[Self] = []
        for payload in payloads:
            if not isinstance(payload, kubernetes.client.V1DaemonSetList):
                msg = "malformed Kubernetes DaemonSet list payload"
                raise OSError(msg)
            for item in payload.items or []:
                if not isinstance(item, kubernetes.client.V1DaemonSet):
                    msg = "malformed Kubernetes DaemonSet entry in list payload"
                    raise OSError(msg)
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
        automount_service_account_token: bool,
        annotations: Mapping[str, str] | None,
        service_account_name: str | None,
        node_selector: Mapping[str, str] | None,
        host_pid: bool | None,
    ) -> dict[str, object]:
        template_labels = dict(labels)
        template_labels.update(selector)
        return {
            "apiVersion": "apps/v1",
            "kind": "DaemonSet",
            "metadata": {
                "name": name,
                "namespace": namespace,
                "labels": dict(labels),
                "annotations": dict(annotations or {}),
            },
            "spec": {
                "selector": {"matchLabels": dict(selector)},
                "template": _pod_template_manifest(
                    labels=template_labels,
                    containers=containers,
                    volumes=volumes,
                    automount_service_account_token=automount_service_account_token,
                    service_account_name=service_account_name,
                    node_selector=node_selector,
                    host_pid=host_pid,
                ),
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
        automount_service_account_token: bool = False,
        annotations: Mapping[str, str] | None = None,
        service_account_name: str | None = None,
        node_selector: Mapping[str, str] | None = None,
        host_pid: bool | None = None,
    ) -> Self:
        """Create or patch one Kubernetes DaemonSet from intent-level fields.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the DaemonSet.
        name : str
            DaemonSet name to create or patch.
        labels : Mapping[str, str]
            Labels to apply to the DaemonSet and pod template.
        selector : Mapping[str, str]
            Immutable pod selector labels for the DaemonSet.
        containers : Collection[ContainerSpec]
            Pod containers to render into the DaemonSet template.
        volumes : Collection[VolumeSpec]
            Pod volumes to render into the DaemonSet template.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        automount_service_account_token : bool, optional
            Whether pods should automount the default service-account token.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to `metadata.annotations`.
        service_account_name : str | None, optional
            Optional pod service account name.
        node_selector : Mapping[str, str] | None, optional
            Optional pod node selector.
        host_pid : bool | None, optional
            Optional pod `hostPID` value.

        Returns
        -------
        DaemonSet
            Wrapped created or patched DaemonSet.

        Raises
        ------
        OSError
            If namespace/name are empty, or Kubernetes create/patch fails or returns
            malformed data.
        """
        namespace = namespace.strip()
        name = name.strip()
        if not namespace or not name:
            msg = "DaemonSet upsert requires non-empty namespace and name"
            raise OSError(msg)
        manifest = cls._manifest(
            namespace=namespace,
            name=name,
            labels=labels,
            selector=selector,
            containers=containers,
            volumes=volumes,
            automount_service_account_token=automount_service_account_token,
            annotations=annotations,
            service_account_name=service_account_name,
            node_selector=node_selector,
            host_pid=host_pid,
        )
        try:
            created = await kube.run(
                lambda request_timeout: kube.apps.create_namespaced_daemon_set(
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=f"failed to create DaemonSet {namespace}/{name}",
            )
        except OSError as err:
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise
        else:
            if not isinstance(created, kubernetes.client.V1DaemonSet):
                msg = f"malformed Kubernetes DaemonSet payload while creating {name!r}"
                raise OSError(msg)
            return cls(obj=created)

        patched = await kube.run(
            lambda request_timeout: kube.apps.patch_namespaced_daemon_set(
                name=name,
                namespace=namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to patch DaemonSet {namespace}/{name}",
        )
        if not isinstance(patched, kubernetes.client.V1DaemonSet):
            msg = f"malformed Kubernetes DaemonSet payload while patching {name!r}"
            raise OSError(msg)
        return cls(obj=patched)

    @property
    def name(self) -> str:
        """Return this DaemonSet's name.

        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def namespace(self) -> str:
        """Return this DaemonSet's namespace.

        Returns
        -------
        str
            Trimmed `metadata.namespace`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.namespace or "").strip() if metadata is not None else ""

    @property
    def labels(self) -> Mapping[str, str]:
        """Return this DaemonSet's labels.

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
    def number_available(self) -> int:
        """Return this DaemonSet's available pod count.

        Returns
        -------
        int
            Available DaemonSet pod count, or zero when unavailable.
        """
        status = self.obj.status
        return int(status.number_available or 0) if status is not None else 0

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this DaemonSet by name and namespace.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        DaemonSet | None
            Fresh wrapper for the same DaemonSet, or `None` if it no longer exists.

        Raises
        ------
        OSError
            If this wrapper does not contain enough metadata to identify the
            DaemonSet, or if Kubernetes returns malformed data.
        """
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = "cannot refresh DaemonSet with missing metadata.name/namespace"
            raise OSError(msg)
        return await type(self).get(
            kube, namespace=namespace, name=name, timeout=timeout
        )

    async def wait_available(
        self,
        kube: Kube,
        *,
        timeout: float,
        minimum: int = 1,
    ) -> Self:
        """Wait until this DaemonSet reports at least `minimum` available pods.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait budget in seconds. If infinite, wait indefinitely.
        minimum : int, optional
            Minimum available pod count required before returning.

        Returns
        -------
        DaemonSet
            Refreshed DaemonSet wrapper that satisfied the availability condition.

        Raises
        ------
        ValueError
            If `minimum` is negative.
        TimeoutError
            If the DaemonSet does not become available before `timeout`.
        OSError
            If this wrapper cannot be refreshed or disappears while waiting.
        """
        if minimum < 0:
            msg = "DaemonSet availability minimum cannot be negative"
            raise ValueError(msg)
        if timeout <= 0:
            msg = f"timed out waiting for DaemonSet {self.namespace}/{self.name}"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        current: Self = self
        while True:
            if current.number_available >= minimum:
                return current
            remaining = deadline - loop.time()
            if remaining <= 0:
                msg = f"timed out waiting for DaemonSet {self.namespace}/{self.name}"
                raise TimeoutError(msg)
            await asyncio.sleep(min(DAEMONSET_WAIT_POLL_INTERVAL_SECONDS, remaining))
            refreshed = await current.refresh(kube, timeout=deadline - loop.time())
            if refreshed is None:
                msg = f"DaemonSet {self.namespace}/{self.name} disappeared"
                raise OSError(msg)
            current = refreshed
