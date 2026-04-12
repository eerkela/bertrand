"""Persistent cache volumes managed by Bertrand's runtime bootstrapping mechanism."""
from __future__ import annotations

import hashlib
import json
from pathlib import PosixPath

from ..config import RESOURCE_NAMES, Bertrand, Config, Resource
from ..config.core import KUBE_SANITIZE_RE, _check_uuid
from ..run import (
    BERTRAND_ENV,
    BERTRAND_NAMESPACE,
    ENV_ID_ENV,
    TOOLS_RUN_DIR,
    CommandError,
    Lock,
    kubectl,
    run,
)
from .helper import (
    PersistentVolumeClaim,
    Pod,
    StorageClass,
    parse_pvc_size,
)

CACHE_VOLUME_ENV: str = "BERTRAND_CACHE_VOLUME"
CACHE_PVC_LOCK = TOOLS_RUN_DIR / "cache-pvc.lock"
OPENEBS_ADDON = "openebs"
OPENEBS_HOSTPATH_STORAGE_CLASS = "openebs-hostpath"
DEFAULT_CACHE_SIZE = "16Mi"


async def collect_mount_specs(
    config: Config,
    tag: str,
    env_id: str
) -> list[tuple[str, PosixPath]]:
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
    list[tuple[str, PosixPath]]
        Deterministically ordered pairs of `(volume_name, target_path)`.

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
    mounts: list[tuple[str, PosixPath]] = []
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
                f"volume hook for resource '{resource.name}' must return a list, got "
                f"{type(declared).__name__}"
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
            except ValueError as err:
                raise OSError(
                    f"resource '{resource.name}' mount '{target_key}' has invalid "
                    f"fingerprint payload: {err}"
                ) from err

            volume_name = KUBE_SANITIZE_RE.sub(
                "-",
                f"bertrand-cache-{resource.name}-{digest}",
            ).strip("-")
            mounts.append((volume_name, target))

    mounts.sort()
    return mounts


async def _get_storage_class(*, timeout: float) -> StorageClass:
    # enable OpenEBS addon if not already enabled
    try:
        await run(
            ["microk8s", "enable", OPENEBS_ADDON],
            capture_output=True,
            timeout=timeout,
        )
    except CommandError as err:
        detail = f"{err.stdout}\n{err.stderr}".strip().lower()
        if "already enabled" not in detail and "alreadyenabled" not in detail:
            raise OSError(
                f"failed to enable MicroK8s OpenEBS addon required for cache PVCs:\n{err}"
            ) from err

    # get OpenEBS storage classes
    storage = await kubectl(
        ["get", "storageclass", "-o", "json"],
        capture_output=True,
        timeout=timeout,
    )
    model = StorageClass.List.model_validate_json(storage.stdout)

    # search for a host-local OpenEBS storage class that supports volume expansion
    hostpath = next((
        item for item in model.items
        if item.metadata.name == OPENEBS_HOSTPATH_STORAGE_CLASS
    ), None)
    if hostpath is None:
        raise OSError(
            f"required {OPENEBS_HOSTPATH_STORAGE_CLASS!r} StorageClass is not "
            "available; cache PVC provisioning cannot proceed"
        )
    if not hostpath.allowVolumeExpansion:
        raise OSError(
            f"{OPENEBS_HOSTPATH_STORAGE_CLASS!r} StorageClass must set "
            "'allowVolumeExpansion=true' for Bertrand cache PVC resizing"
        )
    return hostpath


