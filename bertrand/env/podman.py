"""Install and run a rootless OCI container engine (podman) to orchestrate Bertrand's
CLI.
"""
from __future__ import annotations

import json
import os
import platform
import re
import shlex
import shutil
import sys
import uuid

from datetime import datetime, timezone
from pathlib import Path
from resource import getpagesize
from types import TracebackType
from typing import Annotated, Iterable, Literal, TypeAlias, TypedDict, overload

from pydantic import (
    AwareDatetime,
    BaseModel,
    BeforeValidator,
    AfterValidator,
    ConfigDict,
    PositiveInt,
    StringConstraints,
    ValidationError,
    model_validator,
)

from .package import (
    InstallPackage,
    detect_package_manager
)
from .pipeline import Pipeline, on_init
from .run import (
    CommandError,
    CompletedProcess,
    LockDir,
    User,
    atomic_write_text,
    confirm,
    mkdir_private,
    run,
)
from .systemd import (
    DelegateUserControllers,
)
from .user import EnsureSubIDs, EnsureUserNamespaces
from .version import __version__

#pylint: disable=redefined-builtin, redefined-outer-name, broad-except


############################
####    INSTALLATION    ####
############################


# shared fact names
USER = "user"
UID = "uid"
GID = "gid"
PACKAGE_MANAGER = "package_manager"
DISTRO_ID = "distro_id"
DISTRO_VERSION = "distro_version"
DISTRO_CODENAME = "distro_codename"


@on_init(requires=[], ephemeral=False)
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


