"""Host-side Ceph repository mount lifecycle utilities."""

from __future__ import annotations

import os
import platform
import shutil
import sys
import uuid
from pathlib import Path, PosixPath
from typing import TYPE_CHECKING

from bertrand.env.config.core import UUIDHex, _check_uuid
from bertrand.env.git import (
    BERTRAND_NAMESPACE,
    HOST_MOUNTS,
    STATE,
    Deadline,
    Mount,
    abspath,
    run,
    sudo,
    symlink_points_to,
)
from bertrand.env.kube.ceph.auth import RepoCredentials
from bertrand.env.kube.ceph.volume import (
    REPOSITORY_STATE_RESOURCE,
    ensure_repository_mount_record,
    ensure_repository_volume_claim,
    ensure_repository_volume_record,
    list_repository_volume_claims,
    mark_repository_volume_ready,
    resolve_repository_volume_ceph_path,
    retire_repository_mount,
    retire_repository_mount_record,
)

if TYPE_CHECKING:
    from collections.abc import Sequence

    from bertrand.env.kube.api.client import Kube
    from bertrand.env.kube.volume import PersistentVolumeClaim

DEFAULT_REPO_FS_NAME = "ceph"
if not DEFAULT_REPO_FS_NAME:
    msg = "internal default repository Ceph fs_name cannot be empty"
    raise ValueError(msg)
if "," in DEFAULT_REPO_FS_NAME:
    msg = "internal default repository Ceph fs_name cannot contain commas"
    raise ValueError(msg)
DEFAULT_REPO_MOUNT_OPTIONS: tuple[str, ...] = ()
if any("," in opt for opt in DEFAULT_REPO_MOUNT_OPTIONS):
    msg = "internal default repository mount options cannot contain comma separators"
    raise ValueError(msg)
REPOSITORY_MOUNT_PRUNE_LIMIT = 8


def _current_host_id() -> str:
    try:
        return _check_uuid(STATE.id)
    except OSError as err:
        msg = (
            f"failed to read Bertrand host identity at {STATE.id_file}; run "
            "`bertrand init` to repair host state"
        )
        raise OSError(msg) from err


def _single_repository_volume(
    repo_id: str,
    volumes: Sequence[PersistentVolumeClaim],
) -> PersistentVolumeClaim | None:
    if not volumes:
        return None
    if len(volumes) == 1:
        return volumes[0]
    names = ", ".join(sorted(volume.name for volume in volumes))
    msg = (
        "repository identity maps to multiple cluster claims and cannot be "
        f"disambiguated safely for {repo_id!r}: {names}"
    )
    raise OSError(msg)


def _repository_mount_id(mount: Mount) -> UUIDHex | None:
    mount_point = abspath(mount.mount_point)
    try:
        relative = mount_point.relative_to(STATE.mount)
    except ValueError:
        return None
    if len(relative.parts) != 2:
        return None
    try:
        repo_id = uuid.UUID(relative.parts[0]).hex
    except ValueError:
        return None
    return repo_id if mount_point == STATE.mount / repo_id / "mount" else None


def _managed_alias_ancestor(
    path: Path,
) -> tuple[Path, UUIDHex, Mount | None] | None:
    managed = STATE.managed_alias_ancestor(path)
    if managed is None:
        return None
    alias, repo_id = managed
    repo = STATE.repo(repo_id)
    return alias, repo_id, Mount.search(repo.mount)


def ceph_mount_path(mount: Mount) -> PosixPath | None:
    """Best-effort parsed CephFS path suffix from a host mount source.

    Returns
    -------
    PosixPath | None
        Absolute Ceph path when the host mount source is parseable, otherwise None.
    """
    if mount.fs_type.strip().lower() != "ceph":
        return None
    source = mount.source.strip()
    if not source:
        return None
    _, sep, suffix = source.rpartition(":")
    if not sep:
        return None
    suffix = suffix.strip()
    if not suffix.startswith("/"):
        return None
    return PosixPath(suffix)


