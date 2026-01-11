"""Install Docker Engine and pull container images."""
import json
import os
import platform
import shutil
import shlex
import subprocess
import time
import uuid

from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable, TypedDict, List, Literal

#pylint: disable=redefined-builtin, global-statement


class MountInfo(TypedDict, total=False):
    """Type hint for docker container mount information."""
    Type: Literal["bind", "volume", "tmpfs", "npipe"]
    Destination: str
    Source: str


class ContainerState(TypedDict, total=False):
    """Type hint for docker container state information."""
    Running: bool


class ContainerInspect(TypedDict, total=False):
    """Type hint for docker container inspect output."""
    Mounts: List[MountInfo]
    State: ContainerState


#######################
####    GENERAL    ####
#######################


DOCKER_NEEDS_SUDO: bool | None = None


def confirm(prompt: str, *, assume_yes: bool) -> bool:
    """Ask the user for a yes/no confirmation for a given prompt.

    Parameters
    ----------
    prompt : str
        The prompt to display to the user.
    assume_yes : bool
        If True, automatically return True without prompting the user.

    Returns
    -------
    bool
        True if the user confirmed yes, false otherwise.
    """
    if assume_yes:
        return True
    try:
        response = input(prompt).strip().lower()
    except EOFError:
        return False
    return response in {"y", "yes"}


def run(
    argv: list[str],
    *,
    check: bool = True,
    capture_output: bool = False,
    capture_err: bool = False,
    input: str | None = None,
) -> subprocess.CompletedProcess[str]:
    """A wrapper around `subprocess.run` that defaults to text mode and checks output.

    Parameters
    ----------
    argv : list[str]
        The command and its arguments to run.
    check : bool, optional
        Whether to raise an exception if the command fails (default is True).
    capture_output : bool, optional
        Whether to capture stdout and stderr (default is False).
    capture_err : bool, optional
        Whether to capture stderr only (default is False).
    input : str | None, optional
        Input to send to the command's stdin (default is None).

    Returns
    -------
    subprocess.CompletedProcess[str]
        The completed process result.
    """
    # capture both stdout and stderr
    if capture_output:
        return subprocess.run(
            argv,
            check=check,
            capture_output=True,
            text=True,
            input=input,
        )

    # inherit stdout, but capture stderr for diagnostics
    if capture_err:
        return subprocess.run(
            argv,
            check=check,
            stdout=None,
            stderr=subprocess.PIPE,
            text=True,
            input=input,
        )

    # inherit both stdout and stderr
    return subprocess.run(
        argv,
        check=check,
        text=True,
        input=input,
    )


def sudo_prefix() -> list[str]:
    """Return a base command prefix that uses `sudo` if the current user is not already
    root.

    Returns
    -------
    list[str]
        An empty list or a list containing the super-user command for the current OS.
    """
    if os.name != "posix" or os.geteuid() == 0 or not shutil.which("sudo"):
        return []
    preserve = "DOCKER_HOST,DOCKER_CONTEXT,DOCKER_CONFIG"
    return ["sudo", f"--preserve-env={preserve}"]


def _sudo_fallback_is_sane() -> bool:
    ctx = os.environ.get("DOCKER_CONTEXT") or ""
    if ctx and ctx != "default":
        return False  # custom context: do not attempt sudo

    host = os.environ.get("DOCKER_HOST") or ""
    if not host:
        return True  # default: local docker CLI uses its normal defaults

    # Rootless default sockets often look like unix:///run/user/<uid>/docker.sock
    if host.startswith("unix:///run/user/"):
        return False

    # For other DOCKER_HOST values (tcp://, ssh://, unix://custom), sudo is not
    # reliably helpful.  We will only sudo on strong evidence of local socket permissions
    return True


def _looks_like_socket_permission_denied(stderr: str) -> bool:
    s = (stderr or "").lower()

    # Common variants:
    # - "permission denied while trying to connect to the docker daemon socket"
    # - "got permission denied while trying to connect to the docker daemon socket"
    # - "dial unix /var/run/docker.sock: connect: permission denied"
    # - "connect: permission denied"
    if "permission denied" not in s:
        return False

    # Strong indicator it is the local socket permission case
    return any(m in s for m in (
        "docker daemon socket",
        "docker.sock",
        "/var/run/docker.sock",
        "dial unix",
        "unix:///var/run/docker.sock",
    ))


