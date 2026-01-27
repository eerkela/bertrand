"""Bertrand rootless Docker: install prerequisites, provision subuid/subgid, and run a
Bertrand-owned rootless `dockerd` bound to a private socket and private data-root.

Goals:
- No reliance on /var/run/docker.sock
- No docker group membership
- No sudo at runtime (only during initial install/provision steps)
- Root privileges within each container, but never on the host
- Dedicated daemon socket: unix:///run/user/$UID/bertrand-docker.sock
- Dedicated state: ~/.local/share/bertrand/docker/
"""
import os
import platform
import re
import shlex
import shutil
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable

from .run import (
    CompletedProcess,
    CommandError,
    atomic_write_text,
    confirm,
    host_username,
    run,
    sudo_prefix
)

#pylint: disable=redefined-builtin, global-statement


BERTRAND_SYSTEMD_DIR = Path.home() / ".config" / "systemd" / "user"
BERTRAND_STATE = Path.home() / ".local" / "share" / "bertrand"
BERTRAND_DOCKER_DATA = BERTRAND_STATE / "docker-data"
BERTRAND_DOCKER_CONFIG = BERTRAND_STATE / "docker-config"
SUBUID = Path("/etc/subuid")
SUBGID = Path("/etc/subgid")
RANGE = re.compile(r"^(?P<user>[^:]+):(?P<start>\d+):(?P<count>\d+)\s*$")
WAIT_DAEMON_TIMEOUT = 30.0  # seconds


def _xdg_runtime_dir() -> Path:
    xdg = os.environ.get("XDG_RUNTIME_DIR")
    if xdg:
        return Path(xdg)
    return Path("/run/user") / str(os.getuid())


def _bertrand_runtime_dir() -> Path:
    return _xdg_runtime_dir() / "bertrand-docker"


def _bertrand_socket_path() -> Path:
    return _bertrand_runtime_dir() / "docker.sock"


def _bertrand_exec_root() -> Path:
    return _bertrand_runtime_dir() / "exec-root"


def _bertrand_pidfile() -> Path:
    return _bertrand_runtime_dir() / "dockerd.pid"


def _bertrand_unit_path() -> Path:
    return BERTRAND_SYSTEMD_DIR / "bertrand-docker.service"


def _systemd_user_env() -> dict[str, str]:
    env = os.environ.copy()
    rd = _xdg_runtime_dir()
    env.setdefault("XDG_RUNTIME_DIR", str(rd))
    env.setdefault("DBUS_SESSION_BUS_ADDRESS", f"unix:path={rd}/bus")
    return env


