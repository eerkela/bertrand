"""Install and run a rootless OCI container engine (podman) to orchestrate Bertrand's
CLI.
"""
from __future__ import annotations

import json as json_parser
import math
import os
import re
import shutil
import sys
import time
import uuid

from collections.abc import Mapping
from dataclasses import dataclass, asdict, field
from datetime import datetime, timezone
from importlib import resources as importlib_resources
from pathlib import Path
from types import TracebackType
from typing import (
    Annotated,
    Any,
    Callable,
    Literal,
    Sequence,
    Self,
    cast,
)

from pydantic import (
    AfterValidator,
    AwareDatetime,
    BaseModel,
    ConfigDict,
    PositiveInt,
    NonNegativeInt,
)

from .rpc import CONTAINER_SOCKET, Listener
from .config import (
    BERTRAND_ENV,
    CONTAINER_ID_ENV,
    CONTAINERFILE_RESOURCE,
    DEFAULT_TAG,
    ENV_ID_ENV,
    HOST_SOCKET,
    IMAGE_ID_ENV,
    IMAGE_TAG_ENV,
    METADATA_DIR,
    METADATA_FILE,
    METADATA_TMP,
    SHELLS,
    SOCKET_ENV,
    WORKTREE_ENV,
    WORKTREE_MOUNT,
    AbsolutePath,
    Config,
    Trimmed,
    NonEmpty,
    NoWhiteSpace,
    SanitizedName,
    inside_image,
    lock_worktree,
)
from .pipeline import (
    DelegateUserControllers,
    EnsureSubIDs,
    EnsureUserNamespaces,
    InstallPackage,
    JSONValue,
    Pipeline,
    atomic,
    detect_package_manager,
    on_init,
    on_enter,
    on_code,
    on_publish,
)
from .run import (
    CommandError,
    CompletedProcess,
    Lock,
    TimeoutExpired,
    User,
    atomic_write_text,
    confirm,
    mkdir_private,
    run,
    sanitize_name,
)

# pylint: disable=redefined-builtin, redefined-outer-name, broad-except, bare-except


# environment metadata info
CACHES: str = "/tmp/.cache"
DEFAULT_MAX_COMMITS: int = 10
REFERENCE_TRANSACTION_HOOK: str = "hooks/reference-transaction"
REFERENCE_TRANSACTION_MARKER: str = "# bertrand-managed: reference-transaction"
REGISTRY_FILE = on_init.state_dir / "registry.json"
REGISTRY_LOCK = on_init.state_dir / "registry.lock"
REGISTRY_PURGE_BATCH: int = 16
REGISTRY_PURGE_EVERY: int = 64
TIMEOUT: int = 30
VERSION: int = 1


# shared fact names during init/enter pipelines
USER = "user"
UID = "uid"
GID = "gid"
PACKAGE_MANAGER = "package_manager"
DISTRO_ID = "distro_id"
DISTRO_VERSION = "distro_version"
DISTRO_CODENAME = "distro_codename"
CODE_SERVER_AVAILABLE = "code_server_available"


# CI publish workflow requires an architecture matrix
NORMALIZE_ARCH = {
    "x86_64": "amd64",
    "amd64": "amd64",
    "aarch64": "arm64",
    "arm64": "arm64",
}


def _check_uuid(value: str) -> str:
    try:
        return uuid.UUID(value).hex
    except Exception as err:
        raise ValueError(f"'id' must be a valid UUID: {value}") from err


def _to_utc(value: datetime) -> datetime:
    if value.tzinfo is None or value.tzinfo.utcoffset(value) is None:
        raise ValueError(f"'created' must be a valid ISO timestamp: {value}")
    return value.astimezone(timezone.utc)


type UUIDStr = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_uuid)]
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
type Validator = Callable[[JSONValue], Any]


def _init_assume_yes(ctx: Pipeline.InProgress) -> bool:
    raw = ctx.get("yes")
    if raw is None:
        return False
    if isinstance(raw, bool):
        return raw
    raise TypeError(f"invalid 'yes' fact type: {type(raw).__name__}")


async def _podman_ready() -> bool:
    """Return True when podman is installed and usable for the current user."""
    if not shutil.which("podman"):
        return False
    result = await run(
        ["podman", "info", "--format", "json"],
        check=False,
        capture_output=True
    )
    return result.returncode == 0


@atomic
@dataclass(frozen=True)
class PurgeBertrandArtifacts:
    """Clean up Bertrand containers, images, and volumes before uninstalling podman
    itself.
    """
    # pylint: disable=missing-function-docstring, broad-exception-caught, unused-argument

    async def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        return  # no-op; cleanup is handled in undo

    @staticmethod
    async def undo(
        ctx: Pipeline.InProgress,
        payload: dict[str, JSONValue],
        force: bool
    ) -> None:
        if not shutil.which("podman"):
            return

        # do the same as rm -f to remove all environments as well as their internal
        # artifacts
        Rm._all(force=force, timeout=TIMEOUT)  # pylint: disable=protected-access

        # clean up any dangling images/containers/volumes that weren't removed by
        # rm -f, which can occur if the environment registry is incomplete and the
        # environment has no containers, preventing us from discovering its mount point
        try:
            containers = await _podman_ids("container", {})
            if containers:
                await Container.remove(
                    containers,
                    force=True,
                    timeout=TIMEOUT,
                    missing_ok=True
                )
            images = await _podman_ids("image", {})
            if images:
                await podman_cmd(["image", "rm", "-f", "-i", *images], check=False)
            volumes = await _podman_ids("volume", {})
            if volumes:
                await podman_cmd(["volume", "rm", "-f", "-i", *volumes], check=False)
        except:
            pass


@atomic
@dataclass(frozen=True)
class InstallPodman(InstallPackage):
    """Install podman and its dependencies via the detected package manager.  The undo
    step will first check host podman storage for any containers/images/volumes that
    aren't managed by Bertrand.  If host queries fail or any such objects are found,
    uninstallation is skipped to avoid accidentally removing user data.
    """
    # pylint: disable=missing-function-docstring, broad-exception-caught, unused-argument

    @staticmethod
    async def undo(
        ctx: Pipeline.InProgress,
        payload: dict[str, JSONValue],
        force: bool
    ) -> None:
        # Conservative: if host state is unknown or in use, skip uninstall to avoid
        # clobbering user-managed podman resources.
        try:
            containers = await _podman_ids("container", {})
            if containers:
                return
            images = await _podman_ids("image", {})
            if images:
                return
            volumes = await _podman_ids("volume", {})
            if volumes:
                return
            await InstallPackage.undo(ctx, payload, force)
        except:
            return


@on_init(requires=[], version=1)
async def detect_platform(ctx: Pipeline.InProgress) -> None:
    """Detect the host platform and package manager to use when installing the
    container backend.  These are persisted as facts in the pipeline context.

    Parameters
    ----------
    ctx : Pipeline.InProgress
        The in-flight pipeline context.
    """
    user = User()
    ctx[USER] = user.name
    ctx[UID] = user.uid
    ctx[GID] = user.gid

    detect = detect_package_manager()
    ctx[PACKAGE_MANAGER] = detect.manager
    ctx[DISTRO_ID] = detect.distro_id
    ctx[DISTRO_VERSION] = detect.version_id
    ctx[DISTRO_CODENAME] = detect.codename


@on_init(requires=[detect_platform], version=1)
async def install_container_cli(ctx: Pipeline.InProgress) -> None:
    """Install the base packages required for the container backend via the detected
    package manager.  This may prompt for confirmation unless init is running with
    `--yes`.

    Parameters
    ----------
    ctx : Pipeline.InProgress
        The in-flight pipeline context.

    Raises
    ------
    OSError
        If `podman` is not found and installation is declined by the user.
    """
    assume_yes = _init_assume_yes(ctx)

    # check if podman CLI is already present
    cli = shutil.which("podman")
    if cli:
        # remember to delete all Bertrand-owned images/containers/artifacts, but do
        # not remove podman itself, since it was pre-installed.
        await ctx.do(PurgeBertrandArtifacts())
        return

    # prompt to install dependencies
    package_manager = ctx.get(PACKAGE_MANAGER)
    if not isinstance(package_manager, str):
        raise OSError(f"Invalid package manager: {package_manager}")
    if not confirm(
        "Bertrand requires 'podman' to manage rootless containers.  Would you like to "
        f"install it now using {package_manager} (requires sudo).\n"
        "[y/N] ",
        assume_yes=assume_yes,
    ):
        raise OSError("Installation declined by user.")

    # install podman and rootless helpers for the detected distro
    packages = [
        "podman",
        "slirp4netns",
        "passt",
        "fuse-overlayfs",
    ]
    if package_manager == "apt":
        packages.append("uidmap")
    elif package_manager == "dnf":
        packages.append("shadow-utils")
    await ctx.do(InstallPodman(
        manager=package_manager,
        packages=packages,
        assume_yes=assume_yes,
        refresh=True,
    ))

    # verify installation and record facts for later steps
    cli = shutil.which("podman")
    if not cli:
        raise OSError(
            "Installation completed, but 'podman' is still not found.  Please "
            "investigate the issue and ensure the required packages are installed."
        )

    # remember to delete all images/containers/artifacts on `clean`
    await ctx.do(PurgeBertrandArtifacts())


