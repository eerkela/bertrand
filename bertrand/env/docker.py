"""Install Docker Engine and pull container images."""
import json
import hashlib
import os
import shlex
import uuid

from dataclasses import asdict, dataclass, replace
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable, List, Literal, TypedDict

from .docker_engine import docker_cmd, docker_exec
from .run import CommandError, atomic_write_text

#pylint: disable=redefined-builtin, global-statement


MOUNT: str = "/env"
LABEL: str = "bertrand"


def _bertrand_dir(env_root: Path) -> Path:
    return env_root / ".bertrand"


def _env_file(env_root: Path) -> Path:
    return _bertrand_dir(env_root) / "env.json"


def _tag_dir(env_root: Path) -> Path:
    return _bertrand_dir(env_root) / "tag"


def _tag_file(env_root: Path, digest: str) -> Path:
    return _tag_dir(env_root) / f"{digest}.json"


def _ipc_dir(env_root: Path) -> Path:
    return _bertrand_dir(env_root) / "ipc"


def _ipc_requests(env_root: Path) -> Path:
    return _ipc_dir(env_root) / "requests"


def _ipc_processing(env_root: Path) -> Path:
    return _ipc_dir(env_root) / "processing"


def _ipc_done(env_root: Path) -> Path:
    return _ipc_dir(env_root) / "done"


def _ipc_failed(env_root: Path) -> Path:
    return _ipc_dir(env_root) / "failed"


def _docker_file(env_root: Path) -> Path:
    return env_root / "Dockerfile"


def _docker_ignore(env_root: Path) -> Path:
    return env_root / ".dockerignore"


def _normalize_shell(value: object) -> list[str]:
    if isinstance(value, list) and all(isinstance(x, str) and x for x in value):
        out = []
        for s in value:
            out.extend(shlex.split(s))
        return out
    if isinstance(value, str):
        argv = shlex.split(value.strip())
        if argv:
            return argv
        raise ValueError("shell must not be empty")
    raise ValueError("shell must be a string or list of strings")


# TODO: the environment file may need to be protected by a lock for proper
# synchronization


@dataclass(frozen=True)
class DockerEnvironment:
    """On-disk metadata representing environment-level data structures, which map from
    human-readable tags to build argument hashes, and then to unique image digests,
    which represent individual Docker containers.
    """
    root: Path  # absolute path to env root on host system, for reference when inside the container
    tags: dict[str, list[str]]  # human-readable tag -> parsed argv
    builds: dict[str, str]  # argv hash -> digest
    ids: dict[str, str]  # container id -> digest
    shell: list[str]  # shell command to execute during `bertrand enter`
    code: list[str]  # default host command invoked by `code` within the container


def _write_environment(env_root: Path, env: DockerEnvironment) -> None:
    env_root.mkdir(parents=True, exist_ok=True)
    atomic_write_text(
        _env_file(env_root),
        json.dumps(asdict(env), indent=2) + "\n"
    )


def _read_environment(env_root: Path) -> DockerEnvironment | None:
    env_file = _env_file(env_root)
    if not env_file.exists():
        return None

    try:
        data = json.loads(env_file.read_text(encoding="utf-8"))
        if not isinstance(data, dict):
            raise ValueError("environment metadata must be a JSON mapping")

        # validate root
        root = data.get("root")
        if not isinstance(root, str) or not root.strip():
            raise ValueError(f"missing or invalid 'root' field: {root}")
        data["root"] = Path(root).expanduser().resolve()

        # validate tags
        tags = data.get("tags")
        if not isinstance(tags, dict) or not all(
            isinstance(k, str) and
            isinstance(v, list) and
            all(isinstance(x, str) for x in v) for k, v in tags.items()
        ):
            raise ValueError(f"missing or invalid 'tags' field: {tags}")

        # validate builds
        builds = data.get("builds")
        if not isinstance(builds, dict) or not all(
            isinstance(k, str) and isinstance(v, str) for k, v in builds.items()
        ):
            raise ValueError(f"missing or invalid 'builds' field: {builds}")

        # validate ids
        ids = data.get("ids")
        if not isinstance(ids, dict) or not all(
            isinstance(k, str) and isinstance(v, str) for k, v in ids.items()
        ):
            raise ValueError(f"missing or invalid 'ids' field: {ids}")

        # validate + normalize shell command
        shell = data.get("shell")
        if not isinstance(shell, (str, list)) or not shell:
            raise ValueError("missing required field: shell")
        data["shell"] = _normalize_shell(shell)

        # validate + normalize code command
        code = data.get("code")
        if not isinstance(code, (str, list)) or not code:
            raise ValueError("missing required field: code")
        data["code"] = _normalize_shell(code)

        return DockerEnvironment(
            root=data["root"],
            tags=tags,
            builds=builds,
            ids=ids,
            shell=data["shell"],
            code=data["code"],
        )

    except Exception as err:
        raise ValueError(f"Invalid environment metadata at {env_file}: {err}") from err


