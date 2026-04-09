"""Container-domain runtime helpers for Bertrand's Kubernetes control plane.

This module is the container-side bridge between Bertrand's managed
microK8s/containerd runtime and worktree configuration metadata. It is
responsible for:

1. Inspecting/removing/starting runtime containers.
2. Assembling `nerdctl container create` arguments from validated config.
3. Managing RPC sidecar startup/shutdown for interactive workflows.
"""
from __future__ import annotations

import asyncio
import json as json_parser
import math
import pathlib
import shlex
import stat
import sys
import time
import uuid
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path, PosixPath
from typing import Annotated, Literal, Self

from pydantic import (
    AfterValidator,
    AwareDatetime,
    BaseModel,
    ConfigDict,
    Field,
)

from ..config import Bertrand, Config
from ..config.core import AbsolutePath, NonEmpty, NoWhiteSpace, TOMLKey, Trimmed
from ..rpc import RPC_TIMEOUT
from ..run import (
    BERTRAND_ENV,
    CONTAINER_ID_ENV,
    CONTAINER_RUNTIME_ENV,
    CONTAINER_RUNTIME_MOUNT,
    CONTAINER_SOCKET,
    ENV_ID_ENV,
    IMAGE_ID_ENV,
    IMAGE_TAG_ENV,
    METADATA_DIR,
    PROJECT_ENV,
    PROJECT_MOUNT,
    WORKTREE_ENV,
    WORKTREE_MOUNT,
    atomic_write_text,
    inside_image,
    nerdctl,
)
from .network import format_cpus, format_network
from .volume import format_volumes


def _to_utc(value: datetime) -> datetime:
    if value.tzinfo is None or value.tzinfo.utcoffset(value) is None:
        raise ValueError(f"'created' must be a valid ISO timestamp: {value}")
    return value.astimezone(UTC)


type ImageId = NonEmpty[NoWhiteSpace]
type ContainerId = NonEmpty[NoWhiteSpace]
type ContainerState = Literal[
    "created",
    "restarting",
    "running",
    "removing",
    "paused",
    "exited",
    "dead",
]
type CreatedAt = Annotated[AwareDatetime, AfterValidator(_to_utc)]


