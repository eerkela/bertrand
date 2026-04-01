"""A configuration resource for `pyproject.toml`, which is the standard provider for
project metadata.

This resource locates, parses, validates, and renders the `pyproject.toml` file in the
worktree root, which can also carry configuration for all other resources under the
`[tool.*]` tables.  This resource only validates the core `pyproject.toml` fields
defined by PEP 518/621, and defers validation of any tool tables to the relevant
resource(s) that manage those tools.  The rendering logic is designed not to interfere
with any user-managed fields that aren't part of Bertrand's configuration engine.
"""
from __future__ import annotations

from pathlib import Path
from typing import Annotated, Any, Literal, Self

import packaging.licenses
import packaging.requirements
import packaging.specifiers
import packaging.utils
import tomlkit
from email_validator import EmailNotValidError, validate_email
from pydantic import (
    AfterValidator,
    BaseModel,
    ConfigDict,
    Field,
    StringConstraints,
    model_validator,
)
from tomlkit.exceptions import TOMLKitError

from ..run import atomic_write_text
from ..version import VERSION
from .core import (
    RESOURCE_NAMES,
    URL,
    Config,
    Glob,
    NonEmpty,
    NoWhiteSpace,
    PosixPath,
    Resource,
    TagName,
    Trimmed,
    URLLabel,
    resource,
)


def _check_pep508_requirement(requirement: str) -> str:
    try:
        return str(packaging.requirements.Requirement(requirement))
    except packaging.requirements.InvalidRequirement as err:
        raise ValueError(f"invalid PEP 508 requirement: {requirement}") from err


def _check_pep508_name(name: str) -> str:
    try:
        return packaging.utils.canonicalize_name(name, validate=True)
    except packaging.utils.InvalidName as err:
        raise ValueError(f"invalid PEP 508 name: {name}") from err


def _check_pep440_specifier(version: str) -> str:
    try:
        packaging.specifiers.Specifier(version)
    except packaging.specifiers.InvalidSpecifier as err:
        raise ValueError(f"invalid PEP 440 requires-python specifier: {version}") from err
    return version


def _check_license(expression: str) -> str:
    try:
        return packaging.licenses.canonicalize_license_expression(expression)
    except packaging.licenses.InvalidLicenseExpression as err:
        raise ValueError(f"invalid PEP 639 SPDX license expression: {expression}") from err


def _check_email(email: str) -> str:
    try:
        return validate_email(email, check_deliverability=False).normalized
    except EmailNotValidError as err:
        raise ValueError(f"invalid email address: {email}") from err


type PEP508Requirement = Annotated[
    NonEmpty[Trimmed],
    AfterValidator(_check_pep508_requirement)
]
type PEP508Name = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_pep508_name)]
type PEP440Requirement = Annotated[
    NonEmpty[Trimmed],
    AfterValidator(_check_pep440_specifier)
]
type License = Annotated[NonEmpty[Trimmed], AfterValidator(_check_license)]
type Email = Annotated[NonEmpty[Trimmed], AfterValidator(_check_email)]
type EmailName = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    pattern=r"^[^\r\n,]+$"
)]
type Entrypoint = Annotated[str, StringConstraints(
    strip_whitespace=True,
    pattern=(
        r"^[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*:"
        r"[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*$"
    )
)]
type EntrypointName = Annotated[str, StringConstraints(
    strip_whitespace=True,
    pattern=r"^[a-zA-Z_][a-zA-Z0-9_]*$"
)]


