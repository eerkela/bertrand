"""Image build and metadata primitives for Bertrand's kubernetes runtime.

This module owns image-domain behavior only: generating `nerdctl build`
arguments from validated config state, rendering generated Containerfiles, and
tracking persistent image metadata used by environment orchestration. It does
not orchestrate environment lifecycle directly; callers provide the minimal
runtime inputs (`Config`, `env_id`) needed for image/container materialization.
"""
from __future__ import annotations

import json
import os
import re
import time
import uuid
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Annotated

import jinja2
import packaging.version
from pydantic import (
    AfterValidator,
    AwareDatetime,
    BaseModel,
    ConfigDict,
    PositiveInt,
)

from ..config import Bertrand, Config, PyProject
from ..config.core import (
    NonEmpty,
    NoWhiteSpace,
    OCIImageRef,
    TOMLKey,
    Trimmed,
    locate_template,
)
from ..run import (
    BERTRAND_ENV,
    ENV_ID_ENV,
    IMAGE_ID_ENV,
    IMAGE_TAG_ENV,
    METADATA_DIR,
    WORKTREE_MOUNT,
    Scalar,
    atomic_write_text,
    inside_image,
    nerdctl,
    nerdctl_ids,
)
from ..version import VERSION
from .container import Container, container_args
from .network import format_network
from .volume import collect_mount_specs, gc_volumes


def _to_utc(value: datetime) -> datetime:
    if value.tzinfo is None or value.tzinfo.utcoffset(value) is None:
        raise ValueError(f"'created' must be a valid ISO timestamp: {value}")
    return value.astimezone(UTC)


type ImageId = NonEmpty[NoWhiteSpace]
type CreatedAt = Annotated[AwareDatetime, AfterValidator(_to_utc)]
type ArgsList = list[NonEmpty[Trimmed]]


def _dependency_copy_specs(from_images: Sequence[OCIImageRef]) -> list[dict[str, str]]:
    out: list[dict[str, str]] = []
    for index, image_ref in enumerate(from_images, start=1):
        token = re.sub(r"[^a-z0-9]+", "-", image_ref.lower()).strip("-")
        if not token:
            token = "dependency"
        token = token[:64].rstrip("-")
        target = f"/opt/bertrand/deps/{index:02d}-{token}"
        out.append({"image": image_ref, "target": target})
    return out


def render_containerfile(
    model: Bertrand.Model,
    tag: TOMLKey,
    *,
    build_mounts: Sequence[str] = (),
) -> str:
    """Render a generated Containerfile for one build tag.

    Parameters
    ----------
    model : Bertrand.Model
        The validated Bertrand model containing the build matrix.
    tag : TOMLKey
        The build tag to render.
    build_mounts : Sequence[str], optional
        Additional mount fragments injected into the generated build step.

    Returns
    -------
    str
        Fully rendered Containerfile text.

    Raises
    ------
    ValueError
        If the tag is unknown, or if the tag uses a custom containerfile and
        therefore cannot be rendered from the internal template.
    """
    build = model.build.get(tag)
    if build is None:
        raise ValueError(f"unknown build tag '{tag}'")
    if build.containerfile is not None:
        raise ValueError(
            f"cannot render generated Containerfile for tag '{tag}' when a custom "
            "`containerfile` is configured"
        )

    jinja = jinja2.Environment(
        autoescape=False,
        undefined=jinja2.StrictUndefined,
        keep_trailing_newline=True,
        trim_blocks=False,
        lstrip_blocks=False,
    )
    template = jinja.from_string(
        locate_template("core", "containerfile.v1").read_text(encoding="utf-8")
    )
    bertrand_version = packaging.version.parse(VERSION.bertrand)
    python_version = packaging.version.parse(VERSION.python)
    try:
        page_size_kib = os.sysconf("SC_PAGE_SIZE") // 1024
    except (AttributeError, ValueError, OSError):
        page_size_kib = 4
    return template.render(
        python_major=python_version.major,
        python_minor=python_version.minor,
        python_patch=python_version.micro,
        bertrand_major=bertrand_version.major,
        bertrand_minor=bertrand_version.minor,
        bertrand_patch=bertrand_version.micro,
        cpus=0,
        page_size_kib=page_size_kib,
        env_mount=str(WORKTREE_MOUNT),
        build_mounts=list(build_mounts),
        dependency_copies=_dependency_copy_specs(build.from_),
    )


def _format_build_args(build_args: dict[str, Scalar]) -> list[str]:
    args: list[str] = []
    for key, value in build_args.items():
        args.extend(["--build-arg", f"{key}={value}"])
    return args


