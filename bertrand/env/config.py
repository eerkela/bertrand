"""Layout schema and init-time orchestration for Bertrand environments.

This module is intentionally scoped to a minimal, ctx-driven backend for
`bertrand init`:

1. Build a deterministic layout manifest.
2. Persist it in `.bertrand/env.json` under top-level `layout`.
3. Render and write templated bootstrap resources in deterministic phases.

Canonical templates are packaged with Bertrand and lazily hydrated into
`on_init` pipeline state under `templates/...` before rendering.
"""
from __future__ import annotations

import json
import os
import tomllib

from dataclasses import asdict, dataclass, field
from collections.abc import Mapping, Sequence
from importlib import resources
from importlib.resources.abc import Traversable
from pathlib import Path, PosixPath
from types import MappingProxyType, TracebackType
from typing import Annotated, Any, Callable, Iterator, Literal, Self, TypeVar

from jinja2 import Environment, StrictUndefined
from pydantic import (
    BaseModel,
    ConfigDict,
    Field,
    ValidationError,
    field_validator,
    model_validator,
)
import yaml

from .pipeline import Mkdir, Pipeline, WriteText
from .run import LOCK_TIMEOUT, Lock, atomic_write_text, sanitize_name
from .version import __version__


# Canonical path and name definitions for shared resources
ENV_DIR_NAME: str = ".bertrand"
ENV_FILE_NAME: str = "env.json"
ENV_LOCK_NAME: str = ".lock"
ENV_LAYOUT_KEY: str = "layout"
LAYOUT_SCHEMA_VERSION: int = 1
MOUNT: PosixPath = PosixPath("/env")
assert MOUNT.is_absolute()


# CLI options that affect template rendering
AGENTS: dict[str, tuple[str, ...]] = {
    "none": (),
    "claude": ("anthropic.claude-code",),
    "codex": ("openai.chatgpt",),
}
ASSISTS: dict[str, tuple[str, ...]] = {
    "none": (),
    "copilot": ("GitHub.copilot", "GitHub.copilot-chat"),
}
EDITORS: dict[str, str] = {
    "vscode": "code",
}
SHELLS: dict[str, tuple[str, ...]] = {
    "bash": ("bash", "-l"),
}
DEFAULT_AGENT: str = "none"
DEFAULT_ASSIST: str = "none"
DEFAULT_EDITOR: str = "vscode"
DEFAULT_SHELL: str = "bash"
if DEFAULT_AGENT not in AGENTS:
    raise RuntimeError(f"default agent is unsupported: {DEFAULT_AGENT}")
if DEFAULT_ASSIST not in ASSISTS:
    raise RuntimeError(f"default assist is unsupported: {DEFAULT_ASSIST}")
if DEFAULT_EDITOR not in EDITORS:
    raise RuntimeError(f"default editor is unsupported: {DEFAULT_EDITOR}")
if DEFAULT_SHELL not in SHELLS:
    raise RuntimeError(f"default shell is unsupported: {DEFAULT_SHELL}")


# TODO: would it be better to place these vscode managed workspace details into a
# template and render it on init only?

# Resource IDs and defaults for editor-specific managed artifacts.
VSCODE_WORKSPACE_RESOURCE_ID: str = "vscode-workspace"
_VSCODE_REMOTE_EXTENSION: str = "ms-vscode-remote.remote-containers"
_VSCODE_CLANGD_EXTENSION: str = "llvm-vs-code-extensions.vscode-clangd"
_VSCODE_PYTHON_EXTENSION: str = "ms-python.python"
_VSCODE_RUFF_EXTENSION: str = "charliermarsh.ruff"
_VSCODE_TY_EXTENSION: str = "astral-sh.ty"
_VSCODE_BASE_RECOMMENDED_EXTENSIONS: tuple[str, ...] = (
    _VSCODE_REMOTE_EXTENSION,
    _VSCODE_CLANGD_EXTENSION,
    _VSCODE_PYTHON_EXTENSION,
    _VSCODE_RUFF_EXTENSION,
    _VSCODE_TY_EXTENSION,
)
_VSCODE_MANAGED_SETTINGS: dict[str, Any] = {
    "C_Cpp.intelliSenseEngine": "disabled",
    "clangd.path": "clangd",
    "[python]": {
        "editor.defaultFormatter": _VSCODE_RUFF_EXTENSION,
        "editor.formatOnSave": True,
        "editor.codeActionsOnSave": {
            "source.fixAll.ruff": "explicit",
            "source.organizeImports.ruff": "explicit",
        },
    },
    "ty.serverMode": "languageServer",
    "ty.disableLanguageServices": True,
    "python.testing.pytestEnabled": True,
    "python.testing.unittestEnabled": False,
    "python.testing.pytestPath": "pytest",
    "python.testing.pytestArgs": ["."],
}
_VSCODE_MANAGED_TASKS: dict[str, Any] = {
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Bertrand: pytest",
            "type": "shell",
            "command": "pytest -q",
            "options": {"cwd": "${workspaceFolder}"},
            "problemMatcher": [],
        },
        {
            "label": "Bertrand: ruff check",
            "type": "shell",
            "command": "ruff check .",
            "options": {"cwd": "${workspaceFolder}"},
            "problemMatcher": [],
        },
        {
            "label": "Bertrand: ruff format",
            "type": "shell",
            "command": "ruff format .",
            "options": {"cwd": "${workspaceFolder}"},
            "problemMatcher": [],
        },
        {
            "label": "Bertrand: ty check",
            "type": "shell",
            "command": "ty check .",
            "options": {"cwd": "${workspaceFolder}"},
            "problemMatcher": [],
        },
    ],
}


# In-container environment variables for relevant configuration, for use in upstream
# subsystems like the container runtime and editor integration.
CONTAINER_ID_ENV: str = "BERTRAND_CONTAINER_ID"
CONTAINER_BIN_ENV: str = "BERTRAND_CODE_PODMAN_BIN"
EDITOR_BIN_ENV: str = "BERTRAND_CODE_EDITOR_BIN"
HOST_ENV: str = "BERTRAND_HOST_ENV"


# Global resource catalog.  Extensions can add resources here with associated behavior,
# and then update the capabilities and/or profiles to place them in the generated
# layouts, without needing to change any of the core layout application logic.
CATALOG: dict[str,  Resource] = {}
T = TypeVar("T", bound="Resource")


