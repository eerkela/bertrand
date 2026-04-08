"""Build and run containerized environments using Bertrand's pinned Kubernetes engine
and container runtime.

This module serves as a bridge between Bertrand's local microK8s/containerd
infrastructure and a git worktree's toolchain metadata.  It is responsible for
bootstrapping the artifacts necessary to build images, expose them to the kubernetes
engine, materialize workloads, and manage the lifecycle of containers and their
metadata.  It also provides a number of endpoints for Bertrand's CLI, which hooks into
the same primitives and provides a user-friendly interface for managing development
environments.
"""
from __future__ import annotations

import asyncio
import base64
import binascii
import json as json_parser
import math
import os
import pathlib
import re
import shutil
import stat
import sys
import time
import uuid
from collections.abc import Mapping, Sequence
from dataclasses import dataclass, field
from datetime import UTC, datetime
from pathlib import Path
from types import TracebackType
from typing import Annotated, Any, Literal, Self

from pydantic import (
    AfterValidator,
    AwareDatetime,
    BaseModel,
    ConfigDict,
    Field,
    NonNegativeInt,
    PositiveInt,
)

from .config import (
    DEFAULT_TAG,
    SHELLS,
    Bertrand,
    Config,
    PyProject,
)
from .config.core import (
    AbsolutePath,
    NonEmpty,
    NoWhiteSpace,
    TOMLKey,
    Trimmed,
)
from .rpc import RPC_TIMEOUT
from .run import (
    BERTRAND_ENV,
    CONTAINER_RUNTIME_ENV,
    CONTAINER_SOCKET,
    ENV_ID_ENV,
    IMAGE_ID_ENV,
    IMAGE_TAG_ENV,
    METADATA_DIR,
    METADATA_FILE,
    METADATA_LOCK,
    NERDCTL_BIN,
    NERDCTL_NAMESPACE,
    NORMALIZE_ARCH,
    PROJECT_ENV,
    STATE_DIR,
    TIMEOUT,
    TOOLS_TMP_DIR,
    WORKTREE_ENV,
    CommandError,
    GitRepository,
    Lock,
    atomic_write_text,
    kubectl,
    nerdctl,
    nerdctl_ids,
)

# pylint: disable=redefined-builtin, redefined-outer-name, broad-except, bare-except


# environment metadata info
REGISTRY_FILE = STATE_DIR / "registry.json"
REGISTRY_LOCK = STATE_DIR / "registry.lock"
REGISTRY_PURGE_BATCH: int = 16
REGISTRY_PURGE_EVERY: int = 64
VERSION: int = 1


# TODO: review these capability tokens, and maybe reduce them to config/core.py
# or config/bertrand.py


def _capability_token(value: str) -> str:
    token = re.sub(r"[^a-z0-9_]+", "_", value.strip().lower())
    token = token.strip("_")
    if not token:
        raise ValueError("capability ID cannot be empty")
    return token


def _capability_secret_name(
    *,
    env_id: str,
    kind: Literal["ssh", "secret"],
    capability_id: str,
) -> str:
    env_token = re.sub(r"[^a-z0-9]+", "", env_id.strip().lower())[:12]
    if not env_token:
        raise ValueError("environment ID cannot be empty")
    capability_token = _capability_token(capability_id).replace("_", "-")
    return f"bertrand-{env_token}-{kind}-{capability_token}"[:253]


async def _cluster_secret_data(
    *,
    name: str,
    timeout: float,
) -> dict[str, str] | None:
    result = await kubectl(
        [
            "get",
            "secret",
            name,
            "-o",
            "json",
        ],
        check=False,
        capture_output=True,
        timeout=timeout,
    )
    if result.returncode != 0:
        return None
    try:
        payload = json_parser.loads(result.stdout)
    except json_parser.JSONDecodeError as err:
        raise OSError(
            f"cluster secret '{name}' returned malformed JSON payload"
        ) from err
    raw = payload.get("data", {})
    if not isinstance(raw, dict):
        raise OSError(f"cluster secret '{name}' is missing a valid data mapping")
    out: dict[str, str] = {}
    for key, value in raw.items():
        if isinstance(key, str) and isinstance(value, str):
            out[key] = value
    return out


def _decode_cluster_secret(
    *,
    name: str,
    data: Mapping[str, str],
    preferred_keys: Sequence[str],
) -> bytes:
    keys = [key for key in preferred_keys if key in data]
    if not keys and len(data) == 1:
        keys = list(data)
    if not keys:
        raise OSError(
            f"cluster secret '{name}' does not contain a recognized data key"
        )
    key = keys[0]
    try:
        return base64.b64decode(data[key], validate=True)
    except (binascii.Error, ValueError) as err:
        raise OSError(
            f"cluster secret '{name}' contains invalid base64 data for key '{key}'"
        ) from err


def _device_env_var(capability_id: str) -> str:
    token = re.sub(r"[^A-Za-z0-9]+", "_", capability_id.strip().upper())
    token = token.strip("_")
    if not token:
        raise ValueError("capability ID cannot be empty")
    return f"BERTRAND_DEVICE_{token}"


async def _build_capability_flags(
    *,
    env_id: str,
    tag: TOMLKey,
    build: Bertrand.Model.Build,
) -> tuple[list[str], Path | None]:
    flags: list[str] = []
    capability_dir: Path | None = None

    def _ensure_capability_dir() -> Path:
        nonlocal capability_dir
        if capability_dir is None:
            capability_dir = (
                TOOLS_TMP_DIR /
                "build-capabilities" /
                f"{_capability_token(env_id)}.{_capability_token(tag)}.{uuid.uuid4().hex}"
            )
            capability_dir.mkdir(parents=True, exist_ok=True)
        return capability_dir

    def _warn_optional(kind: str, capability_id: str) -> None:
        print(
            f"bertrand: optional {kind} capability '{capability_id}' was not found; "
            "continuing without it",
            file=sys.stderr
        )

    # cluster-backed build secrets
    for req in build.secrets:
        secret_name = _capability_secret_name(
            env_id=env_id,
            kind="secret",
            capability_id=req.id,
        )
        secret_data = await _cluster_secret_data(name=secret_name, timeout=TIMEOUT)
        if secret_data is None:
            if req.required:
                raise OSError(
                    f"missing required build secret capability '{req.id}' "
                    f"(cluster secret '{secret_name}' not found in namespace "
                    f"'{NERDCTL_NAMESPACE}')"
                )
            _warn_optional("secret", req.id)
            continue
        payload = _decode_cluster_secret(
            name=secret_name,
            data=secret_data,
            preferred_keys=("value", "secret", req.id),
        )
        target = _ensure_capability_dir() / "secrets" / _capability_token(req.id)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
        target.chmod(0o600)
        flags.extend(["--secret", f"id={req.id},src={target}"])

    # cluster-backed SSH keys
    for req in build.ssh:
        secret_name = _capability_secret_name(
            env_id=env_id,
            kind="ssh",
            capability_id=req.id,
        )
        secret_data = await _cluster_secret_data(name=secret_name, timeout=TIMEOUT)
        if secret_data is None:
            if req.required:
                raise OSError(
                    f"missing required build ssh capability '{req.id}' "
                    f"(cluster secret '{secret_name}' not found in namespace "
                    f"'{NERDCTL_NAMESPACE}')"
                )
            _warn_optional("ssh", req.id)
            continue
        payload = _decode_cluster_secret(
            name=secret_name,
            data=secret_data,
            preferred_keys=("private_key", "id_rsa", "value", req.id),
        )
        target = _ensure_capability_dir() / "ssh" / _capability_token(req.id)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
        target.chmod(0o600)
        flags.extend(["--ssh", f"{req.id}={target}"])

    # node-local build devices
    for req in build.devices:
        env_var = _device_env_var(req.id)
        selector = os.environ.get(env_var, "").strip()
        if not selector:
            if req.required:
                raise OSError(
                    f"missing required build device capability '{req.id}' "
                    f"(set {env_var} to a CDI selector or device path)"
                )
            _warn_optional("device", req.id)
            continue
        flags.extend(["--device", f"{selector}:{req.permissions}"])

    return flags, capability_dir


def _cleanup_capability_dir(path: Path | None) -> None:
    if path is None:
        return
    try:
        shutil.rmtree(path)
    except OSError:
        pass





def _check_uuid(value: str) -> str:
    try:
        return uuid.UUID(value).hex
    except Exception as err:
        raise ValueError(f"'id' must be a valid UUID: {value}") from err


def _to_utc(value: datetime) -> datetime:
    if value.tzinfo is None or value.tzinfo.utcoffset(value) is None:
        raise ValueError(f"'created' must be a valid ISO timestamp: {value}")
    return value.astimezone(UTC)


type UUID4Hex = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_uuid)]
type ImageId = NonEmpty[NoWhiteSpace]
type ContainerId = NonEmpty[NoWhiteSpace]
type ContainerState = Literal[
    "created",
    "restarting",
    "running",
    "removing",
    "paused",
    "exited",
    "dead"
]
type CreatedAt = Annotated[AwareDatetime, AfterValidator(_to_utc)]
type ArgsList = list[NonEmpty[Trimmed]]


# TODO: make sure that the registry always stores absolute paths?


