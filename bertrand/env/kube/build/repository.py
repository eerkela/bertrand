"""Cluster-owned OCI image repository for Bertrand's Kubernetes build runtime."""

from __future__ import annotations

import asyncio
import re
import tempfile
from dataclasses import dataclass
from pathlib import Path

import kubernetes

from ...run import (
    BERTRAND_ENV,
    BERTRAND_NAMESPACE,
    INFINITY,
    JSONValue,
    run,
    sudo,
)
from ..api import Kube
from ..deployment import (
    Container,
    ContainerPort,
    Deployment,
    EnvVar,
    Probe,
    Volume,
    VolumeMount,
)
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
IMAGE_REPOSITORY_STORAGE_CLASS_PREFERENCES: tuple[str, ...] = ("cephfs", "rook-cephfs")
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

    def _pvc_manifest(self, *, storage_class: str) -> dict[str, JSONValue]:
        return {
            "apiVersion": "v1",
            "kind": "PersistentVolumeClaim",
            "metadata": {
                "name": self.service,
                "namespace": self.namespace,
                "labels": self.labels,
            },
            "spec": {
                "accessModes": ["ReadWriteMany"],
                "storageClassName": storage_class,
                "resources": {
                    "requests": {
                        "storage": self.storage_request,
                    },
                },
            },
        }

    def _buildkit_config_manifest(self) -> dict[str, JSONValue]:
        return {
            "apiVersion": "v1",
            "kind": "ConfigMap",
            "metadata": {
                "name": BUILDKIT_CONFIG_NAME,
                "namespace": self.namespace,
                "labels": self.labels,
            },
            "data": {
                BUILDKIT_CONFIG_KEY: (
                    f"[registry.\"{self.pull_host}\"]\n"
                    f"  mirrors = [\"{self.service_addr}\"]\n"
                    "  http = true\n"
                    "  insecure = true\n"
                ),
            },
        }

    async def _upsert_pvc(self, kube: Kube, *, timeout: float) -> PersistentVolumeClaim:
        # select storage class for the registry PVC
        storage_class: str | None = None
        for candidate in IMAGE_REPOSITORY_STORAGE_CLASS_PREFERENCES:
            storage = await StorageClass.get(
                kube=kube,
                timeout=timeout,
                name=candidate,
            )
            if storage is None:
                continue
            if not storage.allow_volume_expansion:
                raise OSError(
                    f"storage class {candidate!r} must set "
                    "allowVolumeExpansion=true for registry storage"
                )
            provisioner = storage.provisioner.lower()
            if "cephfs" not in provisioner or "csi.ceph.com" not in provisioner:
                raise OSError(
                    f"storage class {candidate!r} uses provisioner "
                    f"{storage.provisioner!r}, but Bertrand registry storage "
                    "requires a CephFS CSI provisioner"
                )
            storage_class = candidate
            break
        if storage_class is None:
            preferred = ", ".join(repr(name) for name in IMAGE_REPOSITORY_STORAGE_CLASS_PREFERENCES)
            raise OSError(
                "required CephFS storage class is not available for registry storage; "
                f"expected one of {preferred}.  Ensure MicroK8s is linked to MicroCeph."
            )

        # create or update the registry PVC
        manifest = self._pvc_manifest(storage_class=storage_class)
        pvc = await PersistentVolumeClaim.get(
            kube=kube,
            namespace=self.namespace,
            timeout=timeout,
            name=self.service,
        )
        if pvc is None:
            pvc = await PersistentVolumeClaim.create(
                manifest,
                kube=kube,
                timeout=timeout,
            )
        if pvc.storage_class_name != storage_class:
            raise OSError(
                f"registry PVC {self.namespace}/{self.service} uses storage class "
                f"{pvc.storage_class_name!r}, expected {storage_class!r}"
            )
        if "ReadWriteMany" not in pvc.access_modes:
            raise OSError(f"registry PVC {self.namespace}/{self.service} must use ReadWriteMany")

        # wait for the PVC to be bound and resized if necessary
        await pvc.grow(self.storage_request, kube=kube, timeout=timeout)
        live = await pvc.refresh(kube=kube, timeout=timeout)
        if live is None:
            raise OSError(f"registry PVC {self.namespace}/{self.service} disappeared")
        return live

    async def _upsert_buildkit_config(
        self,
        kube: Kube,
        *,
        timeout: float,
    ) -> kubernetes.client.V1ConfigMap:
        manifest = self._buildkit_config_manifest()
        try:
            created = await kube.run(
                lambda request_timeout: kube.core.create_namespaced_config_map(
                    namespace=self.namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=f"failed to create BuildKit registry config {BUILDKIT_CONFIG_NAME!r}",
            )
            if not isinstance(created, kubernetes.client.V1ConfigMap):
                raise OSError("malformed Kubernetes ConfigMap payload during registry create")
            return created
        except OSError as err:
            detail = str(err).lower()
            if not ("status 409" in detail or "already exists" in detail):
                raise

        patched = await kube.run(
            lambda request_timeout: kube.core.patch_namespaced_config_map(
                name=BUILDKIT_CONFIG_NAME,
                namespace=self.namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to patch BuildKit registry config {BUILDKIT_CONFIG_NAME!r}",
        )
        if not isinstance(patched, kubernetes.client.V1ConfigMap):
            raise OSError("malformed Kubernetes ConfigMap payload during registry patch")
        return patched

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

        await self._upsert_pvc(kube, timeout=deadline - loop.time())
        await Service.upsert(
            kube,
            namespace=self.namespace,
            name=self.service,
            labels=self.labels,
            selector=self.selector,
            service_type="NodePort",
            ports=[
                Service.Port(
                    name="registry",
                    port=self.port,
                    target_port=self.port,
                    node_port=self.node_port,
                )
            ],
            timeout=deadline - loop.time(),
        )
        await self._upsert_buildkit_config(kube, timeout=deadline - loop.time())
        deployment = await Deployment.upsert(
            kube,
            namespace=self.namespace,
            name=self.service,
            labels=self.labels,
            selector=self.selector,
            containers=[
                Container(
                    name="registry",
                    image=IMAGE_REPOSITORY_IMAGE,
                    image_pull_policy="IfNotPresent",
                    ports=[
                        ContainerPort(
                            name="registry",
                            container_port=self.port,
                        )
                    ],
                    env=[
                        EnvVar(
                            name="REGISTRY_HTTP_ADDR",
                            value=f"0.0.0.0:{self.port}",
                        )
                    ],
                    readiness_probe=Probe.http(
                        path="/v2/",
                        port=self.port,
                        period_seconds=2,
                        failure_threshold=30,
                    ),
                    liveness_probe=Probe.http(
                        path="/v2/",
                        port=self.port,
                        initial_delay_seconds=10,
                        period_seconds=10,
                        failure_threshold=3,
                    ),
                    volume_mounts=[
                        VolumeMount(
                            name=IMAGE_REPOSITORY_VOLUME,
                            mount_path=IMAGE_REPOSITORY_MOUNT,
                        )
                    ],
                )
            ],
            volumes=[
                Volume.pvc(
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
