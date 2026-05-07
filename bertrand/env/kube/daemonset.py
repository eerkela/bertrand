"""Wrappers for the Kubernetes DaemonSet API and related operations."""

from __future__ import annotations

import asyncio
import builtins
from collections.abc import Collection, Mapping
from dataclasses import dataclass
from types import MappingProxyType
from typing import Self

import kubernetes

from .api import ContainerSpec, Kube, VolumeSpec, _label_selector, _pod_template_manifest

DAEMONSET_WAIT_POLL_INTERVAL_SECONDS = 0.5


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
        """Read one Kubernetes DaemonSet by name."""
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
            raise OSError(
                f"malformed Kubernetes DaemonSet payload for {name!r} in namespace {namespace!r}"
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
        """List Kubernetes DaemonSets with optional namespace and label filtering."""
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
                raise OSError("malformed Kubernetes DaemonSet list payload")
            for item in payload.items or []:
                if not isinstance(item, kubernetes.client.V1DaemonSet):
                    raise OSError("malformed Kubernetes DaemonSet entry in list payload")
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
        """Create or patch one Kubernetes DaemonSet from intent-level fields."""
        namespace = namespace.strip()
        name = name.strip()
        if not namespace or not name:
            raise OSError("DaemonSet upsert requires non-empty namespace and name")
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
            if not isinstance(created, kubernetes.client.V1DaemonSet):
                raise OSError(f"malformed Kubernetes DaemonSet payload while creating {name!r}")
            return cls(obj=created)
        except OSError as err:
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise

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
            raise OSError(f"malformed Kubernetes DaemonSet payload while patching {name!r}")
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
    def number_available(self) -> int:
        """
        Returns
        -------
        int
            Available DaemonSet pod count, or zero when unavailable.
        """
        status = self.obj.status
        return int(status.number_available or 0) if status is not None else 0

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this DaemonSet by name and namespace."""
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            raise OSError("cannot refresh DaemonSet with missing metadata.name/namespace")
        return await type(self).get(kube, namespace=namespace, name=name, timeout=timeout)

    async def wait_available(
        self,
        kube: Kube,
        *,
        timeout: float,
        minimum: int = 1,
    ) -> Self:
        """Wait until this DaemonSet reports at least `minimum` available pods."""
        if minimum < 0:
            raise ValueError("DaemonSet availability minimum cannot be negative")
        if timeout <= 0:
            raise TimeoutError(f"timed out waiting for DaemonSet {self.namespace}/{self.name}")
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        current: Self = self
        while True:
            if current.number_available >= minimum:
                return current
            remaining = deadline - loop.time()
            if remaining <= 0:
                raise TimeoutError(f"timed out waiting for DaemonSet {self.namespace}/{self.name}")
            await asyncio.sleep(min(DAEMONSET_WAIT_POLL_INTERVAL_SECONDS, remaining))
            refreshed = await current.refresh(kube, timeout=deadline - loop.time())
            if refreshed is None:
                raise OSError(f"DaemonSet {self.namespace}/{self.name} disappeared")
            current = refreshed