class Container(BaseModel):
    """Type hint for container inspect output.

    Due to the ephemeral container model, this data is not persisted to disk.

    https://github.com/containerd/nerdctl/blob/main/docs/command-reference.md#whale-nerdctl-inspect
    """
    model_config = ConfigDict(extra="allow")
    Id: ContainerId
    Created: CreatedAt

    class _State(BaseModel, total=False):
        """Type hint for container state information."""
        model_config = ConfigDict(extra="allow")
        Status: ContainerState
        OOMKilled: bool

    State: _State

    class _Config(BaseModel, total=False):
        """Type hint for container configuration details."""
        model_config = ConfigDict(extra="allow")
        Labels: Annotated[dict[str, str], Field(default_factory=dict)]

    Config: _Config
    Image: ImageId
    Path: Annotated[Trimmed | None, Field(default=None)]
    Args: Annotated[list[str], Field(default_factory=list)]

    class _Mounts(BaseModel, total=False):
        """Type hint for container mount information."""
        model_config = ConfigDict(extra="allow")
        Type: Literal["bind", "volume", "tmpfs", "npipe"]
        Source: AbsolutePath
        Destination: AbsolutePath
        RW: bool
        Propagation: Literal["shared", "slave", "private", "rshared", "rslave", "rprivate"]

    Mounts: list[_Mounts]

    @property
    def project_root(self) -> pathlib.Path | None:
        """Extract the host project root path for this container from Bertrand
        labels.

        Returns
        -------
        Path | None
            The resolved host project root path for this container, or None if
            required labels are missing or invalid.
        """
        labels = self.Config.Labels
        value = labels.get(PROJECT_ENV)
        if not value:
            return None
        try:
            path = Path(value).expanduser()
            if not path.is_absolute():
                return None
            return path.resolve()
        except OSError:
            return None

    @property
    def worktree(self) -> pathlib.Path | None:
        """Extract the host worktree path for this container from Bertrand labels.

        Returns
        -------
        Path | None
            The resolved host worktree path for this container, or None if required
            labels are missing or invalid.
        """
        root = self.project_root
        if root is None:
            return None
        labels = self.Config.Labels
        value = labels.get(WORKTREE_ENV)
        if not value:
            return None
        value = value.strip()
        if not value:
            return None
        if value == ".":
            return root
        path = Path(value)
        if path.is_absolute() or any(part in (".", "..") for part in path.parts):
            return None
        try:
            candidate = (root / path).resolve()
            if not candidate.is_relative_to(root):
                return None
            return candidate
        except OSError:
            return None

    @property
    def runtime(self) -> pathlib.Path | None:
        """Extract the host runtime directory for this container from Bertrand
        labels.

        Returns
        -------
        Path | None
            The resolved host runtime directory for this container, or None if
            required labels are missing or invalid.
        """
        worktree = self.worktree
        if worktree is None:
            return None
        labels = self.Config.Labels
        value = labels.get(CONTAINER_RUNTIME_ENV)
        if not value:
            return None
        value = value.strip()
        if not value:
            return None
        path = Path(value)
        if (
            path.is_absolute() or
            not path.parts or
            any(part in (".", "..") for part in path.parts)
        ):
            return None
        try:
            candidate = (worktree / path).resolve()
            if not candidate.is_relative_to(worktree):
                return None
            return candidate
        except OSError:
            return None

    @classmethod
    async def inspect(cls, ids: list[ContainerId]) -> list[Self]:
        """Inspect one or more containers via the container runtime.

        Parameters
        ----------
        ids : list[ContainerId]
            The unique container runtime IDs.

        Returns
        -------
        list[Container]
            A list of validated JSON responses from the container runtime. Missing
            containers are omitted.
        """
        if not ids:
            return []
        result = await nerdctl(
            ["container", "inspect", *ids],
            check=False,
            capture_output=True,
        )
        if result.returncode != 0:
            return []
        stdout = result.stdout.strip()
        if not stdout:
            return []
        data = json_parser.loads(stdout)
        if not isinstance(data, list):
            return []
        return [cls.model_validate(item) for item in data]

    @staticmethod
    async def remove(ids: list[ContainerId], *, force: bool, timeout: float) -> None:
        """Remove one or more containers via the runtime.

        Parameters
        ----------
        ids : list[ContainerId]
            Container IDs to remove.
        force : bool
            If True, remove even when running; otherwise remove only when stopped.
        timeout : float
            Maximum time in seconds to wait for graceful stop before forceful kill.
        """
        cmd = [
            "container",
            "rm",
            "--depend",  # remove dependent containers
            "-v",  # remove anonymous volumes
            "-i",  # ignore missing containers
            "-t", str(int(math.ceil(timeout))),
        ]
        if force:
            cmd.append("-f")
        cmd.extend(ids)
        await nerdctl(cmd)

    async def start(
        self,
        *,
        quiet: bool,
        timeout: float | None,
        attach: bool,
        interactive: bool,
    ) -> None:
        """Start this container.

        Parameters
        ----------
        quiet : bool
            If True, suppress output from the start operation.
        timeout : float | None
            Optional timeout in seconds for the start operation.
        attach : bool
            If True, attach stdout/stderr to the console.
        interactive : bool
            If True, attach stdin for interactive input.
        """
        cmd = ["container", "start"]
        if attach:
            cmd.append("-a")
        if interactive:
            cmd.append("-i")
            cmd.append("--detach-keys=")
        cmd.append(self.Id)
        await nerdctl(
            cmd,
            timeout=timeout,
            capture_output=quiet,
        )


def _render_bootstrap_script(
    *,
    container_cid: PosixPath,
    container_worktree: PosixPath,
    container_runtime: PosixPath,
) -> str:
    return "\n".join(
        [
            "#!/bin/sh",
            "set -eu",
            f"CID_FILE={shlex.quote(str(container_cid))}",
            f"TARGET_WORKTREE={shlex.quote(str(container_worktree))}",
            f"TARGET_RUNTIME={shlex.quote(str(container_runtime))}",
            f"rm -rf {shlex.quote(str(WORKTREE_MOUNT))}",
            (
                "ln -s "
                "\"$TARGET_WORKTREE\" "
                f"{shlex.quote(str(WORKTREE_MOUNT))}"
            ),
            f"rm -rf {shlex.quote(str(CONTAINER_RUNTIME_MOUNT))}",
            (
                "ln -s "
                "\"$TARGET_RUNTIME\" "
                f"{shlex.quote(str(CONTAINER_RUNTIME_MOUNT))}"
            ),
            "if command -v git >/dev/null 2>&1; then",
            (
                "    git config --global --add safe.directory "
                f"{shlex.quote(str(WORKTREE_MOUNT))} >/dev/null 2>&1 || true"
            ),
            (
                "    git config --global --add safe.directory "
                "\"$TARGET_WORKTREE\" >/dev/null 2>&1 || true"
            ),
            "fi",
            "if [ -f \"$CID_FILE\" ]; then",
            "    CID=\"$(cat \"$CID_FILE\" 2>/dev/null || true)\"",
            "    if [ -n \"$CID\" ]; then",
            f"        export {CONTAINER_ID_ENV}=\"$CID\"",
            "    fi",
            "fi",
            "exec \"$@\"",
            "",
        ]
    )


