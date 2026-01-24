"""Install Docker Engine and pull container images."""
from __future__ import annotations

import json
import os
import platform
import re
import shlex
import shutil
import time
import uuid

from datetime import datetime, timezone
from pathlib import Path
from resource import getpagesize
from types import TracebackType
from typing import Iterable, Literal, TypedDict, overload

from psutil import pid_exists

from .docker_engine import docker_cmd, docker_exec
from .run import HIDDEN, CommandError, atomic_write_text, confirm, up_to_date
from .version import __version__

#pylint: disable=redefined-builtin, redefined-outer-name, broad-except


VERSION: int = 1
MOUNT: str = "/env"
TIMEOUT: int = 30
EXCLUDE: str = HIDDEN


def _bertrand_dir(env_root: Path) -> Path:
    return env_root / ".bertrand"


def _env_file(env_root: Path) -> Path:
    return _bertrand_dir(env_root) / "env.json"


def _image_dir(env_root: Path) -> Path:
    return _bertrand_dir(env_root) / "images"


def _image_file(env_root: Path, id: str) -> Path:
    return _image_dir(env_root) / f"{id}.json"


def _container_dir(env_root: Path) -> Path:
    return _bertrand_dir(env_root) / "containers"


def _container_file(env_root: Path, id: str) -> Path:
    return _container_dir(env_root) / f"{id}.json"


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


def _validate_uuid(data: dict[str, object]) -> str:
    id = _validate_id(data)
    try:
        uuid.UUID(id)
    except Exception as err:
        raise ValueError(f"'id' must be a valid UUID: {id}") from err
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
    if not isinstance(args, list) or not all(isinstance(x, str) and x for x in args):
        raise ValueError(f"missing or invalid 'args' field: {args}")
    return _normalize_args(args)


