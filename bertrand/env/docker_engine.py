"""A set of pipeline steps that install and start a rootless Docker Engine daemon
owned by Bertrand, without requiring any host Docker components or privileges at
runtime.

The installation process follows Docker's official rootless installation instructions
as closely as possible, and uses the pipeline infrastructure described in `pipeline.py`
to manage state, and allow crash-safe rollback.  Most steps will be persisted between
runs so that installation only needs to be performed once, and will only be invalidated
if this file changes.

Using a rootless Docker Engine allows users to retain root privileges within their
containers, while protecting against privilege escalation on the host, and avoiding any
reliance on host Docker components or group membership.  The rootless daemon is run as
a systemd user service, with a private socket and data directory, and is automatically
started on login, and persisted after logout via linger.
"""
from __future__ import annotations

import os
import shutil
import time
from pathlib import Path
from typing import cast

from .filesystem import Mkdir, WriteText
from .package import (
    AddRepository,
    InstallPackage,
    UninstallPackage,
    detect_package_manager
)
from .pipeline import Pipeline, on_init
from .run import (
    CompletedProcess,
    CommandError,
    User,
    confirm,
    run,
)
from .systemd import (
    StartService,
    RestartService,
    EnableService,
    ReloadDaemon,
)
from .user import EnableLinger, EnsureSubIDs, EnsureUserNamespaces

# pylint: disable=redefined-builtin, unnecessary-pass, broad-except


WAIT_DAEMON_TIMEOUT = 30.0  # seconds
SERVICE_NAME = "bertrand-docker.service"


# shared fact names
USER = "user"
UID = "uid"
GID = "gid"
PACKAGE_MANAGER = "package_manager"
DISTRO_ID = "distro_id"
DISTRO_VERSION = "distro_version"
DISTRO_CODENAME = "distro_codename"
HOST_DOCKER = "host_docker"
ROOTLESS_SH = "rootless_sh"
DOCKER_DATA = "docker_data"
DOCKER_CONFIG = "docker_config"
TIMEOUT = "timeout"