async def _normalize_mounted_repository_catalog(
    mount_id: str,
    *,
    deadline: Deadline,
) -> tuple[UUIDHex, Mount | None]:
    """Make mounted repository metadata and local mount identity agree.

    Returns
    -------
    tuple[UUIDHex, Mount | None]
        Authoritative repository ID and the mounted entry when present.

    Raises
    ------
    OSError
        If mounted metadata conflicts cannot be repaired safely.
    """
    mount_id = _check_uuid(mount_id)
    repo = STATE.repo(mount_id)
    mounted = Mount.search(repo.mount)
    if mounted is None:
        return mount_id, None

    repaired = await repo.normalize(deadline=deadline)
    repo_id = repaired.repo_id or repaired.mount_id
    repo_id = _check_uuid(repo_id)
    mounted = Mount.search(repaired.mount)
    if mounted is None:
        msg = (
            f"repository metadata id {repo_id} won over mount id {mount_id}, "
            f"but repaired mount {repaired.mount} is not mounted"
        )
        raise OSError(msg)
    return repo_id, mounted


async def ensure_repository_host_mount(
    repo_id: str,
    *,
    ceph_path: PosixPath,
    monitors: Sequence[str],
    ceph_user: str,
    ceph_secretfile: Path,
    deadline: Deadline,
) -> Mount:
    """Ensure a repository hidden Ceph mount exists and return its mount entry.

    Returns
    -------
    Mount
        Mounted hidden repository entry.

    Raises
    ------
    FileNotFoundError
        If the Ceph secret path is missing or is not a regular file.
    OSError
        If mount convergence fails or an incompatible mount already exists.
    ValueError
        If the repository identity or mount inputs are invalid.
    """
    repo_id = _check_uuid(repo_id)
    if not ceph_path.is_absolute():
        ceph_path = PosixPath("/") / ceph_path
    if os.name != "posix" or platform.system() != "Linux":
        msg = "repository mounts are only supported on Linux platforms"
        raise OSError(msg)
    ceph_user = ceph_user.strip()
    if not ceph_user:
        msg = "repository Ceph user cannot be empty"
        raise ValueError(msg)
    if "," in ceph_user:
        msg = "repository Ceph user cannot contain comma separators"
        raise ValueError(msg)
    monitors = [monitor.strip() for monitor in monitors if monitor.strip()]
    if not monitors:
        msg = "repository mount requires at least one Ceph monitor endpoint"
        raise ValueError(msg)
    if any("," in monitor for monitor in monitors):
        msg = "repository Ceph monitor endpoints cannot contain comma separators"
        raise ValueError(msg)

    ceph_secretfile = ceph_secretfile.expanduser().resolve()
    if not ceph_secretfile.exists():
        msg = f"repository Ceph secret file does not exist: {ceph_secretfile}"
        raise FileNotFoundError(msg)
    if not ceph_secretfile.is_file():
        msg = f"repository Ceph secret path is not a file: {ceph_secretfile}"
        raise FileNotFoundError(msg)

    repo_state = STATE.repo(repo_id)
    mount_path = repo_state.mount

    await repo_state.lock.lock(deadline)
    try:
        mounted = Mount.search(mount_path)
        if mounted is None:
            mount_opts = [
                f"name={ceph_user}",
                f"secretfile={ceph_secretfile}",
                f"mds_namespace={DEFAULT_REPO_FS_NAME}",
            ]
            mount_opts.extend(DEFAULT_REPO_MOUNT_OPTIONS)
            await repo_state.init(deadline=deadline)
            await run(
                sudo(
                    [
                        "mount",
                        "-t",
                        "ceph",
                        f"{','.join(monitors)}:{ceph_path}",
                        str(mount_path),
                        "-o",
                        ",".join(mount_opts),
                    ]
                ),
                capture_output=True,
                deadline=deadline,
            )
            mounted = Mount.search(mount_path)
            if mounted is None:
                msg = (
                    f"repository mount target {mount_path!r} failed to appear in "
                    f"{HOST_MOUNTS} after successful mount subcommand"
                )
                raise OSError(msg)

        mounted_ceph_path = ceph_mount_path(mounted)
        if mounted_ceph_path is None:
            msg = (
                f"repository mount target {mounted.mount_point!r} is mounted with "
                f"unsupported source {mounted.source!r}, expected parseable Ceph mount"
            )
            raise OSError(msg)
        if mounted_ceph_path != ceph_path:
            msg = (
                f"repository mount target {mounted.mount_point!r} is attached to "
                f"{mounted.source!r}, expected Ceph source suffix ':{ceph_path}'"
            )
            raise OSError(msg)
    finally:
        await repo_state.lock.unlock(ignore_errors=True)

    return mounted