def _probe_sudo() -> bool | None:
    # rootless / custom host: do not attempt sudo-based probing.
    if not _sudo_fallback_is_sane():
        return False
    try:
        run(["docker", "info"], capture_output=True)
        return False
    except subprocess.CalledProcessError as err:
        stderr = (err.stderr or "") + "\n" + (err.stdout or "")
        if _looks_like_socket_permission_denied(stderr):
            return True
        return None


def _init_sudo() -> None:
    global DOCKER_NEEDS_SUDO
    if DOCKER_NEEDS_SUDO is None:
        DOCKER_NEEDS_SUDO = _probe_sudo() is True


def docker_cmd(
    args: list[str],
    *,
    check: bool = True,
    capture_output: bool = False,
) -> subprocess.CompletedProcess[str]:
    """Run a docker command using sudo if the user lacks access to the docker socket.

    Parameters
    ----------
    args : list[str]
        The docker command arguments (excluding "docker" itself).
    check : bool, optional
        Whether to raise an exception if the command fails (default is True).
    capture_output : bool, optional
        Whether to capture stdout and stderr (default is False).

    Returns
    -------
    subprocess.CompletedProcess[str]
        The completed process result.

    Raises
    ------
    subprocess.CalledProcessError
        If the docker command fails.
    """
    global DOCKER_NEEDS_SUDO
    _init_sudo()
    cmd = ["docker", *args]

    # first attempt: use cached state (or optimistic non-sudo if unknown)
    if DOCKER_NEEDS_SUDO is True:
        sudo = sudo_prefix()
        if sudo:
            return run([*sudo, *cmd], check=check, capture_output=capture_output)
        # if we think sudo is needed but can't use it, just run and fail
        return run(cmd, check=check, capture_output=capture_output)

    try:
        #inherit stdout and capture stderr so we can classify permission errors
        return run(
            cmd,
            check=check,
            capture_output=capture_output,
            capture_err=not capture_output,
        )
    except subprocess.CalledProcessError as err:
        stderr = (err.stderr or "") + "\n" + (err.stdout or "")
        if _sudo_fallback_is_sane() and _looks_like_socket_permission_denied(stderr):
            sudo = sudo_prefix()
            if sudo:  # retry once with sudo and cache
                result = run([*sudo, *cmd], check=check, capture_output=capture_output)
                DOCKER_NEEDS_SUDO = True
                return result
        raise


def docker_argv(args: list[str]) -> list[str]:
    """Build argv for exec-style docker calls, honoring sudo policy.

    Parameters
    ----------
    args : list[str]
        The docker command arguments, excluding 'docker' itself.

    Returns
    -------
    list[str]
        The full argv to use for `os.exec*` calls.
    """
    _init_sudo()
    argv = ["docker", *args]
    if DOCKER_NEEDS_SUDO is True:
        sudo = sudo_prefix()
        if sudo:
            argv = [*sudo, *argv]
    return argv


############################
####    INSTALLATION    ####
############################


@dataclass(frozen=True)
class DockerStatus:
    """A data struct that indicates whether docker is installed and reachable."""
    docker_cli_found: bool
    dockerd_reachable: bool
    sudo_required: bool
    detail: str


