"""Install Docker Engine and pull container images."""
import json
import hashlib
import os
import platform
import shutil
import subprocess

from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable

#pylint: disable=redefined-builtin


#######################
####    GENERAL    ####
#######################


DOCKER_NEEDS_SUDO: bool = False


def is_root() -> bool:
    """Detects whether the current user is the root user.
    
    Returns
    -------
    bool
        True if the user has root privileges, false otherwise
    """
    if os.name != "posix":
        return False
    return os.geteuid() == 0


def sudo_prefix() -> list[str]:
    """Return a base command prefix that uses `sudo` if the current user is not already
    root.

    Returns
    -------
    list[str]
        An empty list or a list containing the super-user command for the current OS.
    """
    # Prefer no sudo if already root.
    if is_root():
        return []
    return ["sudo"]


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
    cmd: list[str],
    *,
    check: bool = True,
    capture_output: bool = False,
    input: str | None = None,
) -> subprocess.CompletedProcess[str]:
    """A wrapper around `subprocess.run` that defaults to text mode and checks output.

    Parameters
    ----------
    cmd : list[str]
        The command and its arguments to run.
    check : bool, optional
        Whether to raise an exception if the command fails (default is True).
    capture_output : bool, optional
        Whether to capture stdout and stderr (default is False).
    input : str | None, optional
        Input to send to the command's stdin (default is None).

    Returns
    -------
    subprocess.CompletedProcess[str]
        The completed process result.
    """
    return subprocess.run(
        cmd,
        check=check,
        capture_output=capture_output,
        text=True,
        input=input,
    )


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
    if DOCKER_NEEDS_SUDO:
        sudo = sudo_prefix()
        return run([*sudo, "docker", *args], capture_output=capture_output, check=check)

    try:
        return run(["docker", *args], capture_output=capture_output, check=check)

    # fall back to sudo if `DOCKER_NEEDS_SUDO` is incorrect
    except subprocess.CalledProcessError as err:
        # re-run once with captured output to inspect the real message
        try:
            err2 = run(["docker", *args], capture_output=True)
            return err2  # unlikely - succeeded on second try
        except subprocess.CalledProcessError as err2:
            msg = ((err.stdout or "") + "\n" + (err.stderr or "")).lower()
            sudo = sudo_prefix()
            if (
                ("permission denied" in msg) and
                ("docker.sock" in msg or "/var/run/docker.sock" in msg) and
                sudo and shutil.which(sudo[0])
            ):
                return run([*sudo, "docker", *args], capture_output=capture_output, check=check)

            # Fall back to original error (preserve semantics)
            raise err2


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


def docker_status() -> DockerStatus:
    """Check whether docker is installed and whether the docker daemon is reachable.

    Returns
    -------
    DockerStatus
        A data struct with 3 fields:
            `docker_cli_found`: True if the docker CLI is found in PATH.
            `dockerd_reachable`: True if the docker daemon is reachable.
            `detail`: A human-readable string describing the failure mode, if any.
    """
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