async def ensure_repository_mount(
    kube: Kube,
    *,
    repo_id: UUIDHex,
    target: Path,
    deadline: Deadline,
    size_request: str,
    volumes: Sequence[PersistentVolumeClaim] | None = None,
) -> Path:
    """Converge a repository claim, credentials, hidden mount, and host alias.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : UUIDHex
        Stable repository identity.
    target : Path
        Desired host alias path for the managed repository mount.
    deadline : Deadline
        Maximum convergence budget in seconds.
    size_request : str
        Storage request used when a new repository claim must be created.
    volumes : Sequence[PersistentVolumeClaim] | None, optional
        Optional preloaded repository volume claims for `repo_id`.

    Returns
    -------
    Path
        Host alias path linked to the hidden managed mount.

    Raises
    ------
    OSError
        If an existing repository claim or hidden mount is ambiguous or incompatible.
    """
    repo_id = _check_uuid(repo_id)
    target = abspath(target)
    candidates = (
        list(volumes)
        if volumes is not None
        else await list_repository_volume_claims(
            kube,
            repo_id,
            deadline=deadline,
        )
    )
    repo_state = STATE.repo(repo_id)
    hidden_mount = repo_state.mount
    volume = _single_repository_volume(repo_id, candidates)
    if volume is not None:
        ceph_path = await resolve_repository_volume_ceph_path(
            kube,
            pvc=volume,
            deadline=deadline,
        )
        mounted = Mount.search(hidden_mount)
        mounted_ceph_path = ceph_mount_path(mounted) if mounted is not None else None
        if mounted is not None and mounted_ceph_path != ceph_path:
            msg = (
                f"repository hidden mount {hidden_mount!r} is occupied by "
                f"{mounted.source!r}, expected Ceph source suffix ':{ceph_path}'"
            )
            raise OSError(msg)
    else:
        volume = await ensure_repository_volume_claim(
            kube,
            repo_id=repo_id,
            deadline=deadline,
            size_request=size_request,
        )
        ceph_path = await resolve_repository_volume_ceph_path(
            kube,
            pvc=volume,
            deadline=deadline,
        )
    await ensure_repository_volume_record(
        kube,
        repo_id=repo_id,
        deadline=deadline,
    )

    credentials: RepoCredentials = await RepoCredentials.ensure(
        repo_id=repo_id,
        ceph_path=ceph_path,
        deadline=deadline,
    )
    staged_alias = target.parent / f".{target.name}.bertrand.mount.{repo_id}"
    target_occupied = target.exists() or target.is_symlink()
    if not target_occupied or symlink_points_to(target, hidden_mount):
        alias = target
    else:
        alias = staged_alias

    with credentials.secretfile() as ceph_secretfile:
        await ensure_repository_host_mount(
            repo_id=repo_id,
            ceph_path=ceph_path,
            deadline=deadline,
            monitors=credentials.monitors,
            ceph_user=credentials.user,
            ceph_secretfile=ceph_secretfile,
        )
    async with repo_state.aliases(deadline=deadline) as aliases:
        aliases.link(alias)

    await STATE.write(repo_state.id_file, repo_state.mount_id + "\n", deadline=deadline)
    await ensure_repository_mount_record(
        kube,
        repo_id=repo_id,
        host_id=_current_host_id(),
        alias_path=alias.as_posix(),
        node_name=platform.node(),
        deadline=deadline,
    )
    return alias


