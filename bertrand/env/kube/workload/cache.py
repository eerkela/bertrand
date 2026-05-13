"""Resource cache volume lifecycle helpers for Bertrand workloads."""

from __future__ import annotations

import asyncio
import hashlib
import json
from dataclasses import dataclass
from typing import TYPE_CHECKING, Self

from bertrand.env.config import RESOURCE_NAMES, Bertrand, Config, Resource
from bertrand.env.config.core import (
    KUBE_SANITIZE_RE,
    AbsolutePosixPath,
    KubeName,
    _check_uuid,
)
from bertrand.env.git import BERTRAND_ENV, BERTRAND_NAMESPACE, ENV_ID_ENV
from bertrand.env.kube.api import VolumeMountSpec, VolumeSpec
from bertrand.env.kube.pod import Pod
from bertrand.env.kube.volume import PersistentVolumeClaim, StorageClass

if TYPE_CHECKING:
    from bertrand.env.kube.api import Kube

CACHE_VOLUME_ENV: str = "BERTRAND_CACHE_VOLUME"


@dataclass(frozen=True)
class CacheVolume:
    """Structured metadata for one resource cache volume.

    Parameters
    ----------
    name : KubeName
        Deterministic Kubernetes PVC name for the cache volume.
    target : AbsolutePosixPath
        Absolute container path where the cache volume should be mounted.
    """

    name: KubeName
    target: AbsolutePosixPath

    @property
    def pod_volume_name(self) -> KubeName:
        """Return a compact Pod-local volume name for this cache PVC.

        Returns
        -------
        KubeName
            Deterministic name used inside Pod specs. This is intentionally shorter
            than the PVC name because Kubernetes volume names share DNS label
            constraints while Bertrand cache claim names encode full fingerprints.
        """
        payload = f"{self.name}:{self.target.as_posix()}".encode()
        return f"cache-{hashlib.sha256(payload).hexdigest()[:16]}"

    def volume_spec(self) -> VolumeSpec:
        """Render this cache as a PVC-backed pod volume.

        Returns
        -------
        VolumeSpec
            Pod volume specification that references this cache PVC.
        """
        return VolumeSpec.pvc(self.pod_volume_name, claim_name=self.name)

    def volume_mount(self, *, read_only: bool | None = None) -> VolumeMountSpec:
        """Render this cache as a container volume mount.

        Parameters
        ----------
        read_only : bool | None, optional
            Whether to mount the cache read-only. ``None`` leaves the Kubernetes
            default, which is read-write for PVC mounts.

        Returns
        -------
        VolumeMountSpec
            Container volume mount specification for this cache target.
        """
        return VolumeMountSpec(
            name=self.pod_volume_name,
            mount_path=self.target.as_posix(),
            read_only=read_only,
        )

    @classmethod
    async def from_config(cls, config: Config, tag: str, env_id: str) -> list[Self]:
        """Collect and validate cache volume specifications for an image tag.

        Parameters
        ----------
        config : Config
            Active configuration context with resolved resources and registry.
        tag : str
            Active image tag used to query each resource's volume declarations.
        env_id : str
            Canonical environment UUID used for volume name derivation and collision
            checks.

        Returns
        -------
        list[CacheVolume]
            Deterministically ordered list of `CacheVolume` objects. Each object
            contains the volume name, which can be used to identify the corresponding
            PVC in the cluster, and the target path, which is the absolute container
            path where the volume should be mounted.

        Raises
        ------
        OSError
            If resource volume declarations fail validation or cannot be resolved.
        """
        env_id = _check_uuid(env_id)
        volumes: list[Self] = []
        target_owner: dict[str, str] = {}

        for name in sorted(config.resources):
            resource = RESOURCE_NAMES[name]
            try:
                declared = await resource.volumes(config, tag)
            except Exception as err:
                msg = (
                    f"failed to resolve cache volumes for resource "
                    f"{resource.name!r}: {err}"
                )
                raise OSError(msg) from err
            if not isinstance(declared, list):
                msg = (
                    f"volume hook for resource {resource.name!r} must return a list, "
                    f"got {type(declared).__name__}"
                )
                raise OSError(msg)

            for raw in declared:
                if not isinstance(raw, Resource.Volume):
                    msg = (
                        f"volume hook for resource {resource.name!r} must return "
                        f"`Resource.Volume` entries, got {type(raw).__name__}"
                    )
                    raise OSError(msg)
                target = raw.target
                if not target.is_absolute():
                    msg = (
                        f"resource {resource.name!r} mount target must be absolute: "
                        f"{target}"
                    )
                    raise OSError(msg)
                if any(part in (".", "..") for part in target.parts):
                    msg = (
                        f"resource {resource.name!r} mount target cannot contain '.' "
                        f"or '..' segments: {target}"
                    )
                    raise OSError(msg)
                target_key = target.as_posix()
                owner = target_owner.setdefault(target_key, resource.name)
                if owner != resource.name:
                    msg = (
                        f"volume target collision at '{target_key}' between resources "
                        f"'{owner}' and '{resource.name}'"
                    )
                    raise OSError(msg)

                try:
                    payload = {
                        "env_id": env_id,
                        "fingerprint": dict(raw.fingerprint),
                        "target": target_key,
                    }
                    text = json.dumps(
                        payload,
                        sort_keys=True,
                        separators=(",", ":"),
                        ensure_ascii=False,
                        allow_nan=False,
                    )
                    digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
                except (TypeError, ValueError) as err:
                    msg = (
                        f"resource {resource.name!r} mount '{target_key}' has invalid "
                        f"fingerprint payload: {err}"
                    )
                    raise OSError(msg) from err

                volume_name = KUBE_SANITIZE_RE.sub(
                    "-",
                    f"bertrand-cache-{resource.name}-{digest}",
                ).strip("-")
                volumes.append(cls(name=volume_name, target=target))

        volumes.sort(key=lambda mv: (mv.name, mv.target))
        return volumes

    @staticmethod
    def _assert_managed_cache(
        pvc: PersistentVolumeClaim,
        *,
        claim_name: str,
        env_id: str,
        storage_class: str | None,
        require_rwo: bool,
    ) -> None:
        labels = pvc.labels
        storage_class_name = pvc.storage_class_name
        access_modes = pvc.access_modes
        if labels.get(BERTRAND_ENV) != "1" or labels.get(CACHE_VOLUME_ENV) != "1":
            msg = (
                f"cluster PVC {claim_name!r} has missing required labels "
                f"{BERTRAND_ENV!r}=1 and {CACHE_VOLUME_ENV!r}=1"
            )
            raise OSError(msg)
        actual_env_id = labels.get(ENV_ID_ENV)
        if actual_env_id != env_id:
            msg = (
                f"cluster PVC {claim_name!r} has mismatched environment identity label "
                f"{ENV_ID_ENV!r}: expected {env_id!r}, got {actual_env_id!r}"
            )
            raise OSError(msg)
        if storage_class is not None and storage_class_name != storage_class:
            msg = (
                f"cluster PVC {claim_name!r} uses storage class "
                f"{storage_class_name!r}, expected {storage_class!r}"
            )
            raise OSError(msg)
        if require_rwo and "ReadWriteOnce" not in access_modes:
            msg = f"cluster PVC {claim_name!r} must include ReadWriteOnce access mode"
            raise OSError(msg)

    @classmethod
    async def ensure(
        cls,
        kube: Kube,
        *,
        config: Config,
        tag: str,
        env_id: str,
        timeout: float,
        storage_class: str,
        size_request: str,
    ) -> None:
        """Ensure deterministic cache PVCs exist for one image tag.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        config : Config
            Active configuration context with resolved resources.
        tag : str
            Image tag whose resource cache declarations should be converged.
        env_id : str
            Canonical environment UUID used for cache ownership labels.
        timeout : float
            Maximum runtime budget in seconds. If infinite, wait indefinitely.
        storage_class : str
            StorageClass name to use for cache PVCs.
        size_request : str
            Requested PVC storage size.

        Raises
        ------
        TimeoutError
            If `timeout` is non-positive or convergence exceeds the budget.
        ValueError
            If `storage_class` or `size_request` is empty.
        """
        env_id = _check_uuid(env_id)
        if timeout <= 0:
            msg = "timeout must be non-negative"
            raise TimeoutError(msg)
        storage_class = storage_class.strip()
        if not storage_class:
            msg = "storage class cannot be empty"
            raise ValueError(msg)
        size_request = size_request.strip()
        if not size_request:
            msg = "size request cannot be empty"
            raise ValueError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        storage = await StorageClass.select(
            kube=kube,
            timeout=deadline - loop.time(),
            preferences=(storage_class,),
            require_expansion=True,
        )
        storage_name = storage.name

        for volume in await CacheVolume.from_config(config, tag, env_id):
            pvc = await PersistentVolumeClaim.upsert(
                kube=kube,
                namespace=BERTRAND_NAMESPACE,
                name=volume.name,
                access_modes=("ReadWriteOnce",),
                storage_class=storage_name,
                storage_request=size_request,
                labels={
                    BERTRAND_ENV: "1",
                    CACHE_VOLUME_ENV: "1",
                    ENV_ID_ENV: env_id,
                },
                timeout=deadline - loop.time(),
            )
            cls._assert_managed_cache(
                pvc,
                claim_name=volume.name,
                env_id=env_id,
                storage_class=storage_class,
                require_rwo=True,
            )

    @classmethod
    async def gc(
        cls,
        kube: Kube,
        config: Config,
        env_id: str,
        *,
        timeout: float,
    ) -> None:
        """Garbage-collect stale labeled cache PVCs for an environment.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        config : Config
            Active configuration context used to compute expected cache PVCs.
        env_id : str
            Canonical environment UUID used for cache ownership labels.
        timeout : float
            Maximum runtime budget in seconds. If infinite, wait indefinitely.

        Raises
        ------
        TimeoutError
            If `timeout` is non-positive or garbage collection exceeds the budget.
        """
        env_id = _check_uuid(env_id)
        if timeout <= 0:
            msg = "timeout must be non-negative"
            raise TimeoutError(msg)

        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        bertrand = config.get(Bertrand)
        if bertrand is None:
            return
        actual = await PersistentVolumeClaim.list(
            kube=kube,
            namespaces=(BERTRAND_NAMESPACE,),
            timeout=deadline - loop.time(),
            labels={BERTRAND_ENV: "1", CACHE_VOLUME_ENV: "1", ENV_ID_ENV: env_id},
        )
        if not actual:
            return

        active = {
            claim_name
            for pod in await Pod.list(
                kube=kube,
                namespaces=(BERTRAND_NAMESPACE,),
                timeout=deadline - loop.time(),
                labels={BERTRAND_ENV: "1", ENV_ID_ENV: env_id},
            )
            if pod.is_active
            for claim_name in pod.persistent_volume_claim_names
        }
        active.discard("")

        expected = {
            volume.name
            for tag in bertrand.image
            for volume in await CacheVolume.from_config(config, tag, env_id)
        }

        stale = [
            pvc
            for pvc in actual
            if pvc.name and pvc.name not in expected and pvc.name not in active
        ]
        for pvc in stale:
            claim_name = pvc.name
            if not claim_name:
                continue
            cls._assert_managed_cache(
                pvc,
                claim_name=claim_name,
                env_id=env_id,
                storage_class=None,
                require_rwo=False,
            )
            await pvc.delete(kube=kube, timeout=deadline - loop.time())
