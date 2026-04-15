"""Persistent Volume Claim (PVC) requests for Bertrand's environment bootstrapping
and caching mechanisms.
"""
from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from typing import Self

from ..config import RESOURCE_NAMES, Bertrand, Config, Resource
from ..config.core import KUBE_SANITIZE_RE, AbsolutePosixPath, KubeName, _check_uuid
from ..run import (
    BERTRAND_ENV,
    BERTRAND_NAMESPACE,
    ENV_ID_ENV,
    REPO_ID_ENV,
)
from .helper import PersistentVolume, PersistentVolumeClaim, Pod, StorageClass

CACHE_VOLUME_ENV: str = "BERTRAND_CACHE_VOLUME"
REPO_MOUNT_ENV: str = "BERTRAND_REPO_MOUNT"
DEFAULT_VOLUME_SIZE = "16Mi"


@dataclass(frozen=True)
class CacheVolume:
    """Structured metadata for a cache volume declaration from a resource."""
    name: KubeName
    target: AbsolutePosixPath


async def configured_cache_volumes(config: Config, tag: str, env_id: str) -> list[CacheVolume]:
    """Collect and validate cache mount specifications for a build tag.

    Parameters
    ----------
    config : Config
        Active configuration context with resolved resources and registry.
    tag : str
        Active build tag used to query each resource's volume declarations.
    env_id : str
        Canonical environment UUID used for volume name derivation and collision
        checks.

    Returns
    -------
    list[CacheVolume]
        Deterministically ordered list of `CacheVolume` objects.  Each object contains
        the volume name, which can be used to identify the corresponding PVC in the
        cluster, and the target path, which is the absolute container path where the
        volume should be mounted. This information can be written to `Containerfile`
        and workload specs.

    Raises
    ------
    OSError
        If resource volume hooks fail, return invalid types, contain invalid
        targets, or produce non-serializable fingerprint payloads.

    Notes
    -----
    Names are derived as stable hashes over each volume's semantic fingerprint
    plus target path. Target collisions across resources are rejected.
    """
    env_id = _check_uuid(env_id)
    mounts: list[CacheVolume] = []
    target_owner: dict[str, str] = {}

    # scan over all resources associated with this environment
    for name in sorted(config.resources):
        resource = RESOURCE_NAMES[name]
        try:
            declared = await resource.volumes(config, tag)
        except Exception as err:
            raise OSError(
                f"failed to resolve cache volumes for resource '{resource.name}': {err}"
            ) from err
        if not isinstance(declared, list):
            raise OSError(
                f"volume hook for resource '{resource.name}' must return a list, got "
                f"{type(declared).__name__}"
            )

        # collect volume requests for each resource and check for collisions
        for raw in declared:
            if not isinstance(raw, Resource.Volume):
                raise OSError(
                    f"volume hook for resource '{resource.name}' must return "
                    f"`Resource.Volume` entries, got {type(raw).__name__}"
                )
            target = raw.target
            if not target.is_absolute():
                raise OSError(
                    f"resource '{resource.name}' mount target must be absolute: {target}"
                )
            if any(part in (".", "..") for part in target.parts):
                raise OSError(
                    f"resource '{resource.name}' mount target cannot contain '.' or '..' "
                    f"segments: {target}"
                )
            target_key = target.as_posix()
            owner = target_owner.setdefault(target_key, resource.name)
            if owner != resource.name:
                raise OSError(
                    f"volume target collision at '{target_key}' between resources "
                    f"'{owner}' and '{resource.name}'"
                )

            # compute semantic hash
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
                raise OSError(
                    f"resource '{resource.name}' mount '{target_key}' has invalid "
                    f"fingerprint payload: {err}"
                ) from err

            # derive a stable volume name
            volume_name = KUBE_SANITIZE_RE.sub(
                "-",
                f"bertrand-cache-{resource.name}-{digest}",
            ).strip("-")
            mounts.append(CacheVolume(name=volume_name, target=target))

    mounts.sort(key=lambda mv: (mv.name, mv.target))
    return mounts


