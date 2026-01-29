"""Install Docker Engine and pull container images."""
from __future__ import annotations

import json
import os
import platform
import shlex
import shutil
import sys
import time
import uuid

from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from resource import getpagesize
from types import TracebackType
from typing import Iterable, Literal, TypedDict, overload

import psutil

from .docker_engine import docker_cmd, docker_exec
from .run import CommandError, atomic_write_text, confirm
from .version import __version__

#pylint: disable=redefined-builtin, redefined-outer-name, broad-except


VERSION: int = 1
MOUNT: str = "/env"
TMP: str = "/tmp"
TIMEOUT: int = 30
SHELLS: dict[str, tuple[str, ...]] = {
    "bash": ("bash", "-l"),
}
EDITORS: dict[str, tuple[str, ...]] = {
    "vscode": ("code",),
}


def _bertrand_dir(env_root: Path) -> Path:
    return env_root / ".bertrand"


def _bertrand_tmp_dir(env_root: Path) -> Path:
    return _bertrand_dir(env_root) / "tmp"


def _cid_file(env_root: Path, name: str) -> Path:
    return _bertrand_tmp_dir(env_root) / f"{name}.cid"


def _iid_file(env_root: Path, name: str) -> Path:
    return _bertrand_tmp_dir(env_root) / f"{name}.iid"


def _env_file(env_root: Path) -> Path:
    return _bertrand_dir(env_root) / "env.json"


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


def _validate_version(data: dict[str, object]) -> int:
    version = data.get("version")
    if not isinstance(version, int) or version <= 0:
        raise ValueError(f"missing or invalid 'version' field: {version}")
    return version


def _validate_id(data: dict[str, object]) -> str:
    id = data.get("id")
    if not isinstance(id, str):
        raise ValueError(f"missing or invalid 'id' field: {id}")
    id = id.strip()
    if not id:
        raise ValueError(f"missing or invalid 'id' field: {id}")
    return id


def _validate_uuid(data: dict[str, object]) -> str:
    uuid_str = data.get("uuid")
    if not isinstance(uuid_str, str):
        raise ValueError(f"missing or invalid 'uuid' field: {uuid_str}")
    uuid_str = uuid_str.strip()
    if not uuid_str:
        raise ValueError(f"missing or invalid 'uuid' field: {uuid_str}")
    try:
        uuid.UUID(uuid_str)
    except Exception as err:
        raise ValueError(f"'uuid' must be a valid UUID: {uuid_str}") from err
    return uuid_str


def _validate_created(data: dict[str, object]) -> datetime:
    created = data.get("created")
    if not isinstance(created, str) or not created.strip():
        raise ValueError(f"missing or invalid 'created' field: {created}")
    try:
        return datetime.fromisoformat(created).astimezone(timezone.utc)
    except Exception as err:
        raise ValueError(f"'created' must be a valid ISO timestamp: {created}") from err


def _validate_args(data: dict[str, object]) -> list[str]:
    args = data.get("args")
    if not isinstance(args, list) or not all(isinstance(x, str) and x for x in args):
        raise ValueError(f"missing or invalid 'args' field: {args}")
    return _normalize_args(args)


def _validate_entry_point(data: dict[str, object]) -> list[str]:
    entry_point = data.get("entry_point")
    if (
        not isinstance(entry_point, list) or
        not all(isinstance(x, str) and x for x in entry_point)
    ):
        raise ValueError(f"missing or invalid 'entry_point' field: {entry_point}")
    return _normalize_args(entry_point)