class Registry(BaseModel):
    """Serialized registry of environments that have been built on this system.

    A global registry of this form is stored in Bertrand's host runtime state
    directory, and allows Bertrand to target all environments on the system for
    various CLI commands, as well as detect relocation of environments, possibly
    across different hosts.

    Attributes
    ----------
    host : UUID4Hex
        A UUID tied to the lifetime of the registry.  Every environment's metadata
        will store the host UUID of the registry it was initialized from.  If we
        attempt to insert the environment into another registry with a different
        UUID, then it signifies the environment was sent from another host, and we
        should force a rebuild of all downstream images and containers.
    environments : dict[UUID4Hex, Path]
        A mapping of environment UUIDs to their corresponding root directories on this
        system.  If we attempt to insert an environment with a pre-existing UUID, then
        we can check to see if the previous path is still valid and matches that UUID,
        in which case the environment we are attempting to insert is a copy, and we
        should assign a new UUID to ensure they are treated as separate environments
        with isolated tags.  If the previous path is no longer valid or doesn't match
        the UUID, then the new environment constitutes a move, and we can transfer
        ownership to the new path.
    ops_since_purge : int
        Counts successful `add()` operations since the last incremental purge trigger.
    purge_cursor : UUID4Hex | None
        Cursor used to scan environment IDs in deterministic batches when purging stale
        entries.
    """
    model_config = ConfigDict(extra="forbid")
    host: UUID4Hex
    environments: dict[UUID4Hex, AbsolutePath]
    ops_since_purge: NonNegativeInt
    purge_cursor: UUID4Hex | None = None

    @staticmethod
    def lock(*, timeout: float) -> Lock:
        """Acquire the registry lock to synchronize access to the registry file.

        Parameters
        ----------
        timeout : float
            The maximum time to wait for the lock in seconds.

        Returns
        -------
        Lock
            An async context manager that must be entered to acquire the lock, and
            releases it on exit.
        """
        STATE_DIR.mkdir(parents=True, exist_ok=True)
        return Lock(REGISTRY_LOCK, timeout=timeout, mode="local")

    # NOTE: we have to strictly obey registry > environment locking order to avoid
    # deadlocks, since `add()` and purge both need to acquire environment locks to
    # validate metadata.  If environment operations consistently acquire the registry
    # lock first, then we can guarantee that deadlocks won't occur, since there will
    # never be simultaneous env -> registry and registry -> env edges.  The following
    # methods assume the locks have already been handled by the caller.

    @staticmethod
    async def _check_env(
        root: AbsolutePath,
        env_id: UUID4Hex | None = None
    ) -> Environment.JSON | None:
        try:
            root = root.expanduser().resolve()
            env_file = root / METADATA_FILE
            if env_file.exists() and env_file.is_file():
                metadata = _read_metadata(root)
                if metadata is not None:
                    await Config.load(root)
                    if env_id is None or metadata.id == env_id:
                        return metadata
        except Exception:
            pass
        return None

    @staticmethod
    async def _discover_environment_mounts() -> list[Path]:
        container_ids = await nerdctl_ids("container", {})
        if not container_ids:
            return []

        # inspect all containers and retrieve unique worktree roots
        containers = await Container.inspect(container_ids)
        seen: set[Path] = set()
        result: list[Path] = []
        for container in containers:
            mount = container.worktree
            if mount is None or mount in seen:
                continue
            seen.add(mount)
            result.append(mount)

        return result

    @classmethod
    async def load(cls) -> Self:
        """Load the environment registry from disk, or create a new one if it doesn't
        exist.  If the registry is invalid or corrupted, then a best-effort attempt is
        made to rebuild it by inspecting active Bertrand containers and their metadata.
        Any missing environments will be re-registered the next time they are accessed.

        Returns
        -------
        Registry
            The loaded or newly created environment registry.

        Raises
        ------
        ValueError
            If the registry JSON is invalid, or if any environment metadata is invalid
            or doesn't match the expected format.
        """
        STATE_DIR.mkdir(parents=True, exist_ok=True)

        # touch a new registry if none exists
        changed = False
        if not REGISTRY_FILE.exists():
            self = cls(
                host=uuid.uuid4().hex,
                ops_since_purge=0,
                purge_cursor=None,
                environments={}
            )
            changed = True

        # otherwise, try to parse registry JSON
        else:
            try:
                data = json_parser.loads(REGISTRY_FILE.read_text(encoding="utf-8"))
                if not isinstance(data, dict):
                    raise ValueError("registry JSON must be an object")
                self = cls.model_validate(data)

            # if the registry is corrupted or otherwise invalid, attempt to rebuild it
            # using active mounts as the source of truth (best-effort)
            except Exception:
                self = cls(
                    host=uuid.uuid4().hex,
                    ops_since_purge=0,
                    purge_cursor=None,
                    environments={}
                )
                for root in await cls._discover_environment_mounts():
                    async with Lock(root / METADATA_LOCK, timeout=TIMEOUT, mode="cluster"):
                        env = await cls._check_env(root)
                        if env is None:
                            continue
                    self.environments.setdefault(env.id, root.expanduser().resolve())
                changed = True

        # write registry back to disk if anything changed
        if changed:
            await self.dump()
        return self

    async def dump(self) -> None:
        """Write the registry back to disk.  This should always be called before
        releasing the registry lock, in order to persist any changes made by the
        `load()` and/or `add()` methods.
        """
        STATE_DIR.mkdir(parents=True, exist_ok=True)
        atomic_write_text(
            REGISTRY_FILE,
            json_parser.dumps({
                "host": self.host,
                "ops_since_purge": self.ops_since_purge,
                "purge_cursor": self.purge_cursor,
                "environments": {env_id: str(root) for env_id, root in sorted(
                    self.environments.items(),
                    key=lambda item: item[0]
                )}
            }, separators=(",", ":")) + "\n",
            encoding="utf-8",
            private=True
        )

    def _purge_candidates(self, *, batch_size: int) -> list[UUID4Hex]:
        env_ids = sorted(self.environments)
        if not env_ids:
            self.purge_cursor = None
            return []

        start = 0
        if self.purge_cursor is not None:
            try:
                start = (env_ids.index(self.purge_cursor) + 1) % len(env_ids)
            except ValueError:
                start = 0

        count = min(batch_size, len(env_ids))
        batch = [env_ids[(start + i) % len(env_ids)] for i in range(count)]
        self.purge_cursor = batch[-1]
        return batch

    async def _purge_incremental(self, *, batch_size: int) -> None:
        for env_id in self._purge_candidates(batch_size=batch_size):
            root = self.environments.get(env_id)
            if root is None:
                continue

            # fast path: missing path can be removed without further validation
            if not root.exists():
                self.environments.pop(env_id, None)
                continue

            try:
                async with Lock(root / METADATA_LOCK, timeout=TIMEOUT, mode="cluster"):
                    env = await self._check_env(root, env_id=env_id)
                if env is None:
                    self.environments.pop(env_id, None)
            except TimeoutError:
                continue  # busy worktree; skip and retry in a future purge batch

        if not self.environments:
            self.purge_cursor = None

    async def add(self, worktree: Path) -> Environment.JSON:
        """Claim a new environment worktree in the registry and resolve relocation or
        duplication if the worktree or environment metadata already exists, or if the
        environment was created on a different host.

        Parameters
        ----------
        worktree : Path
            The root of the environment worktree to claim in the registry.  Note that
            this must be locked prior to calling this method.

        Returns
        -------
        Environment.JSON
            The environment metadata read from the worktree, after correcting for
            relocation/duplication.
        """
        worktree = worktree.expanduser().resolve()

        # claim the requested root
        env = await self._check_env(worktree)
        env_changed = False
        if env is None:
            env = Environment.JSON(
                version=VERSION,
                host=self.host,
                id=uuid.uuid4().hex,
                images={},
                retired=[],
            )
            env_changed = True

        # if the environment being added was initialized from a different host
        # registry, then it signifies a cross-platform relocation.  We handle
        # this by clearing the environment's images and containers to force a
        # rebuild on the new host, and then proceed like normal
        if env.host != self.host:
            env.images.clear()
            env.host = self.host
            env_changed = True

        # if the environment is not already registered, insert it directly.
        # Otherwise, if the root has drifted, then it signals a same-host copy
        # or move
        owner = self.environments.setdefault(env.id, worktree)
        if owner != worktree:
            # if the previous root exists and matches the expected UUID, then
            # the new environment constitutes a copy, and we need to clear its
            # commits and re-key it to guarantee uniqueness
            owner_env = await Registry._check_env(owner, env_id=env.id)
            if owner_env is not None:
                env.images.clear()
                env.id = uuid.uuid4().hex
                while env.id in self.environments:
                    env.id = uuid.uuid4().hex
                env_changed = True

            # otherwise, the new environment constitutes a move, and we can
            # transfer ownership by deleting the old containers (but not
            # images), to avoid coupling with the previous path
            while env.images:
                tag, image = env.images.popitem()
                try:
                    ids = await nerdctl_ids(
                        "container",
                        labels={
                            ENV_ID_ENV: env.id,
                            IMAGE_ID_ENV: image.id
                        },
                    )
                    if ids:
                        await Container.remove(ids, force=True, timeout=TIMEOUT)
                        env_changed = True
                except Exception:
                    env.images[tag] = image
                    raise

            # in both cases, we need to update the registry to point to the new root
            self.environments[env.id] = worktree

        # persist new environment metadata if it changed
        if env_changed:
            _write_metadata(worktree, env)

        # trigger an incremental stale-entry purge at a low frequency to amortize cost
        self.ops_since_purge += 1
        if self.ops_since_purge >= REGISTRY_PURGE_EVERY:
            await self._purge_incremental(batch_size=REGISTRY_PURGE_BATCH)
            self.ops_since_purge = 0

        return env

    async def purge(self) -> None:
        """Remove any stale registry entries that point to non-existent or invalid
        environment roots.  `add()` will trigger an incremental purge at a low
        frequency, but this method can be called to trigger a full purge on demand,
        which is necessary before any command that targets all environments on the
        system, disregarding stale ones.
        """
        normalized: dict[UUID4Hex, AbsolutePath] = {}
        for env_id, root in self.environments.items():
            async with Lock(root / METADATA_LOCK, timeout=TIMEOUT, mode="cluster"):
                env = await self._check_env(root, env_id=env_id)
                if env is None:
                    continue
                normalized.setdefault(env.id, root)  # preserve first match if duplicates

        self.environments = normalized
        self.ops_since_purge = 0
        if self.purge_cursor not in self.environments:
            self.purge_cursor = None