class Template(BaseModel):
    """Stable template reference used by layout resources.

    Canonical templates are packaged with Bertrand under `env/templates` and addressed
    by stable `{namespace}/{name}/{version}` references.  They are lazily hydrated into
    the `on_init` state cache before rendering.
    """
    model_config = ConfigDict(extra="forbid")
    namespace: Annotated[str, Field(description="Template namespace, e.g. 'core'.")]
    name: Annotated[str, Field(description="Template resource name, e.g. 'pyproject'.")]
    version: Annotated[
        str,
        Field(
            description=
                "Stable template version identifier, e.g. '2026-02-15'.  No specific "
                "format is required, but a date-based convention is recommended for "
                "clarity and collision avoidance."
        ),
    ]

    @field_validator("namespace", "name", "version")
    @classmethod
    def _validate_non_empty(cls, value: str) -> str:
        text = value.strip()
        if not text:
            raise ValueError("template reference fields must be non-empty")
        return text

    def packaged_path(self) -> Traversable:
        """Return a path to the version of this template that is packaged with
        Bertrand itself, which will be hydrated into the `on_init` state cache during
        layout application if not already present.

        Returns
        -------
        Traversable
            A path to the packaged template file corresponding to this reference.
        """
        return resources.files("bertrand.env").joinpath("templates").joinpath(
            self.namespace,
            self.name,
            f"{self.version}.j2",
        )


def resource(
    name: str,
    *,
    kind: Literal["file", "dir"],
    template: str | None = None,
) -> Callable[[type[T]], type[T]]:
    """A class decorator for defining layout resources.

    Parameters
    ----------
    name : str
        The unique name of this resource, which serves as its stable identifier in the
        layout manifest and catalog.  This should generally match the `name` portion
        of a corresponding template, if one is given.
    kind : Literal["file", "dir"]
        The type of this resource, which determines how it is rendered and applied.
    template : str | None, optional
        An optional reference to a Jinja template for this resource, of the form
        "namespace/name/version".  If given, the template will be loaded from the
        `on_init` pipeline's state directory, under
        `templates/namespace/name/version.j2`, and will be used to initialize the
        resource's content during `Config.init()`.  If none is given, then the resource
        will not be written during layout initialization.

    Returns
    -------
    Callable[[type[T]], type[T]]
        A class decorator that registers the decorated class as a layout resource in the
        global `CATALOG` under the given name, with the specified kind and template.

    Raises
    ------
    TypeError
        If the resource name is not lowercase without leading or trailing whitespace,
        if it is not unique in the `CATALOG`, or if a template is given for a non-file
        resource.
    """
    norm = name.strip().lower()
    if not norm:
        raise TypeError("resource name cannot be empty")
    if name != norm:
        raise TypeError(
            "resource name must be lowercase and cannot have leading or trailing "
            f"whitespace: '{name}'"
        )

    template_kwargs: dict[str, str] | None = None
    if template is not None:
        if kind != "file":
            raise TypeError(f"only file resources can define a template reference: '{name}'")
        parts = template.split("/")
        if len(parts) != 3:
            raise TypeError(
                f"invalid template reference format for resource '{name}': '{template}' "
                "(expected 'namespace/name/version')"
            )
        template_kwargs = {"namespace": parts[0], "name": parts[1], "version": parts[2]}

    def _decorator(cls: type[T]) -> type[T]:
        if name in CATALOG:
            raise TypeError(f"duplicate resource name in catalog: '{name}'")
        CATALOG[name] = cls(
            name=name,
            kind=kind,
            template=Template(**template_kwargs) if template_kwargs is not None else None,
        )
        return cls

    return _decorator


@resource("containerfile", kind="file", template="core/containerfile/2026-02-15")
@resource("containerignore", kind="file", template="core/containerignore/2026-02-15")
@resource("docs", kind="dir")
@resource("tests", kind="dir")
@resource("src", kind="dir")
@dataclass(frozen=True)
class Resource:
    """A base class describing a single file or directory being managed by the layout
    system.  This is meant to be used in conjunction with the `@resource` class
    decorator in order to register default-constructed resources in the global
    `CATALOG` without coupling to any particular schema.

    Attributes
    ----------
    name : str
        The name that was assigned to this resource in `@resource()`.
    kind : Literal["file", "dir"]
        The kind that was assigned to this resource in `@resource()`.
    template : Template | None
        The template reference that was assigned to this resource in `@resource()`, if
        any.
    """
    class JSON(BaseModel):
        """Serialized representation of a Resource, which is stored inside an
        environment's layout manifest and used to reconstruct the Resource.
        """
        model_config = ConfigDict(extra="forbid")
        kind: Annotated[
            Literal["file", "dir"],
            Field(description="The type of resource, either 'file' or 'dir'."),
        ]
        path: Annotated[
            PosixPath,
            Field(
                description=
                    "The relative path of the resource starting from the environment root.  "
                    "Always stored as a POSIX path."
            ),
        ]
        template: Annotated[
            Template | None,
            Field(
                description=
                    "An optional reference to a template used to render the contents of a "
                    "file resource.  Must be None for non-file resources.",
            ),
        ] = None

        @field_validator("path")
        @classmethod
        def _validate_path(cls, value: PosixPath) -> PosixPath:
            if value.is_absolute():
                raise ValueError(f"layout resource paths must be relative: {value}")
            if value == PosixPath("."):
                raise ValueError("layout resource path must not be empty")
            if any(part == ".." for part in value.parts):
                raise ValueError(f"layout resource path must not traverse parents: {value}")
            return value

        @model_validator(mode="after")
        def _validate(self) -> Self:
            if self.kind != "file" and self.template is not None:
                raise ValueError(
                    "non-file layout resources must not define a template reference"
                )
            return self

    # pylint: disable=unused-argument, redundant-returns-doc
    name: str
    kind: Literal["file", "dir"]
    template: Template | None

    def parse(self, config: Config) -> dict[str, Any] | None:
        """A parser function that can extract normalized config data from this
        resource when entering the `Config` context.

        Parameters
        ----------
        config : Config
            The active configuration context, which provides access to the
            resource's path and other shared state.

        Returns
        -------
        dict[str, Any] | None
            Normalized config data extracted from this resource, or None if no parsing
            was performed.
        """
        return None

    def render(self, config: Config) -> str | None:
        """A renderer function that can produce text content for this resource
        during `Config.sync()`.  This is used to generate derived artifacts from
        the layout without coupling to any particular schema.

        Parameters
        ----------
        config : Config
            The active configuration context, which provides access to the
            resource's path and other shared state.

        Returns
        -------
        str | None
            The text content to write for this resource, or None if no rendering
            was performed.
        """
        return None

    def sources(self, config: Config) -> list[Path] | None:
        """A special parser function that can resolve source files referenced by
        this resource, so that we can reconstruct a compilation database from files
        similar to `compile_commands.json` without coupling to any particular
        schema.

        Parameters
        ----------
        config : Config
            The active configuration context, which provides access to the
            resource's path and other shared state.

        Returns
        -------
        list[Path] | None
            A list of file paths referenced by this resource, or None if no sources
            were resolved.
        """
        return None


