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
import uuid

from collections.abc import Mapping
from dataclasses import dataclass, asdict, field
from datetime import datetime, timezone
from pathlib import Path
from types import TracebackType
from typing import (
    Annotated,
    Any,
    Callable,
    Literal,
    Sequence,
    TypeAlias,
    TypedDict,
    cast,
    overload
)

from pydantic import (
    AwareDatetime,
    BaseModel,
    BeforeValidator,
    AfterValidator,
    ConfigDict,
    PositiveInt,
    StringConstraints,
    field_validator,
)

from .code import (
    CODE_SERVICE_ENV,
    CODE_SOCKET,
    CONTAINER_SOCKET,
    start_code_service,
)
from .config import (
    MOUNT,
    SHELLS,
    CONTAINER_ID_ENV,
    HOST_ENV,
    Config,
    lock_env,
)
from .pipeline import (
    DelegateUserControllers,
    EnsureSubIDs,
    EnsureUserNamespaces,
    InstallPackage,
    JSONValue,
    JSONView,
    Pipeline,
    atomic,
    detect_package_manager,
    on_init,
    on_ls,
    on_rm,
    on_build,
    on_start,
    on_enter,
    on_code,
    on_run,
    on_stop,
    on_pause,
    on_resume,
    on_restart,
    on_prune,
    on_monitor,
    on_log,
    on_top,
)
from .run import (
    CommandError,
    CompletedProcess,
    Lock,
    User,
    atomic_write_text,
    confirm,
    mkdir_private,
    run,
    sanitize_name,
)
#pylint: disable=redefined-builtin, redefined-outer-name, broad-except


# environment metadata info
VERSION: int = 1
CACHES: str = "/tmp/.cache"
TIMEOUT: int = 30
ENV_DIR_NAME: str = ".bertrand"
ENV_FILE_NAME: str = "env.json"
ENV_REGISTRY_FILE = "env-registry.json"
ENV_REGISTRY_LOCK = "env-registry.lock"


# shared fact names during init/enter pipelines
USER = "user"
UID = "uid"
GID = "gid"
PACKAGE_MANAGER = "package_manager"
DISTRO_ID = "distro_id"
DISTRO_VERSION = "distro_version"
DISTRO_CODENAME = "distro_codename"
CODE_SERVER_AVAILABLE = "code_server_available"


def _env_dir(root: Path) -> Path:
    return root.expanduser().resolve() / ENV_DIR_NAME


def _env_file(root: Path) -> Path:
    return _env_dir(root) / ENV_FILE_NAME


def _env_tmp_dir(env_root: Path) -> Path:
    return _env_dir(env_root) / "tmp"


def _cid_file(env_root: Path, name: str) -> Path:
    return _env_tmp_dir(env_root) / f"{name}.cid"


def _iid_file(env_root: Path, name: str) -> Path:
    return _env_tmp_dir(env_root) / f"{name}.iid"


def _registry_file() -> Path:
    return on_init.state_dir / ENV_REGISTRY_FILE


def _registry_lock() -> Path:
    return on_init.state_dir / ENV_REGISTRY_LOCK


def _init_assume_yes(ctx: Pipeline.InProgress) -> bool:
    raw = ctx.get("yes")
    if raw is None:
        return False
    if isinstance(raw, bool):
        return raw
    raise TypeError(f"invalid 'yes' fact type: {type(raw).__name__}")


def _podman_ready() -> bool:
    """Return True when podman is installed and usable for the current user."""
    if not shutil.which("podman"):
        return False
    result = run(["podman", "info", "--format", "json"], check=False, capture_output=True)
    return result.returncode == 0


def _check_boolean(value: Any) -> bool:
    if not isinstance(value, bool):
        raise ValueError(f"expected a boolean value, got: {value}")
    return value


def _check_absolute_path(value: Any) -> Path:
    p = Path(value)
    if p != p.expanduser().resolve():
        raise ValueError(f"path must be absolute: {value}")
    return p


def _check_list_field(value: Any, field: str) -> list[str]:
    if not isinstance(value, list) or not all(isinstance(x, str) for x in value):
        raise ValueError(f"missing or invalid '{field}' field: {value}")
    return value


def _check_args_field(value: Any) -> list[str]:
    return _check_list_field(value, "args")


def _check_entry_point_field(value: Any) -> list[str]:
    return _check_list_field(value, "entry_point")


def _check_uuid_str(value: str) -> str:
    value = value.strip()
    if not value:
        raise ValueError(f"missing or invalid 'id' field: {value}")
    try:
        uuid.UUID(value)
    except Exception as err:
        raise ValueError(f"'id' must be a valid UUID: {value}") from err
    return value


def _to_utc(value: datetime) -> datetime:
    if value.tzinfo is None or value.tzinfo.utcoffset(value) is None:
        raise ValueError(f"'created' must be a valid ISO timestamp: {value}")
    return value.astimezone(timezone.utc)


AbsolutePath: TypeAlias = Annotated[Path, BeforeValidator(_check_absolute_path)]
HostId: TypeAlias = Annotated[str, BeforeValidator(_check_uuid_str)]
EnvironmentId: TypeAlias = Annotated[str, BeforeValidator(_check_uuid_str)]
ImageId: TypeAlias = Annotated[str, StringConstraints(strip_whitespace=True, min_length=1)]
ContainerId: TypeAlias = ImageId
CreatedAt: TypeAlias = Annotated[AwareDatetime, AfterValidator(_to_utc)]
ArgsList: TypeAlias = Annotated[
    list[Annotated[str, StringConstraints(min_length=1)]],
    BeforeValidator(_check_args_field)
]
EntryPoint: TypeAlias = Annotated[
    list[Annotated[str, StringConstraints(min_length=1)]],
    BeforeValidator(_check_entry_point_field)
]


@atomic
@dataclass(frozen=True)
class PurgeBertrandArtifacts:
    """Clean up Bertrand containers, images, and volumes before uninstalling podman
    itself.
    """
    # pylint: disable=missing-function-docstring, broad-exception-caught, unused-argument

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        return  # no-op; cleanup is handled in undo

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue], force: bool) -> None:
        if not shutil.which("podman"):
            return

        # do the same as rm -f to remove all environments as well as their internal
        # artifacts
        Rm._all(force=force, timeout=TIMEOUT)  # pylint: disable=protected-access

        # clean up any dangling images/containers/volumes that weren't removed by
        # rm -f, which can occur if the environment registry is incomplete and the
        # environment has no containers, preventing us from discovering its mount point
        try:
            containers = _podman_ids("container")
            if containers:
                podman_cmd([
                    "container",
                    "rm",
                    "--depend",
                    "-f",
                    "-i",
                    "-v",
                    "-t", str(TIMEOUT),
                    *containers
                ], check=False)
            images = _podman_ids("image")
            if images:
                podman_cmd(["image", "rm", "-f", "-i", *images], check=False)
            volumes = _podman_ids("volume")
            if volumes:
                podman_cmd(["volume", "rm", "-f", "-i", *volumes], check=False)
        except Exception:
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
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue], force: bool) -> None:
        # Conservative: if host state is unknown or in use, skip uninstall to avoid
        # clobbering user-managed podman resources.
        try:
            containers = _podman_ids("container")
            if containers:
                return
            images = _podman_ids("image")
            if images:
                return
            volumes = _podman_ids("volume")
            if volumes:
                return
            InstallPackage.undo(ctx, payload, force)
        except Exception:
            return


