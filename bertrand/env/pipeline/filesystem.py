"""A selection of atomic filesystem operations meant to be used in conjunction with CLI
pipelines.
"""
from __future__ import annotations

import errno
import hashlib
import os
import shutil
import stat
import time
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import cast

from .core import JSONValue, Pipeline, atomic
from ..run import atomic_write_bytes, mkdir_private

# pylint: disable=unused-argument, missing-function-docstring, broad-exception-caught
# pylint: disable=bare-except


def _exists(path: Path) -> bool:
    try:
        path.lstat()
        return True
    except OSError:
        return False


def _is_file(path: Path) -> bool:
    try:
        return stat.S_ISREG(path.lstat().st_mode)
    except OSError:
        return False


def _is_dir(path: Path) -> bool:
    try:
        return stat.S_ISDIR(path.lstat().st_mode)
    except OSError:
        return False


def _is_symlink(path: Path) -> bool:
    try:
        return stat.S_ISLNK(path.lstat().st_mode)
    except OSError:
        return False


def _id(path: Path) -> tuple[int, int]:
    s = path.lstat()
    return (s.st_dev, s.st_ino)


def _permissions(path: Path) -> int:
    return path.lstat().st_mode & 0o7777


def _move_preserve_lstat(source: Path, dest: Path) -> None:
    """Move src -> dst preserving symlinks on cross-device moves.

    - If same filesystem: os.replace() (atomic rename).
    - If EXDEV: copy preserving symlinks, then delete src.
    """
    dest.parent.mkdir(parents=True, exist_ok=True)
    try:
        os.replace(source, dest)
        return
    except OSError as e:
        if e.errno != errno.EXDEV:
            raise

    # cross-device fallback
    st = source.lstat()
    mode = st.st_mode
    if stat.S_ISLNK(mode):
        target = os.readlink(source)
        os.symlink(target, dest)  # recreate symlink itself (dangling-safe)
        source.unlink()
        return
    if stat.S_ISREG(mode):
        shutil.copy2(source, dest, follow_symlinks=False)  # preserve metadata
        source.unlink()
        return
    if stat.S_ISDIR(mode):
        shutil.copytree(  # recursive copy
            source,
            dest,
            symlinks=True,  # preserve symlinks as symlinks
            dirs_exist_ok=False,
        )
        shutil.rmtree(source)
        return

    raise OSError(f"Unsupported file type for move: {source}")


def _only_dirs(root: Path) -> bool:
    """Return True only if every descendant can be inspected and is a directory.

    Any traversal/inspection failure is treated as unsafe and returns False.
    """
    try:
        for p in root.rglob("*"):
            try:
                mode = p.lstat().st_mode
            except OSError:
                return False
            if not stat.S_ISDIR(mode):
                return False
        return True
    except OSError:
        return False


def _remove_path(path: Path, force: bool) -> None:
    if not _exists(path):
        return
    try:
        if _is_dir(path):
            shutil.rmtree(path)
        else:
            path.unlink()
    except OSError:
        if not force:
            raise


def _stash_existing(
    ctx: Pipeline.InProgress,
    payload: dict[str, JSONValue],
    path: Path
) -> None:
    if _exists(path):
        existing: dict[str, JSONValue] = {}
        payload["existing"] = existing
        Stash(path=path).do(ctx, existing)


def _unstash_existing(
    ctx: Pipeline.InProgress,
    payload: dict[str, JSONValue],
    force: bool
) -> None:
    existing = payload.get("existing")
    if isinstance(existing, dict):
        Stash.undo(ctx, existing, force=force)
        payload.pop("existing", None)
        ctx.dump()


def _record_id(
    ctx: Pipeline.InProgress,
    payload: dict[str, JSONValue],
    path: Path,
    dev_key: str = "dev",
    ino_key: str = "ino"
) -> bool:
    try:
        dev, ino = _id(path)
        payload[dev_key] = dev
        payload[ino_key] = ino
        ctx.dump()
        return True
    except OSError:
        return False


def _check_id(
    path: Path,
    payload: dict[str, JSONValue],
    dev_key: str = "dev",
    ino_key: str = "ino"
) -> bool | None:
    dev = payload.get(dev_key)
    ino = payload.get(ino_key)
    if isinstance(dev, int) and isinstance(ino, int):
        try:
            cur_dev, cur_ino = _id(path)
            return cur_dev == dev and cur_ino == ino
        except OSError:
            pass
    return None


def _clear_id(
    ctx: Pipeline.InProgress,
    payload: dict[str, JSONValue],
    dev_key: str = "dev",
    ino_key: str = "ino"
) -> None:
    a = payload.pop(dev_key, None)
    b = payload.pop(ino_key, None)
    if a is not None or b is not None:
        ctx.dump()


def _resolve_conflict(
    ctx: Pipeline.InProgress,
    payload: dict[str, JSONValue],
    path: Path,
    replace: bool | None,
    *,
    error_message: str,
) -> None:
    """Resolve an occupied target path according to tri-state replace semantics."""
    if not _exists(path):
        return
    if replace is True:
        _stash_existing(ctx, payload, path)
        return
    if replace is None:
        _remove_path(path, force=False)
        return
    raise FileExistsError(error_message)


def _resolve_write_conflict(
    ctx: Pipeline.InProgress,
    payload: dict[str, JSONValue],
    path: Path,
    replace: bool | None,
    *,
    error_message: str,
) -> None:
    """Resolve conflicts before atomic file writes.

    For `replace=None`, keep file/symlink replacement atomic by only pre-removing
    directory conflicts that cannot be replaced in place.
    """
    if not _exists(path):
        return
    if replace is True:
        _stash_existing(ctx, payload, path)
        return
    if replace is None:
        if _is_dir(path):
            _remove_path(path, force=False)
        return
    raise FileExistsError(error_message)