@dataclass
class Container:
    """In-memory metadata representing a local Bertrand Docker container, which is a
    runtime context for a compiled image.  An image can have many containers, each
    built with a different runtime configuration, and each container is considered to
    be immutable once created.

    Specific care is taken not to store anything that references the host filesystem or
    container name, in order to allow renaming/relocation of the environment directory.

    Attributes
    ----------
    version : int
        The version number for backwards compatibility.
    id : str
        The unique Docker container ID.  This is equivalent to the metadata file name
        in `container_dir`.
    created : datetime
        The ISO timestamp when the container was created.
    args : list[str]
        The original `docker create` arguments used to create the container.
    entry_point : list[str]
        The shell prefix used to run the container.  This is usually detected during
        compilation by looking for a __main__.py file or a C++ module with a main()
        function.
    """
    class State(TypedDict, total=False):
        """Type hint for docker container state information."""
        Status: Literal["created", "restarting", "running", "removing", "paused", "exited", "dead"]
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

    version: int
    id: str
    created: datetime
    args: list[str]
    entry_point: list[str]

    @staticmethod
    def from_json(data: dict[str, object]) -> Container:
        """Load a Container from a dictionary of metadata.

        Parameters
        ----------
        data : dict[str, object]
            The raw JSON metadata.

        Returns
        -------
        Container
            The loaded `Container` object.

        Raises
        ------
        ValueError
            If the container metadata is malformed.
        """
        return Container(
            version=_validate_version(data),
            id=_validate_id(data),
            created=_validate_created(data),
            args=_validate_args(data),
            entry_point=_validate_entry_point(data),
        )

    def to_json(self) -> dict[str, object]:
        """Serialize this Container to a JSON-compatible dictionary.

        Returns
        -------
        dict[str, object]
            The serialized JSON metadata.
        """
        return {
            "version": self.version,
            "id": self.id,
            "created": self.created.isoformat(),
            "args": self.args,
            "entry_point": self.entry_point,
        }

    def inspect(self) -> Container.Inspect | None:
        """Invoke Docker to inspect this container.

        Returns
        -------
        Container.Inspect | None
            A JSON response from the Docker daemon or None if the container could not
            be found.  Type hints are provided via the `Container.Inspect` TypedDict.
        """
        result = docker_cmd(["inspect", self.id], check=False, capture_output=True)
        if result.returncode != 0 or not result.stdout:
            return None
        data = json.loads(result.stdout)
        return data[0] if data else None

    @staticmethod
    def relocated(env_root: Path, inspect: Container.Inspect) -> bool:
        """Detect whether a Docker container's mounted environment path has drifted
        from the environment's true root directory.

        Parameters
        ----------
        env_root : Path
            An absolute host path to the environment directory.
        inspect : Container.Inspect
            The output of `Container.inspect()` for the container to query.

        Returns
        -------
        bool
            True if the container's mounted environment path differs from `env_root`,
            False otherwise.
        """
        mount = Container.mount(inspect)
        try:
            return mount is None or not os.path.samefile(mount, env_root)
        except OSError:
            return True

    @staticmethod
    def mount(inspect: Container.Inspect) -> Path | None:
        """Extract the root path of the environment directory mounted to this Docker
        container from its inspection data.

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
                    return Path(src).expanduser().resolve()
        return None

    @staticmethod
    def new(inspect: Container.Inspect) -> bool:
        """Check whether a Docker container is newly created (i.e. has never been
        started).

        Parameters
        ----------
        inspect : Container.Inspect
            The output of `Container.inspect()` for the container to query.

        Returns
        -------
        bool
            True if the container is newly created, False otherwise.
        """
        return inspect["State"]["Status"] == "created"

    @staticmethod
    def running(inspect: Container.Inspect) -> bool:
        """Check whether a Docker container is currently running.

        Parameters
        ----------
        inspect : Container.Inspect
            The output of `Container.inspect()` for the container to query.

        Returns
        -------
        bool
            True if the container is currently running, False otherwise.
        """
        state = inspect["State"]
        return state["Running"] or state["Restarting"]

    @staticmethod
    def paused(inspect: Container.Inspect) -> bool:
        """Check whether a Docker container is currently paused.

        Parameters
        ----------
        inspect : Container.Inspect
            The output of `Container.inspect()` for the container to query.

        Returns
        -------
        bool
            True if the container is currently paused, False otherwise.
        """
        return inspect["State"]["Paused"]

    @staticmethod
    def stopped(inspect: Container.Inspect) -> bool:
        """Check whether a Docker container is currently stopped.

        Parameters
        ----------
        inspect : Container.Inspect
            The output of `Container.inspect()` for the container to query.

        Returns
        -------
        bool
            True if the container is currently stopped, False otherwise.
        """
        state = inspect["State"]
        return not (state["Running"] or state["Restarting"] or state["Paused"])

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
        if inspect["State"]["Paused"]:
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


def _validate_containers(data: dict[str, object]) -> dict[str, Container]:
    result: dict[str, Container] = {}
    containers = data.get("containers", {})
    if not isinstance(containers, dict):
        raise ValueError(f"missing or invalid 'containers' field: {containers}")
    for container_tag, container_data in containers.items():
        if not isinstance(container_tag, str):
            raise ValueError(
                f"invalid container tag: {container_tag}"
            )
        if not isinstance(container_data, dict):
            raise ValueError(
                f"invalid data for container tag '{container_tag}': {container_data}"
            )
        if container_tag in result:
            raise ValueError(
                f"duplicate container tag '{container_tag}'"
            )
        result[container_tag] = Container.from_json(container_data)
    return result


@dataclass
class Image:
    """In-memory metadata representing a local Bertrand Docker image, which is a
    compiled snapshot of an environment with a particular set of build arguments.  An
    environment can have many images, each built with a different set of Dockerfile
    arguments, and each image is considered to be immutable once created.

    Specific care is taken not to store anything that references the host filesystem,
    in order to allow renaming/relocation of the environment directory.

    Attributes
    ----------
    version : int
        The version number for backwards compatibility.
    id : str
        The unique Docker image ID.  This is equivalent to the metadata file name in
        `image_dir`.
    created : datetime
        The ISO timestamp when the image was created.
    args : list[str]
        The original `docker build` args used to build the image.
    containers : dict[str, Container]
        A mapping from container tags to their corresponding `Container` metadata
        objects built from this image.
    """
    class Inspect(TypedDict, total=False):
        """Type hint for `docker container inspect` output."""
        Id: str
        Created: str

    version: int
    id: str
    created: datetime
    args: list[str]
    containers: dict[str, Container]

    @staticmethod
    def from_json(data: dict[str, object]) -> Image:
        """Load an Image from a dictionary of metadata representing an entry in the
        `env.json` tag mapping.

        Parameters
        ----------
        data : dict[str, object]
            The raw JSON metadata.

        Returns
        -------
        Image
            The loaded `Image` object.

        Raises
        ------
        ValueError
            If the image metadata is malformed.
        """
        return Image(
            version=_validate_version(data),
            id=_validate_id(data),
            created=_validate_created(data),
            args=_validate_args(data),
            containers=_validate_containers(data),
        )

    def to_json(self) -> dict[str, object]:
        """Serialize this Image to a JSON-compatible dictionary.

        Returns
        -------
        dict[str, object]
            The serialized JSON metadata.
        """
        return {
            "version": self.version,
            "id": self.id,
            "created": self.created.isoformat(),
            "args": self.args,
            "containers": {tag: container.to_json() for tag, container in self.containers.items()}
        }

    def inspect(self) -> Image.Inspect | None:
        """Invoke Docker to inspect this image.

        Returns
        -------
        Image.Inspect | None
            A JSON response from the Docker daemon or None if the image could not
            be found.  Type hints are provided via the `Image.Inspect` TypedDict.
        """
        result = docker_cmd(["image", "inspect", self.id], check=False, capture_output=True)
        if result.returncode != 0 or not result.stdout:
            return None
        data = json.loads(result.stdout)
        return data[0] if data else None

    def build(
        self,
        env_root: Path,
        env_uuid: str,
        image_tag: str,
        container_tag: str,
        container_args: list[str] | None
    ) -> Container:
        """Create a new container associated with this image and parent environment,
        with the specified tag and arguments.  If a container with the same tag and
        arguments already exists and has not been relocated, it will be reused instead.

        Parameters
        ----------
        env_root : Path
            The root path of the environment directory.
        env_uuid : str
            The UUID of the environment.
        image_tag : str
            The tag of the image within the environment.
        container_tag : str
            The tag of the container within the image.
        container_args : list[str] | None
            The `docker create` arguments to use for the container.  If None, the
            existing arguments for the container with the given tag will be reused,
            or an empty list if no such container exists and the tag is empty.

        Returns
        -------
        Container
            The created or reused `Container` object.  Note that if a new container is
            created, it will be in the "created" state, and must be started before use.
            An equivalent key in the `containers` mapping will also be created or
            updated to point to the new container.

        Raises
        ------
        KeyError
            If no previous container exists for the given tag and `container_args` is
            None.
        CommandError
            If the `docker create` or `docker container rm` (for an outdated container)
            command fails.
        """
        existing = self.containers.get(container_tag)
        if container_args is None:
            if existing is None:
                if container_tag:
                    raise KeyError(f"no container '{container_tag}' found for image '{image_tag}'")
                container_args = []  # empty tag implies empty args
            else:
                container_args = existing.args  # reuse existing args
        else:
            container_args = _normalize_args(container_args)  # define new args

        # reuse container if args match and container has not been relocated
        if existing is not None and existing.args == container_args:
            inspect = existing.inspect()
            if inspect is not None and not Container.relocated(env_root, inspect):
                return existing

        # build new container
        container = Container(
            version=VERSION,
            id="",  # corrected after create
            created=datetime.now(timezone.utc),
            args=container_args,
            entry_point=[],  # default entry point
        )
        container_name = (
            f"{_sanitize_name(env_root.name)}.{image_tag}.{container_tag}.{env_uuid[:13]}"
        )
        cid_file = _cid_file(env_root, container_name)
        cache_prefix = f"bertrand-{env_uuid[:13]}"
        try:
            cid_file.parent.mkdir(parents=True, exist_ok=True)
            docker_cmd([
                "create",
                "--init",
                f"--name={container_name}",
                f"--hostname={container_name}",
                "--cidfile", str(cid_file),

                # labels for docker-level lookup
                "--label", "BERTRAND=1",
                "--label", f"BERTRAND_ENV={env_uuid}",
                "--label", f"BERTRAND_IMAGE={image_tag}",
                "--label", f"BERTRAND_CONTAINER={container_tag}",

                # mount environment directory
                "-v", f"{str(env_root)}:{MOUNT}",

                # persistent caches for incremental builds
                "--mount", f"type=volume,src={cache_prefix}-pip,dst={TMP}/.cache/pip",
                "--mount", f"type=volume,src={cache_prefix}-bertrand,dst={TMP}/.cache/bertrand",
                "--mount", f"type=volume,src={cache_prefix}-ccache,dst={TMP}/.cache/ccache",
                "-e", f"PIP_CACHE_DIR={TMP}/.cache/pip",
                "-e", f"BERTRAND_CACHE={TMP}/.cache/bertrand",
                "-e", f"CCACHE_DIR={TMP}/.cache/ccache",

                # environment variables for Bertrand runtime
                "-e", "BERTRAND=1",
                "-e", f"BERTRAND_ENV={env_uuid}",
                "-e", f"BERTRAND_IMAGE={image_tag}",
                "-e", f"BERTRAND_CONTAINER={container_tag}",

                # any additional user-specified args
                *container_args,
                self.id,
                "sleep", "infinity",
            ])
            container.id = cid_file.read_text(encoding="utf-8").strip()
        finally:
            cid_file.unlink(missing_ok=True)

        # remove existing container with same tag if needed
        if existing is not None:
            try:
                docker_cmd(["container", "rm", "-f", existing.id], check=False)
            except CommandError as err:
                docker_cmd(["container", "rm", "-f", container.id], check=False)
                raise err
        self.containers[container_tag] = container
        return container


class Environment:
    """On-disk metadata representing environment-level data structures, which map from
    human-readable, stable tags to the corresponding Docker images and containers built
    within this environment directory.

    This class is meant to be used as a context manager, which will automatically
    acquire and release a lock on the environment directory in order to prevent
    concurrent modifications.  The environment metadata will be loaded upon entering
    the outermost context, and written back to disk upon exiting it, in order to
    synchronize any changes made during the context's lifetime.

    Parameters
    ----------
    root : Path
        The root path of the environment directory.
    timeout : int, optional
        The maximum time in seconds to wait for acquiring the environment lock.
        Defaults to `TIMEOUT`, which equates to 30 seconds.
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
    uuid : str
        A UUID for this environment.  This is used to label all Docker images and
        containers associated with this environment, to allow for easy lookup via the
        Docker daemon.
    shell : list[str]
        The shell command to execute during `bertrand enter`.
    code : list[str]
        The default host command invoked by `code` within the container.
    tags : dict[str, Environment.Tag]
        A mapping from image tags to metadata objects representing corresponding Docker
        images.  Each image object contains a nested mapping from container tags to
        downstream container metadata objects.  This is the single source of truth
        for locating images and containers within this environment, such that the
        underlying Docker image and container IDs may change without affecting the
        user-visible tags.
    """
    root: Path
    timeout: int
    depth: int
    version: int
    uuid: str
    shell: str
    code: str
    tags: dict[str, Image]

    def __init__(
        self,
        root: Path,
        timeout: int = TIMEOUT,
        shell: Literal["bash"] = "bash",
        code: Literal["vscode"] = "vscode"
    ) -> None:
        self.root = root.expanduser().resolve()
        self.timeout = timeout
        self.depth = 0
        self.version = 0
        self.uuid = ""
        self.shell = shell
        self.code = code
        self.tags = {}

    # TODO: this lock can be improved by reusing the LockDir class from `run`, and
    # just manually calling its __enter__ and __exit__ methods here.

    def _lock(self) -> None:
        lock_dir = self.bertrand_dir / ".lock"
        lock_file = lock_dir / "owner.json"
        pid = os.getpid()
        create_time = psutil.Process(pid).create_time()
        start = time.time()
        while True:
            try:
                lock_dir.mkdir(parents=True)
                atomic_write_text(lock_file, json.dumps({
                    "pid": pid,
                    "pid_start": create_time,
                }, indent=2) + "\n")
                break
            except FileExistsError as err:
                # another process holds the lock - check if it's stale
                now = time.time()
                try:
                    owner = json.loads(lock_file.read_text(encoding="utf-8"))
                    if not isinstance(owner, dict):
                        shutil.rmtree(lock_dir, ignore_errors=True)
                        continue
                except Exception:
                    shutil.rmtree(lock_dir, ignore_errors=True)
                    continue

                # check whether owning process is still alive
                owner_pid = owner.get("pid")
                owner_start = owner.get("pid_start")
                tolerance = 0.001  # tolerate floating point precision issues
                if isinstance(owner_pid, int) and isinstance(owner_start, (int, float)) and (
                    not psutil.pid_exists(owner_pid) or
                    psutil.Process(owner_pid).create_time() > (owner_start + tolerance)
                ):
                    shutil.rmtree(lock_dir, ignore_errors=True)
                    continue

                # error on timeout
                if (now - start) > self.timeout:
                    detail = f"\nlock owner: {json.dumps(owner, indent=2)})" if owner else ""
                    raise TimeoutError(
                        f"could not acquire environment lock within {self.timeout} seconds{detail}"
                    ) from err

                # wait and retry
                time.sleep(0.1)

    def _validate_shell(self, data: dict[str, object]) -> None:
        shell = data.get("shell")
        if not isinstance(shell, str):
            raise ValueError("missing required field: shell")
        if shell not in SHELLS:
            raise ValueError(f"unsupported shell: {shell}")
        self.shell = shell

    def _validate_code(self, data: dict[str, object]) -> None:
        code = data.get("code")
        if not isinstance(code, str):
            raise ValueError("missing required field: code")
        if code not in EDITORS:
            raise ValueError(f"unsupported code command: {code}")
        self.code = code

    def _validate_tags(self, data: dict[str, object]) -> None:
        tags = data.get("tags")
        if not isinstance(tags, dict):
            raise ValueError(f"missing or invalid 'tags' field: {tags}")

        for image_tag, image_data in tags.items():
            # validate image tag
            if not isinstance(image_tag, str):
                raise ValueError(f"invalid image tag in environment metadata: {image_tag}")
            image_tag = image_tag.strip()
            sanitized = _sanitize_name(image_tag)
            if image_tag != sanitized:
                raise ValueError(
                    f"invalid characters in image tag '{image_tag}' (sanitizes to: '{sanitized}')"
                )
            if image_tag in self.tags:
                raise ValueError(f"duplicate image tag in environment metadata: {image_tag}")

            # load image metadata + containers
            image = Image.from_json(image_data)
            self.tags[image_tag] = image

    def __enter__(self) -> Environment:
        # allow nested context managers without deadlocking
        self.depth += 1
        if self.depth > 1:
            return self
        self._lock()

        # try to load existing metadata if possible
        env_file = self.env_file
        if env_file.exists():
            data = json.loads(env_file.read_text(encoding="utf-8"))
            if not isinstance(data, dict):
                raise ValueError("environment metadata must be a JSON mapping")
            self.version = _validate_version(data)
            self.uuid = _validate_uuid(data)
            self._validate_shell(data)
            self._validate_code(data)
            self._validate_tags(data)
            return self

        # initialize new metadata if needed
        self.version = VERSION
        self.uuid = uuid.uuid4().hex
        self.tags = {}

        # init .dockerignore
        docker_ignore = self.docker_ignore
        if not docker_ignore.exists():
            docker_ignore.parent.mkdir(parents=True, exist_ok=True)
            atomic_write_text(docker_ignore, r"""# Bertrand internal state