def _docker_status() -> DockerStatus:
    if not shutil.which("docker"):
        return DockerStatus(
            docker_cli_found=False,
            dockerd_reachable=False,
            sudo_required=False,
            detail="Docker CLI not found in PATH."
        )

    sudo = sudo_prefix()

    # determine whether the docker daemon is reachable.  This can fail for:
    # - not installed (client only)
    # - daemon not running
    # - permissions (not in docker group; needs sudo)
    try:
        run(["docker", "info"], capture_output=True)
        return DockerStatus(
            docker_cli_found=True,
            dockerd_reachable=True,
            sudo_required=False,
            detail="docker info succeeded",
        )
    except subprocess.CalledProcessError as err:
        out = (err.stdout or "") + "\n" + (err.stderr or "")
        out = out.strip()
        lower = out.lower()

        # Permission-denied implies Engine exists but user lacks access - try again
        # with sudo
        if (
            _sudo_fallback_is_sane() and
            "permission denied" in lower and
            ("docker.sock" in lower or "/var/run/docker.sock" in lower) and
            sudo and shutil.which(sudo[0])
        ):
            try:
                run([*sudo, "docker", "info"], capture_output=True)
                return DockerStatus(
                    docker_cli_found=True,
                    dockerd_reachable=True,
                    sudo_required=True,
                    detail="docker daemon reachable via sudo (user lacks docker socket permission)",
                )
            except subprocess.CalledProcessError as err2:
                out2 = ((err2.stdout or "") + "\n" + (err2.stderr or "")).strip()
                return DockerStatus(
                    docker_cli_found=True,
                    dockerd_reachable=False,
                    sudo_required=True,
                    detail=f"docker CLI found, but 'sudo docker info' failed:\n\n{out2}",
                )

        # "Cannot connect" often means daemon not started (or missing Engine)
        if "cannot connect to the docker daemon" in lower:
            return DockerStatus(
                docker_cli_found=True,
                dockerd_reachable=False,
                sudo_required=False,
                detail=
                    "docker CLI found, but daemon not reachable (daemon stopped or "
                    "engine missing)",
            )

        return DockerStatus(
            docker_cli_found=True,
            dockerd_reachable=False,
            sudo_required=False,
            detail=f"docker CLI found, but daemon not reachable:\n\n{out}",
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
        v = v.strip().strip('"').strip("'")
        data[k.strip()] = v

    return data


def _install_docker_apt(*, distro: str) -> None:
    os_info = _read_os_release()
    codename = os_info.get("UBUNTU_CODENAME") or os_info.get("VERSION_CODENAME")
    if not codename:
        raise ValueError("Could not determine VERSION_CODENAME from /etc/os-release")

    sudo = sudo_prefix()

    # remove commonly conflicting unofficial packages (safe to ignore failures);
    # Docker docs enumerate these conflicts themselves
    conflicts = [
        "docker.io",
        "docker-compose",
        "docker-compose-v2",
        "docker-doc",
        "podman-docker",
        "containerd",
        "runc"
    ]

    # only remove those that appear installed, to avoid noisy apt errors
    installed: list[str] = []
    for pkg in conflicts:
        try:
            run(["dpkg", "-s", pkg], capture_output=True)
            installed.append(pkg)
        except subprocess.CalledProcessError:
            pass
    if installed:
        run([*sudo, "apt", "remove", "-y", *installed], check=False)

    # Add Docker's official GPG key and repository (per Docker docs)
    run([*sudo, "apt", "update"])
    run([*sudo, "apt", "install", "-y", "ca-certificates", "curl"])
    run([*sudo, "install", "-m", "0755", "-d", "/etc/apt/keyrings"])
    run([
        *sudo,
        "curl",
        "-fsSL",
        f"https://download.docker.com/linux/{distro}/gpg",
        "-o",
        "/etc/apt/keyrings/docker.asc"
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
        ])
    )
    run([*sudo, "apt", "update"])
    run([
        *sudo,
        "apt",
        "install",
        "-y",
        "docker-ce",
        "docker-ce-cli",
        "containerd.io",
        "docker-buildx-plugin",
        "docker-compose-plugin",
    ])

    # try to start service if systemd is present. If it fails, user can start manually
    if shutil.which("systemctl"):
        run([*sudo, "systemctl", "enable", "--now", "docker"], check=False)


def _install_docker_dnf() -> None:
    sudo = sudo_prefix()

    # ensure config-manager is available
    run([*sudo, "dnf", "install", "-y", "dnf-plugins-core"], check=False)

    run([
        *sudo,
        "dnf",
        "config-manager",
        "addrepo",
        "--from-repofile",
        "https://download.docker.com/linux/fedora/docker-ce.repo",
    ])

    run([
        *sudo,
        "dnf",
        "install",
        "-y",
        "docker-ce",
        "docker-ce-cli",
        "containerd.io",
        "docker-buildx-plugin",
        "docker-compose-plugin",
    ])

    if shutil.which("systemctl"):
        run([*sudo, "systemctl", "enable", "--now", "docker"], check=False)


