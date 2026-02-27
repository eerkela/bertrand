"""Layout schema and init-time orchestration for Bertrand environments.

This module is intentionally scoped to a minimal, ctx-driven backend for
`bertrand init` and runtime environment loading:

1. Build deterministic resource placement maps during init.
2. Discover runtime resources from mapped candidate paths.
3. Render and write templated bootstrap resources in deterministic phases.

Canonical templates are packaged with Bertrand and lazily hydrated into
`on_init` pipeline state under `templates/...` before rendering.
"""
from __future__ import annotations

import json
import os
import shutil
import tomllib

from dataclasses import asdict, dataclass, field
from collections.abc import Mapping, Sequence
from importlib import resources as importlib_resources
from pathlib import Path, PosixPath
from types import MappingProxyType, TracebackType
from typing import Annotated, Any, Callable, Iterator, Self, TypeVar, TypedDict

from jinja2 import Environment, StrictUndefined
from pydantic import BaseModel, ConfigDict, Field, field_validator
import yaml

from .pipeline import on_init
from .run import LOCK_TIMEOUT, Lock, atomic_write_text, sanitize_name
from .version import __version__, VERSIONS


# Canonical path and name definitions for shared resources
ENV_DIR_NAME: str = ".bertrand"
ENV_LOCK_NAME: str = ".lock"
MOUNT: PosixPath = PosixPath("/env")
assert MOUNT.is_absolute()


# CLI options that affect template rendering in the `init` phase
SHELLS: dict[str, tuple[str, ...]] = {
    "bash": ("bash", "-l"),
}
DEFAULT_SHELL: str = "bash"
if DEFAULT_SHELL not in SHELLS:
    raise RuntimeError(f"default shell is unsupported: {DEFAULT_SHELL}")


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


def resource(name: str, *, template: str | None = None) -> Callable[[type[T]], type[T]]:
    """A class decorator for defining layout resources.

    Parameters
    ----------
    name : str
        The unique name of this resource, which serves as its stable identifier in the
        resource catalog.  This should generally match the `name` portion
        of a corresponding template file, if one is given.
    template : str | None, optional
        An optional reference to a Jinja template for this resource, of the form
        "namespace/name/version".  If given, the resource will be treated as a file,
        and its initial contents will be rendered from a template file stored in the
        `on_init` pipeline's state directory, under
        `templates/{namespace}/{name}/{version}.j2`.  Bertrand provides its own
        templates as part of its front-end wheel, which are copied into this location
        by default.  If no template is given (the default), then the resource will be
        treated as a directory.

    Returns
    -------
    Callable[[type[T]], type[T]]
        A class decorator that registers the decorated class as a layout resource in the
        global `CATALOG` under the given name, with the specified template.

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
            template=Template(**template_kwargs) if template_kwargs is not None else None,
        )
        return cls

    return _decorator


@resource("publish", template="core/publish/2026-02-15")
@resource("vscode-workspace", template="core/vscode-workspace/2026-02-15")
@resource("containerfile", template="core/containerfile/2026-02-15")
@resource("docs")
@resource("tests")
@resource("src")
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
    template : Template | None
        The template reference that was assigned to this resource in `@resource()`, if
        any.
    """
    # pylint: disable=unused-argument, redundant-returns-doc
    name: str
    template: Template | None

    @property
    def is_file(self) -> bool:
        """
        Returns
        -------
        bool
            True if this resource is a file (i.e. has an associated template), or False
            if it is a directory.
        """
        return self.template is not None

    @property
    def is_dir(self) -> bool:
        """
        Returns
        -------
        bool
            True if this resource is a directory (i.e. has no associated template), or
            False if it is a file.
        """
        return self.template is None

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


def _env_dir(root: Path) -> Path:
    return root.expanduser().resolve() / ENV_DIR_NAME


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


def _require_non_empty_section(
    section: dict[str, Any] | None,
    *,
    section_name: str,
    resource_id: str,
) -> dict[str, Any] | None:
    """Validate that an optional tool section is either absent or non-empty."""
    if section is None:
        return None
    if not section:
        raise OSError(
            f"empty [tool.{section_name}] cannot render resource '{resource_id}'"
        )
    return section


