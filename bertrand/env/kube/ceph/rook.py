"""Rook OSD resource mechanics for Bertrand-managed Ceph storage."""

from __future__ import annotations

from collections.abc import Collection, Mapping
from typing import TYPE_CHECKING

from kubernetes import client as kube_client

from bertrand.env.kube.ceph.api import PreparedOSD, ceph_osds, kube_quantity
from bertrand.env.kube.ceph.bootstrap import (
    ROOK_CLUSTER_NAME,
    ROOK_NAMESPACE,
    ROOK_OSD_STORAGE_CLASS,
    RookCephCluster,
)
from bertrand.env.kube.ceph.capacity import (
    STORAGE_OSD_LABEL,
    STORAGE_OSD_LABEL_VALUE,
    STORAGE_OSD_NAME_LABEL,
    CephStorageOSD,
    storage_osd_resource_names,
)
from bertrand.env.kube.pod import POD_ACTIVE_PHASES, Pod
from bertrand.env.kube.volume import PersistentVolumeClaim

if TYPE_CHECKING:
    from bertrand.env.git import Deadline
    from bertrand.env.kube.api.client import Kube

STORAGE_OSD_WAIT_POLL_SECONDS = 2.0


def storage_osd_spec(
    *,
    name: str,
    origin: str,
    node_name: str,
    host_id: str,
    prepared: PreparedOSD,
    target_bytes: int,
) -> dict[str, object]:
    """Render a storage OSD spec from a prepared host substrate.

    Returns
    -------
    dict[str, object]
        Kubernetes custom-object spec payload for one storage OSD record.
    """
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


async def patch_rook_device_sets(
    kube: Kube,
    *,
    records: Collection[CephStorageOSD],
    deadline: Deadline,
) -> None:
    """Patch the Rook CephCluster device sets for active Bertrand OSD records.

    Raises
    ------
    OSError
        If the existing CephCluster device sets include non-Bertrand-owned entries.
    """
    current = await RookCephCluster.get(
        kube,
        name=ROOK_CLUSTER_NAME,
        deadline=deadline,
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
    api = kube_client.CustomObjectsApi(kube.client)
    await kube.run(
        lambda timeout: api.patch_namespaced_custom_object(
            group="ceph.rook.io",
            version="v1",
            namespace=ROOK_NAMESPACE,
            plural="cephclusters",
            name=ROOK_CLUSTER_NAME,
            body={"spec": {"storage": {"storageClassDeviceSets": device_sets}}},
            _request_timeout=timeout,
        ),
        deadline=deadline,
        context="failed to patch Rook CephCluster OSD device sets",
    )


async def resize_osd_claim(
    kube: Kube,
    *,
    record: CephStorageOSD,
    deadline: Deadline,
) -> None:
    """Resize Rook PVCs that back one managed OSD record."""
    claims = await PersistentVolumeClaim.list(
        kube,
        deadline=deadline,
        namespace=ROOK_NAMESPACE,
        labels={STORAGE_OSD_NAME_LABEL: record.name},
    )
    if not claims:
        return
    api = kube_client.CoreV1Api(kube.client)
    for claim in claims:
        claim_name = claim.name
        claim_namespace = claim.namespace

        def patch(
            request_timeout: float | None,
            *,
            claim_name: str = claim_name,
            claim_namespace: str = claim_namespace,
        ) -> object:
            return api.patch_namespaced_persistent_volume_claim(
                name=claim_name,
                namespace=claim_namespace,
                body={
                    "spec": {
                        "resources": {
                            "requests": {"storage": kube_quantity(record.target_bytes)}
                        }
                    }
                },
                _request_timeout=request_timeout,
            )

        await kube.run(
            patch,
            deadline=deadline,
            context=f"failed to resize OSD PVC {claim_namespace}/{claim_name}",
        )


async def delete_osd_claims(
    kube: Kube, *, record: CephStorageOSD, deadline: Deadline
) -> None:
    """Delete Rook PVCs that back one managed OSD record."""
    claims = await PersistentVolumeClaim.list(
        kube,
        deadline=deadline,
        namespace=ROOK_NAMESPACE,
        labels={STORAGE_OSD_NAME_LABEL: record.name},
    )
    for claim in claims:
        await claim.delete(
            kube,
            deadline=deadline,
        )


async def wait_osd_claims_gone(
    kube: Kube,
    *,
    record: CephStorageOSD,
    deadline: Deadline,
) -> None:
    """Wait until Rook PVCs for one managed OSD record are gone.

    Raises
    ------
    TimeoutError
        If the PVCs still exist when the timeout expires.
    """
    msg = f"timed out waiting for OSD PVCs for {record.name!r} to delete"
    while deadline.remaining > 0:
        claims = await PersistentVolumeClaim.list(
            kube,
            deadline=deadline,
            namespace=ROOK_NAMESPACE,
            labels={STORAGE_OSD_NAME_LABEL: record.name},
        )
        if not claims:
            return
        await deadline.sleep(STORAGE_OSD_WAIT_POLL_SECONDS)
    raise TimeoutError(msg)


async def wait_osd_workloads_gone(
    kube: Kube,
    *,
    record: CephStorageOSD,
    deadline: Deadline,
) -> None:
    """Wait until Rook pods for one managed OSD record are gone.

    Raises
    ------
    TimeoutError
        If active Rook pods still exist when the timeout expires.
    """
    msg = f"timed out waiting for Rook workloads for OSD {record.name!r} to stop"
    claim_names = {
        claim.name
        for claim in await PersistentVolumeClaim.list(
            kube,
            deadline=deadline,
            namespace=ROOK_NAMESPACE,
            labels={STORAGE_OSD_NAME_LABEL: record.name},
        )
    }
    while deadline.remaining > 0:
        pods = await Pod.list(
            kube,
            deadline=deadline,
            namespace=ROOK_NAMESPACE,
        )
        active = [
            pod.name
            for pod in pods
            if not pod.is_terminating
            and pod.phase in POD_ACTIVE_PHASES
            and (
                pod.labels.get("ceph.rook.io/DeviceSet") == record.device_set_name
                or pod.labels.get("ceph.rook.io/pvc") in claim_names
            )
        ]
        if not active:
            return
        await deadline.sleep(STORAGE_OSD_WAIT_POLL_SECONDS)
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


async def observe_rook_osd(
    kube: Kube,
    *,
    record: CephStorageOSD,
    deadline: Deadline,
) -> tuple[int | None, bool]:
    """Observe the live Ceph identity/readiness for one managed Rook OSD.

    Returns
    -------
    tuple[int | None, bool]
        Observed Ceph OSD ID and whether the OSD is up and in the cluster.
    """
    observed_id = record.ceph_osd_id
    if deadline.remaining <= 0:
        return observed_id, False
    while deadline.remaining > 0:
        claims = await PersistentVolumeClaim.list(
            kube,
            deadline=deadline,
            namespace=ROOK_NAMESPACE,
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
            deadline=deadline,
            namespace=ROOK_NAMESPACE,
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
            live = await ceph_osds(deadline=deadline)
        except (OSError, TimeoutError):
            await deadline.sleep(STORAGE_OSD_WAIT_POLL_SECONDS)
            continue
        if observed_id is not None:
            for osd in live:
                if osd.osd_id == observed_id and osd.up and osd.in_cluster:
                    return observed_id, True
        await deadline.sleep(STORAGE_OSD_WAIT_POLL_SECONDS)
    return observed_id, False