@on_init(requires=[install_container_cli], version=1)
async def enable_user_namespaces(ctx: Pipeline.InProgress) -> None:
    """Ensure unprivileged user namespaces are enabled on the host system, which are
    required for the rootless container cli.  Prompts are auto-accepted when init is
    run with `--yes`; permission failures still raise.

    Parameters
    ----------
    ctx : Pipeline.InProgress
        The in-flight pipeline context.
    """
    assume_yes = _init_assume_yes(ctx)
    if await _podman_ready():
        return

    await ctx.do(EnsureUserNamespaces(
        needed=15000,
        prompt=(
            "Rootless containers require unprivileged user namespaces to be enabled on "
            "the host system.  This may require sudo privileges.\n"
            "Do you want to proceed? [y/N] "
        ),
        assume_yes=assume_yes,
    ))


@on_init(requires=[install_container_cli], version=1)
async def provision_subids(ctx: Pipeline.InProgress) -> None:
    """Ensure subordinate UID/GID ranges are allocated for the host user in
    /etc/subuid and /etc/subgid, which are required for rootless Podman operation.
    Prompts are auto-accepted when init is run with `--yes`; permission failures still
    raise.

    Parameters
    ----------
    ctx : Pipeline.InProgress
        The in-flight pipeline context.

    Raises
    ------
    OSError
        If the USER fact is not a valid string.  This should have been set by the
        `detect_platform` step.
    """
    assume_yes = _init_assume_yes(ctx)
    if await _podman_ready():
        return

    user = ctx.get(USER)
    if not isinstance(user, str):
        raise OSError(f"Invalid user: {user}")
    await ctx.do(EnsureSubIDs(
        user=user,
        needed=65536,
        prompt=(
            "Rootless containers require subordinate UID/GID ranges (>= 65536) in "
            "/etc/subuid and /etc/subgid.  Bertrand can configure this, but may "
            "require sudo privileges to do so.\n"
            "Do you want to proceed? [y/N] "
        ),
        assume_yes=assume_yes,
    ))


def _cgroup_v2_controllers() -> set[str] | None:
    path = Path("/sys/fs/cgroup/cgroup.controllers")
    try:
        text = path.read_text(encoding="utf-8").strip()
    except OSError:
        return None
    return set(text.split()) if text else set()


def _user_cgroup_controllers(uid: int) -> set[str] | None:
    path = Path(
        f"/sys/fs/cgroup/user.slice/user-{uid}.slice/"
        f"user@{uid}.service/cgroup.controllers"
    )
    try:
        text = path.read_text(encoding="utf-8").strip()
    except OSError:
        return None
    return set(text.split()) if text else set()


async def _systemd_version() -> int | None:
    cp = await run(["systemctl", "--version"], check=False, capture_output=True)
    if cp.returncode != 0:
        return None
    line = ""
    if cp.stdout:
        line = cp.stdout.splitlines()[0].strip()
    match = re.search(r"systemd\s+(\d+)", line)
    if not match:
        return None
    return int(match.group(1))


def _dropin_delegate_controllers(path: Path) -> set[str] | None:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return None
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("Delegate="):
            value = line.split("=", 1)[1].strip()
            return set(value.split()) if value else set()
    return set()


@on_init(requires=[provision_subids, enable_user_namespaces], version=1)
async def delegate_controllers(ctx: Pipeline.InProgress) -> None:
    """Configure systemd controller delegation for the rootless container CLI, if not
    already configured.  This allows the container CLI to manage resource limits on
    cgroup v2 hosts.  Prompts are auto-accepted when init is run with `--yes`;
    permission failures still raise.

    Parameters
    ----------
    ctx : Pipeline.InProgress
        The in-flight pipeline context.

    Raises
    ------
    OSError
        If systemd is not found, or if elevation is required but not available.
    """
    assume_yes = _init_assume_yes(ctx)
    if await _podman_ready():
        return

    uid = ctx.get(UID)
    if not isinstance(uid, int):
        raise OSError(f"Invalid UID: {uid}")

    # controller delegation for cgroup v2 to enable rootless resource limits
    root_controllers = _cgroup_v2_controllers()
    if root_controllers is not None:
        required = {"cpu", "io", "memory", "pids"}

        # systemd 244+ is required for cpuset delegation
        systemd_version = await _systemd_version()
        if "cpuset" in root_controllers:
            if systemd_version is not None and systemd_version >= 244:
                required.add("cpuset")
            else:
                print(
                    "bertrand: cpuset controller detected, but systemd < 244; "
                    "skipping cpuset delegation.",
                    file=sys.stderr
                )

        # check if delegation is already configured for the requested controllers
        delegated = _user_cgroup_controllers(uid) or set()
        if not required.issubset(delegated):
            dropin_path = Path("/etc/systemd/system/user@.service.d/delegate.conf")
            dropin_controllers = _dropin_delegate_controllers(dropin_path) or set()
            if dropin_controllers and required.issubset(dropin_controllers):
                print(
                    "bertrand: controller delegation is configured but may require "
                    "logging out and back in to take effect.",
                    file=sys.stderr
                )

            # prompt and update delegation if needed
            else:
                await ctx.do(DelegateUserControllers(
                    controllers=sorted(required),
                    prompt=(
                        "Enforcing resource limits for rootless containers requires "
                        "cgroup controllers to be delegated to user sessions.  Bertrand "
                        "can configure this, but may require sudo privileges to do so.\n"
                        "Do you want to proceed? [y/N] "
                    ),
                    assume_yes=assume_yes,
                ))
                dropin_controllers = _dropin_delegate_controllers(dropin_path) or set()
                if dropin_controllers and required.issubset(dropin_controllers):
                    print(
                        "bertrand: controller delegation updated. You may need to "
                        "log out and back in for changes to take effect.",
                        file=sys.stderr
                    )
                else:
                    print(
                        "bertrand: controller delegation could not be configured; some "
                        "resource limits may be unavailable.",
                        file=sys.stderr
                    )


@on_init(requires=[delegate_controllers], version=1)
async def assert_container_cli_ready(ctx: Pipeline.InProgress) -> None:
    """Assert that the rootless container backend is usable after host bootstrap.

    Parameters
    ----------
    ctx : Pipeline.InProgress
        The in-flight pipeline context.

    Raises
    ------
    OSError
        If `podman` is still not usable after host bootstrap.  This likely indicates a
        misconfiguration of host prerequisites (user namespaces, subuid/subgid, or
        controller delegation), and should be investigated by checking `podman info`
        and verifying the host meets all prerequisites.
    """
    if await _podman_ready():
        return

    assume_yes = _init_assume_yes(ctx)
    cmd = "bertrand init --yes" if assume_yes else "bertrand init"
    raise OSError(
        "podman is installed but not usable for rootless operation after init "
        "bootstrap. Verify host prerequisites (user namespaces, subuid/subgid, and "
        "controller delegation) and retry.  Run `podman info` for diagnostics. "
        f"(last command: `{cmd}`)"
    )


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


class Registry(BaseModel):
    """Serialized registry of environments that have been built on this system.

    A global registry of this form is stored in the `init` pipeline's state directory,
    and allows Bertrand to target all environments on the system for various CLI
    commands, as well as detect relocation of environments, possibly across different
    hosts.

    Attributes
    ----------
    host : UUIDStr
        A UUID tied to the lifetime of the registry.  Every environment's metadata
        will store the host UUID of the registry it was initialized from.  If we
        attempt to insert the environment into another registry with a different
        UUID, then it signifies the environment was sent from another host, and we
        should force a rebuild of all downstream images and containers.
    environments : dict[UUIDStr, Path]
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
    purge_cursor : UUIDStr | None
        Cursor used to scan environment IDs in deterministic batches when purging stale
        entries.
    """
    host: UUIDStr
    environments: dict[UUIDStr, AbsolutePath]
    ops_since_purge: NonNegativeInt
    purge_cursor: UUIDStr | None = None

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
        return Lock(REGISTRY_LOCK, timeout=timeout)

    # NOTE: we have to strictly obey registry > environment locking order to avoid
    # deadlocks, since `add()` and purge both need to acquire environment locks to
    # validate metadata.  If environment operations consistently acquire the registry
    # lock first, then we can guarantee that deadlocks won't occur, since there will
    # never be simultaneous env -> registry and registry -> env edges.  The following
    # methods assume the locks have already been handled by the caller.

    @staticmethod
    async def _check_env(root: Path, env_id: UUIDStr | None = None) -> Environment.JSON | None:
        try:
            root = root.expanduser().resolve()
            env_file = root / METADATA_FILE
            if env_file.exists() and env_file.is_file():
                metadata = _read_metadata(root)
                if metadata is not None:
                    await Config.load(root)
                    if env_id is None or metadata.id == env_id:
                        return metadata
        except:
            pass
        return None

    @staticmethod
    async def _discover_environment_mounts() -> list[Path]:
        container_ids = await _podman_ids("container", {})
        if not container_ids:
            return []

        # inspect all containers and retrieve unique bind mounts
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
        made to rebuild it by inspecting active container mounts and their metadata.
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
            except:
                self = cls(
                    host=uuid.uuid4().hex,
                    ops_since_purge=0,
                    purge_cursor=None,
                    environments={}
                )
                for root in await cls._discover_environment_mounts():
                    with lock_worktree(root, timeout=TIMEOUT):
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

    def _purge_candidates(self, *, batch_size: int) -> list[UUIDStr]:
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
                with lock_worktree(root, timeout=TIMEOUT):
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
                images={}
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

            # TODO: is this right with new retiring model?

            # otherwise, the new environment constitutes a move, and we can
            # transfer ownership by deleting the old containers (but not
            # images), to avoid coupling with the previous path
            while env.images:
                tag, image = env.images.popitem()
                try:
                    await image.remove(force=True, timeout=TIMEOUT, missing_ok=True)
                except:
                    env.images[tag] = image
                    raise
                env_changed = True

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
        normalized: dict[UUIDStr, AbsolutePath] = {}
        for env_id, root in self.environments.items():
            with lock_worktree(root, timeout=TIMEOUT):
                env = await self._check_env(root, env_id=env_id)
                if env is None:
                    continue
                normalized.setdefault(env.id, root)  # preserve first match if duplicates

        self.environments = normalized
        self.ops_since_purge = 0
        if self.purge_cursor not in self.environments:
            self.purge_cursor = None