def install_docker(*, assume_yes: bool = False) -> DockerStatus:
    """Check whether Docker Engine is installed, and attempt to install it if not.

    Parameters
    ----------
    assume_yes : bool
        If True, automatically attempt to install Docker without prompting the user.

    Returns
    -------
    DockerStatus
        A data struct with 3 fields:
            `docker_cli_found`: True if the docker CLI is found in PATH.
            `dockerd_reachable`: True if the docker daemon is reachable.
            `detail`: A human-readable string describing the failure mode, if any.

    Raises
    ------
    ValueError
        If automatic installation is not supported for the host OS.
    """
    status = _docker_status()
    if status.docker_cli_found and status.dockerd_reachable:
        return status

    system = platform.system().lower()
    if system != "linux":
        raise ValueError(
            "Automatic Docker installation is currently implemented only for Linux "
            "hosts (apt/dnf).  Please install Docker for your platform, then rerun "
            "`bertrand_init`"
        )

    os_info = _read_os_release()
    distro = (os_info.get("ID") or "").lower()

    # if CLI exists but daemon isn't reachable, try starting it first
    if status.docker_cli_found and not status.dockerd_reachable and shutil.which("systemctl"):
        sudo = sudo_prefix()
        run([*sudo, "systemctl", "start", "docker"], check=False)
        status2 = _docker_status()
        if status2.dockerd_reachable:
            return status2

    # at this point, CLI exists but daemon isn't reachable or startable.  Likely a
    # permission or service state.  We already tried starting, so return status so that
    # caller can surface actionable diagnostics
    if status.docker_cli_found:
        return _docker_status()

    # pylint: disable=unnecessary-lambda-assignment
    installer: Callable[[], None] | None = None
    installer_name = "unknown"
    if distro in {"ubuntu"}:
        installer = lambda: _install_docker_apt(distro="ubuntu")
        installer_name = "apt (Ubuntu)"
    elif distro in {"debian"}:
        installer = lambda: _install_docker_apt(distro="debian")
        installer_name = "apt (Debian)"
    elif distro in {"fedora"}:
        installer = _install_docker_dnf
        installer_name = "dnf (Fedora)"
    else:
        # best-effort - detect package manager even if distro unknown
        if shutil.which("apt"):
            # Default to Debian-style repo URL unless we can confirm Ubuntu; safer is to refuse.
            raise ValueError(
                f"Unsupported Linux distro ID '{distro}'.  I found 'apt', but I won't "
                "guess whether to use the Ubuntu or Debian repository. Install Docker "
                "manually, or extend install_docker() to handle this distro."
            )
        if shutil.which("dnf"):
            installer = _install_docker_dnf
            installer_name = "dnf (unknown distro)"
        else:
            raise ValueError(
                f"Unsupported Linux distro ID '{distro}', and no known package manager "
                "(`apt` or `dnf`) found for automatic installation.  Please install "
                "Docker manually, then rerun."
            )

    prompt = (
        "Docker Engine is required to continue, but it is not installed.\n"
        f"I can attempt to install it now using {installer_name} (this will run "
        "commands via sudo).\n"
        "Proceed with Docker installation? [y/N] "
    )
    if not confirm(prompt, assume_yes=assume_yes):
        raise ValueError(
            "Docker Engine is required - installation declined by user."
        )

    assert installer is not None
    installer()

    final_status = _docker_status()
    if not final_status.docker_cli_found:
        raise ValueError(
            "Docker Engine installation failed - Docker CLI not found after "
            "installation script execution."
        )
    return final_status


