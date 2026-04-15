"""Environment registry and metadata persistence for Bertrand runtime."""
from __future__ import annotations

import json as json_parser
import uuid
from pathlib import Path
from typing import Self

from pydantic import (
    BaseModel,
    ConfigDict,
    Field,
    NonNegativeInt,
    PositiveInt,
)

from ..config import Config
from ..config.core import AbsolutePath, TOMLKey, UUIDHex
from ..run import (
    ENV_ID_ENV,
    IMAGE_ID_ENV,
    METADATA_FILE,
    METADATA_LOCK,
    STATE_DIR,
    TIMEOUT,
    Lock,
    atomic_write_text,
    nerdctl_ids,
)
from .container import Container
from .image import Image

REGISTRY_FILE = STATE_DIR / "registry.json"
REGISTRY_LOCK = STATE_DIR / "registry.lock"
REGISTRY_PURGE_BATCH: int = 16
REGISTRY_PURGE_EVERY: int = 64
VERSION: int = 1


# TODO: restore inline comments, which were deleted after reorganization.
# -> Also, these utilities should probably go in `environment.py`, which is the only
# place they are meant to be used.


# TODO(eerkela): Refactor registry for repo-alias attachments over hidden Ceph mounts.
#
# Motivation:
# Registry currently treats absolute path as canonical environment location. With hidden
# repo mounts + symlink aliases, attachment path and environment identity must be
# decoupled.
#
# Required work:
# 1) Split identity from attachment.
#    - Identity: env_id + host UUID + metadata validity.
#    - Attachment: repo_id -> hidden_mount + alias_path from binding manifest.
# 2) Discovery source.
#    - Prefer binding manifests for attachment discovery.
#    - Use container inspection as supplemental signal, not sole source of truth.
# 3) Same-host relocation.
#    - Alias change with same env metadata is relocation only; update canonical path.
# 4) Same-host copy.
#    - If two independently valid env roots coexist with same metadata, retain copy
#      semantics (mint new env_id for copy) unless explicit shared-mode is introduced.
# 5) Cross-host relocation.
#    - Keep host UUID mismatch behavior: clear host-local runtime/image state and
#      re-claim.
# 6) Recovery behavior.
#    - If alias missing but hidden mount + metadata are valid, recover alias attachment
#      instead of immediately purging registry entry.
# 7) Git/worktree safety.
#    - Validate active attachment root before worktree sync.
#    - Add repair path when git worktree metadata holds stale absolute paths after alias
#      move.
# 8) Destructive safeguards.
#    - Any environment purge/delete path must verify "not mounted" before recursive
#      delete.
# 9) Schema.
#    - Bump registry VERSION and hard-cut rewrite in prototype mode (no migration shim).


class EnvironmentMetadata(BaseModel):
    """Pydantic model representing environment metadata persisted in each worktree.

    Attributes
    ----------
    version : int
        Metadata schema version.
    host : str
        UUID of the host registry that claimed this environment.
    id : str
        Stable environment UUID used for runtime labels and registry indexing.
    images : dict[str, Image]
        Mapping of configured build tags to image metadata.
    retired : list[RetiredImage]
        Images pending garbage collection after rebuild/removal events.
    """
    class RetiredImage(BaseModel):
        """An entry in the retired images list, which allows garbage collection for
        outdated images that may still have running containers.
        """
        model_config = ConfigDict(extra="forbid")
        force: bool
        image: Image

    model_config = ConfigDict(extra="forbid")
    version: PositiveInt
    host: UUIDHex
    id: UUIDHex
    images: dict[TOMLKey, Image] = Field(default_factory=dict)
    retired: list[RetiredImage] = Field(default_factory=list)


def read_metadata(worktree: Path, *, missing_ok: bool = False) -> EnvironmentMetadata | None:
    """Read and validate environment metadata from a worktree root.

    Parameters
    ----------
    worktree : Path
        The root path for the environment metadata file.
    missing_ok : bool, optional
        If True, return None when metadata is missing.

    Returns
    -------
    EnvironmentMetadata | None
        Parsed metadata or None when missing and `missing_ok=True`.
    """
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
        return EnvironmentMetadata.model_validate(data)
    except Exception as err:
        raise OSError(f"invalid environment metadata at {env_file}: {err}") from err


def write_metadata(worktree: Path, metadata: EnvironmentMetadata) -> None:
    """Write validated environment metadata back to disk.

    Parameters
    ----------
    worktree : Path
        The environment root path where metadata is stored.
    metadata : EnvironmentMetadata
        Validated metadata payload to serialize.
    """
    atomic_write_text(
        worktree / METADATA_FILE,
        json_parser.dumps(metadata.model_dump(mode="json"), indent=2) + "\n",
        encoding="utf-8",
        private=True,
    )


