"""CephFS-backed repository volume lifecycle helpers."""

from __future__ import annotations

import asyncio
import hashlib
from dataclasses import dataclass
from typing import TYPE_CHECKING, Self

from bertrand.env.config.core import _check_uuid
from bertrand.env.git import BERTRAND_ENV, BERTRAND_NAMESPACE, REPO_ID_ENV
from bertrand.env.kube.pod import Pod
from bertrand.env.kube.volume import (
    PersistentVolume,
    PersistentVolumeClaim,
    StorageClass,
)

if TYPE_CHECKING:
    import builtins
    from pathlib import PosixPath

    from bertrand.env.kube.api import Kube

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
    def claim_name(repo_id: str) -> str:
        """Return the deterministic PVC name for a repository identity.

        Parameters
        ----------
        repo_id : str
            Repository UUID used to derive the managed claim name.

        Returns
        -------
        str
            Kubernetes PVC name for the repository volume.
        """
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
        actual_name = pvc.name
        if actual_name != claim_name:
            msg = (
                f"cluster PVC for repo {repo_id!r} has non-deterministic claim name "
                f"{actual_name!r}, expected {claim_name!r}"
            )
            raise OSError(msg)
        labels = pvc.labels
        storage_class_name = pvc.storage_class_name
        access_modes = pvc.access_modes
        if labels.get(BERTRAND_ENV) != "1" or labels.get(REPO_VOLUME_ENV) != "1":
            msg = (
                f"cluster PVC {claim_name!r} collides with Bertrand volume claim, but "
                "is not managed by Bertrand"
            )
            raise OSError(msg)
        actual_repo_id = labels.get(REPO_ID_ENV)
        if actual_repo_id != repo_id:
            msg = (
                f"cluster PVC {claim_name!r} has mismatched repo identity label "
                f"{REPO_ID_ENV!r}: expected {repo_id!r}, got {actual_repo_id!r}"
            )
            raise OSError(msg)
        if storage_class is not None and storage_class_name != storage_class:
            msg = (
                f"cluster PVC {claim_name!r} uses storage class "
                f"{storage_class_name!r}, expected {storage_class!r}"
            )
            raise OSError(msg)
        if require_rwx and "ReadWriteMany" not in access_modes:
            msg = f"cluster PVC {claim_name!r} must include ReadWriteMany access mode"
            raise OSError(msg)

    @classmethod
    async def ensure(
        cls,
        kube: Kube,
        *,
        repo_id: str,
        timeout: float,
        size_request: str,
    ) -> Self:
        """Ensure a deterministic, cluster-wide RWX claim exists.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        repo_id : str
            Repository UUID used to derive the claim name and ownership labels.
        timeout : float
            Maximum convergence budget in seconds.
        size_request : str
            Kubernetes storage request to apply to the claim.

        Returns
        -------
        RepoVolume
            Repository volume wrapper for the converged claim.

        Raises
        ------
        OSError
            If a selected storage class or existing PVC is incompatible.
        TimeoutError
            If `timeout` is non-positive.
        ValueError
            If `repo_id` or `size_request` is invalid.
        """
        repo_id = _check_uuid(repo_id)
        if timeout <= 0:
            msg = "timeout must be non-negative"
            raise TimeoutError(msg)
        size_request = size_request.strip()
        if not size_request:
            msg = "size request cannot be empty"
            raise ValueError(msg)
        claim_name = cls.claim_name(repo_id)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        storage = await StorageClass.select(
            kube=kube,
            timeout=deadline - loop.time(),
            preferences=CEPHFS_STORAGE_CLASS_PREFERENCES,
            require_expansion=True,
        )
        if not storage.is_cephfs:
            msg = (
                f"storage class {storage.name!r} uses provisioner "
                f"{storage.provisioner!r}, but Bertrand repository volumes "
                "require a CephFS CSI provisioner"
            )
            raise OSError(msg)
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
    async def list(
        cls,
        kube: Kube,
        repo_id: str | None,
        *,
        timeout: float,
    ) -> builtins.list[Self]:
        """List repository volumes currently present in the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        repo_id : str | None
            Optional repository UUID to filter for.
        timeout : float
            Maximum list budget in seconds.

        Returns
        -------
        list[RepoVolume]
            Repository volumes sorted by repository identity and claim name.

        Raises
        ------
        OSError
            If a discovered PVC has invalid Bertrand repository ownership metadata.
        TimeoutError
            If `timeout` is non-positive.
        """
        labels = {BERTRAND_ENV: "1", REPO_VOLUME_ENV: "1"}
        if repo_id is not None:
            repo_id = _check_uuid(repo_id)
            labels[REPO_ID_ENV] = repo_id
        if timeout <= 0:
            msg = "timeout must be non-negative"
            raise TimeoutError(msg)
        pvcs = await PersistentVolumeClaim.list(
            kube=kube,
            namespaces=(BERTRAND_NAMESPACE,),
            timeout=timeout,
            labels=labels,
        )
        out: list[Self] = []
        for pvc in pvcs:
            labels = pvc.labels
            repo_id = labels.get(REPO_ID_ENV, "")
            if not repo_id:
                msg = f"cluster PVC {pvc.name!r} is missing label {REPO_ID_ENV!r}"
                raise OSError(msg)
            repo_id = _check_uuid(repo_id)
            cls._assert_managed_pvc(
                pvc,
                claim_name=cls.claim_name(repo_id),
                repo_id=repo_id,
                storage_class=None,
                require_rwx=False,
            )
            out.append(cls(repo_id=repo_id, pvc=pvc))

        out.sort(
            key=lambda m: (
                m.repo_id,
                m.pvc.name,
            )
        )
        return out

    async def delete(self, kube: Kube, *, timeout: float, force: bool) -> None:
        """Delete this repository volume claim from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum deletion budget in seconds.
        force : bool
            Whether to delete even when active pods reference the claim.

        Raises
        ------
        OSError
            If the claim is still active and `force` is False, or deletion fails.
        TimeoutError
            If `timeout` is non-positive.
        """
        if timeout <= 0:
            msg = "timeout must be non-negative"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
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
            name = self.pvc.name
            if name in active:
                msg = (
                    f"cannot delete repository volume {name!r} while "
                    "it is being used by active pods"
                )
                raise OSError(msg)

        await self.pvc.delete(kube=kube, timeout=deadline - loop.time())

    async def resolve_ceph_path(self, kube: Kube, *, timeout: float) -> PosixPath:
        """Resolve this repo claim's CephFS path from bound PVC/PV metadata.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum resolution budget in seconds.

        Returns
        -------
        PosixPath
            Absolute CephFS path backing this repository claim.

        Raises
        ------
        OSError
            If the PVC/PV binding is missing or does not expose a CephFS path.
        TimeoutError
            If `timeout` is non-positive.
        """
        if timeout <= 0:
            msg = "timeout must be non-negative"
            raise TimeoutError(msg)
        name = self.pvc.name
        namespace = self.pvc.namespace
        if not name:
            msg = "cannot resolve Ceph path for PVC with missing metadata.name"
            raise OSError(msg)
        if not namespace:
            msg = (
                f"cannot resolve Ceph path for PVC {name!r} with missing "
                "metadata.namespace"
            )
            raise OSError(msg)

        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
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
            msg = (
                f"PersistentVolume {volume_name!r} is not CSI-backed and cannot be "
                "mounted as a Ceph repository volume"
            )
            raise OSError(msg)
        if "cephfs" not in driver.lower():
            msg = (
                f"PersistentVolume {volume_name!r} uses CSI driver {driver!r}, "
                "expected a CephFS driver"
            )
            raise OSError(msg)

        ceph_path = volume.ceph_path
        if ceph_path is None:
            msg = (
                "repository PersistentVolume is missing CephFS path attributes "
                "(expected one of 'subvolumePath', 'rootPath', or 'path')"
            )
            raise OSError(msg)
        return ceph_path