@on_init(requires=[], ephemeral=False)
def detect_platform(ctx: Pipeline.InProgress) -> None:
    """Detect the host platform, package manager, and systemd paths to use when
    installing Docker Engine.  These are persisted as facts in the pipeline context.

    Parameters
    ----------
    ctx : Pipeline.InProgress
        The in-flight pipeline context.

    Raises
    ------
    OSError
        If the systemd user session is not reachable, or if `systemctl` is not
        found.  A user systemd session is required to run the rootless Docker service.
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

    # preflight: ensure systemd user session is reachable
    if not shutil.which("systemctl"):
        raise OSError(
            "systemctl not found. A systemd user session is required to run the "
            "rootless Docker service. Run this in a login session, ensure a user "
            "session exists, or enable linger for the user."
        )
    cp = run(
        ["systemctl", "--user", "is-system-running"],
        check=False,
        capture_output=True,
    )
    state = (cp.stdout or "").strip()
    ok_states = {"running", "degraded", "starting", "maintenance"}
    if state not in ok_states:
        state = state or "unknown"
        raise OSError(
            f"User systemd session is not reachable (state={state}).  A user systemd "
            "session is required to run the rootless Docker service.  Please enable "
            "systemd user sessions for this user and ensure you are running in a login "
            "session, or enable linger for the user."
        )


def _rootless_sh() -> Path | None:
    """Locate `dockerd-rootless.sh` on the host system.

    Returns
    -------
    Path | None
        The resolved path to `dockerd-rootless.sh`, or None if not found.

    Raises
    ------
    OSError
        If the script is found but is not executable.
    """
    which = shutil.which("dockerd-rootless.sh")
    candidates: list[Path] = []
    if which:
        candidates.append(Path(which).expanduser().resolve())
    candidates.extend([
        Path("/usr/bin/dockerd-rootless.sh"),
        Path("/usr/local/bin/dockerd-rootless.sh"),
    ])
    for path in candidates:
        if path.exists():
            if os.access(path, os.X_OK):
                return path
            raise OSError(f"{path} exists but is not executable.")
    return None


def _install_docker_engine_apt(ctx: Pipeline.InProgress) -> None:
    # remove conflicting packages if present
    conflicts = [
        "docker.io",
        "docker-compose",
        "docker-compose-v2",
        "docker-doc",
        "podman-docker",
        "containerd",
        "runc",
    ]
    ctx.do(UninstallPackage(
        manager="apt",
        packages=conflicts
    ))

    # set up Docker official repository
    ctx.do(InstallPackage(
        manager="apt",
        packages=[
            "ca-certificates",
            "curl",
        ],
        refresh=True
    ))
    distro = ctx.get(DISTRO_ID)
    if not isinstance(distro, str):
        raise OSError(f"Invalid distro ID: {distro}")
    codename = ctx.get(DISTRO_CODENAME)
    if not isinstance(codename, str):
        raise OSError(f"Invalid distro codename: {codename}")
    ctx.do(AddRepository(
        manager="apt",
        name="docker",
        url=f"https://download.docker.com/linux/{distro}",
        suite=codename,
        components=["stable"],
        replace=True,
        refresh=True,
        key_url=f"https://download.docker.com/linux/{distro}/gpg",
        key_ext=".asc",
    ))

    # rootless prerequisites
    pkgs = [
        "docker-ce",
        "docker-ce-cli",
        "containerd.io",
        "docker-buildx-plugin",
        "docker-compose-plugin",
        "docker-ce-rootless-extras",
        "uidmap",
        "slirp4netns",
        "fuse-overlayfs",
    ]
    ctx.do(InstallPackage(
        manager="apt",
        packages=pkgs,
        refresh=False
    ))


def _install_docker_engine_dnf(ctx: Pipeline.InProgress) -> None:
    # remove conflicting packages if present
    conflicts = [
        "docker-client",
        "docker-client-latest",
        "docker-common",
        "docker-latest",
        "docker-latest-logrotate",
        "docker-logrotate",
        "docker-selinux",
        "docker-engine-selinux",
        "docker-engine",
        "podman",
        "runc",
    ]
    ctx.do(UninstallPackage(
        manager="dnf",
        packages=conflicts
    ))

    # set up Docker official repository
    ctx.do(InstallPackage(
        manager="dnf",
        packages=["dnf-plugins-core"],
        refresh=True
    ))
    ctx.do(AddRepository(
        manager="dnf",
        name="docker",
        url="https://download.docker.com/linux/fedora/docker-ce.repo",
        repo_url=True,
        replace=True,
        refresh=True,
    ))

    # rootless prerequisites
    pkgs = [
        "docker-ce",
        "docker-ce-cli",
        "containerd.io",
        "docker-buildx-plugin",
        "docker-compose-plugin",
        "docker-ce-rootless-extras",
        "uidmap",
        "slirp4netns",
        "fuse-overlayfs",
        "iptables",
    ]
    ctx.do(InstallPackage(
        manager="dnf",
        packages=pkgs,
        refresh=False
    ))


@on_init(requires=[detect_platform], ephemeral=False)
def install_docker_engine(ctx: Pipeline.InProgress) -> None:
    """Install the base Docker Engine packages required for rootless operation using
    the system package manager.

    Parameters
    ----------
    ctx : Pipeline.InProgress
        The in-flight pipeline context.

    Raises
    ------
    OSError
        If `dockerd-rootless.sh` is not found and installation is declined by the user.
    """
    # check if docker CLI and rootless script are already present
    docker_cli = shutil.which("docker")
    rootless_sh = _rootless_sh()
    if docker_cli and rootless_sh:
        ctx[HOST_DOCKER] = docker_cli
        ctx[ROOTLESS_SH] = str(rootless_sh)
        return

    # prompt to install dependencies
    package_manager = ctx[PACKAGE_MANAGER]
    if not confirm(
        "Bertrand requires its own rootless Docker daemon, but `dockerd-rootless.sh` "
        "was not found.\n"
        f"Would you like to install the required packages now using {package_manager} "
        "(requires sudo).\n"
        "[y/N] ",
        assume_yes=False,
    ):
        raise OSError("Installation declined by user.")

    # follow official docker installation instructions for the detected distro
    if package_manager == "apt":
        _install_docker_engine_apt(ctx)
    elif package_manager == "dnf":
        _install_docker_engine_dnf(ctx)
    else:
        raise OSError(f"Unknown package manager: '{package_manager}'")

    # verify installation and record facts for later steps
    docker_cli = shutil.which("docker")
    rootless_sh = _rootless_sh()
    if not docker_cli or not rootless_sh:
        raise OSError(
            "Installation completed, but `docker` CLI or `dockerd-rootless.sh` still not "
            "found.  Please investigate the issue and ensure the required packages are "
            "installed."
        )
    ctx[HOST_DOCKER] = docker_cli
    ctx[ROOTLESS_SH] = str(rootless_sh)


@on_init(requires=[install_docker_engine], ephemeral=False)
def enable_user_namespaces(ctx: Pipeline.InProgress) -> None:
    """Ensure unprivileged user namespaces are enabled on the host system, which are
    required for rootless Docker operation, per Docker documentation.

    Parameters
    ----------
    ctx : Pipeline.InProgress
        The in-flight pipeline context.
    """
    ctx.do(EnsureUserNamespaces(
        needed=15000,
        prompt=(
            "Rootless Docker requires unprivileged user namespaces to be enabled on "
            "the host system.  This may require root privileges to enable.\nDo you "
            "want to proceed? [y/N] "
        )
    ))


@on_init(requires=[install_docker_engine], ephemeral=False)
def provision_subids(ctx: Pipeline.InProgress) -> None:
    """Ensure subordinate UID/GID ranges are allocated for the host user in
    /etc/subuid and /etc/subgid, which are required for rootless Docker operation per
    Docker documentation.

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
            "Rootless Docker requires subordinate UID/GID ranges (>= 65536) in "
            "/etc/subuid and /etc/subgid.  This may require root privileges to "
            "provision.\nDo you want to proceed? [y/N] "
        )
    ))