def _read_metadata(worktree: Path, *, missing_ok: bool = False) -> Environment.JSON | None:
    env_file = worktree / METADATA_FILE
    if not env_file.exists():
        if missing_ok:
            return None
        raise FileNotFoundError(f"environment metadata file not found: {env_file}")
    if not env_file.is_file():
        raise OSError(f"environment metadata path is not a file: {env_file}")

    try:
        data = json_parser.loads(env_file.read_text(encoding="utf-8"))
    except Exception as err:
        raise OSError(f"failed to parse environment metadata at {env_file}: {err}") from err
    if not isinstance(data, dict):
        raise OSError(f"environment metadata at {env_file} must be a JSON object")

    try:
        return Environment.JSON.model_validate(data)
    except Exception as err:
        raise OSError(f"invalid environment metadata at {env_file}: {err}") from err


def _write_metadata(worktree: Path, metadata: Environment.JSON) -> None:
    atomic_write_text(
        worktree / METADATA_FILE,
        json_parser.dumps(metadata.model_dump(mode="json"), indent=2) + "\n",
        encoding="utf-8",
        private=True
    )


# TODO: update or straight up remove `Container` inspect response, since we're now
# using kubernetes as the control plane 


class Container(BaseModel):
    """Type hint for container inspect output.  Note that due to the ephemeral
    container architecture, this is not persisted to disk, unlike image metadata.

    https://github.com/containerd/nerdctl/blob/main/docs/command-reference.md#whale-nerdctl-inspect
    """
    model_config = ConfigDict(extra="allow")
    Id: ContainerId
    Created: CreatedAt

    class _State(BaseModel, total=False):
        """Type hint for container state information."""
        model_config = ConfigDict(extra="allow")
        Status: ContainerState
        OOMKilled: bool

    State: _State

    class _Config(BaseModel, total=False):
        """Type hint for container configuration details."""
        model_config = ConfigDict(extra="allow")
        Labels: Annotated[dict[str, str], Field(default_factory=dict)]

    Config: _Config
    Image: ImageId
    Path: Annotated[Trimmed | None, Field(default=None)]
    Args: Annotated[list[str], Field(default_factory=list)]

    class _Mounts(BaseModel, total=False):
        """Type hint for container mount information."""
        model_config = ConfigDict(extra="allow")
        Type: Literal["bind", "volume", "tmpfs", "npipe"]
        Source: AbsolutePath
        Destination: AbsolutePath
        RW: bool
        Propagation: Literal["shared", "slave", "private", "rshared", "rslave", "rprivate"]

    Mounts: list[_Mounts]

    @property
    def project_root(self) -> pathlib.Path | None:
        """Extract the host project root path for this container from Bertrand labels.

        Returns
        -------
        Path | None
            The resolved host project root path for this container, or None if required
            labels are missing or invalid.
        """
        labels = self.Config.Labels
        value = labels.get(PROJECT_ENV)
        if not value:
            return None
        try:
            path = Path(value).expanduser()
            if not path.is_absolute():
                return None
            return path.resolve()
        except OSError:
            return None

    @property
    def worktree(self) -> pathlib.Path | None:
        """Extract the host worktree path for this container from Bertrand labels.

        Returns
        -------
        Path | None
            The resolved host worktree path for this container, or None if required
            labels are missing or invalid.
        """
        root = self.project_root
        if root is None:
            return None
        labels = self.Config.Labels
        value = labels.get(WORKTREE_ENV)
        if not value:
            return None
        value = value.strip()
        if not value:
            return None
        if value == ".":
            return root
        path = Path(value)
        if path.is_absolute() or any(part in (".", "..") for part in path.parts):
            return None
        try:
            candidate = (root / path).resolve()
            if not candidate.is_relative_to(root):
                return None
            return candidate
        except OSError:
            return None

    @property
    def runtime(self) -> pathlib.Path | None:
        """Extract the host runtime directory for this container from Bertrand labels.

        Returns
        -------
        Path | None
            The resolved host runtime directory for this container, or None if required
            labels are missing or invalid.
        """
        worktree = self.worktree
        if worktree is None:
            return None
        labels = self.Config.Labels
        value = labels.get(CONTAINER_RUNTIME_ENV)
        if not value:
            return None
        value = value.strip()
        if not value:
            return None
        path = Path(value)
        if (
            path.is_absolute() or
            not path.parts or
            any(part in (".", "..") for part in path.parts)
        ):
            return None
        try:
            candidate = (worktree / path).resolve()
            if not candidate.is_relative_to(worktree):
                return None
            return candidate
        except OSError:
            return None

    @classmethod
    async def inspect(cls, ids: list[ContainerId]) -> list[Self]:
        """Inspect one or more containers via the container runtime.

        Parameters
        ----------
        ids : list[ContainerId]
            The unique container runtime IDs.

        Returns
        -------
        list[Self]
            A list of validated JSON responses from the container runtime.  If a
            container could not be found, it will be omitted from the list.
        """
        if not ids:
            return []
        result = await nerdctl(
            ["container", "inspect", *ids],
            check=False,
            capture_output=True
        )
        if result.returncode != 0:
            return []
        stdout = result.stdout.strip()
        if not stdout:
            return []
        data = json_parser.loads(stdout)
        if not isinstance(data, list):
            return []
        return [cls.model_validate(item) for item in data]

    @staticmethod
    async def remove(ids: list[ContainerId], *, force: bool, timeout: float) -> None:
        """Remove this container via the container runtime.

        Parameters
        ----------
        ids : list[ContainerId]
            The unique container runtime IDs to remove.
        force : bool
            If True, forcefully remove the container even if it is currently running
            or paused.  If False, only remove the container if it is currently stopped.
        timeout : int
            The maximum time in seconds to wait for a running container to stop before
            forcefully killing it.  -1 indicates an infinite wait.

        Raises
        ------
        CommandError
            If the container runtime command fails.
        """
        cmd = [
            "container",
            "rm",
            "--depend",  # remove dependent containers
            "-v",  # remove anonymous volumes
            "-i",  # ignore missing containers
            "-t", str(int(math.ceil(timeout))),
        ]
        if force:
            cmd.append("-f")
        cmd.extend(ids)
        await nerdctl(cmd)

    async def start(
        self,
        *,
        quiet: bool,
        timeout: float | None,
        attach: bool,
        interactive: bool,
    ) -> None:
        """Start this container.

        Parameters
        ----------
        quiet : bool
            If True, suppress output from the start operation.
        timeout : float | None
            Optional timeout in seconds for the start operation.
        attach : bool
            If True, attach stdout/stderr to the console.
        interactive : bool
            If True, attach stdin for interactive input.
        """
        cmd = ["container", "start"]
        if attach:
            cmd.append("-a")
        if interactive:
            cmd.append("-i")
            cmd.append("--detach-keys=")
        cmd.append(self.Id)
        await nerdctl(
            cmd,
            timeout=timeout,
            capture_output=quiet,
        )


