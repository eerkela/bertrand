"""Long-lived BuildKit daemon (Deployment + Service) for Bertrand's Kubernetes image
build runtime.
"""

from __future__ import annotations

import asyncio
from dataclasses import dataclass

from ...run import BERTRAND_NAMESPACE, INFINITY
from ..api import Kube
from ..deployment import (
    Container,
    ContainerPort,
    Deployment,
    Probe,
    Volume,
    VolumeMount,
)
from ..service import Service

BUILDKIT_NAME = "bertrand-buildkit"
BUILDKIT_IMAGE = "moby/buildkit:v0.29.0"
BUILDKIT_PORT = 1234
BUILDKIT_LISTEN_ADDR = f"tcp://0.0.0.0:{BUILDKIT_PORT}"
BUILDKIT_ADDR = f"tcp://{BUILDKIT_NAME}.{BERTRAND_NAMESPACE}.svc.cluster.local:{BUILDKIT_PORT}"
BUILDKIT_CACHE_MOUNT = "/var/lib/buildkit"
BUILDKIT_CACHE_VOLUME = "buildkit-state"
BUILDKIT_CONFIG_DIR = "/etc/buildkit"
BUILDKIT_CONFIG_FILE = f"{BUILDKIT_CONFIG_DIR}/buildkitd.toml"
BUILDKIT_CONFIG_KEY = "buildkitd.toml"
BUILDKIT_CONFIG_NAME = f"{BUILDKIT_NAME}-registry"
BUILDKIT_CONFIG_VOLUME = "buildkit-config"
BUILDKIT_LABEL = "bertrand.dev/buildkit"
BUILDKIT_LABEL_VALUE = "v1"


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
        """
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
        """
        Returns
        -------
        dict[str, str]
            Labels used to bind the BuildKit Service to its pods.
        """
        return {
            "app.kubernetes.io/name": self.service,
            BUILDKIT_LABEL: BUILDKIT_LABEL_VALUE,
        }

    async def ensure(self, kube: Kube, *, timeout: float = INFINITY) -> None:
        """Converge Bertrand's bootstrap BuildKit Deployment and Service.

        Parameters
        ----------
        kube : Kube
            Kubernetes API client for the target cluster.
        timeout : float, optional
            Maximum runtime budget in seconds. If infinite, wait indefinitely.

        Raises
        ------
        TimeoutError
            If `timeout` is non-positive, Kubernetes requests exceed the budget, or the
            Deployment does not report at least one available replica before the deadline.
        OSError
            If Kubernetes create/patch/read operations fail or return malformed payloads.
        """
        if timeout <= 0:
            raise TimeoutError("BuildKit daemon timeout must be non-negative")
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout

        await Service.upsert(
            kube,
            namespace=self.namespace,
            name=self.service,
            labels=self.labels,
            selector=self.selector,
            ports=[
                Service.Port(
                    name="grpc",
                    port=self.port,
                    target_port=self.port,
                )
            ],
            timeout=deadline - loop.time(),
        )
        deployment = await Deployment.upsert(
            kube,
            namespace=self.namespace,
            name=self.service,
            labels=self.labels,
            selector=self.selector,
            containers=[
                Container(
                    name="buildkitd",
                    image=BUILDKIT_IMAGE,
                    image_pull_policy="IfNotPresent",
                    command=["/bin/sh", "-ec"],
                    args=[
                        (
                            f"if [ -s {BUILDKIT_CONFIG_FILE!r} ]; then "
                            f"exec buildkitd --addr {BUILDKIT_LISTEN_ADDR!r} "
                            f"--config {BUILDKIT_CONFIG_FILE!r}; "
                            "fi; "
                            f"exec buildkitd --addr {BUILDKIT_LISTEN_ADDR!r}"
                        )
                    ],
                    ports=[
                        ContainerPort(
                            name="grpc",
                            container_port=self.port,
                        )
                    ],
                    security_context={
                        "privileged": True,
                        "runAsUser": 0,
                    },
                    readiness_probe=Probe.tcp(
                        port=self.port,
                        period_seconds=2,
                        failure_threshold=30,
                    ),
                    liveness_probe=Probe.tcp(
                        port=self.port,
                        initial_delay_seconds=10,
                        period_seconds=10,
                        failure_threshold=3,
                    ),
                    volume_mounts=[
                        VolumeMount(
                            name=BUILDKIT_CACHE_VOLUME,
                            mount_path=BUILDKIT_CACHE_MOUNT,
                        ),
                        VolumeMount(
                            name=BUILDKIT_CONFIG_VOLUME,
                            mount_path=BUILDKIT_CONFIG_DIR,
                            read_only=True,
                        ),
                    ],
                )
            ],
            volumes=[
                Volume.empty_dir(BUILDKIT_CACHE_VOLUME),
                Volume.config_map(
                    BUILDKIT_CONFIG_VOLUME,
                    config_map_name=BUILDKIT_CONFIG_NAME,
                    optional=True,
                ),
            ],
            timeout=deadline - loop.time(),
        )
        await deployment.wait_available(kube, timeout=deadline - loop.time())


BUILDKIT = BuildKit(
    namespace=BERTRAND_NAMESPACE,
    service=BUILDKIT_NAME,
    port=BUILDKIT_PORT,
    addr=BUILDKIT_ADDR,
)
