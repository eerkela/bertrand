"""TODO"""
from __future__ import annotations

from typing import Annotated, Any, Literal, Self

from pydantic import BaseModel, ConfigDict, Field, NonNegativeInt, model_validator

from .core import (
    NonEmpty,
    Config,
    NoCRLF,
    RegexPattern,
    Resource,
    dump_yaml,
    resource,
)
from ..run import CONTAINER_TMP_MOUNT, atomic_write_text


@resource("clangd")
class Clangd(Resource):
    """A resource describing a `.clangd` file, which is used to configure clangd for
    C++ language server features in editors.  This expects native clangd keys in
    `[tool.clangd]`; legacy `arguments` aliasing is intentionally unsupported.
    """
    # pylint: disable=missing-function-docstring, unused-argument, missing-return-doc

    class Model(BaseModel):
        """Validate the `[clangd]` table."""
        model_config = ConfigDict(extra="forbid")

        class _Diagnostics(BaseModel):
            """Validate the `[tool.clangd.diagnostics]` table."""
            model_config = ConfigDict(extra="forbid")
            UnusedIncludes: Annotated[Literal["None", "Strict"], Field(
                default="Strict",
                examples=["None", "Strict"],
                description="Warn on unused #include directives.",
            )]
            MissingIncludes: Annotated[Literal["None", "Strict"], Field(
                default="Strict",
                examples=["None", "Strict"],
                description=
                    "Warn on missing #include directives, including transitive ones "
                    "that are inherited from earlier includes, but are not explicitly "
                    "listed.",
            )]
            Suppress: Annotated[list[NoCRLF], Field(
                default_factory=list,
                description=
                    "List of diagnostics to suppress by code/category.  Note that "
                    "this also includes clang-tidy rules that will be excluded from "
                    "syntax highlighting.  '*' can be used to turn off all "
                    "diagnostics.",
            )]

        Diagnostics: Annotated[_Diagnostics, Field(
            default_factory=_Diagnostics.model_construct,
            description="clangd diagnostics options.",
        )]

        class _Index(BaseModel):
            """Validate the `[tool.clangd.index]` table."""
            model_config = ConfigDict(extra="forbid")
            Background: Annotated[Literal["Build", "Skip"], Field(
                default="Build",
                examples=["Build", "Skip"],
                description=
                    "Control whether clangd should build a symbolic index in the "
                    "background.  This is required for features like cross-file rename "
                    "and efficient code navigation (including by AI agents), but can "
                    "be disabled to reduce resource usage on large codebases with "
                    "heavy churn.",
            )]
            StandardLibrary: Annotated[bool, Field(
                default=True,
                description=
                    "Control whether clangd should eagerly index the standard "
                    "library, which enables features like code completions in "
                    "new/empty files, without needing to build an index first."
            )]

        Index: Annotated[_Index, Field(
            default_factory=_Index.model_construct,
            description="clangd symbolic indexing options.",
        )]

        class _Completion(BaseModel):
            """Validate the `[tool.clangd.completion]` table."""
            model_config = ConfigDict(extra="forbid")
            AllScopes: Annotated[bool, Field(
                default=True,
                description=
                    "Whether code completion should include suggestions from scopes "
                    "that are not directly visible.  The required scope prefix will "
                    "be automatically inserted.",
            )]
            ArgumentLists: Annotated[
                Literal["None", "OpenDelimiter", "Delimiters", "FullPlaceholders"],
                Field(
                    default="Delimiters",
                    examples=["None", "OpenDelimiter", "Delimiters", "FullPlaceholders"],
                    description=
                        "Determines what is inserted in argument list position when "
                        "completing a call to a function.  Some examples:\n"
                        "   - `None`: `fo^` -> `foo^`\n"
                        "   - `OpenDelimiter`: `fo^` -> `foo(^`\n"
                        "   - `Delimiters`: `fo^` -> `foo(^)`\n"
                        "   - `FullPlaceholders`: `fo^` -> `foo(int arg)`, with "
                        "`int arg` selected\n"
                )
            ]
            HeaderInsertion: Annotated[Literal["Never", "IWYU"], Field(
                default="IWYU",
                examples=["Never", "IWYU"],
                description=
                    "Control whether clangd should automatically insert missing "
                    "#include directives when completing symbols.  If set to `IWYU` "
                    "(Include What You Use), clangd will insert headers for symbols "
                    "that are referenced in the current file, but are not already "
                    "included.",
            )]
            CodePatterns: Annotated[Literal["None", "All"], Field(
                default="All",
                examples=["None", "All"],
                description=
                    "Control whether code completion should include snippets that "
                    "insert multiple tokens or lines of code.  Setting this to `None` "
                    "restricts completions to single identifiers or member accesses, "
                    "while `All` allows clangd to suggest more complex code patterns.",
            )]

        Completion: Annotated[_Completion, Field(
            default_factory=_Completion.model_construct,
            description="clangd code completion options.",
        )]

        class _InlayHints(BaseModel):
            """Validate the `[tool.clangd.inlay-hints]` table."""
            model_config = ConfigDict(extra="forbid")
            Enabled: Annotated[bool, Field(
                default=True,
                description=
                    "Enable inlay hints, which are inline annotations shown in the "
                    "editor for various code elements, such as parameter names.",
            )]
            ParameterNames: Annotated[bool, Field(
                default=True,
                description="Show parameter name hints for arguments in function calls.",
            )]
            DeducedTypes: Annotated[bool, Field(
                default=True,
                description="Show deduced type hints for variables declared with `auto`.",
            )]
            Designators: Annotated[bool, Field(
                default=True,
                description="Show name/index hints for elements of braced initializers.",
            )]
            BlockEnd: Annotated[bool, Field(
                default=False,
                description=
                    "Show block end hints that indicate which code block a closing "
                    "brace corresponds to.",
            )]
            DefaultArguments: Annotated[bool, Field(
                default=False,
                description="Show default value hints for implicit function parameters.",
            )]
            TypeNameLimit: Annotated[NonNegativeInt, Field(
                default=24,
                description="Limit the maximum length of type inlay hints.",
            )]

        InlayHints: Annotated[_InlayHints, Field(
            default_factory=_InlayHints.model_construct,
            description="clangd inlay hints options.",
        )]

        class _Hover(BaseModel):
            """Validate the `[tool.clangd.hover]` table."""
            model_config = ConfigDict(extra="forbid")
            ShowAKA: Annotated[bool, Field(
                default=True,
                description=
                    "Resolve type aliases and show their desugared names in hover "
                    "tooltips.",
            )]
            MacroContentsLimit: Annotated[NonNegativeInt, Field(
                default=2048,
                description=
                    "Limit the maximum length of macro contents in hover tooltips.",
            )]

        Hover: Annotated[_Hover, Field(
            default_factory=_Hover.model_construct,
            description="clangd hover options.",
        )]

        class _Documentation(BaseModel):
            """Validate the `[tool.clangd.documentation]` table."""
            model_config = ConfigDict(extra="forbid")
            CommentFormat: Annotated[Literal["PlainText", "Markdown", "Doxygen"], Field(
                default="Doxygen",
                examples=["PlainText", "Markdown", "Doxygen"],
                description=
                    "Control the format of documentation comments in hover tooltips.  "
                    "Doxygen format is a superset of the previous two, and supports "
                    "rich formatting, but PlainText or Markdown can be used to "
                    "disable formatting if it causes issues in some editors.",
            )]

        Documentation: Annotated[_Documentation, Field(
            default_factory=_Documentation.model_construct,
            description="clangd documentation options.",
        )]

        class _If(BaseModel):
            """Validate the `[[tool.clangd.if]]` AoT."""
            model_config = ConfigDict(extra="forbid")
            PathMatch: Annotated[NonEmpty[list[RegexPattern]], Field(
                description=
                    "List of regex patterns to enable this configuration block.  If "
                    "any pattern matches, the configuration will be applied.  If "
                    "multiple `[[tool.clangd.if]]` entries match, their configurations "
                    "will be merged, with later entries taking precedence.",
            )]
            PathExclude: Annotated[list[RegexPattern], Field(
                default_factory=list,
                description=
                    "List of regex patterns to exclude from this configuration block.  "
                    "If any pattern matches, the configuration will not be applied, "
                    "even if `PathMatch` matches.  This allows for fine-grained "
                    "control to apply configurations to specific subdirectories or "
                    "files within a larger codebase."
            )]

            class _Diagnostics(BaseModel):
                """Validate the `[tool.clangd.diagnostics]` table."""
                model_config = ConfigDict(extra="forbid")
                UnusedIncludes: Annotated[Literal["None", "Strict"] | None, Field(
                    default=None,
                    examples=["None", "Strict"],
                    description="See global 'Diagnostics.UnusedIncludes' option.",
                )]
                MissingIncludes: Annotated[Literal["None", "Strict"] | None, Field(
                    default=None,
                    examples=["None", "Strict"],
                    description="See global 'Diagnostics.MissingIncludes' option.",
                )]
                Suppress: Annotated[NonEmpty[list[NoCRLF]] | None, Field(
                    default=None,
                    description=
                        "See global 'Diagnostics.Suppress' option.  Note that this is "
                        "additive to the global list, so it can only be used to "
                        "suppress additional diagnostics, not to re-enable diagnostics "
                        "that are turned off globally.",
                )]

            Diagnostics: Annotated[_Diagnostics | None, Field(
                default=None,
                description="clangd diagnostics options for this conditional block.",
            )]

            class _Index(BaseModel):
                """Validate the `[tool.clangd.index]` table."""
                model_config = ConfigDict(extra="forbid")
                Background: Annotated[Literal["Build", "Skip"] | None, Field(
                    default=None,
                    examples=["Build", "Skip"],
                    description="See global 'Index.Background' option.",
                )]
                StandardLibrary: Annotated[bool | None, Field(
                    default=None,
                    description="See global 'Index.StandardLibrary' option.",
                )]

            Index: Annotated[_Index | None, Field(
                default=None,
                description="clangd index options for this conditional block.",
            )]

            class _Completion(BaseModel):
                """Validate the `[tool.clangd.completion]` table."""
                model_config = ConfigDict(extra="forbid")
                AllScopes: Annotated[bool | None, Field(
                    default=None,
                    description="See global 'Completion.AllScopes' option.",
                )]
                ArgumentLists: Annotated[
                    Literal["None", "OpenDelimiter", "Delimiters", "FullPlaceholders"] | None,
                    Field(
                        default=None,
                        examples=["None", "OpenDelimiter", "Delimiters", "FullPlaceholders"],
                        description="See global 'Completion.ArgumentLists' option.",
                    )
                ]
                HeaderInsertion: Annotated[Literal["Never", "IWYU"] | None, Field(
                    default=None,
                    examples=["Never", "IWYU"],
                    description="See global 'Completion.HeaderInsertion' option.",
                )]
                CodePatterns: Annotated[Literal["None", "All"] | None, Field(
                    default=None,
                    examples=["None", "All"],
                    description="See global 'Completion.CodePatterns' option.",
                )]

            Completion: Annotated[_Completion | None, Field(
                default=None,
                description="clangd code completion options for this conditional block.",
            )]

            class _InlayHints(BaseModel):
                """Validate the `[tool.clangd.inlay-hints]` table."""
                model_config = ConfigDict(extra="forbid")
                Enabled: Annotated[bool | None, Field(
                    default=None,
                    description="See global 'InlayHints.Enabled' option.",
                )]
                ParameterNames: Annotated[bool | None, Field(
                    default=None,
                    description="See global 'InlayHints.ParameterNames' option.",
                )]
                DeducedTypes: Annotated[bool | None, Field(
                    default=None,
                    description="See global 'InlayHints.DeducedTypes' option.",
                )]
                Designators: Annotated[bool | None, Field(
                    default=None,
                    description="See global 'InlayHints.Designators' option.",
                )]
                BlockEnd: Annotated[bool | None, Field(
                    default=None,
                    description="See global 'InlayHints.BlockEnd' option.",
                )]
                DefaultArguments: Annotated[bool | None, Field(
                    default=None,
                    description="See global 'InlayHints.DefaultArguments' option.",
                )]
                TypeNameLimit: Annotated[NonNegativeInt | None, Field(
                    default=None,
                    description="See global 'InlayHints.TypeNameLimit' option.",
                )]

            InlayHints: Annotated[_InlayHints | None, Field(
                default=None,
                description="clangd inlay hints options for this conditional block.",
            )]

            class _Hover(BaseModel):
                """Validate the `[tool.clangd.hover]` table."""
                model_config = ConfigDict(extra="forbid")
                ShowAKA: Annotated[bool | None, Field(
                    default=None,
                    description="See global 'Hover.ShowAKA' option.",
                )]
                MacroContentsLimit: Annotated[NonNegativeInt | None, Field(
                    default=None,
                    description="See global 'Hover.MacroContentsLimit' option.",
                )]

            Hover: Annotated[_Hover | None, Field(
                default=None,
                description="clangd hover options for this conditional block.",
            )]

            class _Documentation(BaseModel):
                """Validate the `[tool.clangd.documentation]` table."""
                model_config = ConfigDict(extra="forbid")
                CommentFormat: Annotated[
                    Literal["PlainText", "Markdown", "Doxygen"] | None,
                    Field(
                        default=None,
                        examples=["PlainText", "Markdown", "Doxygen"],
                        description="See global 'Documentation.CommentFormat' option.",
                    )
                ]

            Documentation: Annotated[_Documentation | None, Field(
                default=None,
                description="clangd documentation options for this conditional block.",
            )]

            @model_validator(mode="after")
            def _validate_nonempty(self) -> Self:
                if (
                    self.Diagnostics is None and
                    self.Index is None and
                    self.Completion is None and
                    self.InlayHints is None and
                    self.Hover is None and
                    self.Documentation is None
                ):
                    raise ValueError(
                        "each [[tool.clangd.if]] entry must define at least one "
                        "of 'Diagnostics', 'Index', 'Completion', 'InlayHints', "
                        "'Hover', or 'Documentation'"
                    )
                return self

        If: Annotated[list[_If], Field(
            default_factory=list,
            description=
                "List of conditional blocks for file-specific clangd configuration.",
        )]

    async def init(self, config: Config, cli: Config.Init) -> dict[str, Any]:
        return self.Model.model_construct().model_dump(by_alias=True)

    async def validate(self, config: Config, fragment: Any) -> Model | None:
        return self.Model.model_validate(fragment)

    async def render(self, config: Config, tag: str | None) -> None:
        model = config.get(Clangd)
        if tag is None or model is None:
            return

        # define top-level config
        top_level: dict[str, Any] = {
            "Diagnostics": {
                "UnusedIncludes": model.Diagnostics.UnusedIncludes,
                "MissingIncludes": model.Diagnostics.MissingIncludes,
                "Suppress": model.Diagnostics.Suppress,
            },
            "Index": {
                "Background": model.Index.Background,
                "StandardLibrary": model.Index.StandardLibrary,
            },
            "Completion": {
                "AllScopes": model.Completion.AllScopes,
                "ArgumentLists": model.Completion.ArgumentLists,
                "HeaderInsertion": model.Completion.HeaderInsertion,
                "CodePatterns": model.Completion.CodePatterns,
            },
            "InlayHints": {
                "Enabled": model.InlayHints.Enabled,
                "ParameterNames": model.InlayHints.ParameterNames,
                "DeducedTypes": model.InlayHints.DeducedTypes,
                "Designators": model.InlayHints.Designators,
                "BlockEnd": model.InlayHints.BlockEnd,
                "DefaultArguments": model.InlayHints.DefaultArguments,
                "TypeNameLimit": model.InlayHints.TypeNameLimit,
            },
            "Hover": {
                "ShowAKA": model.Hover.ShowAKA,
                "MacroContentsLimit": model.Hover.MacroContentsLimit,
            },
            "Documentation": {
                "CommentFormat": model.Documentation.CommentFormat,
            },
        }
        content = dump_yaml(top_level, resource_name=self.name)

        # Add fragments for each `If` section
        for section in model.If:
            fragment: dict[str, Any] = {
                "If": {
                    "PathMatch": section.PathMatch,
                    "PathExclude": section.PathExclude,
                },
            }
            if section.Diagnostics is not None:
                fragment["Diagnostics"] = {
                    "UnusedIncludes": section.Diagnostics.UnusedIncludes,
                    "MissingIncludes": section.Diagnostics.MissingIncludes,
                    "Suppress": section.Diagnostics.Suppress,
                }
            if section.Index is not None:
                fragment["Index"] = {
                    "Background": section.Index.Background,
                    "StandardLibrary": section.Index.StandardLibrary,
                }
            if section.Completion is not None:
                fragment["Completion"] = {
                    "AllScopes": section.Completion.AllScopes,
                    "ArgumentLists": section.Completion.ArgumentLists,
                    "HeaderInsertion": section.Completion.HeaderInsertion,
                    "CodePatterns": section.Completion.CodePatterns,
                }
            if section.InlayHints is not None:
                fragment["InlayHints"] = {
                    "Enabled": section.InlayHints.Enabled,
                    "ParameterNames": section.InlayHints.ParameterNames,
                    "DeducedTypes": section.InlayHints.DeducedTypes,
                    "Designators": section.InlayHints.Designators,
                    "BlockEnd": section.InlayHints.BlockEnd,
                    "DefaultArguments": section.InlayHints.DefaultArguments,
                    "TypeNameLimit": section.InlayHints.TypeNameLimit,
                }
            if section.Hover is not None:
                fragment["Hover"] = {
                    "ShowAKA": section.Hover.ShowAKA,
                    "MacroContentsLimit": section.Hover.MacroContentsLimit,
                }
            if section.Documentation is not None:
                fragment["Documentation"] = {
                    "CommentFormat": section.Documentation.CommentFormat,
                }
            content += "---\n" + dump_yaml(fragment, resource_name=self.name)

        atomic_write_text(
            CONTAINER_TMP_MOUNT / ".clangd",
            content,
            encoding="utf-8"
        )