@atomic
@dataclass(frozen=True)
class Remove:
    """Permanently delete a file/directory/symlink.  This operation cannot be undone.

    Attributes
    ----------
    path : Path
        The path to remove.
    """
    path: Path

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        src = self.path.absolute()
        if not _exists(src):
            return
        payload["source"] = str(src)
        ctx.dump()  # persist source before mutating
        if _is_dir(src):
            shutil.rmtree(src)
        else:
            src.unlink()

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue], force: bool) -> None:
        return  # no-op


@atomic
@dataclass(frozen=True)
class Stash:
    """Move a file/directory/symlink into a private stash, replacing it on undo.

    Attributes
    ----------
    path : Path
        The path to remove.
    """
    path: Path

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        src = self.path.absolute()
        if not _exists(src):
            return
        payload["source"] = str(src)
        ctx.dump()  # persist source before mutating

        # stash under {state}/stash/{uuid4}, where {uuid4} replaces the last path component
        stash_root = ctx.state_dir / "stash"
        dst = stash_root / uuid.uuid4().hex
        if _exists(dst):
            raise FileExistsError(f"Stash location already exists: {dst}")
        payload["stashed_at"] = str(dst)
        ctx.dump()  # persist stash location before mutating

        # move while maintaining lstat info
        mkdir_private(stash_root)
        _move_preserve_lstat(src, dst)

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue], force: bool) -> None:
        src_str = payload.get("source")
        dst_str = payload.get("stashed_at")
        if not isinstance(src_str, str) or not isinstance(dst_str, str):
            return  # do() never got far enough to matter

        # if dst is missing, there's nothing to do.  If both are missing, then some
        # kind of corruption has occurred, but we can't do anything about it here.
        dst = Path(dst_str)
        if not _exists(dst):
            payload.clear()
            ctx.dump()
            return

        # if both exist, restoring would clobber - raise an error to halt rollback
        src = Path(src_str)
        if _exists(src):
            if not force:
                raise FileExistsError(f"Cannot restore stashed path; target exists: {src}")
            _remove_path(src, force=True)
            if _exists(src):
                return

        # restore stashed path
        src.parent.mkdir(parents=True, exist_ok=True)
        try:
            _move_preserve_lstat(dst, src)
        except:
            if not force:
                raise
            return
        payload.clear()
        ctx.dump()


@atomic
@dataclass(frozen=True)
class Mkdir:
    """Create a directory (including any parent directories).  Undo removes what we
    created (without clobbering and only if empty) and restores any stashed paths.

    Attributes
    ----------
    path : Path
        The directory path to create.
    replace : bool | None, optional
        Conflict behavior when the path exists and is not a directory.  If false, raise
        an error.  If true, stash conflicting content before creating the directory.
        If None, overwrite conflicting content in place.  Defaults to false.
    private : bool, optional
        Whether to create the directory with private permissions (0700).  Defaults to
        False.  If `replace` is False and the directory already exists, enabling
        `private` may temporarily tighten permissions; undo will best-effort restore
        the prior mode without clobbering if the directory has been replaced.
    rmtree : bool, optional
        If true and the directory was created by this operation, allow undo to remove
        the full subtree even if it is non-empty.  Defaults to False.
    """
    path: Path
    replace: bool | None = False
    private: bool = False
    rmtree: bool = False

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        path = self.path.absolute()
        created = True
        if _exists(path):
            if not _is_dir(path):
                _resolve_conflict(
                    ctx,
                    payload,
                    path,
                    self.replace,
                    error_message=f"could not create directory; path occupied: {path}",
                )
            else:
                created = False
                if self.private:
                    try:
                        payload["old_permissions"] = _permissions(path)
                        if not _record_id(ctx, payload, path):
                            payload.pop("old_permissions", None)
                    except OSError:
                        pass
        if _exists(path):
            created = False

        # create new directory
        payload["path"] = str(path)
        payload["private"] = self.private
        payload["rmtree"] = self.rmtree
        payload["created"] = created
        payload["replace"] = self.replace
        ctx.dump()
        if self.private:
            mkdir_private(path)
        else:
            path.mkdir(parents=True, exist_ok=True)

        # record dev/ino to avoid clobbering during undo, but only if we created it
        if created:
            _record_id(ctx, payload, path)

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue], force: bool) -> None:
        path_str = payload.get("path")
        private = payload.get("private")
        created = payload.get("created")
        if (
            not isinstance(path_str, str) or
            not isinstance(private, bool) or
            not isinstance(created, bool)
        ):
            _clear_id(ctx, payload)
            _unstash_existing(ctx, payload, force=force)
            return
        rmtree = payload.get("rmtree")
        if not isinstance(rmtree, bool):
            rmtree = False

        # step 1: remove the directory we created, but only if it's the same one
        path = Path(path_str)
        if _exists(path):
            if created:
                if force:
                    _remove_path(path, force=True)
                    _clear_id(ctx, payload)
                else:
                    if not _is_dir(path) or not _check_id(path, payload):
                        raise FileExistsError(
                            f"Cannot remove created directory; path occupied: {path}"
                        )
                    if not rmtree and not _only_dirs(path):
                        raise FileExistsError(
                            f"Cannot remove created directory; path occupied: {path}"
                        )
                    shutil.rmtree(path)
                    _clear_id(ctx, payload)

            # if we didn't create the directory, but we did change its permissions,
            # try to restore them
            elif private:
                permissions = payload.get("old_permissions")
                if (
                    isinstance(permissions, int) and
                    (force or _check_id(path, payload) is not False)
                ):
                    try:
                        path.chmod(permissions)
                        _clear_id(ctx, payload)
                        payload.pop("old_permissions", None)
                    except OSError:
                        pass

        # step 2: restore whatever was there before
        _unstash_existing(ctx, payload, force=force)


