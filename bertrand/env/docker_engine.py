"""Bertrand rootless Docker: install prerequisites, provision subuid/subgid, and run a
Bertrand-owned rootless `dockerd` bound to a private socket and private data-root.

Goals:
- No reliance on host Docker daemon
- No docker group membership
- No sudo at runtime (only during initial install/provision steps)
- Root privileges within each container, but never on the host
- Dedicated daemon socket: unix:///run/user/$UID/bertrand-docker.sock
- Dedicated state: ~/.local/share/bertrand/docker-{data,config}/
"""
from __future__ import annotations

import json
import os
import platform
import re
import shlex
import shutil
import time
import uuid
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, Final, Iterable, Literal, Protocol, TypedDict, cast

from .run import (
    CompletedProcess,
    CommandError,
    LockDir,
    atomic_write_text,
    confirm,
    host_username,
    run,
    sudo_prefix
)

# pylint: disable=redefined-builtin, unnecessary-pass, broad-except


WAIT_DAEMON_TIMEOUT = 30.0  # seconds


########################
####    PIPELINE    ####
########################


CAP_PLATFORM = "platform"
CAP_PACKAGES = "packages"
CAP_ROOTLESS_SH = "rootless_sh"
CAP_USERNS = "userns"
CAP_SUBIDS = "subids"
CAP_SYSTEMD = "systemd"
CAP_DIRS = "dirs"
CAP_UNIT = "unit"
CAP_SERVICE_STARTED = "service_started"
CAP_DAEMON_READY = "daemon_ready"
CAP_SERVICE_ENABLED = "service_enabled"
CAP_LINGER = "linger"


@dataclass(frozen=True)
class DockerLayout:
    """Path and environment layout for Bertrand's rootless Docker daemon.

    This is intentionally the only authority for:
        -   daemon socket path
        -   data-root and config dir
        -   systemd user unit path
        -   env dictionaries for `docker ...` and `systemctl --user ...` commands
    """
    systemd_user_dir: Path = Path.home() / ".config" / "systemd" / "user"
    state_dir: Path = Path.home() / ".local" / "share" / "bertrand"
    runtime_dir: Path | None = None  # if None, derive from XDG_RUNTIME_DIR

    @property
    def docker_data(self) -> Path:
        """
        Returns
        -------
        Path
            The persistent directory used as the `--data-root` for the rootless Docker
            daemon.  Images, containers, and volumes are stored here.
        """
        return self.state_dir / "docker-data"

    @property
    def docker_config(self) -> Path:
        """
        Returns
        -------
        Path
            The Docker CLI/daemon config directory exported via `DOCKER_CONFIG`, where
            per-daemon settings (e.g., contexts) are stored.
        """
        return self.state_dir / "docker-config"

    @property
    def xdg_runtime_dir(self) -> Path:
        """
        Returns
        -------
        Path
            The XDG_RUNTIME_DIR path under which the rootless Docker's runtime files
            (socket, pidfile, exec-root, etc.) are stored.
        """
        if self.runtime_dir is not None:
            return self.runtime_dir
        xdg = os.environ.get("XDG_RUNTIME_DIR")
        if xdg:
            return Path(xdg)
        return Path("/run/user") / str(os.getuid())

    @property
    def bertrand_runtime_dir(self) -> Path:
        """
        Returns
        -------
        Path
            The systemd user runtime directory under XDG_RUNTIME_DIR.
        """
        return self.xdg_runtime_dir / "bertrand-docker"

    @property
    def socket_path(self) -> Path:
        """
        Returns
        -------
        Path
            The Unix socket used by Bertrand's rootless Docker daemon.
        """
        return self.bertrand_runtime_dir / "docker.sock"

    @property
    def exec_root(self) -> Path:
        """
        Returns
        -------
        Path
            The exec-root directory used by Bertrand's rootless Docker daemon.  Used
            for `containerd` shims, etc.
        """
        return self.bertrand_runtime_dir / "exec-root"

    @property
    def pidfile(self) -> Path:
        """
        Returns
        -------
        Path
            The pidfile used by Bertrand's rootless Docker daemon.
        """
        return self.bertrand_runtime_dir / "dockerd.pid"

    @property
    def unit_path(self) -> Path:
        """
        Returns
        -------
        Path
            The `systemd --user` unit file for Bertrand's rootless Docker daemon.
        """
        return self.systemd_user_dir / "bertrand-docker.service"

    @property
    def registry_dir(self) -> Path:
        """
        Returns
        -------
        Path
            The directory holding the persistent install/uninstall registries for
            Bertrand's rootless Docker daemon.
        """
        return self.state_dir / "registry"

    @property
    def registry_file(self) -> Path:
        """
        Returns
        -------
        Path
            The file holding the persistent install/uninstall registry for Bertrand's
            rootless Docker daemon.
        """
        return self.registry_dir / "install.json"

    @property
    def lock_path(self) -> Path:
        """
        Returns
        -------
        Path
            The lock file used to synchronize Bertrand Docker installation steps.
        """
        return self.registry_dir / "install.lock"

    @property
    def backup_dir(self) -> Path:
        """
        Returns
        -------
        Path
            The directory holding backup copies of modified system files (e.g.,
            a conflicting systemd unit) made during installation.
        """
        return self.state_dir / "backup"

    @property
    def docker_env(self) -> dict[str, str]:
        """Environment variables that force the docker CLI to target Bertrand's
        rootless Docker socket.

        Returns
        -------
        dict[str, str]
            An dictionary that can be used as the `env` parameter of subprocess calls
            that need to target Bertrand's rootless Docker daemon.
        """
        env = os.environ.copy()
        env["DOCKER_HOST"] = f"unix://{self.socket_path}"
        env["DOCKER_CONFIG"] = str(self.docker_config)
        env.setdefault("DOCKER_BUILDKIT", "1")  # enable buildkit by default
        for k in (
            "DOCKER_CONTEXT",
            "DOCKER_TLS_VERIFY",
            "DOCKER_CERT_PATH",
            "DOCKER_MACHINE_NAME",
        ):
            env.pop(k, None)
        return env

    @property
    def systemd_env(self) -> dict[str, str]:
        """Environment variables that allow `systemctl --user` to reach the user bus.

        Returns
        -------
        dict[str, str]
            An dictionary that can be used as the `env` parameter of subprocess calls
            that need to reach the systemd user instance.
        """
        env = os.environ.copy()
        rd = self.xdg_runtime_dir
        env.setdefault("XDG_RUNTIME_DIR", str(rd))
        env.setdefault("DBUS_SESSION_BUS_ADDRESS", f"unix:path={rd}/bus")
        return env


