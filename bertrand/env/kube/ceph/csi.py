"""Bertrand CSI driver for Rook OSD block substrates."""

from __future__ import annotations

import argparse
import asyncio
import importlib
import os
from concurrent import futures
from contextlib import suppress
from pathlib import Path
from types import MappingProxyType
from typing import TYPE_CHECKING, Any, NoReturn, override

from bertrand.env.git import BERTRAND_NAMESPACE, Deadline
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.api.spec import ContainerSpec, PodTemplateSpec, VolumeSpec
from bertrand.env.kube.build.repository import (
    CLUSTER_REGISTRY_READY_LABEL,
    CLUSTER_REGISTRY_READY_VALUE,
)
from bertrand.env.kube.ceph import _csi_pb2, _csi_pb2_grpc
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
from bertrand.env.kube.volume import PERSISTENT_VOLUME_CLAIM_RESOURCE

if TYPE_CHECKING:
    from collections.abc import Awaitable, Callable, Iterable

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
CSI_CONTROLLER_LABELS = MappingProxyType(
    {
        "app.kubernetes.io/name": CSI_CONTROLLER_NAME,
        "app.kubernetes.io/part-of": "bertrand",
        **STORAGE_CONTROLLER_LABELS,
    }
)
CSI_NODE_LABELS = MappingProxyType(
    {
        "app.kubernetes.io/name": CSI_NODE_NAME,
        "app.kubernetes.io/part-of": "bertrand",
        **STORAGE_CONTROLLER_LABELS,
    }
)
CSI_CONTROLLER_SELECTOR = MappingProxyType(
    {"app.kubernetes.io/name": CSI_CONTROLLER_NAME}
)
CSI_NODE_SELECTOR = MappingProxyType({"app.kubernetes.io/name": CSI_NODE_NAME})


async def ensure_ceph_osd_csi_driver(
    kube: Kube,
    *,
    image: str,
    service_account: str,
    deadline: Deadline,
) -> None:
    """Converge the Bertrand OSD CSI driver and sidecar workloads."""
    driver_manifest = {
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
        deadline=deadline,
        context=f"failed to read CSIDriver {CSI_DRIVER_NAME!r}",
    )
    if existing is None:
        await kube.run(
            lambda request_timeout: kube.storage.create_csi_driver(
                body=driver_manifest,
                _request_timeout=request_timeout,
            ),
            deadline=deadline,
            context=f"failed to create CSIDriver {CSI_DRIVER_NAME!r}",
        )
    else:
        await kube.run(
            lambda request_timeout: kube.storage.patch_csi_driver(
                name=CSI_DRIVER_NAME,
                body=driver_manifest,
                _request_timeout=request_timeout,
            ),
            deadline=deadline,
            context=f"failed to patch CSIDriver {CSI_DRIVER_NAME!r}",
        )

    socket_mount = {
        "name": "csi-socket",
        "mountPath": Path(CSI_CONTROLLER_SOCKET).parent.as_posix(),
    }
    deployment = await Deployment.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=CSI_CONTROLLER_NAME,
        labels=CSI_CONTROLLER_LABELS,
        selector=CSI_CONTROLLER_SELECTOR,
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
        deadline=deadline,
    )

    plugin_dir = f"{CSI_KUBELET_DIR}/plugins/{CSI_DRIVER_NAME}"
    daemonset = await DaemonSet.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=CSI_NODE_NAME,
        labels=CSI_NODE_LABELS,
        selector=CSI_NODE_SELECTOR,
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
        deadline=deadline,
    )
    await asyncio.gather(
        deployment.wait_rollout(kube, deadline=deadline),
        daemonset.wait_rollout(kube, deadline=deadline),
    )


def _capacity_request(capacity_range: _csi_pb2.CapacityRange) -> int:
    return max(int(capacity_range.required_bytes), int(capacity_range.limit_bytes))


def _require_raw_block_capabilities(
    capabilities: Iterable[_csi_pb2.VolumeCapability],
) -> None:
    capabilities = tuple(
        capability
        for capability in capabilities
        if capability.WhichOneof("access_type") is not None
    )
    if not capabilities:
        msg = "Bertrand OSD CSI volumes require a raw block volume capability"
        raise ValueError(msg)
    for capability in capabilities:
        if capability.WhichOneof("access_type") != "block":
            msg = "Bertrand OSD CSI supports raw block PVCs only"
            raise ValueError(msg)


def _topology(node_name: str) -> _csi_pb2.Topology:
    topology = _csi_pb2.Topology()
    topology.segments["kubernetes.io/hostname"] = node_name
    return topology


