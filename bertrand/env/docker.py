"""Install Docker Engine and pull container images."""
from __future__ import annotations

import json
import os
import re
import shlex
import time
import uuid

from datetime import datetime, timezone
from pathlib import Path
from resource import getpagesize
from types import TracebackType
from typing import Iterable, List, Literal, TypedDict, overload

from .docker_engine import docker_cmd, docker_exec
from .run import HIDDEN, CommandError, atomic_write_text, up_to_date
from .version import __version__

#pylint: disable=redefined-builtin, redefined-outer-name, broad-except


VERSION: int = 1
MOUNT: str = "/env"
LABEL: str = "bertrand"
TIMEOUT: int = 30
EXCLUDE: str = HIDDEN


def _bertrand_dir(env_root: Path) -> Path:
    return env_root / ".bertrand"


def _env_file(env_root: Path) -> Path:
    return _bertrand_dir(env_root) / "env.json"


def _image_dir(env_root: Path) -> Path:
    return _bertrand_dir(env_root) / "images"


def _image_file(env_root: Path, uuid: str) -> Path:
    return _image_dir(env_root) / f"{uuid}.json"


def _container_dir(env_root: Path) -> Path:
    return _bertrand_dir(env_root) / "containers"


def _container_file(env_root: Path, uuid: str) -> Path:
    return _container_dir(env_root) / f"{uuid}.json"


def _docker_file(env_root: Path) -> Path:
    return env_root / "Dockerfile"


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


def _normalize_shell(value: object) -> list[str]:
    if isinstance(value, list) and all(isinstance(x, str) and x for x in value):
        out = _normalize_args(value)
        if not out:
            raise ValueError("shell must not be empty")
        return out
    if isinstance(value, str):
        out = _normalize_args([value])
        if not out:
            raise ValueError("shell must not be empty")
        return out
    raise ValueError("shell must be a string or list of strings")


def _validate_version(data: dict[str, object]) -> int:
    version = data.get("version")
    if not isinstance(version, int) or version <= 0:
        raise ValueError(f"missing or invalid 'version' field: {version}")
    return version


def _validate_tag(data: dict[str, object]) -> str:
    tag = data.get("tag")
    if not isinstance(tag, str):
        raise ValueError(f"missing or invalid 'tag' field: {tag}")
    tag = tag.strip()
    if not tag:
        raise ValueError(f"missing or invalid 'tag' field: {tag}")
    sanitized = _sanitize_name(tag)
    if tag != sanitized:
        raise ValueError(
            f"invalid characters in 'tag' field: '{tag}' (sanitizes to: '{sanitized}')"
        )
    return tag


def _validate_id(data: dict[str, object]) -> str:
    id = data.get("id")
    if not isinstance(id, str):
        raise ValueError(f"missing or invalid 'id' field: {id}")
    id = id.strip()
    if not id:
        raise ValueError(f"missing or invalid 'id' field: {id}")
    return id


def _validate_created(data: dict[str, object]) -> str:
    created = data.get("created")
    if not isinstance(created, str) or not created.strip():
        raise ValueError(f"missing or invalid 'created' field: {created}")
    try:
        datetime.fromisoformat(created)
    except Exception as err:
        raise ValueError(f"'created' must be a valid ISO timestamp: {created}") from err
    return created


def _validate_args(data: dict[str, object]) -> list[str]:
    args = data.get("args")
    if not isinstance(args, list) or not all(
        isinstance(x, str) and x for x in args
    ):
        raise ValueError(f"missing or invalid 'args' field: {args}")
    return _normalize_args(args)


class Image:
    """On-disk metadata representing a local Bertrand Docker image, which is a compiled
    snapshot of an environment with a particular set of build arguments.  An
    environment can have many images, each built with a different set of Dockerfile
    arguments, and each image is considered to be immutable once created.

    Specific care is taken not to store anything that references the host filesystem,
    in order to allow renaming/relocation of the environment directory.

    Parameters
    ----------
    root : Path
        The root path of the environment directory.
    uuid : str
        The unique ID for this image metadata file.  This is equivalent to the file
        name of the image metadata file in `image_dir`.

    Raises
    ------
    ValueError
        If the image metadata is malformed.

    Attributes
    ----------
    root : Path
        An absolute root path to the environment directory.  This is not stored in the
        on-disk JSON in order to allow relocation of the environment directory.
    uuid : str
        The unique ID for this image metadata file.  This is equivalent to the metadata
        file name in `image_dir`, and will not be stored in the on-disk JSON.
    version : int
        The version number for backwards compatibility.
    tag : str
        A human-readable tag identifying this image within the environment.
    id : str
        The unique Docker image ID.
    created : str
        The ISO timestamp when the image was created.
    args : list[str]
        The original `docker build` args used to build the image.
    """
    class Inspect(TypedDict, total=False):
        """Type hint for `docker container inspect` output."""
        Id: str
        Created: str

    root: Path
    uuid: str
    version: int
    tag: str
    id: str
    created: str
    args: list[str]

    def __init__(self, root: Path, uuid: str) -> None:
        self.root = root.expanduser().resolve()
        self.uuid = uuid
        path = _image_file(root, uuid)
        if not path.exists():
            raise ValueError(f"image metadata not found: {path}")
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            if not isinstance(data, dict):
                raise ValueError("image metadata must be a JSON object")
            self.version = _validate_version(data)
            self.tag = _validate_tag(data)
            self.id = _validate_id(data)
            self.created = _validate_created(data)
            self.args = _validate_args(data)
        except Exception as err:
            raise ValueError(f"Invalid image metadata at {path}: {err}") from err

    @property
    def name(self) -> str:
        """Return a human-readable name for this image based on the environment root
        and an optional tag.

        Returns
        -------
        str
            A sanitized, human-readable container name combining the last component of
            the environment root, this image's `tag`, and its `uuid` (e.g.
            `<myproject>.<image_tag>.<uuid>` or `<myproject>.<uuid>`).
        """
        parts = [_sanitize_name(self.root.name)]
        if self.tag:
            parts.append(self.tag)
        parts.append(self.uuid)
        return ".".join(parts)

    @property
    def path(self) -> Path:
        """Return the path to this image's metadata file on disk.

        Returns
        -------
        Path
            The path to this image's metadata file.
        """
        return _image_file(self.root, self.uuid)

    @staticmethod
    def inspect(name_or_id: str) -> Image.Inspect | None:
        """Inspect a Docker image by name or ID.

        Parameters
        ----------
        name_or_id : str
            The name or ID of the Docker image to inspect.

        Returns
        -------
        Image.Inspect | None
            A JSON response from the Docker daemon or None if the image could not
            be found.  Type hints are provided via the `Image.Inspect` TypedDict.
        """
        result = docker_cmd(["image", "inspect", name_or_id], check=False, capture_output=True)
        if result.returncode != 0 or not result.stdout:
            return None
        data = json.loads(result.stdout)
        return data[0] if data else None