@dataclass
class Docker:
    """A mutable, shared state object that represents an in-flight Docker installation,
    which is passed between strategy classes.  By the end of the installation pipeline,
    this object should represent a fully installed and running rootless Docker daemon.
    """
    layout: DockerLayout = field(default_factory=DockerLayout)
    registry: ArtifactRegistry = field(init=False)
    current_step: str | None = None

    # installation options
    assume_yes: bool = False
    remove_packages: bool = False

    # stable local identity so strategies can refer to the host username
    user: str = field(default_factory=host_username)

    # detected platform info
    system: Literal["linux", "windows"] | None = None
    distro: str | None = None
    codename: str | None = None
    package_manager: Literal["apt", "dnf"] | None = None
    installer_name: str | None = None

    # resolved tools
    rootless_sh: Path | None = None

    # status flags
    systemd_reachable: bool = False
    unit_changed: bool = False

    # bookkeeping
    capabilities: set[str] = field(default_factory=set)
    notes: list[str] = field(default_factory=list)

    def __post_init__(self) -> None:
        self.registry = ArtifactRegistry.load(layout=self.layout, user=self.user)

    def apply(self, op: Operation) -> None:
        """Apply an operation within the current step context, and record it in the
        installation registry so that it can be reliably uninstalled.

        Parameters
        ----------
        op : Operation
            The operation to apply.

        Raises
        ------
        OSError
            If there is no active current step in which to record the operation.
        """
        if self.current_step is None:
            raise OSError("Internal error: ctx.apply() called without an active current_step.")

        # TODO: maybe I need to harden this by ensuring the kind has been registered,
        # and the current step is in-progress?

        payload = op.apply(self)
        if payload is None:
            return

        rec: OpRecord = {"kind": op.kind, "payload": payload}
        self.registry.append_op(self.current_step, rec)

    def note(self, message: str) -> None:
        """Add a note to the installation log.

        Parameters
        ----------
        message : str
            The note message to add.
        """
        self.notes.append(message)


class Strategy(Protocol):
    """A universal type annotation describing a composable step in the Docker
    installation pipeline.

    Attributes
    ----------
    name : str
        The unique name of the strategy, for identification and logging purposes.
    requires : frozenset[str]
        The capability flags required by this strategy.  `probe()` and `install()`
        will execute in strict topological order based on these dependencies, and
        `uninstall()` will execute in reverse order.  If a required capability is not
        provided by any other strategy in the pipeline, an OSError will be raised
        before execution.
    provides : frozenset[str]
        The capability flags provided by this strategy upon successful installation,
        which can satisfy the `requires` of other strategies.

    Methods
    -------
    probe(docker: Docker) -> None
        Probe the host for existing Docker conditions and update the state object
        accordingly.  This method should have no side effects on the host system.
    install(docker: Docker) -> None
        Install or configure Docker components on the host system as needed.  The
        state object is initially populated by the probe() method, and then updated
        where necessary as installation steps are performed.
    uninstall(docker: Docker) -> None
        Uninstall or remove Docker components from the host system as needed.  The
        state object is initially populated by the probe() method, and then updated
        where necessary as uninstallation steps are performed.
    """
    # pylint: disable=missing-function-docstring
    # TODO: in Python 3.13, these properties can be replaced with ReadOnly[] annotations
    # in order to avoid needing to define them as methods.
    @property
    def name(self) -> str: ...
    @property
    def requires(self) -> frozenset[str]: ...
    @property
    def provides(self) -> frozenset[str]: ...

    def probe(self, docker: Docker) -> None: ...
    def install(self, docker: Docker) -> None: ...
    def uninstall(self, docker: Docker) -> None: ...


@dataclass(frozen=True)
class Pipeline:
    """A pipeline of composable Docker installation strategies, which execute in the
    correct order based on their dependency flags."""
    steps: tuple[Strategy, ...]

    def plan(self) -> tuple[Strategy, ...]:
        """Compute a deterministic topological order from each step's requires/provides
        dependencies.

        Returns
        -------
        tuple[Strategy, ...]
            The ordered list of strategies to execute.

        Raises
        ------
        OSError
            If the pipeline cannot proceed due to unsatisfied dependencies.
        """
        pending = list(self.steps)
        have: set[str] = set()
        order: list[Strategy] = []

        while pending:
            new_pending: list[Strategy] = []
            for step in pending:
                if step.requires.issubset(have):
                    order.append(step)
                    have.update(step.provides)
                else:
                    new_pending.append(step)

            if len(new_pending) == len(pending):
                lines = ["Pipeline stalled; unsatisfied dependencies:"]
                for s in pending:
                    missing = sorted(s.requires - have)
                    lines.append(f"  - {s.name}: missing {missing}")
                raise OSError("\n".join(lines))

            pending = new_pending

        return tuple(order)

    def install(
        self,
        *,
        assume_yes: bool = False,
        layout: DockerLayout | None = None
    ) -> Docker:
        """Run the installation pipeline on the given Docker state object.

        Parameters
        ----------
        assume_yes : bool
            If True, automatically answer "yes" to all prompts during installation.
        layout : DockerLayout | None
            An optional DockerLayout to use instead of the default.

        Returns
        -------
        Docker
            The fully installed Docker state object.

        Raises
        ------
        OSError
            If the pipeline cannot proceed due to unsatisfied dependencies.
        """
        docker = Docker(assume_yes=assume_yes, layout=layout or DockerLayout())
        order = self.plan()

        # probe pass (no side effects)
        for step in order:
            step.probe(docker)

        # install pass
        for step in order:
            step.install(docker)
            docker.capabilities.update(step.provides)

        return docker

    def recover_incomplete_steps(self, ctx: Docker) -> None:
        """If a previous run died mid-step, roll back ops for those steps (if any).

        Parameters
        ----------
        ctx : DockerContext
            The in-flight Docker installation state.
        """
        steps = ctx.registry.steps
        # find trailing in_progress steps (or any in_progress)
        inprog: list[StepRecord] = [s for s in steps if s.get("status") == "in_progress"]
        if not inprog:
            return

        # rollback in reverse start order
        for step in reversed(inprog):
            ops: list[OpRecord] = step.get("ops", [])
            for op in reversed(ops):
                kind = op["kind"]
                payload = op["payload"]
                undo = UNDO_DISPATCH.get(kind)
                if undo is None:
                    continue  # unknown op kind -> ignore (forward compat)
                try:
                    undo(ctx, payload)
                except Exception:
                    # best-effort rollback; never crash recovery
                    pass
            step["status"] = "rolled_back"
            step["ended_at"] = _utc_now_iso()
        ctx.registry.write()  # pylint: disable=protected-access

    def _registry_uninstall(self, ctx: Docker) -> None:
        """Undo recorded ops in strict reverse order."""
        for step in reversed(ctx.registry.steps):
            ops: list[OpRecord] = step.get("ops", [])
            for op in reversed(ops):
                undo = UNDO_DISPATCH.get(op["kind"])
                if undo is None:
                    continue
                try:
                    undo(ctx, op["payload"])
                except Exception:
                    # best-effort uninstall
                    pass


    def uninstall(
        self,
        *,
        assume_yes: bool = False,
        layout: DockerLayout | None = None,
        remove_packages: bool = False,
    ) -> Docker:
        """Run the installation pipeline in reverse in order to uninstall Docker and
        its artifacts.

        Parameters
        ----------
        assume_yes : bool, optional
            If True, automatically answer "yes" to all prompts during uninstallation.
            Defaults to False.
        layout : DockerLayout | None, optional
            An optional DockerLayout to use instead of the default.
        remove_packages : bool
            If True, remove Docker packages from the host system.  Defaults to False.

        Returns
        -------
        Docker
            The Docker state object after uninstallation.
        """
        docker = Docker(
            assume_yes=assume_yes,
            layout=layout or DockerLayout(),
            remove_packages=remove_packages,
        )
        order = self.plan()

        # probe pass
        for step in order:
            step.probe(docker)

        # uninstall pass in reverse plan order
        for step in reversed(order):
            step.uninstall(docker)

        return docker


