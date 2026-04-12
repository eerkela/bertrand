"""CephFS-backed worktree mounts for Bertrand's runtime bootstrapping mechanism."""
from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from pathlib import PosixPath

from pydantic import ValidationError

from ..config import Config
from ..config.core import _check_uuid
from ..run import (
    BERTRAND_ENV,
    BERTRAND_NAMESPACE,
    PROJECT_MOUNT,
    TOOLS_RUN_DIR,
    CommandError,
    Lock,
    kubectl,
)
from .helper import (
    PersistentVolumeClaim,
    Pod,
    StorageClass,
    parse_pvc_size,
)

WORKTREE_MOUNT_ENV: str = "BERTRAND_WORKTREE_MOUNT"
WORKTREE_MOUNT_LOCK = TOOLS_RUN_DIR / "worktree-mount-pvc.lock"
WORKTREE_MOUNT_MANAGED_V1 = "bertrand.dev/worktree-mount-managed.v1"
WORKTREE_MOUNT_REPO_ID_V1 = "bertrand.dev/worktree-mount-repo-id.v1"
WORKTREE_MOUNT_SCHEMA_V1 = "bertrand.dev/worktree-mount-schema.v1"
WORKTREE_MOUNT_SCHEMA = "1"
DEFAULT_WORKTREE_STORAGE_CLASS = "cephfs"
DEFAULT_WORKTREE_SIZE = "16Mi"
CLAIM_PREFIX = "bertrand-worktree-"


# TODO: review this mount logic similar to what we did for volumes, but in this
# case we have a cluster-wide RWX claim for each repository, and need to include
# helpers to mount that same volume on the host filesystem as well.


# TODO: I'm not totally sure how to provide the repository ID in this case, since Git
# doesn't really have a stable repository identifier that I can use.  That means I
# probably have to add extra metadata in the init or environment relocation code paths,
# which may also involve refactoring the environment registry to support it.


@dataclass(frozen=True)
class WorktreeMountSpec:
    """Resolved mount metadata for one repository-scoped worktree claim."""
    repo_id: str
    claim_name: str


def _claim_name(repo_id: str) -> str:
    repo_id = _check_uuid(repo_id)
    encoded = repo_id.encode("utf-8")
    digest = hashlib.sha256(
        len(encoded).to_bytes(8, "big") + encoded
    ).hexdigest()
    return f"{CLAIM_PREFIX}{digest}"


