"""TODO"""
from __future__ import annotations

import importlib.resources as importlib_resources
import re
import string
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from pathlib import Path, PosixPath
from types import TracebackType
from typing import (
    Annotated,
    Any,
    ClassVar,
    Protocol,
    Self,
    TypeVar,
    cast,
)

import yaml
from pydantic import (
    AfterValidator,
    AnyHttpUrl,
    BaseModel,
    Field,
    StringConstraints,
    TypeAdapter,
    ValidationError,
)

from ..run import (
    BERTRAND_ENV,
    ENV_ID_ENV,
    IMAGE_TAG_ENV,
    LOCK_TIMEOUT,
    GitRepository,
    inside_container,
    inside_image,
    lock_worktree,
    run,
)

# TODO: maybe cache paths should be provided by each resource, so that they can also
# be folded into the resource contract, along with rendering sections in
# `pyproject.toml` depending on the environment's capabilities?


# TODO: I need extra resources/models for `uv`, `ty`, `ruff`, and `pytest`, so that
# they are rendered correctly as well for new projects.


HTTP_URL = TypeAdapter(AnyHttpUrl)


# Canonical path definitions for worktree control
VSCODE_WORKSPACE_FILE: PosixPath = PosixPath(".vscode/vscode.code-workspace")
CACHE_MOUNT: PosixPath = PosixPath("/tmp/.cache")
UV_CACHE: PosixPath = CACHE_MOUNT / "uv"


GLOB_RE = re.compile(r"^[A-Za-z0-9._/\-\*\?\[\]!]+$")
RESOURCE_NAME_RE = re.compile(r"^[a-z]([a-z0-9_.-]*[a-z0-9])?$")


def _check_glob(pattern: str) -> str:
    if not GLOB_RE.fullmatch(pattern):
        raise ValueError(f"invalid glob pattern: '{pattern}'")
    if pattern.startswith("/"):
        raise ValueError(f"glob pattern cannot be absolute: '{pattern}'")
    if any(part in ("..", ".") for part in pattern.split("/")):
        raise ValueError(f"glob pattern cannot contain '.' or '..' segments: '{pattern}'")
    return pattern


def _check_absolute_path[PathT: Path](path: PathT) -> PathT:
    if not path.is_absolute():
        raise ValueError(f"path must be absolute: '{path}'")
    parts = path.parts
    if not parts:
        raise ValueError("path cannot be empty")
    if any(p == "." or p == ".." for p in parts):
        raise ValueError(f"path cannot contain '.' or '..' segments: '{path}'")
    return path


def _check_relative_path(path: PosixPath) -> PosixPath:
    if path.is_absolute():
        raise ValueError(f"path cannot be absolute: '{path}'")
    parts = path.parts
    if not parts:
        raise ValueError("path cannot be empty")
    if any(p == "." or p == ".." for p in parts):
        raise ValueError(f"path cannot contain '.' or '..' segments: '{path}'")
    return path


def _check_regex_pattern(value: str) -> str:
    try:
        re.compile(value)
    except re.error as err:
        raise ValueError(f"invalid regex pattern '{value}': {err}") from err
    return value


def _check_url(url: str) -> str:
    try:
        return str(HTTP_URL.validate_python(url))
    except ValidationError as err:
        raise ValueError(f"invalid URL: {url}") from err


def _check_url_label(label: str) -> str:
    chars_to_remove = string.punctuation + string.whitespace
    removal_map = str.maketrans("", "", chars_to_remove)
    return label.translate(removal_map).lower()