def init_environment(
    env_root: Path,
    *,
    shell: str | list[str],
    code: str | list[str],
) -> DockerEnvironment:
    """Initialize or load an environment directory at the given path.  Note that this
    does not create any Docker images or containers; those are created when entering or
    running the environment.

    Parameters
    ----------
    env_root : Path
        The path at which to create the environment directory.
    shell : str | list[str]
        The shell command to execute when entering the environment
    code : str | list[str]
        The default host command invoked by `bertrand code` within the container.

    Returns
    -------
    DockerEnvironment
        The created or loaded environment specification.

    Raises
    ------
    OSError
        If the environment directory could not be created.
    JSONDecodeError
        If the environment metadata is malformed.
    UnicodeDecodeError
        If the environment metadata cannot be decoded.
    """
    env_root = env_root.expanduser().resolve()
    env_root.mkdir(parents=True, exist_ok=True)

    # check for existing environment
    env = _read_environment(env_root)

    # if no environment exists, create a new one
    if env is None:
        env = DockerEnvironment(
            root=env_root,
            tags={},
            builds={},
            ids={},
            shell=_normalize_shell(shell),
            code=_normalize_shell(code),
        )

        # init Dockerfile
        docker_file = _docker_file(env_root)
        if not docker_file.exists():
            docker_file.parent.mkdir(parents=True, exist_ok=True)
            atomic_write_text(docker_file, r"""# Base image for Bertrand Docker environment
FROM bertrand:latest

# you can extend this file in order to create a reproducible image that others can pull
# from in their own Dockerfiles.  For example:

RUN pip install .

# `pip install .` will compile the contents of the local environment directory and
# install it into the base image as an executable binary, Python package, and/or
# C++ module.  If you then upload this image to a Docker repository, downstream users
# will be able to use `FROM <your-image>` in their own Dockerfiles to inherit all
# of your built artifacts and dependencies without needing to recompile them.

# See the official DockerFile documentation for a comprehensive reference of all the
# commands that can be used here, which Bertrand does not change in any way.
""")

        # init .dockerignore
        docker_ignore = _docker_ignore(env_root)
        if not docker_ignore.exists():
            docker_ignore.parent.mkdir(parents=True, exist_ok=True)
            atomic_write_text(docker_ignore, "")

        # TODO: init IPC directories?  Or do this lazily when needed?

        # init .bertrand/env.json
        _write_environment(env_root, env)

    return env


def parse_environment_tag(arg: str) -> tuple[Path, str]:
    """Parse a string of the form `<env_root>:<tag>` into its components.

    Parameters
    ----------
    arg : str
        The environment path string to parse.

    Returns
    -------
    Path
        The environment root path, expanded and resolved into an absolute path.
    str
        The environment tag or an empty string if no tag was provided.  This consists
        of the portion of the input string after the last colon, as long as it does not
        contain any path separators.

    Raises
    ------
    OSError
        If the environment path could not be resolved, or the tag is empty or contains
        invalid characters.
    """
    prev, sep, tag = arg.rpartition(":")
    if not sep or os.path.sep in tag:
        return Path(arg.strip()).expanduser().resolve(), ""
    tag = tag.strip()
    if not tag:
        raise OSError("environment tag must not be empty")
    sanitized = _sanitize_container_name(tag)
    if tag != sanitized:
        raise OSError(
            f"environment tag contains invalid characters: '{tag}' (sanitizes to: '{sanitized}')"
        )
    return Path(prev.strip()).expanduser().resolve(), sanitized


@dataclass(frozen=True)
class DockerContainer:
    """On-disk metadata representing a local Bertrand Docker container, which is a
    built image of an encapsulating environment.  An environment can have many
    containers, each built with a different set of Dockerfile arguments.

    Specific care is taken not to store anything that references the host filesystem or
    container name, in order to allow renaming/relocation of the environment directory.
    """
    version: int  # version number for backwards compatibility
    argv: list[str]  # Docker build arguments used to create this container
    digest: str  # SHA256 of Dockerfile content + args to detect incremental rebuilds
    image: str  # UUID tag for the built image
    container: str  # Unique container ID linking this environment back to its Docker host
    created: str  # ISO timestamp


def _image_name(container: DockerContainer) -> str:
    return f"bertrand-{container.image}"


def _normalize_argv(argv: Iterable[str]) -> list[str]:
    out: list[str] = []
    for s in argv:
        if any(c.isspace() for c in s):
            out.extend(shlex.split(s))
        else:
            out.append(s)
    return out


def _arg_bytes(argv: Iterable[str]) -> bytes:
    out = bytearray()
    for s in argv:
        out.extend(len(s).to_bytes(8, "big"))  # length prefix to avoid ambiguity
        out.extend(s.encode("utf-8", "surrogateescape"))
    return bytes(out)


def _arg_hash(argv: Iterable[str]) -> str:
    h = hashlib.sha256()
    h.update(_arg_bytes(argv))
    return h.hexdigest()


def _docker_digest(env_root: Path, argv: Iterable[str]) -> str:
    h = hashlib.sha256()
    h.update(_arg_bytes(argv))
    h.update(b"\0")
    h.update(_docker_file(env_root).read_bytes())
    return h.hexdigest()


def _sanitize_container_name(name: str) -> str:
    out = []
    for char in name:
        if char.isalnum() or char in "._":
            out.append(char)
        else:
            out.append("_")
    return "".join(out).strip("_")


def _container_name(env_root: Path, tag: str, container: DockerContainer) -> str:
    # e.g. <myproject>.<tag>.<uuid> or <myproject>.<uuid>
    env_root = env_root.expanduser().resolve()
    parts = [_sanitize_container_name(env_root.name)]
    tag = tag.strip()
    if tag:
        parts.append(_sanitize_container_name(tag))
    parts.append(container.image)
    return ".".join(parts)


def _write_container(env_root: Path, container: DockerContainer) -> None:
    tag_dir = _tag_dir(env_root)
    tag_dir.mkdir(parents=True, exist_ok=True)
    atomic_write_text(
        tag_dir / f"{container.digest}.json",
        json.dumps(asdict(container), indent=2) + "\n"
    )


