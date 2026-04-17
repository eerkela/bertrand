"""Ceph credential lifecycle utilities for Bertrand-managed repository volumes."""
from __future__ import annotations

import asyncio
import os
import re
import tempfile
from collections.abc import Iterator
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path, PosixPath
from typing import Annotated

from pydantic import BaseModel, ConfigDict, Field

from ..config.core import UUIDHex, _check_uuid
from ..run import RUN_DIR, CommandError, ceph, start_microceph

REPO_FS_NAME = "ceph"
REPO_ENTITY_PREFIX = "bertrand-repo-"
MON_ENDPOINT_RE = re.compile(r"^(?:v[12]:)?(?P<addr>.+)$")


class CephMonitors(BaseModel):
    """Validated subset of `ceph mon dump -f json` payload."""
    class Mon(BaseModel):
        """Validated subset of one monitor entry."""
        class PublicAddrs(BaseModel):
            """Validated subset of monitor public address entries."""
            class AddrVec(BaseModel):
                """Validated subset of one monitor address vector entry."""
                model_config = ConfigDict(extra="ignore")
                addr: Annotated[str, Field(default="")]

            model_config = ConfigDict(extra="ignore")
            addrvec: Annotated[list[CephMonitors.Mon.PublicAddrs.AddrVec], Field(
                default_factory=list
            )]

        model_config = ConfigDict(extra="ignore")
        addr: Annotated[str, Field(default="")]
        public_addrs: Annotated[CephMonitors.Mon.PublicAddrs | None, Field(default=None)]

    model_config = ConfigDict(extra="ignore")
    mons: Annotated[list[CephMonitors.Mon], Field(default_factory=list)]

    @staticmethod
    def _is_host_port(value: str) -> bool:
        if not value:
            return False

        if value.startswith("["):
            end = value.rfind("]")
            if end < 1 or end + 2 > len(value) or value[end + 1] != ":":
                return False
            return value[end + 2:].isdigit()

        if value.count(":") != 1:
            return False
        host, port = value.rsplit(":", 1)
        return bool(host) and port.isdigit()

    @staticmethod
    def _extract_addr_token(value: str) -> str:
        value = value.strip()
        if not value:
            return ""
        value = value.split("/", 1)[0].strip()
        match = MON_ENDPOINT_RE.match(value)
        if match is None:
            return ""
        return match.group("addr").strip()

    def endpoints(self) -> tuple[str, ...]:
        """Extract normalized unique monitor endpoints in first-seen order.

        Returns
        -------
        tuple[str, ...]
            Unique monitor endpoints in `host:port` format, in the order they were
            reported by the cluster.  Endpoints that cannot be parsed as `host:port`
            are ignored.
        """
        out: list[str] = []
        seen: set[str] = set()
        for mon in self.mons:
            # newer format: mons[].public_addrs.addrvec[].addr = "v2:x:3300/0"
            if mon.public_addrs is not None:
                for item in mon.public_addrs.addrvec:
                    endpoint = self._extract_addr_token(item.addr).strip()
                    if not self._is_host_port(endpoint) or endpoint in seen:
                        continue
                    seen.add(endpoint)
                    out.append(endpoint)

            # older format: mons[].addr = "v2:x:3300/0,v1:x:6789/0"
            elif mon.addr:
                for token in mon.addr.split(","):
                    endpoint = self._extract_addr_token(token).strip()
                    if not self._is_host_port(endpoint) or endpoint in seen:
                        continue
                    seen.add(endpoint)
                    out.append(endpoint)

        return tuple(out)


async def _get_key(entity: str, *, timeout: float | None) -> str | None:
    try:
        result = await ceph(
            ["auth", "get-key", entity],
            timeout=timeout,
            capture_output=True,
        )
    except CommandError as err:
        detail = f"{err.stdout}\n{err.stderr}".lower()
        if (
            "not found" in detail or
            "does not exist" in detail or
            "enoent" in detail or
            "error enoent" in detail
        ):
            return None
        # NOTE: we intentionally do not re-raise or print the error here, since it
        # could possibly expose a secret key (although unlikely)
        raise OSError(f"failed to read Ceph credentials for {entity!r}")  # noqa: B904
    key = result.stdout.strip()
    if not key:
        raise OSError(f"Ceph returned an empty key for {entity!r}")
    return key