class Image(BaseModel):
    """Persistent metadata representing a local Bertrand image, which acts as a
    compiled snapshot of an environment worktree.  An environment can have many images,
    each built with a different set of image and container arguments, and each is
    considered to be immutable once created.

    Specific care is taken not to store anything that references the host filesystem,
    in order to allow renaming/relocation of the environment directory.

    Attributes
    ----------
    version : int
        The version number for backwards compatibility.
    tag : str
        The user-friendly tag for this image, which is unique within the enclosing
        environment.
    id : str
        The unique image ID.
    created : datetime
        The ISO timestamp when the image was created.
    image_args : list[str]
        The original `nerdctl build` args used to build the image.
    """
    class Inspect(BaseModel):
        """Type hint for `nerdctl image inspect` output.

        https://github.com/containerd/nerdctl/blob/main/docs/command-reference.md#whale-nerdctl-image-inspect
        """
        model_config = ConfigDict(extra="allow")
        Id: ImageId
        Created: CreatedAt

    model_config = ConfigDict(extra="forbid")
    version: PositiveInt
    tag: TOMLKey
    id: ImageId
    created: CreatedAt
    image_args: ArgsList

    async def inspect(self) -> Image.Inspect | None:
        """Inspect this image via the container runtime.

        Returns
        -------
        Image.Inspect | None
            A JSON response from nerdctl or None if the image could not be found.
        """
        result = await nerdctl(
            ["image", "inspect", self.id],
            check=False,
            capture_output=True
        )
        if result.returncode != 0 or not result.stdout:
            return None
        data = json_parser.loads(result.stdout)
        return Image.Inspect.model_validate(data[0]) if data else None

    async def remove(self, *, force: bool, timeout: float) -> bool:
        """Remove this image from the container runtime.  Will also remove all
        containers built from this image.

        Parameters
        ----------
        force : bool
            If True, forcefully remove the image even if it has running containers.
            If False, only remove stopped containers, and then remove the image if no
            containers remain.
        timeout : float
            The maximum time in seconds to wait for running containers to stop before
            forcefully killing them.  0 indicates an infinite wait.

        Returns
        -------
        bool
            True if `force=True` or the image had no running containers, and the image
            was successfully removed.  False otherwise.

        Raises
        ------
        OSError
            If `force` is False and there are still containers referencing this image.
        CommandError
            If any of the nerdctl commands fail.
        """
        deadline = time.monotonic() + timeout

        # identify descendant containers to remove, and filter running containers
        ids = await nerdctl_ids(
            "container",
            labels={IMAGE_ID_ENV: self.id},
            timeout=timeout
        )
        retire = False
        if not force:
            running = set(await nerdctl_ids(
                "container",
                labels={IMAGE_ID_ENV: self.id},
                status=("paused", "restarting", "running"),
                timeout=deadline - time.monotonic()
            ))
            if running:
                ids = [id for id in ids if id not in running]
                retire = True

        # remove stopped containers
        if ids:
            await Container.remove(ids, force=force, timeout=deadline - time.monotonic())
        if retire:
            return False  # retire image instead of removing it

        # no containers remain; safe to remove image
        cmd = ["image", "rm", "-i"]
        if force:
            cmd.append("-f")
        cmd.append(self.id)
        await nerdctl(cmd, timeout=deadline - time.monotonic())
        return True

    async def create(
        self,
        env: Environment,
        cmd: Sequence[str],
        *,
        env_vars: Mapping[str, str] | None = None,
        quiet: bool
    ) -> Container:
        """Create a container from this image in `created` state.

        Parameters
        ----------
        env : Environment
            The parent environment this image belongs to, which describes the worktree
            directory that will be mounted into the container.
        cmd : Sequence[str]
            An optional command to override the container's default entry point.  If
            None, then the container will use the configured tag entry point.
        quiet : bool
            If True, suppress output from container commands.
        env_vars : Mapping[str, str] | None, optional
            Optional additional environment variables to inject into the container
            process at create time.

        Returns
        -------
        Container
            The created container metadata, validated from an immediate inspect call.

        Raises
        ------
        OSError
            If the container fails to create, cannot be identified via cidfile, or
            is not in `created` state after creation.
        TypeError
            If the image configuration for the commit is invalid.
        """
        if cmd is not None:
            if not isinstance(cmd, list):
                raise TypeError("cmd must be a list of strings when provided")
            if not all(isinstance(part, str) for part in cmd):
                raise TypeError("cmd override must be a list of strings")

        # get arguments from environment configuration
        bundle = await env.config.container_args(
            env_id=env.id,
            tag=self.tag,
            image_id=self.id,
            cmd=cmd,
            env_vars=env_vars,
        )
        await nerdctl(
            ["container", "create", *bundle.argv],
            capture_output=True if quiet else None
        )

        # read created container ID and inspect to confirm creation + expected state
        container_id: str | None = None
        cid_file = env.config.root / bundle.cid_file
        try:
            if not cid_file.exists() or not cid_file.is_file():
                raise OSError(f"nerdctl create did not produce a cid file at {cid_file}")
            container_id = cid_file.read_text(encoding="utf-8").strip()
            if not container_id:
                raise OSError(f"nerdctl create produced an empty cid file at {cid_file}")
            inspected = await Container.inspect([container_id])
            if len(inspected) != 1:
                raise OSError(
                    "`nerdctl create` did not resolve to exactly one inspect result "
                    f"for container '{container_id}'"
                )
            container = inspected[0]
            if container.Id != container_id:
                raise OSError(
                    f"container inspect ID mismatch after create: cidfile={container_id}, "
                    f"inspect={container.Id}"
                )
            if container.State.Status != "created":
                raise OSError(
                    f"container '{container_id}' is not in created state after create "
                    f"command (status={container.State.Status!r})"
                )
            return container
        except Exception:
            if container_id:
                await nerdctl(
                    [
                        "container",
                        "rm",
                        "-f",
                        "-i",
                        "-v",
                        "--depend",
                        container_id,
                    ],
                    check=False,
                    capture_output=True,
                )
            raise


