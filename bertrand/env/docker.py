"""Install Docker Engine and pull container images."""
import json
import hashlib
import os
import shlex
import shutil
import uuid

from dataclasses import asdict, dataclass, replace
from datetime import datetime, timezone
from pathlib import Path
from typing import List, Literal, TypedDict

from .docker_engine import docker_cmd, docker_exec
from .run import CommandError, atomic_write_text, confirm, host_user_ids, run, sudo_prefix

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


# TODO: rather than give every image a unique tag, I should use a tag that reflects
# the build configuration (e.g. the dockerfile digest).  This should allow me to
# distribute multiple environments that share a common configuration without
# requiring redundant storage or builds for each one.


BERTRAND_DIR: str = ".bertrand"
ENV_FILE: str = "env.json"
DOCKER_FILE: str = "Dockerfile"
WORKSPACE: str = "/env"


@dataclass(frozen=True)
class DockerEnvironment:
    """On-disk metadata representing a local Bertrand environment.  Specific care is
    taken not to store anything that references the host filesystem, in order to allow
    renaming/relocation of the environment directory.
    """
    version: int
    env_id: str  # UUID used to derive the container name ('bertrand-{env_id}')
    dockerfile_digest: str  # SHA256 digest of the Dockerfile to detect changes
    created: str  # ISO timestamp
    image: str  # e.g. "ubuntu:24.04"
    shell: list[str]  # shell command to execute during `bertrand enter`
    docker_build_args: list[str]  # additional args to pass to `docker build`


def _bertrand_dir(env_root: Path) -> Path:
    return env_root / BERTRAND_DIR


def _env_file(env_root: Path) -> Path:
    return _bertrand_dir(env_root) / ENV_FILE


def _docker_file(env_root: Path) -> Path:
    return env_root / DOCKER_FILE


def _image_tag(spec: DockerEnvironment) -> str:
    return f"bertrand-env:{spec.env_id}"


def _docker_digest(path: Path, build_args: list[str]) -> str:
    h = hashlib.sha256()
    h.update(path.read_bytes())
    h.update(b"\0")
    h.update("\n".join(build_args).encode("utf-8", "surrogateescape"))
    return h.hexdigest()


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


def _ensure_dockerfile(env_root: Path, spec: DockerEnvironment) -> None:
    path = _docker_file(env_root)
    if not path.exists():
        atomic_write_text(path, rf"""FROM {spec.image}

# you can extend this file in order to create a reproducible image that others can pull
# from using '$ bertrand init --from=<your_project>'.  For example:

# RUN pip install .

# This would compile the contents of the local environment directory and install it
# into the base image as an executable binary, Python package, and/or importable C++
# module.  When downstream users pull your project as a base image, they will get a
# clean environment directory with your additions pre-installed.

# See the official DockerFile documentation for a comprehensive reference of all the
# commands that can be used here, which Bertrand does not alter in any way.
""")


def _write_environment(env_root: Path, spec: DockerEnvironment) -> None:
    env_root.mkdir(parents=True, exist_ok=True)
    atomic_write_text(
        _env_file(env_root),
        json.dumps(asdict(spec), indent=2) + "\n"
    )
    _ensure_dockerfile(env_root, spec)


def _read_environment(env_root: Path) -> DockerEnvironment | None:
    meta = _env_file(env_root)
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

        # validate environment id
        env_id = data.get("env_id")
        if not isinstance(env_id, str) or not env_id.strip():
            raise ValueError(f"missing or invalid 'env_id' field: {env_id}")
        try:
            uuid.UUID(env_id)
        except Exception as err:
            raise ValueError(f"env_id must be a valid UUID string: {env_id}") from err

        # validate dockerfile digest
        dockerfile_digest = data.get("dockerfile_digest")
        if not isinstance(dockerfile_digest, str):
            raise ValueError(
                f"missing or invalid 'dockerfile_digest' field: {dockerfile_digest}"
            )
        if not all(c in "0123456789abcdef" for c in dockerfile_digest.lower()):
            raise ValueError(
                f"dockerfile_digest must be a valid SHA256 hex digest: {dockerfile_digest}"
            )

        # validate created timestamp
        created = data.get("created")
        if not isinstance(created, str) or not created.strip():
            raise ValueError(f"missing or invalid 'created' field: {created}")
        try:
            datetime.fromisoformat(created)
        except Exception as err:
            raise ValueError(f"created must be a valid ISO timestamp: {created}") from err

        # validate image
        image = data.get("image")
        if not isinstance(image, str) or not image.strip():
            raise ValueError(f"missing or invalid 'image' field: {image}")

        # validate shell command
        shell = data.get("shell")
        if not isinstance(shell, (str, list)) or not shell:
            raise ValueError("missing required field: shell")
        data["shell"] = _normalize_shell(shell)

        # validate docker build args
        docker_build_args = data.get("docker_build_args")
        if not isinstance(docker_build_args, list) or not all(
            isinstance(x, str) for x in docker_build_args
        ):
            raise ValueError(
                f"missing or invalid 'docker_build_args' field: {docker_build_args}"
            )

        return DockerEnvironment(**data)

    except Exception as err:
        raise ValueError(f"Invalid environment metadata at {meta}: {err}") from err


