"""Cluster-owned OCI image repository for Bertrand's Kubernetes build runtime."""

from __future__ import annotations

import asyncio
import re
import tempfile
from dataclasses import dataclass
from pathlib import Path

from ...run import (
    BERTRAND_ENV,
    BERTRAND_NAMESPACE,
    INFINITY,
    run,
    sudo,
)
from ..api import (
    ContainerPortSpec,
    ContainerSpec,
    EnvVarSpec,
    Kube,
    ProbeSpec,
    ServicePortSpec,
    VolumeMountSpec,
    VolumeSpec,
)
from ..ceph import CEPHFS_STORAGE_CLASS_PREFERENCES
from ..configmap import ConfigMap
from ..deployment import Deployment
from ..service import Service
from ..volume import PersistentVolumeClaim, StorageClass
from .daemon import (
    BUILDKIT_CONFIG_KEY,
    BUILDKIT_CONFIG_NAME,
)

IMAGE_REPOSITORY_NAME = "bertrand-registry"
IMAGE_REPOSITORY_IMAGE = "registry:2"
IMAGE_REPOSITORY_PORT = 5000
IMAGE_REPOSITORY_NODE_PORT = 32000
IMAGE_REPOSITORY_PULL_HOST = f"localhost:{IMAGE_REPOSITORY_NODE_PORT}"
IMAGE_REPOSITORY_SERVICE_ADDR = (
    f"{IMAGE_REPOSITORY_NAME}.{BERTRAND_NAMESPACE}.svc.cluster.local:{IMAGE_REPOSITORY_PORT}"
)
IMAGE_REPOSITORY_SIZE = "4Gi"
IMAGE_REPOSITORY_MOUNT = "/var/lib/registry"
IMAGE_REPOSITORY_VOLUME = "registry-state"
IMAGE_REPOSITORY_LABEL = "bertrand.dev/image-repository"
IMAGE_REPOSITORY_LABEL_VALUE = "v1"
IMAGE_REF_COMPONENT_RE = re.compile(r"^[a-z0-9]+(?:(?:[._]|__|[-]*)[a-z0-9]+)*$")
IMAGE_TAG_RE = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_.-]{0,127}$")


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
        """
        Returns
        -------
        str
            Logical registry server address used in Kubernetes image references.
        """
        return f"http://{self.pull_host}"

    @property
    def trust_hosts(self) -> tuple[str, ...]:
        """
        Returns
        -------
        tuple[str, ...]
            Registry host aliases that should be trusted by containerd.
        """
        return (self.pull_host, f"127.0.0.1:{self.node_port}")

    @property
    def labels(self) -> dict[str, str]:
        """
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
    def selector(self) -> dict[str, str]:
        """
        Returns
        -------
        dict[str, str]
            Labels used to bind the image repository Service to its pods.
        """
        return {
            "app.kubernetes.io/name": self.service,
            IMAGE_REPOSITORY_LABEL: IMAGE_REPOSITORY_LABEL_VALUE,
        }

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
        OSError
            If the local host trust files cannot be installed.
        """
        if timeout <= 0:
            raise TimeoutError("image repository trust timeout must be non-negative")
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
            raise TimeoutError("image repository timeout must be non-negative")
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout

        storage = await StorageClass.select(
            kube=kube,
            timeout=deadline - loop.time(),
            preferences=CEPHFS_STORAGE_CLASS_PREFERENCES,
            require_expansion=True,
        )
        if not storage.is_cephfs:
            raise OSError(
                f"storage class {storage.name!r} uses provisioner "
                f"{storage.provisioner!r}, but Bertrand registry storage requires "
                "a CephFS CSI provisioner"
            )
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
            raise OSError(
                f"registry PVC {self.namespace}/{self.service} uses storage class "
                f"{pvc.storage_class_name!r}, expected {storage.name!r}"
            )
        if "ReadWriteMany" not in pvc.access_modes:
            raise OSError(f"registry PVC {self.namespace}/{self.service} must use ReadWriteMany")

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
        await ConfigMap.upsert(
            kube,
            namespace=self.namespace,
            name=BUILDKIT_CONFIG_NAME,
            labels=self.labels,
            data={
                BUILDKIT_CONFIG_KEY: (
                    f"[registry.\"{self.pull_host}\"]\n"
                    f"  mirrors = [\"{self.service_addr}\"]\n"
                    "  http = true\n"
                    "  insecure = true\n"
                ),
            },
            timeout=deadline - loop.time(),
        )
        deployment = await Deployment.upsert(
            kube,
            namespace=self.namespace,
            name=self.service,
            labels=self.labels,
            selector=self.selector,
            containers=[
                ContainerSpec(
                    name="registry",
                    image=IMAGE_REPOSITORY_IMAGE,
                    image_pull_policy="IfNotPresent",
                    ports=[
                        ContainerPortSpec(
                            name="registry",
                            container_port=self.port,
                        )
                    ],
                    env=[
                        EnvVarSpec(
                            name="REGISTRY_HTTP_ADDR",
                            value=f"0.0.0.0:{self.port}",
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
                    volume_mounts=[
                        VolumeMountSpec(
                            name=IMAGE_REPOSITORY_VOLUME,
                            mount_path=IMAGE_REPOSITORY_MOUNT,
                        )
                    ],
                )
            ],
            volumes=[
                VolumeSpec.pvc(
                    IMAGE_REPOSITORY_VOLUME,
                    claim_name=self.service,
                )
            ],
            timeout=deadline - loop.time(),
        )
        await deployment.wait_available(kube, timeout=deadline - loop.time())

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
        normalized_tag = tag.strip()
        if not path:
            raise ValueError("image repository path cannot be empty")
        if not normalized_tag:
            raise ValueError("image tag cannot be empty")
        if not IMAGE_TAG_RE.fullmatch(normalized_tag):
            raise ValueError(f"invalid image tag: {tag!r}")
        parts = path.split("/")
        if any(not IMAGE_REF_COMPONENT_RE.fullmatch(part) for part in parts):
            raise ValueError(f"invalid image repository path: {name!r}")
        return f"{self.pull_host}/bertrand/{path}:{normalized_tag}"


IMAGES = ImageRepository(
    namespace=BERTRAND_NAMESPACE,
    service=IMAGE_REPOSITORY_NAME,
    port=IMAGE_REPOSITORY_PORT,
    node_port=IMAGE_REPOSITORY_NODE_PORT,
    pull_host=IMAGE_REPOSITORY_PULL_HOST,
    service_addr=IMAGE_REPOSITORY_SERVICE_ADDR,
    storage_request=IMAGE_REPOSITORY_SIZE,
)