def _validate_managed_claim(
    pvc: PersistentVolumeClaim,
    *,
    claim_name: str,
    repo_id: str,
    storage_class: str | None = None,
    require_rwx: bool = False,
) -> None:
    labels = pvc.metadata.labels
    if labels.get(BERTRAND_ENV) != "1" or labels.get(WORKTREE_MOUNT_ENV) != "1":
        raise OSError(
            f"cluster PVC {claim_name!r} collides with Bertrand mount claim name but "
            "is unmanaged"
        )

    managed = labels.get(WORKTREE_MOUNT_MANAGED_V1)
    if managed != "true":
        raise OSError(
            f"cluster PVC {claim_name!r} is missing required label "
            f"{WORKTREE_MOUNT_MANAGED_V1!r}"
        )

    schema = pvc.metadata.annotations.get(WORKTREE_MOUNT_SCHEMA_V1)
    if schema != WORKTREE_MOUNT_SCHEMA:
        raise OSError(
            f"cluster PVC {claim_name!r} has missing/invalid annotation "
            f"{WORKTREE_MOUNT_SCHEMA_V1!r}"
        )

    actual_repo_id = pvc.metadata.annotations.get(WORKTREE_MOUNT_REPO_ID_V1)
    if actual_repo_id != repo_id:
        raise OSError(
            f"cluster PVC {claim_name!r} has mismatched repo identity annotation "
            f"{WORKTREE_MOUNT_REPO_ID_V1!r}: expected {repo_id!r}, got "
            f"{actual_repo_id!r}"
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


async def _get_storage_class(
    storage_class: str,
    *,
    timeout: float,
) -> StorageClass:
    payload = (await kubectl(
        [
            "get",
            "storageclass",
            storage_class,
            "-o", "json",
            "--ignore-not-found=true",
        ],
        capture_output=True,
        timeout=timeout,
    )).stdout.strip()
    if not payload:
        raise OSError(
            f"required storage class {storage_class!r} is not available; "
            "worktree mount provisioning cannot proceed"
        )
    try:
        model = StorageClass.model_validate_json(payload)
    except ValidationError as err:
        raise OSError(
            f"storage class {storage_class!r} returned malformed JSON payload"
        ) from err

    probe = f"{model.metadata.name} {model.provisioner}".lower()
    if "cephfs" not in probe:
        raise OSError(
            f"storage class {storage_class!r} is not CephFS-like (name/provisioner "
            "must include 'cephfs')"
        )
    if not model.allowVolumeExpansion:
        raise OSError(
            f"storage class {storage_class!r} must set allowVolumeExpansion=true "
            "for Bertrand worktree claim resizing"
        )
    return model


async def _active_claims(*, timeout: float) -> set[str]:
    payload = (await kubectl(
        ["get", "pods", "-n", BERTRAND_NAMESPACE, "-o", "json"],
        capture_output=True,
        timeout=timeout,
    )).stdout.strip()
    if not payload:
        return set()

    try:
        pods = Pod.List.model_validate_json(payload).items
    except ValidationError as err:
        raise OSError("cluster returned malformed pod list payload") from err
    active = {
        volume.persistentVolumeClaim.claimName
        for pod in pods if (
            not pod.metadata.deletionTimestamp and
            pod.status.phase in {"Pending", "Running", "Unknown"}
        )
        for volume in pod.spec.volumes if volume.persistentVolumeClaim is not None
    }
    active.discard("")
    return active


def resolve_worktree_mount(config: Config, *, repo_id: str) -> WorktreeMountSpec:
    """Resolve repository-scoped worktree mount metadata for one config context.

    Parameters
    ----------
    config : Config
        Active configuration context containing repository/worktree placement data.
    repo_id : str
        Stable caller-provided repository identity used for deterministic claim names.

    Returns
    -------
    WorktreeMountSpec
        The resolved mount metadata.
    """
    repo_id = _check_uuid(repo_id)
    return WorktreeMountSpec(repo_id=repo_id, claim_name=_claim_name(repo_id))


async def ensure_worktree_mount(
    repo_id: str,
    *,
    timeout: float,
    storage_class: str = DEFAULT_WORKTREE_STORAGE_CLASS,
    size_request: str = DEFAULT_WORKTREE_SIZE,
) -> str:
    """Ensure a deterministic RWX CephFS claim exists for one repository identity.

    Parameters
    ----------
    repo_id : str
        Stable caller-provided repository identity used for deterministic claim names.
    timeout : float
        Maximum runtime command timeout in seconds.
    storage_class : str, optional
        StorageClass name used for claim creation and validation.
    size_request : str, optional
        Requested storage quantity for initial creation and resize checks.

    Returns
    -------
    str
        Deterministic Kubernetes PVC name for this repository identity.
    """
    repo_id = _check_uuid(repo_id)
    if timeout < 0:
        raise TimeoutError("timeout must be non-negative")
    storage_class = storage_class.strip()
    if not storage_class:
        raise ValueError("worktree mount storage class cannot be empty")
    size_request = size_request.strip()
    if not size_request:
        raise ValueError("worktree mount size request cannot be empty")
    requested = parse_pvc_size(size_request)
    claim_name = _claim_name(repo_id)

    TOOLS_RUN_DIR.mkdir(parents=True, exist_ok=True)
    async with Lock(WORKTREE_MOUNT_LOCK, timeout=timeout, mode="local"):
        await _get_storage_class(storage_class, timeout=timeout)
        payload = (await kubectl(
            [
                "get",
                "pvc",
                claim_name,
                "-n", BERTRAND_NAMESPACE,
                "-o", "json",
                "--ignore-not-found=true",
            ],
            capture_output=True,
            timeout=timeout,
        )).stdout.strip()

        if not payload:
            try:
                await kubectl(
                    ["create", "-f", "-"],
                    input=json.dumps(
                        {
                            "apiVersion": "v1",
                            "kind": "PersistentVolumeClaim",
                            "metadata": {
                                "name": claim_name,
                                "namespace": BERTRAND_NAMESPACE,
                                "labels": {
                                    BERTRAND_ENV: "1",
                                    WORKTREE_MOUNT_ENV: "1",
                                    WORKTREE_MOUNT_MANAGED_V1: "true",
                                },
                                "annotations": {
                                    WORKTREE_MOUNT_REPO_ID_V1: repo_id,
                                    WORKTREE_MOUNT_SCHEMA_V1: WORKTREE_MOUNT_SCHEMA,
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
                        },
                        separators=(",", ":"),
                        ensure_ascii=False,
                    ),
                    capture_output=True,
                    timeout=timeout,
                )
                return claim_name
            except CommandError as err:
                detail = f"{err.stdout}\n{err.stderr}".lower()
                if (
                    "already exists" not in detail and
                    "alreadyexists" not in detail and
                    "conflict" not in detail
                ):
                    raise OSError(
                        f"failed to create worktree PVC {claim_name!r}"
                    ) from err

                payload = (await kubectl(
                    [
                        "get",
                        "pvc",
                        claim_name,
                        "-n", BERTRAND_NAMESPACE,
                        "-o", "json",
                    ],
                    capture_output=True,
                    timeout=timeout,
                )).stdout.strip()

        try:
            pvc = PersistentVolumeClaim.model_validate_json(payload)
        except ValidationError as err:
            raise OSError(
                f"cluster PVC {claim_name!r} returned malformed JSON payload"
            ) from err
        _validate_managed_claim(
            pvc,
            claim_name=claim_name,
            repo_id=repo_id,
            storage_class=storage_class,
            require_rwx=True,
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
                    claim_name,
                    "-n", BERTRAND_NAMESPACE,
                    "--type", "merge",
                    "-p", patch,
                ],
                capture_output=True,
                timeout=timeout,
            )
    return claim_name


async def list_worktree_mounts(*, timeout: float) -> list[tuple[str, str]]:
    """List managed repository-scoped worktree claims.

    Parameters
    ----------
    timeout : float
        Maximum runtime command timeout in seconds.

    Returns
    -------
    list[tuple[str, str]]
        Deterministically sorted `(repo_id, claim_name)` pairs.
    """
    if timeout < 0:
        raise TimeoutError("timeout must be non-negative")
    selector = f"{BERTRAND_ENV}=1,{WORKTREE_MOUNT_ENV}=1,{WORKTREE_MOUNT_MANAGED_V1}=true"
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
    if not payload:
        return []

    try:
        items = PersistentVolumeClaim.List.model_validate_json(payload).items
    except ValidationError as err:
        raise OSError("cluster returned malformed worktree PVC list payload") from err

    out: list[tuple[str, str]] = []
    for pvc in items:
        name = pvc.metadata.name
        if not name:
            continue
        repo_id = pvc.metadata.annotations.get(WORKTREE_MOUNT_REPO_ID_V1, "")
        if not repo_id:
            raise OSError(
                f"cluster PVC {name!r} is missing annotation "
                f"{WORKTREE_MOUNT_REPO_ID_V1!r}"
            )
        out.append((_check_uuid(repo_id), name))
    out.sort(key=lambda item: (item[0], item[1]))
    return out


async def delete_worktree_mount(
    repo_id: str,
    *,
    timeout: float,
    force: bool = False,
) -> bool:
    """Delete one managed repository-scoped worktree claim.

    Parameters
    ----------
    repo_id : str
        Stable caller-provided repository identity used for deterministic claim names.
    timeout : float
        Maximum runtime command timeout in seconds.
    force : bool, optional
        If True, bypass active-pod usage checks.

    Returns
    -------
    bool
        True if the claim existed and delete was attempted, False if missing.
    """
    repo_id = _check_uuid(repo_id)
    if timeout < 0:
        raise TimeoutError("timeout must be non-negative")
    claim_name = _claim_name(repo_id)

    TOOLS_RUN_DIR.mkdir(parents=True, exist_ok=True)
    async with Lock(WORKTREE_MOUNT_LOCK, timeout=timeout, mode="local"):
        payload = (await kubectl(
            [
                "get",
                "pvc",
                claim_name,
                "-n", BERTRAND_NAMESPACE,
                "-o", "json",
                "--ignore-not-found=true",
            ],
            capture_output=True,
            timeout=timeout,
        )).stdout.strip()
        if not payload:
            return False

        try:
            pvc = PersistentVolumeClaim.model_validate_json(payload)
        except ValidationError as err:
            raise OSError(
                f"cluster PVC {claim_name!r} returned malformed JSON payload"
            ) from err
        _validate_managed_claim(
            pvc,
            claim_name=claim_name,
            repo_id=repo_id,
        )

        if not force:
            active = await _active_claims(timeout=timeout)
            if claim_name in active:
                raise OSError(
                    f"cannot delete worktree claim {claim_name!r}: it is referenced "
                    "by active pods (retry with force=True after draining workloads)"
                )

        await kubectl(
            [
                "delete",
                "pvc",
                claim_name,
                "-n", BERTRAND_NAMESPACE,
                "--ignore-not-found=true",
            ],
            capture_output=True,
            timeout=timeout,
        )
        return True
