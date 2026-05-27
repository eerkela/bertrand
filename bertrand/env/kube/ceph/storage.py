"""Ceph storage controller control plane composition."""

from __future__ import annotations

import asyncio
import os
import platform
import sys
from collections.abc import Mapping
from contextlib import suppress
from datetime import UTC, datetime
from pathlib import Path
from typing import TYPE_CHECKING

from bertrand.env.git import BERTRAND_NAMESPACE, INFINITY, Deadline
from bertrand.env.kube.api.client import (
    CLUSTER_REGISTRY_READY_LABEL,
    CLUSTER_REGISTRY_READY_VALUE,
    Kube,
)
from bertrand.env.kube.api.spec import ContainerSpec, PodTemplateSpec, VolumeSpec
from bertrand.env.kube.build.request import BUILDKIT_BUILD_GROUP, BUILDKIT_BUILD_PLURAL
from bertrand.env.kube.ceph.api import (
    CephCapacitySnapshot,
    PreparedOSD,
    ceph_df,
    ceph_health,
    ceph_osds,
    delete_loop_fallback_substrate,
    delete_lvm_osd_substrate,
    discover_loop_fallback_osd,
    discover_lvm_osds,
    drain_ceph_osd,
    drain_loop_osd,
    host_capacity_snapshot,
    host_id_from_host_state,
    kube_quantity,
    parse_size_bytes,
    prepare_loop_fallback_osd,
    prepare_lvm_osd,
    purge_ceph_osd,
    purge_loop_osd,
)
from bertrand.env.kube.ceph.bootstrap import (
    ROOK_CEPH_CLUSTER_RESOURCE,
    ROOK_CLUSTER_NAME,
    ROOK_NAMESPACE,
    ROOK_OSD_STORAGE_CLASS,
)
from bertrand.env.kube.ceph.capacity import (
    CEPH_CAPACITY_GROUP,
    STORAGE_ACTION_PLURAL,
    STORAGE_ACTION_RESOURCE,
    STORAGE_ACTION_STALE_SECONDS,
    STORAGE_CONTROLLER_LABELS,
    STORAGE_OSD_LABEL,
    STORAGE_OSD_LABEL_VALUE,
    STORAGE_OSD_NAME_LABEL,
    STORAGE_OSD_STALE_PHASE_SECONDS,
    STORAGE_STATE_NAME,
    STORAGE_STATE_PLURAL,
    STORAGE_STATE_RESOURCE,
    CephStorageActionRecord,
    CephStorageActionSpec,
    CephStorageNodeReport,
    CephStorageOSD,
    CephStoragePolicyStatus,
    CephStorageReservation,
    CephStorageStateRecord,
    CephStorageStateStatus,
    StorageOSDOrigin,
    StorageOSDPhase,
    _eligible_storage_nodes,
    _last_storage_shrink_at,
    _lvm_osds_for_shrink,
    _lvm_shrink_preview,
    _managed_loop_osds,
    _plan_loop_offload_action,
    _plan_loop_shrink_action,
    _plan_lvm_coverage_actions,
    _plan_lvm_shrink_action,
    _plan_storage_grow_actions,
    _storage_action_counts,
    _storage_growth_status,
    _storage_osd_admission_in_flight,
    _storage_osd_counts,
    _storage_utc,
    create_storage_actions,
    ensure_ceph_capacity_crds,
    ensure_default_storage_policy,
    patch_storage_action_status,
    patch_storage_osd_status,
    patch_storage_reservation_status,
    pending_storage_actions,
    read_storage_state,
    storage_loop_osd_name,
    storage_lvm_osd_name,
    storage_osd_resource_names,
    storage_watch_targets,
    upsert_storage_node_report,
    upsert_storage_osd,
)
from bertrand.env.kube.ceph.csi import (
    CSI_CONTROLLER_SOCKET,
    CSI_DRIVER_NAME,
    CSI_KUBELET_DIR,
    CSI_NODE_SOCKET,
    CSI_REGISTRATION_DIR,
    CSI_SOCKET_DIR,
)
from bertrand.env.kube.ceph.snapshot import (
    ensure_repository_snapshot_support,
    maintain_repository_snapshots,
    next_repository_snapshot_time,
)
from bertrand.env.kube.ceph.volume import (
    REPOSITORY_STATE_PLURAL,
    REPOSITORY_STATE_RESOURCE,
    gc_repository_volumes,
    next_repository_volume_gc_time,
)
from bertrand.env.kube.control import MaintenanceClock
from bertrand.env.kube.daemonset import DaemonSet
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.node import Node
from bertrand.env.kube.pod import Pod
from bertrand.env.kube.rbac import (
    upsert_rbac_binding,
    upsert_rbac_role,
)
from bertrand.env.kube.service_account import ServiceAccount
from bertrand.env.kube.volume import PersistentVolumeClaim

if TYPE_CHECKING:
    from collections.abc import Collection

    from bertrand.env.kube.api.spec import PolicyRuleManifest
    from bertrand.env.kube.custom_object import CustomObjectResource

STORAGE_CONTROLLER_SERVICE_ACCOUNT = "bertrand-ceph-storage-controller"
STORAGE_CONTROLLER_NAME = "bertrand-ceph-storage-controller"
STORAGE_AGENT_NAME = "bertrand-ceph-storage-agent"
CSI_CONTROLLER_NAME = "bertrand-ceph-osd-csi-controller"
CSI_NODE_NAME = "bertrand-ceph-osd-csi-node"
CSI_PROVISIONER_IMAGE = "registry.k8s.io/sig-storage/csi-provisioner:v5.2.0"
CSI_RESIZER_IMAGE = "registry.k8s.io/sig-storage/csi-resizer:v1.13.2"
CSI_NODE_REGISTRAR_IMAGE = (
    "registry.k8s.io/sig-storage/csi-node-driver-registrar:v2.13.0"
)
STORAGE_WATCH_RESTART_DELAY_SECONDS = 1.0
STORAGE_CONTROLLER_DEFAULT_RECONCILE_SECONDS = 30.0
STORAGE_AGENT_SYNC_INTERVAL_SECONDS = 5.0
STORAGE_OSD_WAIT_POLL_SECONDS = 2.0
REPOSITORY_VOLUME_GC_EMPTY_CHECK_SECONDS = 3600.0
REPOSITORY_VOLUME_GC_READY_CHECK_SECONDS = 900.0
REPOSITORY_VOLUME_GC_FAILURE_RETRY_SECONDS = 300.0
REPOSITORY_VOLUME_GC_TIMEOUT_SECONDS = 60.0
REPOSITORY_SNAPSHOT_EMPTY_CHECK_SECONDS = 3600.0
REPOSITORY_SNAPSHOT_READY_CHECK_SECONDS = 900.0
REPOSITORY_SNAPSHOT_FAILURE_RETRY_SECONDS = 300.0
REPOSITORY_SNAPSHOT_TIMEOUT_SECONDS = 120.0

HOST_ROOT_VOLUME = "host-root"
HOST_ROOT_MOUNT = "/host"
CSI_SOCKET_VOLUME = "csi-socket"
CSI_NODE_PLUGIN_VOLUME = "csi-node-plugin"
CSI_REGISTRATION_VOLUME = "csi-registration"
CSI_KUBELET_VOLUME = "csi-kubelet"
CSI_HOST_DEV_VOLUME = "host-dev"
CSI_HOST_RUN_VOLUME = "host-run"


def _storage_controller_container(*, image: str, role: str) -> ContainerSpec:
    return ContainerSpec(
        name=role,
        image=image,
        image_pull_policy="Always",
        args=[role],
        env=[
            {
                "name": "NODE_NAME",
                "valueFrom": {"fieldRef": {"fieldPath": "spec.nodeName"}},
            }
        ],
        security_context={"privileged": True, "runAsUser": 0},
        volume_mounts=[{"name": HOST_ROOT_VOLUME, "mountPath": HOST_ROOT_MOUNT}],
    )


def _osd_spec(
    *,
    name: str,
    origin: str,
    node_name: str,
    host_id: str,
    prepared: PreparedOSD,
    target_bytes: int,
) -> dict[str, object]:
    _, _, device_set_name = storage_osd_resource_names(name)
    return {
        "origin": origin,
        "node_name": node_name,
        "host_id": host_id,
        "pv_name": prepared.pv_name,
        "pv_uuid": prepared.pv_uuid,
        "pv_device": prepared.pv_device,
        "lv_name": prepared.lv_name,
        "lv_path": prepared.lv_path,
        "loop_file": prepared.loop_file.as_posix() if prepared.loop_file else "",
        "loop_device": prepared.loop_device,
        "block_path": prepared.block_path.as_posix(),
        "csi_volume_id": "",
        "persistent_volume_name": "",
        "persistent_volume_claim_namespace": "",
        "persistent_volume_claim_name": "",
        "device_set_name": device_set_name,
        "target_bytes": max(target_bytes, prepared.observed_bytes),
    }


def _rook_device_set(record: CephStorageOSD) -> dict[str, object]:
    return {
        "name": record.device_set_name,
        "count": 1,
        "portable": False,
        "tuneDeviceClass": True,
        "volumeClaimTemplates": [
            {
                "metadata": {
                    "name": "data",
                    "labels": {
                        STORAGE_OSD_LABEL: STORAGE_OSD_LABEL_VALUE,
                        STORAGE_OSD_NAME_LABEL: record.name,
                    },
                },
                "spec": {
                    "resources": {
                        "requests": {"storage": kube_quantity(record.target_bytes)}
                    },
                    "storageClassName": ROOK_OSD_STORAGE_CLASS,
                    "volumeMode": "Block",
                    "accessModes": ["ReadWriteOnce"],
                },
            }
        ],
    }