def _read_container(tag_file: Path) -> DockerContainer | None:
    if not tag_file.exists():
        return None

    try:
        data = json.loads(tag_file.read_text(encoding="utf-8"))
        if not isinstance(data, dict):
            raise ValueError("environment metadata must be a JSON object")

        # validate version
        version = data.get("version")
        if not isinstance(version, int) or version <= 0:
            raise ValueError(f"missing or invalid 'version' field: {version}")

        # validate + normalize argv
        argv = data.get("argv")
        if not isinstance(argv, list) or not all(
            isinstance(x, str) and x for x in argv
        ):
            raise ValueError(f"missing or invalid 'argv' field: {argv}")
        data["argv"] = _normalize_argv(argv)

        # validate digest
        container_digest = data.get("digest")
        if not isinstance(container_digest, str) or not container_digest.strip():
            raise ValueError(f"missing or invalid 'digest' field: {container_digest}")
        if container_digest != tag_file.name:
            raise ValueError(
                f"digest field does not match filename: {container_digest} != {tag_file.name}"
            )

        # validate image id
        image = data.get("image")
        if not isinstance(image, str) or not image.strip():
            raise ValueError(f"missing or invalid 'image' field: {image}")
        if not uuid.UUID(image):
            raise ValueError(f"image must be a valid UUID: {image}")

        # validate container id
        container = data.get("container")
        if not isinstance(container, str) or not container.strip():
            raise ValueError(f"missing or invalid 'container' field: {container}")
        check = docker_cmd(["container", "inspect", container], check=False, capture_output=True)
        if check.returncode != 0:
            raise ValueError(f"container id not recognized: {container}")

        # validate created timestamp
        created = data.get("created")
        if not isinstance(created, str) or not created.strip():
            raise ValueError(f"missing or invalid 'created' field: {created}")
        try:
            datetime.fromisoformat(created)
        except Exception as err:
            raise ValueError(f"created must be a valid ISO timestamp: {created}") from err

        return DockerContainer(**data)

    except Exception as err:
        raise ValueError(f"Invalid environment metadata at {tag_file}: {err}") from err


class MountInfo(TypedDict, total=False):
    """Type hint for docker container mount information."""
    Type: Literal["bind", "volume", "tmpfs", "npipe"]
    Destination: str
    Source: str


class ContainerState(TypedDict, total=False):
    """Type hint for docker container state information."""
    Running: bool
    Paused: bool
    Restarting: bool
    Dead: bool


class ContainerInspect(TypedDict, total=False):
    """Type hint for docker container inspect output."""
    Id: str
    Created: str
    Mounts: List[MountInfo]
    State: ContainerState


def _inspect_container(name_or_id: str) -> ContainerInspect | None:
    result = docker_cmd(["inspect", name_or_id], check=False, capture_output=True)
    if result.returncode != 0 or not result.stdout:
        return None
    data = json.loads(result.stdout)
    return data[0] if data else None


def _get_mount_source(inspect: ContainerInspect) -> Path | None:
    mounts = inspect.get("Mounts") or []
    for m in mounts:
        if m.get("Type") == "bind" and m.get("Destination") == MOUNT:
            src = m.get("Source")
            if src:
                return Path(src).expanduser()
    return None


def _start_container(inspect: ContainerInspect) -> None:
    running = bool(((inspect.get("State") or {}).get("Running")))
    if not running:
        docker_cmd(["start", inspect["Id"]])


def _stop_container(inspect: ContainerInspect) -> None:
    running = bool(((inspect.get("State") or {}).get("Running")))
    if running:
        docker_cmd(["stop", inspect["Id"]])


def _remove_container(container: DockerContainer, *, force: bool) -> None:
    if force:
        docker_cmd(["image", "rm", "-f", _image_name(container)], check=False, capture_output=True)
        docker_cmd(["container", "rm", "-f", container.container], check=False, capture_output=True)
    else:
        docker_cmd(["image", "rm", _image_name(container)], check=False, capture_output=True)
        docker_cmd(["container", "rm", container.container], check=False, capture_output=True)


def _search_container(
    env_root: Path,
    *,
    env: DockerEnvironment,
    arg_hash: str,
) -> tuple[DockerContainer | None, ContainerInspect | None]:
    # search environment metadata
    search = env.builds.get(arg_hash)
    if search is None:
        return None, None

    # find container metadata
    destination = _tag_file(env_root, search)
    if not destination.exists():
        env = replace(
            env,
            builds={k: v for k, v in env.builds.items() if k != arg_hash},
            ids={k: v for k, v in env.ids.items() if v != search}
        )
        _write_environment(env_root, env)
        return None, None

    # load metadata
    container = _read_container(destination)
    if container is None:
        destination.unlink(missing_ok=True)
        env = replace(
            env,
            builds={k: v for k, v in env.builds.items() if k != arg_hash},
            ids={k: v for k, v in env.ids.items() if v != search}
        )
        _write_environment(env_root, env)
        return None, None

    # verify container exists
    inspect = _inspect_container(container.container)
    if inspect is None:
        destination.unlink(missing_ok=True)
        env = replace(
            env,
            builds={k: v for k, v in env.builds.items() if k != arg_hash},
            ids={k: v for k, v in env.ids.items() if v != search}
        )
        _write_environment(env_root, env)
        return None, None

    return container, inspect


def _load_container(
    env_root: Path,
    *,
    env: DockerEnvironment,
    arg_hash: str,
    digest: str
) -> tuple[DockerContainer | None, ContainerInspect | None]:
    container, inspect = _search_container(env_root, env=env, arg_hash=arg_hash)
    if container is None or inspect is None:
        return None, None

    # check for incremental rebuild
    destination = _tag_file(env_root, container.digest)
    if container.digest != digest:
        destination.unlink(missing_ok=True)
        env = replace(
            env,
            builds={k: v for k, v in env.builds.items() if k != arg_hash},
            ids={k: v for k, v in env.ids.items() if v != container.digest}
        )
        _write_environment(env_root, env)
        return None, None

    # if the environment directory has moved, an existing container might have a
    # compatible digest, but the bind mount may be stale.  Docker does not support
    # editing mounts in-place, but we can stop, rm, and recreate the container if
    # needed.  Note that this will remove any data that is not stored in the
    # environment directory (i.e., in the container's root filesystem), but those can
    # be recovered by rebuilding the container in reproducible fashion.
    mount = _get_mount_source(inspect)
    if mount is not None:
        try:
            mount = mount.resolve()
            if mount != env_root:  # relocated
                mount = None
        except OSError:  # unable to resolve for some reason
            mount = None
    if mount is None:
        _remove_container(container, force=True)
        destination.unlink(missing_ok=True)
        env = replace(
            env,
            builds={k: v for k, v in env.builds.items() if k != arg_hash},
            ids={k: v for k, v in env.ids.items() if v != digest}
        )
        _write_environment(env_root, env)
        return None, None

    # container is valid, up to date, and mounted correctly
    return container, inspect


