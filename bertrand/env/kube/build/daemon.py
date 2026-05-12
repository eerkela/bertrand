"""Per-node BuildKit daemon pool for Bertrand's Kubernetes image builds."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
from typing import TYPE_CHECKING

from bertrand.env.git import BERTRAND_NAMESPACE, INFINITY
from bertrand.env.host import CACHE_DIR
from bertrand.env.kube.api import (
    ContainerPortSpec,
    ContainerSpec,
    Kube,
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

BUILDKIT_NAME = "bertrand-buildkit"
BUILDKIT_IMAGE = "moby/buildkit:v0.29.0"
BUILDKIT_PORT = 1234
BUILDKIT_LISTEN_ADDR = f"tcp://0.0.0.0:{BUILDKIT_PORT}"
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


@dataclass(frozen=True)
class BuildKitBuilder:
    """Ready BuildKit daemon pod in the builder pool.

    Parameters
    ----------
    namespace : str
        Namespace that owns the builder pod.
    pod : str
        Builder pod name.
    node : str
        Node running the builder pod.
    platform : str
        Native OCI platform exposed by the node.
    pod_ip : str
        Pod IP used by short-lived ``buildctl`` Jobs.
    addr : str
        BuildKit client address, suitable for ``buildctl --addr``.
    config_hash : str
        BuildKit config hash annotation installed on the builder pod.
    config_current : bool
        Whether ``config_hash`` matches the expected hash passed by the caller, or
        ``True`` when no expected hash was provided.
    """

    namespace: str
    pod: str
    node: str
    platform: str
    pod_ip: str
    addr: str
    config_hash: str
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
    daemonset_present : bool
        Whether the BuildKit DaemonSet currently exists.
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
    rollout_ready : bool
        Whether the DaemonSet rollout is current.
    expected_config_hash : str | None
        Optional config hash expected by the caller.
    installed_config_hash : str
        Config hash installed on the DaemonSet pod template.
    config_current : bool
        Whether the DaemonSet template hash matches the expected hash, or ``True``
        when no expected hash was provided.
    cache_path : str
        Host path used for node-local BuildKit daemon cache storage.
    cdi_paths : tuple[str, ...]
        Host CDI spec directories mounted into BuildKit pods.
    builders : tuple[BuildKitBuilder, ...]
        Ready builder pods discovered from the pool.
    ready : bool
        Whether the DaemonSet rollout, config hash, and platform coverage are ready.
    failures : tuple[str, ...]
        Human-readable readiness failures, empty when the pool is ready.
    """

    namespace: str
    name: str
    daemonset_present: bool
    desired_builders: int
    ready_builders: int
    expected_platforms: tuple[str, ...]
    ready_platforms: tuple[str, ...]
    missing_platforms: tuple[str, ...]
    rollout_ready: bool
    expected_config_hash: str | None
    installed_config_hash: str
    config_current: bool
    cache_path: str
    cdi_paths: tuple[str, ...]
    builders: tuple[BuildKitBuilder, ...]
    ready: bool

    @property
    def failures(self) -> tuple[str, ...]:
        """Return semantic readiness failures for this builder pool.

        Returns
        -------
        tuple[str, ...]
            Human-readable failures explaining why the builder pool is not ready.
        """
        failures: list[str] = []
        if not self.daemonset_present:
            failures.append("BuildKit DaemonSet is missing")
        if not self.rollout_ready:
            failures.append("BuildKit DaemonSet rollout is not ready")
        if not self.config_current:
            failures.append("BuildKit DaemonSet has stale registry config")
        if not self.ready_builders:
            failures.append("BuildKit has no ready builder pods")
        if self.missing_platforms:
            platforms = ", ".join(self.missing_platforms)
            failures.append(
                f"BuildKit has no ready builder for platform(s): {platforms}"
            )
        return tuple(failures)