def _assert_managed_cache(
    pvc: PersistentVolumeClaim,
    *,
    claim_name: str,
    env_id: str,
    storage_class: str | None,
    require_rwo: bool,
) -> None:
    labels = pvc.metadata.labels
    if labels.get(BERTRAND_ENV) != "1" or labels.get(CACHE_VOLUME_ENV) != "1":
        raise OSError(
            f"cluster PVC {claim_name!r} has missing required labels "
            f"{BERTRAND_ENV!r}=1 and {CACHE_VOLUME_ENV!r}=1"
        )
    actual_env_id = labels.get(ENV_ID_ENV)
    if actual_env_id != env_id:
        raise OSError(
            f"cluster PVC {claim_name!r} has mismatched environment identity label "
            f"{ENV_ID_ENV!r}: expected {env_id!r}, got {actual_env_id!r}"
        )
    if storage_class is not None and pvc.spec.storageClassName != storage_class:
        raise OSError(
            f"cluster PVC {claim_name!r} uses storage class "
            f"{pvc.spec.storageClassName!r}, expected {storage_class!r}"
        )
    if require_rwo and "ReadWriteOnce" not in pvc.spec.accessModes:
        raise OSError(
            f"cluster PVC {claim_name!r} must include ReadWriteOnce access mode"
        )


async def ensure_cache_volumes(
    config: Config,
    tag: str,
    env_id: str,
    *,
    timeout: float,
    storage_class: str,
    size_request: str,
) -> None:
    """Ensure deterministic cache PVCs exist for one build tag.

    Parameters
    ----------
    config : Config
        Active configuration context.
    tag : str
        Active build tag used to resolve requested cache mounts.
    env_id : str
        Canonical environment UUID used for managed PVC labels.
    timeout : float
        Maximum runtime command timeout in seconds.
    storage_class : str
        StorageClass name used for claim creation and validation.
    size_request : str
        Requested PVC storage quantity for new claims and resize checks.

    Returns
    -------
    None
        This function executes for side effects only.

    Raises
    ------
    ValueError
        If `env_id`, `storage_class`, or `size_request` is empty, or if any of the
        PVC payloads fail validation checks.
    TimeoutError
        If `timeout` is negative or if any kube API calls exceed the timeout.
    CommandError
        If any kube API call fails.

    Notes
    -----
    This function assumes kube API reachability is ensured by the caller. It does not
    call `start_microk8s()`.
    """
    env_id = _check_uuid(env_id)
    if timeout < 0:
        raise TimeoutError("timeout must be non-negative")
    storage_class = storage_class.strip()
    if not storage_class:
        raise ValueError("storage class cannot be empty")
    size_request = size_request.strip()
    if not size_request:
        raise ValueError("size request cannot be empty")

    # get PVC storage class and assert that it supports volume expansion for
    # dynamic resizing
    storage = await StorageClass.get(
        storage_class,
        timeout=timeout
    )
    if storage is None:
        raise OSError(
            f"required {storage_class!r} StorageClass is not available; cache PVC "
            "provisioning cannot proceed"
        )
    if not storage.allowVolumeExpansion:
        raise OSError(
            f"{storage_class!r} StorageClass must set 'allowVolumeExpansion=true' "
            "for Bertrand cache PVC resizing"
        )

    # get/create PVCs for each of this tag's cache mounts
    for volume in await configured_cache_volumes(config, tag, env_id):
        pvc = await PersistentVolumeClaim.get(
            volume.name,
            namespace=BERTRAND_NAMESPACE,
            timeout=timeout
        )
        if pvc is None:  # create a new volume with the requested size
            pvc = await PersistentVolumeClaim.create({
                "apiVersion": "v1",
                "kind": "PersistentVolumeClaim",
                "metadata": {
                    "name": volume.name,
                    "namespace": BERTRAND_NAMESPACE,
                    "labels": {
                        BERTRAND_ENV: "1",
                        CACHE_VOLUME_ENV: "1",
                        ENV_ID_ENV: env_id,
                    },
                },
                "spec": {
                    "accessModes": ["ReadWriteOnce"],
                    "storageClassName": storage.metadata.name,
                    "resources": {
                        "requests": {
                            "storage": size_request,
                        },
                    },
                },
            }, timeout=timeout)

        _assert_managed_cache(
            pvc,
            claim_name=volume.name,
            env_id=env_id,
            storage_class=storage_class,
            require_rwo=True,
        )

        # try to grow existing volume if necessary
        await pvc.grow(size_request, timeout=timeout)