class Manifest(BaseModel):
    """Serializable resource manifest persisted in environment metadata.

    A manifest of this form is stored in `env.json` under the top-level `layout` key,
    and can be loaded to reconstruct the layout after initialization.
    """
    model_config = ConfigDict(extra="forbid")
    schema_version: Annotated[
        int,
        Field(gt=0, description="Version number, for forward compatibility."),
    ] = LAYOUT_SCHEMA_VERSION
    profile: Annotated[
        str,
        Field(
            description=
                "The layout profile used to generate this manifest, e.g. 'flat' or 'src'."
        ),
    ]
    capabilities: list[str] = Field(
        default_factory=list,
        description=
            "List of language capabilities included in this layout, e.g. 'python' "
            "and 'cpp'.  This field is reserved for future use with other languages."
    )
    resources: dict[str, Resource.JSON] = Field(
        default_factory=dict,
        description=
            "Mapping of resource IDs to their specifications.  Resource IDs are "
            "arbitrary strings that serve as stable identifiers for resources, and "
            "should generally match the `name` portion of a corresponding template."
    )

    @field_validator("profile")
    @classmethod
    def _validate_profile(cls, value: str) -> str:
        text = value.strip().lower()
        if not text:
            raise ValueError("layout profile must be non-empty")
        return text

    @field_validator("capabilities")
    @classmethod
    def _validate_capabilities(cls, values: list[str]) -> list[str]:
        # capability names must be non-empty strings with no duplicates
        out: list[str] = []
        seen: set[str] = set()
        for raw in values:
            capability = raw.strip().lower()
            if not capability:
                raise ValueError("layout capabilities must be non-empty strings")
            if capability in seen:
                continue
            seen.add(capability)
            out.append(capability)
        return out

    @field_validator("resources", mode="before")
    @classmethod
    def _validate_resources(cls, value: Any) -> Any:
        if not isinstance(value, dict):
            return value

        # resource ids must be non-empty strings with no duplicates
        normalized: dict[str, Any] = {}
        seen: set[str] = set()
        for raw_id, spec in value.items():
            if not isinstance(raw_id, str):
                raise ValueError("layout resource IDs must be strings")
            resource_id = raw_id.strip()
            if not resource_id:
                raise ValueError("layout resource IDs must be non-empty")
            if resource_id in seen:
                raise ValueError(f"duplicate layout resource ID: {resource_id}")
            seen.add(resource_id)
            normalized[resource_id] = spec
        return normalized

    @model_validator(mode="after")
    def _validate(self) -> Manifest:
        if self.schema_version != LAYOUT_SCHEMA_VERSION:
            raise ValueError(
                f"unsupported layout schema version: {self.schema_version} "
                f"(expected {LAYOUT_SCHEMA_VERSION})"
            )

        # validate no duplicate paths
        by_parts: dict[tuple[str, ...], tuple[str, Resource.JSON]] = {}
        for resource_id in self.resources:
            r = self.resources[resource_id]
            parts = r.path.parts
            existing = by_parts.get(parts)
            if existing is not None:
                existing_id, _ = existing
                raise ValueError(
                    f"layout path collision between resource IDs '{existing_id}' and "
                    f"'{resource_id}' at '{r.path}'"
                )
            by_parts[parts] = (resource_id, r)

        # validate no file ancestors in paths
        for resource_id in self.resources:
            r = self.resources[resource_id]
            parts = r.path.parts
            for depth in range(1, len(parts)):
                parent_parts = parts[:depth]
                parent = by_parts.get(parent_parts)
                if parent is None:
                    continue
                parent_id, parent_r = parent
                if parent_r.kind == "file":
                    parent_path = PosixPath(*parent_parts)
                    raise ValueError(
                        f"layout resource '{resource_id}' at '{r.path}' cannot be nested "
                        f"under file resource '{parent_id}' at '{parent_path}'"
                    )

        return self


def _template_path(ctx: Pipeline.InProgress, ref: Template) -> Path:
    return ctx.state_dir / "templates" / ref.namespace / ref.name / f"{ref.version}.j2"


def _env_dir(root: Path) -> Path:
    return root.expanduser().resolve() / ENV_DIR_NAME


def _env_file(root: Path) -> Path:
    return _env_dir(root) / ENV_FILE_NAME


def lock_env(root: Path, timeout: float = LOCK_TIMEOUT) -> Lock:
    """Lock an environment directory for exclusive access, hiding the lock inside the
    environment metadata directory.

    Parameters
    ----------
    root : Path
        The root path of the environment to lock.
    timeout : float, optional
        The maximum number of seconds to wait for the lock to be acquired before
        raising a `TimeoutError`.  See `Lock()` for the default value.

    Returns
    -------
    Lock
         A lock instance representing the acquired lock on the environment directory.
    """
    # NOTE: pre-touching the lock's parent ensures that lock acquisition is atomic
    lock_dir = _env_dir(root)
    lock_dir.mkdir(parents=True, exist_ok=True)
    return Lock(lock_dir / ENV_LOCK_NAME, timeout=timeout)


def _read_env_json(env_root: Path, *, missing_ok: bool = False) -> dict[str, Any]:
    env_file = _env_file(env_root)
    if not env_file.exists():
        if missing_ok:
            return {}
        raise FileNotFoundError(f"environment metadata file not found: {env_file}")
    if not env_file.is_file():
        raise OSError(f"environment metadata path is not a file: {env_file}")

    try:
        data = json.loads(env_file.read_text(encoding="utf-8"))
    except Exception as err:
        raise OSError(f"failed to parse environment metadata at {env_file}: {err}") from err
    if not isinstance(data, dict):
        raise OSError(f"environment metadata at {env_file} must be a JSON object")
    return data


def _expect_str(name: str, ctx: Pipeline.InProgress) -> str:
    value = ctx.get(name)
    if not isinstance(value, str):
        raise TypeError(f"{name} must be a string")
    text = value.strip()
    if not text:
        raise OSError(f"{name} cannot be empty")
    return text


def _require_dict(value: Any, *, where: str) -> dict[str, Any]:
    if not isinstance(value, Mapping):
        raise OSError(f"expected mapping at '{where}', got {type(value).__name__}")
    out: dict[str, Any] = {}
    for key, item in value.items():
        if not isinstance(key, str):
            raise OSError(f"expected string keys at '{where}', got {type(key).__name__}")
        out[key] = item
    return out


def _require_str_value(value: Any, *, where: str, allow_empty: bool = False) -> str:
    if not isinstance(value, str):
        raise OSError(f"expected string at '{where}', got {type(value).__name__}")
    text = value.strip()
    if not allow_empty and not text:
        raise OSError(f"expected non-empty string at '{where}'")
    return text


def _require_str_list(value: Any, *, where: str) -> list[str]:
    if isinstance(value, (str, bytes)) or not isinstance(value, Sequence):
        raise OSError(
            f"expected sequence[str] at '{where}', got {type(value).__name__}"
        )
    out: list[str] = []
    for idx, item in enumerate(value):
        out.append(_require_str_value(item, where=f"{where}[{idx}]"))
    return out


def _freeze(value: Any) -> Any:
    """Recursively freeze dictionaries and lists into immutable containers."""
    if isinstance(value, Mapping):
        return MappingProxyType({k: _freeze(v) for k, v in value.items()})
    if isinstance(value, list):
        return tuple(_freeze(item) for item in value)
    if isinstance(value, tuple):
        return tuple(_freeze(item) for item in value)
    return value


