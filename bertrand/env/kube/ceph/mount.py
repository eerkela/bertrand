"""Host-side Ceph repository mount lifecycle utilities."""

from __future__ import annotations

import os
import platform
import shutil
import sys
from dataclasses import dataclass, field
from pathlib import Path, PosixPath
from typing import TYPE_CHECKING, Self

from bertrand.env.config.core import UUIDHex, _check_uuid
from bertrand.env.git import (
    BERTRAND_NAMESPACE,
    HOST_MOUNTS,
    METADATA_REPO_ID,
    REPO_ID_ENV,
    CommandError,
    Deadline,
    HostLock,
    abspath,
    atomic_symlink,
    atomic_write_text,
    run,
    sudo,
    symlink_points_to,
)
from bertrand.env.host import HOST_ID_FILE, REPO_DIR, REPO_LOCK_EXT, REPO_MOUNT_EXT
from bertrand.env.kube.ceph.auth import RepoCredentials
from bertrand.env.kube.ceph.volume import (
    REPOSITORY_MOUNT_HOST_HASH_LABEL,
    REPOSITORY_MOUNT_PATH_HASH_LABEL,
    REPOSITORY_MOUNT_PHASE_LABEL,
    REPOSITORY_STATE_RESOURCE,
    ensure_repository_mount_record,
    ensure_repository_volume_claim,
    ensure_repository_volume_record,
    list_repository_volume_claims,
    mark_repository_volume_ready,
    repository_mount_host_hash,
    repository_mount_path_hash,
    resolve_repository_volume_ceph_path,
    retire_repository_mount,
    retire_repository_mount_record,
)

if TYPE_CHECKING:
    from collections.abc import Sequence
    from types import TracebackType

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
_ALIAS_LOCK_RELEASE_ERRORS: tuple[type[Exception], ...] = (
    OSError,
    RuntimeError,
    TimeoutError,
)


def _current_host_id() -> str:
    try:
        return _check_uuid(HOST_ID_FILE.read_text(encoding="utf-8").strip())
    except OSError as err:
        msg = (
            f"failed to read Bertrand host identity at {HOST_ID_FILE}; run "
            "`bertrand init` to repair host state"
        )
        raise OSError(msg) from err


def _warn(message: str) -> None:
    print(f"bertrand: warning: {message}", file=sys.stderr)


def _staged_alias_path(target: Path, repo_id: str) -> Path:
    return target.parent / f".{target.name}.bertrand.mount.{repo_id}"


def _swap_path(target: Path, repo_id: str) -> Path:
    return target.parent / f".{target.name}.bertrand.swap.{repo_id}"


async def _record_repository_mount(
    kube: Kube,
    *,
    repo_id: str,
    host_id: str,
    alias_path: Path,
    timeout: float,
) -> None:
    await ensure_repository_mount_record(
        kube,
        repo_id=repo_id,
        host_id=host_id,
        alias_path=alias_path.as_posix(),
        node_name=platform.node(),
        timeout=timeout,
    )


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


def _managed_alias_target(candidate: Path) -> tuple[UUIDHex, Path] | None:
    try:
        target = candidate.readlink()
    except OSError as err:
        msg = f"failed to inspect managed alias candidate {candidate}: {err}"
        raise OSError(msg) from err
    if not target.is_absolute():
        return None

    # Raw-target ownership is strict: a managed alias must point directly to
    # Bertrand's hidden mount namespace without normalization.
    try:
        relative = target.relative_to(REPO_DIR)
    except ValueError:
        return None
    if len(relative.parts) != 2 or relative.parts[1] != REPO_MOUNT_EXT.as_posix():
        msg = (
            f"repository alias path {candidate} points to malformed managed "
            f"target {target}; expected {REPO_DIR}/<repo_id>/{REPO_MOUNT_EXT}"
        )
        raise OSError(msg)
    try:
        repo_id = _check_uuid(relative.parts[0])
    except ValueError as err:
        msg = (
            f"repository alias path {candidate} points to invalid repository "
            f"target {target}: {err}"
        )
        raise OSError(msg) from err
    return repo_id, REPO_DIR / repo_id / REPO_MOUNT_EXT


def _managed_alias_ancestor(
    path: Path,
) -> tuple[Path, UUIDHex, MountInfo | None] | None:
    inspected = abspath(path)
    for candidate in (inspected, *inspected.parents):
        if not candidate.is_symlink():
            continue
        managed = _managed_alias_target(candidate)
        if managed is None:
            continue
        repo_id, hidden_mount = managed
        return candidate, repo_id, MountInfo.search(hidden_mount)
    return None


