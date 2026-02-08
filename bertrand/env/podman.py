"""Install and run a rootless OCI container engine (podman) to orchestrate Bertrand's
CLI.
"""
from __future__ import annotations

import json as json_parser
import os
import re
import shlex
import shutil
import sys
import uuid

from dataclasses import dataclass, asdict
from datetime import datetime, timezone
from pathlib import Path
from resource import getpagesize
from types import TracebackType
from typing import (
    Annotated,
    Any,
    Iterable,
    Literal,
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
    ValidationError,
    model_validator,
)

from .package import (
    InstallPackage,
    detect_package_manager
)
from .pipeline import (
    JSONView,
    Pipeline,
    on_init,
    on_ls,
    on_rm,
    on_build,
    on_start,
    on_enter,
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


@on_init(requires=[])
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


@on_init(requires=[detect_platform])
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


@on_init(requires=[install_container_cli])
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


@on_init(requires=[install_container_cli])
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


@on_init(requires=[provision_subids, enable_user_namespaces])
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
        if result.returncode != 0 or not result.stdout:
            return None
        data = json_parser.loads(result.stdout)
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
            data = json_parser.loads(env_file.read_text(encoding="utf-8"))
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
            json_parser.dumps(self._json.model_dump(mode="json"), indent=2) + "\n"
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
            the command line or `$ bertrand ls`.

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
        containers = list(podman_cmd([
            "container",
            "ls",
            "-a",
            "-q",
            "--no-trunc",
            "--filter", f"label=BERTRAND_ENV={self.id}",
        ], capture_output=True).stdout.splitlines())
        images = list(podman_cmd([
            "image",
            "ls",
            "-a",
            "-q",
            "--no-trunc",
            "--filter", f"label=BERTRAND_ENV={self.id}",
        ], capture_output=True).stdout.splitlines())

        # remove containers first
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


# pylint: disable=missing-function-docstring, missing-return-doc, unused-argument


def _list_containers(env: Environment, image_tag: str, container_tag: str) -> list[str]:
    image = env.tags.get(image_tag)
    if image is None:
        return []
    container = image.containers.get(container_tag)
    if container is None:
        return []
    return [f"{env.root}:{image_tag}:{container_tag}"]


def _list_images(env: Environment, image_tag: str) -> list[str]:
    image = env.tags.get(image_tag)
    if image is None:
        return []
    return [f"{env.root}:{image_tag}:{c}" for c in image.containers]


def _list_environments(env: Environment) -> list[str]:
    return [f"{env.root}:{k}" for k in env.tags]


def _list_all() -> list[str]:
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
    inspects = json_parser.loads(podman_cmd(
        ["container", "inspect", *out],
        capture_output=True
    ).stdout)
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


# TODO: remove timeout argument from all commands and use TIMEOUT instead.


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
        sanitized = _sanitize_name(x)
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
        sanitized = _sanitize_name(x)
        if x != sanitized:
            raise ValueError(
                f"invalid container tag: '{x}' (must contain only alphanumerics, '_', or '.')"
            )
        return x

    @staticmethod
    def _validate_timeout(x: JSONView) -> int:
        if x is None:
            return TIMEOUT
        if not isinstance(x, int):
            raise TypeError("timeout must be an integer")
        if x < -1:
            raise ValueError("timeout must be non-negative or -1 for no timeout")
        return x

    env = _validate_env
    image_tag = _validate_image
    container_tag = _validate_container
    timeout = _validate_timeout

    def _call(self, ctx: Pipeline.InProgress) -> None:
        # pylint: disable=no-member
        # extract and validate all arguments from the context
        kwargs = {k: v(ctx.get(k)) for k, v in asdict(self).items()}

        # if no environment is given, call the subclass's `all()` method
        env_root = kwargs.pop("env")
        if env_root is None:
            if kwargs["image_tag"] or kwargs["container_tag"]:
                raise TypeError("cannot specify image or container tag when environment is None")
            kwargs["env"] = None
            self.all(**kwargs)  # type: ignore[attr-defined]
            return

        # load environment metadata
        with Environment(env_root, timeout=kwargs["timeout"]) as env:
            kwargs["env"] = env

            # if no image tag is given, call the subclass's `environment()` method
            if kwargs["image_tag"]:
                if kwargs["container_tag"]:
                    raise TypeError("cannot specify a container when image tag is None")
                self.environment(**kwargs)  # type: ignore[attr-defined]
                return

            # if no container tag is given, call the subclass's `image()`
            if kwargs["container_tag"]:
                self.image(**kwargs)  # type: ignore[attr-defined]
                return

            # otherwise, call the subclass's `container()` method
            self.container(**kwargs)  # type: ignore[attr-defined]

    def __call__(self, ctx: Pipeline.InProgress) -> None:
        if Environment.current() is not None:
            raise OSError("cannot invoke podman from within a Bertrand container")
        self._call(ctx)


@dataclass
class Build(_Command):
    """Incrementally build Bertrand images/containers within an environment, scoping to
    specific images and containers if desired.  This command does not start any
    containers, but may rebuild and restart existing containers if their parent images
    are out of date.

    If an image tag is provided, then arbitrary additional command-line arguments may
    be passed to assign to the tag, making them available for use in downstream
    commands.  If no image tag is provided, then no additional arguments may be passed,
    and all images in the environment will be built using their currently-tagged
    arguments.
    """

    @staticmethod
    def _validate_args(x: JSONView) -> list[str]:
        if x is None:
            return []
        if isinstance(x, tuple) and all(isinstance(i, str) for i in x):
            return list(cast(tuple[str, ...], x))
        raise TypeError("args must be a list of strings")

    args = _validate_args

    @staticmethod
    def container(
        *,
        env: Environment,
        image_tag: str,
        container_tag: str,
        args: list[str],
        **kwargs: Any
    ) -> Container:
        raise OSError(
            "The 'build' command is reserved for compiling images.  Use 'start' or "
            "'enter' to run containers from a prebuilt image instead."
        )

    @staticmethod
    def image(
        *,
        env: Environment,
        image_tag: str,
        args: list[str],
        **kwargs: Any
    ) -> Image:
        if args and not image_tag:
            raise OSError("images with non-default arguments must have a tag")

        existing = env.tags.get(image_tag)
        if not args:
            if existing is None:
                if image_tag:
                    raise KeyError(f"no image found for tag: '{image_tag}'")
                args = []  # empty tag implies empty args
            else:
                args = existing.args  # reuse existing args
        else:
            args = _normalize_args(args)  # define new args

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
            args=args,
            containers={},
        )
        image_name = f"{_sanitize_name(env.root.name)}.{image_tag}.{env.id[:13]}"
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
                "-f", str(env.container_file),
                "--iidfile", str(iid_file),
                "--label", "BERTRAND=1",
                "--label", f"BERTRAND_ENV={env.id}",
                "--label", f"BERTRAND_IMAGE={image_tag}",
                *build_args,
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
                    env_root=env.root,
                    env_uuid=env.id,
                    image_tag=image_tag,
                    container_tag=container_tag,
                    container_args=container.args
                )
            image.remove(force=True, timeout=env.timeout, missing_ok=True)
        except Exception as err:
            for container in image.containers.values():
                podman_cmd(["container", "rm", "-f", container.id], check=False)
            podman_cmd(["image", "rm", "-f", image.id], check=False)
            raise err
        env.tags[image_tag] = image
        return image

    @staticmethod
    def environment(
        *,
        env: Environment,
        args: list[str],
        **kwargs: Any
    ) -> Environment:
        if args:
            raise OSError(
                "An environment's default image cannot have arguments.  Either specify "
                "an image tag to assign to these arguments, or set the appropriate "
                "defaults in the environment's Containerfile instead."
            )
        Build.image(env=env, image_tag="", args=args, **kwargs)
        return env

    @staticmethod
    def all(
        *,
        args: list[str],
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
        *,
        env: Environment,
        image_tag: str,
        container_tag: str,
        args: list[str],
        **kwargs: Any
    ) -> Container:
        image = env.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        container = Build.container(
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
            args=args,
        )
        inspect = container.inspect()
        if inspect is None:
            raise OSError(
                f"failed to build container '{container_tag}' in image '{image_tag}'"
            )
        Container.start(inspect)
        return container

    @staticmethod
    def image(
        *,
        env: Environment,
        image_tag: str,
        args: list[str],
        **kwargs: Any
    ) -> Image:
        image = env.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        if args:
            raise OSError("cannot specify arguments when starting an image")
        image = Build.image(env=env, image_tag=image_tag, args=args, **kwargs)
        for container_tag, container in image.containers.items():
            inspect = container.inspect()
            if inspect is None:
                raise OSError(
                    f"failed to build container '{container_tag}' in image '{image_tag}'"
                )
            Container.start(inspect)
        return image

    @staticmethod
    def environment(
        *,
        env: Environment,
        args: list[str],
        **kwargs: Any
    ) -> Environment:
        if args:
            raise OSError("cannot specify arguments when starting a whole environment")
        Build.environment(env=env, args=args, **kwargs)
        for image_tag, image in env.tags.items():
            for container_tag, container in image.containers.items():
                inspect = container.inspect()
                if not inspect:
                    raise OSError(
                        f"failed to build container '{container_tag}' in image '{image_tag}'"
                    )
                Container.start(inspect)
        return env

    @staticmethod
    def all(
        *,
        args: list[str],
        **kwargs: Any
    ) -> None:
        if args:
            raise OSError("cannot specify arguments when starting all environments")

        if confirm(
            "This will start all Bertrand containers on this system.  This may take "
            "a long time depending on the number and complexity of the environments.\n"
            "Are you sure you want to continue? [y/N] "
        ):
            for env_path in _list_all():
                try:
                    with Environment(Path(env_path)) as env:
                        for image_tag, image in env.tags.items():
                            for container_tag, container in image.containers.items():
                                inspect = container.inspect()
                                if inspect is None:
                                    raise OSError(
                                        f"failed to build container '{container_tag}' in "
                                        f"image '{image_tag}'"
                                    )
                                Container.start(inspect)
                except Exception as err:
                    print(err, file=sys.stderr)


