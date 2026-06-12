"""Cluster-owned OCI image repository for Bertrand's Kubernetes build runtime."""

from __future__ import annotations

import asyncio
import contextlib
import hashlib
import json
import math
import os
import shutil
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import uuid
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import TYPE_CHECKING, cast

from bertrand.env.git import (
    BERTRAND_LABEL,
    BERTRAND_LABEL_MANAGED,
    BERTRAND_NAMESPACE,
    NO_DEADLINE,
    STATE,
    Deadline,
    run,
    sudo,
)
from bertrand.env.kube.api.client import K0S_SERVICE_NAME, Kube
from bertrand.env.kube.api.manifest import ContainerSpec, PodTemplateSpec, VolumeSpec
from bertrand.env.kube.build.daemon import (
    BUILDKIT_CONFIG_KEY,
    BUILDKIT_CONFIG_NAME,
    buildkit_worker_gc_toml,
)
from bertrand.env.kube.build.refs import (
    DIGEST_RE,
    IMAGE_REF_COMPONENT_RE,
    rewrite_registry_ref,
    validate_tag,
)
from bertrand.env.kube.configmap import ConfigMap, ConfigMapManifest
from bertrand.env.kube.deployment import Deployment, DeploymentManifest
from bertrand.env.kube.job import Job
from bertrand.env.kube.network.profile import NetworkProfile
from bertrand.env.kube.node import Node
from bertrand.env.kube.service import Service, ServiceManifest, ServicePortView
from bertrand.env.kube.volume import (
    PersistentVolumeClaim,
    StorageClass,
)

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
CLUSTER_REGISTRY_READY_LABEL = "bertrand.dev/registry-ready"
CLUSTER_REGISTRY_READY_VALUE = "true"
_K0S_CONTAINERD_DROPIN_DIR = Path("/etc/k0s/containerd.d")
_K0S_CONTAINERD_CERTS_DIR = _K0S_CONTAINERD_DROPIN_DIR / "certs.d"
_K0S_REGISTRY_DROPIN_FILE = _K0S_CONTAINERD_DROPIN_DIR / "bertrand-registry.toml"
IMAGE_REPOSITORY_LABELS = {
    "app.kubernetes.io/name": IMAGE_REPOSITORY_NAME,
    "app.kubernetes.io/part-of": "bertrand",
    BERTRAND_LABEL: BERTRAND_LABEL_MANAGED,
    IMAGE_REPOSITORY_LABEL: IMAGE_REPOSITORY_LABEL_VALUE,
}
IMAGE_REPOSITORY_SELECTOR = {
    "app.kubernetes.io/name": IMAGE_REPOSITORY_NAME,
    IMAGE_REPOSITORY_LABEL: IMAGE_REPOSITORY_LABEL_VALUE,
}
IMAGE_REPOSITORY_MAINTENANCE_LABELS = {
    **IMAGE_REPOSITORY_LABELS,
    IMAGE_REPOSITORY_MAINTENANCE_LABEL: IMAGE_REPOSITORY_MAINTENANCE_LABEL_VALUE,
}