type NonEmpty[SequenceT: Sequence[Any]] = Annotated[SequenceT, Field(min_length=1)]
type Trimmed = Annotated[str, StringConstraints(strip_whitespace=True)]
type NoCRLF = Annotated[  # pylint: disable=invalid-name
    str,
    StringConstraints(strip_whitespace=True, pattern=r"^[^\r\n]*$")
]
type NoWhiteSpace = Annotated[
    str,
    StringConstraints(strip_whitespace=True, pattern=r"^\S*$")
]
type Glob = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_glob)]
type AbsolutePath = Annotated[Path, AfterValidator(_check_absolute_path)]
type AbsolutePosixPath = Annotated[PosixPath, AfterValidator(_check_absolute_path)]
type RelativePath = Annotated[Path, AfterValidator(_check_relative_path)]
type RelativePosixPath = Annotated[PosixPath, AfterValidator(_check_relative_path)]
type RegexPattern = Annotated[NonEmpty[NoCRLF], AfterValidator(_check_regex_pattern)]
type ResourceName = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    pattern=RESOURCE_NAME_RE.pattern
)]
type URL = Annotated[  # pylint: disable=invalid-name
    NonEmpty[NoCRLF],
    AfterValidator(_check_url)
]
type URLLabel = Annotated[NonEmpty[Trimmed], AfterValidator(_check_url_label)]
type TagName = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    pattern=r"^[a-z0-9]+(?:-[a-z0-9]+)*$"
)]


def locate_template(namespace: str, name: str) -> Path:
    """Get a template reference for the given namespace and name.

    Parameters
    ----------
    namespace : str
        The parent directory of the template within `bertrand.env.templates`.
    name : str
        The file name for the template within the namespace directory, minus the
        `.j2` extension.

    Returns
    -------
    Path
        The path to the template file.

    Raises
    ------
    FileNotFoundError
        If the template file does not exist or is not a file.
    """
    env = importlib_resources.files("bertrand.env")
    with importlib_resources.as_file(env.joinpath(
        "templates",
        namespace,
        f"{name}.j2"
    )) as source:
        if not source.exists() or not source.is_file():
            raise FileNotFoundError(
                f"missing Bertrand template {namespace}/{name}: {source}"
            )
        return source


def dump_yaml(payload: dict[str, Any], *, resource_name: str) -> str:
    """A simple YAML serializer.

    Parameters
    ----------
    payload : dict[str, Any]
        The data to serialize as YAML.  This should be a simple mapping of strings to
        basic data types (strings, numbers, lists, and nested mappings).
    resource_name : str
        The name of the resource being rendered, used for error reporting in case
        serialization fails.

    Returns
    -------
    str
        The serialized YAML string.

    Raises
    ------
    OSError
        If the payload cannot be serialized as YAML, or if it contains unsupported
        data types.
    """
    try:
        text = yaml.safe_dump(
            payload,
            default_flow_style=False,
            sort_keys=False,
            allow_unicode=False,
        )
    except yaml.YAMLError as err:
        raise OSError(
            f"failed to serialize YAML payload for resource '{resource_name}': {err}"
        ) from err
    if not text.endswith("\n"):
        text += "\n"
    return text