@dataclass
class Enter(_Command):
    """Replace the current process with an interactive shell inside the specified
    container, starting or rebuilding it as necessary.  This is equivalent to `Start`
    followed by a `podman container exec {shell}` command on a single container,
    where `{shell}` is set by the parent environment.
    """

    @staticmethod
    def container(
        *,
        env: Environment,
        image_tag: str,
        container_tag: str,
        args: list[str],
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

        container = Start.container(
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
            args=args,
            **kwargs
        )
        podman_exec([
            "exec",
            "-it",
            "-w", MOUNT,
            container.id,
            *env.shell,
        ])

    @staticmethod
    def image(
        *,
        env: Environment,
        image_tag: str,
        args: list[str],
        **kwargs: Any
    ) -> None:
        Enter.container(
            env=env,
            image_tag=image_tag,
            container_tag="",  # default container
            args=args,
            **kwargs
        )

    @staticmethod
    def environment(
        *,
        env: Environment,
        args: list[str],
        **kwargs: Any
    ) -> None:
        Enter.container(
            env=env,
            image_tag="",  # default image
            container_tag="",  # default container
            args=args,
            **kwargs
        )

    @staticmethod
    def all(
        *,
        args: list[str],
        **kwargs: Any
    ) -> None:
        raise OSError("must specify a container to enter")


@dataclass
class Run(_Command):
    """Run an arbitrary command inside a container's context, starting or rebuilding it
    as necessary.  This is equivalent to `Start` followed by a `podman container exec`
    command on a single container, forwarding any arguments to the `exec` portion.
    """

    @staticmethod
    def container(
        *,
        env: Environment,
        image_tag: str,
        container_tag: str,
        args: list[str],
        **kwargs: Any
    ) -> None:
        container = Start.container(
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
            args=[],
            **kwargs
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
            "-w", MOUNT,
            container.id,
            *args
        ])

    @staticmethod
    def image(
        *,
        env: Environment,
        image_tag: str,
        args: list[str],
        **kwargs: Any
    ) -> None:
        Run.container(
            env=env,
            image_tag=image_tag,
            container_tag="",  # default container
            args=args,
            **kwargs
        )

    @staticmethod
    def environment(
        *,
        env: Environment,
        args: list[str],
        **kwargs: Any
    ) -> None:
        Run.container(
            env=env,
            image_tag="",  # default image
            container_tag="",  # default container
            args=args,
            **kwargs
        )

    @staticmethod
    def all(
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
    def container(
        *,
        env: Environment,
        image_tag: str,
        container_tag: str,
        timeout: int,
        **kwargs: Any
    ) -> Container:
        image = env.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        container = image.containers.get(container_tag)
        if container is None:
            raise KeyError(f"no container found for tag: '{container_tag}'")
        inspect = container.inspect()
        if inspect is None:
            raise OSError(
                f"failed to inspect container '{container_tag}' in image '{image_tag}'"
            )
        Container.stop(inspect, timeout=timeout)
        return container

    @staticmethod
    def image(
        *,
        env: Environment,
        image_tag: str,
        timeout: int,
        **kwargs: Any
    ) -> Image:
        image = env.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        image_inspect = image.inspect()
        if image_inspect is None:
            raise OSError(f"failed to inspect image '{image_tag}'")
        for container in image.containers.values():
            inspect = container.inspect()
            if inspect is not None:
                Container.stop(inspect, timeout=timeout)
        return image

    @staticmethod
    def environment(
        *,
        env: Environment,
        timeout: int,
        **kwargs: Any
    ) -> Environment:
        for image in env.tags.values():
            for container in image.containers.values():
                inspect = container.inspect()
                if inspect is not None:
                    Container.stop(inspect, timeout=timeout)
        return env

    @staticmethod
    def all(
        *,
        timeout: int,
        **kwargs: Any
    ) -> None:
        if confirm(
            "This will stop all running Bertrand containers on this system.\n"
            "Are you sure you want to continue? [y/N] "
        ):
            for env_path in _list_all():
                try:
                    with Environment(Path(env_path)) as env:
                        Stop.environment(env=env, timeout=timeout, **kwargs)
                except Exception as err:
                    print(err, file=sys.stderr)


@dataclass
class Pause(_Command):
    """Pause running Bertrand containers within an environment, scoping to specific
    images and containers if desired.
    """

    @staticmethod
    def container(
        *,
        env: Environment,
        image_tag: str,
        container_tag: str,
        **kwargs: Any
    ) -> Container:
        image = env.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        container = image.containers.get(container_tag)
        if container is None:
            raise KeyError(f"no container found for tag: '{container_tag}'")
        inspect = container.inspect()
        if inspect is None:
            raise OSError(
                f"failed to inspect container '{container_tag}' in image '{image_tag}'"
            )
        Container.pause(inspect)
        return container

    @staticmethod
    def image(
        *,
        env: Environment,
        image_tag: str,
        **kwargs: Any
    ) -> Image:
        image = env.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        image_inspect = image.inspect()
        if image_inspect is None:
            raise OSError(f"failed to inspect image '{image_tag}'")
        for container in image.containers.values():
            inspect = container.inspect()
            if inspect is not None:
                Container.pause(inspect)
        return image

    @staticmethod
    def environment(
        *,
        env: Environment,
        **kwargs: Any
    ) -> Environment:
        for image in env.tags.values():
            for container in image.containers.values():
                inspect = container.inspect()
                if inspect is not None:
                    Container.pause(inspect)
        return env

    @staticmethod
    def all(
        **kwargs: Any
    ) -> None:
        if confirm(
            "This will pause all running Bertrand containers on this system.\n"
            "Are you sure you want to continue? [y/N] "
        ):
            for env_path in _list_all():
                try:
                    with Environment(Path(env_path)) as env:
                        Pause.environment(env=env, **kwargs)
                except Exception as err:
                    print(err, file=sys.stderr)


@dataclass
class Resume(_Command):
    """Resume paused Bertrand containers within an environment, scoping to specific
    images and containers if desired.
    """

    @staticmethod
    def container(
        *,
        env: Environment,
        image_tag: str,
        container_tag: str,
        **kwargs: Any
    ) -> Container:
        image = env.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        container = image.containers.get(container_tag)
        if container is None:
            raise KeyError(f"no container found for tag: '{container_tag}'")
        inspect = container.inspect()
        if inspect is None:
            raise OSError(
                f"failed to inspect container '{container_tag}' in image '{image_tag}'"
            )
        Container.resume(inspect)
        return container

    @staticmethod
    def image(
        *,
        env: Environment,
        image_tag: str,
        **kwargs: Any
    ) -> Image:
        image = env.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        image_inspect = image.inspect()
        if image_inspect is None:
            raise OSError(f"failed to inspect image '{image_tag}'")
        for container in image.containers.values():
            inspect = container.inspect()
            if inspect is not None:
                Container.resume(inspect)
        return image

    @staticmethod
    def environment(
        *,
        env: Environment,
        **kwargs: Any
    ) -> Environment:
        for image in env.tags.values():
            for container in image.containers.values():
                inspect = container.inspect()
                if inspect is not None:
                    Container.resume(inspect)
        return env

    @staticmethod
    def all(
        **kwargs: Any
    ) -> None:
        if confirm(
            "This will resume all paused Bertrand containers on this system.\n"
            "Are you sure you want to continue? [y/N] "
        ):
            for env_path in _list_all():
                try:
                    with Environment(Path(env_path)) as env:
                        Resume.environment(env=env, **kwargs)
                except Exception as err:
                    print(err, file=sys.stderr)


@dataclass
class Restart(_Command):
    """Restart running or paused Bertrand containers within an environment, scoping to
    specific images and containers if desired.  If an image or container is out of
    date, then it will be rebuilt before restarting.
    """

    @staticmethod
    def container(
        *,
        env: Environment,
        image_tag: str,
        container_tag: str,
        timeout: int,
        **kwargs: Any
    ) -> Container:
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
            running = Container.running(inspect)

        # possibly rebuild the parent image and all downstream containers
        image = Build.image(env=env, image_tag=image_tag, args=[], **kwargs)

        # if the container was previously running, restart it
        container = image.containers.get(container_tag)  # may have drifted
        if container is None:
            raise KeyError(f"no container found for tag: '{container_tag}' after rebuild")
        inspect = container.inspect()
        if inspect is None:
            raise OSError(
                f"failed to inspect container '{container_tag}' in image '{image_tag}' "
                "after rebuild"
            )
        if running:
            if Container.new(inspect):  # newly-rebuilt
                Container.start(inspect)
            else:
                Container.restart(inspect, timeout=timeout)  # still running
        return container

    @staticmethod
    def image(
        *,
        env: Environment,
        image_tag: str,
        timeout: int,
        **kwargs: Any
    ) -> Image:
        image = env.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")

        # detect whether any containers are currently running
        running: set[str] = set()
        for container_tag, c in image.containers.items():
            inspect = c.inspect()
            if inspect is not None and Container.running(inspect):
                running.add(container_tag)

        # possibly rebuild the image and all downstream containers
        image = Build.image(env=env, image_tag=image_tag, args=[], **kwargs)

        # restart any previously-running containers
        for container_tag in running:
            container = image.containers.get(container_tag)  # may have drifted
            if container is None:
                continue  # container was removed during rebuild, skip it
            inspect = container.inspect()
            if inspect is None:
                continue  # container was removed during rebuild, skip it
            if Container.new(inspect):  # newly-rebuilt
                Container.start(inspect)
            else:
                Container.restart(inspect, timeout=timeout)  # still running

        return image

    @staticmethod
    def environment(
        *,
        env: Environment,
        timeout: int,
        **kwargs: Any
    ) -> Environment:
        for image_tag in env.tags:
            Restart.image(env=env, image_tag=image_tag, timeout=timeout, **kwargs)
        return env

    @staticmethod
    def all(
        *,
        timeout: int,
        **kwargs: Any
    ) -> None:
        if confirm(
            "This will restart all running Bertrand containers on this system.  This may "
            "take a long time depending on the number and complexity of the environments.\n"
            "Are you sure you want to continue? [y/N] "
        ):
            for env_path in _list_all():
                try:
                    with Environment(Path(env_path)) as env:
                        Restart.environment(env=env, timeout=timeout, **kwargs)
                except Exception as err:
                    print(err, file=sys.stderr)


@dataclass
class Prune(_Command):
    """Remove all stopped Bertrand images and containers within an environment, scoping
    to specific images and containers if desired.
    
    This command only deletes images and containers, and never alters the host
    filesystem or existing tags.  Any images or containers that are removed will be
    rebuilt the next time they are started.
    """

    @staticmethod
    def container(
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
        if inspect is not None and Container.stopped(inspect):
            container.remove(force=True, timeout=env.timeout, missing_ok=True)

    @staticmethod
    def image(
        *,
        env: Environment,
        image_tag: str,
        **kwargs: Any
    ) -> None:
        image = env.tags.get(image_tag)
        if image is None:
            return None

        # remove any stopped containers for this image
        for container in image.containers.values():
            inspect = container.inspect()
            if inspect is not None and Container.stopped(inspect):
                container.remove(force=True, timeout=env.timeout, missing_ok=True)

        # check whether the image is now dangling
        containers = list(podman_cmd([
            "container",
            "ls",
            "-a",
            "-q",
            "--no-trunc",
            "--filter", f"ancestor={image.id}",
        ], capture_output=True).stdout.splitlines())
        if not containers:
            podman_cmd(["image", "rm", "-f", "i", image.id])

        # remove any now-dangling volumes
        _remove_dangling_volumes(force=True, missing_ok=True)

    @staticmethod
    def environment(
        *,
        env: Environment,
        **kwargs: Any
    ) -> None:
        for image_tag in env.tags:
            Prune.image(env=env, image_tag=image_tag, **kwargs)

    @staticmethod
    def all(**kwargs: Any) -> None:
        if confirm(
            "This will prune all stopped Bertrand containers and dangling images on this "
            "system.  This may take a long time depending on the number and complexity "
            "of the environments.\n"
            "Are you sure you want to continue? [y/N] "
        ):
            for env_path in _list_all():
                try:
                    with Environment(Path(env_path)) as env:
                        Prune.environment(env=env, **kwargs)
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
    force = bool
    missing_ok = bool

    @staticmethod
    def container(
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
        *,
        env: Environment,
        force: bool,
        timeout: int,
        **kwargs: Any
    ) -> None:
        env.remove(force=force, timeout=timeout, missing_ok=True)
        env.tags.clear()

    @staticmethod
    def all(
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
            for env_path in _list_all():
                try:
                    with Environment(Path(env_path)) as env:
                        env.remove(force=force, timeout=timeout, missing_ok=True)
                        env.tags.clear()
                except Exception:
                    pass


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

    json = bool
    images = bool
    running = bool
    stopped = bool

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
    def _validate_interval(json: bool, interval: int) -> None:
        if interval < 0:
            raise ValueError("interval must be non-negative")
        if json and interval:
            raise ValueError("cannot use 'json' and 'interval' together")

    json = bool
    interval = _validate_interval

    @staticmethod
    def _format(
        labels: list[str],
        *,
        json: bool,
        interval: int
    ) -> list[Monitor.JSON] | None:
        # first, get the container ids for all labels
        ids = list(podman_cmd([
            "container",
            "ls",
            "-a",
            "-q",
            *labels,
        ], capture_output=True).stdout.strip().splitlines())
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
        *,
        env: Environment,
        image_tag: str,
        **kwargs: Any
    ) -> None:
        Top.container(
            env=env,
            image_tag=image_tag,
            container_tag="",  # default container
            **kwargs
        )

    @staticmethod
    def environment(
        *,
        env: Environment,
        **kwargs: Any
    ) -> None:
        Top.container(
            env=env,
            image_tag="",  # default image
            container_tag="",  # default container
            **kwargs
        )

    @staticmethod
    def all(
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
        podman_cmd(cmd, capture_output=True)

    @staticmethod
    def _format_image(image: Image) -> None:
        podman_cmd([
            "image",
            "history",
            "--human",
            (
                "--format=table {{.CreatedAt}}\t{{.CreatedSince}}\t{{.CreatedBy}}\t"
                "{{.Size}}\t{{.Comment}}"
            )
        ])

    @staticmethod
    def container(
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
        *,
        env: Environment,
        images: bool,
        since: str | None,
        until: str | None,
        **kwargs: Any
    ) -> None:
        return Log.image(
            env=env,
            image_tag="",  # default image
            images=images,
            since=since,
            until=until,
            **kwargs
        )

    @staticmethod
    def all(
        *,
        images: bool,
        since: str | None,
        until: str | None,
        **kwargs: Any
    ) -> None:
        raise OSError("must specify an image or container to view logs for")


podman_build: Build = on_build(Build(), ephemeral=True)
podman_start: Start = on_start(Start(), ephemeral=True)
podman_enter: Enter = on_enter(Enter(), ephemeral=True)
podman_run: Run = on_run(Run(), ephemeral=True)
podman_stop: Stop = on_stop(Stop(), ephemeral=True)
podman_pause: Pause = on_pause(Pause(), ephemeral=True)
podman_resume: Resume = on_resume(Resume(), ephemeral=True)
podman_restart: Restart = on_restart(Restart(), ephemeral=True)
podman_prune: Prune = on_prune(Prune(), ephemeral=True)
podman_rm: Rm = on_rm(Rm(), ephemeral=True)
podman_ls: Ls = on_ls(Ls(), ephemeral=True)
podman_monitor: Monitor = on_monitor(Monitor(), ephemeral=True)
podman_top: Top = on_top(Top(), ephemeral=True)
podman_log: Log = on_log(Log(), ephemeral=True)
