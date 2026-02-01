"""A selection of atomic operations meant to be used in conjunction with CLI
pipelines.
"""
from __future__ import annotations

import hashlib
import os
import shutil
from dataclasses import dataclass
from pathlib import Path

from .pipeline import JSONValue, Pipeline, atomic
from .run import atomic_write_bytes

# pylint: disable=unused-argument


@atomic
@dataclass(frozen=True)
class WriteTextFile:
    """Write text to a file, backing up any existing file content for rollback.

    Safety: undo will NOT clobber the file if the user has modified it since apply().
    """
    path: Path
    text: str
    mode: int | None = None
    mkdir_parents: bool = True
    encoding: str = "utf-8"
    newline: str | None = None  # None mimics text-mode newline translation

    def apply(self, ctx: Pipeline.InProgress) -> dict[str, JSONValue] | None:
        p = self.path

        # Basic sanity: refuse to overwrite a directory.
        if p.exists() and p.is_dir():
            raise IsADirectoryError(str(p))

        if self.mkdir_parents:
            p.parent.mkdir(parents=True, exist_ok=True)

        # Apply newline translation similar to Python text I/O:
        # - newline=None: translate '\n' -> os.linesep
        # - newline='\n': no translation
        # - newline='\r\n' (etc): translate '\n' -> that value
        if self.newline is None:
            rendered = self.text.replace("\n", os.linesep)
        elif self.newline == "\n":
            rendered = self.text
        else:
            rendered = self.text.replace("\n", self.newline)

        data = rendered.encode(self.encoding)
        expected_hash = hashlib.sha256(data).hexdigest()
        expected_size = len(data)

        existed = p.exists()
        backup: str | None = None
        restore_mode: int | None = None
        old_mode: int | None = None

        if existed:
            try:
                old_mode = p.stat().st_mode & 0o777
            except OSError:
                old_mode = None

            # Fast no-op path if contents already match.
            try:
                old_bytes = p.read_bytes()
            except OSError:
                old_bytes = None

            if old_bytes is not None and old_bytes == data:
                mode_changed = False
                if self.mode is not None and old_mode is not None and old_mode != self.mode:
                    try:
                        p.chmod(self.mode)
                        mode_changed = True
                    except OSError:
                        pass

                if mode_changed:
                    return {
                        "path": str(p),
                        "existed": True,
                        "backup": None,
                        "restore_mode": old_mode,
                        "expected_size": expected_size,
                        "expected_hash": expected_hash,
                        "wrote": False,
                        "mode_changed": True,
                    }
                return None

            # If we will overwrite, we require a backup; if backup fails, abort.
            backup = ctx.backup(p)
            restore_mode = old_mode

        # Write new contents atomically.
        atomic_write_bytes(p, data)

        mode_changed = False
        if self.mode is not None:
            try:
                # Only mark as changed if we believe it differs.
                if old_mode is None or old_mode != self.mode:
                    p.chmod(self.mode)
                    mode_changed = True
            except OSError:
                pass

        return {
            "path": str(p),
            "existed": existed,
            "backup": backup,
            "restore_mode": restore_mode,
            "expected_size": expected_size,
            "expected_hash": expected_hash,
            "wrote": True,
            "mode_changed": mode_changed,
        }

    @staticmethod
    def undo(payload: dict[str, JSONValue]) -> None:
        p = Path(str(payload.get("path", "")))
        existed = bool(payload.get("existed", False))
        backup = payload.get("backup")
        restore_mode = payload.get("restore_mode")
        expected_size = payload.get("expected_size")
        expected_hash = payload.get("expected_hash")
        mode_changed = bool(payload.get("mode_changed", False))

        def _matches_expected() -> bool:
            if not expected_hash:
                return True  # no reference; be permissive
            try:
                cur = p.read_bytes()
            except OSError:
                return False
            if expected_size is not None and len(cur) != int(expected_size):
                return False
            return hashlib.sha256(cur).hexdigest() == str(expected_hash)

        # If we overwrote an existing file, restore it (but only if safe).
        if existed:
            if backup is not None:
                backup_data = ctx.backup(str(backup))
                if backup_data is None:
                    return
                if p.exists() and not _matches_expected():
                    return  # user changed it; don't clobber

                try:
                    p.parent.mkdir(parents=True, exist_ok=True)
                    atomic_write_bytes(p, backup_data)
                except OSError:
                    return

                if restore_mode is not None:
                    try:
                        p.chmod(int(restore_mode))
                    except OSError:
                        pass
                return

            # No backup => content wasn't changed; maybe only chmod happened.
            if mode_changed and restore_mode is not None and p.exists():
                try:
                    p.chmod(int(restore_mode))
                except OSError:
                    pass
            return

        # File did not exist before: delete it, but only if it still matches what we wrote.
        if p.exists() and _matches_expected():
            try:
                p.unlink()
            except OSError:
                pass


@atomic
@dataclass(frozen=True)
class RmTree:
    """Remove a directory tree from the host system (IRREVERSIBLE)."""
    path: Path
    ignore_errors: bool = True

    def apply(self, ctx: Pipeline.InProgress) -> dict[str, JSONValue] | None:
        p = self.path
        if not p.exists():
            return None
        if p.is_file():
            # Keep semantics tight: this op is for trees.
            raise NotADirectoryError(str(p))

        shutil.rmtree(p, ignore_errors=self.ignore_errors)
        return {"path": str(p), "irreversible": True}

    @staticmethod
    def undo(payload: dict[str, JSONValue]) -> None:
        # Intentionally a no-op: this operation is irreversible.
        return