##########################
####    STRATEGIES    ####
##########################


@dataclass(frozen=True)
class DetectPlatform:
    """A strategy that detects the host platform and package manager to use when
    installing Docker.
    """
    name: str = "DetectPlatform"
    requires: frozenset[str] = frozenset()
    provides: frozenset[str] = frozenset([CAP_PLATFORM])

    @staticmethod
    def read_os_release() -> dict[str, str]:
        """Read `/etc/os-release` in order to detect Linux distribution information.

        Returns
        -------
        dict[str, str]
            A dictionary of key/value pairs from /etc/os-release.
        """
        path = Path("/etc/os-release")
        data: dict[str, str] = {}
        if not path.exists():
            return data
        for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            data[k.strip()] = v.strip().strip('"').strip("'")
        return data

    def probe(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        self.install(docker)  # just reads, no host side effects

    def install(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.

        Raises
        ------
        OSError
            If the platform is unsupported.
        """
        system = platform.system().lower()

        if system == "linux":
            docker.system = cast(Literal["linux"], system)
            os_info = self.read_os_release()
            docker.distro = (os_info.get("ID") or "").lower()
            docker.codename = os_info.get("UBUNTU_CODENAME") or os_info.get("VERSION_CODENAME")
            if shutil.which("apt") and docker.distro in {"ubuntu", "debian"}:
                docker.package_manager = "apt"
                docker.installer_name = f"apt ({docker.distro})"
            elif shutil.which("dnf"):
                docker.package_manager = "dnf"
                docker.installer_name = f"dnf ({docker.distro})"
            else:
                raise OSError(
                    f"Unsupported Linux distro ID '{docker.distro}', and no supported "
                    "package manager (apt/dnf) found."
                )
            return

        raise OSError(
            "Bertrand rootless Docker auto-install is implemented only for Linux "
            "(apt/dnf)."
        )

    def uninstall(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        pass  # nothing to uninstall


@dataclass(frozen=True)
class FindRootlessScript:
    """A strategy that resolves the path to `dockerd-rootless.sh` on the host system,
    which should be installed by a previous `InstallDockerEngine` step.
    """
    name: str = "FindRootlessScript"
    requires: frozenset[str] = frozenset([CAP_PACKAGES])
    provides: frozenset[str] = frozenset([CAP_ROOTLESS_SH])

    @staticmethod
    def path() -> Path | None:
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

    def probe(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        try:
            docker.rootless_sh = self.path()
        except OSError:
            docker.rootless_sh = None

    def install(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.

        Raises
        ------
        OSError
            If `dockerd-rootless.sh` is not found.
        """
        rootless_sh = self.path()
        if rootless_sh is None:
            raise OSError(
                "dockerd-rootless.sh not found. Ensure 'docker-ce-rootless-extras' "
                "are installed."
            )
        docker.rootless_sh = rootless_sh

    def uninstall(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        pass  # nothing to uninstall


@dataclass(frozen=True)
class InstallDockerEngine:
    """Install Docker packages required for rootless operation using the system
    package manager.

    This strategy is responsible for:
        -   deciding whether install is needed (docker CLI or rootless extras missing)
        -   prompting the user for confirmation (unless prompt_user is False)
        -   performing the installation steps according to official Docker
            documentation if needed.
    """
    name: str = "InstallDockerEngine"
    requires: frozenset[str] = frozenset([CAP_PLATFORM])
    provides: frozenset[str] = frozenset([CAP_PACKAGES])
    prompt_user: bool = True

    @staticmethod
    def install_apt(*, distro: str, codename: str | None) -> None:
        """Install Docker engine packages using apt.

        Parameters
        ----------
        distro : str
            The Linux distribution ID (e.g., "ubuntu", "debian").
        codename : str | None
            The distribution codename (e.g., "focal", "buster").

        Raises
        ------
        OSError
            If installation fails or sudo is not available.
        """
        if not codename:
            raise OSError("Could not determine VERSION_CODENAME from /etc/os-release")
        sudo = sudo_prefix()
        if not sudo:
            raise OSError("sudo is required to install Docker packages on this host.")

        # Remove common conflicting packages (best-effort)
        conflicts = [
            "docker.io",
            "docker-compose",
            "docker-compose-v2",
            "docker-doc",
            "podman-docker",
            "containerd",
            "runc",
        ]
        installed: list[str] = []
        for pkg in conflicts:
            try:
                run(["dpkg", "-s", pkg], capture_output=True)
                installed.append(pkg)
            except CommandError:
                pass
        if installed:
            run([*sudo, "apt", "remove", "-y", *installed], check=False)

        # Docker official repo setup
        run([*sudo, "apt", "update"])
        run([*sudo, "apt", "install", "-y", "ca-certificates", "curl"])
        run([*sudo, "install", "-m", "0755", "-d", "/etc/apt/keyrings"])
        run([
            *sudo,
            "curl",
            "-fsSL", f"https://download.docker.com/linux/{distro}/gpg",
            "-o", "/etc/apt/keyrings/docker.asc",
        ])
        run([*sudo, "chmod", "a+r", "/etc/apt/keyrings/docker.asc"])
        run(
            [*sudo, "tee", "/etc/apt/sources.list.d/docker.sources"],
            capture_output=True,
            input="\n".join([
                "Types: deb",
                f"URIs: https://download.docker.com/linux/{distro}",
                f"Suites: {codename}",
                "Components: stable",
                "Signed-By: /etc/apt/keyrings/docker.asc",
                "",
            ]),
        )
        run([*sudo, "apt", "update"])

        # Rootless prerequisites
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
        run([*sudo, "apt", "install", "-y", *pkgs])

    @staticmethod
    def uninstall_apt(*, sudo: list[str]) -> None:
        """Uninstall Docker engine packages and repositories using apt.

        Parameters
        ----------
        sudo : list[str]
            The sudo prefix command to use.
        """
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
        run([*sudo, "apt", "purge", "-y", *pkgs], check=False)
        run([*sudo, "rm", "-f", "/etc/apt/sources.list.d/docker.sources"], check=False)
        run([*sudo, "rm", "-f", "/etc/apt/keyrings/docker.asc"], check=False)
        run([*sudo, "apt", "update"], check=False)
        run([*sudo, "apt", "autoremove", "-y"], check=False)

    @staticmethod
    def install_dnf() -> None:
        """Install Docker engine packages using dnf.

        Raises
        ------
        OSError
            If installation fails or sudo is not available.
        """
        sudo = sudo_prefix()
        if not sudo:
            raise OSError("sudo is required to install Docker packages on this host.")

        run([*sudo, "dnf", "install", "-y", "dnf-plugins-core"], check=False)
        run([
            *sudo,
            "dnf",
            "config-manager",
            "addrepo",
            "--from-repofile",
            "https://download.docker.com/linux/fedora/docker-ce.repo"
        ])
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
        run([*sudo, "dnf", "install", "-y", *pkgs])

    @staticmethod
    def uninstall_dnf(*, sudo: list[str]) -> None:
        """Uninstall Docker engine packages and repositories using dnf.

        Parameters
        ----------
        sudo : list[str]
            The sudo prefix command to use.
        """
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
        run([*sudo, "dnf", "remove", "-y", *pkgs], check=False)
        run([*sudo, "rm", "-f", "/etc/yum.repos.d/docker-ce.repo"], check=False)

    def probe(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        # purely informational (no side effects)
        docker_cli = shutil.which("docker")
        rootless_sh = FindRootlessScript.path()
        if docker_cli and rootless_sh:
            docker.note(
                "Host Docker packages appear installed (docker CLI + rootless extras present)."
            )

    def install(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.

        Raises
        ------
        OSError
            If `dockerd-rootless.sh` is not found, installation is declined by the
            user, or the package manager is unsupported.
        """
        docker_cli = shutil.which("docker")
        rootless_sh = FindRootlessScript.path()
        if docker_cli and rootless_sh:
            docker.note(
                "Host Docker packages appear installed (docker CLI + rootless extras "
                "present)."
            )
            return

        if self.prompt_user and not confirm(
            "Bertrand requires a private, rootless Docker daemon.\n"
            f"I can install the required packages now using {docker.installer_name} (via sudo).\n"
            "Proceed? [y/N] ",
            assume_yes=docker.assume_yes,
        ):
            raise OSError("Installation declined by user.")

        if docker.package_manager == "apt":
            self.install_apt(distro=docker.distro or "ubuntu", codename=docker.codename)
        elif docker.package_manager == "dnf":
            self.install_dnf()
        else:
            raise OSError(f"Internal error: unknown package manager '{docker.package_manager}'")

    def uninstall(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.

        Raises
        ------
        OSError
            If package removal is requested but the package manager is unsupported.
        """
        if not docker.remove_packages:
            return

        sudo = sudo_prefix()
        if not sudo:
            raise OSError("sudo required to uninstall Docker packages.")

        if docker.package_manager == "apt":
            self.uninstall_apt(sudo=sudo)
        elif docker.package_manager == "dnf":
            self.uninstall_dnf(sudo=sudo)
        else:
            raise OSError(f"Unsupported package manager for removal: '{docker.package_manager}'")


@dataclass(frozen=True)
class EnableUserNamespaces:
    """Ensure unprivileged user namespaces are enabled on the host system, which are
    required for rootless Docker operation per Docker documentation.

    This strategy is responsible for:
        -   reading sysctl knobs from /proc/sys
        -   prompting and applying temporary sysctl fixes (best-effort)
    """
    name: str = "EnableUserNamespaces"
    requires: frozenset[str] = frozenset([CAP_PLATFORM])
    provides: frozenset[str] = frozenset([CAP_USERNS])
    prompt_user: bool = True

    @staticmethod
    def read_proc_sys(path: str) -> int | None:
        """Read a sysctl value from /proc/sys.

        Parameters
        ----------
        path : str
            The sysctl path relative to `/proc/sys`
            (e.g., "kernel/unprivileged_userns_clone").

        Returns
        -------
        int | None
            The integer value of the sysctl path, or None if it could not be read.
        """
        p = Path("/proc/sys") / path
        try:
            return int(p.read_text(encoding="utf-8").strip())
        except (OSError, ValueError):
            return None

    def probe(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        pass

    def install(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.

        Raises
        ------
        OSError
            If user namespaces are disabled and cannot be enabled.
        """
        # check user namespace support
        unpriv = self.read_proc_sys("kernel/unprivileged_userns_clone")
        maxns = self.read_proc_sys("user/max_user_namespaces")
        disabled = (unpriv == 0) or (maxns is not None and maxns == 0)
        if not disabled:
            return

        # prompt to enable user namespaces via sudo sysctl
        sudo = sudo_prefix()
        msg = (
            "Rootless Docker needs unprivileged user namespaces enabled. "
            f"(kernel.unprivileged_userns_clone={unpriv}, user.max_user_namespaces={maxns})"
        )
        if not sudo:
            raise OSError(f"{msg} (sudo not available to adjust sysctl)")
        if self.prompt_user and not confirm(
            f"{msg}\nI can enable them using sudo sysctl. Proceed? [y/N] ",
            assume_yes=docker.assume_yes
        ):
            raise OSError(msg)

        # attempt to enable user namespaces via sudo sysctl
        if unpriv == 0:
            run([*sudo, "sysctl", "-w", "kernel.unprivileged_userns_clone=1"])
        if maxns == 0:
            run([*sudo, "sysctl", "-w", "user.max_user_namespaces=15000"])

    def uninstall(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        # do not revert sysctls (policy-owned)
        pass


@dataclass(frozen=True)
class ProvisionSubIDs:
    """Provision enough subordinate UID/GIDs in /etc/subuid and /etc/subgid for
    rootless Docker operation.

    This strategy is responsible for:
        -   parsing /etc/subuid and /etc/subgid
        -   choosing a non-overlapping start aligned to range size
        -   appending to the uid/gid files with flock (if available) via sudo
    """
    name: str = "ProvisionSubIDs"
    requires: frozenset[str] = frozenset([CAP_PLATFORM])
    provides: frozenset[str] = frozenset([CAP_SUBIDS])
    prompt_user: bool = True

    subuid_path: Path = Path("/etc/subuid")
    subgid_path: Path = Path("/etc/subgid")
    needed: int = 65536  # per Docker rootless docs

    # e.g. user:start:count
    regex = re.compile(r"^(?P<user>[^:]+):(?P<start>\d+):(?P<count>\d+)\s*$")

    @dataclass(frozen=True)
    class SubIDRange:
        """Helper class representing a subordinate ID range entry."""
        user: str
        start: int
        count: int

        @property
        def end(self) -> int:
            """
            Returns
            -------
            int
                The first ID after the end of the subordinate ID range.
            """
            return self.start + self.count

    def parse_subid_file(self, path: Path) -> list[ProvisionSubIDs.SubIDRange]:
        """Read a subordinate ID file (/etc/subuid or /etc/subgid) and parse its
        entries.

        Parameters
        -----------
        path : Path
            The path to the subordinate ID file.

        Returns
        -------
        list[SubIDRange]
            A list of parsed subordinate ID ranges.
        """
        if not path.exists():
            return []

        out: list[ProvisionSubIDs.SubIDRange] = []
        for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            m = self.regex.match(line)
            if not m:
                continue
            out.append(self.SubIDRange(
                user=m.group("user"),
                start=int(m.group("start")),
                count=int(m.group("count")),
            ))

        return out

    def has_enough(self, user: str, ranges: Iterable[ProvisionSubIDs.SubIDRange]) -> bool:
        """Check if there is a subordinate ID range for the given user with enough
        contiguous IDs to satisfy the needed count.

        Parameters
        ----------
        user : str
            The username to check for.
        ranges : Iterable[SubIDRange]
            The subordinate ID ranges to check.

        Returns
        -------
        bool
            True if there is a range for the user with enough IDs, False otherwise.
        """
        return any(r.user == user and r.count >= self.needed for r in ranges)

    @staticmethod
    def choose_non_overlapping_start(
        ranges: list[ProvisionSubIDs.SubIDRange],
        needed: int
    ) -> int:
        """Find a non-overlapping start for a new subordinate ID range, aligned to
        the needed count.

        Parameters
        ----------
        ranges : list[SubIDRange]
            The existing subordinate ID ranges to avoid.
        needed : int
            The number of contiguous IDs needed.

        Returns
        -------
        int
            A suitable start for the new subordinate ID range.
        """
        base = 100000  # popular base to avoid conflicts with system users
        max_end = max((r.end for r in ranges), default=base)
        start = max(base, max_end)
        rem = start % needed
        if rem:
            start += (needed - rem)
        return start

    @staticmethod
    def append(
        path: Path,
        user: str,
        start: int,
        count: int,
        sudo: list[str]
    ) -> None:
        """Append a subordinate ID range entry to the given file, using flock (if
        available) to avoid race conditions.

        Parameters
        ----------
        path : Path
            The path to the subordinate ID file.
        user : str
            The username to add the range for.
        start : int
            The start of the subordinate ID range.
        count : int
            The count of IDs in the subordinate ID range.
        sudo : list[str]
            The sudo prefix command to use.
        """
        line = f"{user}:{start}:{count}"
        quoted_line = shlex.quote(line)
        quoted_path = shlex.quote(str(path))
        cmd = (
            "set -euo pipefail; "
            f"touch {quoted_path}; "
            "if command -v flock >/dev/null 2>&1; then "
            f"  exec 9>>{quoted_path}; flock -x 9; "
            "fi; "
            f"grep -F -x -q {quoted_line} {quoted_path} || echo {quoted_line} >> {quoted_path}"
        )
        run([*sudo, "sh", "-lc", cmd])

    def probe(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        pass

    def install(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.

        Raises
        ------
        OSError
            If subordinate ID ranges cannot be provisioned, or if the user declines
            the operation.
        """
        user = docker.user
        uid_ranges = self.parse_subid_file(self.subuid_path)
        gid_ranges = self.parse_subid_file(self.subgid_path)
        if self.has_enough(user, uid_ranges) and self.has_enough(user, gid_ranges):
            return

        # prompt to allow appends to subuid/subgid
        sudo = sudo_prefix()
        if not sudo:
            raise OSError(
                "Rootless Docker requires subuid/subgid ranges, but sudo is not "
                "available. Please have an admin allocate >= 65536 entries for your "
                "user in /etc/subuid and /etc/subgid."
            )
        start_uid = self.choose_non_overlapping_start(uid_ranges, self.needed)
        start_gid = self.choose_non_overlapping_start(gid_ranges, self.needed)
        if self.prompt_user and not confirm(
            "Rootless Docker requires subordinate UID/GID ranges (>= 65536) in "
            "/etc/subuid and /etc/subgid.\n"
            "I can append the following entries using sudo:\n\n"
            f"  {user}:{start_uid}:{self.needed}   (to {self.subuid_path})\n"
            f"  {user}:{start_gid}:{self.needed}   (to {self.subgid_path})\n\n"
            "Proceed? [y/N] ",
            assume_yes=docker.assume_yes
        ):
            raise OSError("Rootless Docker prerequisites declined by user.")

        # append entries via sudo
        self.append(self.subuid_path, user, start_uid, self.needed, sudo)
        self.append(self.subgid_path, user, start_gid, self.needed, sudo)

        # Verify after write
        uid_ranges = self.parse_subid_file(self.subuid_path)
        gid_ranges = self.parse_subid_file(self.subgid_path)
        if not self.has_enough(user, uid_ranges) or not self.has_enough(user, gid_ranges):
            raise OSError("Failed to provision subuid/subgid ranges correctly.")

    def uninstall(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        # do not remove /etc/subuid or /etc/subgid entries (shared host resource)
        pass


@dataclass(frozen=True)
class SystemdUserAvailable:
    """Verify that `systemctl --user` is usable (i.e. user systemd instance exists
    and bus is reachable).
    """
    name: str = "SystemdUserAvailable"
    requires: frozenset[str] = frozenset([CAP_PLATFORM])
    provides: frozenset[str] = frozenset([CAP_SYSTEMD])

    @staticmethod
    def reachable(*, env: dict[str, str]) -> bool:
        """Check if systemd user instance is reachable.

        Parameters
        ----------
        env : dict[str, str]
            The environment variables to use when invoking `systemctl --user`.

        Returns
        -------
        bool
            True if the systemd user instance is reachable, False otherwise.
        """
        if not shutil.which("systemctl"):
            return False
        try:
            cp = run(
                ["systemctl", "--user", "is-system-running"],
                check=False,
                capture_output=True,
                env=env,
            )
        except (OSError, CommandError):
            return False

        out = (cp.stdout or "").strip().lower()
        err = (cp.stderr or "").strip().lower()

        # hard failures: no user bus / no systemd user instance
        if "failed to connect to bus" in err or "no medium found" in err:
            return False

        # systemctl returns these states; some may be non-zero but still mean "reachable"
        if out in {"running", "degraded", "starting", "initializing", "maintenance", "stopping"}:
            return True

        # if we got *any* coherent state string, assume reachable.
        return bool(out)

    def probe(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        docker.systemd_reachable = self.reachable(env=docker.layout.systemd_env)

    def install(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.

        Raises
        ------
        OSError
            If systemd user instance is not reachable.
        """
        docker.systemd_reachable = self.reachable(env=docker.layout.systemd_env)
        if not docker.systemd_reachable:
            raise OSError(
                "systemd user units not available (systemctl --user). Bertrand "
                "requires systemd user sessions for rootless daemon management."
            )

    def uninstall(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        pass  # nothing to uninstall


@dataclass(frozen=True)
class EnsureBertrandDirs:
    """Ensure Bertrand's Docker data/config directories exist with safe permissions."""
    name: str = "EnsureBertrandDirs"
    requires: frozenset[str] = frozenset([CAP_SYSTEMD])
    provides: frozenset[str] = frozenset([CAP_DIRS])

    def probe(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        pass

    def install(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.

        Raises
        ------
        OSError
            If the directories cannot be created.
        """
        docker.layout.docker_data.mkdir(parents=True, exist_ok=True)
        docker.layout.docker_config.mkdir(parents=True, exist_ok=True)
        for p in (docker.layout.docker_data, docker.layout.docker_config):
            try:
                p.chmod(0o700)
            except OSError:
                pass  # best-effort

    def uninstall(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        for p in (docker.layout.docker_data, docker.layout.docker_config):
            if p.exists():
                shutil.rmtree(p, ignore_errors=True)


@dataclass(frozen=True)
class WriteSystemdUnit:
    """Write/update the systemd --user unit to manage Bertrand's rootless Docker daemon.

    This strategy is responsible for:
        -   rendering the unit file from a template
        -   writing the unit file if changed
        -   reloading the systemd user daemon if the unit file changed
    """
    name: str = "WriteSystemdUnit"
    requires: frozenset[str] = frozenset([CAP_ROOTLESS_SH, CAP_SYSTEMD, CAP_DIRS])
    provides: frozenset[str] = frozenset([CAP_UNIT])

    @staticmethod
    def render(layout: DockerLayout, *, rootless_sh: Path) -> str:
        """Return the formatted text of the systemd user unit file.

        Parameters
        ----------
        layout : DockerLayout
            The Docker layout object representing paths.
        rootless_sh : Path
            The path to `dockerd-rootless.sh`.

        Returns
        -------
        str
            The formatted systemd user unit file text.
        """
        # pylint: disable=line-too-long
        return f"""\
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
  --data-root={layout.docker_data} \\
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
"""

    def probe(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        pass

    def install(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        assert docker.rootless_sh is not None
        unit_path = docker.layout.unit_path
        unit_path.parent.mkdir(parents=True, exist_ok=True)

        text = self.render(docker.layout, rootless_sh=docker.rootless_sh)
        old = unit_path.read_text(encoding="utf-8") if unit_path.exists() else ""
        if old != text:
            atomic_write_text(unit_path, text)
            run(["systemctl", "--user", "daemon-reload"], env=docker.layout.systemd_env)
            docker.unit_changed = True
        else:
            docker.unit_changed = False

    def uninstall(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        unit_path = docker.layout.unit_path
        if unit_path.exists():
            try:
                unit_path.unlink()
            except OSError:
                pass
            if docker.systemd_reachable:
                run(
                    ["systemctl", "--user", "daemon-reload"],
                    check=False,
                    env=docker.layout.systemd_env
                )


@dataclass(frozen=True)
class StartUserService:
    """Start (or restart) the bertrand-docker systemd --user service."""
    name: str = "StartUserService"
    requires: frozenset[str] = frozenset([CAP_UNIT, CAP_SYSTEMD])
    provides: frozenset[str] = frozenset([CAP_SERVICE_STARTED])

    def probe(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        pass

    def install(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        env = docker.layout.systemd_env
        if docker.unit_changed:
            run(
                ["systemctl", "--user", "try-restart", "bertrand-docker.service"],
                check=False,
                env=env
            )
        run(
            ["systemctl", "--user", "start", "bertrand-docker.service"],
            env=env
        )

    def uninstall(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        env = docker.layout.systemd_env

        # stop user service if possible
        if docker.systemd_reachable:
            run(["systemctl", "--user", "stop", "bertrand-docker.service"], check=False, env=env)

        # best-effort cleanup runtime dir (socket/pid/exec-root)
        shutil.rmtree(docker.layout.bertrand_runtime_dir, ignore_errors=True)
        try:
            docker.layout.socket_path.unlink(missing_ok=True)
        except OSError:
            pass
        try:
            docker.layout.pidfile.unlink(missing_ok=True)
        except OSError:
            pass



@dataclass(frozen=True)
class WaitForDaemon:
    """Wait for the rootless daemon socket to respond to connections."""
    name: str = "WaitForDaemon"
    requires: frozenset[str] = frozenset([CAP_SERVICE_STARTED])
    provides: frozenset[str] = frozenset([CAP_DAEMON_READY])
    timeout: float = WAIT_DAEMON_TIMEOUT

    def probe(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        pass

    def install(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.

        Raises
        ------
        OSError
            If the Docker daemon does not become reachable within the timeout.
        """
        env = docker.layout.docker_env
        last: Exception | None = None
        deadline = time.time() + self.timeout
        while time.time() < deadline:
            try:
                run(["docker", "info"], capture_output=True, env=env)
                return
            except Exception as err:
                last = err
                time.sleep(0.2)
        if last:
            raise last
        raise OSError("Timed out waiting for Bertrand rootless Docker daemon.")

    def uninstall(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        pass


@dataclass(frozen=True)
class StartServiceOnLogin:
    """Enable the systemd user service to start at user login."""
    name: str = "StartServiceOnLogin"
    requires: frozenset[str] = frozenset([CAP_DAEMON_READY, CAP_SYSTEMD])
    provides: frozenset[str] = frozenset([CAP_SERVICE_ENABLED])

    def probe(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        pass

    def install(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        run(
            ["systemctl", "--user", "enable", "bertrand-docker.service"],
            env=docker.layout.systemd_env
        )

    def uninstall(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        if docker.systemd_reachable:
            env = docker.layout.systemd_env
            run(
                ["systemctl", "--user", "disable", "bertrand-docker.service"],
                check=False,
                env=env
            )
            run(
                ["systemctl", "--user", "reset-failed", "bertrand-docker.service"],
                check=False,
                env=env
            )


@dataclass(frozen=True)
class PersistAfterLogout:
    """Optionally enable linger for the user so the Docker daemon persists after
    logout.

    This strategy is responsible for:
        -   checking if linger is already enabled
        -   prompting the user for confirmation (unless prompt_user is False)
        -   enabling linger via `loginctl enable-linger <user>` with sudo
    """
    name: str = "PersistAfterLogout"
    requires: frozenset[str] = frozenset([CAP_PLATFORM])
    provides: frozenset[str] = frozenset([CAP_LINGER])
    prompt_user: bool = True

    def probe(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        pass

    def install(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        sudo = sudo_prefix()
        if not sudo or not shutil.which("loginctl"):
            return

        # check current linger status
        user = docker.user
        try:
            cp = run(["loginctl", "show-user", user, "-p", "Linger"], capture_output=True)
            if "Linger=yes" in (cp.stdout or ""):
                return
        except CommandError:
            pass

        if self.prompt_user and confirm(
            f"Persist Docker containers even after '{user}' logs out? [y/N]",
            assume_yes=docker.assume_yes
        ):
            run([*sudo, "loginctl", "enable-linger", user])

    def uninstall(self, docker: Docker) -> None:
        """
        Parameters
        ----------
        docker : Docker
            The in-flight Docker installation state.
        """
        pass  # do not disable linger for the whole user


######################
####    PUBLIC    ####
######################


def pipeline(extra: Iterable[Strategy] = ()) -> Pipeline:
    """Generate a complete installation pipeline for Bertrand's rootless Docker daemon.

    Parameters
    ----------
    extra : Iterable[Strategy]
        Additional strategies to append to the pipeline.  These will be executed
        as soon as their dependencies are met.

    Returns
    -------
    Pipeline
        The complete installation pipeline.
    """
    result: list[Strategy] = [
        DetectPlatform(),
        InstallDockerEngine(),
        FindRootlessScript(),
        EnableUserNamespaces(),
        ProvisionSubIDs(),
        SystemdUserAvailable(),
        EnsureBertrandDirs(),
        WriteSystemdUnit(),
        StartUserService(),
        WaitForDaemon(),
        StartServiceOnLogin(),
        PersistAfterLogout(),
    ]
    result.extend(extra)
    return Pipeline(steps=tuple(result))


def install(
    *,
    layout: DockerLayout | None = None,
    extra_steps: Iterable[Strategy] = (),
    assume_yes: bool = False
) -> Docker:
    """Install everything needed for Bertrand rootless Docker and start the daemon.

    One-time sudo is used for:
    - package installation (apt/dnf)
    - /etc/subuid and /etc/subgid provisioning (if missing)

    After successful install, normal Bertrand docker operations never require sudo.

    Parameters
    ----------
    layout : DockerLayout | None
        The Docker layout to use.  If None, the default layout is used.
    extra_steps : Iterable[Strategy]
        Additional strategies to append to the pipeline.  These will be executed
        as soon as their dependencies are met.
    assume_yes : bool
        If True, automatically answer yes to all prompts.  Default is False.

    Returns
    -------
    HostDocker
        The status of Docker installation and daemon reachability.

    Raises
    ------
    OSError
        If installation fails or is declined by the user.
    CommandError
        If the daemon fails to start or is unreachable.
    """
    layout = layout or DockerLayout()
    pipe = pipeline(extra=extra_steps)
    return pipe.install(assume_yes=assume_yes, layout=layout)


def uninstall(
    *,
    layout: DockerLayout | None = None,
    extra_steps: Iterable[Strategy] = (),
    assume_yes: bool = False,
    remove_packages: bool = False
) -> None:
    """Stop/remove Bertrand rootless daemon and (optionally) remove Docker packages.

    Parameters
    ----------
    layout : DockerLayout | None
        The Docker layout to use.  If None, the default layout is used.
    extra_steps : Iterable[Strategy]
        Additional strategies to append to the pipeline.  These will be executed
        in reverse order based on their dependencies.
    assume_yes : bool
        If True, automatically answer yes to all prompts.  Default is False.
    remove_packages : bool
        If True, uninstall Docker packages from the host (apt/dnf). This uses sudo.

    Raises
    ------
    OSError
        If uninstallation fails or is declined by the user.
    """
    layout = layout or DockerLayout()

    msg = "This will remove Bertrand's rootless Docker daemon configuration.\n"
    if remove_packages:
        msg += "It will ALSO uninstall Docker-related packages from the host.\n"
    else:
        msg += "It will NOT uninstall Docker-related packages from the host.\n"
    msg += "Proceed? [y/N] "
    if not confirm(msg, assume_yes=assume_yes):
        raise OSError("Uninstallation declined by user.")

    pipe = pipeline(extra=extra_steps)
    pipe.uninstall(
        layout=layout,
        assume_yes=assume_yes,
        remove_packages=remove_packages
    )

    # TODO: the uninstallation pathway should not require anything below this line, so
    # that it can be symmetrical with installation.

    # system = platform.system().lower()
    # if system != "linux":
    #     raise OSError("Bertrand rootless Docker uninstall is implemented only for Linux.")


    # # Stop user service and remove unit
    # if _systemd_reachable():
    #     run(
    #         ["systemctl", "--user", "stop", "bertrand-docker.service"],
    #         check=False,
    #         env=_systemd_user_env()
    #     )
    #     run(
    #         ["systemctl", "--user", "disable", "bertrand-docker.service"],
    #         check=False,
    #         env=_systemd_user_env()
    #     )
    # unit_path = _bertrand_unit_path()
    # if unit_path.exists():
    #     unit_path.unlink()
    #     if _systemd_reachable():
    #         run(
    #             ["systemctl", "--user", "daemon-reload"],
    #             check=False,
    #             env=_systemd_user_env()
    #         )

    # # Remove runtime artifacts (best-effort)
    # try:
    #     _bertrand_socket_path().unlink(missing_ok=True)
    # except OSError:
    #     pass
    # try:
    #     _bertrand_pidfile().unlink(missing_ok=True)
    # except OSError:
    #     pass

    # # Remove persistent data-root
    # if remove_data:
    #     if BERTRAND_DOCKER_DATA.exists():
    #         shutil.rmtree(BERTRAND_DOCKER_DATA, ignore_errors=True)
    #     if BERTRAND_DOCKER_CONFIG.exists():
    #         shutil.rmtree(BERTRAND_DOCKER_CONFIG, ignore_errors=True)

    # # Optionally remove host packages (apt/dnf)
    # if remove_packages:
    #     sudo = sudo_prefix()
    #     if not sudo:
    #         raise OSError("sudo required to uninstall Docker packages.")

    #     os_info = _read_os_release()
    #     distro = (os_info.get("ID") or "").lower()

    #     if distro in {"ubuntu", "debian"} or shutil.which("apt"):
    #         pkgs = [
    #             "docker-ce",
    #             "docker-ce-cli",
    #             "containerd.io",
    #             "docker-buildx-plugin",
    #             "docker-compose-plugin",
    #             "docker-ce-rootless-extras",
    #             "uidmap",
    #             "slirp4netns",
    #             "fuse-overlayfs",
    #         ]
    #         run([*sudo, "apt", "purge", "-y", *pkgs], check=False)
    #         run([*sudo, "rm", "-f", "/etc/apt/sources.list.d/docker.sources"], check=False)
    #         run([*sudo, "rm", "-f", "/etc/apt/keyrings/docker.asc"], check=False)
    #         run([*sudo, "apt", "update"], check=False)
    #         run([*sudo, "apt", "autoremove", "-y"], check=False)

    #     elif distro in {"fedora"} or shutil.which("dnf"):
    #         pkgs = [
    #             "docker-ce",
    #             "docker-ce-cli",
    #             "containerd.io",
    #             "docker-buildx-plugin",
    #             "docker-compose-plugin",
    #             "docker-ce-rootless-extras",
    #             "uidmap",
    #             "slirp4netns",
    #             "fuse-overlayfs",
    #             "iptables",
    #         ]
    #         run([*sudo, "dnf", "remove", "-y", *pkgs], check=False)
    #         run([*sudo, "rm", "-f", "/etc/yum.repos.d/docker-ce.repo"], check=False)

    #     else:
    #         raise OSError("Unsupported distro/package manager for package removal.")

    # # Remove systemd runtime dir
    # shutil.rmtree(_bertrand_runtime_dir(), ignore_errors=True)


def docker_cmd(
    args: list[str],
    *,
    check: bool = True,
    capture_output: bool | None = False,
    input: str | None = None,
    cwd: Path | None = None,
    layout: DockerLayout | None = None
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
    layout : DockerLayout | None, optional
        The Docker layout to use, for testing purposes.  If None, the default layout is
        used.

    Returns
    -------
    CompletedProcess
        The completed process result.

    Raises
    ------
    CommandError
        If the command fails and `check` is True.
    """
    layout = layout or DockerLayout()
    return run(
        ["docker", *args],
        check=check,
        capture_output=capture_output,
        input=input,
        cwd=cwd,
        env=layout.docker_env,
    )


def docker_exec(args: list[str], *, layout: DockerLayout | None = None) -> None:
    """Execute a docker command against the Bertrand rootless daemon, replacing the
    current process.

    Parameters
    ----------
    args : list[str]
        The docker command arguments (excluding the "docker" executable).
    layout : DockerLayout | None, optional
        The Docker layout to use, for testing purposes.  If None, the default layout is
        used.

    Raises
    ------
    OSError
        If execution fails.
    """
    layout = layout or DockerLayout()
    os.execvpe("docker", ["docker", *args], layout.docker_env)
