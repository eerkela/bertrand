"""Long-lived BuildKit daemon for Bertrand's Kubernetes image build runtime.

The daemon is represented as one stable Deployment and Service pair.
"""

from __future__ import annotations

import asyncio
from dataclasses import dataclass

from bertrand.env.git import BERTRAND_NAMESPACE, INFINITY
from bertrand.env.kube.api import (
    ContainerPortSpec,
    ContainerSpec,
    DeploymentStrategySpec,
    Kube,
    ProbeSpec,
    SecurityContextSpec,
    ServicePortSpec,
    VolumeMountSpec,
    VolumeSpec,
)
from bertrand.env.kube.build._status import _deployment_status, _service_status
from bertrand.env.kube.build.cache import BUILDKIT_CACHE, BuildKitCacheStatus
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.pod import Pod
from bertrand.env.kube.service import Service

BUILDKIT_NAME = "bertrand-buildkit"
BUILDKIT_IMAGE = "moby/buildkit:v0.29.0"
BUILDKIT_PORT = 1234
BUILDKIT_LISTEN_ADDR = f"tcp://0.0.0.0:{BUILDKIT_PORT}"
BUILDKIT_CACHE_MOUNT = "/var/lib/buildkit"
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


@dataclass(frozen=True)
class BuildKitStatus:
    """Read-only readiness report for Bertrand's BuildKit daemon.

    Parameters
    ----------
    namespace : str
        Namespace that owns the BuildKit resources.
    name : str
        BuildKit Service and Deployment name.
    service_present : bool
        Whether the BuildKit Service currently exists.
    service_selector_ready : bool
        Whether the Service selector matches the expected BuildKit pod selector.
    service_port_ready : bool
        Whether the Service exposes the expected BuildKit gRPC port.
    service_ready : bool
        Whether the Service exists and has the expected type, selector, and port.
    deployment_present : bool
        Whether the BuildKit Deployment currently exists.
    available_replicas : int
        Deployment replicas currently reported available.
    updated_replicas : int
        Deployment replicas updated to the latest pod template.
    observed_generation : int
        Deployment generation observed by the Kubernetes controller.
    generation : int
        Desired Deployment metadata generation.
    rollout_ready : bool
        Whether the Deployment controller has observed the desired generation and at
        least one replica is updated and available.
    expected_config_hash : str | None
        Optional config hash expected by the caller.
    installed_config_hash : str
        Config hash installed on the BuildKit pod template annotation.
    config_current : bool
        Whether the installed config hash matches the expected hash, or `True` when
        no expected hash was provided.
    storage_ready : bool
        Whether the persistent BuildKit daemon cache PVC is ready.
    cache : BuildKitCacheStatus
        BuildKit daemon cache PVC readiness report.
    ready : bool
        Whether the Service exists, Deployment rollout is current, config is current,
        and cache is ready.
    """

    namespace: str
    name: str
    service_present: bool
    service_selector_ready: bool
    service_port_ready: bool
    service_ready: bool
    deployment_present: bool
    available_replicas: int
    updated_replicas: int
    observed_generation: int
    generation: int
    rollout_ready: bool
    expected_config_hash: str | None
    installed_config_hash: str
    config_current: bool
    storage_ready: bool
    cache: BuildKitCacheStatus
    ready: bool