def _ensure_unit(rootless_sh: Path) -> bool:
    # pylint: disable=line-too-long
    text = f"""\
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
ExecStart={rootless_sh} --host=unix://%t/bertrand-docker/docker.sock --data-root=%h/.local/share/bertrand/docker-data --exec-root=%t/bertrand-docker/exec-root --pidfile=%t/bertrand-docker/dockerd.pid

# reload on SIGHUP
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

    unit_path = _bertrand_unit_path()
    unit_path.parent.mkdir(parents=True, exist_ok=True)
    old = unit_path.read_text(encoding="utf-8") if unit_path.exists() else ""
    if old != text:
        atomic_write_text(unit_path, text)
        run(["systemctl", "--user", "daemon-reload"], env=_systemd_user_env())
        return True
    return False


def _dockerd_rootless_sh() -> Path | None:
    # typically provided by docker-ce-rootless-extras
    which = shutil.which("dockerd-rootless.sh")
    candidates: list[Path] = []
    if which:
        candidates.append(Path(which).expanduser().resolve())  # ensure absolute path
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


def _systemd_reachable() -> bool:
    if not shutil.which("systemctl"):
        return False
    try:
        cp = run(
            ["systemctl", "--user", "is-system-running"],
            check=False,              # critical: degraded returns non-zero
            capture_output=True,
            env=_systemd_user_env()
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


@dataclass(frozen=True)
class SubIDRange:
    """A subordinate UID/GID range entry from /etc/subuid or /etc/subgid."""
    user: str
    start: int
    count: int

    @property
    def end(self) -> int:
        """
        Returns
        -------
        int
            The first ID after the end of this range (exclusive).
        """
        return self.start + self.count


def _parse_subid_file(path: Path) -> list[SubIDRange]:
    if not path.exists():
        return []
    out: list[SubIDRange] = []
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        m = RANGE.match(line)
        if not m:
            continue
        out.append(SubIDRange(
            user=m.group("user"),
            start=int(m.group("start")),
            count=int(m.group("count")),
        ))
    return out


def _has_enough_subids(user: str, ranges: Iterable[SubIDRange], needed: int = 65536) -> bool:
    return any(r.user == user and r.count >= needed for r in ranges)


def _choose_non_overlapping_start(ranges: list[SubIDRange], needed: int = 65536) -> int:
    base = 100000  # popular default start for subuid/subgid allocations
    max_end = max([r.end for r in ranges], default=base)
    start = max(base, max_end)
    rem = start % needed
    if rem:
        start += (needed - rem)
    return start


def _append_subid_entry(path: Path, user: str, start: int, count: int, sudo: list[str]) -> None:
    # flock is commonly available on Linux; if not, fall back to non-locked append
    line = f"{user}:{start}:{count}"
    q_line = shlex.quote(line)
    q_path = shlex.quote(str(path))
    cmd = (
        "set -euo pipefail; "
        f"touch {q_path}; "
        "if command -v flock >/dev/null 2>&1; then "
        f"  exec 9>>{q_path}; flock -x 9; "
        "fi; "
        f"grep -F -x -q {q_line} {q_path} || echo {q_line} >> {q_path}"
    )
    run([*sudo, "sh", "-lc", cmd])


def _provision_subids(user: str, *, assume_yes: bool) -> None:
    uid_ranges = _parse_subid_file(SUBUID)
    gid_ranges = _parse_subid_file(SUBGID)
    if _has_enough_subids(user, uid_ranges) and _has_enough_subids(user, gid_ranges):
        return

    sudo = sudo_prefix()
    if not sudo:
        raise OSError(
            "Rootless Docker requires subuid/subgid ranges, but sudo is not available. "
            "Please have an admin allocate >= 65536 entries for your user in "
            "/etc/subuid and /etc/subgid."
        )

    start_uid = _choose_non_overlapping_start(uid_ranges)
    start_gid = _choose_non_overlapping_start(gid_ranges)
    prompt = (
        "Rootless Docker requires subordinate UID/GID ranges (>= 65536) in /etc/subuid "
        "and /etc/subgid.\n"
        f"I can append the following entries using sudo:\n\n"
        f"  {user}:{start_uid}:65536   (to /etc/subuid)\n"
        f"  {user}:{start_gid}:65536   (to /etc/subgid)\n\n"
        "Proceed? [y/N] "
    )
    if not confirm(prompt, assume_yes=assume_yes):
        raise OSError("Rootless Docker prerequisites declined by user.")

    # Append. For a single-user dev host, this is typically sufficient.
    _append_subid_entry(SUBUID, user, start_uid, 65536, sudo)
    _append_subid_entry(SUBGID, user, start_gid, 65536, sudo)

    # Verify after write
    uid_ranges = _parse_subid_file(SUBUID)
    gid_ranges = _parse_subid_file(SUBGID)
    if not (_has_enough_subids(user, uid_ranges) and _has_enough_subids(user, gid_ranges)):
        raise OSError("Failed to provision subuid/subgid ranges correctly.")


@dataclass(frozen=True)
class HostDocker:
    """A data struct representing the status of Docker on the host system."""
    installed: bool
    rootless_sh: bool
    daemon_running: bool
    detail: str


def host_docker() -> HostDocker:
    """Check whether Docker and its rootless installation script are present on the
    host system.

    Returns
    -------
    HostDocker
        The status of Docker installation and daemon reachability.
    """
    if shutil.which("docker") is None:
        return HostDocker(
            installed=False,
            rootless_sh=False,
            daemon_running=False,
            detail="Docker CLI not found on host.",
        )
    try:
        rootless_sh = bool(_dockerd_rootless_sh())
    except OSError as err:  # Status probe should not crash; treat as "missing" and include detail
        return HostDocker(
            installed=True,
            rootless_sh=False,
            daemon_running=False,
            detail=str(err),
        )
    try:
        run(["docker", "info"], capture_output=True)
        return HostDocker(
            installed=True,
            rootless_sh=rootless_sh,
            daemon_running=True,
            detail="Bertrand rootless daemon reachable.",
        )
    except CommandError as err:
        return HostDocker(
            installed=True,
            rootless_sh=rootless_sh,
            daemon_running=False,
            detail=f"Docker daemon not reachable:\n\n{str(err)}",
        )


def local_docker_env() -> dict[str, str]:
    """Return an environment dictionary configured to use Bertrand's rootless
    Docker daemon.

    Returns
    -------
    dict[str, str]
        An environment dictionary for use with Bertrand's rootless Docker daemon.
    """
    env = os.environ.copy()
    env["DOCKER_HOST"] = f"unix://{_bertrand_socket_path()}"
    env["DOCKER_CONFIG"] = str(BERTRAND_DOCKER_CONFIG)
    env.setdefault("DOCKER_BUILDKIT", "1")  # enable buildkit by default
    for k in ("DOCKER_CONTEXT", "DOCKER_TLS_VERIFY", "DOCKER_CERT_PATH", "DOCKER_MACHINE_NAME"):
        env.pop(k, None)
    return env


def _read_os_release() -> dict[str, str]:
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


def _install_docker_apt(*, distro: str) -> None:
    """Install Docker packages needed for rootless operation, but do NOT enable
    docker.service.

    Parameters
    ----------
    distro : str
        The Linux distribution ID (e.g., "ubuntu", "debian") for constructing
        the Docker apt repository URL.

    Raises
    ------
    OSError
    """
    os_info = _read_os_release()
    codename = os_info.get("UBUNTU_CODENAME") or os_info.get("VERSION_CODENAME")
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
        "runc"
    ]
    installed: list[str] = []
    for pkg in conflicts:
        try:
            run(["dpkg", "-s", pkg], capture_output=True)
            installed.append(pkg)  # if successful
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
        "-o", "/etc/apt/keyrings/docker.asc"
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

    # Rootless prerequisites:
    # - docker-ce-rootless-extras provides dockerd-rootless.sh on deb/rpm installs
    # - uidmap provides newuidmap/newgidmap
    # - slirp4netns/fuse-overlayfs are commonly recommended for rootless networking/storage
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


def _install_docker_dnf() -> None:
    sudo = sudo_prefix()
    if not sudo:
        raise OSError("sudo is required to install Docker packages on this host.")

    # repo
    run([*sudo, "dnf", "install", "-y", "dnf-plugins-core"], check=False)
    run([*sudo, "dnf", "config-manager", "addrepo", "--from-repofile",
         "https://download.docker.com/linux/fedora/docker-ce.repo"])

    # Rootless prerequisites: fuse-overlayfs and iptables can be recommended on some
    # RHEL-family systems
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


def _persist_after_logout(user: str, *, assume_yes: bool) -> None:
    sudo = sudo_prefix()
    if not sudo or not shutil.which("loginctl"):
        return

    # Check current linger status
    try:
        cp = run(["loginctl", "show-user", user, "-p", "Linger"], capture_output=True)
        if "Linger=yes" in (cp.stdout or ""):
            return
    except CommandError:
        pass
    if confirm(
        f"Persist Docker containers even after '{user}' logs out? [y/N]",
        assume_yes=assume_yes
    ):
        run([*sudo, "loginctl", "enable-linger", user])


def _read_proc_sys(path: str) -> int | None:
    p = Path("/proc/sys") / path
    try:
        return int(p.read_text(encoding="utf-8").strip())
    except (OSError, ValueError):
        return None


def _configure_userns(*, assume_yes: bool) -> None:
    # These knobs vary by distro/kernel config, so treat missing as "unknown".
    unpriv = _read_proc_sys("kernel/unprivileged_userns_clone")
    maxns = _read_proc_sys("user/max_user_namespaces")

    # If explicitly disabled, give an actionable error (and optionally fix via sudo).
    if unpriv == 0 or (maxns is not None and maxns == 0):
        sudo = sudo_prefix()
        msg = (
            "Rootless Docker needs unprivileged user namespaces enabled. "
            f"(kernel.unprivileged_userns_clone={unpriv}, user.max_user_namespaces={maxns})"
        )
        if not sudo:
            raise OSError(msg + " (sudo not available to adjust sysctl)")
        if confirm(msg + "\nI can enable them using sudo sysctl. Proceed? [y/N] ",
                   assume_yes=assume_yes):
            # Best-effort set. (Some systems may require permanent config in /etc/sysctl.d/)
            if unpriv == 0:
                run([*sudo, "sysctl", "-w", "kernel.unprivileged_userns_clone=1"])
            if maxns == 0:
                run([*sudo, "sysctl", "-w", "user.max_user_namespaces=15000"])
        else:
            raise OSError(msg)


def _daemon_ready_fast() -> bool:
    sock = _bertrand_socket_path()
    if not sock.exists():
        return False
    if shutil.which("curl"):
        cp = run(
            ["curl", "-fsS", "--unix-socket", str(sock), "http://localhost/_ping"],
            check=False,
            capture_output=True,
        )
        return (cp.stdout or "").strip() == "OK"
    return False


def _wait_for_daemon(timeout: float = WAIT_DAEMON_TIMEOUT) -> None:
    deadline = time.time() + timeout
    last: Exception | None = None
    while time.time() < deadline:
        try:
            if _daemon_ready_fast():
                return
            # fall back to docker info if curl isn't available or ping didn't answer yet
            run(["docker", "info"], capture_output=True, env=local_docker_env())
            return
        except CommandError as err:
            last = err
            time.sleep(0.2)
    if last:
        raise last
    raise OSError("Timed out waiting for Bertrand rootless Docker daemon.")


def start_docker(*, assume_yes: bool = False) -> HostDocker:
    """Install everything needed for Bertrand rootless Docker and start the daemon.

    One-time sudo is used for:
    - package installation (apt/dnf)
    - /etc/subuid and /etc/subgid provisioning (if missing)

    After successful install, normal Bertrand docker operations never require sudo.

    Parameters
    ----------
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
    system = platform.system().lower()
    if system != "linux":
        raise OSError(
            "Bertrand rootless Docker auto-install is implemented only for Linux (apt/dnf)."
        )
    os_info = _read_os_release()
    distro = (os_info.get("ID") or "").lower()

    # determine host package manager
    installer: Callable[[], None] | None = None
    installer_name = "unknown"
    if shutil.which("apt") and distro in {"ubuntu", "debian"}:
        # pylint: disable=unnecessary-lambda-assignment
        installer = lambda: _install_docker_apt(distro=distro)
        installer_name = f"apt ({distro})"
    elif shutil.which("dnf"):
        installer = _install_docker_dnf
        installer_name = f"dnf ({distro})"
    else:
        raise OSError(
            f"Unsupported Linux distro ID '{distro}', and no supported package "
            "manager (apt/dnf) found."
        )

    # check for host docker daemon and install if missing
    host = host_docker()
    if not host.installed or not host.rootless_sh:
        if not confirm(
            "Bertrand requires a private, rootless Docker daemon.\n"
            f"I can install the required packages now using {installer_name} (via sudo).\n"
            "Proceed? [y/N] ",
            assume_yes=assume_yes
        ):
            raise OSError("Installation declined by user.")
        installer()

    # ensure dockerd-rootless.sh is present
    rootless_sh = _dockerd_rootless_sh()
    if rootless_sh is None:
        raise OSError(
            "dockerd-rootless.sh not found. Ensure docker rootless extras are installed "
            "(e.g., 'docker-ce-rootless-extras')."
        )

    # ensure we can map uids/gids for rootless daemon
    for tool in ("newuidmap", "newgidmap"):
        if shutil.which(tool) is None:
            raise OSError(f"Missing required tool '{tool}'. Install 'uidmap'.")
    for tool in ("rootlesskit", "slirp4netns"):
        if shutil.which(tool) is None:
            raise OSError(f"Missing required tool '{tool}'. (rootless dependency)")

    # ensure userns enabled in kernel
    _configure_userns(assume_yes=assume_yes)

    # reserve uid/gid ranges if needed
    user = host_username()
    _provision_subids(user, assume_yes=assume_yes)

    # ensure systemd user session is available
    if not _systemd_reachable():
        raise OSError(
            "systemd user units not available (systemctl --user). Bertrand requires "
            "systemd user sessions for rootless daemon management."
        )
    BERTRAND_DOCKER_DATA.mkdir(parents=True, exist_ok=True)
    try:
        BERTRAND_DOCKER_DATA.chmod(0o700)
    except OSError:
        pass  # best-effort

    # warn if configs may lead to reduced features
    if not Path("/sys/fs/cgroup/cgroup.controllers").exists():
        print(
            "Warning: cgroup v2 controllers not fully enabled. "
            "Rootless Docker may have reduced performance or functionality."
        )
    if shutil.which("fuse-overlayfs") is None:
        print("Warning: fuse-overlayfs not found; rootless storage may be slow (vfs fallback).")

    # ensure config dir + unit file, then start the user service and verify
    BERTRAND_DOCKER_CONFIG.mkdir(parents=True, exist_ok=True)
    try:
        BERTRAND_DOCKER_CONFIG.chmod(0o700)
    except OSError:
        pass  # best-effort
    _persist_after_logout(user, assume_yes=assume_yes)
    changed = _ensure_unit(rootless_sh=rootless_sh)
    if changed:
        run(
            ["systemctl", "--user", "try-restart", "bertrand-docker.service"],
            check=False,
            env=_systemd_user_env()
        )
    run(
        ["systemctl", "--user", "start", "bertrand-docker.service"],
        env=_systemd_user_env()
    )
    try:
        _wait_for_daemon()
        run(
            ["systemctl", "--user", "enable", "bertrand-docker.service"],
            env=_systemd_user_env()
        )  # enable on future login

    # if the daemon fails to start, report detailed diagnostics
    except CommandError as err:
        st = run(
            ["systemctl", "--user", "status", "bertrand-docker.service", "--no-pager"],
            check=False,
            capture_output=True,
            env=_systemd_user_env()
        )
        orig = f"{err.stdout}\n{err.stderr}".strip()
        st_err = f"{st.stdout}\n{st.stderr}".strip()
        parts = [
            f"=== docker info error ===\n{orig}",
            f"=== systemctl --user status ===\n{st_err}",
        ]
        if shutil.which("journalctl"):
            jl = run([
                "journalctl",
                "--user-unit",
                "bertrand-docker.service",
                "-n",
                "200",
                "--no-pager"
            ], check=False, capture_output=True)
            jl_err = f"{jl.stdout}\n{jl.stderr}".strip()
            if jl_err:
                parts.append(f"=== journalctl logs ===\n{jl_err}")
        raise CommandError(
            err.returncode,
            err.cmd,
            "",
            "\n\n".join(parts)
        ) from err

    # return updated host docker status
    return host_docker()


