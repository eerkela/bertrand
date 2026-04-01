"""A configuration resource for Ruff, which provides Python formatting and linting.

This resource validates a minimal `[tool.ruff]` baseline while allowing additional
Ruff-native keys to pass through unchanged for forward compatibility.
"""
from __future__ import annotations

import re
from typing import Annotated, Any

from pydantic import BaseModel, ConfigDict, Field, PositiveInt, StringConstraints

from ..version import VERSION
from .core import Config, Resource, Trimmed, resource

type RuffRule = Annotated[str, StringConstraints(strip_whitespace=True, min_length=1)]


def _ruff_target_version() -> str:
    match = re.match(r"^\s*(\d+)\.(\d+)", VERSION.python)
    if match is None:
        return "py312"
    major, minor = match.groups()
    return f"py{major}{minor}"


@resource("ruff")
class RuffConfig(Resource):
    """A resource describing the `[tool.ruff]` table in `pyproject.toml`."""
    # pylint: disable=missing-function-docstring, unused-argument, missing-return-doc

    class Model(BaseModel):
        """Validate the `[tool.ruff]` table."""
        model_config = ConfigDict(extra="allow")

        class Format(BaseModel):
            """Validate the `[tool.ruff.format]` table."""
            model_config = ConfigDict(extra="allow")
            quote_style: Annotated[Trimmed, Field(
                default="preserve",
                alias="quote-style",
                description="String quote normalization mode used by Ruff format.",
            )]
            indent_style: Annotated[Trimmed, Field(
                default="space",
                alias="indent-style",
                description="Indentation style used by Ruff format.",
            )]
            line_ending: Annotated[Trimmed, Field(
                default="auto",
                alias="line-ending",
                description="Line-ending mode used by Ruff format.",
            )]

        class Lint(BaseModel):
            """Validate the `[tool.ruff.lint]` table."""
            model_config = ConfigDict(extra="allow")

            class Pydocstyle(BaseModel):
                """Validate the `[tool.ruff.lint.pydocstyle]` table."""
                model_config = ConfigDict(extra="allow")
                convention: Annotated[Trimmed, Field(
                    default="numpy",
                    description="Docstring convention used by Ruff pydocstyle rules.",
                )]

            class Flake8Annotations(BaseModel):
                """Validate the `[tool.ruff.lint.flake8-annotations]` table."""
                model_config = ConfigDict(extra="allow")
                mypy_init_return: Annotated[bool, Field(
                    default=True,
                    alias="mypy-init-return",
                    description=
                        "Whether `__init__` is allowed to omit explicit `-> None`.",
                )]

            select: Annotated[list[RuffRule], Field(
                default_factory=lambda: ["E", "F", "I", "B", "UP", "DOC", "FIX"],
                description="Enabled Ruff lint rule families/codes.",
            )]
            exclude: Annotated[list[Trimmed], Field(
                default_factory=lambda: ["*.pyi"],
                description="Path globs excluded from Ruff linting.",
            )]
            pydocstyle: Annotated[Pydocstyle, Field(
                default_factory=Pydocstyle.model_construct,
                description="pydocstyle plugin settings.",
            )]
            flake8_annotations: Annotated[Flake8Annotations, Field(
                default_factory=Flake8Annotations.model_construct,
                alias="flake8-annotations",
                description="flake8-annotations plugin settings.",
            )]

        target_version: Annotated[Trimmed, Field(
            default_factory=_ruff_target_version,
            alias="target-version",
            description="Target Python version used by Ruff for analysis/formatting.",
        )]
        line_length: Annotated[PositiveInt, Field(
            default=100,
            alias="line-length",
            description="Maximum allowed line length for formatting/linting context.",
        )]
        indent_width: Annotated[PositiveInt, Field(
            default=4,
            alias="indent-width",
            description="Indent width used by Ruff formatting behavior.",
        )]
        output_format: Annotated[Trimmed, Field(
            default="full",
            alias="output-format",
            description="Diagnostic output format used by Ruff commands.",
        )]
        respect_gitignore: Annotated[bool, Field(
            default=True,
            alias="respect-gitignore",
            description="Whether Ruff should honor ignore files from Git.",
        )]
        format: Annotated[Format, Field(
            default_factory=Format.model_construct,
            description="Formatting settings for Ruff.",
        )]
        lint: Annotated[Lint, Field(
            default_factory=Lint.model_construct,
            description="Linting settings for Ruff.",
        )]

    async def init(self, config: Config, cli: Config.Init) -> dict[str, Any]:
        return self.Model.model_construct().model_dump(by_alias=True)

    async def validate(self, config: Config, fragment: Any) -> Model | None:
        return self.Model.model_validate(fragment)

    async def schema(self) -> dict[str, Any]:
        return self.Model.model_json_schema(by_alias=True, mode="validation")