def _resume_repository_cutover(
    *,
    target: Path,
    alias: Path,
    hidden_mount: Path,
) -> None:
    if target.exists() or target.is_symlink():
        if not symlink_points_to(target, hidden_mount):
            msg = (
                f"cannot resume repository cutover at {target}: alias path "
                f"{target} does not target expected mount path {hidden_mount}"
            )
            raise OSError(msg)
        return

    if not (alias.exists() or alias.is_symlink()):
        msg = (
            f"cannot resume repository cutover at {target}: neither staged "
            "alias nor destination path exists while swap path is present"
        )
        raise OSError(msg)
    if not symlink_points_to(alias, hidden_mount):
        msg = (
            f"cannot resume repository cutover at {target}: alias path "
            f"{alias} does not target expected mount path {hidden_mount}"
        )
        raise OSError(msg)
    alias.rename(target)


def _promote_repository_alias(
    *,
    target: Path,
    alias: Path,
    hidden_mount: Path,
    swap_path: Path,
    replace_existing: bool,
) -> None:
    if target.exists() or target.is_symlink():
        if not (alias.exists() or alias.is_symlink()):
            msg = (
                f"cannot cut over repository at {target}: staged alias does not exist "
                f"({alias})"
            )
            raise OSError(msg)
        if not symlink_points_to(alias, hidden_mount):
            msg = (
                f"cannot cut over repository at {target}: alias path {alias} does "
                f"not target expected mount path {hidden_mount}"
            )
            raise OSError(msg)
        if not replace_existing:
            msg = "repository cutover declined by user"
            raise PermissionError(msg)
        target.rename(swap_path)
        try:
            alias.rename(target)
        except OSError as err:
            if (swap_path.exists() or swap_path.is_symlink()) and not (
                target.exists() or target.is_symlink()
            ):
                swap_path.rename(target)
            msg = f"failed to atomically swap repository path at {target}"
            raise OSError(msg) from err
        return

    if not (alias.exists() or alias.is_symlink()):
        msg = (
            f"cannot finalize repository cutover at {target}: staged alias does "
            f"not exist ({alias})"
        )
        raise OSError(msg)
    if not symlink_points_to(alias, hidden_mount):
        msg = (
            f"cannot finalize repository cutover at {target}: alias path "
            f"{alias} does not target expected mount path {hidden_mount}"
        )
        raise OSError(msg)
    alias.rename(target)