@on_init(requires=[], version=1)
def detect_platform(ctx: Pipeline.InProgress) -> None:
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
def install_container_cli(ctx: Pipeline.InProgress) -> None:
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
        ctx.do(PurgeBertrandArtifacts())
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
    ctx.do(InstallPodman(
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
    ctx.do(PurgeBertrandArtifacts())


@on_init(requires=[install_container_cli], version=1)
def enable_user_namespaces(ctx: Pipeline.InProgress) -> None:
    """Ensure unprivileged user namespaces are enabled on the host system, which are
    required for the rootless container cli.  Prompts are auto-accepted when init is
    run with `--yes`; permission failures still raise.

    Parameters
    ----------
    ctx : Pipeline.InProgress
        The in-flight pipeline context.
    """
    assume_yes = _init_assume_yes(ctx)
    if _podman_ready():
        return

    ctx.do(EnsureUserNamespaces(
        needed=15000,
        prompt=(
            "Rootless containers require unprivileged user namespaces to be enabled on "
            "the host system.  This may require sudo privileges.\n"
            "Do you want to proceed? [y/N] "
        ),
        assume_yes=assume_yes,
    ))


@on_init(requires=[install_container_cli], version=1)
def provision_subids(ctx: Pipeline.InProgress) -> None:
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
    if _podman_ready():
        return

    user = ctx.get(USER)
    if not isinstance(user, str):
        raise OSError(f"Invalid user: {user}")
    ctx.do(EnsureSubIDs(
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


def _systemd_version() -> int | None:
    cp = run(["systemctl", "--version"], check=False, capture_output=True)
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
def delegate_controllers(ctx: Pipeline.InProgress) -> None:
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
    if _podman_ready():
        return

    uid = ctx.get(UID)
    if not isinstance(uid, int):
        raise OSError(f"Invalid UID: {uid}")

    # controller delegation for cgroup v2 to enable rootless resource limits
    root_controllers = _cgroup_v2_controllers()
    if root_controllers is not None:
        required = {"cpu", "io", "memory", "pids"}

        # systemd 244+ is required for cpuset delegation
        systemd_version = _systemd_version()
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
                ctx.do(DelegateUserControllers(
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
def assert_container_cli_ready(ctx: Pipeline.InProgress) -> None:
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
    if _podman_ready():
        return

    assume_yes = _init_assume_yes(ctx)
    cmd = "bertrand init --yes" if assume_yes else "bertrand init"
    raise OSError(
        "podman is installed but not usable for rootless operation after init "
        "bootstrap. Verify host prerequisites (user namespaces, subuid/subgid, and "
        "controller delegation) and retry.  Run `podman info` for diagnostics. "
        f"(last command: `{cmd}`)"
    )


def _read_metadata(
    root: Path,
    *,
    missing_ok: bool = False
) -> Environment.JSON | None:
    env_file = _env_file(root)
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


def _write_metadata(root: Path, metadata: Environment.JSON) -> None:
    atomic_write_text(
        _env_file(root),
        json_parser.dumps(metadata.model_dump(mode="json"), indent=2) + "\n",
        encoding="utf-8",
        private=True
    )


def _discover_environment_mounts() -> list[Path]:
    container_ids = _podman_ids("container")
    if not container_ids:
        return []

    # inspect all containers
    try:
        inspects = json_parser.loads(podman_cmd(
            ["container", "inspect", *container_ids],
            capture_output=True
        ).stdout)
    except Exception:
        return []
    if not isinstance(inspects, list):
        return []

    # retrieve unique bind mounts
    seen: set[Path] = set()
    result: list[Path] = []
    for inspect in inspects:
        if not isinstance(inspect, dict):
            continue
        try:
            mount = Container.mount(inspect)  # type: ignore[arg-type]
        except Exception:
            continue
        if mount is None or mount in seen:
            continue
        seen.add(mount)
        result.append(mount)

    return result


def _check_env(root: Path, env_id: EnvironmentId | None = None) -> Environment.JSON | None:
    try:
        root = root.expanduser().resolve()
        env_file = _env_file(root)
        if env_file.exists() and env_file.is_file():
            metadata = _read_metadata(root)
            if metadata is not None:
                Config.load(root)
                if env_id is None or metadata.id == env_id:
                    return metadata
    except Exception:
        pass
    return None


class EnvironmentRegistry(BaseModel):
    """Serialized registry of environments that have been built on this system.

    A global registry of this form is stored in the `init` pipeline's state directory,
    and allows Bertrand to target all environments on the system for various CLI
    commands, as well as detect relocation of environments, possibly across different
    hosts.

    Attributes
    ----------
    host : HostId
        A UUID tied to the lifetime of the registry.  Every environment's metadata
        will store the host UUID of the registry it was initialized from.  If we
        attempt to insert the environment into another registry with a different
        UUID, then it signifies the environment was sent from another host, and we
        should force a rebuild of all downstream images and containers.
    environments : dict[EnvironmentId, Path]
        A mapping of environment UUIDs to their corresponding root directories on this
        system.  If we attempt to insert an environment with a pre-existing UUID, then
        we can check to see if the previous path is still valid and matches that UUID,
        in which case the environment we are attempting to insert is a copy, and we
        should assign a new UUID to ensure they are treated as separate environments
        with isolated tags.  If the previous path is no longer valid or doesn't match
        the UUID, then the new environment constitutes a move, and we can transfer
        ownership to the new path.
    """
    host: HostId
    environments: dict[EnvironmentId, AbsolutePath]


# NOTE: lock order is always registry > environment, and never the reverse, in order to
# avoid deadlocks.  As a result of this, the `_dump_registry`, `_load_registry`, and
# `_reconcile_registry` methods all assume that the registry lock has already been
# acquired before they can be safely called.


def _dump_registry(registry: EnvironmentRegistry) -> None:
    atomic_write_text(
        _registry_file(),
        json_parser.dumps({
            "host": registry.host,
            "environments": {env_id: str(root) for env_id, root in sorted(
                registry.environments.items(),
                key=lambda item: item[0]
            )}
        }, indent=2) + "\n",
        encoding="utf-8",
        private=True
    )


def _load_registry() -> EnvironmentRegistry:
    # touch a new registry if none exists
    path = _registry_file()
    changed = False
    if not path.exists():
        registry = EnvironmentRegistry(host=uuid.uuid4().hex, environments={})
        changed = True

    # otherwise, try to parse registry JSON
    else:
        try:
            data = json_parser.loads(path.read_text(encoding="utf-8"))
            if not isinstance(data, dict):
                raise ValueError("registry JSON must be an object")
            registry = EnvironmentRegistry.model_validate(data)

        # if the registry is corrupted or otherwise invalid, attempt to rebuild it using
        # active mounts as the source of truth (best-effort)
        except Exception:
            registry = EnvironmentRegistry(
                host=uuid.uuid4().hex,
                environments={}
            )
            for root in _discover_environment_mounts():
                with lock_env(root, timeout=TIMEOUT):
                    env = _check_env(root)
                    if env is None:
                        continue
                registry.environments.setdefault(env.id, root.expanduser().resolve())
            changed = True

        # remove any stale entries
        normalized: dict[EnvironmentId, AbsolutePath] = {}
        for env_id, root in registry.environments.items():
            with lock_env(root, timeout=TIMEOUT):
                env = _check_env(root, env_id=env_id)
                if env is None or normalized.setdefault(env.id, root) != root:
                    changed = True
        registry.environments = normalized

    # write registry back to disk if anything changed
    if changed:
        _dump_registry(registry)
    return registry


@overload
def _reconcile_registry(add: None = ...) -> tuple[None, None, list[Path]]: ...
@overload
def _reconcile_registry(add: Path) -> tuple[Environment.JSON, Config, list[Path]]: ...
def _reconcile_registry(
    add: Path | None = None
) -> tuple[Environment.JSON | None, Config | None, list[Path]]:
    env: Environment.JSON | None = None
    config: Config | None = None
    registry = _load_registry()
    registry_changed = False

    # claim the requested root
    if add is not None:
        add = add.expanduser().resolve()
        with lock_env(add, timeout=TIMEOUT):  # lock environment
            env = _check_env(add)
            env_changed = False
            if env is None:
                env = Environment.JSON(
                    version=VERSION,
                    host=registry.host,
                    id=uuid.uuid4().hex,
                    tags={}
                )
                env_changed = True

            # if the environment being added was initialized from a different host
            # registry, then it signifies a cross-platform relocation.  We handle
            # this by clearing the environment's built tags to force a rebuild of
            # all downstream images and containers on the new host, and then
            # proceed like normal
            if env.host != registry.host:
                env.tags.clear()
                env.host = registry.host
                env_changed = True

            # unconditionally delete any tags that are no longer declared in the
            # environment's config or whose arguments have drifted
            config = Config.load(add)
            with config:
                declared_images = config["images"]
                for image_tag, image in list(env.tags.items()):
                    # check if the image tag is still declared with the same
                    # arguments
                    declared_image = declared_images.get(image_tag)
                    if (
                        declared_image is None or
                        tuple(image.args) != tuple(declared_image["args"])
                    ):
                        image.remove(force=True, timeout=TIMEOUT, missing_ok=True)
                        env.tags.pop(image_tag, None)
                        env_changed = True
                        continue

                    # check if any descendant container tags are no longer
                    # declared, or have mismatched arguments
                    declared_containers = declared_image["containers"]
                    for container_tag, container in list(image.containers.items()):
                        declared_container_args = declared_containers.get(container_tag)
                        if (
                            declared_container_args is None or
                            tuple(container.args) != tuple(declared_container_args)
                        ):
                            container.remove(force=True, timeout=TIMEOUT, missing_ok=True)
                            image.containers.pop(container_tag, None)
                            env_changed = True

            # if the environment is not already registered, insert it directly
            owner = registry.environments.get(env.id)
            if owner is None:
                registry.environments[env.id] = add
                registry_changed = True

            # otherwise, if the root has drifted, then it signals a same-host copy
            # or move
            elif owner != add:
                # if the previous root exists and matches the expected UUID, then
                # the new environment constitutes a copy, and we need to clear its
                # tags and re-key it to guarantee uniqueness
                owner_env = _check_env(owner, env_id=env.id)
                if owner_env is not None:
                    env.tags.clear()
                    env.id = uuid.uuid4().hex
                    while env.id in registry.environments:
                        env.id = uuid.uuid4().hex
                    env_changed = True

                # otherwise, the new environment constitutes a move, and we can
                # transfer ownership by deleting the old containers (but not
                # images), to avoid coupling with the previous path
                for image in env.tags.values():
                    while image.containers:
                        _, container = image.containers.popitem()
                        container.remove(force=True, timeout=TIMEOUT, missing_ok=True)
                        env_changed = True

                # in both cases, we need to update the environment metadata and
                # then the registry to point to the new root
                registry.environments[env.id] = add
                registry_changed = True

            # persist new environment metadata if it changed
            if env_changed:
                _write_metadata(add, env)

    # persist new registry metadata if it changed
    if registry_changed:
        _dump_registry(registry)

    return env, config, list(registry.environments.values())


@on_init(requires=[assert_container_cli_ready], ephemeral=True)
def init_environment(ctx: Pipeline.InProgress) -> None:
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
    root = Path(env).expanduser().resolve()
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
    capabilities: list[str] | None = None
    raw_capabilities = ctx.get("capabilities")
    if raw_capabilities is not None:
        if not isinstance(raw_capabilities, (list, tuple)):
            raise TypeError("capabilities must be a list or tuple of strings")
        capabilities = []
        for cap in raw_capabilities:
            if not isinstance(cap, str):
                raise TypeError(f"capability must be a string: {cap}")
            capabilities.append(cap)

    # apply effective resource config and render templates in environment directory
    cfg = Config.init(root, profile=profile, capabilities=capabilities)
    cfg.apply(timeout=TIMEOUT)
    with cfg:
        cfg.sync()  # synchronize any config-based artifacts


@on_init(requires=[init_environment], ephemeral=True)
def init_repository(ctx: Pipeline.InProgress) -> None:
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

    # initialize repo and make an initial commit with the newly-rendered environment
    stage = "initialize git repository"
    try:
        run(["git", "init", "--quiet"], cwd=root, capture_output=True)
        stage = "stage files for initial commit"
        run(["git", "add", "-A"], cwd=root, capture_output=True)
        stage = "create initial commit"
        run(["git", "commit", "--quiet", "-m", "Initial commit"], cwd=root, capture_output=True)
    except CommandError as err:
        print(f"bertrand: warning: failed to {stage} in {root}\n{err}", file=sys.stderr)


@on_init(requires=[init_repository], ephemeral=True)
def register_environment(ctx: Pipeline.InProgress) -> None:
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
    root = Path(env).expanduser().resolve()

    # add to global environment registry
    with Lock(_registry_lock(), timeout=TIMEOUT):
        _reconcile_registry(root)


def podman_cmd(
    args: list[str],
    *,
    check: bool = True,
    capture_output: bool | None = False,
    input: str | None = None,
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
    return run(
        ["podman", *args],
        check=check,
        capture_output=capture_output,
        input=input,
        cwd=cwd,
        env=env,
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


def _podman_ids(
    mode: Literal["container", "image", "volume"],
    labels: Sequence[str] = (),
    *,
    status: Sequence[str] | None = None,
) -> list[str]:
    # form basic command based on mode
    cmd: list[str] = []
    if mode == "volume":
        cmd.extend(["volume", "ls", "-q", "--filter", "label=BERTRAND=1"])
    else:
        cmd.extend([
            "image" if mode == "image" else "container",
            "ls",
            "-a",
            "-q",
            "--no-trunc",
            "--filter", "label=BERTRAND=1"
        ])

    # append additional labels to filter results
    for label in labels:
        cmd.extend(["--filter", f"label={label}"])

    # parse results, returning an empty/partial list on failure (best-effort)
    out: list[str] = []
    seen: set[str] = set()
    try:
        # return all statuses by default
        if status is None:
            result = podman_cmd(cmd, capture_output=True, check=False)
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
            result = podman_cmd(
                [*cmd, "--filter", f"status={stat}"],
                capture_output=True,
                check=False
            )
            if result.returncode != 0:
                continue
            for raw_id in result.stdout.splitlines():
                container_id = raw_id.strip()
                if not container_id or container_id in seen:
                    continue
                seen.add(container_id)
                out.append(container_id)
    except Exception:
        pass
    return out


def _ensure_volume(name: str, env_id: str, kind: str) -> None:
    try:
        podman_cmd([
            "volume",
            "create",
            "--label", "BERTRAND=1",
            "--label", f"BERTRAND_ENV={env_id}",
            "--label", f"BERTRAND_VOLUME={kind}",
            name,
        ], check=False)
    except Exception:
        pass


def _remove_dangling_volumes(*, force: bool, missing_ok: bool) -> None:
    volumes = list(podman_cmd([
        "volume",
        "ls",
        "-q",
        "--filter", "label=BERTRAND=1",
        "--filter", "dangling=true",
    ], capture_output=True).stdout.splitlines())
    if volumes:
        cmd = ["volume", "rm"]
        if force:
            cmd.append("-f")
        if missing_ok:
            cmd.append("-i")
        podman_cmd([*cmd, *volumes])


class Container(BaseModel):
    """In-memory metadata representing a local Bertrand container, which acts as a
    runtime harness for a compiled image.  An image can have many containers, each
    built with a different runtime configuration, and each container is considered to
    be immutable once created.

    Specific care is taken not to store anything that references the host filesystem or
    container name, in order to allow renaming/relocation of the environment directory.

    Attributes
    ----------
    version : int
        The version number for backwards compatibility.
    id : str
        The unique podman container ID.
    created : datetime
        The ISO timestamp when the container was created.
    args : list[str]
        The original `podman create` arguments used to create the container.
    entry_point : list[str]
        The shell prefix used to run the container.  This is usually detected during
        compilation by looking for a __main__.py file or a C++ module with a main()
        function.
    """
    class State(TypedDict, total=False):
        """Type hint for container state information."""
        Status: Literal[
            "created",
            "restarting",
            "running",
            "removing",
            "paused",
            "exited",
            "dead"
        ]
        Running: bool
        Paused: bool
        Restarting: bool
        OOMKilled: bool
        Dead: bool

    class Mounts(TypedDict, total=False):
        """Type hint for container mount information."""
        Type: Literal["bind", "volume", "tmpfs", "npipe"]
        Source: str
        Destination: str
        RW: bool
        Propagation: Literal["shared", "slave", "private", "rshared", "rslave", "rprivate"]

    class Inspect(TypedDict, total=False):
        """Type hint for container inspect output.

        https://docs.podman.io/en/latest/markdown/podman-container-inspect.1.html#examples
        """
        Id: ContainerId
        Created: CreatedAt
        State: Container.State
        Image: ImageId
        Mounts: list[Container.Mounts]

    model_config = ConfigDict(extra="forbid")
    version: PositiveInt
    id: ContainerId
    created: CreatedAt
    args: ArgsList
    entry_point: EntryPoint

    def remove(self, *, force: bool, timeout: int, missing_ok: bool) -> None:
        """Remove this container via podman.

        Parameters
        ----------
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
            "-t", str(timeout),
        ]
        if force:
            cmd.append("-f")
        if missing_ok:
            cmd.append("-i")
        podman_cmd([*cmd, self.id])

        # remove any now-dangling volumes
        _remove_dangling_volumes(force=force, missing_ok=missing_ok)

    def inspect(self) -> Container.Inspect | None:
        """Invoke podman to inspect this container.

        Returns
        -------
        Container.Inspect | None
            A JSON response from podman or None if the container could not be found.
        """
        result = podman_cmd(["container", "inspect", self.id], check=False, capture_output=True)
        if result.returncode != 0:
            return None
        stdout = result.stdout.strip()
        if not stdout:
            return None
        data = json_parser.loads(stdout)
        return data[0] if data else None

    @staticmethod
    def mount(inspect: Container.Inspect) -> Path | None:
        """Extract the root path of the environment directory mounted to this container
        from its inspection data.

        Parameters
        ----------
        inspect : Container.Inspect
            The output of `Container.inspect()` for the container to query.

        Returns
        -------
        Path | None
            The root path of the environment directory mounted to the container, or
            None if no such mount exists.
        """
        mounts = inspect.get("Mounts") or []
        for m in mounts:
            if m.get("Type") == "bind" and m.get("Destination") == str(MOUNT):
                src = m.get("Source")
                if src:
                    return Path(src).expanduser().resolve()
        return None

    @staticmethod
    def start(inspect: Container.Inspect, check: bool = True) -> None:
        """Start a container if it is not already running.

        Parameters
        ----------
        inspect : Container.Inspect
            The output of `Container.inspect()` for the container to start.
        check : bool, optional
            If True, raise CommandError if the container fails to start.  Default is
            True.

        Raises
        ------
        KeyError
            If the inspection data is missing required fields.
        CommandError
            If the container fails to start and `check` is True.
        """
        state = inspect.get("State")
        if not isinstance(state, dict):
            raise KeyError("invalid container inspect data: missing 'State'")
        if state.get("Running") or state.get("Restarting"):
            return

        id = inspect.get("Id")
        if not isinstance(id, str):
            raise KeyError("invalid container inspect data: missing 'Id'")

        if state.get("Paused"):
            podman_cmd(["container", "unpause", id], check=check)
        else:
            podman_cmd(["container", "start", id], check=check)


class Image(BaseModel):
    """In-memory metadata representing a local Bertrand image, which acts as a compiled
    snapshot of an environment with a particular set of build arguments.  An
    environment can have many images, each built with a different set of Containerfile
    arguments, and each image is considered to be immutable once created.

    Specific care is taken not to store anything that references the host filesystem,
    in order to allow renaming/relocation of the environment directory.

    Attributes
    ----------
    version : int
        The version number for backwards compatibility.
    id : str
        The unique podman image ID.  This is equivalent to the metadata file name in
        `image_dir`.
    created : datetime
        The ISO timestamp when the image was created.
    args : list[str]
        The original `podman build` args used to build the image.
    containers : dict[str, Container]
        A mapping from container tags to their corresponding `Container` metadata
        objects built from this image.
    """
    class Inspect(TypedDict, total=False):
        """Type hint for `podman image inspect` output.

        https://docs.podman.io/en/latest/markdown/podman-image-inspect.1.html#example
        """
        Id: ImageId
        Created: CreatedAt

    model_config = ConfigDict(extra="forbid")
    version: PositiveInt
    id: ImageId
    created: CreatedAt
    args: ArgsList
    containers: dict[str, Container]

    def remove(self, *, force: bool, timeout: int, missing_ok: bool) -> None:
        """Remove this image via podman.  Will also remove all containers built from
        this image.

        Parameters
        ----------
        force : bool
            If True, forcefully remove the image even if it has running containers.
            If False, only remove stopped containers, and then remove the image if no
            containers remain.
        timeout : int
            The maximum time in seconds to wait for running containers to stop before
            forcefully killing them.  -1 indicates an infinite wait.
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
        containers = list(podman_cmd([
            "container",
            "ls",
            "-a",
            "-q",
            "--no-trunc",
            "--filter", f"ancestor={self.id}",
        ], capture_output=True).stdout.splitlines())
        if containers:
            cmd = [
                "container",
                "rm",
                "--depend",  # remove dependent containers
                "-v",  # remove anonymous volumes
                "-t", str(timeout),
            ]
            if force:
                cmd.append("-f")
            if missing_ok:
                cmd.append("-i")
            podman_cmd([*cmd, *containers])

        # remove image
        cmd = ["image", "rm"]
        if force:
            cmd.append("-f")
        if missing_ok:
            cmd.append("-i")
        podman_cmd([*cmd, self.id])

        # remove any now-dangling volumes
        _remove_dangling_volumes(force=force, missing_ok=missing_ok)

    def inspect(self) -> Image.Inspect | None:
        """Invoke podman to inspect this image.

        Returns
        -------
        Image.Inspect | None
            A JSON response from podman or None if the image could not be found.
        """
        result = podman_cmd(["image", "inspect", self.id], check=False, capture_output=True)
        if result.returncode != 0 or not result.stdout:
            return None
        data = json_parser.loads(result.stdout)
        return data[0] if data else None


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
        model_config = ConfigDict(extra="forbid", validate_assignment=True)
        version: PositiveInt
        host: HostId
        id: EnvironmentId
        tags: dict[str, Image]

        @field_validator("tags", mode="after")
        @classmethod
        def _check_tags(cls, tags: dict[str, Image]) -> dict[str, Image]:
            cleaned: dict[str, Image] = {}
            for raw_tag, image in tags.items():
                if not isinstance(raw_tag, str):
                    raise ValueError(
                        f"invalid image tag in environment metadata: {raw_tag}"
                    )
                tag = raw_tag.strip()
                sanitized = sanitize_name(tag)
                if tag != sanitized:
                    raise ValueError(
                        f"invalid characters in image tag '{tag}' (sanitizes to: '{sanitized}')"
                    )
                if tag in cleaned:
                    raise ValueError(f"duplicate image tag in environment metadata: {tag}")
                cleaned[tag] = image
            return cleaned

    root: Path
    _json: JSON
    _config: Config | None
    _lock: Lock
    _entered: int

    def __init__(self, root: Path, timeout: int = TIMEOUT) -> None:
        self.root = root.expanduser().resolve()
        self._json = self.JSON.model_construct(
            version=0,
            host="",
            id="",
            tags={}
        )
        self._config = None
        self._lock = lock_env(self.root, timeout=timeout)
        self._entered = 0

    def __enter__(self) -> Environment:
        # obey registry > environment lock order
        with Lock(_registry_lock(), timeout=self.timeout):
            self._lock.__enter__()
            self._entered += 1
            if self._entered > 1:
                return self  # re-entrant case

            # add to/synchronize the environment registry with on-disk metadata
            try:
                self._json, self._config, _ = _reconcile_registry(self.root)
                self._config.__enter__()
                return self

            except Exception as err:
                self._entered -= 1
                self._config = None
                self._lock.__exit__(type(err), err, getattr(err, "__traceback__", None))
                raise

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc_value: BaseException | None,
        traceback: TracebackType | None
    ) -> None:
        if self._entered < 1:
            raise RuntimeError("environment context manager was not entered")

        try:
            env_dir = _env_dir(self.root)
            if self._entered == 1 and env_dir.exists():
                _write_metadata(self.root, self._json)

        # always release the lock and local context depth
        finally:
            if self._entered == 1 and self._config is not None:
                self._config.__exit__(exc_type, exc_value, traceback)
                self._config = None
            self._entered -= 1
            self._lock.__exit__(exc_type, exc_value, traceback)

    def __hash__(self) -> int:
        return hash(self.root)

    def __eq__(self, other: Any) -> bool:
        if not isinstance(other, Environment):
            return NotImplemented
        return self.root == other.root

    @staticmethod
    def parse(spec: str) -> tuple[str, str, str]:
        """Parse a string of the form `<env_root>[:<image_tag>[:<container_tag>]]` into
        its constituent parts.

        Parameters
        ----------
        spec : str
            The container specification string to parse, which is usually provided from
            the command line or `$ bertrand ls`.

        Returns
        -------
        tuple[str, str, str]
            A tuple containing the environment root path, image tag, and container tag.
            The environment root path is expanded and resolved into an absolute path.
            The image and container tags will be empty strings if not provided.

        Raises
        ------
        OSError
            If the environment path could not be resolved, or either tag is empty or
            contains invalid characters.
        """
        # resolve rightmost tag
        prev, sep, tag1 = spec.rpartition(":")
        if not sep or os.path.sep in tag1:
            return str(Path(spec.strip()).expanduser().resolve()), "", ""
        tag1 = tag1.strip()
        if not tag1:
            raise OSError(f"tag must not be empty: '{spec}'")
        san1 = sanitize_name(tag1)
        if tag1 != san1:
            raise OSError(
                f"tag contains invalid characters: '{tag1}' (sanitizes to: '{san1}')"
            )

        # resolve middle tag
        prev, sep, tag2 = prev.rpartition(":")
        if not sep or os.path.sep in tag2:
            return str(Path(prev.strip()).expanduser().resolve()), san1, ""
        tag2 = tag2.strip()
        if not tag2:
            raise OSError(f"tag must not be empty: '{spec}'")
        san2 = sanitize_name(tag2)
        if tag2 != san2:
            raise OSError(
                f"tag contains invalid characters: '{tag2}' (sanitizes to: '{san2}')"
            )
        return str(Path(prev.strip()).expanduser().resolve()), san2, san1

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
        if (
            os.environ.get("BERTRAND", "0") == "1" and
            "BERTRAND_ENV" in os.environ and
            "BERTRAND_IMAGE" in os.environ and
            "BERTRAND_CONTAINER" in os.environ
        ):
            return Environment(root=MOUNT)
        return None

    @property
    def timeout(self) -> int:
        """
        Returns
        -------
        int
            The maximum time in seconds to wait for acquiring the environment lock.
        """
        return math.ceil(self._lock.timeout)

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
    def tags(self) -> dict[str, Image]:
        """
        Returns
        -------
        dict[str, Image]
            A mapping from image tags to metadata objects representing their built
            counterparts.  Each image contains a nested mapping from container tags to
            downstream container objects.  Note that only built images and containers
            are represented in this mapping.  To get all declared images and
            containers, use the `config` property to access the full configuration
            snapshot instead.
        """
        return self._json.tags

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

    @overload
    def __getitem__(self, args: tuple[str, str]) -> Container | None: ...
    @overload
    def __getitem__(self, args: str | tuple[str, None]) -> Image | None: ...
    def __getitem__(self, args: str | tuple[str, str | None]) -> Image | Container | None:
        """Locate an image or container by tag within this environment, and load its
        metadata without modifying the environment or clearing stale references.

        Parameters
        ----------
        image : str
            The image tag to search for.
        container : str | None, optional
            An optional container tag to search for within the indicated image.  If
            None (the default), only the image will be searched for.

        Returns
        -------
        Image | Container | None
            The corresponding image or container metadata object, or None if no
            matching tag could be found.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager.
        TypeError
            If the arguments are not of the expected types.
        """
        if self._entered < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if isinstance(args, str):
            image_tag = args
            container_tag = None
        elif isinstance(args, tuple) and len(args) == 2:
            image_tag, container_tag = args
            if (
                not isinstance(image_tag, str) or
                (container_tag is not None and not isinstance(container_tag, str))
            ):
                raise TypeError("invalid arguments to __getitem__")
        else:
            raise TypeError("invalid arguments to __getitem__")

        # search for image
        image = self.tags.get(image_tag)
        if image is None or container_tag is None:
            return image

        # search for container
        return image.containers.get(container_tag)

    def remove(self, *, force: bool, timeout: int, missing_ok: bool) -> None:
        """Remove this environment via podman, including all descendant images and
        containers.  Note that this does not delete the referencing tags, meaning the
        images and containers will be rebuilt again if referenced in the future.

        Parameters
        ----------
        force : bool
            If True, remove images even if they have dependent containers, removing
            the containers as well.  If False, only remove images that have no
            dependent containers.
        timeout : int
            The maximum time in seconds to wait for dependent containers to stop before
            forcefully killing them.  -1 indicates an infinite wait.
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
        containers = _podman_ids("container", labels=[f"BERTRAND_ENV={self.id}"])
        images = _podman_ids("image", labels=[f"BERTRAND_ENV={self.id}"])

        # remove containers first
        if containers:
            cmd = [
                "container",
                "rm",
                "--depend",  # remove dependents
                "-v",  # remove anonymous volumes
                "-t", str(timeout),
            ]
            if force:
                cmd.append("-f")
            if missing_ok:
                cmd.append("-i")
            podman_cmd([*cmd, *containers])

        # remove images
        if images:
            cmd = ["image", "rm"]
            if force:
                cmd.append("-f")
            if missing_ok:
                cmd.append("-i")
            podman_cmd([*cmd, *images])

        # remove any now-dangling volumes
        _remove_dangling_volumes(force=force, missing_ok=missing_ok)

        # remove environment metadata and lock
        try:
            shutil.rmtree(_env_dir(self.root), ignore_errors=True)
        except Exception:
            pass


Validator: TypeAlias = Callable[[JSONView], Any]


def _all_environments() -> list[Path]:
    try:
        with Lock(_registry_lock(), timeout=TIMEOUT):
            _, _, environments = _reconcile_registry()
            return environments
    except OSError as err:
        print(
            f"bertrand: warning: failed to load global environment registry: {err}",
            file=sys.stderr
        )
        return []


def _validate_args(x: JSONView) -> list[str]:
    if x is None:
        return []
    if isinstance(x, tuple) and all(isinstance(i, str) for i in x):
        return list(cast(tuple[str, ...], x))
    raise TypeError("args must be a sequence of strings")


def _validate_code_server_available(x: JSONView) -> bool:
    if x is None:
        return False
    if isinstance(x, bool):
        return x
    raise TypeError(f"invalid '{CODE_SERVER_AVAILABLE}' fact type: {type(x).__name__}")


# pylint: disable=missing-function-docstring, missing-param-doc
# pylint: disable=missing-return-doc, unused-argument, protected-access


@dataclass
class _Command:

    @staticmethod
    def _validate_env(x: JSONView) -> Path | None:
        if x is None:
            return None
        if not isinstance(x, str):
            raise TypeError("environment path must be a string")
        x = x.strip()
        if not x:
            return None
        return Path(x).expanduser().resolve()

    @staticmethod
    def _validate_image(x: JSONView) -> str:
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
    def _validate_container(x: JSONView) -> str:
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
class Build(_Command):
    """Incrementally build Bertrand images and materialize declared containers within
    an environment, scoping to specific images/containers if desired.  This command
    does not start containers, and all build/create arguments are resolved from
    `[tool.bertrand]` in `pyproject.toml`.
    """
    dist: Validator = field(default=_check_boolean)

    @staticmethod
    def _image(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        dist: bool = False,
    ) -> tuple[Image, bool]:
        changed = False
        declared = env.config["images"].get(image_tag)
        if declared is None:
            raise KeyError(
                f"undeclared image tag '{image_tag}' for environment at {env.root}."
            )
        args = list(declared["args"])

        # build candidate image
        # NOTE: podman + the OCI build system will automatically reuse cached layers as
        # long as none of the inputs have changed (including the contents of the
        # environment directory, excluding patterns in .containerignore).  Therefore,
        # we can build unconditionally, and check whether the ID has changed afterwards
        # to detect whether a rebuild was necessary.
        candidate = Image.model_construct(
            version=VERSION,
            id="",  # corrected after build
            created=datetime.now(timezone.utc),
            args=args,
            containers={},
        )
        image_name = f"{sanitize_name(env.root.name)}.{image_tag}.{env.id[:13]}"
        build_args: list[str] = []
        for arg in args:
            build_args.append(f"--build-arg={arg}")
            build_args.append(f"--env={arg}")
        iid_file = _iid_file(env.root, image_name)
        try:
            iid_file.parent.mkdir(parents=True, exist_ok=True)
            podman_cmd([
                "build",
                "-t", image_name,
                "-f", str(env.config.path("containerfile")),
                "--iidfile", str(iid_file),
                "--label", "BERTRAND=1",
                "--label", f"BERTRAND_ENV={env.id}",
                "--label", f"BERTRAND_IMAGE={image_tag}",
                *build_args,
                str(env.root)
            ], cwd=env.root, capture_output=dist)
            candidate.id = iid_file.read_text(encoding="utf-8").strip()
            try:
                # confirm that the image was actually built and is reachable
                if candidate.inspect() is None:
                    raise OSError(
                        f"failed to build image '{image_tag}' for environment at {env.root}"
                    )

                # rebuild all downstream containers if the image changed
                existing = env.tags.get(image_tag)
                changed = existing is None or candidate.id != existing.id
                if changed and not dist:
                    for container_tag in declared["containers"]:
                        Build._container(
                            ctx,
                            env=env,
                            image_tag=image_tag,
                            image=candidate,  # build using the temp image
                            container_tag=container_tag,
                        )

                # otherwise, preserve original timestamp and containers
                elif existing is not None:
                    candidate.created = existing.created
                    candidate.containers = existing.containers

            except Exception:
                cmd = ["container", "rm", "-f"]
                cmd.extend(c.id for c in candidate.containers.values())
                podman_cmd(cmd, check=False)
                podman_cmd(["image", "rm", "-f", candidate.id], check=False)
                raise

            # swap and delete old image if it exists
            env.tags[image_tag] = candidate
            if changed and existing is not None:
                existing.remove(force=True, timeout=env.timeout, missing_ok=True)
            return candidate, changed

        finally:
            iid_file.unlink(missing_ok=True)

    @staticmethod
    def _container(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        image: Image | None,
        container_tag: str,
    ) -> tuple[Container, bool]:
        declared_image = env.config["images"].get(image_tag)
        if declared_image is None:
            raise KeyError(
                f"undeclared image tag '{image_tag}' for environment at {env.root}"
            )
        args = declared_image["containers"].get(container_tag)
        if args is None:
            raise KeyError(
                f"undeclared container tag '{container_tag}' for image '{image_tag}' in "
                f"environment at {env.root}"
            )
        args = list(args)

        # attempt to build the parent image first
        if image is None:
            image, changed = Build._image(
                ctx,
                env=env,
                image_tag=image_tag,
                dist=False,
            )
            if changed:  # implicitly rebuilt container along with image
                return image.containers[container_tag], True

        # reuse container if args match and container has not been relocated
        existing = image.containers.get(container_tag)
        if existing is not None and list(existing.args) == args:
            inspect = existing.inspect()
            if inspect is not None:
                mount = Container.mount(inspect)
                try:
                    if mount is not None and os.path.samefile(mount, env.root):
                        return existing, False  # no change
                except OSError:
                    pass

        # build new container
        container = Container.model_construct(
            version=VERSION,
            id="",  # corrected after create
            created=datetime.now(timezone.utc),
            args=args,
            entry_point=[],  # default entry point
        )
        cid_file = _cid_file(env.root, f"create-{uuid.uuid4().hex}")
        cache_prefix = f"bertrand-{env.id[:13]}"
        uv_volume = f"{cache_prefix}-uv"
        bertrand_volume = f"{cache_prefix}-bertrand"
        ccache_volume = f"{cache_prefix}-ccache"
        conan_volume = f"{cache_prefix}-conan"
        _ensure_volume(uv_volume, env.id, "uv")
        _ensure_volume(bertrand_volume, env.id, "bertrand")
        _ensure_volume(ccache_volume, env.id, "ccache")
        _ensure_volume(conan_volume, env.id, "conan")
        mkdir_private(CODE_SOCKET.parent)
        try:
            cid_file.parent.mkdir(parents=True, exist_ok=True)
            podman_cmd([
                "create",
                "--init",
                "--cidfile", str(cid_file),

                # labels for podman-level lookup
                "--label", "BERTRAND=1",
                "--label", f"BERTRAND_ENV={env.id}",
                "--label", f"BERTRAND_IMAGE={image_tag}",
                "--label", f"BERTRAND_CONTAINER={container_tag}",

                # mount environment directory
                "-v", f"{str(env.root)}:{str(MOUNT)}",
                "--mount",
                (
                    "type=bind,"
                    f"src={str(CODE_SOCKET.parent)},"
                    f"dst={str(CONTAINER_SOCKET.parent)},"
                    "ro=true"
                ),

                # persistent caches for incremental builds
                "--mount", f"type=volume,src={uv_volume},dst={CACHES}/uv",
                "--mount", f"type=volume,src={bertrand_volume},dst={CACHES}/bertrand",
                "--mount", f"type=volume,src={ccache_volume},dst={CACHES}/ccache",
                "--mount", f"type=volume,src={conan_volume},dst=/opt/conan",

                # environment variables for Bertrand runtime
                "-e", "BERTRAND=1",
                "-e", f"BERTRAND_ENV={env.id}",
                "-e", f"BERTRAND_IMAGE={image_tag}",
                "-e", f"BERTRAND_CONTAINER={container_tag}",

                # any additional user-specified args
                *args,
                image.id,
                "sleep", "infinity",
            ])
            container.id = cid_file.read_text(encoding="utf-8").strip()
            try:
                # confirm that the container was actually built and is reachable
                if container.inspect() is None:
                    raise OSError(
                        f"failed to create container '{container_tag}' for image "
                        f"'{image_tag}' in environment at {env.root}"
                    )
            except Exception:
                podman_cmd(["container", "rm", "-f", container.id], check=False)
                raise

            # delete old container if it exists
            image.containers[container_tag] = container
            if existing is not None:
                existing.remove(force=True, timeout=env.timeout, missing_ok=True)
            return container, True  # container was rebuilt

        finally:
            cid_file.unlink(missing_ok=True)

    @staticmethod
    def _print_dist(env: Environment, image: str | None) -> None:
        if image is None:
            print(json_parser.dumps(
                {k: v.id for k, v in sorted(env.tags.items(), key=lambda x: x[0])},
                separators=(",", ":")
            ))
        else:
            print(json_parser.dumps(
                {image: env.tags[image].id} if image in env.tags else {},
                separators=(",", ":")
            ))

    @staticmethod
    def container(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        container_tag: str,
        dist: bool,
        **kwargs: Any
    ) -> None:
        if dist:
            raise OSError(
                "cannot materialize a container with --dist enabled.  Specify an "
                "environment or image scope only."
            )
        Build._container(
            ctx,
            env=env,
            image_tag=image_tag,
            image=None,  # build or reuse image as needed
            container_tag=container_tag,
        )

    @staticmethod
    def image(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        dist: bool,
        **kwargs: Any
    ) -> None:
        image, changed = Build._image(
            ctx,
            env=env,
            image_tag=image_tag,
            dist=dist,
        )

        # if we didn't rebuild the image itself, make sure to rebuild all descendant
        # containers in order to enforce proper CLI scope
        if not dist and not changed:
            for container_tag in env.config["images"][image_tag]["containers"]:
                Build._container(
                    ctx,
                    env=env,
                    image_tag=image_tag,
                    image=image,  # reuse built image
                    container_tag=container_tag,
                )

        # emit image json
        if dist:
            Build._print_dist(env, image_tag)

    @staticmethod
    def environment(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        dist: bool = False,
        **kwargs: Any
    ) -> None:
        for image_tag in env.config["images"]:
            image, changed = Build._image(
                ctx,
                env=env,
                image_tag=image_tag,
                dist=dist,
            )

            # if we didn't rebuild the image itself, make sure to rebuild all descendant
            # containers in order to enforce proper CLI scope
            if not dist and not changed:
                for container_tag in env.config["images"][image_tag]["containers"]:
                    Build._container(
                        ctx,
                        env=env,
                        image_tag=image_tag,
                        image=image,  # reuse built image
                        container_tag=container_tag,
                    )

        # emit image json
        if dist:
            Build._print_dist(env, None)

    @staticmethod
    def all(
        ctx: Pipeline.InProgress,
        **kwargs: Any
    ) -> None:
        raise OSError("cannot build all environments")


@dataclass
class Start(_Command):
    """Start Bertrand containers within an environment, scoping to specific images and
    containers if desired.  This is equivalent to `Build` followed by a
    `podman container start` command on all referenced containers.
    """

    @staticmethod
    def container(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        container_tag: str,
        **kwargs: Any
    ) -> None:
        Build.container(
            ctx,
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
            dist=False,
            **kwargs,
        )
        container = env[image_tag, container_tag]
        if container is None:
            raise OSError(
                f"unable to build container '{container_tag}' in image '{image_tag}'"
            )
        inspect = container.inspect()
        if inspect is None:
            container.remove(force=True, timeout=env.timeout, missing_ok=True)
            raise OSError(
                f"failed to build container '{container_tag}' for image '{image_tag}' "
                f"in environment at {env.root}"
            )
        Container.start(inspect)

    @staticmethod
    def image(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        **kwargs: Any
    ) -> None:
        Build.image(ctx, env=env, image_tag=image_tag, dist=False, **kwargs)
        image = env[image_tag]
        if image is None:
            raise OSError(
                f"unable to build image '{image_tag}' in environment at {env.root}"
            )
        for container_tag, container in image.containers.items():
            inspect = container.inspect()
            if inspect is None:
                container.remove(force=True, timeout=env.timeout, missing_ok=True)
                raise OSError(
                    f"failed to build container '{container_tag}' for image '{image_tag}' "
                    f"in environment at {env.root}"
                )
            Container.start(inspect)

    @staticmethod
    def environment(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        **kwargs: Any
    ) -> None:
        for image_tag in env.config["images"]:
            Start.image(ctx, env=env, image_tag=image_tag, **kwargs)

    @staticmethod
    def all(
        ctx: Pipeline.InProgress,
        **kwargs: Any
    ) -> None:
        if confirm(
            "This will start all Bertrand containers on this system.  This may take "
            "a long time depending on the number and complexity of the environments.\n"
            "Are you sure you want to continue? [y/N] "
        ):
            for env_path in _all_environments():
                try:
                    with Environment(env_path) as env:
                        Start.environment(ctx, env=env)
                except Exception as err:
                    print(err, file=sys.stderr)


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
        ctx[CODE_SERVER_AVAILABLE] = start_code_service(ctx, env_root=env.root, strict=False)

        # start container if necessary
        Start.container(
            ctx,
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
            **kwargs
        )
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
            "-w", str(MOUNT),
            "-e", f"{HOST_ENV}={str(env.root)}",
            "-e", f"{CONTAINER_ID_ENV}={container.id}",
            "-e", f"{CODE_SERVICE_ENV}={'1' if code_server_available else '0'}",
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
            container_tag="",  # default container
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
            image_tag="",  # default image
            container_tag="",  # default container
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
        ctx[CODE_SERVER_AVAILABLE] = start_code_service(ctx, env_root=env.root, strict=True)

        # start container
        Start.container(
            ctx,
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
            **kwargs
        )
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
            "-w", str(MOUNT),
            "-e", f"{HOST_ENV}={str(env.root)}",
            "-e", f"{CONTAINER_ID_ENV}={container.id}",
            "-e", f"{CODE_SERVICE_ENV}={'1' if code_server_available else '0'}",
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
            container_tag="",  # default container
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
            image_tag="",  # default image
            container_tag="",  # default container
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


@dataclass
class Run(_Command):
    """Run an arbitrary command inside a container's context, starting or rebuilding it
    as necessary.  This is equivalent to `Start` followed by a `podman container exec`
    command on a single container, forwarding any arguments to the `exec` portion.
    """
    args: Validator = field(default=_validate_args)

    @staticmethod
    def container(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        container_tag: str,
        args: list[str],
        **kwargs: Any
    ) -> None:
        Start.container(
            ctx,
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
            **kwargs
        )
        container = env[image_tag, container_tag]
        if container is None:
            raise OSError(
                f"unable to start container '{container_tag}' in image '{image_tag}'"
            )

        # always run in interactive mode, but only add a TTY if we're currently attached
        # to one
        cmd = ["-i"]
        if (
            sys.stdin.isatty() and
            sys.stdout.isatty() and
            sys.stderr.isatty() and
            os.environ.get("TERM", "") not in ("", "dumb")
        ):
            cmd.append("-t")
        podman_cmd([
            "exec",
            *cmd,
            "-w", str(MOUNT),
            container.id,
            *args
        ])

    @staticmethod
    def image(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        args: list[str],
        **kwargs: Any
    ) -> None:
        Run.container(
            ctx,
            env=env,
            image_tag=image_tag,
            container_tag="",  # default container
            args=args,
            **kwargs
        )

    @staticmethod
    def environment(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        args: list[str],
        **kwargs: Any
    ) -> None:
        Run.container(
            ctx,
            env=env,
            image_tag="",  # default image
            container_tag="",  # default container
            args=args,
            **kwargs
        )

    @staticmethod
    def all(
        ctx: Pipeline.InProgress,
        *,
        args: list[str],
        **kwargs: Any
    ) -> None:
        raise OSError("must specify a container to run a command in")


@dataclass
class Stop(_Command):
    """Stop running Bertrand containers within an environment, scoping to specific images
    and containers if desired.
    """

    @staticmethod
    def _validate_timeout(x: JSONView) -> int:
        if x is None:
            return TIMEOUT
        if not isinstance(x, int):
            raise TypeError("timeout must be an integer")
        if x < -1:
            raise ValueError("timeout must be non-negative or -1 for no timeout")
        return x

    timeout: Validator = field(default=_validate_timeout)

    @staticmethod
    def _batch(labels: list[str], timeout: int) -> None:
        ids = _podman_ids("container", labels, status=("running", "restarting", "paused"))
        if ids:
            podman_cmd(["container", "stop", "-t", str(timeout), *ids], check=False)

    @staticmethod
    def container(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        container_tag: str,
        timeout: int,
        **kwargs: Any
    ) -> None:
        image = env.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")

        container = image.containers.get(container_tag)
        if container is None:
            raise KeyError(f"no container found for tag: '{container_tag}'")

        inspect = container.inspect()
        if inspect is None:
            image.containers.pop(container_tag, None)
        else:
            state = inspect.get("State")
            if not isinstance(state, dict):
                raise OSError(f"invalid container state for container '{container_tag}'")
            if state.get("Running") or state.get("Restarting") or state.get("Paused"):
                id = inspect.get("Id")
                if not isinstance(id, str):
                    raise OSError(f"invalid container ID for container '{container_tag}'")
                podman_cmd(["container", "stop", id, "-t", str(timeout)])

    @staticmethod
    def image(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        timeout: int,
        **kwargs: Any
    ) -> None:
        image = env.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        image_inspect = image.inspect()
        if image_inspect is None:
            raise OSError(f"failed to inspect image '{image_tag}'")
        Stop._batch([f"BERTRAND_ENV={env.id}", f"BERTRAND_IMAGE={image_tag}"], timeout)

    @staticmethod
    def environment(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        timeout: int,
        **kwargs: Any
    ) -> None:
        Stop._batch([f"BERTRAND_ENV={env.id}"], timeout)

    @staticmethod
    def all(
        ctx: Pipeline.InProgress,
        *,
        timeout: int,
        **kwargs: Any
    ) -> None:
        if confirm(
            "This will stop all running Bertrand containers on this system.\n"
            "Are you sure you want to continue? [y/N] "
        ):
            Stop._batch([], timeout)


@dataclass
class Pause(_Command):
    """Pause running Bertrand containers within an environment, scoping to specific
    images and containers if desired.
    """

    @staticmethod
    def _batch(labels: list[str]) -> None:
        ids = _podman_ids("container", labels, status=("running", "restarting"))
        if ids:
            podman_cmd(["container", "pause", *ids], check=False)

    @staticmethod
    def container(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        container_tag: str,
        **kwargs: Any
    ) -> None:
        image = env.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        container = image.containers.get(container_tag)
        if container is None:
            raise KeyError(f"no container found for tag: '{container_tag}'")
        inspect = container.inspect()
        if inspect is None:
            image.containers.pop(container_tag, None)
        else:
            state = inspect.get("State")
            if not isinstance(state, dict):
                raise OSError(f"invalid container state for container '{container_tag}'")
            if state.get("Running") or state.get("Restarting"):
                id = inspect.get("Id")
                if not isinstance(id, str):
                    raise OSError(f"invalid container ID for container '{container_tag}'")
                podman_cmd(["container", "pause", id])

    @staticmethod
    def image(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        **kwargs: Any
    ) -> None:
        image = env.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        Pause._batch([f"BERTRAND_ENV={env.id}", f"BERTRAND_IMAGE={image_tag}"])

    @staticmethod
    def environment(ctx: Pipeline.InProgress, *, env: Environment, **kwargs: Any) -> None:
        Pause._batch([f"BERTRAND_ENV={env.id}"])

    @staticmethod
    def all(ctx: Pipeline.InProgress, **kwargs: Any) -> None:
        if confirm(
            "This will pause all running Bertrand containers on this system.\n"
            "Are you sure you want to continue? [y/N] "
        ):
            Pause._batch([])


@dataclass
class Resume(_Command):
    """Resume paused Bertrand containers within an environment, scoping to specific
    images and containers if desired.
    """

    @staticmethod
    def _batch(labels: list[str]) -> None:
        ids = _podman_ids("container", labels, status=("paused",))
        if ids:
            podman_cmd(["container", "unpause", *ids], check=False)

    @staticmethod
    def container(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        container_tag: str,
        **kwargs: Any
    ) -> None:
        image = env.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        container = image.containers.get(container_tag)
        if container is None:
            raise KeyError(f"no container found for tag: '{container_tag}'")
        inspect = container.inspect()
        if inspect is None:
            image.containers.pop(container_tag, None)
        else:
            state = inspect.get("State")
            if not isinstance(state, dict):
                raise OSError(f"invalid container state for container '{container_tag}'")
            if state.get("Paused"):
                id = inspect.get("Id")
                if not isinstance(id, str):
                    raise OSError(f"invalid container ID for container '{container_tag}'")
                podman_cmd(["container", "unpause", id])

    @staticmethod
    def image(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        **kwargs: Any
    ) -> None:
        image = env.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        Resume._batch([f"BERTRAND_ENV={env.id}", f"BERTRAND_IMAGE={image_tag}"])

    @staticmethod
    def environment(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        **kwargs: Any
    ) -> None:
        Resume._batch([f"BERTRAND_ENV={env.id}"])

    @staticmethod
    def all(
        ctx: Pipeline.InProgress,
        **kwargs: Any
    ) -> None:
        if confirm(
            "This will resume all paused Bertrand containers on this system.\n"
            "Are you sure you want to continue? [y/N] "
        ):
            Resume._batch([])


@dataclass
class Restart(_Command):
    """Restart running or paused Bertrand containers within an environment, scoping to
    specific images and containers if desired.  If an image or container is out of
    date, then it will be rebuilt before restarting.
    """

    @staticmethod
    def _validate_timeout(x: JSONView) -> int:
        if x is None:
            return TIMEOUT
        if not isinstance(x, int):
            raise TypeError("timeout must be an integer")
        if x < -1:
            raise ValueError("timeout must be non-negative or -1 for no timeout")
        return x

    timeout: Validator = field(default=_validate_timeout)

    @staticmethod
    def container(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        container_tag: str,
        timeout: int,
        **kwargs: Any
    ) -> None:
        image = env.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        container = image.containers.get(container_tag)
        if container is None:
            raise KeyError(f"no container found for tag: '{container_tag}'")

        # detect whether the container is currently running
        inspect = container.inspect()
        if inspect is None:
            running = False
        else:
            state = inspect.get("State")
            if not isinstance(state, dict):
                raise OSError(f"invalid container state for container '{container_tag}'")
            running = bool(
                state.get("Running") or
                state.get("Restarting") or
                state.get("Paused")
            )

        # possibly rebuild the parent image and all downstream containers
        Build.image(ctx, env=env, image_tag=image_tag, dist=False, **kwargs)
        image = env[image_tag]
        if image is None:
            raise OSError(f"unable to build image '{image_tag}'")

        # if the container was previously running, restart it
        container = image.containers.get(container_tag)  # may have drifted
        if container is None:
            raise KeyError(f"no container found for tag: '{container_tag}' after rebuild")
        inspect = container.inspect()
        if inspect is None:
            image.containers.pop(container_tag, None)
        elif running:
            state = inspect.get("State")
            if not isinstance(state, dict):
                raise OSError(f"invalid container state for container '{container_tag}'")
            if state.get("Status") == "created":  # newly-rebuilt
                Container.start(inspect)
            elif state.get("Running") or state.get("Paused"):
                id = inspect.get("Id")
                if not isinstance(id, str):
                    raise OSError(f"invalid container ID for container '{container_tag}'")
                podman_cmd(["container", "restart", id, "-t", str(timeout)])

    @staticmethod
    def image(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        timeout: int,
        **kwargs: Any
    ) -> None:
        image = env.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")

        # detect whether any containers are currently running
        running: set[str] = set()
        for container_tag, c in image.containers.items():
            inspect = c.inspect()
            if inspect is not None:
                state = inspect.get("State")
                if not isinstance(state, dict):
                    raise OSError(
                        f"invalid container state for container '{container_tag}' in image "
                        f"'{image_tag}'"
                    )
                if state.get("Running") or state.get("Restarting") or state.get("Paused"):
                    running.add(container_tag)

        # possibly rebuild the image and all downstream containers
        Build.image(ctx, env=env, image_tag=image_tag, dist=False, **kwargs)
        image = env[image_tag]
        if image is None:
            raise OSError(f"unable to build image '{image_tag}'")

        # restart any previously-running containers
        for container_tag in running:
            container = image.containers.get(container_tag)  # may have drifted
            if container is None:
                continue  # container was removed during rebuild, skip it
            inspect = container.inspect()
            if inspect is None:
                continue  # container was removed during rebuild, skip it

            state = inspect.get("State")
            if not isinstance(state, dict):
                raise OSError(
                    f"invalid container state for container '{container_tag}' in image "
                    f"'{image_tag}'"
                )
            if state.get("Status") == "created":  # newly-rebuilt
                Container.start(inspect)
            elif state.get("Running") or state.get("Paused"):
                id = inspect.get("Id")
                if not isinstance(id, str):
                    raise OSError(f"invalid container ID for container '{container_tag}'")
                podman_cmd(["container", "restart", id, "-t", str(timeout)])

    @staticmethod
    def environment(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        timeout: int,
        **kwargs: Any
    ) -> None:
        for image_tag in env.tags:
            Restart.image(ctx, env=env, image_tag=image_tag, timeout=timeout, **kwargs)

    @staticmethod
    def all(
        ctx: Pipeline.InProgress,
        *,
        timeout: int,
        **kwargs: Any
    ) -> None:
        if confirm(
            "This will restart all running Bertrand containers on this system.  This may "
            "take a long time depending on the number and complexity of the environments.\n"
            "Are you sure you want to continue? [y/N] "
        ):
            for env_path in _all_environments():
                try:
                    with Environment(env_path) as env:
                        Restart.environment(ctx, env=env, timeout=timeout, **kwargs)
                except Exception as err:
                    print(err, file=sys.stderr)


@dataclass
class Prune(_Command):
    """Remove all stopped Bertrand containers within an environment, scoping to
    specific images and containers if desired.

    This command only deletes containers, never images or anything on the host
    filesystem.  Any containers that are removed will be rebuilt the next time they are
    started.
    """

    @staticmethod
    def container(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        container_tag: str,
        **kwargs: Any
    ) -> None:
        image = env.tags.get(image_tag)
        if image is None:
            return None
        container = image.containers.get(container_tag)
        if container is None:
            return None
        inspect = container.inspect()
        if inspect is not None:
            state = inspect.get("State")
            if not isinstance(state, dict):
                raise OSError(f"invalid container state for container '{container_tag}'")
            if (
                not state.get("Running") and
                not state.get("Restarting") and
                not state.get("Paused")
            ):
                container.remove(force=True, timeout=env.timeout, missing_ok=True)
                image.containers.pop(container_tag, None)

    # NOTE: do NOT delete dangling images.  Purge is a container-level operation, and
    # deleting images would cause the user to explicitly rebuild them, providing their
    # exact arguments once again.

    @staticmethod
    def image(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        **kwargs: Any
    ) -> None:
        image = env.tags.get(image_tag)
        if image is None:
            return None

        # remove any stopped containers for this image
        for container_tag, container in list(image.containers.items()):
            inspect = container.inspect()
            if inspect is not None:
                state = inspect.get("State")
                if not isinstance(state, dict):
                    raise OSError(
                        f"invalid container state for container '{container_tag}' in image "
                        f"'{image_tag}'"
                    )
                if (
                    not state.get("Running") and
                    not state.get("Restarting") and
                    not state.get("Paused")
                ):
                    container.remove(force=True, timeout=env.timeout, missing_ok=True)
                    image.containers.pop(container_tag, None)

    @staticmethod
    def environment(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        **kwargs: Any
    ) -> None:
        for image_tag in env.tags:
            Prune.image(ctx, env=env, image_tag=image_tag, **kwargs)

    @staticmethod
    def all(
        ctx: Pipeline.InProgress,
        **kwargs: Any
    ) -> None:
        if confirm(
            "This will remove all stopped Bertrand containers on this system.\n"
            "Are you sure you want to continue? [y/N] "
        ):
            for env_path in _all_environments():
                try:
                    with Environment(env_path) as env:
                        Prune.environment(ctx, env=env, **kwargs)
                except Exception as err:
                    print(err, file=sys.stderr)


@dataclass
class Rm(_Command):
    """Delete Bertrand entities on the system, scoping to images and containers within
    an environment if desired.

    This command only deletes images and containers, and never alters the host
    filesystem.  The environment directory may be safely deleted after invoking this
    command.
    """

    @staticmethod
    def _validate_timeout(x: JSONView) -> int:
        if x is None:
            return TIMEOUT
        if not isinstance(x, int):
            raise TypeError("timeout must be an integer")
        if x < -1:
            raise ValueError("timeout must be non-negative or -1 for no timeout")
        return x

    force: Validator = field(default=_check_boolean)
    timeout: Validator = field(default=_validate_timeout)

    @staticmethod
    def container(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        container_tag: str,
        force: bool,
        timeout: int,
        **kwargs: Any
    ) -> None:
        i = env.tags.get(image_tag)
        if i is None:
            return
        c = i.containers.get(container_tag)
        if c is None:
            return
        c.remove(force=force, timeout=timeout, missing_ok=True)
        i.containers.pop(container_tag)

    @staticmethod
    def image(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        force: bool,
        timeout: int,
        **kwargs: Any
    ) -> None:
        i = env.tags.get(image_tag)
        if i is None:
            return
        i.remove(force=force, timeout=timeout, missing_ok=True)
        env.tags.pop(image_tag)

    @staticmethod
    def environment(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        force: bool,
        timeout: int,
        **kwargs: Any
    ) -> None:
        env.remove(force=force, timeout=timeout, missing_ok=True)
        env.tags.clear()

    @staticmethod
    def _all(
        *,
        force: bool,
        timeout: int,
        **kwargs: Any
    ) -> None:
        for env_path in _all_environments():
            try:
                with Environment(env_path) as env:
                    env.remove(force=force, timeout=timeout, missing_ok=True)
                    env.tags.clear()
            except Exception as err:
                print(err, file=sys.stderr)

    @staticmethod
    def all(
        ctx: Pipeline.InProgress,
        *,
        force: bool,
        timeout: int,
        **kwargs: Any
    ) -> None:
        if confirm(
            "This will remove all Bertrand images, containers, and volumes on this "
            "system.  This action cannot be undone.\n"
            "Are you sure you want to continue? [y/N] "
        ):
            Rm._all(force=force, timeout=timeout, **kwargs)


@dataclass
class Ls(_Command):
    """Gather status information for all containers in a Bertrand environment, scoping
    to specific images and containers if desired.

    If `parse` is True, then the output will be parsed and returned as a list of JSON
    dictionaries, one per container.  Otherwise, all information will be printed to
    stdout in a human-readable table format, and nothing will be returned by this
    function.
    """

    class JSON(TypedDict):
        """Type hint for the json output of `podman container ls`."""
        ID: str
        Names: str
        Image: str
        Labels: str
        CreatedAt: str
        Size: str
        Mounts: str
        Ports: str
        Networks: str
        State: Literal["created", "running", "paused", "restarting", "exited", "removing", "dead"]
        Status: str
        Command: str
        RunningFor: str

    json: Validator = field(default=_check_boolean)
    images: Validator = field(default=_check_boolean)
    running: Validator = field(default=_check_boolean)
    stopped: Validator = field(default=_check_boolean)

    @staticmethod
    def _format_images(
        labels: list[str],
        *,
        json: bool,
        running: bool,
        stopped: bool,
    ) -> list[Ls.JSON] | None:
        cmd = ["image", "ls", "-a", *labels]

        # filter by running/stopped status of descendant containers
        if running and not stopped:
            cmd.extend(["--filter", "containers=true"])
        if stopped and not running:
            cmd.extend(["--filter", "containers=false"])

        # parse JSON
        if json:
            cmd.append("--no-trunc")
            cmd.append("--format=json")
            result = podman_cmd(cmd, capture_output=True)
            out = json_parser.loads(result.stdout)
            if not isinstance(out, list):
                out = [out]
            return out

        # print table
        cmd.append(
            "--format=table {{.Names}}\t{{.CreatedAt}}\t{{.Containers}}\t"
            "{{.ReadOnly}}\t{{.Size}}\t{{.History}}"
        )
        podman_cmd(cmd)
        return None

    @staticmethod
    def _format_containers(
        labels: list[str],
        *,
        json: bool,
        running: bool,
        stopped: bool,
    ) -> list[Ls.JSON] | None:
        cmd = ["container", "ls", "-a", "--size", *labels]

        # filter by running/stopped status
        if running and not stopped:
            cmd.extend([
                "--filter",
                "status=running",
                "--filter",
                "status=paused",
                "--filter",
                "status=restarting",
            ])
        if stopped and not running:
            cmd.extend([
                "--filter",
                "status=created",
                "--filter",
                "status=removing",
                "--filter",
                "status=exited",
                "--filter",
                "status=dead",
            ])

        # parse JSON
        if json:
            cmd.append("--no-trunc")
            cmd.append("--format=json")
            result = podman_cmd(cmd, capture_output=True)
            out = json_parser.loads(result.stdout)
            if not isinstance(out, list):
                out = [out]
            return out

        # print table
        cmd.append(
            "--format=table {{.Names}}\t{{.CreatedAt}}\t{{.State}}\t{{.Command}}\t"
            "{{.RunningFor}}\t{{.Status}}\t{{.Restarts}}\t{{.Size}}\t{{.Mounts}}\t"
            "{{.Networks}}\t{{.Ports}}"
        )
        podman_cmd(cmd)
        return None

    @staticmethod
    def container(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        container_tag: str,
        json: bool,
        images: bool,
        running: bool,
        stopped: bool,
        **kwargs: Any
    ) -> None:
        image = env.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        container = image.containers.get(container_tag)
        if container is None:
            raise KeyError(f"no container found for tag: '{container_tag}'")

        # if we're listing images, then we want to show the parent image of this
        # container
        if images:
            out = Ls._format_images(
                [
                    "--filter", "label=BERTRAND=1",
                    "--filter", f"label=BERTRAND_ENV={env.id}",
                    "--filter", f"label=BERTRAND_IMAGE={image_tag}",
                ],
                json=json,
                running=running,
                stopped=stopped,
            )
        else:
            out = Ls._format_containers(
                [
                    "--filter", "label=BERTRAND=1",
                    "--filter", f"label=BERTRAND_ENV={env.id}",
                    "--filter", f"label=BERTRAND_IMAGE={image_tag}",
                    "--filter", f"label=BERTRAND_CONTAINER={container_tag}",
                ],
                json=json,
                running=running,
                stopped=stopped,
            )
        if json:
            print(json_parser.dumps(out, indent=2))

    @staticmethod
    def image(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        json: bool,
        images: bool,
        running: bool,
        stopped: bool,
        **kwargs: Any
    ) -> None:
        image = env.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        labels = [
            "--filter", "label=BERTRAND=1",
            "--filter", f"label=BERTRAND_ENV={env.id}",
            "--filter", f"label=BERTRAND_IMAGE={image_tag}",
        ]
        if images:
            out = Ls._format_images(
                labels,
                json=json,
                running=running,
                stopped=stopped,
            )
        else:
            out = Ls._format_containers(
                labels,
                json=json,
                running=running,
                stopped=stopped,
            )
        if json:
            print(json_parser.dumps(out, indent=2))

    @staticmethod
    def environment(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        json: bool,
        images: bool,
        running: bool,
        stopped: bool,
        **kwargs: Any
    ) -> None:
        labels = [
            "--filter", "label=BERTRAND=1",
            "--filter", f"label=BERTRAND_ENV={env.id}",
        ]
        if images:
            out = Ls._format_images(
                labels,
                json=json,
                running=running,
                stopped=stopped,
            )
        else:
            out = Ls._format_containers(
                labels,
                json=json,
                running=running,
                stopped=stopped,
            )
        if json:
            print(json_parser.dumps(out, indent=2))

    @staticmethod
    def all(
        ctx: Pipeline.InProgress,
        *,
        json: bool,
        images: bool,
        running: bool,
        stopped: bool,
        **kwargs: Any
    ) -> None:
        labels = ["--filter", "label=BERTRAND=1"]
        if images:
            out = Ls._format_images(
                labels,
                json=json,
                running=running,
                stopped=stopped,
            )
        else:
            out = Ls._format_containers(
                labels,
                json=json,
                running=running,
                stopped=stopped,
            )
        if json:
            print(json_parser.dumps(out, indent=2))


@dataclass
class Monitor(_Command):
    """Gather resource utilization statistics for all containers in a Bertrand
    environment, scoping to specific images and containers if desired.

    If `json` is True, then the output will be parsed and returned as a list of JSON
    dictionaries, one per container.  Otherwise, all information will be printed to
    stdout in a human-readable table format, and nothing will be returned by this
    function.  `json` is incompatible with `interval`, which continuously updates the
    printed output in a streaming format if set to a non-zero value.
    """

    class JSON(TypedDict):
        """Type hint for the json output of `podman stats`."""
        Container: str
        ID: str
        Name: str
        CPUPerc: str
        MemPerc: str
        MemUsage: str
        NetIO: str
        BlockIO: str
        PIDs: str

    @staticmethod
    def _validate_interval(x: JSONView) -> int:
        if x is None:
            return 0
        if not isinstance(x, int):
            raise TypeError("interval must be an integer")
        if x < 0:
            raise ValueError("interval must be non-negative")
        return x

    json: Validator = field(default=_check_boolean)
    interval: Validator = field(default=_validate_interval)

    @staticmethod
    def _format(
        labels: list[str],
        *,
        json: bool,
        interval: int
    ) -> list[Monitor.JSON] | None:
        if json and interval:
            raise ValueError("cannot use 'json' and 'interval' together")

        # first, get the container ids for all labels
        ids = _podman_ids("container", labels)
        if not ids:
            return [] if json else None

        # build stats command with appropriate flags
        cmd = ["container", "stats"]
        if not interval:
            cmd.append("--no-stream")
        else:
            cmd.append(f"--interval={interval}")

        # parse JSON
        if json:
            cmd.append("--no-trunc")
            cmd.append("--format=json")
            cmd.extend(ids)
            result = podman_cmd(cmd, capture_output=True)
            out = json_parser.loads(result.stdout)
            if not isinstance(out, list):
                return [out]
            return out

        # print table
        cmd.append(
            "--format=table {{.Name}}\t{{.AVGCPU}}\t{{.CPUPerc}}\t{{.PIDs}}\t"
            "{{.MemUsage}}\t{{.NetIO}}\t{{.BlockIO}}"
        )
        cmd.extend(ids)
        podman_cmd(cmd)
        return None

    @staticmethod
    def container(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        container_tag: str,
        json: bool,
        interval: int,
        **kwargs: Any
    ) -> None:
        image = env.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        container = image.containers.get(container_tag)
        if container is None:
            raise KeyError(f"no container found for tag: '{container_tag}'")
        out = Monitor._format([
            "--filter", "label=BERTRAND=1",
            "--filter", f"label=BERTRAND_ENV={env.id}",
            "--filter", f"label=BERTRAND_IMAGE={image_tag}",
            "--filter", f"label=BERTRAND_CONTAINER={container_tag}",
        ], json=json, interval=interval)
        if json:
            print(json_parser.dumps(out, indent=2))

    @staticmethod
    def image(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        json: bool,
        interval: int,
        **kwargs: Any
    ) -> None:
        image = env.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        out = Monitor._format([
            "--filter", "label=BERTRAND=1",
            "--filter", f"label=BERTRAND_ENV={env.id}",
            "--filter", f"label=BERTRAND_IMAGE={image_tag}",
        ], json=json, interval=interval)
        if json:
            print(json_parser.dumps(out, indent=2))

    @staticmethod
    def environment(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        json: bool,
        interval: int,
        **kwargs: Any
    ) -> None:
        out = Monitor._format([
            "--filter", "label=BERTRAND=1",
            "--filter", f"label=BERTRAND_ENV={env.id}",
        ], json=json, interval=interval)
        if json:
            print(json_parser.dumps(out, indent=2))

    @staticmethod
    def all(
        ctx: Pipeline.InProgress,
        *,
        json: bool,
        interval: int,
        **kwargs: Any
    ) -> None:
        out = Monitor._format([
            "--filter", "label=BERTRAND=1",
        ], json=json, interval=interval)
        if json:
            print(json_parser.dumps(out, indent=2))


@dataclass
class Top(_Command):
    """Display the running processes for a specific container in a Bertrand
    environment.

    Note that this command does not scope to images or environments, and always prints
    to stdout in a human-readable table format.
    """

    @staticmethod
    def container(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        container_tag: str,
        **kwargs: Any
    ) -> None:
        image = env.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        container = image.containers.get(container_tag)
        if container is None:
            raise KeyError(f"no container found for tag: '{container_tag}'")
        podman_cmd([
            "container",
            "top",
            container.id,
        ])

    @staticmethod
    def image(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        **kwargs: Any
    ) -> None:
        Top.container(
            ctx,
            env=env,
            image_tag=image_tag,
            container_tag="",  # default container
            **kwargs
        )

    @staticmethod
    def environment(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        **kwargs: Any
    ) -> None:
        Top.container(
            ctx,
            env=env,
            image_tag="",  # default image
            container_tag="",  # default container
            **kwargs
        )

    @staticmethod
    def all(
        ctx: Pipeline.InProgress,
        **kwargs: Any
    ) -> None:
        raise OSError("must specify a container to view processes for")


@dataclass
class Log(_Command):
    """View the logs for a specific image or container in a Bertrand environment.
    Note that this command does not scope to environments, and always prints to stdout
    in a human-readable format.
    """

    @staticmethod
    def _validate_time(x: JSONView) -> str | None:
        if x is None:
            return None
        if not isinstance(x, str):
            raise TypeError("timestamp must be a string")
        x = x.strip()
        return x or None

    images: Validator = field(default=_check_boolean)
    since: Validator = field(default=_validate_time)
    until: Validator = field(default=_validate_time)

    @staticmethod
    def _format_container(
        container: Container,
        since: str | None,
        until: str | None,
    ) -> None:
        cmd = [
            "container",
            "logs",
            "--color",
            "--follow",
            "--names",
            "--timestamps",
            container.id,
        ]
        if since is not None:
            cmd.extend(["--since", since])
        if until is not None:
            cmd.extend(["--until", until])
        podman_cmd(cmd)

    @staticmethod
    def _format_image(image: Image) -> None:
        podman_cmd([
            "image",
            "history",
            "--human",
            (
                "--format=table {{.CreatedAt}}\t{{.CreatedSince}}\t{{.CreatedBy}}\t"
                "{{.Size}}\t{{.Comment}}"
            ),
            image.id,
        ])

    @staticmethod
    def container(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        container_tag: str,
        images: bool,
        since: str | None,
        until: str | None,
        **kwargs: Any
    ) -> None:
        image = env.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        if images:
            Log._format_image(image)
        else:
            container = image.containers.get(container_tag)
            if container is None:
                raise KeyError(f"no container found for tag: '{container_tag}'")
            Log._format_container(container, since=since, until=until)

    @staticmethod
    def image(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        image_tag: str,
        images: bool,
        since: str | None,
        until: str | None,
        **kwargs: Any
    ) -> None:
        image = env.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        if images:
            Log._format_image(image)
        else:
            container = image.containers.get("")
            if container is None:
                raise KeyError("no container found for tag: ''")
            Log._format_container(container, since=since, until=until)

    @staticmethod
    def environment(
        ctx: Pipeline.InProgress,
        *,
        env: Environment,
        images: bool,
        since: str | None,
        until: str | None,
        **kwargs: Any
    ) -> None:
        return Log.image(
            ctx,
            env=env,
            image_tag="",  # default image
            images=images,
            since=since,
            until=until,
            **kwargs
        )

    @staticmethod
    def all(
        ctx: Pipeline.InProgress,
        *,
        images: bool,
        since: str | None,
        until: str | None,
        **kwargs: Any
    ) -> None:
        raise OSError("must specify an image or container to view logs for")


@on_build(ephemeral=True)
def podman_build(ctx: Pipeline.InProgress) -> None:
    Build()(ctx)


@on_start(ephemeral=True)
def podman_start(ctx: Pipeline.InProgress) -> None:
    Start()(ctx)


@on_code(ephemeral=True)
def podman_code(ctx: Pipeline.InProgress) -> None:
    Code()(ctx)


@on_enter(ephemeral=True)
def podman_enter(ctx: Pipeline.InProgress) -> None:
    Enter()(ctx)


@on_run(ephemeral=True)
def podman_run(ctx: Pipeline.InProgress) -> None:
    Run()(ctx)


@on_stop(ephemeral=True)
def podman_stop(ctx: Pipeline.InProgress) -> None:
    Stop()(ctx)


@on_pause(ephemeral=True)
def podman_pause(ctx: Pipeline.InProgress) -> None:
    Pause()(ctx)


@on_resume(ephemeral=True)
def podman_resume(ctx: Pipeline.InProgress) -> None:
    Resume()(ctx)


@on_restart(ephemeral=True)
def podman_restart(ctx: Pipeline.InProgress) -> None:
    Restart()(ctx)


@on_prune(ephemeral=True)
def podman_prune(ctx: Pipeline.InProgress) -> None:
    Prune()(ctx)


@on_rm(ephemeral=True)
def podman_rm(ctx: Pipeline.InProgress) -> None:
    Rm()(ctx)


@on_ls(ephemeral=True)
def podman_ls(ctx: Pipeline.InProgress) -> None:
    Ls()(ctx)


@on_monitor(ephemeral=True)
def podman_monitor(ctx: Pipeline.InProgress) -> None:
    Monitor()(ctx)


@on_top(ephemeral=True)
def podman_top(ctx: Pipeline.InProgress) -> None:
    Top()(ctx)


@on_log(ephemeral=True)
def podman_log(ctx: Pipeline.InProgress) -> None:
    Log()(ctx)