def _systemd_env(ctx: Pipeline.InProgress) -> dict[str, str]:
    env = os.environ.copy()
    uid = ctx.get(UID)
    if not isinstance(uid, int):
        raise OSError(f"Invalid UID: {uid}")
    xdg = env.setdefault("XDG_RUNTIME_DIR", f"/run/user/{uid}")
    env["DBUS_SESSION_BUS_ADDRESS"] = f"unix:path={xdg}/bus"
    return env


@on_init(requires=[provision_subids, enable_user_namespaces], ephemeral=False)
def write_systemd_unit(ctx: Pipeline.InProgress) -> None:
    """Write the `systemd --user` unit file for Bertrand's rootless Docker daemon, and
    reload the systemd user daemon if the unit file changed.

    Parameters
    ----------
    ctx : Pipeline.InProgress
        The in-flight pipeline context.

    Raises
    ------
    OSError
        If the UID fact is not a valid integer, or if the ROOTLESS_SH fact is not a
        valid string.  These should have been set by previous steps.
    """
    # store docker state in the `on_init` state directory
    data_dir = ctx.state_dir / "docker_data"
    config_dir = ctx.state_dir / "docker_config"
    ctx[DOCKER_DATA] = str(data_dir)
    ctx[DOCKER_CONFIG] = str(config_dir)
    ctx.do(Mkdir(
        path=data_dir,
        private=True
    ), undo=False)
    ctx.do(Mkdir(
        path=config_dir,
        private=True
    ), undo=False)

    # render unit file
    systemd_user_dir = Path.home() / ".config" / "systemd" / "user"
    ctx.do(Mkdir(path=systemd_user_dir), undo=False)
    rootless_sh = ctx.get(ROOTLESS_SH)
    if not isinstance(rootless_sh, str):
        raise OSError(f"Invalid path to dockerd-rootless.sh: {rootless_sh}")
    ctx.do(WriteText(
        path=systemd_user_dir / SERVICE_NAME,
        text=f"""\
[Unit]
Description=Bertrand Rootless Docker Daemon
After=default.target

[Service]
Type=simple
Delegate=yes
TasksMax=infinity
UMask=0077

# systemd creates %t/bertrand-docker with correct ownership
RuntimeDirectory=bertrand-docker
RuntimeDirectoryMode=0700

# rootless networking via slirp4netns
Environment=DOCKERD_ROOTLESS_ROOTLESSKIT_NET=slirp4netns
Environment=DOCKERD_ROOTLESS_ROOTLESSKIT_PORT_DRIVER=builtin

# allow rootless helpers to find necessary tools
Environment=PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

# clean stale artifacts
ExecStartPre=/usr/bin/rm -f %t/bertrand-docker/docker.sock %t/bertrand-docker/dockerd.pid
ExecStartPre=/usr/bin/mkdir -p %t/bertrand-docker/exec-root

# start rootless dockerd
ExecStart={rootless_sh} \\
  --host=unix://%t/bertrand-docker/docker.sock \\
  --data-root={data_dir} \\
  --exec-root=%t/bertrand-docker/exec-root \\
  --pidfile=%t/bertrand-docker/dockerd.pid

ExecReload=/bin/kill -s HUP $MAINPID
Restart=on-failure
RestartSec=2
TimeoutStartSec={int(WAIT_DAEMON_TIMEOUT)}
LimitNOFILE=1048576

# don't kill the whole cgroup on restart/stop - that can nuke container shims
KillMode=process

[Install]
WantedBy=default.target
""",
        encoding="utf-8",
        replace=True,
    ))

    # reload systemd user daemon to pick up changes
    env = _systemd_env(ctx)
    ctx.do(ReloadDaemon(user=True, env=env), undo=False)

    # restart the service if it was previously running
    ctx.do(RestartService(
        name=SERVICE_NAME,
        user=True,
        env=env
    ), undo=False)