async def gc_cache_volumes(config: Config, env_id: str, *, timeout: float) -> None:
    """Garbage-collect stale labeled cache PVCs for an environment.

    Parameters
    ----------
    config : Config
        Active configuration context.
    env_id : str
        Canonical environment UUID used to scope labeled cache PVCs.
    timeout : float
        Maximum runtime command timeout in seconds.

    Returns
    -------
    None
        This function executes for side effects only.

    Raises
    ------
    ValueError
        If `env_id` is empty or any PVC payloads fail validation checks.
    TimeoutError
        If `timeout` is negative or if any kube API calls exceed the timeout.
    CommandError
        If any kube API call fails.

    Notes
    -----
    Only Bertrand-labeled cache PVCs for this environment are candidates. Claims
    currently referenced by active pods are never deleted.
    """
    env_id = _check_uuid(env_id)
    if timeout < 0:
        raise TimeoutError("timeout must be non-negative")
    bertrand = config.get(Bertrand)
    if bertrand is None:
        return

    # get all PVCs associated with this environment
    actual = (await PersistentVolumeClaim.List.get(
        {BERTRAND_ENV: "1", CACHE_VOLUME_ENV: "1", ENV_ID_ENV: env_id},
        namespace=BERTRAND_NAMESPACE,
        timeout=timeout,
    )).items
    if not actual:
        return  # no volumes to clean up

    # get PVCs with active pods
    active = {
        volume.persistentVolumeClaim.claimName
        for pod in (await Pod.List.get(
            {BERTRAND_ENV: "1", ENV_ID_ENV: env_id},
            namespace=BERTRAND_NAMESPACE,
            timeout=timeout,
        )).items if (
            not pod.metadata.deletionTimestamp and
            pod.status.phase in {"Pending", "Running", "Unknown"}
        )
        for volume in pod.spec.volumes if volume.persistentVolumeClaim is not None
    }
    active.discard("")

    # get expected PVCs for this environment based on current semantic hash
    expected = {
        volume.name
        for tag in bertrand.build
        for volume in await configured_cache_volumes(config, tag, env_id)
    }

    # delete actual claims whose names are not in the expected and active sets
    stale = [
        pvc for pvc in actual if (
            pvc.metadata.name and
            pvc.metadata.name not in expected and
            pvc.metadata.name not in active
        )
    ]
    for pvc in stale:
        _assert_managed_cache(
            pvc,
            claim_name=pvc.metadata.name,
            env_id=env_id,
            storage_class=None,
            require_rwo=False,
        )
        await pvc.delete(timeout=timeout)