async def _patch_rook_device_sets(
    kube: Kube,
    *,
    records: Collection[CephStorageOSD],
    timeout: float,
) -> None:
    current = await ROOK_CEPH_CLUSTER_RESOURCE.get(
        kube,
        name=ROOK_CLUSTER_NAME,
        timeout=timeout,
        context="failed to inspect Rook CephCluster OSD device sets",
    )
    allowed_names = {record.device_set_name for record in records}
    storage: object = {}
    if current is not None:
        spec = current.spec
        if isinstance(spec, Mapping):
            candidate = spec.get("storage")
            if isinstance(candidate, Mapping):
                storage = candidate
    existing_sets = (
        storage.get("storageClassDeviceSets", [])
        if isinstance(storage, Mapping)
        else []
    )
    if isinstance(existing_sets, list):
        for item in existing_sets:
            if not isinstance(item, dict):
                continue
            name = str(item.get("name") or "").strip()
            if name and name not in allowed_names:
                msg = (
                    f"Rook device set {name!r} is not owned by Bertrand's OSD "
                    "inventory; refusing to replace storageClassDeviceSets"
                )
                raise OSError(msg)
    device_sets = [
        _rook_device_set(record)
        for record in sorted(records, key=lambda item: item.device_set_name)
        if record.phase not in {"Failed", "Shrinking", "Retiring", "Retired"}
    ]
    await ROOK_CEPH_CLUSTER_RESOURCE.patch(
        kube,
        name=ROOK_CLUSTER_NAME,
        body={"spec": {"storage": {"storageClassDeviceSets": device_sets}}},
        timeout=timeout,
        context="failed to patch Rook CephCluster OSD device sets",
    )


async def _resize_osd_claim(
    kube: Kube,
    *,
    record: CephStorageOSD,
    timeout: float,
) -> None:
    claims = await PersistentVolumeClaim.list(
        kube,
        timeout=timeout,
        namespaces=(ROOK_NAMESPACE,),
        labels={STORAGE_OSD_NAME_LABEL: record.name},
    )
    if not claims:
        return
    for claim in claims:
        claim_name = claim.name
        claim_namespace = claim.namespace

        def patch(
            request_timeout: float | None,
            *,
            claim_name: str = claim_name,
            claim_namespace: str = claim_namespace,
        ) -> object:
            return kube.core.patch_namespaced_persistent_volume_claim(
                name=claim_name,
                namespace=claim_namespace,
                body={
                    "spec": {
                        "resources": {
                            "requests": {
                                "storage": kube_quantity(record.target_bytes)
                            }
                        }
                    }
                },
                _request_timeout=request_timeout,
            )

        await kube.run(
            patch,
            timeout=timeout,
            context=f"failed to resize OSD PVC {claim_namespace}/{claim_name}",
        )


async def _delete_osd_claims(
    kube: Kube, *, record: CephStorageOSD, timeout: float
) -> None:
    claims = await PersistentVolumeClaim.list(
        kube,
        timeout=timeout,
        namespaces=(ROOK_NAMESPACE,),
        labels={STORAGE_OSD_NAME_LABEL: record.name},
    )
    for claim in claims:
        await claim.delete(kube, timeout=timeout)


async def _wait_osd_claims_gone(
    kube: Kube,
    *,
    record: CephStorageOSD,
    timeout: float,
) -> None:
    msg = f"timed out waiting for OSD PVCs for {record.name!r} to delete"
    deadline = Deadline.from_timeout(timeout, message=msg)
    while deadline.remaining() > 0:
        claims = await PersistentVolumeClaim.list(
            kube,
            timeout=deadline.remaining(),
            namespaces=(ROOK_NAMESPACE,),
            labels={STORAGE_OSD_NAME_LABEL: record.name},
        )
        if not claims:
            return
        await asyncio.sleep(deadline.bounded(STORAGE_OSD_WAIT_POLL_SECONDS))
    raise TimeoutError(msg)


async def _wait_osd_workloads_gone(
    kube: Kube,
    *,
    record: CephStorageOSD,
    timeout: float,
) -> None:
    msg = (
        f"timed out waiting for Rook workloads for OSD {record.name!r} to stop"
    )
    deadline = Deadline.from_timeout(timeout, message=msg)
    claim_names = {
        claim.name
        for claim in await PersistentVolumeClaim.list(
            kube,
            timeout=deadline.remaining(),
            namespaces=(ROOK_NAMESPACE,),
            labels={STORAGE_OSD_NAME_LABEL: record.name},
        )
    }
    while deadline.remaining() > 0:
        pods = await Pod.list(
            kube,
            timeout=deadline.remaining(),
            namespaces=(ROOK_NAMESPACE,),
        )
        active = [
            pod.name
            for pod in pods
            if pod.is_active
            and (
                pod.labels.get("ceph.rook.io/DeviceSet") == record.device_set_name
                or pod.labels.get("ceph.rook.io/pvc") in claim_names
            )
        ]
        if not active:
            return
        await asyncio.sleep(deadline.bounded(STORAGE_OSD_WAIT_POLL_SECONDS))
    raise TimeoutError(msg)


def _metadata_osd_id(labels: Mapping[str, str]) -> int | None:
    for key in (
        "ceph.rook.io/osd-id",
        "ceph.rook.io/osd",
        "ceph-osd-id",
    ):
        value = labels.get(key, "").strip()
        if value.isdigit():
            return int(value)
    return None


def _deadline_from_budget(seconds: float) -> Deadline:
    if seconds <= 0:
        return Deadline(
            expires_at=asyncio.get_running_loop().time(),
            timeout=seconds,
        )
    return Deadline.from_timeout(seconds, message="")


async def _observe_rook_osd(
    kube: Kube,
    *,
    record: CephStorageOSD,
    timeout: float,
) -> tuple[int | None, bool]:
    deadline = _deadline_from_budget(timeout)
    observed_id = record.ceph_osd_id
    while deadline.remaining() > 0:
        claims = await PersistentVolumeClaim.list(
            kube,
            timeout=deadline.remaining(),
            namespaces=(ROOK_NAMESPACE,),
            labels={STORAGE_OSD_NAME_LABEL: record.name},
        )
        claim_names = {claim.name for claim in claims}
        for claim in claims:
            osd_id = _metadata_osd_id(claim.labels) or _metadata_osd_id(
                claim.annotations
            )
            if osd_id is not None:
                observed_id = osd_id
        pods = await Pod.list(
            kube,
            timeout=deadline.remaining(),
            namespaces=(ROOK_NAMESPACE,),
        )
        for pod in pods:
            labels = pod.labels
            if (
                labels.get("ceph.rook.io/DeviceSet") != record.device_set_name
                and labels.get("ceph.rook.io/pvc") not in claim_names
            ):
                continue
            osd_id = _metadata_osd_id(labels) or _metadata_osd_id(pod.annotations)
            if osd_id is not None:
                observed_id = osd_id
        try:
            live = await ceph_osds(timeout=deadline.remaining())
        except (OSError, TimeoutError):
            await asyncio.sleep(deadline.bounded(STORAGE_OSD_WAIT_POLL_SECONDS))
            continue
        if observed_id is not None:
            for osd in live:
                if osd.osd_id == observed_id and osd.up and osd.in_cluster:
                    return observed_id, True
        await asyncio.sleep(deadline.bounded(STORAGE_OSD_WAIT_POLL_SECONDS))
    return observed_id, False


async def _upsert_csi_driver_object(kube: Kube, *, timeout: float) -> None:
    manifest = {
        "apiVersion": "storage.k8s.io/v1",
        "kind": "CSIDriver",
        "metadata": {
            "name": CSI_DRIVER_NAME,
            "labels": STORAGE_CONTROLLER_LABELS,
        },
        "spec": {
            "attachRequired": False,
            "podInfoOnMount": False,
            "volumeLifecycleModes": ["Persistent"],
            "fsGroupPolicy": "None",
            "requiresRepublish": False,
            "storageCapacity": False,
        },
    }
    existing = await kube.run(
        lambda request_timeout: kube.storage.read_csi_driver(
            name=CSI_DRIVER_NAME,
            _request_timeout=request_timeout,
        ),
        timeout=timeout,
        context=f"failed to read CSIDriver {CSI_DRIVER_NAME!r}",
    )
    if existing is None:
        await kube.run(
            lambda request_timeout: kube.storage.create_csi_driver(
                body=manifest,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to create CSIDriver {CSI_DRIVER_NAME!r}",
        )
        return
    await kube.run(
        lambda request_timeout: kube.storage.patch_csi_driver(
            name=CSI_DRIVER_NAME,
            body=manifest,
            _request_timeout=request_timeout,
        ),
        timeout=timeout,
        context=f"failed to patch CSIDriver {CSI_DRIVER_NAME!r}",
    )


def _csi_common_labels(name: str) -> dict[str, str]:
    return {
        "app.kubernetes.io/name": name,
        "app.kubernetes.io/part-of": "bertrand",
        **STORAGE_CONTROLLER_LABELS,
    }


async def _ensure_csi_controller(
    kube: Kube,
    *,
    image: str,
    deadline: Deadline,
) -> None:
    labels = _csi_common_labels(CSI_CONTROLLER_NAME)
    socket_mount = {
        "name": CSI_SOCKET_VOLUME,
        "mountPath": Path(CSI_CONTROLLER_SOCKET).parent.as_posix(),
    }
    deployment = await Deployment.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=CSI_CONTROLLER_NAME,
        labels=labels,
        selector={"app.kubernetes.io/name": CSI_CONTROLLER_NAME},
        replicas=1,
        pod_template=PodTemplateSpec(
            containers=[
                ContainerSpec(
                    name="driver",
                    image=image,
                    image_pull_policy="Always",
                    command=["bertrand-ceph-csi"],
                    args=[
                        "controller",
                        "--endpoint",
                        f"unix://{CSI_CONTROLLER_SOCKET}",
                    ],
                    volume_mounts=[socket_mount],
                ),
                ContainerSpec(
                    name="external-provisioner",
                    image=CSI_PROVISIONER_IMAGE,
                    args=[
                        f"--csi-address={CSI_CONTROLLER_SOCKET}",
                        "--extra-create-metadata=true",
                        "--feature-gates=Topology=true",
                        "--leader-election=false",
                        "--timeout=300s",
                    ],
                    volume_mounts=[socket_mount],
                ),
                ContainerSpec(
                    name="external-resizer",
                    image=CSI_RESIZER_IMAGE,
                    args=[
                        f"--csi-address={CSI_CONTROLLER_SOCKET}",
                        "--leader-election=false",
                        "--timeout=300s",
                    ],
                    volume_mounts=[socket_mount],
                ),
            ],
            volumes=[VolumeSpec.empty_dir(CSI_SOCKET_VOLUME)],
            service_account_name=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
            automount_service_account_token=True,
            node_selector={CLUSTER_REGISTRY_READY_LABEL: CLUSTER_REGISTRY_READY_VALUE},
        ),
        timeout=deadline.remaining(),
    )
    await deployment.wait_rollout(kube, timeout=deadline.remaining())


