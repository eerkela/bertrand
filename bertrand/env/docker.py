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
from typing import Iterable, List, Literal, TypedDict

from .docker_engine import docker_cmd, docker_exec
from .run import CommandError, atomic_write_text, confirm, host_user_ids, run, sudo_prefix

#pylint: disable=redefined-builtin, global-statement


MOUNT: str = "/env"
LABEL: str = "bertrand"


# TODO: here's how I expect the environment spec to work:
# - Directory structure:
#       myproject/                  # per-project environment root
#           .devcontainer/          # VSCode devcontainer config
#               devcontainer.json   # TODO: this would have to key on digest somehow, to get proper config
#           .vscode/                # optional VSCode config
#               tasks.json          # tooling integrations
#               settings.json       # tooling integrations
#           .bertrand/
#               tag/
#                   <digest>.json   # metadata for each build configuration by digest
#               env.json            # metadata about the overall environment + tag lookup table(s)
#           .gitignore              # files to ignore in the environment
#           .dockerignore           # files to ignore during docker build
#           Dockerfile              # base dockerfile for the environment
#           pyproject.toml          # project configuration for pip install
# - The container id is what links the environment directory back to its container, and
#   is set to the internal Docker ID for the container AFTER it has been created.
# - A SHA256 digest of the Dockerfile content + build args is stored in the metadata,
#   and used to detect when the image needs to be rebuilt due to Dockerfile changes or
#   different build arguments.
# - The `BERTRAND_ENV` environment variable in each container references `digest`
#   inside the running container, which can then be mapped back to the container id by
#   accessing the digest file inside `/env/.bertrand/tag/` (bind-mounted), allowing it
#   to be detected from any location or subprocess within the container, for reverse
#   lookup purposes, as well as detecting whether we are currently inside a Bertrand
#   environment at all.
# - `.bertrand/env.json` stores project-level metadata, including a mapping from human-
#   readable tags to normalized build argument hashes, and then another mapping from
#   normalized build argument hashes to up-to-date digests.  Combining the two allows
#   users to select images by either tag or build args.  Adding a human-readable tag
#   would simply add an entry to the tag -> build arg mapping, while providing
#   equivalent build args would end up mapping to the same digest, and not a separate
#   container.  If/when an environment gets pushed to Docker Hub or a similar
#   repository, the tag map would be used to determine all the tags to push for the
#   project.
# - Finding the project root given a container id from the host system involves
#   extracting the bind mount using `docker inspect`, and any tag digests would be
#   available at `/.bertrand/tag/` within that mount.  I would then add another table
#   to `env.json` mapping container id -> digest.  That way, you can always reliably
#   map a container back to its digest file, both inside and outside the container,
#   and the digest file becomes a single source of truth.
# - Container creation moves out of `bertrand init myproject` and into
#   `bertrand enter myproject`, `bertrand run myproject`, `bertrand start myproject`,
#   etc.  You can specify `myproject:tag` and/or provide manual build args to select a
#   specific variant of the environment to create/enter/start/run.  The build args must
#   be normalized (sorted, duplicates removed, whitespace handled) before hashing to
#   ensure consistent results.
#
# This would present by far the best UX, and make dealing with multiple containers per
# environment much easier.


def _bertrand_dir(env_root: Path) -> Path:
    return env_root / ".bertrand"


def _env_file(env_root: Path) -> Path:
    return _bertrand_dir(env_root) / "env.json"


def _tag_dir(env_root: Path) -> Path:
    return _bertrand_dir(env_root) / "tag"


def _docker_file(env_root: Path) -> Path:
    return env_root / "Dockerfile"


def _docker_ignore(env_root: Path) -> Path:
    return env_root / ".dockerignore"


# TODO: the environment file may need to be protected by a lock for proper
# synchronization


@dataclass(frozen=True)
class DockerEnvironment:
    """On-disk metadata representing environment-level data structures, which map from
    human-readable tags to build argument hashes, and then to unique image digests,
    which represent individual Docker containers.
    """
    tags: dict[str, str]  # human-readable tag -> build arg hash
    builds: dict[str, str]  # build arg hash -> digest
    ids: dict[str, str]  # container id -> digest