@on_init(requires=[assert_container_cli_ready], ephemeral=True)
async def init_environment(ctx: Pipeline.InProgress) -> None:
    """Initialize an environment directory using discovered/requested resources and
    templates.

    Parameters
    ----------
    ctx : Pipeline.InProgress
        The in-flight pipeline context.

    Raises
    ------
    OSError
        If required init facts are missing and no existing resource config can be
        discovered for fallback, or if requested resources conflict with existing
        resources.
    TypeError
        If init facts are present with invalid types.
    """
    env = ctx.get("env")
    if env is None:
        return
    if not isinstance(env, str):
        raise TypeError("environment path must be a string")
    worktree = Path(env).expanduser().resolve()
    if ctx["image_tag"] or ctx["container_tag"]:
        raise OSError(
            "cannot specify image or container tag when initializing an environment "
            "directory."
        )

    # parse requested profile
    profile = ctx.get("profile")
    if profile is not None and not isinstance(profile, str):
        raise TypeError("profile must be a string")

    # parse requested capabilities
    capabilities: set[str] = set()
    raw_capabilities = ctx.get("capabilities")
    if raw_capabilities is not None:
        if not isinstance(raw_capabilities, (list, tuple)):
            raise TypeError("capabilities must be a list or tuple of strings")
        for cap in raw_capabilities:
            if not isinstance(cap, str):
                raise TypeError(f"capability must be a string: {cap}")
            capabilities.add(cap)

    # TODO: pass in `bertrand init` CLI args as kwarg facts to `init`.

    # apply effective resource config and render templates in environment directory
    await Config.init(worktree, profile, capabilities)


@on_init(requires=[init_environment], ephemeral=True)
async def init_repository(ctx: Pipeline.InProgress) -> None:
    """Initialize a git repository in the environment directory.

    Parameters
    ----------
    ctx : Pipeline.InProgress
        The in-flight pipeline context.

    Raises
    ------
    TypeError
        If the "env" fact is missing or not a string.
    """
    env = ctx.get("env")
    if env is None:
        return
    if not isinstance(env, str):
        raise TypeError("environment path must be a string")
    root = Path(env).expanduser().resolve()

    # check if git is available
    if not shutil.which("git"):
        return

    # check if repo is already initialized
    git_dir = root / ".git"
    if git_dir.exists():
        return

    # initialize repo and make an initial commit with the rendered environment
    stage = "initialize git repository"
    try:
        await run(["git", "init", "--quiet"], cwd=root, capture_output=True)
        stage = "stage files for initial commit"
        await run(["git", "add", "-A"], cwd=root, capture_output=True)
        stage = "create initial commit"
        await run(
            ["git", "commit", "--quiet", "-m", "Initial commit"],
            cwd=root,
            capture_output=True
        )
    except Exception as err:
        print(f"bertrand: failed to {stage} in {root}\n{err}", file=sys.stderr)


def _load_reference_transaction_hook() -> str:
    source = importlib_resources.files("bertrand.env").joinpath(
        "git",
        "reference_transaction.py",
    ).read_text(encoding="utf-8")
    lines = source.splitlines()
    if not lines:
        raise ValueError("packaged reference_transaction.py is empty")
    if lines[0].strip() != "#!/usr/bin/env python3":
        raise ValueError(
            "packaged reference_transaction.py must start with '#!/usr/bin/env python3'"
        )
    return "\n".join([
        lines[0],
        REFERENCE_TRANSACTION_MARKER,
        *lines[1:]
    ]).rstrip("\n") + "\n"


async def _resolve_git_path(root: Path, git_path: str) -> Path:
    result = await run(
        ["git", "rev-parse", "--git-path", git_path],
        cwd=root,
        capture_output=True,
    )
    resolved = Path(result.stdout.strip())
    if not resolved.is_absolute():
        resolved = root / resolved
    return resolved.resolve()


async def _resolve_head_worktree(root: Path) -> Path:
    git_dir = root / ".git"
    if git_dir.is_file():  # linked worktree
        return root
    if not git_dir.is_dir():
        return root

    # only parent bare repositories should auto-expand to the HEAD branch worktree
    is_bare = await run(
        ["git", "--git-dir", str(git_dir), "rev-parse", "--is-bare-repository"],
        check=False,
        capture_output=True,
    )
    if is_bare.returncode != 0 or is_bare.stdout.strip().lower() != "true":
        return root

    # expand to the current HEAD branch
    head_branch = await run(
        ["git", "--git-dir", str(git_dir), "symbolic-ref", "--quiet", "--short", "HEAD"],
        check=False,
        capture_output=True,
    )
    branch = head_branch.stdout.strip()
    if head_branch.returncode != 0 or not branch:
        return root
    candidate = (root / branch).resolve()
    if candidate.exists() and candidate.is_dir():
        return candidate
    return root


@on_init(requires=[init_repository], ephemeral=True)
async def install_git_hooks(  # pylint: disable=missing-raises-doc
    ctx: Pipeline.InProgress
) -> None:
    """Install git hooks in the environment repository to enable automatic lifecycle
    management for branch worktrees.

    Parameters
    ----------
    ctx : Pipeline.InProgress
        The in-flight pipeline context.

    Raises
    ------
    TypeError
        If the "env" fact is missing or not a string.
    """
    env = ctx.get("env")
    if env is None:
        return
    if not isinstance(env, str):
        raise TypeError("environment path must be a string")
    root = Path(env).expanduser().resolve()

    # check if git is available
    if not shutil.which("git"):
        return

    # check if repo is not initialized
    git_dir = root / ".git"
    if not git_dir.exists():
        return

    # load git hooks from packaged resources
    stage = "load packaged reference-transaction hook"
    try:
        hook_text = _load_reference_transaction_hook()
        stage = f"resolve git hook path for '{REFERENCE_TRANSACTION_HOOK}'"
        hook_path = await _resolve_git_path(root, REFERENCE_TRANSACTION_HOOK)

        # check for conflicts
        install_hook = True
        if hook_path.exists():  # do not clobber non-Bertrand hooks
            if not hook_path.is_file():
                raise OSError(f"git hook path is not a file: {hook_path}")
            existing = hook_path.read_text(encoding="utf-8")
            if existing == hook_text:
                install_hook = False  # already installed and up-to-date
            elif REFERENCE_TRANSACTION_MARKER not in existing:
                print(
                    f"existing git hook at {hook_path} is not managed by Bertrand; "
                    f"skipping to avoid clobbering user-managed hook.",
                    file=sys.stderr
                )
                install_hook = False

        # install hook into git directory
        if install_hook:
            stage = f"write git hook to {hook_path}"
            atomic_write_text(hook_path, hook_text, encoding="utf-8")
            stage = f"set executable permissions on git hook {hook_path}"
            try:
                hook_path.chmod(0o755)
            except OSError:
                pass
    except Exception as err:
        print(f"bertrand: failed to {stage} in {root}\n{err}", file=sys.stderr)


@on_init(requires=[install_git_hooks], ephemeral=True)
async def register_environment(ctx: Pipeline.InProgress) -> None:
    """Register the environment in the global registry to enable management by CLI
    commands.

    Parameters
    ----------
    ctx : Pipeline.InProgress
        The in-flight pipeline context.

    Raises
    ------
    TypeError
        If the "env" fact is missing or not a string.
    """
    env = ctx.get("env")
    if env is None:
        return
    if not isinstance(env, str):
        raise TypeError("environment path must be a string")
    worktree = Path(env).expanduser().resolve()

    # add to global environment registry, respecting registry > environment lock order
    async with Registry.lock(timeout=TIMEOUT):
        async with lock_worktree(worktree, timeout=TIMEOUT):
            registry = await Registry.load()
            await registry.add(worktree)
            await registry.dump()