def _image_exists(tag: str) -> bool:
    try:
        docker_cmd(["image", "inspect", tag], capture_output=True)
        return True
    except CommandError:
        return False


def _pull_image(image: str) -> None:
    try:
        docker_cmd(["image", "inspect", image], capture_output=True)
    except CommandError:
        docker_cmd(["pull", image])


def _ensure_image_built(env_root: Path, spec: DockerEnvironment) -> DockerEnvironment:
    env_root = env_root.expanduser().resolve()

    # make sure dockerfile is present
    _ensure_dockerfile(env_root, spec)
    dockerfile = _docker_file(env_root)
    digest = _docker_digest(dockerfile, spec.docker_build_args)
    tag = _image_tag(spec)

    needs_build = not _image_exists(tag) or spec.dockerfile_digest != digest
    if needs_build:
        _pull_image(spec.image)
        docker_cmd([
            "build",
            *spec.docker_build_args,
            "-t", tag,
            "-f", str(dockerfile),
            "--label", f"bertrand.env_id={spec.env_id}",
            str(env_root),
        ])

    # persist digest in metadata
    if spec.dockerfile_digest != digest:
        spec2 = replace(spec, dockerfile_digest=digest)
        _write_environment(env_root, spec2)
        return spec2

    return spec


def _container_inspect(name: str) -> ContainerInspect | None:
    try:
        result = docker_cmd(["container", "inspect", name], capture_output=True)
        data = json.loads(result.stdout)
        return data[0] if data else None
    except (CommandError, json.JSONDecodeError, IndexError, TypeError):
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
    args = [
        "create",
        "--init",
        f"--name={container}",
        f"--hostname={_sanitize_environment_name(env_root.name)}",
        f"--workdir={WORKSPACE}",
        "--security-opt=no-new-privileges",
    ]

    ids = host_user_ids()
    if ids is not None:
        uid, gid = ids
        args.extend([
            "--user", f"{uid}:{gid}",
            "--cap-drop=ALL",
            "--cap-add=SYS_PTRACE",
        ])

    args.extend([
        "-v", f"{str(env_root)}:{WORKSPACE}",
        "-e", f"BERTRAND_ENV={spec.env_id}",
        _image_tag(spec),
        "sleep", "infinity",
    ])
    return args


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
        spec = _ensure_image_built(env_root, spec)
        docker_cmd(_container_build_args(env_root, spec))
        info = _container_inspect(container)

    running = bool(((info or {}).get("State") or {}).get("Running"))
    if not running:
        docker_cmd(["start", container])

    return container


def _copy_editor_hooks(env_root: Path) -> None:
    env_root = env_root.expanduser().resolve()
    spec = _read_environment(env_root)
    if spec is None:
        return

    container = _container_name(spec)
    info = _container_inspect(container)
    if info is None:
        # environment container not created yet (or deleted); Nothing to copy.
        return

    # ensure container is running (docker exec requires it)
    running = bool(((info.get("State") or {}).get("Running")))
    if not running:
        docker_cmd(["start", container], check=False, capture_output=True)

    # copy templates into bind mount (/env) without overwriting existing files.
    # We run inside the container as host UID/GID so created files are owned correctly.
    ids = host_user_ids()
    if ids is None:
        uid, gid = (0, 0)
    else:
        uid, gid = ids

    script = r"""
set -euo pipefail

SRC="/opt/bertrand/templates/devcontainer"
DST="/env"

# nothing to do if templates are missing in the image
if [ ! -d "$SRC" ]; then
  exit 0
fi

# Create target dirs
install -d "$DST/.devcontainer" "$DST/.vscode"

copy_if_missing() {
  local rel="$1"
  local mode="$2"
  if [ ! -e "$DST/$rel" ]; then
    install -m "$mode" "$SRC/$rel" "$DST/$rel"
  fi
}

copy_if_missing ".devcontainer/devcontainer.json" "0644"
copy_if_missing ".devcontainer/postCreate.sh" "0755"
copy_if_missing ".vscode/tasks.json" "0644"
copy_if_missing ".vscode/settings.json" "0644"
""".strip()

    docker_cmd(
        ["exec", "-u", f"{uid}:{gid}", container, "/bin/sh", "-lc", script],
        capture_output=True,
        check=False,  # best-effort; we patch below even if copy partially fails
    )

    # Patch the devcontainer.json to reference the *actual* image tag for this env.
    # We only patch if the file exists and parses as JSON.
    devcontainer_path = env_root / ".devcontainer" / "devcontainer.json"
    if devcontainer_path.exists():
        try:
            data = json.loads(devcontainer_path.read_text(encoding="utf-8"))
            if isinstance(data, dict):
                desired = _image_tag(spec)  # e.g. bertrand-env:<uuid>
                if data.get("image") != desired:
                    data["image"] = desired
                    atomic_write_text(devcontainer_path, json.dumps(data, indent=2) + "\n")
        except json.JSONDecodeError:
            # if user edited it into invalid JSON, do not clobber their file
            pass


