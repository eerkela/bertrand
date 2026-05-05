"""Long-lived BuildKit daemon (Deployment + Service) for Bertrand's Kubernetes image
build runtime.
"""
from __future__ import annotations

import asyncio
from dataclasses import dataclass

import kubernetes

from ...run import BERTRAND_NAMESPACE, INFINITY
from ..api import Kube

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
BUILDKIT_WAIT_INTERVAL = 0.5


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

    def _service_manifest(self) -> dict[str, object]:
        return {
            "apiVersion": "v1",
            "kind": "Service",
            "metadata": {
                "name": self.service,
                "namespace": self.namespace,
                "labels": {
                    "app.kubernetes.io/name": self.service,
                    "app.kubernetes.io/part-of": "bertrand",
                    BUILDKIT_LABEL: BUILDKIT_LABEL_VALUE,
                },
            },
            "spec": {
                "type": "ClusterIP",
                "selector": {
                    "app.kubernetes.io/name": self.service,
                    BUILDKIT_LABEL: BUILDKIT_LABEL_VALUE,
                },
                "ports": [
                    {
                        "name": "grpc",
                        "port": self.port,
                        "targetPort": self.port,
                        "protocol": "TCP",
                    }
                ],
            },
        }

    def _deployment_manifest(self) -> dict[str, object]:
        labels = {
            "app.kubernetes.io/name": self.service,
            "app.kubernetes.io/part-of": "bertrand",
            BUILDKIT_LABEL: BUILDKIT_LABEL_VALUE,
        }
        return {
            "apiVersion": "apps/v1",
            "kind": "Deployment",
            "metadata": {
                "name": self.service,
                "namespace": self.namespace,
                "labels": labels,
            },
            "spec": {
                "replicas": 1,
                "selector": {
                    "matchLabels": {
                        "app.kubernetes.io/name": self.service,
                        BUILDKIT_LABEL: BUILDKIT_LABEL_VALUE,
                    }
                },
                "template": {
                    "metadata": {"labels": labels},
                    "spec": {
                        "automountServiceAccountToken": False,
                        "containers": [
                            {
                                "name": "buildkitd",
                                "image": BUILDKIT_IMAGE,
                                "imagePullPolicy": "IfNotPresent",
                                "command": ["/bin/sh", "-ec"],
                                "args": [
                                    (
                                        f"if [ -s {BUILDKIT_CONFIG_FILE!r} ]; then "
                                        f"exec buildkitd --addr {BUILDKIT_LISTEN_ADDR!r} "
                                        f"--config {BUILDKIT_CONFIG_FILE!r}; "
                                        "fi; "
                                        f"exec buildkitd --addr {BUILDKIT_LISTEN_ADDR!r}"
                                    )
                                ],
                                "ports": [
                                    {
                                        "name": "grpc",
                                        "containerPort": self.port,
                                        "protocol": "TCP",
                                    }
                                ],
                                "securityContext": {
                                    "privileged": True,
                                    "runAsUser": 0,
                                },
                                "readinessProbe": {
                                    "tcpSocket": {"port": self.port},
                                    "periodSeconds": 2,
                                    "failureThreshold": 30,
                                },
                                "livenessProbe": {
                                    "tcpSocket": {"port": self.port},
                                    "initialDelaySeconds": 10,
                                    "periodSeconds": 10,
                                    "failureThreshold": 3,
                                },
                                "volumeMounts": [
                                    {
                                        "name": BUILDKIT_CACHE_VOLUME,
                                        "mountPath": BUILDKIT_CACHE_MOUNT,
                                    },
                                    {
                                        "name": BUILDKIT_CONFIG_VOLUME,
                                        "mountPath": BUILDKIT_CONFIG_DIR,
                                        "readOnly": True,
                                    },
                                ],
                            }
                        ],
                        "volumes": [
                            {
                                "name": BUILDKIT_CACHE_VOLUME,
                                "emptyDir": {},
                            },
                            {
                                "name": BUILDKIT_CONFIG_VOLUME,
                                "configMap": {
                                    "name": BUILDKIT_CONFIG_NAME,
                                    "optional": True,
                                },
                            },
                        ],
                    },
                },
            },
        }

    async def _upsert_service(
        self,
        kube: Kube,
        *,
        timeout: float,
    ) -> kubernetes.client.V1Service:
        manifest = self._service_manifest()

        # try to create the service if it did not previously exist
        try:
            created = await kube.run(
                lambda t: kube.core.create_namespaced_service(
                    namespace=self.namespace,
                    body=manifest,
                    _request_timeout=t,
                ),
                timeout=timeout,
                context=f"failed to create BuildKit service {self.service!r}",
            )
            if not isinstance(created, kubernetes.client.V1Service):
                raise OSError("malformed Kubernetes Service payload during BuildKit create")
            return created
        except OSError as err:
            detail = str(err).lower()
            if not ("status 409" in detail or "already exists" in detail):
                raise

        # if the service already exists, patch it to ensure the spec is correct
        patched = await kube.run(
            lambda t: kube.core.patch_namespaced_service(
                name=self.service,
                namespace=self.namespace,
                body=manifest,
                _request_timeout=t,
            ),
            timeout=timeout,
            context=f"failed to patch BuildKit service {self.service!r}",
        )
        if not isinstance(patched, kubernetes.client.V1Service):
            raise OSError("malformed Kubernetes Service payload during BuildKit patch")
        return patched

    async def _upsert_deployment(
        self,
        kube: Kube,
        *,
        timeout: float,
    ) -> kubernetes.client.V1Deployment:
        manifest = self._deployment_manifest()

        # try to create the deployment if it did not previously exist
        try:
            created = await kube.run(
                lambda request_timeout: kube.apps.create_namespaced_deployment(
                    namespace=self.namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=f"failed to create BuildKit deployment {self.service!r}",
            )
            if not isinstance(created, kubernetes.client.V1Deployment):
                raise OSError("malformed Kubernetes Deployment payload during BuildKit create")
            return created
        except OSError as err:
            detail = str(err).lower()
            if not ("status 409" in detail or "already exists" in detail):
                raise

        # if the deployment already exists, patch it to ensure the spec is correct
        patched = await kube.run(
            lambda request_timeout: kube.apps.patch_namespaced_deployment(
                name=self.service,
                namespace=self.namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to patch BuildKit deployment {self.service!r}",
        )
        if not isinstance(patched, kubernetes.client.V1Deployment):
            raise OSError("malformed Kubernetes Deployment payload during BuildKit patch")
        return patched

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

        await self._upsert_service(kube, timeout=deadline - loop.time())
        await self._upsert_deployment(kube, timeout=deadline - loop.time())

        # wait until the deployment becomes available
        timestamp = loop.time()
        while True:
            remaining = deadline - timestamp
            if remaining <= 0:
                raise TimeoutError(
                    "timed out waiting for BuildKit deployment "
                    f"{self.namespace}/{self.service} to become available"
                )

            # check for deployment availability
            deployment = await kube.run(
                lambda t: kube.apps.read_namespaced_deployment(
                    name=self.service,
                    namespace=self.namespace,
                    _request_timeout=t,
                ),
                timeout=remaining,
                context=f"failed to read BuildKit deployment {self.service!r}",
            )
            if deployment is not None:
                if not isinstance(deployment, kubernetes.client.V1Deployment):
                    raise OSError("malformed Kubernetes Deployment payload during BuildKit read")
                if (
                    deployment.status is not None and
                    (deployment.status.available_replicas or 0) >= 1
                ):
                    return

            timestamp = loop.time()
            await asyncio.sleep(min(BUILDKIT_WAIT_INTERVAL, deadline - timestamp))


BUILDKIT = BuildKit(
    namespace=BERTRAND_NAMESPACE,
    service=BUILDKIT_NAME,
    port=BUILDKIT_PORT,
    addr=BUILDKIT_ADDR,
)
