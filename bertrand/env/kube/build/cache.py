"""Build cache volume lifecycle helpers for Bertrand's Kubernetes runtime."""

from __future__ import annotations

import asyncio
import hashlib
import json
from dataclasses import dataclass
from typing import Self

from ...config import RESOURCE_NAMES, Bertrand, Config, Resource
from ...config.core import KUBE_SANITIZE_RE, AbsolutePosixPath, KubeName, _check_uuid
from ...run import BERTRAND_ENV, BERTRAND_NAMESPACE, ENV_ID_ENV
from ..api import Kube
from ..pod import Pod
from ..volume import PersistentVolumeClaim, StorageClass

CACHE_VOLUME_ENV: str = "BERTRAND_CACHE_VOLUME"


@dataclass(frozen=True)
class CacheVolume:
    """Structured metadata for a cache volume declaration from a resource."""

    name: KubeName
    target: AbsolutePosixPath

    @classmethod
    async def from_config(cls, config: Config, tag: str, env_id: str) -> list[Self]:
        """Collect and validate cache volume specifications for a build tag.

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
            Deterministically ordered list of `CacheVolume` objects. Each object
            contains the volume name, which can be used to identify the corresponding
            PVC in the cluster, and the target path, which is the absolute container
            path where the volume should be mounted.
        """
        env_id = _check_uuid(env_id)
        volumes: list[Self] = []
        target_owner: dict[str, str] = {}

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
                    f"volume hook for resource '{resource.name}' must return a list, "
                    f"got {type(declared).__name__}"
                )

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
                        f"resource '{resource.name}' mount target cannot contain '.' "
                        f"or '..' segments: {target}"
                    )
                target_key = target.as_posix()
                owner = target_owner.setdefault(target_key, resource.name)
                if owner != resource.name:
                    raise OSError(
                        f"volume target collision at '{target_key}' between resources "
                        f"'{owner}' and '{resource.name}'"
                    )

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
        if storage_class is not None and storage_class_name != storage_class:
            raise OSError(
                f"cluster PVC {claim_name!r} uses storage class "
                f"{storage_class_name!r}, expected {storage_class!r}"
            )
        if require_rwo and "ReadWriteOnce" not in access_modes:
            raise OSError(f"cluster PVC {claim_name!r} must include ReadWriteOnce access mode")

    @classmethod
    async def ensure(
        cls,
        config: Config,
        tag: str,
        env_id: str,
        *,
        timeout: float,
        storage_class: str,
        size_request: str,
    ) -> None:
        """Ensure deterministic cache PVCs exist for one build tag."""
        env_id = _check_uuid(env_id)
        if timeout <= 0:
            raise TimeoutError("timeout must be non-negative")
        storage_class = storage_class.strip()
        if not storage_class:
            raise ValueError("storage class cannot be empty")
        size_request = size_request.strip()
        if not size_request:
            raise ValueError("size request cannot be empty")
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        with await Kube.host(timeout=deadline - loop.time()) as kube:
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
    async def gc(cls, config: Config, env_id: str, *, timeout: float) -> None:
        """Garbage-collect stale labeled cache PVCs for an environment."""
        env_id = _check_uuid(env_id)
        if timeout <= 0:
            raise TimeoutError("timeout must be non-negative")

        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        bertrand = config.get(Bertrand)
        if bertrand is None:
            return
        with await Kube.host(timeout=deadline - loop.time()) as kube:
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
                for tag in bertrand.build
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


async def format_volumes(config: Config, tag: str, env_id: str) -> list[str]:
    """Render legacy `nerdctl` volume flags for configured cache volumes."""
    flags: list[str] = []
    for volume in await CacheVolume.from_config(config, tag, env_id):
        flags.extend(["-v", f"{volume.name}:{volume.target.as_posix()}"])
    return flags
