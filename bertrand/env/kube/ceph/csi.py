"""Bertrand CSI driver for Rook OSD block substrates."""

from __future__ import annotations

import argparse
import asyncio
import importlib
import os
from concurrent import futures
from contextlib import suppress
from pathlib import Path
from typing import TYPE_CHECKING, Any

from bertrand.env.git import BERTRAND_NAMESPACE, Deadline
from bertrand.env.kube.api.client import (
    CLUSTER_REGISTRY_READY_LABEL,
    CLUSTER_REGISTRY_READY_VALUE,
    Kube,
)
from bertrand.env.kube.api.spec import ContainerSpec, PodTemplateSpec, VolumeSpec
from bertrand.env.kube.ceph.api import (
    bind_block_device,
    host_id_from_host_state,
    prepare_loop_fallback_osd,
    prepare_lvm_osd,
    unbind_block_device,
)
from bertrand.env.kube.ceph.capacity import (
    STORAGE_CONTROLLER_LABELS,
    STORAGE_OSD_NAME_LABEL,
    CephStorageOSD,
    patch_storage_osd_status,
    read_storage_state,
    upsert_storage_osd,
)
from bertrand.env.kube.daemonset import DaemonSet
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.volume import PersistentVolumeClaim

if TYPE_CHECKING:
    from collections.abc import Awaitable, Callable, Mapping

CSI_DRIVER_NAME = "osd.csi.bertrand.dev"
CSI_CONTROLLER_NAME = "bertrand-ceph-osd-csi-controller"
CSI_NODE_NAME = "bertrand-ceph-osd-csi-node"
CSI_CONTROLLER_SOCKET = "/run/bertrand-csi/csi.sock"
CSI_NODE_SOCKET = "/csi/csi.sock"
CSI_SOCKET_DIR = "/csi"
CSI_REGISTRATION_DIR = "/registration"
CSI_KUBELET_DIR = "/var/lib/kubelet"
CSI_PROVISIONER_IMAGE = "registry.k8s.io/sig-storage/csi-provisioner:v5.2.0"
CSI_RESIZER_IMAGE = "registry.k8s.io/sig-storage/csi-resizer:v1.13.2"
CSI_NODE_REGISTRAR_IMAGE = (
    "registry.k8s.io/sig-storage/csi-node-driver-registrar:v2.13.0"
)
CSI_REQUEST_TIMEOUT_SECONDS = 300.0
CSI_PVC_NAME_PARAMETER = "csi.storage.k8s.io/pvc/name"
CSI_PVC_NAMESPACE_PARAMETER = "csi.storage.k8s.io/pvc/namespace"
CSI_PV_NAME_PARAMETER = "csi.storage.k8s.io/pv/name"


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
    service_account: str,
    deadline: Deadline,
) -> None:
    labels = _csi_common_labels(CSI_CONTROLLER_NAME)
    socket_mount = {
        "name": "csi-socket",
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
            volumes=[VolumeSpec.empty_dir("csi-socket")],
            service_account_name=service_account,
            automount_service_account_token=True,
            node_selector={CLUSTER_REGISTRY_READY_LABEL: CLUSTER_REGISTRY_READY_VALUE},
        ),
        timeout=deadline.remaining(),
    )
    await deployment.wait_rollout(kube, timeout=deadline.remaining())