@on_init(requires=[write_systemd_unit], ephemeral=False)
def enable_background_service(ctx: Pipeline.InProgress) -> None:
    """Mark the service as a background service in the registry, so it is not stopped
    automatically on pipeline completion.

    Parameters
    ----------
    ctx : Pipeline.InProgress
        The in-flight pipeline context.

    Raises
    ------
    OSError
        If the UID or USER facts are invalid.  These should have been set by the
        `detect_platform` step.
    """
    # enable on login
    ctx.do(EnableService(
        name=SERVICE_NAME,
        user=True,
        env=_systemd_env(ctx)
    ))

    # enable linger so the service can persist after logout
    user = ctx.get(USER)
    if not isinstance(user, str):
        raise OSError(f"Invalid user: {user}")
    ctx.do(EnableLinger(
        user=user,
        prompt=(
            f"Persist Docker containers even after '{user}' logs out?  This may require "
            "root privileges to enable.\nDo you want to proceed? [y/N] "
        )
    ))


def _docker_env(ctx: Pipeline | Pipeline.InProgress) -> dict[str, str]:
    env = os.environ.copy()
    uid = ctx.get(UID)
    if not isinstance(uid, int):
        raise OSError(f"Invalid UID: {uid}")
    docker_config = ctx.get(DOCKER_CONFIG)
    if not isinstance(docker_config, str):
        raise OSError(f"Invalid Docker config path: {docker_config}")

    xdg = env.setdefault("XDG_RUNTIME_DIR", f"/run/user/{uid}")
    env["DOCKER_HOST"] = f"unix://{xdg}/bertrand-docker/docker.sock"
    env["DOCKER_CONFIG"] = docker_config
    env.setdefault("DOCKER_BUILDKIT", "1")  # enable buildkit by default
    for k in (
        "DOCKER_CONTEXT",
        "DOCKER_TLS_VERIFY",
        "DOCKER_CERT_PATH",
        "DOCKER_MACHINE_NAME",
    ):
        env.pop(k, None)
    return env


@on_init(requires=[enable_background_service], ephemeral=True)
def start_user_service(ctx: Pipeline.InProgress) -> None:
    """Start the rootless docker systemd service if it was not already started.

    Parameters
    ----------
    ctx : Pipeline.InProgress
        The in-flight pipeline context.

    Raises
    ------
    OSError
        If the Docker daemon does not become reachable within the timeout, or if the
        UID, DOCKER_CONFIG, or HOST_DOCKER, or TIMEOUT facts are invalid.  These should
        have been set by previous steps, or by command-line arguments in the case of
        TIMEOUT.
    CommandError
        If a `docker info` command fails before the daemon becomes reachable.
    """
    ctx.do(StartService(
        name=SERVICE_NAME,
        user=True,
        env=_systemd_env(ctx)
    ), undo=False)

    # wait for the daemon socket to become available
    env = _docker_env(ctx)
    host_docker = ctx.get(HOST_DOCKER)
    if not isinstance(host_docker, str):
        raise OSError(f"Invalid path to docker CLI: {host_docker}")
    last: CommandError | None = None
    timeout = ctx.get(TIMEOUT, WAIT_DAEMON_TIMEOUT)
    if not isinstance(timeout, (int, float)):
        raise OSError(f"Invalid timeout value: {timeout}")
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            # invoke host docker with proper env to target the rootless socket
            run([host_docker, "info"], capture_output=True, env=env)
            return
        except CommandError as err:
            last = err
            time.sleep(0.2)
    if last:
        raise last
    raise OSError("Timed out waiting for Bertrand rootless Docker daemon.")


def docker_cmd(
    args: list[str],
    *,
    check: bool = True,
    capture_output: bool | None = False,
    input: str | None = None,
    cwd: Path | None = None
) -> CompletedProcess:
    """Bertrand-only docker command. Always targets the Bertrand rootless daemon.

    Parameters
    ----------
    args : list[str]
        The docker command arguments (excluding the "docker" executable).
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
    with on_init:
        return run(
            [cast(str, on_init[HOST_DOCKER]), *args],
            check=check,
            capture_output=capture_output,
            input=input,
            cwd=cwd,
            env=_docker_env(on_init),
        )


def docker_exec(args: list[str]) -> None:
    """Execute a docker command against the Bertrand rootless daemon, replacing the
    current process.

    Parameters
    ----------
    args : list[str]
        The docker command arguments (excluding the "docker" executable).

    Raises
    ------
    OSError
        If execution fails.
    """
    with on_init:
        cmd = cast(str, on_init[HOST_DOCKER])
        os.execvpe(cmd, [cmd, *args], _docker_env(on_init))
