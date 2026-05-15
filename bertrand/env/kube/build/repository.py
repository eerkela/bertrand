"""Cluster-owned OCI image repository for Bertrand's Kubernetes build runtime."""

from __future__ import annotations

import asyncio
import hashlib
import json
import math
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import uuid
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import TYPE_CHECKING

from bertrand.env.git import BERTRAND_ENV, BERTRAND_NAMESPACE, INFINITY, run, sudo
from bertrand.env.kube.api.client import (
    CLUSTER_REGISTRY_READY_LABEL,
    CLUSTER_REGISTRY_READY_VALUE,
    Kube,
)
from bertrand.env.kube.api.spec import (
    ContainerPortSpec,
    ContainerSpec,
    DeploymentStrategySpec,
    PodTemplateSpec,
    ProbeSpec,
    ServicePortSpec,
    VolumeMountSpec,
    VolumeSpec,
)
from bertrand.env.kube.build.daemon import (
    BUILDKIT_CONFIG_KEY,
    BUILDKIT_CONFIG_NAME,
    buildkit_worker_gc_toml,
)
from bertrand.env.kube.build.execution import run_observed_job
from bertrand.env.kube.build.refs import (
    DIGEST_RE,
    IMAGE_REF_COMPONENT_RE,
    rewrite_registry_ref,
    validate_tag,
)
from bertrand.env.kube.ceph.volume import CEPHFS_STORAGE_CLASS_PREFERENCES
from bertrand.env.kube.configmap import ConfigMap
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.job import Job
from bertrand.env.kube.network import NetworkProfile
from bertrand.env.kube.node import Node
from bertrand.env.kube.service import Service
from bertrand.env.kube.volume import PersistentVolumeClaim, StorageClass

if TYPE_CHECKING:
    from collections.abc import Awaitable, Callable, Mapping

IMAGE_REPOSITORY_NAME = "bertrand-registry"
IMAGE_REPOSITORY_IMAGE = "registry:2"
IMAGE_REPOSITORY_PORT = 5000
IMAGE_REPOSITORY_NODE_PORT = 32000
IMAGE_REPOSITORY_PULL_HOST = f"localhost:{IMAGE_REPOSITORY_NODE_PORT}"
IMAGE_REPOSITORY_SERVICE_ADDR = (
    f"{IMAGE_REPOSITORY_NAME}.{BERTRAND_NAMESPACE}.svc.cluster.local:"
    f"{IMAGE_REPOSITORY_PORT}"
)
IMAGE_REPOSITORY_SIZE = "4Gi"
IMAGE_REPOSITORY_MOUNT = "/var/lib/registry"
IMAGE_REPOSITORY_VOLUME = "registry-state"
IMAGE_REPOSITORY_CONFIG_NAME = f"{IMAGE_REPOSITORY_NAME}-config"
IMAGE_REPOSITORY_CONFIG_KEY = "config.yml"
IMAGE_REPOSITORY_CONFIG_DIR = "/etc/docker/registry"
IMAGE_REPOSITORY_CONFIG_FILE = (
    f"{IMAGE_REPOSITORY_CONFIG_DIR}/{IMAGE_REPOSITORY_CONFIG_KEY}"
)
IMAGE_REPOSITORY_CONFIG_VOLUME = "registry-config"
IMAGE_REPOSITORY_CONFIG_HASH_ANNOTATION = "bertrand.dev/registry-config-hash"
IMAGE_REPOSITORY_LABEL = "bertrand.dev/image-repository"
IMAGE_REPOSITORY_LABEL_VALUE = "v1"
IMAGE_REPOSITORY_GC_JOB_LABEL = "bertrand.dev/registry-gc-job"
IMAGE_REPOSITORY_GC_JOB_LABEL_VALUE = "v1"
IMAGE_REPOSITORY_GC_TTL_SECONDS = 3600
IMAGE_REPOSITORY_GC_LOG_TAIL_LINES = 120
IMAGE_REPOSITORY_GC_DIAGNOSTIC_TIMEOUT_SECONDS = 10.0
IMAGE_REPOSITORY_GC_CLEANUP_TIMEOUT_SECONDS = 10.0
IMAGE_REPOSITORY_GC_RESTORE_TIMEOUT_SECONDS = 120.0
IMAGE_REPOSITORY_MAINTENANCE_NAME = f"{IMAGE_REPOSITORY_NAME}-maintenance"
IMAGE_REPOSITORY_MAINTENANCE_LABEL = "bertrand.dev/image-repository-maintenance"
IMAGE_REPOSITORY_MAINTENANCE_LABEL_VALUE = "v1"
IMAGE_REPOSITORY_MAINTENANCE_REASON_GC = "storage-gc"
IMAGE_REPOSITORY_MAINTENANCE_MESSAGE_GC = (
    "image registry maintenance is running; build is queued"
)
IMAGE_REPOSITORY_ROUTE_POLL_INTERVAL_SECONDS = 0.5
IMAGE_REPOSITORY_ROUTE_REQUEST_TIMEOUT_SECONDS = 2.0
IMAGE_REPOSITORY_ROUTE_READY_STATUS = frozenset({200, 401})
IMAGE_REPOSITORY_DELETE_SUCCESS_STATUS = frozenset({202, 404})


