"""Environment lifecycle orchestration for Bertrand's Kubernetes runtime."""
from __future__ import annotations

from dataclasses import dataclass, field
from datetime import UTC, datetime
from pathlib import Path
from types import TracebackType
from typing import Any, Self

from ..config import Bertrand, Config
from ..config.core import TOMLKey
from ..run import (
    ENV_ID_ENV,
    INFINITY,
    METADATA_DIR,
    METADATA_LOCK,
    Lock,
    nerdctl,
    nerdctl_ids,
)
from .api import Kube
from .container import Container
from .device import ConfigMap
from .image import Image, image_args
from .registry import VERSION, EnvironmentMetadata, Registry, write_metadata
from .secret import Secret


@dataclass
class Environment:
    """A context manager that orchestrates interactions with the Kubernetes runtime
    to build and schedule containerized workloads from a git worktree.

    This class acquires and releases a lock on the environment directory to prevent
    concurrent metadata mutations. Environment metadata is loaded on outermost entry
    and written back to disk on final exit to synchronize runtime changes.

    Attributes
    ----------
    config : Config
        The configuration object for this environment, responsible for loading and
        resolving toolchain metadata from the worktree.
    lock : Lock
        The environment lock used to synchronize metadata access.
    """
    config: Config
    lock: Lock = field(repr=False)
    _json: EnvironmentMetadata = field(
        default_factory=lambda: EnvironmentMetadata.model_construct(
            version=0,
            host="",
            id="",
            images={},
            retired=[],
        ),
        repr=False,
    )

    @classmethod
    async def load(
        cls,
        worktree: Path,
        *,
        repo=None,
        timeout: float = INFINITY,
    ) -> Self:
        """Load an environment at the given worktree path.

        Parameters
        ----------
        worktree : Path
            The root path of the environment worktree to load.
        repo : GitRepository | None, optional
            Optional repository handle for worktree resolution.
        timeout : float, optional
            The maximum time in seconds to wait while acquiring locks.

        Returns
        -------
        Environment
            The loaded environment instance. Note that the environment lock is not
            acquired, and full configuration loading is deferred until entering the
            context manager.
        """
        return cls(
            config=await Config.load(worktree, repo=repo, timeout=timeout),
            lock=Lock(worktree / METADATA_LOCK, timeout=timeout, mode="cluster"),
        )

    async def __aenter__(self) -> Self:
        """Acquire the environment lock for exclusive access, register relocation
        state, and finish loading the worktree configuration.
        """
        nested = bool(self.config)
        acquired = False
        try:
            # always obey registry > environment lock order
            async with Registry.lock(timeout=self.lock.timeout):
                await self.lock.lock()
                acquired = True
                if nested:
                    return self

                registry = await Registry.load()
                self._json = await registry.add(self.config.root)
                await registry.dump()

            await self.config.__aenter__()
            return self
        except Exception:
            if acquired:
                await self.lock.unlock(ignore_errors=True)
            raise

    async def __aexit__(
        self,
        exc_type: type[BaseException] | None,
        exc_value: BaseException | None,
        traceback: TracebackType | None,
    ) -> None:
        """Release the environment lock, retire outdated images, and write metadata
        back to disk if anything changed.
        """
        if not self.config:
            raise RuntimeError("environment context manager was not entered")

        try:
            retired: list[EnvironmentMetadata.RetiredImage] = []
            keep = {image.id for image in self._json.images.values()}
            pending: dict[str, bool] = {}
            for ret in self._json.retired:
                pending[ret.image.id] = pending.get(ret.image.id, ret.force) or ret.force
            for ret in self._json.retired:
                if ret.image.id in keep:
                    continue
                force = pending.pop(ret.image.id, None)
                if force is None:
                    continue
                ret.force = force
                try:
                    if not await ret.image.remove(force=force, timeout=self.lock.timeout):
                        retired.append(ret)
                except Exception:
                    retired.append(ret)
            self._json.retired = retired
        finally:
            await self.config.__aexit__(exc_type, exc_value, traceback)
            if not self.config and (self.config.root / METADATA_DIR).exists():
                write_metadata(self.config.root, self._json)
            await self.lock.unlock(ignore_errors=exc_value is not None)

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
        """Return metadata schema version for backwards compatibility.

        Returns
        -------
        int
            The metadata version for this environment.
        """
        return self._json.version

    @property
    def id(self) -> str:
        """Return the canonical UUID for this environment.

        Returns
        -------
        str
            The environment UUID used for registry lookup and runtime labels.
        """
        return self._json.id

    @property
    def images(self) -> dict[TOMLKey, Image]:
        """Return image metadata map for this environment.

        Returns
        -------
        dict[TOMLKey, Image]
            Mapping of build tags to image metadata persisted in environment state.
        """
        return self._json.images

    @property
    async def containers(self) -> list[Container]:
        """The current list of active containers associated with this environment.

        Active means any container in `created`, `paused`, `restarting`, or `running`
        states.
        """
        ids = await nerdctl_ids(
            "container",
            {ENV_ID_ENV: self.id},
            status=["created", "paused", "restarting", "running"],
        )
        return await Container.inspect(ids)

    async def build(self, tag: TOMLKey, *, quiet: bool) -> Image:
        """Incrementally build an image from this environment, updating it and
        gracefully retiring outdated alternatives.

        Parameters
        ----------
        tag : TOMLKey
            The build tag to compile.
        quiet : bool
            If True, suppress container runtime build output.

        Returns
        -------
        Image
            Updated image metadata for the requested tag. This may be identical to a
            previous build when worktree state has not drifted.

        Raises
        ------
        OSError
            If called outside an active context or if the build fails.
        ValueError
            If the tag is unknown.
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

        bundle = await image_args(self.config, env_id=self.id, tag=tag)
        candidate = Image.model_construct(
            version=VERSION,
            tag=tag,
            id="",
            created=datetime.now(UTC),
            image_args=bundle.argv,
        )
        capability_dir: Path | None = None
        try:
            with await Kube.host(timeout=self.lock.timeout) as kube:
                secret_flags, capability_dir = await Secret.build_flags(
                    kube=kube,
                    env_id=self.id,
                    build=build,
                    timeout=self.lock.timeout,
                )
                device_flags = await ConfigMap.build_flags(
                    kube=kube,
                    env_id=self.id,
                    build=build,
                    timeout=self.lock.timeout,
                )
            await nerdctl(
                [
                    "build",
                    *bundle.argv[:-1],
                    *secret_flags,
                    *device_flags,
                    bundle.argv[-1],
                ],
                cwd=self.config.root,
                capture_output=quiet,
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
                        capture_output=quiet,
                    )
                raise
        finally:
            Secret.cleanup_staged(capability_dir)

        if changed:
            self.images[tag] = candidate
            if existing is not None:
                self._json.retired.append(
                    EnvironmentMetadata.RetiredImage(
                        force=False,
                        image=existing,
                    )
                )
            return candidate
        assert existing is not None
        return existing
