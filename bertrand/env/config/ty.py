"""A configuration resource for ty, which provides static typing diagnostics.

This resource validates a minimal `[tool.ty]` baseline while allowing additional
ty-native keys to pass through unchanged for forward compatibility.
"""

from __future__ import annotations

from typing import Annotated, Literal

from pydantic import BaseModel, ConfigDict, Field, StringConstraints

from .core import NonEmpty, Resource, Trimmed, resource

type TyRuleName = Annotated[str, StringConstraints(strip_whitespace=True, min_length=1)]
type TyRuleSeverity = Literal["ignore", "warn", "error"]


@resource("ty")
class TyConfig(Resource):
    """A resource describing the `[tool.ty]` table in `pyproject.toml`."""

    class Model(BaseModel):
        """Validate the `[tool.ty]` table."""

        class Src(BaseModel):
            """Validate the `[tool.ty.src]` table."""

            model_config = ConfigDict(extra="allow")
            include: Annotated[
                NonEmpty[list[Trimmed]],
                Field(
                    default_factory=lambda: [".", "tests"],
                    description="Relative paths that ty should include in analysis.",
                ),
            ]
            exclude: Annotated[
                list[Trimmed],
                Field(
                    default_factory=list,
                    description="Explicitly excluded source paths.",
                ),
            ]
            respect_ignore_files: Annotated[
                bool,
                Field(
                    default=True,
                    alias="respect-ignore-files",
                    description="Whether ignore files should affect source discovery.",
                ),
            ]

        class Analysis(BaseModel):
            """Validate the `[tool.ty.analysis]` table."""

            model_config = ConfigDict(extra="allow")
            replace_imports_with_any: Annotated[
                list[Trimmed],
                Field(
                    default_factory=lambda: ["conan.**"],
                    alias="replace-imports-with-any",
                    description=(
                        "Module globs whose imports should be treated as `typing.Any`."
                    ),
                ),
            ]

        class Terminal(BaseModel):
            """Validate the `[tool.ty.terminal]` table."""

            model_config = ConfigDict(extra="allow")
            error_on_warning: Annotated[
                bool,
                Field(
                    default=False,
                    alias="error-on-warning",
                    description=(
                        "Whether warnings should produce a non-zero exit status."
                    ),
                ),
            ]

        model_config = ConfigDict(extra="allow")
        src: Annotated[
            Src,
            Field(
                default_factory=Src.model_construct,
                description="Source-discovery settings for ty.",
            ),
        ]
        analysis: Annotated[
            Analysis,
            Field(
                default_factory=Analysis.model_construct,
                description="Type-analysis behavior settings for ty.",
            ),
        ]
        rules: Annotated[
            dict[TyRuleName, TyRuleSeverity],
            Field(
                default_factory=lambda: {
                    "possibly-unresolved-reference": "error",
                    "redundant-cast": "warn",
                    "unused-ignore-comment": "warn",
                },
                description="Rule severities keyed by ty rule name.",
            ),
        ]
        terminal: Annotated[
            Terminal,
            Field(
                default_factory=Terminal.model_construct,
                description="Terminal output and exit-policy settings for ty.",
            ),
        ]
