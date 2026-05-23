"""Generated Containerfile rendering for project image builds."""

from __future__ import annotations

import os
import re
from typing import TYPE_CHECKING, Protocol

import jinja2
import packaging.version

from bertrand.env.build_args import normalize_image_build_args
from bertrand.env.config.core import locate_template
from bertrand.env.git import WORKTREE_MOUNT
from bertrand.env.version import VERSION

if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence
    from pathlib import Path

    from bertrand.env.config.bertrand import Bertrand


class _CapabilityRequest(Protocol):
    id: str
    required: bool


def project_containerfile(
    root: Path,
    model: Bertrand.Model,
    image_config: Bertrand.Model.Image,
) -> str:
    """Load or render the Containerfile for the configured project image.

    Parameters
    ----------
    root : Path
        Project root directory.
    model : Bertrand.Model
        Bertrand project configuration.
    image_config : Bertrand.Model.Image
        Image configuration.

    Returns
    -------
    str
        Containerfile text.

    Raises
    ------
    OSError
        If a custom Containerfile cannot be read as UTF-8 text.
    """
    if image_config.containerfile is not None:
        path = root / image_config.containerfile
        try:
            return path.read_text(encoding="utf-8")
        except OSError as err:
            msg = f"failed to read Containerfile: {path}"
            raise OSError(msg) from err
        except UnicodeDecodeError as err:
            msg = f"Containerfile is not UTF-8 encoded: {path}"
            raise OSError(msg) from err
    return _render_containerfile(model)


def _render_containerfile(model: Bertrand.Model) -> str:
    image_config = model.image
    if image_config.containerfile is not None:
        msg = "cannot render generated Containerfile when a custom one is configured"
        raise ValueError(msg)

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
        build_args=_build_arg_specs(image_config.args),
        run_mounts=[
            *_capability_mount_specs("secret", image_config.secrets),
            *_capability_mount_specs("ssh", image_config.ssh),
        ],
        dependency_copies=_dependency_copy_specs(image_config.from_),
    )


def _build_arg_specs(args: Mapping[str, object]) -> list[dict[str, str]]:
    return [
        {"key": key, "value": value}
        for key, value in normalize_image_build_args(args).items()
    ]


def _capability_mount_specs(
    kind: str,
    requests: Sequence[_CapabilityRequest],
) -> list[str]:
    out: list[str] = []
    for request in sorted(requests, key=lambda entry: entry.id):
        required = "true" if request.required else "false"
        out.append(f"type={kind},id={request.id},required={required}")
    return out


def _dependency_copy_specs(from_images: Sequence[str]) -> list[dict[str, str]]:
    out: list[dict[str, str]] = []
    for index, image_ref in enumerate(from_images, start=1):
        token = re.sub(r"[^a-z0-9]+", "-", image_ref.lower()).strip("-")
        if not token:
            token = "dependency"
        token = token[:64].rstrip("-")
        target = f"/opt/bertrand/deps/{index:02d}-{token}"
        out.append({"image": image_ref, "target": target})
    return out