class Resource:
    """A base class describing a single configuration entity that can be parsed,
    validated, and/or rendered by Bertrand's layout system.

    Attributes
    ----------
    name : ResourceName
        The globally unique name (any valid TOML key) for this resource, which serves
        as a stable CLI identifier, allows it to be validated from a `parse()` snapshot
        during `Config.__aenter__()`.
    paths : frozenset[RelativePath]
        The set of relative paths that this resource manages within the project
        worktree.  If not empty, then `Config.load()` will attempt to discover this
        resource by searching for the given paths within the worktree, and will add
        the resource to its context if ALL paths are found.
    """
    # pylint: disable=unused-argument, redundant-returns-doc
    name: ClassVar[ResourceName]
    paths: ClassVar[frozenset[RelativePath]]

    def __hash__(self) -> int:
        return hash(self.name)

    def __lt__(self, other: object) -> bool:
        if isinstance(other, Resource):
            return self.name < other.name
        if isinstance(other, str):
            return self.name < other
        return NotImplemented

    def __le__(self, other: object) -> bool:
        if isinstance(other, Resource):
            return self.name <= other.name
        if isinstance(other, str):
            return self.name <= other
        return NotImplemented

    def __eq__(self, other: object) -> bool:
        if isinstance(other, Resource):
            return self.name == other.name
        if isinstance(other, str):
            return self.name == other
        return NotImplemented

    def __ne__(self, other: object) -> bool:
        if isinstance(other, Resource):
            return self.name != other.name
        if isinstance(other, str):
            return self.name != other
        return NotImplemented

    def __ge__(self, other: object) -> bool:
        if isinstance(other, Resource):
            return self.name >= other.name
        if isinstance(other, str):
            return self.name >= other
        return NotImplemented

    def __gt__(self, other: object) -> bool:
        if isinstance(other, Resource):
            return self.name > other.name
        if isinstance(other, str):
            return self.name > other
        return NotImplemented

    async def init(self, config: Config, cli: Config.Init) -> dict[str, Any]:
        """Render this resource's initial contents during `bertrand init`.

        Parameters
        ----------
        config : Config
            The active configuration context, which provides access to the
            resource's path and other shared state.
        cli : Config.Init
            Normalized CLI input to the `bertrand init` command, which can be used to
            customize the default values for this resource based on user input.

        Returns
        -------
        dict[str, Any]
            Normalized config data describing the default values for all of this
            resource's relevant configuration options.  For resources with pydantic
            models, this can often be obtained by simply dumping a default-constructed
            instance of the model. The result must pass a later `validate()` call,
            which is invoked after all `Resource.parse()` hooks have been merged
            against the outputs from this hook.

        Notes
        -----
        Resources that do not implement this function will be treated as stateless.  If
        such a resource also implements a `validate()` hook, then it means that it
        always expects to find valid config data from other resources via their
        `parse()` hooks.
        """
        return {}

    async def parse(self, config: Config) -> dict[str, dict[str, Any]]:
        """A parse function that can extract normalized config data from this
        resource when entering the `Config` context.

        Parameters
        ----------
        config : Config
            The active configuration context, which provides access to the
            resource's path and other shared state.

        Returns
        -------
        dict[str, dict[str, Any]]
            Normalized config data extracted from this resource, if any.  The
            dictionary's top-level keys must describe the resource names that were
            detected during parsing, which will be merged into the results of the
            `init()` phase, and whose `validate()` hooks will be called to normalize
            the output.

        Notes
        -----
        This function is responsible for loading the resource's content without
        coupling to any particular input schema, and transforming it into a fragment
        that can be merged to form a global snapshot.  Only after all fragments have
        been merged will the `validate()` phase begin, allowing valid configs to be
        shared across any combination of resources, regardless of origin.

        Resources that do not implement this function will be treated as output-only.
        """
        return {}

    async def validate(self, config: Config, fragment: Any) -> BaseModel | None:
        """A function that validates the merged output of the `parse()` phase against
        this resource.

        Parameters
        ----------
        config : Config
            The active configuration context, which provides access to the merged
            config snapshot from the `parse()` phase.
        fragment : Any
            The fragment of the merged config snapshot that is relevant to this
            resource, which the method must validate.

        Returns
        -------
        BaseModel | None
             A Pydantic model containing validated configuration fields matching this
             resource, or None if the resource does not require validation.  Usually,
             this means that another resource has already validated the relevant
             fields, or the resource is purely output-oriented and does not have any
             state to validate.

        Raises
        ------
        ValidationError
            If the config fields relevant to this resource are present but fail
            validation.

        Notes
        -----
        If a Pydantic model is returned, then it means this resource should be added to
        the `Config` resource list, even if it is not currently present on disk.  This
        is what allows resources mentioned in config to always be rendered during
        `sync()`, even if their original source files are missing.
        """
        return None

    async def schema(self) -> dict[str, Any] | None:
        """Return a JSON Schema description for this resource, if available.

        Returns
        -------
        dict[str, Any] | None
            The resource's validation-mode JSON Schema generated from its nested
            Pydantic `Model` class using alias-aware keys, or None if the resource
            does not define a `Model`.

        Notes
        -----
        This is internal docs infrastructure used to expose authoritative resource
        schemas without coupling to CLI/docsite export behavior.
        """
        return None

    async def render(self, config: Config, tag: str | None) -> None:
        """A render function that writes content for this resource during
        `Config.sync()`.

        Parameters
        ----------
        config : Config
            The active configuration context, which provides access to the valid
            outputs from the `validate()` phase.
        tag : str | None
            The active image tag for the configured environment, which is used to
            search the `config.get(Bertrand).tags` list for tag-specific overrides
            during image builds.  If None, then it means this hook was invoked during
            a `bertrand init` command, and should therefore not attempt to render any
            out-of-tree artifacts that would require access to a container filesystem.

        Notes
        -----
        This is used to generate derived artifacts from a validated config without
        coupling to any particular output schema.
        """