def create_environment(
    env_root: Path,
    *,
    image: str,
    swap: int,
    shell: str | list[str],
    docker_build_args: list[str]
) -> DockerEnvironment:
    """Create (or load) a local Bertrand Docker environment at the given path.

    Parameters
    ----------
    env_root : Path
        The path at which to create the environment directory.
    image : str
        The Docker image to use for the environment.
    swap : int
        The amount of swap space (in GiB) to allocate during the container build.
    shell : str | list[str]
        The shell command to execute when entering the environment
    docker_build_args : list[str]
        Additional arguments to pass to `docker build` when building the environment
        image.

    Returns
    -------
    DockerEnvironment
        The created or loaded environment specification.
    """
    env_root = env_root.expanduser().resolve()
    env_root.mkdir(parents=True, exist_ok=True)

    # create swap memory for large builds
    swapfile = env_root / "swapfile"
    sudo = sudo_prefix()
    if swap:
        run([*sudo, "fallocate", "-l", f"{swap}G", str(swapfile)])
        run([*sudo, "chmod", "600", str(swapfile)])
        run([*sudo, "mkswap", str(swapfile)])
        run([*sudo, "swapon", str(swapfile)])

    try:
        # check for existing environment
        spec = _read_environment(env_root)
        if spec is None:
            spec = DockerEnvironment(
                version=1,
                env_id=str(uuid.uuid4()),
                dockerfile_digest="",  # filled in during image build
                created=datetime.now(timezone.utc).isoformat(),
                image=image,
                shell=_normalize_shell(shell),
                docker_build_args=docker_build_args,
            )
            _write_environment(env_root, spec)

        # ensure image and container are built
        spec = _ensure_image_built(env_root, spec)
        _ensure_container(env_root, spec)
        _copy_editor_hooks(env_root)
        return spec

    # clear swap memory
    finally:
        if swapfile.exists():
            print("Cleaning up swap file...")
            run([*sudo, "swapoff", str(swapfile)], check=False)
            swapfile.unlink(missing_ok=True)


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
    CommandError
        If any docker command fails.
    """
    env_root = env_root.expanduser().resolve()
    spec = _read_environment(env_root)
    if spec is None:
        raise ValueError(
            f"No docker environment found at: {env_root} (missing .bertrand/env.json)"
        )

    spec = _ensure_image_built(env_root, spec)
    _ensure_container(env_root, spec)
    docker_exec(["exec", "-it", "-w", WORKSPACE])


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
        If no .bertrand/env.json file is found in any parent directory, indicating that
        `start` does not lie within a Bertrand environment.
    """
    start = start.expanduser().resolve()
    if start.is_file():
        start = start.parent

    for p in (start, *start.parents):
        if _env_file(p).exists():
            return p

    raise FileNotFoundError(
        f"No .bertrand/env.json found in any parent directory starting from {start}"
    )


def monitor_environment(env_root: Path) -> None:
    """Print the environment's top processes and their resource utilization to the
    command line.

    Parameters
    ----------
    env_root : Path
        The path to the environment directory.

    Raises
    ------
    FileNotFoundError
        If no environment is found at the given path.
    CommandError
        If the `docker top` command fails.
    """
    env_root = env_root.expanduser().resolve()
    spec = _read_environment(env_root)
    if spec is None:
        raise FileNotFoundError(
            f"No docker environment found at: {env_root} (missing .bertrand/env.json)"
        )
    docker_cmd(["top", _container_name(spec)])