@dataclass
class Environment:
    """A context manager that orchestrates interactions with the kubernetes runtime in
    order to build and schedule containerized workloads from a git worktree.

    This class is meant to be used as a context manager, which will automatically
    acquire and release a lock on the environment directory in order to prevent
    concurrent modifications.  The environment metadata will be loaded upon entering
    the outermost context, and written back to disk upon exiting it, in order to
    synchronize any changes made during the context's lifetime.

    Attributes
    ----------
    config : Config
        The configuration object for this environment, which is responsible for loading
        and resolving toolchain metadata from its worktree.
    lock : Lock
        The environment lock to synchronize access to the environment's metadata.  The
        lock will be held as long as an `Environment` context is active.
    """
    class JSON(BaseModel):
        """Pydantic model representing JSON metadata to store in the environment."""
        model_config = ConfigDict(extra="forbid")
        version: Annotated[PositiveInt, Field(
            description=
                "The schema version for backwards compatibility.  This should be "
                "incremented whenever a breaking change is made to the metadata "
                "format.",
        )]
        host: Annotated[UUID4Hex, Field(
            description=
                "The unique ID of the host registry this environment belongs to, which "
                "is used to detect cross-host relocations.",
        )]
        id: Annotated[UUID4Hex, Field(
            description=
                "The unique environment ID, which is used for registry lookup and "
                "identification purposes.  All kubernetes resources created within "
                "this environment will be labeled with this ID to support "
                "environment-wide lookups.",
        )]
        images: Annotated[dict[TOMLKey, Image], Field(
            default_factory=dict,
            description=
                "A mapping of image tags to their corresponding metadata.  Each tag "
                "must be present in the environment's configured's `images` section.  "
                "The values are populated when the image is built, and persisted to "
                "disk as part of the environment metadata.",
        )]

        class RetiredImage(BaseModel):
            """An entry in the retired images list, which allows garbage collection
            for outdated images that may still have running containers.
            """
            model_config = ConfigDict(extra="forbid")
            force: Annotated[bool, Field(
                description=
                    "Whether to forcefully remove the image upon consumption, even if "
                    "it has running containers.  This is used for explicit deletion, "
                    "which is handled by the same system, but is distinct from the "
                    "normal retirement process for incremental image builds.",
            )]
            image: Annotated[Image, Field(
                description=
                    "The metadata for the image to remove.  This starts out in the "
                    "environment's `images` map, and moves to its `retired` list when "
                    "the image is invalidated or removed.",
            )]

        retired: Annotated[list[RetiredImage], Field(
            default_factory=list,
            description=
                "A list of retired images that are pending removal.  Images are added "
                "to this list when they are invalidated by a new image build, or when "
                "they are explicitly deleted by the user.  The environment will "
                "attempt to gracefully remove these images before writing the metadata "
                "back to disk.",
        )]

    config: Config
    lock: Lock = field(repr=False)
    _json: JSON = field(
        default_factory=lambda: Environment.JSON.model_construct(
            version=0,
            host="",
            id="",
            images={},
            retired=[]
        ),
        repr=False
    )

    @classmethod
    async def load(
        cls,
        worktree: Path,
        *,
        repo: GitRepository | None = None,
        timeout: float = TIMEOUT,
    ) -> Self:
        """Load an environment at the given worktree path.

        Parameters
        ----------
        worktree : Path
            The root path of the environment worktree to load.
        repo : GitRepository | None, optional
            An optional GitRepository instance to use for resolving the worktree path.
            If omitted, then the repository will be automatically discovered by asking
            git for the common directory at the worktree path.  The worktree path must
            be a child of the repository's base directory, and must be a valid git
            worktree associated with it.
        timeout : float, optional
            The maximum time in seconds to wait while acquiring the environment lock.
            Defaults to `TIMEOUT`, which equates to 30 seconds.

        Returns
        -------
        Environment
            The loaded environment instance, with initial metadata read from the
            worktree configuration.  Note that the environment lock is not acquired,
            and the full configuration is not loaded until the context manager is
            entered by the caller.
        """
        return cls(
            config=await Config.load(worktree, repo=repo, timeout=timeout),
            lock=Lock(worktree / METADATA_LOCK, timeout=timeout, mode="cluster"),
        )

    async def __aenter__(self) -> Self:
        """Acquire the environment lock for exclusive access, register the environment
        to account for relocation, and finish loading the worktree's configuration.
        """
        nested = bool(self.config)
        acquired = False
        try:
            # always obey registry > environment lock order
            async with Registry.lock(timeout=self.lock.timeout):
                await self.lock.lock()
                acquired = True
                if nested:  # re-entrant case
                    return self

                # add to the environment registry while lock is held
                registry = await Registry.load()
                self._json = await registry.add(self.config.root)
                await registry.dump()

            # release registry lock before loading environment config to minimize contention
            await self.config.__aenter__()
            return self

        except Exception as err:
            if acquired:
                await self.lock.unlock(suppress_backend_errors=True)
            raise

    async def __aexit__(
        self,
        exc_type: type[BaseException] | None,
        exc_value: BaseException | None,
        traceback: TracebackType | None
    ) -> None:
        """Release the environment lock, retire outdated images, and write the new
        metadata back to disk if anything changed.
        """
        if not self.config:
            raise RuntimeError("environment context manager was not entered")

        try:
            # attempt to empty the retired images list
            retired: list[Environment.JSON.RetiredImage] = []
            keep: set[ImageId] = set(image.id for image in self._json.images.values())
            pending: dict[ImageId, bool] = {}
            for ret in self._json.retired:
                pending[ret.image.id] = pending.get(ret.image.id, ret.force) or ret.force
            for ret in self._json.retired:
                if ret.image.id in keep:
                    continue  # resurrect active images
                force = pending.pop(ret.image.id, None)
                if force is None:
                    continue  # already processed
                ret.force = force
                try:
                    if not await ret.image.remove(force=force, timeout=self.lock.timeout):
                        retired.append(ret)  # propagate to next generation
                except Exception:
                    retired.append(ret)
            self._json.retired = retired

        # always release the lock and local context depth
        finally:
            await self.config.__aexit__(exc_type, exc_value, traceback)

            # write metadata back to disk
            if not self.config and (self.config.root / METADATA_DIR).exists():
                _write_metadata(self.config.root, self._json)

            await self.lock.unlock(suppress_backend_errors=exc_value is not None)

    def __bool__(self) -> bool:
        return bool(self.config)

    def __hash__(self) -> int:
        return hash(self.config)

    def __eq__(self, other: Any) -> bool:
        if isinstance(other, Environment):
            return self.config == other.config
        return NotImplemented

    @property
    def version(self) -> int:
        """
        Returns
        -------
        int
            The version number for this environment's metadata format.  This is used for
            backwards compatibility.
        """
        return self._json.version

    @property
    def id(self) -> str:
        """
        Returns
        -------
        str
            A UUID for this environment.  This is used to label all images and
            containers associated with this environment, to allow for easy lookup.
        """
        return self._json.id

    @property
    def images(self) -> dict[TOMLKey, Image]:
        """
        Returns
        -------
        dict[str, Image]
            A dictionary mapping sanitized image names to their corresponding image objects.
            across all commits in this environment, which may be useful for performing
            environment-level operations across all images.
        """
        return self._json.images

    @property
    async def containers(self) -> list[Container]:
        """
        Returns
        -------
        list[Container]
            The current list of active containers associated with this environment,
            which may be useful for confirming liveness or performing environment-level
            operations targeting these containers.  "Active" in this context means any
            container that was just created and has not yet been started, or is
            currently paused, restarting, or running normally.
        """
        ids = await nerdctl_ids(
            "container",
            {ENV_ID_ENV: self.id},
            status=["created", "paused", "restarting", "running"]
        )
        return await Container.inspect(ids)

    async def build(self, tag: TOMLKey, *, quiet: bool) -> Image:
        """Incrementally build an image from this environment, updating it and
        gracefully retiring any outdated alternatives.

        Parameters
        ----------
        tag : str
            The image tag to build, which must be defined in the environment's build
            matrix.
        quiet : bool
            Whether to suppress output for the image build.  This is generally meant
            to enable CI workflows to make parsing stdout easier to automate.

        Returns
        -------
        Image
            The updated image metadata for the requested tag, which may be the same
            as a previous build if the worktree state hasn't drifted since the last
            build.

        Raises
        ------
        OSError
            If this method is called outside an active context, or the image build
            fails.
        TypeError
            If the project is incorrectly configured.
        ValueError
            If the tag is not recognized.
        """
        if not self.config:
            raise OSError(
                "environment must be acquired as a context manager before accessing "
                "configuration"
            )
        bertrand = self.config.get(Bertrand)
        if bertrand is None:
            raise OSError(
                f"missing 'bertrand' configuration for environment at {self.config.root}"
            )
        build = bertrand.build.get(tag)
        if build is None:
            raise ValueError(
                f"unknown build tag '{tag}' for environment at {self.config.root}"
            )

        # get arguments from configured build matrix
        bundle = await self.config.image_args(env_id=self.id, tag=tag)

        # build candidate image
        candidate = Image.model_construct(
            version=VERSION,
            tag=tag,
            id="",  # corrected from iid file after build
            created=datetime.now(UTC),
            image_args=bundle.argv,
        )
        capability_flags: list[str]
        capability_dir: Path | None
        capability_flags, capability_dir = await _build_capability_flags(
            env_id=self.id,
            tag=tag,
            build=build,
        )
        try:
            await nerdctl(
                [
                    "build",
                    *bundle.argv[:-1],
                    *capability_flags,
                    bundle.argv[-1],
                ],
                cwd=self.config.root,
                capture_output=quiet
            )
            candidate.id = bundle.iid_file.read_text(encoding="utf-8").strip()
            existing = self.images.get(tag)
            changed = existing is None or existing.id != candidate.id
            try:
                if await candidate.inspect() is None:
                    raise OSError(
                        f"failed to build image '{tag}' for environment at {self.config.root}"
                    )
            except Exception:
                if changed:
                    await nerdctl(
                        ["image", "rm", "-f", candidate.id],
                        check=False,
                        capture_output=quiet
                    )
                raise
        finally:
            _cleanup_capability_dir(capability_dir)

        # retire existing image in favor of new candidate if they differ
        if changed:
            self.images[tag] = candidate
            if existing is not None:  # retire the previous image
                self._json.retired.append(Environment.JSON.RetiredImage(
                    force=False,
                    image=existing
                ))
            return candidate
        assert existing is not None
        return existing


def _normalize_version(value: str) -> str:
    out = value.strip()
    if not out:
        raise ValueError("version cannot be empty")
    if out.startswith("v") and len(out) > 1:
        out = out[1:]
    if not out:
        raise ValueError("version cannot be empty")
    return out


def _normalize_arch(value: str) -> str:
    arch = value.strip().lower()
    if not arch:
        raise ValueError("architecture cannot be empty")
    return NORMALIZE_ARCH.get(
        arch,
        re.sub(r"[^a-z0-9._-]+", "-", arch).strip("-")
    )


def _parse_manifest_arches(value: str | None) -> list[str]:
    if value is None:
        raise ValueError("--manifest requires --manifest-arches")
    raw = value.strip()
    if not raw:
        raise ValueError("--manifest-arches cannot be empty")
    out: list[str] = []
    seen: set[str] = set()
    for token in raw.split(","):
        arch = _normalize_arch(token)
        if not arch:
            raise ValueError(f"invalid architecture in --manifest-arches: {token!r}")
        if arch in seen:
            continue
        seen.add(arch)
        out.append(arch)
    if not out:
        raise ValueError("--manifest-arches must include at least one architecture")
    return out


async def _cli_containers(
    env: Environment,
    tag: TOMLKey | None,
    *,
    status: tuple[ContainerState, ...] = ("created", "paused", "restarting", "running"),
    timeout: float,
) -> list[ContainerId]:
    if tag is None:
        labels = {ENV_ID_ENV: env.id}
    elif tag not in env.images:
        raise KeyError(f"no image found for tag: '{tag}'")
    else:
        labels = {ENV_ID_ENV: env.id, IMAGE_TAG_ENV: tag}
    return await nerdctl_ids(
        "container",
        labels=labels,
        status=status,
        timeout=timeout
    )


async def _cli_images(
    env: Environment,
    tag: TOMLKey | None,
    *,
    timeout: float,
) -> list[ImageId]:
    if tag is None:
        labels = {ENV_ID_ENV: env.id}
    else:
        labels = {ENV_ID_ENV: env.id, IMAGE_TAG_ENV: tag}
    return await nerdctl_ids("image", labels=labels, timeout=timeout)


async def _start_rpc_sidecar(
    *,
    container: Container,
    container_bin: Path,
    deadline: float,
    strict: bool,
    warn_context: str | None = None,
) -> asyncio.subprocess.Process | None:
    runtime = container.runtime
    if runtime is None:
        err = OSError(
            "created container is missing runtime label metadata needed for RPC "
            f"sidecar flow: {container.Id}"
        )
        if strict:
            raise err
        text = warn_context or "bertrand: failed to start RPC sidecar"
        print(f"{text}\n{err}", file=sys.stderr)
        return None
    host_socket = runtime / CONTAINER_SOCKET.name

    sidecar: asyncio.subprocess.Process | None = None
    try:
        sidecar = await asyncio.create_subprocess_exec(
            "bertrand-rpc",
            "--socket", str(host_socket),
            "--container-id", container.Id,
            "--container-bin", str(container_bin),
            stdin=asyncio.subprocess.DEVNULL,
            stdout=asyncio.subprocess.DEVNULL,
            stderr=asyncio.subprocess.DEVNULL,
        )
        while True:
            socket_ready = (
                host_socket.exists() and
                stat.S_ISSOCK(host_socket.lstat().st_mode)
            )
            if socket_ready:
                return sidecar
            if sidecar.returncode is not None:
                raise OSError(f"bertrand-rpc exited early with code {sidecar.returncode}")
            if time.monotonic() >= deadline:
                raise TimeoutError(
                    "timed out waiting for bertrand-rpc sidecar readiness "
                    f"(socket={host_socket})"
                )
            await asyncio.sleep(0.1)
    except Exception as err:
        await _stop_rpc_sidecar(sidecar)
        if strict:
            raise
        text = warn_context or "bertrand: failed to start RPC sidecar"
        print(f"{text}\n{err}", file=sys.stderr)
        return None


