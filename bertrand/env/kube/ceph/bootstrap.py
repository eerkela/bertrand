"""Rook-managed Ceph bootstrap helpers for Bertrand storage."""

from __future__ import annotations

import asyncio
import json
from typing import TYPE_CHECKING

from bertrand.env.git import BERTRAND_ENV, until
from bertrand.env.kube.api.bootstrap import kubectl
from bertrand.env.kube.ceph.csi import CSI_DRIVER_NAME
from bertrand.env.kube.crd import CustomResourceDefinition
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.namespace import Namespace
from bertrand.env.kube.volume import StorageClass

if TYPE_CHECKING:
    from collections.abc import Mapping

    from bertrand.env.kube.api.client import Kube

ROOK_VERSION = "v1.17.9"
CEPH_IMAGE = "quay.io/ceph/ceph:v19.2.3"

ROOK_NAMESPACE = "rook-ceph"
ROOK_OPERATOR_DEPLOYMENT = "rook-ceph-operator"
ROOK_TOOLBOX_DEPLOYMENT = "rook-ceph-tools"
ROOK_CLUSTER_NAME = "bertrand-ceph"
ROOK_CEPHFS_NAME = "ceph"
ROOK_CEPHFS_DATA_POOL = "cephfs-data0"
ROOK_CEPHFS_METADATA_POOL = "cephfs-metadata"

ROOK_OSD_STORAGE_CLASS = "bertrand-osd-csi"
ROOK_CEPHFS_STORAGE_CLASS = "cephfs"
ROOK_CEPHFS_FALLBACK_STORAGE_CLASS = "rook-cephfs"

ROOK_INSTALL_BASE = (
    f"https://raw.githubusercontent.com/rook/rook/{ROOK_VERSION}/deploy/examples"
)
ROOK_CRDS_URL = f"{ROOK_INSTALL_BASE}/crds.yaml"
ROOK_COMMON_URL = f"{ROOK_INSTALL_BASE}/common.yaml"
ROOK_OPERATOR_URL = f"{ROOK_INSTALL_BASE}/operator.yaml"
ROOK_TOOLBOX_URL = f"{ROOK_INSTALL_BASE}/toolbox.yaml"
ROOK_BACKEND_POLL_SECONDS = 0.5
ROOK_MANAGED_LABEL = "bertrand.dev/rook-ceph"
ROOK_MANAGED_VALUE = "v1"
ROOK_LABELS = {
    BERTRAND_ENV: "1",
    ROOK_MANAGED_LABEL: ROOK_MANAGED_VALUE,
}


async def ensure_rook_ceph_base(kube: Kube, *, timeout: float) -> None:
    """Install Bertrand's Rook operator substrate without waiting for Ceph health.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum convergence budget in seconds.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive or convergence exceeds the budget.
    """
    if timeout <= 0:
        msg = "Rook Ceph base convergence timeout must be positive"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    await _ensure_namespace_owned(kube, ROOK_NAMESPACE, timeout=deadline - loop.time())
    await _apply_urls(
        ROOK_CRDS_URL,
        ROOK_COMMON_URL,
        ROOK_OPERATOR_URL,
        timeout=deadline - loop.time(),
    )
    await _wait_crd_established(
        kube,
        "cephclusters.ceph.rook.io",
        timeout=deadline - loop.time(),
    )
    await _wait_crd_established(
        kube,
        "cephfilesystems.ceph.rook.io",
        timeout=deadline - loop.time(),
    )
    await _wait_deployment(
        kube,
        namespace=ROOK_NAMESPACE,
        name=ROOK_OPERATOR_DEPLOYMENT,
        timeout=deadline - loop.time(),
    )
    await _ensure_rook_storage_classes(timeout=deadline - loop.time())
    await _ensure_rook_cluster(timeout=deadline - loop.time())
    await _apply_urls(ROOK_TOOLBOX_URL, timeout=deadline - loop.time())


async def wait_rook_ceph_ready(kube: Kube, *, timeout: float) -> None:
    """Wait until the Rook-managed Ceph cluster and storage classes are ready.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive or readiness exceeds the budget.
    """
    if timeout <= 0:
        msg = "Rook Ceph readiness timeout must be positive"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    await _wait_ceph_cluster_ready(kube, timeout=deadline - loop.time())
    await _wait_deployment(
        kube,
        namespace=ROOK_NAMESPACE,
        name=ROOK_TOOLBOX_DEPLOYMENT,
        timeout=deadline - loop.time(),
    )
    await _wait_storage_classes(kube, timeout=deadline - loop.time())