def _config_hash(data: Mapping[str, str]) -> str:
    payload = json.dumps(data, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


async def _install_root_file(
    path: Path,
    payload: str,
    *,
    mode: str = "0644",
    group: str = "root",
    deadline: Deadline,
) -> bool:
    with contextlib.suppress(OSError):
        if path.read_text(encoding="utf-8") == payload:
            return False
    fd: int | None = None
    temp_file: Path | None = None
    try:
        fd, name = tempfile.mkstemp(prefix="bertrand-k0s.", suffix=".tmp")
        temp_file = Path(name)
        os.write(fd, payload.encode("utf-8"))
        os.fsync(fd)
        os.close(fd)
        fd = None
        await run(
            sudo(
                [
                    "install",
                    "-D",
                    "-m",
                    mode,
                    "-o",
                    "root",
                    "-g",
                    group,
                    str(temp_file),
                    str(path),
                ]
            ),
            deadline=deadline,
        )
    finally:
        if fd is not None:
            with contextlib.suppress(OSError):
                os.close(fd)
        if temp_file is not None:
            temp_file.unlink(missing_ok=True)
    return True


async def _k0s_service_active(*, deadline: Deadline) -> bool:
    if not shutil.which("systemctl"):
        return False
    result = await run(
        sudo(["systemctl", "is-active", "--quiet", K0S_SERVICE_NAME]),
        check=False,
        deadline=deadline,
    )
    return result.returncode == 0


async def _configure_k0s_registry_trust(
    *,
    hosts: tuple[str, ...],
    deadline: Deadline,
) -> None:
    normalized = tuple(sorted({item.strip() for item in hosts if item.strip()}))
    await run(
        sudo(
            [
                "install",
                "-d",
                "-m",
                "0755",
                "-o",
                "root",
                "-g",
                "root",
                str(_K0S_CONTAINERD_DROPIN_DIR),
            ]
        ),
        deadline=deadline,
    )
    changed = await _install_root_file(
        _K0S_REGISTRY_DROPIN_FILE,
        "\n".join(
            (
                'version = 2',
                '',
                '[plugins."io.containerd.grpc.v1.cri".registry]',
                f'  config_path = "{_K0S_CONTAINERD_CERTS_DIR}"',
                '',
            )
        ),
        deadline=deadline,
    )
    for host in normalized:
        cert_dir = _K0S_CONTAINERD_CERTS_DIR / host
        await run(
            sudo(
                [
                    "install",
                    "-d",
                    "-m",
                    "0755",
                    "-o",
                    "root",
                    "-g",
                    "root",
                    str(cert_dir),
                ]
            ),
            deadline=deadline,
        )
        host_changed = await _install_root_file(
            cert_dir / "hosts.toml",
            "\n".join(
                (
                    f'server = "http://{host}"',
                    '',
                    f'[host."http://{host}"]',
                    '  capabilities = ["pull", "resolve"]',
                    '',
                )
            ),
            deadline=deadline,
        )
        changed = changed or host_changed
    if (
        changed
        and STATE.kube.bin.is_file()
        and await _k0s_service_active(deadline=deadline)
    ):
        await run(
            sudo(["systemctl", "restart", K0S_SERVICE_NAME], non_interactive=True),
            deadline=deadline,
        )


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

    def start_maintenance(
        self,
        *,
        reason: str,
        message: str,
        now: datetime,
    ) -> ImageRepositoryMaintenanceStatus:
        """Return this status with registry maintenance marked active.

        Parameters
        ----------
        reason : str
            Stable maintenance reason identifier.
        message : str
            Concise maintenance message.
        now : datetime
            Timestamp to record as the maintenance start time.

        Returns
        -------
        ImageRepositoryMaintenanceStatus
            Updated registry maintenance status.

        Raises
        ------
        OSError
            If `reason` or `message` is empty.
        """
        reason = reason.strip()
        message = message.strip()
        if not reason or not message:
            msg = "registry maintenance status requires reason and message"
            raise OSError(msg)
        return ImageRepositoryMaintenanceStatus(
            active=True,
            reason=reason,
            started_at=now,
            message=message,
            dirty_count=self.dirty_count,
            dirty_since=self.dirty_since,
            last_gc_at=self.last_gc_at,
        )

    def clear_maintenance(self) -> ImageRepositoryMaintenanceStatus:
        """Return this status with registry maintenance marked inactive.

        Returns
        -------
        ImageRepositoryMaintenanceStatus
            Updated registry maintenance status.
        """
        return ImageRepositoryMaintenanceStatus(
            active=False,
            dirty_count=self.dirty_count,
            dirty_since=self.dirty_since,
            last_gc_at=self.last_gc_at,
        )

    def mark_storage_dirty(
        self,
        *,
        count: int,
        now: datetime,
    ) -> ImageRepositoryMaintenanceStatus:
        """Return this status with additional pending registry GC work.

        Parameters
        ----------
        count : int
            Number of newly collected image records.
        now : datetime
            Timestamp to use if this is the first dirty marker.

        Returns
        -------
        ImageRepositoryMaintenanceStatus
            Updated registry maintenance status.

        Raises
        ------
        ValueError
            If `count` is negative.
        """
        if count < 0:
            msg = "registry storage dirty count cannot be negative"
            raise ValueError(msg)
        if count == 0:
            return self
        return ImageRepositoryMaintenanceStatus(
            active=self.active,
            reason=self.reason,
            started_at=self.started_at,
            message=self.message,
            dirty_count=self.dirty_count + count,
            dirty_since=self.dirty_since or now,
            last_gc_at=self.last_gc_at,
        )

    def clear_storage_dirty(
        self,
        *,
        last_gc_at: datetime | None = None,
    ) -> ImageRepositoryMaintenanceStatus:
        """Return this status with the registry GC dirty marker cleared.

        Parameters
        ----------
        last_gc_at : datetime | None, optional
            Timestamp to record for a successful registry storage GC.

        Returns
        -------
        ImageRepositoryMaintenanceStatus
            Updated registry maintenance status.
        """
        return ImageRepositoryMaintenanceStatus(
            active=self.active,
            reason=self.reason,
            started_at=self.started_at,
            message=self.message,
            dirty_count=0,
            dirty_since=None,
            last_gc_at=last_gc_at or self.last_gc_at,
        )

    @property
    def storage_dirty(self) -> bool:
        """Return whether registry storage GC is due.

        Returns
        -------
        bool
            Whether manifest lifecycle GC has marked registry storage dirty.
        """
        return self.dirty_count > 0


IMAGE_REPOSITORY_PULL_SERVER = f"http://{IMAGE_REPOSITORY_PULL_HOST}"
IMAGE_REPOSITORY_TRUST_HOSTS = (
    IMAGE_REPOSITORY_PULL_HOST,
    f"127.0.0.1:{IMAGE_REPOSITORY_NODE_PORT}",
)


def buildkit_config_data(profile: NetworkProfile) -> dict[str, str]:
    """Return BuildKit daemon ConfigMap data.

    Parameters
    ----------
    profile : NetworkProfile
        Cluster networking profile to compose into BuildKit daemon configuration.

    Returns
    -------
    dict[str, str]
        Data payload for the BuildKit daemon configuration ConfigMap.
    """
    network_config = profile.buildkit_toml()
    registry_config = (
        f"[registry.\"{IMAGE_REPOSITORY_PULL_HOST}\"]\n"
        f"  mirrors = [\"{IMAGE_REPOSITORY_SERVICE_ADDR}\"]\n"
        "  http = true\n"
        "  insecure = true\n"
    )
    fragments = [buildkit_worker_gc_toml()]
    if network_config:
        fragments.append(network_config)
    fragments.append(registry_config)
    return {BUILDKIT_CONFIG_KEY: "\n".join(fragments)}


def registry_config_data(*, read_only: bool = False) -> dict[str, str]:
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
            f"  addr: 0.0.0.0:{IMAGE_REPOSITORY_PORT}\n"
        )
    }