async def _stop_rpc_sidecar(sidecar: asyncio.subprocess.Process | None) -> None:
    if sidecar is None or sidecar.returncode is not None:
        return
    sidecar.terminate()
    try:
        await asyncio.wait_for(sidecar.wait(), timeout=RPC_TIMEOUT)
    except Exception:
        sidecar.kill()
        try:
            await sidecar.wait()
        except Exception:
            pass


def _recover_spec(worktree: Path, workload: str | None, tag: str | None) -> str:
    spec = str(worktree)
    if workload:
        spec += f"@{workload}"
    if tag:
        spec += f":{tag}"
    return spec


def _parse_output_format(value: str, *, allow_id: bool) -> tuple[str, str | None]:
    raw = value.strip()
    if not raw:
        raise ValueError("format must not be empty")

    mode, _, tail = raw.partition(" ")
    mode = mode.strip().lower()
    template = tail.strip() or None
    if mode == "table":
        return mode, template
    if template is not None:
        raise ValueError(
            "only table format accepts a template (expected: 'table' or "
            "'table <template>')"
        )
    if mode == "json":
        return mode, None
    if mode == "id":
        if not allow_id:
            raise ValueError("format 'id' is only supported for the 'ls' command")
        return mode, None

    if allow_id:
        expected = "id, json, table, or table <template>"
    else:
        expected = "json, table, or table <template>"
    raise ValueError(f"invalid format: {raw!r} (expected {expected})")


async def bertrand_build(
    worktree: Path,
    workload: str | None,
    tag: TOMLKey | None,
    *,
    quiet: bool,
) -> None:
    """Incrementally build Bertrand images within an environment.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.  If None, then the command
        will target tags in the environment's build matrix.  Otherwise, it will start
        the workload and target tags within it.
    tag : str | None
        The member to target, if any.  All images matching the tag will be included in
        the output, according to the workload.
    quiet : bool
        Whether to suppress build output from the container runtime.  If true, then
        nothing will be printed to stdout or stderr unless an error occurs.

    Notes
    -----
    This command does not materialize or start any containers; it only builds images,
    which corresponds to Ahead of Time (AoT) compilation of the container environment.
    The precise build arguments are resolved from the project's configuration files
    for the selected worktree, using tags defined in its build matrix.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")
    if tag is None:
        tag = DEFAULT_TAG

    async with await Environment.load(worktree, timeout=TIMEOUT) as env:
        await env.build(tag, quiet=quiet)


async def bertrand_publish(
    worktree: Path,
    *,
    repo: str,
    version: str | None,
    manifest: bool,
    manifest_arches: str | None,
) -> str | None:
    """Build and publish Bertrand images for all declared tags in an environment.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    repo : str
        Remote OCI repository where tags/manifests should be published.
    version : str | None
        Optional release version to enforce.  Accepts `X.Y.Z` or `vX.Y.Z`.
    manifest : bool
        If True, assemble and push multi-arch manifests only.
    manifest_arches : str | None
        Comma-separated architectures for manifest assembly.  Required when
        `manifest=True`.

    Returns
    -------
    str | None
        The normalized host architecture in build mode (`manifest=False`), otherwise
        None.

    Raises
    ------
    OSError
        If the environment is invalid, the project version cannot be determined, the
        configured tags are invalid, or the container runtime encounters an error
        during build or publish.
    ValueError
        If the repository name is empty, the version string is invalid, or the manifest
        architectures are invalid.
    """
    repo = repo.strip().lower()
    if not repo:
        raise ValueError("OCI repository must be non-empty when provided")

    async with await Environment.load(worktree, timeout=TIMEOUT) as env:
        bertrand = env.config.get(Bertrand)
        python = env.config.get(PyProject)
        if python is None:
            raise OSError("could not determine project version for publish")
        if bertrand is None:
            raise OSError("could not determine configured tags for publish")
        if not bertrand.build:
            raise OSError("publish requires at least one configured tag")

        # normalize release version and ensure it matches project version
        project_version = _normalize_version(python.project.version)
        publish_version = project_version
        if version is not None:
            publish_version = _normalize_version(version)
            if publish_version != project_version:
                raise OSError(
                    f"publish version '{version}' does not match project version "
                    f"'{project_version}'"
                )

        # phase 1: build + publish arch-specific refs for the current runner
        if not manifest:
            arch = _normalize_arch((await nerdctl(
                ["info", "--format", "{{.Host.Arch}}"],
                capture_output=True,
            )).stdout)
            if not arch:
                raise OSError(
                    "could not determine host architecture from `nerdctl info` output"
                )

            # build all declared tags first
            built: dict[str, Image] = {}
            for tag in bertrand.build:
                try:
                    built[tag] = await env.build(tag, quiet=False)
                except Exception as err:
                    raise OSError(
                        f"failed to build tag '{tag}' for publish"
                    ) from err

            # push only after full build success
            for tag in bertrand.build:
                suffix = "" if tag == DEFAULT_TAG else f"-{tag}"
                image = built[tag]
                ref = f"{repo}:{publish_version}{suffix}-{arch}"
                await nerdctl(["tag", image.id, ref], capture_output=True)
                await nerdctl(["push", ref], attempts=3, capture_output=True)
            return arch

        # phase 2: assemble and publish multi-arch manifests
        arches = _parse_manifest_arches(manifest_arches)
        for tag in bertrand.build:
            suffix = "" if tag == DEFAULT_TAG else f"-{tag}"
            manifest_ref = f"{repo}:{publish_version}{suffix}"
            source_refs = [f"{manifest_ref}-{arch}" for arch in arches]
            for ref in source_refs:
                await nerdctl(
                    ["manifest", "inspect", f"docker://{ref}"],
                    attempts=3,
                    capture_output=True,
                )
            try:
                await nerdctl(
                    ["manifest", "rm", manifest_ref],
                    check=False,
                    capture_output=True,
                )
                await nerdctl(
                    ["manifest", "create", manifest_ref],
                    capture_output=True
                )
                for ref in source_refs:
                    await nerdctl(
                        ["manifest", "add", manifest_ref, f"docker://{ref}"],
                        attempts=3,
                        capture_output=True
                    )
                await nerdctl(
                    ["manifest", "push", "--all", manifest_ref, f"docker://{manifest_ref}"],
                    attempts=3,
                    capture_output=True
                )
            finally:
                await nerdctl(
                    ["manifest", "rm", manifest_ref],
                    check=False,
                    capture_output=True,
                )
        return None


async def bertrand_start(
    worktree: Path,
    workload: str | None,
    tag: TOMLKey | None,
    *,
    cmd: Sequence[str],
) -> None:
    """Start Bertrand containers within an environment.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.  If None, then the command
        will target tags in the environment's build matrix.  Otherwise, it will start
        the workload and target tags within it.
    tag : str | None
        The member to target, if any.  All containers matching the tag will be included
        in the output, according to the workload.
    cmd : Sequence[str]
        An optional command to override the default entry point for the container.  If
        provided, then this command will be used instead of the default entry point
        defined in the project's build matrix for the selected tag.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")
    if tag is None:
        tag = DEFAULT_TAG

    async with await Environment.load(worktree, timeout=TIMEOUT) as env:
        # build/update image first, then materialize and start container from it
        image = await env.build(tag, quiet=False)
        container = await image.create(env, cmd, quiet=False)
        await container.start(
            quiet=False,
            timeout=env.lock.timeout,
            attach=False,
            interactive=False,
        )