@atomic
@dataclass(frozen=True)
class WriteBytes:
    """Write arbitrary bytes to a file on the host system, stashing an existing file
    inside the pipeline's state directory to allow for rollback.

    Attributes
    ----------
    path : Path
        The file path to write to.
    data : bytes
        The data to write to the file.
    replace : bool | None, optional
        Whether to write the file even if it already exists, stashing the existing
        file (if True) or overwriting it in-place (if None).  Otherwise, an error is
        raised if the file exists.  Defaults to False.
    private : bool, optional
        Whether to write the file with private permissions (0600).  Defaults to False.
    """
    path: Path
    data: bytes
    replace: bool | None = False
    private: bool = False

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        path = self.path.absolute()
        _resolve_write_conflict(
            ctx,
            payload,
            path,
            self.replace,
            error_message=f"could not write file; path occupied: {path}",
        )

        # write new file
        payload["path"] = str(path)
        payload["fingerprint"] = hashlib.sha256(self.data).hexdigest()
        payload["private"] = self.private
        payload["replace"] = self.replace
        ctx.dump()
        atomic_write_bytes(path, self.data, private=self.private)

        # record dev/ino to avoid clobbering during undo
        _record_id(ctx, payload, path)

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue], force: bool) -> None:
        # delete written file
        path_str = payload.get("path")
        if not isinstance(path_str, str):
            _clear_id(ctx, payload)
            _unstash_existing(ctx, payload, force=force)
            return

        # step 1: remove the file we wrote, but only if it's the same one
        path = Path(path_str)
        if _exists(path):
            if force:
                _remove_path(path, force=True)
                _clear_id(ctx, payload)
            else:
                if not _is_file(path) or not _check_id(path, payload):
                    raise FileExistsError(f"Cannot remove written file; path occupied: {path}")

                # fall back to content fingerprint to ensure we don't clobber edits
                fingerprint = payload.get("fingerprint")
                if isinstance(fingerprint, str):
                    cur = hashlib.sha256(path.read_bytes()).hexdigest()
                    if cur != fingerprint:
                        raise FileExistsError(
                            f"Cannot remove written file; contents have changed: {path}"
                        )
                path.unlink()
                _clear_id(ctx, payload)

        # step 2: restore whatever was there before, if stashed
        _unstash_existing(ctx, payload, force=force)


@atomic
@dataclass(frozen=True)
class WriteText:
    """Write arbitrary text to a file on the host system, stashing an existing file
    inside the pipeline's state directory to allow for rollback.

    Attributes
    ----------
    path : Path
        The file path to write to.
    text : str
        The text to write to the file.
    encoding : str, optional
        The text encoding to use when writing the file.  Defaults to "utf-8".
    replace : bool | None, optional
        Whether to write the file even if it already exists, stashing the existing
        file (if True) or overwriting it in-place (if None).  Otherwise, an error is
        raised if the file exists.  Defaults to False.
    private : bool, optional
        Whether to write the file with private permissions (0600).  Defaults to False.
    """
    path: Path
    text: str
    encoding: str = "utf-8"
    replace: bool | None = False
    private: bool = False

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        payload["encoding"] = self.encoding
        WriteBytes(
            path=self.path,
            data=self.text.encode(self.encoding),
            replace=self.replace,
            private=self.private,
        ).do(ctx, payload)

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue], force: bool) -> None:
        WriteBytes.undo(ctx, payload, force=force)