def _ensure_container(
    env_root: Path,
    tag: str,
    argv: list[str],
    *,
    env: DockerEnvironment,
    arg_hash: str,
    digest: str,
) -> tuple[DockerContainer, ContainerInspect]:
    # search for existing container
    container, inspect = _load_container(env_root, env=env, arg_hash=arg_hash, digest=digest)

    # if no valid container could be loaded, create a new one
    if container is None or inspect is None:
        container = DockerContainer(
            version=1,
            argv=argv,
            digest=digest,
            image=str(uuid.uuid4()),
            container="",  # filled in after creation
            created=datetime.now(timezone.utc).isoformat(),  # corrected after creation
        )

        # check for base image and build if missing
        image_name = _image_name(container)
        image_info = docker_cmd(["image", "inspect", image_name], check=False, capture_output=True)
        if image_info.returncode != 0:
            docker_cmd([
                "build",
                *argv,
                "-t", image_name,
                "-f", str(_docker_file(env_root)),
                "--label", f"{LABEL}=1",
                str(env_root),
            ])

        # TODO: all device, ports, resource limits, etc. are set in `docker create`
        # here.

        # create container from image
        container_name = _container_name(env_root, tag, container)  # human-readable + unambiguous
        docker_cmd([
            "create",
            "--init",
            f"--name={container_name}",
            f"--hostname={container_name}",
            "--label", f"{LABEL}=1",
            "-v", f"{str(env_root)}:{MOUNT}",
            "-e", f"BERTRAND_ENV={container.digest}",
            image_name,
            "sleep", "infinity",
        ])
        inspect = _inspect_container(container_name)
        if inspect is None:
            raise CommandError(
                returncode=1,
                cmd=["docker", "inspect", container_name],
                stdout="",
                stderr=f"Failed to create container: {container_name}"
            )
        container = replace(
            container,
            container=inspect["Id"],
            created=inspect["Created"],
        )
        _write_container(env_root, container)

    # update environment search structures
    env = replace(
        env,
        tags=(env.tags | {tag: argv}) if tag else env.tags,
        builds=env.builds | {arg_hash: container.digest},
        ids=env.ids | {container.container: container.digest},
    )
    _write_environment(env_root, env)
    return container, inspect


def start_container(env_root: Path, tag: str, *, argv: list[str]) -> DockerContainer:
    """Start an existing container with the given tag or build arguments, or create a
    new one by running the user's Dockerfile with the specified build arguments.

    Parameters
    ----------
    env_root : Path
        A path to the root environment directory.
    tag : str
        An optional, human-readable tag to assign to the container.  If `argv` is empty
        and this tag is not, then it will be searched in the environment metadata in
        order to replace `argv`.  Otherwise, `argv` will be used directly, and the tag
        will be associated with them, making the container accessible via
        `<env_root>:<tag>` in the future.
    argv : list[str]
        An arbitrary number of command-line arguments to pass to the Dockerfile build
        process.  These must match the expected `ARG` directives in the environment's
        Dockerfile.

    Returns
    -------
    DockerContainer
        The created or loaded container metadata.

    Raises
    ------
    FileNotFoundError
        If the environment metadata could not be found at the given path.
    KeyError
        If a tag was provided without arguments, but the tag could not be found in the
        environment metadata.
    CommandError
        If a Docker command fails or the container could not be created.
    JSONDecodeError
        If the environment or container metadata is malformed.
    UnicodeDecodeError
        If the environment or container metadata cannot be decoded.
    """
    # load environment
    env_root = env_root.expanduser().resolve()
    env = _read_environment(env_root)
    if env is None:
        raise FileNotFoundError(f"Failed to read environment metadata at: {env_root}")

    # resolve tag or normalize argv
    argv = _normalize_argv(argv)
    if tag and not argv:
        argv = env.tags.get(tag, [])
        if not argv:
            raise KeyError(f"Environment tag not found: {tag}")

    # load or create container
    arg_hash = _arg_hash(argv)
    digest = _docker_digest(env_root, argv)
    container, inspect = _ensure_container(
        env_root,
        tag,
        argv,
        env=env,
        arg_hash=arg_hash,
        digest=digest
    )

    # start container if not already running
    _start_container(inspect)
    return container


def enter_container(env_root: Path, tag: str, *, argv: list[str]) -> DockerContainer:
    """Start an existing container with the given tag or build arguments, or create a
    new one by running the user's Dockerfile with the specified build arguments, and
    then replace the current process with a shell inside the container.

    Parameters
    ----------
    env_root : Path
        The path to the environment directory.
    tag : str
        An optional, human-readable tag to assign to the container.  If `argv` is empty
        and this tag is not, then it will be searched in the environment metadata in
        order to replace `argv`.  Otherwise, `argv` will be used directly, and the tag
        will be associated with them, making the container accessible via
        `<env_root>:<tag>` in the future.
    argv : list[str]
        An arbitrary number of command-line arguments to pass to the Dockerfile build
        process.  These must match the expected `ARG` directives in the environment's
        Dockerfile.

    Returns
    -------
    DockerContainer
        The created or loaded container metadata.

    Raises
    ------
    FileNotFoundError
        If no environment is found at the given path.
    KeyError
        If a tag was provided without arguments, but the tag could not be found in the
        environment metadata.
    CommandError
        If a Docker command fails or the container could not be created.
    JSONDecodeError
        If the environment or container metadata is malformed.
    UnicodeDecodeError
        If the environment or container metadata cannot be decoded.
    """
    # load environment
    env_root = env_root.expanduser().resolve()
    env = _read_environment(env_root)
    if env is None:
        raise FileNotFoundError(f"No environment found at: {env_root}")

    # resolve tag or normalize argv
    argv = _normalize_argv(argv)
    if tag and not argv:
        argv = env.tags.get(tag, [])
        if not argv:
            raise KeyError(f"Environment tag not found: {tag}")

    # load or create container
    arg_hash = _arg_hash(argv)
    digest = _docker_digest(env_root, argv)
    container, inspect = _ensure_container(
        env_root,
        tag,
        argv,
        env=env,
        arg_hash=arg_hash,
        digest=digest
    )

    # start container if not already running
    _start_container(inspect)

    # replace current process with container shell
    docker_exec([
        "exec",
        "-it",
        "-w", MOUNT,
        container.container,
        *env.shell
    ])
    return container