async def ensure_cache_volumes(
    config: Config,
    tag: str,
    env_id: str,
    *,
    timeout: float,
    size_request: str = DEFAULT_CACHE_SIZE,
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
    size_request : str, optional
        Requested PVC storage quantity for new claims and resize checks.

    Returns
    -------
    None
        This function executes for side effects only.

    Raises
    ------
    ValueError
        If `env_id` or `size_request` is empty, or if any of the PVC payloads fail
        validation checks.
    TimeoutError
        If `timeout` is negative or if any kube API calls exceed the timeout.
    CommandError
        If any kube API call fails.

    Notes
    -----
    This function assumes kube API reachability is ensured by the caller. It does not
    call `ensure_kube()`.
    """
    env_id = _check_uuid(env_id)
    if timeout < 0:
        raise TimeoutError("timeout must be non-negative")
    size_request = size_request.strip()
    if not size_request:
        raise ValueError("cache PVC size request cannot be empty")
    requested = parse_pvc_size(size_request)

    TOOLS_RUN_DIR.mkdir(parents=True, exist_ok=True)
    async with Lock(CACHE_PVC_LOCK, timeout=timeout, mode="local"):
        storage_class = await _get_storage_class(timeout=timeout)

        # get/create PVCs for each of this tag's cache mounts
        for pvc_name, _ in await collect_mount_specs(config, tag, env_id):
            payload = (await kubectl(
                [
                    "get",
                    "pvc",
                    pvc_name,
                    "-n", BERTRAND_NAMESPACE,
                    "-o", "json",
                    "--ignore-not-found=true",
                ],
                capture_output=True,
                timeout=timeout,
            )).stdout.strip()

            # if missing, create an initial claim with the requested size
            if not payload:
                await kubectl(
                    ["create", "-f", "-"],
                    input=json.dumps(
                        {
                            "apiVersion": "v1",
                            "kind": "PersistentVolumeClaim",
                            "metadata": {
                                "name": pvc_name,
                                "namespace": BERTRAND_NAMESPACE,
                                "labels": {
                                    BERTRAND_ENV: "1",
                                    CACHE_VOLUME_ENV: "1",
                                    ENV_ID_ENV: env_id,
                                },
                            },
                            "spec": {
                                "accessModes": ["ReadWriteOnce"],
                                "storageClassName": storage_class.metadata.name,
                                "resources": {
                                    "requests": {
                                        "storage": size_request,
                                    },
                                },
                            },
                        },
                        separators=(",", ":"),
                        ensure_ascii=False
                    ),
                    capture_output=True,
                    timeout=timeout,
                )
                continue

            # otherwise, validate the response and check if resizing is needed
            pvc = PersistentVolumeClaim.model_validate_json(
                payload
            )
            current_size = parse_pvc_size(pvc.spec.resources.requests.storage)
            if current_size < requested:
                patch = json.dumps(
                    {"spec": {"resources": {"requests": {"storage": size_request}}}},
                    separators=(",", ":"),
                    ensure_ascii=False,
                )
                await kubectl(
                    [
                        "patch",
                        "pvc",
                        pvc_name,
                        "-n", BERTRAND_NAMESPACE,
                        "--type", "merge",
                        "-p", patch,
                    ],
                    capture_output=True,
                    timeout=timeout,
                )


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

    TOOLS_RUN_DIR.mkdir(parents=True, exist_ok=True)
    async with Lock(CACHE_PVC_LOCK, timeout=timeout, mode="local"):
        # get all PVCs associated with this environment
        selector = f"{BERTRAND_ENV}=1,{CACHE_VOLUME_ENV}=1,{ENV_ID_ENV}={env_id}"
        payload = (await kubectl(
            [
                "get",
                "pvc",
                "-n", BERTRAND_NAMESPACE,
                "-l", selector,
                "-o", "json",
            ],
            capture_output=True,
            timeout=timeout,
        )).stdout.strip()
        actual = {
            item.metadata.name
            for item in PersistentVolumeClaim.List.model_validate_json(
                payload
            ).items
        }
        actual.discard("")
        if not actual:
            return  # no volumes to clean up

        # get PVCs with active pods
        payload = (await kubectl(
            ["get", "pods", "-n", BERTRAND_NAMESPACE, "-o", "json"],
            capture_output=True,
            timeout=timeout,
        )).stdout.strip()
        active = {
            volume.persistentVolumeClaim.claimName
            for pod in Pod.List.model_validate_json(payload).items if (
                not pod.metadata.deletionTimestamp and
                pod.status.phase in {"Pending", "Running", "Unknown"}
            )
            for volume in pod.spec.volumes if volume.persistentVolumeClaim is not None
        }
        active.discard("")

        # get expected PVCs for this environment based on current semantic hash
        expected = {
            name
            for tag in bertrand.build
            for name, _ in await collect_mount_specs(config, tag, env_id)
        }

        # delete actual claims whose names are not in the expected and active sets
        stale = sorted(
            name for name in actual if name not in expected and name not in active
        )
        for name in stale:
            await kubectl(
                [
                    "delete",
                    "pvc", name,
                    "-n", BERTRAND_NAMESPACE,
                    "--ignore-not-found=true",
                ],
                capture_output=True,
                timeout=timeout,
            )
