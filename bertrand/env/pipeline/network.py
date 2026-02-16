"""A selection of atomic network operations meant to be used in conjunction with CLI
pipelines.
"""
from __future__ import annotations

import hashlib
import os
import urllib.request
import uuid
from dataclasses import dataclass
from pathlib import Path

from .pipeline import JSONValue, Pipeline, atomic
from .filesystem import (
    _check_id,
    _clear_id,
    _exists,
    _is_file,
    _record_id,
    _remove_path,
    _resolve_write_conflict,
    _unstash_existing,
)

# pylint: disable=unused-argument, missing-function-docstring, broad-exception-caught


@atomic
@dataclass(frozen=True)
class Download:
    """Download a URL to a local file, stashing any existing target.

    Attributes
    ----------
    url : str
        The URL to download.
    target : Path
        The local file path to write to.
    replace : bool | None, optional
        Controls conflict behavior at the target path.
        - False: raise if occupied.
        - True: stash existing content and restore it during undo.
        - None: overwrite/remove existing content in place without stashing.
        Defaults to false.
    sha256 : str | None, optional
        If provided, verify the downloaded content hash matches this value.
    """
    url: str
    target: Path
    replace: bool | None = False
    sha256: str | None = None

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        target = self.target.absolute()
        _resolve_write_conflict(
            ctx,
            payload,
            target,
            self.replace,
            error_message=f"could not download; path occupied: {target}"
        )

        payload["url"] = self.url
        payload["target"] = str(target)
        payload["replace"] = self.replace
        if self.sha256 is not None:
            payload["sha256"] = self.sha256

        # download to temp file in target directory
        temp = target.with_name(f"{target.name}.tmp.{uuid.uuid4().hex}")
        payload["temp"] = str(temp)
        ctx.dump()

        # build hash while downloading
        target.parent.mkdir(parents=True, exist_ok=True)
        hasher = hashlib.sha256()
        try:
            with urllib.request.urlopen(self.url, timeout=ctx.timeout) as r:
                with temp.open("wb") as f:
                    while True:
                        chunk = r.read(1024 * 64)
                        if not chunk:
                            break
                        f.write(chunk)
                        hasher.update(chunk)
        except Exception:
            try:
                temp.unlink()
            except OSError:
                pass
            raise

        # checksum verification
        digest = hasher.hexdigest()
        if self.sha256 is not None and digest != self.sha256:
            try:
                temp.unlink()
            except OSError:
                pass
            raise OSError(f"Download hash mismatch for {target}")

        # replace target with temp file
        os.replace(temp, target)
        payload["fingerprint"] = digest
        _record_id(ctx, payload, target)
        ctx.dump()

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue], force: bool) -> None:
        target_str = payload.get("target")
        if not isinstance(target_str, str):
            _clear_id(ctx, payload)
            _unstash_existing(ctx, payload, force=force)
            return

        # remove downloaded file if it can be verified
        target = Path(target_str)
        if force:
            if _exists(target):
                _remove_path(target, force=True)
            payload.pop("fingerprint", None)
            _clear_id(ctx, payload)
        elif _exists(target):
            if not _is_file(target):
                raise FileExistsError(f"Cannot remove downloaded file; path occupied: {target}")
            check = _check_id(target, payload)
            if check is False:
                raise FileExistsError(f"Cannot remove downloaded file; path occupied: {target}")
            if check is None:
                raise FileExistsError(f"Cannot verify downloaded file identity: {target}")

            fingerprint = payload.get("fingerprint")
            if not isinstance(fingerprint, str):
                raise FileExistsError(f"Missing download fingerprint: {target}")
            cur = hashlib.sha256(target.read_bytes()).hexdigest()
            if cur != fingerprint:
                raise FileExistsError(
                    f"Cannot remove downloaded file; contents have changed: {target}"
                )

            target.unlink()
            payload.pop("fingerprint", None)
            _clear_id(ctx, payload)

        # remove temp file if it exists
        temp_str = payload.get("temp")
        if isinstance(temp_str, str):
            temp = Path(temp_str)
            if _exists(temp) and _is_file(temp):
                try:
                    temp.unlink()
                except OSError:
                    pass

        # restore stashed file (if any)
        _unstash_existing(ctx, payload, force=force)