RESOURCES: set[Resource] = set()
RESOURCE_NAMES: dict[ResourceName, Resource] = {}
RESOURCE_PATHS: dict[RelativePath, Resource] = {}


def resource[ResourceT: Resource](
    name: ResourceName,
    *,
    paths: set[RelativePath] | frozenset[RelativePath] = frozenset(),
) -> Callable[[type[ResourceT]], type[ResourceT]]:
    """A class decorator for defining layout resources.  See `Resource` for more
    details on the parameters and intended semantics of layout resources.

    Parameters
    ----------
    name : ResourceName
        The globally unique name (any valid TOML key) for this resource, which serves
        as a stable CLI identifier, allows it to be validated from a `parse()` snapshot
        during `Config.__aenter__()`.
    paths : set[RelativePath] | frozenset[RelativePath], optional
        The relative paths that this resource manages, which allows it to be discovered
        by `Config.load()`, assuming all paths are found.  The paths are relative to
        the worktree root, and must not contain `..` segments.

    Returns
    -------
    Callable[[type[ResourceT]], type[ResourceT]]
        A class decorator that registers the decorated class as a layout resource in the
        global catalog under the given names, with the specified path/groups.

    Raises
    ------
    TypeError
        If any resource name is not sanitized, or if any path is absolute or contains
        `..` segments.
    """
    def _decorator(cls: type[ResourceT]) -> type[ResourceT]:
        self = cls()
        RESOURCES.add(self)

        # reserve name
        if not RESOURCE_NAME_RE.fullmatch(name):
            raise TypeError(
                f"invalid resource name {name!r} (must match regex "
                f"{RESOURCE_NAME_RE.pattern})"
            )
        if RESOURCE_NAMES.setdefault(name, self) is not self:
            raise TypeError(f"duplicate resource name: {name!r}")

        # reserve paths
        for path in paths:
            if path.is_absolute():
                raise TypeError(f"invalid resource path '{path}': must be relative")
            if any(part == ".." for part in path.parts):
                raise TypeError(
                    f"invalid resource path '{path}': cannot contain '..' segments"
                )
            other = RESOURCE_PATHS.setdefault(path, self)
            if other is not self:
                raise TypeError(
                    f"duplicate resource path maps to both {name!r} and "
                    f"{other.name!r}: {path}"
                )

        # stamp variables at class level to simplify `Config.get(T)`
        cls.name = name
        cls.paths = frozenset(paths)
        return cls

    return _decorator


_ResourceModel_co = TypeVar("_ResourceModel_co", bound=BaseModel, covariant=True)


class _ResourceLike(Protocol[_ResourceModel_co]):
    """A type helper that allows `Config.get()` to infer a resource's validated model
    type by inspecting its `validate()` method.
    """
    # pylint: disable=missing-function-docstring
    name: ClassVar[ResourceName]
    async def validate(self, config: Config, fragment: Any) -> _ResourceModel_co | None: ...