async def image_repository_maintenance_status(
    kube: Kube,
    *,
    deadline: Deadline = NO_DEADLINE,
) -> ImageRepositoryMaintenanceStatus:
    """Read the current registry maintenance status.

    Returns
    -------
    ImageRepositoryMaintenanceStatus
        Current registry maintenance status. Missing status means inactive.
    """
    status = await ConfigMap.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=IMAGE_REPOSITORY_MAINTENANCE_NAME,
        deadline=deadline,
    )
    if status is None:
        return ImageRepositoryMaintenanceStatus(active=False)
    return ImageRepositoryMaintenanceStatus.from_data(status.data)


async def _write_image_repository_maintenance_status(
    kube: Kube,
    *,
    status: ImageRepositoryMaintenanceStatus,
    deadline: Deadline,
) -> None:
    await ConfigMap.upsert(
        kube,
        intent=ConfigMapManifest(
            namespace=BERTRAND_NAMESPACE,
            name=IMAGE_REPOSITORY_MAINTENANCE_NAME,
            labels=IMAGE_REPOSITORY_MAINTENANCE_LABELS,
            data=status.data(),
        ),
        deadline=deadline,
    )


async def start_image_repository_maintenance(
    kube: Kube,
    *,
    reason: str,
    message: str,
    deadline: Deadline = NO_DEADLINE,
) -> None:
    """Mark registry maintenance as active.

    Raises
    ------
    OSError
        If `reason` or `message` is empty, or Kubernetes upsert fails.
    """
    if not reason.strip() or not message.strip():
        msg = "registry maintenance status requires reason and message"
        raise OSError(msg)
    current = await image_repository_maintenance_status(kube, deadline=deadline)
    status = current.start_maintenance(
        reason=reason,
        message=message,
        now=datetime.now(UTC),
    )
    await _write_image_repository_maintenance_status(
        kube,
        status=status,
        deadline=deadline,
    )


async def clear_image_repository_maintenance(
    kube: Kube,
    *,
    deadline: Deadline = NO_DEADLINE,
) -> None:
    """Clear registry maintenance status."""
    current = await image_repository_maintenance_status(kube, deadline=deadline)
    await _write_image_repository_maintenance_status(
        kube,
        status=current.clear_maintenance(),
        deadline=deadline,
    )