@dataclass(frozen=True)
class RepoMount:
    """Structured metadata for a CephFS-backed repository volume in the local cluster.

    One of these volumes will be created whenever Bertrand initializes a new project.
    The resulting volume will be mounted to a private directory on the host system,
    and a symlink to the mounted directory will be placed at the initialized path.
    The volume itself will always store a single git repository together with one or
    more worktrees, along with any extra metadata needed to manage the volume itself.

    Attributes
    ----------
    repo_id : str
        Stable repository identity used for volume naming and management.  This is a
        UUID hex string that is generated during repository initialization and stored
        in the repository's metadata for the lifetime of the volume.
    pvc : PersistentVolumeClaim
        Kubernetes PVC object representing the claim for this repository volume in the
        cluster.
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
        labels = pvc.metadata.labels
        if labels.get(BERTRAND_ENV) != "1" or labels.get(REPO_MOUNT_ENV) != "1":
            raise OSError(
                f"cluster PVC {claim_name!r} collides with Bertrand mount claim name but "
                "is unmanaged"
            )
        actual_repo_id = labels.get(REPO_ID_ENV)
        if actual_repo_id != repo_id:
            raise OSError(
                f"cluster PVC {claim_name!r} has mismatched repo identity label "
                f"{REPO_ID_ENV!r}: expected {repo_id!r}, got {actual_repo_id!r}"
            )
        if storage_class is not None and pvc.spec.storageClassName != storage_class:
            raise OSError(
                f"cluster PVC {claim_name!r} uses storage class "
                f"{pvc.spec.storageClassName!r}, expected {storage_class!r}"
            )
        if require_rwx and "ReadWriteMany" not in pvc.spec.accessModes:
            raise OSError(
                f"cluster PVC {claim_name!r} must include ReadWriteMany access mode"
            )

    @classmethod
    async def get(cls, repo_id: str | None, *, timeout: float) -> list[Self]:
        """List repository volumes currently present in the cluster.

        Parameters
        ----------
        repo_id : str | None
            If provided, filter the repository volumes by this specific repository ID.
        timeout : float
            Maximum runtime command timeout in seconds.

        Returns
        -------
        list[RepoMount]
            Structured metadata for each repository mount claim found in the cluster
            that matches the filters.
        """
        labels = {BERTRAND_ENV: "1", REPO_MOUNT_ENV: "1"}
        if repo_id is not None:
            repo_id = _check_uuid(repo_id)
            labels[REPO_ID_ENV] = repo_id
        if timeout < 0:
            raise TimeoutError("timeout must be non-negative")

        # get matching PVCs
        pvcs = (await PersistentVolumeClaim.List.get(
            labels,
            namespace=BERTRAND_NAMESPACE,
            timeout=timeout,
        )).items
        out: list[Self] = []
        for pvc in pvcs:
            repo_id = pvc.metadata.labels.get(REPO_ID_ENV, "")
            if not repo_id:
                raise OSError(
                    f"cluster PVC {pvc.metadata.name!r} is missing label {REPO_ID_ENV!r}"
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

        # deterministically order the output
        out.sort(key=lambda m: (m.repo_id, m.pvc.metadata.name))
        return out

    @classmethod
    async def create(
        cls,
        repo_id: str,
        *,
        timeout: float,
        storage_class: str,
        size_request: str,
    ) -> Self:
        """Ensure a deterministic, cluster-wide RWX claim exists for one repository
        identity.

        Parameters
        ----------
        repo_id : str
            Stable, caller-provided repository identity used for deterministic claim names.
        timeout : float
            Maximum runtime command timeout in seconds.
        storage_class : str
            StorageClass name used for claim creation and validation.
        size_request : str
            Requested storage quantity for initial creation and resize checks.

        Returns
        -------
        RepoMount
            Structured metadata for the created/grown repository mount claim.

        Raises
        ------
        ValueError
            If `repo_id`, `storage_class`, or `size_request` is empty, or if the PVC
            payload fails validation checks.
        TimeoutError
            If `timeout` is negative or if any kube API calls exceed the timeout.
        CommandError
            If any kube API call fails.
        """
        repo_id = _check_uuid(repo_id)
        if timeout < 0:
            raise TimeoutError("timeout must be non-negative")
        storage_class = storage_class.strip()
        if not storage_class:
            raise ValueError("storage class cannot be empty")
        size_request = size_request.strip()
        if not size_request:
            raise ValueError("size request cannot be empty")
        claim_name = cls._kube_name(repo_id)

        # get PVC storage class and assert that it supports volume expansion for
        # dynamic resizing
        storage = await StorageClass.get(
            storage_class,
            timeout=timeout
        )
        if storage is None:
            raise OSError(
                f"required storage class {storage_class!r} is not available; repository "
                "mount provisioning cannot proceed"
            )
        if not storage.allowVolumeExpansion:
            raise OSError(
                f"storage class {storage_class!r} must set allowVolumeExpansion=true "
                "for Bertrand repository mount resizing"
            )

        # get existing PVC or create a new one if missing
        pvc = await PersistentVolumeClaim.get(
            claim_name,
            namespace=BERTRAND_NAMESPACE,
            timeout=timeout,
        )
        if pvc is None:
            pvc = await PersistentVolumeClaim.create({
                "apiVersion": "v1",
                "kind": "PersistentVolumeClaim",
                "metadata": {
                    "name": claim_name,
                    "namespace": BERTRAND_NAMESPACE,
                    "labels": {
                        BERTRAND_ENV: "1",
                        REPO_MOUNT_ENV: "1",
                        REPO_ID_ENV: repo_id,
                    },
                },
                "spec": {
                    "accessModes": ["ReadWriteMany"],
                    "storageClassName": storage_class,
                    "resources": {
                        "requests": {
                            "storage": size_request,
                        },
                    },
                },
            }, timeout=timeout)

        cls._assert_managed_pvc(
            pvc,
            claim_name=claim_name,
            repo_id=repo_id,
            storage_class=storage_class,
            require_rwx=True,
        )

        # try to grow existing volume if necessary
        await pvc.grow(size_request, timeout=timeout)
        return cls(repo_id=repo_id, pvc=pvc)

    async def delete(self, *, timeout: float, force: bool) -> None:
        """Delete this repository mount claim from the cluster.

        Parameters
        ----------
        timeout : float
            Maximum runtime command timeout in seconds.
        force : bool
            If True, delete the claim even if it is currently referenced by active pods.
        """
        if timeout < 0:
            raise TimeoutError("timeout must be non-negative")

        # check for running pods associated with this pvc, unless overridden
        if not force:
            pods = (await Pod.List.get(
                {BERTRAND_ENV: "1", REPO_ID_ENV: self.repo_id},
                namespace=BERTRAND_NAMESPACE,
                timeout=timeout,
            )).items
            active = {
                volume.persistentVolumeClaim.claimName
                for pod in pods if (
                    not pod.metadata.deletionTimestamp and
                    pod.status.phase in {"Pending", "Running", "Unknown"}
                )
                for volume in pod.spec.volumes if volume.persistentVolumeClaim is not None
            }
            active.discard("")
            if self.pvc.metadata.name in active:
                raise OSError(
                    f"cannot delete repository mount {self.pvc.metadata.name!r} while "
                    "it is being used by active pods"
                )

        # delete the volume, ignoring race conditions
        await self.pvc.delete(timeout=timeout)

    async def resolve_ceph_path(self, *, timeout: float) -> str:
        """Resolve this repo claim's CephFS path from its bound PVC/PV metadata.

        Parameters
        ----------
        timeout : float
            Maximum runtime command timeout in seconds.
        """
        volume_name = (self.pvc.spec.volumeName or "").strip()
        if not volume_name:
            raise OSError(
                f"repository claim {self.pvc.metadata.name!r} is not bound to a "
                "PersistentVolume"
            )
        volume = await PersistentVolume.get(volume_name, timeout=timeout)
        if volume is None:
            raise OSError(
                f"repository claim {self.pvc.metadata.name!r} references missing "
                f"PersistentVolume {volume_name!r}"
            )
        csi = volume.spec.csi
        if csi is None:
            raise OSError(
                f"PersistentVolume {volume_name!r} is not CSI-backed and cannot be "
                "mounted as a Ceph repository volume"
            )
        driver = csi.driver.lower()
        if "cephfs" not in driver:
            raise OSError(
                f"PersistentVolume {volume_name!r} uses CSI driver {csi.driver!r}, "
                "expected a CephFS driver"
            )
        for key in ("subvolumePath", "rootPath", "path"):
            value = csi.volumeAttributes.get(key, "").strip()
            if not value:
                continue
            if not value.startswith("/"):
                value = f"/{value}"
            return value
        raise OSError(
            "repository PersistentVolume is missing CephFS path attributes (expected "
            "one of 'subvolumePath', 'rootPath', or 'path')"
        )