def _write_environment(env_root: Path, env: DockerEnvironment | None) -> None:
    env_root.mkdir(parents=True, exist_ok=True)

    # if an environment file is given, write it to disk
    if env is not None:
        atomic_write_text(
            _env_file(env_root),
            json.dumps(asdict(env), indent=2) + "\n"
        )
        return

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

    # init .bertrand/env.json
    env_file = _env_file(env_root)
    if not env_file.exists():
        env_file.parent.mkdir(parents=True, exist_ok=True)
        atomic_write_text(
            env_file,
            json.dumps({
                "tags": {},  # human-readable tag -> build arg hash
                "builds": {},  # build arg hash -> digest
                "ids": {},  # container id -> digest
            }, indent=2) + "\n"
        )


def _read_environment(env_root: Path) -> DockerEnvironment | None:
    env_file = _env_file(env_root)
    if not env_file.exists():
        return None

    try:
        data = json.loads(env_file.read_text(encoding="utf-8"))
        if not isinstance(data, dict):
            raise ValueError("environment metadata must be a JSON mapping")

        # validate tags
        tags = data.get("tags")
        if not isinstance(tags, dict) or not all(
            isinstance(k, str) and isinstance(v, str) for k, v in tags.items()
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

        return DockerEnvironment(tags=tags, builds=builds, ids=ids)

    except Exception as err:
        raise ValueError(f"Invalid environment metadata at {env_file}: {err}") from err


@dataclass(frozen=True)
class DockerContainer:
    """On-disk metadata representing a local Bertrand Docker container, which is a
    built image of an encapsulating environment.  An environment can have many
    containers, each built with a different set of Dockerfile arguments.

    Specific care is taken not to store anything that references the host filesystem or
    container name, in order to allow renaming/relocation of the environment directory.
    """
    version: int  # version number for backwards compatibility
    digest: str  # SHA256 of Dockerfile content + args to detect incremental rebuilds
    image: str  # UUID tag for the built image
    container: str  # Unique container ID linking this environment back to its Docker host
    created: str  # ISO timestamp
    shell: list[str]  # shell command to execute during `bertrand enter`


def _arg_bytes(build_args: Iterable[str]) -> bytes:
    out = bytearray()
    for s in build_args:
        for s2 in shlex.split(s):
            out.extend(len(s2).to_bytes(8, "big"))  # length prefix to avoid ambiguity
            out.extend(s2.encode("utf-8", "surrogateescape"))
    return bytes(out)


def _docker_digest(env_root: Path, build_args: Iterable[str]) -> str:
    h = hashlib.sha256()
    h.update(_arg_bytes(build_args))
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


def _container_name(env_root: Path, tag: str | None, build_args: Iterable[str]) -> str:
    env_root = env_root.expanduser().resolve()
    if tag is None:
        arg_hash = hashlib.sha256(_arg_bytes(build_args)).hexdigest()[:12]
        return f"{_sanitize_container_name(env_root.name)}.{arg_hash}"
    return f"{_sanitize_container_name(env_root.name)}.{_sanitize_container_name(tag)}"


def _normalize_shell(value: object) -> list[str]:
    if isinstance(value, list) and all(isinstance(x, str) and x for x in value):
        return value
    if isinstance(value, str):
        argv = shlex.split(value.strip())
        if argv:
            return argv
        raise ValueError("shell must not be empty")
    raise ValueError("shell must be a string or list[str]")


def _write_container(env_root: Path, container: DockerContainer) -> None:
    tag_dir = _tag_dir(env_root)
    tag_dir.mkdir(parents=True, exist_ok=True)
    atomic_write_text(
        tag_dir / f"{container.digest}.json",
        json.dumps(asdict(container), indent=2) + "\n"
    )


def _read_container(env_root: Path, digest: str) -> DockerContainer | None:
    path = _tag_dir(env_root) / f"{digest}.json"
    if not path.exists():
        return None

    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(data, dict):
            raise ValueError("environment metadata must be a JSON object")

        # validate version
        version = data.get("version")
        if not isinstance(version, int) or version <= 0:
            raise ValueError(f"missing or invalid 'version' field: {version}")

        # validate digest
        container_digest = data.get("digest")
        if not isinstance(container_digest, str) or not container_digest.strip():
            raise ValueError(f"missing or invalid 'digest' field: {container_digest}")
        if container_digest != digest:
            raise ValueError(
                f"digest field does not match filename: {container_digest} != {digest}"
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

        # validate shell command
        shell = data.get("shell")
        if not isinstance(shell, (str, list)) or not shell:
            raise ValueError("missing required field: shell")
        data["shell"] = _normalize_shell(shell)

        return DockerContainer(**data)

    except Exception as err:
        raise ValueError(f"Invalid environment metadata at {path}: {err}") from err


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


def _inspect_container(name_or_id: str) -> ContainerInspect | None:
    try:
        result = docker_cmd(["inspect", name_or_id], capture_output=True)
        if not result.stdout:
            return None
        data = json.loads(result.stdout)
        return data[0] if data else None
    except Exception:  # pylint: disable=broad-except
        return None


def _get_mount_source(inspect: ContainerInspect) -> Path | None:
    mounts = inspect.get("Mounts") or []
    for m in mounts:
        if m.get("Type") == "bind" and m.get("Destination") == MOUNT:
            src = m.get("Source")
            if src:
                return Path(src).expanduser()
    return None


def _remove_container(container: DockerContainer, *, force: bool = False) -> None:
    if force:
        docker_cmd([
            "image",
            "rm",
            "-f",
            f"bertrand-{container.image}"
        ], check=False, capture_output=True)
        docker_cmd(["container", "rm", "-f", container.container], check=False, capture_output=True)
    else:
        docker_cmd([
            "image",
            "rm",
            f"bertrand-{container.image}"
        ], check=False, capture_output=True)
        docker_cmd(["container", "rm", container.container], check=False, capture_output=True)


def _ensure_container(
    env_root: Path,
    container: DockerContainer,
    tag: str | None,
    build_args: list[str]
) -> None:
    # NOTE: `container` is guaranteed to be present on disk and match `build_args`.
    # Digest conflicts are resolved before calling this function, by removing the
    # previous digest file and container, and then calling this function to recreate
    # it.
    env_root = env_root.expanduser().resolve()

    # if the environment directory has moved, the existing container's bind mount may
    # be stale.  Docker does not support editing mounts in-place, but we can stop, rm,
    # and recreate the container if needed.  Note that this will remove any data that
    # is not stored in the environment directory (i.e., in the container's root
    # filesystem), but those can be recovered by rebuilding the container in
    # reproducible fashion.
    inspect = _inspect_container(container.container)
    if inspect:
        mount = _get_mount_source(inspect)
        if mount is not None:
            try:
                mount = mount.resolve()
                if mount != env_root:
                    mount = None
            except OSError:
                mount = None

        # if mount is missing or points somewhere else, rebuild
        if mount is None:
            docker_cmd(["stop", container.container], check=False, capture_output=True)
            _remove_container(container, force=True)

            # delete from environment metadata
            env = _read_environment(env_root)
            if env is None:
                raise FileNotFoundError(f"Failed to read environment metadata at: {env_root}")
            if tag is not None:
                env.tags.pop(tag, None)
            env.builds.pop(container.digest, None)
            env.ids.pop(container.container, None)
            _write_environment(env_root, env)

            inspect = None

    # if no container is found, create a new one
    if inspect is None:
        # check for base image and build if missing
        image_name = f"bertrand-{container.image}"  # unambiguous
        image_info = docker_cmd(["image", "inspect", image_name], check=False, capture_output=True)
        if image_info.returncode != 0:
            docker_cmd([
                "build",
                *build_args,
                "-t", image_name,
                "-f", str(_docker_file(env_root)),
                "--label", f"{LABEL}=1",
                str(env_root),
            ])

        # create container from image
        container_name = _container_name(env_root, tag, build_args)  # human-readable
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
            raise RuntimeError(f"Failed to create container: {container_name}")

        # set final container id in metadata
        container_id = docker_cmd(["ps", "-aqf", f"name={container_name}"], capture_output=True)
        if not container_id.stdout:
            raise RuntimeError(f"Failed to get container ID for: {container_name}")
        container = replace(container, container=container_id.stdout.strip())
        _write_container(env_root, container)

        # write new container id to environment metadata
        env = _read_environment(env_root)
        if env is None:
            raise FileNotFoundError(f"Failed to read environment metadata at: {env_root}")
        if tag is not None:
            env.tags[tag] = container.digest
        env.builds[container.digest] = container.digest
        env.ids[container.container] = container.digest
        _write_environment(env_root, env)

    # start container if not running
    running = bool(((inspect.get("State") or {}).get("Running")))
    if not running:
        docker_cmd(["start", container.container])


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





def _image_exists(tag: str) -> bool:
    try:
        docker_cmd(["image", "inspect", tag], capture_output=True)
        return True
    except CommandError:
        return False


def _ensure_image_built(env_root: Path, container: DockerContainer) -> DockerContainer:
    env_root = env_root.expanduser().resolve()

    # make sure dockerfile is present
    docker_file = _docker_file(env_root)
    if not docker_file.exists():
        raise FileNotFoundError(f"Missing Dockerfile at: {docker_file}")

    # 

    digest = _docker_digest(dockerfile, spec.docker_build_args)
    tag = _image_tag(spec)

    needs_build = not _image_exists(tag) or spec.dockerfile_digest != digest
    if needs_build:
        docker_cmd(["image", "inspect", image], capture_output=True)

        _pull_image(spec.image)
        docker_cmd([
            "build",
            *spec.docker_build_args,
            "-t", tag,
            "-f", str(dockerfile),
            "--label", f"{LABEL}=1",
            "--label", f"{LABEL_ENV_ID}={spec.env_id}",
            str(env_root),
        ])

    # persist digest in metadata
    if spec.dockerfile_digest != digest:
        spec2 = replace(spec, dockerfile_digest=digest)
        _write_environment(env_root, spec2)
        return spec2

    return spec






def _container_build_args(env_root: Path, spec: DockerEnvironment) -> list[str]:
    env_root = env_root.expanduser().resolve()
    container = _container_name(spec)
    args = [
        "create",
        "--init",
        f"--name={container}",
        f"--hostname={container}",
        f"--workdir={MOUNT}",
        "--security-opt=no-new-privileges",

        # Hard-identify this container as Bertrand-managed.
        "--label", f"{LABEL}=1",
        "--label", f"{LABEL_ENV_ID}={spec.env_id}",
        "--label", f"{LABEL_MOUNT}={MOUNT}",
        "--label", f"{LABEL_VERSION}={spec.version}",
        "--label", f"{LABEL_CREATED}={spec.created}",
        "--label", f"{LABEL_IMAGE}={spec.image}",
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
        "-v", f"{str(env_root)}:{MOUNT}",
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


def in_environment() -> bool:
    """Detect whether the current process is running inside a Bertrand Docker
    container.

    Returns
    -------
    bool
        True if running inside a Bertrand Docker container, false otherwise.
    """
    return bool(os.environ.get("BERTRAND_ENV"))


def list_environments(key: str) -> list[str]:
    """List the container ids of all Bertrand Docker environments that match a given id
    or name fragment.

    Parameters
    ----------
    key : str
        An exact id or fragment of the environment name to match against.  If a name
        is given, then it may be a partial match for one or more containers.

    Returns
    -------
    list[str]
        A list of matching container ids.  Empty if no matches are found, or possibly
        multiple matches if the key is ambiguous.
    """
    # try an exact ID match first
    result = docker_cmd([
        "ps",
        "--filter", f"label={LABEL}=1",
        "--filter", f"id={key}",
    ], check=False, capture_output=True)
    if result.returncode == 0 and result.stdout and result.stdout.strip():
        return [key]

    # fall back to partial name match
    result = docker_cmd([
        "ps",
        "--filter", f"label={LABEL}=1",
        "--filter", f"name={key}",
        "--format", "{{.ID}}"
    ], check=False, capture_output=True)
    if result.returncode == 0 and result.stdout and result.stdout.strip():
        return [line.split(None, 1)[0] for line in result.stdout.splitlines()]

    # no matches
    return []


def find_environment(anchor: str | Path | None = None) -> Path | None:
    """Find the environment directory corresponding to a Bertrand Docker environment
    by container id, name, or subordinate path, assuming one exists.

    Parameters
    ----------
    anchor : str | Path | None
        The container id, name, or path to search from, or None.  If this is a string,
        then it will first be interpreted as an exact container id or name, and does
        not permit partial matches.  If a container is found, then its mount path will
        be obtained via `docker inspect`.  If no container is found, or if `anchor` is
        a Path, then the path will be searched upwards for a `.bertrand/env.json` file,
        which defines the root path.  If it is None (the default), then the environment
        will be found using environment variables from the current process.

    Returns
    -------
    Path | None
        A resolved path to the environment directory, or `None` if no environment
        could be found.
    """
    # if None, map to container name by concatenating environment id from env
    if anchor is None:
        anchor = f"bertrand-{os.environ.get("BERTRAND_ENV")}"

    # try container id or name first
    if isinstance(anchor, str):
        result = docker_cmd(["inspect", anchor], check=False, capture_output=True)
        if result.returncode == 0:
            data = json.loads(result.stdout)
            if data:
                return _get_mount_source(data[0])
        try:
            anchor = Path(anchor)
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
    docker_exec(["exec", "-it", "-w", MOUNT])


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