async def _get_monitors(*, timeout: float | None) -> tuple[str, ...]:
    result = await ceph(
        ["mon", "dump", "-f", "json"],
        timeout=timeout,
        capture_output=True,
    )
    parsed = CephMonitors.model_validate_json(result.stdout)
    out = parsed.endpoints()
    if not out:
        raise OSError("Ceph monitor dump did not provide any usable monitor endpoints")
    return out


@dataclass(frozen=True)
class RepoCredentials:
    """Ceph auth material for one repository identity.

    Attributes
    ----------
    repo_id : str
        Repository UUID that these credentials are associated with.
    entity : str
        A Ceph entity derived from `repo_id`, which tracks the per-repository user
        identity in the Ceph cluster.
    key : str
        CephX key for the repository entity.
    monitors : tuple[str, ...]
        Ordered Ceph monitor endpoints in `host:port` format.
    """
    repo_id: UUIDHex
    entity: str
    key: str
    monitors: tuple[str, ...]


async def ensure_repo_credentials(
    repo_id: UUIDHex,
    *,
    ceph_path: PosixPath,
    timeout: float | None,
) -> RepoCredentials:
    """Ensure per-repo Ceph credentials exist and return fresh auth material.

    Parameters
    ----------
    repo_id : str
        Repository UUID to ensure credentials for.
    ceph_path : PosixPath
        CephFS path prefix to authorize this identity for (for example, `/myrepo`).
    timeout : float | None
        Maximum time to wait for the cluster to be ready and credentials to
        materialize, in seconds.  If None, wait indefinitely.

    Returns
    -------
    RepoCredentials
        Fresh credentials for this repository identity.  The credentials will be
        created if they do not already exist.

    Notes
    -----
    This function is idempotent and lock-free, and the credentials it returns are
    bounded to the lifetime of a corresponding CephFS repository volume, which is only
    removed explicitly deleted via the CLI and no active reference exists in the
    cluster.  As a result, concurrent ensure/delete operations for the same `repo_id`
    should never occur under normal operation, and if they do, they should fail
    gracefully.
    """
    if timeout is not None and timeout < 0:
        raise TimeoutError("timeout must be non-negative")
    repo_id = _check_uuid(repo_id)
    if not ceph_path.parts:
        raise ValueError("Ceph repository path cannot be empty")
    if not ceph_path.is_absolute():
        ceph_path = PosixPath("/") / ceph_path
    entity = f"{REPO_ENTITY_PREFIX}{repo_id}"
    loop = asyncio.get_running_loop()
    deadline = 0.0 if timeout is None else loop.time() + timeout

    # bootstrap the microceph cluster if it's not already running
    await start_microceph(timeout=None if timeout is None else deadline - loop.time())

    # `fs authorize` is idempotent and converges CephX caps for this identity.
    await ceph(
        ["fs", "authorize", REPO_FS_NAME, entity, str(ceph_path), "rw"],
        timeout=None if timeout is None else deadline - loop.time(),
        capture_output=True,
    )

    # `fs authorize` should materialize a key for this entity if it didn't already
    # exist.
    key = await _get_key(
        entity,
        timeout=None if timeout is None else deadline - loop.time(),
    )
    if key is None:
        raise OSError(
            f"Ceph authorization did not materialize credentials for {entity!r}"
        )

    # retrieve current monitor endpoints; this ensures the caller has enough
    # information to confidently connect to and mount the repository using the
    # returned credentials.
    monitors = await _get_monitors(
        timeout=None if timeout is None else deadline - loop.time()
    )
    return RepoCredentials(
        repo_id=repo_id,
        entity=entity,
        key=key,
        monitors=monitors,
    )