# TODO: `$ docker run` has a massive list of configuration options, in addition to
# the options passed to the entry point itself.  Either I should bake the run
# options into the container metadata, or I should come up with a scheme where you
# pass them like:

#   $ bertrand run [run-options] <env>:<tag> [entrypoint-args]

# but honestly that's pretty ugly.  There should be a better way to manage all the
# options more effectively, so it's more obvious which options go where.
# -> The only real way I can think to avoid this is to somehow either bake all
# these options into the container itself, so that they are also reflected within
# `$ bertrand enter`, or to have some sort of other command that just sets options
# for a future run/enter, which might be the same as `$ bertrand init <env>:<tag>`.
# The former is probably better, since it keeps everything self-contained, but it's
# hard to extend to `$ bertrand enter` because it uses `docker exec` instead of
# `docker run`.  The only way to really solve this cleanly is to make
# `$ bertrand enter` use `docker run` as well, which I'm not sure is totally
# possible.

# Really, this requires me to nail down the exact compilation pipeline, since that
# affects what information can be stored where.  If compilation equates to building
# a container, then all compilation options must be baked into the container
# definition, which may need to include information like the number of CPUs,
# amount of virtual memory to use, etc.  The only alternative is to somehow detect
# these options during the Dockerfile build process, which would allow me to omit them
# from the image metadata.  I'm not really going to know the answer to this until I
# start implementing the compilation system more fully, so it's kind of just broken
# for now.



# TODO: run_container should actually just run via docker exec


def run_container(env_root: Path, tag: str, *, argv: list[str]) -> DockerContainer:
    """Invoke a tagged container's entry point with the given command-line arguments.
    Note that the arguments here are passed to the container's entry point, not to the
    Docker build process.

    Parameters
    ----------
    env_root : Path
        The path to the environment directory.
    tag : str
        An optional, human-readable tag to assign to the container.  If `argv` is empty
        and this tag is not, then it will be searched in the environment metadata in
        order to replace `argv`.  Otherwise, `argv` will be used directly, and the tag
        will be associated with them, making the container accessible via
        `<env_root>:<tag>` in the future.
    argv : list[str]
        An arbitrary number of command-line arguments to pass to the environment's
        Dockerfile-defined entry point.

    Returns
    -------
    DockerContainer
        The created or loaded container metadata.

    Raises
    ------
    FileNotFoundError
        If no environment is found at the given path.
    KeyError
        If a tag was provided, but could not be found in the environment metadata.
    CommandError
        If a Docker command fails or the container could not be created.
    JSONDecodeError
        If the environment or container metadata is malformed.
    """
    # load environment
    env_root = env_root.expanduser().resolve()
    env = _read_environment(env_root)
    if env is None:
        raise FileNotFoundError(f"No environment found at: {env_root}")

    # resolve tag or use empty build arguments
    container_args = []
    if tag:
        container_args = env.tags.get(tag, [])
        if not container_args:
            raise KeyError(f"Environment tag not found: {tag}")

    # load or create container
    arg_hash = _arg_hash(container_args)
    digest = _docker_digest(env_root, container_args)
    container, inspect = _ensure_container(
        env_root,
        tag,
        container_args,
        env=env,
        arg_hash=arg_hash,
        digest=digest
    )

    # launch container entry point with normalized arguments
    argv = _normalize_argv(argv)
    docker_cmd([
        "run",
        _image_name(container),
        "-w", MOUNT,
        *argv
    ])
    return container


def in_container() -> bool:
    """Detect whether the current process is running inside a Bertrand Docker
    container.

    Returns
    -------
    bool
        True if running inside a Bertrand Docker container, false otherwise.
    """
    return bool(os.environ.get("BERTRAND_ENV"))


def list_containers(key: str | Path | None = None) -> list[str]:
    """List the ids of all Bertrand Docker containers that match a given id or name
    fragment, an environment root path, or the id of the current container if no key
    is given.

    Parameters
    ----------
    key : str | Path | None, optional
        An exact id or fragment of the container name to match against, or a path to
        a root environment directory.  If a name is given, then it may be a partial
        match for one or more containers.  If a path is given, then all containers
        associated with that environment directory are matched.  If None (the default),
        then the container id will be obtained for the active environment (if any) by
        inspecting the tag registry.

    Returns
    -------
    list[str]
        A list of matching container ids.  Empty if no matches are found, or possibly
        multiple matches if the key is ambiguous.
    """
    # if key is None, then detect the id of the active container, if any
    if key is None:
        digest = os.environ.get("BERTRAND_ENV")
        if digest is None:
            return []
        container = _read_container(_tag_file(Path("/env"), digest))
        if container is None:
            return []
        return [container.container]

    # otherwise, try an exact ID match first
    result = docker_cmd([
        "ps",
        "--filter", f"label={LABEL}=1",
        "--filter", f"id={key}",
    ], check=False, capture_output=True)
    if result.returncode == 0 and result.stdout and result.stdout.strip():
        return [str(key)]

    # fall back to partial name match
    result = docker_cmd([
        "ps",
        "--filter", f"label={LABEL}=1",
        "--filter", f"name={key}",
        "--format", "{{.ID}}"
    ], check=False, capture_output=True)
    if result.returncode == 0 and result.stdout and result.stdout.strip():
        return [line.split(None, 1)[0] for line in result.stdout.splitlines()]

    # interpret as environment path
    try:
        tag_dir = _tag_dir(Path(key))
        if not tag_dir.exists() or not tag_dir.is_dir():
            return []
    except OSError:
        return []

    # iterate over tag directory to extract container ids
    out: list[str] = []
    for tag_file in tag_dir.iterdir():
        container = _read_container(tag_file)
        if container is not None:
            out.append(container.container)
    return out