def _dump_yaml(payload: dict[str, Any], *, resource_id: str) -> str:
    try:
        text = yaml.safe_dump(
            payload,
            default_flow_style=False,
            sort_keys=False,
            allow_unicode=False,
        )
    except yaml.YAMLError as err:
        raise OSError(
            f"failed to serialize YAML payload for resource '{resource_id}': {err}"
        ) from err
    if not text.endswith("\n"):
        text += "\n"
    return text


def _load_pyproject(config: Config, *, resource_id: str) -> dict[str, Any]:
    path = config.path("pyproject")
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as err:
        raise OSError(
            f"failed to read pyproject for resource '{resource_id}' at {path}: {err}"
        ) from err
    try:
        parsed = tomllib.loads(text)
    except tomllib.TOMLDecodeError as err:
        raise OSError(
            f"failed to parse pyproject TOML for resource '{resource_id}' at {path}: {err}"
        ) from err
    return _require_dict(parsed, where="pyproject")


def _load_tool_section(
    config: Config,
    section: str,
    *,
    resource_id: str,
    required: bool,
) -> dict[str, Any] | None:
    pyproject = _load_pyproject(config, resource_id=resource_id)
    tool = _require_dict(pyproject.get("tool"), where="tool")
    raw = tool.get(section)
    if raw is None:
        if required:
            raise OSError(
                f"missing required [tool.{section}] for resource '{resource_id}'"
            )
        return None
    return _require_dict(raw, where=f"tool.{section}")


@resource("pyproject", kind="file", template="core/pyproject/2026-02-15")
class PyProject(Resource):
    """A resource describing a `pyproject.toml` file, which is used to configure
    Python projects and tools, and is also used as the primary vehicle for
    configuring Bertrand itself through the `[tool.bertrand]` section.
    """
    # pylint: disable=missing-function-docstring, unused-argument, missing-return-doc

    @staticmethod
    def _require_tool(pyproject: dict[str, Any], *, resource_id: str) -> dict[str, Any]:
        tool_raw = pyproject.get("tool")
        if tool_raw is None:
            raise OSError(f"missing '[tool]' table in resource '{resource_id}'")
        return _require_dict(tool_raw, where="tool")

    @staticmethod
    def _require_choice(
        value: str,
        *,
        where: str,
        supported: dict[str, Any],
        description: str,
    ) -> str:
        if value not in supported:
            choices = ", ".join(sorted(supported))
            raise OSError(
                f"unsupported {description} at '{where}': '{value}' "
                f"(supported: {choices})"
            )
        return value

    def parse(self, config: Config) -> dict[str, Any] | None:
        resource_id = "pyproject"
        pyproject = _load_pyproject(config, resource_id=resource_id)
        tool = self._require_tool(pyproject, resource_id=resource_id)

        # validate `[tool.bertrand]`
        bertrand = _require_dict(tool.get("bertrand"), where="tool.bertrand")
        shell = self._require_choice(
            _require_str_value(bertrand.get("shell"), where="tool.bertrand.shell"),
            where="tool.bertrand.shell",
            supported=SHELLS,
            description="shell",
        )
        code = self._require_choice(
            _require_str_value(bertrand.get("code"), where="tool.bertrand.code"),
            where="tool.bertrand.code",
            supported=EDITORS,
            description="editor",
        )
        agent = self._require_choice(
            _require_str_value(bertrand.get("agent"), where="tool.bertrand.agent"),
            where="tool.bertrand.agent",
            supported=AGENTS,
            description="agent",
        )
        assist = self._require_choice(
            _require_str_value(bertrand.get("assist"), where="tool.bertrand.assist"),
            where="tool.bertrand.assist",
            supported=ASSISTS,
            description="assist",
        )
        return {
            "shell": shell,
            "code": code,
            "agent": agent,
            "assist": assist,
        }


@resource("compile_commands", kind="file", template="core/compile_commands/2026-02-15")
class CompileCommands(Resource):
    """A resource describing a `compile_commands.json` file, which is used to
    configure C++ projects and tools, and can also be used as a source of truth for
    C++ resource placement by exposing the set of source files referenced in the
    compilation database.
    """
    # pylint: disable=missing-function-docstring, unused-argument, missing-return-doc

    def sources(self, config: Config) -> list[Path] | None:
        resource_id = "compile_commands"
        path = config.path(resource_id)

        try:
            text = path.read_text(encoding="utf-8")
        except OSError as err:
            raise OSError(f"failed to read compile database at {path}: {err}") from err

        try:
            payload = json.loads(text)
        except json.JSONDecodeError as err:
            raise OSError(f"failed to parse compile database JSON at {path}: {err}") from err

        if not isinstance(payload, list):
            raise OSError(
                f"compile database at {path} must be a JSON list, got {type(payload).__name__}"
            )

        out: list[Path] = []
        seen: set[Path] = set()
        for idx, raw_entry in enumerate(payload):
            entry = _require_dict(raw_entry, where=f"compile_commands[{idx}]")
            file_raw = entry.get("file")
            directory_raw = entry.get("directory")

            file_rel = Path(_require_str_value(file_raw, where=f"compile_commands[{idx}].file"))
            if directory_raw is None:
                base = config.root
            else:
                base = Path(_require_str_value(
                    directory_raw,
                    where=f"compile_commands[{idx}].directory"
                ))
                if not base.is_absolute():
                    base = config.root / base

            source = file_rel if file_rel.is_absolute() else base / file_rel
            normalized = source.expanduser().resolve()
            if not normalized.exists() or not normalized.is_file():
                continue
            if normalized in seen:
                continue
            seen.add(normalized)
            out.append(normalized)

        return out


@resource("clang-format", kind="file")
class ClangFormat(Resource):
    """A resource describing a `.clang-format` file, which is used to configure
    clang-format for C++ code formatting.
    """
    # pylint: disable=missing-function-docstring, unused-argument, missing-return-doc

    def render(self, config: Config) -> str | None:
        section = _load_tool_section(
            config,
            "clang-format",
            resource_id="clang-format",
            required=True,
        )
        assert section is not None
        payload: dict[str, Any] = {}
        if "style" in section:
            style = _require_dict(section["style"], where="tool.clang-format.style")
            payload.update(style)
        for key, value in section.items():
            if key == "style":
                continue
            if key in payload:
                raise OSError(
                    "duplicate '.clang-format' key after style expansion: "
                    f"'{key}'"
                )
            payload[key] = value
        if not payload:
            raise OSError("empty [tool.clang-format] cannot render .clang-format")
        return _dump_yaml(payload, resource_id="clang-format")