class Registry(BaseModel):
    """Serialized registry of environments that have been built on this system.

    A global registry in Bertrand's host state directory allows CLI commands to target
    all local environments and detect same-host relocation versus copy semantics.

    Attributes
    ----------
    host : UUIDHex
        UUID tied to the registry lifetime. Every environment metadata file stores the
        host UUID that claimed it. When that host UUID differs, the environment is
        treated as cross-host relocation and rebuilt.
    environments : dict[UUIDHex, AbsolutePath]
        Mapping of environment UUIDs to canonical worktree roots. This map is used to
        detect same-host copy vs move semantics.
    ops_since_purge : int
        Counts successful `add()` operations since the last incremental purge trigger.
    purge_cursor : UUIDHex | None
        Cursor used to scan environment IDs in deterministic batches during stale
        entry purges.
    """
    model_config = ConfigDict(extra="forbid")
    host: UUIDHex
    environments: dict[UUIDHex, AbsolutePath]
    ops_since_purge: NonNegativeInt
    purge_cursor: UUIDHex | None = None

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

    # NOTE: lock ordering must remain registry -> environment to avoid deadlocks.

    @staticmethod
    async def _check_env(
        root: AbsolutePath,
        env_id: UUIDHex | None = None,
    ) -> EnvironmentMetadata | None:
        try:
            root = root.expanduser().resolve()
            env_file = root / METADATA_FILE
            if env_file.exists() and env_file.is_file():
                metadata = read_metadata(root)
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
        exist. If the registry is invalid or corrupted, then a best-effort attempt is
        made to rebuild it by inspecting active Bertrand containers and their metadata.
        Any missing environments will be re-registered the next time they are
        accessed.

        Returns
        -------
        Registry
            The loaded or newly created environment registry.
        """
        STATE_DIR.mkdir(parents=True, exist_ok=True)
        changed = False

        if not REGISTRY_FILE.exists():
            self = cls(
                host=uuid.uuid4().hex,
                ops_since_purge=0,
                purge_cursor=None,
                environments={},
            )
            changed = True
        else:
            try:
                data = json_parser.loads(REGISTRY_FILE.read_text(encoding="utf-8"))
                if not isinstance(data, dict):
                    raise ValueError("registry JSON must be an object")
                self = cls.model_validate(data)
            except Exception:
                self = cls(
                    host=uuid.uuid4().hex,
                    ops_since_purge=0,
                    purge_cursor=None,
                    environments={},
                )
                for root in await cls._discover_environment_mounts():
                    async with Lock(root / METADATA_LOCK, timeout=TIMEOUT, mode="cluster"):
                        env = await cls._check_env(root)
                        if env is None:
                            continue
                    self.environments.setdefault(env.id, root.expanduser().resolve())
                changed = True

        if changed:
            await self.dump()
        return self

    async def dump(self) -> None:
        """Write the registry back to disk. This should always be called before
        releasing the registry lock, in order to persist any changes made by the
        `load()` and/or `add()` methods.
        """
        STATE_DIR.mkdir(parents=True, exist_ok=True)
        atomic_write_text(
            REGISTRY_FILE,
            json_parser.dumps(
                {
                    "host": self.host,
                    "ops_since_purge": self.ops_since_purge,
                    "purge_cursor": self.purge_cursor,
                    "environments": {env_id: str(root) for env_id, root in sorted(
                        self.environments.items(),
                        key=lambda item: item[0]
                    )},
                },
                separators=(",", ":"),
            ) + "\n",
            encoding="utf-8",
        )

    def _purge_candidates(self, *, batch_size: int) -> list[UUIDHex]:
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

            if not root.exists():
                self.environments.pop(env_id, None)
                continue

            try:
                async with Lock(root / METADATA_LOCK, timeout=TIMEOUT, mode="cluster"):
                    env = await self._check_env(root, env_id=env_id)
                if env is None:
                    self.environments.pop(env_id, None)
            except TimeoutError:
                continue

        if not self.environments:
            self.purge_cursor = None

    async def add(self, worktree: Path) -> EnvironmentMetadata:
        """Claim a new environment worktree in the registry and resolve relocation or
        duplication if the worktree or environment metadata already exists, or if the
        environment was created on a different host.

        Parameters
        ----------
        worktree : Path
            The root of the environment worktree to claim in the registry. Note that
            this must be locked prior to calling this method.

        Returns
        -------
        EnvironmentMetadata
            The environment metadata read from the worktree, after correcting for
            relocation/duplication.
        """
        worktree = worktree.expanduser().resolve()
        env = await self._check_env(worktree)
        env_changed = False
        if env is None:
            env = EnvironmentMetadata(
                version=VERSION,
                host=self.host,
                id=uuid.uuid4().hex,
                images={},
                retired=[],
            )
            env_changed = True

        if env.host != self.host:
            env.images.clear()
            env.host = self.host
            env.id = uuid.uuid4().hex
            while env.id in self.environments:
                env.id = uuid.uuid4().hex
            env_changed = True

        owner = self.environments.setdefault(env.id, worktree)
        if owner != worktree:
            owner_env = await Registry._check_env(owner, env_id=env.id)
            if owner_env is not None:
                env.images.clear()
                env.id = uuid.uuid4().hex
                while env.id in self.environments:
                    env.id = uuid.uuid4().hex
                env_changed = True

            while env.images:
                tag, image = env.images.popitem()
                try:
                    ids = await nerdctl_ids(
                        "container",
                        labels={ENV_ID_ENV: env.id, IMAGE_ID_ENV: image.id},
                    )
                    if ids:
                        await Container.remove(ids, force=True, timeout=TIMEOUT)
                        env_changed = True
                except Exception:
                    env.images[tag] = image
                    raise
            self.environments[env.id] = worktree

        if env_changed:
            write_metadata(worktree, env)

        self.ops_since_purge += 1
        if self.ops_since_purge >= REGISTRY_PURGE_EVERY:
            await self._purge_incremental(batch_size=REGISTRY_PURGE_BATCH)
            self.ops_since_purge = 0

        return env

    async def purge(self) -> None:
        """Remove any stale registry entries that point to non-existent or invalid
        environment roots. `add()` will trigger an incremental purge at a low
        frequency, but this method can be called to trigger a full purge on demand.
        """
        normalized: dict[UUIDHex, AbsolutePath] = {}
        for env_id, root in self.environments.items():
            async with Lock(root / METADATA_LOCK, timeout=TIMEOUT, mode="cluster"):
                env = await self._check_env(root, env_id=env_id)
                if env is None:
                    continue
                normalized.setdefault(env.id, root)

        self.environments = normalized
        self.ops_since_purge = 0
        if self.purge_cursor not in self.environments:
            self.purge_cursor = None