@dataclass(frozen=True)
class ContainerArgs:
    """A full argument tail and metadata for `nerdctl container create`."""
    argv: list[str]
    run_id: str
    runtime_dir: Path
    cid_file: Path
    bootstrap_script: Path


async def container_args(
    config: Config,
    *,
    env_id: str,
    tag: TOMLKey,
    image_id: str,
    cmd: Sequence[NonEmpty[Trimmed]] = (),
    env_vars: Mapping[NonEmpty[NoWhiteSpace], Trimmed] | None = None,
) -> ContainerArgs:
    """Assemble runtime container create arguments from config state.

    Parameters
    ----------
    config : Config
        Active configuration context for the target worktree.
    env_id : str
        Canonical environment UUID to include in runtime labels.
    tag : TOMLKey
        Workload tag to materialize.
    image_id : str
        Source image ID for the container create operation.
    cmd : Sequence[str], optional
        Optional command override. If empty, configured workload command is used.
    env_vars : Mapping[str, str] | None, optional
        Optional extra environment variables to inject at create time.

    Returns
    -------
    ContainerArgs
        Runtime `nerdctl container create` argument bundle and generated artifacts.
    """
    if inside_image():
        raise RuntimeError("container_args() cannot be called from within a container")
    env_id = env_id.strip()
    if not env_id:
        raise ValueError("environment ID cannot be empty when forming container args")
    image_id = image_id.strip()
    if not image_id:
        raise ValueError("image ID cannot be empty when forming container args")

    bertrand = config.get(Bertrand)
    if bertrand is None:
        raise TypeError(
            f"missing 'bertrand' configuration for environment at {config.root}"
        )
    workload = bertrand.workload.get(tag)
    if workload is None:
        raise ValueError(
            f"unknown workload tag '{tag}' for environment at {config.root}"
        )
    if cmd:
        _cmd: list[str] = []
        for part in cmd:
            part = part.strip()
            if not part:
                raise ValueError("entry point arguments must be non-empty strings")
            _cmd.append(part)
        cmd = _cmd
    else:
        cmd = workload.cmd
        if not cmd:
            raise ValueError(
                f"tag '{tag}' has no effective entry point: provide a command "
                f"override or configure [tool.bertrand.workload.{tag}.cmd] for this "
                "tag"
            )

    run_id = uuid.uuid4().hex
    runtime = METADATA_DIR / "containers" / f"{tag}.{run_id}"
    host_runtime_dir = config.root / runtime
    host_runtime_dir.mkdir(parents=True, exist_ok=True)
    host_cid = host_runtime_dir / "cid"
    host_bootstrap = host_runtime_dir / "entrypoint.sh"
    if config.worktree.parts:
        worktree_env = config.worktree.as_posix()
        container_worktree = PROJECT_MOUNT / worktree_env
    else:
        worktree_env = "."
        container_worktree = PROJECT_MOUNT
    container_runtime = container_worktree / runtime
    container_cid = container_runtime / "cid"
    container_bootstrap = container_runtime / "entrypoint.sh"
    atomic_write_text(
        host_bootstrap,
        _render_bootstrap_script(
            container_cid=container_cid,
            container_worktree=container_worktree,
            container_runtime=container_runtime,
        ),
        encoding="utf-8",
    )
    host_bootstrap.chmod(0o755)

    argv = [
        "--init",
        "--rm",
        "--cidfile",
        str(host_cid),
        "--label",
        f"{BERTRAND_ENV}=1",
        "--label",
        f"{PROJECT_ENV}={config.repo.root}",
        "--label",
        f"{WORKTREE_ENV}={worktree_env}",
        "--label",
        f"{CONTAINER_RUNTIME_ENV}={runtime}",
        "--label",
        f"{ENV_ID_ENV}={env_id}",
        "--label",
        f"{IMAGE_ID_ENV}={image_id}",
        "--label",
        f"{IMAGE_TAG_ENV}={tag}",
        "-v",
        f"{config.repo.root}:{PROJECT_MOUNT}",
        "-e",
        f"{BERTRAND_ENV}=1",
        "-e",
        f"{PROJECT_ENV}={config.repo.root}",
        "-e",
        f"{WORKTREE_ENV}={worktree_env}",
        "-e",
        f"{CONTAINER_RUNTIME_ENV}={runtime}",
        "-e",
        f"{ENV_ID_ENV}={env_id}",
        "-e",
        f"{IMAGE_ID_ENV}={image_id}",
        "-e",
        f"{IMAGE_TAG_ENV}={tag}",
    ]
    if env_vars:
        for key, value in sorted(env_vars.items()):
            key = key.strip()
            if not key or any(c.isspace() for c in key):
                raise ValueError(
                    "environment variable keys must be non-empty strings without "
                    f"whitespace: {key!r}"
                )
            argv.extend(["-e", f"{key}={value.strip()}"])
    argv.extend(
        [
            *(await format_volumes(config, tag, env_id)),
            *format_network(bertrand.network.run),
            *format_cpus(workload.cpus),
            "--entrypoint",
            str(container_bootstrap),
            image_id,
            *cmd,
        ]
    )
    return ContainerArgs(
        argv=argv,
        run_id=run_id,
        runtime_dir=runtime,
        cid_file=runtime / "cid",
        bootstrap_script=runtime / "entrypoint.sh",
    )