def uninstall_docker(*, assume_yes: bool = False, remove_data: bool = True) -> None:
    """Uninstall Docker Engine from the host system.

    Parameters
    ----------
    assume_yes : bool
        If True, automatically attempt to uninstall Docker without prompting the user.
    remove_data : bool
        If True, also delete /var/lib/docker and /var/lib/containerd to remove all
        Docker images, containers, volumes, and networks.

    Raises
    ------
    ValueError
        If automatic uninstallation is not supported for the host OS.
    """
    system = platform.system().lower()
    if system != "linux":
        raise ValueError(
            "Automatic Docker uninstallation is currently implemented only for Linux "
            "hosts (apt/dnf).  Please uninstall Docker for your platform manually."
        )

    os_info = _read_os_release()
    distro = (os_info.get("ID") or "").lower()
    sudo = sudo_prefix()

    warning = (
        "This will uninstall Docker Engine from this machine.\n" + (
            "It will ALSO delete /var/lib/docker and /var/lib/containerd, removing "
            "existing images, containers, volumes, and networks.\n"
            if remove_data else
            "It will NOT delete /var/lib/docker or /var/lib/containerd, so existing "
            "images, containers, volumes, and networks will be preserved.\n"
        ) +
        "Proceed with Docker uninstallation? [y/N] "
    )
    if not confirm(warning, assume_yes=assume_yes):
        raise ValueError("Docker Engine uninstallation declined by user.")

    # stop daemon if present (best-effort)
    if shutil.which("systemctl"):
        run([*sudo, "systemctl", "stop", "docker"], check=False)
        run([*sudo, "systemctl", "stop", "containerd"], check=False)

    # purge official Docker packages (per Docker docs).  Include
    # docker-ce-rootless-extras since Docker does not include it in purge list
    if distro in {"ubuntu", "debian"} or shutil.which("apt"):
        packages = [
            "docker-ce",
            "docker-ce-cli",
            "containerd.io",
            "docker-buildx-plugin",
            "docker-compose-plugin",
            "docker-ce-rootless-extras",
        ]
        run([*sudo, "apt", "purge", "-y", *packages], check=False)

        # also remove common conflicting/unofficial packages if present (best-effort)
        run([
            *sudo,
            "apt",
            "purge",
            "-y",
             "docker.io",
             "docker-compose",
             "docker-compose-v2",
             "docker-doc",
             "podman-docker"
        ], check=False)

        # remove our repo/keyring artifacts (if they exist)
        run([*sudo, "rm", "-f", "/etc/apt/sources.list.d/docker.sources"], check=False)
        run([*sudo, "rm", "-f", "/etc/apt/keyrings/docker.asc"], check=False)

        # refresh apt state
        run([*sudo, "apt", "update"], check=False)
        run([*sudo, "apt", "autoremove", "-y"], check=False)

    elif distro in {"fedora"} or shutil.which("dnf"):
         # Remove official Docker packages (per Docker docs)
        packages = [
            "docker-ce",
            "docker-ce-cli",
            "containerd.io",
            "docker-buildx-plugin",
            "docker-compose-plugin",
            "docker-ce-rootless-extras",
        ]
        run([*sudo, "dnf", "remove", "-y", *packages], check=False)

        # Remove repo file created by "dnf config-manager addrepo ..."
        run([*sudo, "rm", "-f", "/etc/yum.repos.d/docker-ce.repo"], check=False)

    else:
        raise ValueError(
            f"Unsupported Linux distro ID '{distro}', and no supported package manager found."
        )

    if remove_data:
        # Docker docs: these paths contain images/containers/volumes and are not
        # removed automatically
        run([*sudo, "rm", "-rf", "/var/lib/docker"], check=False)
        run([*sudo, "rm", "-rf", "/var/lib/containerd"], check=False)


def add_to_docker_group(*, assume_yes: bool = False) -> bool:
    """Offer to add the current user to the 'docker' group so that docker can be
    run without requiring sudo privileges.

    Parameters
    ----------
    assume_yes : bool
        If True, automatically attempt to add the user without prompting.

    Returns
    -------
    bool
        True if the user was added to the docker group, false otherwise.
    """
    if os.name != "posix":
        return False

    user = os.environ.get("SUDO_USER") or os.environ.get("USER") or ""
    if not user:
        return False

    # if already in docker group, nothing to do
    try:
        cp = run(["id", "-nG", user], capture_output=True)
        groups = set(cp.stdout.strip().split())
        if "docker" in groups:
            return True
    except subprocess.CalledProcessError:
        pass

    prompt = (
        f"Your user '{user}' does not have permission to access Docker without sudo.\n"
        "I can add you to the 'docker' group (requires sudo). You will need to log "
        "out and back in\n"
        "for this to take effect (or run 'newgrp docker' in your shell).\n"
        "Proceed? [y/N] "
    )
    if not confirm(prompt, assume_yes=assume_yes):
        return False

    # ensure group exists (best-effort), and add user
    sudo = sudo_prefix()
    run([*sudo, "groupadd", "docker"], check=False)
    run([*sudo, "usermod", "-aG", "docker", user])
    global DOCKER_NEEDS_SUDO
    DOCKER_NEEDS_SUDO = True
    return True