def _config_hash(data: Mapping[str, str]) -> str:
    payload = json.dumps(data, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def _registry_route_status(url: str, timeout: float) -> int:
    request = urllib.request.Request(url, method="GET")
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return int(response.status)
    except urllib.error.HTTPError as err:
        return int(err.code)


def _registry_manifest_delete(url: str, timeout: float | None) -> int:
    request = urllib.request.Request(url, method="DELETE")
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return int(response.status)
    except urllib.error.HTTPError as err:
        return int(err.code)


@dataclass(frozen=True)
class ImageRepositoryStatus:
    """Read-only readiness report for Bertrand's OCI image repository.

    Parameters
    ----------
    namespace : str
        Namespace that owns the registry resources.
    name : str
        Registry Service, Deployment, and PVC name.
    storage : str
        Concise PVC/storage readiness summary.
    rollout : str
        Concise Deployment rollout summary.
    trusted_nodes : tuple[str, ...]
        Node names with the registry-ready label.
    untrusted_nodes : tuple[str, ...]
        Node names missing the registry-ready label.
    ready : bool
        Whether the registry Service, Deployment rollout, PVC, BuildKit config, and
        node trust labels are ready.
    failures : tuple[str, ...]
        Human-readable readiness failures, empty when the repository is ready.
    """

    namespace: str
    name: str
    storage: str
    rollout: str
    trusted_nodes: tuple[str, ...]
    untrusted_nodes: tuple[str, ...]
    ready: bool
    failures: tuple[str, ...]


@dataclass(frozen=True)
class ImageRepositoryMaintenanceStatus:
    """Read-only registry maintenance report.

    Parameters
    ----------
    active : bool
        Whether registry maintenance is currently active.
    reason : str
        Stable maintenance reason identifier.
    started_at : datetime | None
        Timestamp recorded when maintenance began.
    message : str
        Concise user-facing maintenance message.
    dirty_count : int
        Number of collected image records since the last registry storage GC.
    dirty_since : datetime | None
        Timestamp when registry storage first became dirty.
    last_gc_at : datetime | None
        Timestamp for the last successful registry storage GC.
    """

    active: bool
    reason: str = ""
    started_at: datetime | None = None
    message: str = ""
    dirty_count: int = 0
    dirty_since: datetime | None = None
    last_gc_at: datetime | None = None

    @staticmethod
    def _datetime_payload(value: datetime | None) -> str:
        return value.isoformat() if value is not None else ""

    @staticmethod
    def _parse_datetime(field: str, value: str) -> datetime | None:
        value = value.strip()
        if not value:
            return None
        try:
            parsed = datetime.fromisoformat(value)
        except ValueError as err:
            msg = f"registry maintenance status field {field!r} is not a timestamp"
            raise OSError(msg) from err
        if parsed.tzinfo is None:
            return parsed.replace(tzinfo=UTC)
        return parsed.astimezone(UTC)

    @staticmethod
    def _parse_count(value: str) -> int:
        value = value.strip()
        if not value:
            return 0
        try:
            count = int(value)
        except ValueError as err:
            msg = "registry maintenance dirty_count is not an integer"
            raise OSError(msg) from err
        if count < 0:
            msg = "registry maintenance dirty_count cannot be negative"
            raise OSError(msg)
        return count

    @classmethod
    def from_data(
        cls,
        data: Mapping[str, str],
    ) -> ImageRepositoryMaintenanceStatus:
        """Parse ConfigMap data into registry maintenance status.

        Parameters
        ----------
        data : Mapping[str, str]
            Raw ConfigMap data.

        Returns
        -------
        ImageRepositoryMaintenanceStatus
            Parsed maintenance status.
        """
        return cls(
            active=data.get("active", "").strip().lower() == "true",
            reason=data.get("reason", "").strip(),
            started_at=cls._parse_datetime("started_at", data.get("started_at", "")),
            message=data.get("message", "").strip(),
            dirty_count=cls._parse_count(data.get("dirty_count", "")),
            dirty_since=cls._parse_datetime("dirty_since", data.get("dirty_since", "")),
            last_gc_at=cls._parse_datetime("last_gc_at", data.get("last_gc_at", "")),
        )

    def data(self) -> dict[str, str]:
        """Render registry maintenance status as ConfigMap data.

        Returns
        -------
        dict[str, str]
            Deterministic ConfigMap data payload.

        Raises
        ------
        OSError
            If `dirty_count` is negative.
        """
        if self.dirty_count < 0:
            msg = "registry maintenance dirty_count cannot be negative"
            raise OSError(msg)
        return {
            "active": "true" if self.active else "false",
            "reason": self.reason,
            "started_at": self._datetime_payload(self.started_at),
            "message": self.message,
            "dirty_count": str(self.dirty_count),
            "dirty_since": self._datetime_payload(self.dirty_since),
            "last_gc_at": self._datetime_payload(self.last_gc_at),
        }

    @property
    def storage_dirty(self) -> bool:
        """Return whether registry storage GC is due.

        Returns
        -------
        bool
            Whether manifest lifecycle GC has marked registry storage dirty.
        """
        return self.dirty_count > 0


@dataclass(frozen=True)
class ImageRepository:
    """Stable in-cluster OCI image repository for Bertrand workloads.

    Attributes
    ----------
    namespace : str
        Kubernetes namespace that owns the registry resources.
    service : str
        Kubernetes Service and Deployment name for the registry.
    port : int
        Registry container and Service port.
    node_port : int
        NodePort used by MicroK8s/containerd for stable `localhost` pulls.
    pull_host : str
        Logical registry host used in Kubernetes image references.
    service_addr : str
        In-cluster registry Service address used by BuildKit registry routing.
    storage_request : str
        Minimum PVC size requested for registry storage.
    """

    namespace: str
    service: str
    port: int
    node_port: int
    pull_host: str
    service_addr: str
    storage_request: str

    @property
    def pull_server(self) -> str:
        """Return the HTTP registry server address.

        Returns
        -------
        str
            Logical registry server address used in Kubernetes image references.
        """
        return f"http://{self.pull_host}"

    @property
    def trust_hosts(self) -> tuple[str, ...]:
        """Return local registry aliases trusted by containerd.

        Returns
        -------
        tuple[str, ...]
            Registry host aliases that should be trusted by containerd.
        """
        return (self.pull_host, f"127.0.0.1:{self.node_port}")

    @property
    def labels(self) -> dict[str, str]:
        """Return labels shared by image repository resources.

        Returns
        -------
        dict[str, str]
            Labels shared by the image repository resources.
        """
        return {
            "app.kubernetes.io/name": self.service,
            "app.kubernetes.io/part-of": "bertrand",
            BERTRAND_ENV: "1",
            IMAGE_REPOSITORY_LABEL: IMAGE_REPOSITORY_LABEL_VALUE,
        }

    @property
    def maintenance_labels(self) -> dict[str, str]:
        """Return labels shared by registry maintenance resources.

        Returns
        -------
        dict[str, str]
            Labels applied to the registry maintenance status ConfigMap.
        """
        return {
            **self.labels,
            IMAGE_REPOSITORY_MAINTENANCE_LABEL: (
                IMAGE_REPOSITORY_MAINTENANCE_LABEL_VALUE
            ),
        }

    @property
    def selector(self) -> dict[str, str]:
        """Return the image repository pod selector.

        Returns
        -------
        dict[str, str]
            Labels used to bind the image repository Service to its pods.
        """
        return {
            "app.kubernetes.io/name": self.service,
            IMAGE_REPOSITORY_LABEL: IMAGE_REPOSITORY_LABEL_VALUE,
        }

    def buildkit_config_data(self, profile: NetworkProfile) -> dict[str, str]:
        """Return BuildKit daemon ConfigMap data.

        Parameters
        ----------
        profile : NetworkProfile
            Cluster networking profile to compose into BuildKit daemon
            configuration.

        Returns
        -------
        dict[str, str]
            Data payload for the BuildKit daemon configuration ConfigMap.
        """
        network_config = profile.buildkit_toml()
        registry_config = (
            f"[registry.\"{self.pull_host}\"]\n"
            f"  mirrors = [\"{self.service_addr}\"]\n"
            "  http = true\n"
            "  insecure = true\n"
        )
        fragments = [buildkit_worker_gc_toml()]
        if network_config:
            fragments.append(network_config)
        fragments.append(registry_config)
        return {BUILDKIT_CONFIG_KEY: "\n".join(fragments)}

    def registry_config_data(self, *, read_only: bool = False) -> dict[str, str]:
        """Return OCI registry ConfigMap data.

        Parameters
        ----------
        read_only : bool, optional
            Whether the registry should reject writes for storage maintenance.

        Returns
        -------
        dict[str, str]
            Data payload for the registry configuration ConfigMap.
        """
        readonly = "true" if read_only else "false"
        return {
            IMAGE_REPOSITORY_CONFIG_KEY: (
                "version: 0.1\n"
                "log:\n"
                "  level: info\n"
                "storage:\n"
                "  filesystem:\n"
                f"    rootdirectory: {IMAGE_REPOSITORY_MOUNT}\n"
                "  delete:\n"
                "    enabled: true\n"
                "  maintenance:\n"
                "    readonly:\n"
                f"      enabled: {readonly}\n"
                "http:\n"
                f"  addr: 0.0.0.0:{self.port}\n"
            )
        }

    async def maintenance_status(
        self,
        kube: Kube,
        *,
        timeout: float = INFINITY,
    ) -> ImageRepositoryMaintenanceStatus:
        """Read the current registry maintenance status.

        Parameters
        ----------
        kube : Kube
            Kubernetes API client for the target cluster.
        timeout : float, optional
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        ImageRepositoryMaintenanceStatus
            Current registry maintenance status. Missing status means inactive.
        """
        status = await ConfigMap.get(
            kube,
            namespace=self.namespace,
            name=IMAGE_REPOSITORY_MAINTENANCE_NAME,
            timeout=timeout,
        )
        if status is None:
            return ImageRepositoryMaintenanceStatus(active=False)
        return ImageRepositoryMaintenanceStatus.from_data(status.data)

    async def _write_maintenance_status(
        self,
        kube: Kube,
        *,
        status: ImageRepositoryMaintenanceStatus,
        timeout: float,
    ) -> None:
        await ConfigMap.upsert(
            kube,
            namespace=self.namespace,
            name=IMAGE_REPOSITORY_MAINTENANCE_NAME,
            labels=self.maintenance_labels,
            data=status.data(),
            timeout=timeout,
        )

    async def start_maintenance(
        self,
        kube: Kube,
        *,
        reason: str,
        message: str,
        timeout: float = INFINITY,
    ) -> None:
        """Mark registry maintenance as active.

        Parameters
        ----------
        kube : Kube
            Kubernetes API client for the target cluster.
        reason : str
            Stable maintenance reason identifier.
        message : str
            Concise user-facing maintenance message.
        timeout : float, optional
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Raises
        ------
        OSError
            If `reason` or `message` is empty, or Kubernetes upsert fails.
        """
        reason = reason.strip()
        message = message.strip()
        if not reason or not message:
            msg = "registry maintenance status requires reason and message"
            raise OSError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        current = await self.maintenance_status(kube, timeout=deadline - loop.time())
        await self._write_maintenance_status(
            kube,
            status=ImageRepositoryMaintenanceStatus(
                active=True,
                reason=reason,
                started_at=datetime.now(UTC),
                message=message,
                dirty_count=current.dirty_count,
                dirty_since=current.dirty_since,
                last_gc_at=current.last_gc_at,
            ),
            timeout=deadline - loop.time(),
        )

    async def clear_maintenance(
        self,
        kube: Kube,
        *,
        timeout: float = INFINITY,
    ) -> None:
        """Clear registry maintenance status.

        Parameters
        ----------
        kube : Kube
            Kubernetes API client for the target cluster.
        timeout : float, optional
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        current = await self.maintenance_status(kube, timeout=deadline - loop.time())
        await self._write_maintenance_status(
            kube,
            status=ImageRepositoryMaintenanceStatus(
                active=False,
                dirty_count=current.dirty_count,
                dirty_since=current.dirty_since,
                last_gc_at=current.last_gc_at,
            ),
            timeout=deadline - loop.time(),
        )

    async def mark_storage_dirty(
        self,
        kube: Kube,
        *,
        count: int,
        timeout: float = INFINITY,
    ) -> None:
        """Mark registry storage as needing garbage collection.

        Parameters
        ----------
        kube : Kube
            Kubernetes API client for the target cluster.
        count : int
            Number of newly collected image records to add to the dirty count.
        timeout : float, optional
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Raises
        ------
        ValueError
            If `count` is negative.
        """
        if count < 0:
            msg = "registry storage dirty count cannot be negative"
            raise ValueError(msg)
        if count == 0:
            return
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        current = await self.maintenance_status(kube, timeout=deadline - loop.time())
        await self._write_maintenance_status(
            kube,
            status=ImageRepositoryMaintenanceStatus(
                active=current.active,
                reason=current.reason,
                started_at=current.started_at,
                message=current.message,
                dirty_count=current.dirty_count + count,
                dirty_since=current.dirty_since or datetime.now(UTC),
                last_gc_at=current.last_gc_at,
            ),
            timeout=deadline - loop.time(),
        )

    async def clear_storage_dirty(
        self,
        kube: Kube,
        *,
        last_gc_at: datetime | None = None,
        timeout: float = INFINITY,
    ) -> None:
        """Clear the durable registry storage dirty marker.

        Parameters
        ----------
        kube : Kube
            Kubernetes API client for the target cluster.
        last_gc_at : datetime | None, optional
            Timestamp to record for a successful registry storage GC. If omitted,
            preserve the existing timestamp.
        timeout : float, optional
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        current = await self.maintenance_status(kube, timeout=deadline - loop.time())
        await self._write_maintenance_status(
            kube,
            status=ImageRepositoryMaintenanceStatus(
                active=current.active,
                reason=current.reason,
                started_at=current.started_at,
                message=current.message,
                dirty_count=0,
                dirty_since=None,
                last_gc_at=last_gc_at or current.last_gc_at,
            ),
            timeout=deadline - loop.time(),
        )

    async def current_buildkit_config_data(
        self,
        kube: Kube,
        *,
        timeout: float,
    ) -> dict[str, str]:
        """Return BuildKit daemon ConfigMap data for the active cluster profile.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        dict[str, str]
            Data payload for the BuildKit daemon configuration ConfigMap.
        """
        profile = await NetworkProfile.get(kube, timeout=timeout)
        return self.buildkit_config_data(profile)

    async def current_buildkit_config_hash(
        self,
        kube: Kube,
        *,
        timeout: float,
    ) -> str:
        """Return the expected BuildKit daemon ConfigMap hash.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        str
            SHA-256 digest of the BuildKit daemon configuration data.
        """
        return _config_hash(
            await self.current_buildkit_config_data(kube, timeout=timeout)
        )

    async def ensure_trust(self, *, timeout: float = INFINITY) -> None:
        """Converge local MicroK8s containerd trust for this repository.

        Parameters
        ----------
        timeout : float, optional
            Maximum runtime budget in seconds. If infinite, wait indefinitely.

        Raises
        ------
        TimeoutError
            If `timeout` is non-positive or host file updates exceed the budget.
        """
        if timeout <= 0:
            msg = "image repository trust timeout must be non-negative"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        content = (
            f"server = \"{self.pull_server}\"\n"
            f"[host.\"{self.pull_server}\"]\n"
            "  capabilities = [\"pull\", \"resolve\", \"push\"]\n"
            "  skip_verify = true\n"
        )
        with tempfile.NamedTemporaryFile(
            "w",
            encoding="utf-8",
            delete=True,
            delete_on_close=True,
        ) as handle:
            handle.write(content)
            handle.flush()
            staged = Path(handle.name)
            for host in self.trust_hosts:
                trust_dir = Path(f"/var/snap/microk8s/current/args/certs.d/{host}")
                trust_file = trust_dir / "hosts.toml"
                if trust_file.is_file():
                    try:
                        if trust_file.read_text(encoding="utf-8") == content:
                            continue
                    except OSError:
                        pass
                await run(
                    sudo(["install", "-d", "-m", "0755", str(trust_dir)]),
                    capture_output=True,
                    timeout=deadline - loop.time(),
                )
                await run(
                    sudo(["install", "-m", "0644", str(staged), str(trust_file)]),
                    capture_output=True,
                    timeout=deadline - loop.time(),
                )

    async def ensure_node_trust(
        self,
        kube: Kube,
        *,
        timeout: float = INFINITY,
    ) -> None:
        """Converge local registry trust and mark the local node ready.

        Parameters
        ----------
        kube : Kube
            Kubernetes API client for the target cluster.
        timeout : float, optional
            Maximum runtime budget in seconds. If infinite, wait indefinitely.

        Raises
        ------
        TimeoutError
            If `timeout` is non-positive, local route verification fails, or
            convergence exceeds the budget.
        """
        if timeout <= 0:
            msg = "image repository node-trust timeout must be non-negative"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        await self.ensure_trust(timeout=deadline - loop.time())
        await self.assert_local_route(timeout=deadline - loop.time())
        node = await Node.local(kube, timeout=deadline - loop.time())
        await node.set_label(
            kube=kube,
            label=CLUSTER_REGISTRY_READY_LABEL,
            value=CLUSTER_REGISTRY_READY_VALUE,
            timeout=deadline - loop.time(),
        )

    async def assert_local_route(self, *, timeout: float = INFINITY) -> None:
        """Assert the local registry route is reachable.

        Parameters
        ----------
        timeout : float, optional
            Maximum wait budget in seconds. If infinite, wait indefinitely.

        Raises
        ------
        TimeoutError
            If `timeout` is non-positive or the local registry route does not return
            an accepted status before the deadline.
        """
        if timeout <= 0:
            msg = "image repository local route timeout must be non-negative"
            raise TimeoutError(msg)
        url = f"{self.pull_server}/v2/"
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        last_error = ""
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                msg = f"local image repository route {url!r} is not ready"
                if last_error:
                    msg = f"{msg}: {last_error}"
                raise TimeoutError(msg)
            request_timeout = min(
                IMAGE_REPOSITORY_ROUTE_REQUEST_TIMEOUT_SECONDS,
                remaining,
            )
            try:
                status = await asyncio.to_thread(
                    _registry_route_status,
                    url,
                    request_timeout,
                )
                if status in IMAGE_REPOSITORY_ROUTE_READY_STATUS:
                    return
                last_error = f"HTTP status {status}"
            except (OSError, TimeoutError, urllib.error.URLError) as err:
                last_error = str(err)
            await asyncio.sleep(
                min(IMAGE_REPOSITORY_ROUTE_POLL_INTERVAL_SECONDS, remaining),
            )

    async def assert_node_trust(
        self,
        kube: Kube,
        *,
        timeout: float = INFINITY,
    ) -> None:
        """Assert all cluster nodes are marked ready for registry pulls.

        Parameters
        ----------
        kube : Kube
            Kubernetes API client for the target cluster.
        timeout : float, optional
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Raises
        ------
        OSError
            If any cluster node is missing the registry-ready label.
        """
        nodes = await Node.list(kube=kube, timeout=timeout)
        ready = {
            node.name
            for node in nodes
            if node.name
            and node.labels.get(CLUSTER_REGISTRY_READY_LABEL)
            == CLUSTER_REGISTRY_READY_VALUE
        }
        missing = sorted(
            node.name for node in nodes if node.name and node.name not in ready
        )
        if missing:
            msg = (
                "build runtime rollout blocked: registry trust label is missing on "
                f"node(s): {', '.join(missing)}. Run `bertrand init` on those "
                "hosts first to converge registry trust and mark them ready."
            )
            raise OSError(msg)

    async def status(
        self,
        kube: Kube,
        *,
        timeout: float = INFINITY,
    ) -> ImageRepositoryStatus:
        """Inspect repository readiness without mutating the cluster.

        Parameters
        ----------
        kube : Kube
            Kubernetes API client for the target cluster.
        timeout : float, optional
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        ImageRepositoryStatus
            Read-only image repository readiness report.

        Raises
        ------
        TimeoutError
            If `timeout` is non-positive.
        OSError
            If Kubernetes read operations fail or return malformed data.
        """
        if timeout <= 0:
            msg = "image repository status timeout must be non-negative"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout

        try:
            service = await Service.get(
                kube,
                namespace=self.namespace,
                timeout=deadline - loop.time(),
                name=self.service,
            )
            deployment = await Deployment.get(
                kube,
                namespace=self.namespace,
                timeout=deadline - loop.time(),
                name=self.service,
            )
            pvc = await PersistentVolumeClaim.get(
                kube=kube,
                namespace=self.namespace,
                timeout=deadline - loop.time(),
                name=self.service,
            )
            buildkit_config = await ConfigMap.get(
                kube,
                namespace=self.namespace,
                timeout=deadline - loop.time(),
                name=BUILDKIT_CONFIG_NAME,
            )
            registry_config = await ConfigMap.get(
                kube,
                namespace=self.namespace,
                timeout=deadline - loop.time(),
                name=IMAGE_REPOSITORY_CONFIG_NAME,
            )
            maintenance = await self.maintenance_status(
                kube,
                timeout=deadline - loop.time(),
            )
            nodes = await Node.list(kube=kube, timeout=deadline - loop.time())

            expected_port = ServicePortSpec(
                name="registry",
                port=self.port,
                target_port=self.port,
                node_port=self.node_port,
            )
            service_ready = (
                service.matches(
                    service_type="NodePort",
                    selector=self.selector,
                    ports=(expected_port,),
                )
                if service is not None
                else False
            )
            available_replicas = (
                deployment.available_replicas if deployment is not None else 0
            )
            updated_replicas = (
                deployment.updated_replicas if deployment is not None else 0
            )
            observed_generation = (
                deployment.observed_generation if deployment is not None else 0
            )
            generation = deployment.generation if deployment is not None else 0
            rollout_ready = (
                deployment.rollout_ready(minimum=1) if deployment is not None else False
            )
            pvc_managed = (
                all(
                    pvc.labels.get(key) == value
                    for key, value in {
                        BERTRAND_ENV: "1",
                        IMAGE_REPOSITORY_LABEL: IMAGE_REPOSITORY_LABEL_VALUE,
                    }.items()
                )
                if pvc is not None
                else False
            )
            pvc_bound = pvc.is_bound if pvc is not None else False
            pvc_phase = pvc.phase if pvc is not None else "missing"
            storage_class = pvc.storage_class_name if pvc is not None else "missing"
            access_modes = pvc.access_modes if pvc is not None else ()
            storage_request = pvc.requested_storage if pvc is not None else "missing"
            storage_summary = (
                f"{pvc_phase}; class={storage_class}; "
                f"request={storage_request}; modes={','.join(access_modes) or 'none'}"
            )
            rollout_summary = (
                f"available={available_replicas}; updated={updated_replicas}; "
                f"observed={observed_generation}; generation={generation}"
            )
            storage_ready = (
                pvc is not None
                and pvc_managed
                and pvc_bound
                and bool(storage_class)
                and pvc.has_access_mode("ReadWriteMany")
            )
            desired_config_data = await self.current_buildkit_config_data(
                kube,
                timeout=deadline - loop.time(),
            )
            desired_config_hash = _config_hash(desired_config_data)
            installed_config_hash = (
                _config_hash(buildkit_config.data)
                if buildkit_config is not None
                else ""
            )
            config_current = installed_config_hash == desired_config_hash
            desired_registry_config = self.registry_config_data(read_only=False)
            desired_read_only_registry_config = self.registry_config_data(
                read_only=True
            )
            registry_config_current = registry_config is not None and (
                registry_config.data == desired_registry_config
                or (
                    maintenance.active
                    and registry_config.data == desired_read_only_registry_config
                )
            )

            named_nodes = sorted(node.name for node in nodes if node.name)
            trusted = sorted(
                node.name
                for node in nodes
                if node.name
                and node.labels.get(CLUSTER_REGISTRY_READY_LABEL)
                == CLUSTER_REGISTRY_READY_VALUE
            )
            trusted_set = frozenset(trusted)
            untrusted = [name for name in named_nodes if name not in trusted_set]
            node_trust_ready = bool(named_nodes) and not untrusted
            failures: list[str] = []
            if not service_ready:
                failures.append(
                    "image registry Service is missing or has the wrong shape"
                )
            if not rollout_ready:
                failures.append("image registry Deployment rollout is not ready")
            if not storage_ready:
                failures.append("image registry storage is not bound and ready")
            if not registry_config_current:
                failures.append("image registry config is missing or stale")
            if not config_current:
                failures.append("BuildKit daemon config is stale")
            if not node_trust_ready:
                failures.append(
                    "one or more Kubernetes nodes do not trust the image registry"
                )
            return ImageRepositoryStatus(
                namespace=self.namespace,
                name=self.service,
                storage=storage_summary,
                rollout=rollout_summary,
                trusted_nodes=tuple(trusted),
                untrusted_nodes=tuple(untrusted),
                ready=not failures,
                failures=tuple(failures),
            )
        except OSError as err:
            msg = (
                f"failed to inspect image repository "
                f"{self.namespace}/{self.service}: {err}"
            )
            raise OSError(msg) from err

    async def ensure(self, kube: Kube, *, timeout: float = INFINITY) -> None:
        """Converge Bertrand's OCI image repository resources.

        Parameters
        ----------
        kube : Kube
            Kubernetes API client for the target cluster.
        timeout : float, optional
            Maximum runtime budget in seconds. If infinite, wait indefinitely.

        Raises
        ------
        TimeoutError
            If `timeout` is non-positive or registry readiness exceeds the budget.
        OSError
            If Kubernetes create/patch/read operations fail or storage prerequisites
            are not present.
        """
        if timeout <= 0:
            msg = "image repository timeout must be non-negative"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout

        storage = await StorageClass.select(
            kube=kube,
            timeout=deadline - loop.time(),
            preferences=CEPHFS_STORAGE_CLASS_PREFERENCES,
            require_expansion=True,
        )
        if not storage.is_cephfs:
            msg = (
                f"storage class {storage.name!r} uses provisioner "
                f"{storage.provisioner!r}, but Bertrand registry storage requires "
                "a CephFS CSI provisioner"
            )
            raise OSError(msg)
        pvc = await PersistentVolumeClaim.upsert(
            kube=kube,
            namespace=self.namespace,
            name=self.service,
            access_modes=("ReadWriteMany",),
            storage_class=storage.name,
            storage_request=self.storage_request,
            labels=self.labels,
            timeout=deadline - loop.time(),
        )
        if pvc.storage_class_name != storage.name:
            msg = (
                f"registry PVC {self.namespace}/{self.service} uses storage class "
                f"{pvc.storage_class_name!r}, expected {storage.name!r}"
            )
            raise OSError(msg)
        if "ReadWriteMany" not in pvc.access_modes:
            msg = f"registry PVC {self.namespace}/{self.service} must use ReadWriteMany"
            raise OSError(msg)
        await pvc.wait_bound(kube, timeout=deadline - loop.time())

        await Service.upsert(
            kube,
            namespace=self.namespace,
            name=self.service,
            labels=self.labels,
            selector=self.selector,
            service_type="NodePort",
            ports=[
                ServicePortSpec(
                    name="registry",
                    port=self.port,
                    target_port=self.port,
                    node_port=self.node_port,
                )
            ],
            timeout=deadline - loop.time(),
        )
        registry_config = await ConfigMap.upsert(
            kube,
            namespace=self.namespace,
            name=IMAGE_REPOSITORY_CONFIG_NAME,
            labels=self.labels,
            data=self.registry_config_data(read_only=False),
            timeout=deadline - loop.time(),
        )
        await ConfigMap.upsert(
            kube,
            namespace=self.namespace,
            name=BUILDKIT_CONFIG_NAME,
            labels=self.labels,
            data=await self.current_buildkit_config_data(
                kube,
                timeout=deadline - loop.time(),
            ),
            timeout=deadline - loop.time(),
        )
        deployment = await self._upsert_deployment(
            kube,
            config_hash=_config_hash(registry_config.data),
            timeout=deadline - loop.time(),
        )
        await deployment.wait_rollout(kube, timeout=deadline - loop.time())

    async def restore_writable(
        self,
        kube: Kube,
        *,
        timeout: float = INFINITY,
    ) -> None:
        """Converge the registry Deployment back to writable mode.

        Parameters
        ----------
        kube : Kube
            Kubernetes API client for the target cluster.
        timeout : float, optional
            Maximum runtime budget in seconds. If infinite, wait indefinitely.

        Raises
        ------
        TimeoutError
            If `timeout` is non-positive or rollout exceeds the budget.
        OSError
            If Kubernetes create/patch/read operations fail.
        """
        if timeout <= 0:
            msg = "image repository writable restore timeout must be non-negative"
            raise TimeoutError(msg)
        try:
            loop = asyncio.get_running_loop()
            deadline = loop.time() + timeout
            await self._set_read_only(
                kube,
                read_only=False,
                timeout=deadline - loop.time(),
            )
            await self.clear_maintenance(kube, timeout=deadline - loop.time())
        except TimeoutError:
            raise
        except OSError as err:
            msg = f"failed to restore image registry writable mode: {err}"
            raise OSError(msg) from err

    async def garbage_collect_storage(
        self,
        kube: Kube,
        *,
        timeout: float = INFINITY,
        preflight: Callable[[float], Awaitable[bool]] | None = None,
    ) -> bool:
        """Run registry storage garbage collection with writes disabled.

        Parameters
        ----------
        kube : Kube
            Kubernetes API client for the target cluster.
        timeout : float, optional
            Maximum runtime budget in seconds. If infinite, wait indefinitely.
        preflight : Callable[[float], Awaitable[bool]] | None, optional
            Async callback invoked after maintenance status is published but before
            the registry is rolled into read-only mode. A false result skips GC.

        Returns
        -------
        bool
            Whether registry storage GC actually ran.

        Raises
        ------
        TimeoutError
            If `timeout` is non-positive or GC exceeds the budget.
        OSError
            If Kubernetes operations fail or the GC Job fails.
        """
        if timeout <= 0:
            msg = "image repository storage GC timeout must be non-negative"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        restore_required = False
        try:
            try:
                await self.start_maintenance(
                    kube,
                    reason=IMAGE_REPOSITORY_MAINTENANCE_REASON_GC,
                    message=IMAGE_REPOSITORY_MAINTENANCE_MESSAGE_GC,
                    timeout=deadline - loop.time(),
                )
                if preflight is not None and not await preflight(
                    deadline - loop.time()
                ):
                    return False
                restore_required = True
                await self._set_read_only(
                    kube,
                    read_only=True,
                    timeout=deadline - loop.time(),
                )
                job = await Job.create(
                    kube,
                    namespace=self.namespace,
                    name=f"{self.service}-gc-{uuid.uuid4().hex[:8]}",
                    labels={
                        **self.labels,
                        IMAGE_REPOSITORY_GC_JOB_LABEL: (
                            IMAGE_REPOSITORY_GC_JOB_LABEL_VALUE
                        ),
                    },
                    pod_template=PodTemplateSpec(
                        containers=[
                            ContainerSpec(
                                name="registry-gc",
                                image=IMAGE_REPOSITORY_IMAGE,
                                image_pull_policy="IfNotPresent",
                                command=["registry"],
                                args=[
                                    "garbage-collect",
                                    "--quiet",
                                    IMAGE_REPOSITORY_CONFIG_FILE,
                                ],
                                volume_mounts=self._volume_mounts(),
                            )
                        ],
                        volumes=self._volumes(),
                    ),
                    ttl_seconds_after_finished=IMAGE_REPOSITORY_GC_TTL_SECONDS,
                    timeout=deadline - loop.time(),
                )
                await run_observed_job(
                    kube,
                    job,
                    timeout=deadline - loop.time(),
                    failure_context="image registry storage garbage collection failed",
                    log_heading="registry GC Job logs",
                    log_failure_label="registry GC Job pod logs",
                    tail_lines=IMAGE_REPOSITORY_GC_LOG_TAIL_LINES,
                    diagnostic_timeout=IMAGE_REPOSITORY_GC_DIAGNOSTIC_TIMEOUT_SECONDS,
                    cleanup_timeout=IMAGE_REPOSITORY_GC_CLEANUP_TIMEOUT_SECONDS,
                )
            except TimeoutError:
                raise
            except OSError as err:
                msg = f"image registry storage garbage collection failed: {err}"
                raise OSError(msg) from err
            return True
        finally:
            if restore_required:
                await self.restore_writable(
                    kube,
                    timeout=IMAGE_REPOSITORY_GC_RESTORE_TIMEOUT_SECONDS,
                )
            else:
                await self.clear_maintenance(
                    kube,
                    timeout=IMAGE_REPOSITORY_GC_RESTORE_TIMEOUT_SECONDS,
                )

    async def _set_read_only(
        self,
        kube: Kube,
        *,
        read_only: bool,
        timeout: float,
    ) -> None:
        if timeout <= 0:
            msg = "image repository config rollout timeout must be non-negative"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        config = await ConfigMap.upsert(
            kube,
            namespace=self.namespace,
            name=IMAGE_REPOSITORY_CONFIG_NAME,
            labels=self.labels,
            data=self.registry_config_data(read_only=read_only),
            timeout=deadline - loop.time(),
        )
        deployment = await self._upsert_deployment(
            kube,
            config_hash=_config_hash(config.data),
            timeout=deadline - loop.time(),
        )
        await deployment.wait_rollout(kube, timeout=deadline - loop.time())

    async def _upsert_deployment(
        self,
        kube: Kube,
        *,
        config_hash: str,
        timeout: float,
    ) -> Deployment:
        return await Deployment.upsert(
            kube,
            namespace=self.namespace,
            name=self.service,
            labels=self.labels,
            selector=self.selector,
            pod_template=PodTemplateSpec(
                containers=[
                    ContainerSpec(
                        name="registry",
                        image=IMAGE_REPOSITORY_IMAGE,
                        image_pull_policy="IfNotPresent",
                        command=["registry"],
                        args=["serve", IMAGE_REPOSITORY_CONFIG_FILE],
                        ports=[
                            ContainerPortSpec(
                                name="registry",
                                container_port=self.port,
                            )
                        ],
                        readiness_probe=ProbeSpec.http(
                            path="/v2/",
                            port=self.port,
                            period_seconds=2,
                            failure_threshold=30,
                        ),
                        liveness_probe=ProbeSpec.http(
                            path="/v2/",
                            port=self.port,
                            initial_delay_seconds=10,
                            period_seconds=10,
                            failure_threshold=3,
                        ),
                        volume_mounts=self._volume_mounts(),
                    )
                ],
                volumes=self._volumes(),
                annotations={IMAGE_REPOSITORY_CONFIG_HASH_ANNOTATION: config_hash},
            ),
            strategy=DeploymentStrategySpec.recreate(),
            timeout=timeout,
        )

    def _volume_mounts(self) -> tuple[VolumeMountSpec, ...]:
        return (
            VolumeMountSpec(
                name=IMAGE_REPOSITORY_VOLUME,
                mount_path=IMAGE_REPOSITORY_MOUNT,
            ),
            VolumeMountSpec(
                name=IMAGE_REPOSITORY_CONFIG_VOLUME,
                mount_path=IMAGE_REPOSITORY_CONFIG_DIR,
                read_only=True,
            ),
        )

    def _volumes(self) -> tuple[VolumeSpec, ...]:
        return (
            VolumeSpec.pvc(
                IMAGE_REPOSITORY_VOLUME,
                claim_name=self.service,
            ),
            VolumeSpec.config_map(
                IMAGE_REPOSITORY_CONFIG_VOLUME,
                config_map_name=IMAGE_REPOSITORY_CONFIG_NAME,
            ),
        )

    def ref(self, name: str, tag: str) -> str:
        """Render a stable Bertrand image reference.

        Parameters
        ----------
        name : str
            Repository path below the Bertrand namespace, for example
            ``"autoscale"`` or ``"operators/autoscale"``.
        tag : str
            OCI image tag.

        Returns
        -------
        str
            Fully-qualified image reference rooted at :attr:`pull_host`.

        Raises
        ------
        ValueError
            If the repository path or tag is empty or invalid.
        """
        path = name.strip().strip("/")
        normalized_tag = validate_tag(tag)
        if not path:
            msg = "image repository path cannot be empty"
            raise ValueError(msg)
        parts = path.split("/")
        if any(not IMAGE_REF_COMPONENT_RE.fullmatch(part) for part in parts):
            msg = f"invalid image repository path: {name!r}"
            raise ValueError(msg)
        return f"{self.pull_host}/bertrand/{path}:{normalized_tag}"

    def service_ref(self, ref: str) -> str:
        """Rewrite an internal image ref to the in-cluster Service address.

        Parameters
        ----------
        ref : str
            Image reference rooted at either :attr:`pull_host` or
            :attr:`service_addr`.

        Returns
        -------
        str
            Equivalent image reference rooted at :attr:`service_addr`.
        """
        return rewrite_registry_ref(
            ref,
            source=self.pull_host,
            target=self.service_addr,
            canonical_host=self.pull_host,
        )

    def pull_ref(self, ref: str) -> str:
        """Rewrite an internal image ref to the canonical pull address.

        Parameters
        ----------
        ref : str
            Image reference rooted at either :attr:`service_addr` or
            :attr:`pull_host`.

        Returns
        -------
        str
            Equivalent image reference rooted at :attr:`pull_host`.
        """
        return rewrite_registry_ref(
            ref,
            source=self.service_addr,
            target=self.pull_host,
            canonical_host=self.pull_host,
        )

    def _digest_delete_url(self, digest_ref: str) -> str:
        ref = digest_ref.strip()
        prefix = f"{self.pull_host}/"
        if not ref.startswith(prefix):
            msg = (
                f"image digest ref {digest_ref!r} does not belong to registry "
                f"{self.pull_host!r}"
            )
            raise ValueError(msg)
        payload = ref[len(prefix) :]
        repo_path, sep, digest = payload.rpartition("@")
        if not sep or not repo_path or not digest:
            msg = f"image reference must include an immutable digest: {digest_ref!r}"
            raise ValueError(msg)
        if not DIGEST_RE.fullmatch(digest):
            msg = f"unsupported image digest in ref {digest_ref!r}"
            raise ValueError(msg)
        encoded_path = urllib.parse.quote(repo_path, safe="/")
        encoded_digest = urllib.parse.quote(digest, safe=":")
        return f"{self.pull_server}/v2/{encoded_path}/manifests/{encoded_digest}"

    async def delete_manifest(
        self,
        digest_ref: str,
        *,
        timeout: float = INFINITY,
    ) -> None:
        """Delete one image manifest by immutable registry digest reference.

        Parameters
        ----------
        digest_ref : str
            Fully qualified immutable image reference rooted at this repository,
            for example ``localhost:32000/bertrand/app@sha256:...``.
        timeout : float, optional
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Raises
        ------
        TimeoutError
            If `timeout` is non-positive or the registry request times out.
        OSError
            If the registry rejects the delete request.
        """
        if timeout <= 0:
            msg = "image manifest delete timeout must be non-negative"
            raise TimeoutError(msg)
        url = self._digest_delete_url(digest_ref)
        request_timeout = None if math.isinf(timeout) else timeout
        try:
            status = await asyncio.to_thread(
                _registry_manifest_delete,
                url,
                request_timeout,
            )
        except TimeoutError:
            raise
        except (OSError, urllib.error.URLError) as err:
            msg = f"failed to delete image manifest {digest_ref!r}: {err}"
            raise OSError(msg) from err
        if status not in IMAGE_REPOSITORY_DELETE_SUCCESS_STATUS:
            msg = (
                f"failed to delete image manifest {digest_ref!r}: registry returned "
                f"HTTP status {status}"
            )
            raise OSError(msg)


IMAGES = ImageRepository(
    namespace=BERTRAND_NAMESPACE,
    service=IMAGE_REPOSITORY_NAME,
    port=IMAGE_REPOSITORY_PORT,
    node_port=IMAGE_REPOSITORY_NODE_PORT,
    pull_host=IMAGE_REPOSITORY_PULL_HOST,
    service_addr=IMAGE_REPOSITORY_SERVICE_ADDR,
    storage_request=IMAGE_REPOSITORY_SIZE,
)