@atomic
@dataclass(frozen=True)
class Copy:
    """Recursively copy files/directories/symlinks to a new location.  Undo removes
    what we copied (without clobbering) and restores any stashed paths.

    If the source is a directory, then the entire directory tree will be copied and
    merged into the target location.  If the target is another directory, then this
    will not disturb any existing contents unless conflicting child paths are present,
    in which case `replace` controls whether they error, stash, or overwrite.

    Attributes
    ----------
    source : Path
        The source file/directory path to copy.
    target : Path
        The target file/directory path to copy to.
    replace : bool | None, optional
        Conflict behavior when a target path already exists.  If false, raise an error.
        If true, stash conflicts.  If None, overwrite conflicts in place.  Defaults to
        false.
    """
    source: Path
    target: Path
    replace: bool | None = False

    def _stash(
        self,
        ctx: Pipeline.InProgress,
        payload: dict[str, JSONValue],
        path: Path
    ) -> None:
        _resolve_conflict(
            ctx,
            payload,
            path,
            self.replace,
            error_message=f"could not copy to target; path occupied: {path}",
        )

    def _do(
        self,
        ctx: Pipeline.InProgress,
        payload: dict[str, JSONValue],
        source: Path,
        target: Path
    ) -> None:
        mode = source.lstat().st_mode

        # symlinks are copied directly
        if stat.S_ISLNK(mode):
            payload["kind"] = "symlink"
            ctx.dump()
            self._stash(ctx, payload, target)
            target.parent.mkdir(parents=True, exist_ok=True)
            os.symlink(os.readlink(source), target)
            _record_id(ctx, payload, target)

        # regular files are copied with fingerprinting
        elif stat.S_ISREG(mode):
            payload["kind"] = "file"
            ctx.dump()
            self._stash(ctx, payload, target)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target, follow_symlinks=False)
            fingerprint = hashlib.sha256(target.read_bytes()).hexdigest()
            payload["fingerprint"] = fingerprint
            ctx.dump()
            _record_id(ctx, payload, target)

        # directories are copied recursively
        elif stat.S_ISDIR(mode):
            contents: list[dict[str, JSONValue]] = []
            payload["kind"] = "dir"
            payload["contents"] = cast(JSONValue, contents)
            ctx.dump()

            # stash conflicts if they are not also directories
            if _exists(target):
                if _is_dir(target):
                    payload["created"] = False
                    ctx.dump()
                else:
                    self._stash(ctx, payload, target)
                    payload["created"] = True
                    ctx.dump()
                    target.mkdir(parents=True, exist_ok=True)
            else:
                payload["created"] = True
                ctx.dump()
                target.mkdir(parents=True, exist_ok=True)
            _record_id(ctx, payload, target)

            # recursively copy contents
            for item in list(source.iterdir()):
                item_target = target / item.name
                item_payload: dict[str, JSONValue] = {
                    "source": str(item),
                    "target": str(item_target),
                }
                contents.append(item_payload)
                ctx.dump()
                self._do(ctx, item_payload, item, item_target)

        else:
            raise OSError(f"Unsupported file type for copy: {source}")

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        source = self.source.absolute()
        target = self.target.absolute()
        if source == target:
            raise OSError(f"Cannot copy path onto itself: {source}")
        if target.is_relative_to(source):
            raise OSError(f"Cannot copy path into its own subpath: {source} -> {target}")

        payload["source"] = str(source)
        payload["target"] = str(target)
        payload["replace"] = self.replace
        ctx.dump()
        self._do(ctx, payload, source, target)

    @staticmethod
    def _undo(
        ctx: Pipeline.InProgress,
        payload: dict[str, JSONValue],
        target: Path,
        errors: list[str],
        force: bool,
    ) -> None:
        kind = payload.get("kind")
        if force:
            if not isinstance(kind, str) or kind not in ("symlink", "file", "dir"):
                _clear_id(ctx, payload)
                _unstash_existing(ctx, payload, force=True)
                return
            if not _exists(target):
                _clear_id(ctx, payload)
                _unstash_existing(ctx, payload, force=True)
                return
            if kind in ("symlink", "file"):
                _remove_path(target, force=True)
                payload.pop("fingerprint", None)
                _clear_id(ctx, payload)
            elif kind == "dir":
                if _is_dir(target):
                    contents = payload.get("contents")
                    if isinstance(contents, list):
                        for item_payload in reversed(contents):
                            if not isinstance(item_payload, dict):
                                continue
                            item_target_str = item_payload.get("target")
                            if isinstance(item_target_str, str):
                                item_target = Path(item_target_str)
                                Copy._undo(ctx, item_payload, item_target, errors, force=True)
                else:
                    _remove_path(target, force=True)
                created = payload.get("created")
                if isinstance(created, bool) and created:
                    _remove_path(target, force=True)
                    _clear_id(ctx, payload)
            _unstash_existing(ctx, payload, force=True)
            return

        if not isinstance(kind, str):
            errors.append(f"missing file kind: {target}")
            return
        if kind not in ("symlink", "file", "dir"):
            errors.append(f"unsupported file kind: {kind}")
            return

        # if the target is already gone, just clear identity and unstash
        if not _exists(target):
            _clear_id(ctx, payload)
            try:
                _unstash_existing(ctx, payload, force=force)
            except Exception as e:
                errors.append(f"[{kind}] error unstashing previous target: {e}")
            return

        # symlinks are removed directly
        if kind == "symlink":
            if not _is_symlink(target):
                errors.append(f"[symlink] target is not a symlink: {target}")
                return
            check = _check_id(target, payload)
            if check is False:
                errors.append(f"[symlink] target identity drifted: {target}")
                return
            if check is None:
                errors.append(f"[symlink] cannot verify target identity: {target}")
                return
            target.unlink()
            _clear_id(ctx, payload)

        # files are only removed after fingerprint check to avoid clobbering
        elif kind == "file":
            if not _is_file(target):
                errors.append(f"[file] target is not a file: {target}")
                return
            check = _check_id(target, payload)
            if check is False:
                errors.append(f"[file] target identity drifted: {target}")
                return
            if check is None:
                errors.append(f"[file] cannot verify target identity: {target}")
                return
            fingerprint = payload.get("fingerprint")
            if not isinstance(fingerprint, str):
                errors.append(f"[file] missing fingerprint: {target}")
                return
            cur = hashlib.sha256(target.read_bytes()).hexdigest()
            if cur != fingerprint:
                errors.append(f"[file] contents have changed: {target}")
                return
            target.unlink()
            payload.pop("fingerprint", None)
            _clear_id(ctx, payload)

        # directories have their children removed recursively, and are removed only if empty
        elif kind == "dir":
            if not _is_dir(target):
                errors.append(f"[dir] target is not a directory: {target}")
                return
            check = _check_id(target, payload)
            if check is False:
                errors.append(f"[dir] target identity drifted: {target}")
                return
            if check is None:
                errors.append(f"[dir] cannot verify target identity: {target}")
                return

            # recurse into children first
            contents = payload.get("contents")
            if isinstance(contents, list):
                for item_payload in reversed(contents):
                    if not isinstance(item_payload, dict):
                        continue
                    item_target_str = item_payload.get("target")
                    if isinstance(item_target_str, str):
                        item_target = Path(item_target_str)
                        Copy._undo(ctx, item_payload, item_target, errors, force)

            # avoid clobbering if we didn't create the directory
            created = payload.get("created")
            if isinstance(created, bool) and created:
                try:
                    target.rmdir()  # error if not empty
                except OSError:
                    errors.append(f"[dir] directory not empty: {target}")
                    return
                _clear_id(ctx, payload)

        # restore whatever was there before
        try:
            _unstash_existing(ctx, payload, force=force)
        except Exception as e:
            errors.append(f"[{kind}] error unstashing previous target: {e}")

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue], force: bool) -> None:
        target_str = payload.get("target")
        if isinstance(target_str, str):
            errors: list[str] = []
            Copy._undo(ctx, payload, Path(target_str), errors, force)
            if errors and not force:
                raise OSError(f"Errors occurred during copy undo:\n{'\n'.join(errors)}")
        else:
            _clear_id(ctx, payload)
            _unstash_existing(ctx, payload, force=force)