async def _ensure_csi_node(
    kube: Kube,
    *,
    image: str,
    service_account: str,
    deadline: Deadline,
) -> None:
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
                            "valueFrom": {"fieldRef": {"fieldPath": "spec.nodeName"}},
                        }
                    ],
                    security_context={"privileged": True, "runAsUser": 0},
                    volume_mounts=[
                        {"name": "csi-node-plugin", "mountPath": CSI_SOCKET_DIR},
                        {
                            "name": "csi-kubelet",
                            "mountPath": CSI_KUBELET_DIR,
                            "mountPropagation": "Bidirectional",
                        },
                        {
                            "name": "host-root",
                            "mountPath": "/host",
                            "mountPropagation": "Bidirectional",
                        },
                        {
                            "name": "host-dev",
                            "mountPath": "/dev",
                            "mountPropagation": "HostToContainer",
                        },
                        {
                            "name": "host-run",
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
                        {"name": "csi-node-plugin", "mountPath": CSI_SOCKET_DIR},
                        {
                            "name": "csi-registration",
                            "mountPath": CSI_REGISTRATION_DIR,
                        },
                    ],
                ),
            ],
            volumes=[
                VolumeSpec.host_path(
                    "csi-node-plugin",
                    path=plugin_dir,
                    host_path_type="DirectoryOrCreate",
                ),
                VolumeSpec.host_path(
                    "csi-registration",
                    path=f"{CSI_KUBELET_DIR}/plugins_registry",
                    host_path_type="DirectoryOrCreate",
                ),
                VolumeSpec.host_path(
                    "csi-kubelet",
                    path=CSI_KUBELET_DIR,
                    host_path_type="Directory",
                ),
                VolumeSpec.host_path(
                    "host-root",
                    path="/",
                    host_path_type="Directory",
                ),
                VolumeSpec.host_path(
                    "host-dev",
                    path="/dev",
                    host_path_type="Directory",
                ),
                VolumeSpec.host_path(
                    "host-run",
                    path="/run",
                    host_path_type="Directory",
                ),
            ],
            service_account_name=service_account,
            automount_service_account_token=True,
            node_selector={CLUSTER_REGISTRY_READY_LABEL: CLUSTER_REGISTRY_READY_VALUE},
            host_pid=True,
        ),
        timeout=deadline.remaining(),
    )
    await daemonset.wait_rollout(kube, timeout=deadline.remaining())


async def ensure_ceph_osd_csi_driver(
    kube: Kube,
    *,
    image: str,
    service_account: str,
    deadline: Deadline,
) -> None:
    """Converge the Bertrand OSD CSI driver and sidecar workloads."""
    await _upsert_csi_driver_object(kube, timeout=deadline.remaining())
    await _ensure_csi_controller(
        kube,
        image=image,
        service_account=service_account,
        deadline=deadline,
    )
    await _ensure_csi_node(
        kube,
        image=image,
        service_account=service_account,
        deadline=deadline,
    )


def _varint(value: int) -> bytes:
    chunks: list[int] = []
    value = int(value)
    while True:
        byte = value & 0x7F
        value >>= 7
        chunks.append(byte | 0x80 if value else byte)
        if not value:
            return bytes(chunks)


def _read_varint(data: bytes, offset: int) -> tuple[int, int]:
    shift = 0
    value = 0
    while offset < len(data):
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return value, offset
        shift += 7
    msg = "truncated protobuf varint"
    raise ValueError(msg)


def _field_varint(number: int, value: int) -> bytes:
    return _varint((number << 3) | 0) + _varint(value)


def _field_bytes(number: int, value: bytes) -> bytes:
    return _varint((number << 3) | 2) + _varint(len(value)) + value


def _field_string(number: int, value: str) -> bytes:
    return _field_bytes(number, value.encode("utf-8"))


def _message(*parts: bytes) -> bytes:
    return b"".join(part for part in parts if part)


def _map_entry(key: str, value: str) -> bytes:
    return _message(_field_string(1, key), _field_string(2, value))


def _field_map(number: int, values: Mapping[str, str]) -> bytes:
    return b"".join(
        _field_bytes(number, _map_entry(key, value))
        for key, value in sorted(values.items())
    )


def _parse_fields(data: bytes) -> dict[int, list[tuple[int, int | bytes]]]:
    fields: dict[int, list[tuple[int, int | bytes]]] = {}
    offset = 0
    while offset < len(data):
        key, offset = _read_varint(data, offset)
        number = key >> 3
        wire_type = key & 0x07
        if wire_type == 0:
            value, offset = _read_varint(data, offset)
        elif wire_type == 2:
            length, offset = _read_varint(data, offset)
            value = data[offset : offset + length]
            offset += length
        else:
            msg = f"unsupported protobuf wire type {wire_type}"
            raise ValueError(msg)
        fields.setdefault(number, []).append((wire_type, value))
    return fields


def _first_bytes(data: bytes, number: int) -> bytes:
    for wire_type, value in _parse_fields(data).get(number, ()):
        if wire_type == 2 and isinstance(value, bytes):
            return value
    return b""


def _first_string(data: bytes, number: int) -> str:
    return _first_bytes(data, number).decode("utf-8")


def _first_int(data: bytes, number: int) -> int:
    for wire_type, value in _parse_fields(data).get(number, ()):
        if wire_type == 0 and isinstance(value, int):
            return value
    return 0


