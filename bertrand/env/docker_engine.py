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
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable

from .run import CommandError, atomic_write_text, confirm, host_username, run, sudo_prefix

#pylint: disable=redefined-builtin, global-statement


BERTRAND_SYSTEMD_DIR = Path.home() / ".config" / "systemd" / "user"
BERTRAND_STATE = Path.home() / ".local" / "share" / "bertrand"
BERTRAND_DOCKER_DATA = BERTRAND_STATE / "docker-data"
BERTRAND_DOCKER_CONFIG = BERTRAND_STATE / "docker-config"
SUBUID = Path("/etc/subuid")
SUBGID = Path("/etc/subgid")
RANGE = re.compile(r"^(?P<user>[^:]+):(?P<start>\d+):(?P<count>\d+)\s*$")


def _xdg_runtime_dir() -> Path:
    xdg = os.environ.get("XDG_RUNTIME_DIR")
    if xdg:
        return Path(xdg)
    return Path("/run/user") / str(os.getuid())


def _bertrand_socket_path() -> Path:
    return _xdg_runtime_dir() / "bertrand-docker.sock"


def _bertrand_exec_root() -> Path:
    return _xdg_runtime_dir() / "bertrand-docker"


def _bertrand_pidfile() -> Path:
    return _xdg_runtime_dir() / "bertrand-docker.pid"


def _bertrand_unit_path() -> Path:
    return BERTRAND_SYSTEMD_DIR / "bertrand-docker.service"


def _desired_unit_text(rootless_sh: Path) -> str:
    host = f"unix://{_bertrand_socket_path()}"
    data = str(BERTRAND_DOCKER_DATA)
    exe = str(_bertrand_exec_root())
    pidfile = str(_bertrand_pidfile())

    # Note: explicit PATH helps rootless helpers be found reliably under systemd.
    # Also, rootless networking is typically provided via RootlessKit+slirp4netns.
    return f"""\
[Unit]
Description=Bertrand Rootless Docker Daemon
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
Environment=PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
Environment=DOCKERD_ROOTLESS_ROOTLESSKIT_PORT_DRIVER=slirp4netns
ExecStart={rootless_sh} --host={host} --data-root={data} --exec-root={exe} --pidfile={pidfile}
ExecReload=/bin/kill -s HUP $MAINPID
Restart=on-failure
RestartSec=2
LimitNOFILE=1048576

[Install]
WantedBy=default.target
"""


def _ensure_unit(rootless_sh: Path) -> bool:
    unit_path = _bertrand_unit_path()
    desired = _desired_unit_text(rootless_sh)

    unit_path.parent.mkdir(parents=True, exist_ok=True)
    old = unit_path.read_text(encoding="utf-8") if unit_path.exists() else ""
    if old != desired:
        atomic_write_text(unit_path, desired)
        run(["systemctl", "--user", "daemon-reload"])
        return True
    return False


def _systemd_user_available() -> bool:
    if not shutil.which("systemctl"):
        return False
    try:
        cp = run(
            ["systemctl", "--user", "is-system-running"],
            check=False,              # critical: degraded returns non-zero
            capture_output=True
        )
    except (OSError, CommandError):
        return False

    out = (cp.stdout or "").strip().lower()
    err = (cp.stderr or "").strip().lower()

    # Hard failures: no user bus / no systemd user instance
    if "failed to connect to bus" in err or "no medium found" in err:
        return False

    # systemctl returns these states; some may be non-zero but still mean "reachable"
    if out in {"running", "degraded", "starting", "initializing", "maintenance", "stopping"}:
        return True

    # If we got *any* coherent state string, assume reachable.
    return bool(out)


def _dockerd_rootless_sh() -> Path | None:
    # Typically provided by docker-ce-rootless-extras
    which = shutil.which("dockerd-rootless.sh")
    candidates: list[Path] = []
    if which:
        candidates.append(Path(which))
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
    total = sum(r.count for r in ranges if r.user == user)
    return total >= needed


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


def _ensure_subids(user: str, *, assume_yes: bool) -> None:
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


def local_docker_env() -> dict[str, str]:
    """Return an environment dictionary configured to use Bertrand's rootless
    Docker daemon.

    Returns
    -------
    dict[str, str]
        An environment dictionary for use with Bertrand's rootless Docker daemon.
    """
    env = dict(os.environ)
    env["DOCKER_HOST"] = f"unix://{_bertrand_socket_path()}"
    env["DOCKER_CONFIG"] = str(BERTRAND_DOCKER_CONFIG)
    env.pop("DOCKER_CONTEXT", None)  # ensure user contexts never interfere
    return env


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
        run(["docker", "info"], capture_output=True, env=local_docker_env())
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

    # Ensure rootful system daemon is not enabled/started (we are rootless-only)
    if shutil.which("systemctl"):
        run([*sudo, "systemctl", "disable", "--now", "docker.service"], check=False)
        run([*sudo, "systemctl", "disable", "--now", "docker.socket"], check=False)
        run([*sudo, "systemctl", "disable", "--now", "containerd.service"], check=False)


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

    # Ensure rootful system daemon is not enabled/started (we are rootless-only)
    if shutil.which("systemctl"):
        run([*sudo, "systemctl", "disable", "--now", "docker.service"], check=False)
        run([*sudo, "systemctl", "disable", "--now", "docker.socket"], check=False)
        run([*sudo, "systemctl", "disable", "--now", "containerd.service"], check=False)