async def mark_image_repository_storage_dirty(
    kube: Kube,
    *,
    count: int,
    deadline: Deadline = NO_DEADLINE,
) -> None:
    """Mark registry storage as needing garbage collection.

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
    current = await image_repository_maintenance_status(kube, deadline=deadline)
    status = current.mark_storage_dirty(count=count, now=datetime.now(UTC))
    await _write_image_repository_maintenance_status(
        kube,
        status=status,
        deadline=deadline,
    )


async def clear_image_repository_storage_dirty(
    kube: Kube,
    *,
    last_gc_at: datetime | None = None,
    deadline: Deadline = NO_DEADLINE,
) -> None:
    """Clear the durable registry storage dirty marker."""
    current = await image_repository_maintenance_status(kube, deadline=deadline)
    await _write_image_repository_maintenance_status(
        kube,
        status=current.clear_storage_dirty(last_gc_at=last_gc_at),
        deadline=deadline,
    )


async def current_buildkit_config_data(
    kube: Kube,
    *,
    deadline: Deadline,
) -> dict[str, str]:
    """Return BuildKit daemon ConfigMap data for the active cluster profile.

    Returns
    -------
    dict[str, str]
        Data payload for the BuildKit daemon configuration ConfigMap.
    """
    profile = await NetworkProfile.get(kube, deadline=deadline)
    return buildkit_config_data(profile)


async def current_buildkit_config_hash(kube: Kube, *, deadline: Deadline) -> str:
    """Return the expected BuildKit daemon ConfigMap hash.

    Returns
    -------
    str
        SHA-256 digest of the BuildKit daemon configuration data.
    """
    return _config_hash(await current_buildkit_config_data(kube, deadline=deadline))


async def _ensure_image_repository_trust(*, deadline: Deadline) -> None:
    await _configure_k0s_registry_trust(
        hosts=IMAGE_REPOSITORY_TRUST_HOSTS,
        deadline=deadline,
    )


async def _assert_image_repository_local_route(*, deadline: Deadline) -> None:
    url = f"{IMAGE_REPOSITORY_PULL_SERVER}/v2/"
    last_error: OSError | TimeoutError | urllib.error.URLError | None = None
    while True:
        remaining = deadline.remaining
        if remaining <= 0:
            msg = f"local image repository route {url!r} is not ready"
            if last_error is not None:
                msg = f"{msg}: {last_error}"
            raise TimeoutError(msg) from last_error
        request_timeout = min(IMAGE_REPOSITORY_ROUTE_REQUEST_TIMEOUT_SECONDS, remaining)
        try:
            status = await asyncio.to_thread(
                _registry_route_status,
                url,
                request_timeout,
            )
            if status in IMAGE_REPOSITORY_ROUTE_READY_STATUS:
                return
            msg = f"HTTP status {status}"
            last_error = urllib.error.URLError(msg)
        except (OSError, TimeoutError, urllib.error.URLError) as err:
            last_error = err
        await deadline.sleep(IMAGE_REPOSITORY_ROUTE_POLL_INTERVAL_SECONDS)


async def ensure_image_repository_node_trust(
    *,
    kube: Kube,
    deadline: Deadline,
) -> None:
    """Converge local registry trust and mark the local node ready."""
    await _ensure_image_repository_trust(deadline=deadline)
    await _assert_image_repository_local_route(deadline=deadline)
    node = await Node.local(kube, deadline=deadline)
    await node.set_label(
        kube=kube,
        label=CLUSTER_REGISTRY_READY_LABEL,
        value=CLUSTER_REGISTRY_READY_VALUE,
        deadline=deadline,
    )


# TODO: assert_image_repository_node_trust should be rolled into
# ensure_image_repository_node_trust


async def assert_image_repository_node_trust(
    kube: Kube,
    *,
    deadline: Deadline = NO_DEADLINE,
) -> None:
    """Assert all cluster nodes are marked ready for registry pulls.

    Raises
    ------
    OSError
        If any cluster node is missing the registry-ready label.
    """
    nodes = await Node.list(kube=kube, deadline=deadline)
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


async def image_repository_status(
    kube: Kube,
    *,
    deadline: Deadline = NO_DEADLINE,
) -> ImageRepositoryStatus:
    """Inspect repository readiness without mutating the cluster.

    Returns
    -------
    ImageRepositoryStatus
        Read-only image repository readiness report.

    Raises
    ------
    OSError
        If Kubernetes read operations fail or return malformed data.
    """
    try:
        service_task = asyncio.create_task(
            Service.get(
                kube,
                namespace=BERTRAND_NAMESPACE,
                deadline=deadline,
                name=IMAGE_REPOSITORY_NAME,
            )
        )
        deployment_task = asyncio.create_task(
            Deployment.get(
                kube,
                namespace=BERTRAND_NAMESPACE,
                deadline=deadline,
                name=IMAGE_REPOSITORY_NAME,
            )
        )
        pvc_task = asyncio.create_task(
            PersistentVolumeClaim.get(
                kube=kube,
                namespace=BERTRAND_NAMESPACE,
                deadline=deadline,
                name=IMAGE_REPOSITORY_NAME,
            )
        )
        buildkit_config_task = asyncio.create_task(
            ConfigMap.get(
                kube,
                namespace=BERTRAND_NAMESPACE,
                deadline=deadline,
                name=BUILDKIT_CONFIG_NAME,
            )
        )
        registry_config_task = asyncio.create_task(
            ConfigMap.get(
                kube,
                namespace=BERTRAND_NAMESPACE,
                deadline=deadline,
                name=IMAGE_REPOSITORY_CONFIG_NAME,
            )
        )
        maintenance_task = asyncio.create_task(
            image_repository_maintenance_status(
                kube,
                deadline=deadline,
            )
        )
        nodes_task = asyncio.create_task(
        Node.list(kube=kube, deadline=deadline),
        )
        desired_buildkit_config_task = asyncio.create_task(
            current_buildkit_config_data(
                kube,
                deadline=deadline,
            ),
        )
        await asyncio.gather(
            cast("Awaitable[object]", service_task),
            cast("Awaitable[object]", deployment_task),
            cast("Awaitable[object]", pvc_task),
            cast("Awaitable[object]", buildkit_config_task),
            cast("Awaitable[object]", registry_config_task),
            cast("Awaitable[object]", maintenance_task),
            cast("Awaitable[object]", nodes_task),
            cast("Awaitable[object]", desired_buildkit_config_task),
        )
        service = service_task.result()
        deployment = deployment_task.result()
        pvc = pvc_task.result()
        buildkit_config = buildkit_config_task.result()
        registry_config = registry_config_task.result()
        maintenance = maintenance_task.result()
        nodes = tuple(nodes_task.result())
        desired_buildkit_config = desired_buildkit_config_task.result()

        expected_port = ServicePortView(
            name="registry",
            port=IMAGE_REPOSITORY_PORT,
            target_port=IMAGE_REPOSITORY_PORT,
            protocol="TCP",
            node_port=IMAGE_REPOSITORY_NODE_PORT,
        )
        service_ready = (
            service.matches(
                service_type="NodePort",
                selector=IMAGE_REPOSITORY_SELECTOR,
                ports=(expected_port,),
            )
            if service is not None
            else False
        )

        available_replicas = (
            deployment.available_replicas if deployment is not None else 0
        )
        updated_replicas = deployment.updated_replicas if deployment is not None else 0
        observed_generation = (
            deployment.observed_generation if deployment is not None else 0
        )
        generation = deployment.generation if deployment is not None else 0
        rollout_summary = (
            f"available={available_replicas}; updated={updated_replicas}; "
            f"observed={observed_generation}; generation={generation}"
        )
        rollout_ready = (
            deployment.rollout_ready(minimum=1) if deployment is not None else False
        )

        pvc_managed = (
            all(
                pvc.labels.get(key) == value
                for key, value in {
                    BERTRAND_LABEL: BERTRAND_LABEL_MANAGED,
                    IMAGE_REPOSITORY_LABEL: IMAGE_REPOSITORY_LABEL_VALUE,
                }.items()
            )
            if pvc is not None
            else False
        )
        storage_class = pvc.storage_class_name if pvc is not None else "missing"
        access_modes = pvc.access_modes if pvc is not None else ()
        storage_request = pvc.requested_storage if pvc is not None else "missing"
        phase = pvc.phase if pvc is not None else "missing"
        storage_summary = (
            f"{phase}; class={storage_class}; "
            f"request={storage_request}; modes={','.join(access_modes) or 'none'}"
        )
        storage_ready = (
            pvc is not None
            and pvc_managed
            and pvc.is_bound
            and bool(storage_class)
            and pvc.has_access_mode("ReadWriteMany")
        )

        desired_buildkit_hash = _config_hash(desired_buildkit_config)
        installed_buildkit_hash = (
            _config_hash(buildkit_config.data) if buildkit_config is not None else ""
        )
        desired_registry_config = registry_config_data(read_only=False)
        desired_read_only_registry_config = registry_config_data(read_only=True)
        registry_config_current = registry_config is not None and (
            registry_config.data == desired_registry_config
            or (
                maintenance.active
                and registry_config.data == desired_read_only_registry_config
            )
        )
        buildkit_config_current = installed_buildkit_hash == desired_buildkit_hash

        named_nodes = sorted(node.name for node in nodes if node.name)
        trusted_nodes = tuple(
            sorted(
                node.name
                for node in nodes
                if node.name
                and node.labels.get(CLUSTER_REGISTRY_READY_LABEL)
                == CLUSTER_REGISTRY_READY_VALUE
            )
        )
        trusted_set = frozenset(trusted_nodes)
        untrusted_nodes = tuple(name for name in named_nodes if name not in trusted_set)
        node_trust_ready = bool(named_nodes) and not untrusted_nodes

        failures: list[str] = []
        if not service_ready:
            failures.append("image registry Service is missing or has the wrong shape")
        if not rollout_ready:
            failures.append("image registry Deployment rollout is not ready")
        if not storage_ready:
            failures.append("image registry storage is not bound and ready")
        if not registry_config_current:
            failures.append("image registry config is missing or stale")
        if not buildkit_config_current:
            failures.append("BuildKit daemon config is stale")
        if not node_trust_ready:
            failures.append(
                "one or more Kubernetes nodes do not trust the image registry"
            )

        return ImageRepositoryStatus(
            namespace=BERTRAND_NAMESPACE,
            name=IMAGE_REPOSITORY_NAME,
            storage=storage_summary,
            rollout=rollout_summary,
            trusted_nodes=trusted_nodes,
            untrusted_nodes=untrusted_nodes,
            ready=not failures,
            failures=tuple(failures),
        )
    except OSError as err:
        msg = (
            f"failed to inspect image repository "
            f"{BERTRAND_NAMESPACE}/{IMAGE_REPOSITORY_NAME}: {err}"
        )
        raise OSError(msg) from err


async def ensure_image_repository(
    kube: Kube, *, deadline: Deadline = NO_DEADLINE
) -> None:
    """Converge Bertrand's OCI image repository resources.

    Raises
    ------
    OSError
        If Kubernetes create/patch/read operations fail or storage prerequisites are
        not present.
    """
    from bertrand.env.kube.ceph.volume import CEPHFS_STORAGE_CLASS_PREFERENCES

    storage = await StorageClass.select(
        kube=kube,
        deadline=deadline,
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
        namespace=BERTRAND_NAMESPACE,
        name=IMAGE_REPOSITORY_NAME,
        access_modes=("ReadWriteMany",),
        storage_class=storage.name,
        storage_request=IMAGE_REPOSITORY_SIZE,
        labels=IMAGE_REPOSITORY_LABELS,
        deadline=deadline,
    )
    if pvc.storage_class_name != storage.name:
        msg = (
            f"registry PVC {BERTRAND_NAMESPACE}/{IMAGE_REPOSITORY_NAME} uses "
            f"storage class {pvc.storage_class_name!r}, expected {storage.name!r}"
        )
        raise OSError(msg)
    if "ReadWriteMany" not in pvc.access_modes:
        msg = (
            f"registry PVC {BERTRAND_NAMESPACE}/{IMAGE_REPOSITORY_NAME} must use "
            "ReadWriteMany"
        )
        raise OSError(msg)
    await pvc.wait_bound(kube, deadline=deadline)

    async def upsert_buildkit_config() -> None:
        data = await current_buildkit_config_data(kube, deadline=deadline)
        await ConfigMap.upsert(
            kube,
            intent=ConfigMapManifest(
                namespace=BERTRAND_NAMESPACE,
                name=BUILDKIT_CONFIG_NAME,
                labels=IMAGE_REPOSITORY_LABELS,
                data=data,
            ),
            deadline=deadline,
        )

    service_task = asyncio.create_task(
        Service.upsert(
            kube,
            intent=ServiceManifest(
                namespace=BERTRAND_NAMESPACE,
                name=IMAGE_REPOSITORY_NAME,
                labels=IMAGE_REPOSITORY_LABELS,
                selector=IMAGE_REPOSITORY_SELECTOR,
                service_type="NodePort",
                ports=[
                    ServicePortView(
                        name="registry",
                        port=IMAGE_REPOSITORY_PORT,
                        target_port=IMAGE_REPOSITORY_PORT,
                        protocol="TCP",
                        node_port=IMAGE_REPOSITORY_NODE_PORT,
                    )
                ],
            ),
            deadline=deadline,
        )
    )
    config_task = asyncio.create_task(upsert_buildkit_config())
    await asyncio.gather(
        cast("Awaitable[object]", service_task),
        cast("Awaitable[object]", config_task),
    )
    await _rollout_registry_config(
        kube,
        read_only=False,
        deadline=deadline,
    )


async def restore_image_repository_writable(
    kube: Kube,
    *,
    deadline: Deadline = NO_DEADLINE,
) -> None:
    """Converge the registry Deployment back to writable mode.

    Raises
    ------
    OSError
        If Kubernetes create/patch/read operations fail.
    TimeoutError
        If `timeout` is non-positive or rollout exceeds the budget.
    """
    try:
        await _rollout_registry_config(
            kube,
            read_only=False,
            deadline=deadline,
        )
        await clear_image_repository_maintenance(kube, deadline=deadline)
    except TimeoutError:
        raise
    except OSError as err:
        msg = f"failed to restore image registry writable mode: {err}"
        raise OSError(msg) from err


async def garbage_collect_image_repository_storage(
    kube: Kube,
    *,
    deadline: Deadline = NO_DEADLINE,
    preflight: Callable[[Deadline], Awaitable[bool]] | None = None,
) -> bool:
    """Run registry storage garbage collection with writes disabled.

    Returns
    -------
    bool
        Whether registry storage GC actually ran.

    Raises
    ------
    OSError
        If Kubernetes operations fail or the GC Job fails.
    TimeoutError
        If `timeout` is non-positive or GC exceeds the budget.
    """
    restore_required = False
    try:
        try:
            await start_image_repository_maintenance(
                kube,
                reason=IMAGE_REPOSITORY_MAINTENANCE_REASON_GC,
                message=IMAGE_REPOSITORY_MAINTENANCE_MESSAGE_GC,
                deadline=deadline,
            )
            if preflight is not None and not await preflight(deadline):
                return False
            restore_required = True
            await _rollout_registry_config(
                kube,
                read_only=True,
                deadline=deadline,
            )
            job = await Job.create(
                kube,
                namespace=BERTRAND_NAMESPACE,
                name=f"{IMAGE_REPOSITORY_NAME}-gc-{uuid.uuid4().hex[:8]}",
                labels={
                    **IMAGE_REPOSITORY_LABELS,
                    IMAGE_REPOSITORY_GC_JOB_LABEL: IMAGE_REPOSITORY_GC_JOB_LABEL_VALUE,
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
                            volume_mounts=_volume_mounts(),
                        )
                    ],
                    volumes=_volumes(),
                ),
                ttl_seconds_after_finished=IMAGE_REPOSITORY_GC_TTL_SECONDS,
                deadline=deadline,
            )
            await job.run_observed(
                kube,
                deadline=deadline,
                failure_context="image registry storage garbage collection failed",
                log_heading="registry GC Job logs",
                log_failure_label="registry GC Job pod logs",
                tail_lines=IMAGE_REPOSITORY_GC_LOG_TAIL_LINES,
                diagnostic_deadline=Deadline(
                    min(
                        IMAGE_REPOSITORY_GC_DIAGNOSTIC_TIMEOUT_SECONDS,
                        deadline.remaining,
                    )
                ),
                cleanup_deadline=Deadline(
                    min(
                        IMAGE_REPOSITORY_GC_CLEANUP_TIMEOUT_SECONDS,
                        deadline.remaining,
                    )
                ),
            )
        except TimeoutError:
            raise
        except OSError as err:
            msg = f"image registry storage garbage collection failed: {err}"
            raise OSError(msg) from err
        return True
    finally:
        remaining = deadline.remaining
        if remaining > 0:
            restore_deadline = Deadline(
                min(IMAGE_REPOSITORY_GC_RESTORE_TIMEOUT_SECONDS, remaining)
            )
            if restore_required:
                await restore_image_repository_writable(
                    kube,
                    deadline=restore_deadline,
                )
            else:
                await clear_image_repository_maintenance(
                    kube,
                    deadline=restore_deadline,
                )


async def _rollout_registry_config(
    kube: Kube,
    *,
    read_only: bool,
    deadline: Deadline,
) -> None:
    config = await ConfigMap.upsert(
        kube,
        intent=ConfigMapManifest(
            namespace=BERTRAND_NAMESPACE,
            name=IMAGE_REPOSITORY_CONFIG_NAME,
            labels=IMAGE_REPOSITORY_LABELS,
            data=registry_config_data(read_only=read_only),
        ),
        deadline=deadline,
    )
    deployment = await Deployment.upsert(
        kube,
        intent=DeploymentManifest(
            namespace=BERTRAND_NAMESPACE,
            name=IMAGE_REPOSITORY_NAME,
            labels=IMAGE_REPOSITORY_LABELS,
            selector=IMAGE_REPOSITORY_SELECTOR,
            pod_template=PodTemplateSpec(
                containers=[
                    ContainerSpec(
                        name="registry",
                        image=IMAGE_REPOSITORY_IMAGE,
                        image_pull_policy="IfNotPresent",
                        command=["registry"],
                        args=["serve", IMAGE_REPOSITORY_CONFIG_FILE],
                        ports=[
                            {
                                "name": "registry",
                                "containerPort": IMAGE_REPOSITORY_PORT,
                                "protocol": "TCP",
                            }
                        ],
                        readiness_probe={
                            "httpGet": {
                                "path": "/v2/",
                                "port": IMAGE_REPOSITORY_PORT,
                            },
                            "periodSeconds": 2,
                            "failureThreshold": 30,
                        },
                        liveness_probe={
                            "httpGet": {
                                "path": "/v2/",
                                "port": IMAGE_REPOSITORY_PORT,
                            },
                            "initialDelaySeconds": 10,
                            "periodSeconds": 10,
                            "failureThreshold": 3,
                        },
                        volume_mounts=_volume_mounts(),
                    )
                ],
                volumes=_volumes(),
                annotations={
                    IMAGE_REPOSITORY_CONFIG_HASH_ANNOTATION: _config_hash(config.data)
                },
            ),
            strategy={"type": "Recreate", "rollingUpdate": None},
        ),
        deadline=deadline,
    )
    await deployment.wait_rollout(kube, deadline=deadline)


def _volume_mounts() -> tuple[Mapping[str, object], ...]:
    return (
        {"name": IMAGE_REPOSITORY_VOLUME, "mountPath": IMAGE_REPOSITORY_MOUNT},
        {
            "name": IMAGE_REPOSITORY_CONFIG_VOLUME,
            "mountPath": IMAGE_REPOSITORY_CONFIG_DIR,
            "readOnly": True,
        },
    )


def _volumes() -> tuple[VolumeSpec, ...]:
    return (
        VolumeSpec.pvc(IMAGE_REPOSITORY_VOLUME, claim_name=IMAGE_REPOSITORY_NAME),
        VolumeSpec.config_map(
            IMAGE_REPOSITORY_CONFIG_VOLUME,
            config_map_name=IMAGE_REPOSITORY_CONFIG_NAME,
        ),
    )


def image_repository_ref(name: str, tag: str) -> str:
    """Render a stable Bertrand image reference.

    Returns
    -------
    str
        Fully-qualified image reference rooted at Bertrand's local registry host.

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
    return f"{IMAGE_REPOSITORY_PULL_HOST}/bertrand/{path}:{normalized_tag}"


