"""Per-node BuildKit daemon pool for Bertrand's Kubernetes image builds."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING

from bertrand.env.git import BERTRAND_NAMESPACE, NO_DEADLINE, Deadline
from bertrand.env.host import CACHE_DIR
from bertrand.env.kube.api.spec import ContainerSpec, PodTemplateSpec, VolumeSpec
from bertrand.env.kube.daemonset import DaemonSet
from bertrand.env.kube.node import Node
from bertrand.env.kube.pod import Pod

if TYPE_CHECKING:
    from collections.abc import Iterable

    from bertrand.env.kube.api.client import Kube

BUILDKIT_NAME = "bertrand-buildkit"
BUILDKIT_IMAGE = "moby/buildkit:v0.29.0"
BUILDKIT_PORT = 1234
BUILDKIT_LISTEN_ADDR = f"tcp://0.0.0.0:{BUILDKIT_PORT}"
BUILDKIT_SOCKET_DIR = "/run/bertrand/buildkit"
BUILDKIT_SOCKET_FILE = f"{BUILDKIT_SOCKET_DIR}/buildkitd.sock"
BUILDKIT_SOCKET_ADDR = f"unix://{BUILDKIT_SOCKET_FILE}"
BUILDKIT_SOCKET_VOLUME = "buildkit-socket"
BUILDKIT_CACHE_MOUNT = "/var/lib/buildkit"
BUILDKIT_CACHE_PATH = CACHE_DIR / "buildkit"
BUILDKIT_CACHE_VOLUME = "buildkit-state"
BUILDKIT_CONFIG_DIR = "/etc/buildkit"
BUILDKIT_CONFIG_FILE = f"{BUILDKIT_CONFIG_DIR}/buildkitd.toml"
BUILDKIT_CONFIG_KEY = "buildkitd.toml"
BUILDKIT_CONFIG_NAME = f"{BUILDKIT_NAME}-registry"
BUILDKIT_CONFIG_HASH_ANNOTATION = "bertrand.dev/buildkit-config-hash"
BUILDKIT_CONFIG_VOLUME = "buildkit-config"
BUILDKIT_DEVICE_ENTITLEMENT = "device"
BUILDKIT_INSECURE_ENTITLEMENTS = (BUILDKIT_DEVICE_ENTITLEMENT,)
BUILDKIT_GC_RESERVED_SPACE = "4GB"
BUILDKIT_GC_MAX_USED_SPACE = "20GB"
BUILDKIT_GC_MIN_FREE_SPACE = "10GB"
BUILDKIT_GC_LOCAL_KEEP_DURATION = "48h"
BUILDKIT_GC_BROAD_KEEP_DURATION = "720h"
BUILDKIT_CDI_SPEC_MOUNTS = (
    ("buildkit-cdi-etc", "/etc/cdi"),
    ("buildkit-cdi-run", "/var/run/cdi"),
)
BUILDKIT_LABEL = "bertrand.dev/buildkit"
BUILDKIT_LABEL_VALUE = "v1"
_BUILDKIT_POOL_LABELS = MappingProxyType(
    {
        "app.kubernetes.io/name": BUILDKIT_NAME,
        "app.kubernetes.io/part-of": "bertrand",
        BUILDKIT_LABEL: BUILDKIT_LABEL_VALUE,
    }
)
_BUILDKIT_POOL_SELECTOR = MappingProxyType(
    {
        "app.kubernetes.io/name": BUILDKIT_NAME,
        BUILDKIT_LABEL: BUILDKIT_LABEL_VALUE,
    }
)
BUILDKIT_NODE_SELECTOR = {"kubernetes.io/os": "linux"}
BUILDKIT_CONTROL_PLANE_TOLERATIONS = (
    {
        "key": "node-role.kubernetes.io/control-plane",
        "operator": "Exists",
        "effect": "NoSchedule",
    },
    {
        "key": "node-role.kubernetes.io/master",
        "operator": "Exists",
        "effect": "NoSchedule",
    },
)
BUILDKIT_ROLLOUT_DIAGNOSTIC_TIMEOUT_SECONDS = 10.0


def buildkit_worker_gc_toml() -> str:
    """Return BuildKit worker garbage-collection TOML.

    Returns
    -------
    str
        TOML fragment that bounds the node-local OCI worker cache.
    """
    return (
        "[worker.oci]\n"
        "  enabled = true\n"
        "  gc = true\n"
        f"  reservedSpace = \"{BUILDKIT_GC_RESERVED_SPACE}\"\n"
        f"  maxUsedSpace = \"{BUILDKIT_GC_MAX_USED_SPACE}\"\n"
        f"  minFreeSpace = \"{BUILDKIT_GC_MIN_FREE_SPACE}\"\n"
        "\n"
        "[[worker.oci.gcpolicy]]\n"
        "  filters = [\n"
        "    \"type==source.local\",\n"
        "    \"type==exec.cachemount\",\n"
        "    \"type==source.git.checkout\",\n"
        "  ]\n"
        f"  keepDuration = \"{BUILDKIT_GC_LOCAL_KEEP_DURATION}\"\n"
        f"  reservedSpace = \"{BUILDKIT_GC_RESERVED_SPACE}\"\n"
        f"  maxUsedSpace = \"{BUILDKIT_GC_MAX_USED_SPACE}\"\n"
        f"  minFreeSpace = \"{BUILDKIT_GC_MIN_FREE_SPACE}\"\n"
        "\n"
        "[[worker.oci.gcpolicy]]\n"
        f"  keepDuration = \"{BUILDKIT_GC_BROAD_KEEP_DURATION}\"\n"
        f"  reservedSpace = \"{BUILDKIT_GC_RESERVED_SPACE}\"\n"
        f"  maxUsedSpace = \"{BUILDKIT_GC_MAX_USED_SPACE}\"\n"
        f"  minFreeSpace = \"{BUILDKIT_GC_MIN_FREE_SPACE}\"\n"
        "\n"
        "[[worker.oci.gcpolicy]]\n"
        f"  reservedSpace = \"{BUILDKIT_GC_RESERVED_SPACE}\"\n"
        f"  maxUsedSpace = \"{BUILDKIT_GC_MAX_USED_SPACE}\"\n"
        f"  minFreeSpace = \"{BUILDKIT_GC_MIN_FREE_SPACE}\"\n"
        "\n"
        "[[worker.oci.gcpolicy]]\n"
        "  all = true\n"
        f"  reservedSpace = \"{BUILDKIT_GC_RESERVED_SPACE}\"\n"
        f"  maxUsedSpace = \"{BUILDKIT_GC_MAX_USED_SPACE}\"\n"
        f"  minFreeSpace = \"{BUILDKIT_GC_MIN_FREE_SPACE}\"\n"
    )


@dataclass(frozen=True)
class BuildKitPoolStatus:
    """Read-only readiness report for the BuildKit builder pool.

    Parameters
    ----------
    namespace : str
        Namespace that owns the BuildKit DaemonSet.
    name : str
        BuildKit DaemonSet name.
    desired_builders : int
        Number of eligible Linux nodes expected to host builders.
    ready_builders : int
        Number of ready BuildKit builder pods.
    expected_platforms : tuple[str, ...]
        Native platforms present on eligible cluster nodes.
    ready_platforms : tuple[str, ...]
        Native platforms represented by ready builder pods.
    missing_platforms : tuple[str, ...]
        Eligible native platforms without a ready builder pod.
    ready : bool
        Whether the DaemonSet rollout, config hash, and platform coverage are ready.
    failures : tuple[str, ...]
        Human-readable readiness failures, empty when the pool is ready.
    pod_diagnostics : tuple[str, ...]
        Diagnostics from non-ready BuildKit pods, including image pull and container
        waiting reasons when Kubernetes reports them.
    """

    namespace: str
    name: str
    desired_builders: int
    ready_builders: int
    expected_platforms: tuple[str, ...]
    ready_platforms: tuple[str, ...]
    missing_platforms: tuple[str, ...]
    ready: bool
    failures: tuple[str, ...]
    pod_diagnostics: tuple[str, ...]


@dataclass(frozen=True)
class _BuildKitBuilder:
    node_name: str
    platform: str
    current: bool


@dataclass(frozen=True)
class _BuildKitPoolInventory:
    daemonset: DaemonSet | None
    eligible_nodes: tuple[Node, ...]
    builders: tuple[_BuildKitBuilder, ...]
    expected_platforms: tuple[str, ...]
    ready_platforms: tuple[str, ...]
    missing_platforms: tuple[str, ...]
    pod_diagnostics: tuple[str, ...]

    def ready_nodes_by_platform(
        self, *, current_only: bool
    ) -> dict[str, tuple[str, ...]]:
        out: dict[str, tuple[str, ...]] = {}
        for platform in self.expected_platforms:
            out[platform] = tuple(
                builder.node_name
                for builder in self.builders
                if builder.platform == platform
                and (builder.current or not current_only)
            )
        return out


async def ensure_buildkit_pool(
    kube: Kube,
    *,
    deadline: Deadline = NO_DEADLINE,
    config_hash: str | None = None,
) -> None:
    """Converge the per-node BuildKit DaemonSet.

    Parameters
    ----------
    kube : Kube
        Kubernetes API client for the target cluster.
    deadline : Deadline
        Maximum runtime budget in seconds. If infinite, wait indefinitely.
    config_hash : str | None, optional
        Hash of the mounted BuildKit configuration. When provided, the hash is
        applied to the pod template to trigger a rollout after config changes.

    Raises
    ------
    TimeoutError
        If rollout exceeds the budget.
    OSError
        If the cluster has no ready, schedulable Linux build nodes.
    """
    nodes = await Node.list(kube, deadline=deadline)
    if not _eligible_nodes_from(nodes):
        msg = "BuildKit pool requires at least one ready schedulable Linux node"
        raise OSError(msg)
    daemonset = await DaemonSet.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=BUILDKIT_NAME,
        labels=_BUILDKIT_POOL_LABELS,
        selector=_BUILDKIT_POOL_SELECTOR,
        pod_template=_pod_template(config_hash=config_hash),
        deadline=deadline,
    )
    try:
        await daemonset.wait_rollout(kube, deadline=deadline)
    except (OSError, TimeoutError) as err:
        msg = await _rollout_error(kube, config_hash=config_hash)
        if isinstance(err, TimeoutError):
            raise TimeoutError(msg) from err
        raise OSError(msg) from err


async def buildkit_pool_status(
    kube: Kube,
    *,
    deadline: Deadline = NO_DEADLINE,
    config_hash: str | None = None,
) -> BuildKitPoolStatus:
    """Inspect BuildKit pool readiness without mutating the cluster.

    Parameters
    ----------
    kube : Kube
        Kubernetes API client for the target cluster.
    deadline : Deadline
        Maximum request budget in seconds. If infinite, wait indefinitely.
    config_hash : str | None, optional
        Expected BuildKit configuration hash. When omitted, config freshness is not
        considered a failure.

    Returns
    -------
    BuildKitPoolStatus
        Read-only BuildKit pool readiness report.

    Raises
    ------
    OSError
        If Kubernetes read operations fail or return malformed data.
    """
    try:
        inventory = await _buildkit_pool_inventory(
            kube,
            config_hash=config_hash,
            deadline=deadline,
        )
        installed_hash = (
            inventory.daemonset.pod_annotations.get(BUILDKIT_CONFIG_HASH_ANNOTATION, "")
            if inventory.daemonset is not None
            else ""
        )
        config_current = config_hash is None or installed_hash == config_hash
        rollout_ready = (
            inventory.daemonset.rollout_ready(minimum=1)
            if inventory.daemonset is not None
            else False
        )
        failures: list[str] = []
        if inventory.daemonset is None:
            failures.append("BuildKit DaemonSet is missing")
        if not rollout_ready:
            failures.append("BuildKit DaemonSet rollout is not ready")
        if not config_current:
            failures.append("BuildKit DaemonSet has stale registry config")
        if not inventory.builders:
            failures.append("BuildKit has no ready builder pods")
        if inventory.missing_platforms:
            platforms = ", ".join(inventory.missing_platforms)
            failures.append(
                f"BuildKit has no ready builder for platform(s): {platforms}"
            )
        if failures and inventory.pod_diagnostics:
            failures.extend(inventory.pod_diagnostics)
        return BuildKitPoolStatus(
            namespace=BERTRAND_NAMESPACE,
            name=BUILDKIT_NAME,
            desired_builders=len(inventory.eligible_nodes),
            ready_builders=len(inventory.builders),
            expected_platforms=inventory.expected_platforms,
            ready_platforms=inventory.ready_platforms,
            missing_platforms=inventory.missing_platforms,
            ready=not failures,
            failures=tuple(failures),
            pod_diagnostics=inventory.pod_diagnostics,
        )
    except OSError as err:
        msg = (
            f"failed to inspect BuildKit pool {BERTRAND_NAMESPACE}/{BUILDKIT_NAME}: "
            f"{err}"
        )
        raise OSError(msg) from err


async def ready_buildkit_platform_nodes(
    kube: Kube,
    *,
    deadline: Deadline,
    config_hash: str | None,
) -> dict[str, tuple[str, ...]]:
    """Return ready BuildKit node names grouped by native platform.

    Parameters
    ----------
    kube : Kube
        Kubernetes API client for the target cluster.
    deadline : Deadline
        Maximum discovery budget in seconds. If infinite, wait indefinitely.
    config_hash : str | None
        Expected BuildKit configuration hash. When provided, stale builder pods are
        excluded from the returned candidates.

    Returns
    -------
    dict[str, tuple[str, ...]]
        Ready node names keyed by eligible native platform.

    Raises
    ------
    OSError
        If no current builder is available for an eligible platform.
    """
    inventory = await _buildkit_pool_inventory(
        kube,
        config_hash=config_hash,
        deadline=deadline,
    )
    if not inventory.expected_platforms:
        msg = "BuildKit scheduling requires at least one eligible platform"
        raise OSError(msg)

    out: dict[str, tuple[str, ...]] = {}
    builders_by_platform = inventory.ready_nodes_by_platform(
        current_only=config_hash is not None
    )
    for platform in inventory.expected_platforms:
        candidates = builders_by_platform[platform]
        if not candidates:
            msg = f"BuildKit has no ready builder for platform {platform!r}"
            if config_hash is not None:
                msg = (
                    f"{msg} with config hash {config_hash!r}; rerun "
                    "`bertrand init` to refresh the builder pool"
                )
            raise OSError(msg)
        out[platform] = candidates
    return out


async def _buildkit_pool_inventory(
    kube: Kube,
    *,
    config_hash: str | None,
    deadline: Deadline,
) -> _BuildKitPoolInventory:
    daemonset, nodes, pods = await asyncio.gather(
        DaemonSet.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=BUILDKIT_NAME,
            deadline=deadline,
        ),
        Node.list(kube, deadline=deadline),
        Pod.list(
            kube,
            deadline=deadline,
            namespaces=(BERTRAND_NAMESPACE,),
            labels=_BUILDKIT_POOL_SELECTOR,
        ),
    )
    nodes = tuple(nodes)
    pods = tuple(pods)
    eligible_nodes = _eligible_nodes_from(nodes)
    nodes_by_name = {
        node.name: node
        for node in nodes
        if node.name and node.is_build_eligible and node.platform
    }

    builders: list[_BuildKitBuilder] = []
    for pod in pods:
        node = nodes_by_name.get(pod.node_name)
        if node is None or not pod.is_ready or not pod.pod_ip:
            continue
        pod_hash = pod.annotations.get(BUILDKIT_CONFIG_HASH_ANNOTATION, "")
        builders.append(
            _BuildKitBuilder(
                node_name=node.name,
                platform=node.platform,
                current=config_hash is None or pod_hash == config_hash,
            )
        )
    builders.sort(key=lambda builder: (builder.platform, builder.node_name))

    expected_platforms = tuple(
        sorted({node.platform for node in eligible_nodes if node.platform})
    )
    ready_platforms = tuple(
        sorted({builder.platform for builder in builders if builder.platform})
    )
    missing_platforms = tuple(
        platform
        for platform in expected_platforms
        if platform not in set(ready_platforms)
    )
    return _BuildKitPoolInventory(
        daemonset=daemonset,
        eligible_nodes=eligible_nodes,
        builders=tuple(builders),
        expected_platforms=expected_platforms,
        ready_platforms=ready_platforms,
        missing_platforms=missing_platforms,
        pod_diagnostics=_pod_diagnostics(pods),
    )


def _buildkitd_flags() -> str:
    return " ".join(
        (
            f"--addr {BUILDKIT_LISTEN_ADDR!r}",
            f"--addr {BUILDKIT_SOCKET_ADDR!r}",
            *(
                f"--allow-insecure-entitlement {entitlement!r}"
                for entitlement in BUILDKIT_INSECURE_ENTITLEMENTS
            ),
        )
    )


def _pod_template(*, config_hash: str | None) -> PodTemplateSpec:
    buildkitd_flags = _buildkitd_flags()
    annotations = (
        None if config_hash is None else {BUILDKIT_CONFIG_HASH_ANNOTATION: config_hash}
    )
    return PodTemplateSpec(
        containers=[
            ContainerSpec(
                name="buildkitd",
                image=BUILDKIT_IMAGE,
                image_pull_policy="IfNotPresent",
                command=["/bin/sh", "-ec"],
                args=[
                    (
                        f"if [ -s {BUILDKIT_CONFIG_FILE!r} ]; then "
                        f"exec buildkitd {buildkitd_flags} "
                        f"--config {BUILDKIT_CONFIG_FILE!r}; "
                        "fi; "
                        f"exec buildkitd {buildkitd_flags}"
                    )
                ],
                ports=[
                    {"name": "grpc", "containerPort": BUILDKIT_PORT, "protocol": "TCP"}
                ],
                security_context={"privileged": True, "runAsUser": 0},
                readiness_probe={
                    "tcpSocket": {"port": BUILDKIT_PORT},
                    "periodSeconds": 2,
                    "failureThreshold": 30,
                },
                liveness_probe={
                    "tcpSocket": {"port": BUILDKIT_PORT},
                    "initialDelaySeconds": 10,
                    "periodSeconds": 10,
                    "failureThreshold": 3,
                },
                volume_mounts=[
                    {
                        "name": BUILDKIT_CACHE_VOLUME,
                        "mountPath": BUILDKIT_CACHE_MOUNT,
                    },
                    {
                        "name": BUILDKIT_CONFIG_VOLUME,
                        "mountPath": BUILDKIT_CONFIG_DIR,
                        "readOnly": True,
                    },
                    {
                        "name": BUILDKIT_SOCKET_VOLUME,
                        "mountPath": BUILDKIT_SOCKET_DIR,
                    },
                    *(
                        {"name": name, "mountPath": path, "readOnly": True}
                        for name, path in BUILDKIT_CDI_SPEC_MOUNTS
                    ),
                ],
            )
        ],
        volumes=[
            VolumeSpec.host_path(
                BUILDKIT_CACHE_VOLUME,
                path=BUILDKIT_CACHE_PATH,
                host_path_type="DirectoryOrCreate",
            ),
            VolumeSpec.config_map(
                BUILDKIT_CONFIG_VOLUME,
                config_map_name=BUILDKIT_CONFIG_NAME,
                optional=True,
            ),
            VolumeSpec.host_path(
                BUILDKIT_SOCKET_VOLUME,
                path=BUILDKIT_SOCKET_DIR,
                host_path_type="DirectoryOrCreate",
            ),
            *(
                VolumeSpec.host_path(
                    name,
                    path=path,
                    host_path_type="DirectoryOrCreate",
                )
                for name, path in BUILDKIT_CDI_SPEC_MOUNTS
            ),
        ],
        annotations=annotations,
        node_selector=BUILDKIT_NODE_SELECTOR,
        tolerations=BUILDKIT_CONTROL_PLANE_TOLERATIONS,
    )


async def _rollout_error(
    kube: Kube,
    *,
    config_hash: str | None,
) -> str:
    msg = (
        f"BuildKit DaemonSet {BERTRAND_NAMESPACE}/{BUILDKIT_NAME} did not become ready"
    )
    try:
        status = await buildkit_pool_status(
            kube,
            deadline=Deadline(BUILDKIT_ROLLOUT_DIAGNOSTIC_TIMEOUT_SECONDS),
            config_hash=config_hash,
        )
    except (OSError, TimeoutError) as diagnostic_err:
        return f"{msg}; failed to collect pod diagnostics: {diagnostic_err}"
    failures = status.failures or ("rollout did not become ready",)
    detail = "\n".join(f"- {failure}" for failure in failures)
    return f"{msg}:\n{detail}"


def _eligible_nodes_from(nodes: Iterable[Node]) -> tuple[Node, ...]:
    return tuple(
        sorted(
            (node for node in nodes if node.is_build_eligible),
            key=lambda node: node.name,
        )
    )


def _pod_diagnostics(pods: Iterable[Pod]) -> tuple[str, ...]:
    out: list[str] = []
    for pod in sorted(pods, key=lambda pod: (pod.namespace, pod.name)):
        if pod.is_ready:
            continue
        diagnostics = pod.status_diagnostics
        if diagnostics:
            out.extend(diagnostics)
        else:
            out.append(f"Pod {pod.namespace}/{pod.name} is not ready")
    return tuple(out)
