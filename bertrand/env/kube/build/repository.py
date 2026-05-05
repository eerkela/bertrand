"""Cluster-owned OCI image repository for Bertrand's Kubernetes build runtime."""

from __future__ import annotations

import asyncio
import re
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING

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
from .daemon import (
    BUILDKIT_CONFIG_KEY,
    BUILDKIT_CONFIG_NAME,
)

if TYPE_CHECKING:
    from ..volume import PersistentVolumeClaim

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
IMAGE_REPOSITORY_WAIT_INTERVAL = 0.5
IMAGE_REPOSITORY_STORAGE_CLASS_PREFERENCES: tuple[str, ...] = ("cephfs", "rook-cephfs")
IMAGE_REF_COMPONENT_RE = re.compile(r"^[a-z0-9]+(?:(?:[._]|__|[-]*)[a-z0-9]+)*$")
IMAGE_TAG_RE = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_.-]{0,127}$")


def _remaining(deadline: float) -> float:
    remaining = deadline - asyncio.get_running_loop().time()
    if remaining <= 0:
        raise TimeoutError("timed out while converging image repository")
    return remaining


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
        """Return the HTTP URL for the stable node-local pull endpoint."""
        return f"http://{self.pull_host}"

    @property
    def trust_hosts(self) -> tuple[str, ...]:
        """Return registry host aliases that should be trusted by containerd."""
        return (
            self.pull_host,
            f"127.0.0.1:{self.node_port}",
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

    def _labels(self) -> dict[str, str]:
        return {
            "app.kubernetes.io/name": self.service,
            "app.kubernetes.io/part-of": "bertrand",
            BERTRAND_ENV: "1",
            IMAGE_REPOSITORY_LABEL: IMAGE_REPOSITORY_LABEL_VALUE,
        }

    def _pvc_manifest(self, *, storage_class: str) -> dict[str, JSONValue]:
        return {
            "apiVersion": "v1",
            "kind": "PersistentVolumeClaim",
            "metadata": {
                "name": self.service,
                "namespace": self.namespace,
                "labels": self._labels(),
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

    def _service_manifest(self) -> dict[str, JSONValue]:
        labels = self._labels()
        selector = {
            "app.kubernetes.io/name": self.service,
            IMAGE_REPOSITORY_LABEL: IMAGE_REPOSITORY_LABEL_VALUE,
        }
        return {
            "apiVersion": "v1",
            "kind": "Service",
            "metadata": {
                "name": self.service,
                "namespace": self.namespace,
                "labels": labels,
            },
            "spec": {
                "type": "NodePort",
                "selector": selector,
                "ports": [
                    {
                        "name": "registry",
                        "port": self.port,
                        "targetPort": self.port,
                        "nodePort": self.node_port,
                        "protocol": "TCP",
                    }
                ],
            },
        }

    def _deployment_manifest(self) -> dict[str, JSONValue]:
        labels = self._labels()
        selector = {
            "app.kubernetes.io/name": self.service,
            IMAGE_REPOSITORY_LABEL: IMAGE_REPOSITORY_LABEL_VALUE,
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
                "selector": {"matchLabels": selector},
                "template": {
                    "metadata": {"labels": labels},
                    "spec": {
                        "automountServiceAccountToken": False,
                        "containers": [
                            {
                                "name": "registry",
                                "image": IMAGE_REPOSITORY_IMAGE,
                                "imagePullPolicy": "IfNotPresent",
                                "ports": [
                                    {
                                        "name": "registry",
                                        "containerPort": self.port,
                                        "protocol": "TCP",
                                    }
                                ],
                                "env": [
                                    {
                                        "name": "REGISTRY_HTTP_ADDR",
                                        "value": f"0.0.0.0:{self.port}",
                                    }
                                ],
                                "readinessProbe": {
                                    "httpGet": {
                                        "path": "/v2/",
                                        "port": self.port,
                                    },
                                    "periodSeconds": 2,
                                    "failureThreshold": 30,
                                },
                                "livenessProbe": {
                                    "httpGet": {
                                        "path": "/v2/",
                                        "port": self.port,
                                    },
                                    "initialDelaySeconds": 10,
                                    "periodSeconds": 10,
                                    "failureThreshold": 3,
                                },
                                "volumeMounts": [
                                    {
                                        "name": IMAGE_REPOSITORY_VOLUME,
                                        "mountPath": IMAGE_REPOSITORY_MOUNT,
                                    }
                                ],
                            }
                        ],
                        "volumes": [
                            {
                                "name": IMAGE_REPOSITORY_VOLUME,
                                "persistentVolumeClaim": {
                                    "claimName": self.service,
                                },
                            }
                        ],
                    },
                },
            },
        }

    def _buildkit_config(self) -> str:
        return (
            f"[registry.\"{self.pull_host}\"]\n"
            f"  mirrors = [\"{self.service_addr}\"]\n"
            "  http = true\n"
            "  insecure = true\n"
        )

    def _buildkit_config_manifest(self) -> dict[str, JSONValue]:
        return {
            "apiVersion": "v1",
            "kind": "ConfigMap",
            "metadata": {
                "name": BUILDKIT_CONFIG_NAME,
                "namespace": self.namespace,
                "labels": self._labels(),
            },
            "data": {
                BUILDKIT_CONFIG_KEY: self._buildkit_config(),
            },
        }

    async def _select_storage_class(self, kube: Kube, *, timeout: float) -> str:
        from ..volume import StorageClass

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
            return candidate

        preferred = ", ".join(repr(name) for name in IMAGE_REPOSITORY_STORAGE_CLASS_PREFERENCES)
        raise OSError(
            "required CephFS storage class is not available for registry storage; "
            f"expected one of {preferred}.  Ensure MicroK8s is linked to MicroCeph."
        )

    async def _upsert_pvc(self, kube: Kube, *, timeout: float) -> PersistentVolumeClaim:
        from ..volume import PersistentVolumeClaim

        storage_class = await self._select_storage_class(kube, timeout=timeout)
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
        await pvc.grow(self.storage_request, kube=kube, timeout=timeout)
        live = await pvc.refresh(kube=kube, timeout=timeout)
        if live is None:
            raise OSError(f"registry PVC {self.namespace}/{self.service} disappeared")
        return live

    async def _upsert_service(
        self,
        kube: Kube,
        *,
        timeout: float,
    ) -> kubernetes.client.V1Service:
        manifest = self._service_manifest()
        try:
            created = await kube.run(
                lambda request_timeout: kube.core.create_namespaced_service(
                    namespace=self.namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=f"failed to create image repository service {self.service!r}",
            )
            if not isinstance(created, kubernetes.client.V1Service):
                raise OSError("malformed Kubernetes Service payload during registry create")
            return created
        except OSError as err:
            detail = str(err).lower()
            if not ("status 409" in detail or "already exists" in detail):
                raise

        patched = await kube.run(
            lambda request_timeout: kube.core.patch_namespaced_service(
                name=self.service,
                namespace=self.namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to patch image repository service {self.service!r}",
        )
        if not isinstance(patched, kubernetes.client.V1Service):
            raise OSError("malformed Kubernetes Service payload during registry patch")
        return patched

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

    async def _upsert_deployment(
        self,
        kube: Kube,
        *,
        timeout: float,
    ) -> kubernetes.client.V1Deployment:
        manifest = self._deployment_manifest()
        try:
            created = await kube.run(
                lambda request_timeout: kube.apps.create_namespaced_deployment(
                    namespace=self.namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=f"failed to create image repository deployment {self.service!r}",
            )
            if not isinstance(created, kubernetes.client.V1Deployment):
                raise OSError("malformed Kubernetes Deployment payload during registry create")
            return created
        except OSError as err:
            detail = str(err).lower()
            if not ("status 409" in detail or "already exists" in detail):
                raise

        patched = await kube.run(
            lambda request_timeout: kube.apps.patch_namespaced_deployment(
                name=self.service,
                namespace=self.namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to patch image repository deployment {self.service!r}",
        )
        if not isinstance(patched, kubernetes.client.V1Deployment):
            raise OSError("malformed Kubernetes Deployment payload during registry patch")
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

        await self._upsert_pvc(kube, timeout=_remaining(deadline))
        await self._upsert_service(kube, timeout=_remaining(deadline))
        await self._upsert_buildkit_config(kube, timeout=_remaining(deadline))
        await self._upsert_deployment(kube, timeout=_remaining(deadline))

        while True:
            remaining = _remaining(deadline)
            deployment = await kube.run(
                lambda request_timeout: kube.apps.read_namespaced_deployment(
                    name=self.service,
                    namespace=self.namespace,
                    _request_timeout=request_timeout,
                ),
                timeout=remaining,
                context=f"failed to read image repository deployment {self.service!r}",
            )
            if deployment is not None:
                if not isinstance(deployment, kubernetes.client.V1Deployment):
                    raise OSError("malformed Kubernetes Deployment payload during registry read")
                if (
                    deployment.status is not None
                    and (deployment.status.available_replicas or 0) >= 1
                ):
                    return
            await asyncio.sleep(min(IMAGE_REPOSITORY_WAIT_INTERVAL, _remaining(deadline)))

    async def ensure_node_trust(self, *, timeout: float = INFINITY) -> None:
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
                timeout=_remaining(deadline),
            )
            with tempfile.NamedTemporaryFile("w", encoding="utf-8", delete=False) as handle:
                handle.write(content)
                temp_path = Path(handle.name)
            try:
                await run(
                    sudo(["install", "-m", "0644", str(temp_path), str(trust_file)]),
                    capture_output=True,
                    timeout=_remaining(deadline),
                )
            finally:
                temp_path.unlink(missing_ok=True)


IMAGES = ImageRepository(
    namespace=BERTRAND_NAMESPACE,
    service=IMAGE_REPOSITORY_NAME,
    port=IMAGE_REPOSITORY_PORT,
    node_port=IMAGE_REPOSITORY_NODE_PORT,
    pull_host=IMAGE_REPOSITORY_PULL_HOST,
    service_addr=IMAGE_REPOSITORY_SERVICE_ADDR,
    storage_request=IMAGE_REPOSITORY_SIZE,
)