def _field_messages(data: bytes, number: int) -> list[bytes]:
    return [
        value
        for wire_type, value in _parse_fields(data).get(number, ())
        if wire_type == 2 and isinstance(value, bytes)
    ]


def _map_values(data: bytes, number: int) -> dict[str, str]:
    values: dict[str, str] = {}
    for entry in _field_messages(data, number):
        key = _first_string(entry, 1)
        value = _first_string(entry, 2)
        if key:
            values[key] = value
    return values


def _capacity_request(data: bytes) -> int:
    capacity_range = _first_bytes(data, 2)
    return max(_first_int(capacity_range, 1), _first_int(capacity_range, 2))


def _volume_capabilities(data: bytes, number: int) -> list[bytes]:
    return _field_messages(data, number)


def _require_raw_block_capabilities(data: bytes, number: int) -> None:
    capabilities = _volume_capabilities(data, number)
    if not capabilities:
        msg = "Bertrand OSD CSI volumes require a raw block volume capability"
        raise ValueError(msg)
    for capability in capabilities:
        mount = _first_bytes(capability, 3)
        block = _first_bytes(capability, 4)
        if mount or not block:
            msg = "Bertrand OSD CSI supports raw block PVCs only"
            raise ValueError(msg)


def _topology(node_name: str) -> bytes:
    segments = {"kubernetes.io/hostname": node_name}
    return _field_map(1, segments)


def _volume(record: CephStorageOSD, *, volume_id: str) -> bytes:
    context = {
        "block_path": record.block_path,
        "osd_name": record.name,
        "origin": record.origin,
    }
    return _message(
        _field_string(1, volume_id),
        _field_varint(2, record.target_bytes),
        _field_map(3, context),
        _field_bytes(5, _topology(record.node_name)),
    )


def _service_capability(kind: int) -> bytes:
    return _field_bytes(1, _field_bytes(1, _field_varint(1, kind)))


def _plugin_expansion_capability(kind: int) -> bytes:
    return _field_bytes(1, _field_bytes(2, _field_varint(1, kind)))


def _controller_capability(kind: int) -> bytes:
    return _field_bytes(1, _field_bytes(1, _field_varint(1, kind)))


def _node_capability(kind: int) -> bytes:
    return _field_bytes(1, _field_bytes(1, _field_varint(1, kind)))


def _abort(context: Any, code_name: str, message: str) -> None:
    grpc = importlib.import_module("grpc")
    context.abort(getattr(grpc.StatusCode, code_name), message)