@dataclass(frozen=True)
class MountInfo:
    r"""Describe a host mount entry relevant to Bertrand repository mounts.

    Attributes
    ----------
    mount_point : Path
        Mountpoint path from mountinfo field 5 (pre-`-` section). Kernel escaping
        is decoded before storage (`\\040`, `\\011`, `\\012`, `\\134`).
    fs_type : str
        Filesystem type from the first field after the `-` separator.
    source : str
        Kernel-reported mount source from the second field after `-`.
    """

    mount_point: Path
    fs_type: str = field(default="")
    source: str = field(default="")

    @property
    def repo_id(self) -> str | None:
        """Repository UUID derived from this mount point, when applicable.

        Returns
        -------
        str | None
            UUID for repository hidden mount points that match Bertrand's layout
            (`<REPO_DIR>/<repo_id>/<REPO_MOUNT_EXT>`), otherwise None.
        """
        mount_point = abspath(self.mount_point)
        try:
            relative = mount_point.relative_to(REPO_DIR)
        except ValueError:
            return None
        if len(relative.parts) != 2 or relative.parts[1] != REPO_MOUNT_EXT.as_posix():
            return None
        try:
            return _check_uuid(relative.parts[0])
        except ValueError:
            return None

    @property
    def ceph_path(self) -> PosixPath | None:
        """Best-effort parsed CephFS path suffix from this mount's source.

        Returns
        -------
        PosixPath | None
            Absolute Ceph path when this mount is a parseable kernel Ceph mount,
            otherwise None.
        """
        if self.fs_type.strip().lower() != "ceph":
            return None
        source = self.source.strip()
        if not source:
            return None
        _, sep, suffix = source.rpartition(":")
        if not sep:
            return None
        suffix = suffix.strip()
        if not suffix.startswith("/"):
            return None
        return PosixPath(suffix)

    @classmethod
    def resolve(cls, path: Path) -> tuple[UUIDHex | None, Self | None]:
        """Resolve managed-alias ancestry for a host path.

        This inspects `path` and its ancestors (deepest-first) to find Bertrand-
        managed alias symlinks.  Managed ownership is strict: the raw symlink target
        must be absolute and exactly match Bertrand's hidden mount layout.

        Parameters
        ----------
        path : Path
            Host path to inspect for managed alias ancestry.

        Returns
        -------
        tuple[UUIDHex | None, MountInfo | None]
            `(None, None)` if no managed alias ancestor is present.
            `(repo_id, None)` if a managed alias ancestor exists but its hidden mount
            is not currently active.
            `(repo_id, mount)` if a managed alias ancestor exists and hidden mount is
            currently active.

        Raises
        ------
        OSError
            If a symlink target points into Bertrand's hidden mount namespace but does
            not match expected layout or contains an invalid repository UUID.
        """  # noqa: DOC502 - helper raises documented alias validation errors.
        inspected = abspath(path)
        for candidate in (inspected, *inspected.parents):
            if not candidate.is_symlink():
                continue
            managed = _managed_alias_target(candidate)
            if managed is None:
                continue
            repo_id, hidden_mount = managed
            return repo_id, cls.search(hidden_mount)

        return None, None

    @classmethod
    def local(cls) -> dict[Path, Self]:
        """Parse and return local mount entries from `/proc/self/mountinfo`.

        Returns
        -------
        dict[Path, MountInfo]
            Parsed entries with decoded mountpoint paths as keys, for fast lookup.

        Raises
        ------
        OSError
            If the host mount table cannot be read.
        """
        try:
            lines = HOST_MOUNTS.read_text(encoding="utf-8").splitlines()
        except OSError as err:
            msg = f"failed to inspect host mount table: {err}"
            raise OSError(msg) from err

        out: dict[Path, Self] = {}
        for line in lines:
            parts = line.strip().split()
            # ignore malformed rows defensively; kernel format should contain enough
            # fields for mountpoint + post-separator fs/source data
            try:
                if len(parts) < 10:
                    continue
                sep = parts.index("-")
                if sep + 2 >= len(parts):
                    continue
            except ValueError:
                continue

            info = cls(
                mount_point=abspath(
                    Path(
                        parts[4]
                        .replace("\\040", " ")
                        .replace("\\011", "\t")
                        .replace("\\012", "\n")
                        .replace("\\134", "\\")
                    )
                ),
                fs_type=parts[sep + 1],
                source=parts[sep + 2],
            )
            out[info.mount_point] = info
        return out

    @classmethod
    def under(cls, root: Path) -> dict[Path, Self]:
        """Return mounted paths at or below `root`.

        Parameters
        ----------
        root : Path
            Root path to check for mounted entries at or below.

        Returns
        -------
        dict[Path, MountInfo]
            Parsed mount entries that are mounted at or below `root`, with decoded
            mountpoint paths as keys for fast lookup.
        """
        prefix = abspath(root)
        out: dict[Path, Self] = {}
        for entry in cls.local().values():
            if entry.mount_point.is_relative_to(prefix):
                out[entry.mount_point] = entry
        return out

    @classmethod
    def search(cls, path: Path) -> Self | None:
        """Return the exact mounted entry for `path`, if present.

        Parameters
        ----------
        path : Path
            Path to check for an exact mountpoint match.

        Returns
        -------
        MountInfo | None
            Matching mount entry, or None when `path` is not mounted.
        """
        return cls.local().get(abspath(path))

    @classmethod
    async def mount(
        cls,
        repo_id: str,
        *,
        ceph_path: PosixPath,
        monitors: Sequence[str],
        ceph_user: str,
        ceph_secretfile: Path,
        timeout: float,
    ) -> MountInfo:
        """Ensure a repository hidden mount exists and return its mount entry.

        Parameters
        ----------
        repo_id : str
            Repository UUID used to derive hidden mount paths.
        ceph_path : PosixPath
            Absolute CephFS path to mount for this repository.
        monitors : Sequence[str]
            Ceph monitor endpoints.
        ceph_user : str
            Ceph user used for host mount options.
        ceph_secretfile : Path
            Existing secret file containing Ceph key material.
        timeout : float
            Maximum runtime timeout in seconds for convergence.

        Returns
        -------
        MountInfo
            Mounted hidden entry for this repository.

        Raises
        ------
        FileNotFoundError
            If `ceph_secretfile` is missing or is not a regular file.
        OSError
            If mount convergence fails or an incompatible mount already exists.
        TimeoutError
            If timeout is non-positive.
        ValueError
            If the repository identity or mount inputs are invalid.
        """
        repo_id = _check_uuid(repo_id)
        if not ceph_path.is_absolute():
            ceph_path = PosixPath("/") / ceph_path
        if os.name != "posix" or platform.system() != "Linux":
            msg = "repository mounts are only supported on Linux platforms"
            raise OSError(msg)
        message = "timeout must be non-negative"
        if timeout <= 0:
            raise TimeoutError(message)
        deadline = Deadline.from_timeout(timeout, message=message)

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

        root = REPO_DIR / repo_id
        mount_path = root / REPO_MOUNT_EXT
        root.mkdir(parents=True, exist_ok=True)

        async with HostLock(
            root / REPO_LOCK_EXT,
            timeout=deadline.remaining(),
        ):
            mounted = cls.search(mount_path)
            if mounted is None:
                mount_opts = [
                    f"name={ceph_user}",
                    f"secretfile={ceph_secretfile}",
                    f"mds_namespace={DEFAULT_REPO_FS_NAME}",
                ]
                mount_opts.extend(DEFAULT_REPO_MOUNT_OPTIONS)
                mount_path.mkdir(parents=True, exist_ok=True)
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
                    timeout=deadline.remaining(),
                )
                mounted = cls.search(mount_path)
                if mounted is None:
                    msg = (
                        f"repository mount target {mount_path!r} failed to appear in "
                        f"{HOST_MOUNTS} after successful mount subcommand"
                    )
                    raise OSError(msg)

            if mounted.ceph_path is None:
                msg = (
                    f"repository mount target {mounted.mount_point!r} is mounted with "
                    f"unsupported source {mounted.source!r}, expected parseable Ceph "
                    "mount"
                )
                raise OSError(msg)
            if mounted.ceph_path != ceph_path:
                msg = (
                    f"repository mount target {mounted.mount_point!r} is attached to "
                    f"{mounted.source!r}, expected Ceph source suffix ':{ceph_path}'"
                )
                raise OSError(msg)

        return mounted

    async def unmount(self, *, timeout: float, force: bool) -> bool:
        """Detach this mounted entry from the host.

        Parameters
        ----------
        timeout : float
            Maximum runtime command timeout in seconds for this unmount operation.
        force : bool
            If True, pass `-f` to `umount`.

        Returns
        -------
        bool
            True if this mount was detached, False if it was already detached.

        Raises
        ------
        TimeoutError
            If `timeout` is non-positive.
        OSError
            If unmounting fails or the mount remains attached.
        """
        if os.name != "posix" or platform.system() != "Linux":
            msg = "repository mounts are only supported on Linux platforms"
            raise OSError(msg)
        if timeout <= 0:
            msg = "timeout must be non-negative"
            raise TimeoutError(msg)

        cmd = ["umount"]
        if force:
            cmd.append("-f")
        cmd.append(str(self.mount_point))
        try:
            await run(
                sudo(cmd),
                capture_output=True,
                timeout=timeout,
            )
        except CommandError as err:
            # Treat a stale-entry race as idempotent success.
            if MountInfo.search(self.mount_point) is None:
                return False
            msg = f"unmount command failed:\n{err}"
            raise OSError(msg) from err

        if MountInfo.search(self.mount_point) is not None:
            msg = f"failed to unmount volume at {self.mount_point}"
            raise OSError(msg)
        return True

    @dataclass
    class Aliases:
        """Async context manager for lock-scoped alias symlink mutation.

        Attributes
        ----------
        aliases : set[Path]
            Alias paths touched by this context.
        """

        mount: MountInfo
        timeout: float
        aliases: set[Path] = field(default_factory=set)
        _root: Path = field(init=False)
        _mount_path: Path = field(init=False)
        _lock: HostLock | None = field(default=None)
        _deadline: Deadline | None = field(default=None)
        _depth: int = field(default=0)

        def __post_init__(self) -> None:
            """Validate the alias manager and derive repository paths.

            Raises
            ------
            OSError
                If the mount is not a managed repository mount.
            TimeoutError
                If `timeout` is non-positive.
            """
            if self.timeout <= 0:
                msg = "timeout must be non-negative"
                raise TimeoutError(msg)
            if self.mount.repo_id is None:
                msg = (
                    "alias state is only available for repository mounts with a valid "
                    "repository identity"
                )
                raise OSError(msg)
            self._root = REPO_DIR / self.mount.repo_id
            self._mount_path = self._root / REPO_MOUNT_EXT

        async def __aenter__(self) -> Self:
            """Lock host-local alias mutation for this mount.

            Returns
            -------
            MountInfo.Aliases
                Active alias state manager.
            """
            if self._depth >= 1:
                self._depth += 1
                return self  # re-entrant

            # hold the repo-local lock for the entire mutation window so alias
            # symlinks cannot drift across concurrent init/clean callers
            self._deadline = Deadline.from_timeout(
                self.timeout,
                message="timeout must be non-negative",
            )
            self._root.mkdir(parents=True, exist_ok=True)
            self._lock = HostLock(
                self._root / REPO_LOCK_EXT,
                timeout=self._deadline.remaining(),
            )
            await self._lock.lock()  # block until acquire or deadline

            self._depth = 1
            return self

        def link(self, path: Path) -> bool:
            """Create/update a managed alias symlink to this mount's registry.

            Parameters
            ----------
            path : Path
                Alias path to create or update.

            Returns
            -------
            bool
                True if a new symlink was created, meaning that the alias path was
                previously unoccupied.  False if it already existed.

            Raises
            ------
            OSError
                If the path collides with an unmanaged file/symlink.
            RuntimeError
                If called outside an active context manager block.
            """
            if self._depth < 1:
                msg = (
                    "MountAliases must be used inside 'async with' before calling "
                    "link()/unlink()"
                )
                raise RuntimeError(msg)

            # avoid collisions with file or directory paths, as well as symlinks that
            # do not point to the expected mount
            path = abspath(path)
            missing = not (path.exists() or path.is_symlink())
            if missing:
                atomic_symlink(self._mount_path, path)
            elif not symlink_points_to(path, self._mount_path):
                msg = (
                    f"repository alias path {path!r} already exists and is not a "
                    f"managed symlink to {self._mount_path!r}"
                )
                raise OSError(msg)
            self.aliases.add(path)
            return missing

        def unlink(self, path: Path) -> bool:
            """Remove a managed alias from the active alias state.

            Parameters
            ----------
            path : Path
                Alias path to remove.

            Returns
            -------
            bool
                True if `path` corresponds to an active alias entry, in which case its
                symlink will be removed if it is still valid.  False otherwise

            Raises
            ------
            RuntimeError
                If called outside an active context manager block.
            """
            if self._depth < 1:
                msg = (
                    "MountAliases must be used inside 'async with' before calling "
                    "link()/unlink()"
                )
                raise RuntimeError(msg)
            path = abspath(path)

            # only unlink if the symlink still points to this mount
            if symlink_points_to(path, self._mount_path):
                path.unlink()
                self.aliases.discard(path)
                return True
            if path in self.aliases:
                self.aliases.discard(path)
                return True
            return False

        async def __aexit__(
            self,
            exc_type: type[BaseException] | None,
            exc_value: BaseException | None,
            traceback: TracebackType | None,
        ) -> None:
            """Release the repository alias mutation lock."""
            self._depth -= 1
            if self._depth > 0:
                return  # re-entrant

            if self._lock is not None:
                try:
                    await self._lock.unlock(ignore_errors=exc_value is not None)
                except _ALIAS_LOCK_RELEASE_ERRORS as err:
                    if exc_value is None:
                        raise
                    print(
                        "bertrand: warning: failed to release repository alias lock "
                        f"during error unwinding: {err}",
                        file=sys.stderr,
                    )
                finally:
                    self._lock = None
            self._depth = 0

    def aliases(self, *, timeout: float) -> Aliases:
        """Return an async context manager for locked alias symlink mutation.

        Parameters
        ----------
        timeout : float
            Maximum timeout for lock acquisition and alias convergence.

        Returns
        -------
        MountAliases
            Async context manager for locked host-local alias mutation.

        Raises
        ------
        OSError
            If this mount does not correspond to a Bertrand repository.
        """
        if self.repo_id is None:
            msg = (
                "alias state is only available for repository mounts with a valid "
                "repository identity"
            )
            raise OSError(msg)
        return self.Aliases(self, timeout=timeout)