@on_init(requires=[detect_platform], ephemeral=False)
def install_container_cli(ctx: Pipeline.InProgress) -> None:
    """Install the base packages required for the container backend via the detected
    package manager.  This will prompt the user for confirmation before

    Parameters
    ----------
    ctx : Pipeline.InProgress
        The in-flight pipeline context.

    Raises
    ------
    OSError
        If `podman` is not found and installation is declined by the user.
    """
    # check if podman CLI is already present
    cli = shutil.which("podman")
    if cli:
        return

    # prompt to install dependencies
    package_manager = ctx.get(PACKAGE_MANAGER)
    if not isinstance(package_manager, str):
        raise OSError(f"Invalid package manager: {package_manager}")
    if not confirm(
        "Bertrand requires 'podman' to manage rootless containers.  Would you like to "
        f"install it now using {package_manager} (requires sudo).\n"
        "[y/N] ",
        assume_yes=False,
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
    else:
        raise OSError(f"Unknown package manager: '{package_manager}'")
    ctx.do(InstallPackage(
        manager=package_manager,
        packages=packages,
        refresh=True
    ))

    # verify installation and record facts for later steps
    cli = shutil.which("podman")
    if not cli:
        raise OSError(
            "Installation completed, but 'podman' is still not found.  Please "
            "investigate the issue and ensure the required packages are installed."
        )


@on_init(requires=[install_container_cli], ephemeral=False)
def enable_user_namespaces(ctx: Pipeline.InProgress) -> None:
    """Ensure unprivileged user namespaces are enabled on the host system, which are
    required for the rootless container cli.

    Parameters
    ----------
    ctx : Pipeline.InProgress
        The in-flight pipeline context.
    """
    ctx.do(EnsureUserNamespaces(
        needed=15000,
        prompt=(
            "Rootless containers require unprivileged user namespaces to be enabled on "
            "the host system.  This may require sudo privileges.\n"
            "Do you want to proceed? [y/N] "
        )
    ))


@on_init(requires=[install_container_cli], ephemeral=False)
def provision_subids(ctx: Pipeline.InProgress) -> None:
    """Ensure subordinate UID/GID ranges are allocated for the host user in
    /etc/subuid and /etc/subgid, which are required for rootless Podman operation.

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
        )
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


@on_init(requires=[provision_subids, enable_user_namespaces], ephemeral=False)
def delegate_controllers(ctx: Pipeline.InProgress) -> None:
    """Configure systemd controller delegation for the rootless container CLI, if not
    already configured.  This allows the container CLI to manage resource limits on
    cgroup v2 hosts.

    Parameters
    ----------
    ctx : Pipeline.InProgress
        The in-flight pipeline context.

    Raises
    ------
    OSError
        If systemd is not found, or if elevation is required but not available.
    """
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


DOCKER_ENV_VARS = (
    "DOCKER_CONTEXT",
    "DOCKER_HOST",
    "DOCKER_TLS_VERIFY",
    "DOCKER_CERT_PATH",
    "DOCKER_MACHINE_NAME",
    "DOCKER_CONFIG",
    "DOCKER_BUILDKIT",
)


def _podman_env(ctx: Pipeline | Pipeline.InProgress) -> dict[str, str]:
    env = os.environ.copy()
    uid = ctx.get(UID)
    if not isinstance(uid, int):
        raise OSError(f"Invalid UID: {uid}")

    # set up state directories
    data = ctx.state_dir / "container_data"
    config = ctx.state_dir / "container_config"
    mkdir_private(data)
    mkdir_private(config)

    # isolate podman configuration under pipeline state
    env["XDG_DATA_HOME"] = str(data)
    env["XDG_CONFIG_HOME"] = str(config)
    env.setdefault("XDG_RUNTIME_DIR", f"/run/user/{uid}")
    for k in DOCKER_ENV_VARS:
        env.pop(k, None)
    return env


def podman_cmd(
    args: list[str],
    *,
    check: bool = True,
    capture_output: bool | None = False,
    input: str | None = None,
    cwd: Path | None = None
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
        env=_podman_env(on_init),
    )


def podman_exec(args: list[str]) -> None:
    """Run a podman command by replacing the current process.  This is intended for
    interactive use cases like `bertrand enter` where we want to drop the user into a
    shell inside the container.

    Parameters
    ----------
    args : list[str]
        The podman command arguments (excluding the 'podman' executable).

    Raises
    ------
    OSError
        If execution fails.
    """
    os.execvpe("podman", ["podman", *args], _podman_env(on_init))


###################
####    CLI    ####
###################


VERSION: int = 1
MOUNT: str = "/env"
TMP: str = "/tmp"
TIMEOUT: int = 30
SHELLS: dict[str, tuple[str, ...]] = {
    "bash": ("bash", "-l"),
}
EDITORS: dict[str, tuple[str, ...]] = {
    "vscode": ("code",),
}


def _env_dir(env_root: Path) -> Path:
    return env_root / ".bertrand"


def _env_tmp_dir(env_root: Path) -> Path:
    return _env_dir(env_root) / "tmp"


def _cid_file(env_root: Path, name: str) -> Path:
    return _env_tmp_dir(env_root) / f"{name}.cid"


def _iid_file(env_root: Path, name: str) -> Path:
    return _env_tmp_dir(env_root) / f"{name}.iid"


def _env_file(env_root: Path) -> Path:
    return _env_dir(env_root) / "env.json"


def _container_file(env_root: Path) -> Path:
    return env_root / "Containerfile"


def _sanitize_name(name: str) -> str:
    out = []
    for char in name:
        if char.isalnum() or char in "._":
            out.append(char)
        else:
            out.append("_")
    return "".join(out).strip("_")


def _normalize_args(args: Iterable[str]) -> list[str]:
    out: list[str] = []
    for s in args:
        if any(c.isspace() for c in s):
            out.extend(shlex.split(s))
        else:
            out.append(s)
    return out


def _check_list_field(value: object, field: str) -> list[str]:
    if not isinstance(value, list) or not all(isinstance(x, str) for x in value):
        raise ValueError(f"missing or invalid '{field}' field: {value}")
    return _normalize_args(value)


def _check_args_field(value: object) -> list[str]:
    return _check_list_field(value, "args")


def _check_entry_point_field(value: object) -> list[str]:
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


def _check_shell(value: str) -> str:
    if value not in SHELLS:
        raise ValueError(f"unsupported shell: {value}")
    return value


def _check_code(value: str) -> str:
    if value not in EDITORS:
        raise ValueError(f"unsupported code command: {value}")
    return value


def _to_utc(value: datetime) -> datetime:
    if value.tzinfo is None or value.tzinfo.utcoffset(value) is None:
        raise ValueError(f"'created' must be a valid ISO timestamp: {value}")
    return value.astimezone(timezone.utc)


ContainerId: TypeAlias = Annotated[str, StringConstraints(strip_whitespace=True, min_length=1)]
ImageId: TypeAlias = ContainerId
EnvironmentId: TypeAlias = Annotated[str, BeforeValidator(_check_uuid_str)]
CreatedAt: TypeAlias = Annotated[AwareDatetime, AfterValidator(_to_utc)]
ArgsList: TypeAlias = Annotated[
    list[Annotated[str, StringConstraints(min_length=1)]],
    BeforeValidator(_check_args_field)
]
EntryPoint: TypeAlias = Annotated[
    list[Annotated[str, StringConstraints(min_length=1)]],
    BeforeValidator(_check_entry_point_field)
]


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

    def inspect(self) -> Container.Inspect | None:
        """Invoke podman to inspect this container.

        Returns
        -------
        Container.Inspect | None
            A JSON response from podman or None if the container could not be found.
        """
        result = podman_cmd(["container", "inspect", self.id], check=False, capture_output=True)
        if result.returncode != 0 or not result.stdout:
            return None
        data = json.loads(result.stdout)
        return data[0] if data else None

    @staticmethod
    def relocated(env_root: Path, inspect: Container.Inspect) -> bool:
        """Detect whether a container's mounted environment path has drifted from the
        environment's true root directory.

        Parameters
        ----------
        env_root : Path
            An absolute host path to the environment directory.
        inspect : Container.Inspect
            The output of `Container.inspect()` for the container to query.

        Returns
        -------
        bool
            True if the container's mounted environment path differs from `env_root`,
            False otherwise.
        """
        mount = Container.mount(inspect)
        try:
            return mount is None or not os.path.samefile(mount, env_root)
        except OSError:
            return True

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
            if m.get("Type") == "bind" and m.get("Destination") == MOUNT:
                src = m.get("Source")
                if src:
                    return Path(src).expanduser().resolve()
        return None

    @staticmethod
    def new(inspect: Container.Inspect) -> bool:
        """Check whether a container has been newly created (i.e. has never been
        started).

        Parameters
        ----------
        inspect : Container.Inspect
            The output of `Container.inspect()` for the container to query.

        Returns
        -------
        bool
            True if the container is newly created, False otherwise.
        """
        return inspect["State"]["Status"] == "created"

    @staticmethod
    def running(inspect: Container.Inspect) -> bool:
        """Check whether a container is currently running.

        Parameters
        ----------
        inspect : Container.Inspect
            The output of `Container.inspect()` for the container to query.

        Returns
        -------
        bool
            True if the container is currently running, False otherwise.
        """
        state = inspect["State"]
        return state["Running"] or state["Restarting"]

    @staticmethod
    def paused(inspect: Container.Inspect) -> bool:
        """Check whether a container is currently paused.

        Parameters
        ----------
        inspect : Container.Inspect
            The output of `Container.inspect()` for the container to query.

        Returns
        -------
        bool
            True if the container is currently paused, False otherwise.
        """
        return inspect["State"]["Paused"]

    @staticmethod
    def stopped(inspect: Container.Inspect) -> bool:
        """Check whether a container is currently stopped.

        Parameters
        ----------
        inspect : Container.Inspect
            The output of `Container.inspect()` for the container to query.

        Returns
        -------
        bool
            True if the container is currently stopped, False otherwise.
        """
        state = inspect["State"]
        return not (state["Running"] or state["Restarting"] or state["Paused"])

    @staticmethod
    def start(inspect: Container.Inspect) -> None:
        """Start a container if it is not already running.

        Parameters
        ----------
        inspect : Container.Inspect
            The output of `Container.inspect()` for the container to start.
        """
        state = inspect["State"]
        if state["Running"] or state["Restarting"]:
            return
        if state["Paused"]:
            podman_cmd(["container", "unpause", inspect["Id"]], check=False)
        else:
            podman_cmd(["container", "start", inspect["Id"]], check=False)

    @staticmethod
    def stop(inspect: Container.Inspect, timeout: int = TIMEOUT) -> None:
        """Stop a container if it is currently running.

        Parameters
        ----------
        inspect : Container.Inspect
            The output of `Container.inspect()` for the container to stop.
        timeout : int, optional
            The maximum time in seconds to wait for the container to stop before
            forcefully killing it.  Default is `TIMEOUT`, which equates to 30 seconds.
        """
        state = inspect["State"]
        if state["Running"] or state["Restarting"] or state["Paused"]:
            podman_cmd(
                ["container", "stop", inspect["Id"], "-t", str(timeout)],
                check=False
            )

    @staticmethod
    def pause(inspect: Container.Inspect) -> None:
        """Pause a container if it is currently running.

        Parameters
        ----------
        inspect : Container.Inspect
            The output of `Container.inspect()` for the container to pause.
        """
        state = inspect["State"]
        if state["Running"] or state["Restarting"]:
            podman_cmd(["container", "pause", inspect["Id"]], check=False)

    @staticmethod
    def resume(inspect: Container.Inspect) -> None:
        """Resume a container if it is currently paused.

        Parameters
        ----------
        inspect : Container.Inspect
            The output of `Container.inspect()` for the container to resume.
        """
        if inspect["State"]["Paused"]:
            podman_cmd(["container", "unpause", inspect["Id"]], check=False)

    @staticmethod
    def restart(inspect: Container.Inspect, timeout: int = TIMEOUT) -> None:
        """Restart a container.

        Parameters
        ----------
        inspect : Container.Inspect
            The output of `Container.inspect()` for the container to restart.
        timeout : int, optional
            The maximum time in seconds to wait for the container to stop before
            forcefully killing it.  Default is `TIMEOUT`, which equates to 30 seconds.
        """
        state = inspect["State"]
        if state["Running"] or state["Paused"]:
            podman_cmd(
                ["container", "restart", inspect["Id"], "-t", str(timeout)],
                check=False
            )


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
        data = json.loads(result.stdout)
        return data[0] if data else None

    def build(
        self,
        env_root: Path,
        env_uuid: str,
        image_tag: str,
        container_tag: str,
        container_args: list[str] | None
    ) -> Container:
        """Create a new container associated with this image and parent environment,
        with the specified tag and arguments.  If a container with the same tag and
        arguments already exists and has not been relocated, it will be reused instead.

        Parameters
        ----------
        env_root : Path
            The root path of the environment directory.
        env_uuid : str
            The UUID of the environment.
        image_tag : str
            The tag of the image within the environment.
        container_tag : str
            The tag of the container within the image.
        container_args : list[str] | None
            The `podman create` arguments to use for the container.  If None, the
            existing arguments for the container with the given tag will be reused,
            or an empty list if no such container exists and the tag is empty.

        Returns
        -------
        Container
            The created or reused `Container` object.  Note that if a new container is
            created, it will be in the "created" state, and must be started before use.
            An equivalent key in the `containers` mapping will also be created or
            updated to point to the new container.

        Raises
        ------
        KeyError
            If no previous container exists for the given tag and `container_args` is
            None.
        CommandError
            If the `podman create` or `podman container rm` (for an outdated container)
            command fails.
        """
        existing = self.containers.get(container_tag)
        if container_args is None:
            if existing is None:
                if container_tag:
                    raise KeyError(f"no container '{container_tag}' found for image '{image_tag}'")
                container_args = []  # empty tag implies empty args
            else:
                container_args = existing.args  # reuse existing args
        else:
            container_args = _normalize_args(container_args)  # define new args

        # reuse container if args match and container has not been relocated
        if existing is not None and existing.args == container_args:
            inspect = existing.inspect()
            if inspect is not None and not Container.relocated(env_root, inspect):
                return existing

        # build new container
        container = Container.model_construct(
            version=VERSION,
            id="",  # corrected after create
            created=datetime.now(timezone.utc),
            args=container_args,
            entry_point=[],  # default entry point
        )
        container_name = (
            f"{_sanitize_name(env_root.name)}.{image_tag}.{container_tag}.{env_uuid[:13]}"
        )
        cid_file = _cid_file(env_root, container_name)
        cache_prefix = f"bertrand-{env_uuid[:13]}"
        try:
            cid_file.parent.mkdir(parents=True, exist_ok=True)
            podman_cmd([
                "create",
                "--init",
                f"--name={container_name}",
                f"--hostname={container_name}",
                "--cidfile", str(cid_file),

                # labels for podman-level lookup
                "--label", "BERTRAND=1",
                "--label", f"BERTRAND_ENV={env_uuid}",
                "--label", f"BERTRAND_IMAGE={image_tag}",
                "--label", f"BERTRAND_CONTAINER={container_tag}",

                # mount environment directory
                "-v", f"{str(env_root)}:{MOUNT}",

                # persistent caches for incremental builds
                "--mount", f"type=volume,src={cache_prefix}-pip,dst={TMP}/.cache/pip",
                "--mount", f"type=volume,src={cache_prefix}-bertrand,dst={TMP}/.cache/bertrand",
                "--mount", f"type=volume,src={cache_prefix}-ccache,dst={TMP}/.cache/ccache",
                "-e", f"PIP_CACHE_DIR={TMP}/.cache/pip",
                "-e", f"BERTRAND_CACHE={TMP}/.cache/bertrand",
                "-e", f"CCACHE_DIR={TMP}/.cache/ccache",

                # environment variables for Bertrand runtime
                "-e", "BERTRAND=1",
                "-e", f"BERTRAND_ENV={env_uuid}",
                "-e", f"BERTRAND_IMAGE={image_tag}",
                "-e", f"BERTRAND_CONTAINER={container_tag}",

                # any additional user-specified args
                *container_args,
                self.id,
                "sleep", "infinity",
            ])
            container.id = cid_file.read_text(encoding="utf-8").strip()
        finally:
            cid_file.unlink(missing_ok=True)

        # remove existing container with same tag if needed
        if existing is not None:
            try:
                podman_cmd(["container", "rm", "-f", existing.id], check=False)
            except CommandError as err:
                podman_cmd(["container", "rm", "-f", container.id], check=False)
                raise err
        self.containers[container_tag] = container
        return container


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
    shell : list[str] | None, optional
        The shell command to execute during `bertrand enter`.  If None, defaults to
        `["bash", "-l"]`.
    code : list[str] | None, optional
        The default host command invoked by `code` within the container.  If None,
        defaults to `["vscode"]`.

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
        id: EnvironmentId
        shell: Annotated[str, AfterValidator(_check_shell)]
        code: Annotated[str, AfterValidator(_check_code)]
        tags: dict[str, Image]

        @model_validator(mode="after")
        def _check_tags(self) -> Environment.JSON:
            cleaned: dict[str, Image] = {}
            for tag, image in self.tags.items():
                if not isinstance(tag, str):
                    raise ValueError(f"invalid image tag in environment metadata: {tag}")
                tag = tag.strip()
                sanitized = _sanitize_name(tag)
                if tag != sanitized:
                    raise ValueError(
                        f"invalid characters in image tag '{tag}' (sanitizes to: '{sanitized}')"
                    )
                if tag in cleaned:
                    raise ValueError(f"duplicate image tag in environment metadata: {tag}")
                cleaned[tag] = image
            self.tags = cleaned
            return self

    root: Path
    _json: JSON
    _lock: LockDir

    def __init__(
        self,
        root: Path,
        timeout: int = TIMEOUT,
        shell: Literal["bash"] = "bash",
        code: Literal["vscode"] = "vscode"
    ) -> None:
        self.root = root.expanduser().resolve()
        self._json = self.JSON.model_construct(
            version=0,
            id="",
            shell=shell,
            code=code,
            tags={}
        )
        self._lock = LockDir(_env_dir(self.root) / ".lock", timeout=timeout)

    def __enter__(self) -> Environment:
        self._lock.__enter__()
        if self._lock.depth > 1:
            return self  # re-entrant case

        # try to load existing metadata if possible
        env_file = _env_file(self.root)
        if env_file.exists():
            data = json.loads(env_file.read_text(encoding="utf-8"))
            if not isinstance(data, dict):
                raise ValueError("environment metadata must be a JSON mapping")
            try:
                self._json = self.JSON.model_validate(data)
            except ValidationError as err:
                raise ValueError(f"invalid environment metadata: {err}") from err
            return self

        # initialize new metadata if needed
        self._json = self.JSON(
            version=VERSION,
            id=uuid.uuid4().hex,
            shell=self._json.shell,
            code=self._json.code,
            tags={},
        )

        # init .containerignore
        container_ignore = self.container_ignore
        if not container_ignore.exists():
            container_ignore.parent.mkdir(parents=True, exist_ok=True)
            atomic_write_text(container_ignore, r"""# Bertrand internal state
.bertrand/
**/.bertrand/

# Python
__pycache__/
*.py[cod]
*.egg-info/
.dist/
.build/
.eggs/
.venv/
venv/

# C/C++
build/
out/
*.o
*.obj
*.a
*.lib
*.so
*.dylib
*.dll

# VCS / IDE
.git/
.gitignore
.vscode/
.idea/
.DS_Store
""")

        # init Containerfile
        container_file = self.container_file
        if not container_file.exists():
            container_file.parent.mkdir(parents=True, exist_ok=True)
            # pylint:disable=line-too-long
            atomic_write_text(container_file, rf"""# Bertrand requires a minimal set of arguments to be provided at compile time, which
# are baked into its reproducible images to avoid lengthy recompilation.  These
# arguments may be overridden by passing `<arg>=<value>` options to the
# `bertrand build`, `bertrand start`, or `bertrand enter` commands, which are then
# forwarded to this Containerfile.  Otherwise, the default values will be used.

# toolchain version to install (defaults to host Bertrand version)
ARG BERTRAND={__version__}

# enable stack traces + debug assertions to prevent undefined behavior
ARG DEBUG=true

# include developer tools (language servers, sanitizers, debuggers, AI assistants) in base image
ARG DEV=true

# number of hardware threads for concurrent runtime (>= 1, defaults to host CPU count)
ARG CPUS={os.cpu_count() or 1}

# pull base Bertrand image with the specified configuration
FROM bertrand:${{BERTRAND}}.${{DEBUG}}.${{DEV}}.${{CPUS}}.{getpagesize() // 1024}

# set cwd to the mounted environment directory
WORKDIR {MOUNT}

# set up incremental builds
ENV PIP_CACHE_DIR={TMP}/.cache/pip
ENV BERTRAND_CACHE={TMP}/.cache/bertrand
ENV CCACHE_DIR={TMP}/.cache/ccache
COPY . {MOUNT}

# you can extend this file in order to create a reproducible image that others can pull
# from in their own Dockerfiles.  For example:

RUN --mount=type=cache,target={TMP}/.cache/pip,sharing=locked \
    --mount=type=cache,target={TMP}/.cache/bertrand,sharing=locked \
    --mount=type=cache,target={TMP}/.cache/ccache,sharing=locked \
    pip install .

# A `pip install .` command of that form will incrementally compile the contents of the
# local environment directory (WORKDIR) and install them into the base image as Python
# packages, C++ modules, and/or executable binaries on the container's PATH.  If you
# then upload this image to an external repository, downstream users will be able to
# use `FROM <your-image>` in their own Containerfiles in order to inherit Bertrand's
# toolchain along with your built artifacts and dependencies without needing to
# recompile them from scratch.  This can be useful for large projects where build time
# is significant, or which have external dependencies or build configurations that are
# otherwise difficult to install.  Small projects without significant configuration
# needs are encouraged to use the bundled package managers instead, and leave this file
# alone.

# In most cases, `pip install .` is all you need, but if you'd like to add your own
# compilation flags or install additional system dependencies outside of
# `pyproject.toml`, then you can do so using standard Containerfile commands.  See the
# official Containerfile documentation for a comprehensive reference, and the Bertrand
# toolchain documentation for more details on how this fits into the overall build
# process, as well as tips for your own Containerfiles.
""")
        return self

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc_value: BaseException | None,
        traceback: TracebackType | None
    ) -> None:
        if self._lock.depth > 1:
            self._lock.__exit__(exc_type, exc_value, traceback)
            return  # re-entrant case

        # write changes
        _env_dir(self.root).mkdir(parents=True, exist_ok=True)
        self._json = self.JSON.model_validate(self._json.model_dump(mode="python"))
        atomic_write_text(
            _env_file(self.root),
            json.dumps(self._json.model_dump(mode="json"), indent=2) + "\n"
        )

        # release lock
        shutil.rmtree(_env_dir(self.root) / ".lock", ignore_errors=True)

    def __hash__(self) -> int:
        return hash(self.root)

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, Environment):
            return NotImplemented
        return self.root == other.root

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
            return Environment(root=Path(MOUNT))
        return None

    @staticmethod
    def parse(spec: str) -> tuple[Path, str, str]:
        """Parse a string of the form `<env_root>[:<image_tag>[:<container_tag>]]` into
        its constituent parts.

        Parameters
        ----------
        spec : str
            The container specification string to parse, which is usually provided from
            the command line or `ls()`.

        Returns
        -------
        tuple[Path, str, str]
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
            return Path(spec.strip()).expanduser().resolve(), "", ""
        tag1 = tag1.strip()
        if not tag1:
            raise OSError(f"tag must not be empty: '{spec}'")
        san1 = _sanitize_name(tag1)
        if tag1 != san1:
            raise OSError(
                f"tag contains invalid characters: '{tag1}' (sanitizes to: '{san1}')"
            )

        # resolve middle tag
        prev, sep, tag2 = prev.rpartition(":")
        if not sep or os.path.sep in tag2:
            return Path(prev.strip()).expanduser().resolve(), san1, ""
        tag2 = tag2.strip()
        if not tag2:
            raise OSError(f"tag must not be empty: '{spec}'")
        san2 = _sanitize_name(tag2)
        if tag2 != san2:
            raise OSError(
                f"tag contains invalid characters: '{tag2}' (sanitizes to: '{san2}')"
            )
        return Path(prev.strip()).expanduser().resolve(), san2, san1

    @property
    def timeout(self) -> int:
        """
        Returns
        -------
        int
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
    def shell(self) -> tuple[str, ...]:
        """
        Returns
        -------
        tuple[str, ...]
            The shell command to execute during `bertrand enter`, as specified in the
            environment metadata.
        """
        return SHELLS[self._json.shell]

    @property
    def code(self) -> tuple[str, ...]:
        """
        Returns
        -------
        tuple[str, ...]
            The default host command invoked by `bertrand code` while within the
            container, as specified in the environment metadata.
        """
        return EDITORS[self._json.code]

    @property
    def tags(self) -> dict[str, Image]:
        """
        Returns
        -------
        dict[str, Image]
            A mapping from image tags to metadata objects representing corresponding
            images.  Each image object contains a nested mapping from container tags to
            downstream container metadata objects.  This is the single source of truth
            for locating images and containers within this environment, such that the
            underlying image and container IDs may change without affecting the
            user-visible tags.
        """
        return self._json.tags

    @property
    def container_file(self) -> Path:
        """
        Returns
        -------
        Path
            The path to the `Containerfile` within the environment root.
        """
        return _container_file(self.root)

    @property
    def container_ignore(self) -> Path:
        """
        Returns
        -------
        Path
            The path to the `.containerignore` file within the environment root.
        """
        return self.root / ".containerignore"

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
        if self._lock.depth < 1:
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

    def ls(self, image_tag: str | None = None, container_tag: str | None = None) -> list[str]:
        """Return a list of image or container tags within this environment.

        Parameters
        ----------
        image_tag : str | None, optional
            An optional image tag to list.  If None (the default), all image tags in
            this environment will be listed.
        container_tag : str | None, optional
            An optional container tag to list.  If None (the default), all container
            tags in the indicated image will be listed.

        Returns
        -------
        list[str]
            A list of fully qualified image or container tags within this environment,
            depending on the provided arguments.  An empty list will be returned if no
            matching tags could be found.  Use `parse()` to split the returned tags
            into their constituent parts.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager.
        TypeError
            If `image` is None but `container` is not.
        """
        if self._lock.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")

        # list all images
        if image_tag is None:
            if container_tag is not None:
                raise TypeError("cannot specify a container when listing all images")
            return [f"{self.root}:{k}" for k in self.tags]

        # list all containers in an image
        image = self.tags.get(image_tag)
        if image is None:
            return []
        if container_tag is None:
            return [f"{self.root}:{image_tag}:{k}" for k in image.containers]

        # check for specific container in an image
        if container_tag in image.containers:
            return [f"{self.root}:{image_tag}:{container_tag}"]
        return []

    def _rm_container(self, image_tag: str, container_tag: str) -> None:
        image = self.tags.get(image_tag)
        if image is None:
            return
        container = image.containers.get(container_tag)
        if container is None:
            return
        podman_cmd(["container", "rm", "-f", container.id], check=False)
        image.containers.pop(container_tag, None)

    def _rm_image(self, image_tag: str) -> None:
        image = self.tags.get(image_tag)
        if image is None:
            return

        # remove all descendant containers
        out = podman_cmd([
            "container",
            "ls",
            "-a",
            "--no-trunc",
            "--filter", f"ancestor={image.id}",
            "--format", "{{.ID}}"
        ], check=False, capture_output=True)
        if out.returncode == 0:
            ids = list(out.stdout.splitlines())
            if ids:
                podman_cmd(["container", "rm", "-f", *ids], check=False)

        # remove image
        podman_cmd(["image", "rm", "-f", image.id], check=False)
        self.tags.pop(image_tag)

    def _rm_all(self) -> None:
        # remove all containers associated with this environment
        out = podman_cmd([
            "container",
            "ls",
            "-a",
            "--no-trunc",
            "--filter", f"label=BERTRAND_ENV={self.id}",
            "--format", "{{.ID}}"
        ], check=False, capture_output=True)
        if out.returncode == 0:
            ids = list(out.stdout.splitlines())
            if ids:
                podman_cmd(["container", "rm", "-f", *ids], check=False)

        # remove all images associated with this environment
        out = podman_cmd([
            "image",
            "ls",
            "-a",
            "--no-trunc",
            "--filter", f"label=BERTRAND_ENV={self.id}",
            "--format", "{{.ID}}"
        ], check=False, capture_output=True)
        if out.returncode == 0:
            ids = list(out.stdout.splitlines())
            if ids:
                podman_cmd(["image", "rm", "-f", *ids], check=False)

        # delete cache volumes
        cache_prefix = f"bertrand-{self.id[:13]}"
        out = podman_cmd([
            "volume",
            "ls",
            "--filter", f"name={cache_prefix}",
            "--format", "{{.Name}}"
        ], check=False, capture_output=True)
        if out.returncode == 0:
            names = list(out.stdout.splitlines())
            for name in names:
                if name.startswith(cache_prefix):
                    podman_cmd(["volume", "rm", "-f", name], check=False)

        # clear metadata
        self.tags.clear()

    def rm(self, image_tag: str | None = None, container_tag: str | None = None) -> None:
        """Delete images and/or containers from this environment.

        Parameters
        ----------
        image_tag : str | None, optional
            An optional image tag to remove.  If None (the default), all images and
            containers in this environment will be removed.
        container_tag : str | None, optional
            An optional container tag to remove.  If None (the default), all containers
            in the indicated image (or whole environment if `image_tag` is None) will
            be removed.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container.
        TypeError
            If `image_tag` is None but `container_tag` is not.
        """
        if self._lock.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke podman from within a Bertrand container")

        # delete all images
        if image_tag is None:
            if container_tag is not None:
                raise TypeError("cannot specify a container when removing all images")
            self._rm_all()
            return

        # delete all containers in an image
        if container_tag is None:
            self._rm_image(image_tag)
            return

        # delete specific container in an image
        self._rm_container(image_tag, container_tag)

    def _build_container(
        self,
        image_tag: str,
        container_tag: str,
        container_args: list[str] | None
    ) -> Container:
        image = self.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        return image.build(
            env_root=self.root,
            env_uuid=self.id,
            image_tag=image_tag,
            container_tag=container_tag,
            container_args=container_args
        )

    def _build_image(self, image_tag: str, image_args: list[str] | None) -> Image:
        existing = self.tags.get(image_tag)
        if image_args is None:
            if existing is None:
                if image_tag:
                    raise KeyError(f"no image found for tag: '{image_tag}'")
                image_args = []  # empty tag implies empty args
            else:
                image_args = existing.args  # reuse existing args
        else:
            image_args = _normalize_args(image_args)  # define new args


        # build new image
        # NOTE: podman + BuildAh will automatically reuse cached layers as long as
        # none of the inputs have changed (including the contents of the environment
        # directory, excluding patterns in .containerignore).  Therefore, we can build
        # unconditionally, and check whether the ID has changed afterwards to detect
        # whether a rebuild was necessary.
        image = Image.model_construct(
            version=VERSION,
            id="",  # corrected after build
            created=datetime.now(timezone.utc),
            args=image_args,
            containers={},
        )
        image_name = f"{_sanitize_name(self.root.name)}.{image_tag}.{self.id[:13]}"
        iid_file = _iid_file(self.root, image_name)
        try:
            iid_file.parent.mkdir(parents=True, exist_ok=True)
            podman_cmd([
                "build",
                *image_args,
                "-t", image_name,
                "-f", str(self.container_file),
                "--iidfile", str(iid_file),
                "--label", "BERTRAND=1",
                "--label", f"BERTRAND_ENV={self.id}",
                "--label", f"BERTRAND_IMAGE={image_tag}",
                str(self.root),
            ])
            image.id = iid_file.read_text(encoding="utf-8").strip()  # build returns image ID
        finally:
            iid_file.unlink(missing_ok=True)
        if existing is not None and image.id == existing.id:
            return existing

        # rebuild downstream containers for new image, then replace existing image
        try:
            for container_tag, container in existing.containers.items() if existing else []:
                image.build(
                    env_root=self.root,
                    env_uuid=self.id,
                    image_tag=image_tag,
                    container_tag=container_tag,
                    container_args=container.args
                )
            self._rm_image(image_tag)
        except Exception as err:
            for container in image.containers.values():
                podman_cmd(["container", "rm", "-f", container.id], check=False)
            podman_cmd(["image", "rm", "-f", image.id], check=False)
            raise err
        self.tags[image_tag] = image
        return image

    def _build_all(self) -> Environment:
        for t in list(self.tags):
            self._build_image(t, None)
        return self

    @overload
    def build(
        self,
        image_tag: str,
        image_args: list[str] | None = ...
    ) -> Image: ...
    @overload
    def build(
        self,
        image_tag: None = ...,
        image_args: None = ...
    ) -> Environment: ...
    def build(
        self,
        image_tag: str | None = None,
        image_args: list[str] | None = None
    ) -> Environment | Image:
        """Incrementally build an image with the given tag and arguments, or load an
        existing one if it is already up-to-date.

        Parameters
        ----------
        image_tag : str | None, optional
            The image tag to search for.  If no existing image with the same tag is
            found, or the existing image is out of date, or its arguments differ from
            `image_args`, then a new image will be built and associated with this tag.
            If None (the default), all images in the environment will be built
            incrementally using their existing arguments.
        image_args : list[str] | None, optional
            The `podman build` arguments to use when building the image if no existing
            image could be found.  If an existing image is found with the same tag, but
            different arguments, then it will be removed and rebuilt with these
            arguments.  If a non-empty list of arguments is provided, then `image_tag`
            must also be non-empty.  If None (the default), the existing arguments for
            the image will be reused if possible, or an error will be raised if no
            existing image could be found.

        Returns
        -------
        Environment | Image
            The resulting image metadata, which may be a reference to an existing image
            or a newly built one.  If `image_tag` is None, then the environment itself
            will be returned, after building all images.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container, or if non-empty
            `image_args` are provided with an empty `image_tag`, or vice versa.
        TypeError
            If `image_tag` is None but `image_args` is not None, or if non-empty
            `image_args` are provided with an empty `image_tag`.
        KeyError
            If no existing image could be found for the given `image_tag`, and
            `image_args` is None.
        CommandError
            If a `podman build` or `podman image inspect` command fails.
        """
        # pylint: disable=missing-raises-doc
        if self._lock.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke podman from within a Bertrand container")
        if image_args and not image_tag:
            raise TypeError("images with non-default arguments must have a tag")

        # if no tag is provided, build all images
        if image_tag is None:
            if image_args is not None:
                raise TypeError("cannot provide arguments when building all images")
            return self._build_all()

        # build specific image
        return self._build_image(image_tag, image_args)

    def _start_container(
        self,
        image_tag: str,
        container_tag: str,
        container_args: list[str] | None
    ) -> Container:
        self._build_image(image_tag, None)
        container = self._build_container(image_tag, container_tag, container_args)
        inspect = container.inspect()
        assert inspect is not None, (
            f"failed to build container '{container_tag}' in image '{image_tag}'"
        )
        Container.start(inspect)
        return container

    def _start_image(self, image_tag: str) -> Image:
        image = self._build_image(image_tag, None)
        for container_tag, container in list(image.containers.items()):
            inspect = container.inspect()
            if inspect is None:
                self._rm_container(image_tag, container_tag)
            else:
                Container.start(inspect)
        return image

    def _start_all(self) -> Environment:
        for image_tag in list(self.tags):
            self._start_image(image_tag)
        return self

    @overload
    def start(
        self,
        image_tag: str,
        container_tag: str,
        container_args: list[str] | None = ...
    ) -> Container: ...
    @overload
    def start(
        self,
        image_tag: str,
        container_tag: None = ...,
        container_args: None = ...
    ) -> Image: ...
    @overload
    def start(
        self,
        image_tag: None = ...,
        container_tag: None = ...,
        container_args: None = ...
    ) -> Environment: ...
    def start(
        self,
        image_tag: str | None = None,
        container_tag: str | None = None,
        container_args: list[str] | None = None
    ) -> Environment | Image | Container:
        """Incrementally build a container with the given image, tag, and arguments, or
        load an existing one if it is already up-to-date, and then start it.

        Parameters
        ----------
        image_tag : str | None, optional
            The image tag to search for.  If no existing image with the same tag is
            found, and the tag is not empty, then an error will be raised.  Otherwise,
            if the tagged image is out of date, it will be rebuilt using its original
            arguments.  If None (the default), all images in the environment will be
            started.
        container_tag : str | None, optional
            The container tag to search for.  If no existing container with the same
            tag is found, or the existing container is out of date, or its arguments
            differ from `container_args`, then a new container will be created and
            associated with this tag.  If None (the default), all containers in the
            indicated image will be started.
        container_args : list[str] | None, optional
            The `podman create` arguments to use when creating the container if no
            existing container could be found.  If an existing container is found with
            the same tag, but different arguments, then it will be removed and rebuilt
            with these arguments.  If a non-empty list of arguments is provided, then
            `container_tag` must also be non-empty.  If None (the default), the
            existing arguments for the container will be reused if possible, or an
            error will be raised if no existing container could be found.

        Returns
        -------
        Environment | Image | Container
            The resulting container metadata, which may be a reference to an existing
            container or a newly created one.  If `image_tag` is None, then the
            environment itself will be returned, after starting all containers.  If
            `container_tag` is None, then the parent image will be returned, after
            starting all containers in that image.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container.
        TypeError
            If `image_tag` or `container_tag` is None but any of the subsequent
            arguments are not None, or if non-empty `container_args` are provided with
            an empty `container_tag`.
        KeyError
            If an image tag is provided but could not be found, or if a container tag
            is provided and `container_args` is None, but the container could not be
            found.
        CommandError
            If a `podman build`, `podman image inspect`, `podman create`,
            `podman container inspect`, or `podman start` command fails.
        """
        # pylint: disable=missing-raises-doc
        if self._lock.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke podman from within a Bertrand container")
        if container_args and not container_tag:
            raise TypeError("containers with non-default arguments must have a tag")

        # start all containers in this environment
        if image_tag is None:
            if container_tag is not None or container_args is not None:
                raise TypeError("cannot specify a tag or arguments when starting all containers")
            return self._start_all()

        # start all containers in the specified image
        if container_tag is None:
            if container_args is not None:
                raise TypeError("cannot specify arguments when starting all containers in an image")
            return self._start_image(image_tag)

        # start specific container
        return self._start_container(image_tag, container_tag, container_args)

    def enter(
        self,
        image_tag: str,
        container_tag: str,
        container_args: list[str] | None = None
    ) -> Container:
        """Replace the current process with an interactive shell inside the specified
        container, starting or rebuilding it as necessary.

        Parameters
        ----------
        image_tag : str
            The image tag to search for.  If no existing image with the same tag is
            found, and the tag is not empty, then an error will be raised.  Otherwise
            if the tagged image is out of date, it will be rebuilt using its original
            arguments.
        container_tag : str
            The container tag to search for.  If no existing container with the same
            tag is found, or the existing container is out of date, or its arguments
            differ from `container_args`, then a new container will be created and
            associated with this tag.  If a non-empty tag is provided, then
            `container_args` must also be non-empty.
        container_args : list[str] | None, optional
            The `podman create` arguments to use when creating the container if no
            existing container could be found.  If an existing container is found with
            the same tag, but different arguments, then it will be removed and rebuilt
            with these arguments.  If a non-empty list of arguments is provided, then
            `container_tag` must also be non-empty.  If None (the default), the
            existing arguments for the container will be reused if possible, or an
            error will be raised if no existing container could be found.

        Returns
        -------
        Container
            The resulting container metadata, which may be a reference to an existing
            container or a newly created one.

        Raises
        ------
        KeyError
            If an image tag is provided but could not be found.
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container, or if non-empty
            `container_args` are provided with an empty `container_tag`, or vice versa.
        CommandError
            If a `podman build`, `podman image inspect`, `podman create`,
            `podman container inspect`, or `podman exec` command fails.
        """
        # start or rebuild the container, then replace current process with an inner shell
        container = self.start(image_tag, container_tag, container_args)

        # ensure stdin and stdout can attach to a TTY
        if not sys.stdin.isatty() or not sys.stdout.isatty():
            raise CommandError(
                returncode=1,
                cmd=["bertrand", "enter", f"{self.root}:{image_tag}:{container_tag}"],
                stdout="",
                stderr="'bertrand enter' requires both stdin and stdout to be a TTY."
            )

        # launch interactive shell
        podman_exec([
            "exec",
            "-it",
            "-w", MOUNT,
            container.id,
            *self.shell
        ])
        return container

    def _run_interactive(self) -> list[str]:
        if (
            sys.stdin.isatty() and
            sys.stdout.isatty() and
            sys.stderr.isatty() and
            os.environ.get("TERM", "") not in ("", "dumb")
        ):
            return ["-i", "-t"]
        return ["-i"]

    def _run_container(
        self,
        image_tag: str,
        container_tag: str,
        argv: list[str]
    ) -> Container:
        # ensure container has a valid entry point
        container = self._start_container(image_tag, container_tag, None)
        if not container.entry_point:
            raise CommandError(
                returncode=1,
                cmd=["bertrand", "run", f"{self.root}:{image_tag}:{container_tag}", *argv],
                stdout="",
                stderr=
                    "Cannot run command: container has no entry point defined.  Either "
                    "write a '__main__.py' file or include a C++ source file that "
                    "implements an 'int main()' function."
            )

        # run the command within the container context
        podman_cmd([
            "exec",
            *self._run_interactive(),
            "-w", MOUNT,
            container.id,
            *container.entry_point,
            *argv
        ])
        return container

    def _run_image(self, image_tag: str, argv: list[str]) -> Image:
        image = self._build_image(image_tag, None)
        for container_tag, container in list(image.containers.items()):
            inspect = container.inspect()
            assert inspect is not None, (
                f"failed to build container '{container_tag}' in image '{image_tag}'"
            )
            Container.start(inspect)

            # ensure container has a valid entry point
            if not container.entry_point:
                raise CommandError(
                    returncode=1,
                    cmd=["bertrand", "run", f"{self.root}:{image_tag}:{container_tag}", *argv],
                    stdout="",
                    stderr=
                        "Cannot run command: container has no entry point defined.  Either "
                        "write a '__main__.py' file or include a C++ source file that "
                        "implements an 'int main()' function to run."
                )

            # run the command within the container context
            podman_cmd([
                "exec",
                *self._run_interactive(),
                "-w", MOUNT,
                container.id,
                *container.entry_point,
                *argv
            ])
        return image

    def _run_all(self, argv: list[str]) -> Environment:
        for image_tag in list(self.tags):
            self._run_image(image_tag, argv)
        return self

    @overload
    def run(
        self,
        image_tag: str,
        container_tag: str,
        argv: list[str] | None = ...
    ) -> Container: ...
    @overload
    def run(
        self,
        image_tag: str,
        container_tag: None = ...,
        argv: list[str] | None = ...
    ) -> Image: ...
    @overload
    def run(
        self,
        image_tag: None = ...,
        container_tag: None = ...,
        argv: list[str] | None = ...
    ) -> Environment: ...
    def run(
        self,
        image_tag: str | None = None,
        container_tag: str | None = None,
        argv: list[str] | None = None
    ) -> Environment | Image | Container:
        """Invoke a container's entry point, starting or rebuilding it as necessary,
        and passing along any additional arguments.

        Parameters
        ----------
        image_tag : str | None
            The image tag to search for.  If no existing image with the same tag is
            found, and the tag is not empty, then an error will be raised.  Otherwise,
            if the tagged image is out of date, it will be rebuilt using its original
            arguments.  If None (the default), all images in the environment will be
            run with the same arguments.
        container_tag : str | None
            The container tag to search for.  If no existing container with the same
            tag is found, and the tag is not empty, then an error will be raised.
            Otherwise, if the tagged container is out of date, it will be rebuilt using
            its original arguments.  If None (the default), all containers in the
            indicated image will be run with the same arguments.
        argv : list[str] | None
            The arguments to pass to the container's entry point when running the
            command.  The entry point itself is prepended automatically.  If None
            (the default), then no additional arguments will be passed.

        Returns
        -------
        Environment | Image | Container
            The resulting container metadata, which may be a reference to an existing
            container or a newly created one.  If `image_tag` is None, then the
            environment itself will be returned, after scheduling all containers.  If
            `container_tag` is None, then the parent image will be returned, after
            scheduling all containers in that image.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container.
        TypeError
            If `image_tag` is None but `container_tag` is not.
        KeyError
            If an image or container tag is provided but could not be found.
        CommandError
            If a `podman build`, `podman image inspect`, `podman create`,
            `podman container inspect`, or `podman exec` command fails, or if the
            container does not have a valid entry point.
        """
        if self._lock.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke podman from within a Bertrand container")
        if argv is None:
            argv = []

        # run all containers in this environment
        if image_tag is None:
            if container_tag is not None:
                raise TypeError("cannot specify a container when image tag is None")
            return self._run_all(argv)

        # run all containers in the specified image
        if container_tag is None:
            return self._run_image(image_tag, argv)

        # run a specific container
        return self._run_container(image_tag, container_tag, argv)

    @overload
    def stop(self, image_tag: str, container_tag: str) -> Container | None: ...
    @overload
    def stop(self, image_tag: str, container_tag: None = None) -> Image | None: ...
    @overload
    def stop(self, image_tag: None = None, container_tag: None = None) -> Environment | None: ...
    def stop(
        self,
        image_tag: str | None = None,
        container_tag: str | None = None
    ) -> Environment | Image | Container | None:
        """Stop running containers within this environment.

        Parameters
        ----------
        image_tag : str | None, optional
            An optional image tag to stop containers within.  If None (the default),
            all containers in this environment will be stopped.
        container_tag : str | None, optional
            An optional container tag to stop within the indicated image.  If None (the
            default), all containers in the indicated image (or whole environment if
            `image_tag` is None) will be stopped.

        Returns
        -------
        Environment | Image | Container | None
            The top-level environment, image, or container metadata that was stopped,
            or None if no matching tag could be found.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container.
        TypeError
            If `image_tag` is None but `container_tag` is not.
        """
        if self._lock.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke podman from within a Bertrand container")

        # stop all containers in the environment
        if image_tag is None:
            if container_tag is not None:
                raise TypeError("cannot specify a container when stopping all images")
            for i in self.tags.values():
                for c in i.containers.values():
                    inspect = c.inspect()
                    if inspect is None:
                        continue
                    Container.stop(inspect, self.timeout)
            return self

        # stop all containers in an image
        if container_tag is None:
            image = self.tags.get(image_tag)
            if image is None:
                return None
            for c in image.containers.values():
                inspect = c.inspect()
                if inspect is None:
                    continue
                Container.stop(inspect, self.timeout)
            return image

        # stop a specific container in an image
        image = self.tags.get(image_tag)
        if image is None:
            return None
        container = image.containers.get(container_tag)
        if container is None:
            return None
        inspect = container.inspect()
        if inspect is None:
            return None
        Container.stop(inspect, self.timeout)
        return container

    @overload
    def pause(self, image_tag: str, container_tag: str) -> Container | None: ...
    @overload
    def pause(self, image_tag: str, container_tag: None = None) -> Image | None: ...
    @overload
    def pause(self, image_tag: None = None, container_tag: None = None) -> Environment | None: ...
    def pause(
        self,
        image_tag: str | None = None,
        container_tag: str | None = None
    ) -> Environment | Image | Container | None:
        """Pause running containers within this environment.

        Parameters
        ----------
        image_tag : str | None, optional
            An optional image tag to pause containers within.  If None (the default),
            all containers in this environment will be paused.
        container_tag : str | None, optional
            An optional container tag to pause within the indicated image.  If None (the
            default), all containers in the indicated image (or whole environment if
            `image_tag` is None) will be paused.

        Returns
        -------
        Environment | Image | Container | None
            The top-level environment, image, or container metadata that was paused,
            or None if no matching tag could be found.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container.
        TypeError
            If `image_tag` is None but `container_tag` is not.
        """
        if self._lock.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke podman from within a Bertrand container")

        # pause all containers in the environment
        if image_tag is None:
            if container_tag is not None:
                raise TypeError("cannot specify a container when pausing all images")
            for i in self.tags.values():
                for c in i.containers.values():
                    inspect = c.inspect()
                    if inspect is None:
                        continue
                    Container.pause(inspect)
            return self

        # pause all containers in an image
        if container_tag is None:
            image = self.tags.get(image_tag)
            if image is None:
                return None
            for c in image.containers.values():
                inspect = c.inspect()
                if inspect is None:
                    continue
                Container.pause(inspect)
            return image

        # pause a specific container in an image
        image = self.tags.get(image_tag)
        if image is None:
            return None
        container = image.containers.get(container_tag)
        if container is None:
            return None
        inspect = container.inspect()
        if inspect is None:
            return None
        Container.pause(inspect)
        return container

    @overload
    def resume(self, image_tag: str, container_tag: str) -> Container | None: ...
    @overload
    def resume(self, image_tag: str, container_tag: None = None) -> Image | None: ...
    @overload
    def resume(self, image_tag: None = None, container_tag: None = None) -> Environment | None: ...
    def resume(
        self,
        image_tag: str | None = None,
        container_tag: str | None = None
    ) -> Environment | Image | Container | None:
        """Resume paused containers within this environment.

        Parameters
        ----------
        image_tag : str | None, optional
            An optional image tag to resume containers within.  If None (the default),
            all paused containers in this environment will be resumed.
        container_tag : str | None, optional
            An optional container tag to resume within the indicated image.  If None
            (the default), all paused containers in the indicated image (or whole
            environment if `image_tag` is None) will be resumed.

        Returns
        -------
        Environment | Image | Container | None
            The top-level environment, image, or container metadata that was resumed,
            or None if no matching tag could be found.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container.
        TypeError
            If `image_tag` is None but `container_tag` is not.
        """
        if self._lock.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke podman from within a Bertrand container")

        # resume all paused containers in the environment
        if image_tag is None:
            if container_tag is not None:
                raise TypeError("cannot specify a container when stopping all images")
            for i in self.tags.values():
                for c in i.containers.values():
                    inspect = c.inspect()
                    if inspect is None:
                        continue
                    Container.resume(inspect)
            return self

        # resume all paused containers in an image
        if container_tag is None:
            image = self.tags.get(image_tag)
            if image is None:
                return None
            for c in image.containers.values():
                inspect = c.inspect()
                if inspect is None:
                    continue
                Container.resume(inspect)
            return image

        # resume a specific container in an image
        image = self.tags.get(image_tag)
        if image is None:
            return None
        container = image.containers.get(container_tag)
        if container is None:
            return None
        inspect = container.inspect()
        if inspect is None:
            return None
        Container.resume(inspect)
        return container

    def _prune_container(self, image_tag: str, container_tag: str) -> None:
        image = self.tags.get(image_tag)
        if image is None:
            return None
        container = image.containers.get(container_tag)
        if container is None:
            return None
        inspect = container.inspect()
        if inspect is None or Container.relocated(self.root, inspect):
            self._rm_container(image_tag, container_tag)
            return

    def _prune_image(self, image_tag: str) -> None:
        image = self.tags.get(image_tag)
        if image is None:
            return
        if image.inspect() is None:
            self._rm_image(image_tag)
            return
        for container_tag, container in list(image.containers.items()):
            inspect = container.inspect()
            if inspect is None or Container.relocated(self.root, inspect):
                self._rm_container(image_tag, container_tag)

    def _prune_all(self) -> None:
        for image_tag, image in list(self.tags.items()):
            if image.inspect() is None:
                self._rm_image(image_tag)
                continue
            for container_tag, container in list(image.containers.items()):
                inspect = container.inspect()
                if inspect is None or Container.relocated(self.root, inspect):
                    self._rm_container(image_tag, container_tag)

    def prune(self, image_tag: str | None = None, container_tag: str | None = None) -> None:
        """Delete stale images and/or containers from this environment.

        Parameters
        ----------
        image_tag : str | None, optional
            An optional image tag to prune.  If None (the default), all images and
            containers in this environment will be pruned.
        container_tag : str | None, optional
            An optional container tag to prune.  If None (the default), all containers
            in the indicated image (or whole environment if `image_tag` is None) will
            be pruned.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container.
        TypeError
            If `image_tag` is None but `container_tag` is not.
        """
        if self._lock.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke podman from within a Bertrand container")

        # prune all images
        if image_tag is None:
            if container_tag is not None:
                raise TypeError("cannot specify a container when pruning all images")
            self._prune_all()
            return

        # prune all containers in an image
        if container_tag is None:
            self._prune_image(image_tag)
            return

        # prune specific container in an image
        self._prune_container(image_tag, container_tag)

    def _restart_container(self, image_tag: str, container_tag: str) -> Container:
        image = self.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        container = image.containers.get(container_tag)
        if container is None:
            raise KeyError(f"no container found for tag: '{container_tag}'")

        # detect whether the current container is running
        inspect = container.inspect()
        if inspect is None:
            image.containers.pop(container_tag)
            running = False
        else:
            running = Container.running(inspect)

        # possibly rebuild the parent image and all downstream containers
        image = self._build_image(image_tag, None)

        # if the container was previously running, restart it
        container = image.containers.get(container_tag)
        assert container is not None, (
            f"failed to build container '{container_tag}' in image '{image_tag}'"
        )
        inspect = container.inspect()
        assert inspect is not None, (
            f"failed to build container '{container_tag}' in image '{image_tag}'"
        )
        if running:
            if Container.new(inspect):
                Container.start(inspect)
            else:
                Container.restart(inspect, self.timeout)
        return container

    def _restart_image(self, image_tag: str) -> Image:
        image = self.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")

        # record all running containers before rebuilding
        running: list[str] = []
        for container_tag, c in image.containers.items():
            inspect = c.inspect()
            if inspect is not None and Container.running(inspect):
                running.append(container_tag)

        # rebuild the image and all downstream containers
        image = self._build_image(image_tag, None)

        # restart previously running containers
        for container_tag in running:
            container = image.containers.get(container_tag)
            assert container is not None, (
                f"failed to build container '{container_tag}' in image '{image_tag}'"
            )
            inspect = container.inspect()
            assert inspect is not None, (
                f"failed to build container '{container_tag}' in image '{image_tag}'"
            )
            if Container.new(inspect):
                Container.start(inspect)
            else:
                Container.restart(inspect, self.timeout)

        return image

    def _restart_all(self) -> Environment:
        for image_tag in list(self.tags):
            self._restart_image(image_tag)
        return self

    @overload
    def restart(self, image_tag: str, container_tag: str) -> Container | None: ...
    @overload
    def restart(self, image_tag: str, container_tag: None = None) -> Image | None: ...
    @overload
    def restart(self, image_tag: None = None, container_tag: None = None) -> Environment | None: ...
    def restart(
        self,
        image_tag: str | None = None,
        container_tag: str | None = None
    ) -> Environment | Image | Container | None:
        """Restart running containers within this environment, possibly rebuilding any
        that are out of date.

        Parameters
        ----------
        image_tag : str | None, optional
            An optional image tag to restart containers within.  If None (the default),
            all running containers in this environment will be restarted.
        container_tag : str | None, optional
            An optional container tag to restart within the indicated image.  If None
            (the default), all running containers in the indicated image (or whole
            environment if `image_tag` is None) will be restarted.

        Returns
        -------
        Environment | Image | Container | None
            The top-level environment, image, or container metadata that was restarted,
            or None if no matching tag could be found.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container.
        TypeError
            If `image_tag` is None but `container_tag` is not.
        KeyError
            If an image or container tag is provided but could not be found.
        """
        if self._lock.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke podman from within a Bertrand container")

        # restart all containers in the environment
        if image_tag is None:
            if container_tag is not None:
                raise TypeError("cannot specify a container when stopping all images")
            return self._restart_all()

        # restart all containers in an image
        if container_tag is None:
            return self._restart_image(image_tag)

        # restart a specific container in an image
        return self._restart_container(image_tag, container_tag)

    class Info(TypedDict):
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

    def _info(self, cmd: list[str], *, parse: bool) -> list[Environment.Info] | None:
        if parse:
            cmd.append("--no-trunc")
            cmd.append("--format=json")
            result = podman_cmd(cmd, capture_output=True)
            out = json.loads(result.stdout)
            if not isinstance(out, list):
                out = [out]
            return out

        cmd.append(
            "--format=table {{.Names}}\t{{.CreatedAt}}\t{{.State}}\t{{.Command}}\t"
            "{{.RunningFor}}\t{{.Status}}\t{{.Size}}\t{{.Ports}}"
        )
        podman_cmd(cmd)
        return None

    @overload
    def info(
        self,
        image_tag: str | None = None,
        container_tag: str | None = None,
        *,
        parse: Literal[True] = ...
    ) -> list[Environment.Info]: ...
    @overload
    def info(
        self,
        image_tag: str | None = None,
        container_tag: str | None = None,
        *,
        parse: Literal[False],
    ) -> None: ...
    def info(
        self,
        image_tag: str | None = None,
        container_tag: str | None = None,
        *,
        parse: bool = True
    ) -> list[Environment.Info] | None:
        """Gather status information for containers within this environment.

        Parameters
        ----------
        image_tag : str | None, optional
            An optional image tag to gather information for.  If None (the default),
            information will be gathered for all containers in this environment.
        container_tag : str | None, optional
            An optional container tag to gather information for.  If None (the default),
            information will be gathered for all containers in the indicated image (or
            whole environment if `image_tag` is None).
        parse : bool, optional
            Whether to parse the output as JSON and return it as a list of dictionaries.
            If false, the output will be printed to stdout in a human-readable table
            format, and nothing will be returned by this method.  Default is True.

        Returns
        -------
        list[Environment.Info] | None
            If `parse` is True, then a list of dictionaries containing status information
            for each matching container.  If `parse` is False, then None.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container.
        TypeError
            If `image_tag` is None but `container_tag` is not.
        """
        if self._lock.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke podman from within a Bertrand container")

        cmd = [
            "container",
            "ls",
            "--all",
            "--size",
            "--filter", f"label=BERTRAND_ENV={self.id}",
        ]

        # gather info for all containers in the environment
        if image_tag is None:
            if container_tag is not None:
                raise TypeError("cannot specify a container when listing all images")
            return self._info(cmd, parse=parse)

        # gather info for all containers in an image
        cmd.append("--filter")
        cmd.append(f"label=BERTRAND_IMAGE={image_tag}")
        if container_tag is None:
            return self._info(cmd, parse=parse)

        # gather info for a specific container in an image
        cmd.append("--filter")
        cmd.append(f"label=BERTRAND_CONTAINER={container_tag}")
        return self._info(cmd, parse=parse)

    class Stats(TypedDict):
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

    def _stats(
        self,
        ids: Iterable[str],
        *,
        parse: bool,
        stream: bool
    ) -> list[Environment.Stats] | None:
        cmd = ["container", "stats"]
        if not stream:
            cmd.append("--no-stream")

        if parse:
            cmd.append("--no-trunc")
            cmd.append("--format=json")
            cmd.extend(ids)
            result = podman_cmd(cmd, capture_output=True)
            out = json.loads(result.stdout)
            if not isinstance(out, list):
                return [out]
            return out

        if platform.system() == "Windows":
            cmd.append(
                "--format=table {{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}\t{{.NetIO}}\t"
                "{{.BlockIO}}"
            )
        else:
            cmd.append(
                "--format=table {{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}\t{{.MemPerc}}\t"
                "{{.NetIO}}\t{{.BlockIO}}\t{{.PIDs}}"
            )
        cmd.extend(ids)
        podman_cmd(cmd)
        return None

    @overload
    def stats(
        self,
        image_tag: str | None = None,
        container_tag: str | None = None,
        *,
        parse: Literal[True] = ...,
        stream: bool = ...
    ) -> list[Environment.Stats]: ...
    @overload
    def stats(
        self,
        image_tag: str | None = None,
        container_tag: str | None = None,
        *,
        parse: Literal[False],
        stream: bool = ...
    ) -> None: ...
    def stats(
        self,
        image_tag: str | None = None,
        container_tag: str | None = None,
        *,
        parse: bool = True,
        stream: bool = False
    ) -> list[Environment.Stats] | None:
        """Gather resource utilization statistics for containers within this
        environment.

        Parameters
        ----------
        image_tag : str | None, optional
            An optional image tag to gather statistics for.  If None (the default),
            statistics will be gathered for all containers in this environment.
        container_tag : str | None, optional
            An optional container tag to gather statistics for.  If None (the default),
            statistics will be gathered for all containers in the indicated image (or
            whole environment if `image_tag` is None).
        parse : bool, optional
            Whether to parse the output as JSON and return it as a list of dictionaries.
            If false, the output will be printed to stdout in a human-readable table
            format, and nothing will be returned by this method.  Default is True.
        stream : bool, optional
            Whether to continuously update the printed output in a streaming format.
            Incompatible with `parse=True`.  Default is False.

        Returns
        -------
        list[Environment.Stats] | None
            If `parse` is True, then a list of dictionaries containing resource
            utilization statistics for each matching container.  If `parse` is False,
            then None.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container.
        TypeError
            If `image_tag` is None but `container_tag` is not, or if both `parse` and
            `stream` are True.
        """
        if self._lock.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke podman from within a Bertrand container")
        if parse and stream:
            raise TypeError("cannot stream output in json format")

        search = [
            "container",
            "ls",
            "--format", "{{.ID}}",
            "--filter", f"label=BERTRAND_ENV={self.id}",
        ]

        # gather stats for all containers in the environment
        if image_tag is None:
            if container_tag is not None:
                raise TypeError("cannot specify a container when listing all images")
            ids = podman_cmd(search, capture_output=True)
            return self._stats(ids.stdout.strip().splitlines(), parse=parse, stream=stream)

        # gather stats for all containers in an image
        search.append(f"--filter=label=BERTRAND_IMAGE={image_tag}")
        if container_tag is None:
            ids = podman_cmd(search, capture_output=True)
            return self._stats(ids.stdout.strip().splitlines(), parse=parse, stream=stream)

        # gather stats for a specific container in an image
        search.append(f"--filter=label=BERTRAND_CONTAINER={container_tag}")
        ids = podman_cmd(search, capture_output=True)
        return self._stats(ids.stdout.strip().splitlines(), parse=parse, stream=stream)

    def top(self, image_tag: str, container_tag: str) -> None:
        """Print the running processes within a specific container in this
        environment.

        Parameters
        ----------
        image_tag : str
            The image tag containing the container to inspect.
        container_tag : str
            The container tag to inspect.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container.
        KeyError
            If the specified image or container tag could not be found.
        """
        if self._lock.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke podman from within a Bertrand container")

        image = self.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        container = image.containers.get(container_tag)
        if container is None:
            raise KeyError(f"no container found for tag: '{container_tag}'")
        podman_cmd(["container", "top", f"{container.id}"])


def ls_global() -> list[str]:
    """Return a list of paths to all Bertrand environments on the system.

    Returns
    -------
    list[str]
        A list of paths to all Bertrand environments found on the system.

    Raises
    ------
    OSError
        If this function is invoked from within a Bertrand container.
    """
    if Environment.current() is not None:
        raise OSError("cannot invoke podman from within a Bertrand container")

    # find all Bertrand containers on the system
    out = list(podman_cmd([
        "container",
        "ls",
        "--all",
        "--filter", "label=BERTRAND=1",
        "--no-trunc",
        "--format={{.ID}}"
    ], capture_output=True).stdout.strip().splitlines())
    if not out:
        return []

    # inspect all containers to find their mount points
    inspects = json.loads(podman_cmd(["container", "inspect", *out], capture_output=True).stdout)
    assert isinstance(inspects, list)
    seen: set[Path] = set()
    result: list[str] = []
    for inspect in inspects:
        assert isinstance(inspect, dict)
        mount = Container.mount(inspect)  # type: ignore[arg-type]
        if mount is not None and mount not in seen:
            seen.add(mount)
            result.append(str(mount))
    return result


def rm_global(*, assume_yes: bool = False) -> None:
    """Delete all Bertrand environments on the system.

    Parameters
    ----------
    assume_yes : bool, optional
        If True, do not prompt for confirmation before deleting all environments.
        Default is False.

    Raises
    ------
    OSError
        If this function is invoked from within a Bertrand container.
    """
    if Environment.current() is not None:
        raise OSError("cannot invoke podman from within a Bertrand container")

    if confirm(
        "This will delete all Bertrand environments, images, and containers on this "
        "system.  This action cannot be undone.\n"
        "Are you sure you want to continue? [y/N]: ",
        assume_yes=assume_yes
    ):
        for env_path in ls_global():
            try:
                with Environment(Path(env_path)) as env:
                    env.rm()
            except Exception:
                pass


def build_global(*, assume_yes: bool = False) -> None:
    """Build all Bertrand environments on the system.

    Parameters
    ----------
    assume_yes : bool, optional
        If True, do not prompt for confirmation before building all environments.
        Default is False.

    Raises
    ------
    OSError
        If this function is invoked from within a Bertrand container.
    """
    if Environment.current() is not None:
        raise OSError("cannot invoke podman from within a Bertrand container")

    if confirm(
        "This will build all Bertrand environments on this system.  This may take "
        "a long time depending on the number and complexity of the environments.\n"
        "Are you sure you want to continue? [y/N]: ",
        assume_yes=assume_yes
    ):
        for env_path in ls_global():
            try:
                with Environment(Path(env_path)) as env:
                    env.build()
            except Exception:
                pass


def start_global(*, assume_yes: bool = False) -> None:
    """Start all stopped Bertrand containers on the system.

    Parameters
    ----------
    assume_yes : bool, optional
        If True, do not prompt for confirmation before starting all containers.
        Default is False.

    Raises
    ------
    OSError
        If this function is invoked from within a Bertrand container.
    """
    if Environment.current() is not None:
        raise OSError("cannot invoke podman from within a Bertrand container")

    if confirm(
        "This will start all stopped Bertrand containers on this system.\n"
        "Are you sure you want to continue? [y/N]: ",
        assume_yes=assume_yes
    ):
        for env_path in ls_global():
            try:
                with Environment(Path(env_path)) as env:
                    env.start()
            except Exception:
                pass


def run_global(argv: list[str] | None = None, *, assume_yes: bool = False) -> None:
    """Run all stopped Bertrand containers on the system.

    Parameters
    ----------
    argv : list[str] | None, optional
        An optional list of command-line arguments to pass to each container
        when running.  If None (the default), the default command for each container
        will be used.
    assume_yes : bool, optional
        If True, do not prompt for confirmation before running all containers.
        Default is False.

    Raises
    ------
    OSError
        If this function is invoked from within a Bertrand container.
    """
    if Environment.current() is not None:
        raise OSError("cannot invoke podman from within a Bertrand container")

    if confirm(
        "This will run all Bertrand containers on this system.\n"
        "Are you sure you want to continue? [y/N]: ",
        assume_yes=assume_yes
    ):
        for env_path in ls_global():
            try:
                with Environment(Path(env_path)) as env:
                    env.run(argv=argv)
            except Exception:
                pass


def stop_global(*, assume_yes: bool = False) -> None:
    """Stop all running Bertrand containers on the system.

    Parameters
    ----------
    assume_yes : bool, optional
        If True, do not prompt for confirmation before stopping all containers.
        Default is False.

    Raises
    ------
    OSError
        If this function is invoked from within a Bertrand container.
    """
    if Environment.current() is not None:
        raise OSError("cannot invoke podman from within a Bertrand container")

    if confirm(
        "This will stop all running Bertrand containers on this system.\n"
        "Are you sure you want to continue? [y/N]: ",
        assume_yes=assume_yes
    ):
        for env_path in ls_global():
            try:
                with Environment(Path(env_path)) as env:
                    env.stop()
            except Exception:
                pass


def pause_global(*, assume_yes: bool = False) -> None:
    """Pause all running Bertrand containers on the system.

    Parameters
    ----------
    assume_yes : bool, optional
        If True, do not prompt for confirmation before pausing all containers.
        Default is False.

    Raises
    ------
    OSError
        If this function is invoked from within a Bertrand container.
    """
    if Environment.current() is not None:
        raise OSError("cannot invoke podman from within a Bertrand container")

    if confirm(
        "This will pause all running Bertrand containers on this system.\n"
        "Are you sure you want to continue? [y/N]: ",
        assume_yes=assume_yes
    ):
        for env_path in ls_global():
            try:
                with Environment(Path(env_path)) as env:
                    env.pause()
            except Exception:
                pass


def resume_global(*, assume_yes: bool = False) -> None:
    """Resume all paused Bertrand containers on the system.

    Parameters
    ----------
    assume_yes : bool, optional
        If True, do not prompt for confirmation before resuming all containers.
        Default is False.

    Raises
    ------
    OSError
        If this function is invoked from within a Bertrand container.
    """
    if Environment.current() is not None:
        raise OSError("cannot invoke podman from within a Bertrand container")

    if confirm(
        "This will resume all paused Bertrand containers on this system.\n"
        "Are you sure you want to continue? [y/N]: ",
        assume_yes=assume_yes
    ):
        for env_path in ls_global():
            try:
                with Environment(Path(env_path)) as env:
                    env.resume()
            except Exception:
                pass


def prune_global() -> None:
    """Delete all stale Bertrand images and containers on the system.

    Raises
    ------
    OSError
        If this function is invoked from within a Bertrand container.
    """
    if Environment.current() is not None:
        raise OSError("cannot invoke podman from within a Bertrand container")

    for env_path in ls_global():
        try:
            with Environment(Path(env_path)) as env:
                env.prune()
        except Exception:
            pass


def restart_global(*, assume_yes: bool = False) -> None:
    """Restart all running Bertrand containers on the system.

    Parameters
    ----------
    assume_yes : bool, optional
        If True, do not prompt for confirmation before restarting all containers.
        Default is False.

    Raises
    ------
    OSError
        If this function is invoked from within a Bertrand container.
    """
    if Environment.current() is not None:
        raise OSError("cannot invoke podman from within a Bertrand container")

    if confirm(
        "This will restart all running Bertrand containers on this system.\n"
        "Are you sure you want to continue? [y/N]: ",
        assume_yes=assume_yes
    ):
        for env_path in ls_global():
            try:
                with Environment(Path(env_path)) as env:
                    env.restart()
            except Exception:
                pass


@overload
def info_global(*, parse: Literal[True] = ...) -> list[Environment.Info]: ...
@overload
def info_global(*, parse: Literal[False]) -> None: ...
def info_global(*, parse: bool = True) -> list[Environment.Info] | None:
    """Gather status information for all containers in all Bertrand environments
    on the system.

    Parameters
    ----------
    parse : bool, optional
        Whether to parse the output as JSON and return it as a list of dictionaries.
        If false, the output will be printed to stdout in a human-readable table
        format, and nothing will be returned by this function.  Default is True.

    Returns
    -------
    list[Environment.Info] | None
        If `parse` is True, then a list of dictionaries containing status information
        for each container in all Bertrand environments.  If `parse` is False, then
        None.

    Raises
    ------
    OSError
        If this function is invoked from within a Bertrand container.
    """
    if Environment.current() is not None:
        raise OSError("cannot invoke podman from within a Bertrand container")

    cmd = [
        "container",
        "ls",
        "--all",
        "--size",
        "--filter", "label=BERTRAND=1",
    ]

    if parse:
        cmd.append("--no-trunc")
        cmd.append("--format=json")
        result = podman_cmd(cmd, capture_output=True)
        out = json.loads(result.stdout)
        if not isinstance(out, list):
            out = [out]
        return out

    cmd.append(
        "--format=table {{.Names}}\t{{.CreatedAt}}\t{{.State}}\t{{.Command}}\t"
        "{{.RunningFor}}\t{{.Status}}\t{{.Size}}\t{{.Ports}}"
    )
    podman_cmd(cmd)
    return None


@overload
def stats_global(*, parse: Literal[True] = ..., stream: bool = ...) -> list[Environment.Stats]: ...
@overload
def stats_global(*, parse: Literal[False], stream: bool = ...) -> None: ...
def stats_global(*, parse: bool = True, stream: bool = False) -> list[Environment.Stats] | None:
    """Gather resource utilization statistics for all containers in all Bertrand
    environments on the system.

    Parameters
    ----------
    parse : bool, optional
        Whether to parse the output as JSON and return it as a list of dictionaries.
        If false, the output will be printed to stdout in a human-readable table
        format, and nothing will be returned by this function.  Default is True.
    stream : bool, optional
        Whether to continuously update the printed output in a streaming format.
        Incompatible with `parse=True`.  Default is False.

    Returns
    -------
    list[Environment.Stats] | None
        If `parse` is True, then a list of dictionaries containing resource
        utilization statistics for each container in all Bertrand environments.  If
        `parse` is False, then None.

    Raises
    ------
    OSError
        If this function is invoked from within a Bertrand container.
    TypeError
        If both `parse` and `stream` are True.
    """
    if Environment.current() is not None:
        raise OSError("cannot invoke podman from within a Bertrand container")
    if parse and stream:
        raise TypeError("cannot stream output in json format")

    cmd = [
        "container",
        "stats",
        "--no-trunc",
        "--filter", "label=BERTRAND=1",
    ]
    if not stream:
        cmd.append("--no-stream")

    if parse:
        cmd.append("--format=json")
        result = podman_cmd(cmd, capture_output=True)
        out = json.loads(result.stdout)
        if not isinstance(out, list):
            return [out]
        return out

    if platform.system() == "Windows":
        cmd.append(
            "--format=table {{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}\t{{.NetIO}}\t"
            "{{.BlockIO}}"
        )
    else:
        cmd.append(
            "--format=table {{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}\t{{.MemPerc}}\t"
            "{{.NetIO}}\t{{.BlockIO}}\t{{.PIDs}}"
        )
    podman_cmd(cmd)
    return None
