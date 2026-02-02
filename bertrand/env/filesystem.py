"""A selection of atomic operations meant to be used in conjunction with CLI
pipelines.
"""
from __future__ import annotations

import errno
import hashlib
import os
import shutil
import stat
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Literal

from .pipeline import JSONValue, Pipeline, atomic
from .run import atomic_write_bytes, mkdir_private

# pylint: disable=unused-argument, missing-function-docstring


def _exists(path: Path) -> bool:
    try:
        path.lstat()
        return True
    except FileNotFoundError:
        return False


def _is_file(path: Path) -> bool:
    try:
        return stat.S_ISREG(path.lstat().st_mode)
    except FileNotFoundError:
        return False


def _is_dir(path: Path) -> bool:
    try:
        return stat.S_ISDIR(path.lstat().st_mode)
    except FileNotFoundError:
        return False


def _is_symlink(path: Path) -> bool:
    try:
        return stat.S_ISLNK(path.lstat().st_mode)
    except FileNotFoundError:
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
    try:
        for p in root.rglob("*"):
            try:
                mode = p.lstat().st_mode
            except FileNotFoundError:
                continue  # races: ignore disappearing entries
            if not stat.S_ISDIR(mode):
                return False
        return True
    except FileNotFoundError:
        return True


def _check_id(path: Path, payload: dict[str, JSONValue], *, dev: str, ino: str) -> bool | None:
    dev_key = payload.get(dev)
    ino_key = payload.get(ino)
    if not isinstance(dev_key, int) or not isinstance(ino_key, int):
        return None
    try:
        cur_dev, cur_ino = _id(path)
    except FileNotFoundError:
        return None
    return (cur_dev == dev_key and cur_ino == ino_key)