async def finalize_repository_mount(
    kube: Kube,
    *,
    repo_id: UUIDHex,
    target: Path,
    alias: Path,
    deadline: Deadline,
    replace_existing: bool,
) -> Path:
    """Promote a staged repository mount alias into its final location.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : UUIDHex
        Stable repository identity.
    target : Path
        Final user-facing repository path.
    alias : Path
        Current managed alias path returned by repository mount convergence.
    deadline : Deadline
        Maximum finalization budget in seconds.
    replace_existing : bool
        Whether an occupied destination may be displaced during cutover.

    Returns
    -------
    Path
        Final alias path after successful cutover.

    Raises
    ------
    OSError
        If the staged alias, final alias, or interrupted swap state is invalid.
    PermissionError
        If the destination is occupied and `replace_existing` is false.
    TimeoutError
        If `timeout` is non-positive.
    """  # noqa: DOC502 - helpers raise documented cutover validation errors.
    repo_id = _check_uuid(repo_id)
    target = abspath(target)
    alias = abspath(alias)
    repo_state = STATE.repo(repo_id)
    hidden_mount = repo_state.mount
    staged_alias = target.parent / f".{target.name}.bertrand.mount.{repo_id}"
    swap_path = target.parent / f".{target.name}.bertrand.swap.{repo_id}"
    host_id = _current_host_id()

    if alias == target:
        if not symlink_points_to(target, hidden_mount):
            msg = (
                f"cannot finalize repository at {target}: alias path {target} does "
                f"not target expected mount path {hidden_mount}"
            )
            raise OSError(msg)
    elif swap_path.exists() or swap_path.is_symlink():
        _resume_repository_cutover(
            target=target,
            alias=alias,
            hidden_mount=hidden_mount,
        )
    else:
        _promote_repository_alias(
            target=target,
            alias=alias,
            hidden_mount=hidden_mount,
            swap_path=swap_path,
            replace_existing=replace_existing,
        )
    if not symlink_points_to(target, hidden_mount):
        msg = (
            f"repository cutover failed for destination path {target}: alias path "
            f"{target} does not target expected mount path {hidden_mount}"
        )
        raise OSError(msg)

    if swap_path.exists() or swap_path.is_symlink():
        try:
            if not swap_path.is_symlink() and swap_path.is_dir():
                shutil.rmtree(swap_path)
            else:
                swap_path.unlink()
        except OSError as err:
            print(
                "bertrand: warning: failed to delete conversion swap path at "
                f"{swap_path}: {err}",
                file=sys.stderr,
            )

    for stale_alias in (alias, staged_alias):
        if (
            stale_alias != target
            and (stale_alias.exists() or stale_alias.is_symlink())
            and symlink_points_to(stale_alias, hidden_mount)
        ):
            async with repo_state.aliases(deadline=deadline) as aliases:
                aliases.unlink(stale_alias)
        if stale_alias != target:
            await retire_repository_mount(
                kube,
                repo_id=repo_id,
                host_id=host_id,
                alias_path=stale_alias.as_posix(),
                deadline=deadline,
            )

    await ensure_repository_mount_record(
        kube,
        repo_id=repo_id,
        host_id=host_id,
        alias_path=target.as_posix(),
        node_name=platform.node(),
        deadline=deadline,
    )
    await mark_repository_volume_ready(
        kube,
        repo_id=repo_id,
        deadline=deadline,
    )

    return target


async def _mount_repository_volume(
    kube: Kube,
    *,
    repo_id: str,
    deadline: Deadline,
) -> tuple[UUIDHex, Mount]:
    volumes = await list_repository_volume_claims(
        kube,
        repo_id,
        deadline=deadline,
    )
    volume = _single_repository_volume(repo_id, volumes)
    if volume is None:
        msg = (
            f"repository {repo_id} has mount recovery metadata, but no matching "
            "cluster claim exists"
        )
        raise OSError(msg)

    ceph_path = await resolve_repository_volume_ceph_path(
        kube,
        pvc=volume,
        deadline=deadline,
    )
    credentials: RepoCredentials = await RepoCredentials.ensure(
        repo_id=repo_id,
        ceph_path=ceph_path,
        deadline=deadline,
    )
    with credentials.secretfile() as ceph_secretfile:
        mount = await ensure_repository_host_mount(
            repo_id=repo_id,
            ceph_path=ceph_path,
            deadline=deadline,
            monitors=credentials.monitors,
            ceph_user=credentials.user,
            ceph_secretfile=ceph_secretfile,
        )
    repo_id, mount = await _normalize_mounted_repository_catalog(
        repo_id,
        deadline=deadline,
    )
    if mount is None:
        msg = f"repository {repo_id} mount disappeared during recovery"
        raise OSError(msg)
    await mark_repository_volume_ready(
        kube,
        repo_id=repo_id,
        deadline=deadline,
    )
    return repo_id, mount