async def rook_ceph_ready(kube: Kube, *, timeout: float) -> bool:
    """Return whether the Rook-managed Ceph substrate appears ready.

    Returns
    -------
    bool
        True when Rook and Bertrand storage classes can be observed.
    """
    try:
        await _wait_deployment(
            kube,
            namespace=ROOK_NAMESPACE,
            name=ROOK_OPERATOR_DEPLOYMENT,
            timeout=timeout,
        )
        await _wait_ceph_cluster_ready(kube, timeout=timeout)
        await _wait_storage_classes(kube, timeout=timeout)
    except (OSError, TimeoutError, ValueError):
        return False
    return True


async def _ensure_namespace_owned(kube: Kube, name: str, *, timeout: float) -> None:
    current = await Namespace.get(kube, name=name, timeout=timeout)
    if current is not None:
        labels = current.labels
        if labels.get(ROOK_MANAGED_LABEL) != ROOK_MANAGED_VALUE:
            msg = (
                f"namespace {name!r} already exists and is not labelled as "
                "Bertrand-managed; refusing to mutate a shared Rook install"
            )
            raise OSError(msg)
    await Namespace.upsert(kube, name=name, labels=ROOK_LABELS, timeout=timeout)


async def _apply_urls(*urls: str, timeout: float) -> None:
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    for url in urls:
        try:
            await kubectl(
                ["apply", "--server-side", "-f", url],
                capture_output=True,
                timeout=deadline - loop.time(),
            )
        except OSError as err:
            msg = f"failed to apply storage backend manifest {url!r}"
            raise OSError(msg) from err


async def _kubectl_apply_manifest(
    manifest: Mapping[str, object],
    *,
    timeout: float,
) -> None:
    await kubectl(
        ["apply", "--server-side", "-f", "-"],
        stdin=json.dumps(manifest, separators=(",", ":")),
        capture_output=True,
        timeout=timeout,
    )


async def _wait_crd_established(
    kube: Kube,
    name: str,
    *,
    timeout: float,
) -> CustomResourceDefinition:
    async def established(remaining: float) -> CustomResourceDefinition:
        crd = await CustomResourceDefinition.get(kube, name=name, timeout=remaining)
        if crd is None:
            msg = f"storage backend CRD {name!r} is not installed yet"
            raise TimeoutError(msg)
        if not crd.is_established:
            msg = f"storage backend CRD {name!r} is not Established yet"
            raise TimeoutError(msg)
        return crd

    return await until(
        established,
        timeout=timeout,
        interval=ROOK_BACKEND_POLL_SECONDS,
        action=f"waiting for storage backend CRD {name!r}",
    )


async def _wait_deployment(
    kube: Kube,
    *,
    namespace: str,
    name: str,
    timeout: float,
) -> Deployment:
    async def available(remaining: float) -> Deployment:
        deployment = await Deployment.get(
            kube,
            namespace=namespace,
            name=name,
            timeout=remaining,
        )
        if deployment is None:
            msg = f"Deployment {namespace}/{name} is not created yet"
            raise TimeoutError(msg)
        if not deployment.has_available_replicas():
            msg = f"Deployment {namespace}/{name} is not Available yet"
            raise TimeoutError(msg)
        return deployment

    return await until(
        available,
        timeout=timeout,
        interval=ROOK_BACKEND_POLL_SECONDS,
        action=f"waiting for Deployment {namespace}/{name}",
    )


async def _ensure_rook_storage_classes(*, timeout: float) -> None:
    manifest = {
        "apiVersion": "v1",
        "kind": "List",
        "items": [
            {
                "apiVersion": "storage.k8s.io/v1",
                "kind": "StorageClass",
                "metadata": {
                    "name": ROOK_OSD_STORAGE_CLASS,
                    "labels": ROOK_LABELS,
                },
                "provisioner": CSI_DRIVER_NAME,
                "allowVolumeExpansion": True,
                "volumeBindingMode": "WaitForFirstConsumer",
                "reclaimPolicy": "Delete",
            },
            {
                "apiVersion": "storage.k8s.io/v1",
                "kind": "StorageClass",
                "metadata": {
                    "name": ROOK_CEPHFS_STORAGE_CLASS,
                    "labels": ROOK_LABELS,
                },
                "provisioner": f"{ROOK_NAMESPACE}.cephfs.csi.ceph.com",
                "allowVolumeExpansion": True,
                "reclaimPolicy": "Retain",
                "parameters": {
                    "clusterID": ROOK_NAMESPACE,
                    "fsName": ROOK_CEPHFS_NAME,
                    "pool": ROOK_CEPHFS_DATA_POOL,
                    "csi.storage.k8s.io/provisioner-secret-name": (
                        "rook-csi-cephfs-provisioner"
                    ),
                    "csi.storage.k8s.io/provisioner-secret-namespace": (ROOK_NAMESPACE),
                    "csi.storage.k8s.io/controller-expand-secret-name": (
                        "rook-csi-cephfs-provisioner"
                    ),
                    "csi.storage.k8s.io/controller-expand-secret-namespace": (
                        ROOK_NAMESPACE
                    ),
                    "csi.storage.k8s.io/node-stage-secret-name": (
                        "rook-csi-cephfs-node"
                    ),
                    "csi.storage.k8s.io/node-stage-secret-namespace": ROOK_NAMESPACE,
                },
            },
        ],
    }
    await _kubectl_apply_manifest(manifest, timeout=timeout)