def remove_docker(
    *,
    assume_yes: bool = False,
    remove_data: bool = True,
    remove_packages: bool = False
) -> None:
    """
    Stop/remove Bertrand rootless daemon and (optionally) remove Docker packages.

    Parameters
    ----------
    assume_yes : bool
        If True, automatically answer yes to all prompts.  Default is False.
    remove_data : bool
        If True, delete Bertrand's Docker data-root (images/containers/volumes).
    remove_packages : bool
        If True, uninstall Docker packages from the host (apt/dnf). This uses sudo.

    Raises
    ------
    OSError
        If uninstallation fails or is declined by the user.
    """
    system = platform.system().lower()
    if system != "linux":
        raise OSError("Bertrand rootless Docker uninstall is implemented only for Linux.")

    if not confirm(
        "This will remove Bertrand's rootless Docker daemon configuration.\n" + (
            f"It will ALSO delete Bertrand's Docker data directory ({BERTRAND_DOCKER_DATA}).\n"
            if remove_data else
            "It will NOT delete Bertrand's Docker data directory.\n"
        ) +
        (
            "It will ALSO uninstall Docker OS packages from the host.\n"
            if remove_packages else
            "It will NOT uninstall Docker OS packages from the host.\n"
        ) +
        "Proceed? [y/N] ",
        assume_yes=assume_yes
    ):
        raise OSError("Uninstallation declined by user.")

    # Stop user service and remove unit
    if _systemd_reachable():
        run(
            ["systemctl", "--user", "stop", "bertrand-docker.service"],
            check=False,
            env=_systemd_user_env()
        )
        run(
            ["systemctl", "--user", "disable", "bertrand-docker.service"],
            check=False,
            env=_systemd_user_env()
        )
    unit_path = _bertrand_unit_path()
    if unit_path.exists():
        unit_path.unlink()
        if _systemd_reachable():
            run(
                ["systemctl", "--user", "daemon-reload"],
                check=False,
                env=_systemd_user_env()
            )

    # Remove runtime artifacts (best-effort)
    try:
        _bertrand_socket_path().unlink(missing_ok=True)
    except OSError:
        pass
    try:
        _bertrand_pidfile().unlink(missing_ok=True)
    except OSError:
        pass

    # Remove persistent data-root
    if remove_data:
        if BERTRAND_DOCKER_DATA.exists():
            shutil.rmtree(BERTRAND_DOCKER_DATA, ignore_errors=True)
        if BERTRAND_DOCKER_CONFIG.exists():
            shutil.rmtree(BERTRAND_DOCKER_CONFIG, ignore_errors=True)

    # Optionally remove host packages (apt/dnf)
    if remove_packages:
        sudo = sudo_prefix()
        if not sudo:
            raise OSError("sudo required to uninstall Docker packages.")

        os_info = _read_os_release()
        distro = (os_info.get("ID") or "").lower()

        if distro in {"ubuntu", "debian"} or shutil.which("apt"):
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

        elif distro in {"fedora"} or shutil.which("dnf"):
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

        else:
            raise OSError("Unsupported distro/package manager for package removal.")

    # Remove systemd runtime dir
    shutil.rmtree(_bertrand_runtime_dir(), ignore_errors=True)


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
    return run(
        ["docker", *args],
        check=check,
        capture_output=capture_output,
        input=input,
        cwd=cwd,
        env=local_docker_env()
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
    os.execvpe("docker", ["docker", *args], local_docker_env())
