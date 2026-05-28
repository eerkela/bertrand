"""Rook OSD resource mechanics for Bertrand-managed Ceph storage."""

from __future__ import annotations

import asyncio
from collections.abc import Collection, Mapping
from typing import TYPE_CHECKING

from bertrand.env.git import Deadline
from bertrand.env.kube.ceph.api import PreparedOSD, ceph_osds, kube_quantity
from bertrand.env.kube.ceph.bootstrap import (
    ROOK_CEPH_CLUSTER_RESOURCE,
    ROOK_CLUSTER_NAME,
    ROOK_NAMESPACE,
    ROOK_OSD_STORAGE_CLASS,
)
from bertrand.env.kube.ceph.capacity import (
    STORAGE_OSD_LABEL,
    STORAGE_OSD_LABEL_VALUE,
    STORAGE_OSD_NAME_LABEL,
    CephStorageOSD,
    storage_osd_resource_names,
)
from bertrand.env.kube.pod import Pod
from bertrand.env.kube.volume import PersistentVolumeClaim

if TYPE_CHECKING:
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
    timeout: float,
) -> None:
    """Patch the Rook CephCluster device sets for active Bertrand OSD records.

    Raises
    ------
    OSError
        If the existing CephCluster device sets include non-Bertrand-owned entries.
    """
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


async def resize_osd_claim(
    kube: Kube,
    *,
    record: CephStorageOSD,
    timeout: float,
) -> None:
    """Resize Rook PVCs that back one managed OSD record."""
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
                            "requests": {"storage": kube_quantity(record.target_bytes)}
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


async def delete_osd_claims(
    kube: Kube, *, record: CephStorageOSD, timeout: float
) -> None:
    """Delete Rook PVCs that back one managed OSD record."""
    claims = await PersistentVolumeClaim.list(
        kube,
        timeout=timeout,
        namespaces=(ROOK_NAMESPACE,),
        labels={STORAGE_OSD_NAME_LABEL: record.name},
    )
    for claim in claims:
        await claim.delete(kube, timeout=timeout)


async def wait_osd_claims_gone(
    kube: Kube,
    *,
    record: CephStorageOSD,
    timeout: float,
) -> None:
    """Wait until Rook PVCs for one managed OSD record are gone.

    Raises
    ------
    TimeoutError
        If the PVCs still exist when the timeout expires.
    """
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


async def wait_osd_workloads_gone(
    kube: Kube,
    *,
    record: CephStorageOSD,
    timeout: float,
) -> None:
    """Wait until Rook pods for one managed OSD record are gone.

    Raises
    ------
    TimeoutError
        If active Rook pods still exist when the timeout expires.
    """
    msg = f"timed out waiting for Rook workloads for OSD {record.name!r} to stop"
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


async def observe_rook_osd(
    kube: Kube,
    *,
    record: CephStorageOSD,
    timeout: float,
) -> tuple[int | None, bool]:
    """Observe the live Ceph identity/readiness for one managed Rook OSD.

    Returns
    -------
    tuple[int | None, bool]
        Observed Ceph OSD ID and whether the OSD is up and in the cluster.
    """
    observed_id = record.ceph_osd_id
    if timeout <= 0:
        return observed_id, False
    deadline = Deadline.from_timeout(
        timeout,
        message=f"Rook OSD observation timeout for {record.name!r} must be positive",
    )
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