def _validate_entrypoint(data: dict[str, object]) -> list[str]:
    entrypoint = data.get("entrypoint")
    if not isinstance(entrypoint, list) or not all(isinstance(x, str) and x for x in entrypoint):
        raise ValueError(f"missing or invalid 'entrypoint' field: {entrypoint}")
    return _normalize_args(entrypoint)


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
    id : str
        The Docker ID for this image.  This is equivalent to the file name of the image
        metadata in `image_dir`.

    Raises
    ------
    ValueError
        If the image metadata is malformed.

    Attributes
    ----------
    root : Path
        An absolute root path to the environment directory.  This is not stored in the
        on-disk JSON in order to allow relocation of the environment directory.
    version : int
        The version number for backwards compatibility.
    tag : str
        A human-readable tag identifying this image within the environment.
    id : str
        The unique Docker image ID.  This is equivalent to the metadata file name in
        `image_dir`.
    uuid : str
        The unique id used to name this image.  This is not used anywhere else, but
        is retained so that the name can be reconstructed exactly.
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
    version: int
    tag: str
    id: str
    uuid: str
    created: str
    args: list[str]

    def __init__(self, root: Path, id: str) -> None:
        self.root = root.expanduser().resolve()
        path = _image_file(root, id)
        if not path.exists():
            raise ValueError(f"image metadata not found: {path}")
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            if not isinstance(data, dict):
                raise ValueError("image metadata must be a JSON object")
            self.version = _validate_version(data)
            self.tag = _validate_tag(data)
            self.id = _validate_id(data)
            if self.id != id:
                raise ValueError(f"image ID mismatch: expected {id}, found {self.id}")
            self.uuid = _validate_uuid(data)
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
            the environment root, this image's `tag`, and a shortened `uuid` to
            disambiguate (e.g. `<myproject>.<image_tag>.<uuid>` or `<myproject>.<uuid>`).
        """
        parts = [_sanitize_name(self.root.name)]
        if self.tag:
            parts.append(self.tag)
        parts.append(self.uuid[:13])
        return ".".join(parts)

    @property
    def path(self) -> Path:
        """Return the path to this image's metadata file on disk.

        Returns
        -------
        Path
            The path to this image's metadata file.
        """
        return _image_file(self.root, self.id)

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
    id : str
        The Docker ID for this container.  This is equivalent to the file name of the
        container metadata in `container_dir`.

    Raises
    ------
    ValueError
        If the container metadata is malformed.

    Attributes
    ----------
    root : Path
        An absolute root path to the environment directory.  This is not stored in the
        on-disk JSON in order to allow relocation of the environment directory.
    version : int
        The version number for backwards compatibility.
    parent : str
        A unique ID for the image metadata file from which this container was created.
        This is equivalent to the file name of the image metadata file in `image_dir`.
    tag : str
        A human-readable tag identifying this container within the parent image.
    id : str
        The unique Docker container ID.  This is equivalent to the metadata file name
        in `container_dir`.
    uuid : str
        The unique id used to name this container.  This is not used anywhere else, but
        is retained so that the name can be reconstructed exactly.
    created : str
        The ISO timestamp when the container was created.
    args : list[str]
        The original `docker create` arguments used to create the container.
    """
    class State(TypedDict, total=False):
        """Type hint for docker container state information."""
        Running: bool
        Paused: bool
        Restarting: bool
        OOMKilled: bool
        Dead: bool

    class Mounts(TypedDict, total=False):
        """Type hint for docker container mount information."""
        Type: Literal["bind", "volume", "tmpfs", "npipe"]
        Source: str
        Destination: str
        RW: bool
        Propagation: Literal["shared", "slave", "private", "rshared", "rslave", "rprivate"]

    class Inspect(TypedDict, total=False):
        """Type hint for docker container inspect output."""
        Id: str
        Created: str
        State: Container.State
        Image: str
        Mounts: list[Container.Mounts]

    root: Path
    version: int
    parent: str
    tag: str
    id: str
    uuid: str
    created: str
    args: list[str]
    entrypoint: list[str]

    def __init__(self, root: Path, id: str) -> None:
        self.root = root.expanduser().resolve()
        path = _container_file(root, id)
        if not path.exists():
            raise ValueError(f"container metadata not found: {path}")
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            if not isinstance(data, dict):
                raise ValueError("container metadata must be a JSON object")
            self.version = _validate_version(data)
            self.parent = _validate_id(data)
            if not _image_file(self.root, self.parent).exists():
                raise ValueError(f"parent image metadata not found: {self.parent}")
            self.tag = _validate_tag(data)
            self.id = _validate_id(data)
            if self.id != id:
                raise ValueError(f"container ID mismatch: expected {id}, found {self.id}")
            self.uuid = _validate_uuid(data)
            self.created = _validate_created(data)
            self.args = _validate_args(data)
            self.entrypoint = _validate_entrypoint(data)
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
            a shortened `uuid` (e.g. `<myproject>.<image_tag>.<container_tag>.<uuid>`, 
            `<myproject>.<image_tag>.<uuid>`, or `<myproject>.<uuid>`).
        """
        parts = [_sanitize_name(self.root.name)]
        image = self.image
        if image.tag:
            parts.append(image.tag)
        if self.tag:
            parts.append(self.tag)
        parts.append(self.uuid[:13])
        return ".".join(parts)

    @property
    def path(self) -> Path:
        """Return the path to this container's metadata file on disk.

        Returns
        -------
        Path
            The path to this container's metadata file.
        """
        return _container_file(self.root, self.id)

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
        state = inspect["State"]
        if state["Running"] or state["Restarting"]:
            return
        if state["Paused"]:
            docker_cmd(["container", "unpause", inspect["Id"]], check=False)
        else:
            docker_cmd(["container", "start", inspect["Id"]], check=False)

    @staticmethod
    def stop(inspect: Container.Inspect, timeout: int = TIMEOUT) -> None:
        """Stop a Docker container if it is currently running.

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
            docker_cmd(
                ["container", "stop", inspect["Id"], "-t", str(timeout)],
                check=False
            )

    @staticmethod
    def pause(inspect: Container.Inspect) -> None:
        """Pause a Docker container if it is currently running.

        Parameters
        ----------
        inspect : Container.Inspect
            The output of `Container.inspect()` for the container to pause.
        """
        state = inspect["State"]
        if state["Running"] or state["Restarting"]:
            docker_cmd(["container", "pause", inspect["Id"]], check=False)

    @staticmethod
    def resume(inspect: Container.Inspect) -> None:
        """Resume a Docker container if it is currently paused.

        Parameters
        ----------
        inspect : Container.Inspect
            The output of `Container.inspect()` for the container to resume.
        """
        state = inspect["State"]
        if state["Paused"]:
            docker_cmd(["container", "unpause", inspect["Id"]], check=False)

    @staticmethod
    def restart(inspect: Container.Inspect, timeout: int = TIMEOUT) -> None:
        """Restart a Docker container.

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
            docker_cmd(
                ["container", "restart", inspect["Id"], "-t", str(timeout)],
                check=False
            )


class Environment:
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
    tags : dict[str, Environment.Tag]
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
        id: str  # Docker image ID and filename
        containers: dict[str, str]  # container tag -> Docker container ID + filename

    root: Path
    timeout: int
    depth: int
    version: int
    tags: dict[str, Environment.Tag]
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
        out: dict[str, Environment.Tag] = {}
        for image_tag, image in tags.items():
            if not isinstance(image_tag, str):
                raise ValueError(f"invalid image tag: {image_tag}")
            if not isinstance(image, dict):
                raise ValueError(f"invalid data for image tag '{image_tag}': {image}")

            # validate id
            image_id = image["id"]
            if not isinstance(image_id, str):
                raise ValueError(f"invalid id for image tag '{image_tag}': {image_id}")
            if not _image_file(self.root, image_id).exists():
                raise ValueError(
                    f"missing image metadata file for id '{image_id}' in image tag '{image_tag}'"
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
                        f"invalid container id '{container_id}' for container tag "
                        f"'{container_tag}' in image tag '{image_tag}'"
                    )
                if not _container_file(self.root, container_id).exists():
                    raise ValueError(
                        f"missing container metadata file for id '{container_id}' with "
                        f"container tag '{container_tag}' in image tag '{image_tag}'"
                    )
                validated[container_tag] = container_id
            out[image_tag] = {"id": image_id, "containers": validated}

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

    def __enter__(self) -> Environment:
        # allow nested context managers without deadlocking
        self.depth += 1
        if self.depth > 1:
            return self

        # acquire lock without deadlocking
        lock_dir = self.bertrand_dir / ".lock"
        lock_file = lock_dir / "owner.json"
        start = time.time()
        stale_after = max(self.timeout * 4, 120)  # lock considered stale after 2 min by default
        while True:
            try:
                lock_dir.mkdir(parents=True)
                atomic_write_text(lock_file, json.dumps({
                    "version": VERSION,
                    "timestamp": datetime.now(timezone.utc).isoformat(),
                    "host": platform.node(),
                    "pid": os.getpid(),
                    "ppid": os.getppid(),
                    "token": uuid.uuid4().hex,
                }, indent=2) + "\n")
                break
            except FileExistsError as err:
                # another process holds the lock - check if it's stale
                now = time.time()
                try:
                    owner = json.loads(lock_file.read_text(encoding="utf-8"))
                    if not isinstance(owner, dict):
                        owner = {}
                except Exception:
                    owner = {}

                # check whether owning process is still alive
                pid = owner.get("pid")
                if pid and isinstance(pid, int) and not pid_exists(pid):
                    shutil.rmtree(lock_dir, ignore_errors=True)
                    continue

                # check age of lock
                timestamp = owner.get("timestamp")
                if timestamp and isinstance(timestamp, str):
                    try:
                        age = (
                            datetime.now(timezone.utc) -
                            datetime.fromisoformat(timestamp).replace(tzinfo=timezone.utc)
                        ).total_seconds()
                        if age > stale_after:
                            shutil.rmtree(lock_dir, ignore_errors=True)
                            continue
                    except Exception:
                        pass

                # error on timeout
                if (now - start) > self.timeout:
                    detail = f"\nlock owner: {json.dumps(owner, indent=2)})" if owner else ""
                    raise TimeoutError(
                        f"could not acquire environment lock within {self.timeout} seconds{detail}"
                    ) from err

                # wait and retry
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
        shutil.rmtree(self.bertrand_dir / ".lock", ignore_errors=True)

    def __hash__(self) -> int:
        return hash(self.root)

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, Environment):
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
    def current() -> Environment | None:
        """Detect whether the current process is running inside a Bertrand Docker
        container.

        Returns
        -------
        Environment | None
            An `Environment` metadata object with the proper mount path if invoked
            within a Bertrand Docker container, or None otherwise.  Note that the
            result is disengaged, and must be acquired as a context manager before it
            can be used to access or modify the environment.
        """
        if "BERTRAND_CONTAINER" not in os.environ or "BERTRAND_IMAGE" not in os.environ:
            return None
        return Environment(root=Path(MOUNT))

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
            return Image(self.root, tag["id"])

        # search for container
        containers = tag["containers"]
        container_id = containers.get(container_tag)
        if container_id is None:
            return None
        return Container(self.root, container_id)

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
            A list of fully qualified image or container tags within this environment,
            depending on the provided arguments.  An empty list will be returned if no
            matching tags could be found.

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
            return [f"{self.root}:{k}" for k in self.tags.keys()]

        # list all containers in an image
        tag = self.tags.get(image)
        if tag is None:
            return []
        containers = tag["containers"]
        if container is None:
            return [f"{self.root}:{image}:{k}" for k in containers.keys()]

        # check for specific container in an image
        if container in containers:
            return [f"{self.root}:{image}:{container}"]
        return []

    def _rm_all(self) -> None:
        # remove all containers associated with this environment
        out = docker_cmd([
            "container",
            "ls",
            "-a",
            "--no-trunc",
            "--filter", f"label=BERTRAND_ENV=\"{self.root}\"",
            "--format", "{{.ID}}"
        ], check=False, capture_output=True)
        if out.returncode == 0:
            docker_cmd(["container", "rm", "-f", " ".join(out.stdout.splitlines())], check=False)

        # remove all images associated with this environment
        out = docker_cmd([
            "image",
            "ls",
            "-a",
            "--no-trunc",
            "--filter", f"label=BERTRAND_ENV=\"{self.root}\"",
            "--format", "{{.ID}}"
        ], check=False, capture_output=True)
        if out.returncode == 0:
            docker_cmd(["image", "rm", "-f", " ".join(out.stdout.splitlines())], check=False)

        # remove all metadata files (including orphans)
        shutil.rmtree(self.image_dir, ignore_errors=True)
        shutil.rmtree(self.container_dir, ignore_errors=True)
        self.tags.clear()

    def _rm_image(self, image_tag: str) -> None:
        tag = self.tags.get(image_tag)
        if tag is None:
            return
        image_id = tag["id"]

        # remove all descendant containers
        out = docker_cmd([
            "container",
            "ls",
            "-a",
            "--no-trunc",
            "--filter", f"ancestor={image_id}",
            "--format", "{{.ID}}"
        ], check=False, capture_output=True)
        if out.returncode == 0:
            docker_cmd(["container", "rm", "-f", " ".join(out.stdout.splitlines())], check=False)

        # remove any container files associated with the image
        for path in self.container_dir.iterdir():
            try:
                container = Container(self.root, path.name)
            except Exception:
                path.unlink(missing_ok=True)
                continue
            if container.parent == image_id:
                path.unlink(missing_ok=True)

        # remove image
        docker_cmd(["image", "rm", "-f", image_id], check=False)
        _image_file(self.root, image_id).unlink(missing_ok=True)
        self.tags.pop(image_tag)

    def _rm_container(self, image_tag: str, container_tag: str) -> None:
        tag = self.tags.get(image_tag)
        if tag is None:
            return
        containers = tag["containers"]
        container_id = containers.get(container_tag)
        if container_id is None:
            return

        # remove the container
        docker_cmd(["container", "rm", "-f", container_id], check=False)
        _container_file(self.root, container_id).unlink(missing_ok=True)
        containers.pop(container_tag, None)

    def rm(self, image_tag: str | None = None, container_tag: str | None = None) -> None:
        """Delete images and/or containers from this environment.

        Parameters
        ----------
        image_tag : str | None, optional
            An optional image tag to remove.  If None (the default), all images and
            containers in this environment will be removed.
        container_tag : str | None, optional
            An optional container tag to remove.  If None (the default), all containers
            in the indicated image (or whole environment if `image_tag` is None) will
            be removed.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container.
        TypeError
            If `image_tag` is None but `container_tag` is not.
        """
        if self.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke Docker from within a Bertrand container")

        # delete all images
        if image_tag is None:
            if container_tag is not None:
                raise TypeError("cannot specify a container when removing all images")
            self._rm_all()
            return

        # delete all containers in an image
        if container_tag is None:
            self._rm_image(image_tag)
            return

        # delete specific container in an image
        self._rm_container(image_tag, container_tag)

    def _stale_image(self, image_id: str) -> bool:
        try:
            image = Image(self.root, image_id)
            return Image.inspect(image.id) is None
        except Exception:
            return True

    def _stale_container(self, container_id: str) -> bool:
        try:
            container = Container(self.root, container_id)
            container_inspect = Container.inspect(container.id)
            if container_inspect is None:
                return True
            return self._relocate_container(container, container_inspect) is None
        except Exception:
            return True

    def _prune_all(self) -> None:
        # identify all stale images and containers
        defer: list[tuple[str, str | None]] = []
        for image_tag, tag in self.tags.items():
            image_id = tag["id"]
            if self._stale_image(image_id):
                defer.append((image_tag, None))
                continue

            # check for individual stale containers within the image
            defer.extend(
                (image_tag, container_tag)
                for container_tag, container_id in tag["containers"].items()
                if self._stale_container(container_id)
            )

        # delete stale images and containers
        for i, c in defer:
            if c is None:
                self._rm_image(i)
            else:
                self._rm_container(i, c)

        # scan for orphaned image metadata that aren't present in the tags mapping
        valid_image_ids = {tag["id"] for tag in self.tags.values()}
        for path in self.image_dir.iterdir():
            if path.name not in valid_image_ids:
                docker_cmd(["image", "rm", "-f", path.name], check=False)
                path.unlink(missing_ok=True)

        # scan for orphaned container metadata that aren't present in the tags mapping
        valid_container_ids = {
            container_id
            for tag in self.tags.values()
            for container_id in tag["containers"].values()
        }
        for path in self.container_dir.iterdir():
            if path.name not in valid_container_ids:
                docker_cmd(["container", "rm", "-f", path.name], check=False)
                path.unlink(missing_ok=True)

    def _prune_image(self, image_tag: str) -> None:
        # prune image
        tag = self.tags.get(image_tag)
        if tag is None:
            return
        image_id = tag["id"]
        if self._stale_image(image_id):
            self._rm_image(image_tag)
            return

        # prune stale containers within the image
        defer = [
            container_tag
            for container_tag, container_id in tag["containers"].items()
            if self._stale_container(container_id)
        ]
        for c in defer:
            self._rm_container(image_tag, c)

    def _prune_container(self, image_tag: str, container_tag: str) -> None:
        # prune image
        tag = self.tags.get(image_tag)
        if tag is None:
            return
        image_id = tag["id"]
        if self._stale_image(image_id):
            self._rm_image(image_tag)
            return

        # prune container
        container_id = tag["containers"].get(container_tag)
        if container_id is None:
            return
        if self._stale_container(container_id):
            self._rm_container(image_tag, container_tag)

    def prune(self, image_tag: str | None = None, container_tag: str | None = None) -> None:
        """Delete stale images and/or containers from this environment.

        Parameters
        ----------
        image_tag : str | None, optional
            An optional image tag to prune.  If None (the default), all images and
            containers in this environment will be pruned.
        container_tag : str | None, optional
            An optional container tag to prune.  If None (the default), all containers
            in the indicated image (or whole environment if `image_tag` is None) will
            be pruned.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container.
        TypeError
            If `image_tag` is None but `container_tag` is not.
        """
        if self.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke Docker from within a Bertrand container")

        # prune all images
        if image_tag is None:
            if container_tag is not None:
                raise TypeError("cannot specify a container when pruning all images")
            self._prune_all()
            return

        # prune all containers in an image
        if container_tag is None:
            self._prune_image(image_tag)
            return

        # prune specific container in an image
        self._prune_container(image_tag, container_tag)

    # TODO: maybe I should allow tags to reference default images/containers, but not
    # for default tags to represent non-default arguments.

    def _build(self, image_tag: str) -> tuple[Image | None, list[str] | None]:
        # search for image
        tag = self.tags.get(image_tag)
        if tag is None:
            return None, None

        # attempt to load existing image metadata
        try:
            image = Image(self.root, tag["id"])
            args = image.args
        except Exception:
            return None, None

        # verify underlying Docker image exists
        inspect = Image.inspect(image.id)
        if inspect is None:
            return None, args

        # check for incremental rebuild
        created = datetime.fromisoformat(image.created).replace(tzinfo=timezone.utc)
        if not up_to_date(self.root, created, self.exclude):
            return None, args

        return image, args

    @overload
    def build(
        self,
        image_tag: str,
        image_args: list[str] | None = ...
    ) -> Image: ...
    @overload
    def build(
        self,
        image_tag: None = ...,
        image_args: None = ...
    ) -> Environment: ...
    def build(
        self,
        image_tag: str | None = None,
        image_args: list[str] | None = None
    ) -> Environment | Image:
        """Incrementally build a Docker image with the given tag and arguments, or load
        an existing one if it is already up-to-date.

        Parameters
        ----------
        image_tag : str | None, optional
            The image tag to search for.  If no existing image with the same tag is
            found, or the existing image is out of date, or its arguments differ from
            `image_args`, then a new image will be built and associated with this tag.
            If a non-empty tag is provided, then `image_args` must also be non-empty
            or None.  If None (the default), all images in the environment will be
            built incrementally using their existing arguments.
        image_args : list[str] | None, optional
            The `docker build` arguments to use when building the image if no existing
            image could be found.  If an existing image is found with the same tag, but
            different arguments, then it will be removed and rebuilt with these
            arguments.  If a non-empty list of arguments is provided, then `image_tag`
            must also be non-empty.  If None (the default), the existing arguments for
            the image will be reused if possible, or an error will be raised if no
            existing image could be found.

        Returns
        -------
        Environment | Image
            The resulting image metadata, which may be a reference to an existing image
            or a newly built one.  If `image_tag` is None, then the environment itself
            will be returned, after building all images.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container, or if non-empty
            `image_args` are provided with an empty `image_tag`, or vice versa.
        TypeError
            If `image_tag` is None but `image_args` is not None, or if non-empty
            `image_args` are provided with an empty `image_tag`, or vice versa.
        KeyError
            If no existing image could be found for the given `image_tag`, and
            `image_args` is None.
        CommandError
            If a `docker build` or `docker image inspect` command fails.
        """
        # pylint: disable=missing-raises-doc
        if self.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke Docker from within a Bertrand container")
        if image_args and not image_tag:
            raise TypeError("images with non-default arguments must have a tag")
        if image_tag and not image_args:
            raise TypeError("tagged images must have non-default arguments")

        # if no tag is provided, build all images
        if image_tag is None:
            if image_args is not None:
                raise TypeError("cannot provide arguments when building all images")
            for t in list(self.tags.keys()):
                self.build(t)
            return self

        # search for up-to-date image with identical args
        image, original_args = self._build(image_tag)
        if image_args is None:
            if original_args is None:
                if image_tag:
                    raise KeyError(f"no image found for tag: '{image_tag}'")
                original_args = []  # empty tag implies empty args
            image_args = original_args
        else:
            image_args = _normalize_args(image_args)
        if image is not None:
            if image.args == image_args:
                return image
            image = None  # force rebuild

        # delete stale image if not found
        if image is None:
            self._rm_image(image_tag)

        # build new image
        image = Image.__new__(Image)
        image.root = self.root
        image.version = VERSION
        image.tag = image_tag
        image.id = ""  # corrected after build
        image.uuid = uuid.uuid4().hex
        image.created = ""  # corrected after build
        image.args = image_args

        # invoke docker build
        image_name = image.name
        docker_cmd([
            "build",
            *image_args,
            "-t", image_name,
            "-f", str(self.docker_file),
            "--label", "BERTRAND=1",
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
            prev, _, next = inspect["Id"].partition(":")
            image.id = next if next else prev
            image.created = inspect["Created"]
            atomic_write_text(
                image.path,
                json.dumps({
                    "version": image.version,
                    "tag": image.tag,
                    "id": image.id,
                    "uuid": image.uuid,
                    "created": image.created,
                    "args": image.args,
                }, indent=2) + "\n"
            )
        except Exception as err:
            docker_cmd(["image", "rm", "-f", image_name], check=False, capture_output=True)
            raise err

        # register image tag
        self.tags |= {image_tag: {"id": image.id, "containers": {}}}
        return image

    def _relocate_container(
        self,
        container: Container,
        inspect: Container.Inspect
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
            return None
        return container

    def _start(
        self,
        image_tag: str,
        container_tag: str
    ) -> tuple[Container | None, list[str] | None]:
        # search for image
        tag = self.tags.get(image_tag)
        if tag is None:
            return None, None

        # attempt to load existing container metadata to get original args
        containers = tag["containers"]
        container_id = containers.get(container_tag)
        if container_id is None:
            return None, None
        try:
            container = Container(self.root, container_id)
            args = container.args
        except Exception:
            return None, None

        # ensure image is up-to-date
        self.build(image_tag)
        containers = self.tags[image_tag]["containers"]
        container_id = containers.get(container_tag)
        if container_id is None:
            return None, args  # image rebuild may have removed old container

        # verify underlying Docker container exists
        inspect = Container.inspect(container.id)
        if inspect is None:
            return None, args

        # if the environment directory has moved, an existing container might have a
        # compatible digest, but the bind mount may be stale.  Docker does not support
        # editing mounts in-place, but we can stop, rm, and recreate the container if
        # needed.  Note that this will remove any data that is not stored in the
        # environment directory (i.e., in the container's root filesystem), but those
        # can be recovered by rebuilding the container in reproducible fashion.
        _container = self._relocate_container(container, inspect)
        if _container is None:
            return None, args

        # start the container
        Container.start(inspect)
        return _container, args

    def _start_all(self) -> Environment:
        # because start() may rebuild images and remove containers, we need to begin by
        # collecting all image/container tags + container args for posterity
        defer: list[tuple[str, str, list[str]]] = []
        for image_tag, tag in self.tags.items():
            for container_tag, container_id in tag["containers"].items():
                try:
                    container = Container(self.root, container_id)
                    defer.append((image_tag, container_tag, container.args))
                except Exception:
                    self._rm_container(image_tag, container_tag)

        # start all containers
        for image_tag, container_tag, container_args in defer:
            self.start(image_tag, container_tag, container_args)
        return self

    def _start_image(self, image_tag: str) -> Image:
        tag = self.tags.get(image_tag)
        if tag is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")

        # because start() may rebuild images and remove containers, we need to begin by
        # collecting all container tags + container args for posterity
        defer: list[tuple[str, list[str]]] = []
        for container_tag, container_id in tag["containers"].items():
            try:
                container = Container(self.root, container_id)
                defer.append((container_tag, container.args))
            except Exception:
                self._rm_container(image_tag, container_tag)

        # start all containers in the specified image
        for container_tag, container_args in defer:
            self.start(image_tag, container_tag, container_args)

        # the image id may have changed during rebuilds
        return Image(self.root, self.tags[image_tag]["id"])

    @overload
    def start(
        self,
        image_tag: str,
        container_tag: str,
        container_args: list[str] | None = ...
    ) -> Container: ...
    @overload
    def start(
        self,
        image_tag: str,
        container_tag: None = ...,
        container_args: None = ...
    ) -> Image: ...
    @overload
    def start(
        self,
        image_tag: None = ...,
        container_tag: None = ...,
        container_args: None = ...
    ) -> Environment: ...
    def start(
        self,
        image_tag: str | None = None,
        container_tag: str | None = None,
        container_args: list[str] | None = None
    ) -> Environment | Image | Container:
        """Incrementally build a Docker container with the given image, tag, and
        arguments, or load an existing one if it is already up-to-date, and then start
        it.

        Parameters
        ----------
        image_tag : str | None, optional
            The image tag to search for.  If no existing image with the same tag is
            found, and the tag is not empty, then an error will be raised.  Otherwise,
            if the tagged image is out of date, it will be rebuilt using its original
            arguments.  If None (the default), all images in the environment will be
            started.
        container_tag : str | None, optional
            The container tag to search for.  If no existing container with the same
            tag is found, or the existing container is out of date, or its arguments
            differ from `container_args`, then a new container will be created and
            associated with this tag.  If a non-empty tag is provided, then
            `container_args` must also be non-empty or None.  If None (the default),
            all containers in the indicated image will be started.
        container_args : list[str] | None, optional
            The `docker create` arguments to use when creating the container if no
            existing container could be found.  If an existing container is found with
            the same tag, but different arguments, then it will be removed and rebuilt
            with these arguments.  If a non-empty list of arguments is provided, then
            `container_tag` must also be non-empty.  If None (the default), the
            existing arguments for the container will be reused if possible, or an
            error will be raised if no existing container could be found.

        Returns
        -------
        Environment | Image | Container
            The resulting container metadata, which may be a reference to an existing
            container or a newly created one.  If `image_tag` is None, then the
            environment itself will be returned, after starting all containers.  If
            `container_tag` is None, then the parent image will be returned, after
            starting all containers in that image.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container.
        TypeError
            If `image_tag` or `container_tag` is None but any of the subsequent
            arguments are not None, or if non-empty `container_args` are provided with
            an empty `container_tag`, or vice versa.
        KeyError
            If an image tag is provided but could not be found, or if a container tag
            is provided and `container_args` is None, but the container could not be
            found.
        CommandError
            If a `docker build`, `docker image inspect`, `docker create`,
            `docker container inspect`, or `docker start` command fails.
        """
        # pylint: disable=missing-raises-doc
        if self.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke Docker from within a Bertrand container")
        if container_args and not container_tag:
            raise TypeError("containers with non-default arguments must have a tag")
        if container_tag and not container_args:
            raise TypeError("tagged containers must have non-default arguments")

        # start all containers in this environment
        if image_tag is None:
            if container_tag is not None or container_args is not None:
                raise TypeError("cannot specify a tag or arguments when starting all containers")
            return self._start_all()

        # start all containers in the specified image
        if container_tag is None:
            if container_args is not None:
                raise TypeError("cannot specify arguments when starting all containers in an image")
            return self._start_image(image_tag)

        # search for up-to-date container with identical args
        container, original_args = self._start(image_tag, container_tag)
        tag = self.tags[image_tag]
        image_id = tag["id"]
        containers = tag["containers"]
        if container_args is None:
            if original_args is None:
                if container_tag:
                    raise KeyError(f"no container found for tag: '{container_tag}'")
                original_args = []  # empty tag implies empty args
            container_args = original_args  # reuse existing args
        else:
            container_args = _normalize_args(container_args)  # define new args
        if container is not None:
            if container.args == container_args:
                return container
            container = None  # force rebuild

        # delete stale container if not found
        if container is None:
            self._rm_container(image_tag, container_tag)

        # build new container
        container = Container.__new__(Container)
        container.root = self.root
        container.version = VERSION
        container.parent = image_id
        container.tag = container_tag
        container.id = ""  # corrected after create
        container.uuid = uuid.uuid4().hex
        container.created = ""  # corrected after create
        container.args = container_args
        container.entrypoint = []  # default entrypoint

        # invoke docker create
        container_name = container.name
        docker_cmd([
            "create",
            "--init",
            f"--name={container_name}",
            f"--hostname={container_name}",
            "--label", "BERTRAND=1",
            "--label", f"BERTRAND_ENV=\"{self.root}\"",
            "--label", f"BERTRAND_IMAGE={image_tag}",
            "--label", f"BERTRAND_CONTAINER={container_tag}",
            "-v", f"{str(self.root)}:{MOUNT}",
            "-e", f"BERTRAND_ENV=\"{self.root}\"",
            "-e", f"BERTRAND_IMAGE={image_tag}",
            "-e", f"BERTRAND_CONTAINER={container_tag}",
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
            prev, _, next = inspect["Id"].partition(":")
            container.id = next if next else prev
            container.created = inspect["Created"]
            # write container metadata to disk
            atomic_write_text(
                container.path,
                json.dumps({
                    "version": container.version,
                    "parent": container.parent,
                    "tag": container.tag,
                    "id": container.id,
                    "uuid": container.uuid,
                    "created": container.created,
                    "args": container.args,
                    "entrypoint": container.entrypoint,
                }, indent=2) + "\n"
            )

            # start the container
            docker_cmd(["container", "start", container.id])

        except Exception as err:
            docker_cmd(["container", "rm", "-f", container_name], check=False, capture_output=True)
            raise err

        # register container tag
        containers |= {container_tag: container.id}
        return container

    def enter(
        self,
        image_tag: str,
        container_tag: str,
        container_args: list[str] | None = None
    ) -> Container:
        """Replace the current process with an interactive shell inside the specified
        Docker container, starting or rebuilding it as necessary.

        Parameters
        ----------
        image_tag : str
            The image tag to search for.  If no existing image with the same tag is
            found, and the tag is not empty, then an error will be raised.  Otherwise
            if the tagged image is out of date, it will be rebuilt using its original
            arguments.
        container_tag : str
            The container tag to search for.  If no existing container with the same
            tag is found, or the existing container is out of date, or its arguments
            differ from `container_args`, then a new container will be created and
            associated with this tag.  If a non-empty tag is provided, then
            `container_args` must also be non-empty.
        container_args : list[str] | None, optional
            The `docker create` arguments to use when creating the container if no
            existing container could be found.  If an existing container is found with
            the same tag, but different arguments, then it will be removed and rebuilt
            with these arguments.  If a non-empty list of arguments is provided, then
            `container_tag` must also be non-empty.  If None (the default), the
            existing arguments for the container will be reused if possible, or an
            error will be raised if no existing container could be found.

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
            If a `docker build`, `docker image inspect`, `docker create`,
            `docker container inspect`, or `docker exec` command fails.
        """
        # start or rebuild the container, then replace current process with an inner shell
        container = self.start(image_tag, container_tag, container_args)
        docker_exec([
            "exec",
            "-it",
            "-w", MOUNT,
            container.id,
            *self.shell
        ])
        return container

    def _run_all(self, *, argv: list[str] | None) -> Environment:
        self._start_all()
        for image_tag, tag in self.tags.items():
            containers = tag["containers"]
            for container_tag in containers.keys():
                self.run(image_tag, container_tag, argv)
        return self

    def _run_image(self, image_tag: str, *, argv: list[str] | None) -> Image:
        self._start_image(image_tag)
        for container_tag in self.tags[image_tag]["containers"].keys():
            self.run(image_tag, container_tag, argv)
        return Image(self.root, self.tags[image_tag]["id"])

    @overload
    def run(
        self,
        image_tag: str,
        container_tag: str,
        argv: list[str] | None = ...
    ) -> Container: ...
    @overload
    def run(
        self,
        image_tag: str,
        container_tag: None = ...,
        argv: list[str] | None = ...
    ) -> Image: ...
    @overload
    def run(
        self,
        image_tag: None = ...,
        container_tag: None = ...,
        argv: list[str] | None = ...
    ) -> Environment: ...
    def run(
        self,
        image_tag: str | None = None,
        container_tag: str | None = None,
        argv: list[str] | None = None
    ) -> Environment | Image | Container:
        """Run a command inside the specified Docker container, starting or rebuilding
        it as necessary.

        Parameters
        ----------
        image_tag : str | None
            The image tag to search for.  If no existing image with the same tag is
            found, and the tag is not empty, then an error will be raised.  Otherwise,
            if the tagged image is out of date, it will be rebuilt using its original
            arguments.  If None (the default), all images in the environment will be
            run with the same arguments.
        container_tag : str | None
            The container tag to search for.  If no existing container with the same
            tag is found, and the tag is not empty, then an error will be raised.
            Otherwise, if the tagged container is out of date, it will be rebuilt using
            its original arguments.  If None (the default), all containers in the
            indicated image will be run with the same arguments.
        argv : list[str] | None
            The arguments to pass to the container's entrypoint when running the
            command.  The entrypoint itself is prepended automatically, and if no
            entrypoint is defined, then the command will be run directly.  If None
            (the default), then no additional arguments will be passed.

        Returns
        -------
        Environment | Image | Container
            The resulting container metadata, which may be a reference to an existing
            container or a newly created one.  If `image_tag` is None, then the
            environment itself will be returned, after scheduling all containers.  If
            `container_tag` is None, then the parent image will be returned, after
            scheduling all containers in that image.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container.
        TypeError
            If `image_tag` is None but `container_tag` is not.
        KeyError
            If an image or container tag is provided but could not be found.
        CommandError
            If a `docker build`, `docker image inspect`, `docker create`,
            `docker container inspect`, or `docker exec` command fails, or if the
            container does not have a valid entrypoint.
        """
        if self.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke Docker from within a Bertrand container")

        # run all containers in this environment
        if image_tag is None:
            if container_tag is not None:
                raise TypeError("cannot specify a container when image tag is None")
            return self._run_all(argv=argv)

        # run all containers in the specified image
        if container_tag is None:
            return self._run_image(image_tag, argv=argv)

        # search for up-to-date container or start a new one if the container tag is empty
        container = self.start(image_tag, container_tag)
        if argv is None:
            argv = []

        # ensure container has a valid entrypoint
        if not container.entrypoint:
            raise CommandError(
                returncode=1,
                cmd=["bertrand", "run", f"{self.root}:{image_tag}:{container_tag}", *argv],
                stdout="",
                stderr=
                    "Cannot run command: container has no entrypoint defined.  Either "
                    "write a '__main__.py' file or include a C++ source file that "
                    "implements an 'int main()' function."
            )

        # run the command within the container context
        docker_cmd([
            "exec",
            "-it",
            "-w", MOUNT,
            container.id,
            *container.entrypoint,
            *argv
        ])
        return container

    @overload
    def stop(self, image_tag: str, container_tag: str) -> Container | None: ...
    @overload
    def stop(self, image_tag: str, container_tag: None = None) -> Image | None: ...
    @overload
    def stop(self, image_tag: None = None, container_tag: None = None) -> Environment | None: ...
    def stop(
        self,
        image_tag: str | None = None,
        container_tag: str | None = None
    ) -> Environment | Image | Container | None:
        """Stop running containers within this environment.

        Parameters
        ----------
        image_tag : str | None, optional
            An optional image tag to stop containers within.  If None (the default),
            all containers in this environment will be stopped.
        container_tag : str | None, optional
            An optional container tag to stop within the indicated image.  If None (the
            default), all containers in the indicated image (or whole environment if
            `image_tag` is None) will be stopped.

        Returns
        -------
        Environment | Image | Container | None
            The top-level environment, image, or container metadata that was stopped,
            or None if no matching tag could be found.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container.
        TypeError
            If `image_tag` is None but `container_tag` is not.
        """
        if self.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke Docker from within a Bertrand container")

        # stop all containers in the environment
        if image_tag is None:
            if container_tag is not None:
                raise TypeError("cannot specify a container when stopping all images")
            for t in self.tags.values():
                for c in t["containers"].values():
                    container = Container(self.root, c)
                    inspect = Container.inspect(container.id)
                    if inspect is None:
                        continue
                    Container.stop(inspect, self.timeout)
            return self

        # stop all containers in an image
        if container_tag is None:
            tag = self.tags.get(image_tag)
            if tag is None:
                return None
            image = Image(self.root, tag["id"])
            containers = tag["containers"]
            for c in containers.values():
                container = Container(self.root, c)
                inspect = Container.inspect(container.id)
                if inspect is None:
                    continue
                Container.stop(inspect, self.timeout)
            return image

        # stop a specific container in an image
        tag = self.tags.get(image_tag)
        if tag is None:
            return None
        containers = tag["containers"]
        container_id = containers.get(container_tag)
        if container_id is None:
            return None
        container = Container(self.root, container_id)
        inspect = Container.inspect(container.id)
        if inspect is None:
            return None
        Container.stop(inspect, self.timeout)
        return container

    @overload
    def pause(self, image_tag: str, container_tag: str) -> Container | None: ...
    @overload
    def pause(self, image_tag: str, container_tag: None = None) -> Image | None: ...
    @overload
    def pause(self, image_tag: None = None, container_tag: None = None) -> Environment | None: ...
    def pause(
        self,
        image_tag: str | None = None,
        container_tag: str | None = None
    ) -> Environment | Image | Container | None:
        """Pause running containers within this environment.

        Parameters
        ----------
        image_tag : str | None, optional
            An optional image tag to pause containers within.  If None (the default),
            all containers in this environment will be paused.
        container_tag : str | None, optional
            An optional container tag to pause within the indicated image.  If None (the
            default), all containers in the indicated image (or whole environment if
            `image_tag` is None) will be paused.

        Returns
        -------
        Environment | Image | Container | None
            The top-level environment, image, or container metadata that was paused,
            or None if no matching tag could be found.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container.
        TypeError
            If `image_tag` is None but `container_tag` is not.
        """
        if self.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke Docker from within a Bertrand container")

        # pause all containers in the environment
        if image_tag is None:
            if container_tag is not None:
                raise TypeError("cannot specify a container when pausing all images")
            for t in self.tags.values():
                for c in t["containers"].values():
                    container = Container(self.root, c)
                    inspect = Container.inspect(container.id)
                    if inspect is None:
                        continue
                    Container.pause(inspect)
            return self

        # pause all containers in an image
        if container_tag is None:
            tag = self.tags.get(image_tag)
            if tag is None:
                return None
            image = Image(self.root, tag["id"])
            containers = tag["containers"]
            for c in containers.values():
                container = Container(self.root, c)
                inspect = Container.inspect(container.id)
                if inspect is None:
                    continue
                Container.pause(inspect)
            return image

        # pause a specific container in an image
        tag = self.tags.get(image_tag)
        if tag is None:
            return None
        containers = tag["containers"]
        container_id = containers.get(container_tag)
        if container_id is None:
            return None
        container = Container(self.root, container_id)
        inspect = Container.inspect(container.id)
        if inspect is None:
            return None
        Container.pause(inspect)
        return container

    @overload
    def resume(self, image_tag: str, container_tag: str) -> Container | None: ...
    @overload
    def resume(self, image_tag: str, container_tag: None = None) -> Image | None: ...
    @overload
    def resume(self, image_tag: None = None, container_tag: None = None) -> Environment | None: ...
    def resume(
        self,
        image_tag: str | None = None,
        container_tag: str | None = None
    ) -> Environment | Image | Container | None:
        """Resume paused containers within this environment.

        Parameters
        ----------
        image_tag : str | None, optional
            An optional image tag to resume containers within.  If None (the default),
            all paused containers in this environment will be resumed.
        container_tag : str | None, optional
            An optional container tag to resume within the indicated image.  If None
            (the default), all paused containers in the indicated image (or whole
            environment if `image_tag` is None) will be resumed.

        Returns
        -------
        Environment | Image | Container | None
            The top-level environment, image, or container metadata that was resumed,
            or None if no matching tag could be found.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container.
        TypeError
            If `image_tag` is None but `container_tag` is not.
        """
        if self.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke Docker from within a Bertrand container")

        # resume all paused containers in the environment
        if image_tag is None:
            if container_tag is not None:
                raise TypeError("cannot specify a container when stopping all images")
            for t in self.tags.values():
                for c in t["containers"].values():
                    container = Container(self.root, c)
                    inspect = Container.inspect(container.id)
                    if inspect is None:
                        continue
                    Container.resume(inspect)
            return self

        # resume all paused containers in an image
        if container_tag is None:
            tag = self.tags.get(image_tag)
            if tag is None:
                return None
            image = Image(self.root, tag["id"])
            containers = tag["containers"]
            for c in containers.values():
                container = Container(self.root, c)
                inspect = Container.inspect(container.id)
                if inspect is None:
                    continue
                Container.resume(inspect)
            return image

        # resume a specific container in an image
        tag = self.tags.get(image_tag)
        if tag is None:
            return None
        containers = tag["containers"]
        container_id = containers.get(container_tag)
        if container_id is None:
            return None
        container = Container(self.root, container_id)
        inspect = Container.inspect(container.id)
        if inspect is None:
            return None
        Container.resume(inspect)
        return container

    # TODO: restart should rebuild stale images and containers?

    @overload
    def restart(self, image_tag: str, container_tag: str) -> Container | None: ...
    @overload
    def restart(self, image_tag: str, container_tag: None = None) -> Image | None: ...
    @overload
    def restart(self, image_tag: None = None, container_tag: None = None) -> Environment | None: ...
    def restart(
        self,
        image_tag: str | None = None,
        container_tag: str | None = None
    ) -> Environment | Image | Container | None:
        """Restart running containers within this environment.

        Parameters
        ----------
        image_tag : str | None, optional
            An optional image tag to restart containers within.  If None (the default),
            all running containers in this environment will be restarted.
        container_tag : str | None, optional
            An optional container tag to restart within the indicated image.  If None
            (the default), all running containers in the indicated image (or whole
            environment if `image_tag` is None) will be restarted.

        Returns
        -------
        Environment | Image | Container | None
            The top-level environment, image, or container metadata that was restarted,
            or None if no matching tag could be found.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container.
        TypeError
            If `image_tag` is None but `container_tag` is not.
        """
        if self.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke Docker from within a Bertrand container")

        # restart all containers in the environment
        if image_tag is None:
            if container_tag is not None:
                raise TypeError("cannot specify a container when stopping all images")
            for t in self.tags.values():
                for c in t["containers"].values():
                    container = Container(self.root, c)
                    inspect = Container.inspect(container.id)
                    if inspect is None:
                        continue
                    Container.restart(inspect, self.timeout)
            return self

        # restart all containers in an image
        if container_tag is None:
            tag = self.tags.get(image_tag)
            if tag is None:
                return None
            image = Image(self.root, tag["id"])
            containers = tag["containers"]
            for c in containers.values():
                container = Container(self.root, c)
                inspect = Container.inspect(container.id)
                if inspect is None:
                    continue
                Container.restart(inspect, self.timeout)
            return image

        # restart a specific container in an image
        tag = self.tags.get(image_tag)
        if tag is None:
            return None
        containers = tag["containers"]
        container_id = containers.get(container_tag)
        if container_id is None:
            return None
        container = Container(self.root, container_id)
        inspect = Container.inspect(container.id)
        if inspect is None:
            return None
        Container.restart(inspect, self.timeout)
        return container

    class Info(TypedDict):
        """Type hint for the json output of `docker container ls`."""
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

    def _info(self, cmd: list[str], *, parse: bool) -> list[Environment.Info] | None:
        if parse:
            cmd.append("--no-trunc")
            cmd.append("--format=json")
            result = docker_cmd(cmd, capture_output=True)
            out = json.loads(result.stdout)
            if not isinstance(out, list):
                out = [out]
            return out

        cmd.append(
            "--format=table {{.Names}}\t{{.CreatedAt}}\t{{.State}}\t{{.Command}}\t"
            "{{.RunningFor}}\t{{.Status}}\t{{.Size}}\t{{.Ports}}"
        )
        docker_cmd(cmd)
        return None

    @overload
    def info(
        self,
        image_tag: str | None = None,
        container_tag: str | None = None,
        *,
        parse: Literal[True] = ...
    ) -> list[Environment.Info]: ...
    @overload
    def info(
        self,
        image_tag: str | None = None,
        container_tag: str | None = None,
        *,
        parse: Literal[False],
    ) -> None: ...
    def info(
        self,
        image_tag: str | None = None,
        container_tag: str | None = None,
        *,
        parse: bool = True
    ) -> list[Environment.Info] | None:
        """Gather status information for containers within this environment.

        Parameters
        ----------
        image_tag : str | None, optional
            An optional image tag to gather information for.  If None (the default),
            information will be gathered for all containers in this environment.
        container_tag : str | None, optional
            An optional container tag to gather information for.  If None (the default),
            information will be gathered for all containers in the indicated image (or
            whole environment if `image_tag` is None).
        parse : bool, optional
            Whether to parse the output as JSON and return it as a list of dictionaries.
            If false, the output will be printed to stdout in a human-readable table
            format, and nothing will be returned by this method.  Default is True.

        Returns
        -------
        list[Environment.Info] | None
            If `parse` is True, then a list of dictionaries containing status information
            for each matching container.  If `parse` is False, then None.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container.
        TypeError
            If `image_tag` is None but `container_tag` is not.
        """
        if self.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke Docker from within a Bertrand container")

        cmd = [
            "container",
            "ls",
            "--all",
            "--size",
            "--filter", f"label=BERTRAND_ENV=\"{self.root}\"",
        ]

        # gather info for all containers in the environment
        if image_tag is None:
            if container_tag is not None:
                raise TypeError("cannot specify a container when listing all images")
            return self._info(cmd, parse=parse)

        # gather info for all containers in an image
        cmd.append("--filter")
        cmd.append(f"label=BERTRAND_IMAGE={image_tag}")
        if container_tag is None:
            return self._info(cmd, parse=parse)

        # gather info for a specific container in an image
        cmd.append("--filter")
        cmd.append(f"label=BERTRAND_CONTAINER={container_tag}")
        return self._info(cmd, parse=parse)

    class Stats(TypedDict):
        """Type hint for the json output of `docker stats`."""
        Container: str
        ID: str
        Name: str
        CPUPerc: str
        MemPerc: str
        MemUsage: str
        NetIO: str
        BlockIO: str
        PIDs: str

    def _stats(
        self,
        ids: Iterable[str],
        *,
        parse: bool,
        stream: bool
    ) -> list[Environment.Stats] | None:
        cmd = ["container", "stats"]
        if not stream:
            cmd.append("--no-stream")

        if parse:
            cmd.append("--no-trunc")
            cmd.append("--format=json")
            cmd.extend(ids)
            result = docker_cmd(cmd, capture_output=True)
            out = json.loads(result.stdout)
            if not isinstance(out, list):
                return [out]
            return out

        if platform.system() == "Windows":
            cmd.append(
                "--format=table {{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}\t{{.NetIO}}\t"
                "{{.BlockIO}}"
            )
        else:
            cmd.append(
                "--format=table {{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}\t{{.MemPerc}}\t"
                "{{.NetIO}}\t{{.BlockIO}}\t{{.PIDs}}"
            )
        cmd.extend(ids)
        docker_cmd(cmd)
        return None

    @overload
    def stats(
        self,
        image_tag: str | None = None,
        container_tag: str | None = None,
        *,
        parse: Literal[True] = ...,
        stream: bool = ...
    ) -> list[Environment.Stats]: ...
    @overload
    def stats(
        self,
        image_tag: str | None = None,
        container_tag: str | None = None,
        *,
        parse: Literal[False],
        stream: bool = ...
    ) -> None: ...
    def stats(
        self,
        image_tag: str | None = None,
        container_tag: str | None = None,
        *,
        parse: bool = True,
        stream: bool = False
    ) -> list[Environment.Stats] | None:
        """Gather resource utilization statistics for containers within this
        environment.

        Parameters
        ----------
        image_tag : str | None, optional
            An optional image tag to gather statistics for.  If None (the default),
            statistics will be gathered for all containers in this environment.
        container_tag : str | None, optional
            An optional container tag to gather statistics for.  If None (the default),
            statistics will be gathered for all containers in the indicated image (or
            whole environment if `image_tag` is None).
        parse : bool, optional
            Whether to parse the output as JSON and return it as a list of dictionaries.
            If false, the output will be printed to stdout in a human-readable table
            format, and nothing will be returned by this method.  Default is True.
        stream : bool, optional
            Whether to continuously update the printed output in a streaming format.
            Incompatible with `parse=True`.  Default is False.

        Returns
        -------
        list[Environment.Stats] | None
            If `parse` is True, then a list of dictionaries containing resource
            utilization statistics for each matching container.  If `parse` is False,
            then None.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container.
        TypeError
            If `image_tag` is None but `container_tag` is not, or if both `parse` and
            `stream` are True.
        """
        if self.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke Docker from within a Bertrand container")
        if parse and stream:
            raise TypeError("cannot stream output in json format")

        search = [
            "container",
            "ls",
            "--format", "{{.ID}}",
            "--filter", f"label=BERTRAND_ENV=\"{self.root}\"",
        ]

        # gather stats for all containers in the environment
        if image_tag is None:
            if container_tag is not None:
                raise TypeError("cannot specify a container when listing all images")
            ids = docker_cmd(search, capture_output=True)
            return self._stats(ids.stdout.strip().splitlines(), parse=parse, stream=stream)

        # gather stats for all containers in an image
        search.append(f"--filter=label=BERTRAND_IMAGE={image_tag}")
        if container_tag is None:
            ids = docker_cmd(search, capture_output=True)
            return self._stats(ids.stdout.strip().splitlines(), parse=parse, stream=stream)

        # gather stats for a specific container in an image
        search.append(f"--filter=label=BERTRAND_CONTAINER={container_tag}")
        ids = docker_cmd(search, capture_output=True)
        return self._stats(ids.stdout.strip().splitlines(), parse=parse, stream=stream)

    def top(self, image_tag: str, container_tag: str) -> None:
        """Print the running processes within a specific container in this
        environment.

        Parameters
        ----------
        image_tag : str
            The image tag containing the container to inspect.
        container_tag : str
            The container tag to inspect.

        Raises
        ------
        OSError
            If the environment has not been acquired as a context manager, or if this
            method is invoked from within a Bertrand container.
        KeyError
            If the specified image or container tag could not be found.
        """
        if self.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke Docker from within a Bertrand container")

        tag = self.tags.get(image_tag)
        if tag is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        containers = tag["containers"]
        container_id = containers.get(container_tag)
        if container_id is None:
            raise KeyError(f"no container found for tag: '{container_tag}'")

        docker_cmd([
            "container",
            "top",
            f"{Container(self.root, container_id).id}",
        ])


def ls() -> list[str]:
    """Return a list of paths to all Bertrand environments on the system.

    Returns
    -------
    list[str]
        A list of paths to all Bertrand environments found on the system.

    Raises
    ------
    OSError
        If this function is invoked from within a Bertrand container.
    """
    if Environment.current() is not None:
        raise OSError("cannot invoke Docker from within a Bertrand container")

    # find all Bertrand containers on the system
    result = docker_cmd([
        "container",
        "ls",
        "--all",
        "--filter", "label=BERTRAND=1",
        "--no-trunc",
        "--format=json"
    ], capture_output=True)
    out = json.loads(result.stdout)
    if not isinstance(out, list):
        out = [out]

    # gather unique mount points and attempt to load environment metadata from all containers
    seen: set[str] = set()
    envs: list[str] = []
    for item in out:
        for mount in item.get("Mounts", "").split(","):
            if mount not in seen:
                seen.add(mount)
                try:
                    with Environment(Path(mount)):
                        envs.append(mount)
                except Exception:
                    pass

    return envs


def build(*, assume_yes: bool = False) -> None:
    """Build all Bertrand environments on the system.

    Parameters
    ----------
    assume_yes : bool, optional
        If True, do not prompt for confirmation before building all environments.
        Default is False.

    Raises
    ------
    OSError
        If this function is invoked from within a Bertrand container.
    """
    if Environment.current() is not None:
        raise OSError("cannot invoke Docker from within a Bertrand container")

    if confirm(
        "This will build all Bertrand environments on this system.  This may take "
        "a long time depending on the number and complexity of the environments.\n"
        "Are you sure you want to continue? [y/N]: ",
        assume_yes=assume_yes
    ):
        for env_path in ls():
            try:
                with Environment(Path(env_path)) as env:
                    env.build()
            except Exception:
                pass


def start(*, assume_yes: bool = False) -> None:
    """Start all stopped Bertrand containers on the system.

    Parameters
    ----------
    assume_yes : bool, optional
        If True, do not prompt for confirmation before starting all containers.
        Default is False.

    Raises
    ------
    OSError
        If this function is invoked from within a Bertrand container.
    """
    if Environment.current() is not None:
        raise OSError("cannot invoke Docker from within a Bertrand container")

    if confirm(
        "This will start all stopped Bertrand containers on this system.\n"
        "Are you sure you want to continue? [y/N]: ",
        assume_yes=assume_yes
    ):
        for env_path in ls():
            try:
                with Environment(Path(env_path)) as env:
                    env.start()
            except Exception:
                pass


def run(argv: list[str] | None = None, *, assume_yes: bool = False) -> None:
    """Run all stopped Bertrand containers on the system.

    Parameters
    ----------
    argv : list[str] | None, optional
        An optional list of command-line arguments to pass to each container
        when running.  If None (the default), the default command for each container
        will be used.
    assume_yes : bool, optional
        If True, do not prompt for confirmation before running all containers.
        Default is False.

    Raises
    ------
    OSError
        If this function is invoked from within a Bertrand container.
    """
    if Environment.current() is not None:
        raise OSError("cannot invoke Docker from within a Bertrand container")

    if confirm(
        "This will run all Bertrand containers on this system.\n"
        "Are you sure you want to continue? [y/N]: ",
        assume_yes=assume_yes
    ):
        for env_path in ls():
            try:
                with Environment(Path(env_path)) as env:
                    env.run(argv=argv)
            except Exception:
                pass


def stop(*, assume_yes: bool = False) -> None:
    """Stop all running Bertrand containers on the system.

    Parameters
    ----------
    assume_yes : bool, optional
        If True, do not prompt for confirmation before stopping all containers.
        Default is False.

    Raises
    ------
    OSError
        If this function is invoked from within a Bertrand container.
    """
    if Environment.current() is not None:
        raise OSError("cannot invoke Docker from within a Bertrand container")

    if confirm(
        "This will stop all running Bertrand containers on this system.\n"
        "Are you sure you want to continue? [y/N]: ",
        assume_yes=assume_yes
    ):
        for env_path in ls():
            try:
                with Environment(Path(env_path)) as env:
                    env.stop()
            except Exception:
                pass


def pause(*, assume_yes: bool = False) -> None:
    """Pause all running Bertrand containers on the system.

    Parameters
    ----------
    assume_yes : bool, optional
        If True, do not prompt for confirmation before pausing all containers.
        Default is False.

    Raises
    ------
    OSError
        If this function is invoked from within a Bertrand container.
    """
    if Environment.current() is not None:
        raise OSError("cannot invoke Docker from within a Bertrand container")

    if confirm(
        "This will pause all running Bertrand containers on this system.\n"
        "Are you sure you want to continue? [y/N]: ",
        assume_yes=assume_yes
    ):
        for env_path in ls():
            try:
                with Environment(Path(env_path)) as env:
                    env.pause()
            except Exception:
                pass


def resume(*, assume_yes: bool = False) -> None:
    """Resume all paused Bertrand containers on the system.

    Parameters
    ----------
    assume_yes : bool, optional
        If True, do not prompt for confirmation before resuming all containers.
        Default is False.

    Raises
    ------
    OSError
        If this function is invoked from within a Bertrand container.
    """
    if Environment.current() is not None:
        raise OSError("cannot invoke Docker from within a Bertrand container")

    if confirm(
        "This will resume all paused Bertrand containers on this system.\n"
        "Are you sure you want to continue? [y/N]: ",
        assume_yes=assume_yes
    ):
        for env_path in ls():
            try:
                with Environment(Path(env_path)) as env:
                    env.resume()
            except Exception:
                pass


def restart(*, assume_yes: bool = False) -> None:
    """Restart all running Bertrand containers on the system.

    Parameters
    ----------
    assume_yes : bool, optional
        If True, do not prompt for confirmation before restarting all containers.
        Default is False.

    Raises
    ------
    OSError
        If this function is invoked from within a Bertrand container.
    """
    if Environment.current() is not None:
        raise OSError("cannot invoke Docker from within a Bertrand container")

    if confirm(
        "This will restart all running Bertrand containers on this system.\n"
        "Are you sure you want to continue? [y/N]: ",
        assume_yes=assume_yes
    ):
        for env_path in ls():
            try:
                with Environment(Path(env_path)) as env:
                    env.restart()
            except Exception:
                pass


def rm(*, assume_yes: bool = False) -> None:
    """Delete all Bertrand environments on the system.

    Parameters
    ----------
    assume_yes : bool, optional
        If True, do not prompt for confirmation before deleting all environments.
        Default is False.

    Raises
    ------
    OSError
        If this function is invoked from within a Bertrand container.
    """
    if Environment.current() is not None:
        raise OSError("cannot invoke Docker from within a Bertrand container")

    if confirm(
        "This will delete all Bertrand environments, images, and containers on this "
        "system.  This action cannot be undone.\n"
        "Are you sure you want to continue? [y/N]: ",
        assume_yes=assume_yes
    ):
        for env_path in ls():
            try:
                with Environment(Path(env_path)) as env:
                    env.rm()
            except Exception:
                pass


def prune() -> None:
    """Delete all stale Bertrand images and containers on the system.

    Raises
    ------
    OSError
        If this function is invoked from within a Bertrand container.
    """
    if Environment.current() is not None:
        raise OSError("cannot invoke Docker from within a Bertrand container")

    for env_path in ls():
        try:
            with Environment(Path(env_path)) as env:
                env.prune()
        except Exception:
            pass


@overload
def info(*, parse: Literal[True] = ...) -> list[Environment.Info]: ...
@overload
def info(*, parse: Literal[False]) -> None: ...
def info(*, parse: bool = True) -> list[Environment.Info] | None:
    """Gather status information for all containers in all Bertrand environments
    on the system.

    Parameters
    ----------
    parse : bool, optional
        Whether to parse the output as JSON and return it as a list of dictionaries.
        If false, the output will be printed to stdout in a human-readable table
        format, and nothing will be returned by this function.  Default is True.

    Returns
    -------
    list[Environment.Info] | None
        If `parse` is True, then a list of dictionaries containing status information
        for each container in all Bertrand environments.  If `parse` is False, then
        None.

    Raises
    ------
    OSError
        If this function is invoked from within a Bertrand container.
    """
    if Environment.current() is not None:
        raise OSError("cannot invoke Docker from within a Bertrand container")

    cmd = [
        "container",
        "ls",
        "--all",
        "--size",
        "--filter", "label=BERTRAND=1",
    ]

    if parse:
        cmd.append("--no-trunc")
        cmd.append("--format=json")
        result = docker_cmd(cmd, capture_output=True)
        out = json.loads(result.stdout)
        if not isinstance(out, list):
            out = [out]
        return out

    cmd.append(
        "--format=table {{.Names}}\t{{.CreatedAt}}\t{{.State}}\t{{.Command}}\t"
        "{{.RunningFor}}\t{{.Status}}\t{{.Size}}\t{{.Ports}}"
    )
    docker_cmd(cmd)
    return None


@overload
def stats(*, parse: Literal[True] = ..., stream: bool = ...) -> list[Environment.Stats]: ...
@overload
def stats(*, parse: Literal[False], stream: bool = ...) -> None: ...
def stats(*, parse: bool = True, stream: bool = False) -> list[Environment.Stats] | None:
    """Gather resource utilization statistics for all containers in all Bertrand
    environments on the system.

    Parameters
    ----------
    parse : bool, optional
        Whether to parse the output as JSON and return it as a list of dictionaries.
        If false, the output will be printed to stdout in a human-readable table
        format, and nothing will be returned by this function.  Default is True.
    stream : bool, optional
        Whether to continuously update the printed output in a streaming format.
        Incompatible with `parse=True`.  Default is False.

    Returns
    -------
    list[Environment.Stats] | None
        If `parse` is True, then a list of dictionaries containing resource
        utilization statistics for each container in all Bertrand environments.  If
        `parse` is False, then None.

    Raises
    ------
    OSError
        If this function is invoked from within a Bertrand container.
    TypeError
        If both `parse` and `stream` are True.
    """
    if Environment.current() is not None:
        raise OSError("cannot invoke Docker from within a Bertrand container")
    if parse and stream:
        raise TypeError("cannot stream output in json format")

    cmd = [
        "container",
        "stats",
        "--no-trunc",
        "--filter", "label=BERTRAND=1",
    ]
    if not stream:
        cmd.append("--no-stream")

    if parse:
        cmd.append("--format=json")
        result = docker_cmd(cmd, capture_output=True)
        out = json.loads(result.stdout)
        if not isinstance(out, list):
            return [out]
        return out

    if platform.system() == "Windows":
        cmd.append(
            "--format=table {{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}\t{{.NetIO}}\t"
            "{{.BlockIO}}"
        )
    else:
        cmd.append(
            "--format=table {{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}\t{{.MemPerc}}\t"
            "{{.NetIO}}\t{{.BlockIO}}\t{{.PIDs}}"
        )
    docker_cmd(cmd)
    return None





# TODO: all this IPC stuff is required to implement editor hooks, but I haven't
# figured out how to do that cleanly yet.  It will require running a command inside the
# container that prompts the host to launch the editor and attach that container's
# clangd, Python, AI integrations, etc.  That also has to be robust against
# arbitrary code injection, and has to support multiple editors (at least the most
# common ones).


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