async def podman_cmd(
    args: list[str],
    *,
    check: bool = True,
    capture_output: bool | None = False,
    input: str | None = None,
    timeout: float | None = None,
    cwd: Path | None = None,
    env: Mapping[str, str] | None = None,
) -> CompletedProcess:
    """Run a podman command.

    Parameters
    ----------
    args : list[str]
        The podman command arguments (excluding the 'podman' executable).
    check : bool, optional
        If True, raise CommandError on non-zero exit code.  Default is True.
    capture_output : bool | None, optional
        If True, capture stdout/stderr in the returned `CompletedProcess` or
        `CommandError`.  If False, do not capture output.  If None, tee output to both
        the console and the returned objects.
    input : str | None, optional
        Input to send to the command's stdin (default is None).
    timeout : float | None, optional
        An optional timeout in seconds to wait for the command to complete before
        raising a `TimeoutExpired` exception.  Default is None, which means to wait
        indefinitely.
    cwd : Path | None, optional
        An optional working directory to run the command in.  If None (the default),
        then the current working directory will be used.
    env : Mapping[str, str] | None, optional
        An optional mapping of environment variables to set for the command.  If None
        (the default), then the current environment will be used.

    Returns
    -------
    CompletedProcess
        The completed process result.

    Raises
    ------
    CommandError
        If the command fails and `check` is True.
    """
    return await run(
        ["podman", *args],
        check=check,
        capture_output=capture_output,
        input=input,
        cwd=cwd,
        env=env,
        timeout=timeout
    )


def podman_exec(args: list[str], *, env: Mapping[str, str] | None = None) -> None:
    """Run a podman command by replacing the current process.  This is intended for
    interactive use cases like `bertrand enter` where we want to drop the user into a
    shell inside the container.

    Parameters
    ----------
    args : list[str]
        The podman command arguments (excluding the 'podman' executable).
    env : Mapping[str, str] | None, optional
        An optional mapping of environment variables to set for the command.  If None
        (the default), then the current environment will be used.

    Raises
    ------
    OSError
        If execution fails.
    """
    if env is None:
        os.execvp("podman", ["podman", *args])
    else:
        os.execvpe("podman", ["podman", *args], env)


async def _podman_ids(
    mode: Literal["container", "image", "volume"],
    labels: Mapping[str, str],
    *,
    status: Sequence[str] | None = None,
    timeout: float = TIMEOUT,
) -> list[str]:
    deadline = time.time() + timeout

    # form basic command based on mode
    cmd: list[str] = []
    if mode == "volume":
        cmd.extend(["volume", "ls", "-q", "--filter", f"label={BERTRAND_ENV}=1"])
    elif mode == "image":
        cmd.extend([
            "image",
            "ls",
            "-a",
            "-q",
            "--no-trunc",
            "--filter", f"label={BERTRAND_ENV}=1"
        ])
    else:
        cmd.extend([
            "container",
            "ls",
            "-a",
            "-q",
            "--no-trunc",
            "--filter", f"label={BERTRAND_ENV}=1"
        ])

    # append additional labels to filter results
    for k, v in labels.items():
        cmd.extend(["--filter", f"label={k}={v}"])

    # parse results, returning an empty/partial list on failure (best-effort)
    out: list[str] = []
    seen: set[str] = set()
    try:
        # return all statuses by default
        if status is None:
            result = await podman_cmd(
                cmd,
                capture_output=True,
                check=False,
                timeout=deadline - time.time()
            )
            if result.returncode == 0:
                for raw_id in result.stdout.splitlines():
                    container_id = raw_id.strip()
                    if not container_id or container_id in seen:
                        continue
                    seen.add(container_id)
                    out.append(container_id)
            return out

        # filter by status
        for stat in status:
            result = await podman_cmd(
                [*cmd, "--filter", f"status={stat}"],
                capture_output=True,
                check=False,
                timeout=deadline - time.time()
            )
            if result.returncode != 0:
                continue
            for raw_id in result.stdout.splitlines():
                container_id = raw_id.strip()
                if not container_id or container_id in seen:
                    continue
                seen.add(container_id)
                out.append(container_id)
    except (TimeoutError, TimeoutExpired, KeyboardInterrupt, SystemExit):
        raise
    except:
        pass
    return out


class Container(BaseModel):
    """Type hint for container inspect output.  Note that due to the ephemeral
    container architecture, this is not persisted to disk, unlike image metadata.

    https://docs.podman.io/en/latest/markdown/podman-container-inspect.1.html#examples
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
    Image: ImageId

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
    def worktree(self) -> Path | None:
        """Extract the root path of the worktree directory mounted to a container from
        its inspection data.

        Returns
        -------
        Path | None
            The root path of the worktree directory mounted to the container, or None
            if no such mount exists.
        """
        for m in self.Mounts:
            if m.Type == "bind" and m.Destination == str(WORKTREE_MOUNT):
                src = m.Source
                if src:
                    return Path(src).expanduser().resolve()
        return None

    @classmethod
    async def inspect(cls, ids: list[ContainerId]) -> list[Self]:
        """Invoke podman to inspect this container and return the parsed result.

        Parameters
        ----------
        ids : list[ContainerId]
            The unique podman container IDs.

        Returns
        -------
        list[Self]
            A list of validated JSON responses from podman.  If a container could not be
            found, it will be omitted from the list.
        """
        if not ids:
            return []
        result = await podman_cmd(
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
    async def remove(
        ids: list[ContainerId],
        *,
        force: bool,
        timeout: float,
        missing_ok: bool
    ) -> None:
        """Remove this container via podman.

        Parameters
        ----------
        ids : list[ContainerId]
            The unique podman container IDs to remove.
        force : bool
            If True, forcefully remove the container even if it is currently running
            or paused.  If False, only remove the container if it is currently stopped.
        timeout : int
            The maximum time in seconds to wait for a running container to stop before
            forcefully killing it.  -1 indicates an infinite wait.
        missing_ok : bool
            If True, do not raise an error if the container is already removed or
            otherwise missing.

        Raises
        ------
        CommandError
            If the podman command fails.
        """
        cmd = [
            "container",
            "rm",
            "--depend",  # remove dependent containers
            "-v",  # remove anonymous volumes
            "-t", str(int(math.ceil(timeout))),
        ]
        if force:
            cmd.append("-f")
        if missing_ok:
            cmd.append("-i")
        cmd.extend(ids)
        await podman_cmd(cmd)

        # remove any now-dangling volumes
        volumes = list((await podman_cmd([
            "volume",
            "ls",
            "-q",
            "--filter", f"label={BERTRAND_ENV}=1",
            "--filter", "dangling=true",
        ], capture_output=True)).stdout.splitlines())
        if volumes:
            cmd = ["volume", "rm"]
            if force:
                cmd.append("-f")
            if missing_ok:
                cmd.append("-i")
            cmd.extend(volumes)
            await podman_cmd(cmd)


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
        The unique podman image ID.
    created : datetime
        The ISO timestamp when the image was created.
    image_args : list[str]
        The original `podman build` args used to build the image.
    container_args : list[str]
        The `podman create` args used to build containers from this image.
    """
    class Inspect(BaseModel):
        """Type hint for `podman image inspect` output.

        https://docs.podman.io/en/latest/markdown/podman-image-inspect.1.html#example
        """
        model_config = ConfigDict(extra="allow")
        Id: ImageId
        Created: CreatedAt

    model_config = ConfigDict(extra="forbid")
    version: PositiveInt
    tag: SanitizedName
    id: ImageId
    created: CreatedAt
    image_args: ArgsList
    container_args: ArgsList

    async def inspect(self) -> Image.Inspect | None:
        """Invoke podman to inspect this image.

        Returns
        -------
        Image.Inspect | None
            A JSON response from podman or None if the image could not be found.
        """
        result = await podman_cmd(
            ["image", "inspect", self.id],
            check=False,
            capture_output=True
        )
        if result.returncode != 0 or not result.stdout:
            return None
        data = json_parser.loads(result.stdout)
        return Image.Inspect.model_validate(data[0]) if data else None

    async def remove(self, *, force: bool, timeout: float, missing_ok: bool) -> None:
        """Remove this image via podman.  Will also remove all containers built from
        this image.

        Parameters
        ----------
        force : bool
            If True, forcefully remove the image even if it has running containers.
            If False, only remove stopped containers, and then remove the image if no
            containers remain.
        timeout : float
            The maximum time in seconds to wait for running containers to stop before
            forcefully killing them.  0 indicates an infinite wait.
        missing_ok : bool
            If True, do not raise an error if the image or any descendant container is
            already removed or otherwise missing.

        Raises
        ------
        OSError
            If `force` is False and there are still containers referencing this image.
        CommandError
            If any of the podman commands fail.
        """
        # remove descendant containers first to avoid dangling references
        containers = await _podman_ids(
            "container",
            labels={IMAGE_ID_ENV: self.id},
            timeout=timeout
        )
        if containers:
            await Container.remove(
                containers,
                force=force,
                timeout=timeout,
                missing_ok=missing_ok
            )

        # remove image
        cmd = ["image", "rm"]
        if force:
            cmd.append("-f")
        if missing_ok:
            cmd.append("-i")
        cmd.append(self.id)
        await podman_cmd(cmd)

    async def _volume(self, env: Environment, kind: str) -> str:
        name = f"bertrand-{env.id[:13]}-{kind}"
        try:
            await podman_cmd([
                "volume",
                "create",
                "--label", f"{BERTRAND_ENV}=1",
                "--label", f"{ENV_ID_ENV}={env.id}",
                "--label", f"{IMAGE_TAG_ENV}={self.tag}",
                name,
            ], check=False)
        except:
            pass
        return name

    # TODO: container builds should possibly just run the `create` portion of the
    # process, so that it starts in a paused state.

    async def container(
        self,
        env: Environment,
        cmd: list[str] | None,
        *,
        quiet: bool
    ) -> None:
        """Build and run an ephemeral container from this image if it does not already
        exist.

        Parameters
        ----------
        env : Environment
            The parent environment this image belongs to, which describes the worktree
            directory that will be mounted into the container.
        cmd : list[str] | None
            An optional command to override the container's default entry point.  If
            None, then the container will run the default startup behavior from its
            originating image.  In both cases, the container will be destroyed once
            the command completes.
        quiet : bool
            If True, suppress output from the podman commands.

        Raises
        ------
        OSError
            If the container fails to build or cannot be found after building.
        TypeError
            If the image configuration for the commit is invalid.
        """
        if cmd is not None:
            if not isinstance(cmd, list):
                raise TypeError("cmd must be a list of strings when provided")
            if not cmd:
                raise OSError("cmd override must be non-empty when provided")
            if not all(isinstance(part, str) for part in cmd):
                raise TypeError("cmd override must be a list of strings")

        # build candidate container
        mkdir_private(HOST_SOCKET.parent)
        uv_cache = self._volume(env, "uv")
        bertrand_cache = self._volume(env, "bertrand")
        ccache_cache = self._volume(env, "ccache")
        conan_cache = self._volume(env, "conan")
        argv = [
            "run",
            "--detach",
            "--rm",
            "--init",

            # labels for podman-level lookup
            "--label", f"{BERTRAND_ENV}=1",
            "--label", f"{ENV_ID_ENV}={env.id}",
            "--label", f"{IMAGE_ID_ENV}={self.id}",
            "--label", f"{IMAGE_TAG_ENV}={self.tag}",

            # mount mutable worktree
            "-v", f"{str(env.worktree)}:{str(WORKTREE_MOUNT)}",
            "--mount",
            (
                "type=bind,"
                f"src={str(HOST_SOCKET.parent)},"
                f"dst={str(CONTAINER_SOCKET.parent)},"
                "ro=true"
            ),

            # persistent caches for incremental builds
            "--mount", f"type=volume,src={uv_cache},dst={CACHES}/uv",
            "--mount", f"type=volume,src={bertrand_cache},dst={CACHES}/bertrand",
            "--mount", f"type=volume,src={ccache_cache},dst={CACHES}/ccache",
            "--mount", f"type=volume,src={conan_cache},dst=/opt/conan",

            # environment variables for Bertrand runtime
            "-e", f"{BERTRAND_ENV}=1",
            "-e", f"{ENV_ID_ENV}={env.id}",
            "-e", f"{IMAGE_TAG_ENV}={self.tag}",
        ]
        argv.extend(self.container_args)
        if cmd is None:  # preserve image defaults (ENTRYPOINT/CMD)
            argv.append(self.id)
        else:  # override entrypoint and pass additional args, if any
            argv.extend(["--entrypoint", cmd[0], self.id, *cmd[1:]])
        await podman_cmd(argv, capture_output=quiet)