async def _resurrect_repository_mount_record(
    kube: Kube,
    path: Path,
    *,
    deadline: Deadline,
) -> tuple[UUIDHex, Mount] | None:
    inspected = abspath(path)
    await REPOSITORY_STATE_RESOURCE.ensure_crd(kube, deadline=deadline)
    states = await REPOSITORY_STATE_RESOURCE.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        deadline=deadline,
    )

    for candidate in (inspected, *inspected.parents):
        alias_path = candidate.as_posix()
        records = [
            record
            for state in states
            for record in state.status.mounts.values()
            if record.alias_path == alias_path and record.phase == "Active"
        ]
        if not records:
            continue

        repo_ids = {record.repo_id for record in records}
        if len(repo_ids) != 1:
            ids = ", ".join(sorted(repo_ids))
            msg = (
                f"repository mount recovery for {alias_path} is ambiguous: "
                f"multiple repository identities match ({ids})"
            )
            raise OSError(msg)
        repo_id = next(iter(repo_ids))
        repo_state = STATE.repo(repo_id)
        hidden_mount = repo_state.mount
        if (candidate.exists() or candidate.is_symlink()) and not symlink_points_to(
            candidate,
            hidden_mount,
        ):
            msg = (
                f"cannot recover repository mount at {candidate}: path is occupied "
                f"and is not a managed symlink to {hidden_mount}"
            )
            raise OSError(msg)

        repo_id, mount = await _mount_repository_volume(
            kube,
            repo_id=repo_id,
            deadline=deadline,
        )
        repo_state = STATE.repo(repo_id)
        async with repo_state.aliases(deadline=deadline) as aliases:
            aliases.link(candidate)
        await ensure_repository_mount_record(
            kube,
            repo_id=repo_id,
            host_id=_current_host_id(),
            alias_path=candidate.as_posix(),
            node_name=platform.node(),
            deadline=deadline,
        )
        return repo_id, mount

    return None


async def resurrect_repository_mount(
    kube: Kube,
    path: Path,
    *,
    deadline: Deadline,
) -> tuple[UUIDHex, Mount] | None:
    """Restore a managed repository mount from symlinks or CRD records.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    path : Path
        Host path to inspect for managed alias ancestry or recorded mount aliases.
    deadline : Deadline
        Maximum resurrection budget in seconds.

    Returns
    -------
    tuple[UUIDHex, Mount] | None
        `(repo_id, mount)` when a managed alias or mount record is found and mounted,
        or None when no managed recovery metadata is present.

    """
    # Search for managed Bertrand symlink alias pointing to a mounted volume.
    managed = _managed_alias_ancestor(path)
    if managed is None:
        return await _resurrect_repository_mount_record(
            kube,
            path,
            deadline=deadline,
        )
    alias, repo_id, mount = managed
    repo_id, mount = await _normalize_mounted_repository_catalog(
        repo_id,
        deadline=deadline,
    )
    if mount is not None:
        repo_state = STATE.repo(repo_id)
        async with repo_state.aliases(deadline=deadline) as aliases:
            aliases.link(alias)
        return repo_id, mount

    # Managed alias ancestry is authoritative: if the mount is missing, recover it
    # from cluster state or fail closed to avoid silently drifting ownership.
    repo_id, mount = await _mount_repository_volume(
        kube,
        repo_id=repo_id,
        deadline=deadline,
    )
    repo_state = STATE.repo(repo_id)
    async with repo_state.aliases(deadline=deadline) as aliases:
        aliases.link(alias)
    return repo_id, mount