async def _ensure_csi_node(kube: Kube, *, image: str, deadline: Deadline) -> None:
    labels = _csi_common_labels(CSI_NODE_NAME)
    plugin_dir = f"{CSI_KUBELET_DIR}/plugins/{CSI_DRIVER_NAME}"
    daemonset = await DaemonSet.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=CSI_NODE_NAME,
        labels=labels,
        selector={"app.kubernetes.io/name": CSI_NODE_NAME},
        pod_template=PodTemplateSpec(
            containers=[
                ContainerSpec(
                    name="driver",
                    image=image,
                    image_pull_policy="Always",
                    command=["bertrand-ceph-csi"],
                    args=["node", "--endpoint", f"unix://{CSI_NODE_SOCKET}"],
                    env=[
                        {
                            "name": "NODE_NAME",
                            "valueFrom": {
                                "fieldRef": {"fieldPath": "spec.nodeName"}
                            },
                        }
                    ],
                    security_context={"privileged": True, "runAsUser": 0},
                    volume_mounts=[
                        {"name": CSI_NODE_PLUGIN_VOLUME, "mountPath": CSI_SOCKET_DIR},
                        {
                            "name": CSI_KUBELET_VOLUME,
                            "mountPath": CSI_KUBELET_DIR,
                            "mountPropagation": "Bidirectional",
                        },
                        {
                            "name": HOST_ROOT_VOLUME,
                            "mountPath": HOST_ROOT_MOUNT,
                            "mountPropagation": "Bidirectional",
                        },
                        {
                            "name": CSI_HOST_DEV_VOLUME,
                            "mountPath": "/dev",
                            "mountPropagation": "HostToContainer",
                        },
                        {
                            "name": CSI_HOST_RUN_VOLUME,
                            "mountPath": "/host-run",
                            "mountPropagation": "HostToContainer",
                        },
                    ],
                ),
                ContainerSpec(
                    name="node-driver-registrar",
                    image=CSI_NODE_REGISTRAR_IMAGE,
                    args=[
                        f"--csi-address={CSI_NODE_SOCKET}",
                        f"--kubelet-registration-path={plugin_dir}/csi.sock",
                    ],
                    volume_mounts=[
                        {"name": CSI_NODE_PLUGIN_VOLUME, "mountPath": CSI_SOCKET_DIR},
                        {
                            "name": CSI_REGISTRATION_VOLUME,
                            "mountPath": CSI_REGISTRATION_DIR,
                        },
                    ],
                ),
            ],
            volumes=[
                VolumeSpec.host_path(
                    CSI_NODE_PLUGIN_VOLUME,
                    path=plugin_dir,
                    host_path_type="DirectoryOrCreate",
                ),
                VolumeSpec.host_path(
                    CSI_REGISTRATION_VOLUME,
                    path=f"{CSI_KUBELET_DIR}/plugins_registry",
                    host_path_type="DirectoryOrCreate",
                ),
                VolumeSpec.host_path(
                    CSI_KUBELET_VOLUME,
                    path=CSI_KUBELET_DIR,
                    host_path_type="Directory",
                ),
                VolumeSpec.host_path(
                    HOST_ROOT_VOLUME,
                    path="/",
                    host_path_type="Directory",
                ),
                VolumeSpec.host_path(
                    CSI_HOST_DEV_VOLUME,
                    path="/dev",
                    host_path_type="Directory",
                ),
                VolumeSpec.host_path(
                    CSI_HOST_RUN_VOLUME,
                    path="/run",
                    host_path_type="Directory",
                ),
            ],
            service_account_name=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
            automount_service_account_token=True,
            node_selector={CLUSTER_REGISTRY_READY_LABEL: CLUSTER_REGISTRY_READY_VALUE},
            host_pid=True,
        ),
        timeout=deadline.remaining(),
    )
    await daemonset.wait_rollout(kube, timeout=deadline.remaining())


async def _ensure_csi_driver(kube: Kube, *, image: str, deadline: Deadline) -> None:
    await _upsert_csi_driver_object(kube, timeout=deadline.remaining())
    await _ensure_csi_controller(kube, image=image, deadline=deadline)
    await _ensure_csi_node(kube, image=image, deadline=deadline)


async def _ensure_rbac(kube: Kube, *, deadline: Deadline) -> None:
    await _ensure_storage_service_account(kube, deadline=deadline)
    await _ensure_storage_cluster_role(kube, deadline=deadline)
    await _ensure_storage_cluster_role_binding(kube, deadline=deadline)


async def _ensure_storage_service_account(kube: Kube, *, deadline: Deadline) -> None:
    await ServiceAccount.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
        labels=STORAGE_CONTROLLER_LABELS,
        timeout=deadline.remaining(),
    )


async def _ensure_storage_cluster_role(kube: Kube, *, deadline: Deadline) -> None:
    await upsert_rbac_role(
        kube,
        kind="ClusterRole",
        name=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
        labels=STORAGE_CONTROLLER_LABELS,
        rules=_storage_controller_rbac_rules(),
        timeout=deadline.remaining(),
    )


def _storage_controller_rbac_rules() -> list[PolicyRuleManifest]:
    return [
        {
            "apiGroups": [CEPH_CAPACITY_GROUP],
            "resources": [
                STORAGE_STATE_PLURAL,
                STORAGE_ACTION_PLURAL,
                REPOSITORY_STATE_PLURAL,
            ],
            "verbs": ["get", "list", "watch", "create", "update", "patch"],
        },
        {
            "apiGroups": [CEPH_CAPACITY_GROUP],
            "resources": [
                f"{STORAGE_STATE_PLURAL}/status",
                f"{STORAGE_ACTION_PLURAL}/status",
                f"{REPOSITORY_STATE_PLURAL}/status",
            ],
            "verbs": ["get", "update", "patch"],
        },
        {
            "apiGroups": ["ceph.rook.io"],
            "resources": ["cephclusters"],
            "verbs": ["get", "list", "watch", "patch"],
        },
        {
            "apiGroups": [""],
            "resources": [
                "events",
                "persistentvolumes",
                "persistentvolumeclaims",
            ],
            "verbs": [
                "get",
                "list",
                "watch",
                "create",
                "update",
                "patch",
                "delete",
            ],
        },
        {
            "apiGroups": ["storage.k8s.io"],
            "resources": [
                "csidrivers",
                "csinodes",
                "storageclasses",
                "volumeattachments",
            ],
            "verbs": [
                "get",
                "list",
                "watch",
                "create",
                "update",
                "patch",
                "delete",
            ],
        },
        {
            "apiGroups": ["coordination.k8s.io"],
            "resources": ["leases"],
            "verbs": [
                "get",
                "list",
                "watch",
                "create",
                "update",
                "patch",
                "delete",
            ],
        },
        {
            "apiGroups": [CEPH_CAPACITY_GROUP],
            "resources": [REPOSITORY_STATE_PLURAL],
            "verbs": ["delete"],
        },
        {
            "apiGroups": [""],
            "resources": ["secrets"],
            "verbs": ["get", "list", "watch", "delete"],
        },
        {
            "apiGroups": [BUILDKIT_BUILD_GROUP],
            "resources": [BUILDKIT_BUILD_PLURAL],
            "verbs": ["get", "list", "watch"],
        },
        {
            "apiGroups": [""],
            "resources": ["nodes"],
            "verbs": ["get", "list", "watch"],
        },
        {
            "apiGroups": [""],
            "resources": ["pods"],
            "verbs": ["get", "list", "watch"],
        },
        {
            "apiGroups": ["apps"],
            "resources": ["deployments"],
            "verbs": ["get", "list", "watch"],
        },
        {
            "apiGroups": ["batch"],
            "resources": ["jobs", "cronjobs"],
            "verbs": ["get", "list", "watch"],
        },
        {
            "apiGroups": ["snapshot.storage.k8s.io"],
            "resources": ["volumesnapshots"],
            "verbs": ["get", "list", "watch", "create", "delete"],
        },
        {
            "apiGroups": ["snapshot.storage.k8s.io"],
            "resources": ["volumesnapshotclasses"],
            "verbs": ["get", "list", "watch", "create"],
        },
    ]


async def _ensure_storage_cluster_role_binding(
    kube: Kube,
    *,
    deadline: Deadline,
) -> None:
    await upsert_rbac_binding(
        kube,
        kind="ClusterRoleBinding",
        name=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
        role_kind="ClusterRole",
        role_name=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
        service_account_name=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
        service_account_namespace=BERTRAND_NAMESPACE,
        labels=STORAGE_CONTROLLER_LABELS,
        timeout=deadline.remaining(),
    )