@atomic
@dataclass(frozen=True)
class Rm:
    """Remove a file/directory/symlink by moving it into stash.

    Attributes
    ----------
    path : Path
        The path to remove.
    stash : bool, optional
        Whether to stash the removed path to allow for undo.  Defaults to true.  If
        false, then the path will be permanently deleted and cannot be restored during
        undo.
    """
    path: Path
    stash: bool = True

    # TODO: incorporate stash into apply/undo logic

    def apply(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        # resolve cwd, but not symlinks
        src = self.path.absolute()
        if not _exists(src):
            return

        # stored under {state}/stash/{uuid4}, where {uuid4} replaces the last path component
        stash_root = ctx.state_dir / "stash"
        dst = stash_root / uuid.uuid4().hex
        if _exists(dst):
            raise FileExistsError(f"Stash location already exists: {dst}")

        # record paths first in case move is interrupted
        src_str = str(src)
        dst_str = str(dst)
        payload["source"] = src_str
        payload["stashed_at"] = dst_str
        try:
            payload["permissions"] = _permissions(src)
        except FileNotFoundError:
            # race: disappeared between _exists() and _permissions(); treat as no-op.
            payload.clear()
            return

        # persist intended move before mutating
        ctx.dump()

        # move while maintaining lstat info
        mkdir_private(stash_root)
        _move_preserve_lstat(src, dst)

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        src_str = payload.get("source")
        dst_str = payload.get("stashed_at")
        if not isinstance(src_str, str) or not isinstance(dst_str, str):
            return  # apply() never got far enough to matter

        src = Path(src_str)
        dst = Path(dst_str)
        src_exists = _exists(src)
        dst_exists = _exists(dst)

        # if dst is missing, there's nothing to do.  If both are missing, then some
        # kind of corruption has occurred, but we can't do anything about it here.
        if not dst_exists:
            payload.pop("stashed_at", None)
            payload.pop("permissions", None)
            ctx.dump()
            return

        # if both exist, restoring would clobber - raise an error to halt rollback
        if src_exists:
            raise FileExistsError(f"Cannot restore stashed path; target exists: {src}")

        # restore stashed path
        src.parent.mkdir(parents=True, exist_ok=True)
        _move_preserve_lstat(dst, src)
        payload.pop("stashed_at", None)
        ctx.dump()

        # restore permissions if possible
        perms = payload.get("permissions")
        if isinstance(perms, int):
            # only chmod if the restored entry is not a symlink
            # (chmod on symlink is non-portable; Linux chmod affects target)
            if not _is_symlink(src):
                try:
                    src.chmod(perms)
                except FileNotFoundError:
                    pass
            payload.pop("permissions", None)
            ctx.dump()


@atomic
@dataclass(frozen=True)
class Mkdir:
    """Create a directory (including any parent directories).  Undo removes what we
    created (without clobbering, only if empty) and restores any stashed paths.

    Attributes
    ----------
    path : Path
        The directory path to create.
    exists_ok : bool, optional
        If true, stash any existing directory at the path before creating a new one.
        Otherwise, raise an error if the directory already exists.  Defaults to false.
    private : bool, optional
        Whether to create the directory with private permissions (0700).  Defaults to
        False.
    """
    path: Path
    exists_ok: bool = False
    private: bool = False

    # TODO: incorporate exists_ok into apply/undo logic

    def apply(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        # resolve cwd, but not symlinks
        path = self.path.absolute()

        # stash anything at the path first
        existing: dict[str, JSONValue] = {}
        payload["existing"] = existing
        Rm(path).apply(ctx, existing)

        # create new directory
        payload["path"] = str(path)
        payload["private"] = self.private
        ctx.dump()
        if self.private:
            mkdir_private(path)
        else:
            path.mkdir(parents=True, exist_ok=True)

        # record identity of what we created so undo can avoid clobbering + detect completion
        try:
            dev, ino = _id(path)
            payload["created_dev"] = dev
            payload["created_ino"] = ino
            ctx.dump()
        except FileNotFoundError:
            # directory vanished; treat as if creation never completed
            pass

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        path_str = payload.get("path")
        if not isinstance(path_str, str):
            # Rm.apply() failed - attempt to undo and exit
            existing = payload.get("existing")
            if isinstance(existing, dict):
                Rm.undo(ctx, existing)
            return

        # step 1: remove the directory we created, but only if it's the same one
        path = Path(path_str)
        if _exists(path):
            # if we have identity info, verify it's the same directory, and don't clobber
            if (
                not _is_dir(path) or
                _check_id(path, payload, dev="created_dev", ino="created_ino") is False or
                not _only_dirs(path)
            ):
                raise FileExistsError(
                    f"Cannot remove created directory; path occupied: {path}"
                )
            shutil.rmtree(path)

        # step 2: restore whatever was there before
        existing = payload.get("existing")
        if isinstance(existing, dict):
            Rm.undo(ctx, existing)

        # clean up payload
        payload.pop("created_dev", None)
        payload.pop("created_ino", None)
        ctx.dump()


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
    exists_ok : bool, optional
        Whether to write the file even if it already exists, stashing the existing
        file.  Otherwise, an error is raised if the file exists.  Defaults to False.
    private : bool, optional
        Whether to write the file with private permissions (0600).  Defaults to False.
    """
    path: Path
    data: bytes
    exists_ok: bool = False
    private: bool = False

    # TODO: incorporate exists_ok into apply/undo logic

    def apply(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        # resolve cwd, but not symlinks
        path = self.path.absolute()

        # remove existing file
        existing: dict[str, JSONValue] = {}
        payload["existing"] = existing
        Rm(path).apply(ctx, existing)

        # write new file
        payload["path"] = str(path)
        payload["fingerprint"] = hashlib.sha256(self.data).hexdigest()
        payload["private"] = self.private
        ctx.dump()
        atomic_write_bytes(path, self.data, private=self.private)

        # record identity of the file we just created, so undo can avoid deleting a
        # different file that happens to have the same content
        try:
            dev, ino = _id(path)
            payload["created_dev"] = dev
            payload["created_ino"] = ino
            ctx.dump()
        except FileNotFoundError:
            # write may have failed; treat as if it never completed
            pass

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        # delete written file
        path_str = payload.get("path")
        if not isinstance(path_str, str):
            # Rm.apply() failed - attempt to undo and exit
            existing = payload.get("existing")
            if isinstance(existing, dict):
                Rm.undo(ctx, existing)
            return

        # step 1: remove the file we wrote, but only if it's the same one
        path = Path(path_str)
        exists = _exists(path)
        if exists:
            # if we have identity info, verify it's the same file, and don't clobber
            if (
                not _is_file(path) or
                _check_id(path, payload, dev="created_dev", ino="created_ino") is False
            ):
                raise FileExistsError(
                    f"Cannot remove written file; path occupied: {path}"
                )

            # fall back to content fingerprint to ensure we don't clobber edits
            fingerprint = payload.get("fingerprint")
            if isinstance(fingerprint, str):
                cur = hashlib.sha256(path.read_bytes()).hexdigest()
                if cur != fingerprint:
                    raise FileExistsError(
                        f"Cannot remove written file; contents have changed: {path}"
                    )
            path.unlink()

        # step 2: restore whatever was there before, if stashed
        existing = payload.get("existing")
        if isinstance(existing, dict):
            Rm.undo(ctx, existing)

        # clean up payload
        payload.pop("created_dev", None)
        payload.pop("created_ino", None)
        ctx.dump()


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
    exists_ok : bool, optional
        Whether to write the file even if it already exists, stashing the existing
        file.  Otherwise, an error is raised if the file exists.  Defaults to False
    private : bool, optional
        Whether to write the file with private permissions (0600).  Defaults to False.
    """
    path: Path
    text: str
    encoding: str = "utf-8"
    exists_ok: bool = False
    private: bool = False

    def apply(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        """Write the text to the file, moving any conflicting file to a stash location.

        Parameters
        ----------
        ctx : Pipeline.InProgress
            The in-progress pipeline context.
        payload : dict[str, JSONValue]
            A mutable dictionary that can be populated with any JSON-serializable state
            needed to undo the operation later.
        """
        payload["encoding"] = self.encoding
        WriteBytes(
            path=self.path,
            data=self.text.encode(self.encoding),
            exists_ok=self.exists_ok,
            private=self.private,
        ).apply(ctx, payload)

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        """Delete the written file and restore any stashed file back to its original
        location.

        Parameters
        ----------
        ctx : Pipeline.InProgress
        payload : dict[str, JSONValue]
            The payload returned by `apply()`.
        """
        WriteBytes.undo(ctx, payload)


@atomic
@dataclass(frozen=True)
class Copy:
    """Copy a file/dir/symlink to a new location.  Undo removes what we copied (without
    clobbering) and restores any stashed paths.

    Attributes
    ----------
    source : Path
        The source file/directory path to copy.
    target : Path
        The target file/directory path to copy to.
    exists_ok : bool, optional
        If true, stash any existing file/directory/symlink at the target path before
        copying.  Otherwise, raise an error if the target already exists.  Defaults to
        false.
    rmtree : bool, optional
        Whether to remove a copied directory tree during undo, even if it is not empty
        (the default).  If false, the directory is only removed if it exclusively
        contains other directories.
    """
    source: Path
    target: Path
    exists_ok: bool = False
    rmtree: bool = True

    # TODO: incorporate exists_ok into apply/undo logic

    def apply(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        # resolve cwd, but not symlinks
        source = self.source.absolute()
        target = self.target.absolute()

        # remove existing path
        existing: dict[str, JSONValue] = {}
        payload["existing"] = existing
        Rm(target).apply(ctx, existing)

        # persist intent before mutating
        payload["source"] = str(source)
        payload["target"] = str(target)
        payload["rmtree"] = self.rmtree
        ctx.dump()

        # copy using lstat-preserving methods
        mode = source.lstat().st_mode
        if stat.S_ISLNK(mode):
            target.parent.mkdir(parents=True, exist_ok=True)
            os.symlink(os.readlink(source), target)
        elif stat.S_ISREG(mode):
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target, follow_symlinks=False)
        elif stat.S_ISDIR(mode):
            shutil.copytree(
                source,
                target,
                symlinks=True,           # preserve symlinks as symlinks
                dirs_exist_ok=False,
            )
        else:
            raise OSError(f"Unsupported file type for copy: {source}")

        # record identity of what we created so undo can avoid clobbering + detect completion
        try:
            dev, ino = _id(target)
            payload["created_dev"] = dev
            payload["created_ino"] = ino
            ctx.dump()
        except FileNotFoundError:
            # copy may have failed; treat as if it never completed
            pass

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        target_str = payload.get("target")
        if not isinstance(target_str, str):
            # Rm.apply() failed - attempt to undo and exit
            existing = payload.get("existing")
            if isinstance(existing, dict):
                Rm.undo(ctx, existing)
            return

        target = Path(target_str)
        if _exists(target):
            # verify identity if possible
            if _check_id(target, payload, dev="created_dev", ino="created_ino") is False:
                raise FileExistsError(
                    f"Cannot remove copied path; target occupied: {target}"
                )

            # directories are only removed if empty, unless rmtree is set
            mode = target.lstat().st_mode
            if stat.S_ISDIR(mode):
                if not payload.get("rmtree", False) and not _only_dirs(target):
                    raise FileExistsError(
                        "Cannot remove copied directory; rmtree is false and "
                        f"target contains non-directory contents: {target}"
                    )
                shutil.rmtree(target)
            else:  # files/symlinks are trivially removed
                target.unlink()

        # step 2: restore whatever was there before
        existing = payload.get("existing")
        if isinstance(existing, dict):
            Rm.undo(ctx, existing)

        # clean up payload
        payload.pop("created_dev", None)
        payload.pop("created_ino", None)
        ctx.dump()


@atomic
@dataclass(frozen=True)
class Move:
    """Move a file/dir/symlink to a new location, stashing any existing target.

    Attributes
    ----------
    source : Path
        The source file/directory path to move.
    target : Path
        The target file/directory path to move to.
    exists_ok : bool, optional
        If true, stash any existing file/directory/symlink at the target path before
        moving.  Otherwise, raise an error if the target already exists.  Defaults to
        false.
    """
    source: Path
    target: Path
    exists_ok: bool = False

    # TODO: incorporate exists_ok into apply/undo logic

    def apply(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        # resolve cwd, but not symlinks
        source = self.source.absolute()
        target = self.target.absolute()

        # remove existing path
        existing: dict[str, JSONValue] = {}
        payload["existing"] = existing
        Rm(target).apply(ctx, existing)

        # persist intent before mutating
        payload["source"] = str(source)
        payload["target"] = str(target)
        ctx.dump()

        # move new file/directory
        _move_preserve_lstat(source, target)

        # record identity of what we created so undo can avoid clobbering + detect completion
        try:
            dev, ino = _id(target)
            payload["created_dev"] = dev
            payload["created_ino"] = ino
            ctx.dump()
        except FileNotFoundError:
            # move may have failed; treat as if it never completed
            pass

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        source_str = payload.get("source")
        target_str = payload.get("target")
        if not isinstance(source_str, str) or not isinstance(target_str, str):
            # Rm.apply() failed - attempt to undo and exit
            existing = payload.get("existing")
            if isinstance(existing, dict):
                Rm.undo(ctx, existing)
            return

        source = Path(source_str)
        target = Path(target_str)
        src_exists = _exists(source)
        tgt_exists = _exists(target)

        # step 1: move back the file/directory we moved, but only if it's the same one
        if tgt_exists:
            if (
                src_exists or
                _check_id(target, payload, dev="created_dev", ino="created_ino") is False
            ):
                raise FileExistsError(
                    f"Cannot restore moved path; source exists or target occupied: {source}"
                )
            source.parent.mkdir(parents=True, exist_ok=True)
            _move_preserve_lstat(target, source)
            payload.pop("created_dev", None)
            payload.pop("created_ino", None)
            ctx.dump()

        # step 2: restore whatever was at the target before the move
        existing = payload.get("existing")
        if isinstance(existing, dict):
            Rm.undo(ctx, existing)


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
    exists_ok : bool, optional
        If true, stash any existing file/directory/symlink at the target path before
        creating the symlink.  Otherwise, raise an error if the target already exists.
        Defaults to false.
    """
    source: Path
    target: Path
    exists_ok: bool = False

    # TODO: incorporate exists_ok into apply/undo logic

    def apply(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        # resolve cwd, but not symlinks
        source = self.source
        target = self.target.absolute()

        # remove existing path
        existing: dict[str, JSONValue] = {}
        payload["existing"] = existing
        Rm(target).apply(ctx, existing)

        # persist intent before mutating
        payload["source"] = str(source)
        payload["target"] = str(target)
        ctx.dump()

        # create symlink
        target.parent.mkdir(parents=True, exist_ok=True)
        target.symlink_to(source)

        # record identity of what we created so undo can avoid clobbering + detect completion
        try:
            dev, ino = _id(target)
            payload["created_dev"] = dev
            payload["created_ino"] = ino
            ctx.dump()
        except FileNotFoundError:
            # symlink creation may have failed; treat as if it never completed
            pass

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        target_str = payload.get("target")
        if not isinstance(target_str, str):
            # Rm.apply() failed - attempt to undo and exit
            existing = payload.get("existing")
            if isinstance(existing, dict):
                Rm.undo(ctx, existing)
            return

        # step 1: remove the symlink we created, but only if it's the same one
        target = Path(target_str)
        if _exists(target):
            if (
                not _is_symlink(target) or
                _check_id(target, payload, dev="created_dev", ino="created_ino") is False
            ):
                raise FileExistsError(
                    f"Cannot remove created symlink; path occupied: {target}"
                )
            target.unlink()

        # step 2: restore whatever was there before
        existing = payload.get("existing")
        if isinstance(existing, dict):
            Rm.undo(ctx, existing)

        # clean up payload
        payload.pop("created_dev", None)
        payload.pop("created_ino", None)
        ctx.dump()


# TODO: SwapDirs?


# TODO: Merge, which copies/moves into an existing source tree?


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

    def apply(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
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
        payload["dev"] = st.st_dev
        payload["ino"] = st.st_ino
        ctx.dump()

        # change mode
        path.chmod(self.mode)

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
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
            raise FileExistsError(f"Cannot undo chmod; path is now a symlink: {path}")

        # verify identity to avoid clobbering
        dev = payload.get("dev")
        ino = payload.get("ino")
        if isinstance(dev, int) and isinstance(ino, int):
            cur = path.lstat()
            if cur.st_dev != dev or cur.st_ino != ino:
                raise FileExistsError(f"Cannot undo chmod; path occupied: {path}")

        # restore old mode
        path.chmod(old_mode)


# TODO: Chown?


# TODO: Touch, which just updates timestamps?


# TODO: Extract?


###############################
####    PACKAGE MANAGER    ####
###############################


# @atomic
# @dataclass(frozen=True)
# class InstallPackage:
#     """Install a package using the host system's package manager.
#     """
#     package_manager: Literal["apt-get", "dnf"]
#     prefix: list[str] = []