async def start_rpc_sidecar(
    *,
    container: Container,
    container_bin: Path,
    deadline: float,
    strict: bool,
    warn_context: str | None = None,
) -> asyncio.subprocess.Process | None:
    """Start the RPC sidecar and wait for socket readiness.

    Parameters
    ----------
    container : Container
        Created container metadata used to determine runtime socket location.
    container_bin : Path
        Host path to the container runtime CLI used by the sidecar.
    deadline : float
        Monotonic deadline for sidecar readiness.
    strict : bool
        If True, raise startup errors. If False, emit warnings and continue.
    warn_context : str | None, optional
        Optional warning prefix used when `strict=False`.

    Returns
    -------
    asyncio.subprocess.Process | None
        Sidecar process handle when ready, or None in non-strict warning mode.
    """
    runtime = container.runtime
    if runtime is None:
        err = OSError(
            "created container is missing runtime label metadata needed for RPC "
            f"sidecar flow: {container.Id}"
        )
        if strict:
            raise err
        text = warn_context or "bertrand: failed to start RPC sidecar"
        print(f"{text}\n{err}", file=sys.stderr)
        return None
    host_socket = runtime / CONTAINER_SOCKET.name

    sidecar: asyncio.subprocess.Process | None = None
    try:
        sidecar = await asyncio.create_subprocess_exec(
            "bertrand-rpc",
            "--socket",
            str(host_socket),
            "--container-id",
            container.Id,
            "--container-bin",
            str(container_bin),
            stdin=asyncio.subprocess.DEVNULL,
            stdout=asyncio.subprocess.DEVNULL,
            stderr=asyncio.subprocess.DEVNULL,
        )
        while True:
            socket_ready = (
                host_socket.exists() and
                stat.S_ISSOCK(host_socket.lstat().st_mode)
            )
            if socket_ready:
                return sidecar
            if sidecar.returncode is not None:
                raise OSError(f"bertrand-rpc exited early with code {sidecar.returncode}")
            if time.monotonic() >= deadline:
                raise TimeoutError(
                    "timed out waiting for bertrand-rpc sidecar readiness "
                    f"(socket={host_socket})"
                )
            await asyncio.sleep(0.1)
    except Exception as err:
        await stop_rpc_sidecar(sidecar)
        if strict:
            raise
        text = warn_context or "bertrand: failed to start RPC sidecar"
        print(f"{text}\n{err}", file=sys.stderr)
        return None


async def stop_rpc_sidecar(sidecar: asyncio.subprocess.Process | None) -> None:
    """Best-effort sidecar shutdown with terminate-then-kill fallback."""
    if sidecar is None or sidecar.returncode is not None:
        return
    sidecar.terminate()
    try:
        await asyncio.wait_for(sidecar.wait(), timeout=RPC_TIMEOUT)
    except Exception:
        sidecar.kill()
        try:
            await sidecar.wait()
        except Exception:
            pass