def _normalize_ignore_list(value: Any, *, where: str) -> tuple[str, ...]:
    out: list[str] = []
    seen: set[str] = set()
    for pattern in _require_str_list(value, where=where):
        if pattern in seen:
            continue
        seen.add(pattern)
        out.append(pattern)
    return tuple(out)


def _render_ignore_patterns(*groups: Sequence[str]) -> str:
    lines = [
        "# This file is managed by Bertrand.  Direct edits may be overwritten by",
        "# bertrand sync/build flows.",
    ]
    seen: set[str] = set()
    for patterns in groups:
        for pattern in patterns:
            if pattern in seen:
                continue
            seen.add(pattern)
            lines.append(pattern)
    return "\n".join(lines) + "\n"


@resource("pyproject", template="core/pyproject/2026-02-15")
class PyProject(Resource):
    """A resource describing a `pyproject.toml` file, which is used to configure
    Python projects and tools, and is also used as the primary vehicle for
    configuring Bertrand itself through the `[tool.bertrand]` section.
    """
    # pylint: disable=missing-function-docstring, unused-argument, missing-return-doc

    class Image(TypedDict):
        """Normalized image declaration parsed from `[tool.bertrand.images]`."""
        args: tuple[str, ...]
        containers: dict[str, tuple[str, ...]]

    def parse(self, config: Config) -> dict[str, Any] | None:
        """Parse and normalize `[tool.bertrand]` from `pyproject.toml`.

        Parameters
        ----------
        config : Config
            The active configuration context, which provides access to the
            resource's path and other shared state.

        Returns
        -------
        dict[str, Any]
            The normalized config snapshot fragment:

            {
                "version": str,
                "shell": str,
                "ignore": tuple[str, ...],
                "git_ignore": tuple[str, ...],
                "container_ignore": tuple[str, ...],
                "images": {
                    "<image_tag>": {
                        "args": tuple[str, ...],
                        "containers": {
                            "<container_tag>": tuple[str, ...],
                        },
                    },
                },
            }

        Raises
        ------
        OSError
            If the `[tool.bertrand]` section is missing or contains invalid values.
        """
        resource_id = "pyproject"
        pyproject = _load_pyproject(config, resource_id=resource_id)
        project = _require_dict(pyproject.get("project"), where="project")
        version = _require_str_value(project.get("version"), where="project.version")
        tool_raw = pyproject.get("tool")
        if tool_raw is None:
            raise OSError(f"missing '[tool]' table in resource '{resource_id}'")
        tool = _require_dict(tool_raw, where="tool")

        # validate `[tool.bertrand]`
        bertrand = _require_dict(tool.get("bertrand"), where="tool.bertrand")
        unknown_tool_keys = sorted(k for k in bertrand if k not in {
            "shell", "images", "ignore", "git_ignore", "container_ignore"
        })
        if unknown_tool_keys:
            raise OSError(
                "unsupported key(s) at 'tool.bertrand': "
                f"{', '.join(unknown_tool_keys)} "
                "(allowed: container_ignore, git_ignore, ignore, images, shell)"
            )

        shell = _require_str_value(bertrand.get("shell"), where="tool.bertrand.shell")
        if shell not in SHELLS:
            choices = ", ".join(sorted(SHELLS))
            raise OSError(
                f"unsupported shell at 'tool.bertrand.shell': '{shell}' "
                f"(supported: {choices})"
            )

        ignore = _normalize_ignore_list(
            bertrand.get("ignore", []),
            where="tool.bertrand.ignore",
        )
        git_ignore = _normalize_ignore_list(
            bertrand.get("git_ignore", []),
            where="tool.bertrand.git_ignore",
        )
        container_ignore = _normalize_ignore_list(
            bertrand.get("container_ignore", []),
            where="tool.bertrand.container_ignore",
        )

        raw_images = bertrand.get("images")
        if raw_images is None:
            image_rows: list[dict[str, Any]] = []
        elif isinstance(raw_images, (str, bytes)) or not isinstance(raw_images, Sequence):
            raise OSError(
                "expected array of tables at 'tool.bertrand.images', got "
                f"{type(raw_images).__name__}"
            )
        else:
            image_rows = [
                _require_dict(item, where=f"tool.bertrand.images[{idx}]")
                for idx, item in enumerate(raw_images)
            ]

        images: dict[str, PyProject.Image] = {
            "": {
                "args": tuple(),
                "containers": {
                    "": tuple()
                }
            }
        }
        seen_images: set[str] = set()
        for image_idx, image_row in enumerate(image_rows):
            image_where = f"tool.bertrand.images[{image_idx}]"
            unknown_image_keys = sorted(
                k for k in image_row if k not in {"tag", "args", "containers"}
            )
            if unknown_image_keys:
                raise OSError(
                    f"unsupported key(s) at '{image_where}': {', '.join(unknown_image_keys)} "
                    "(allowed: args, containers, tag)"
                )

            image_tag = _require_str_value(
                image_row.get("tag"),
                where=f"{image_where}.tag",
                allow_empty=True
            )
            sanitized_image_tag = sanitize_name(image_tag)
            if image_tag != sanitized_image_tag:
                raise OSError(
                    f"invalid tag at '{image_where}.tag': '{image_tag}' "
                    f"(sanitizes to: '{sanitized_image_tag}')"
                )
            if image_tag in seen_images:
                raise OSError(f"duplicate image tag at '{image_where}.tag': '{image_tag}'")
            seen_images.add(image_tag)
            image_args = tuple(_require_str_list(
                image_row.get("args", []),
                where=f"{image_where}.args"
            ))

            raw_containers = image_row.get("containers")
            if raw_containers is None:
                container_rows: list[dict[str, Any]] = []
            elif (
                isinstance(raw_containers, (str, bytes)) or
                not isinstance(raw_containers, Sequence)
            ):
                raise OSError(
                    f"expected array of tables at '{image_where}.containers', got "
                    f"{type(raw_containers).__name__}"
                )
            else:
                container_rows = [
                    _require_dict(item, where=f"{image_where}.containers[{idx}]")
                    for idx, item in enumerate(raw_containers)
                ]

            containers: dict[str, tuple[str, ...]] = {"": tuple()}
            seen_containers: set[str] = set()
            for container_idx, container_row in enumerate(container_rows):
                container_where = f"{image_where}.containers[{container_idx}]"
                unknown_container_keys = sorted(
                    k for k in container_row if k not in {"tag", "args"}
                )
                if unknown_container_keys:
                    raise OSError(
                        f"unsupported key(s) at '{container_where}': "
                        f"{', '.join(unknown_container_keys)} (allowed: args, tag)"
                    )

                container_tag = _require_str_value(
                    container_row.get("tag"),
                    where=f"{container_where}.tag",
                    allow_empty=True
                )
                sanitized_container_tag = sanitize_name(container_tag)
                if container_tag != sanitized_container_tag:
                    raise OSError(
                        f"invalid tag at '{container_where}.tag': '{container_tag}' "
                        f"(sanitizes to: '{sanitized_container_tag}')"
                    )
                if container_tag in seen_containers:
                    raise OSError(
                        f"duplicate container tag at '{container_where}.tag': "
                        f"'{container_tag}'"
                    )
                seen_containers.add(container_tag)
                container_args = tuple(_require_str_list(
                    container_row.get("args", []),
                    where=f"{container_where}.args"
                ))
                containers[container_tag] = container_args

            images[image_tag] = {
                "args": image_args,
                "containers": containers,
            }

        return {
            "version": version,
            "shell": shell,
            "ignore": ignore,
            "git_ignore": git_ignore,
            "container_ignore": container_ignore,
            "images": images,
        }