@dataclass
class Config:
    """Read-only view representing resource placements within a worktree, as well as
    normalized config data parsed from those resources, without coupling to any
    particular schema.
    """
    @dataclass(frozen=True)
    class Init:
        """A context object representing normalized CLI input to the `bertrand init`
        command, which is passed to each resource's `init()` hook to drive their
        initial values.

        Attributes
        ----------
        repo : GitRepository
            The parent git repository containing the worktree being initialized.
        worktree : RelativePath
            The relative path from the repository root to the current git worktree,
            which is the actual target for resource rendering.  This may be `.` if the
            worktree is the same as the project root, which is the case for
            single-worktree repositories.  For multi-worktree repositories, it will
            usually be a branch-named subdirectory of the project root, except in cases
            where the branch contains path separators (creating nested directories) or
            is detached from any branch (in which case it will be an arbitrary path).
            The relative path will never contain `..` segments.
        """
        repo: GitRepository
        worktree: RelativePath

    worktree: Path
    timeout: float
    resources: dict[ResourceName, BaseModel | None] = field(
        default_factory=lambda: {"bertrand": None}
    )
    init: Init | None = field(default=None, repr=False)
    _entered: int = field(default=0, repr=False)

    @classmethod
    async def load(cls, worktree: Path, *, timeout: float = LOCK_TIMEOUT) -> Self:
        """Load a worktree configuration by scanning the environment root for known
        resource placements based on their managed paths, and resolving any collisions
        or invalid placements.

        Parameters
        ----------
        worktree : Path
            The root path of the environment directory.
        timeout : float, optional
            Maximum time to wait for acquiring the worktree lock, in seconds.  If
            the lock cannot be acquired within this time, a `TimeoutError` is raised.

        Returns
        -------
        Self
            A resolved `Config` instance containing the discovered resources.  This
            instance must be entered as a context manager to parse and validate config
            data from the discovered resources, and to make that data available as
            attributes on the instance, which are outside the scope of this method.

        Raises
        ------
        TimeoutError
            If the worktree lock cannot be acquired within the specified timeout.
        ValueError
            If any resource placements reference unknown resource IDs, or if there are
            any path collisions between resources in the environment (either from
            multiple resources mapping to the same path, or from a single resource
            mapping to multiple paths).
        """
        worktree = worktree.expanduser().resolve()
        async with lock_worktree(worktree, timeout=timeout):
            self = cls(worktree=worktree, timeout=timeout)
            self.resources.update({
                r.name: None
                for r in RESOURCES
                if r.paths and all((worktree / p).exists() for p in r.paths)
            })
            return self

    def _merge_fragment(
        self,
        r: Resource,
        fragment: dict[Any, Any],
        snapshot: dict[str, Any],
        *,
        key_owner: dict[tuple[str, ...], str],
        path_prefix: tuple[str, ...],
    ) -> None:
        for key, value in fragment.items():
            if not isinstance(key, str):
                parent = ".".join(path_prefix)
                raise OSError(
                    f"parse hook for resource '{r.name}' returned non-string key "
                    f"under '{parent}': '{key}'"
                )
            value_is_map = isinstance(value, dict)

            # reserve ownership to prevent collisions with other parsed resources.
            # Note that the default values provided by `init()` hooks are not
            # considered, and will therefore be overwritten
            key_path = path_prefix + (key,)
            owner = key_owner.setdefault(key_path, r.name)

            # if the key is already present, then do a deep merge if both values are
            # mappings, or directly replace the value if not
            existing = snapshot.setdefault(key, value)
            existing_is_map = isinstance(existing, dict)
            if existing_is_map and value_is_map:
                self._merge_fragment(
                    r,
                    value,
                    existing,
                    key_owner=key_owner,
                    path_prefix=key_path,
                )
            elif owner != r.name or existing_is_map or value_is_map:
                raise OSError(
                    f"config parse key collision at '{'.'.join(key_path)}' between "
                    f"resources '{owner}' and '{r.name}'"
                )
            else:
                snapshot[key] = value

    async def __aenter__(self) -> Self:
        """Parse and validate config data from resources in the environment, which
        remains valid until the outermost context is exited.

        Raises
        ------
        OSError
            If any resource parsing or validation fails, or if there are any key
            collisions between parsed config fragments from different resources
            (enforcing unique ownership).
        """
        if self._entered > 0:  # re-entrant case
            self._entered += 1
            return self

        old_resources = self.resources.copy()
        try:
            async with lock_worktree(self.worktree):
                # invoke `init()` hooks for all resources to get baseline snapshot
                snapshot = {} if self.init is None else {
                    r: await RESOURCE_NAMES[r].init(self, self.init)
                    for r in sorted(self.resources)
                }

                # invoke parse hooks for all resources in deterministic order
                key_owner: dict[tuple[str, ...], ResourceName] = {}
                for name in sorted(self.resources):
                    r = RESOURCE_NAMES[name]
                    try:
                        fragment = await r.parse(self)
                    except Exception as err:
                        raise OSError(f"failed to parse resource {r.name!r}: {err}") from err
                    if not isinstance(fragment, dict):
                        raise OSError(
                            f"parse hook for resource {r.name!r} must return a string "
                            f"mapping: {fragment}"
                        )

                    # normalize aliases and merge fragment, checking for key collisions
                    for raw_key, table in fragment.items():
                        if not isinstance(raw_key, str):
                            raise OSError(
                                f"parse hook for resource {r.name!r} returned "
                                f"non-string key: {raw_key}"
                            )
                        if not isinstance(table, dict):
                            raise OSError(
                                f"parse hook for resource {r.name!r} returned "
                                f"non-mapping value for key '{raw_key}': {table}"
                            )
                        lookup = RESOURCE_NAMES.get(raw_key)
                        if lookup is not None:
                            self._merge_fragment(
                                r,
                                table,
                                snapshot.setdefault(lookup.name, {}),
                                key_owner=key_owner,
                                path_prefix=(lookup.name,)
                            )

                # validate each parsed fragment against its corresponding resource
                for key, table in snapshot.items():
                    lookup = RESOURCE_NAMES.get(key)
                    if lookup is None:
                        continue  # skip unrecognized tables

                    # record validated output for future, type-safe access
                    model = await lookup.validate(self, table)
                    if self.resources.get(lookup.name) is not None:
                        raise OSError(
                            f"config validation collision for resource '{lookup.name}': "
                            f"multiple resources writing to the same top-level table"
                        )
                    self.resources[lookup.name] = model

                self._entered += 1
                return self
        except:
            self.resources = old_resources
            self._entered = 0
            raise

    async def __aexit__(
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
            self.resources = {r: None for r in self.resources}

    def __bool__(self) -> bool:
        return self._entered > 0

    def __contains__(self, key: ResourceName) -> bool:
        """Check if a resource ID is present in the environment.

        Parameters
        ----------
        key : ResourceName
            The stable identifier of the resource to check for, as defined in the
            global catalog.

        Returns
        -------
        bool
            True if the resource ID is present in the environment, False otherwise.
        """
        return key in self.resources

    def get(self, r: type[_ResourceLike[_ResourceModel_co]]) -> _ResourceModel_co | None:
        """Retrieve the parsed config model for the given resource ID, assuming it is
        present in the environment.

        Parameters
        ----------
        r : type[Resource]
            A raw resource type (decorated with `@resource`) to get the model for.

        Returns
        -------
        BaseModel | None
            The parsed config model for the given resource ID, or None if the resource
            is not present in the environment.  The exact type of the model always
            matches the return type of the resource's `validate()` method, in order to
            safely propagate static type information.
        """
        return cast(_ResourceModel_co | None, self.resources.get(r.name))

    @staticmethod
    async def schema() -> dict[ResourceName, dict[str, Any] | None]:
        """Return all registered resource schemas in deterministic catalog order.

        Returns
        -------
        dict[ResourceName, dict[str, Any] | None]
            A mapping from canonical resource names to their validation-mode,
            alias-aware JSON Schemas.  Output-only resources are included with a
            value of None.

        Notes
        -----
        This omits pyproject table-path metadata by design and is intended for
        internal docs composition.
        """
        return {r.name: await r.schema() for r in sorted(RESOURCES)}

    def image_args(self, worktree: Path, tag: str) -> list[str]:
        """Retrieve a set of `podman build` arguments to apply during image builds for
        the given tag.

        Parameters
        ----------
        worktree : Path
            Absolute path to the host worktree whose configuration is being used.  This
            is used to resolve tag-relative artifact paths into concrete host
            filesystem locations for podman to consume.
        tag : str
            The active image tag for the configured environment, which is used to
            search the `bertrand.tags` list for tag-specific overrides when generating
            build arguments.  Usually, this is supplied by either the build system or an
            in-container environment variable, but we make no assumptions here.

        Returns
        -------
        list[str]
            A list of arguments to append to the `podman build` command when building
            the specified image.

        Raises
        ------
        TypeError
            If the `bertrand` config is not present in this environment, or if the
            `cmd` override is not a list of strings.
        ValueError
            If the specified tag is not present in the `bertrand` config.
        """
        from .bertrand import Bertrand
        bertrand = self.get(Bertrand)
        if bertrand is None:
            raise TypeError(
                f"missing 'bertrand' configuration for environment at {self.worktree}"
            )
        cfg = next((t for t in bertrand.tags if t.tag == tag), None)
        if cfg is None:
            raise ValueError(
                f"unknown image tag '{tag}' for environment at {self.worktree}"
            )

        # TODO: the containerfile is now an output artifact under the metadata
        # directory, rather than a tracked file in the worktree
        worktree = worktree.expanduser().resolve()
        containerfile = worktree / cfg.containerfile

        # TODO: expand the set of arguments to cover the entire build configuration
        # for this tag.  This will be more complicated than it sounds because we need
        # to cover the podman surface area.
        return [
            "--file", str(containerfile),
        ]

    async def container_args(
        self,
        worktree: Path,
        env_id: str,
        tag: str,
        image_id: str,
        cmd: list[NonEmpty[Trimmed]] | None,
        bootstrap: PosixPath,
    ) -> list[str]:
        """Retrieve a set of `podman run` arguments to apply during container runs for
        the given tag.

        Parameters
        ----------
        worktree : Path
            Absolute path to the host worktree whose configuration is being used.  This
            is used to resolve tag-relative artifact paths into concrete host
            filesystem locations for podman to consume.
        env_id : str
            The Bertrand environment UUID used for stable volume naming and labeling.
        tag : str
            The active image tag for the configured environment, which is used to
            search the `bertrand.tags` list for tag-specific overrides when generating
            run arguments.  Usually, this is supplied by either the build system or an
            in-container environment variable, but we make no assumptions here.
        image_id : str
            The OCI image ID to run, used as the image operand in the final podman
            argv tail.
        cmd : list[str] | None
            Optional command override supplied by the CLI.  If not provided, the
            configured `entry-point` for the selected tag is used.
        bootstrap : PosixPath
            Absolute in-container path to the runtime bootstrap script that should be
            used as the podman entrypoint.  This runs immediately before the normal
            entry point and completes startup by creating various symlinks and
            environment variables within the container context.

        Returns
        -------
        list[str]
            A list of arguments to append to the `podman run` command when running a
            container for the specified image tag, based on that tag's configuration
            in the build matrix.

        Raises
        ------
        TypeError
            If the `bertrand` config is not present in this environment, or if the
            `cmd` override is not a list of strings.
        ValueError
            If the specified tag is not present in the `bertrand` config, if the
            effective entry point is empty after accounting for overrides, or if any
            entry point argument is an empty or whitespace-only string.
        """
        from .bertrand import Bertrand
        bertrand = self.get(Bertrand)
        if bertrand is None:
            raise TypeError(
                f"missing 'bertrand' configuration for environment at {self.worktree}"
            )
        cfg = next((t for t in bertrand.tags if t.tag == tag), None)
        if cfg is None:
            raise ValueError(
                f"unknown image tag '{tag}' for environment at {self.worktree}"
            )
        worktree = worktree.expanduser().resolve()
        if not worktree.exists() or not worktree.is_dir():
            raise ValueError(f"worktree must be an existing directory: {worktree}")
        env_id = env_id.strip()
        if not env_id:
            raise ValueError("environment ID cannot be empty when forming container args")
        image_id = image_id.strip()
        if not image_id:
            raise ValueError("image ID cannot be empty when forming container args")
        if not bootstrap.is_absolute():
            raise ValueError(f"path to bootstrap script must be absolute: {bootstrap}")

        # determine effective entry point, accounting for override
        if cmd is None:
            entry_point = cfg.entry_point
        else:
            if not isinstance(cmd, list):
                raise TypeError("command override must be a list of strings")
            if not all(isinstance(part, str) for part in cmd):
                raise TypeError("command override must be a list of strings")
            entry_point = cmd
        if not entry_point:
            raise ValueError(
                f"tag '{tag}' has no effective entry point: provide a command override "
                "or configure [tool.bertrand.tags.entry-point] for this tag"
            )
        if any(not part.strip() for part in entry_point):
            raise ValueError("entry point arguments must be non-empty strings")

        # create/ensure named cache volumes and emit corresponding mount args
        mounts: list[str] = []
        for kind, destination in (
            ("uv", str(CACHE_MOUNT / "uv")),
            ("bertrand", str(CACHE_MOUNT / "bertrand")),
            ("ccache", str(CACHE_MOUNT / "ccache")),
            ("conan", "/opt/conan"),
        ):
            name = f"bertrand-{env_id[:13]}-{kind}"
            try:
                await run([
                    "podman",
                    "volume",
                    "create",
                    "--label", f"{BERTRAND_ENV}=1",
                    "--label", f"{ENV_ID_ENV}={env_id}",
                    "--label", f"{IMAGE_TAG_ENV}={tag}",
                    name,
                ], check=False, capture_output=True)
            except Exception:
                pass
            mounts.extend([
                "--mount",
                f"type=volume,src={name},dst={destination}",
            ])

        # TODO: expand the set of arguments to cover the entire run configuration for
        # this tag.  This will be more complicated than it sounds because we need to
        # cover the podman surface area.
        return [
            *mounts,
            "--entrypoint", str(bootstrap),
            image_id,
            *entry_point
        ]

    async def sync(self, tag: str | None) -> None:
        """Render and write derived artifact resources from active context snapshot.

        This requires an active config context (`async with config:`).

        Parameters
        ----------
        tag : str | None
            The active image tag for the configured environment, which is used to
            search the `bertrand.tags` list for tag-specific overrides during
            rendering.  If None, then this method was called as part of a
            `bertrand init` command, and only the global configuration worktree
            resources will be rendered.  Otherwise, it was called from a
            `bertrand build` command, and out-of-tree resources may also be rendered
            to the container filesystem for the active tag.

        Raises
        ------
        RuntimeError
            If called outside a a Bertrand image or active config context.
        OSError
            If any render hooks fail.
        """
        if not self:
            raise RuntimeError("sync() artifact rendering requires an active config context")

        # invoke render hooks for all resources in deterministic order
        async with lock_worktree(self.worktree):
            for name in sorted(self.resources):
                r = RESOURCE_NAMES[name]
                try:
                    await r.render(self, tag)
                except Exception as err:
                    raise OSError(f"failed to render resource '{r.name}': {err}") from err

    async def build(self, tag: str) -> None:
        """Install Python dependencies and builds/installs the project for the given
        tag.

        This requires an active config context (`async with config:`), and is intended to
        run after `sync()` so generated artifacts are available before invoking build
        backends.

        Parameters
        ----------
        tag : str
            The active image tag for this build.

        Raises
        ------
        RuntimeError
            If called outside an image context or without an active config context.
        OSError
            If required config state is missing, tag/group resolution fails.
        CommandError
            If a build command fails.
        """
        from .bertrand import Bertrand
        from .python import PyProject

        if not inside_image():
            raise RuntimeError("build() requires access to a container filesystem")
        if not self:
            raise RuntimeError("build() requires an active config context")
        python = self.get(PyProject)
        bertrand = self.get(Bertrand)
        if python is None:
            raise OSError("build() requires parsed 'pyproject' configuration")
        if bertrand is None:
            raise OSError("build() requires parsed 'bertrand' configuration")
        tags = {entry.tag for entry in bertrand.tags}
        if tag not in tags:
            raise OSError(
                f"build() received unknown active tag '{tag}' (declared tags: "
                f"{', '.join(sorted(repr(name) for name in tags))})"
            )
        groups = python.project.optional_dependencies
        if tag not in groups:
            raise OSError(
                "build() requires matching [project.optional-dependencies] group for "
                f"active tag '{tag}'"
            )

        # form 1-step sync command
        sync_cmd = [
            "uv",
            "sync",
            "--locked",
            "--system",  # install into system Python
            "--inexact",  # preserve existing compatible dependencies where possible
            "--no-default-groups",  # don't install any extras
            "--no-dev",  # don't install extra dependency groups
            "--extra", tag,  # only install the group matching the active tag
            "--no-build-isolation-package", python.project.name,  # no isolation
        ]
        if not inside_container():
            sync_cmd.append("--no-editable")  # image build context -> non-editable

        async with lock_worktree(self.worktree):
            await run(["uv", "lock"], cwd=self.worktree)  # update lockfile
            await run(sync_cmd, cwd=self.worktree)  # orchestrate build
