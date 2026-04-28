"""Host-side Ceph repository mount lifecycle utilities."""
from __future__ import annotations

import asyncio
import json
import os
import platform
import sys
from collections.abc import Sequence
from dataclasses import dataclass, field
from datetime import UTC, datetime, timedelta
from pathlib import Path, PosixPath
from types import TracebackType
from typing import Annotated, Self

from pydantic import (
    AfterValidator,
    AwareDatetime,
    BaseModel,
    ConfigDict,
    PositiveInt,
    ValidationError,
)

from ..config.core import UUIDHex, _check_uuid
from ..run import (
    HOST_MOUNTS,
    INFINITY,
    REPO_ALIASES_EXT,
    REPO_DIR,
    REPO_LOCK_EXT,
    REPO_MOUNT_EXT,
    CommandError,
    Lock,
    abspath,
    atomic_symlink,
    atomic_write_text,
    run,
    sudo,
    symlink_points_to,
)

DEFAULT_REPO_FS_NAME = "ceph"
if not DEFAULT_REPO_FS_NAME:
    raise ValueError("internal default repository Ceph fs_name cannot be empty")
if "," in DEFAULT_REPO_FS_NAME:
    raise ValueError("internal default repository Ceph fs_name cannot contain commas")
DEFAULT_REPO_MOUNT_OPTIONS: tuple[str, ...] = ()
if any("," in opt for opt in DEFAULT_REPO_MOUNT_OPTIONS):
    raise ValueError(
        "internal default repository mount options cannot contain comma separators"
    )

ALIASES_SCHEMA_VERSION = 1
REPO_ORPHAN_GC_BATCH_SIZE = 8
REPO_ORPHAN_GC_GRACE = timedelta(minutes=1)
REPO_ORPHAN_GC_SLOT_PERIOD = timedelta(minutes=1)
REPO_ORPHAN_GC_BUDGET = 2.0


def _check_alias_version(value: int) -> int:
    if value != ALIASES_SCHEMA_VERSION:
        raise ValueError(
            "repository alias index file has unsupported schema version "
            f"{value}; expected {ALIASES_SCHEMA_VERSION}"
        )
    return value


def _check_alias_path(value: Path) -> Path:
    if value != abspath(value):
        raise ValueError(
            "repository alias index file contains invalid alias path: expected "
            f"canonical absolute path, got {value}"
        )
    return value


def _to_utc(value: datetime) -> datetime:
    if value.tzinfo is None or value.utcoffset() is None:
        raise ValueError(
            "repository alias index file has invalid format: 'last_accessed' must "
            "include timezone information"
        )
    return value.astimezone(UTC)