def start_environment(env_root: Path) -> None:
    """Start an environment container, launching all necessary processes within it.

    Parameters
    ----------
    env_root : Path
        The path to the environment directory.

    Raises
    ------
    FileNotFoundError
        If no environment is found at the given path.
    CommandError
        If any docker command fails.
    """
    env_root = env_root.expanduser().resolve()
    spec = _read_environment(env_root)
    if spec is None:
        raise FileNotFoundError(
            f"No docker environment found at: {env_root} (missing .bertrand/env.json)"
        )

    _ensure_image_built(env_root, spec)
    _ensure_container(env_root, spec)
    _copy_editor_hooks(env_root)
    docker_cmd(["start", _container_name(spec)], check=False, capture_output=True)


def stop_environment(env_root: Path, *, force: bool) -> None:
    """Stop an environment container, terminating all running processes within it.

    Parameters
    ----------
    env_root : Path
        The path to the environment directory.
    force : bool
        If True, forcibly stop the docker container without waiting.

    Raises
    ------
    FileNotFoundError
        If no environment is found at the given path.
    CommandError
        If any docker command fails.
    """
    env_root = env_root.expanduser().resolve()
    spec = _read_environment(env_root)
    if spec is None:
        raise FileNotFoundError(
            f"No docker environment found at: {env_root} (missing .bertrand/env.json)"
        )

    container = f"bertrand-{spec.env_id}"
    timeout = "0" if force else "10"
    docker_cmd(["stop", "-t", timeout, container], check=False, capture_output=True)


def pause_environment(env_root: Path) -> None:
    """Pause an environment container, suspending all running processes within it.

    Parameters
    ----------
    env_root : Path
        The path to the environment directory.

    Raises
    ------
    FileNotFoundError
        If no environment is found at the given path.
    CommandError
        If any docker command fails.
    """
    env_root = env_root.expanduser().resolve()
    spec = _read_environment(env_root)
    if spec is None:
        raise FileNotFoundError(
            f"No docker environment found at: {env_root} (missing .bertrand/env.json)"
        )

    container = f"bertrand-{spec.env_id}"
    docker_cmd(["pause", container], check=False, capture_output=True)


def resume_environment(env_root: Path) -> None:
    """Resume a paused environment container, restarting all suspended processes
    within it.

    Parameters
    ----------
    env_root : Path
        The path to the environment directory.

    Raises
    ------
    FileNotFoundError
        If no environment is found at the given path.
    CommandError
        If any docker command fails.
    """
    env_root = env_root.expanduser().resolve()
    spec = _read_environment(env_root)
    if spec is None:
        raise FileNotFoundError(
            f"No docker environment found at: {env_root} (missing .bertrand/env.json)"
        )

    container = f"bertrand-{spec.env_id}"
    docker_cmd(["unpause", container], check=False, capture_output=True)


def delete_environment(
    env_root: Path,
    *,
    assume_yes: bool,
    force: bool,
    remove: bool
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
    remove : bool
        If True, also delete all files in the environment directory.  If False,
        only the docker container and image are removed, leaving the environment
        directory intact.

    Raises
    ------
    RuntimeError
        If called from inside a Bertrand Docker environment.
    ValueError
        If no environment is found at the given path, or if deletion fails.
    """
    env_root = env_root.expanduser().resolve()
    spec = _read_environment(env_root)
    if spec is None:
        raise ValueError(
            f"No docker environment found at: {env_root} (missing .bertrand/env.json)"
        )

    container = f"bertrand-{spec.env_id}"
    if remove:
        prompt = (
            f"This will permanently delete the environment at:\n  {env_root}\n"
            "Nothing will survive this operation.\n"
            "Proceed? [y/N] "
        )
    else:
        prompt = (
            f"This will delete the docker container for the environment at:\n  {container}\n"
            "The environment directory will be preserved, along with its contents.\n"
            "Proceed? [y/N] "
        )
    if not confirm(prompt, assume_yes=assume_yes):
        raise ValueError("Environment deletion declined by user.")

    # remove container + built image (best-effort)
    _remove_container(container, force=force)
    docker_cmd(["image", "rm", "-f", _image_tag(spec)], check=False, capture_output=True)

    # remove environment directory
    if remove:
        try:
            shutil.rmtree(env_root)
        except OSError as err:
            raise ValueError(f"Failed to remove environment directory: {env_root}\n{err}") from err
