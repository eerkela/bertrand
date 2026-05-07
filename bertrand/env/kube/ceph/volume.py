"""CephFS-backed repository volume lifecycle helpers."""

from __future__ import annotations

import asyncio
import hashlib
from dataclasses import dataclass
from pathlib import PosixPath
from typing import Self

from ...config.core import _check_uuid
from ...run import BERTRAND_ENV, BERTRAND_NAMESPACE, REPO_ID_ENV
from ..api import Kube
from ..pod import Pod
from ..volume import PersistentVolume, PersistentVolumeClaim, StorageClass

REPO_VOLUME_ENV: str = "BERTRAND_REPO_VOLUME"
DEFAULT_VOLUME_SIZE = "16Mi"
CEPHFS_STORAGE_CLASS_PREFERENCES: tuple[str, ...] = ("cephfs", "rook-cephfs")


@dataclass(frozen=True)
class RepoVolume:
    """Structured metadata for a CephFS-backed repository volume in the local cluster.

    Attributes
    ----------
    repo_id : str
        Stable repository identity used for volume naming and management.
    pvc : PersistentVolumeClaim
        Kubernetes PVC object representing the repository volume claim.
    """

    repo_id: str
    pvc: PersistentVolumeClaim

    @staticmethod
    def _kube_name(repo_id: str) -> str:
        repo_id = _check_uuid(repo_id)
        h = hashlib.sha256()
        encoded: bytes = repo_id.encode("utf-8")
        h.update(len(encoded).to_bytes(8, "big"))
        h.update(encoded)
        return f"bertrand-repo-{h.hexdigest()}"

    @staticmethod
    def _assert_managed_pvc(
        pvc: PersistentVolumeClaim,
        *,
        claim_name: str,
        repo_id: str,
        storage_class: str | None,
        require_rwx: bool,
    ) -> None:
        meta = pvc.obj.metadata
        actual_name = meta.name if meta is not None else None
        if actual_name != claim_name:
            raise OSError(
                f"cluster PVC for repo {repo_id!r} has non-deterministic claim name "
                f"{actual_name!r}, expected {claim_name!r}"
            )
        labels = (meta.labels or {}) if meta is not None else {}
        spec = pvc.obj.spec
        storage_class_name = spec.storage_class_name if spec is not None else None
        access_modes = (spec.access_modes or []) if spec is not None else []
        if labels.get(BERTRAND_ENV) != "1" or labels.get(REPO_VOLUME_ENV) != "1":
            raise OSError(
                f"cluster PVC {claim_name!r} collides with Bertrand volume claim, but "
                "is not managed by Bertrand"
            )
        actual_repo_id = labels.get(REPO_ID_ENV)
        if actual_repo_id != repo_id:
            raise OSError(
                f"cluster PVC {claim_name!r} has mismatched repo identity label "
                f"{REPO_ID_ENV!r}: expected {repo_id!r}, got {actual_repo_id!r}"
            )
        if storage_class is not None and storage_class_name != storage_class:
            raise OSError(
                f"cluster PVC {claim_name!r} uses storage class "
                f"{storage_class_name!r}, expected {storage_class!r}"
            )
        if require_rwx and "ReadWriteMany" not in access_modes:
            raise OSError(f"cluster PVC {claim_name!r} must include ReadWriteMany access mode")

    @classmethod
    async def create(
        cls,
        repo_id: str,
        *,
        timeout: float,
        size_request: str,
    ) -> Self:
        """Ensure a deterministic, cluster-wide RWX claim exists for one repository."""
        repo_id = _check_uuid(repo_id)
        if timeout <= 0:
            raise TimeoutError("timeout must be non-negative")
        size_request = size_request.strip()
        if not size_request:
            raise ValueError("size request cannot be empty")
        claim_name = cls._kube_name(repo_id)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        with await Kube.host(timeout=deadline - loop.time()) as kube:
            storage = await StorageClass.select(
                kube=kube,
                timeout=deadline - loop.time(),
                preferences=CEPHFS_STORAGE_CLASS_PREFERENCES,
                require_expansion=True,
            )
            if not storage.is_cephfs:
                raise OSError(
                    f"storage class {storage.name!r} uses provisioner "
                    f"{storage.provisioner!r}, but Bertrand repository volumes require a "
                    "CephFS CSI provisioner"
                )
            storage_class = storage.name

            pvc = await PersistentVolumeClaim.upsert(
                kube=kube,
                namespace=BERTRAND_NAMESPACE,
                name=claim_name,
                access_modes=("ReadWriteMany",),
                storage_class=storage_class,
                storage_request=size_request,
                labels={
                    BERTRAND_ENV: "1",
                    REPO_VOLUME_ENV: "1",
                    REPO_ID_ENV: repo_id,
                },
                timeout=deadline - loop.time(),
            )

            cls._assert_managed_pvc(
                pvc,
                claim_name=claim_name,
                repo_id=repo_id,
                storage_class=storage_class,
                require_rwx=True,
            )

            return cls(repo_id=repo_id, pvc=pvc)

    @classmethod
    async def get(cls, repo_id: str | None, *, timeout: float) -> list[Self]:
        """List repository volumes currently present in the cluster."""
        labels = {BERTRAND_ENV: "1", REPO_VOLUME_ENV: "1"}
        if repo_id is not None:
            repo_id = _check_uuid(repo_id)
            labels[REPO_ID_ENV] = repo_id
        if timeout <= 0:
            raise TimeoutError("timeout must be non-negative")
        with await Kube.host(timeout=timeout) as kube:
            pvcs = await PersistentVolumeClaim.list(
                kube=kube,
                namespaces=(BERTRAND_NAMESPACE,),
                timeout=timeout,
                labels=labels,
            )
            out: list[Self] = []
            for pvc in pvcs:
                meta = pvc.obj.metadata
                labels = (meta.labels or {}) if meta is not None else {}
                repo_id = labels.get(REPO_ID_ENV, "")
                if not repo_id:
                    raise OSError(
                        "cluster PVC "
                        f"{(meta.name if meta is not None else '')!r} is missing "
                        f"label {REPO_ID_ENV!r}"
                    )
                repo_id = _check_uuid(repo_id)
                cls._assert_managed_pvc(
                    pvc,
                    claim_name=cls._kube_name(repo_id),
                    repo_id=repo_id,
                    storage_class=None,
                    require_rwx=False,
                )
                out.append(cls(repo_id=repo_id, pvc=pvc))

            out.sort(
                key=lambda m: (
                    m.repo_id,
                    (m.pvc.obj.metadata.name if m.pvc.obj.metadata is not None else ""),
                )
            )
            return out

    async def delete(self, *, timeout: float, force: bool) -> None:
        """Delete this repository volume claim from the cluster."""
        if timeout <= 0:
            raise TimeoutError("timeout must be non-negative")
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        with await Kube.host(timeout=deadline - loop.time()) as kube:
            if not force:
                pods = await Pod.list(
                    kube=kube,
                    namespaces=(BERTRAND_NAMESPACE,),
                    timeout=deadline - loop.time(),
                    labels={BERTRAND_ENV: "1", REPO_ID_ENV: self.repo_id},
                )
                active = {
                    claim_name
                    for pod in pods
                    if pod.is_active
                    for claim_name in pod.persistent_volume_claim_names
                }
                active.discard("")
                meta = self.pvc.obj.metadata
                name = meta.name if meta is not None else None
                if name in active:
                    raise OSError(
                        f"cannot delete repository volume {name!r} while "
                        "it is being used by active pods"
                    )

            await self.pvc.delete(kube=kube, timeout=deadline - loop.time())

    async def resolve_ceph_path(self, *, timeout: float) -> PosixPath:
        """Resolve this repo claim's CephFS path from bound PVC/PV metadata."""
        if timeout <= 0:
            raise TimeoutError("timeout must be non-negative")
        meta = self.pvc.obj.metadata
        name = (meta.name or "") if meta is not None else ""
        namespace = (meta.namespace or "") if meta is not None else ""
        if not name:
            raise OSError("cannot resolve Ceph path for PVC with missing metadata.name")
        if not namespace:
            raise OSError(
                f"cannot resolve Ceph path for PVC {name!r} with missing metadata.namespace"
            )

        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        with await Kube.host(timeout=deadline - loop.time()) as kube:
            pvc = await self.pvc.wait_bound(
                kube=kube,
                timeout=deadline - loop.time(),
            )
            volume_name = pvc.volume_name
            volume = await PersistentVolume.wait_present(
                kube=kube,
                timeout=deadline - loop.time(),
                name=volume_name,
            )

            driver = volume.csi_driver
            if not driver:
                raise OSError(
                    f"PersistentVolume {volume_name!r} is not CSI-backed and cannot be "
                    "mounted as a Ceph repository volume"
                )
            if "cephfs" not in driver.lower():
                raise OSError(
                    f"PersistentVolume {volume_name!r} uses CSI driver {driver!r}, "
                    "expected a CephFS driver"
                )

            ceph_path = volume.ceph_path
            if ceph_path is None:
                raise OSError(
                    "repository PersistentVolume is missing CephFS path attributes "
                    "(expected one of 'subvolumePath', 'rootPath', or 'path')"
                )
            return ceph_path