@dataclass(frozen=True)
class BuildKit:
    """Stable in-cluster address for Bertrand's BuildKit daemon.

    Attributes
    ----------
    namespace : str
        Kubernetes namespace that contains the BuildKit Service.
    service : str
        Kubernetes Service name used to route BuildKit client traffic.
    port : int
        TCP port exposed by the Service for BuildKit's gRPC API.
    addr : str
        BuildKit client address, suitable for `buildctl --addr` or `BUILDKIT_HOST`.
    """

    namespace: str
    service: str
    port: int
    addr: str

    @property
    def labels(self) -> dict[str, str]:
        """Return shared BuildKit resource labels.

        Returns
        -------
        dict[str, str]
            Labels shared by the BuildKit Deployment and Service.
        """
        return {
            "app.kubernetes.io/name": self.service,
            "app.kubernetes.io/part-of": "bertrand",
            BUILDKIT_LABEL: BUILDKIT_LABEL_VALUE,
        }

    @property
    def selector(self) -> dict[str, str]:
        """Return the BuildKit pod selector.

        Returns
        -------
        dict[str, str]
            Labels used to bind the BuildKit Service to its pods.
        """
        return {
            "app.kubernetes.io/name": self.service,
            BUILDKIT_LABEL: BUILDKIT_LABEL_VALUE,
        }

    async def ensure(
        self,
        kube: Kube,
        *,
        timeout: float = INFINITY,
        config_hash: str | None = None,
    ) -> None:
        """Converge Bertrand's bootstrap BuildKit Deployment and Service.

        Parameters
        ----------
        kube : Kube
            Kubernetes API client for the target cluster.
        timeout : float, optional
            Maximum runtime budget in seconds. If infinite, wait indefinitely.
        config_hash : str | None, optional
            Hash of the mounted BuildKit configuration.  When provided, the hash is
            applied to the pod template to trigger a rollout after config changes.

        Raises
        ------
        TimeoutError
            If `timeout` is non-positive, Kubernetes requests exceed the budget, or the
            Deployment does not report at least one available replica before the
            deadline.
        """
        if timeout <= 0:
            msg = "BuildKit daemon timeout must be non-negative"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        pod_annotations = (
            {BUILDKIT_CONFIG_HASH_ANNOTATION: config_hash}
            if config_hash is not None
            else None
        )
        buildkitd_flags = (
            f"--addr {BUILDKIT_LISTEN_ADDR!r} "
            f"--allow-insecure-entitlement {BUILDKIT_DEVICE_ENTITLEMENT!r}"
        )

        await Service.upsert(
            kube,
            namespace=self.namespace,
            name=self.service,
            labels=self.labels,
            selector=self.selector,
            ports=[
                ServicePortSpec(
                    name="grpc",
                    port=self.port,
                    target_port=self.port,
                )
            ],
            timeout=deadline - loop.time(),
        )
        cache = await BUILDKIT_CACHE.ensure(kube, timeout=deadline - loop.time())
        deployment = await Deployment.upsert(
            kube,
            namespace=self.namespace,
            name=self.service,
            labels=self.labels,
            selector=self.selector,
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
                VolumeSpec.pvc(
                    BUILDKIT_CACHE_VOLUME,
                    claim_name=cache.name,
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
            pod_annotations=pod_annotations,
            strategy=DeploymentStrategySpec.recreate(),
            timeout=deadline - loop.time(),
        )
        await deployment.wait_rollout(kube, timeout=deadline - loop.time())

    async def status(
        self,
        kube: Kube,
        *,
        timeout: float = INFINITY,
        config_hash: str | None = None,
    ) -> BuildKitStatus:
        """Inspect BuildKit daemon readiness without mutating the cluster.

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
        BuildKitStatus
            Read-only BuildKit runtime readiness report.

        Raises
        ------
        TimeoutError
            If `timeout` is non-positive.
        OSError
            If Kubernetes read operations fail or return malformed data.
        """
        if timeout <= 0:
            msg = "BuildKit daemon status timeout must be non-negative"
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
            cache = await BUILDKIT_CACHE.status(kube, timeout=deadline - loop.time())

            expected_port = ServicePortSpec(
                name="grpc",
                port=self.port,
                target_port=self.port,
            )
            service_status = _service_status(
                service,
                service_type="ClusterIP",
                selector=self.selector,
                ports=(expected_port,),
            )
            deployment_status = _deployment_status(deployment, minimum=1)
            installed_hash = deployment_status.pod_annotations.get(
                BUILDKIT_CONFIG_HASH_ANNOTATION,
                "",
            )

            config_current = config_hash is None or installed_hash == config_hash
            return BuildKitStatus(
                namespace=self.namespace,
                name=self.service,
                service_present=service_status.present,
                service_selector_ready=service_status.selector_ready,
                service_port_ready=service_status.port_ready,
                service_ready=service_status.ready,
                deployment_present=deployment_status.present,
                available_replicas=deployment_status.available_replicas,
                updated_replicas=deployment_status.updated_replicas,
                observed_generation=deployment_status.observed_generation,
                generation=deployment_status.generation,
                rollout_ready=deployment_status.rollout_ready,
                expected_config_hash=config_hash,
                installed_config_hash=installed_hash,
                config_current=config_current,
                storage_ready=cache.ready,
                cache=cache,
                ready=(
                    service_status.ready
                    and deployment_status.rollout_ready
                    and config_current
                    and cache.ready
                ),
            )
        except OSError as err:
            msg = (
                f"failed to inspect BuildKit daemon "
                f"{self.namespace}/{self.service}: {err}"
            )
            raise OSError(msg) from err

    async def active_node(self, kube: Kube, *, timeout: float) -> str:
        """Return the node running the active BuildKit daemon pod.

        Parameters
        ----------
        kube : Kube
            Kubernetes API client for the target cluster.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        str
            Kubernetes node name running the active BuildKit pod.

        Raises
        ------
        TimeoutError
            If `timeout` is non-positive.
        OSError
            If no active BuildKit pod exists, more than one active pod exists, or
            the active pod has not been assigned to a node.
        """
        if timeout <= 0:
            msg = "BuildKit active node timeout must be non-negative"
            raise TimeoutError(msg)
        pods = await Pod.list(
            kube,
            timeout=timeout,
            namespaces=(self.namespace,),
            labels=self.selector,
        )
        active = [pod for pod in pods if pod.is_active]
        if len(active) != 1:
            msg = (
                f"expected exactly one active BuildKit pod for {self.namespace}/"
                f"{self.service}, found {len(active)}"
            )
            raise OSError(msg)
        node_name = active[0].node_name
        if not node_name:
            msg = (
                f"active BuildKit pod {active[0].namespace}/{active[0].name} is not "
                "assigned to a node"
            )
            raise OSError(msg)
        return node_name


BUILDKIT = BuildKit(
    namespace=BERTRAND_NAMESPACE,
    service=BUILDKIT_NAME,
    port=BUILDKIT_PORT,
    addr=f"tcp://{BUILDKIT_NAME}.{BERTRAND_NAMESPACE}.svc.cluster.local:{BUILDKIT_PORT}",
)