@dataclass(frozen=True)
class MountInfo:
    """Subset of `/proc/self/mountinfo` used by Bertrand's Ceph repository mounts and
    their symlink aliases, as well as other utilities that need to inspect host mounts.

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
        """
        inspected = abspath(path)
        for candidate in (inspected, *inspected.parents):
            if not candidate.is_symlink():
                continue
            try:
                target = candidate.readlink()
            except OSError as err:
                raise OSError(
                    f"failed to inspect managed alias candidate {candidate}: {err}"
                ) from err
            if not target.is_absolute():
                continue

            # raw-target ownership is strict: a managed alias must point directly to
            # Bertrand's hidden mount namespace without normalization.
            try:
                relative = target.relative_to(REPO_DIR)
            except ValueError:
                continue
            if len(relative.parts) != 2 or relative.parts[1] != REPO_MOUNT_EXT.as_posix():
                raise OSError(
                    f"repository alias path {candidate} points to malformed managed "
                    f"target {target}; expected {REPO_DIR}/<repo_id>/{REPO_MOUNT_EXT}"
                )
            try:
                repo_id = _check_uuid(relative.parts[0])
            except ValueError as err:
                raise OSError(
                    f"repository alias path {candidate} points to invalid repository "
                    f"target {target}: {err}"
                ) from err
            return repo_id, cls.search(REPO_DIR / repo_id / REPO_MOUNT_EXT)

        return None, None

    @classmethod
    def local(cls) -> dict[Path, Self]:
        """Parse and return local mount entries from `/proc/self/mountinfo`.

        Returns
        -------
        dict[Path, MountInfo]
            Parsed entries with decoded mountpoint paths as keys, for fast lookup.
        """
        try:
            lines = HOST_MOUNTS.read_text(encoding="utf-8").splitlines()
        except OSError as err:
            raise OSError(f"failed to inspect host mount table: {err}") from err

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
                mount_point=abspath(Path(
                    parts[4]
                    .replace("\\040", " ")
                    .replace("\\011", "\t")
                    .replace("\\012", "\n")
                    .replace("\\134", "\\")
                )),
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
        OSError
            If mount convergence fails or an incompatible mount already exists.
        TimeoutError
            If timeout is non-positive.
        """
        repo_id = _check_uuid(repo_id)
        if not ceph_path.is_absolute():
            ceph_path = PosixPath("/") / ceph_path
        if os.name != "posix" or platform.system() != "Linux":
            raise OSError("repository mounts are only supported on Linux platforms")
        if timeout <= 0:
            raise TimeoutError("timeout must be non-negative")

        ceph_user = ceph_user.strip()
        if not ceph_user:
            raise ValueError("repository Ceph user cannot be empty")
        if "," in ceph_user:
            raise ValueError("repository Ceph user cannot contain comma separators")
        monitors = [monitor.strip() for monitor in monitors if monitor.strip()]
        if not monitors:
            raise ValueError("repository mount requires at least one Ceph monitor endpoint")

        ceph_secretfile = ceph_secretfile.expanduser().resolve()
        if not ceph_secretfile.exists():
            raise FileNotFoundError(
                f"repository Ceph secret file does not exist: {ceph_secretfile}"
            )
        if not ceph_secretfile.is_file():
            raise FileNotFoundError(
                f"repository Ceph secret path is not a file: {ceph_secretfile}"
            )

        root = REPO_DIR / repo_id
        mount_path = root / REPO_MOUNT_EXT
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        root.mkdir(parents=True, exist_ok=True)

        async with Lock(
            root / REPO_LOCK_EXT,
            timeout=deadline - loop.time(),
            mode="local",
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
                            "-t", "ceph",
                            f"{','.join(monitors)}:{ceph_path}",
                            str(mount_path),
                            "-o", ",".join(mount_opts),
                        ]
                    ),
                    capture_output=True,
                    timeout=deadline - loop.time(),
                )
                mounted = cls.search(mount_path)
                if mounted is None:
                    raise OSError(
                        f"repository mount target {mount_path!r} failed to appear in "
                        f"{HOST_MOUNTS} after successful mount subcommand"
                    )

            if not mounted.ceph_path is not None:
                raise OSError(
                    f"repository mount target {mounted.mount_point!r} is mounted with "
                    f"unsupported source {mounted.source!r}, expected parseable Ceph "
                    "mount"
                )
            if ceph_path is not None and mounted.ceph_path != ceph_path:
                raise OSError(
                    f"repository mount target {mounted.mount_point!r} is attached to "
                    f"{mounted.source!r}, expected Ceph source suffix ':{ceph_path}'"
                )

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
            raise OSError("repository mounts are only supported on Linux platforms")
        if timeout <= 0:
            raise TimeoutError("timeout must be non-negative")

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
            raise OSError(f"unmount command failed:\n{err}") from err

        if MountInfo.search(self.mount_point) is not None:
            raise OSError(f"failed to unmount volume at {self.mount_point}")
        return True

    @dataclass
    class Aliases:
        """Async context manager for lock-scoped alias state mutation.

        Attributes
        ----------
        aliases : set[Path]
            In-memory managed alias set loaded for the repository when entered.
        """
        class JSON(BaseModel):
            """Persistent alias index payload for a single repository mount.

            Parameters
            ----------
            version : int
                Alias metadata schema version.
            aliases : list[Path]
                Declared absolute alias paths.
            last_accessed : datetime
                Last alias-state write timestamp.
            """
            model_config = ConfigDict(extra="forbid")
            version: Annotated[PositiveInt, AfterValidator(_check_alias_version)]
            aliases: list[Annotated[Path, AfterValidator(_check_alias_path)]]
            last_accessed: Annotated[AwareDatetime, AfterValidator(_to_utc)]

            @classmethod
            def load(cls, path: Path) -> Self:
                """Load alias metadata from disk with strict schema validation.

                Parameters
                ----------
                path : Path
                    Path to load alias metadata from.

                Returns
                -------
                MountInfo.Aliases.JSON
                    Parsed alias metadata.
                """
                if not path.exists():
                    return cls(
                        version=ALIASES_SCHEMA_VERSION,
                        aliases=[],
                        last_accessed=datetime.now(UTC),
                    )
                if not path.is_file():
                    raise FileNotFoundError(
                        f"repository alias index path is not a file: {path}"
                    )
                try:
                    text = path.read_text(encoding="utf-8")
                except OSError as err:
                    raise OSError(
                        f"failed to read repository alias index file: {err}"
                    ) from err
                try:
                    return cls.model_validate_json(text)
                except ValidationError as err:
                    raise ValueError(
                        f"repository alias index file has invalid format: {err}"
                    ) from err

            def dump(self, path: Path) -> None:
                """Persist alias metadata to disk in canonical JSON format.

                Parameters
                ----------
                path : Path
                    Path to persist alias metadata to.
                """
                try:
                    atomic_write_text(
                        path,
                        json.dumps(
                            self.model_dump(mode="json"),
                            sort_keys=True,
                            separators=(",", ":"),
                            ensure_ascii=False,
                            allow_nan=False,
                        ),
                        encoding="utf-8"
                    )
                except OSError as err:
                    raise OSError(
                        f"failed to write repository alias index file: {err}"
                    ) from err

            def filter(self, mount: Path) -> set[Path]:
                """Return the subset of declared aliases that still point to the given
                mount path, filtering out any stale entries.

                Parameters
                ----------
                mount : Path
                    Mounted path to check alias symlinks against.

                Returns
                -------
                set[Path]
                    Living alias paths that point to the given mount.
                """
                mount = mount.expanduser().resolve()
                return {
                    alias for alias in self.aliases
                    if symlink_points_to(alias, mount)
                }

        mount: MountInfo
        timeout: float
        gc: bool = field(default=True)
        aliases: set[Path] = field(default_factory=set)
        _root: Path = field(init=False)
        _mount_path: Path = field(init=False)
        _lock: Lock | None = field(default=None)
        _deadline: float = field(default=INFINITY)
        _depth: int = field(default=0)

        def __post_init__(self) -> None:
            if self.timeout <= 0:
                raise TimeoutError("timeout must be non-negative")
            if self.mount.repo_id is None:
                raise OSError(
                    "alias state is only available for repository mounts with a valid "
                    "repository identity"
                )
            self._root = REPO_DIR / self.mount.repo_id
            self._mount_path = self._root / REPO_MOUNT_EXT

        async def __aenter__(self) -> Self:
            """Read the alias registry for this mount, automatically pruning stale
            entries and synchronizing concurrent mutations.
            """
            if self._depth >= 1:
                self._depth += 1
                return self  # re-entrant

            # hold the repo-local lock for the entire mutation window so alias state is
            # read/modify/write atomic across concurrent init/clean callers
            loop = asyncio.get_running_loop()
            self._deadline = loop.time() + self.timeout
            self._root.mkdir(parents=True, exist_ok=True)
            self._lock = Lock(
                self._root / REPO_LOCK_EXT,
                timeout=self._deadline - loop.time(),
                mode="local",
            )
            await self._lock.lock()  # block until acquire or deadline

            # load the alias state for this mount from its registry and filter any
            # stale entries that no longer refer to this mount
            try:
                aliases = self.JSON.load(self._root / REPO_ALIASES_EXT)
                self.aliases = aliases.filter(self._mount_path)
                self._depth = 1
                return self
            except Exception:
                try:
                    await self._lock.unlock(ignore_errors=True)
                except Exception:
                    pass
                self._lock = None
                self._depth = 0
                raise

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
                raise RuntimeError(
                    "MountAliases must be used inside 'async with' before calling "
                    "link()/unlink()"
                )

            # avoid collisions with file or directory paths, as well as symlinks that
            # do not point to the expected mount
            path = abspath(path)
            missing = not (path.exists() or path.is_symlink())
            if missing:
                atomic_symlink(self._mount_path, path)
            elif not symlink_points_to(path, self._mount_path):
                raise OSError(
                    f"repository alias path {path!r} already exists and is not a "
                    f"managed symlink to {self._mount_path!r}"
                )
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
                raise RuntimeError(
                    "MountAliases must be used inside 'async with' before calling "
                    "link()/unlink()"
                )
            path = abspath(path)
            if path not in self.aliases:
                return False

            # only unlink if the symlink still points to this mount
            if symlink_points_to(path, self._mount_path):
                path.unlink()
            self.aliases.discard(path)
            return True

        async def _gc_orphan_mounts(self, *, timeout: float) -> None:
            if timeout <= 0:
                return
            loop = asyncio.get_running_loop()
            deadline = loop.time() + min(timeout, REPO_ORPHAN_GC_BUDGET)

            # collect all repository mount directories in canonical order
            try:
                roots = sorted(
                    (
                        entry for entry in REPO_DIR.iterdir()
                        if entry.is_dir() and not entry.is_symlink()
                    ),
                    key=lambda item: item.as_posix(),
                )
                if not roots:
                    return
            except FileNotFoundError:
                return
            except OSError as err:
                print(
                    "bertrand: warning: failed to scan repository roots for orphan "
                    f"mount GC: {err}",
                    file=sys.stderr,
                )
                return

            # rotate over the roots based on current timestamp to ensure GC covers the
            # whole set of mounts over time, without any extra state
            now = datetime.now(UTC)
            start = (
                int(now.timestamp() // REPO_ORPHAN_GC_SLOT_PERIOD.total_seconds()) % len(roots)
            )
            mounts = MountInfo.local()
            for offset in range(min(REPO_ORPHAN_GC_BATCH_SIZE, len(roots))):
                root = roots[(start + offset) % len(roots)]
                mount = mounts.get(root / REPO_MOUNT_EXT)
                if mount is None:
                    continue  # not a recognized mount

                # use non-blocking lock acquisition so GC always stays bounded
                remaining = deadline - loop.time()
                if remaining <= 0:
                    break
                lock = Lock(root / REPO_LOCK_EXT, timeout=0, mode="local")
                locked = False
                try:
                    locked = await lock.try_lock()
                    if not locked:
                        continue  # opportunistic lock

                    # load and filter stale aliases to check for ownership
                    now = datetime.now(UTC)
                    data = self.JSON.load(root / REPO_ALIASES_EXT)
                    if now - data.last_accessed < REPO_ORPHAN_GC_GRACE:
                        continue  # still in grace period
                    aliases = data.filter(root / REPO_MOUNT_EXT)
                    if aliases:
                        continue  # has living aliases

                    # unmount elderly orphans
                    remaining = deadline - loop.time()
                    if remaining <= 0:
                        break
                    try:
                        await mount.unmount(timeout=remaining, force=False)
                    except asyncio.CancelledError:
                        raise
                    except Exception as err:
                        print(
                            "bertrand: warning: failed to garbage-collect orphan "
                            f"repository mount {mount.mount_point}: {err}",
                            file=sys.stderr,
                        )
                except asyncio.CancelledError:
                    raise
                except Exception as err:
                    print(
                        "bertrand: warning: failed to process orphan mount GC candidate "
                        f"{root}: {err}",
                        file=sys.stderr,
                    )
                finally:
                    if locked:  # release opportunistic lock if we acquired it
                        try:
                            await lock.unlock()
                        except Exception as err:
                            print(
                                "bertrand: warning: failed to release orphan mount GC "
                                f"lock for {root}: {err}",
                                file=sys.stderr,
                            )

        async def __aexit__(
            self,
            exc_type: type[BaseException] | None,
            exc_value: BaseException | None,
            traceback: TracebackType | None,
        ) -> None:
            self._depth -= 1
            if self._depth > 0:
                return  # re-entrant

            # always write canonical alias state on exit so stale entries are pruned
            # and last_accessed is touched for GC grace calculations
            internal_error: Exception | None = None
            try:
                state = self.JSON(
                    version=ALIASES_SCHEMA_VERSION,
                    aliases=sorted(self.aliases, key=lambda item: item.as_posix()),
                    last_accessed=datetime.now(UTC),
                )
                state.dump(self._root / REPO_ALIASES_EXT)
            except Exception as err:  # pylint: disable=broad-except
                internal_error = err

            # release this mount's lock to not deadlock during GC
            if self._lock is not None:
                try:
                    await self._lock.unlock(
                        ignore_errors=internal_error is not None or exc_value is not None
                    )
                except Exception as err:  # pylint: disable=broad-except
                    if internal_error is None:
                        internal_error = err
                finally:
                    self._lock = None
            self._depth = 0

            # if GC is enabled, do a bounded scan over a rotating subset of repository
            # mounts and detach any that are orphaned (i.e. have no living aliases) and
            # have exceeded the grace period since their last alias state mutation
            if self.gc:
                try:
                    loop = asyncio.get_running_loop()
                    await self._gc_orphan_mounts(timeout=self._deadline - loop.time())
                except asyncio.CancelledError:
                    raise
                except Exception as err:  # pylint: disable=broad-except
                    print(
                        f"bertrand: warning: orphan repository mount GC failed: {err}",
                        file=sys.stderr,
                    )

            # only raise internal errors if we're not already handling an active
            # exception, otherwise report them as warnings to preserve the original
            # error
            if internal_error is not None:
                if exc_value is None:
                    raise internal_error
                print(
                    "bertrand: warning: failed to flush repository alias state during "
                    f"error unwinding: {internal_error}",
                    file=sys.stderr,
                )

    def aliases(self, *, timeout: float, gc: bool) -> Aliases:
        """Return an async context manager for this repo mount's symlink aliases.

        Parameters
        ----------
        timeout : float
            Maximum timeout for lock acquisition and alias convergence.
        gc : bool
            Whether to run orphan-mount garbage collection when the context exits.
            Usually True, but False can be useful in `clean`-like scenarios, where it
            can help avoid redundant work.

        Returns
        -------
        MountAliases
            Async context manager for in-memory alias state mutation.

        Raises
        ------
        OSError
            If this mount does not correspond to a Bertrand repository.
        """
        if self.repo_id is None:
            raise OSError(
                "alias state is only available for repository mounts with a valid "
                "repository identity"
            )
        return self.Aliases(self, timeout=timeout, gc=gc)