@dataclass(frozen=True)
class _BuildKitPoolSnapshot:
    daemonset: DaemonSet | None
    eligible_nodes: tuple[Node, ...]
    builders: tuple[BuildKitBuilder, ...]
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
        buildkitd_flags = (
            f"--addr {BUILDKIT_LISTEN_ADDR!r} "
            f"--allow-insecure-entitlement {BUILDKIT_DEVICE_ENTITLEMENT!r}"
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

    async def builders(
        self,
        kube: Kube,
        *,
        timeout: float,
        config_hash: str | None = None,
    ) -> tuple[BuildKitBuilder, ...]:
        """List ready BuildKit builders in deterministic order.

        Parameters
        ----------
        kube : Kube
            Kubernetes API client for the target cluster.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        config_hash : str | None, optional
            Expected BuildKit config hash used to mark builder freshness.

        Returns
        -------
        tuple[BuildKitBuilder, ...]
            Ready builder pods with client addresses and native platform metadata.

        Raises
        ------
        TimeoutError
            If ``timeout`` is non-positive.
        """
        if timeout <= 0:
            msg = "BuildKit builder discovery timeout must be non-negative"
            raise TimeoutError(msg)
        snapshot = await self._snapshot(
            kube,
            timeout=timeout,
            config_hash=config_hash,
        )
        return snapshot.builders

    async def platforms(self, kube: Kube, *, timeout: float) -> tuple[str, ...]:
        """Return native platforms currently available for builds.

        Parameters
        ----------
        kube : Kube
            Kubernetes API client for the target cluster.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        tuple[str, ...]
            Sorted unique platforms from ready, schedulable Linux nodes.

        Raises
        ------
        TimeoutError
            If ``timeout`` is non-positive.
        """
        if timeout <= 0:
            msg = "BuildKit platform discovery timeout must be non-negative"
            raise TimeoutError(msg)
        snapshot = await self._snapshot(kube, timeout=timeout)
        return snapshot.expected_platforms

    async def schedule(
        self,
        kube: Kube,
        *,
        timeout: float,
        platforms: Iterable[str] | None = None,
        preferred_node: str | None = None,
        config_hash: str | None = None,
    ) -> tuple[BuildKitBuilder, ...]:
        """Select one ready builder for each requested platform.

        Parameters
        ----------
        kube : Kube
            Kubernetes API client for the target cluster.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        platforms : Iterable[str] | None, optional
            Explicit platform filters. If omitted, every eligible cluster platform
            is targeted.
        preferred_node : str | None, optional
            Node to prefer when selecting a builder for its matching platform.
        config_hash : str | None, optional
            Expected BuildKit config hash. When provided, stale builders are
            excluded from scheduling.

        Returns
        -------
        tuple[BuildKitBuilder, ...]
            Selected builders, one per requested platform.

        Raises
        ------
        TimeoutError
            If ``timeout`` is non-positive.
        """
        if timeout <= 0:
            msg = "BuildKit scheduling timeout must be non-negative"
            raise TimeoutError(msg)
        snapshot = await self._snapshot(
            kube,
            timeout=timeout,
            config_hash=config_hash,
        )
        return self._schedule_from(
            snapshot,
            platforms=platforms,
            preferred_node=preferred_node,
            config_hash=config_hash,
        )

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
            return BuildKitPoolStatus(
                namespace=self.namespace,
                name=self.name,
                daemonset_present=daemonset is not None,
                desired_builders=len(snapshot.eligible_nodes),
                ready_builders=len(snapshot.builders),
                expected_platforms=snapshot.expected_platforms,
                ready_platforms=snapshot.ready_platforms,
                missing_platforms=snapshot.missing_platforms,
                rollout_ready=rollout_ready,
                expected_config_hash=config_hash,
                installed_config_hash=installed_hash,
                config_current=config_current,
                cache_path=str(self.cache_path),
                cdi_paths=tuple(path for _, path in BUILDKIT_CDI_SPEC_MOUNTS),
                builders=snapshot.builders,
                ready=(
                    daemonset is not None
                    and rollout_ready
                    and config_current
                    and bool(snapshot.builders)
                    and not snapshot.missing_platforms
                ),
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

    def _eligible_nodes_from(self, nodes: Iterable[Node]) -> tuple[Node, ...]:
        return tuple(
            sorted(
                (node for node in nodes if node.is_build_eligible),
                key=lambda node: node.name,
            )
        )

    def _candidate_groups(
        self,
        snapshot: _BuildKitPoolSnapshot,
        *,
        platforms: Iterable[str] | None,
        preferred_node: str | None,
        config_hash: str | None,
    ) -> dict[str, tuple[BuildKitBuilder, ...]]:
        preferred = preferred_node.strip() if preferred_node is not None else ""
        available = snapshot.expected_platforms
        targets = (
            _normalize_platforms(platforms) if platforms is not None else available
        )
        if not targets:
            msg = "BuildKit scheduling requires at least one eligible platform"
            raise OSError(msg)
        missing = tuple(platform for platform in targets if platform not in available)
        if missing:
            msg = (
                "BuildKit scheduling requested unavailable platform(s): "
                f"{', '.join(missing)}"
            )
            raise OSError(msg)

        builders = snapshot.builders
        if config_hash is not None:
            builders = tuple(builder for builder in builders if builder.config_current)

        out: dict[str, tuple[BuildKitBuilder, ...]] = {}
        for platform in targets:
            candidates = [
                builder for builder in builders if builder.platform == platform
            ]
            candidates.sort(
                key=lambda builder: (
                    builder.node != preferred,
                    builder.platform,
                    builder.node,
                    builder.pod,
                )
            )
            if not candidates:
                msg = f"BuildKit has no ready builder for platform {platform!r}"
                if config_hash is not None:
                    msg = (
                        f"{msg} with config hash {config_hash!r}; rerun "
                        "`bertrand init` to refresh the builder pool"
                    )
                raise OSError(msg)
            out[platform] = tuple(candidates)
        return out

    def _schedule_from(
        self,
        snapshot: _BuildKitPoolSnapshot,
        *,
        platforms: Iterable[str] | None,
        preferred_node: str | None,
        config_hash: str | None,
    ) -> tuple[BuildKitBuilder, ...]:
        groups = self._candidate_groups(
            snapshot,
            platforms=platforms,
            preferred_node=preferred_node,
            config_hash=config_hash,
        )
        return tuple(candidates[0] for candidates in groups.values())

    def _builders_from(
        self,
        nodes: Iterable[Node],
        pods: Iterable[Pod],
        *,
        config_hash: str | None,
    ) -> tuple[BuildKitBuilder, ...]:
        nodes_by_name = {
            node.name: node
            for node in nodes
            if node.name and node.is_build_eligible and node.platform
        }
        builders: list[BuildKitBuilder] = []
        for pod in pods:
            node = nodes_by_name.get(pod.node_name)
            if node is None or not pod.is_ready or not pod.pod_ip:
                continue
            pod_hash = pod.annotations.get(BUILDKIT_CONFIG_HASH_ANNOTATION, "")
            builders.append(
                BuildKitBuilder(
                    namespace=pod.namespace,
                    pod=pod.name,
                    node=node.name,
                    platform=node.platform,
                    pod_ip=pod.pod_ip,
                    addr=f"tcp://{pod.pod_ip}:{self.port}",
                    config_hash=pod_hash,
                    config_current=config_hash is None or pod_hash == config_hash,
                )
            )
        return tuple(sorted(builders, key=_builder_sort_key))


def _builder_sort_key(builder: BuildKitBuilder) -> tuple[str, str, str]:
    return (builder.platform, builder.node, builder.pod)


def _normalize_platforms(platforms: Iterable[str]) -> tuple[str, ...]:
    normalized = {platform.strip().lower() for platform in platforms}
    normalized.discard("")
    return tuple(sorted(normalized))


BUILDKIT_POOL = BuildKitPool(
    namespace=BERTRAND_NAMESPACE,
    name=BUILDKIT_NAME,
    port=BUILDKIT_PORT,
    cache_path=BUILDKIT_CACHE_PATH,
)