@resource("containerignore", template="core/containerignore/2026-02-15")
class ContainerIgnore(Resource):
    """A resource describing a `.containerignore` file, which is used to exclude files
    from the build context when compiling container images.  This is generated by the
    relevant `ignore` sections of the central project configuration.
    """
    # pylint: disable=missing-function-docstring, unused-argument, missing-return-doc

    def render(self, config: Config) -> str | None:
        shared = _normalize_ignore_list(
            config.get("ignore", ()),
            where="config.ignore",
        )
        container_only = _normalize_ignore_list(
            config.get("container_ignore", ()),
            where="config.container_ignore",
        )
        return _render_ignore_patterns(shared, container_only)


@resource("gitignore", template="core/gitignore/2026-02-15")
class GitIgnore(Resource):
    """A resource describing a `.gitignore` file, which is used to exclude files from
    the repository context during version control.  This is generated by the relevant
    `ignore` sections of the central project configuration.
    """
    # pylint: disable=missing-function-docstring, unused-argument, missing-return-doc

    def render(self, config: Config) -> str | None:
        shared = _normalize_ignore_list(
            config.get("ignore", ()),
            where="config.ignore",
        )
        git_only = _normalize_ignore_list(
            config.get("git_ignore", ()),
            where="config.git_ignore",
        )
        return _render_ignore_patterns(shared, git_only)


