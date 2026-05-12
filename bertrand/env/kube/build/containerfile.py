"""Generated Containerfile rendering for project image builds."""

from __future__ import annotations

import os
import re
from typing import TYPE_CHECKING

import jinja2
import packaging.version

from bertrand.env.config.core import locate_template
from bertrand.env.git import WORKTREE_MOUNT
from bertrand.env.version import VERSION

if TYPE_CHECKING:
    from collections.abc import Sequence
    from pathlib import Path

    from bertrand.env.config import Bertrand


def project_containerfile(
    root: Path,
    model: Bertrand.Model,
    tag: str,
    image_config: Bertrand.Model.Image,
) -> str:
    """Load or render the Containerfile for one configured image.

    Parameters
    ----------
    root : Path
        Project root directory.
    model : Bertrand.Model
        Bertrand project configuration.
    tag : str
        Configured image key.
    image_config : Bertrand.Model.Image
        Image configuration for `tag`.

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
            msg = f"failed to read Containerfile for tag '{tag}': {path}"
            raise OSError(msg) from err
        except UnicodeDecodeError as err:
            msg = f"Containerfile for tag '{tag}' is not UTF-8 encoded: {path}"
            raise OSError(msg) from err
    return _render_containerfile(model, tag)


def _render_containerfile(model: Bertrand.Model, tag: str) -> str:
    image_config = model.image.get(tag)
    if image_config is None:
        msg = f"unknown image key '{tag}'"
        raise ValueError(msg)
    if image_config.containerfile is not None:
        msg = (
            f"cannot render generated Containerfile for tag '{tag}' when a custom "
            "`containerfile` is configured"
        )
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
        build_mounts=[],
        dependency_copies=_dependency_copy_specs(image_config.from_),
    )


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