def _volume(record: CephStorageOSD, *, volume_id: str) -> _csi_pb2.Volume:
    context = {
        "block_path": record.block_path,
        "osd_name": record.name,
        "origin": record.origin,
    }
    volume = _csi_pb2.Volume(
        volume_id=volume_id,
        capacity_bytes=record.target_bytes,
    )
    volume.volume_context.update(context)
    volume.accessible_topology.append(_topology(record.node_name))
    return volume


def _abort(context: Any, code_name: str, message: str) -> NoReturn:
    grpc = importlib.import_module("grpc")
    context.abort(getattr(grpc.StatusCode, code_name), message)
    msg = "CSI abort returned unexpectedly"
    raise RuntimeError(msg)


class BertrandOSDCSIDriver(
    _csi_pb2_grpc.IdentityServicer,
    _csi_pb2_grpc.ControllerServicer,
    _csi_pb2_grpc.NodeServicer,
):
    """Minimal CSI endpoint for Bertrand-managed Rook OSD PVCs."""

    def __init__(self, *, role: str, node_name: str = "") -> None:
        self.role = role.strip()
        self.node_name = node_name.strip() or os.environ.get("NODE_NAME", "").strip()
        self.host_id = ""
        if self.role == "node":
            if not self.node_name:
                msg = "Bertrand OSD CSI node role requires NODE_NAME"
                raise ValueError(msg)
            self.host_id = host_id_from_host_state()

    def _rpc[ResultT](
        self,
        context: Any,
        func: Callable[[Kube], Awaitable[ResultT]],
    ) -> ResultT:
        async def invoke() -> ResultT:
            with Kube.internal() as kube:
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
        volume_id = volume_id.strip()
        storage = await read_storage_state(
            kube,
            deadline=Deadline(CSI_REQUEST_TIMEOUT_SECONDS),
        )
        for record in storage.status.osds.values():
            if volume_id in {record.name, record.csi_volume_id}:
                return record
        msg = f"unknown Bertrand OSD CSI volume {volume_id!r}"
        raise KeyError(msg)

    @override
    def GetPluginInfo(
        self,
        request: _csi_pb2.GetPluginInfoRequest,
        context: Any,
    ) -> _csi_pb2.GetPluginInfoResponse:
        return _csi_pb2.GetPluginInfoResponse(
            name=CSI_DRIVER_NAME,
            vendor_version="v1alpha1",
        )

    @override
    def GetPluginCapabilities(
        self,
        request: _csi_pb2.GetPluginCapabilitiesRequest,
        context: Any,
    ) -> _csi_pb2.GetPluginCapabilitiesResponse:
        return _csi_pb2.GetPluginCapabilitiesResponse(
            capabilities=[
                _csi_pb2.PluginCapability(
                    service=_csi_pb2.PluginCapability.Service(type="CONTROLLER_SERVICE")
                ),
                _csi_pb2.PluginCapability(
                    service=_csi_pb2.PluginCapability.Service(
                        type="VOLUME_ACCESSIBILITY_CONSTRAINTS"
                    )
                ),
                _csi_pb2.PluginCapability(
                    volume_expansion=_csi_pb2.PluginCapability.VolumeExpansion(
                        type="ONLINE"
                    )
                ),
            ]
        )

    @override
    def Probe(
        self,
        request: _csi_pb2.ProbeRequest,
        context: Any,
    ) -> _csi_pb2.ProbeResponse:
        return _csi_pb2.ProbeResponse()

    @override
    def ControllerGetCapabilities(
        self,
        request: _csi_pb2.ControllerGetCapabilitiesRequest,
        context: Any,
    ) -> _csi_pb2.ControllerGetCapabilitiesResponse:
        self._require_role(context, "controller", "ControllerGetCapabilities")
        return _csi_pb2.ControllerGetCapabilitiesResponse(
            capabilities=[
                _csi_pb2.ControllerServiceCapability(
                    rpc=_csi_pb2.ControllerServiceCapability.RPC(
                        type="CREATE_DELETE_VOLUME"
                    )
                ),
                _csi_pb2.ControllerServiceCapability(
                    rpc=_csi_pb2.ControllerServiceCapability.RPC(type="EXPAND_VOLUME")
                ),
            ]
        )

    @override
    def CreateVolume(
        self,
        request: _csi_pb2.CreateVolumeRequest,
        context: Any,
    ) -> _csi_pb2.CreateVolumeResponse:
        self._require_role(context, "controller", "CreateVolume")
        parameters = request.parameters
        pvc_name = parameters.get(CSI_PVC_NAME_PARAMETER, "").strip()
        pvc_namespace = parameters.get(CSI_PVC_NAMESPACE_PARAMETER, "").strip()
        pv_name = parameters.get(CSI_PV_NAME_PARAMETER, "").strip()
        if not pvc_name or not pvc_namespace:
            _abort(context, "INVALID_ARGUMENT", "CreateVolume missing PVC metadata")
        try:
            _require_raw_block_capabilities(request.volume_capabilities)
        except ValueError as err:
            _abort(context, "INVALID_ARGUMENT", str(err))
        volume_id = (request.name or pv_name or pvc_name).strip()
        requested = _capacity_request(request.capacity_range)

        async def invoke(kube: Kube) -> _csi_pb2.CreateVolumeResponse:
            claim = await PERSISTENT_VOLUME_CLAIM_RESOURCE.get(
                kube,
                namespace=pvc_namespace,
                name=pvc_name,
                deadline=Deadline(CSI_REQUEST_TIMEOUT_SECONDS),
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
                deadline=Deadline(CSI_REQUEST_TIMEOUT_SECONDS),
            )
            record = storage.status.osds.get(osd_name)
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
                deadline=Deadline(CSI_REQUEST_TIMEOUT_SECONDS),
            )
            return _csi_pb2.CreateVolumeResponse(
                volume=_volume(fresh, volume_id=volume_id)
            )

        return self._rpc(context, invoke)

    @override
    def DeleteVolume(
        self,
        request: _csi_pb2.DeleteVolumeRequest,
        context: Any,
    ) -> _csi_pb2.DeleteVolumeResponse:
        self._require_role(context, "controller", "DeleteVolume")
        volume_id = request.volume_id.strip()
        if not volume_id:
            _abort(context, "INVALID_ARGUMENT", "DeleteVolume missing volume_id")

        async def invoke(kube: Kube) -> _csi_pb2.DeleteVolumeResponse:
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
                        deadline=Deadline(CSI_REQUEST_TIMEOUT_SECONDS),
                    )
                    return _csi_pb2.DeleteVolumeResponse()
                await patch_storage_osd_status(
                    kube,
                    osd=record,
                    status={"last_error": ""},
                    deadline=Deadline(CSI_REQUEST_TIMEOUT_SECONDS),
                )
            return _csi_pb2.DeleteVolumeResponse()

        return self._rpc(context, invoke)

    @override
    def ControllerExpandVolume(
        self,
        request: _csi_pb2.ControllerExpandVolumeRequest,
        context: Any,
    ) -> _csi_pb2.ControllerExpandVolumeResponse:
        self._require_role(context, "controller", "ControllerExpandVolume")
        volume_id = request.volume_id.strip()
        requested = _capacity_request(request.capacity_range)
        if not volume_id:
            _abort(
                context,
                "INVALID_ARGUMENT",
                "ControllerExpandVolume missing volume_id",
            )

        async def invoke(kube: Kube) -> _csi_pb2.ControllerExpandVolumeResponse:
            record = await self._record_for_volume(kube, volume_id)
            if requested > record.target_bytes:
                msg = (
                    f"expansion requested {requested} bytes but OSD "
                    f"{record.name} target is {record.target_bytes} bytes"
                )
                raise ValueError(msg)
            return _csi_pb2.ControllerExpandVolumeResponse(
                capacity_bytes=record.target_bytes,
                node_expansion_required=True,
            )

        return self._rpc(context, invoke)

    @override
    def ValidateVolumeCapabilities(
        self,
        request: _csi_pb2.ValidateVolumeCapabilitiesRequest,
        context: Any,
    ) -> _csi_pb2.ValidateVolumeCapabilitiesResponse:
        self._require_role(context, "controller", "ValidateVolumeCapabilities")
        try:
            _require_raw_block_capabilities(request.volume_capabilities)
        except ValueError as err:
            _abort(context, "INVALID_ARGUMENT", str(err))
        return _csi_pb2.ValidateVolumeCapabilitiesResponse(
            confirmed=_csi_pb2.ValidateVolumeCapabilitiesResponse.Confirmed()
        )

    @override
    def NodeGetCapabilities(
        self,
        request: _csi_pb2.NodeGetCapabilitiesRequest,
        context: Any,
    ) -> _csi_pb2.NodeGetCapabilitiesResponse:
        self._require_role(context, "node", "NodeGetCapabilities")
        return _csi_pb2.NodeGetCapabilitiesResponse(
            capabilities=[
                _csi_pb2.NodeServiceCapability(
                    rpc=_csi_pb2.NodeServiceCapability.RPC(type="EXPAND_VOLUME")
                )
            ]
        )

    @override
    def NodeGetInfo(
        self,
        request: _csi_pb2.NodeGetInfoRequest,
        context: Any,
    ) -> _csi_pb2.NodeGetInfoResponse:
        self._require_role(context, "node", "NodeGetInfo")
        return _csi_pb2.NodeGetInfoResponse(
            node_id=self.node_name,
            accessible_topology=_topology(self.node_name),
        )

    @override
    def NodeStageVolume(
        self,
        request: _csi_pb2.NodeStageVolumeRequest,
        context: Any,
    ) -> _csi_pb2.NodeStageVolumeResponse:
        self._require_role(context, "node", "NodeStageVolume")
        return _csi_pb2.NodeStageVolumeResponse()

    @override
    def NodeUnstageVolume(
        self,
        request: _csi_pb2.NodeUnstageVolumeRequest,
        context: Any,
    ) -> _csi_pb2.NodeUnstageVolumeResponse:
        self._require_role(context, "node", "NodeUnstageVolume")
        return _csi_pb2.NodeUnstageVolumeResponse()

    @override
    def NodePublishVolume(
        self,
        request: _csi_pb2.NodePublishVolumeRequest,
        context: Any,
    ) -> _csi_pb2.NodePublishVolumeResponse:
        self._require_role(context, "node", "NodePublishVolume")
        volume_id = request.volume_id.strip()
        target_path = request.target_path.strip()
        if not volume_id or not target_path:
            _abort(context, "INVALID_ARGUMENT", "NodePublishVolume missing arguments")
        try:
            _require_raw_block_capabilities((request.volume_capability,))
        except ValueError as err:
            _abort(context, "INVALID_ARGUMENT", str(err))

        async def invoke(kube: Kube) -> _csi_pb2.NodePublishVolumeResponse:
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
                    deadline=Deadline(CSI_REQUEST_TIMEOUT_SECONDS),
                )
            else:
                prepared = await prepare_lvm_osd(
                    name=record.name,
                    target_bytes=record.target_bytes,
                    pv_name=record.pv_name,
                    lv_name=record.lv_name,
                    deadline=Deadline(CSI_REQUEST_TIMEOUT_SECONDS),
                )
            await bind_block_device(
                block_path=record.block_path,
                target_path=target_path,
                deadline=Deadline(CSI_REQUEST_TIMEOUT_SECONDS),
            )
            await patch_storage_osd_status(
                kube,
                osd=record,
                status={
                    "phase": ("Ready" if record.phase == "Ready" else "Binding"),
                    "observed_bytes": prepared.observed_bytes,
                    "last_error": "",
                },
                deadline=Deadline(CSI_REQUEST_TIMEOUT_SECONDS),
            )
            return _csi_pb2.NodePublishVolumeResponse()

        return self._rpc(context, invoke)

    @override
    def NodeUnpublishVolume(
        self,
        request: _csi_pb2.NodeUnpublishVolumeRequest,
        context: Any,
    ) -> _csi_pb2.NodeUnpublishVolumeResponse:
        self._require_role(context, "node", "NodeUnpublishVolume")
        target_path = request.target_path.strip()
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
                    deadline=Deadline(CSI_REQUEST_TIMEOUT_SECONDS),
                )
            )
        except (OSError, TimeoutError, ValueError) as err:
            _abort(context, "FAILED_PRECONDITION", str(err))
        return _csi_pb2.NodeUnpublishVolumeResponse()

    @override
    def NodeExpandVolume(
        self,
        request: _csi_pb2.NodeExpandVolumeRequest,
        context: Any,
    ) -> _csi_pb2.NodeExpandVolumeResponse:
        self._require_role(context, "node", "NodeExpandVolume")
        volume_id = request.volume_id.strip()
        if not volume_id:
            _abort(context, "INVALID_ARGUMENT", "NodeExpandVolume missing volume_id")

        async def invoke(kube: Kube) -> _csi_pb2.NodeExpandVolumeResponse:
            record = await self._record_for_volume(kube, volume_id)
            return _csi_pb2.NodeExpandVolumeResponse(capacity_bytes=record.target_bytes)

        return self._rpc(context, invoke)


def serve_csi(*, role: str, endpoint: str, node_name: str = "") -> None:
    """Serve the Bertrand OSD CSI endpoint over a Unix-domain socket.

    Raises
    ------
    ValueError
        If `endpoint` does not use a Unix-domain socket URL.
    """
    grpc = importlib.import_module("grpc")
    endpoint = endpoint.strip()
    if not endpoint.startswith("unix://"):
        msg = "Bertrand OSD CSI endpoint must use unix://"
        raise ValueError(msg)
    socket_path = Path(endpoint.removeprefix("unix://"))
    socket_path.parent.mkdir(parents=True, exist_ok=True)
    with suppress(FileNotFoundError):
        socket_path.unlink()
    driver = BertrandOSDCSIDriver(role=role, node_name=node_name)

    server = grpc.server(futures.ThreadPoolExecutor(max_workers=8))
    _csi_pb2_grpc.add_IdentityServicer_to_server(driver, server)
    _csi_pb2_grpc.add_ControllerServicer_to_server(driver, server)
    _csi_pb2_grpc.add_NodeServicer_to_server(driver, server)
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
