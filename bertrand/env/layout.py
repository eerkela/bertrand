"""Layout schema and init-time orchestration for Bertrand environments.

This module is intentionally scoped to a minimal, ctx-driven backend for
`bertrand init`:

1. Build a deterministic layout manifest.
2. Persist it in `.bertrand/env.json` under top-level `layout`.
3. Render and write managed bootstrap resources in deterministic phases.

Canonical templates are packaged with Bertrand and lazily hydrated into
`on_init` pipeline state under `templates/...` before rendering.
"""
from __future__ import annotations

import json
import os

from dataclasses import asdict, dataclass, field
from importlib import resources
from importlib.resources.abc import Traversable
from pathlib import Path, PosixPath
from typing import Any, Literal, Self

from jinja2 import Environment, StrictUndefined
from pydantic import (
    BaseModel,
    ConfigDict,
    Field,
    ValidationError,
    field_validator,
    model_validator,
)

from .config import DEFAULT_SHELL, MOUNT
from .pipeline import Mkdir, Pipeline, WriteText
from .run import sanitize_name
from .version import __version__


LAYOUT_SCHEMA_VERSION: int = 1
ENV_DIR_NAME: str = ".bertrand"
ENV_FILE_NAME: str = "env.json"
ENV_LAYOUT_KEY: str = "layout"


class TemplateRef(BaseModel):
    """Stable template reference used by layout resources.

    Canonical templates are packaged with Bertrand under `env/templates` and addressed
    by stable `{namespace}/{name}/{version}` references.  They are lazily hydrated into
    the `on_init` state cache before rendering.
    """
    model_config = ConfigDict(extra="forbid")
    namespace: str = Field(description="Template namespace, e.g. 'core'.")
    name: str = Field(description="Template resource name, e.g. 'pyproject'.")
    version: str = Field(
        description=
            "Stable template version identifier, e.g. '2026-02-15'.  No specific "
            "format is required, but a date-based convention is recommended for "
            "clarity and collision avoidance."
    )

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


class Resource(BaseModel):
    """A single file or directory entry in a layout manifest, which may be created
    in the environment root when the layout is applied.
    """
    model_config = ConfigDict(extra="forbid")
    kind: Literal["file", "dir"] = Field(
        description="The type of resource, either 'file' or 'dir'."
    )
    path: PosixPath = Field(
        description=
            "The relative path of the resource starting from the environment root.  "
            "Always stored as a POSIX path."
    )
    managed: bool = Field(
        default=True,
        description=
            "Whether this resource is managed by the layout system.  Managed resources "
            "will be rendered during layout application, and must have a matching "
            "template if they are files.  Unmanaged resources are not automatically "
            "created or modified by the layout system.",
    )
    required: bool = Field(
        default=True,
        description=
            "Whether this resource is required to be present in the environment.  If "
            "True, then the layout application process will raise an error if the "
            "resource is missing or has the wrong type after applying the layout.",
    )
    template: TemplateRef | None = Field(
        default=None,
        description=
            "An optional reference to a template used to render the contents of a "
            "managed file resource.  Must be None for non-file resources.  Managed "
            "file resources must define one.",
    )

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
            raise ValueError("non-file layout resources must not define a template reference")
        if self.managed and self.kind == "file" and self.template is None:
            raise ValueError("managed file resources must define a template reference")
        return self