class BertrandOSDCSIDriver:
    """Minimal CSI endpoint for Bertrand-managed Rook OSD PVCs."""

    def __init__(self, *, role: str, node_name: str = "") -> None:
        self.role = role
        self.node_name = node_name.strip() or os.environ.get("NODE_NAME", "").strip()
        self.host_id = ""
        if role == "node":
            self.host_id = host_id_from_host_state()

    def _rpc[ResultT](
        self,
        context: Any,
        func: Callable[[Kube], Awaitable[ResultT]],
    ) -> ResultT:
        async def invoke() -> ResultT:
            with Kube.inside_cluster() as kube:
                return await func(kube)

        try:
            return asyncio.run(invoke())
        except KeyError as err:
            _abort(context, "NOT_FOUND", str(err))
        except PermissionError as err:
            _abort(context, "PERMISSION_DENIED", str(err))
        except (OSError, TimeoutError, ValueError) as err:
            _abort(context, "FAILED_PRECONDITION", str(err))
        msg = "CSI abort returned unexpectedly"
        raise RuntimeError(msg)

    def _require_role(self, context: Any, role: str, method: str) -> None:
        if self.role != role:
            _abort(
                context,
                "UNIMPLEMENTED",
                f"{method} is unavailable on Bertrand CSI {self.role!r} role",
            )

    async def _record_for_volume(
        self,
        kube: Kube,
        volume_id: str,
    ) -> CephStorageOSD:
        storage = await read_storage_state(kube, timeout=CSI_REQUEST_TIMEOUT_SECONDS)
        for record in storage.status.osds.values():
            if volume_id in {record.name, record.csi_volume_id}:
                return record
        msg = f"unknown Bertrand OSD CSI volume {volume_id!r}"
        raise KeyError(msg)

    def _get_plugin_info(self, _request: bytes, _context: Any) -> bytes:
        return _message(
            _field_string(1, CSI_DRIVER_NAME),
            _field_string(2, "v1alpha1"),
        )

    def _get_plugin_capabilities(self, _request: bytes, _context: Any) -> bytes:
        return _message(
            _service_capability(1),
            _service_capability(2),
            _plugin_expansion_capability(1),
        )

    def _probe(self, _request: bytes, _context: Any) -> bytes:
        return b""

    def _controller_get_capabilities(self, _request: bytes, context: Any) -> bytes:
        self._require_role(context, "controller", "ControllerGetCapabilities")
        return _message(_controller_capability(1), _controller_capability(9))

    def _node_get_capabilities(self, _request: bytes, context: Any) -> bytes:
        self._require_role(context, "node", "NodeGetCapabilities")
        return _message(_node_capability(3))

    def _node_get_info(self, _request: bytes, context: Any) -> bytes:
        self._require_role(context, "node", "NodeGetInfo")
        return _message(
            _field_string(1, self.node_name),
            _field_bytes(3, _topology(self.node_name)),
        )

    def _create_volume(self, request: bytes, context: Any) -> bytes:
        self._require_role(context, "controller", "CreateVolume")
        parameters = _map_values(request, 4)
        pvc_name = parameters.get(CSI_PVC_NAME_PARAMETER, "").strip()
        pvc_namespace = parameters.get(CSI_PVC_NAMESPACE_PARAMETER, "").strip()
        pv_name = parameters.get(CSI_PV_NAME_PARAMETER, "").strip()
        if not pvc_name or not pvc_namespace:
            _abort(context, "INVALID_ARGUMENT", "CreateVolume missing PVC metadata")
        try:
            _require_raw_block_capabilities(request, 3)
        except ValueError as err:
            _abort(context, "INVALID_ARGUMENT", str(err))
        volume_id = _first_string(request, 1) or pv_name or pvc_name
        requested = _capacity_request(request)

        async def invoke(kube: Kube) -> bytes:
            claim = await PersistentVolumeClaim.get(
                kube,
                namespace=pvc_namespace,
                name=pvc_name,
                timeout=CSI_REQUEST_TIMEOUT_SECONDS,
            )
            if claim is None:
                msg = f"PVC {pvc_namespace}/{pvc_name} does not exist"
                raise KeyError(msg)
            osd_name = claim.labels.get(STORAGE_OSD_NAME_LABEL, "").strip()
            if not osd_name:
                msg = (
                    f"PVC {pvc_namespace}/{pvc_name} is not a "
                    "Bertrand-managed OSD claim"
                )
                raise PermissionError(msg)
            storage = await read_storage_state(
                kube,
                timeout=CSI_REQUEST_TIMEOUT_SECONDS,
            )
            record: CephStorageOSD | None = None
            for item in storage.status.osds.values():
                if item.name == osd_name:
                    record = item
                    break
            if record is None:
                msg = (
                    f"PVC {pvc_namespace}/{pvc_name} references missing "
                    f"OSD record {osd_name!r}"
                )
                raise KeyError(msg)
            if requested > record.target_bytes:
                msg = (
                    f"PVC requests {requested} bytes but OSD "
                    f"{record.name} is prepared for "
                    f"{record.target_bytes} bytes"
                )
                raise ValueError(msg)
            bound = record.model_copy(
                update={
                    "csi_volume_id": volume_id,
                    "persistent_volume_name": pv_name,
                    "persistent_volume_claim_namespace": claim.namespace,
                    "persistent_volume_claim_name": claim.name,
                },
            )
            phase = record.phase
            if phase not in {"Ready", "Expanding"}:
                phase = "Binding"
            fresh = await upsert_storage_osd(
                kube,
                name=record.name,
                spec=bound,
                phase=phase,
                timeout=CSI_REQUEST_TIMEOUT_SECONDS,
            )
            return _field_bytes(1, _volume(fresh, volume_id=volume_id))

        return self._rpc(context, invoke)

    def _delete_volume(self, request: bytes, context: Any) -> bytes:
        self._require_role(context, "controller", "DeleteVolume")
        volume_id = _first_string(request, 1)
        if not volume_id:
            _abort(context, "INVALID_ARGUMENT", "DeleteVolume missing volume_id")

        async def invoke(kube: Kube) -> bytes:
            with suppress(KeyError):
                record = await self._record_for_volume(kube, volume_id)
                if record.phase not in {"Shrinking", "Retiring", "Retired"}:
                    await patch_storage_osd_status(
                        kube,
                        osd=record,
                        status={
                            "last_error": (
                                "CSI DeleteVolume was requested while the OSD was "
                                f"{record.phase}; preserving host substrate "
                                "because Bertrand has not started shrink/retirement"
                            )
                        },
                        timeout=CSI_REQUEST_TIMEOUT_SECONDS,
                    )
                    return b""
                await patch_storage_osd_status(
                    kube,
                    osd=record,
                    status={"last_error": ""},
                    timeout=CSI_REQUEST_TIMEOUT_SECONDS,
                )
            return b""

        return self._rpc(context, invoke)

    def _controller_expand_volume(self, request: bytes, context: Any) -> bytes:
        self._require_role(context, "controller", "ControllerExpandVolume")
        volume_id = _first_string(request, 1)
        requested = _capacity_request(request)
        if not volume_id:
            _abort(
                context,
                "INVALID_ARGUMENT",
                "ControllerExpandVolume missing volume_id",
            )

        async def invoke(kube: Kube) -> bytes:
            record = await self._record_for_volume(kube, volume_id)
            if requested > record.target_bytes:
                msg = (
                    f"expansion requested {requested} bytes but OSD "
                    f"{record.name} target is {record.target_bytes} bytes"
                )
                raise ValueError(msg)
            return _message(
                _field_varint(1, record.target_bytes),
                _field_varint(2, 1),
            )

        return self._rpc(context, invoke)

    def _validate_volume_capabilities(self, request: bytes, context: Any) -> bytes:
        self._require_role(context, "controller", "ValidateVolumeCapabilities")
        try:
            _require_raw_block_capabilities(request, 2)
        except ValueError as err:
            _abort(context, "INVALID_ARGUMENT", str(err))
        return _field_bytes(1, b"")

    def _node_stage_volume(self, _request: bytes, context: Any) -> bytes:
        self._require_role(context, "node", "NodeStageVolume")
        return b""

    def _node_unstage_volume(self, _request: bytes, context: Any) -> bytes:
        self._require_role(context, "node", "NodeUnstageVolume")
        return b""

    def _node_publish_volume(self, request: bytes, context: Any) -> bytes:
        self._require_role(context, "node", "NodePublishVolume")
        volume_id = _first_string(request, 1)
        target_path = _first_string(request, 4)
        if not volume_id or not target_path:
            _abort(context, "INVALID_ARGUMENT", "NodePublishVolume missing arguments")
        try:
            _require_raw_block_capabilities(request, 5)
        except ValueError as err:
            _abort(context, "INVALID_ARGUMENT", str(err))

        async def invoke(kube: Kube) -> bytes:
            record = await self._record_for_volume(kube, volume_id)
            if record.host_id != self.host_id:
                msg = (
                    f"volume {volume_id!r} belongs to host {record.host_id}, "
                    f"not this host {self.host_id}"
                )
                raise PermissionError(msg)
            if record.origin == "loop-fallback":
                prepared = await prepare_loop_fallback_osd(
                    name=record.name,
                    target_bytes=record.target_bytes,
                    timeout=CSI_REQUEST_TIMEOUT_SECONDS,
                )
            else:
                prepared = await prepare_lvm_osd(
                    name=record.name,
                    target_bytes=record.target_bytes,
                    pv_name=record.pv_name,
                    lv_name=record.lv_name,
                    timeout=CSI_REQUEST_TIMEOUT_SECONDS,
                )
            await bind_block_device(
                block_path=record.block_path,
                target_path=target_path,
                timeout=CSI_REQUEST_TIMEOUT_SECONDS,
            )
            await patch_storage_osd_status(
                kube,
                osd=record,
                status={
                    "phase": ("Ready" if record.phase == "Ready" else "Binding"),
                    "observed_bytes": prepared.observed_bytes,
                    "last_error": "",
                },
                timeout=CSI_REQUEST_TIMEOUT_SECONDS,
            )
            return b""

        return self._rpc(context, invoke)

    def _node_unpublish_volume(self, request: bytes, context: Any) -> bytes:
        self._require_role(context, "node", "NodeUnpublishVolume")
        target_path = _first_string(request, 2)
        if not target_path:
            _abort(
                context,
                "INVALID_ARGUMENT",
                "NodeUnpublishVolume missing target path",
            )
        try:
            asyncio.run(
                unbind_block_device(
                    target_path=target_path,
                    timeout=CSI_REQUEST_TIMEOUT_SECONDS,
                )
            )
        except (OSError, TimeoutError, ValueError) as err:
            _abort(context, "FAILED_PRECONDITION", str(err))
        return b""

    def _node_expand_volume(self, request: bytes, context: Any) -> bytes:
        self._require_role(context, "node", "NodeExpandVolume")
        volume_id = _first_string(request, 1)
        if not volume_id:
            _abort(context, "INVALID_ARGUMENT", "NodeExpandVolume missing volume_id")

        async def invoke(kube: Kube) -> bytes:
            record = await self._record_for_volume(kube, volume_id)
            return _field_varint(1, record.target_bytes)

        return self._rpc(context, invoke)


