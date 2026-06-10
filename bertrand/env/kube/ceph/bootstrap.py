"""Rook-managed Ceph bootstrap helpers for Bertrand storage."""

from __future__ import annotations

import asyncio
import json
from collections.abc import Mapping
from typing import TYPE_CHECKING

from bertrand.env.git import BERTRAND_LABEL, BERTRAND_LABEL_MANAGED, Deadline, until
from bertrand.env.kube.api.client import kubectl
from bertrand.env.kube.ceph.csi import CSI_DRIVER_NAME
from bertrand.env.kube.crd import (
    CUSTOM_RESOURCE_DEFINITION_RESOURCE,
    CustomResourceDefinition,
)
from bertrand.env.kube.custom_object import CustomObject, CustomObjectResource
from bertrand.env.kube.deployment import DEPLOYMENT_RESOURCE, Deployment
from bertrand.env.kube.namespace import NAMESPACE_RESOURCE, Namespace
from bertrand.env.kube.volume import STORAGE_CLASS_RESOURCE

if TYPE_CHECKING:
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
    BERTRAND_LABEL: BERTRAND_LABEL_MANAGED,
    ROOK_MANAGED_LABEL: ROOK_MANAGED_VALUE,
}
ROOK_CEPH_CLUSTER_RESOURCE: CustomObjectResource[CustomObject] = CustomObjectResource(
    group="ceph.rook.io",
    version="v1",
    kind="CephCluster",
    plural="cephclusters",
    default_namespace=ROOK_NAMESPACE,
)


async def ensure_rook_ceph_base(kube: Kube, *, deadline: Deadline) -> None:
    """Install Bertrand's Rook operator substrate without waiting for Ceph health.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    deadline : Deadline
        Maximum convergence budget in seconds.

    """
    await _ensure_namespace_owned(kube, ROOK_NAMESPACE, deadline=deadline)
    await _apply_urls(
        ROOK_CRDS_URL,
        ROOK_COMMON_URL,
        ROOK_OPERATOR_URL,
        deadline=deadline,
    )
    await asyncio.gather(
        _wait_crd_established(
            kube,
            "cephclusters.ceph.rook.io",
            deadline=deadline,
        ),
        _wait_crd_established(
            kube,
            "cephfilesystems.ceph.rook.io",
            deadline=deadline,
        ),
    )
    await _wait_deployment(
        kube,
        namespace=ROOK_NAMESPACE,
        name=ROOK_OPERATOR_DEPLOYMENT,
        deadline=deadline,
    )
    await asyncio.gather(
        _ensure_rook_storage_classes(deadline=deadline),
        _ensure_rook_cluster(deadline=deadline),
    )
    await _apply_urls(ROOK_TOOLBOX_URL, deadline=deadline)


async def wait_rook_ceph_ready(kube: Kube, *, deadline: Deadline) -> None:
    """Wait until the Rook-managed Ceph cluster and storage classes are ready."""
    await asyncio.gather(
        _wait_ceph_cluster_ready(kube, deadline=deadline),
        _wait_deployment(
            kube,
            namespace=ROOK_NAMESPACE,
            name=ROOK_TOOLBOX_DEPLOYMENT,
            deadline=deadline,
        ),
        _wait_storage_classes(kube, deadline=deadline),
    )


async def rook_ceph_ready(kube: Kube, *, deadline: Deadline) -> bool:
    """Return whether the Rook-managed Ceph substrate appears ready.

    Returns
    -------
    bool
        True when Rook and Bertrand storage classes can be observed.
    """
    try:
        await asyncio.gather(
            _wait_deployment(
                kube,
                namespace=ROOK_NAMESPACE,
                name=ROOK_OPERATOR_DEPLOYMENT,
                deadline=deadline,
            ),
            _wait_ceph_cluster_ready(kube, deadline=deadline),
            _wait_storage_classes(kube, deadline=deadline),
        )
    except (OSError, TimeoutError, ValueError):
        return False
    return True


async def _ensure_namespace_owned(kube: Kube, name: str, *, deadline: Deadline) -> None:
    current = await NAMESPACE_RESOURCE.get(kube, name=name, deadline=deadline)
    if current is not None:
        labels = current.labels
        if labels.get(ROOK_MANAGED_LABEL) != ROOK_MANAGED_VALUE:
            msg = (
                f"namespace {name!r} already exists and is not labelled as "
                "Bertrand-managed; refusing to mutate a shared Rook install"
            )
            raise OSError(msg)
    await Namespace.upsert(kube, name=name, labels=ROOK_LABELS, deadline=deadline)


async def _apply_urls(*urls: str, deadline: Deadline) -> None:
    for url in urls:
        try:
            await kubectl(
                ["apply", "--server-side", "-f", url],
                capture_output=True,
                deadline=deadline,
            )
        except OSError as err:
            msg = f"failed to apply storage backend manifest {url!r}"
            raise OSError(msg) from err


