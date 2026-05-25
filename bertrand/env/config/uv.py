"""Validate uv dependency management configuration.

This resource validates a minimal, opinionated `[tool.uv]` baseline while allowing
additional uv-native keys to pass through unchanged for forward compatibility.
"""

from __future__ import annotations

from pathlib import PosixPath
from typing import Annotated

from pydantic import BaseModel, ConfigDict, Field

from bertrand.env.version import VERSION

from .core import Resource, Trimmed, resource

UV_CACHE: PosixPath = PosixPath("/tmp/.cache/uv")


class UvConfigModel(BaseModel):
    """Validate the `[tool.uv]` table."""

    model_config = ConfigDict(extra="allow")
    managed: Annotated[
        bool,
        Field(
            default=True,
            description="Whether this is treated as a uv-managed project.",
        ),
    ]
    required_version: Annotated[
        Trimmed,
        Field(
            default=VERSION.uv,
            alias="required-version",
            description="Pinned uv version expected by this toolchain.",
        ),
    ]
    python_preference: Annotated[
        Trimmed,
        Field(
            default="only-system",
            alias="python-preference",
            description="Preferred Python interpreter source for uv operations.",
        ),
    ]
    python_downloads: Annotated[
        Trimmed,
        Field(
            default="never",
            alias="python-downloads",
            description="Policy controlling implicit Python runtime downloads.",
        ),
    ]
    index_strategy: Annotated[
        Trimmed,
        Field(
            default="first-index",
            alias="index-strategy",
            description="Index resolution strategy for package lookup.",
        ),
    ]
    resolution: Annotated[
        Trimmed,
        Field(
            default="highest",
            description="Version resolution strategy for dependency solving.",
        ),
    ]
    prerelease: Annotated[
        Trimmed,
        Field(
            default="if-necessary-or-explicit",
            description="Prerelease selection policy for dependency solving.",
        ),
    ]


@resource("uv")
class UvConfig(Resource[UvConfigModel]):
    """A resource describing the `[tool.uv]` table in `pyproject.toml`."""