def read_os_release() -> dict[str, str]:
    """Parse /etc/os-release to find OS codename and version information.

    Returns
    -------
    dict[str, str]
        A dictionary mapping keys to values from /etc/os-release.
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
        v = v.strip().strip('"').strip("'")
        data[k.strip()] = v

    return data


def install_docker_apt(*, distro: str) -> None:
    """Install Docker engine using the `apt` package manager.

    Parameters
    ----------
    distro : str
        The Linux distribution to install Docker for (e.g., "ubuntu", "debian").

    Raises
    ------
    ValueError
        If VERSION_CODENAME could not be read from /etc/os-release

    """
    os_info = read_os_release()
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


def install_docker_dnf() -> None:
    """Install Docker engine using the `dnf` package manager."""
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
    global DOCKER_NEEDS_SUDO  # pylint: disable=global-statement
    status = docker_status()
    DOCKER_NEEDS_SUDO = status.sudo_required
    if status.docker_cli_found and status.dockerd_reachable:
        return status

    system = platform.system().lower()
    if system != "linux":
        raise ValueError(
            "Atomatic Docker installation is currently implemented only for Linux "
            "hosts (apt/dnf).  Please install Docker for your platform, then rerun "
            "`bertrand_init`"
        )

    os_info = read_os_release()
    distro = (os_info.get("ID") or "").lower()

    # if CLI exists but daemon isn't reachable, try starting it first
    if status.docker_cli_found and not status.dockerd_reachable and shutil.which("systemctl"):
        sudo = sudo_prefix()
        run([*sudo, "systemctl", "start", "docker"], check=False)
        status2 = docker_status()
        if status2.dockerd_reachable:
            return status2

    # at this point, CLI exists but daemon isn't reachable or startable.  Likely a
    # permission or service state.  We already tried starting, so return status so that
    # caller can surface actionable diagnostics
    if status.docker_cli_found:
        return docker_status()

    # pylint: disable=unnecessary-lambda-assignment
    installer: Callable[[], None] | None = None
    installer_name = "unknown"
    if distro in {"ubuntu"}:
        installer = lambda: install_docker_apt(distro="ubuntu")
        installer_name = "apt (Ubuntu)"
    elif distro in {"debian"}:
        installer = lambda: install_docker_apt(distro="debian")
        installer_name = "apt (Debian)"
    elif distro in {"fedora"}:
        installer = install_docker_dnf
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
            installer = install_docker_dnf
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

    final_status = docker_status()
    if not final_status.docker_cli_found:
        raise ValueError(
            "Docker Engine installation failed - Docker CLI not found after "
            "installation script execution."
        )
    DOCKER_NEEDS_SUDO = final_status.sudo_required
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
            "Atomatic Docker uninstallation is currently implemented only for Linux "
            "hosts (apt/dnf).  Please uninstall Docker for your platform manually."
        )

    os_info = read_os_release()
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
    global DOCKER_NEEDS_SUDO  # pylint: disable=global-statement
    DOCKER_NEEDS_SUDO = True
    return True


###################
####    CLI    ####
###################


@dataclass(frozen=True)
class DockerEnvironment:
    """On-disk metadata representing a local Bertrand environment."""
    version: int
    path: str  # absolute path
    name: str  # final path component (user-facing)
    image: str  # e.g. "ubuntu:24.04"
    container: str  # stable docker container name
    workspace: str  # e.g. "workspace"
    created: str  # ISO timestamp


def sanitize_name(name: str) -> str:
    """Ensure that a given string is a valid Docker container name.

    Parameters
    ----------
    name : str
        The desired name.  This is usually the last component of the environment path.

    Returns
    -------
    str
        A sanitized version of the name that is valid for Docker container naming.
        Possibly an empty string if the input name had no valid characters.
    """
    # Docker container names allow [a-zA-Z0-9][a-zA-Z0-9_.-]
    out = []
    for char in name:
        if char.isalnum() or char in "._-":
            out.append(char)
        else:
            out.append("-")
    return "".join(out).strip("-")


def container_name(path: Path) -> str:
    """Generate a stable Docker container name for a given environment path.

    Parameters
    ----------
    path : Path
        The path to the environment.

    Returns
    -------
    str
        A stable Docker container name with a hash derived from the environment path.
    """
    # include a short hash of the absolute path to avoid collisions
    env_name = sanitize_name(path.name)
    h = hashlib.sha256(str(path).encode("utf-8")).hexdigest()[:10]
    return f"bertrand-{env_name}-{h}"


def write_environment(spec: DockerEnvironment) -> Path:
    """Write the metadata file for a Docker-based Bertrand environment.

    Parameters
    ----------
    spec : DockerEnvironment
        The environment specification to write.

    Returns
    -------
    Path
        The path to the environment directory where the metadata file was written.
    """
    path = Path(spec.path)
    path.mkdir(parents=True, exist_ok=True)
    path /= ".bertrand.json"
    path.write_text(json.dumps(asdict(spec), indent=2) + "\n", encoding="utf-8")
    return path.parent


def read_environment(path: Path) -> DockerEnvironment | None:
    """Read the metadata file for a Docker-based Bertrand environment.

    Parameters
    ----------
    path : Path
        The path to the environment.

    Returns
    -------
    DockerEnvironment | None
        The corresponding environment specification, or None if the metadata file does
        not exist.
    """
    path /= ".bertrand.json"
    if not path.exists():
        return None
    data = json.loads(path.read_text(encoding="utf-8"))
    return DockerEnvironment(**data)


def pull_image(image: str) -> None:
    """Pull a given Docker image from the remote registry if it is not already present
    locally.

    Parameters
    ----------
    image : str
        The Docker image name (e.g., "ubuntu:24.04").

    Raises
    ------
    subprocess.CalledProcessError
        If the docker pull command fails.
    """
    try:
        docker_cmd(["image", "inspect", image], capture_output=True)
    except subprocess.CalledProcessError:
        docker_cmd(["pull", image])


def build_container(spec: DockerEnvironment) -> None:
    """Build and/or start a persistent Docker container for the given environment.

    Parameters
    ----------
    spec : DockerEnvironment
        The environment specification to access or build if absent.

    Raises
    ------
    subprocess.CalledProcessError
        If any docker command fails.
    """
    path = Path(spec.path)
    workspace = spec.workspace

    # create container if absent
    try:
        docker_cmd(["container", "inspect", spec.container], capture_output=True)
    except subprocess.CalledProcessError:
        # Create a persistent container whose filesystem layer persists.
        # Keep it alive with `sleep infinity` so `docker exec` works.
        docker_cmd([
            "create",
            "--name", spec.container,
            "--hostname", spec.name,
            "--workdir", f"/{workspace}",
            "-v", f"{str(path)}:/{workspace}",

            # Persistent Bertrand identity (available to all exec'd shells)
            "-e", f"BERTRAND_ENV={spec.path}",
            "-e", f"BERTRAND_ENV_NAME={spec.name}",
            "-e", f"BERTRAND_ENV_CONTAINER={spec.container}",
            "-e", f"BERTRAND_ENV_WORKSPACE=/{workspace}",

            spec.image,
            "sleep", "infinity",
        ])

    # start container if not running
    container = docker_cmd(
        ["container", "inspect", "-f", "{{.State.Running}}", spec.container],
        capture_output=True,
    )
    if not container.stdout.strip().lower() == "true":
        docker_cmd(["start", spec.container])


def create_environment(
    path: Path,
    *,
    image: str = "ubuntu:latest",
    workspace: str = "workspace",
) -> DockerEnvironment:
    """Create (or load) a local Bertrand Docker environment at the given path.

    Parameters
    ----------
    path : Path
        The path at which to create the environment.
    image : str, optional
        The Docker image to use for the environment (default is "ubuntu:latest").
    workspace : str, optional
        The path within the container to mount the environment workspace (default is
        "workspace").

    Returns
    -------
    DockerEnvironment
        The created or loaded environment specification.
    """
    path = path.expanduser().resolve()
    path.mkdir(parents=True, exist_ok=True)

    # check for existing environment
    spec = read_environment(path)
    if spec is not None:
        pull_image(spec.image)
        build_container(spec)
        return spec

    # create new environment
    pull_image(image)
    spec = DockerEnvironment(
        version=1,
        path=str(path),
        name=path.name,
        image=image,
        container=container_name(path),
        workspace=workspace,
        created=datetime.now(timezone.utc).isoformat(),
    )
    write_environment(spec)
    build_container(spec)
    return spec


def enter_environment(path: Path) -> None:
    """Start and/or attach to a Bertrand Docker environment, dropping into an
    interactive shell.

    Parameters
    ----------
    path : Path
        The path to the environment.

    Raises
    ------
    ValueError
        If no environment is found at the given path.
    subprocess.CalledProcessError
        If any docker command fails.
    """
    path = path.expanduser().resolve()
    spec = read_environment(path)
    if spec is None:
        raise ValueError(
            f"No docker environment found at: {path} (missing .bertrand.json)"
        )

    pull_image(spec.image)
    build_container(spec)

    # Prefer bash if present, else sh.  Using exec rather than start -ai avoids needing
    # the container's main process to be a shell, and using `os.execvp` causes the
    # current process to be replaced by the shell, rather than spawning a new one
    cmd = [
        "docker", "exec", "-it",
        "-w", f"/{spec.workspace}",
        spec.container,
        "bash", "-l"
    ]
    if DOCKER_NEEDS_SUDO:
        sudo = sudo_prefix()
        if sudo and shutil.which(sudo[0]):
            cmd = [*sudo, *cmd]
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


def stop_environment(path: Path, *, force: bool = False) -> None:
    """Stop an environment container, but leave files and container intact.

    Parameters
    ----------
    path : Path
        The path to the environment.
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

    path = path.expanduser().resolve()
    spec = read_environment(path)
    if spec is None:
        raise ValueError(f"No docker environment found at: {path} (missing .bertrand.json)")

    # If container doesn't exist, treat as already-stopped
    try:
        docker_cmd(["container", "inspect", spec.container], capture_output=True, check=True)
    except subprocess.CalledProcessError:
        return

    if force:
        docker_cmd(["stop", "-t", "0", spec.container], check=False)
    else:
        docker_cmd(["stop", spec.container], check=False)