async def _ensure_workloads(kube: Kube, *, image: str, deadline: Deadline) -> None:
    volumes = [
        VolumeSpec.host_path(HOST_ROOT_VOLUME, path="/", host_path_type="Directory")
    ]

    controller = await Deployment.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=STORAGE_CONTROLLER_NAME,
        labels={
            "app.kubernetes.io/name": STORAGE_CONTROLLER_NAME,
            "app.kubernetes.io/part-of": "bertrand",
            **STORAGE_CONTROLLER_LABELS,
        },
        selector={"app.kubernetes.io/name": STORAGE_CONTROLLER_NAME},
        pod_template=PodTemplateSpec(
            containers=[
                _storage_controller_container(image=image, role="controller")
            ],
            volumes=volumes,
            service_account_name=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
            automount_service_account_token=True,
            node_selector={CLUSTER_REGISTRY_READY_LABEL: CLUSTER_REGISTRY_READY_VALUE},
            host_pid=True,
        ),
        timeout=deadline.remaining(),
    )
    await controller.wait_rollout(kube, timeout=deadline.remaining())

    agent = await DaemonSet.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=STORAGE_AGENT_NAME,
        labels={
            "app.kubernetes.io/name": STORAGE_AGENT_NAME,
            "app.kubernetes.io/part-of": "bertrand",
            **STORAGE_CONTROLLER_LABELS,
        },
        selector={"app.kubernetes.io/name": STORAGE_AGENT_NAME},
        pod_template=PodTemplateSpec(
            containers=[_storage_controller_container(image=image, role="agent")],
            volumes=volumes,
            service_account_name=STORAGE_CONTROLLER_SERVICE_ACCOUNT,
            automount_service_account_token=True,
            node_selector={CLUSTER_REGISTRY_READY_LABEL: CLUSTER_REGISTRY_READY_VALUE},
            host_pid=True,
        ),
        timeout=deadline.remaining(),
    )
    await agent.wait_rollout(kube, timeout=deadline.remaining())


async def ensure_ceph_storage_controller(
    kube: Kube, *, image: str, timeout: float
) -> None:
    """Converge Ceph storage controller CRDs, RBAC, and workloads in the local cluster.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    image : str
        Fully qualified storage controller image reference.
    timeout : float
        Maximum convergence budget in seconds.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive or convergence exceeds the budget.
    ValueError
        If `image` is empty.
    """
    if timeout <= 0:
        msg = "timeout must be non-negative"
        raise TimeoutError(msg)
    image = image.strip()
    if not image:
        msg = "control plane image reference cannot be empty"
        raise ValueError(msg)
    deadline = Deadline.from_timeout(timeout, message="timeout must be non-negative")
    await ensure_ceph_capacity_crds(
        kube,
        timeout=deadline.remaining(),
    )
    await REPOSITORY_STATE_RESOURCE.ensure_crd(
        kube,
        timeout=deadline.remaining(),
    )
    await ensure_repository_snapshot_support(
        kube,
        timeout=deadline.remaining(),
    )
    await _ensure_rbac(kube, deadline=deadline)
    await ensure_default_storage_policy(
        kube,
        timeout=deadline.remaining(),
    )
    await _ensure_csi_driver(kube, image=image, deadline=deadline)
    await _ensure_workloads(kube, image=image, deadline=deadline)


async def _watch_storage_resource(
    kube: Kube,
    *,
    client: CustomObjectResource[object],
    wake: asyncio.Event,
    deadline: Deadline,
    context: str,
) -> None:
    while True:
        try:
            async for _event in client.watch(
                kube,
                namespace=BERTRAND_NAMESPACE,
                timeout=deadline.remaining(),
            ):
                wake.set()
            wake.set()
            await asyncio.sleep(deadline.bounded(STORAGE_WATCH_RESTART_DELAY_SECONDS))
        except asyncio.CancelledError:
            raise
        except (OSError, RuntimeError, ValueError) as err:
            print(
                "bertrand: warning: Ceph storage controller "
                f"{context} watch failed: {err}",
                file=sys.stderr,
            )
            wake.set()
            await asyncio.sleep(deadline.bounded(STORAGE_WATCH_RESTART_DELAY_SECONDS))


async def _ready_storage_nodes(kube: Kube, *, timeout: float) -> list[str]:
    nodes = await Node.list(
        kube,
        labels={CLUSTER_REGISTRY_READY_LABEL: CLUSTER_REGISTRY_READY_VALUE},
        timeout=timeout,
    )
    return sorted(node.name for node in nodes if node.name and node.is_ready)


async def _mark_stale_actions_failed(
    kube: Kube,
    *,
    actions: Collection[CephStorageActionRecord],
    timeout: float,
) -> bool:
    now = datetime.now(UTC)
    changed = False
    for action in actions:
        if action.status.phase != "Running":
            continue
        started_at = _storage_utc(action.status.started_at)
        if started_at is None:
            continue
        age = (now - started_at).total_seconds()
        if age < STORAGE_ACTION_STALE_SECONDS:
            continue
        await patch_storage_action_status(
            kube,
            action=action,
            status={
                "phase": "Failed",
                "finished_at": now.isoformat(),
                "message": (
                    f"storage action timed out after {int(age)}s without "
                    "node-agent progress"
                ),
                "diagnostics": (
                    "The storage agent may have crashed or lost Kubernetes "
                    "connectivity. Re-run storage doctor and inspect the "
                    "bertrand-ceph-storage-agent logs."
                ),
            },
            timeout=timeout,
        )
        changed = True
    return changed


async def _mark_stale_osds_failed(
    kube: Kube,
    *,
    osd_records: Collection[CephStorageOSD],
    timeout: float,
) -> bool:
    now = datetime.now(UTC)
    changed = False
    for record in osd_records:
        if record.phase not in {"HostPrepared", "Binding", "Expanding", "Shrinking"}:
            continue
        changed_at = (
            _storage_utc(record.phase_changed_at)
            or _storage_utc(record.last_seen_at)
            or _storage_utc(record.created_at)
        )
        if changed_at is None:
            continue
        age = (now - changed_at).total_seconds()
        if age < STORAGE_OSD_STALE_PHASE_SECONDS:
            continue
        await patch_storage_osd_status(
            kube,
            osd=record,
            status={
                "phase": "Failed",
                "last_error": (
                    f"OSD admission timed out after {int(age)}s in phase "
                    f"{record.phase}; inspect Rook OSD pods, the Bertrand "
                    "OSD CSI driver, and this record's PVC binding"
                ),
            },
            timeout=timeout,
        )
        changed = True
    return changed


async def _refresh_osd_readiness(
    kube: Kube,
    *,
    osd_records: Collection[CephStorageOSD],
    timeout: float,
) -> bool:
    try:
        live = await ceph_osds(timeout=timeout)
    except (OSError, TimeoutError):
        return False
    verified = {osd.osd_id for osd in live if osd.up and osd.in_cluster}
    changed = False
    for record in osd_records:
        osd_id = record.ceph_osd_id
        if osd_id is None or record.phase not in {"Binding", "Ready", "Expanding"}:
            continue
        if osd_id in verified and record.phase != "Ready":
            await patch_storage_osd_status(
                kube,
                osd=record,
                status={"phase": "Ready", "last_error": ""},
                timeout=timeout,
            )
            changed = True
        elif osd_id not in verified and record.phase == "Ready":
            await patch_storage_osd_status(
                kube,
                osd=record,
                status={
                    "phase": "Binding",
                    "last_error": (
                        f"Ceph osd.{osd_id} is no longer verified as up and in"
                    ),
                },
                timeout=timeout,
            )
            changed = True
    return changed


async def _reconcile_reservations(
    kube: Kube,
    *,
    policy: CephStorageStateRecord,
    reservations: Collection[CephStorageReservation],
    osd_records: Collection[CephStorageOSD],
    capacity: CephCapacitySnapshot,
    deadline: Deadline,
) -> bool:
    now = datetime.now(UTC)
    changed = False
    try:
        health_clean, health_detail, health_status = await ceph_health(
            timeout=deadline.remaining()
        )
        health_error = "" if health_clean else health_detail or health_status
    except (OSError, TimeoutError) as err:
        health_error = str(err)
    growth = _storage_growth_status(
        policy=policy,
        capacity=capacity,
        reservations=reservations,
        now=now,
    )
    free_bytes = growth.free_bytes or 0
    ready_possible = (
        not health_error
        and not _storage_osd_admission_in_flight(osd_records)
        and free_bytes >= growth.headroom_target_bytes + growth.reserved_bytes
    )
    for reservation in reservations:
        if reservation.phase in {"Released", "Expired", "Failed"}:
            continue
        if reservation.expires_at_utc() <= now:
            await patch_storage_reservation_status(
                kube,
                reservation=reservation,
                status={
                    "phase": "Expired",
                    "released_at": now.isoformat(),
                    "observed_free_bytes": free_bytes,
                    "last_error": "reservation expired before it was released",
                },
                timeout=deadline.remaining(),
            )
            changed = True
            continue
        if ready_possible and reservation.phase != "Ready":
            await patch_storage_reservation_status(
                kube,
                reservation=reservation,
                status={
                    "phase": "Ready",
                    "ready_at": now.isoformat(),
                    "observed_free_bytes": free_bytes,
                    "last_error": "",
                },
                timeout=deadline.remaining(),
            )
            changed = True
        elif not ready_possible and reservation.phase == "Pending":
            blockers: list[str] = []
            if health_error:
                blockers.append(f"Ceph health is not clean: {health_error}")
            if _storage_osd_admission_in_flight(osd_records):
                blockers.append("OSD admission or retirement is still in flight")
            required_free = growth.headroom_target_bytes + growth.reserved_bytes
            if free_bytes < required_free:
                blockers.append("free capacity is below reservation-adjusted headroom")
            await patch_storage_reservation_status(
                kube,
                reservation=reservation,
                status={
                    "phase": "Pending",
                    "observed_free_bytes": free_bytes,
                    "last_error": "; ".join(blockers),
                },
                timeout=deadline.remaining(),
            )
            changed = True
    return changed