async def _resolve_scope(config: Config) -> str:
    if config.worktree.parts:
        scope = re.sub(r"[^a-zA-Z0-9._]+", "-", config.worktree.as_posix()).strip("-")
        if scope:
            return scope
    branch = await config.repo.head_branch()
    if branch:
        scope = re.sub(r"[^a-zA-Z0-9._]+", "-", branch).strip("-")
        if scope:
            return scope
    return "detached"


@dataclass(frozen=True)
class ImageArgs:
    """A full argument tail and metadata for `nerdctl build`."""
    argv: list[str]
    run_id: str
    image_name: str
    iid_file: Path
    containerfile: Path


async def image_args(
    config: Config,
    *,
    env_id: str,
    tag: TOMLKey,
) -> ImageArgs:
    """Assemble runtime image build arguments from config state.

    Parameters
    ----------
    config : Config
        Active configuration context for the target worktree.
    env_id : str
        Canonical environment UUID to include in image labels.
    tag : TOMLKey
        Build tag to assemble.

    Returns
    -------
    ImageArgs
        Build argument tail and generated artifact metadata for one image tag.

    Raises
    ------
    RuntimeError
        If called from inside a container or without an active config context.
    ValueError
        If `env_id` is empty or the tag is unknown.
    OSError
        If required config sections are missing.
    """
    if inside_image():
        raise RuntimeError("image_args() cannot be called from within a container")
    if not config:
        raise RuntimeError("image_args() requires an active config context")
    env_id = env_id.strip()
    if not env_id:
        raise ValueError("environment ID cannot be empty")

    python = config.get(PyProject)
    if python is None:
        raise OSError(
            f"missing 'python' configuration for environment at {config.root}"
        )
    bertrand = config.get(Bertrand)
    if bertrand is None:
        raise OSError(
            f"missing 'bertrand' configuration for environment at {config.root}"
        )
    build = bertrand.build.get(tag)
    if build is None:
        raise ValueError(
            f"unknown build tag '{tag}' for environment at {config.root}"
        )

    try:
        await gc_volumes(config, env_id)
    except Exception:
        pass

    run_id = uuid.uuid4().hex
    scope = await _resolve_scope(config)
    image_name = f"{python.project.name}.{scope}.{tag}.{run_id[:7]}"
    iid_file = config.root / METADATA_DIR / "images" / tag / "iid"
    iid_file.parent.mkdir(parents=True, exist_ok=True)

    if build.containerfile is None:
        containerfile = config.root / METADATA_DIR / "images" / tag / "Containerfile"
        containerfile.parent.mkdir(parents=True, exist_ok=True)
        build_mounts: list[str] = [
            f"--mount=type=cache,id={volume_name},target={volume_target},sharing=locked"
            for volume_name, volume_target in await collect_mount_specs(config, tag)
        ]
        atomic_write_text(
            containerfile,
            render_containerfile(
                bertrand,
                tag,
                build_mounts=build_mounts,
            ),
            encoding="utf-8",
        )
    else:
        containerfile = config.root / build.containerfile

    argv = [
        "-t",
        image_name,
        "--file",
        str(containerfile),
        "--iidfile",
        str(iid_file),
        "--label",
        f"{BERTRAND_ENV}=1",
        "--label",
        f"{ENV_ID_ENV}={env_id}",
        "--label",
        f"{IMAGE_TAG_ENV}={tag}",
        *_format_build_args(build.args),
        *format_network(bertrand.network.build),
        str(config.root),
    ]
    return ImageArgs(
        argv=argv,
        run_id=run_id,
        image_name=image_name,
        iid_file=iid_file,
        containerfile=containerfile,
    )