def remove_from_docker_group(*, assume_yes: bool = False) -> bool:
    """Offer to remove the current user from the 'docker' group.

    Parameters
    ----------
    assume_yes : bool
        If True, automatically attempt to remove the user without prompting.

    Returns
    -------
    bool
        True if the user was removed from the docker group, false otherwise.
    """
    if os.name != "posix":
        return False

    user = os.environ.get("SUDO_USER") or os.environ.get("USER") or ""
    if not user:
        return False

    prompt = (
        f"Do you want to remove your user '{user}' from the 'docker' group? [y/N] "
    )
    if not confirm(prompt, assume_yes=assume_yes):
        return False

    sudo = sudo_prefix()
    run([*sudo, "gpasswd", "-d", user, "docker"], check=False)
    global DOCKER_NEEDS_SUDO
    DOCKER_NEEDS_SUDO = None  # re-probe on next docker command
    return True


###################
####    CLI    ####
###################


WORKSPACE: str = "/env"


@dataclass(frozen=True)
class DockerEnvironment:
    """On-disk metadata representing a local Bertrand environment.  Specific care is
    taken not to store anything that references the host filesystem, in order to allow
    renaming/relocation of the environment directory.
    """
    version: int
    env_id: str  # UUID used to derive the container name ('bertrand-{env_id}')
    created: str  # ISO timestamp
    image: str  # e.g. "ubuntu:24.04"
    shell: list[str]  # shell command to execute during `bertrand enter`


def _sanitize_environment_name(name: str) -> str:
    out = []
    for char in name:
        if char.isalnum() or char in "._-":
            out.append(char)
        else:
            out.append("-")
    return "".join(out).strip("-")


def _container_name(spec: DockerEnvironment) -> str:
    return f"bertrand-{spec.env_id}"


def _normalize_shell(value: object) -> list[str]:
    if isinstance(value, list) and all(isinstance(x, str) and x for x in value):
        return value
    if isinstance(value, str):
        argv = shlex.split(value.strip())
        if argv:
            return argv
        raise ValueError("shell must not be empty")
    raise ValueError("shell must be a string or list[str]")


def _atomic_write_text(path: Path, text: str) -> None:
    tmp = path.with_name(f"{path.name}.tmp.{os.getpid()}.{int(time.time())}")
    tmp.write_text(text, encoding="utf-8")
    try:
        with tmp.open("r+", encoding="utf-8") as f:
            f.flush()
            os.fsync(f.fileno())
    except OSError:
        pass
    tmp.replace(path)


def _write_environment(env_root: Path, spec: DockerEnvironment) -> None:
    env_root.mkdir(parents=True, exist_ok=True)
    _atomic_write_text(env_root / ".bertrand.json", json.dumps(asdict(spec), indent=2) + "\n")


def _read_environment(env_root: Path) -> DockerEnvironment | None:
    meta = env_root / ".bertrand.json"
    if not meta.exists():
        return None

    try:
        data = json.loads(meta.read_text(encoding="utf-8"))
        if not isinstance(data, dict):
            raise ValueError("environment metadata must be a JSON object")

        # validate version
        version = data.get("version")
        if not isinstance(version, int) or version <= 0:
            raise ValueError(f"missing or invalid 'version' field: {version}")

        # validate created timestamp
        created = data.get("created")
        if not isinstance(created, str) or not created.strip():
            raise ValueError(f"missing or invalid 'created' field: {created}")
        try:
            datetime.fromisoformat(created)
        except Exception as err:
            raise ValueError(f"created must be a valid ISO timestamp: {created}") from err

        # validate environment id
        env_id = data.get("env_id")
        if not isinstance(env_id, str) or not env_id.strip():
            raise ValueError(f"missing or invalid 'env_id' field: {env_id}")
        try:
            uuid.UUID(env_id)
        except Exception as err:
            raise ValueError(f"env_id must be a valid UUID string: {env_id}") from err

        # validate image
        image = data.get("image")
        if not isinstance(image, str) or not image.strip():
            raise ValueError(f"missing or invalid 'image' field: {image}")

        # validate shell command
        shell = data.get("shell")
        if not isinstance(shell, (str, list)) or not shell:
            raise ValueError("missing required field: shell")
        data["shell"] = _normalize_shell(shell)

        return DockerEnvironment(**data)

    except Exception as err:
        raise ValueError(f"Invalid environment metadata at {meta}: {err}") from err