def image_repository_service_ref(ref: str) -> str:
    """Rewrite an internal image ref to the in-cluster Service address.

    Returns
    -------
    str
        Equivalent image reference rooted at the registry Service address.
    """
    return rewrite_registry_ref(
        ref,
        source=IMAGE_REPOSITORY_PULL_HOST,
        target=IMAGE_REPOSITORY_SERVICE_ADDR,
        canonical_host=IMAGE_REPOSITORY_PULL_HOST,
    )


def image_repository_pull_ref(ref: str) -> str:
    """Rewrite an internal image ref to the canonical pull address.

    Returns
    -------
    str
        Equivalent image reference rooted at the local pull address.
    """
    return rewrite_registry_ref(
        ref,
        source=IMAGE_REPOSITORY_SERVICE_ADDR,
        target=IMAGE_REPOSITORY_PULL_HOST,
        canonical_host=IMAGE_REPOSITORY_PULL_HOST,
    )


async def delete_image_manifest(
    digest_ref: str,
    *,
    deadline: Deadline = NO_DEADLINE,
) -> None:
    """Delete one image manifest by immutable registry digest reference.

    Raises
    ------
    OSError
        If the registry rejects the delete request.
    TimeoutError
        If `timeout` is non-positive or the registry request times out.
    ValueError
        If `digest_ref` is not an immutable ref in Bertrand's image repository.
    """
    ref = digest_ref.strip()
    prefix = f"{IMAGE_REPOSITORY_PULL_HOST}/"
    if not ref.startswith(prefix):
        msg = (
            f"image digest ref {digest_ref!r} does not belong to registry "
            f"{IMAGE_REPOSITORY_PULL_HOST!r}"
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
    url = f"{IMAGE_REPOSITORY_PULL_SERVER}/v2/{encoded_path}/manifests/{encoded_digest}"
    remaining = deadline.check(f"timed out deleting image manifest {digest_ref!r}")
    request_timeout = None if math.isinf(remaining) else remaining
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