@atomic
@dataclass(frozen=True)
class Move:
    """Recursively move files/directories/symlinks to a new location.  Undo moves all
    files/directories/symlinks back to their original locations (without clobbering)
    and restores any stashed paths.

    If the source is a directory, then the entire directory tree will be moved and
    merged into the target location.  If the target is another directory, then this
    will not disturb any existing contents unless conflicting child paths are present,
    in which case `replace` controls whether they error, stash, or overwrite.

    Attributes
    ----------
    source : Path
        The source file/directory path to move.
    target : Path
        The target file/directory path to move to.
    replace : bool | None, optional
        Conflict behavior when a target path already exists.  If false, raise an error.
        If true, stash conflicts.  If None, overwrite conflicts in place.  Defaults to
        false.
    """
    source: Path
    target: Path
    replace: bool | None = False

    def _stash(
        self,
        ctx: Pipeline.InProgress,
        payload: dict[str, JSONValue],
        path: Path
    ) -> None:
        _resolve_conflict(
            ctx,
            payload,
            path,
            self.replace,
            error_message=f"could not move to target; path occupied: {path}",
        )

    def _do(
        self,
        ctx: Pipeline.InProgress,
        payload: dict[str, JSONValue],
        source: Path,
        target: Path
    ) -> None:
        mode = source.lstat().st_mode

        # symlinks are moved directly
        if stat.S_ISLNK(mode):
            payload["kind"] = "symlink"
            ctx.dump()
            self._stash(ctx, payload, target)
            _move_preserve_lstat(source, target)
            _record_id(ctx, payload, target)

        # regular files are moved with fingerprinting
        elif stat.S_ISREG(mode):
            payload["kind"] = "file"
            ctx.dump()
            self._stash(ctx, payload, target)
            _move_preserve_lstat(source, target)
            fingerprint = hashlib.sha256(target.read_bytes()).hexdigest()
            payload["fingerprint"] = fingerprint
            ctx.dump()
            _record_id(ctx, payload, target)

        # directories are moved recursively
        elif stat.S_ISDIR(mode):
            contents: list[dict[str, JSONValue]] = []
            payload["kind"] = "dir"
            payload["contents"] = cast(JSONValue, contents)
            ctx.dump()

            # stash conflicts if they are not also directories
            if _exists(target):
                if _is_dir(target):
                    payload["created"] = False
                    ctx.dump()
                else:
                    self._stash(ctx, payload, target)
                    payload["created"] = True
                    ctx.dump()
                    target.mkdir(parents=True, exist_ok=True)
            else:
                payload["created"] = True
                ctx.dump()
                target.mkdir(parents=True, exist_ok=True)
            _record_id(ctx, payload, target)

            # recursively move contents
            for item in list(source.iterdir()):
                item_target = target / item.name
                item_payload: dict[str, JSONValue] = {
                    "source": str(item),
                    "target": str(item_target),
                }
                contents.append(item_payload)
                ctx.dump()
                self._do(ctx, item_payload, item, item_target)

            # remove the now-empty source directory
            try:
                source.rmdir()
            except OSError as e:
                raise OSError(
                    f"Failed to remove source directory after move: {source}"
                ) from e

        else:
            raise OSError(f"Unsupported file type for move: {source}")

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        source = self.source.absolute()
        target = self.target.absolute()
        if source == target:
            raise OSError(f"Cannot move path onto itself: {source}")
        if target.is_relative_to(source):
            raise OSError(f"Cannot move path into its own subpath: {source} -> {target}")

        # persist intent before mutating
        payload["source"] = str(source)
        payload["target"] = str(target)
        payload["replace"] = self.replace
        ctx.dump()
        self._do(ctx, payload, source, target)

    @staticmethod
    def _undo(
        ctx: Pipeline.InProgress,
        payload: dict[str, JSONValue],
        source: Path,
        target: Path,
        errors: list[str],
        force: bool,
    ) -> None:
        kind = payload.get("kind")
        if force:
            if not isinstance(kind, str) or kind not in ("symlink", "file", "dir"):
                _clear_id(ctx, payload)
                _unstash_existing(ctx, payload, force=True)
                return
            if not _exists(target):
                _clear_id(ctx, payload)
                _unstash_existing(ctx, payload, force=True)
                return
            if kind in ("symlink", "file"):
                if _exists(source):
                    _remove_path(source, force=True)
                try:
                    _move_preserve_lstat(target, source)
                except:
                    pass
                payload.pop("fingerprint", None)
                _clear_id(ctx, payload)
            elif kind == "dir":
                source.mkdir(parents=True, exist_ok=True)
                if _is_dir(target):
                    contents = payload.get("contents")
                    if isinstance(contents, list):
                        for item_payload in reversed(contents):
                            if not isinstance(item_payload, dict):
                                continue
                            item_source_str = item_payload.get("source")
                            item_target_str = item_payload.get("target")
                            if (
                                isinstance(item_source_str, str) and
                                isinstance(item_target_str, str)
                            ):
                                item_source = Path(item_source_str)
                                item_target = Path(item_target_str)
                                Move._undo(
                                    ctx,
                                    item_payload,
                                    item_source,
                                    item_target,
                                    errors,
                                    force=True
                                )
                else:
                    _remove_path(target, force=True)
                created = payload.get("created")
                if isinstance(created, bool) and created:
                    _remove_path(target, force=True)
                    _clear_id(ctx, payload)
            _unstash_existing(ctx, payload, force=True)
            return

        if not isinstance(kind, str):
            errors.append(f"missing file kind: {target}")
            return
        if kind not in ("symlink", "file", "dir"):
            errors.append(f"unsupported file kind: {kind}")
            return

        # if the target is already gone, just clear identity and unstash
        if not _exists(target):
            _clear_id(ctx, payload)
            try:
                _unstash_existing(ctx, payload, force=force)
            except Exception as e:
                errors.append(f"[{kind}] error unstashing previous target: {e}")
            return

        # symlinks are moved back directly
        if kind == "symlink":
            if _exists(source):
                errors.append(f"source path occupied during move undo: {source}")
                return
            if not _is_symlink(target):
                errors.append(f"[symlink] target is not a symlink: {target}")
                return
            check = _check_id(target, payload)
            if check is False:
                errors.append(f"[symlink] target identity drifted: {target}")
                return
            if check is None:
                errors.append(f"[symlink] cannot verify target identity: {target}")
                return
            _move_preserve_lstat(target, source)
            _clear_id(ctx, payload)

        # files are only moved back after fingerprint check to avoid clobbering
        elif kind == "file":
            if _exists(source):
                errors.append(f"source path occupied during move undo: {source}")
                return
            if not _is_file(target):
                errors.append(f"[file] target is not a file: {target}")
                return
            check = _check_id(target, payload)
            if check is False:
                errors.append(f"[file] target identity drifted: {target}")
                return
            if check is None:
                errors.append(f"[file] cannot verify target identity: {target}")
                return
            fingerprint = payload.get("fingerprint")
            if not isinstance(fingerprint, str):
                errors.append(f"[file] missing fingerprint: {target}")
                return
            cur = hashlib.sha256(target.read_bytes()).hexdigest()
            if cur != fingerprint:
                errors.append(f"[file] contents have changed: {target}")
                return
            _move_preserve_lstat(target, source)
            payload.pop("fingerprint", None)
            _clear_id(ctx, payload)

        # directories have their children moved back recursively, and are removed only if empty
        elif kind == "dir":
            if _exists(source) and not _is_dir(source):
                errors.append(f"source path occupied during move undo: {source}")
                return
            if not _is_dir(target):
                errors.append(f"[dir] target is not a directory: {target}")
                return
            check = _check_id(target, payload)
            if check is False:
                errors.append(f"[dir] target identity drifted: {target}")
                return
            if check is None:
                errors.append(f"[dir] cannot verify target identity: {target}")
                return

            # create the source directory (even if it is empty)
            source.mkdir(parents=True, exist_ok=True)

            # recurse into children first
            contents = payload.get("contents")
            if isinstance(contents, list):
                for item_payload in reversed(contents):
                    if not isinstance(item_payload, dict):
                        continue
                    item_source_str = item_payload.get("source")
                    item_target_str = item_payload.get("target")
                    if isinstance(item_source_str, str) and isinstance(item_target_str, str):
                        item_source = Path(item_source_str)
                        item_target = Path(item_target_str)
                        Move._undo(ctx, item_payload, item_source, item_target, errors, force)

            # avoid clobbering if we didn't create the directory
            created = payload.get("created")
            if isinstance(created, bool) and created:
                try:
                    target.rmdir()  # error if not empty
                except OSError:
                    errors.append(f"[dir] directory not empty: {target}")
                    return
                _clear_id(ctx, payload)

        # restore whatever was there before
        try:
            _unstash_existing(ctx, payload, force=force)
        except Exception as e:
            errors.append(f"[unstash] error unstashing existing: {e}")

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue], force: bool) -> None:
        source_str = payload.get("source")
        target_str = payload.get("target")
        if isinstance(source_str, str) and isinstance(target_str, str):
            errors: list[str] = []
            Move._undo(ctx, payload, Path(source_str), Path(target_str), errors, force)
            if errors and not force:
                raise OSError(f"Errors occurred during move undo:\n{'\n'.join(errors)}")
        else:
            _clear_id(ctx, payload)
            _unstash_existing(ctx, payload, force=force)