async def _plan_storage_actions(
    kube: Kube,
    *,
    policy: CephStorageStateRecord,
    actions: Collection[CephStorageActionRecord],
    reports: Collection[CephStorageNodeReport],
    osd_records: Collection[CephStorageOSD],
    reservations: Collection[CephStorageReservation],
    capacity: CephCapacitySnapshot,
    loop_offload_offset: int,
    deadline: Deadline,
) -> tuple[list[CephStorageActionSpec], CephStoragePolicyStatus, int]:
    now = datetime.now(UTC)
    next_loop_offload_offset = loop_offload_offset
    min_growth_bytes = parse_size_bytes(policy.spec.min_growth_step)
    growth = _storage_growth_status(
        policy=policy,
        capacity=capacity,
        reservations=reservations,
        now=now,
    )
    ready_nodes = await _ready_storage_nodes(kube, timeout=deadline.remaining())
    eligible_nodes = _eligible_storage_nodes(
        ready_nodes=ready_nodes,
        reports=reports,
        actions=actions,
        osds=osd_records,
        growth_bytes=min_growth_bytes,
    )
    planned = _plan_storage_grow_actions(
        policy=policy,
        capacity=capacity,
        actions=actions,
        osd_records=osd_records,
        eligible_nodes=eligible_nodes,
        growth=growth,
        min_growth_bytes=min_growth_bytes,
    )
    if not planned and capacity.total_bytes > 0:
        planned = _plan_lvm_coverage_actions(
            policy=policy,
            actions=actions,
            osd_records=osd_records,
            eligible_nodes=eligible_nodes,
        )

    shrink_candidates = []
    lvm_reclaimable_bytes = 0
    lvm_shrink_candidate = ""
    lvm_shrink_target_bytes = 0
    if not planned and policy.spec.enabled and policy.spec.shrink_enabled:
        health_clean, _health_detail, _health_status = await ceph_health(
            timeout=deadline.remaining()
        )
        if health_clean:
            live_osds = await ceph_osds(timeout=deadline.remaining())
            shrink_candidates = _managed_loop_osds(
                osd_records=osd_records,
                osds=live_osds,
            )
            lvm_shrink_candidates = _lvm_osds_for_shrink(
                osd_records=osd_records,
                osds=live_osds,
            )
            (
                lvm_reclaimable_bytes,
                lvm_shrink_candidate,
                lvm_shrink_target_bytes,
            ) = _lvm_shrink_preview(
                policy=policy,
                capacity=capacity,
                growth=growth,
                lvm_osds=lvm_shrink_candidates,
            )
            planned, next_loop_offload_offset = _plan_loop_offload_action(
                policy=policy,
                capacity=capacity,
                actions=actions,
                eligible_nodes=eligible_nodes,
                candidates=shrink_candidates,
                growth_bytes=min_growth_bytes,
                offset=loop_offload_offset,
            )
            if not planned and capacity.used_ratio < policy.spec.low_watermark:
                planned = _plan_loop_shrink_action(
                    policy=policy,
                    capacity=capacity,
                    actions=actions,
                    candidates=shrink_candidates,
                )
            if not planned:
                planned = _plan_lvm_shrink_action(
                    policy=policy,
                    capacity=capacity,
                    actions=actions,
                    osd_records=osd_records,
                    growth=growth,
                    lvm_candidates=lvm_shrink_candidates,
                    loop_candidates=shrink_candidates,
                )
    status = _storage_policy_status(
        capacity=capacity,
        actions=actions,
        osd_records=osd_records,
        growth=growth,
        missing_lvm_osd_pvs=sum(
            1
            for target in eligible_nodes
            if target["operation"] == "expand-lvm"
            and target["current_bytes"] <= 0
        ),
        shrink_candidate_count=len(shrink_candidates),
        lvm_reclaimable_bytes=lvm_reclaimable_bytes,
        lvm_shrink_candidate=lvm_shrink_candidate,
        lvm_shrink_target_bytes=lvm_shrink_target_bytes,
    )
    return planned, status, next_loop_offload_offset


def _storage_policy_status(
    *,
    capacity: CephCapacitySnapshot,
    actions: Collection[CephStorageActionRecord],
    osd_records: Collection[CephStorageOSD],
    growth: CephStoragePolicyStatus,
    missing_lvm_osd_pvs: int,
    shrink_candidate_count: int,
    lvm_reclaimable_bytes: int,
    lvm_shrink_candidate: str,
    lvm_shrink_target_bytes: int,
) -> CephStoragePolicyStatus:
    counts = _storage_action_counts(actions)
    managed, loop, lvm, elastic_bytes, durable_bytes = _storage_osd_counts(
        osd_records
    )
    return growth.model_copy(
        update={
            "total_bytes": capacity.total_bytes,
            "used_bytes": capacity.used_bytes,
            "used_ratio": capacity.used_ratio,
            "pending_actions": counts.get("Pending", 0),
            "running_actions": counts.get("Running", 0),
            "succeeded_actions": counts.get("Succeeded", 0),
            "failed_actions": counts.get("Failed", 0),
            "managed_osds": managed,
            "loop_osds": loop,
            "lvm_osds": lvm,
            "elastic_bytes": elastic_bytes,
            "durable_bytes": durable_bytes,
            "lvm_preferred": lvm > 0,
            "shrink_candidates": shrink_candidate_count,
            "missing_lvm_osd_pvs": missing_lvm_osd_pvs,
            "lvm_reclaimable_bytes": lvm_reclaimable_bytes,
            "lvm_shrink_candidate": lvm_shrink_candidate,
            "lvm_shrink_target_bytes": lvm_shrink_target_bytes,
            "last_shrink_at": _last_storage_shrink_at(actions),
            "last_reconciled_at": datetime.now(UTC),
            "last_error": "",
        },
    )


async def _read_ceph_capacity(
    *,
    deadline: Deadline,
) -> tuple[CephCapacitySnapshot, str]:
    try:
        return await ceph_df(timeout=deadline.remaining()), ""
    except (OSError, TimeoutError) as err:
        return (
            CephCapacitySnapshot(total_bytes=0, used_bytes=0, used_ratio=1.0),
            str(err),
        )


async def _reconcile_storage_controller(
    kube: Kube,
    *,
    deadline: Deadline,
    loop_offload_offset: int,
) -> tuple[float, int]:
    policy = await read_storage_state(kube, timeout=deadline.remaining())
    actions = await STORAGE_ACTION_RESOURCE.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        timeout=deadline.remaining(),
    )
    reservations = sorted(
        policy.status.reservations.values(),
        key=lambda item: item.name,
    )
    reports = sorted(policy.status.nodes.values(), key=lambda item: item.name)
    osd_records = sorted(policy.status.osds.values(), key=lambda item: item.name)

    if await _mark_stale_actions_failed(
        kube,
        actions=actions,
        timeout=deadline.remaining(),
    ):
        actions = await STORAGE_ACTION_RESOURCE.list(
            kube,
            namespace=BERTRAND_NAMESPACE,
            timeout=deadline.remaining(),
        )
    if await _mark_stale_osds_failed(
        kube,
        osd_records=osd_records,
        timeout=deadline.remaining(),
    ):
        storage = await read_storage_state(kube, timeout=deadline.remaining())
        osd_records = sorted(storage.status.osds.values(), key=lambda item: item.name)
    await _patch_rook_device_sets(
        kube,
        records=osd_records,
        timeout=deadline.remaining(),
    )
    capacity, capacity_error = await _read_ceph_capacity(deadline=deadline)
    if await _refresh_osd_readiness(
        kube,
        osd_records=osd_records,
        timeout=deadline.remaining(),
    ):
        storage = await read_storage_state(kube, timeout=deadline.remaining())
        osd_records = sorted(storage.status.osds.values(), key=lambda item: item.name)
    if await _reconcile_reservations(
        kube,
        policy=policy,
        reservations=reservations,
        osd_records=osd_records,
        capacity=capacity,
        deadline=deadline,
    ):
        storage = await read_storage_state(kube, timeout=deadline.remaining())
        reservations = sorted(
            storage.status.reservations.values(),
            key=lambda item: item.name,
        )

    planned, status, next_loop_offload_offset = await _plan_storage_actions(
        kube,
        policy=policy,
        actions=actions,
        reports=reports,
        osd_records=osd_records,
        reservations=reservations,
        capacity=capacity,
        loop_offload_offset=loop_offload_offset,
        deadline=deadline,
    )

    if planned:
        await create_storage_actions(
            kube,
            actions=planned,
            timeout=deadline.remaining(),
        )
        actions = await STORAGE_ACTION_RESOURCE.list(
            kube,
            namespace=BERTRAND_NAMESPACE,
            timeout=deadline.remaining(),
        )
        storage = await read_storage_state(kube, timeout=deadline.remaining())
        osd_records = sorted(storage.status.osds.values(), key=lambda item: item.name)
        counts = _storage_action_counts(actions)
        managed, loop, lvm, elastic_bytes, durable_bytes = _storage_osd_counts(
            osd_records
        )
        status = status.model_copy(
            update={
                "pending_actions": counts.get("Pending", 0),
                "running_actions": counts.get("Running", 0),
                "succeeded_actions": counts.get("Succeeded", 0),
                "failed_actions": counts.get("Failed", 0),
                "managed_osds": managed,
                "loop_osds": loop,
                "lvm_osds": lvm,
                "elastic_bytes": elastic_bytes,
                "durable_bytes": durable_bytes,
                "lvm_preferred": lvm > 0,
                "shrink_candidates": (
                    0
                    if any(
                        action.operation in {"retire-loop", "shrink-lvm"}
                        for action in planned
                    )
                    else status.shrink_candidates
                ),
                "last_shrink_at": _last_storage_shrink_at(actions),
            },
        )

    storage = await read_storage_state(kube, timeout=deadline.remaining())
    await STORAGE_STATE_RESOURCE.patch_status(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=STORAGE_STATE_NAME,
        status=storage.status.model_copy(
            update={
                "policy": status.model_copy(
                    update={"observed_generation": policy.generation}
                )
            },
        ),
        timeout=deadline.remaining(),
    )
    if capacity_error and not planned:
        raise OSError(capacity_error)
    return float(policy.spec.reconcile_interval_seconds), next_loop_offload_offset