def find_environment(anchor: str | Path | None = None) -> Path | None:
    """Find the environment directory corresponding to a Bertrand Docker container id,
    name, or a subordinate path, assuming one exists.

    Parameters
    ----------
    anchor : str | Path | None
        The container id, name, or path to search from, or None.  If this is a string,
        then it will first be interpreted as an exact container id or name, and does
        not permit partial matches.  If a container is found, then its mount path will
        be obtained via `docker inspect`.  If no container is found, or if `anchor` is
        a Path, then the path will be searched upwards for a `.bertrand/env.json` file,
        which defines the root path.  If it is None (the default), then the container
        will be found using environment variables from the current process.

    Returns
    -------
    Path | None
        A resolved path to the environment directory, or `None` if no environment
        could be found.
    """
    # if None, access via bind mount and read stored path in env.json
    if anchor is None:
        if not in_container():
            return None
        env = _read_environment(Path("/env/"))
        if env is None:
            return None
        return Path(env.root)

    # try container id or name first
    if isinstance(anchor, str):
        container = _inspect_container(anchor)
        if container is not None:
            mount = _get_mount_source(container)
            if mount is not None:
                return mount
        try:
            anchor = Path(anchor)  # reinterpret as path
        except Exception:  # pylint: disable=broad-except
            return None

    # search upwards from path
    anchor = anchor.expanduser().resolve()
    if anchor.is_file():
        anchor = anchor.parent
    for p in (anchor, *anchor.parents):
        if _env_file(p).exists():
            return p
    return None


def container_activity(env_root: Path, tag: str, *, argv: list[str]) -> DockerContainer:
    """Print a container's top processes and their resource utilization to the
    command line.

    Parameters
    ----------
    env_root : Path
        The path to the environment directory.
    tag : str
        An optional, human-readable tag to look up in order to determine the proper
        arguments.  If `argv` is empty and this tag is not, then it will be searched in
        the environment metadata in order to replace `argv`.  Otherwise, `argv` will be
        used directly, and this tag must be empty.
    argv : list[str]
        An arbitrary number of command-line arguments to identify the container.
        These must match the expected `ARG` directives in the environment's Dockerfile.
        If `tag` is provided, then `argv` must be empty.

    Returns
    -------
    DockerContainer
        The container's metadata.

    Raises
    ------
    FileNotFoundError
        If no environment is found at the given path.
    KeyError
        If no container matches the given tag or build arguments.
    CommandError
        If any docker command fails, or if both `tag` and `argv` are provided at the
        same time.
    JSONDecodeError
        If the environment or container metadata is malformed.
    UnicodeDecodeError
        If the environment or container metadata cannot be decoded.
    """
    # load environment
    env_root = env_root.expanduser().resolve()
    env = _read_environment(env_root)
    if env is None:
        raise FileNotFoundError(f"Failed to read environment metadata at: {env_root}")

    # resolve tag or normalize argv
    argv = _normalize_argv(argv)
    if tag:
        if argv:
            raise CommandError(
                returncode=1,
                cmd=["bertrand", "activity", f"{str(env_root)}:{tag}", *argv],
                stdout="",
                stderr=
                    "Cannot specify both tag and build arguments when querying container activity.",
            )
        argv = env.tags.get(tag, [])
        if not argv:
            raise KeyError(f"Environment tag not found: {tag}")

    # search for container
    arg_hash = _arg_hash(argv)
    container, inspect = _search_container(env_root, env=env, arg_hash=arg_hash)
    if container is None or inspect is None:
        raise KeyError("No container found for the given tag or build arguments.")

    # print container activity
    docker_cmd(["top", container.container])
    return container


def stop_container(env_root: Path, tag: str, *, argv: list[str]) -> DockerContainer:
    """Stop a container, terminating all running processes within it.

    Parameters
    ----------
    env_root : Path
        The path to the environment directory.
    tag : str
        An optional, human-readable tag to look up in order to determine the proper
        arguments.  If `argv` is empty and this tag is not, then it will be searched in
        the environment metadata in order to replace `argv`.  Otherwise, `argv` will be
        used directly, and this tag must be empty.
    argv : list[str]
        An arbitrary number of command-line arguments to identify the container.
        These must match the expected `ARG` directives in the environment's Dockerfile.
        If `tag` is provided, then `argv` must be empty.

    Returns
    -------
    DockerContainer
        The stopped container metadata.

    Raises
    ------
    FileNotFoundError
        If no environment is found at the given path.
    KeyError
        If no container matches the given tag.
    CommandError
        If any docker command fails, or if both `tag` and `argv` are provided at the
        same time.
    JSONDecodeError
        If the environment or container metadata is malformed.
    UnicodeDecodeError
        If the environment or container metadata cannot be decoded.
    """
    # load environment
    env_root = env_root.expanduser().resolve()
    env = _read_environment(env_root)
    if env is None:
        raise FileNotFoundError(f"Failed to read environment metadata at: {env_root}")

    # resolve tag or normalize argv
    argv = _normalize_argv(argv)
    if tag:
        if argv:
            raise CommandError(
                returncode=1,
                cmd=["bertrand", "stop", f"{str(env_root)}:{tag}", *argv],
                stdout="",
                stderr="Cannot specify both tag and build arguments when stopping a container.",
            )
        argv = env.tags.get(tag, [])
        if not argv:
            raise KeyError(f"Environment tag not found: {tag}")

    # search for container
    arg_hash = _arg_hash(argv)
    container, inspect = _search_container(env_root, env=env, arg_hash=arg_hash)
    if container is None or inspect is None:
        raise KeyError("No container found for the given tag or build arguments.")

    # if container was not found or was removed, nothing to do
    _stop_container(inspect)
    return container