class Container:
    """On-disk metadata representing a local Bertrand Docker container, which is a
    runtime context for a compiled image.  An image can have many containers, each
    built with a different runtime configuration, and each container is considered to
    be immutable once created.

    Specific care is taken not to store anything that references the host filesystem or
    container name, in order to allow renaming/relocation of the environment directory.

    Parameters
    ----------
    root : Path
        The root path of the environment directory.
    uuid : str
        The unique ID for this container metadata file.  This is equivalent to the file
        name of the container metadata file in `container_dir`.

    Raises
    ------
    ValueError
        If the container metadata is malformed.

    Attributes
    ----------
    root : Path
        An absolute root path to the environment directory.  This is not stored in the
        on-disk JSON in order to allow relocation of the environment directory.
    uuid : str
        The unique ID for this container metadata file.  This is equivalent to the
        metadata file name in `container_dir`, and will not be stored in the on-disk
        JSON.
    version : int
        The version number for backwards compatibility.
    parent : str
        A unique ID for the image metadata file from which this container was created.
        This is equivalent to the file name of the image metadata file in `image_dir`.
    tag : str
        A human-readable tag identifying this container within the parent image.
    id : str
        The unique Docker container ID.
    created : str
        The ISO timestamp when the container was created.
    args : list[str]
        The original `docker create` arguments used to create the container.
    """
    class Mount(TypedDict, total=False):
        """Type hint for docker container mount information."""
        Type: Literal["bind", "volume", "tmpfs", "npipe"]
        Destination: str
        Source: str

    class State(TypedDict, total=False):
        """Type hint for docker container state information."""
        Running: bool
        Paused: bool
        Restarting: bool
        Dead: bool

    class Inspect(TypedDict, total=False):
        """Type hint for docker container inspect output."""
        Id: str
        Created: str
        Mounts: List[Container.Mount]
        State: Container.State

    root: Path
    uuid: str
    version: int
    parent: str
    tag: str
    id: str
    created: str
    args: list[str]

    def __init__(self, root: Path, uuid: str) -> None:
        self.root = root.expanduser().resolve()
        self.uuid = uuid
        path = _container_file(root, uuid)
        if not path.exists():
            raise ValueError(f"container metadata not found: {path}")
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            if not isinstance(data, dict):
                raise ValueError("container metadata must be a JSON object")
            self.version = _validate_version(data)
            self.parent = _validate_id(data)
            self.tag = _validate_tag(data)
            self.id = _validate_id(data)
            self.created = _validate_created(data)
            self.args = _validate_args(data)
        except Exception as err:
            raise ValueError(f"Invalid container metadata at {path}: {err}") from err

    @property
    def image(self) -> Image:
        """Read the parent image metadata for this container.

        Returns
        -------
        Image
            The parent `Image` object for this container.
        """
        return Image(self.root, self.parent)

    @property
    def name(self) -> str:
        """Return a human-readable name for this container based on the environment
        root, parent image, and an optional tag.

        Returns
        -------
        str
            A sanitized, human-readable container name combining the last component of
            the environment root, the parent image `tag`, this container's `tag`, and
            its `uuid` (e.g. `<myproject>.<image_tag>.<container_tag>.<uuid>`, 
            `<myproject>.<image_tag>.<uuid>`, or `<myproject>.<uuid>`).
        """
        parts = [_sanitize_name(self.root.name)]
        image = self.image
        if image.tag:
            parts.append(image.tag)
        if self.tag:
            parts.append(self.tag)
        parts.append(self.uuid)
        return ".".join(parts)

    @property
    def path(self) -> Path:
        """Return the path to this container's metadata file on disk.

        Returns
        -------
        Path
            The path to this container's metadata file.
        """
        return _container_file(self.root, self.uuid)

    @staticmethod
    def inspect(name_or_id: str) -> Container.Inspect | None:
        """Inspect a Docker container by name or ID.

        Parameters
        ----------
        name_or_id : str
            The name or ID of the Docker container to inspect.

        Returns
        -------
        Container.Inspect | None
            A JSON response from the Docker daemon or None if the container could not
            be found.  Type hints are provided via the `Container.Inspect` TypedDict.
        """
        result = docker_cmd(["inspect", name_or_id], check=False, capture_output=True)
        if result.returncode != 0 or not result.stdout:
            return None
        data = json.loads(result.stdout)
        return data[0] if data else None

    @staticmethod
    def mount(inspect: Container.Inspect) -> Path | None:
        """Return the root path of the environment directory mounted to a given
        Docker container.

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
                    return Path(src).expanduser()
        return None

    @staticmethod
    def start(inspect: Container.Inspect) -> None:
        """Start a Docker container if it is not already running.

        Parameters
        ----------
        inspect : Container.Inspect
            The output of `Container.inspect()` for the container to start.
        """
        running = bool(((inspect.get("State") or {}).get("Running")))
        if not running:
            docker_cmd(["container", "start", inspect["Id"]])

    @staticmethod
    def stop(inspect: Container.Inspect) -> None:
        """Stop a Docker container if it is currently running.

        Parameters
        ----------
        inspect : Container.Inspect
            The output of `Container.inspect()` for the container to stop.
        """
        running = bool(((inspect.get("State") or {}).get("Running")))
        if running:
            docker_cmd(["container", "stop", inspect["Id"]])


class DockerEnvironment:
    """On-disk metadata representing environment-level data structures, which map from
    human-readable tags to argument hashes, and then to unique IDs for Docker images
    and containers.

    This class is meant to be used as a context manager, which will automatically
    acquire and release a lock on the environment directory in order to prevent
    concurrent modifications.  The environment metadata will be loaded upon entering
    the outermost context, and written back to disk upon exiting it, to synchronize
    any changes made during the context's lifetime.

    Parameters
    ----------
    root : Path
        The root path of the environment directory.
    timeout : int, optional
        The maximum time in seconds to wait for acquiring the environment lock.
        Defaults to `TIMEOUT`, which equates to 30 seconds.
    exclude : str, optional
        A regular expression pattern that excludes certain files and directories
        from consideration during incremental builds.  Files that do not match this
        pattern will have their modification times compared to determine whether
        a rebuild is necessary.  If a directory matches this pattern, then all files
        and subdirectories within it will also be excluded.  Defaults to `HIDDEN`,
        which excludes all files and directories that begin with a dot.
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
        on-disk JSON in order to allow relocation of the environment directory.
    timeout : int
        The maximum time in seconds to wait for acquiring the environment lock.  Not
        stored in the on-disk JSON.
    depth : int
        The nested depth of `with` statements for this environment (0 == disengaged).
        Not stored in the on-disk JSON.
    version : int
        The version number for backwards compatibility.
    tags : dict[str, DockerEnvironment.Tag]
        A mapping from image tags to dictionaries containing the corresponding file
        name in `image_dir`, as well as a nested dictionary mapping container tags to
        their file names in `container_dir`.  This is the main data structure used to
        look up images and containers by human-readable names, and all relevant
        metadata can be found in the corresponding files.
    shell : list[str]
        The shell command to execute during `bertrand enter`.
    code : list[str]
        The default host command invoked by `code` within the container.
    """
    class Tag(TypedDict):
        """Type hint representing an image in the `tags` dictionary."""
        uuid: str  # filename in `image_dir`
        containers: dict[str, str]  # container tag -> filename in `container_dir`

    root: Path
    timeout: int
    depth: int
    version: int
    tags: dict[str, DockerEnvironment.Tag]
    exclude: str
    shell: list[str]
    code: list[str]

    def __init__(
        self,
        root: Path,
        timeout: int = TIMEOUT,
        exclude: str = EXCLUDE,
        shell: list[str] | None = None,
        code: list[str] | None = None
    ) -> None:
        self.root = root.expanduser().resolve()
        self.timeout = timeout
        self.depth = 0
        self.version = 0
        self.tags = {}
        self.exclude = exclude
        self.shell = _normalize_shell(shell) or ["bash", "-l"]
        self.code = _normalize_shell(code) or ["vscode"]

    def _validate_tags(self, data: dict[str, object]) -> None:
        tags = data.get("tags")
        if not isinstance(tags, dict):
            raise ValueError(f"missing or invalid 'tags' field: {tags}")

        # validate each tag entry
        out: dict[str, DockerEnvironment.Tag] = {}
        for image_tag, image in tags.items():
            if not isinstance(image_tag, str):
                raise ValueError(f"invalid image tag: {image_tag}")
            if not isinstance(image, dict):
                raise ValueError(f"invalid data for image tag '{image_tag}': {image}")

            # validate uuid
            image_id = image["uuid"]
            if not isinstance(image_id, str):
                raise ValueError(f"invalid uuid for image tag '{image_tag}': {image_id}")
            try:
                uuid.UUID(image_id)
            except ValueError as err:
                raise ValueError(f"invalid uuid for image tag '{image_tag}': {image_id}") from err
            if not _image_file(self.root, image_id).exists():
                raise ValueError(
                    f"missing image metadata file for uuid '{image_id}' in image tag '{image_tag}'"
                )

            # validate nested containers
            containers = image["containers"]
            if not isinstance(containers, dict):
                raise ValueError(f"invalid containers for image tag '{image_tag}': {containers}")
            validated: dict[str, str] = {}
            for container_tag, container_id in containers.items():
                if not isinstance(container_tag, str):
                    raise ValueError(
                        f"invalid container tag for image tag '{image_tag}': {container_tag}"
                    )
                if not isinstance(container_id, str):
                    raise ValueError(
                        f"invalid container uuid '{container_id}' for container tag "
                        f"'{container_tag}' in image tag '{image_tag}'"
                    )
                try:
                    uuid.UUID(container_id)
                except ValueError as err:
                    raise ValueError(
                        f"invalid container uuid '{container_id}' for container tag "
                        f"'{container_tag}' in image tag '{image_tag}'"
                    ) from err
                if not _container_file(self.root, container_id).exists():
                    raise ValueError(
                        f"missing container metadata file for uuid '{container_id}' with "
                        f"container tag '{container_tag}' in image tag '{image_tag}'"
                    )
                validated[container_tag] = container_id
            out[image_tag] = {"uuid": image_id, "containers": validated}

        self.tags = out

    def _validate_exclude(self, data: dict[str, object]) -> None:
        exclude = data.get("exclude")
        if not isinstance(exclude, str) or not exclude:
            raise ValueError("missing required field: exclude")
        try:
            re.compile(exclude)
        except re.error as err:
            raise ValueError(f"invalid regex pattern for 'exclude': {exclude}") from err
        self.exclude = exclude

    def _validate_shell(self, data: dict[str, object]) -> None:
        shell = data.get("shell")
        if not isinstance(shell, (str, list)) or not shell:
            raise ValueError("missing required field: shell")
        self.shell = _normalize_shell(shell)

    def _validate_code(self, data: dict[str, object]) -> None:
        code = data.get("code")
        if not isinstance(code, (str, list)) or not code:
            raise ValueError("missing required field: code")
        self.code = _normalize_shell(code)

    def __enter__(self) -> DockerEnvironment:
        # allow nested context managers without deadlocking
        self.depth += 1
        if self.depth > 1:
            return self

        # acquire lock
        path = self.bertrand_dir / ".lock"
        start = time.time()
        while True:
            try:
                path.mkdir(parents=True)
                break
            except FileExistsError as err:
                if (time.time() - start) > self.timeout:
                    raise TimeoutError(
                        f"could not acquire environment lock within {self.timeout} seconds"
                    ) from err
                time.sleep(0.1)

        # try to load existing metadata if possible
        env_file = self.env_file
        if env_file.exists():
            try:
                data = json.loads(env_file.read_text(encoding="utf-8"))
                if not isinstance(data, dict):
                    raise ValueError("environment metadata must be a JSON mapping")
                self.version = _validate_version(data)
                self._validate_tags(data)
                self._validate_exclude(data)
                self._validate_shell(data)
                self._validate_code(data)
            except Exception as err:
                raise ValueError(f"Invalid environment metadata at {env_file}: {err}") from err
            return self

        # initialize new metadata if needed
        self.version = VERSION
        self.tags = {}

        # init .dockerignore
        docker_ignore = self.docker_ignore
        if not docker_ignore.exists():
            docker_ignore.parent.mkdir(parents=True, exist_ok=True)
            atomic_write_text(docker_ignore, "")

        # init Dockerfile
        docker_file = self.docker_file
        if not docker_file.exists():
            docker_file.parent.mkdir(parents=True, exist_ok=True)
            atomic_write_text(docker_file, rf"""# syntax=docker/dockerfile:1