async def _maybe_repository_volume_gc(
    kube: Kube,
    *,
    clock: MaintenanceClock,
    deadline: Deadline,
) -> None:
    now = datetime.now(UTC)
    pass_deadline = clock.pass_deadline(
        now,
        deadline=deadline,
        timeout=REPOSITORY_VOLUME_GC_TIMEOUT_SECONDS,
    )
    if pass_deadline is None:
        return
    try:
        next_run = await next_repository_volume_gc_time(
            kube,
            timeout=pass_deadline.remaining(),
        )
        if next_run is None:
            clock.schedule_after(REPOSITORY_VOLUME_GC_EMPTY_CHECK_SECONDS)
            return
        if next_run > now:
            clock.schedule_at(next_run)
            return
        await gc_repository_volumes(kube, timeout=pass_deadline.remaining())
        clock.schedule_after(REPOSITORY_VOLUME_GC_READY_CHECK_SECONDS)
    except (OSError, TimeoutError, ValueError) as err:
        clock.schedule_after(REPOSITORY_VOLUME_GC_FAILURE_RETRY_SECONDS)
        print(
            f"bertrand: warning: repository volume garbage collection failed: {err}",
            file=sys.stderr,
        )


async def _maybe_repository_snapshot_maintenance(
    kube: Kube,
    *,
    clock: MaintenanceClock,
    deadline: Deadline,
) -> None:
    now = datetime.now(UTC)
    pass_deadline = clock.pass_deadline(
        now,
        deadline=deadline,
        timeout=REPOSITORY_SNAPSHOT_TIMEOUT_SECONDS,
    )
    if pass_deadline is None:
        return
    try:
        next_run = await next_repository_snapshot_time(
            kube,
            timeout=pass_deadline.remaining(),
        )
        if next_run is None:
            clock.schedule_after(REPOSITORY_SNAPSHOT_EMPTY_CHECK_SECONDS)
            return
        if next_run > now:
            clock.schedule_at(next_run)
            return
        await maintain_repository_snapshots(kube, timeout=pass_deadline.remaining())
        clock.schedule_after(REPOSITORY_SNAPSHOT_READY_CHECK_SECONDS)
    except (OSError, TimeoutError, ValueError) as err:
        clock.schedule_after(REPOSITORY_SNAPSHOT_FAILURE_RETRY_SECONDS)
        print(
            f"bertrand: warning: repository snapshot maintenance failed: {err}",
            file=sys.stderr,
        )


async def _patch_storage_controller_error(
    kube: Kube,
    *,
    error: str,
    deadline: Deadline,
) -> None:
    storage = await read_storage_state(kube, timeout=deadline.remaining())
    policy = CephStoragePolicyStatus.model_validate(
        {
            "observedGeneration": storage.generation,
            "total_bytes": None,
            "used_bytes": None,
            "used_ratio": None,
            "free_bytes": None,
            "headroom_target_bytes": 0,
            "reserved_bytes": 0,
            "write_rate_ewma_bytes_per_second": 0.0,
            "projected_seconds_to_headroom_floor": None,
            "growth_recommendation_bytes": 0,
            "pending_actions": 0,
            "running_actions": 0,
            "succeeded_actions": 0,
            "failed_actions": 0,
            "managed_osds": 0,
            "loop_osds": 0,
            "lvm_osds": 0,
            "elastic_bytes": 0,
            "durable_bytes": 0,
            "lvm_preferred": False,
            "shrink_candidates": 0,
            "missing_lvm_osd_pvs": 0,
            "lvm_reclaimable_bytes": 0,
            "lvm_shrink_candidate": "",
            "lvm_shrink_target_bytes": 0,
            "last_shrink_at": None,
            "last_reconciled_at": datetime.now(UTC).isoformat(),
            "last_error": error,
        }
    )
    await STORAGE_STATE_RESOURCE.patch_status(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=STORAGE_STATE_NAME,
        status=CephStorageStateStatus(
            policy=policy,
            reservations=storage.status.reservations,
            nodes=storage.status.nodes,
            osds=storage.status.osds,
        ),
        timeout=deadline.remaining(),
    )


async def run_ceph_storage_controller(*, timeout: float = INFINITY) -> None:
    """Run the Ceph storage controller loop until cancelled or timed out.

    Parameters
    ----------
    timeout : float, default=INFINITY
        Maximum controller runtime in seconds.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive or the loop exceeds the budget.
    asyncio.CancelledError
        If the surrounding task is cancelled.
    """
    if timeout <= 0:
        msg = "controller timeout must be positive"
        raise TimeoutError(msg)
    deadline = Deadline.from_timeout(
        timeout,
        message="controller timeout must be positive",
    )
    wake = asyncio.Event()
    wake.set()
    loop_offload_offset = 0
    repository_volume_gc = MaintenanceClock()
    repository_snapshot = MaintenanceClock()
    with Kube.inside_cluster() as kube:
        async with asyncio.TaskGroup() as group:
            for client, context in storage_watch_targets():
                group.create_task(
                    _watch_storage_resource(
                        kube,
                        client=client,
                        wake=wake,
                        deadline=deadline,
                        context=context,
                    )
                )
            interval = STORAGE_CONTROLLER_DEFAULT_RECONCILE_SECONDS
            while True:
                if not wake.is_set():
                    wait_timeout = deadline.bounded(interval)
                    with suppress(TimeoutError):
                        await asyncio.wait_for(wake.wait(), timeout=wait_timeout)
                wake.clear()
                interval = STORAGE_CONTROLLER_DEFAULT_RECONCILE_SECONDS
                error: str | None = None
                try:
                    interval, loop_offload_offset = await _reconcile_storage_controller(
                        kube,
                        deadline=deadline,
                        loop_offload_offset=loop_offload_offset,
                    )
                except asyncio.CancelledError:
                    raise
                except TimeoutError as err:
                    if deadline.remaining() <= 0:
                        raise
                    error = str(err)
                except (OSError, ValueError, RuntimeError) as err:
                    error = str(err)
                if error is not None:
                    with suppress(OSError, TimeoutError, ValueError):
                        await _patch_storage_controller_error(
                            kube,
                            error=error,
                            deadline=deadline,
                        )
                await _maybe_repository_volume_gc(
                    kube,
                    clock=repository_volume_gc,
                    deadline=deadline,
                )
                await _maybe_repository_snapshot_maintenance(
                    kube,
                    clock=repository_snapshot,
                    deadline=deadline,
                )