def _generic_rpc_handlers(grpc: Any, driver: BertrandOSDCSIDriver) -> tuple[Any, ...]:
    def raw_handler(method: Callable[[bytes, Any], bytes]) -> Any:
        return grpc.unary_unary_rpc_method_handler(
            method,
            request_deserializer=lambda payload: payload,
            response_serializer=lambda payload: payload,
        )

    return (
        grpc.method_handlers_generic_handler(
            "csi.v1.Identity",
            {
                "GetPluginInfo": raw_handler(driver._get_plugin_info),
                "GetPluginCapabilities": raw_handler(driver._get_plugin_capabilities),
                "Probe": raw_handler(driver._probe),
            },
        ),
        grpc.method_handlers_generic_handler(
            "csi.v1.Controller",
            {
                "ControllerGetCapabilities": raw_handler(
                    driver._controller_get_capabilities
                ),
                "CreateVolume": raw_handler(driver._create_volume),
                "DeleteVolume": raw_handler(driver._delete_volume),
                "ControllerExpandVolume": raw_handler(driver._controller_expand_volume),
                "ValidateVolumeCapabilities": raw_handler(
                    driver._validate_volume_capabilities
                ),
            },
        ),
        grpc.method_handlers_generic_handler(
            "csi.v1.Node",
            {
                "NodeGetCapabilities": raw_handler(driver._node_get_capabilities),
                "NodeGetInfo": raw_handler(driver._node_get_info),
                "NodeStageVolume": raw_handler(driver._node_stage_volume),
                "NodeUnstageVolume": raw_handler(driver._node_unstage_volume),
                "NodePublishVolume": raw_handler(driver._node_publish_volume),
                "NodeUnpublishVolume": raw_handler(driver._node_unpublish_volume),
                "NodeExpandVolume": raw_handler(driver._node_expand_volume),
            },
        ),
    )


