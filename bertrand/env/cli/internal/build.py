"""Internal image/container build command implementation."""

from __future__ import annotations

import asyncio
from contextlib import asynccontextmanager
from typing import TYPE_CHECKING

from bertrand.env.build_args import (
    IMAGE_BUILD_ARGS_CONFIG_SETTING,
    IMAGE_BUILD_ARGS_FILE,
    decode_image_build_args,
    encode_image_build_args,
    normalize_image_build_args,
)
from bertrand.env.cli.internal._helper import image_build_context, live_project_context
from bertrand.env.config.bertrand import Bertrand
from bertrand.env.config.core import Config, _metadata_lock_key
from bertrand.env.config.python import PyProject
from bertrand.env.git import (
    atomic_write_text,
    inside_container,
    inside_image,
    run,
)
from bertrand.env.kube.lock.cluster import ClusterLock

if TYPE_CHECKING:
    import argparse
    from collections.abc import AsyncIterator, Sequence


def bertrand_build(args: argparse.Namespace) -> None:
    """Execute the internal ``bertrand build`` command.

    Parameters
    ----------
    args : argparse.Namespace
        Parsed internal CLI arguments.

    """
    asyncio.run(_bertrand_build_async(args))


async def _bertrand_build_async(args: argparse.Namespace) -> None:
    cli_args = _parse_build_args(args.build_arg)
    resolved_args = _resolve_image_build_args(cli_args)
    if inside_container():
        async with live_project_context("build") as context:
            await _build(context.config, build_args=resolved_args)
    else:
        async with image_build_context("build") as config:
            await _build(config, build_args=resolved_args)


def _parse_build_args(entries: Sequence[str]) -> dict[str, str]:
    raw: dict[str, object] = {}
    for entry in entries:
        key, sep, value = entry.partition("=")
        if not sep:
            msg = f"build argument must use KEY=VALUE syntax: {entry!r}"
            raise ValueError(msg)
        if key in raw:
            msg = f"duplicate build argument: {key!r}"
            raise ValueError(msg)
        raw[key] = value
    return normalize_image_build_args(raw)


def _resolve_image_build_args(requested: dict[str, str]) -> dict[str, str]:
    if not inside_image():
        msg = "`bertrand build` requires access to a Bertrand image filesystem"
        raise RuntimeError(msg)
    if inside_container():
        if requested:
            msg = (
                "`bertrand build --build-arg` is only valid during image builds; "
                "interactive container builds reuse the parent image contract"
            )
            raise ValueError(msg)
        return _read_stored_image_build_args(required=True)

    parent = _read_stored_image_build_args(required=False)
    merged = normalize_image_build_args({**parent, **requested})
    _write_stored_image_build_args(merged)
    return merged


def _read_stored_image_build_args(*, required: bool) -> dict[str, str]:
    try:
        text = IMAGE_BUILD_ARGS_FILE.read_text(encoding="utf-8")
    except FileNotFoundError:
        if required:
            msg = (
                "missing immutable image build argument contract: "
                f"{IMAGE_BUILD_ARGS_FILE}"
            )
            raise OSError(msg) from None
        return {}
    except OSError as err:
        msg = f"failed to read image build argument contract: {IMAGE_BUILD_ARGS_FILE}"
        raise OSError(msg) from err
    try:
        return decode_image_build_args(text)
    except (TypeError, ValueError) as err:
        msg = f"invalid image build argument contract: {IMAGE_BUILD_ARGS_FILE}"
        raise ValueError(msg) from err


def _write_stored_image_build_args(args: dict[str, str]) -> None:
    text = encode_image_build_args(args) + "\n"
    atomic_write_text(IMAGE_BUILD_ARGS_FILE, text, encoding="utf-8")
    IMAGE_BUILD_ARGS_FILE.chmod(0o444)


async def _build(config: Config, *, build_args: dict[str, str]) -> None:
    if not config:
        msg = "build() requires an active config context"
        raise RuntimeError(msg)
    python = config.get(PyProject)
    bertrand = config.get(Bertrand)
    if python is None:
        msg = "build() requires parsed 'pyproject' configuration"
        raise OSError(msg)
    if bertrand is None:
        msg = "build() requires parsed 'bertrand' configuration"
        raise OSError(msg)

    sync_cmd = [
        "uv",
        "sync",
        "--locked",
        "--system",
        "--inexact",
        *_uv_build_arg_settings(build_args),
    ]
    sync_cmd.extend(
        [
            "--no-build-isolation-package",
            python.project.name,
        ]
    )
    if not inside_container():
        sync_cmd.append("--no-editable")

    async with _build_lock(config):
        await config.sync(image_build=True)
        await run(
            [
                "uv",
                "lock",
                *_uv_build_arg_settings(build_args),
            ],
            cwd=config.root,
        )
        await run(sync_cmd, cwd=config.root)


def _uv_build_arg_settings(build_args: dict[str, str]) -> list[str]:
    return [
        "--config-setting",
        f"{IMAGE_BUILD_ARGS_CONFIG_SETTING}={encode_image_build_args(build_args)}",
    ]


@asynccontextmanager
async def _build_lock(config: Config) -> AsyncIterator[None]:
    if config.kube is None:
        yield
        return
    async with ClusterLock(
        config.kube,
        _metadata_lock_key(config.repo, config.root),
        timeout=config.timeout,
    ):
        yield