class Image(BaseModel):
    """Persistent metadata representing a local Bertrand image, which acts as a
    compiled snapshot of an environment worktree. An environment can have many
    images, each built with a different set of image and container arguments, and each
    is considered to be immutable once created.

    Specific care is taken not to store anything that references the host filesystem,
    in order to allow renaming or relocation of the environment directory.

    Attributes
    ----------
    version : int
        The version number for backwards compatibility.
    tag : str
        The user-friendly tag for this image, which is unique within the enclosing
        environment.
    id : str
        The unique image ID.
    created : datetime
        The ISO timestamp when the image was created.
    image_args : list[str]
        The original `nerdctl build` args used to build the image.
    """
    class Inspect(BaseModel):
        """Type hint for `nerdctl image inspect` output.

        https://github.com/containerd/nerdctl/blob/main/docs/command-reference.md#whale-nerdctl-image-inspect
        """
        model_config = ConfigDict(extra="allow")
        Id: ImageId
        Created: CreatedAt

    model_config = ConfigDict(extra="forbid")
    version: PositiveInt
    tag: TOMLKey
    id: ImageId
    created: CreatedAt
    image_args: ArgsList

    async def inspect(self) -> Image.Inspect | None:
        """Inspect this image via the container runtime.

        Returns
        -------
        Image.Inspect | None
            A JSON response from nerdctl or None if the image could not be found.
        """
        result = await nerdctl(
            ["image", "inspect", self.id],
            check=False,
            capture_output=True,
        )
        if result.returncode != 0 or not result.stdout:
            return None
        data = json.loads(result.stdout)
        return Image.Inspect.model_validate(data[0]) if data else None

    async def remove(self, *, force: bool, timeout: float) -> bool:
        """Remove this image from the container runtime. Will also remove all
        containers built from this image.

        Parameters
        ----------
        force : bool
            If True, forcefully remove the image even if it has running containers.
            If False, only remove stopped containers, and then remove the image if no
            containers remain.
        timeout : float
            The maximum time in seconds to wait for running containers to stop before
            forcefully killing them. `0` indicates an infinite wait.

        Returns
        -------
        bool
            True if `force=True` or the image had no running containers and the image
            was successfully removed. False otherwise.

        Raises
        ------
        OSError
            If `force` is False and there are still containers referencing this image.
        CommandError
            If any of the nerdctl commands fail.
        """
        deadline = time.monotonic() + timeout
        ids = await nerdctl_ids(
            "container",
            labels={IMAGE_ID_ENV: self.id},
            timeout=timeout,
        )
        retire = False
        if not force:
            running = set(
                await nerdctl_ids(
                    "container",
                    labels={IMAGE_ID_ENV: self.id},
                    status=("paused", "restarting", "running"),
                    timeout=deadline - time.monotonic(),
                )
            )
            if running:
                ids = [id_ for id_ in ids if id_ not in running]
                retire = True

        if ids:
            await Container.remove(ids, force=force, timeout=deadline - time.monotonic())
        if retire:
            return False

        cmd = ["image", "rm", "-i"]
        if force:
            cmd.append("-f")
        cmd.append(self.id)
        await nerdctl(cmd, timeout=deadline - time.monotonic())
        return True

    async def create(
        self,
        config: Config,
        env_id: str,
        cmd: Sequence[str],
        *,
        env_vars: Mapping[str, str] | None = None,
        quiet: bool,
    ) -> Container:
        """Create a container from this image in `created` state.

        Parameters
        ----------
        config : Config
            Active configuration context for the parent environment.
        env_id : str
            Canonical environment UUID used to label the created container.
        cmd : Sequence[str]
            An optional command to override the container's default entry point. If
            empty, the configured workload command is used.
        quiet : bool
            If True, suppress output from container commands.
        env_vars : Mapping[str, str] | None, optional
            Optional additional environment variables to inject into the container.

        Returns
        -------
        Container
            The created container metadata, validated from an immediate inspect call.

        Raises
        ------
        OSError
            If the container fails to create, cannot be identified via cidfile, or is
            not in `created` state after creation.
        TypeError
            If the command override is not a list of strings.
        """
        if cmd is not None:
            if not isinstance(cmd, list):
                raise TypeError("cmd must be a list of strings when provided")
            if not all(isinstance(part, str) for part in cmd):
                raise TypeError("cmd override must be a list of strings")

        bundle = await container_args(
            config,
            env_id=env_id,
            tag=self.tag,
            image_id=self.id,
            cmd=cmd,
            env_vars=env_vars,
        )
        await nerdctl(
            ["container", "create", *bundle.argv],
            capture_output=True if quiet else None,
        )

        container_id: str | None = None
        cid_file = config.root / bundle.cid_file
        try:
            if not cid_file.exists() or not cid_file.is_file():
                raise OSError(f"nerdctl create did not produce a cid file at {cid_file}")
            container_id = cid_file.read_text(encoding="utf-8").strip()
            if not container_id:
                raise OSError(f"nerdctl create produced an empty cid file at {cid_file}")
            inspected = await Container.inspect([container_id])
            if len(inspected) != 1:
                raise OSError(
                    "`nerdctl create` did not resolve to exactly one inspect result "
                    f"for container '{container_id}'"
                )
            container = inspected[0]
            if container.Id != container_id:
                raise OSError(
                    f"container inspect ID mismatch after create: cidfile={container_id}, "
                    f"inspect={container.Id}"
                )
            if container.State.Status != "created":
                raise OSError(
                    f"container '{container_id}' is not in created state after create "
                    f"command (status={container.State.Status!r})"
                )
            return container
        except Exception:
            if container_id:
                await nerdctl(
                    [
                        "container",
                        "rm",
                        "-f",
                        "-i",
                        "-v",
                        "--depend",
                        container_id,
                    ],
                    check=False,
                    capture_output=True,
                )
            raise
