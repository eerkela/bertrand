"""A configuration resource for pytest, which provides Python test discovery and
execution defaults.

This resource validates a minimal `[tool.pytest]` baseline while allowing additional
pytest-native keys to pass through unchanged for forward compatibility.
"""
from __future__ import annotations

import re
from typing import Annotated, Any

from pydantic import BaseModel, ConfigDict, Field, StringConstraints

from ..version import VERSION
from .core import Config, Resource, resource

type PytestString = Annotated[str, StringConstraints(strip_whitespace=True, min_length=1)]


def _pytest_minversion() -> str:
    match = re.match(r"^\s*(\d+)\.(\d+)", VERSION.pytest)
    if match is None:
        return "9.0"
    major, minor = match.groups()
    return f"{major}.{minor}"


@resource("pytest")
class PytestConfig(Resource):
    """A resource describing the `[tool.pytest]` table in `pyproject.toml`."""
    # pylint: disable=missing-function-docstring, unused-argument, missing-return-doc

    class Model(BaseModel):
        """Validate the `[tool.pytest]` table."""
        model_config = ConfigDict(extra="allow")
        minversion: Annotated[PytestString, Field(
            default_factory=_pytest_minversion,
            description="Minimum pytest version expected by this configuration.",
        )]
        required_plugins: Annotated[list[PytestString], Field(
            default_factory=list,
            alias="required_plugins",
            description="Plugins that must be installed for test execution.",
        )]
        testpaths: Annotated[list[PytestString], Field(
            default_factory=lambda: ["tests"],
            description="Default root paths used for test discovery.",
        )]
        python_files: Annotated[list[PytestString], Field(
            default_factory=lambda: ["test_*.py"],
            alias="python_files",
            description="Glob patterns for Python test files.",
        )]
        python_functions: Annotated[list[PytestString], Field(
            default_factory=lambda: ["test_*"],
            alias="python_functions",
            description="Glob patterns for Python test functions.",
        )]
        python_classes: Annotated[list[PytestString], Field(
            default_factory=lambda: ["Test*"],
            alias="python_classes",
            description="Glob patterns for Python test classes.",
        )]
        filterwarnings: Annotated[list[PytestString], Field(
            default_factory=list,
            description="Warning filter directives applied during test runs.",
        )]
        markers: Annotated[list[PytestString], Field(
            default_factory=list,
            description="Custom pytest markers for test categorization.",
        )]
        usefixtures: Annotated[list[PytestString], Field(
            default_factory=list,
            description="Fixtures to auto-apply to collected tests.",
        )]
        addopts: Annotated[list[PytestString], Field(
            default_factory=lambda: ["-ra", "-q"],
            description="Default command-line options passed to pytest.",
        )]
        console_output_style: Annotated[PytestString, Field(
            default="progress",
            alias="console_output_style",
            description="Console progress output style used during test runs.",
        )]
        strict: Annotated[bool, Field(
            default=True,
            description="Whether strict mode checks are enabled for pytest.",
        )]

    async def init(self, config: Config, cli: Config.Init) -> dict[str, Any]:
        return self.Model.model_construct().model_dump(by_alias=True)

    async def validate(self, config: Config, fragment: Any) -> Model | None:
        return self.Model.model_validate(fragment)

    async def schema(self) -> dict[str, Any]:
        return self.Model.model_json_schema(by_alias=True, mode="validation")