.bertrand/
**/.bertrand/

# Python
__pycache__/
*.py[cod]
*.egg-info/
.dist/
.build/
.eggs/
.venv/
venv/

# C/C++
build/
out/
*.o
*.obj
*.a
*.lib
*.so
*.dylib
*.dll

# VCS / IDE
.git/
.gitignore
.vscode/
.idea/
.DS_Store
""")

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

# set cwd to the mounted environment directory
WORKDIR {MOUNT}

# set up incremental builds
ENV PIP_CACHE_DIR={TMP}/.cache/pip
ENV BERTRAND_CACHE={TMP}/.cache/bertrand
ENV CCACHE_DIR={TMP}/.cache/ccache
COPY . {MOUNT}

# you can extend this file in order to create a reproducible image that others can pull
# from in their own Dockerfiles.  For example:

RUN --mount=type=cache,target={TMP}/.cache/pip,sharing=locked \
    --mount=type=cache,target={TMP}/.cache/bertrand,sharing=locked \
    --mount=type=cache,target={TMP}/.cache/ccache,sharing=locked \
    pip install .

# A `pip install .` command of that form will incrementally compile the contents of the
# local environment directory (WORKDIR) and install them into the base image as Python
# packages, C++ modules, and/or executable binaries on the container's PATH.  If you
# then upload this image to a Docker repository, downstream users will be able to use
# `FROM <your-image>` in their own Dockerfiles in order to inherit Bertrand's toolchain
# along with your built artifacts and dependencies without needing to recompile them
# from scratch.  This can be useful for large projects where build time is significant,
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
        self.bertrand_dir.mkdir(parents=True, exist_ok=True)
        atomic_write_text(
            self.env_file,
            json.dumps({
                "version": self.version,
                "uuid": self.uuid,
                "shell": self.shell,
                "code": self.code,
                "tags": {tag: image.to_json() for tag, image in self.tags.items()}
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
        if (
            os.environ.get("BERTRAND", "0") == "1" and
            "BERTRAND_ENV" in os.environ and
            "BERTRAND_IMAGE" in os.environ and
            "BERTRAND_CONTAINER" in os.environ
        ):
            return Environment(root=Path(MOUNT))
        return None

    @staticmethod
    def parse(spec: str) -> tuple[Path, str, str]:
        """Parse a string of the form `<env_root>[:<image_tag>[:<container_tag>]]` into
        its constituent parts.

        Parameters
        ----------
        spec : str
            The container specification string to parse, which is usually provided from
            the command line or `ls()`.

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
        metadata without modifying the environment or clearing stale references.

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
        image = self.tags.get(image_tag)
        if image is None or container_tag is None:
            return image

        # search for container
        return image.containers.get(container_tag)

    def ls(self, image_tag: str | None = None, container_tag: str | None = None) -> list[str]:
        """Return a list of image or container tags within this environment.

        Parameters
        ----------
        image_tag : str | None, optional
            An optional image tag to list.  If None (the default), all image tags in
            this environment will be listed.
        container_tag : str | None, optional
            An optional container tag to list.  If None (the default), all container
            tags in the indicated image will be listed.

        Returns
        -------
        list[str]
            A list of fully qualified image or container tags within this environment,
            depending on the provided arguments.  An empty list will be returned if no
            matching tags could be found.  Use `parse()` to split the returned tags
            into their constituent parts.

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
        if image_tag is None:
            if container_tag is not None:
                raise TypeError("cannot specify a container when listing all images")
            return [f"{self.root}:{k}" for k in self.tags]

        # list all containers in an image
        image = self.tags.get(image_tag)
        if image is None:
            return []
        if container_tag is None:
            return [f"{self.root}:{image_tag}:{k}" for k in image.containers]

        # check for specific container in an image
        if container_tag in image.containers:
            return [f"{self.root}:{image_tag}:{container_tag}"]
        return []

    def _rm_container(self, image_tag: str, container_tag: str) -> None:
        image = self.tags.get(image_tag)
        if image is None:
            return
        container = image.containers.get(container_tag)
        if container is None:
            return
        docker_cmd(["container", "rm", "-f", container.id], check=False)
        image.containers.pop(container_tag, None)

    def _rm_image(self, image_tag: str) -> None:
        image = self.tags.get(image_tag)
        if image is None:
            return

        # remove all descendant containers
        out = docker_cmd([
            "container",
            "ls",
            "-a",
            "--no-trunc",
            "--filter", f"ancestor={image.id}",
            "--format", "{{.ID}}"
        ], check=False, capture_output=True)
        if out.returncode == 0:
            ids = list(out.stdout.splitlines())
            if ids:
                docker_cmd(["container", "rm", "-f", *ids], check=False)

        # remove image
        docker_cmd(["image", "rm", "-f", image.id], check=False)
        self.tags.pop(image_tag)

    def _rm_all(self) -> None:
        # remove all containers associated with this environment
        out = docker_cmd([
            "container",
            "ls",
            "-a",
            "--no-trunc",
            "--filter", f"label=BERTRAND_ENV={self.uuid}",
            "--format", "{{.ID}}"
        ], check=False, capture_output=True)
        if out.returncode == 0:
            ids = list(out.stdout.splitlines())
            if ids:
                docker_cmd(["container", "rm", "-f", *ids], check=False)

        # remove all images associated with this environment
        out = docker_cmd([
            "image",
            "ls",
            "-a",
            "--no-trunc",
            "--filter", f"label=BERTRAND_ENV={self.uuid}",
            "--format", "{{.ID}}"
        ], check=False, capture_output=True)
        if out.returncode == 0:
            ids = list(out.stdout.splitlines())
            if ids:
                docker_cmd(["image", "rm", "-f", *ids], check=False)

        # delete cache volumes
        cache_prefix = f"bertrand-{self.uuid[:13]}"
        out = docker_cmd([
            "volume",
            "ls",
            "--filter", f"name={cache_prefix}",
            "--format", "{{.Name}}"
        ], check=False, capture_output=True)
        if out.returncode == 0:
            names = list(out.stdout.splitlines())
            for name in names:
                if name.startswith(cache_prefix):
                    docker_cmd(["volume", "rm", "-f", name], check=False)

        # clear metadata
        self.tags.clear()

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

    def _build_container(
        self,
        image_tag: str,
        container_tag: str,
        container_args: list[str] | None
    ) -> Container:
        image = self.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        return image.build(
            env_root=self.root,
            env_uuid=self.uuid,
            image_tag=image_tag,
            container_tag=container_tag,
            container_args=container_args
        )

    def _build_image(self, image_tag: str, image_args: list[str] | None) -> Image:
        existing = self.tags.get(image_tag)
        if image_args is None:
            if existing is None:
                if image_tag:
                    raise KeyError(f"no image found for tag: '{image_tag}'")
                image_args = []  # empty tag implies empty args
            else:
                image_args = existing.args  # reuse existing args
        else:
            image_args = _normalize_args(image_args)  # define new args


        # build new image
        # NOTE: Docker + BuildKit will automatically reuse cached layers as long as
        # none of the inputs have changed (including the contents of the environment
        # directory, excluding patterns in .dockerignore).  Therefore, we can build
        # unconditionally, and check whether the ID has changed afterwards to detect
        # whether a rebuild was necessary.
        image = Image(
            version=VERSION,
            id="",  # corrected after build
            created=datetime.now(timezone.utc),
            args=image_args,
            containers={},
        )
        image_name = f"{_sanitize_name(self.root.name)}.{image_tag}.{self.uuid[:13]}"
        iid_file = _iid_file(self.root, image_name)
        try:
            iid_file.parent.mkdir(parents=True, exist_ok=True)
            docker_cmd([
                "build",
                *image_args,
                "-t", image_name,
                "-f", str(self.docker_file),
                "--iidfile", str(iid_file),
                "--label", "BERTRAND=1",
                "--label", f"BERTRAND_ENV={self.uuid}",
                "--label", f"BERTRAND_IMAGE={image_tag}",
                str(self.root),
            ])
            image.id = iid_file.read_text(encoding="utf-8").strip()  # build returns image ID
        finally:
            iid_file.unlink(missing_ok=True)
        if existing is not None and image.id == existing.id:
            return existing

        # rebuild downstream containers for new image, then replace existing image
        try:
            for container_tag, container in existing.containers.items() if existing else []:
                image.build(
                    env_root=self.root,
                    env_uuid=self.uuid,
                    image_tag=image_tag,
                    container_tag=container_tag,
                    container_args=container.args
                )
            self._rm_image(image_tag)
        except Exception as err:
            for container in image.containers.values():
                docker_cmd(["container", "rm", "-f", container.id], check=False)
            docker_cmd(["image", "rm", "-f", image.id], check=False)
            raise err
        self.tags[image_tag] = image
        return image

    def _build_all(self) -> Environment:
        for t in list(self.tags):
            self._build_image(t, None)
        return self

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
            If None (the default), all images in the environment will be built
            incrementally using their existing arguments.
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
            `image_args` are provided with an empty `image_tag`.
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

        # if no tag is provided, build all images
        if image_tag is None:
            if image_args is not None:
                raise TypeError("cannot provide arguments when building all images")
            return self._build_all()

        # build specific image
        return self._build_image(image_tag, image_args)

    def _start_container(
        self,
        image_tag: str,
        container_tag: str,
        container_args: list[str] | None
    ) -> Container:
        self._build_image(image_tag, None)
        container = self._build_container(image_tag, container_tag, container_args)
        inspect = container.inspect()
        assert inspect is not None, (
            f"failed to build container '{container_tag}' in image '{image_tag}'"
        )
        Container.start(inspect)
        return container

    def _start_image(self, image_tag: str) -> Image:
        image = self._build_image(image_tag, None)
        for container_tag, container in list(image.containers.items()):
            inspect = container.inspect()
            if inspect is None:
                self._rm_container(image_tag, container_tag)
            else:
                Container.start(inspect)
        return image

    def _start_all(self) -> Environment:
        for image_tag in list(self.tags):
            self._start_image(image_tag)
        return self

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
            associated with this tag.  If None (the default), all containers in the
            indicated image will be started.
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
            an empty `container_tag`.
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

        # start specific container
        return self._start_container(image_tag, container_tag, container_args)

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

        # ensure stdin and stdout can attach to a TTY
        if not sys.stdin.isatty() or not sys.stdout.isatty():
            raise CommandError(
                returncode=1,
                cmd=["bertrand", "enter", f"{self.root}:{image_tag}:{container_tag}"],
                stdout="",
                stderr="'bertrand enter' requires both stdin and stdout to be a TTY."
            )

        # launch interactive shell
        docker_exec([
            "exec",
            "-it",
            "-w", MOUNT,
            container.id,
            *SHELLS[self.shell]
        ])
        return container

    def _run_interactive(self) -> list[str]:
        if (
            sys.stdin.isatty() and
            sys.stdout.isatty() and
            sys.stderr.isatty() and
            os.environ.get("TERM", "") not in ("", "dumb")
        ):
            return ["-i", "-t"]
        return ["-i"]

    def _run_container(
        self,
        image_tag: str,
        container_tag: str,
        argv: list[str]
    ) -> Container:
        # ensure container has a valid entry point
        container = self._start_container(image_tag, container_tag, None)
        if not container.entry_point:
            raise CommandError(
                returncode=1,
                cmd=["bertrand", "run", f"{self.root}:{image_tag}:{container_tag}", *argv],
                stdout="",
                stderr=
                    "Cannot run command: container has no entry point defined.  Either "
                    "write a '__main__.py' file or include a C++ source file that "
                    "implements an 'int main()' function."
            )

        # run the command within the container context
        docker_cmd([
            "exec",
            *self._run_interactive(),
            "-w", MOUNT,
            container.id,
            *container.entry_point,
            *argv
        ])
        return container

    def _run_image(self, image_tag: str, argv: list[str]) -> Image:
        image = self._build_image(image_tag, None)
        for container_tag, container in list(image.containers.items()):
            inspect = container.inspect()
            assert inspect is not None, (
                f"failed to build container '{container_tag}' in image '{image_tag}'"
            )
            Container.start(inspect)

            # ensure container has a valid entry point
            if not container.entry_point:
                raise CommandError(
                    returncode=1,
                    cmd=["bertrand", "run", f"{self.root}:{image_tag}:{container_tag}", *argv],
                    stdout="",
                    stderr=
                        "Cannot run command: container has no entry point defined.  Either "
                        "write a '__main__.py' file or include a C++ source file that "
                        "implements an 'int main()' function to run."
                )

            # run the command within the container context
            docker_cmd([
                "exec",
                *self._run_interactive(),
                "-w", MOUNT,
                container.id,
                *container.entry_point,
                *argv
            ])
        return image

    def _run_all(self, argv: list[str]) -> Environment:
        for image_tag in list(self.tags):
            self._run_image(image_tag, argv)
        return self

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
        """Invoke a Docker container's entry point, starting or rebuilding it as
        necessary, and passing along any additional arguments.

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
            The arguments to pass to the container's entry point when running the
            command.  The entry point itself is prepended automatically.  If None
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
            container does not have a valid entry point.
        """
        if self.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke Docker from within a Bertrand container")
        if argv is None:
            argv = []

        # run all containers in this environment
        if image_tag is None:
            if container_tag is not None:
                raise TypeError("cannot specify a container when image tag is None")
            return self._run_all(argv)

        # run all containers in the specified image
        if container_tag is None:
            return self._run_image(image_tag, argv)

        # run a specific container
        return self._run_container(image_tag, container_tag, argv)

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
            for i in self.tags.values():
                for c in i.containers.values():
                    inspect = c.inspect()
                    if inspect is None:
                        continue
                    Container.stop(inspect, self.timeout)
            return self

        # stop all containers in an image
        if container_tag is None:
            image = self.tags.get(image_tag)
            if image is None:
                return None
            for c in image.containers.values():
                inspect = c.inspect()
                if inspect is None:
                    continue
                Container.stop(inspect, self.timeout)
            return image

        # stop a specific container in an image
        image = self.tags.get(image_tag)
        if image is None:
            return None
        container = image.containers.get(container_tag)
        if container is None:
            return None
        inspect = container.inspect()
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
            for i in self.tags.values():
                for c in i.containers.values():
                    inspect = c.inspect()
                    if inspect is None:
                        continue
                    Container.pause(inspect)
            return self

        # pause all containers in an image
        if container_tag is None:
            image = self.tags.get(image_tag)
            if image is None:
                return None
            for c in image.containers.values():
                inspect = c.inspect()
                if inspect is None:
                    continue
                Container.pause(inspect)
            return image

        # pause a specific container in an image
        image = self.tags.get(image_tag)
        if image is None:
            return None
        container = image.containers.get(container_tag)
        if container is None:
            return None
        inspect = container.inspect()
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
            for i in self.tags.values():
                for c in i.containers.values():
                    inspect = c.inspect()
                    if inspect is None:
                        continue
                    Container.resume(inspect)
            return self

        # resume all paused containers in an image
        if container_tag is None:
            image = self.tags.get(image_tag)
            if image is None:
                return None
            for c in image.containers.values():
                inspect = c.inspect()
                if inspect is None:
                    continue
                Container.resume(inspect)
            return image

        # resume a specific container in an image
        image = self.tags.get(image_tag)
        if image is None:
            return None
        container = image.containers.get(container_tag)
        if container is None:
            return None
        inspect = container.inspect()
        if inspect is None:
            return None
        Container.resume(inspect)
        return container

    def _prune_container(self, image_tag: str, container_tag: str) -> None:
        image = self.tags.get(image_tag)
        if image is None:
            return None
        container = image.containers.get(container_tag)
        if container is None:
            return None
        inspect = container.inspect()
        if inspect is None or Container.relocated(self.root, inspect):
            self._rm_container(image_tag, container_tag)
            return

    def _prune_image(self, image_tag: str) -> None:
        image = self.tags.get(image_tag)
        if image is None:
            return
        if image.inspect() is None:
            self._rm_image(image_tag)
            return
        for container_tag, container in list(image.containers.items()):
            inspect = container.inspect()
            if inspect is None or Container.relocated(self.root, inspect):
                self._rm_container(image_tag, container_tag)

    def _prune_all(self) -> None:
        for image_tag, image in list(self.tags.items()):
            if image.inspect() is None:
                self._rm_image(image_tag)
                continue
            for container_tag, container in list(image.containers.items()):
                inspect = container.inspect()
                if inspect is None or Container.relocated(self.root, inspect):
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

    def _restart_container(self, image_tag: str, container_tag: str) -> Container:
        image = self.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        container = image.containers.get(container_tag)
        if container is None:
            raise KeyError(f"no container found for tag: '{container_tag}'")

        # detect whether the current container is running
        inspect = container.inspect()
        if inspect is None:
            image.containers.pop(container_tag)
            running = False
        else:
            running = Container.running(inspect)

        # possibly rebuild the parent image and all downstream containers
        image = self._build_image(image_tag, None)

        # if the container was previously running, restart it
        container = image.containers.get(container_tag)
        assert container is not None, (
            f"failed to build container '{container_tag}' in image '{image_tag}'"
        )
        inspect = container.inspect()
        assert inspect is not None, (
            f"failed to build container '{container_tag}' in image '{image_tag}'"
        )
        if running:
            if Container.new(inspect):
                Container.start(inspect)
            else:
                Container.restart(inspect, self.timeout)
        return container

    def _restart_image(self, image_tag: str) -> Image:
        image = self.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")

        # record all running containers before rebuilding
        running: list[str] = []
        for container_tag, c in image.containers.items():
            inspect = c.inspect()
            if inspect is not None and Container.running(inspect):
                running.append(container_tag)

        # rebuild the image and all downstream containers
        image = self._build_image(image_tag, None)

        # restart previously running containers
        for container_tag in running:
            container = image.containers.get(container_tag)
            assert container is not None, (
                f"failed to build container '{container_tag}' in image '{image_tag}'"
            )
            inspect = container.inspect()
            assert inspect is not None, (
                f"failed to build container '{container_tag}' in image '{image_tag}'"
            )
            if Container.new(inspect):
                Container.start(inspect)
            else:
                Container.restart(inspect, self.timeout)

        return image

    def _restart_all(self) -> Environment:
        for image_tag in list(self.tags):
            self._restart_image(image_tag)
        return self

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
        """Restart running containers within this environment, possibly rebuilding any
        that are out of date.

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
        KeyError
            If an image or container tag is provided but could not be found.
        """
        if self.depth < 1:
            raise OSError("environment must be acquired as a context manager before accessing")
        if Environment.current() is not None:
            raise OSError("cannot invoke Docker from within a Bertrand container")

        # restart all containers in the environment
        if image_tag is None:
            if container_tag is not None:
                raise TypeError("cannot specify a container when stopping all images")
            return self._restart_all()

        # restart all containers in an image
        if container_tag is None:
            return self._restart_image(image_tag)

        # restart a specific container in an image
        return self._restart_container(image_tag, container_tag)

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
            "--filter", f"label=BERTRAND_ENV={self.uuid}",
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
            "--filter", f"label=BERTRAND_ENV={self.uuid}",
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

        image = self.tags.get(image_tag)
        if image is None:
            raise KeyError(f"no image found for tag: '{image_tag}'")
        container = image.containers.get(container_tag)
        if container is None:
            raise KeyError(f"no container found for tag: '{container_tag}'")
        docker_cmd(["container", "top", f"{container.id}"])


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
    out = list(docker_cmd([
        "container",
        "ls",
        "--all",
        "--filter", "label=BERTRAND=1",
        "--no-trunc",
        "--format={{.ID}}"
    ], capture_output=True).stdout.strip().splitlines())
    if not out:
        return []

    # inspect all containers to find their mount points
    inspects = json.loads(docker_cmd(["container", "inspect", *out], capture_output=True).stdout)
    assert isinstance(inspects, list)
    seen: set[Path] = set()
    result: list[str] = []
    for inspect in inspects:
        assert isinstance(inspect, dict)
        mount = Container.mount(inspect)  # type: ignore[arg-type]
        if mount is not None and mount not in seen:
            seen.add(mount)
            result.append(str(mount))
    return result


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