# Bertrand requires a minimal set of arguments to be provided at compile time, which
# are baked into its reproducible Docker images to avoid lengthy recompilation.  These
# arguments may be overridden by passing `<arg>=<value>` options to the
# `bertrand build`, `bertrand start`, or `bertrand enter` commands, which are then
# forwarded to this Dockerfile.  Otherwise, the default values will be used.

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

# you can extend this file in order to create a reproducible image that others can pull
# from in their own Dockerfiles.  For example:

RUN pip install .

# `pip install .` will compile the contents of the local environment directory (which
# is always the default WORKDIR) and install them into the base image as Python
# packages, C++ modules, and/or executable binaries on the container's PATH.  If you
# then upload this image to a Docker repository, downstream users will be able to use
# `FROM <your-image>` in their own Dockerfiles in order to inherit Bertrand's toolchain
# along with your built artifacts and dependencies without needing to recompile them
# from source.  This can be useful for large projects where build time is significant,
# or which have external dependencies or build configurations that are otherwise
# difficult to install.  Small projects without significant configuration needs are
# encouraged to use the bundled package managers instead, and leave this file alone.

# In most cases, `pip install .` is all you need, but if you'd like to add your own
# compilation flags or install additional system dependencies outside of
# `pyproject.toml`, then you can do so using standard Dockerfile commands.  See the
# official Dockerfile documentation for a comprehensive reference, and the Bertrand
# toolchain documentation for more details on how this fits into the overall build
# process, as well as tips for your own Dockerfiles.
""")
        return self

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc_value: BaseException | None,
        traceback: TracebackType | None
    ) -> None:
        # allow nested context managers without deadlocking
        self.depth -= 1
        if self.depth > 0:
            return

        # write changes
        self.root.mkdir(parents=True, exist_ok=True)
        atomic_write_text(
            self.env_file,
            json.dumps({
                "version": self.version,
                "tags": self.tags,
                "exclude": self.exclude,
                "shell": self.shell,
                "code": self.code,
            }, indent=2) + "\n"
        )

        # release lock
        (self.bertrand_dir / ".lock").rmdir()

    def __hash__(self) -> int:
        return hash(self.root)

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, DockerEnvironment):
            return NotImplemented
        return self.root == other.root

    @property
    def bertrand_dir(self) -> Path:
        """
        Returns
        -------
        Path
            The path to the `.bertrand` directory within the environment root.
        """
        return _bertrand_dir(self.root)

    @property
    def env_file(self) -> Path:
        """
        Returns
        -------
        Path
            The path to the `env.json` file which this metadata is tied to.
        """
        return _env_file(self.root)

    @property
    def image_dir(self) -> Path:
        """
        Returns
        -------
        Path
            The path to the `images` directory storing image metadata by digest.
        """
        return _image_dir(self.root)

    @property
    def container_dir(self) -> Path:
        """
        Returns
        -------
        Path
            The path to the `containers` directory storing container metadata by digest.
        """
        return _container_dir(self.root)

    @property
    def ipc_dir(self) -> Path:
        """
        Returns
        -------
        Path
            The path to the `ipc` directory for inter-process communication.
        """
        return self.bertrand_dir / "ipc"

    @property
    def ipc_requests(self) -> Path:
        """
        Returns
        -------
        Path
            The path to the `requests` file for IPC requests.
        """
        return self.ipc_dir / "requests"

    @property
    def ipc_processing(self) -> Path:
        """
        Returns
        -------
        Path
            The path to the `processing` file for IPC processing status.
        """
        return self.ipc_dir / "processing"

    @property
    def ipc_done(self) -> Path:
        """
        Returns
        -------
        Path
            The path to the `done` file for IPC completion status.
        """
        return self.ipc_dir / "done"

    @property
    def ipc_failed(self) -> Path:
        """
        Returns
        -------
        Path
            The path to the `failed` file for IPC failure status.
        """
        return self.ipc_dir / "failed"

    @property
    def docker_file(self) -> Path:
        """
        Returns
        -------
        Path
            The path to the `Dockerfile` within the environment root.
        """
        return _docker_file(self.root)

    @property
    def docker_ignore(self) -> Path:
        """
        Returns
        -------
        Path
            The path to the `.dockerignore` file within the environment root.
        """
        return self.root / ".dockerignore"

    @staticmethod
    def current() -> DockerEnvironment | None:
        """Detect whether the current process is running inside a Bertrand Docker
        container.

        Returns
        -------
        DockerEnvironment | None
            A DockerEnvironment metadata object with the proper mount path if invoked
            within a Bertrand Docker container, or None otherwise.  Note that the
            result is disengaged, and must be acquired as a context manager before it
            can be used to access or modify the environment.
        """
        if "BERTRAND_CONTAINER" not in os.environ or "BERTRAND_IMAGE" not in os.environ:
            return None
        return DockerEnvironment(root=Path(MOUNT))

    @staticmethod
    def parse(spec: str) -> tuple[Path, str, str]:
        """Parse a string of the form `<env_root>[:<image_tag>[:<container_tag>]]` into
        its constituent parts.

        Parameters
        ----------
        spec : str
            The container specification string to parse, which is usually provided from
            the command line.

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

    def ls(self, image: str | None = None, container: str | None = None) -> list[str]:
        """Return a list of image or container tags within this environment.

        Parameters
        ----------
        image : str | None, optional
            An optional image tag to list.  If None (the default), all image tags in
            this environment will be listed.
        container : str | None, optional
            An optional container tag to list.  If None (the default), all container
            tags in the indicated image will be listed.

        Returns
        -------
        list[str]
            A list of image or container tags within this environment.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager.
        TypeError
            If `image` is None but `container` is not.
        """
        if self.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")

        # list all images
        if image is None:
            if container is not None:
                raise TypeError("cannot specify a container when listing all images")
            return list(self.tags.keys())

        # list all containers in an image
        tag = self.tags.get(image)
        if tag is None:
            return []
        containers = tag["containers"]
        if container is None:
            return list(containers.keys())

        # check for specific container in an image
        if container in containers:
            return [container]
        return []

    def _rm_all(self) -> int:
        total = 0
        for image in self.tags.values():
            image_id = image["uuid"]
            containers = image["containers"]
            for container_id in containers.values():
                docker_cmd(
                    ["container", "rm", "-f", container_id],
                    check=False,
                    capture_output=True
                )
                _container_file(self.root, container_id).unlink(missing_ok=True)
            total += len(containers)
            docker_cmd(["image", "rm", "-f", image_id], check=False, capture_output=True)
            _image_file(self.root, image_id).unlink(missing_ok=True)
        total += len(self.tags)
        self.tags.clear()
        return total

    def _rm_image(self, image: str) -> int:
        tag = self.tags.get(image)
        if tag is None:
            return 0
        image_id = tag["uuid"]
        containers = tag["containers"]
        for container_id in containers.values():
            docker_cmd(
                ["container", "rm", "-f", container_id],
                check=False,
                capture_output=True
            )
            _container_file(self.root, container_id).unlink(missing_ok=True)
        docker_cmd(["image", "rm", "-f", image_id], check=False, capture_output=True)
        _image_file(self.root, image_id).unlink(missing_ok=True)
        self.tags.pop(image)
        return len(containers) + 1

    def rm(self, image: str | None = None, container: str | None = None) -> int:
        """Delete images and/or containers from this environment.

        Parameters
        ----------
        image : str | None, optional
            An optional image tag to remove.  If None (the default), all images and
            containers in this environment will be removed.
        container : str | None, optional
            An optional container tag to remove.  If None (the default), all containers
            in the indicated image (or whole environment if `image` is None) will be
            removed.

        Returns
        -------
        int
            The number of images and containers that were removed, for posterity.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager.
        TypeError
            If `image` is None but `container` is not.
        """
        if self.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if DockerEnvironment.current() is not None:
            raise OSError("cannot modify an environment from within a Bertrand container")

        # delete all images
        if image is None:
            if container is not None:
                raise TypeError("cannot specify a container when removing all images")
            return self._rm_all()

        # delete all containers in an image
        if container is None:
            return self._rm_image(image)

        # delete specific container in an image
        tag = self.tags.get(image)
        if tag is None:
            return 0
        containers = tag["containers"]
        container_id = containers.get(container)
        if container_id is None:
            return 0
        docker_cmd(["container", "rm", "-f", container_id], check=False, capture_output=True)
        _container_file(self.root, container_id).unlink(missing_ok=True)
        containers.pop(container, None)
        return 1

    @overload
    def __getitem__(self, args: tuple[str, str]) -> Container | None: ...
    @overload
    def __getitem__(self, args: str | tuple[str, None]) -> Image | None: ...
    def __getitem__(self, args: str | tuple[str, str | None]) -> Image | Container | None:
        """Locate an image or container by tag within this environment, and load its
        metadata without modifying the environment.

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
        if self.depth < 1:
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
        tag = self.tags.get(image_tag)
        if tag is None:
            return None
        if container_tag is None:
            return Image(self.root, tag["uuid"])

        # search for container
        containers = tag["containers"]
        container_id = containers.get(container_tag)
        if container_id is None:
            return None
        return Container(self.root, container_id)

    def _load_image(
        self,
        image_tag: str,
        image_id: str,
        containers: dict[str, str]
    ) -> Image | None:
        # find image metadata
        destination = _image_file(self.root, image_id)
        if not destination.exists():
            for container_id in containers.values():
                docker_cmd(
                    ["container", "rm", "-f", container_id],
                    check=False,
                    capture_output=True
                )
                _container_file(self.root, container_id).unlink(missing_ok=True)
            self.tags.pop(image_tag)
            return None

        # load image metadata
        try:
            image = Image(self.root, image_id)
        except Exception:
            for container_id in containers.values():
                docker_cmd(
                    ["container", "rm", "-f", container_id],
                    check=False,
                    capture_output=True
                )
                _container_file(self.root, container_id).unlink(missing_ok=True)
            self.tags.pop(image_tag)
            destination.unlink(missing_ok=True)
            return None

        return image

    def _inspect_image(
        self,
        image: Image,
        containers: dict[str, str]
    ) -> Image.Inspect | None:
        inspect = Image.inspect(image.id)
        if inspect is None:
            for c in containers.values():
                docker_cmd(
                    ["container", "rm", "-f", c],
                    check=False,
                    capture_output=True
                )
                _container_file(self.root, c).unlink(missing_ok=True)
            self.tags.pop(image.tag)
            image.path.unlink(missing_ok=True)
            return None
        return inspect

    def _rebuild_image(
        self,
        image: Image,
        containers: dict[str, str],
    ) -> Image | None:
        created = datetime.fromisoformat(image.created).replace(tzinfo=timezone.utc)
        if not up_to_date(self.root, created, self.exclude):
            for c in containers.values():
                docker_cmd(["container", "rm", "-f", c], check=False, capture_output=True)
                _container_file(self.root, c).unlink(missing_ok=True)
            docker_cmd(["image", "rm", "-f", image.id], check=False, capture_output=True)
            image.path.unlink(missing_ok=True)
            self.tags.pop(image.tag, None)
            return None
        return image

    def _load_container(
        self,
        container_id: str,
        containers: dict[str, str]
    ) -> Container | None:
        # find container metadata
        destination = _container_file(self.root, container_id)
        if not destination.exists():
            containers.pop(container_id, None)
            return None

        # load container metadata
        try:
            container = Container(self.root, container_id)
        except Exception:
            containers.pop(container_id, None)
            destination.unlink(missing_ok=True)
            return None

        return container

    def _inspect_container(
        self,
        container: Container,
        containers: dict[str, str]
    ) -> Container.Inspect | None:
        inspect = Container.inspect(container.id)
        if inspect is None:
            containers.pop(container.tag, None)
            container.path.unlink(missing_ok=True)
            return None
        return inspect

    def _relocate_container(
        self,
        container: Container,
        inspect: Container.Inspect,
        containers: dict[str, str]
    ) -> Container | None:
        mount = Container.mount(inspect)
        if mount is not None:
            try:
                mount = mount.resolve()
                if mount != self.root:  # relocated
                    mount = None
            except OSError:  # unable to resolve for some reason
                mount = None
        if mount is None:
            docker_cmd(["container", "rm", "-f", container.id], check=False, capture_output=True)
            containers.pop(container.id, None)
            container.path.unlink(missing_ok=True)
            return None

        return container

    @overload
    def load(self, image_tag: str, container_tag: str) -> Container | None: ...
    @overload
    def load(self, image_tag: str, container_tag: None = None) -> Image | None: ...
    def load(self, image_tag: str, container_tag: str | None = None) -> Image | Container | None:
        """Locate an image or container by tag within this environment and load its
        metadata, cleaning up any stale references if necessary.

        Parameters
        ----------
        image_tag : str
            The image tag to search for.
        container_tag : str | None, optional
            An optional container tag to search for within the indicated image.  If
            None (the default), only the image will be searched for.

        Returns
        -------
        Image | Container | None
            The corresponding image or container metadata object, or None if no
            matching tag could be found, the metadata could not be loaded, the
            underlying Docker image or container could not be found, or has drifted
            from the expected mount point.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container.
        """
        if self.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if DockerEnvironment.current() is not None:
            raise OSError("cannot modify an environment from within a Bertrand container")

        # attempt to load existing image metadata and Docker image
        tag = self.tags.get(image_tag)
        if tag is None:
            return None
        image_id = tag["uuid"]
        containers = tag["containers"]
        image = self._load_image(image_tag, image_id, containers)
        if image is None or self._inspect_image(image, containers) is None:
            return None

        # if no container tag is provided, then we are done
        if container_tag is None:
            return image

        # otherwise, attempt to load existing container metadata and Docker container
        container_id = containers.get(container_tag)
        if container_id is None:
            return None
        container = self._load_container(container_id, containers)
        if container is None:
            return None
        container_inspect = self._inspect_container(container, containers)
        if container_inspect is None:
            return None

        # verify mount point hasn't drifted
        return self._relocate_container(container, container_inspect, containers)

    def _build_image(self, image_tag: str) -> tuple[Image | None, list[str] | None]:
        # search for image
        tag = self.tags.get(image_tag)
        if tag is None:
            return None, None
        image_id = tag["uuid"]
        containers = tag["containers"]

        # attempt to load existing image metadata
        image = self._load_image(image_tag, image_id, containers)
        if image is None:
            return None, None
        args = image.args

        # verify underlying Docker image exists
        inspect = self._inspect_image(image, containers)
        if inspect is None:
            return None, args

        # check for incremental rebuild
        image = self._rebuild_image(image, containers)
        if image is None:
            return None, args

        return image, args

    def build_image(self, image_tag: str, image_args: list[str]) -> Image:
        """Incrementally build a Docker image with the given tag and arguments, or load
        an existing one if it is already up-to-date.

        Parameters
        ----------
        image_tag : str
            The image tag to search for.  If no existing image with the same tag is
            found, or the existing image is out of date, or its arguments differ from
            `image_args`, then a new image will be built and associated with this tag.
            If a non-empty tag is provided, then `image_args` must also be non-empty.
        image_args : list[str]
            The `docker build` arguments to use when building the image if no existing
            image could be found.  If an existing image is found with the same tag, but
            different arguments, then it will be removed and replaced with a new image
            built with these arguments.  If a non-empty list of arguments is provided,
            then `image_tag` must also be non-empty.

        Returns
        -------
        Image
            The resulting image metadata, which may be a reference to an existing image
            or a newly built one.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container, or if non-empty
            `image_args` are provided with an empty `image_tag`, or vice versa.
        CommandError
            If a `docker build` or `docker image inspect` command fails.
        """
        # pylint: disable=missing-raises-doc
        if self.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if DockerEnvironment.current() is not None:
            raise OSError("cannot modify an environment from within a Bertrand container")
        if image_args and not image_tag:
            raise OSError("images with non-default arguments must have a tag")
        if image_tag and not image_args:
            raise OSError("tagged images must have non-default arguments")

        # search for up-to-date image with identical args
        image_args = _normalize_args(image_args)
        image, _ = self._build_image(image_tag)
        if image is not None:
            if image.args == image_args:
                return image

            # if image is up to date, but args differ, remove it
            for c in self.tags[image_tag]["containers"].values():
                docker_cmd(["container", "rm", "-f", c], check=False, capture_output=True)
                _container_file(self.root, c).unlink(missing_ok=True)
            docker_cmd(["image", "rm", "-f", image.id], check=False, capture_output=True)
            image.path.unlink(missing_ok=True)
            self.tags.pop(image_tag, None)

        # build new image
        image = Image.__new__(Image)
        image.root = self.root
        image.uuid = uuid.uuid4().hex
        image.version = VERSION
        image.tag = image_tag
        image.id = ""  # corrected after build
        image.created = ""  # corrected after build
        image.args = image_args

        # invoke docker build
        image_name = image.name
        docker_cmd([
            "build",
            *image_args,
            "-t", image_name,
            "-f", str(self.docker_file),
            "--label", f"{LABEL}=1",
            str(image.root),
        ])

        # inspect built image, then correct and write metadata to disk
        try:
            inspect = Image.inspect(image_name)
            if inspect is None:
                raise CommandError(
                    returncode=1,
                    cmd=["docker", "image", "inspect", image_name],
                    stdout="",
                    stderr=f"Failed to create image: {image_name}"
                )
            image.id = inspect["Id"]
            image.created = inspect["Created"]
            atomic_write_text(
                image.path,
                json.dumps({
                    "version": image.version,
                    "tag": image.tag,
                    "id": image.id,
                    "created": image.created,
                    "args": image.args,
                }, indent=2) + "\n"
            )
        except Exception as err:
            docker_cmd(["image", "rm", "-f", image_name], check=False, capture_output=True)
            raise err

        # register image tag
        self.tags |= {image_tag: {"uuid": image.uuid, "containers": {}}}
        return image

    def _build_container(self, image_tag: str, container_tag: str) -> Container | None:
        # search for image
        tag = self.tags.get(image_tag)
        if tag is None:
            return None
        containers = tag["containers"]

        # attempt to load existing image metadata and rebuild using original args if needed
        image, image_args = self._build_image(image_tag)
        if image is None:
            if image_args is None:
                if image_tag:
                    raise KeyError(f"no image found for tag: '{image_tag}'")
                image_args = []  # empty tag implies empty args
            image = self.build_image(image_tag, image_args)
            image_args = image.args

        # attempt to load existing container metadata
        container_id = containers.get(container_tag)
        if container_id is None:
            return None
        container = self._load_container(container_id, containers)
        if container is None:
            return None

        # verify underlying Docker container exists
        inspect = self._inspect_container(container, containers)
        if inspect is None:
            return None

        # if the environment directory has moved, an existing container might have a
        # compatible digest, but the bind mount may be stale.  Docker does not support
        # editing mounts in-place, but we can stop, rm, and recreate the container if
        # needed.  Note that this will remove any data that is not stored in the
        # environment directory (i.e., in the container's root filesystem), but those
        # can be recovered by rebuilding the container in reproducible fashion.
        return self._relocate_container(container, inspect, containers)

    def build_container(
        self,
        image_tag: str,
        container_tag: str,
        container_args: list[str]
    ) -> Container:
        """Incrementally build a Docker container with the given image, tag, and
        arguments, or load an existing one if it is already up-to-date.

        Parameters
        ----------
        image_tag : str
            The image tag to search for.  If no existing image with the same tag is
            found, and the tag is not empty, then an error will be raised.  Otherwise,
            if the tagged image is out of date, it will be rebuilt using its original
            arguments.
        container_tag : str
            The container tag to search for.  If no existing container with the same
            tag is found, or the existing container is out of date, or its arguments
            differ from `container_args`, then a new container will be created and
            associated with this tag.  If a non-empty tag is provided, then
            `container_args` must also be non-empty.
        container_args : list[str]
            The `docker create` arguments to use when creating the container if no
            existing container could be found.  If an existing container is found with
            the same tag, but different arguments, then it will be removed and replaced
            with a new container created with these arguments.  If a non-empty list of
            arguments is provided, then `container_tag` must also be non-empty.

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
            If a `docker create` or `docker container inspect` command fails.
        """
        # pylint: disable=missing-raises-doc
        if self.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if DockerEnvironment.current() is not None:
            raise OSError("cannot modify an environment from within a Bertrand container")
        if container_args and not container_tag:
            raise OSError("containers with non-default arguments must have a tag")
        if container_tag and not container_args:
            raise OSError("tagged containers must have non-default arguments")

        # search for up-to-date container with identical args
        container_args = _normalize_args(container_args)
        container = self._build_container(image_tag, container_tag)
        tag = self.tags[image_tag]
        image_id = tag["uuid"]
        containers = tag["containers"]
        if container is not None:
            if container.args == container_args:
                return container

            # if container is up to date, but args differ, remove it
            docker_cmd(
                ["container", "rm", "-f", container.id],
                check=False,
                capture_output=True
            )
            container.path.unlink(missing_ok=True)
            containers.pop(container_tag, None)

        # build new container
        container = Container.__new__(Container)
        container.root = self.root
        container.uuid = uuid.uuid4().hex
        container.version = VERSION
        container.parent = image_id
        container.tag = container_tag
        container.id = ""  # corrected after create
        container.created = ""  # corrected after create
        container.args = container_args

        # invoke docker create
        container_name = container.name
        docker_cmd([
            "create",
            "--init",
            f"--name={container_name}",
            f"--hostname={container_name}",
            "--label", f"{LABEL}=1",
            "-v", f"{str(self.root)}:{MOUNT}",
            "-e", f"BERTRAND_ENV={container.id}",
            *container_args,
            container_name,
            "sleep", "infinity",
        ])

        # inspect created container, then correct and write metadata to disk
        try:
            inspect = Container.inspect(container_name)
            if inspect is None:
                raise CommandError(
                    returncode=1,
                    cmd=["docker", "container", "inspect", container_name],
                    stdout="",
                    stderr=f"Failed to create container: {container_name}"
                )
            container.id = inspect["Id"]
            container.created = inspect["Created"]
            atomic_write_text(
                container.path,
                json.dumps({
                    "version": container.version,
                    "parent": container.parent,
                    "tag": container.tag,
                    "id": container.id,
                    "created": container.created,
                    "args": container.args,
                }, indent=2) + "\n"
            )
        except Exception as err:
            docker_cmd(["container", "rm", "-f", container_name], check=False, capture_output=True)
            raise err

        # register container tag
        containers |= {container_tag: container.uuid}
        return container


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
    ValueError
        If the environment or container metadata is malformed.
    JSONDecodeError
        If the environment or container metadata is not a valid JSON object.
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
    ValueError
        If the environment or container metadata is malformed.
    JSONDecodeError
        If the environment or container metadata is not a valid JSON object.
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
    ValueError
        If the environment or container metadata is malformed.
    JSONDecodeError
        If the environment or container metadata is not a valid JSON object.
    UnicodeDecodeError
        If the environment or container metadata cannot be decoded.
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

    Raises
    ------
    ValueError
        If the container metadata is malformed.
    JSONDecodeError
        If the container metadata is not a valid JSON object.
    UnicodeDecodeError
        If the container metadata cannot be decoded.
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
        could be found.  Note that if this command is run inside a container, 
    """
    # if we are inside a container, only one option is valid, and verification must
    # be done with respect to the container's digest file rather than invoking docker.
    if in_container():
        # TODO: implement this
        pass

    # otherwise
    else:
        # fall back to below
        pass

    # if None, access via bind mount and read stored path in env.json
    if anchor is None:
        if not in_container():
            return None
        path = Path("/env/")
        env = _read_environment(path)
        if env is None:
            return None
        return path

    # try container id or name first
    if isinstance(anchor, str):
        container = _inspect_container(anchor)
        if container is not None:
            mount = _get_mount_source(container)
            if mount is not None:
                return mount
        try:
            anchor = Path(anchor)  # reinterpret as path
        except Exception:
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
    ValueError
        If the environment or container metadata is malformed.
    JSONDecodeError
        If the environment or container metadata is not a valid JSON object.
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
    ValueError
        If the environment or container metadata is malformed.
    JSONDecodeError
        If the environment or container metadata is not a valid JSON object.
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
    ValueError
        If the environment or container metadata is malformed.
    JSONDecodeError
        If the environment or container metadata is not a valid JSON object.
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


def resume_container(env_root: Path, tag: str, *, argv: list[str]) -> DockerContainer:
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
    ValueError
        If the environment or container metadata is malformed.
    JSONDecodeError
        If the environment or container metadata is not a valid JSON object.
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


def delete_container(env_root: Path, tag: str, *, argv: list[str]) -> None:
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
    ValueError
        If the environment or container metadata is malformed.
    JSONDecodeError
        If the environment or container metadata is not a valid JSON object.
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