async def _ensure_rook_cluster(*, timeout: float) -> None:
    manifest = {
        "apiVersion": "v1",
        "kind": "List",
        "items": [
            {
                "apiVersion": "ceph.rook.io/v1",
                "kind": "CephCluster",
                "metadata": {
                    "name": ROOK_CLUSTER_NAME,
                    "namespace": ROOK_NAMESPACE,
                    "labels": ROOK_LABELS,
                },
                "spec": {
                    "cephVersion": {"image": CEPH_IMAGE},
                    "dataDirHostPath": "/var/lib/rook",
                    "mon": {"count": 1, "allowMultiplePerNode": True},
                    "mgr": {"count": 1, "allowMultiplePerNode": True},
                    "dashboard": {"enabled": False},
                    "crashCollector": {"disable": False},
                    "storage": {
                        "useAllNodes": False,
                        "useAllDevices": False,
                        "allowOsdCrushWeightUpdate": True,
                        "storageClassDeviceSets": [],
                    },
                },
            },
            {
                "apiVersion": "ceph.rook.io/v1",
                "kind": "CephFilesystem",
                "metadata": {
                    "name": ROOK_CEPHFS_NAME,
                    "namespace": ROOK_NAMESPACE,
                    "labels": ROOK_LABELS,
                },
                "spec": {
                    "metadataPool": {"replicated": {"size": 1}},
                    "dataPools": [
                        {
                            "name": "data0",
                            "replicated": {"size": 1},
                        }
                    ],
                    "metadataServer": {
                        "activeCount": 1,
                        "activeStandby": False,
                    },
                },
            },
        ],
    }
    await _kubectl_apply_manifest(manifest, timeout=timeout)


async def _wait_ceph_cluster_ready(kube: Kube, *, timeout: float) -> None:
    async def ready(remaining: float) -> None:
        obj = await kube.run(
            lambda request_timeout: kube.custom.get_namespaced_custom_object(
                group="ceph.rook.io",
                version="v1",
                namespace=ROOK_NAMESPACE,
                plural="cephclusters",
                name=ROOK_CLUSTER_NAME,
                _request_timeout=request_timeout,
            ),
            timeout=remaining,
            context=f"failed to read Rook CephCluster {ROOK_NAMESPACE}/"
            f"{ROOK_CLUSTER_NAME}",
        )
        if not isinstance(obj, dict):
            msg = "Rook CephCluster payload is malformed"
            raise TimeoutError(msg)
        status = obj.get("status")
        phase = ""
        health = ""
        if isinstance(status, dict):
            phase = str(status.get("phase") or "").strip()
            ceph = status.get("ceph")
            if isinstance(ceph, dict):
                health = str(ceph.get("health") or "").strip()
        if (
            phase.lower() not in {"ready", "connected"}
            and health.upper() != "HEALTH_OK"
        ):
            msg = (
                f"Rook CephCluster {ROOK_CLUSTER_NAME!r} is not ready yet "
                f"(phase={phase!r}, health={health!r})"
            )
            raise TimeoutError(msg)

    await until(
        ready,
        timeout=timeout,
        interval=ROOK_BACKEND_POLL_SECONDS,
        action=f"waiting for Rook CephCluster {ROOK_CLUSTER_NAME!r}",
    )


async def _wait_storage_classes(kube: Kube, *, timeout: float) -> None:
    async def ready(remaining: float) -> None:
        cephfs = await StorageClass.get(
            kube,
            name=ROOK_CEPHFS_STORAGE_CLASS,
            timeout=remaining,
        )
        if cephfs is None:
            cephfs = await StorageClass.get(
                kube,
                name=ROOK_CEPHFS_FALLBACK_STORAGE_CLASS,
                timeout=remaining,
            )
        osd_csi = await StorageClass.get(
            kube,
            name=ROOK_OSD_STORAGE_CLASS,
            timeout=remaining,
        )
        if cephfs is None:
            msg = "Rook CephFS StorageClass is not available yet"
            raise TimeoutError(msg)
        if osd_csi is None:
            msg = "Bertrand OSD CSI StorageClass is not available yet"
            raise TimeoutError(msg)

    await until(
        ready,
        timeout=timeout,
        interval=ROOK_BACKEND_POLL_SECONDS,
        action="waiting for Bertrand storage classes",
    )