class Environment:
    """On-disk metadata representing environment-level data structures, which map from
    human-readable, stable tags to the corresponding images and containers built within
    this environment directory.

    This class is meant to be used as a context manager, which will automatically
    acquire and release a lock on the environment directory in order to prevent
    concurrent modifications.  The environment metadata will be loaded upon entering
    the outermost context, and written back to disk upon exiting it, in order to
    synchronize any changes made during the context's lifetime.

    Parameters
    ----------
    root : Path
        The root path of the environment directory.
    timeout : int, optional
        The maximum time in seconds to wait for acquiring the environment lock.
        Defaults to `TIMEOUT`, which equates to 30 seconds.

    Attributes
    ----------
    root : Path
        An absolute root path to the environment directory.  This is not stored in the
        on-disk JSON in order to allow relocation of the environment directory..
    """
    class JSON(BaseModel):
        """Pydantic model representing JSON metadata for a Bertrand environment."""
        model_config = ConfigDict(extra="forbid")
        version: PositiveInt
        host: UUIDStr
        id: UUIDStr
        images: dict[SanitizedName, Image]
        retired: list[Image]

    worktree: Path
    _json: JSON
    _config: Config | None
    _lock: Lock
    _entered: int

    def __init__(self, worktree: Path, timeout: float = TIMEOUT) -> None:
        self.worktree = worktree.expanduser().resolve()
        self._json = self.JSON.model_construct(version=0, host="", id="", images={})
        self._config = None
        self._lock = lock_worktree(self.worktree, timeout=timeout)
        self._entered = 0

    async def __aenter__(self) -> Self:
        entered = self._entered
        try:
            # obey registry > environment lock order
            async with Registry.lock(timeout=self.timeout):
                await self._lock.__aenter__()
                self._entered += 1
                if self._entered > 1:
                    return self  # re-entrant case

                # add to the environment registry while lock is held
                registry = await Registry.load()
                self._json = await registry.add(self.worktree)
                await registry.dump()

            # release registry lock before loading environment config to minimize contention
            self._config = await Config.load(self.worktree)
            await self._config.__aenter__()
            return self

        except Exception as err:
            if self._entered > entered:
                self._config = None
                self._entered -= 1
                await self._lock.__aexit__(
                    type(err),
                    err,
                    getattr(err, "__traceback__", None)
                )
            raise

    async def __aexit__(
        self,
        exc_type: type[BaseException] | None,
        exc_value: BaseException | None,
        traceback: TracebackType | None
    ) -> None:
        if self._entered < 1:
            raise RuntimeError("environment context manager was not entered")

        try:
            # attempt to empty the retired images list
            retired: list[Image] = []
            for image in self._json.retired:
                # TODO: remove(force=False) should return a bool indicating whether
                # the image was removed, so that this works
                if not await image.remove(
                    force=False,
                    timeout=self.timeout,
                    missing_ok=True
                ):
                    retired.append(image)
            self._json.retired = retired

            # write metadata back to disk
            env_dir = self.worktree / METADATA_DIR
            if self._entered == 1 and env_dir.exists():
                _write_metadata(self.worktree, self._json)

        # always release the lock and local context depth
        finally:
            if self._entered == 1 and self._config is not None:
                await self._config.__aexit__(exc_type, exc_value, traceback)
                self._config = None
            await self._lock.__aexit__(exc_type, exc_value, traceback)
            self._entered -= 1

    def __bool__(self) -> bool:
        return self._entered > 0 and self._config is not None

    def __hash__(self) -> int:
        return hash(self.worktree)

    def __eq__(self, other: Any) -> bool:
        if not isinstance(other, Environment):
            return NotImplemented
        return self.worktree == other.worktree

    @staticmethod
    async def parse(spec: str) -> tuple[Path, str | None, str | None]:
        """Parse a string of the form `<worktree>[@<workload>][:<tag>]` into its
        constituent parts.

        Parameters
        ----------
        spec : str
            The container specification string to parse, which is usually provided from
            the command line or `$ bertrand ls`.

        Returns
        -------
        tuple[Path, str, str]
            A tuple containing the environment root path, workload, and container tag.
            The environment root path is expanded and resolved into an absolute path,
            which may extend a bare repository path to point to the current HEAD
            branch.  The workload and container tag will be empty strings if not
            provided.

        Raises
        ------
        ValueError
            If the environment path could not be resolved, or either tag is empty or
            contains invalid characters.
        """
        workload: str | None
        tag: str | None

        # extract tag if present
        prev, sep, tag = spec.rpartition(":")
        if not sep:
            prev = tag  # swap if no `:` found
            tag = None
        elif os.path.sep in tag:
            prev = prev + ":" + tag  # replace if `:` is part of the path, not a tag
            tag = None
        if tag is not None:
            tag = tag.strip()
            if not tag:
                raise ValueError(
                    "tag cannot be empty following ':' in container specification"
                )
            san = sanitize_name(tag)
            if not tag or tag != san:
                raise ValueError(
                    f"tag contains invalid characters: '{tag}' (sanitizes to: '{san}')"
                )

        # extract workload if present
        worktree, sep, workload = prev.rpartition("@")
        if not sep:
            worktree = workload  # swap if no `@` found
            workload = None
        elif os.path.sep in workload:
            worktree = prev + "@" + workload  # replace if `@` is part of the path (unlikely)
            workload = None
        if workload is not None:
            workload = workload.strip()
            if not workload:
                raise ValueError(
                    "workload cannot be empty following '@' in container specification"
                )
            san = sanitize_name(workload)
            if not workload or workload != san:
                raise ValueError(
                    f"workload contains invalid characters: '{workload}' (sanitizes "
                    f"to: '{san}')"
                )

        # attempt to load environment
        root = Path(worktree.strip()).expanduser().resolve()
        root = await _resolve_head_worktree(root)
        return root, workload, tag

    @staticmethod
    def current() -> Environment | None:
        """Detect whether the current process is running inside a Bertrand container.

        Returns
        -------
        Environment | None
            An `Environment` metadata object with the proper mount path if invoked
            within a Bertrand container, or None otherwise.  Note that the result is
            disengaged, and must be acquired as a context manager before it can be
            used to access or modify the environment.
        """
        if not inside_image():
            return None
        return Environment(worktree=WORKTREE_MOUNT)

    @property
    def config(self) -> Config:
        """
        Returns
        -------
        Config
            The configuration object representing the layout of the environment
            directory and registered image/container tags and their arguments.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if the
            configuration was not loaded from disk.
        """
        if self._entered < 1 or self._config is None:
            raise OSError(
                "environment must be acquired as a context manager before accessing "
                "configuration"
            )
        return self._config

    # TODO: return a float, and then apply math.ceil in the podman commands that
    # require it.

    @property
    def timeout(self) -> float:
        """
        Returns
        -------
        float
            The maximum time in seconds to wait for acquiring the environment lock.
        """
        return self._lock.timeout

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
    def images(self) -> dict[SanitizedName, Image]:
        """
        Returns
        -------
        dict[SanitizedName, Image]
            A dictionary mapping sanitized image names to their corresponding image objects.
            across all commits in this environment, which may be useful for performing
            environment-level operations across all images.
        """
        return self._json.images

    @property
    def retired(self) -> list[Image]:
        """
        Returns
        -------
        list[Image]
            A list of retired images that have become outdated and scheduled for
            removal.  Each command will attempt to empty this list by checking whether
            the images are still referenced by any active containers, and removing them
            if not, allowing the containers to run to completion normally, unless
            stopped by another command.
        """
        return self._json.retired

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
        ids = await _podman_ids(
            "container",
            {ENV_ID_ENV: self.id},
            status=["created", "paused", "restarting", "running"]
        )
        return await Container.inspect(ids)

    async def remove(self, *, force: bool, timeout: float, missing_ok: bool) -> None:
        """Remove this environment via podman, including all descendant images and
        containers.  Note that this does not delete the referencing tags, meaning the
        images and containers will be rebuilt again if referenced in the future.

        Parameters
        ----------
        force : bool
            If True, remove images even if they have dependent containers, removing
            the containers as well.  If False, only remove images that have no
            dependent containers.
        timeout : float
            The maximum time in seconds to wait for dependent containers to stop before
            forcefully killing them.  0 indicates an infinite wait.
        missing_ok : bool
            If True, do not raise an error if the environment or any descendant image or
            container is already removed or otherwise missing.

        Raises
        ------
        OSError
            If `force` is False and there are still images or containers referencing
            this environment.
        CommandError
            If any of the podman commands fail and `missing_ok` is False.
        """
        # find all descendant containers + images
        containers = await _podman_ids("container", {ENV_ID_ENV: self.id})
        images = await _podman_ids("image", {ENV_ID_ENV: self.id})

        # remove containers first
        if containers:
            await Container.remove(
                containers,
                force=force,
                timeout=timeout,
                missing_ok=missing_ok,
            )

        # remove images
        if images:
            cmd = ["image", "rm"]
            if force:
                cmd.append("-f")
            if missing_ok:
                cmd.append("-i")
            cmd.extend(images)
            await podman_cmd(cmd)

        # remove environment metadata and lock
        try:
            self._json.images.clear()
            shutil.rmtree(self.worktree / METADATA_DIR, ignore_errors=True)
        except:
            pass