async def bertrand_code(
    worktree: Path,
    workload: str | None,
    tag: TOMLKey | None,
    *,
    editor: str | None,
) -> None:
    """Launch a host-side editor by running a blocking in-container `bertrand code`
    command in an ephemeral container, with a socket-coupled RPC sidecar.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.  If None, then the command
        will target tags in the environment's build matrix.  Otherwise, it will start
        the workload and target tags within it.
    tag : str | None
        The member to target, if any.  All containers matching the tag will be included
        in the output, according to the workload.
    editor : str | None
        Optional editor override alias.  If provided, this is forwarded to the
        in-container `bertrand code` command and validated there.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")
    if tag is None:
        tag = DEFAULT_TAG
    if editor is not None:
        editor = editor.strip()
        if not editor:
            raise ValueError("editor override must not be empty")

    async with await Environment.load(worktree, timeout=TIMEOUT) as env:
        # build/update image first, then create code container with RPC metadata
        image = await env.build(tag, quiet=False)
        cmd = ["bertrand", "code", "--block"]
        if editor is not None:
            cmd.extend(["--editor", editor])
        container = await image.create(
            env,
            cmd,
            quiet=False,
        )
        deadline = time.monotonic() + min(env.lock.timeout, RPC_TIMEOUT)

        # strict sidecar startup for code flow
        sidecar: asyncio.subprocess.Process | None = None
        try:
            sidecar = await _start_rpc_sidecar(
                container=container,
                container_bin=NERDCTL_BIN,
                deadline=deadline,
                strict=True,
            )

            # start code container command and block until it exits
            await container.start(
                quiet=False,
                timeout=deadline - time.monotonic(),
                attach=False,
                interactive=False,
            )
            wait = await nerdctl(
                ["container", "wait", container.Id],
                capture_output=True,
                timeout=env.lock.timeout,
            )
            exit_code = wait.stdout.strip()
            if exit_code and exit_code != "0":
                raise OSError(
                    f"container exited with non-zero status while running "
                    f"'bertrand code': {exit_code}"
                )
        finally:
            await _stop_rpc_sidecar(sidecar)

            # best-effort container cleanup fallback (container should usually be
            # removed automatically via --rm on command exit).
            await nerdctl(
                [
                    "container",
                    "rm",
                    "-f",
                    "-i",
                    "-v",
                    "--depend",
                    container.Id,
                ],
                check=False,
                capture_output=True,
            )


async def bertrand_enter(
    worktree: Path,
    workload: str | None,
    tag: TOMLKey | None,
    *,
    shell: str | None,
) -> None:
    """Replace the current process with an interactive shell inside the specified
    container, starting or rebuilding it as necessary.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.  If None, then the command
        will target tags in the environment's build matrix.  Otherwise, it will start
        the workload and target tags within it.
    tag : str | None
        The member to target, if any.  All containers matching the tag will be included
        in the output, according to the workload.
    shell : str | None
        An optional shell to override the default for the container.  If provided, then
        this shell will be used instead of the default defined in the project's build
        matrix for the selected tag.  Must be a shell recognized by `bertrand init`.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")

    # ensure interactive TTY
    if not sys.stdin.isatty() or not sys.stdout.isatty():
        cmd = ["bertrand", "enter", _recover_spec(worktree, workload, tag)]
        if shell is not None:
            cmd.append(shell)
        raise CommandError(
            returncode=1,
            cmd=cmd,
            output="",
            stderr="'bertrand enter' requires both stdin and stdout to be a TTY."
        )

    async with await Environment.load(worktree, timeout=TIMEOUT) as env:
        bertrand = env.config.get(Bertrand)
        if not bertrand:
            raise OSError(
                f"Bertrand configuration is missing from the worktree config at "
                f"{worktree}.  This should never occur; if you see this message, "
                "try re-running `bertrand init` to regenerate your project "
                "configuration, or report an issue if the problem persists."
            )

        # load shell command from environment config
        shell_cmd = SHELLS.get(bertrand.shell)
        if shell_cmd is None:
            raise ValueError(f"unrecognized shell: {bertrand.shell}")
        if shell is not None:
            shell_cmd = SHELLS.get(shell)
            if shell_cmd is None:
                raise ValueError(f"unrecognized shell override: {shell}")

        # build/update image first, then create a shell container with RPC metadata
        image = await env.build(tag or DEFAULT_TAG, quiet=False)
        container = await image.create(
            env,
            shell_cmd,
            quiet=False,
        )
        deadline = time.monotonic() + min(env.lock.timeout, RPC_TIMEOUT)

        # best-effort sidecar startup for development RPC features
        sidecar = await _start_rpc_sidecar(
            container=container,
            container_bin=NERDCTL_BIN,
            deadline=deadline,
            strict=False,
            warn_context=(
                "bertrand: failed to start RPC sidecar; continuing without access to "
                "host RPC features"
            ),
        )

        try:
            # interactive attach to PID1 shell; detach keys disabled to keep `exit`
            # as the canonical lifetime boundary
            await container.start(
                quiet=False,
                timeout=deadline - time.monotonic(),
                attach=True,
                interactive=True,
            )
        finally:
            await _stop_rpc_sidecar(sidecar)

            # best-effort container cleanup fallback (container should usually be
            # removed automatically via --rm on shell exit).
            await nerdctl(
                [
                    "container",
                    "rm",
                    "-f",
                    "-i",
                    "-v",
                    "--depend",
                    container.Id,
                ],
                check=False,
                capture_output=True,
            )


async def bertrand_stop(
    worktree: Path,
    workload: str | None,
    tag: TOMLKey | None,
    *,
    deadline: float,
) -> None:
    """Pause running Bertrand containers within an environment.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.  If None, then the command
        will target tags in the environment's build matrix.  Otherwise, it will start
        the workload and target tags within it.
    tag : str | None
        The member to target, if any.  All containers matching the tag will be included
        in the output, according to the workload.
    deadline : float
        The timestamp before which this command should complete, relative to the
        epoch.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")

    async with await Environment.load(worktree, timeout=deadline - time.time()) as env:
        ids = await _cli_containers(
            env,
            tag,
            status=("running", "restarting", "paused"),
            timeout=deadline - time.time()
        )
        if ids:
            timeout = deadline - time.time()
            await nerdctl(
                [
                    "container",
                    "stop",
                    "-t", str(int(math.ceil(timeout))),
                    *ids
                ],
                timeout=timeout
            )


async def bertrand_pause(
    worktree: Path,
    workload: str | None,
    tag: TOMLKey | None,
    *,
    deadline: float
) -> None:
    """Pause running Bertrand containers within an environment.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.  If None, then the command
        will target tags in the environment's build matrix.  Otherwise, it will start
        the workload and target tags within it.
    tag : str | None
        The member to target, if any.  All containers matching the tag will be included
        in the output, according to the workload.
    deadline : float
        The timestamp before which this command should complete, relative to the
        epoch.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")

    async with await Environment.load(worktree, timeout=deadline - time.time()) as env:
        ids = await _cli_containers(
            env,
            tag,
            status=("running", "restarting"),
            timeout=deadline - time.time()
        )
        if ids:
            await nerdctl(
                ["container", "pause", *ids],
                timeout=deadline - time.time()
            )


async def bertrand_resume(
    worktree: Path,
    workload: str | None,
    tag: TOMLKey | None,
    *,
    deadline: float
) -> None:
    """Resume paused Bertrand containers within an environment.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.  If None (the default), then
        the command will target tags in the environment's build matrix.  Otherwise, it
        will start the workload and target tags within it.
    tag : str | None
        The member to target, if any.  All containers matching the tag will be included
        in the output, according to the workload.
    deadline : float
        The timestamp before which this command should complete, relative to the
        epoch.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")

    async with await Environment.load(worktree, timeout=deadline - time.time()) as env:
        ids = await _cli_containers(
            env,
            tag,
            status=("paused",),
            timeout=deadline - time.time()
        )
        if ids:
            await nerdctl(
                ["container", "unpause", *ids],
                timeout=deadline - time.time()
            )


async def bertrand_restart(
    worktree: Path,
    workload: str | None,
    tag: TOMLKey | None,
) -> None:
    """Restart running or paused Bertrand containers within an environment.  If an
    image or container is out of date, then it will be rebuilt before restarting.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.  If None (the default), then
        the command will target tags in the environment's build matrix.  Otherwise, it
        will start the workload and target tags within it.
    tag : str | None
        The member to target, if any.  All containers matching the tag will be included
        in the output, according to the workload.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")

    async with await Environment.load(worktree, timeout=TIMEOUT) as env:
        tags: list[str]
        if tag is None:
            tags = list(env.images)
        else:
            tags = [tag]
        for tag in tags:
            # snapshot all running containers matching the specified tag
            containers = await Container.inspect(await _cli_containers(
                env,
                tag,
                status=("running", "restarting", "paused"),
                timeout=env.lock.timeout
            ))
            if not containers:
                continue  # nothing to restart

            # incrementally rebuild image
            image = await env.build(tag, quiet=False)

            # stop outdated containers and restart those that were not affected
            defer: list[list[str]] = []
            for container in containers:
                try:
                    if container.Image == image.id:  # restart directly
                        await nerdctl(
                            [
                                "container",
                                "restart",
                                "-t", str(int(math.ceil(env.lock.timeout))),
                                container.Id
                            ],
                            timeout=env.lock.timeout
                        )
                    else:  # stop and restart on updated image
                        await nerdctl(
                            [
                                "container",
                                "stop",
                                "-t", str(int(math.ceil(env.lock.timeout))),
                                container.Id
                            ],
                            timeout=env.lock.timeout
                        )
                        if container.Path:
                            defer.append([container.Path, *container.Args])
                        else:
                            print(
                                "bertrand: could not recover container argv during "
                                f"restart of {_recover_spec(worktree, workload, tag)}: "
                                f"{container.Id}",
                                file=sys.stderr
                            )
                except Exception as err:
                    print(
                        f"bertrand: failed to stop container during restart of "
                        f"{_recover_spec(worktree, workload, tag)}: {container.Id}\n"
                        f"{err}",
                        file=sys.stderr
                    )

            # if we have to restart outdated containers, rebuild them from the newest
            # image and start them with the same arguments as before
            for cmd in defer:
                container = await image.create(env, cmd, quiet=False)
                await container.start(
                    quiet=False,
                    timeout=env.lock.timeout,
                    attach=False,
                    interactive=False,
                )