@atomic
@dataclass(frozen=True)
class Symlink:
    """Create a symbolic link at `target` pointing to `source`, stashing any existing
    target.

    Attributes
    ----------
    source : Path
        The source file/directory path the symlink points to.
    target : Path
        The path at which to create the symlink.
    replace : bool | None, optional
        Conflict behavior when the target path already exists.  If false, raise an
        error.  If true, stash conflicts.  If None, overwrite conflicts in place.
        Defaults to false.
    """
    source: Path
    target: Path
    replace: bool | None = False

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        # resolve cwd, but not symlinks
        source = self.source
        target = self.target.absolute()
        _resolve_conflict(
            ctx,
            payload,
            target,
            self.replace,
            error_message=f"could not create symlink; path occupied: {target}",
        )

        # persist intent before mutating
        payload["source"] = str(source)
        payload["target"] = str(target)
        payload["replace"] = self.replace
        ctx.dump()

        # create symlink
        target.parent.mkdir(parents=True, exist_ok=True)
        target.symlink_to(source)

        # record dev/ino to avoid clobbering during undo
        _record_id(ctx, payload, target)

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue], force: bool) -> None:
        target_str = payload.get("target")
        if not isinstance(target_str, str):
            _clear_id(ctx, payload)
            _unstash_existing(ctx, payload, force=force)
            return

        # step 1: remove the symlink we created, but only if it's the same one
        target = Path(target_str)
        if _exists(target):
            if force:
                _remove_path(target, force=True)
                _clear_id(ctx, payload)
            else:
                source_str = payload.get("source")
                if (
                    not _is_symlink(target) or
                    (isinstance(source_str, str) and target.readlink() != Path(source_str)) or
                    not _check_id(target, payload)
                ):
                    raise FileExistsError(
                        f"Cannot remove created symlink; path occupied: {target}"
                    )
                target.unlink()
                _clear_id(ctx, payload)

        # step 2: restore whatever was there before
        _unstash_existing(ctx, payload, force=force)