@resource("clang-tidy", kind="file")
class ClangTidy(Resource):
    """A resource describing a `.clang-tidy` file, which is used to configure
    clang-tidy for C++ linting.
    """
    # pylint: disable=missing-function-docstring, unused-argument, missing-return-doc

    @staticmethod
    def _join_checks(value: Any, *, where: str) -> str:
        if isinstance(value, str):
            return _require_str_value(value, where=where)
        checks = _require_str_list(value, where=where)
        return ",".join(checks)

    def render(self, config: Config) -> str | None:
        section = _load_tool_section(
            config,
            "clang-tidy",
            resource_id="clang-tidy",
            required=True,
        )
        assert section is not None

        payload: dict[str, Any] = {}
        for key, value in section.items():
            if key == "checks":
                payload["Checks"] = self._join_checks(
                    value,
                    where="tool.clang-tidy.checks",
                )
            elif key == "warnings_as_errors":
                payload["WarningsAsErrors"] = self._join_checks(
                    value,
                    where="tool.clang-tidy.warnings_as_errors",
                )
            elif key == "header_filter_regex":
                payload["HeaderFilterRegex"] = _require_str_value(
                    value,
                    where="tool.clang-tidy.header_filter_regex",
                )
            elif key == "options":
                options = _require_dict(value, where="tool.clang-tidy.options")
                payload["CheckOptions"] = {
                    option_name: str(option_value)
                    for option_name, option_value in options.items()
                }
            else:
                payload[key] = value

        if not payload:
            raise OSError("empty [tool.clang-tidy] cannot render .clang-tidy")
        return _dump_yaml(payload, resource_id="clang-tidy")


@resource("clangd", kind="file")
class Clangd(Resource):
    """A resource describing a `.clangd` file, which is used to configure clangd for
    C++ language server features in editors.
    """
    # pylint: disable=missing-function-docstring, unused-argument, missing-return-doc

    def parse(self, config: Config) -> dict[str, Any] | None:
        section = _load_tool_section(
            config,
            "clangd",
            resource_id="clangd",
            required=False,
        )
        if section is None or "arguments" not in section:
            return None

        return {
            "clangd": {
                "arguments": _require_str_list(
                    section["arguments"],
                    where="tool.clangd.arguments",
                ),
            },
        }

    def render(self, config: Config) -> str | None:
        section = _load_tool_section(
            config,
            "clangd",
            resource_id="clangd",
            required=True,
        )
        assert section is not None
        payload: dict[str, Any] = {}
        arguments: list[str] | None = None

        for key, value in section.items():
            if key == "arguments":
                arguments = _require_str_list(value, where="tool.clangd.arguments")
                continue
            payload[key] = value

        if arguments is not None:
            compile_flags = payload.get("CompileFlags")
            if compile_flags is None:
                payload["CompileFlags"] = {"Add": arguments}
            else:
                compile_flags_map = _require_dict(
                    compile_flags,
                    where="tool.clangd.CompileFlags",
                )
                if "Add" in compile_flags_map:
                    raise OSError(
                        "tool.clangd cannot define both 'arguments' and 'CompileFlags.Add'"
                    )
                merged = dict(compile_flags_map)
                merged["Add"] = arguments
                payload["CompileFlags"] = merged

        if not payload:
            raise OSError("empty [tool.clangd] cannot render .clangd")
        return _dump_yaml(payload, resource_id="clangd")


@resource(VSCODE_WORKSPACE_RESOURCE_ID, kind="file")
class VSCodeWorkspace(Resource):
    """A managed VS Code workspace file used by host-side editor attach flows."""
    # pylint: disable=missing-function-docstring, unused-argument, missing-return-doc

    def render(self, config: Config) -> str | None:
        recommended_extensions: list[str] = []
        seen: set[str] = set()
        for ext in (
            *_VSCODE_BASE_RECOMMENDED_EXTENSIONS,
            *AGENTS[config["agent"]],
            *ASSISTS[config["assist"]],
        ):
            if ext in seen:
                continue
            seen.add(ext)
            recommended_extensions.append(ext)

        settings = dict(_VSCODE_MANAGED_SETTINGS)
        clangd_cfg = config["clangd"] if "clangd" in config else {}
        if isinstance(clangd_cfg, Mapping) and "arguments" in clangd_cfg:
            clangd_arguments = _require_str_list(
                clangd_cfg["arguments"],
                where="clangd.arguments",
            )
        else:
            clangd_arguments = []
        settings["clangd.arguments"] = clangd_arguments

        payload = {
            "folders": [{"path": str(MOUNT)}],
            "settings": settings,
            "extensions": {"recommendations": recommended_extensions},
            "tasks": _VSCODE_MANAGED_TASKS,
        }
        return json.dumps(payload, indent=2, sort_keys=True) + "\n"


# NOTE: "*" indicates a baseline, while other keys act as overlay diffs that merge on
# top to avoid duplication.


# Profiles define only resource placement paths: wildcard baseline + profile diffs.
PROFILES: dict[str, dict[str, PosixPath]] = {
    "*": {
        "containerfile": PosixPath("Containerfile"),
        "containerignore": PosixPath(".containerignore"),
        "docs": PosixPath("docs"),
        "tests": PosixPath("tests"),
    },
    "flat": {},
    "src": {
        "src": PosixPath("src"),
    },
}


# Capabilities define only language/tool resource placement paths: wildcard baseline
# + profile-specific diffs.
CAPABILITIES: dict[str, dict[str, dict[str, PosixPath]]] = {
    "python": {
        "*": {
            "pyproject": PosixPath("pyproject.toml"),
        },
        "flat": {},
        "src": {},
    },
    "cpp": {
        "*": {
            "compile_commands": PosixPath("compile_commands.json"),
            "clang-format": PosixPath(".clang-format"),
            "clang-tidy": PosixPath(".clang-tidy"),
            "clangd": PosixPath(".clangd"),
        },
        "flat": {},
        "src": {},
    },
    "vscode": {
        "*": {
            VSCODE_WORKSPACE_RESOURCE_ID: PosixPath(".vscode/bertrand.code-workspace"),
        },
        "flat": {},
        "src": {},
    },
}