def _validate_dependency_groups(*, pyproject: Any | None, bertrand: Any | None) -> None:
    if pyproject is None or bertrand is None:
        return  # only fire once both resources have been parsed

    groups = set(pyproject.project.optional_dependencies)
    tags = {tag.tag for tag in bertrand.tags}
    unknown = sorted(groups.difference(tags))
    missing = sorted(tags.difference(groups))

    # enforce exact match
    problems: list[str] = []
    if unknown:
        problems.append(
            "unknown [project.optional-dependencies] groups with no matching "
            "[[tool.bertrand.tags]].tag: "
            f"{', '.join(repr(name) for name in unknown)}"
        )
    if missing:
        problems.append(
            "missing [project.optional-dependencies] groups for declared "
            "[[tool.bertrand.tags]].tag values: "
            f"{', '.join(repr(name) for name in missing)}"
        )
    if problems:
        raise ValueError("; ".join(problems))


@resource("python", paths={Path("pyproject.toml")})
class PyProject(Resource):
    """A resource describing a `pyproject.toml` file, which is the primary vehicle for
    configuring a top-level Python project, as well as Bertrand itself and its entire
    toolchain via the `[tool.bertrand]` table.
    """
    # pylint: disable=missing-function-docstring, unused-argument, missing-return-doc

    class Model(BaseModel):
        """Validate the core `pyproject.toml` fields, as defined by PEP 518/621."""
        model_config = ConfigDict(extra="forbid")

        class BuildSystem(BaseModel):
            """Validate the `[build-system]` table."""
            model_config = ConfigDict(extra="forbid")

            @staticmethod
            def _check_requires(value: list[PEP508Requirement]) -> list[PEP508Requirement]:
                if value != ["bertrand"]:
                    raise ValueError("build-system.requires must be set to ['bertrand']")
                return value

            requires: Annotated[
                list[PEP508Requirement],
                AfterValidator(_check_requires),
                Field(default_factory=lambda: ["bertrand"])
            ]
            build_backend: Annotated[
                Literal["bertrand.env.build"],
                Field(default="bertrand.env.build", alias="build-backend")
            ]

        build_system: Annotated[BuildSystem, Field(
            default_factory=BuildSystem.model_construct,
            alias="build-system"
        )]

        class Project(BaseModel):
            """Validate the `[project]` table."""
            model_config = ConfigDict(extra="allow")
            name: Annotated[PEP508Name, Field(
                description=
                    "Canonical project name, which is initially seeded from the "
                    "worktree root directory name, but can be overridden by the user.",
            )]
            version: Annotated[str, Field(
                description=
                    "Project version string, which should ideally follow semantic "
                    "versioning (MAJOR.MINOR.MICRO), but is not required to.",
            )]
            requires_python: Annotated[PEP440Requirement, Field(
                default=f">={VERSION.python}",
                alias="requires-python",
                description="Container toolchain's Python version.",
            )]
            description: Annotated[str | None, Field(
                default=None,
                description="A short, one-line summary of the project.",
            )]
            readme: Annotated[PosixPath | None, Field(
                default=None,
                description=
                    "Relative (POSIX) path to the project's README file, starting from "
                    "the worktree root.",
            )]
            license: Annotated[License | None, Field(
                default=None,
                description="SPDX license identifier (e.g. MIT, Apache-2.0, etc.).",
            )]
            license_files: Annotated[list[Glob] | None, Field(
                default=None,
                alias="license-files",
                description=
                    "list of relative (POSIX) paths from worktree root to license "
                    "files, supporting glob patterns.",
            )]

            class Author(BaseModel):
                """Validate entries in the `authors` and `maintainers` lists."""
                model_config = ConfigDict(extra="forbid")
                name: Annotated[EmailName | None, Field(
                    default=None,
                    description=
                        "Author name, which can be an email local-part but must not "
                        "contain commas or newlines (to avoid ambiguity when parsing)"
                )]
                email: Annotated[Email | None, Field(
                    default=None,
                    description="Contact email address"
                )]

                @model_validator(mode="after")
                def _require_name_or_email(self) -> Self:
                    if self.name is None and self.email is None:
                        raise ValueError("at least one of 'name' or 'email' must be provided")
                    return self

            authors: Annotated[list[Author], Field(
                default_factory=list,
                description="List of project authors."
            )]
            maintainers: Annotated[list[Author], Field(
                default_factory=list,
                description="List of project maintainers."
            )]
            keywords: Annotated[list[str], Field(
                default_factory=list,
                description=
                    "Arbitrary list of keywords describing the project, for search "
                    "optimization.",
            )]
            classifiers: Annotated[list[str], Field(
                default_factory=list,
                description="List of PyPI classifiers (https://pypi.org/classifiers/)."
            )]
            urls: Annotated[dict[URLLabel, URL], Field(
                default_factory=dict,
                description=
                    "Mapping of URL labels to project URLs (e.g. documentation, "
                    "source code repository, etc.).  PEP753 defines a standard set of "
                    "labels that third-party tools may recognize."
            )]
            dependencies: Annotated[list[PEP508Requirement], Field(
                default_factory=list,
                description=
                    "Python-level dependencies as PEP508 requirement specifiers.",
            )]
            optional_dependencies: Annotated[dict[TagName, list[PEP508Requirement]], Field(
                default_factory=dict,
                alias="optional-dependencies",
                description=
                    "Mapping of optional dependency groups, which should exactly match "
                    "the declared tags in [tool.bertrand.tags], to further Python-level "
                    "dependencies.  Using dependency groups allows package managers to "
                    "select tags via normal 'extras' syntax (e.g. "
                    "`pip install myproject[dev]`).",
            )]
            scripts: Annotated[dict[EntrypointName, Entrypoint], Field(
                default_factory=dict,
                description=
                    "Mapping of console script entry points, where keys are the "
                    "exposed command names and values are the corresponding "
                    "importable entry points in 'module:object' format.  ':object' "
                    "typically points to a 'main' function, which may be implemented "
                    "in C++.",
            )]
            gui_scripts: Annotated[dict[EntrypointName, Entrypoint], Field(
                default_factory=dict,
                alias="gui-scripts",
                description=
                    "Mapping of GUI script entry points, where keys are the "
                    "exposed command names and values are the corresponding "
                    "importable entry points in 'module:object' format.  These "
                    "behave similarly to 'scripts', but additionally mount a "
                    "Wayland socket to allow GUI access from within the container.",
            )]

            @model_validator(mode="after")
            def _validate_script_collisions(self) -> Self:
                collisions = set(self.scripts).intersection(set(self.gui_scripts))
                if collisions:
                    raise ValueError(
                        "duplicate script names across 'project.scripts' and "
                        f"'project.gui-scripts': {', '.join(sorted(collisions))}"
                    )
                return self

            def resolve_licenses(self, root: Path) -> None:
                seen: set[str] = set()
                for pattern in self.license_files or ():
                    for path in sorted(
                        (p for p in root.glob(pattern) if p.is_file()),
                        key=lambda p: p.as_posix()
                    ):
                        relative = path.relative_to(root).as_posix()
                        if relative not in seen:
                            try:
                                path.read_text(encoding="utf-8")
                            except UnicodeDecodeError as err:
                                raise OSError(
                                    f"license file is not UTF-8 encoded '{relative}': "
                                    f"{err}"
                                ) from err
                            seen.add(relative)

        project: Project

    async def init(self, config: Config, cli: Config.Init) -> dict[str, Any]:
        return self.Model.model_construct(
            project=self.Model.Project.model_construct(
                name=cli.repo.git_dir.parent.name,
                version="0.1.0",
            )
        ).model_dump(by_alias=True)

    async def parse(self, config: Config) -> dict[str, dict[str, Any]]:
        # get content of the current worktree's `pyproject.toml`
        path = config.worktree / "pyproject.toml"
        if not path.exists():
            return {}
        try:
            text = path.read_text(encoding="utf-8")
        except OSError as err:
            raise OSError(
                f"failed to read pyproject for resource '{self.name}' at {path}: {err}"
            ) from err

        # load toml mapping
        try:
            parsed = tomlkit.parse(text).unwrap()
        except TOMLKitError as err:
            raise OSError(
                f"failed to parse pyproject TOML for resource '{self.name}' at {path}: {err}"
            ) from err
        if not isinstance(parsed, dict):
            raise OSError(f"expected mapping at 'pyproject', got {type(parsed).__name__}")

        # normalize core pyproject.toml fields
        snapshot: dict[str, Any] = {}
        build_system = parsed.get("build-system")
        project = parsed.get("project")
        if isinstance(build_system, dict) and isinstance(project, dict):
            snapshot[self.name] = {
                "build-system": build_system,
                "project": project,
            }

        # search `tool.{key}` tables for any fields matching a known resource name, and
        # add them to the returned snapshot under the same top-level keys, so that they
        # can be validated by the relevant resource(s) during the `validate()` phase
        tool = parsed.get("tool")
        if isinstance(tool, dict):
            for key, value in tool.items():
                if key in RESOURCE_NAMES and snapshot.setdefault(key, value) is not value:
                    raise OSError(
                        f"conflicting top-level keys in pyproject.toml for resource "
                        f"{self.name!r}: {key!r} is present in both 'tool' and the "
                        "core table"
                    )

        return snapshot

    async def validate(self, config: Config, fragment: Any) -> Model | None:
        from .bertrand import Bertrand
        result = self.Model.model_validate(fragment)
        result.project.resolve_licenses(config.worktree)
        _validate_dependency_groups(pyproject=result, bertrand=config.get(Bertrand))
        return result

    async def render(self, config: Config, tag: str | None) -> None:
        python = config.get(PyProject)
        if python is None:
            return

        # parse existing `pyproject.toml` or initialize an empty document
        path = config.worktree / "pyproject.toml"
        if path.exists():
            try:
                text = path.read_text(encoding="utf-8")
            except OSError as err:
                raise OSError(f"failed to read pyproject at {path}: {err}") from err
            try:
                doc = tomlkit.parse(text)
            except TOMLKitError as err:
                raise OSError(f"failed to parse pyproject TOML at {path}: {err}") from err
        else:
            doc = tomlkit.document()

        # fully overwrite [build-system] table
        doc["build-system"] = python.build_system.model_dump(
            by_alias=True,
            exclude_none=True
        )

        # conservatively overwrite [project] while preserving unrelated user keys
        project_table = doc.get("project")
        if project_table is None:
            project_table = tomlkit.table()
            doc["project"] = project_table
        elif not isinstance(project_table, dict):
            raise OSError("invalid pyproject shape: '[project]' must be a table")
        for key, value in python.project.model_dump(
            by_alias=True,
            exclude_none=True
        ).items():
            project_table[key] = value

        # TODO: the new `config.resources[ResourceName] -> Model | None` API should
        # simplify this

        # Render all Config.Tool models (except python) to [tool.<resource>] using
        # canonical resource names
        tool_models: dict[Resource, BaseModel] = {}
        for name in config.resources:
            if name == "python":
                continue
            model = config.resources.get(name)
            if model is None:
                continue
            lookup = RESOURCE_NAMES.get(name)
            if lookup is None or lookup.name != name:
                continue
            tool_models[lookup] = model
        if tool_models:
            tool_table = doc.get("tool")
            if tool_table is None:
                tool_table = tomlkit.table()
                doc["tool"] = tool_table
            elif not isinstance(tool_table, dict):
                raise OSError("invalid pyproject shape: '[tool]' must be a table")
            for resource, model in sorted(tool_models.items()):
                tool_table[resource.name] = model.model_dump(
                    by_alias=True,
                    exclude_none=True
                )

        # write back if any changes were made
        rendered = tomlkit.dumps(doc)
        if not rendered.endswith("\n"):
            rendered += "\n"
        if rendered != text:
            atomic_write_text(path, rendered, encoding="utf-8")