def _pull_image(image: str) -> None:
    try:
        docker_cmd(["image", "inspect", image], capture_output=True)
    except subprocess.CalledProcessError:
        docker_cmd(["pull", image])


def _container_inspect(name: str) -> ContainerInspect | None:
    try:
        result = docker_cmd(["container", "inspect", name], capture_output=True)
        data = json.loads(result.stdout)
        return data[0] if data else None
    except (subprocess.CalledProcessError, json.JSONDecodeError, IndexError, TypeError):
        return None


def _remove_container(name: str, *, force: bool = False) -> None:
    if force:
        docker_cmd(["rm", "-f", name], check=False, capture_output=True)
    else:
        docker_cmd(["rm", name], check=False, capture_output=True)


def _get_mount_source(container_info: ContainerInspect) -> Path | None:
    mounts = container_info.get("Mounts") or []
    for m in mounts:
        if m.get("Type") == "bind" and m.get("Destination") == WORKSPACE:
            src = m.get("Source")
            if src:
                return Path(src).expanduser()
    return None


def _container_build_args(env_root: Path, spec: DockerEnvironment) -> list[str]:
    env_root = env_root.expanduser().resolve()
    container = _container_name(spec)
    return [
        "create",
        "--init",
        "--name", container,
        "--hostname", _sanitize_environment_name(env_root.name),
        "--workdir", WORKSPACE,
        "-v", f"{str(env_root)}:{WORKSPACE}",
        "-e", f"BERTRAND_ENV={spec.env_id}",
        spec.image,
        "sleep", "infinity",
    ]


def _ensure_container(env_root: Path, spec: DockerEnvironment) -> str:
    env_root = env_root.expanduser().resolve()
    container = _container_name(spec)

    # if the environment directory moved, the existing container's bind mount may be
    # stale.  Docker does not support editing mounts in-place, but we can stop, rm,
    # and recreate the container if needed.  Note that this will remove any data that
    # is not stored in the bind mount (i.e., in the container's root filesystem), but
    # those can be recovered by refreshing the container's image and/or re-installing
    # the contents of the bind mount.
    info = _container_inspect(container)
    if info is not None:
        mount_src = _get_mount_source(info)
        mount_ok = False
        if mount_src is not None:
            try:
                mount_ok = mount_src.resolve() == env_root
            except OSError:
                mount_ok = False

        # if mount is missing or points somewhere else, rebuild
        if not mount_ok:
            docker_cmd(["stop", container], check=False, capture_output=True)
            _remove_container(container, force=True)
            info = None

    if info is None:
        docker_cmd(_container_build_args(env_root, spec))
        info = _container_inspect(container)

    running = bool(((info or {}).get("State") or {}).get("Running"))
    if not running:
        docker_cmd(["start", container])

    return container


def create_environment(
    env_root: Path,
    *,
    image: str,
    shell: str | list[str],
) -> DockerEnvironment:
    """Create (or load) a local Bertrand Docker environment at the given path.

    Parameters
    ----------
    env_root : Path
        The path at which to create the environment directory.
    image : str
        The Docker image to use for the environment.
    shell : str | list[str]
        The shell command to execute when entering the environment

    Returns
    -------
    DockerEnvironment
        The created or loaded environment specification.
    """
    env_root = env_root.expanduser().resolve()
    env_root.mkdir(parents=True, exist_ok=True)

    # check for existing environment
    spec = _read_environment(env_root)
    if spec is None:
        spec = DockerEnvironment(
            version=1,
            env_id=str(uuid.uuid4()),
            created=datetime.now(timezone.utc).isoformat(),
            image=image,
            shell=_normalize_shell(shell),
        )
        _write_environment(env_root, spec)

    # ensure image and container
    _pull_image(spec.image)
    _ensure_container(env_root, spec)
    return spec


