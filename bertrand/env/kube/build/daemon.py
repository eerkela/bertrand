"""Per-node BuildKit daemon pool for Bertrand's Kubernetes image builds."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
from typing import TYPE_CHECKING

from bertrand.env.git import BERTRAND_NAMESPACE, INFINITY
from bertrand.env.host import CACHE_DIR
from bertrand.env.kube.api.spec import (
    ContainerPortSpec,
    ContainerSpec,
    PodTemplateSpec,
    ProbeSpec,
    SecurityContextSpec,
    TolerationSpec,
    VolumeMountSpec,
    VolumeSpec,
)
from bertrand.env.kube.daemonset import DaemonSet
from bertrand.env.kube.node import Node
from bertrand.env.kube.pod import Pod

if TYPE_CHECKING:
    from collections.abc import Iterable
    from pathlib import Path

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
BUILDKIT_NODE_SELECTOR = {"kubernetes.io/os": "linux"}
BUILDKIT_CONTROL_PLANE_TOLERATIONS = (
    TolerationSpec(
        key="node-role.kubernetes.io/control-plane",
        operator="Exists",
        effect="NoSchedule",
    ),
    TolerationSpec(
        key="node-role.kubernetes.io/master",
        operator="Exists",
        effect="NoSchedule",
    ),
)


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
class _BuildKitBuilder:
    """Ready BuildKit daemon pod in the builder pool.

    Parameters
    ----------
    node : str
        Node running the builder pod.
    platform : str
        Native OCI platform exposed by the node.
    config_current : bool
        Whether ``config_hash`` matches the expected hash passed by the caller, or
        ``True`` when no expected hash was provided.
    """

    node: str
    platform: str
    config_current: bool


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


@dataclass(frozen=True)
class _BuildKitPoolSnapshot:
    daemonset: DaemonSet | None
    eligible_nodes: tuple[Node, ...]
    builders: tuple[_BuildKitBuilder, ...]
    expected_platforms: tuple[str, ...]
    ready_platforms: tuple[str, ...]
    missing_platforms: tuple[str, ...]


@dataclass(frozen=True)
class BuildKitPool:
    """Per-node BuildKit daemon pool.

    Parameters
    ----------
    namespace : str
        Namespace that owns the BuildKit DaemonSet.
    name : str
        BuildKit DaemonSet name.
    port : int
        TCP port exposed by each BuildKit pod.
    cache_path : Path
        Host path used for node-local BuildKit daemon cache storage.
    """

    namespace: str
    name: str
    port: int
    cache_path: Path

    @property
    def labels(self) -> dict[str, str]:
        """Return labels applied to BuildKit pool resources.

        Returns
        -------
        dict[str, str]
            Shared BuildKit pool labels.
        """
        return {
            "app.kubernetes.io/name": self.name,
            "app.kubernetes.io/part-of": "bertrand",
            BUILDKIT_LABEL: BUILDKIT_LABEL_VALUE,
        }

    @property
    def selector(self) -> dict[str, str]:
        """Return the BuildKit pod selector.

        Returns
        -------
        dict[str, str]
            Labels used to select BuildKit daemon pods.
        """
        return {
            "app.kubernetes.io/name": self.name,
            BUILDKIT_LABEL: BUILDKIT_LABEL_VALUE,
        }

    async def ensure(
        self,
        kube: Kube,
        *,
        timeout: float = INFINITY,
        config_hash: str | None = None,
    ) -> None:
        """Converge the per-node BuildKit DaemonSet.

        Parameters
        ----------
        kube : Kube
            Kubernetes API client for the target cluster.
        timeout : float, optional
            Maximum runtime budget in seconds. If infinite, wait indefinitely.
        config_hash : str | None, optional
            Hash of the mounted BuildKit configuration. When provided, the hash is
            applied to the pod template to trigger a rollout after config changes.

        Raises
        ------
        TimeoutError
            If ``timeout`` is non-positive or rollout exceeds the budget.
        OSError
            If the cluster has no ready, schedulable Linux build nodes.
        """
        if timeout <= 0:
            msg = "BuildKit pool timeout must be non-negative"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        eligible = await self._eligible_nodes(kube, timeout=deadline - loop.time())
        if not eligible:
            msg = "BuildKit pool requires at least one ready schedulable Linux node"
            raise OSError(msg)
        pod_annotations = (
            {BUILDKIT_CONFIG_HASH_ANNOTATION: config_hash}
            if config_hash is not None
            else None
        )
        buildkitd_flags = " ".join(
            (
                f"--addr {BUILDKIT_LISTEN_ADDR!r}",
                f"--addr {BUILDKIT_SOCKET_ADDR!r}",
                *(
                    f"--allow-insecure-entitlement {entitlement!r}"
                    for entitlement in BUILDKIT_INSECURE_ENTITLEMENTS
                ),
            )
        )
        daemonset = await DaemonSet.upsert(
            kube,
            namespace=self.namespace,
            name=self.name,
            labels=self.labels,
            selector=self.selector,
            pod_template=PodTemplateSpec(
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
                            ContainerPortSpec(
                                name="grpc",
                                container_port=self.port,
                            )
                        ],
                        security_context=SecurityContextSpec(
                            privileged=True,
                            run_as_user=0,
                        ),
                        readiness_probe=ProbeSpec.tcp(
                            port=self.port,
                            period_seconds=2,
                            failure_threshold=30,
                        ),
                        liveness_probe=ProbeSpec.tcp(
                            port=self.port,
                            initial_delay_seconds=10,
                            period_seconds=10,
                            failure_threshold=3,
                        ),
                        volume_mounts=[
                            VolumeMountSpec(
                                name=BUILDKIT_CACHE_VOLUME,
                                mount_path=BUILDKIT_CACHE_MOUNT,
                            ),
                            VolumeMountSpec(
                                name=BUILDKIT_CONFIG_VOLUME,
                                mount_path=BUILDKIT_CONFIG_DIR,
                                read_only=True,
                            ),
                            VolumeMountSpec(
                                name=BUILDKIT_SOCKET_VOLUME,
                                mount_path=BUILDKIT_SOCKET_DIR,
                            ),
                            *(
                                VolumeMountSpec(
                                    name=name,
                                    mount_path=path,
                                    read_only=True,
                                )
                                for name, path in BUILDKIT_CDI_SPEC_MOUNTS
                            ),
                        ],
                    )
                ],
                volumes=[
                    VolumeSpec.host_path(
                        BUILDKIT_CACHE_VOLUME,
                        path=self.cache_path,
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
                annotations=pod_annotations,
                node_selector=BUILDKIT_NODE_SELECTOR,
                tolerations=BUILDKIT_CONTROL_PLANE_TOLERATIONS,
            ),
            timeout=deadline - loop.time(),
        )
        await daemonset.wait_rollout(kube, timeout=deadline - loop.time())

    async def status(
        self,
        kube: Kube,
        *,
        timeout: float = INFINITY,
        config_hash: str | None = None,
    ) -> BuildKitPoolStatus:
        """Inspect BuildKit pool readiness without mutating the cluster.

        Parameters
        ----------
        kube : Kube
            Kubernetes API client for the target cluster.
        timeout : float, optional
            Maximum request budget in seconds. If infinite, wait indefinitely.
        config_hash : str | None, optional
            Expected BuildKit configuration hash. When omitted, config freshness is
            not considered a failure.

        Returns
        -------
        BuildKitPoolStatus
            Read-only BuildKit pool readiness report.

        Raises
        ------
        TimeoutError
            If ``timeout`` is non-positive.
        OSError
            If Kubernetes read operations fail or return malformed data.
        """
        if timeout <= 0:
            msg = "BuildKit pool status timeout must be non-negative"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        try:
            snapshot = await self._snapshot(
                kube,
                timeout=deadline - loop.time(),
                config_hash=config_hash,
                include_daemonset=True,
            )
            daemonset = snapshot.daemonset
            installed_hash = (
                daemonset.pod_annotations.get(BUILDKIT_CONFIG_HASH_ANNOTATION, "")
                if daemonset is not None
                else ""
            )
            config_current = config_hash is None or installed_hash == config_hash
            rollout_ready = (
                daemonset.rollout_ready(minimum=1) if daemonset is not None else False
            )
            failures: list[str] = []
            if daemonset is None:
                failures.append("BuildKit DaemonSet is missing")
            if not rollout_ready:
                failures.append("BuildKit DaemonSet rollout is not ready")
            if not config_current:
                failures.append("BuildKit DaemonSet has stale registry config")
            if not snapshot.builders:
                failures.append("BuildKit has no ready builder pods")
            if snapshot.missing_platforms:
                platforms = ", ".join(snapshot.missing_platforms)
                failures.append(
                    f"BuildKit has no ready builder for platform(s): {platforms}"
                )
            return BuildKitPoolStatus(
                namespace=self.namespace,
                name=self.name,
                desired_builders=len(snapshot.eligible_nodes),
                ready_builders=len(snapshot.builders),
                expected_platforms=snapshot.expected_platforms,
                ready_platforms=snapshot.ready_platforms,
                missing_platforms=snapshot.missing_platforms,
                ready=not failures,
                failures=tuple(failures),
            )
        except OSError as err:
            msg = f"failed to inspect BuildKit pool {self.namespace}/{self.name}: {err}"
            raise OSError(msg) from err

    async def _snapshot(
        self,
        kube: Kube,
        *,
        timeout: float,
        config_hash: str | None = None,
        include_daemonset: bool = False,
    ) -> _BuildKitPoolSnapshot:
        if timeout <= 0:
            msg = "BuildKit pool discovery timeout must be non-negative"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        daemonset = (
            await DaemonSet.get(
                kube,
                namespace=self.namespace,
                name=self.name,
                timeout=deadline - loop.time(),
            )
            if include_daemonset
            else None
        )
        nodes = await Node.list(kube, timeout=deadline - loop.time())
        pods = await Pod.list(
            kube,
            timeout=deadline - loop.time(),
            namespaces=(self.namespace,),
            labels=self.selector,
        )
        eligible = self._eligible_nodes_from(nodes)
        builders = self._builders_from(nodes, pods, config_hash=config_hash)
        expected_platforms = tuple(
            sorted({node.platform for node in eligible if node.platform})
        )
        ready_platforms = tuple(
            sorted({builder.platform for builder in builders if builder.platform})
        )
        missing_platforms = tuple(
            platform
            for platform in expected_platforms
            if platform not in set(ready_platforms)
        )
        return _BuildKitPoolSnapshot(
            daemonset=daemonset,
            eligible_nodes=eligible,
            builders=builders,
            expected_platforms=expected_platforms,
            ready_platforms=ready_platforms,
            missing_platforms=missing_platforms,
        )

    async def _eligible_nodes(self, kube: Kube, *, timeout: float) -> tuple[Node, ...]:
        nodes = await Node.list(kube, timeout=timeout)
        return self._eligible_nodes_from(nodes)

    async def _ready_platform_nodes(
        self,
        kube: Kube,
        *,
        timeout: float,
        config_hash: str | None,
    ) -> dict[str, tuple[str, ...]]:
        """Return ready BuildKit node names grouped by native platform.

        Parameters
        ----------
        kube : Kube
            Kubernetes API client for the target cluster.
        timeout : float
            Maximum discovery budget in seconds. If infinite, wait indefinitely.
        config_hash : str | None
            Expected BuildKit configuration hash. When provided, stale builder pods
            are excluded from the returned candidates.

        Returns
        -------
        dict[str, tuple[str, ...]]
            Ready node names keyed by eligible native platform.

        Raises
        ------
        OSError
            If no current builder is available for an eligible platform.
        """
        snapshot = await self._snapshot(
            kube,
            timeout=timeout,
            config_hash=config_hash,
        )
        targets = snapshot.expected_platforms
        if not targets:
            msg = "BuildKit scheduling requires at least one eligible platform"
            raise OSError(msg)

        builders = snapshot.builders
        if config_hash is not None:
            builders = tuple(builder for builder in builders if builder.config_current)

        out: dict[str, tuple[str, ...]] = {}
        for platform in targets:
            candidates = [
                builder for builder in builders if builder.platform == platform
            ]
            candidates.sort(key=_builder_sort_key)
            if not candidates:
                msg = f"BuildKit has no ready builder for platform {platform!r}"
                if config_hash is not None:
                    msg = (
                        f"{msg} with config hash {config_hash!r}; rerun "
                        "`bertrand init` to refresh the builder pool"
                    )
                raise OSError(msg)
            out[platform] = tuple(builder.node for builder in candidates)
        return out

    def _eligible_nodes_from(self, nodes: Iterable[Node]) -> tuple[Node, ...]:
        return tuple(
            sorted(
                (node for node in nodes if node.is_build_eligible),
                key=lambda node: node.name,
            )
        )

    def _builders_from(
        self,
        nodes: Iterable[Node],
        pods: Iterable[Pod],
        *,
        config_hash: str | None,
    ) -> tuple[_BuildKitBuilder, ...]:
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
                    node=node.name,
                    platform=node.platform,
                    config_current=config_hash is None or pod_hash == config_hash,
                )
            )
        return tuple(sorted(builders, key=_builder_sort_key))


def _builder_sort_key(builder: _BuildKitBuilder) -> tuple[str, str]:
    return (builder.platform, builder.node)


BUILDKIT_POOL = BuildKitPool(
    namespace=BERTRAND_NAMESPACE,
    name=BUILDKIT_NAME,
    port=BUILDKIT_PORT,
    cache_path=BUILDKIT_CACHE_PATH,
)