@atomic
@dataclass(frozen=True)
class Swap:
    """Swap two files/directories/symlinks.  Undo swaps them back.

    Attributes
    ----------
    first : Path
        The first path to swap.
    second : Path
        The second path to swap.
    """
    first: Path
    second: Path

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        first = self.first.absolute()
        second = self.second.absolute()
        if os.path.samefile(first, second):
            return

        payload["first"] = str(first)
        payload["second"] = str(second)
        ctx.dump()  # persist intent before mutating
        _record_id(ctx, payload, first, "first_dev", "first_ino")
        _record_id(ctx, payload, second, "second_dev", "second_ino")

        # swap using a temporary stash location
        stash_root = ctx.state_dir / "stash"
        temp = stash_root / uuid.uuid4().hex
        mkdir_private(stash_root)
        _move_preserve_lstat(first, temp)
        _move_preserve_lstat(second, first)
        _move_preserve_lstat(temp, second)

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue], force: bool) -> None:
        first_str = payload.get("first")
        second_str = payload.get("second")
        if not isinstance(first_str, str) or not isinstance(second_str, str):
            return

        # do nothing if the paths are the same
        first = Path(first_str)
        second = Path(second_str)
        if force:
            if not _exists(first) or not _exists(second):
                return
            try:
                if os.path.samefile(first, second):
                    return
            except OSError:
                return
            stash_root = ctx.state_dir / "stash"
            temp = stash_root / uuid.uuid4().hex
            mkdir_private(stash_root)
            try:
                _move_preserve_lstat(first, temp)
                _move_preserve_lstat(second, first)
                _move_preserve_lstat(temp, second)
            except OSError:
                return
            return
        if os.path.samefile(first, second):
            return

        # verify identities to avoid clobbering
        if not _check_id(first, payload, "first_dev", "first_ino"):
            raise FileExistsError(f"Cannot undo swap; path occupied: {first}")
        if not _check_id(second, payload, "second_dev", "second_ino"):
            raise FileExistsError(f"Cannot undo swap; path occupied: {second}")

        # swap back using a temporary stash location
        stash_root = ctx.state_dir / "stash"
        temp = stash_root / uuid.uuid4().hex
        mkdir_private(stash_root)
        _move_preserve_lstat(first, temp)
        _move_preserve_lstat(second, first)
        _move_preserve_lstat(temp, second)


@atomic
@dataclass(frozen=True)
class Chmod:
    """Change the permissions bits (mode & 0o7777) of a file/directory/symlink.  Undo
    restores the previous permissions.  Symlinks are treated as no-ops to avoid
    platform-dependent behavior.

    Attributes
    ----------
    path : Path
        The path to change permissions on.
    mode : int
        The new permissions mode to set (only permission bits are used).
    """
    path: Path
    mode: int

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        path = self.path.absolute()
        if not _exists(path):
            return

        # no-op on symlinks
        if _is_symlink(path):
            payload["path"] = str(path)
            payload["symlink"] = True
            ctx.dump()
            return

        # record old mode
        st = path.lstat()
        payload["path"] = str(path)
        payload["old_mode"] = st.st_mode & 0o7777
        payload["new_mode"] = self.mode
        ctx.dump()

        # record identity to avoid clobbering during undo
        _record_id(ctx, payload, path)

        # change mode
        path.chmod(self.mode)

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue], force: bool) -> None:
        if payload.get("symlink", False):
            return  # no-op on symlinks

        path_str = payload.get("path")
        old_mode = payload.get("old_mode")
        if not isinstance(path_str, str) or not isinstance(old_mode, int):
            return

        path = Path(path_str)
        if not _exists(path):
            return

        if _is_symlink(path):
            # somehow the path became a symlink; skip chmod
            if force:
                return
            raise FileExistsError(f"Cannot undo chmod; path is now a symlink: {path}")

        # verify identity to avoid clobbering
        if not force and not _check_id(path, payload):
            raise FileExistsError(f"Cannot undo chmod; path occupied: {path}")

        # restore old mode
        path.chmod(old_mode)