def delete_environment(
    path: Path,
    *,
    assume_yes: bool = False,
    force: bool = False
) -> None:
    """Delete a Bertrand Docker environment at the given path.

    Parameters
    ----------
    path : Path
        The path to the environment.
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

    path = path.expanduser().resolve()
    spec = read_environment(path)
    if spec is None:
        raise ValueError(f"No docker environment found at: {path} (missing .bertrand.json)")

    prompt = (
        f"This will permanently delete the environment at:\n  {path}\n"
        f"And remove the docker container:\n  {spec.container}\n"
        "Proceed? [y/N] "
    )
    if not confirm(prompt, assume_yes=assume_yes):
        raise ValueError("Environment deletion declined by user.")

    # Remove container if it exists
    try:
        docker_cmd(["container", "inspect", spec.container], capture_output=True)
        try:
            if force:
                docker_cmd(["rm", "-f", spec.container])
            else:
                # rm will fail if running; give a helpful error
                docker_cmd(["rm", spec.container])
        except subprocess.CalledProcessError as err:
            # If inspect failed, container already gone; if rm failed, surface guidance
            msg = ((err.stdout or "") + "\n" + (err.stderr or "")).strip()
            if msg:
                raise ValueError(
                    "Failed to remove docker container. If it's running, rerun with --force.\n"
                    f"detail:\n{msg}"
                ) from err
    except subprocess.CalledProcessError:
        pass  # container does not exist; nothing to do

    # Remove the directory on disk
    try:
        shutil.rmtree(path)
    except OSError as err:
        raise ValueError(f"Failed to remove environment directory: {path}\n{err}") from err