@resource("compile_commands", template="core/compile_commands/2026-02-15")
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


@resource("clang-format", template="core/clang-format/2026-02-15")
class ClangFormat(Resource):
    """A resource describing a `.clang-format` file, which is used to configure
    clang-format for C++ code formatting.  The `[tool.clang-format]` table is
    projected directly to YAML with no key remapping.
    """
    # pylint: disable=missing-function-docstring, unused-argument, missing-return-doc

    def render(self, config: Config) -> str | None:
        section = _require_non_empty_section(_load_tool_section(
            config,
            "clang-format",
            resource_id="clang-format",
            required=False,
        ), section_name="clang-format", resource_id="clang-format")
        if section is None:
            return None
        return _dump_yaml(section, resource_id="clang-format")


@resource("clang-tidy", template="core/clang-tidy/2026-02-15")
class ClangTidy(Resource):
    """A resource describing a `.clang-tidy` file, which is used to configure
    clang-tidy for C++ linting.  This expects native clang-tidy key names in TOML.
    `Checks` and `WarningsAsErrors` may be specified as arrays for convenience and
    will be joined to comma-separated strings.
    """
    # pylint: disable=missing-function-docstring, unused-argument, missing-return-doc

    @staticmethod
    def _join_checks(value: Any, *, where: str) -> str:
        if isinstance(value, str):
            return _require_str_value(value, where=where)
        checks = _require_str_list(value, where=where)
        return ",".join(checks)

    def render(self, config: Config) -> str | None:
        section = _require_non_empty_section(_load_tool_section(
            config,
            "clang-tidy",
            resource_id="clang-tidy",
            required=False,
        ), section_name="clang-tidy", resource_id="clang-tidy")
        if section is None:
            return None

        payload: dict[str, Any] = {}
        for key, value in section.items():
            if key == "Checks":
                payload["Checks"] = self._join_checks(
                    value,
                    where="tool.clang-tidy.Checks",
                )
            elif key == "WarningsAsErrors":
                payload["WarningsAsErrors"] = self._join_checks(
                    value,
                    where="tool.clang-tidy.WarningsAsErrors",
                )
            else:
                payload[key] = value

        return _dump_yaml(payload, resource_id="clang-tidy")


@resource("clangd", template="core/clangd/2026-02-15")
class Clangd(Resource):
    """A resource describing a `.clangd` file, which is used to configure clangd for
    C++ language server features in editors.  This expects native clangd keys in
    `[tool.clangd]`; legacy `arguments` aliasing is intentionally unsupported.
    """
    # pylint: disable=missing-function-docstring, unused-argument, missing-return-doc

    def render(self, config: Config) -> str | None:
        section = _require_non_empty_section(_load_tool_section(
            config,
            "clangd",
            resource_id="clangd",
            required=False,
        ), section_name="clangd", resource_id="clangd")
        if section is None:
            return None
        if "arguments" in section:
            raise OSError(
                "unsupported key [tool.clangd].arguments. "
                "Use native clangd keys (for example, CompileFlags.Add) if needed."
            )
        return _dump_yaml(section, resource_id="clangd")