class CephStorageAgent:
    """DaemonSet agent role for node-local Ceph capacity mutation."""

    def __init__(self, *, node_name: str | None = None) -> None:
        self.node_name = node_name or self.resolve_node_name()
        self.host_id = host_id_from_host_state()

    @staticmethod
    def resolve_node_name() -> str:
        """Resolve the Kubernetes node name for this agent process.

        Returns
        -------
        str
            Resolved Kubernetes node name.

        Raises
        ------
        OSError
            If no node name can be inferred from the process environment.
        """
        name = os.environ.get("NODE_NAME", "").strip()
        if name:
            return name
        name = sys.argv[2].strip() if len(sys.argv) > 2 else ""
        if name:
            return name
        name = platform.node().strip()
        if name:
            return name
        msg = "Ceph storage controller agent could not resolve NODE_NAME"
        raise OSError(msg)

    async def _watch_actions(
        self,
        kube: Kube,
        *,
        wake: asyncio.Event,
        deadline: Deadline,
    ) -> None:
        action_client = STORAGE_ACTION_RESOURCE
        while True:
            try:
                async for event in action_client.watch(
                    kube,
                    namespace=BERTRAND_NAMESPACE,
                    timeout=deadline.remaining(),
                    emit_initial=True,
                ):
                    action = event.object
                    if (
                        action.spec.host_id == self.host_id
                        and action.status.phase == "Pending"
                    ):
                        wake.set()
                wake.set()
                await asyncio.sleep(
                    deadline.bounded(STORAGE_WATCH_RESTART_DELAY_SECONDS)
                )
            except asyncio.CancelledError:
                raise
            except (OSError, RuntimeError, ValueError) as err:
                print(
                    "bertrand: warning: Ceph storage controller action watch "
                    f"failed: {err}",
                    file=sys.stderr,
                )
                wake.set()
                await asyncio.sleep(
                    deadline.bounded(STORAGE_WATCH_RESTART_DELAY_SECONDS)
                )

    async def _upsert_node_report(self, kube: Kube, *, deadline: Deadline) -> None:
        """Report current host free capacity for this node."""
        try:
            status = await host_capacity_snapshot(timeout=deadline.remaining())
        except OSError as err:
            status = {
                "free_bytes": 0,
                "path": "",
                "lvm_free_bytes": 0,
                "lvm_pvs": [],
                "lvm_pv_inventory": [],
                "loop_fallback_active": False,
                "heartbeat_at": datetime.now(UTC).isoformat(),
                "last_error": str(err),
            }
        await upsert_storage_node_report(
            kube,
            node_name=self.node_name,
            host_id=self.host_id,
            status=status,
            timeout=deadline.remaining(),
        )

    async def _recover_loop_devices(self, kube: Kube, *, deadline: Deadline) -> None:
        """Best-effort recreation of this node's loop fallback device."""
        storage = await read_storage_state(kube, timeout=deadline.remaining())
        records = sorted(storage.status.osds.values(), key=lambda item: item.name)
        for record in records:
            if (
                record.node_name != self.node_name
                or record.host_id != self.host_id
                or record.origin != "loop-fallback"
                or record.phase
                not in {"HostPrepared", "Binding", "Ready", "Expanding"}
            ):
                continue
            try:
                await prepare_loop_fallback_osd(
                    name=record.name,
                    target_bytes=record.target_bytes,
                    timeout=deadline.remaining(),
                )
            except (OSError, TimeoutError, ValueError) as err:
                await patch_storage_osd_status(
                    kube,
                    osd=record,
                    status={
                        "phase": "Failed",
                        "last_error": str(err),
                    },
                    timeout=deadline.remaining(),
                )

    async def _recover_missing_osd_records(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
    ) -> None:
        """Reconstruct OSD records from live Bertrand host substrates."""
        storage = await read_storage_state(kube, timeout=deadline.remaining())
        existing = {
            record.name: record
            for record in storage.status.osds.values()
        }
        for name, prepared in await discover_lvm_osds(timeout=deadline.remaining()):
            if name in existing:
                continue
            spec = _osd_spec(
                name=name,
                origin="lvm-pv",
                node_name=self.node_name,
                host_id=self.host_id,
                prepared=prepared,
                target_bytes=prepared.observed_bytes,
            )
            await upsert_storage_osd(
                kube,
                name=name,
                spec=spec,
                phase="HostPrepared",
                timeout=deadline.remaining(),
            )
        loop_name = storage_loop_osd_name(self.host_id)
        if loop_name in existing:
            return
        prepared = await discover_loop_fallback_osd(
            name=loop_name,
            timeout=deadline.remaining(),
        )
        if prepared is None:
            return
        spec = _osd_spec(
            name=loop_name,
            origin="loop-fallback",
            node_name=self.node_name,
            host_id=self.host_id,
            prepared=prepared,
            target_bytes=prepared.observed_bytes,
        )
        await upsert_storage_osd(
            kube,
            name=loop_name,
            spec=spec,
            phase="HostPrepared",
            timeout=deadline.remaining(),
        )

    async def _pending_actions(
        self, kube: Kube, *, deadline: Deadline
    ) -> list[CephStorageActionRecord]:
        """List pending actions assigned to this node.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Event-loop deadline for this synchronization pass.

        Returns
        -------
        list[CephStorageActionRecord]
            Pending actions targeting this agent's node.
        """
        return [
            action
            for action in await pending_storage_actions(
                kube,
                node_name=self.node_name,
                timeout=deadline.remaining(),
            )
            if action.spec.host_id == self.host_id
        ]

    async def _patch_action(
        self,
        kube: Kube,
        *,
        action: CephStorageActionRecord,
        status: Mapping[str, object],
        timeout: float,
    ) -> None:
        """Patch the status for one assigned action."""
        await patch_storage_action_status(
            kube,
            action=action,
            status=status,
            timeout=timeout,
        )

    @staticmethod
    def _shrink_osd_id(action: CephStorageActionRecord) -> int:
        if action.spec.osd_id is None:
            msg = "retire-loop action is missing osd_id"
            raise ValueError(msg)
        return action.spec.osd_id

    @staticmethod
    def _target_bytes(action: CephStorageActionRecord) -> int:
        if action.spec.target_bytes is None:
            msg = f"{action.spec.operation} action is missing target_bytes"
            raise ValueError(msg)
        return action.spec.target_bytes

    @staticmethod
    def _lvm_pv_name(action: CephStorageActionRecord) -> str:
        pv_name = (action.spec.pv_name or "").strip()
        if not pv_name:
            msg = "expand-lvm action is missing pv_name"
            raise ValueError(msg)
        return pv_name

    def _storage_osd_name(self, action: CephStorageActionRecord) -> str:
        name = (action.spec.storage_osd_name or "").strip()
        if name:
            return name
        if action.spec.operation == "expand-loop":
            return storage_loop_osd_name(action.spec.host_id or self.host_id)
        if action.spec.operation == "expand-lvm":
            pv_name = self._lvm_pv_name(action)
            return storage_lvm_osd_name(action.spec.host_id or self.host_id, pv_name)
        msg = f"{action.spec.operation} action does not target an OSD record"
        raise ValueError(msg)

    @staticmethod
    def _missing_loop_record(osd_id: int) -> OSError:
        msg = f"could not find managed loop fallback record for osd.{osd_id}"
        return OSError(msg)

    @staticmethod
    def _missing_lvm_record(name: str) -> OSError:
        msg = f"could not find managed LVM OSD record {name!r}"
        return OSError(msg)

    @staticmethod
    def _invalid_lvm_shrink_target(target_bytes: int, current_bytes: int) -> ValueError:
        msg = (
            f"shrink target {target_bytes} must be smaller than "
            f"current target {current_bytes}"
        )
        return ValueError(msg)

    @staticmethod
    def _mismatched_lvm_osd_id(
        name: str,
        current_osd_id: int | None,
        expected_osd_id: int,
    ) -> OSError:
        msg = (
            f"LVM OSD {name!r} currently maps to osd.{current_osd_id}; "
            f"action expected osd.{expected_osd_id}"
        )
        return OSError(msg)

    async def _claim_action(
        self,
        kube: Kube,
        *,
        action: CephStorageActionRecord,
        deadline: Deadline,
    ) -> None:
        await self._patch_action(
            kube,
            action=action,
            status={
                "phase": "Running",
                "message": "action claimed by node agent",
                "worker_node": self.node_name,
                "started_at": datetime.now(UTC).isoformat(),
            },
            timeout=deadline.remaining(),
        )

    async def _succeed_action(
        self,
        kube: Kube,
        *,
        action: CephStorageActionRecord,
        status: Mapping[str, object],
        deadline: Deadline,
    ) -> None:
        await self._patch_action(
            kube,
            action=action,
            status={
                "phase": "Succeeded",
                "worker_node": self.node_name,
                **dict(status),
                "finished_at": datetime.now(UTC).isoformat(),
            },
            timeout=deadline.remaining(),
        )

    async def _fail_action(
        self,
        kube: Kube,
        *,
        action: CephStorageActionRecord,
        error: BaseException,
        deadline: Deadline,
    ) -> None:
        await self._patch_action(
            kube,
            action=action,
            status={
                "phase": "Failed",
                "message": str(error),
                "diagnostics": str(error),
                "worker_node": self.node_name,
                "finished_at": datetime.now(UTC).isoformat(),
            },
            timeout=deadline.remaining(),
        )

    async def _osd_by_name(
        self,
        kube: Kube,
        *,
        name: str,
        deadline: Deadline,
    ) -> CephStorageOSD | None:
        storage = await read_storage_state(kube, timeout=deadline.remaining())
        return next(
            (
                item
                for item in storage.status.osds.values()
                if item.name == name
            ),
            None,
        )

    async def _lvm_osd_by_name(
        self,
        kube: Kube,
        *,
        name: str,
        deadline: Deadline,
    ) -> CephStorageOSD | None:
        storage = await read_storage_state(kube, timeout=deadline.remaining())
        return next(
            (
                item
                for item in storage.status.osds.values()
                if item.name == name
                and item.origin == "lvm-pv"
                and item.host_id == self.host_id
            ),
            None,
        )

    async def _loop_osd_by_id(
        self,
        kube: Kube,
        *,
        osd_id: int,
        deadline: Deadline,
    ) -> CephStorageOSD | None:
        storage = await read_storage_state(kube, timeout=deadline.remaining())
        return next(
            (
                item
                for item in storage.status.osds.values()
                if item.origin == "loop-fallback"
                and item.ceph_osd_id == osd_id
            ),
            None,
        )

    async def _patch_rook_current_osds(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
    ) -> None:
        storage = await read_storage_state(kube, timeout=deadline.remaining())
        records = sorted(storage.status.osds.values(), key=lambda item: item.name)
        await _patch_rook_device_sets(
            kube,
            records=records,
            timeout=deadline.remaining(),
        )

    async def _admit_prepared_osd(
        self,
        kube: Kube,
        *,
        name: str,
        origin: StorageOSDOrigin,
        prepared: PreparedOSD,
        target_bytes: int,
        phase: StorageOSDPhase,
        resize_claim: bool,
        deadline: Deadline,
    ) -> tuple[CephStorageOSD, int | None, bool]:
        spec = _osd_spec(
            name=name,
            origin=origin,
            node_name=self.node_name,
            host_id=self.host_id,
            prepared=prepared,
            target_bytes=target_bytes,
        )
        record = await upsert_storage_osd(
            kube,
            name=name,
            spec=spec,
            phase=phase,
            timeout=deadline.remaining(),
        )
        await self._patch_rook_current_osds(kube, deadline=deadline)
        if resize_claim:
            await _resize_osd_claim(
                kube,
                record=record,
                timeout=deadline.remaining(),
            )
        observed_id, ready = await _observe_rook_osd(
            kube,
            record=record,
            timeout=deadline.remaining(),
        )
        await patch_storage_osd_status(
            kube,
            osd=record,
            status={
                "phase": "Ready" if ready else "Binding",
                "observed_bytes": prepared.observed_bytes,
                "ceph_osd_id": observed_id,
                "last_error": "",
            },
            timeout=deadline.remaining(),
        )
        return record, observed_id, ready

    async def _mark_target_osd_failed(
        self,
        kube: Kube,
        *,
        action: CephStorageActionRecord,
        error: BaseException,
        deadline: Deadline,
    ) -> None:
        if action.spec.operation not in {"expand-lvm", "expand-loop", "shrink-lvm"}:
            return
        name = self._storage_osd_name(action)
        record = await self._osd_by_name(kube, name=name, deadline=deadline)
        if record is None:
            return
        await patch_storage_osd_status(
            kube,
            osd=record,
            status={
                "phase": "Failed",
                "last_error": str(error),
            },
            timeout=deadline.remaining(),
        )

    async def _execute_expand_lvm(
        self,
        kube: Kube,
        *,
        action: CephStorageActionRecord,
        deadline: Deadline,
    ) -> None:
        pv_name = self._lvm_pv_name(action)
        name = self._storage_osd_name(action)
        target_bytes = self._target_bytes(action)
        existing = await self._osd_by_name(kube, name=name, deadline=deadline)
        prepared = await prepare_lvm_osd(
            name=name,
            target_bytes=target_bytes,
            pv_name=pv_name,
            lv_name=action.spec.lv_name,
            timeout=deadline.remaining(),
        )
        phase: StorageOSDPhase = "Expanding" if existing is not None else "HostPrepared"
        _, observed_id, ready = await self._admit_prepared_osd(
            kube,
            name=name,
            origin="lvm-pv",
            prepared=prepared,
            target_bytes=target_bytes,
            phase=phase,
            resize_claim=True,
            deadline=deadline,
        )
        await self._succeed_action(
            kube,
            action=action,
            status={
                "message": (
                    "Rook LVM OSD expansion submitted"
                    if not ready
                    else "Rook LVM OSD expansion completed"
                ),
                "created_osd_ids": [] if observed_id is None else [observed_id],
                "osd_origin": "lvm-pv",
                "osd_quality": "durable",
                "source_pv": pv_name,
                "source_lv": prepared.lv_name,
                "provisioned_bytes": prepared.observed_bytes,
            },
            deadline=deadline,
        )

    async def _execute_expand_loop(
        self,
        kube: Kube,
        *,
        action: CephStorageActionRecord,
        deadline: Deadline,
    ) -> None:
        name = self._storage_osd_name(action)
        target_bytes = self._target_bytes(action)
        existing = await self._osd_by_name(kube, name=name, deadline=deadline)
        prepared = await prepare_loop_fallback_osd(
            name=name,
            target_bytes=target_bytes,
            timeout=deadline.remaining(),
        )
        phase: StorageOSDPhase = "Expanding" if existing is not None else "HostPrepared"
        _, observed_id, ready = await self._admit_prepared_osd(
            kube,
            name=name,
            origin="loop-fallback",
            prepared=prepared,
            target_bytes=target_bytes,
            phase=phase,
            resize_claim=True,
            deadline=deadline,
        )
        await self._succeed_action(
            kube,
            action=action,
            status={
                "message": (
                    "Rook loop fallback OSD expansion submitted"
                    if not ready
                    else "Rook loop fallback OSD expansion completed"
                ),
                "created_osd_ids": [] if observed_id is None else [observed_id],
                "osd_origin": "loop-fallback",
                "osd_quality": "elastic",
                "provisioned_bytes": prepared.observed_bytes,
            },
            deadline=deadline,
        )

    async def _execute_shrink_lvm(
        self,
        kube: Kube,
        *,
        action: CephStorageActionRecord,
        deadline: Deadline,
    ) -> None:
        name = self._storage_osd_name(action)
        target_bytes = self._target_bytes(action)
        old_osd_id = self._shrink_osd_id(action)
        record = await self._lvm_osd_by_name(kube, name=name, deadline=deadline)
        if record is None:
            raise self._missing_lvm_record(name)
        if target_bytes >= record.target_bytes:
            raise self._invalid_lvm_shrink_target(
                target_bytes,
                record.target_bytes,
            )
        if record.ceph_osd_id != old_osd_id:
            raise self._mismatched_lvm_osd_id(
                name,
                record.ceph_osd_id,
                old_osd_id,
            )
        await patch_storage_osd_status(
            kube,
            osd=record,
            status={
                "phase": "Shrinking",
                "last_error": "",
            },
            timeout=deadline.remaining(),
        )
        await self._patch_rook_current_osds(kube, deadline=deadline)
        await drain_ceph_osd(old_osd_id, timeout=deadline.remaining())
        await _wait_osd_workloads_gone(
            kube,
            record=record,
            timeout=deadline.remaining(),
        )
        await purge_ceph_osd(old_osd_id, timeout=deadline.remaining())
        await _delete_osd_claims(
            kube,
            record=record,
            timeout=deadline.remaining(),
        )
        await _wait_osd_claims_gone(
            kube,
            record=record,
            timeout=deadline.remaining(),
        )
        await delete_lvm_osd_substrate(
            lv_name=record.lv_name,
            block_path=record.block_path,
            timeout=deadline.remaining(),
        )
        prepared = await prepare_lvm_osd(
            name=name,
            target_bytes=target_bytes,
            pv_name=record.pv_name,
            lv_name=record.lv_name,
            timeout=deadline.remaining(),
        )
        record, observed_id, ready = await self._admit_prepared_osd(
            kube,
            name=name,
            origin="lvm-pv",
            prepared=prepared,
            target_bytes=target_bytes,
            phase="HostPrepared",
            resize_claim=False,
            deadline=deadline,
        )
        await self._succeed_action(
            kube,
            action=action,
            status={
                "message": (
                    "Rook LVM OSD shrink submitted"
                    if not ready
                    else "Rook LVM OSD shrink completed"
                ),
                "removed_osd_ids": [old_osd_id],
                "created_osd_ids": [] if observed_id is None else [observed_id],
                "osd_origin": "lvm-pv",
                "osd_quality": "durable",
                "source_pv": record.pv_name,
                "source_lv": prepared.lv_name,
                "provisioned_bytes": prepared.observed_bytes,
            },
            deadline=deadline,
        )

    async def _execute_retire_loop(
        self,
        kube: Kube,
        *,
        action: CephStorageActionRecord,
        deadline: Deadline,
    ) -> None:
        osd_id = self._shrink_osd_id(action)
        record = await self._loop_osd_by_id(kube, osd_id=osd_id, deadline=deadline)
        if record is None:
            raise self._missing_loop_record(osd_id)
        await patch_storage_osd_status(
            kube,
            osd=record,
            status={
                "phase": "Retiring",
                "last_error": "",
            },
            timeout=deadline.remaining(),
        )
        await drain_loop_osd(osd_id, timeout=deadline.remaining())
        await self._patch_rook_current_osds(kube, deadline=deadline)
        await _wait_osd_workloads_gone(
            kube,
            record=record,
            timeout=deadline.remaining(),
        )
        await purge_loop_osd(osd_id, timeout=deadline.remaining())
        await _delete_osd_claims(
            kube,
            record=record,
            timeout=deadline.remaining(),
        )
        await delete_loop_fallback_substrate(
            loop_file=record.loop_file,
            loop_device=record.loop_device,
            block_path=record.block_path,
            timeout=deadline.remaining(),
        )
        await patch_storage_osd_status(
            kube,
            osd=record,
            status={
                "phase": "Retired",
                "retired_at": datetime.now(UTC).isoformat(),
                "last_error": "",
            },
            timeout=deadline.remaining(),
        )
        await self._succeed_action(
            kube,
            action=action,
            status={
                "message": f"Rook loop fallback retirement completed for osd.{osd_id}",
                "removed_osd_ids": [osd_id],
            },
            deadline=deadline,
        )

    async def _execute_action(
        self, kube: Kube, *, action: CephStorageActionRecord, deadline: Deadline
    ) -> None:
        """Claim and execute one pending action on this node.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        action : CephStorageActionRecord
            Pending action assigned to this node.
        deadline : Deadline
            Event-loop deadline for this agent run.

        Raises
        ------
        asyncio.CancelledError
            If the surrounding task is cancelled.
        """
        try:
            await self._claim_action(kube, action=action, deadline=deadline)
            if action.spec.operation == "expand-lvm":
                await self._execute_expand_lvm(
                    kube,
                    action=action,
                    deadline=deadline,
                )
            elif action.spec.operation == "expand-loop":
                await self._execute_expand_loop(
                    kube,
                    action=action,
                    deadline=deadline,
                )
            elif action.spec.operation == "shrink-lvm":
                await self._execute_shrink_lvm(
                    kube,
                    action=action,
                    deadline=deadline,
                )
            else:
                await self._execute_retire_loop(
                    kube,
                    action=action,
                    deadline=deadline,
                )
        except asyncio.CancelledError:
            raise
        except (OSError, TimeoutError, ValueError, RuntimeError) as err:
            with suppress(OSError, TimeoutError, ValueError):
                await self._mark_target_osd_failed(
                    kube,
                    action=action,
                    error=err,
                    deadline=deadline,
                )
            await self._fail_action(
                kube,
                action=action,
                error=err,
                deadline=deadline,
            )

    async def sync(self, kube: Kube, *, deadline: Deadline) -> None:
        """Run one node-agent synchronization pass.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Event-loop deadline for this synchronization pass.
        """
        await self._recover_missing_osd_records(kube, deadline=deadline)
        await self._recover_loop_devices(kube, deadline=deadline)
        await self._upsert_node_report(kube, deadline=deadline)
        for action in await self._pending_actions(kube, deadline=deadline):
            await self._execute_action(kube, action=action, deadline=deadline)

    async def run(self, *, timeout: float = INFINITY) -> None:
        """Run the node agent loop until cancelled or timed out.

        Parameters
        ----------
        timeout : float, default=INFINITY
            Maximum agent runtime in seconds.

        Raises
        ------
        TimeoutError
            If `timeout` is non-positive or the loop exceeds the budget.
        """
        if timeout <= 0:
            msg = "agent timeout must be positive"
            raise TimeoutError(msg)
        deadline = Deadline.from_timeout(
            timeout,
            message="agent timeout must be positive",
        )
        wake = asyncio.Event()
        wake.set()
        with Kube.inside_cluster() as kube:
            async with asyncio.TaskGroup() as group:
                group.create_task(
                    self._watch_actions(kube, wake=wake, deadline=deadline)
                )
                while True:
                    if not wake.is_set():
                        wait_timeout = deadline.bounded(
                            STORAGE_AGENT_SYNC_INTERVAL_SECONDS
                        )
                        with suppress(TimeoutError):
                            await asyncio.wait_for(
                                wake.wait(),
                                timeout=wait_timeout,
                            )
                    wake.clear()
                    await self.sync(kube, deadline=deadline)


def main(argv: list[str] | None = None) -> int:
    """Entry point for control plane container role dispatch.

    Parameters
    ----------
    argv : list[str] | None, optional
        Command-line arguments without the executable name. If `None`, use
        `sys.argv[1:]`.

    Returns
    -------
    int
        Process exit code.
    """
    if argv is None:
        argv = sys.argv[1:]
    role = argv[0].strip().lower() if argv else "controller"
    if role not in {"controller", "agent"}:
        print(
            "usage: python -m bertrand.env.kube.ceph.storage [controller|agent]",
            file=sys.stderr,
        )
        return 2
    with asyncio.Runner() as runner:
        if role == "controller":
            runner.run(run_ceph_storage_controller(timeout=INFINITY))
        else:
            runner.run(CephStorageAgent().run(timeout=INFINITY))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