async def ensure_repository_mount(
    kube: Kube,
    *,
    repo_id: UUIDHex,
    target: Path,
    timeout: float,
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
    timeout : float
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
    TimeoutError
        If `timeout` is non-positive.
    """
    repo_id = _check_uuid(repo_id)
    target = abspath(target)
    message = "timed out before repository mount convergence started"
    if timeout <= 0:
        raise TimeoutError(message)
    deadline = Deadline.from_timeout(timeout, message=message)

    candidates = (
        list(volumes)
        if volumes is not None
        else await list_repository_volume_claims(
            kube,
            repo_id,
            timeout=deadline.remaining(),
        )
    )
    hidden_mount = REPO_DIR / repo_id / REPO_MOUNT_EXT
    volume = _single_repository_volume(repo_id, candidates)
    if volume is not None:
        ceph_path = await resolve_repository_volume_ceph_path(
            kube,
            pvc=volume,
            timeout=deadline.remaining(),
        )
        mounted = MountInfo.search(hidden_mount)
        if mounted is not None and not (
            mounted.ceph_path is not None and mounted.ceph_path == ceph_path
        ):
            msg = (
                f"repository hidden mount {hidden_mount!r} is occupied by "
                f"{mounted.source!r}, expected Ceph source suffix ':{ceph_path}'"
            )
            raise OSError(msg)
    else:
        volume = await ensure_repository_volume_claim(
            kube,
            repo_id=repo_id,
            timeout=deadline.remaining(),
            size_request=size_request,
        )
        ceph_path = await resolve_repository_volume_ceph_path(
            kube,
            pvc=volume,
            timeout=deadline.remaining(),
        )
    await ensure_repository_volume_record(
        kube,
        repo_id=repo_id,
        timeout=deadline.remaining(),
    )

    credentials: RepoCredentials = await RepoCredentials.ensure(
        repo_id=repo_id,
        ceph_path=ceph_path,
        timeout=deadline.remaining(),
    )
    staged_alias = _staged_alias_path(target, repo_id)
    target_occupied = target.exists() or target.is_symlink()
    if not target_occupied or symlink_points_to(target, hidden_mount):
        alias = target
    else:
        alias = staged_alias

    with credentials.secretfile() as ceph_secretfile:
        mount = await MountInfo.mount(
            repo_id=repo_id,
            ceph_path=ceph_path,
            timeout=deadline.remaining(),
            monitors=credentials.monitors,
            ceph_user=credentials.user,
            ceph_secretfile=ceph_secretfile,
        )
    async with mount.aliases(timeout=deadline.remaining()) as aliases:
        aliases.link(alias)

    atomic_write_text(
        hidden_mount / METADATA_REPO_ID,
        repo_id,
        encoding="utf-8",
    )
    await _record_repository_mount(
        kube,
        repo_id=repo_id,
        host_id=_current_host_id(),
        alias_path=alias,
        timeout=deadline.remaining(),
    )
    return alias


def _path_exists(path: Path) -> bool:
    return path.exists() or path.is_symlink()


def _ensure_existing_final_alias(target: Path, hidden_mount: Path) -> None:
    if not symlink_points_to(target, hidden_mount):
        msg = (
            f"cannot finalize repository at {target}: alias path {target} does "
            f"not target expected mount path {hidden_mount}"
        )
        raise OSError(msg)


def _resume_repository_cutover(
    *,
    target: Path,
    alias: Path,
    hidden_mount: Path,
) -> None:
    if _path_exists(target):
        if not symlink_points_to(target, hidden_mount):
            msg = (
                f"cannot resume repository cutover at {target}: alias path "
                f"{target} does not target expected mount path {hidden_mount}"
            )
            raise OSError(msg)
        return

    if not _path_exists(alias):
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
    if _path_exists(target):
        if not _path_exists(alias):
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
            if _path_exists(swap_path) and not _path_exists(target):
                swap_path.rename(target)
            msg = f"failed to atomically swap repository path at {target}"
            raise OSError(msg) from err
        return

    if not _path_exists(alias):
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


def _ensure_final_repository_alias(target: Path, hidden_mount: Path) -> None:
    if not symlink_points_to(target, hidden_mount):
        msg = (
            f"repository cutover failed for destination path {target}: alias path "
            f"{target} does not target expected mount path {hidden_mount}"
        )
        raise OSError(msg)


def _cleanup_repository_swap(swap_path: Path) -> None:
    if not _path_exists(swap_path):
        return
    try:
        if not swap_path.is_symlink() and swap_path.is_dir():
            shutil.rmtree(swap_path)
        else:
            swap_path.unlink()
    except OSError as err:
        _warn(f"failed to delete conversion swap path at {swap_path}: {err}")


async def _retire_stale_repository_aliases(
    kube: Kube,
    *,
    repo_id: UUIDHex,
    host_id: str,
    target: Path,
    alias: Path,
    staged_alias: Path,
    hidden_mount: Path,
    deadline: Deadline,
) -> None:
    for stale_alias in (alias, staged_alias):
        if (
            stale_alias != target
            and _path_exists(stale_alias)
            and symlink_points_to(stale_alias, hidden_mount)
        ):
            mount = MountInfo(mount_point=hidden_mount)
            async with mount.aliases(timeout=deadline.remaining()) as aliases:
                aliases.unlink(stale_alias)
        if stale_alias != target:
            await retire_repository_mount(
                kube,
                repo_id=repo_id,
                host_id=host_id,
                alias_path=stale_alias.as_posix(),
                timeout=deadline.remaining(),
            )


async def _mark_repository_mount_finalized(
    kube: Kube,
    *,
    repo_id: UUIDHex,
    host_id: str,
    target: Path,
    deadline: Deadline,
) -> None:
    await _record_repository_mount(
        kube,
        repo_id=repo_id,
        host_id=host_id,
        alias_path=target,
        timeout=deadline.remaining(),
    )
    await mark_repository_volume_ready(
        kube,
        repo_id=repo_id,
        timeout=deadline.remaining(),
    )


async def finalize_repository_mount(
    kube: Kube,
    *,
    repo_id: UUIDHex,
    target: Path,
    alias: Path,
    timeout: float,
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
    timeout : float
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
    message = "timed out before repository mount finalization started"
    if timeout <= 0:
        raise TimeoutError(message)
    deadline = Deadline.from_timeout(timeout, message=message)
    hidden_mount = REPO_DIR / repo_id / REPO_MOUNT_EXT
    staged_alias = _staged_alias_path(target, repo_id)
    swap_path = _swap_path(target, repo_id)
    host_id = _current_host_id()

    if alias == target:
        _ensure_existing_final_alias(target, hidden_mount)
    elif _path_exists(swap_path):
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
    _ensure_final_repository_alias(target, hidden_mount)
    _cleanup_repository_swap(swap_path)
    await _retire_stale_repository_aliases(
        kube,
        repo_id=repo_id,
        host_id=host_id,
        target=target,
        alias=alias,
        staged_alias=staged_alias,
        hidden_mount=hidden_mount,
        deadline=deadline,
    )
    await _mark_repository_mount_finalized(
        kube,
        repo_id=repo_id,
        host_id=host_id,
        target=target,
        deadline=deadline,
    )

    return target


async def _mount_repository_volume(
    kube: Kube,
    *,
    repo_id: str,
    timeout: float,
) -> MountInfo:
    deadline = Deadline.from_timeout(
        timeout,
        message="timed out before repository volume remount started",
    )
    volumes = await list_repository_volume_claims(
        kube,
        repo_id,
        timeout=deadline.remaining(),
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
        timeout=deadline.remaining(),
    )
    credentials: RepoCredentials = await RepoCredentials.ensure(
        repo_id=repo_id,
        ceph_path=ceph_path,
        timeout=deadline.remaining(),
    )
    with credentials.secretfile() as ceph_secretfile:
        mount = await MountInfo.mount(
            repo_id=repo_id,
            ceph_path=ceph_path,
            timeout=deadline.remaining(),
            monitors=credentials.monitors,
            ceph_user=credentials.user,
            ceph_secretfile=ceph_secretfile,
        )
    await mark_repository_volume_ready(
        kube,
        repo_id=repo_id,
        timeout=deadline.remaining(),
    )
    atomic_write_text(
        REPO_DIR / repo_id / REPO_MOUNT_EXT / METADATA_REPO_ID,
        repo_id,
        encoding="utf-8",
    )
    return mount


async def _resurrect_repository_mount_record(
    kube: Kube,
    path: Path,
    *,
    timeout: float,
) -> tuple[UUIDHex, MountInfo] | None:
    deadline = Deadline.from_timeout(
        timeout,
        message="timed out before repository mount record recovery started",
    )
    inspected = abspath(path)
    await REPOSITORY_STATE_RESOURCE.ensure_crd(kube, timeout=deadline.remaining())
    states = await REPOSITORY_STATE_RESOURCE.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        timeout=deadline.remaining(),
    )

    for candidate in (inspected, *inspected.parents):
        alias_path = candidate.as_posix()
        records = [
            record
            for state in states
            for record in state.status.mounts.values()
            if record.alias_path == alias_path
            and record.labels.get(REPOSITORY_MOUNT_PATH_HASH_LABEL)
            == repository_mount_path_hash(alias_path)
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
        hidden_mount = REPO_DIR / repo_id / REPO_MOUNT_EXT
        if (candidate.exists() or candidate.is_symlink()) and not symlink_points_to(
            candidate,
            hidden_mount,
        ):
            msg = (
                f"cannot recover repository mount at {candidate}: path is occupied "
                f"and is not a managed symlink to {hidden_mount}"
            )
            raise OSError(msg)

        mount = await _mount_repository_volume(
            kube,
            repo_id=repo_id,
            timeout=deadline.remaining(),
        )
        async with mount.aliases(timeout=deadline.remaining()) as aliases:
            aliases.link(candidate)
        await _record_repository_mount(
            kube,
            repo_id=repo_id,
            host_id=_current_host_id(),
            alias_path=candidate,
            timeout=deadline.remaining(),
        )
        return repo_id, mount

    return None


async def resurrect_repository_mount(
    kube: Kube,
    path: Path,
    *,
    timeout: float,
) -> tuple[UUIDHex, MountInfo] | None:
    """Restore a managed repository mount from symlinks or CRD records.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    path : Path
        Host path to inspect for managed alias ancestry or recorded mount aliases.
    timeout : float
        Maximum resurrection budget in seconds.

    Returns
    -------
    tuple[UUIDHex, MountInfo] | None
        `(repo_id, mount)` when a managed alias or mount record is found and mounted,
        or None when no managed recovery metadata is present.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    """
    message = "timed out before repository mount resurrection started"
    if timeout <= 0:
        raise TimeoutError(message)
    deadline = Deadline.from_timeout(timeout, message=message)

    # Search for managed Bertrand symlink alias pointing to a mounted volume.
    repo_id, mount = MountInfo.resolve(path)
    if repo_id is None:
        return await _resurrect_repository_mount_record(
            kube,
            path,
            timeout=deadline.remaining(),
        )
    if mount is not None:
        return repo_id, mount

    # Managed alias ancestry is authoritative: if the mount is missing, recover it
    # from cluster state or fail closed to avoid silently drifting ownership.
    mount = await _mount_repository_volume(
        kube,
        repo_id=repo_id,
        timeout=deadline.remaining(),
    )
    return repo_id, mount


async def refresh_repository_alias_for_path(
    kube: Kube,
    path: Path,
    *,
    timeout: float,
) -> UUIDHex | None:
    """Refresh mount metadata for a managed alias used by a host path.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    path : Path
        Host path to inspect before resolving symlinks.
    timeout : float
        Maximum refresh budget in seconds.

    Returns
    -------
    UUIDHex | None
        Repository UUID when a managed alias was refreshed or resurrected, otherwise
        None.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    """
    message = "timed out before repository alias refresh started"
    if timeout <= 0:
        raise TimeoutError(message)
    deadline = Deadline.from_timeout(timeout, message=message)

    managed = _managed_alias_ancestor(path)
    if managed is not None:
        alias, repo_id, mount = managed
        if mount is None:
            mount = await _mount_repository_volume(
                kube,
                repo_id=repo_id,
                timeout=deadline.remaining(),
            )
        await _record_repository_mount(
            kube,
            repo_id=repo_id,
            host_id=_current_host_id(),
            alias_path=alias,
            timeout=deadline.remaining(),
        )
        return repo_id

    resurrected = await _resurrect_repository_mount_record(
        kube,
        path,
        timeout=deadline.remaining(),
    )
    if resurrected is None:
        return None
    repo_id, _ = resurrected
    return repo_id


async def prune_repository_mount_aliases(
    kube: Kube,
    *,
    repo_id: str,
    timeout: float,
) -> bool:
    """Retire stale local alias records for one repository.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    repo_id : str
        Repository UUID whose local aliases should be reconciled.
    timeout : float
        Maximum prune budget in seconds.

    Returns
    -------
    bool
        True if at least one live local alias still points to the hidden mount.

    Raises
    ------
    OSError
        If a recorded alias path is occupied by unmanaged content.
    TimeoutError
        If `timeout` is non-positive.
    """
    message = "timed out before repository alias pruning started"
    if timeout <= 0:
        raise TimeoutError(message)
    deadline = Deadline.from_timeout(timeout, message=message)
    repo_id = _check_uuid(repo_id)
    host_id = _current_host_id()
    states = await REPOSITORY_STATE_RESOURCE.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        timeout=deadline.remaining(),
    )
    records = [
        record
        for state in states
        for record in state.status.mounts.values()
        if record.labels.get(REPO_ID_ENV) == repo_id
        and record.labels.get(REPOSITORY_MOUNT_HOST_HASH_LABEL)
        == repository_mount_host_hash(host_id)
        and record.labels.get(REPOSITORY_MOUNT_PHASE_LABEL) == "active"
    ]
    hidden_mount = REPO_DIR / repo_id / REPO_MOUNT_EXT
    live = False
    for record in records:
        if record.repo_id != repo_id or record.host_id != host_id:
            continue
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
            timeout=deadline.remaining(),
        )
    return live


async def prune_repository_mounts(
    kube: Kube,
    *,
    timeout: float,
    limit: int = REPOSITORY_MOUNT_PRUNE_LIMIT,
) -> tuple[UUIDHex, ...]:
    """Prune bounded hidden repository mounts with no live local aliases.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum prune budget in seconds.
    limit : int, optional
        Maximum number of hidden mounted repositories to inspect.

    Returns
    -------
    tuple[UUIDHex, ...]
        Repository UUIDs whose hidden mounts were detached.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    ValueError
        If `limit` is negative.
    """
    message = "timed out before repository mount pruning started"
    if timeout <= 0:
        raise TimeoutError(message)
    deadline = Deadline.from_timeout(timeout, message=message)
    if limit < 0:
        msg = "repository mount prune limit cannot be negative"
        raise ValueError(msg)
    if limit == 0:
        return ()

    mounted = [
        mount
        for mount in MountInfo.under(REPO_DIR).values()
        if mount.repo_id is not None
    ]
    mounted.sort(key=lambda item: item.mount_point.as_posix())
    pruned: list[UUIDHex] = []
    for mount in mounted[:limit]:
        repo_id = mount.repo_id
        if repo_id is None:
            continue
        live = await prune_repository_mount_aliases(
            kube,
            repo_id=repo_id,
            timeout=deadline.remaining(),
        )
        if live:
            continue
        current = MountInfo.search(mount.mount_point)
        if current is None:
            continue
        await current.unmount(timeout=deadline.remaining(), force=False)
        pruned.append(repo_id)
    return tuple(pruned)