def _ensure_linger(user: str, *, assume_yes: bool) -> None:
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
        f"Allow Docker containers even when '{user}' is not logged in? [y/N]",
        assume_yes=assume_yes
    ):
        run([*sudo, "loginctl", "enable-linger", user])


def _launch_docker(*, assume_yes: bool = False) -> None:
    for tool in ("newuidmap", "newgidmap"):
        if shutil.which(tool) is None:
            raise OSError(f"Missing required tool '{tool}'. Install 'uidmap'.")

    user = host_username()
    _ensure_subids(user, assume_yes=assume_yes)
    rootless_sh = _dockerd_rootless_sh()
    if rootless_sh is None:
        raise OSError(
            "dockerd-rootless.sh not found. Ensure docker rootless extras are installed "
            "(e.g., 'docker-ce-rootless-extras')."
        )

    BERTRAND_DOCKER_DATA.mkdir(parents=True, exist_ok=True)
    _bertrand_exec_root().mkdir(parents=True, exist_ok=True)
    if not _systemd_user_available():
        raise OSError(
            "systemd user units not available (systemctl --user). "
            "Bertrand currently requires systemd user sessions for robust rootless "
            "daemon management."
        )

    # Ensure config dir + unit file, then start the user service and verify
    BERTRAND_DOCKER_CONFIG.mkdir(parents=True, exist_ok=True)
    _ensure_linger(user, assume_yes=assume_yes)
    changed = _ensure_unit(rootless_sh=rootless_sh)
    if changed:
        run(["systemctl", "--user", "try-restart", "bertrand-docker.service"], check=False)
    run(["systemctl", "--user", "start", "bertrand-docker.service"])
    try:
        run(["docker", "info"], capture_output=True, env=local_docker_env())
        run(["systemctl", "--user", "enable", "bertrand-docker.service"])  # enable on login
    except CommandError as err:
        st = run([
            "systemctl",
            "--user",
            "status",
            "bertrand-docker.service",
            "--no-pager"
        ], check=False, capture_output=True)
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


def ensure_docker(*, assume_yes: bool = False) -> HostDocker:
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
    """
    system = platform.system().lower()
    if system != "linux":
        raise OSError(
            "Bertrand rootless Docker auto-install is implemented only for Linux (apt/dnf)."
        )

    os_info = _read_os_release()
    distro = (os_info.get("ID") or "").lower()

    # Determine installer
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

    host = host_docker()
    if not host.installed or not host.rootless_sh:
        if not confirm(
            "Bertrand requires a private rootless Docker daemon.\n"
            f"I can install the required packages now using {installer_name} (via sudo).\n"
            "Proceed? [y/N] ",
            assume_yes=assume_yes
        ):
            raise OSError("Installation declined by user.")
        installer()

    # Ensure daemon running (may provision /etc/subuid,gid via sudo one time)
    _launch_docker(assume_yes=assume_yes)
    return host_docker()


def clean_docker(
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
    if _systemd_user_available():
        run(["systemctl", "--user", "stop", "bertrand-docker.service"], check=False)
        run(["systemctl", "--user", "disable", "bertrand-docker.service"], check=False)
    unit_path = _bertrand_unit_path()
    if unit_path.exists():
        unit_path.unlink()
        if _systemd_user_available():
            run(["systemctl", "--user", "daemon-reload"], check=False)

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

    # Remove runtime exec-root
    shutil.rmtree(_bertrand_exec_root(), ignore_errors=True)


def docker_cmd(
    args: list[str],
    *,
    check: bool = True,
    capture_output: bool = False,
    tee: bool = True,
    input: str | None = None,
    cwd: Path | None = None
) -> subprocess.CompletedProcess[str]:
    """Bertrand-only docker command. Always targets the Bertrand rootless daemon.

    Parameters
    ----------
    args : list[str]
        The docker command arguments (excluding the "docker" executable).
    check : bool, optional
        If True, raise CommandError on non-zero exit code.  Default is True.
    capture_output : bool, optional
        If True, capture stdout/stderr in the returned CompletedProcess.  Default is False.
    tee : bool, optional
        If True, tee stdout/stderr to the console while capturing, even if
        `capture_output` is false.  Default is True.
    input : str | None, optional
        Input to send to the command's stdin (default is None).
    cwd : Path | None, optional
        An optional working directory to run the command in.  If None (the default),
        then the current working directory will be used.

    Returns
    -------
    subprocess.CompletedProcess[str]
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
        tee=tee,
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