async def _kubectl_apply_manifest(
    manifest: Mapping[str, object],
    *,
    deadline: Deadline,
) -> None:
    await kubectl(
        ["apply", "--server-side", "-f", "-"],
        stdin=json.dumps(manifest, separators=(",", ":")),
        capture_output=True,
        deadline=deadline,
    )


async def _wait_crd_established(
    kube: Kube,
    name: str,
    *,
    deadline: Deadline,
) -> CustomResourceDefinition:
    async def established(attempt_deadline: Deadline) -> CustomResourceDefinition:
        crd = await CUSTOM_RESOURCE_DEFINITION_RESOURCE.get(
            kube,
            name=name,
            deadline=attempt_deadline,
        )
        if crd is None:
            msg = f"storage backend CRD {name!r} is not installed yet"
            raise TimeoutError(msg)
        if not crd.is_established:
            msg = f"storage backend CRD {name!r} is not Established yet"
            raise TimeoutError(msg)
        return crd

    try:
        return await until(
            established,
            deadline=deadline,
            delay=ROOK_BACKEND_POLL_SECONDS,
        )
    except TimeoutError as err:
        msg = f"waiting for storage backend CRD {name!r} timed out"
        raise TimeoutError(msg) from err


async def _wait_deployment(
    kube: Kube,
    *,
    namespace: str,
    name: str,
    deadline: Deadline,
) -> Deployment:
    async def available(attempt_deadline: Deadline) -> Deployment:
        deployment = await DEPLOYMENT_RESOURCE.get(
            kube,
            namespace=namespace,
            name=name,
            deadline=attempt_deadline,
        )
        if deployment is None:
            msg = f"Deployment {namespace}/{name} is not created yet"
            raise TimeoutError(msg)
        if not deployment.has_available_replicas():
            msg = f"Deployment {namespace}/{name} is not Available yet"
            raise TimeoutError(msg)
        return deployment

    try:
        return await until(
            available,
            deadline=deadline,
            delay=ROOK_BACKEND_POLL_SECONDS,
        )
    except TimeoutError as err:
        msg = f"waiting for Deployment {namespace}/{name} timed out"
        raise TimeoutError(msg) from err


async def _ensure_rook_storage_classes(*, deadline: Deadline) -> None:
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
    await _kubectl_apply_manifest(manifest, deadline=deadline)


async def _ensure_rook_cluster(*, deadline: Deadline) -> None:
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
    await _kubectl_apply_manifest(manifest, deadline=deadline)


async def _wait_ceph_cluster_ready(kube: Kube, *, deadline: Deadline) -> None:
    async def ready(attempt_deadline: Deadline) -> None:
        obj = await ROOK_CEPH_CLUSTER_RESOURCE.get(
            kube,
            name=ROOK_CLUSTER_NAME,
            deadline=attempt_deadline,
            context=f"failed to read Rook CephCluster {ROOK_NAMESPACE}/"
            f"{ROOK_CLUSTER_NAME}",
        )
        if obj is None:
            msg = "Rook CephCluster payload is malformed"
            raise TimeoutError(msg)
        status = obj.status
        phase = ""
        health = ""
        if isinstance(status, Mapping):
            phase = str(status.get("phase") or "").strip()
            ceph = status.get("ceph")
            if isinstance(ceph, Mapping):
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

    try:
        await until(ready, deadline=deadline, delay=ROOK_BACKEND_POLL_SECONDS)
    except TimeoutError as err:
        msg = f"waiting for Rook CephCluster {ROOK_CLUSTER_NAME!r} timed out"
        raise TimeoutError(msg) from err


async def _wait_storage_classes(kube: Kube, *, deadline: Deadline) -> None:
    async def ready(attempt_deadline: Deadline) -> None:
        cephfs, fallback, osd_csi = await asyncio.gather(
            STORAGE_CLASS_RESOURCE.get(
                kube,
                name=ROOK_CEPHFS_STORAGE_CLASS,
                deadline=attempt_deadline,
            ),
            STORAGE_CLASS_RESOURCE.get(
                kube,
                name=ROOK_CEPHFS_FALLBACK_STORAGE_CLASS,
                deadline=attempt_deadline,
            ),
            STORAGE_CLASS_RESOURCE.get(
                kube,
                name=ROOK_OSD_STORAGE_CLASS,
                deadline=attempt_deadline,
            ),
        )
        cephfs = cephfs or fallback
        if cephfs is None:
            msg = "Rook CephFS StorageClass is not available yet"
            raise TimeoutError(msg)
        if osd_csi is None:
            msg = "Bertrand OSD CSI StorageClass is not available yet"
            raise TimeoutError(msg)

    try:
        await until(ready, deadline=deadline, delay=ROOK_BACKEND_POLL_SECONDS)
    except TimeoutError as err:
        msg = "waiting for Bertrand storage classes timed out"
        raise TimeoutError(msg) from err
