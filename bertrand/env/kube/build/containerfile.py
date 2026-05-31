"""Generated Containerfile rendering for project image builds."""

from __future__ import annotations

import os
import re
from typing import TYPE_CHECKING

import packaging.version

from bertrand.env.build_args import normalize_image_build_args
from bertrand.env.git import BERTRAND_LABEL, BERTRAND_LABEL_IMAGE, WORKTREE_MOUNT
from bertrand.env.version import VERSION

if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence
    from pathlib import Path

    from bertrand.env.config.bertrand import BertrandModel
    from bertrand.env.kube.workload.capability import CapabilityRequest


def project_containerfile(
    root: Path,
    model: BertrandModel,
    image_config: BertrandModel.Image,
) -> str:
    """Load or render the Containerfile for the configured project image.

    Parameters
    ----------
    root : Path
        Project root directory.
    model : BertrandModel
        Bertrand project configuration.
    image_config : BertrandModel.Image
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


def _render_containerfile(model: BertrandModel) -> str:
    image_config = model.image
    if image_config.containerfile is not None:
        msg = "cannot render generated Containerfile when a custom one is configured"
        raise ValueError(msg)

    bertrand_version = packaging.version.parse(VERSION.bertrand)
    try:
        page_size_kib = os.sysconf("SC_PAGE_SIZE") // 1024
    except (AttributeError, ValueError, OSError):
        page_size_kib = 4

    build_args = _build_arg_keys(image_config.args)
    run_mounts = [
        *_capability_mount_specs("secret", image_config.secrets),
        *_capability_mount_specs("ssh", image_config.ssh),
    ]
    dependency_copies = _dependency_copy_specs(image_config.from_)

    lines = [
        (
            "ARG BERTRAND_VERSION="
            f"{bertrand_version.major}.{bertrand_version.minor}."
            f"{bertrand_version.micro}"
        ),
        "ARG DEBUG=true",
        "ARG DEV=true",
        "ARG CPUS=0",
        (
            "FROM bertrand:"
            f"${{BERTRAND_VERSION}}.${{DEBUG}}.${{DEV}}.${{CPUS}}.{page_size_kib}"
        ),
    ]
    lines.extend(f"ARG {key}" for key in build_args)
    lines.extend(
        (
            f"ENV {BERTRAND_LABEL}={BERTRAND_LABEL_IMAGE}",
            "ENV PIP_DISABLE_PIP_VERSION_CHECK=1",
            f"WORKDIR {WORKTREE_MOUNT}",
            f"COPY . {WORKTREE_MOUNT}",
            "",
        )
    )
    lines.extend(
        f"COPY --from={image} / {target}" for image, target in dependency_copies
    )
    if dependency_copies:
        lines.append("")
    lines.extend(_run_lines(run_mounts=run_mounts, build_args=build_args))
    return "\n".join(lines) + "\n"


def _build_arg_keys(args: Mapping[str, object]) -> list[str]:
    return list(normalize_image_build_args(args))


def _run_lines(*, run_mounts: list[str], build_args: list[str]) -> list[str]:
    prefix = "RUN"
    if run_mounts:
        prefix += "".join(f" --mount={mount}" for mount in run_mounts)
    prefix += " bertrand build"
    if not build_args:
        return [prefix]
    lines = [f"{prefix} \\"]
    for index, key in enumerate(build_args):
        suffix = " \\" if index < len(build_args) - 1 else ""
        lines.append(f"    --build-arg {key}=\"${{{key}}}\"{suffix}")
    return lines


def _capability_mount_specs(
    kind: str,
    requests: Sequence[CapabilityRequest],
) -> list[str]:
    out: list[str] = []
    for request in sorted(requests, key=lambda entry: entry.id):
        required = "true" if request.required else "false"
        out.append(f"type={kind},id={request.id},required={required}")
    return out


def _dependency_copy_specs(from_images: Sequence[str]) -> list[tuple[str, str]]:
    out: list[tuple[str, str]] = []
    for index, image_ref in enumerate(from_images, start=1):
        token = re.sub(r"[^a-z0-9]+", "-", image_ref.lower()).strip("-")
        if not token:
            token = "dependency"
        token = token[:64].rstrip("-")
        target = f"/opt/bertrand/deps/{index:02d}-{token}"
        out.append((image_ref, target))
    return out
