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

from .pipeline import Mkdir, Pipeline, WriteText
from .run import LOCK_TIMEOUT, Lock, sanitize_name
from .version import __version__


# Canonical path and name definitions for shared resources
ENV_DIR_NAME: str = ".bertrand"
ENV_FILE_NAME: str = "env.json"
ENV_LOCK_NAME: str = ".lock"
ENV_LAYOUT_KEY: str = "layout"
LAYOUT_SCHEMA_VERSION: int = 1
MOUNT: PosixPath = PosixPath("/env")
assert MOUNT.is_absolute()


# semantic role names for resource discovery by other subsystems
ROLE_CONFIG_PRIMARY: str = "config.primary"
ROLE_ARTIFACT_CPP: str = "artifact.cpp"
ROLES: set[str] = {ROLE_CONFIG_PRIMARY, ROLE_ARTIFACT_CPP}


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
    and can be loaded to reconstruct the layout after initialization.  Roles map
    semantic names (e.g. "config.primary") to concrete resource IDs, allowing other
    subsystems to resolve canonical configuration sources and derived artifacts without
    hardcoding filenames.
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
    roles: dict[str, list[str]] = Field(
        default_factory=dict,
        description=
            "Mapping of semantic role names to ordered resource IDs.  Role targets "
            "must reference resources defined in this manifest."
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

    @field_validator("roles", mode="before")
    @classmethod
    def _validate_roles(cls, value: Any) -> Any:
        if not isinstance(value, dict):
            return value

        # role names must be non-empty strings with no duplicates
        normalized: dict[str, list[str]] = {}
        seen_roles: set[str] = set()
        for raw_name, raw_ids in value.items():
            if not isinstance(raw_name, str):
                raise ValueError("layout role names must be strings")
            role_name = raw_name.strip()
            if not role_name:
                raise ValueError("layout role names must be non-empty")
            if role_name in seen_roles:
                raise ValueError(f"duplicate layout role name: {role_name}")
            seen_roles.add(role_name)

            # role targets must be non-empty lists of resource IDs
            if not isinstance(raw_ids, list):
                raise ValueError(f"layout role targets must be lists: {role_name}")
            ids: list[str] = []
            seen_ids: set[str] = set()
            for raw_id in raw_ids:
                if not isinstance(raw_id, str):
                    raise ValueError(
                        f"layout role resource IDs must be strings: {role_name}"
                    )
                resource_id = raw_id.strip()
                if not resource_id:
                    raise ValueError(
                        f"layout role resource IDs must be non-empty: {role_name}"
                    )
                if resource_id in seen_ids:
                    raise ValueError(
                        f"duplicate resource ID '{resource_id}' in layout role '{role_name}'"
                    )
                seen_ids.add(resource_id)
                ids.append(resource_id)
            if not ids:
                raise ValueError(f"layout role must reference at least one resource: {role_name}")
            normalized[role_name] = ids
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

        # validate role targets point to known resources
        for role_name in self.roles:
            for resource_id in self.roles[role_name]:
                if resource_id not in self.resources:
                    raise ValueError(
                        f"layout role '{role_name}' references unknown resource ID: "
                        f"'{resource_id}'"
                    )
        return self


class Capability(BaseModel):
    """Profile-specific contribution from a capability.

    This bundles both resource specs and semantic role bindings, so capabilities can
    provide profile-aware paths and role memberships in one place.
    """
    model_config = ConfigDict(extra="forbid")
    resources: dict[str, Resource] = Field(default_factory=dict)
    roles: dict[str, list[str]] = Field(default_factory=dict)


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


def _template_path(ctx: Pipeline.InProgress, ref: TemplateRef) -> Path:
    return ctx.state_dir / "templates" / ref.namespace / ref.name / f"{ref.version}.j2"


# NOTE: "*" indicates a base line, while other keys act as overlay diffs that merge
# on top to avoid duplication.


# Profiles define basic directory layout and minimal bootstrap resources.
PROFILES: dict[str, dict[str, Resource]] = {
    "*": {
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
    "flat": {},
    "src": {
        "src": Resource(
            kind="dir",
            path=PosixPath("src"),
            managed=False,
            required=False,
            template=None,
        ),
    },
}


# Capabilities define language-specific resources and role bindings for later
# discovery and generation.
CAPABILITIES: dict[str, dict[str, Capability]] = {
    "python": {
        "*": Capability(
            resources={
                "pyproject": Resource(
                    kind="file",
                    path=PosixPath("pyproject.toml"),
                    template=TemplateRef(
                        namespace="core",
                        name="pyproject",
                        version="2026-02-15"
                    ),
                ),
            },
            roles={
                ROLE_CONFIG_PRIMARY: ["pyproject"],
            },
        ),
    },
    "cpp": {
        "*": Capability(
            resources={
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
            roles={
                ROLE_ARTIFACT_CPP: ["clang_format", "clang_tidy", "clangd"],
            },
        ),
    },
    # TODO: we may want other capabilities related to editors or language-specific
    # tools, such as the managed workspace file for vscode, etc.  We'll have to
    # revisit this later down the line.
}


@dataclass
class Layout:
    """Read-only view representing the deserialized contents of a layout manifest,
    together with the environment root path in which to apply it.  This is the main
    entry point for layout rendering and application logic, and provides role-based
    resolution APIs for consumers that need stable access to configuration sources and
    derived artifacts.
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
    def _merge_resource_maps(
        base: dict[str, Resource],
        overlay: dict[str, Resource],
    ) -> dict[str, Resource]:
        merged = {
            resource_id: resource.model_copy(deep=True)
            for resource_id, resource in base.items()
        }
        for resource_id, resource in overlay.items():
            merged[resource_id] = resource.model_copy(deep=True)
        return merged

    @staticmethod
    def _merge_role_maps(
        base: dict[str, list[str]],
        overlay: dict[str, list[str]],
    ) -> dict[str, list[str]]:
        merged = {
            role_name: role_ids.copy()
            for role_name, role_ids in base.items()
        }
        for role_name, role_ids in overlay.items():
            target = merged.setdefault(role_name, [])
            for resource_id in role_ids:
                if resource_id not in target:
                    target.append(resource_id)
        return merged

    @staticmethod
    def _resolve_profile(profile: str) -> dict[str, Resource]:
        base = PROFILES.get("*")
        if base is None:
            raise ValueError("missing wildcard baseline in PROFILES: '*'")
        overlay = PROFILES.get(profile)
        if overlay is None:
            raise ValueError(
                f"unknown layout profile: {profile} (supported: "
                f"{', '.join(sorted(profile for profile in PROFILES if profile != "*"))})"
            )
        return Layout._merge_resource_maps(base, overlay)

    @staticmethod
    def _resolve_capability(capability: str, profile: str) -> Capability:
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
        overlay = variants.get(profile, Capability())
        return Capability(
            resources=Layout._merge_resource_maps(base.resources, overlay.resources),
            roles=Layout._merge_role_maps(base.roles, overlay.roles),
        )

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
            unknown, if wildcard baselines are missing, if `config.primary` is
            missing, or if there are any invalid resource collisions (including path
            collisions) when merging.
        """
        # normalize and validate profile
        profile_key = profile.strip().lower()
        supported = sorted(profile for profile in PROFILES if profile != "*")
        if profile_key not in supported:
            raise ValueError(
                f"unknown layout profile: {profile} (supported: {', '.join(supported)})"
            )

        # merge profile resources
        merged = cls._resolve_profile(profile_key)
        merged_roles: dict[str, list[str]] = {}

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

        # merge resolved capability resources/roles, checking for collisions
        for cap in caps:
            variant = cls._resolve_capability(cap, profile_key)
            for resource_id in variant.resources:
                resource = variant.resources[resource_id]
                existing = merged.get(resource_id)
                if existing is None:
                    merged[resource_id] = resource.model_copy(deep=True)
                    continue
                if existing.model_dump(mode="python") != resource.model_dump(mode="python"):
                    raise ValueError(
                        f"layout resource collision for '{resource_id}' while applying "
                        f"capability '{cap}'"
                    )
            merged_roles = cls._merge_role_maps(merged_roles, variant.roles)

        # validate that the merged layout includes a primary config source, which is
        # required for later configuration loading and artifact generation
        primary = merged_roles.get(ROLE_CONFIG_PRIMARY)
        if primary is None or len(primary) == 0:
            raise ValueError(
                f"layout requires role '{ROLE_CONFIG_PRIMARY}' for profile '{profile_key}' "
                f"and capabilities {caps}"
            )

        return cls(
            root=env_root,
            manifest=Manifest(
                schema_version=LAYOUT_SCHEMA_VERSION,
                profile=profile_key,
                capabilities=caps,
                resources=merged,
                roles=merged_roles,
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

    def role(self, name: str) -> tuple[str, ...]:
        """Resolve a semantic role to its ordered resource IDs.

        Parameters
        ----------
        name : str
            The semantic role name to resolve.

        Returns
        -------
        tuple[str, ...]
            Ordered, immutable resource IDs for this role.  This may be empty if the
            role is not present, but will never be empty otherwise.

        Raises
        ------
        ValueError
            If the role name is unknown (i.e. not in the `ROLES` set).
        """
        name = name.strip()
        if name not in ROLES:
            raise ValueError(
                f"unknown layout role: '{name}' (supported: {', '.join(sorted(ROLES))})"
            )
        if name in self.manifest.roles:
            return tuple(self.manifest.roles[name])
        return ()

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