class Manifest(BaseModel):
    """Serializable resource manifest persisted in environment metadata.

    A manifest of this form is stored in `env.json` under the top-level `layout` key,
    and can be loaded to reconstruct the layout after initialization.
    """
    model_config = ConfigDict(extra="forbid")
    schema_version: int = Field(
        default=LAYOUT_SCHEMA_VERSION,
        gt=0,
        description="Version number, for forward compatibility."
    )
    profile: str = Field(
        description=
            "The layout profile used to generate this manifest, e.g. 'flat' or 'src'."
    )
    capabilities: list[str] = Field(
        default_factory=list,
        description=
            "List of language capabilities included in this layout, e.g. 'python' "
            "and 'cpp'.  This field is reserved for future use with other languages."
    )
    resources: dict[str, Resource] = Field(
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
        by_parts: dict[tuple[str, ...], tuple[str, Resource]] = {}
        for resource_id in self.resources:
            resource = self.resources[resource_id]
            parts = resource.path.parts
            existing = by_parts.get(parts)
            if existing is not None:
                existing_id, _ = existing
                raise ValueError(
                    f"layout path collision between resource IDs '{existing_id}' and "
                    f"'{resource_id}' at '{resource.path}'"
                )
            by_parts[parts] = (resource_id, resource)

        # validate no file ancestors in paths
        for resource_id in self.resources:
            resource = self.resources[resource_id]
            parts = resource.path.parts
            for depth in range(1, len(parts)):
                parent_parts = parts[:depth]
                parent = by_parts.get(parent_parts)
                if parent is None:
                    continue
                parent_id, parent_resource = parent
                if parent_resource.kind == "file":
                    parent_path = PosixPath(*parent_parts)
                    raise ValueError(
                        f"layout resource '{resource_id}' at '{resource.path}' cannot be nested "
                        f"under file resource '{parent_id}' at '{parent_path}'"
                    )
        return self


def _env_dir(root: Path) -> Path:
    return root.expanduser().resolve() / ENV_DIR_NAME


def _env_file(root: Path) -> Path:
    return _env_dir(root) / ENV_FILE_NAME


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


def _template_path(ctx: Pipeline.InProgress, ref: TemplateRef) -> Path:
    return ctx.state_dir / "templates" / ref.namespace / ref.name / f"{ref.version}.j2"


LAYOUT_PROFILES: dict[str, dict[str, Resource]] = {
    "flat": {
        "pyproject": Resource(
            kind="file",
            path=PosixPath("pyproject.toml"),
            template=TemplateRef(
                namespace="core",
                name="pyproject",
                version="2026-02-15"
            ),
        ),
        "containerfile": Resource(
            kind="file",
            path=PosixPath("Containerfile"),
            template=TemplateRef(
                namespace="core",
                name="containerfile",
                version="2026-02-15"
            ),
        ),
        "containerignore": Resource(
            kind="file",
            path=PosixPath(".containerignore"),
            template=TemplateRef(
                namespace="core",
                name="containerignore",
                version="2026-02-15"
            ),
        ),
        "docs": Resource(
            kind="dir",
            path=PosixPath("docs"),
            managed=False,
            required=False,
            template=None,
        ),
        "tests": Resource(
            kind="dir",
            path=PosixPath("tests"),
            managed=False,
            required=False,
            template=None,
        ),
    },
    "src": {
        "pyproject": Resource(
            kind="file",
            path=PosixPath("pyproject.toml"),
            template=TemplateRef(
                namespace="core",
                name="pyproject",
                version="2026-02-15"
            ),
        ),
        "containerfile": Resource(
            kind="file",
            path=PosixPath("Containerfile"),
            template=TemplateRef(
                namespace="core",
                name="containerfile",
                version="2026-02-15"
            ),
        ),
        "containerignore": Resource(
            kind="file",
            path=PosixPath(".containerignore"),
            template=TemplateRef(
                namespace="core",
                name="containerignore",
                version="2026-02-15"
            ),
        ),
        "src": Resource(
            kind="dir",
            path=PosixPath("src"),
            managed=False,
            required=False,
            template=None,
        ),
        "docs": Resource(
            kind="dir",
            path=PosixPath("docs"),
            managed=False,
            required=False,
            template=None,
        ),
        "tests": Resource(
            kind="dir",
            path=PosixPath("tests"),
            managed=False,
            required=False,
            template=None,
        ),
    },
}


LAYOUT_CAPABILITIES: dict[str, dict[str, Resource]] = {
    "python": {
        # NOTE: configuration centralized in pyproject.toml
    },
    "cpp": {
        # NOTE: these are generated from pyproject at runtime, not init-time templates
        "clang_format": Resource(
            kind="file",
            path=PosixPath(".clang-format"),
            managed=False,
            required=False,
            template=None,
        ),
        "clang_tidy": Resource(
            kind="file",
            path=PosixPath(".clang-tidy"),
            managed=False,
            required=False,
            template=None,
        ),
        "clangd": Resource(
            kind="file",
            path=PosixPath(".clangd"),
            managed=False,
            required=False,
            template=None,
        ),
    },
}


@dataclass
class Layout:
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

    def __post_init__(self) -> None:
        self.root = self.root.expanduser().resolve()

    @classmethod
    def load(cls, env_root: Path) -> Self:
        """Load layout manifest from `env_root` and return a resolved Layout.

        Parameters
        ----------
        env_root : Path
            The root path of the environment, used to locate the manifest and resolve
            resource paths.

        Returns
        -------
        Self
            A resolved Layout instance containing the manifest and root path.

        Raises
        ------
        OSError
            If the manifest file is missing, malformed, or contains an unsupported
            schema version.
        """
        data = _read_env_json(env_root)
        layout = data.get(ENV_LAYOUT_KEY)
        if layout is None:
            raise OSError(
                f"missing '{ENV_LAYOUT_KEY}' in environment metadata at {_env_file(env_root)}"
            )
        try:
            return cls(
                root=env_root,
                manifest=Manifest.model_validate(layout)
            )
        except ValidationError as err:
            raise OSError(
                f"invalid layout manifest in environment metadata at {_env_file(env_root)}: {err}"
            ) from err

    @classmethod
    def init(
        cls,
        env_root: Path,
        *,
        profile: str,
        capabilities: list[str] | None = None
    ) -> Self:
        """Build a layout reflecting the given profile and capabilities, using the
        definitions in `LAYOUT_PROFILES` and `LAYOUT_CAPABILITIES`.

        Parameters
        ----------
        env_root : Path
            The root path to the environment described by the layout.
        profile : str
            The layout profile to use, e.g. 'flat' or 'src'.  Profiles define a base
            set of resources to include in the layout.
        capabilities : list[str] | None
            An optional list of language capabilities to include, e.g. 'python' and
            'cpp'.  Capabilities define additional resources to include based on the
            languages used in the project.

        Returns
        -------
        Self
            A Layout instance containing the environment root and generated manifest.

        Raises
        ------
        ValueError
            If the specified profile is unknown, if any specified capability is
            unknown, or if there are any invalid resource collisions (including path
            collisions) when merging the profile and capabilities.
        """
        # merge profile resources
        profile_key = profile.strip().lower()
        profile_resources = LAYOUT_PROFILES.get(profile_key)
        if profile_resources is None:
            raise ValueError(
                f"unknown layout profile: {profile} (supported: "
                f"{', '.join(sorted(LAYOUT_PROFILES))})"
            )
        merged = {
            resource_id: resource.model_copy(deep=True)
            for resource_id, resource in profile_resources.items()
        }

        # normalize and validate capabilities
        seen: set[str] = set()
        caps: list[str] = []
        if capabilities is not None:
            for raw in capabilities:
                cap = raw.strip().lower()
                if not cap:
                    raise ValueError("layout capabilities must be non-empty")
                if cap not in seen:
                    if cap not in LAYOUT_CAPABILITIES:
                        raise ValueError(
                            f"unknown layout capability: {cap} (supported: "
                            f"{', '.join(sorted(LAYOUT_CAPABILITIES))})"
                        )
                    seen.add(cap)
                    caps.append(cap)

        # merge capability resources, checking for collisions
        for cap in caps:
            for resource_id, resource in LAYOUT_CAPABILITIES[cap].items():
                existing = merged.get(resource_id)
                if existing is None:
                    merged[resource_id] = resource.model_copy(deep=True)
                    continue
                if existing.model_dump(mode="python") != resource.model_dump(mode="python"):
                    raise ValueError(
                        f"layout resource collision for '{resource_id}' while applying "
                        f"capability '{cap}'"
                    )

        return cls(
            root=env_root,
            manifest=Manifest(
                schema_version=LAYOUT_SCHEMA_VERSION,
                profile=profile_key,
                capabilities=caps,
                resources=merged,
            )
        )

    def resource(self, resource_id: str) -> Resource:
        """Retrieve the resource specification for the given resource ID.

        Parameters
        ----------
        resource_id : str
            The stable identifier of the resource to retrieve, as defined in the
            manifest.

        Returns
        -------
        Resource
            The resource specification associated with the given resource ID.

        Raises
        ------
        KeyError
            If the given resource ID is not defined in the manifest.
        """
        return self.manifest.resources[resource_id]

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
        return self.root / Path(self.resource(resource_id).path)

    def _facts(self, ctx: Pipeline.InProgress) -> Layout.Facts:
        """Build a Jinja context from pipeline facts, which can be used to render
        layout resources.

        Parameters
        ----------
        ctx : Pipeline.InProgress
            The current pipeline context, whose state directory holds layout
            templates and whose facts record CLI input.

        Returns
        -------
        Layout.Facts
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

        return Layout.Facts(
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
        """Render managed file resources in deterministic resource-id order.

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

        # collect template references from managed file resources
        refs: dict[tuple[str, str, str], TemplateRef] = {}
        for resource_id in sorted(self.manifest.resources):
            resource = self.resource(resource_id)
            if resource.kind != "file" or not resource.managed or resource.template is None:
                continue
            ref = resource.template
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

        # render managed resources with Jinja context
        out: dict[str, str] = {}
        for resource_id in sorted(self.manifest.resources):
            resource = self.resource(resource_id)
            if resource.kind != "file" or not resource.managed or resource.template is None:
                continue

            # load template
            path = _template_path(ctx, resource.template)
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
        """Apply the layout to the environment directory by rendering managed resources
        and writing them to disk.  Unmanaged resources are validated only through
        `required` checks.

        Parameters
        ----------
        ctx : Pipeline.InProgress
            The current pipeline context, used to drive template rendering and record
            operations.

        Raises
        ------
        OSError
            If there are any filesystem errors when writing rendered resources to disk,
            or if any required resources are missing or have an invalid type after
            applying the layout.
        """
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
            resource = self.manifest.resources[resource_id]
            if resource.kind == "dir" and resource.managed:
                ctx.do(Mkdir(path=self.path(resource_id), replace=False), undo=False)

        # write missing files in deterministic order
        for resource_id, text in rendered.items():
            target = self.path(resource_id)
            if not target.exists():
                ctx.do(WriteText(path=target, text=text, replace=False), undo=False)

        # ensure required resources exist with the expected kind
        errors: list[str] = []
        for resource_id in sorted(self.manifest.resources):
            resource = self.resource(resource_id)
            if not resource.required:
                continue

            # verify required resource exists
            target = self.path(resource_id)
            if not target.exists():
                errors.append(
                    f"- missing required {resource.kind} '{resource_id}' at {target}"
                )
                continue

            # verify required resource has expected type
            if resource.kind == "file" and not target.is_file():
                errors.append(
                    f"- required file '{resource_id}' has wrong type at {target}"
                )
            elif resource.kind == "dir" and not target.is_dir():
                errors.append(
                    f"- required dir '{resource_id}' has wrong type at {target}"
                )

        # if any required resources are missing, merge them into a single error message
        if errors:
            raise OSError(
                "layout application left required resources missing or invalid:\n"
                f"{'\n'.join(errors)}"
            )