def _check_boolean(value: Any) -> bool:
    if not isinstance(value, bool):
        raise ValueError(f"expected a boolean value, got: {value}")
    return value


def _validate_args(x: JSONValue) -> list[str]:
    if x is None:
        return []
    if isinstance(x, tuple) and all(isinstance(i, str) for i in x):
        return list(cast(tuple[str, ...], x))
    raise TypeError("args must be a sequence of strings")


def _validate_code_server_available(x: JSONValue) -> bool:
    if x is None:
        return False
    if isinstance(x, bool):
        return x
    raise TypeError(f"invalid '{CODE_SERVER_AVAILABLE}' fact type: {type(x).__name__}")


# pylint: disable=missing-function-docstring, missing-param-doc
# pylint: disable=missing-return-doc, unused-argument, protected-access


# TODO: I should try to update the simplest commands first and remove their
# corresponding pipelines.  I'll handle the build/start/enter/code/publish commands
# last.


@dataclass
class _Command:

    @staticmethod
    def _validate_env(x: JSONValue) -> Path | None:
        if x is None:
            return None
        if not isinstance(x, str):
            raise TypeError("environment path must be a string")
        x = x.strip()
        if not x:
            return None
        return Path(x).expanduser().resolve()

    @staticmethod
    def _validate_image(x: JSONValue) -> str:
        if x is None:
            return ""
        if not isinstance(x, str):
            raise TypeError("image tag must be a string")
        x = x.strip()
        sanitized = sanitize_name(x)
        if x != sanitized:
            raise ValueError(
                f"invalid image tag: '{x}' (must contain only alphanumerics, '_', or '.')"
            )
        return x

    @staticmethod
    def _validate_container(x: JSONValue) -> str:
        if x is None:
            return ""
        if not isinstance(x, str):
            raise TypeError("container tag must be a string")
        x = x.strip()
        sanitized = sanitize_name(x)
        if x != sanitized:
            raise ValueError(
                f"invalid container tag: '{x}' (must contain only alphanumerics, '_', or '.')"
            )
        return x

    env: Validator = field(default=_validate_env)
    image_tag: Validator = field(default=_validate_image)
    container_tag: Validator = field(default=_validate_container)

    def _call(self, ctx: Pipeline.InProgress) -> None:
        # pylint: disable=no-member
        # extract and validate all arguments from the context
        kwargs = {k: v(ctx.get(k)) for k, v in asdict(self).items()}

        # if no environment is given, call the subclass's `all()` method
        env_root = kwargs.pop("env")
        if not env_root:
            if kwargs["image_tag"] or kwargs["container_tag"]:
                raise TypeError("cannot specify image or container tag without environment")
            kwargs["env"] = None
            self.all(ctx, **kwargs)  # type: ignore[attr-defined]
            return

        # load environment metadata
        with Environment(env_root, timeout=TIMEOUT) as env:
            kwargs["env"] = env

            # if no image tag is given, call the subclass's `environment()` method
            if not kwargs["image_tag"]:
                if kwargs["container_tag"]:
                    raise TypeError("cannot specify a container when image tag is None")
                self.environment(ctx, **kwargs)  # type: ignore[attr-defined]
                return

            # if no container tag is given, call the subclass's `image()`
            if not kwargs["container_tag"]:
                self.image(ctx, **kwargs)  # type: ignore[attr-defined]
                return

            # otherwise, call the subclass's `container()` method
            self.container(ctx, **kwargs)  # type: ignore[attr-defined]

    def __call__(self, ctx: Pipeline.InProgress) -> None:
        if Environment.current() is not None:
            raise OSError("cannot invoke podman from within a Bertrand container")
        self._call(ctx)