async def get_repo_credentials(
    repo_id: UUIDHex,
    *,
    timeout: float | None,
) -> RepoCredentials | None:
    """Get existing per-repo Ceph credentials without creating new identities.

    Parameters
    ----------
    repo_id : str
        Repository UUID to get credentials for.
    timeout : float | None
        Maximum time to wait for the cluster to be ready and credentials to
        materialize, in seconds.  If None, wait indefinitely.

    Returns
    -------
    RepoCredentials | None
        Fresh credentials for this repository identity, or None if credentials do not
        exist.
    """
    if timeout is not None and timeout < 0:
        raise TimeoutError("timeout must be non-negative")
    repo_id = _check_uuid(repo_id)
    entity = f"{REPO_ENTITY_PREFIX}{repo_id}"
    loop = asyncio.get_running_loop()
    deadline = 0.0 if timeout is None else loop.time() + timeout

    # bootstrap the microceph cluster if it's not already running
    await start_microceph(timeout=None if timeout is None else deadline - loop.time())

    # get repository credentials if they exist
    key = await _get_key(
        entity,
        timeout=None if timeout is None else deadline - loop.time(),
    )
    if key is None:
        return None

    # retrieve current monitor endpoints
    monitors = await _get_monitors(
        timeout=None if timeout is None else deadline - loop.time()
    )
    return RepoCredentials(
        repo_id=repo_id,
        entity=entity,
        key=key,
        monitors=monitors,
    )


# TODO: deleting the credentials for a volume should only occur after deleting the
# volume itself, which can only occur when there are zero active pods referencing it,
# and the user explicitly deletes that volume via the CLI.  When that occurs, I should
# also trigger each node in the cluster to unmount the volume and delete any local
# symlinks referencing it, then proceed to delete its credentials.


async def delete_repo_credentials(repo_id: UUIDHex, *, timeout: float | None) -> bool:
    """Delete per-repo Ceph credentials if they exist.

    Parameters
    ----------
    repo_id : str
        Repository UUID to delete credentials for.
    timeout : float | None
        Maximum time to wait for the cluster to be ready and credentials to be deleted,
        in seconds.  If None, wait indefinitely.

    Returns
    -------
    bool
        True if credentials were deleted, False if credentials did not exist.

    Notes
    -----
    This function intentionally uses eventual-consistency semantics.  Concurrent
    ensure/delete operations for the same `repo_id` may race in Ceph's auth DB.
    Callers should retry and revalidate on transient auth failures.
    """
    if timeout is not None and timeout < 0:
        raise TimeoutError("timeout must be non-negative")
    repo_id = _check_uuid(repo_id)
    entity = f"{REPO_ENTITY_PREFIX}{repo_id}"
    loop = asyncio.get_running_loop()
    deadline = 0.0 if timeout is None else loop.time() + timeout

    # bootstrap the microceph cluster if it's not already running
    await start_microceph(timeout=None if timeout is None else deadline - loop.time())

    result = await ceph(
        ["auth", "del", entity],
        timeout=None if timeout is None else deadline - loop.time(),
        check=False,
        capture_output=True,
    )
    if result.returncode == 0:
        return True
    detail = f"{result.stdout}\n{result.stderr}".lower()
    if (
        "not found" in detail or
        "does not exist" in detail or
        "enoent" in detail or
        "error enoent" in detail
    ):
        return False
    # NOTE: we intentionally do not print the error here, since it could possibly
    # expose a secret key (although unlikely)
    raise OSError(f"failed to delete Ceph credentials for {entity!r}")


@contextmanager
def secretfile(credentials: RepoCredentials) -> Iterator[Path]:
    """Yield a temporary secretfile path for kernel Ceph mount operations.

    The file is created with private mode (0600), stored under Bertrand's runtime
    directory (`RUN_DIR`) which is expected to be tmpfs-backed, contains only the Ceph
    key, and is always deleted when the context exits.

    Parameters
    ----------
    credentials : RepoCredentials
        The repository credentials to write to the secretfile.

    Yields
    ------
    Path
        The path to the temporary secretfile, which is valid only for the duration of
        the context.
    """
    key = credentials.key.strip()
    if not key:
        raise ValueError("repository credentials key cannot be empty")

    # create private tempfile
    fd: int | None = None
    path: Path | None = None
    try:
        fd, name = tempfile.mkstemp(prefix="bertrand-ceph-secret.", dir=RUN_DIR)
        path = Path(name)
        os.fchmod(fd, 0o600)
        os.write(fd, f"{key}\n".encode(encoding="utf-8"))  # noqa: UP012
        os.fsync(fd)
        os.close(fd)
        fd = None
        yield path

    # unconditionally delete the file on context exit
    finally:
        if fd is not None:
            try:
                os.close(fd)
            except OSError:
                pass
        if path is not None:
            path.unlink(missing_ok=True)
