"""Host-side Ceph repository mount lifecycle utilities."""
from __future__ import annotations

import asyncio
import json
import os
import platform
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path, PosixPath
from typing import Self

from ..run import (
    HOST_MOUNTS,
    REPO_ALIASES_EXT,
    REPO_DIR,
    REPO_LOCK_EXT,
    REPO_MOUNT_EXT,
    Lock,
    atomic_symlink,
    atomic_write_text,
    run,
    symlink_points_to,
    sudo,
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


# TODO: I also have to cover true garbage collection for stale mounts that have no
# aliases, separate from just the unmount on last alias removal.  This should
# periodically scan the mount directory for any such mounts and unmount them, probably
# in blocks of a fixed size every time we mount a new repository, similar to the
# current environment registry.


@dataclass(frozen=True)
class MountInfo:
    """Subset of `/proc/self/mountinfo` fields used by Bertrand mount validation.

    This model intentionally captures only the fields needed for host mount
    convergence and mismatch detection. We do not retain mount IDs, parent IDs,
    mount root, mount options, optional propagation fields, or super options.

    Attributes
    ----------
    mount_point : Path
        Mountpoint path from mountinfo field 5 (pre-`-` section). Kernel escaping
        is decoded before storage (`\\040`, `\\011`, `\\012`, `\\134`), then this
        value is compared with normalized exact-path matching.
    fs_type : str
        Filesystem type from the first field after the `-` separator. For Ceph host
        attachments we expect this to be `"ceph"`.
    source : str
        Kernel-reported mount source from the second field after `-` (device/export/
        source path string). Used for idempotency checks and mismatch detection.
    """
    mount_point: Path
    fs_type: str
    source: str

    @classmethod
    def all(cls) -> list[Self]:
        """Parse and return mount entries from `/proc/self/mountinfo`.

        Returns
        -------
        list[MountInfo]
            Parsed entries with decoded mountpoint paths.
        """
        try:
            lines = HOST_MOUNTS.read_text(encoding="utf-8").splitlines()
        except OSError as err:
            raise OSError(f"failed to inspect host mount table: {err}") from err

        rows: list[Self] = []
        for line in lines:
            parts = line.strip().split()
            # Ignore malformed rows defensively; kernel format should contain enough
            # fields for mountpoint + post-separator fs/source data.  The "-"
            # separator splits optional fields from fs/source fields.
            if len(parts) < 10:
                continue
            try:
                sep = parts.index("-")
            except ValueError:
                continue
            if sep + 2 >= len(parts):
                continue

            # Field 5 (pre-`-`): mount point; decode mountinfo escapes.
            mount_point = Path(
                parts[4]
                .replace("\\040", " ")
                .replace("\\011", "\t")
                .replace("\\012", "\n")
                .replace("\\134", "\\")
            )

            # Post-separator fields: filesystem type and mount source.
            rows.append(
                cls(
                    mount_point=mount_point,
                    fs_type=parts[sep + 1],
                    source=parts[sep + 2],
                )
            )
        return rows

    @classmethod
    def under(cls, root: Path) -> list[Path]:
        """Return mounted paths at or below `root`.

        Parameters
        ----------
        root : Path
            Root path to check for mounted entries at or below.  This is normalized and
            compared with normalized mount point paths for prefix matching.

        Returns
        -------
        list[Path]
            List of mount point paths that are mounted at or below `root`.
        """
        prefix = os.path.normpath(root)
        matches: list[Path] = []
        for entry in cls.all():
            norm = os.path.normpath(entry.mount_point)
            if norm == prefix or norm.startswith(prefix + os.sep):
                matches.append(entry.mount_point)
        return matches

    @classmethod
    def search(cls, path: Path) -> Self | None:
        """Check whether `path` points to a recognized host mount entry in
        `/proc/self/mountinfo`.

        Parameters
        ----------
        path : Path
            Absolute path to check for an exact mount match.

        Returns
        -------
        MountInfo | None
            The matching mount entry when an exact normalized match is found, or None
            when no match is found (i.e. the path is not currently mounted).
        """
        needle = os.path.normpath(path)
        for entry in cls.all():
            # require exact normalized mountpoint equality so parent/child paths are
            # never mistaken for this exact mount target
            if os.path.normpath(entry.mount_point) == needle:
                return entry

        # no exact entry means this path is not currently mounted
        return None

    def is_cephfs(self) -> bool:
        """
        Returns
        -------
        bool
            True when this mount entry is a kernel CephFS mount.
        """
        return self.fs_type.strip().lower() == "ceph"

    def references_ceph_path(self, ceph_path: PosixPath) -> bool:
        """Check whether the target of this mount references the given CephFS path.

        Parameters
        ----------
        ceph_path : PosixPath
            Absolute or relative CephFS path to check for.  If relative, it is resolved
            against the CephFS root (i.e. treated as if it were prefixed with "/").

        Returns
        -------
        bool
            True if this mount's source references the given CephFS path.
        """
        if not ceph_path.is_absolute():
            ceph_path = PosixPath("/") / ceph_path
        return self.source.endswith(f":{ceph_path}")


@dataclass(frozen=True)
class RepoMount:
    """Structured metadata for a CephFS-backed repository mount on the host.

    One of these mounts will be created whenever Bertrand initializes a new project.
    The resulting mount will be attached to a private directory on the host system,
    and a symlink to the mounted directory will be placed at the initialized path.
    The mount itself should point at a single git repository together with one or
    more worktrees, along with any extra metadata needed to manage the mount itself.

    Attributes
    ----------
    repo_id : str
        Stable repository identity used for deterministic host mount state paths.
    ceph_path : PosixPath
        Absolute CephFS path to mount for this repository identity within the Ceph
        filesystem.
    """
    repo_id: str
    ceph_path: PosixPath

    def _assert_ceph_mount(
        self,
        mount: MountInfo,
        *,
        ceph_path: PosixPath | None = None
    ) -> None:
        if not mount.is_cephfs():
            raise OSError(
                f"repository mount target {mount.mount_point!r} is mounted with "
                f"unsupported filesystem type {mount.fs_type!r}, expected 'ceph'"
            )
        if ceph_path is not None and not mount.references_ceph_path(ceph_path):
            raise OSError(
                f"repository mount target {mount.mount_point!r} is attached to "
                f"{mount.source!r}, expected Ceph source suffix ':{ceph_path}'"
            )

    @staticmethod
    def _alias_path(path: Path) -> Path:
        return Path(os.path.abspath(str(path.expanduser())))

    def _load_aliases(self) -> set[Path]:
        root = REPO_DIR / self.repo_id
        mount_path = root / REPO_MOUNT_EXT
        path = root / REPO_ALIASES_EXT
        if not path.exists():
            return set()
        if not path.is_file():
            raise FileNotFoundError(f"repository alias index path is not a file: {path}")
        text = path.read_text(encoding="utf-8")
        data = json.loads(text)
        if not isinstance(data, list):
            raise TypeError(
                f"repository alias index file has invalid format: expected a JSON "
                f"list of alias paths, got {type(data).__name__}"
            )
        aliases = set()
        for item in data:
            if not isinstance(item, str):
                raise TypeError(
                    f"repository alias index file has invalid format: expected a JSON "
                    f"list of alias paths, got an item of type {type(item).__name__}"
                )
            alias = Path(item)
            if not alias.is_absolute():
                raise ValueError(
                    "repository alias index file contains invalid alias path: "
                    f"expected absolute path, got {alias}"
                )
            if any(part in (".", "..") for part in alias.parts):
                raise ValueError(
                    "repository alias index file contains invalid alias path: "
                    f"cannot contain '.' or '..' segments, got {alias}"
                )
            if symlink_points_to(alias, mount_path):
                aliases.add(alias)
        return aliases

    def _write_aliases(self, aliases: set[Path]) -> None:
        path = REPO_DIR / self.repo_id / REPO_ALIASES_EXT
        try:
            text = json.dumps(
                sorted(str(alias) for alias in aliases),
                sort_keys=True,
                separators=(",", ":"),
                ensure_ascii=False,
                allow_nan=False,
            )
            atomic_write_text(path, text, encoding="utf-8")
        except OSError as err:
            raise OSError(f"failed to write repository alias index file: {err}") from err

    async def mount(
        self,
        path: Path,
        *,
        timeout: float,
        monitors: Sequence[str],
        ceph_user: str,
        ceph_secretfile: Path,
    ) -> None:
        """Ensure this repo mount is mounted to the host and exposed at the given path.

        Parameters
        ----------
        path : Path
            Absolute path where the repository should be accessible.  A symlink will be
            created at this path pointing to the actual mount location, which is
            stored internally and not meant to be directly accessed by users.  Instead,
            the symlink can can be freely renamed, relocated, or removed without
            affecting the underlying Ceph volume.
        timeout : float
            Maximum runtime command timeout in seconds for the entire mount operation,
            including any necessary setup, validation, and retries.  If infinite, wait
            indefinitely.
        monitors : Sequence[str]
            List of Ceph monitor endpoints to use for mounting.  Each endpoint should
            be a valid Ceph monitor address.
        ceph_user : str
            Ceph user name to use for mounting.  This should correspond to a valid Ceph
            user with appropriate permissions to access the repository volume.
        ceph_secretfile : Path
            Path to the Ceph secret file containing the authentication credentials for
            the specified Ceph user.  This file must exist and be a regular file, and
            it should have appropriate permissions to ensure security.
        """
        root = REPO_DIR / self.repo_id
        mount_path = root / REPO_MOUNT_EXT
        ceph_path = self.ceph_path
        if os.name != "posix" or platform.system() != "Linux":
            raise OSError("repository mounts are only supported on Linux platforms")
        if timeout <= 0:
            raise TimeoutError("timeout must be non-negative")
        path = self._alias_path(path)
        if path.is_symlink() and not symlink_points_to(path, mount_path):
            raise OSError(
                f"repository alias path {path!r} already exists and is not a "
                f"managed symlink to {mount_path!r}"
            )
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

        # use a local repository lock to prevent race conditions
        loop = asyncio.get_event_loop()
        deadline = loop.time() + timeout
        root.mkdir(parents=True, exist_ok=True)
        async with Lock(
            root / REPO_LOCK_EXT,
            timeout=deadline - loop.time(),
            mode="local",
        ):
            # get or create the host mount for this repository claim
            mounted = MountInfo.search(mount_path)
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
                mounted = MountInfo.search(mount_path)
                if mounted is None:
                    raise OSError(
                        f"repository mount target {mount_path!r} failed to appear in "
                        f"{HOST_MOUNTS} after successful mount subcommand"
                    )

            # validate ceph mount, then write public symlink
            self._assert_ceph_mount(mounted, ceph_path=ceph_path)
            if path.exists() and not symlink_points_to(path, mount_path):
                raise OSError(
                    f"repository alias path {path!r} already exists and is not a "
                    f"managed symlink to {mount_path!r}"
                )
            atomic_symlink(mount_path, path)

            # register symlink alias
            aliases = self._load_aliases()
            aliases.add(path)
            self._write_aliases(aliases)

    async def unmount(
        self,
        path: Path,
        *,
        timeout: float,
        force: bool,
        lazy: bool,
    ) -> bool:
        """Delete an alias symlink and detach the underlying mount if it is the last
        registered alias.

        Parameters
        ----------
        path : Path
            Absolute path to a symlink produced by `mount()`.  If this is the last
            symlink alias to the underlying mount, the mount will be detached from the
            host.
        timeout : float
            Maximum runtime command timeout in seconds for the entire unmount
            operation, including any necessary setup, validation, and retries.  If
            infinite, wait indefinitely.
        force : bool
            If True, forcefully unmount the repository volume even if it is currently
            referenced by active processes.  This option is passed directly to the
            `umount` command as `-f` and should be used with caution, as it can lead to
            data loss if processes are actively reading from or writing to the volume.
        lazy : bool
            If True, defer the unmount operation until the mount is no longer busy.
            This option is passed directly to the `umount` command as `-l` and can be
            used to avoid errors when the volume is currently in use, but it can lead
            to resource leaks if the mount is never released by the system, or if it
            is resurrected before the lazy unmount can take effect.

        Returns
        -------
        bool
            True if the underlying mount was detached from the host as a result of this
            operation, or False if the mount is still present (e.g. because other
            aliases still point to it).
        """
        root = REPO_DIR / self.repo_id
        mount_path = root / REPO_MOUNT_EXT
        if os.name != "posix" or platform.system() != "Linux":
            raise OSError("repository mounts are only supported on Linux platforms")
        if timeout <= 0:
            raise TimeoutError("timeout must be non-negative")
        path = self._alias_path(path)

        # use a local repository lock to prevent race conditions
        loop = asyncio.get_event_loop()
        deadline = loop.time() + timeout
        root.mkdir(parents=True, exist_ok=True)
        async with Lock(
            root / REPO_LOCK_EXT,
            timeout=deadline - loop.time(),
            mode="local",
        ):
            # remove symlink
            if not symlink_points_to(path, mount_path):
                raise OSError(
                    f"repository alias path {path!r} must be an existing managed "
                    f"symlink to {mount_path!r}"
                )
            path.unlink()

            # scan the system's active mount info and confirm it's a valid ceph mount
            # (best-effort)
            mount = MountInfo.search(mount_path)
            if mount is None:
                return False  # already unmounted
            self._assert_ceph_mount(mount)

            # remove from the mount's registered aliases, if present
            aliases = self._load_aliases()
            aliases.discard(path)
            self._write_aliases(aliases)
            if aliases:
                return False  # active aliases still exist

            # check to see if there are any active processes using the mount
            if not force and not lazy:
                result = await run(
                    sudo(["fuser", "-m", str(mount_path)]),
                    check=False,
                    capture_output=True,
                    timeout=deadline - loop.time(),
                )
                if result.returncode == 0:
                    detail = (f"{result.stdout}\n{result.stderr}").strip()
                    raise OSError("\n".join((
                        f"cannot remove repository mount at {mount_path} because it is "
                        "still in use",
                        detail
                    )))
                if result.returncode != 1:
                    detail = (f"{result.stdout}\n{result.stderr}").strip()
                    raise OSError("\n".join((
                        f"failed to determine whether repository mount at {mount_path} "
                        "is busy",
                        detail
                    )))

            # unmount the repository volume
            cmd = ["umount"]
            if force:
                cmd.append("-f")
            if lazy:
                cmd.append("-l")
            cmd.append(str(mount_path))
            await run(
                sudo(cmd),
                capture_output=True,
                timeout=deadline - loop.time(),
            )
            if MountInfo.search(mount_path) is not None:
                raise OSError(
                    f"repository hidden mount is still attached after umount: {mount_path}"
                )
            return True