@dataclass
class Config:
    """Read-only view representing the deserialized contents of a layout manifest,
    together with the environment root path in which to apply it.  This is the main
    entry point for layout rendering and application logic.
    """
    @dataclass(frozen=True)
    class Facts:
        """Jinja context for rendering layout resources."""
        @staticmethod
        def _page_size_kib() -> int:
            try:
                page_size = os.sysconf("SC_PAGESIZE")
                if isinstance(page_size, int) and page_size > 0:
                    return max(1, page_size // 1024)
            except (AttributeError, OSError, ValueError):
                pass
            return 4

        env: str = field()
        manifest: dict[str, Any] = field()
        paths: dict[str, str] = field()
        project_name: str = field()
        code: str = field()
        agent: str = field()
        assist: str = field()
        shell: str = field(default=DEFAULT_SHELL)
        bertrand_version: str = field(default=__version__)
        cpus: int = field(default_factory=lambda: os.cpu_count() or 1)
        page_size_kib: int = field(default_factory=_page_size_kib)
        mount_path: str = field(default=str(MOUNT))
        cache_dir: str = field(default="/tmp/.cache")

    root: Path
    manifest: Manifest
    _entered: int = field(default=0, init=False, repr=False)
    _snapshot: dict[str, Any] | None = field(default=None, init=False, repr=False)
    _snapshot_key_owner: dict[tuple[str, ...], str] = field(
        default_factory=dict,
        init=False,
        repr=False,
    )

    def __post_init__(self) -> None:
        self.root = self.root.expanduser().resolve()

    @classmethod
    def load(cls, env_root: Path) -> Self:
        """Load layout manifest from `env_root` and return a resolved `Config`.

        Parameters
        ----------
        env_root : Path
            The root path of the environment, used to locate the manifest and resolve
            resource paths.

        Returns
        -------
        Self
            A resolved `Config` instance containing the manifest and root path.

        Raises
        ------
        OSError
            If the manifest file is missing, malformed, or contains an unsupported
            schema version.
        """
        root = env_root.expanduser().resolve()
        with lock_env(root):
            data = _read_env_json(root)
            layout = data.get(ENV_LAYOUT_KEY)
            if layout is None:
                raise OSError(
                    f"missing '{ENV_LAYOUT_KEY}' in environment metadata at {_env_file(root)}"
                )
            try:
                return cls(
                    root=root,
                    manifest=Manifest.model_validate(layout)
                )
            except ValidationError as err:
                raise OSError(
                    f"invalid layout manifest in environment metadata at {_env_file(root)}: {err}"
                ) from err

    @staticmethod
    def _merge_placement_maps(
        base: dict[str, PosixPath],
        overlay: dict[str, PosixPath],
    ) -> dict[str, PosixPath]:
        merged = {
            resource_id: path
            for resource_id, path in base.items()
        }
        for resource_id, path in overlay.items():
            merged[resource_id] = path
        return merged

    @staticmethod
    def _resolve_profile(profile: str) -> dict[str, PosixPath]:
        base = PROFILES.get("*")
        if base is None:
            raise ValueError("missing wildcard baseline in PROFILES: '*'")
        overlay = PROFILES.get(profile)
        if overlay is None:
            raise ValueError(
                f"unknown layout profile: {profile} (supported: "
                f"{', '.join(sorted(profile for profile in PROFILES if profile != "*"))})"
            )
        return Config._merge_placement_maps(base, overlay)

    @staticmethod
    def _resolve_capability(capability: str, profile: str) -> dict[str, PosixPath]:
        variants = CAPABILITIES.get(capability)
        if variants is None:
            raise ValueError(
                f"unknown layout capability: {capability} (supported: "
                f"{', '.join(sorted(CAPABILITIES))})"
            )
        base = variants.get("*")
        if base is None:
            raise ValueError(
                f"layout capability '{capability}' is missing wildcard baseline '*'"
            )
        overlay = variants.get(profile, {})
        return Config._merge_placement_maps(base, overlay)

    @classmethod
    def init(
        cls,
        env_root: Path,
        *,
        profile: str,
        capabilities: list[str] | None = None
    ) -> Self:
        """Build a layout reflecting the given profile and capabilities.

        Parameters
        ----------
        env_root : Path
            The root path to the environment described by the layout.
        profile : str
            The layout profile to use, e.g. 'flat' or 'src'.  Profiles define a base
            set of resources to include in the layout.
        capabilities : list[str] | None
            An optional list of language capabilities to include, e.g. 'python' and
            'cpp'.  Capabilities define additional resource placements to include
            based on the languages used in the project.

        Returns
        -------
        Self
            A Config instance containing the environment root and generated manifest.

        Raises
        ------
        ValueError
            If the specified profile is unknown, if any specified capability is
            unknown, if wildcard baselines are missing, if any placement references
            an unknown catalog resource ID, or if there are any invalid resource
            collisions (including path collisions) when merging.
        """
        # normalize and validate profile
        profile_key = profile.strip().lower()
        supported = sorted(profile for profile in PROFILES if profile != "*")
        if profile_key not in supported:
            raise ValueError(
                f"unknown layout profile: {profile} (supported: {', '.join(supported)})"
            )

        # merge profile resource placements
        merged_paths = cls._resolve_profile(profile_key)

        # normalize and validate capabilities
        seen: set[str] = set()
        caps: list[str] = []
        if capabilities is not None:
            for raw in capabilities:
                cap = raw.strip().lower()
                if not cap:
                    raise ValueError("layout capabilities must be non-empty")
                if cap not in seen:
                    if cap not in CAPABILITIES:
                        raise ValueError(
                            f"unknown layout capability: {cap} (supported: "
                            f"{', '.join(sorted(CAPABILITIES))})"
                        )
                    seen.add(cap)
                    caps.append(cap)

        # merge resolved capability resource placements, checking for collisions
        for cap in caps:
            variant = cls._resolve_capability(cap, profile_key)
            for resource_id, path in variant.items():
                existing = merged_paths.get(resource_id)
                if existing is None:
                    merged_paths[resource_id] = path
                    continue
                if existing != path:
                    raise ValueError(
                        f"layout resource path collision for '{resource_id}' while applying "
                        f"capability '{cap}': {existing} != {path}"
                    )

        # validate cross-resource dependencies for active capabilities.
        if (
            VSCODE_WORKSPACE_RESOURCE_ID in merged_paths and
            "pyproject" not in merged_paths
        ):
            raise ValueError(
                "resource dependency error: "
                f"'{VSCODE_WORKSPACE_RESOURCE_ID}' requires 'pyproject' to be present"
            )

        # materialize manifest resources from catalog defaults
        merged_resources: dict[str, Resource.JSON] = {}
        for resource_id, path in merged_paths.items():
            r = CATALOG.get(resource_id)
            if r is None:
                raise ValueError(f"unknown layout resource ID: '{resource_id}'")
            merged_resources[resource_id] = Resource.JSON(
                kind=r.kind,
                path=path,
                template=r.template.model_copy(deep=True) if r.template is not None else None,
            )

        return cls(
            root=env_root,
            manifest=Manifest(
                schema_version=LAYOUT_SCHEMA_VERSION,
                profile=profile_key,
                capabilities=caps,
                resources=merged_resources,
            )
        )

    def _merge_snapshot_fragment(
        self,
        resource_id: str,
        fragment: dict[str, Any],
        snapshot: dict[str, Any],
        *,
        key_owner: dict[tuple[str, ...], str],
        path_prefix: tuple[str, ...] = (),
    ) -> None:
        for raw_key, value in fragment.items():
            if not isinstance(raw_key, str):  # defensive check
                if path_prefix:  # type: ignore[unreachable]
                    parent = ".".join(path_prefix)
                else:
                    parent = "<root>"
                raise OSError(
                    f"parse hook for resource '{resource_id}' returned non-string key "
                    f"under '{parent}': '{raw_key}'"
                )

            # insert value if key is new, and recurse if value is a nested dict
            key_path = path_prefix + (raw_key,)
            if raw_key not in snapshot:
                if isinstance(value, dict):
                    child: dict[str, Any] = {}
                    snapshot[raw_key] = child
                    key_owner[key_path] = resource_id
                    self._merge_snapshot_fragment(
                        resource_id,
                        value,
                        child,
                        path_prefix=key_path,
                        key_owner=key_owner,
                    )
                else:
                    snapshot[raw_key] = value
                    key_owner[key_path] = resource_id
                continue

            # if an existing key is present, and both the key and value are nested
            # dicts, then merge recursively
            existing = snapshot[raw_key]
            existing_owner = key_owner.get(key_path, "<unknown>")
            if isinstance(existing, dict) and isinstance(value, dict):
                self._merge_snapshot_fragment(
                    resource_id,
                    value,
                    existing,
                    path_prefix=key_path,
                    key_owner=key_owner,
                )
                continue

            # otherwise, this is a collision
            raise OSError(
                f"config parse key collision at '{'.'.join(key_path)}' between "
                f"resources '{existing_owner}' and '{resource_id}'"
            )

    def __enter__(self) -> Self:
        """Load a context-scoped config snapshot from parse-capable resources."""
        if self._entered == 0:
            with lock_env(self.root):
                try:
                    snapshot: dict[str, Any] = {}
                    key_owner: dict[tuple[str, ...], str] = {}
                    for resource_id in self.manifest.resources:
                        r = CATALOG.get(resource_id)
                        if r is None:
                            raise OSError(
                                "layout manifest references unknown resource ID: "
                                f"'{resource_id}'"
                            )

                        # invoke parser to extract config fragment
                        try:
                            result = r.parse(self)
                            if result is None:
                                continue
                        except Exception as err:
                            raise OSError(
                                f"failed to parse resource '{resource_id}' at "
                                f"{self.path(resource_id)}: {err}"
                            ) from err
                        if not isinstance(result, dict) or not all(
                            isinstance(k, str) for k in result
                        ):
                            raise OSError(
                                f"parse hook for resource '{resource_id}' must return a "
                                f"string mapping: {result}"
                            )

                        # merge fragment into snapshot, checking for key collisions
                        self._merge_snapshot_fragment(
                            resource_id,
                            result,
                            snapshot,
                            key_owner=key_owner,
                        )
                except Exception:
                    self._snapshot = None
                    self._snapshot_key_owner = {}
                    raise
            self._snapshot = _freeze(snapshot)
            self._snapshot_key_owner = key_owner
        self._entered += 1
        return self

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc: BaseException | None,
        traceback: TracebackType | None,
    ) -> None:
        """Release one context level and clear snapshot on outermost exit."""
        del exc_type, exc, traceback
        if self._entered == 0:
            raise RuntimeError("layout context is not active")
        self._entered -= 1
        if self._entered == 0:
            self._snapshot = None
            self._snapshot_key_owner = {}

    def __getitem__(self, key: str) -> Any:
        """Look up immutable normalized snapshot data from the active context."""
        if self._entered < 1 or self._snapshot is None:
            raise RuntimeError(
                "layout config snapshot is unavailable outside an active layout context"
            )
        if not isinstance(key, str):
            raise TypeError(f"invalid key type: {type(key)}")
        return self._snapshot[key]

    def __iter__(self) -> Iterator[str]:
        """Iterate over keys in the active context snapshot."""
        if self._entered < 1 or self._snapshot is None:
            raise RuntimeError(
                "layout config snapshot is unavailable outside an active layout context"
            )
        return iter(self._snapshot)

    def __contains__(self, key: str) -> bool:
        """Check for the presence of a key in the active context snapshot."""
        if self._entered < 1 or self._snapshot is None:
            raise RuntimeError(
                "layout config snapshot is unavailable outside an active layout context"
            )
        if not isinstance(key, str):
            raise TypeError(f"invalid key type: {type(key)}")
        return key in self._snapshot

    def __bool__(self) -> bool:
        return self._entered > 0 and self._snapshot is not None

    def resource(self, resource_id: str) -> Resource:
        """Retrieve the resource specification for the given resource ID.

        Parameters
        ----------
        resource_id : str
            The stable identifier of the resource to retrieve, as defined in `CATALOG`.

        Returns
        -------
        Resource
            The resource specification associated with the given resource ID.

        Raises
        ------
        KeyError
            If the given resource ID is not defined in the manifest.
        """
        if resource_id not in self.manifest.resources:
            raise KeyError(f"unknown resource ID: '{resource_id}'")
        return CATALOG[resource_id]

    def path(self, resource_id: str) -> Path:
        """Resolve an absolute path to the given resource within the environment root.

        Parameters
        ----------
        resource_id : str
            The stable identifier of the resource to resolve, as defined in the
            manifest.

        Returns
        -------
        Path
            An absolute path to the resource within the environment root directory.
        """
        return self.root / Path(self.manifest.resources[resource_id].path)

    # TODO: figure out a better way to pass and validate CLI arguments from __main__.py

    def _facts(self, ctx: Pipeline.InProgress) -> Config.Facts:
        """Build a Jinja context from pipeline facts, which can be used to render
        layout resources.

        Parameters
        ----------
        ctx : Pipeline.InProgress
            The current pipeline context, whose state directory holds layout
            templates and whose facts record CLI input.

        Returns
        -------
        Config.Facts
            A Facts instance containing the relevant context for layout rendering.

        Raises
        ------
        OSError
            If the environment path in pipeline facts does not match this layout's
            root path.
        """
        env = Path(_expect_str("env", ctx)).expanduser().resolve()
        if env != self.root:
            raise OSError(
                f"layout context mismatch for environment root: layout={self.root}, ctx={env}"
            )

        return Config.Facts(
            env=str(self.root),
            manifest=self.manifest.model_dump(mode="python"),
            paths={
                resource_id: str(self.path(resource_id))
                for resource_id in sorted(self.manifest.resources)
            },
            project_name=sanitize_name(self.root.name, replace="-"),
            code=_expect_str("code", ctx),
            agent=_expect_str("agent", ctx),
            assist=_expect_str("assist", ctx),
        )

    def render(self, ctx: Pipeline.InProgress) -> dict[str, str]:
        """Render templated file resources in deterministic resource-id order.

        This function renders text only.  Callers are responsible for filesystem writes.

        Parameters
        ----------
        ctx : Pipeline.InProgress
            The current pipeline context, whose state directory holds layout templates.

        Returns
        -------
        dict[str, str]
            A mapping of resource IDs to their rendered text content.

        Raises
        ------
        OSError
            If there are any errors during template loading or rendering.
        """
        # gather jinja context
        jinja = Environment(
            autoescape=False,
            undefined=StrictUndefined,
            keep_trailing_newline=True,
            trim_blocks=False,
            lstrip_blocks=False,
        )
        replacements = asdict(self._facts(ctx))

        # collect template references from templated file resources
        refs: dict[tuple[str, str, str], Template] = {}
        for resource_id in sorted(self.manifest.resources):
            r = self.resource(resource_id)
            if r.kind != "file" or r.template is None:
                continue
            ref = r.template
            refs[(ref.namespace, ref.name, ref.version)] = ref

        # hydrate any missing templates from packaged Bertrand sources
        for _, ref in sorted(refs.items()):
            target = _template_path(ctx, ref)
            if target.exists():
                if not target.is_file():
                    raise OSError(f"template cache path is not a file: {target}")
                continue  # already hydrated, skip

            # load template from packaged resources
            source = ref.packaged_path()
            if not source.is_file():
                raise FileNotFoundError(
                    "missing packaged template for layout reference "
                    f"{ref.namespace}/{ref.name}/{ref.version}: {source}"
                )
            try:
                text = source.read_text(encoding="utf-8")
            except OSError as err:
                raise OSError(
                    "failed to read packaged template for layout reference "
                    f"{ref.namespace}/{ref.name}/{ref.version} at {source}: {err}"
                ) from err

            # write template to state cache for rendering
            ctx.do(WriteText(path=target, text=text, replace=False), undo=False)

        # render templated resources with Jinja context
        out: dict[str, str] = {}
        for resource_id in sorted(self.manifest.resources):
            r = self.resource(resource_id)
            if r.kind != "file" or r.template is None:
                continue

            # load template
            path = _template_path(ctx, r.template)
            if not path.exists() or not path.is_file():
                raise FileNotFoundError(
                    f"missing template for layout resource '{resource_id}': {path}"
                )
            try:
                text = path.read_text(encoding="utf-8")
            except OSError as err:
                raise OSError(
                    f"failed to read template for layout resource '{resource_id}' at "
                    f"{path}: {err}"
                ) from err

            # render template and store output
            try:
                rendered = jinja.from_string(text).render(**replacements)
            except Exception as err:
                raise OSError(
                    f"failed to render template for layout resource '{resource_id}' at {path}: "
                    f"{err}"
                ) from err
            if not isinstance(rendered, str):
                raise OSError(
                    f"template render returned non-string for layout resource '{resource_id}' "
                    f"at {path}"
                )
            out[resource_id] = rendered

        return out

    def apply(self, ctx: Pipeline.InProgress) -> None:
        """Apply the layout to the environment directory by rendering templated file
        resources and writing missing outputs to disk.

        Parameters
        ----------
        ctx : Pipeline.InProgress
            The current pipeline context, used to drive template rendering and record
            operations.

        Raises
        ------
        OSError
            If there are any filesystem errors when writing rendered resources to disk.
        """
        with lock_env(self.root, timeout=ctx.timeout):
            rendered = self.render(ctx)

            # serialize layout to env.json
            data = _read_env_json(self.root, missing_ok=True)
            data[ENV_LAYOUT_KEY] = self.manifest.model_dump(mode="json")
            env_file = _env_file(self.root)
            try:
                ctx.do(WriteText(
                    path=env_file,
                    text=json.dumps(data, indent=2) + "\n",
                    replace=None
                ), undo=False)
            except Exception as err:
                raise OSError(
                    f"failed to serialize environment metadata for {env_file}: {err}"
                ) from err

            # create directory resources
            for resource_id in self.manifest.resources:
                r = self.manifest.resources[resource_id]
                if r.kind == "dir":
                    ctx.do(Mkdir(path=self.path(resource_id), replace=False), undo=False)

            # write missing files in deterministic order
            for resource_id, text in rendered.items():
                target = self.path(resource_id)
                if not target.exists():
                    ctx.do(WriteText(path=target, text=text, replace=False), undo=False)

    def sync(self) -> None:
        """Render and write derived artifact resources from active context snapshot.

        This requires an active layout context (`with layout:`), because render hooks
        are expected to read parsed snapshot values through `__getitem__`.

        Raises
        ------
        RuntimeError
            If called outside an active layout context.
        OSError
            If render hooks fail, return invalid output, or if any filesystem I/O
            fails during artifact synchronization.
        """
        if self._entered < 1 or self._snapshot is None:
            raise RuntimeError(
                "layout config snapshot is unavailable outside an active layout context"
            )

        with lock_env(self.root):
            for resource_id in self.manifest.resources:
                r = CATALOG.get(resource_id)
                if r is None:
                    raise OSError(
                        f"layout manifest references unknown resource ID: '{resource_id}'"
                    )

                # render artifact content and validate output
                target = self.path(resource_id)
                try:
                    text = r.render(self)
                    if text is None:
                        continue
                except Exception as err:
                    raise OSError(
                        f"failed to render sync resource '{resource_id}' at {target}: {err}"
                    ) from err
                if not isinstance(text, str):
                    raise OSError(
                        f"sync renderer returned non-string output for resource "
                        f"'{resource_id}' at {target}"
                    )

                # skip write if content is unchanged
                if target.exists():
                    if not target.is_file():
                        raise OSError(
                            f"cannot write sync output; target is not a file: {target}"
                        )
                    try:
                        current = target.read_text(encoding="utf-8")
                    except OSError as err:
                        raise OSError(
                            f"failed to read sync target for resource '{resource_id}' at "
                            f"{target}: {err}"
                        ) from err
                    if current == text:
                        continue

                # atomically write rendered content to target path
                try:
                    atomic_write_text(target, text, encoding="utf-8")
                except OSError as err:
                    raise OSError(
                        f"failed to write sync output for resource '{resource_id}' at "
                        f"{target}: {err}"
                    ) from err

    def sources(self) -> list[Path]:
        """Resolve and deduplicate source file paths from source-capable resources.

        Returns
        -------
        list[Path]
            Absolute source paths deduplicated in first-seen order.

        Raises
        ------
        OSError
            If a source hook fails, returns invalid output, or if resource kind/path
            validation fails.
        """
        out: list[Path] = []
        seen: set[Path] = set()
        with lock_env(self.root):
            for resource_id in self.manifest.resources:
                r = CATALOG.get(resource_id)
                if r is None:
                    raise OSError(
                        f"layout manifest references unknown resource ID: '{resource_id}'"
                    )

                # invoke resolver to extract source paths
                try:
                    paths = r.sources(self)
                    if paths is None:
                        continue
                except Exception as err:
                    raise OSError(
                        f"failed to resolve sources for resource '{resource_id}' at "
                        f"{self.path(resource_id)}: {err}"
                    ) from err
                if not isinstance(paths, list):
                    raise OSError(
                        f"source hook for resource '{resource_id}' returned non-list output: "
                        f"{type(paths)}"
                    )

                # normalize + deduplicate source paths, checking for validity
                for raw_path in paths:
                    if not isinstance(raw_path, Path):
                        raise OSError(
                            f"source hook for resource '{resource_id}' returned non-Path "
                            f"entry: {repr(raw_path)}"
                        )
                    normalized = raw_path.expanduser()
                    if not normalized.is_absolute():
                        normalized = (self.root / normalized).expanduser()
                    normalized = normalized.resolve()
                    if normalized in seen:
                        continue
                    seen.add(normalized)
                    out.append(normalized)

        return out

    @property
    def capabilities(self) -> tuple[str, ...]:
        """Return the list of active capabilities in this layout config.

        Returns
        -------
        tuple[str, ...]
            The list of active capabilities declared in the manifest.
        """
        return tuple(self.manifest.capabilities)