@dataclass
class Publish(_Command):
    """Build and publish Bertrand images to a remote OCI registry.  This command is
    meant to be used in CI workflows triggered by git tags, and usually does not need
    to be invoked by the user directly.
    """
    @staticmethod
    def _validate_version(x: JSONValue) -> str | None:
        if x is None:
            return None
        if not isinstance(x, str):
            raise TypeError("version must be a string")
        x = x.strip()
        return x or None

    @staticmethod
    def _validate_repo(x: JSONValue) -> str:
        if not isinstance(x, str):
            raise TypeError("OCI repository must be a string")
        x = x.strip()
        if not x:
            raise ValueError("OCI repository must be non-empty when provided")
        return x

    version: Validator = field(default=_validate_version)
    repo: Validator = field(default=_validate_repo)
    manifest: Validator = field(default=_check_boolean)

    @staticmethod
    def container(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        container_tag: str,
        version: str | None,
        repo: str,
        manifest: bool,
        **kwargs: Any
    ) -> None:
        raise OSError(
            "cannot publish a singular container.  Specify an environment scope only."
        )

    @staticmethod
    def image(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        version: str | None,
        repo: str,
        manifest: bool,
        **kwargs: Any
    ) -> None:
        raise OSError(
            "cannot publish a singular image.  Specify an environment scope only."
        )

    @staticmethod
    def environment(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        version: str | None,
        repo: str,
        manifest: bool,
        **kwargs: Any
    ) -> None:
        # get version number from project configuration, and confirm it matches the
        # tagged version if provided
        project_version = env.config.get("version")
        if not isinstance(project_version, str):
            raise OSError(f"missing or invalid project version: {project_version}")
        project_version = project_version.strip()
        if not project_version:
            raise OSError(f"project version must be non-empty: '{project_version}'")
        if version is not None and version != project_version:
            raise OSError(
                f"publish version '{version}' does not match project version "
                f"'{project_version}'"
            )

        # detect host architecture
        arch = podman_cmd(
            ["info", "--format", "{{.Host.Arch}}"],
            capture_output=True
        ).stdout.strip().lower()
        if not arch:
            arch = "unknown"
        else:
            arch = NORMALIZE_ARCH.get(
                arch,
                re.sub(r"[^a-z0-9._-]+", "-", arch).strip("-") or "unknown"
            )

        # phase 1: build, tag, and push all images for this architecture
        if not manifest:
            for image_tag in env.config["images"]:
                image, _ = Build._image(ctx, env=env, image_tag=image_tag, publish=True)
                suffix = f"-{image_tag}" if image_tag else ""
                ref = f"{repo}:{project_version}{suffix}-{arch}"
                podman_cmd(["tag", image.id, ref], capture_output=True)
                podman_cmd(["push", ref], capture_output=True)
            return

        # phase 2: assemble manifest and push to GHCR
        for image_tag in env.config["images"]:
            suffix = f"-{image_tag}" if image_tag else ""
            manifest_ref = f"{repo}:{project_version}{suffix}"
            amd_ref = f"{manifest_ref}-amd64"
            arm_ref = f"{manifest_ref}-arm64"

            # TODO: what are all of these podman commands doing, actually?
            for ref in (amd_ref, arm_ref):
                try:
                    podman_cmd(
                        ["manifest", "inspect", f"docker://{ref}"],
                        capture_output=True,
                    )
                except Exception as err:
                    raise OSError(f"missing source image for manifest: {ref}\n{err}") from err
            try:
                podman_cmd(
                    ["manifest", "rm", manifest_ref],
                    check=False,
                    capture_output=True,
                )
                podman_cmd(["manifest", "create", manifest_ref], capture_output=True)
                podman_cmd(
                    ["manifest", "add", manifest_ref, f"docker://{amd_ref}"],
                    capture_output=True,
                )
                podman_cmd(
                    ["manifest", "add", manifest_ref, f"docker://{arm_ref}"],
                    capture_output=True,
                )
                podman_cmd(
                    ["manifest", "push", "--all", manifest_ref, f"docker://{manifest_ref}"],
                    capture_output=True,
                )
            finally:
                podman_cmd(
                    ["manifest", "rm", manifest_ref],
                    check=False,
                    capture_output=True,
                )

    @staticmethod
    def all(
        ctx: Pipeline.InProgress,
        *,
        version: str | None,
        repo: str,
        manifest: bool,
        **kwargs: Any
    ) -> None:
        raise OSError("cannot publish all environments")


@dataclass
class Enter(_Command):
    """Replace the current process with an interactive shell inside the specified
    container, starting or rebuilding it as necessary.  This is equivalent to `Start`
    followed by a `podman container exec {shell}` command on a single container,
    where `{shell}` is set by the parent environment.

    Code RPC startup is best-effort for this command.  Shell entry always continues,
    and `BERTRAND_CODE_SERVER=0|1` is injected to describe whether in-container
    `bertrand code` is expected to work.
    """
    args: Validator = field(default=_validate_args)
    code_server_available: Validator = field(default=_validate_code_server_available)

    @staticmethod
    def container(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        container_tag: str,
        args: list[str],
        code_server_available: bool,
        **kwargs: Any
    ) -> None:
        if not sys.stdin.isatty() or not sys.stdout.isatty():
            raise CommandError(
                returncode=1,
                cmd=[
                    "bertrand",
                    "enter",
                    f"{env.root}:{image_tag}:{container_tag}",
                    *args
                ],
                stdout="",
                stderr="'bertrand enter' requires both stdin and stdout to be a TTY."
            )

        # start/refresh systemd code service, but do not fail if it is unavailable
        ctx[CODE_SERVER_AVAILABLE] = Listener.start(ctx, env_root=env.root, strict=False)

        # start container if necessary
        Start.container(
            ctx,
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
            **kwargs
        )
        # TODO: env[image] is no longer valid.  Now you have to reference a commit
        # first
        container = env[image_tag, container_tag]
        if container is None:
            raise OSError(
                f"unable to start container '{container_tag}' in image '{image_tag}'"
            )

        # load shell command from normalized config snapshot
        with Config.load(env.root) as config:
            shell_name = config["shell"]
        if not isinstance(shell_name, str):
            raise OSError("'shell' in config snapshot must be a string")
        shell_name = shell_name.strip()
        if not shell_name:
            raise OSError("'shell' in config snapshot cannot be empty")
        shell = SHELLS.get(shell_name)
        if shell is None:
            raise OSError(
                f"unsupported shell in config snapshot: '{shell_name}'\n"
                f"must be one of: {', '.join(SHELLS.keys())}"
            )

        # exec into container with appropriate shell and `code`-related environment variables
        podman_exec([
            "exec",
            "-it",
            "-w", str(WORKTREE_MOUNT),
            "-e", f"{WORKTREE_ENV}={str(env.root)}",
            "-e", f"{CONTAINER_ID_ENV}={container.id}",
            "-e", f"{SOCKET_ENV}={'1' if code_server_available else '0'}",
            container.id,
            *shell,
        ])

    @staticmethod
    def image(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        args: list[str],
        code_server_available: bool,
        **kwargs: Any
    ) -> None:
        Enter.container(
            ctx,
            env=env,
            image_tag=image_tag,
            container_tag=DEFAULT_TAG,
            args=args,
            code_server_available=code_server_available,
            **kwargs
        )

    @staticmethod
    def environment(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        args: list[str],
        code_server_available: bool,
        **kwargs: Any
    ) -> None:
        Enter.container(
            ctx,
            env=env,
            image_tag=DEFAULT_TAG,
            container_tag=DEFAULT_TAG,
            args=args,
            code_server_available=code_server_available,
            **kwargs
        )

    @staticmethod
    def all(
        ctx: Pipeline.InProgress,
        *,
        args: list[str],
        code_server_available: bool,
        **kwargs: Any
    ) -> None:
        raise OSError("must specify a container to enter")


@dataclass
class Code(_Command):
    """Launch a host-side editor by invoking the internal `bertrand code` command
    within a running container context.  This command requires the host RPC service
    to be reachable before it proceeds.
    """
    code_server_available: Validator = field(default=_validate_code_server_available)

    @staticmethod
    def container(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        container_tag: str,
        code_server_available: bool,
        **kwargs: Any
    ) -> None:
        # start/refresh systemd code service
        ctx[CODE_SERVER_AVAILABLE] = Listener.start(ctx, env_root=env.root, strict=True)

        # start container
        Start.container(
            ctx,
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
            **kwargs
        )
        # TODO: env[image] is no longer valid.  Now you have to reference a commit
        # first
        container = env[image_tag, container_tag]
        if container is None:
            raise OSError(
                f"unable to start container '{container_tag}' in image '{image_tag}'"
            )

        # delegate to in-container implementation, which will forward to the host RPC
        # service
        podman_cmd([
            "exec",
            "-i",
            "-w", str(WORKTREE_MOUNT),
            "-e", f"{WORKTREE_ENV}={str(env.root)}",
            "-e", f"{CONTAINER_ID_ENV}={container.id}",
            "-e", f"{SOCKET_ENV}={'1' if code_server_available else '0'}",
            container.id,
            "bertrand", "code",
        ])

    @staticmethod
    def image(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        code_server_available: bool,
        **kwargs: Any
    ) -> None:
        Code.container(
            ctx,
            env=env,
            image_tag=image_tag,
            container_tag=DEFAULT_TAG,
            code_server_available=code_server_available,
            **kwargs
        )

    @staticmethod
    def environment(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        code_server_available: bool,
        **kwargs: Any
    ) -> None:
        Code.container(
            ctx,
            env=env,
            image_tag=DEFAULT_TAG,
            container_tag=DEFAULT_TAG,
            code_server_available=code_server_available,
            **kwargs
        )

    @staticmethod
    def all(
        ctx: Pipeline.InProgress,
        *,
        code_server_available: bool,
        **kwargs: Any
    ) -> None:
        raise OSError("must specify a container to run 'code' in")


async def _cli_containers(
    env: Environment,
    tag: str | None,
    *,
    status: tuple[str, ...] = ("created", "paused", "restarting", "running"),
    timeout: float,
) -> list[ContainerId]:
    if tag is None:
        labels = {ENV_ID_ENV: env.id}
    elif tag not in env.images:
        raise KeyError(f"no image found for tag: '{tag}'")
    else:
        labels = {ENV_ID_ENV: env.id, IMAGE_TAG_ENV: tag}
    return await _podman_ids("container", labels=labels, status=status, timeout=timeout)


async def _cli_images(
    env: Environment,
    tag: str | None,
    *,
    timeout: float,
) -> list[ImageId]:
    if tag is None:
        labels = {ENV_ID_ENV: env.id}
    else:
        labels = {ENV_ID_ENV: env.id, IMAGE_TAG_ENV: tag}
    return await _podman_ids("image", labels=labels, timeout=timeout)


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