# NOTE: "*" indicates a baseline, while other keys act as overlay diffs that merge on
# top to avoid duplication.


# Profiles define only resource placement paths: wildcard baseline + profile diffs.
PROFILES: dict[str, dict[str, PosixPath]] = {
    "*": {
        "publish": PosixPath(".github") / "workflows" / "publish.yml",
        "gitignore": PosixPath(".gitignore"),
        "containerignore": PosixPath(".containerignore"),
        "containerfile": PosixPath("Containerfile"),
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
            "vscode-workspace": PosixPath(".vscode/bertrand.code-workspace"),
        },
        "flat": {},
        "src": {},
    },
}


@dataclass
class Config:
    """Read-only view representing resource placements within an environment root,
    as well as normalized config data parsed from resources that implement a `parse()`
    method, without coupling to any particular schema.
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

        @staticmethod
        def _python_version() -> str:
            version = VERSIONS.python
            if not isinstance(version, str) or not version:
                raise OSError(
                    "missing PYTHON_VERSION in canonical Containerfile; cannot render "
                    "python_version template fact"
                )
            return version

        # TODO: rather than baking things like page size into the template context,
        # I should just auto-detect it when writing the CMakeLists.txt file in the
        # pep517 backend, and then eliminate it from the base image's qualified name
        # in the templated Containerfile.

        env: str = field()
        paths: dict[str, str] = field()
        project_name: str = field()
        shell: str = field(default=DEFAULT_SHELL)
        bertrand_version: str = field(default=__version__)
        python_version: str = field(default_factory=_python_version)
        cpus: int = field(default_factory=lambda: os.cpu_count() or 1)
        page_size_kib: int = field(default_factory=_page_size_kib)
        mount_path: str = field(default=str(MOUNT))
        cache_dir: str = field(default="/tmp/.cache")

    root: Path
    resources: dict[str, PosixPath]
    _entered: int = field(default=0, init=False, repr=False)
    _snapshot: Any | None = field(default=None, init=False, repr=False)
    _snapshot_key_owner: dict[tuple[str, ...], str] = field(
        default_factory=dict,
        init=False,
        repr=False,
    )

    def __post_init__(self) -> None:
        self.root = self.root.expanduser().resolve()
        for r_id, path in self.resources.items():
            if r_id not in CATALOG:
                raise ValueError(f"unknown resource id in config: '{r_id}'")
            self._check_relative_path(path, where=f"resource '{r_id}'")

    @staticmethod
    def _check_relative_path(path: PosixPath, *, where: str) -> None:
        if path.is_absolute():
            raise OSError(f"mapped resource path must be relative at '{where}': {path}")
        if path == PosixPath("."):
            raise OSError(f"mapped resource path must not be empty at '{where}'")
        if any(part == ".." for part in path.parts):
            raise OSError(
                f"mapped resource path must not traverse parents at '{where}': {path}"
            )

    @classmethod
    def load(cls, env_root: Path) -> Self:
        """Load layout by scanning the environment root for known resource placements
        based on the `PROFILES` and `CAPABILITIES` maps.

        Parameters
        ----------
        env_root : Path
            The root path of the environment directory.

        Returns
        -------
        Self
            A resolved `Config` instance containing the discovered resources.

        Raises
        ------
        OSError
            If any resource placements reference unknown resource IDs, or if there are
            any path collisions between resources in the environment (either from
            multiple resources mapping to the same path, or from a single resource
            mapping to multiple paths).
        """
        env_root = env_root.expanduser().resolve()
        with lock_env(env_root):
            # build a candidate map of resource locations based on all known placements
            # across the current profiles and capabilities, so that we don't need to
            # do a full filesystem walk
            candidates: list[tuple[str, str, PosixPath]] = [
                (f"PROFILES['{profile}']['{r_id}']", r_id, path)
                for profile, placements in PROFILES.items()
                for r_id, path in placements.items()
            ]
            candidates.extend(
                (f"CAPABILITIES['{capability}']['{profile}']['{r_id}']", r_id, path)
                for capability, variants in CAPABILITIES.items()
                for profile, placements in variants.items()
                for r_id, path in placements.items()
            )
            seen: dict[PosixPath, str] = {}
            for where, r_id, path in candidates:
                if r_id not in CATALOG:
                    raise OSError(
                        f"unknown resource id in mapped placement '{where}': {r_id}"
                    )
                cls._check_relative_path(path, where=where)
                observed_id = seen.setdefault(path, r_id)
                if observed_id != r_id:
                    raise OSError(
                        f"resource path collision at '{where}': '{r_id}' and "
                        f"'{observed_id}' both map to '{path}'"
                    )

            # search the candidate locations to discover the actual resources present
            # in the environment
            discovered: dict[str, PosixPath] = {}
            for path, r_id in seen.items():
                r = CATALOG[r_id]
                target = env_root / path
                if target.exists() and (
                    (r.is_file and target.is_file()) or
                    (r.is_dir and target.is_dir())
                ):
                    observed_path = discovered.setdefault(r_id, path)
                    if observed_path != path:
                        raise OSError(
                            f"ambiguous mapped resource '{r_id}' in environment at "
                            f"{env_root}: '{observed_path}' and '{path}'"
                        )

            # return as a resolved Config instance with normalized paths
            return cls(root=env_root, resources=discovered)

    @classmethod
    def init(
        cls,
        env_root: Path,
        *,
        profile: str | None,
        capabilities: list[str] | None = None
    ) -> Self:
        """Build a layout reflecting the given profile and capabilities.

        Parameters
        ----------
        env_root : Path
            The root path to the environment described by the layout.
        profile : str | None, optional
            The layout profile to use, e.g. 'flat' or 'src'.  Profiles define a base
            set of resources to include in the layout.  If None (the default), then the
            resource profile will be inferred from the existing environment where
            possible, and will error otherwise.
        capabilities : list[str] | None, optional
            An optional list of language capabilities to include, e.g. 'python' and
            'cpp'.  Capabilities define additional resource placements to include
            based on the languages used in the project.

        Returns
        -------
        Self
            A Config instance containing the environment root and generated resources.

        Raises
        ------
        ValueError
            If the specified profile is unknown, if any specified capability is
            unknown, if wildcard baselines are missing, if any placement references
            an unknown catalog resource ID, or if there are any invalid resource
            collisions (including path collisions) when merging.
        """
        # lock the environment during layout generation
        with lock_env(env_root):
            # load any existing resources from the environment
            result = cls.load(env_root)

            # normalize the requested profile, inferring from the loaded layout if
            # necessary
            base_profile = PROFILES.get("*")
            if base_profile is None:
                raise ValueError("missing wildcard baseline in PROFILES: '*'")
            if profile is None:
                # choose the profile with the most matching placements, to prefer src
                # layouts over flat where both would be valid
                n = 0
                for candidate_profile, placements in PROFILES.items():
                    if candidate_profile == "*":
                        continue
                    merged_profile = base_profile.copy()
                    merged_profile.update(placements)
                    if len(merged_profile) > n and all(
                        result.resources.get(r_id) == path
                        for r_id, path in merged_profile.items()
                    ):
                        profile = candidate_profile
                        n = len(merged_profile)

                # if we couldn't infer a profile, then we hard error rather than clobbering
                # an existing environment
                if profile is None:
                    raise ValueError(
                        "unable to infer layout profile from environment, please specify "
                        "explicitly (supported: "
                        f"{', '.join(sorted(p for p in PROFILES if p != '*'))})"
                    )
            else:
                profile = profile.strip()
                if not profile:
                    raise ValueError("layout profile cannot be empty")
                if profile == "*":
                    raise ValueError("layout profile cannot be wildcard '*'")

                # merge the selected profile diff on top of the base placements
                overlay_profile = PROFILES.get(profile)
                if overlay_profile is None:
                    raise ValueError(
                        f"unknown layout profile: {profile} (supported: "
                        f"{', '.join(sorted(p for p in PROFILES if p != '*'))})"
                    )
                merged_profile = base_profile.copy()
                merged_profile.update(overlay_profile)

                # update the result with placements from the merged profile, checking
                # for collisions
                for r_id, path in merged_profile.items():
                    existing = result.resources.setdefault(r_id, path)
                    if existing != path:
                        raise ValueError(
                            f"layout resource path collision for '{r_id}' while applying "
                            f"profile '{profile}': {existing} != {path}"
                        )

            # merge capability resource placements, checking for collisions
            if capabilities:
                seen: set[str] = set()
                for raw in capabilities:
                    # normalize capability and skip duplicates
                    cap = raw.strip()
                    if cap in seen:
                        continue
                    if not cap:
                        raise ValueError("layout capability cannot be empty")
                    if cap == "*":
                        raise ValueError("layout capability cannot be wildcard '*'")

                    # start with the wildcard baseline and merge the profile-specific
                    # diff on top; if a variant does not specify a diff, treat it as an
                    # empty overlay rather than an error
                    variants = CAPABILITIES.get(cap)
                    if variants is None:
                        raise ValueError(
                            f"unknown layout capability: {cap} (supported: "
                            f"{', '.join(sorted(CAPABILITIES))})"
                        )
                    base_cap = variants.get("*")
                    if base_cap is None:
                        raise ValueError(
                            f"layout capability '{cap}' is missing wildcard baseline '*'"
                        )
                    overlay_cap = variants.get(profile, {})
                    merged_caps = base_cap.copy()
                    merged_caps.update(overlay_cap)

                    # check for collisions during merge
                    for r_id, path in merged_caps.items():
                        existing = result.resources.setdefault(r_id, path)
                        if existing != path:
                            raise ValueError(
                                f"layout resource path collision for '{r_id}' while "
                                f"applying capability '{cap}': {existing} != {path}"
                            )
                    seen.add(cap)

            return result

    def apply(self, *, timeout: float = LOCK_TIMEOUT) -> None:
        """Apply the layout to the environment directory by rendering templated file
        resources and writing missing outputs to disk.

        Parameters
        ----------
        timeout : float, optional
            The maximum time to wait for acquiring the environment lock, by default
            `LOCK_TIMEOUT`.

        Raises
        ------
        OSError
            If there are any filesystem errors when writing rendered resources to disk.
        """
        # gather jinja context
        templates = on_init.state_dir / "templates"
        base_templates = importlib_resources.files("bertrand.env").joinpath("templates")
        jinja = Environment(
            autoescape=False,
            undefined=StrictUndefined,
            keep_trailing_newline=True,
            trim_blocks=False,
            lstrip_blocks=False,
        )
        replacements = asdict(Config.Facts(
            env=str(self.root),
            paths={r_id: str(self.path(r_id)) for r_id in sorted(self.resources)},
            project_name=sanitize_name(self.root.name, replace="-"),
        ))

        # lock the environment during application
        with lock_env(self.root, timeout=timeout):
            for r_id in sorted(self.resources):
                path = self.path(r_id)
                r = self.resource(r_id)
                if path.exists():
                    if r.is_file and not path.is_file():
                        raise OSError(
                            f"cannot apply layout resource '{r_id}' to {path}: "
                            "target exists and is not a file"
                        )
                    if r.is_dir and not path.is_dir():
                        raise OSError(
                            f"cannot apply layout resource '{r_id}' to {path}: "
                            "target exists and is not a directory"
                        )
                    continue

                # directories are trivially created
                if r.is_dir:
                    path.mkdir(parents=True, exist_ok=True)
                    continue

                # locate file template and copy it into the template directory if it's
                # not already present
                if r.template is None:
                    raise OSError(f"no template specified for file resource '{r_id}'")
                template = (
                    templates /
                    r.template.namespace /
                    r.template.name /
                    f"{r.template.version}.j2"
                )
                if not template.exists():
                    with importlib_resources.as_file(base_templates.joinpath(
                        r.template.namespace,
                        r.template.name,
                        f"{r.template.version}.j2",
                    )) as source:
                        if not source.exists():
                            raise FileNotFoundError(
                                "missing Bertrand template for layout resource "
                                f"'{r_id}' reference {r.template.namespace}/"
                                f"{r.template.name}/{r.template.version}: {source}"
                            )
                        if not source.is_file():
                            raise FileNotFoundError(
                                "missing Bertrand template for layout resource "
                                f"'{r_id}' reference {r.template.namespace}/"
                                f"{r.template.name}/{r.template.version}: {source}"
                            )
                        template.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copy(source, template)

                # render template to disk
                try:
                    path.parent.mkdir(parents=True, exist_ok=True)
                    text = template.read_text(encoding="utf-8")
                    path.write_text(
                        jinja.from_string(text).render(**replacements),
                        encoding="utf-8"
                    )
                except OSError as err:
                    raise OSError(
                        f"failed to render template for layout resource '{r_id}' at "
                        f"{path}: {err}"
                    ) from err

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
                    for resource_id in sorted(self.resources):
                        r = CATALOG.get(resource_id)
                        if r is None:
                            raise OSError(
                                f"config references unknown resource ID: '{resource_id}'"
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
        if self._entered <= 0:
            raise RuntimeError("layout context is not active")
        self._entered -= 1
        if self._entered == 0:
            self._snapshot = None
            self._snapshot_key_owner = {}

    def __getitem__(self, key: str) -> Any:
        if self._entered < 1 or self._snapshot is None:
            raise RuntimeError(
                "layout config snapshot is unavailable outside an active layout context"
            )
        if not isinstance(key, str):
            raise TypeError(f"invalid key type: {type(key)}")
        return self._snapshot[key]

    def __iter__(self) -> Iterator[str]:
        if self._entered < 1 or self._snapshot is None:
            raise RuntimeError(
                "layout config snapshot is unavailable outside an active layout context"
            )
        return iter(self._snapshot)

    def __contains__(self, key: str) -> bool:
        if self._entered < 1 or self._snapshot is None:
            raise RuntimeError(
                "layout config snapshot is unavailable outside an active layout context"
            )
        if not isinstance(key, str):
            raise TypeError(f"invalid key type: {type(key)}")
        return key in self._snapshot

    def __bool__(self) -> bool:
        return self._entered > 0 and self._snapshot is not None

    def get(self, key: str, default: Any = None) -> Any:
        """Look up a key in the active context snapshot, returning a default value if
        the key is not present.

        Parameters
        ----------
        key : str
            The key to look up in the snapshot.
        default : Any, optional
            The value to return if the key is not present, by default None.

        Returns
        -------
        Any
            The value associated with the key in the snapshot, or the default value if
            the key is not present.

        Raises
        ------
        RuntimeError
            If there is no active layout context or if the snapshot is unavailable.
        TypeError
            If the key is not a string.
        """
        if self._entered < 1 or self._snapshot is None:
            raise RuntimeError(
                "layout config snapshot is unavailable outside an active layout context"
            )
        if not isinstance(key, str):
            raise TypeError(f"invalid key type: {type(key)}")
        return self._snapshot.get(key, default)

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
            If the given resource ID is not detected in the environment.
        """
        if resource_id not in self.resources:
            raise KeyError(f"unknown resource ID: '{resource_id}'")
        return CATALOG[resource_id]

    def path(self, resource_id: str) -> Path:
        """Resolve an absolute path to the given resource within the environment root.

        Parameters
        ----------
        resource_id : str
            The stable identifier of the resource to resolve, as in `CATALOG`.

        Returns
        -------
        Path
            An absolute path to the resource within the environment root directory.

        Raises
        ------
        KeyError
            If the given resource ID is not detected in the environment.
        """
        if resource_id not in self.resources:
            raise KeyError(f"unknown resource ID: '{resource_id}'")
        return self.root / Path(self.resources[resource_id])

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
            for resource_id in sorted(self.resources):
                r = CATALOG.get(resource_id)
                if r is None:
                    raise OSError(f"config references unknown resource ID: '{resource_id}'")
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
            for resource_id in sorted(self.resources):
                r = CATALOG.get(resource_id)
                if r is None:
                    raise OSError(f"config references unknown resource ID: '{resource_id}'")
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