def pause_container(env_root: Path, tag: str, *, argv: list[str]) -> DockerContainer:
    """Pause an container, suspending all running processes within it, but not
    terminating them.

    Parameters
    ----------
    env_root : Path
        The path to the environment directory.
    tag : str
        An optional, human-readable tag to look up in order to determine the proper
        arguments.  If `argv` is empty and this tag is not, then it will be searched in
        the environment metadata in order to replace `argv`.  Otherwise, `argv` will be
        used directly, and this tag must be empty.
    argv : list[str]
        An arbitrary number of command-line arguments to identify the container.
        These must match the expected `ARG` directives in the environment's Dockerfile.
        If `tag` is provided, then `argv` must be empty.

    Returns
    -------
    DockerContainer
        The paused container's metadata.

    Raises
    ------
    FileNotFoundError
        If no environment is found at the given path.
    KeyError
        If no container matches the given tag or build arguments.
    CommandError
        If any docker command fails, or if both `tag` and `argv` are provided at the
        same time.
    JSONDecodeError
        If the environment or container metadata is malformed.
    UnicodeDecodeError
        If the environment or container metadata cannot be decoded.
    """
    # load environment
    env_root = env_root.expanduser().resolve()
    env = _read_environment(env_root)
    if env is None:
        raise FileNotFoundError(f"Failed to read environment metadata at: {env_root}")

    # resolve tag or normalize argv
    argv = _normalize_argv(argv)
    if tag:
        if argv:
            raise CommandError(
                returncode=1,
                cmd=["bertrand", "stop", f"{str(env_root)}:{tag}", *argv],
                stdout="",
                stderr="Cannot specify both tag and build arguments when stopping a container.",
            )
        argv = env.tags.get(tag, [])
        if not argv:
            raise KeyError(f"Environment tag not found: {tag}")

    # search container
    arg_hash = _arg_hash(argv)
    container, inspect = _search_container(env_root, env=env, arg_hash=arg_hash)
    if container is None or inspect is None:
        raise KeyError("No container found for the given tag or build arguments.")

    # stop container if running
    if inspect.get("State", {}).get("Running", False):
        docker_cmd(["pause", inspect["Id"]])
    return container


def resume_environment(env_root: Path, tag: str, *, argv: list[str]) -> DockerContainer:
    """Resume a paused environment container, restarting all suspended processes
    within it.

    Parameters
    ----------
    env_root : Path
        The path to the environment directory.
    tag : str
        An optional, human-readable tag to look up in order to determine the proper
        arguments.  If `argv` is empty and this tag is not, then it will be searched in
        the environment metadata in order to replace `argv`.  Otherwise, `argv` will be
        used directly, and this tag must be empty.
    argv : list[str]
        An arbitrary number of command-line arguments to identify the container.
        These must match the expected `ARG` directives in the environment's Dockerfile.
        If `tag` is provided, then `argv` must be empty.

    Returns
    -------
    DockerContainer
        The resumed container's metadata.

    Raises
    ------
    FileNotFoundError
        If no environment is found at the given path.
    KeyError
        If no container matches the given tag or build arguments.
    CommandError
        If any docker command fails, or if both `tag` and `argv` are provided at the
        same time.
    JSONDecodeError
        If the environment or container metadata is malformed.
    UnicodeDecodeError
        If the environment or container metadata cannot be decoded.
    """
    # load environment
    env_root = env_root.expanduser().resolve()
    env = _read_environment(env_root)
    if env is None:
        raise FileNotFoundError(f"Failed to read environment metadata at: {env_root}")

    # resolve tag or normalize argv
    argv = _normalize_argv(argv)
    if tag:
        if argv:
            raise CommandError(
                returncode=1,
                cmd=["bertrand", "stop", f"{str(env_root)}:{tag}", *argv],
                stdout="",
                stderr="Cannot specify both tag and build arguments when stopping a container.",
            )
        argv = env.tags.get(tag, [])
        if not argv:
            raise KeyError(f"Environment tag not found: {tag}")

    # load container
    arg_hash = _arg_hash(argv)
    container, inspect = _search_container(env_root, env=env, arg_hash=arg_hash)
    if container is None or inspect is None:
        raise KeyError("No container found for the given tag or build arguments.")

    # stop container if running
    if not inspect.get("State", {}).get("Running", False):
        docker_cmd(["unpause", inspect["Id"]])
    return container


def delete_environment(env_root: Path, tag: str, *, argv: list[str]) -> None:
    """Delete a container, removing it and its associated image from the Docker
    daemon, and deleting its metadata from the environment directory.

    Parameters
    ----------
    env_root : Path
        The path to the environment directory.
    tag : str
        An optional, human-readable tag to look up in order to determine the proper
        arguments.  If `argv` is empty and this tag is not, then it will be searched in
        the environment metadata in order to replace `argv`.  Otherwise, `argv` will be
        used directly, and this tag must be empty.
    argv : list[str]
        An arbitrary number of command-line arguments to identify the container.
        These must match the expected `ARG` directives in the environment's Dockerfile.
        If `tag` is provided, then `argv` must be empty.

    Raises
    ------
    FileNotFoundError
        If no environment is found at the given path.
    KeyError
        If no container matches the given tag or build arguments.
    CommandError
        If any docker command fails, or if both `tag` and `argv` are provided at the
        same time.
    JSONDecodeError
        If the environment or container metadata is malformed.
    UnicodeDecodeError
        If the environment or container metadata cannot be decoded.
    """
    # load environment
    env_root = env_root.expanduser().resolve()
    env = _read_environment(env_root)
    if env is None:
        raise FileNotFoundError(f"Failed to read environment metadata at: {env_root}")

    # resolve tag or normalize argv
    argv = _normalize_argv(argv)
    if tag:
        if argv:
            raise CommandError(
                returncode=1,
                cmd=["bertrand", "stop", f"{str(env_root)}:{tag}", *argv],
                stdout="",
                stderr="Cannot specify both tag and build arguments when stopping a container.",
            )
        argv = env.tags.get(tag, [])
        if not argv:
            raise KeyError(f"Environment tag not found: {tag}")

    # load container
    arg_hash = _arg_hash(argv)
    digest = env.builds.get(arg_hash, "")
    container, inspect = _load_container(env_root, env=env, arg_hash=arg_hash, digest=digest)

    # update environment search structures
    env = replace(
        env,
        tags={k: v for k, v in env.tags.items() if tag and k != tag},
        builds={k: v for k, v in env.builds.items() if v != digest},
        ids={k: v for k, v in env.ids.items() if v != digest},
    )
    _write_environment(env_root, env)

    # stop container if running, then remove both container and image
    if container is not None and inspect is not None:
        _stop_container(inspect)
        _remove_container(container, force=False)