async def podman_build(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    quiet: bool,
) -> Image:
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
        Whether to suppress build output from podman.  If true, then nothing will be
        printed to stdout or stderr unless an error occurs.

    Returns
    -------
    Image
        The most recently built image for the specified tag, which may be the same as
        the previously cached image if no changes were detected.

    Raises
    ------
    TypeError
        If the commit's project configuration is missing or malformed.
    OSError
        If the environment hasn't been acquired as a context manager or the image
        fails to build.

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

    async with Environment(worktree, timeout=TIMEOUT) as env:
        config = env._config  # pylint: disable=protected-access
        if config is None:
            raise OSError(f"could not load environment at {worktree}")

        # get config for the commit we are about to build, which may differ from
        # that of the enclosing environment
        image_args = config.image_args(tag)
        container_args = config.container_args(tag)
        if config.pyproject is not None:
            project_name = config.pyproject.project.name
        else:
            raise TypeError("could not determine project name")

        # build candidate image
        candidate = Image.model_construct(
            version=VERSION,
            tag=tag,
            id="",  # corrected from iid file after build
            created=datetime.now(timezone.utc),
            image_args=image_args,
            container_args=container_args,
        )
        image_name = f"{project_name}.{tag}.{env.id[:7]}"
        iid_file = env.worktree / METADATA_TMP / f"{image_name}.iid"
        iid_file.parent.mkdir(parents=True, exist_ok=True)
        await podman_cmd([
            "build",
            "-t", image_name,
            "-f", str(config.path(CONTAINERFILE_RESOURCE)),
            "--iidfile", str(iid_file),
            "--label", f"{BERTRAND_ENV}=1",
            "--label", f"{ENV_ID_ENV}={env.id}",
            "--label", f"{IMAGE_TAG_ENV}={tag}",
            *image_args,
            str(env.worktree)
        ], cwd=env.worktree, capture_output=quiet)
        candidate.id = iid_file.read_text(encoding="utf-8").strip()
        iid_file.unlink(missing_ok=True)
        existing = env.images.get(tag)
        changed = existing is None or existing.id != candidate.id
        try:
            if candidate.inspect() is None:
                raise OSError(
                    f"failed to build image '{tag}' for environment at {env.worktree}"
                )
        except:
            if changed:
                await podman_cmd(
                    ["image", "rm", "-f", candidate.id],
                    check=False,
                    capture_output=quiet
                )
            raise

        # retire existing image in favor of new candidate if they differ
        if changed:
            env.images[tag] = candidate
            if existing is not None:
                env.retired.append(existing)
            return candidate
        assert existing is not None
        return existing


@on_publish(ephemeral=True)
async def podman_publish(ctx: Pipeline.InProgress) -> None:
    Publish()(ctx)


# TODO: the best thing to do for start is probably to allow arbitrary argv to follow
# `bertrand run myproject/branch@workload:tag sleep infinity`, which will override the
# default entry point for the generated container.


async def podman_start(
    worktree: Path,
    workload: str | None,
    tag: str | None,
    *,
    quiet: bool,
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
    quiet : bool
        Whether to suppress output from podman.  If true, then nothing will be printed
        to stdout or stderr unless an error occurs.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")
    if tag is None:
        tag = DEFAULT_TAG

    async with Environment(worktree, timeout=TIMEOUT) as env:
        config = env._config  # pylint: disable=protected-access
        if config is None:
            raise OSError(f"could not load environment at {worktree}")

        # TODO: support cmd argument?

        # build/update image first, then materialize container from it
        image = await podman_build(worktree, workload, tag, quiet=quiet)
        await image.container(env, None, quiet=quiet)


@on_code(ephemeral=True)
async def podman_code(ctx: Pipeline.InProgress) -> None:
    Code()(ctx)


@on_enter(ephemeral=True)
async def podman_enter(ctx: Pipeline.InProgress) -> None:
    Enter()(ctx)


async def podman_stop(
    worktree: Path,
    workload: str | None,
    tag: str | None,
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

    async with Environment(worktree, timeout=deadline - time.time()) as env:
        ids = await _cli_containers(
            env,
            tag,
            status=("running", "restarting", "paused"),
            timeout=deadline - time.time()
        )
        if ids:
            timeout = deadline - time.time()
            await podman_cmd(
                [
                    "container",
                    "stop",
                    "-t", str(int(math.ceil(timeout))),
                    *ids
                ],
                timeout=timeout
            )


async def podman_pause(
    worktree: Path,
    workload: str | None,
    tag: str | None,
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

    async with Environment(worktree, timeout=deadline - time.time()) as env:
        ids = await _cli_containers(
            env,
            tag,
            status=("running", "restarting"),
            timeout=deadline - time.time()
        )
        if ids:
            await podman_cmd(["container", "pause", *ids], timeout=deadline - time.time())


async def podman_resume(
    worktree: Path,
    workload: str | None,
    tag: str | None,
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

    async with Environment(worktree, timeout=deadline - time.time()) as env:
        ids = await _cli_containers(
            env,
            tag,
            status=("paused",),
            timeout=deadline - time.time()
        )
        if ids:
            await podman_cmd(["container", "unpause", *ids], timeout=deadline - time.time())


async def podman_restart(
    worktree: Path,
    workload: str | None,
    tag: str | None,
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

    async with Environment(worktree, timeout=TIMEOUT) as env:
        tags: list[str]
        if tag is None:
            tags = list(env.images)
        else:
            tags = [tag]
        for tag in tags:
            # get all running containers matching the specified tag
            ids = await _cli_containers(
                env,
                tag,
                status=("running", "restarting", "paused"),
                timeout=env.timeout
            )
            if not ids:
                continue  # nothing to restart

            # update image
            updated = await podman_build(
                worktree,
                workload,
                tag,
                quiet=True  # TODO: delete this?
            )

            # stop outdated containers and restart those that were not affected
            to_restart = set(await _podman_ids(
                "container",
                labels={
                    ENV_ID_ENV: env.id,
                    IMAGE_ID_ENV: updated.id,
                    IMAGE_TAG_ENV: tag,
                },
                timeout=env.timeout
            ))
            defer: list[list[str]] = []
            for id in ids:
                if id in to_restart:
                    await podman_cmd(
                        ["container", "restart", "-t", str(int(math.ceil(env.timeout))), id],
                        timeout=env.timeout
                    )
                else:
                    await podman_cmd(
                        ["container", "stop", "-t", str(int(math.ceil(env.timeout))), id],
                        timeout=env.timeout
                    )
                    # TODO: record the command used to start the original container
                    defer.append([])

            # if we have to restart outdated containers, rebuild them from the newest
            # image and start them with the same arguments as before
            for cmd in defer:
                await updated.container(
                    env,
                    cmd,
                    quiet=True  # TODO: delete this and pass a command instead
                )


async def podman_rm(
    worktree: Path,
    workload: str | None,
    tag: str | None,
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

    async with Environment(worktree, timeout=deadline - time.time()) as env:
        if tag is None:
            await env.remove(force=force, timeout=deadline - time.time(), missing_ok=True)
        else:
            image = env.images.pop(tag)
            if image is not None:
                await image.remove(
                    force=force,
                    timeout=deadline - time.time(),
                    missing_ok=True
                )


async def podman_ls(
    worktree: Path,
    workload: str | None,
    tag: str | None,
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
                podman documentation for the available fields for containers and
                images.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")
    format_mode, table_template = _parse_output_format(format, allow_id=True)

    async with Environment(worktree, timeout=deadline - time.time()) as env:
        if format_mode == "id":
            if image:
                ids = await _cli_images(env, tag, timeout=deadline - time.time())
            else:
                ids = await _cli_containers(env, tag, timeout=deadline - time.time())
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

        await podman_cmd(cmd, timeout=deadline - time.time())


async def podman_monitor(
    worktree: Path,
    workload: str | None,
    tag: str | None,
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
                podman documentation for the available fields for containers and
                images.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")
    if interval < 0:
        raise ValueError("interval must be non-negative")
    format_mode, table_template = _parse_output_format(format, allow_id=False)
    if format_mode == "json" and interval:
        raise ValueError("cannot use 'json' and 'interval' together")

    async with Environment(worktree, timeout=deadline - time.time()) as env:
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
            await podman_cmd(cmd, timeout=deadline - time.time())
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
            await podman_cmd(cmd, timeout=deadline - time.time())


async def podman_top(
    worktree: Path,
    workload: str | None,
    tag: str | None,
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

    async with Environment(worktree, timeout=deadline - time.time()) as env:
        ids = await _cli_containers(env, tag, timeout=deadline - time.time())
        for id in ids:
            await podman_cmd(
                ["container", "top", id],
                timeout=deadline - time.time(),
            )
            print()  # delimiter between containers


async def podman_log(
    worktree: Path,
    workload: str | None,
    tag: str | None,
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
        Only show logs since this timestamp.  Should be a string parsable by Podman,
        such as "2024-01-01T00:00:00" or "5m" for 5 minutes ago.  If None, then all
        logs from the beginning of time will be shown.
    until : str | None
        Only show logs until this timestamp.  Should be a string parsable by Podman,
        such as "2024-01-01T00:00:00" or "5m" for 5 minutes ago.  If None, then all
        logs up to the current time will be shown.
    """
    if workload is not None:
        raise NotImplementedError("kubernetes workloads are not yet supported")

    async with Environment(worktree, timeout=deadline - time.time()) as env:
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
            ids = await _cli_containers(env, tag, timeout=deadline - time.time())
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
            tmp = cmd.copy()
            tmp.append(id)
            await podman_cmd(tmp, timeout=deadline - time.time())
            print()  # delimiter between containers