@atomic
@dataclass(frozen=True)
class Chown:
    """Change the ownership of a file/directory/symlink.  Undo restores the previous
    ownership.  Symlinks are treated as no-ops to avoid platform-dependent behavior.

    Attributes
    ----------
    path : Path
        The path to change ownership on.
    uid : int | None
        The new user id to set, or None to leave unchanged.
    gid : int | None
        The new group id to set, or None to leave unchanged.
    """
    path: Path
    uid: int | None = None
    gid: int | None = None

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        path = self.path.absolute()
        if not _exists(path):
            return

        # no-op on symlinks
        if _is_symlink(path):
            payload["path"] = str(path)
            payload["symlink"] = True
            ctx.dump()
            return

        st = path.lstat()
        payload["path"] = str(path)
        payload["old_uid"] = st.st_uid
        payload["old_gid"] = st.st_gid
        ctx.dump()

        # record identity to avoid clobbering during undo
        _record_id(ctx, payload, path)
        os.chown(
            path,
            -1 if self.uid is None else self.uid,
            -1 if self.gid is None else self.gid
        )

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue], force: bool) -> None:
        if payload.get("symlink", False):
            return  # no-op on symlinks

        path_str = payload.get("path")
        old_uid = payload.get("old_uid")
        old_gid = payload.get("old_gid")
        if (
            not isinstance(path_str, str) or
            not isinstance(old_uid, int) or
            not isinstance(old_gid, int)
        ):
            return

        path = Path(path_str)
        if not _exists(path):
            return

        if _is_symlink(path):
            if force:
                return
            raise FileExistsError(f"Cannot undo chown; path is now a symlink: {path}")

        # verify identity to avoid clobbering
        if not force and not _check_id(path, payload):
            raise FileExistsError(f"Cannot undo chown; path occupied: {path}")

        os.chown(path, old_uid, old_gid)


@atomic
@dataclass(frozen=True)
class Touch:
    """Update the access and modification times of a file/directory/symlink.  Undo
    restores the previous timestamps.  Symlinks are treated as no-ops to avoid
    platform-dependent behavior.

    Attributes
    ----------
    path : Path
        The path to update timestamps on.
    atime_ns : int | None
        The access time in nanoseconds, or None to use current time.
    mtime_ns : int | None
        The modification time in nanoseconds, or None to use current time.
    """
    path: Path
    atime_ns: int | None = None
    mtime_ns: int | None = None

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        path = self.path.absolute()
        if not _exists(path):
            return

        # no-op on symlinks
        if _is_symlink(path):
            payload["path"] = str(path)
            payload["symlink"] = True
            ctx.dump()
            return

        st = path.lstat()
        payload["path"] = str(path)
        payload["old_atime_ns"] = st.st_atime_ns
        payload["old_mtime_ns"] = st.st_mtime_ns

        now = time.time_ns()
        atime_ns = now if self.atime_ns is None else self.atime_ns
        mtime_ns = now if self.mtime_ns is None else self.mtime_ns
        payload["new_atime_ns"] = atime_ns
        payload["new_mtime_ns"] = mtime_ns
        ctx.dump()

        # record identity to avoid clobbering during undo
        _record_id(ctx, payload, path)
        os.utime(path, ns=(atime_ns, mtime_ns))

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue], force: bool) -> None:
        if payload.get("symlink", False):
            return  # no-op on symlinks

        path_str = payload.get("path")
        old_atime_ns = payload.get("old_atime_ns")
        old_mtime_ns = payload.get("old_mtime_ns")
        if (
            not isinstance(path_str, str) or
            not isinstance(old_atime_ns, int) or
            not isinstance(old_mtime_ns, int)
        ):
            return

        path = Path(path_str)
        if not _exists(path):
            return

        if _is_symlink(path):
            if force:
                return
            raise FileExistsError(f"Cannot undo touch; path is now a symlink: {path}")

        # verify identity to avoid clobbering
        if not force and not _check_id(path, payload):
            raise FileExistsError(f"Cannot undo touch; path occupied: {path}")

        os.utime(path, ns=(old_atime_ns, old_mtime_ns))


@atomic
@dataclass(frozen=True)
class Extract:
    """Extract an archive into a temporary directory, then move it into a target.

    Attributes
    ----------
    archive : Path
        The archive to extract.
    target : Path
        The target directory to move extracted contents into.
    replace : bool | None, optional
        Conflict behavior in the target during the final move.  If false, raise on
        conflicts.  If true, stash conflicts.  If None, overwrite conflicts in place.
        Defaults to false.
    """
    archive: Path
    target: Path
    replace: bool | None = False

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        archive = self.archive.absolute()
        target = self.target.absolute()
        if not _exists(archive) or not _is_file(archive):
            raise FileNotFoundError(f"Archive not found: {archive}")

        temp = ctx.state_dir / "extract" / uuid.uuid4().hex
        payload["archive"] = str(archive)
        payload["target"] = str(target)
        payload["replace"] = self.replace
        payload["temp"] = str(temp)
        ctx.dump()

        # extract archive into temporary stash
        mkdir_private(temp)
        shutil.unpack_archive(archive, temp)

        # move extracted contents into target
        move_payload: dict[str, JSONValue] = {}
        payload["move"] = move_payload
        ctx.dump()
        Move(temp, target, replace=self.replace).do(ctx, move_payload)

        # delete temporary directory
        shutil.rmtree(temp, ignore_errors=True)

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue], force: bool) -> None:
        # undo move
        move_payload = payload.get("move")
        if isinstance(move_payload, dict):
            Move.undo(ctx, move_payload, force=force)

        # delete temporary directory
        temp_str = payload.get("temp")
        if isinstance(temp_str, str):
            shutil.rmtree(Path(temp_str), ignore_errors=True)