def enter_environment(env_root: Path) -> None:
    """Start and/or attach to a Bertrand Docker environment, dropping into an
    interactive shell.

    Parameters
    ----------
    env_root : Path
        The path to the environment directory.

    Raises
    ------
    ValueError
        If no environment is found at the given path.
    subprocess.CalledProcessError
        If any docker command fails.
    """
    env_root = env_root.expanduser().resolve()
    spec = _read_environment(env_root)
    if spec is None:
        raise ValueError(
            f"No docker environment found at: {env_root} (missing .bertrand.json)"
        )

    _pull_image(spec.image)
    container = _ensure_container(env_root, spec)

    cmd = docker_argv([
        "exec", "-it",
        "-w", WORKSPACE,
        container,
        *spec.shell
    ])
    os.execvp(cmd[0], cmd)


def in_environment() -> bool:
    """Detect whether the current process is running inside a Bertrand Docker
    container.

    Returns
    -------
    bool
        True if running inside a Bertrand Docker container, false otherwise.
    """
    return bool(os.environ.get("BERTRAND_ENV"))


def find_environment(start: Path) -> Path:
    """Navigate to the root of the Bertrand environment containing the given path.

    Parameters
    ----------
    start : Path
        The starting path to search from.

    Returns
    -------
    Path
        The path to the root of the Bertrand environment's mount directory.

    Raises
    ------
    FileNotFoundError
        If no .bertrand.json file is found in any parent directory, indicating that
        `start` does not lie within a Bertrand environment.
    """
    start = start.expanduser().resolve()
    if start.is_file():
        start = start.parent

    for p in (start, *start.parents):
        if (p / ".bertrand.json").exists():
            return p

    raise FileNotFoundError(
        f"No .bertrand.json found in any parent directory starting from {start}"
    )


def stop_environment(env_root: Path, *, force: bool) -> None:
    """Stop an environment container, but leave files and container intact.

    Parameters
    ----------
    env_root : Path
        The path to the environment directory.
    force : bool
        If True, forcibly stop the docker container without waiting.

    Raises
    ------
    RuntimeError
        If called from inside a Bertrand Docker environment.
    ValueError
        If no environment is found at the given path.
    subprocess.CalledProcessError
        If any docker command fails.
    """
    if in_environment():
        raise RuntimeError("Cannot stop an environment from inside a Bertrand environment.")

    env_root = env_root.expanduser().resolve()
    spec = _read_environment(env_root)
    if spec is None:
        raise ValueError(f"No docker environment found at: {env_root} (missing .bertrand.json)")

    container = f"bertrand-{spec.env_id}"
    timeout = "0" if force else "10"
    docker_cmd(["stop", "-t", timeout, container], check=False, capture_output=True)


def delete_environment(
    env_root: Path,
    *,
    assume_yes: bool,
    force: bool
) -> None:
    """Delete a Bertrand Docker environment at the given path.

    Parameters
    ----------
    env_root : Path
        The path to the environment directory.
    assume_yes : bool
        If True, automatically confirm deletion without prompting the user.
    force : bool
        If True, forcibly remove the docker container even if it is running.

    Raises
    ------
    RuntimeError
        If called from inside a Bertrand Docker environment.
    ValueError
        If no environment is found at the given path, or if deletion fails.
    """
    if in_environment():
        raise RuntimeError(
            "Cannot delete an environment from inside a Bertrand environment."
        )

    env_root = env_root.expanduser().resolve()
    spec = _read_environment(env_root)
    if spec is None:
        raise ValueError(f"No docker environment found at: {env_root} (missing .bertrand.json)")

    container = f"bertrand-{spec.env_id}"
    prompt = (
        f"This will permanently delete the environment at:\n  {env_root}\n"
        f"And remove the docker container:\n  {container}\n"
        "Proceed? [y/N] "
    )
    if not confirm(prompt, assume_yes=assume_yes):
        raise ValueError("Environment deletion declined by user.")

    # remove container (best-effort)
    _remove_container(container, force=force)

    # remove environment directory
    try:
        shutil.rmtree(env_root)
    except OSError as err:
        raise ValueError(f"Failed to remove environment directory: {env_root}\n{err}") from err