async def bertrand_rm(
    worktree: Path,
    workload: str | None,
    tag: TOMLKey | None,
    *,
    deadline: float,
    force: bool,
) -> None:
    """Delete Bertrand entities on the system, scoping to images and containers within
    an environment if desired.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.  If None (the default), then
        the command will target tags in the environment's build matrix.  Otherwise, it
        will start the workload and target tags within it.
    tag : str | None
        The member to target, if any.  All containers matching the tag will be included
        in the output, according to the workload.
    deadline : float
        The timestamp before which this command should complete, relative to the
        epoch.
    force : bool
        If True, then all containers will be forcefully removed.  Otherwise, only
        stopped containers will be removed, and any running containers will be left
        alone.

    Notes
    -----
    This command only deletes images and containers, and never alters the host
    filesystem.  The environment directory may be safely deleted after successfully
    invoking this command.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")

    # pylint: disable=protected-access
    async with await Environment.load(worktree, timeout=deadline - time.time()) as env:
        if tag is None:
            while env.images:
                tag, image = env.images.popitem()
                env._json.retired.append(Environment.JSON.RetiredImage(
                    force=force,
                    image=image,
                ))
        else:
            image = env.images.pop(tag)
            if image is not None:
                env._json.retired.append(Environment.JSON.RetiredImage(
                    force=force,
                    image=image,
                ))


async def bertrand_ls(
    worktree: Path,
    workload: str | None,
    tag: TOMLKey | None,
    *,
    deadline: float,
    image: bool,
    format: str,
) -> None:
    """Gather status information for all containers in a Bertrand environment, scoping
    to specific images and containers if desired.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.  If None (the default), then
        the command will target tags in the environment's build matrix.  Otherwise, it
        will start the workload and target tags within it.
    tag : str | None
        The member to target, if any.  All containers matching the tag will be included
        in the output, according to the workload.
    deadline : float
        The timestamp before which this command should complete, relative to the
        epoch.
    image : bool
        If True, then the output will include images instead of containers.  Otherwise,
        the output will only include containers.
    format : str
        A string describing the output format.  This can be one of the following:
            -   `id`: print only the container or image IDs, one per line.
            -   `json`: print the full output as JSON, with one object per container or
                image.
            -   `table`: print the output as a human-readable table with default
                columns.
            -   `table <template>`: print the output as a human-readable table with
                columns specified by the Go template string `<template>`.  See the
                nerdctl documentation for the available fields for containers and
                images.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")
    format_mode, table_template = _parse_output_format(
        format,
        allow_id=True
    )

    async with await Environment.load(worktree, timeout=deadline - time.time()) as env:
        if format_mode == "id":
            if image:
                ids = await _cli_images(
                    env,
                    tag,
                    timeout=deadline - time.time()
                )
            else:
                ids = await _cli_containers(
                    env,
                    tag,
                    timeout=deadline - time.time()
                )
            for id in ids:
                print(id)
            return

        if image:
            cmd = [
                "image",
                "ls",
                "-a",
                "--filter", f"label={BERTRAND_ENV}=1",
                "--filter", f"label={ENV_ID_ENV}={env.id}",
                "--filter", f"label={IMAGE_TAG_ENV}={tag}",
            ]
            if format_mode == "json":
                cmd.append("--no-trunc")
                cmd.append("--format=json")
            else:
                template = (
                    table_template or
                    "{{.Names}}\t{{.CreatedAt}}\t{{.Containers}}\t{{.ReadOnly}}\t"
                    "{{.Size}}\t{{.History}}"
                )
                cmd.append(
                    f"--format=table {template}"
                )
        else:
            cmd = [
                "container",
                "ls",
                "-a",
                "--size",
                "--filter", f"label={BERTRAND_ENV}=1",
                "--filter", f"label={ENV_ID_ENV}={env.id}",
                "--filter", f"label={IMAGE_TAG_ENV}={tag}",
            ]
            if format_mode == "json":
                cmd.append("--no-trunc")
                cmd.append("--format=json")
            else:
                template = (
                    table_template or
                    "{{.Names}}\t{{.CreatedAt}}\t{{.State}}\t{{.Command}}\t"
                    "{{.RunningFor}}\t{{.Status}}\t{{.Restarts}}\t{{.Size}}\t"
                    "{{.Mounts}}\t{{.Networks}}\t{{.Ports}}"
                )
                cmd.append(
                    f"--format=table {template}"
                )

        await nerdctl(cmd, timeout=deadline - time.time())


async def bertrand_monitor(
    worktree: Path,
    workload: str | None,
    tag: TOMLKey | None,
    *,
    deadline: float,
    interval: int,
    format: str,
) -> None:
    """Gather resource utilization statistics for a family of containers in a Bertrand
    environment, printing the output to stdout.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.  If None (the default), then
        the command will target tags in the environment's build matrix.  Otherwise, it
        will start the workload and target tags within it.
    tag : str | None
        The member to target, if any.  All containers matching the tag will be included
        in the output, according to the workload.
    deadline : float
        The timestamp before which this command should complete, relative to the
        epoch.  If `interval` is positive, then this will only apply to environment
        acquisition and ID retrieval.
    interval : int
        If non-zero, then the output will be continuously printed to stdout in a
        streaming format every `interval` seconds.  Otherwise, the output will be
        printed once and the command will exit.  `interval` is incompatible with
        `json`.
    format : str
        A string describing the output format.  This can be one of the following:
            -   `json`: print the full output as JSON, with one object per container or
                image.
            -   `table`: print the output as a human-readable table with default
                columns.
            -   `table <template>`: print the output as a human-readable table with
                columns specified by the Go template string `<template>`.  See the
                nerdctl documentation for the available fields for containers and
                images.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")
    if interval < 0:
        raise ValueError("interval must be non-negative")
    format_mode, table_template = _parse_output_format(
        format,
        allow_id=False
    )
    if format_mode == "json" and interval:
        raise ValueError("cannot use 'json' and 'interval' together")

    async with await Environment.load(worktree, timeout=deadline - time.time()) as env:
        ids = await _cli_containers(env, tag, timeout=deadline - time.time())
        if not ids:
            if format_mode == "json":
                print("[]")
            return

        cmd = ["container", "stats"]
        if not interval:
            cmd.append("--no-stream")
        else:
            cmd.append(f"--interval={interval}")

        if format_mode == "json":
            cmd.append("--no-trunc")
            cmd.append("--format=json")
            cmd.extend(ids)
            await nerdctl(cmd, timeout=deadline - time.time())
        else:
            template = (
                table_template or
                "{{.Name}}\t{{.AVGCPU}}\t{{.CPUPerc}}\t{{.PIDs}}\t{{.MemUsage}}\t"
                "{{.NetIO}}\t{{.BlockIO}}"
            )
            cmd.append(
                f"--format=table {template}"
            )
            cmd.extend(ids)
            await nerdctl(cmd, timeout=deadline - time.time())


async def bertrand_top(
    worktree: Path,
    workload: str | None,
    tag: TOMLKey | None,
    *,
    deadline: float
) -> None:
    """Display the running processes for a family of containers in a Bertrand
    environment, printing them to stdout as newline-delimited tables.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.  If None (the default), then
        the command will target tags in the environment's build matrix.  Otherwise, it
        will start the workload and target tags within it.
    tag : str | None
        The member to target, if any.  All containers matching the tag will be included
        in the output, according to the workload.
    deadline : float
        The timestamp before which this command should complete, relative to the
        epoch.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")

    async with await Environment.load(worktree, timeout=deadline - time.time()) as env:
        ids = await _cli_containers(
            env,
            tag,
            timeout=deadline - time.time()
        )
        for id in ids:
            await nerdctl(
                ["container", "top", id],
                timeout=deadline - time.time(),
            )
            print()  # delimiter between containers


async def bertrand_log(
    worktree: Path,
    workload: str | None,
    tag: TOMLKey | None,
    *,
    deadline: float,
    image: bool,
    since: str | None,
    until: str | None,
) -> None:
    """Print the logs for a specific image or container in a Bertrand environment to
    stdout as newline-delimited tables.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    workload : str | None
        The kubernetes workload to target, if applicable.  If None (the default), then
        the command will target tags in the environment's build matrix.  Otherwise, it
        will start the workload and target tags within it.
    tag : str | None
        The member to target, if any.  All containers matching the tag will be included
        in the output, according to the workload.
    deadline : float
        The timestamp before which this command should complete, relative to the
        epoch.
    image : bool
        If True, then the logs will show image history instead of container logs.
    since : str | None
        Only show logs since this timestamp.  Should be a string parsable by nerdctl,
        such as "2024-01-01T00:00:00" or "5m" for 5 minutes ago.  If None, then all
        logs from the beginning of time will be shown.
    until : str | None
        Only show logs until this timestamp.  Should be a string parsable by nerdctl,
        such as "2024-01-01T00:00:00" or "5m" for 5 minutes ago.  If None, then all
        logs up to the current time will be shown.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")

    async with await Environment.load(worktree, timeout=deadline - time.time()) as env:
        if image:
            ids = await _cli_images(env, tag, timeout=deadline - time.time())
            cmd = [
                "image",
                "history",
                "--human",
                (
                    "--format=table {{.CreatedAt}}\t{{.CreatedSince}}\t{{.CreatedBy}}\t"
                    "{{.Size}}\t{{.Comment}}"
                ),
            ]
            if since is not None:
                raise ValueError("cannot use 'since' with image logs")
            if until is not None:
                raise ValueError("cannot use 'until' with image logs")
        else:
            ids = await _cli_containers(
                env,
                tag,
                timeout=deadline - time.time()
            )
            cmd = [
                "container",
                "logs",
                "--color",
                "--follow",
                "--names",
                "--timestamps",
            ]
            if since is not None:
                cmd.append("--since")
                cmd.append(since)
            if until is not None:
                cmd.append("--until")
                cmd.append(until)

        # print results to stdout, delimiting each container with a newline
        for id in ids:
            await nerdctl([*cmd, id], timeout=deadline - time.time())
            print()  # delimiter between containers