# # TODO: all this IPC stuff should come at the end of the file, to differentiate it
# # from the basic docker interface, and associate it with a `bertrand code` command
# # inside the container.


# # TODO: Maybe if I modify `$ bertrand enter` to forward to `$ docker run`, I don't
# # need custom IPC constructs at all, and can use docker's built-in mechanisms for
# # invoking host processes from inside the container?


# @dataclass(frozen=True)
# class HostRequest:
#     """A JSON struct representing an IPC request to the host system, which will be
#     caught by a watcher process.  This is used to implement editor hooks without
#     installing full editors into the container image.
#     """
#     version: int  # version number for backwards compatibility
#     created: str  # ISO timestamp of request
#     digest: str  # digest of container issuing the request
#     action: str  # identifies the host action to take without allowing arbitrary code injection
#                  # currently only "code" is supported


# @dataclass(frozen=True)
# class CodeRequest(HostRequest):
#     """A special case of host request that covers the `code` action within a
#     container.
#     """
#     editor: str  # "vscode"|"nvim"|"vim"|"nano"


# @dataclass(frozen=True)
# class HostResponse:
#     """A JSON struct representing an IPC response from the host system, which can
#     be post-processed by the container.  This is used to implement editor hooks
#     without installing full editors into the container image.
#     """
#     version: int  # version number for backwards compatibility
#     created: str  # ISO timestamp of response
#     container: str  # id of requesting container
#     action: str  # requested action
#     returncode: int  # return code of host request
#     detail: str  # detail string describing what occurred for debugging purposes


# @dataclass(frozen=True)
# class CodeResponse(HostResponse):
#     """A special case of host response that covers the `code` action within a container
#     """
#     editor: str  # "vscode"|"nvim"|"vim"|"nano"



# # TODO: I don't know how to write the editor hooks in a way that respects the
# # 1:many relationship between environments and containers.  How would vscode discover
# # the correct container image for a given environment if there are multiple?  The
# # previous solution was to write out the editor hooks to the environment directory when
# # the container gets created, such that `code .` in the environment directory would
# # pick up that image automatically.  But with multiple containers per environment,
# # you'd have to provide some extra mechanism to select which image to use regardless,
# # or figure out a system where you can launch the text editor from inside the container
# # and pass its configuration into the editor explicitly.  That would be best, and
# # would move container selection into the `bertrand enter` command, where it ought to
# # be, and leave the editor invocation alone outside of hooking the container's internal
# # tools that I bootstrapped as part of the Dockerfile.  So really, the only true
# # solution is to launch the editor from inside the container, which probably
# # compromosies the whole idea of using `devcontainer.json` in the first place?


# def _copy_editor_hooks(env_root: Path) -> None:
#     env_root = env_root.expanduser().resolve()
#     env = _read_environment(env_root)
#     if env is None:
#         return

#     container = _container_name(spec)
#     info = _container_inspect(container)
#     if info is None:
#         # environment container not created yet (or deleted); Nothing to copy.
#         return

#     # ensure container is running (docker exec requires it)
#     running = bool(((info.get("State") or {}).get("Running")))
#     if not running:
#         docker_cmd(["start", container], check=False, capture_output=True)

#     # copy templates into bind mount (/env) without overwriting existing files.
#     # We run inside the container as host UID/GID so created files are owned correctly.
#     ids = host_user_ids()
#     if ids is None:
#         uid, gid = (0, 0)
#     else:
#         uid, gid = ids

#     script = r"""
# set -euo pipefail

# SRC="/opt/bertrand/templates/devcontainer"
# DST="/env"

# # nothing to do if templates are missing in the image
# if [ ! -d "$SRC" ]; then
#   exit 0
# fi

# # Create target dirs
# install -d "$DST/.devcontainer" "$DST/.vscode"

# copy_if_missing() {
#   local rel="$1"
#   local mode="$2"
#   if [ ! -e "$DST/$rel" ]; then
#     install -m "$mode" "$SRC/$rel" "$DST/$rel"
#   fi
# }

# copy_if_missing ".devcontainer/devcontainer.json" "0644"
# copy_if_missing ".devcontainer/postCreate.sh" "0755"
# copy_if_missing ".vscode/tasks.json" "0644"
# copy_if_missing ".vscode/settings.json" "0644"
# """.strip()

#     docker_cmd(
#         ["exec", "-u", f"{uid}:{gid}", container, "/bin/sh", "-lc", script],
#         capture_output=True,
#         check=False,  # best-effort; we patch below even if copy partially fails
#     )

#     # Patch the devcontainer.json to reference the *actual* image tag for this env.
#     # We only patch if the file exists and parses as JSON.
#     devcontainer_path = env_root / ".devcontainer" / "devcontainer.json"
#     if devcontainer_path.exists():
#         try:
#             data = json.loads(devcontainer_path.read_text(encoding="utf-8"))
#             if isinstance(data, dict):
#                 desired = _image_tag(spec)  # e.g. bertrand-env:<uuid>
#                 if data.get("image") != desired:
#                     data["image"] = desired
#                     atomic_write_text(devcontainer_path, json.dumps(data, indent=2) + "\n")
#         except json.JSONDecodeError:
#             # if user edited it into invalid JSON, do not clobber their file
#             pass
