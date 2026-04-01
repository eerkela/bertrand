"""A configuration resource for uv, which manages Python dependency resolution and
project environments.

This resource validates a minimal, opinionated `[tool.uv]` baseline while allowing
additional uv-native keys to pass through unchanged for forward compatibility.
"""
from __future__ import annotations

from typing import Annotated, Any

from pydantic import BaseModel, ConfigDict, Field

from ..version import VERSION
from .core import Config, Resource, Trimmed, resource


@resource("uv")
class UvConfig(Resource):
    """A resource describing the `[tool.uv]` table in `pyproject.toml`."""
    # pylint: disable=missing-function-docstring, unused-argument, missing-return-doc

    class Model(BaseModel):
        """Validate the `[tool.uv]` table."""
        model_config = ConfigDict(extra="allow")
        managed: Annotated[bool, Field(
            default=True,
            description="Whether this is treated as a uv-managed project.",
        )]
        required_version: Annotated[Trimmed, Field(
            default=VERSION.uv,
            alias="required-version",
            description="Pinned uv version expected by this toolchain.",
        )]
        python_preference: Annotated[Trimmed, Field(
            default="only-system",
            alias="python-preference",
            description="Preferred Python interpreter source for uv operations.",
        )]
        python_downloads: Annotated[Trimmed, Field(
            default="never",
            alias="python-downloads",
            description="Policy controlling implicit Python runtime downloads.",
        )]
        index_strategy: Annotated[Trimmed, Field(
            default="first-index",
            alias="index-strategy",
            description="Index resolution strategy for package lookup.",
        )]
        resolution: Annotated[Trimmed, Field(
            default="highest",
            description="Version resolution strategy for dependency solving.",
        )]
        prerelease: Annotated[Trimmed, Field(
            default="if-necessary-or-explicit",
            description="Prerelease selection policy for dependency solving.",
        )]

    async def init(self, config: Config, cli: Config.Init) -> dict[str, Any]:
        return self.Model.model_construct().model_dump(by_alias=True)

    async def validate(self, config: Config, fragment: Any) -> Model | None:
        return self.Model.model_validate(fragment)

    async def schema(self) -> dict[str, Any]:
        return self.Model.model_json_schema(by_alias=True, mode="validation")