async def refresh_repository_alias_for_path(
    kube: Kube,
    path: Path,
    *,
    deadline: Deadline,
) -> UUIDHex | None:
    """Refresh mount metadata for a managed alias used by a host path.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    path : Path
        Host path to inspect before resolving symlinks.
    deadline : Deadline
        Maximum refresh budget in seconds.

    Returns
    -------
    UUIDHex | None
        Repository UUID when a managed alias was refreshed or resurrected, otherwise
        None.

    """
    managed = _managed_alias_ancestor(path)
    if managed is not None:
        alias, repo_id, mount = managed
        repo_id, mount = await _normalize_mounted_repository_catalog(
            repo_id,
            deadline=deadline,
        )
        if mount is None:
            repo_id, _ = await _mount_repository_volume(
                kube,
                repo_id=repo_id,
                deadline=deadline,
            )
        repo_state = STATE.repo(repo_id)
        async with repo_state.aliases(deadline=deadline) as aliases:
            aliases.link(alias)
        await ensure_repository_mount_record(
            kube,
            repo_id=repo_id,
            host_id=_current_host_id(),
            alias_path=alias.as_posix(),
            node_name=platform.node(),
            deadline=deadline,
        )
        return repo_id

    resurrected = await _resurrect_repository_mount_record(
        kube,
        path,
        deadline=deadline,
    )
    if resurrected is None:
        return None
    repo_id, _ = resurrected
    return repo_id


async def prune_repository_mount_aliases(
    kube: Kube,
    *,
    repo_id: str,
    deadline: Deadline,
) -> bool:
    """Retire stale local alias records for one repository.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : str
        Repository UUID whose local aliases should be reconciled.
    deadline : Deadline
        Maximum prune budget in seconds.

    Returns
    -------
    bool
        True if at least one live local alias still points to the hidden mount.

    Raises
    ------
    OSError
        If a recorded alias path is occupied by unmanaged content.
    """
    repo_id = _check_uuid(repo_id)
    host_id = _current_host_id()
    states = await REPOSITORY_STATE_RESOURCE.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        deadline=deadline,
    )
    records = [
        record
        for state in states
        for record in state.status.mounts.values()
        if record.repo_id == repo_id
        and record.host_id == host_id
        and record.phase == "Active"
    ]
    repo_state = STATE.repo(repo_id)
    hidden_mount = repo_state.mount
    live = False
    for record in records:
        alias = Path(record.alias_path)
        if symlink_points_to(alias, hidden_mount):
            live = True
            continue
        if alias.exists() or alias.is_symlink():
            msg = (
                f"recorded repository alias path {alias} is occupied but is not a "
                f"managed symlink to {hidden_mount}"
            )
            raise OSError(msg)
        await retire_repository_mount_record(
            kube,
            record=record,
            deadline=deadline,
        )
    return live


async def prune_repository_mounts(
    kube: Kube,
    *,
    deadline: Deadline,
    limit: int = REPOSITORY_MOUNT_PRUNE_LIMIT,
) -> tuple[UUIDHex, ...]:
    """Prune bounded hidden repository mounts with no live local aliases.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    deadline : Deadline
        Maximum prune budget in seconds.
    limit : int, optional
        Maximum number of hidden mounted repositories to inspect.

    Returns
    -------
    tuple[UUIDHex, ...]
        Repository UUIDs whose hidden mounts were detached.

    Raises
    ------
    ValueError
        If `limit` is negative.
    """
    if limit < 0:
        msg = "repository mount prune limit cannot be negative"
        raise ValueError(msg)
    if limit == 0:
        return ()

    mounted = [
        mount
        for mount in Mount.under(STATE.mount).values()
        if _repository_mount_id(mount) is not None
    ]
    mounted.sort(key=lambda item: item.mount_point.as_posix())
    pruned: list[UUIDHex] = []
    for mount in mounted[:limit]:
        repo_id = _repository_mount_id(mount)
        if repo_id is None:
            continue
        live = await prune_repository_mount_aliases(
            kube,
            repo_id=repo_id,
            deadline=deadline,
        )
        if live:
            continue
        if await STATE.repo(repo_id).gc(deadline=deadline, force=False, mount=mount):
            pruned.append(repo_id)
    return tuple(pruned)
