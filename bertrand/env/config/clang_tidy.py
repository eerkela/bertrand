"""Render clang-tidy configuration.

This resource generates a `.clang-tidy` artifact from a standardized `[clang-tidy]`
schema stored in project configuration.  The default settings are designed to be
conservative and non-blocking, but can be customized as needed, and are exhaustively
listed in a self-documenting fashion.
"""

from __future__ import annotations

from typing import Annotated, Any, Literal, Self

from pydantic import (
    AfterValidator,
    BaseModel,
    ConfigDict,
    Field,
    StringConstraints,
    model_validator,
)

from bertrand.env.git import CONTAINER_TMP_MOUNT, Scalar, atomic_write_text

from .core import (
    Config,
    RegexPattern,
    Resource,
    dump_yaml,
    resource,
)

type ClangTidyCheckPattern = Annotated[
    str,
    StringConstraints(strip_whitespace=True, min_length=1, pattern=r"^[^,\s\r\n]+$"),
]
type ClangTidyOptionName = Annotated[
    str,
    StringConstraints(
        strip_whitespace=True, min_length=1, pattern=r"^[A-Za-z_][A-Za-z0-9_]*$"
    ),
]


@resource("clang-tidy")
class ClangTidy(Resource):
    """Describe a `.clang-tidy` configuration file.

    This expects native clang-tidy key names in TOML. `Checks` and
    `WarningsAsErrors` may be specified as arrays for convenience and will be joined
    to comma-separated strings.
    """

    class Model(BaseModel):
        """Validate the `[clang-tidy]` table."""

        class Check(BaseModel):
            """Validate entries in the `[[tool.clang-tidy.Checks]]` AoT."""

            model_config = ConfigDict(extra="forbid")
            Enable: Annotated[
                ClangTidyCheckPattern | None,
                Field(
                    default=None,
                    examples=["modernize-use-auto", "performance-*"],
                    description=(
                        "A clang-tidy check pattern to enable.  Must be unique across "
                        "both 'Enable' and 'Disable' entries."
                    ),
                ),
            ]
            Disable: Annotated[
                ClangTidyCheckPattern | None,
                Field(
                    default=None,
                    examples=["modernize-use-auto", "performance-*"],
                    description=(
                        "A clang-tidy check pattern to explicitly disable.  This may "
                        "be a subset of the 'Enabled' checks."
                    ),
                ),
            ]
            Action: Annotated[
                Literal["disable", "warn", "error"],
                Field(
                    default="warn",
                    examples=["disable", "warn", "error"],
                    description=(
                        "The action to take for this check pattern.  'disable' turns "
                        "off the check, 'warn' enables the check and reports "
                        "diagnostics as warnings, and 'error' promotes the check to an "
                        "error."
                    ),
                ),
            ]
            Options: Annotated[
                dict[ClangTidyOptionName, Scalar],
                Field(
                    default_factory=dict,
                    description=(
                        "A mapping of custom options for this check.  See the "
                        "clang-tidy documentation for which options are supported by "
                        "each check, and how to format their values."
                    ),
                ),
            ]

            @model_validator(mode="after")
            def _validate_enable_or_disable(self) -> Self:
                if (self.Enable is None) == (self.Disable is None):
                    msg = (
                        "each entry in [[tool.clang-tidy.Checks]] must define "
                        "exactly one of 'Enable' or 'Disable'"
                    )
                    raise ValueError(msg)
                return self

        @staticmethod
        def _check_duplicate_checks(value: list[Check]) -> list[Check]:
            seen: set[ClangTidyCheckPattern] = set()
            for entry in value:
                if entry.Enable is not None:
                    if entry.Enable in seen:
                        msg = f"duplicate clang-tidy check entry: '{entry.Enable}'"
                        raise ValueError(msg)
                    seen.add(entry.Enable)
                if entry.Disable is not None:
                    if entry.Disable in seen:
                        msg = f"duplicate clang-tidy check entry: '{entry.Disable}'"
                        raise ValueError(msg)
                    seen.add(entry.Disable)
            return value

        model_config = ConfigDict(extra="forbid")
        DisableFormat: Annotated[
            bool,
            Field(
                default=False,
                description="Enable clang-format fixes for clang-tidy diagnostics.",
            ),
        ]
        HeaderFilterRegex: Annotated[
            RegexPattern,
            Field(
                default="^.*$",
                description=(
                    "A regex pattern to filter which headers are included in "
                    "clang-tidy diagnostics.  Only diagnostics from headers that match "
                    "this pattern will be included.  This can be used to focus "
                    "diagnostics on project headers and exclude external dependencies, "
                    "which often have noisy diagnostics that the user cannot fix."
                ),
            ),
        ]
        ExcludeHeaderFilterRegex: Annotated[
            RegexPattern,
            Field(
                default="^$",
                description=(
                    "A regex pattern to filter which headers are excluded from "
                    "clang-tidy diagnostics.  Only diagnostics from headers that do "
                    "not match this pattern will be included, regardless of whether "
                    "they match 'HeaderFilterRegex'."
                ),
            ),
        ]
        SystemHeaders: Annotated[
            bool,
            Field(
                default=False,
                description=(
                    "Control whether diagnostics from system headers are included.  "
                    "This overrides 'HeaderFilterRegex' and 'ExcludeHeaderFilterRegex' "
                    "for system headers."
                ),
            ),
        ]
        UseColor: Annotated[
            bool,
            Field(
                default=True,
                description=(
                    "Use color output where possible, if the terminal supports it."
                ),
            ),
        ]
        Checks: Annotated[
            list[Check],
            AfterValidator(_check_duplicate_checks),
            Field(
                default_factory=list,
                description=(
                    "List of clang-tidy checks to enable or disable.  Checks will be "
                    "processed in order, so later entries can override earlier ones.  "
                    "See the clang-tidy documentation for available checks and their "
                    "options."
                ),
            ),
        ]

    async def init(self, config: Config, cli: Config.Init) -> dict[str, Any]:
        """Return default clang-tidy configuration.

        Returns
        -------
        dict[str, Any]
            Default clang-tidy configuration data.
        """
        _ = config, cli
        return self.Model.model_construct().model_dump(by_alias=True)

    async def validate(self, config: Config, fragment: Any) -> Model | None:
        """Validate clang-tidy configuration.

        Returns
        -------
        Model | None
            Validated clang-tidy configuration.
        """
        _ = config
        return self.Model.model_validate(fragment)

    async def render(self, config: Config, tag: str | None) -> None:
        """Render the `.clang-tidy` artifact.

        Raises
        ------
        OSError
            If duplicate effective check options are configured.
        """
        model = config.get(ClangTidy)
        if tag is None or model is None:
            return

        # define top-level config
        content: dict[str, Any] = {
            # enforce deterministic configs at project scope
            "InheritParentConfig": False,
            "FormatStyle": "none" if model.DisableFormat else "file",
            "HeaderFilterRegex": model.HeaderFilterRegex,
            "ExcludeHeaderFilterRegex": model.ExcludeHeaderFilterRegex,
            "SystemHeaders": model.SystemHeaders,
            "UseColor": model.UseColor,
        }

        # define checks
        checks: list[str] = []
        check_options: dict[str, Scalar] = {}
        warnings_as_errors: list[str] = []
        for check in model.Checks:
            if check.Enable is not None:
                checks.append(check.Enable)
                if check.Options:
                    for key, value in check.Options.items():
                        option_key = f"{check.Enable}: {key}"
                        if option_key in check_options:
                            msg = (
                                f"duplicate clang-tidy check option '{option_key}' "
                                "(check names must be unique across all checks)"
                            )
                            raise OSError(msg)
                        check_options[option_key] = value
                if check.Action == "error":
                    warnings_as_errors.append(check.Enable)
            if check.Disable is not None:
                checks.append(f"-{check.Disable}")

        # render yaml
        if checks:
            content["Checks"] = ",".join(checks)
        if warnings_as_errors:
            content["WarningsAsErrors"] = ",".join(warnings_as_errors)
        if check_options:
            content["CheckOptions"] = check_options

        atomic_write_text(
            CONTAINER_TMP_MOUNT / ".clang-tidy",
            dump_yaml(content, resource_name=self.name),
            encoding="utf-8",
        )

    async def schema(self) -> dict[str, Any]:
        """Return the clang-tidy configuration schema.

        Returns
        -------
        dict[str, Any]
            JSON Schema for the clang-tidy configuration.
        """
        return self.Model.model_json_schema(by_alias=True, mode="validation")