def serve_csi(*, role: str, endpoint: str, node_name: str = "") -> None:
    """Serve the Bertrand OSD CSI endpoint over a Unix-domain socket.

    Raises
    ------
    ValueError
        If `endpoint` does not use a Unix-domain socket URL.
    """
    grpc = importlib.import_module("grpc")
    if not endpoint.startswith("unix://"):
        msg = "Bertrand OSD CSI endpoint must use unix://"
        raise ValueError(msg)
    socket_path = Path(endpoint.removeprefix("unix://"))
    socket_path.parent.mkdir(parents=True, exist_ok=True)
    with suppress(FileNotFoundError):
        socket_path.unlink()
    driver = BertrandOSDCSIDriver(role=role, node_name=node_name)

    server = grpc.server(futures.ThreadPoolExecutor(max_workers=8))
    server.add_generic_rpc_handlers(_generic_rpc_handlers(grpc, driver))
    server.add_insecure_port(endpoint)
    server.start()
    try:
        server.wait_for_termination()
    finally:
        with suppress(FileNotFoundError):
            socket_path.unlink()


def main(argv: list[str] | None = None) -> None:
    """Run the Bertrand OSD CSI driver."""
    parser = argparse.ArgumentParser(prog="bertrand-ceph-csi")
    parser.add_argument("role", choices=("controller", "node"))
    parser.add_argument("--endpoint", default="")
    parser.add_argument("--node-name", default="")
    ns = parser.parse_args(argv)
    endpoint = ns.endpoint or (
        f"unix://{CSI_CONTROLLER_SOCKET}"
        if ns.role == "controller"
        else f"unix://{CSI_NODE_SOCKET}"
    )
    serve_csi(role=ns.role, endpoint=endpoint, node_name=ns.node_name)
