"""TODO"""
from __future__ import annotations

import importlib.resources as importlib_resources
import re
import string
from collections.abc import Callable, Mapping, Sequence
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
    METADATA_LOCK,
    TIMEOUT,
    GitRepository,
    Lock,
    inside_container,
    inside_image,
    run,
)

CACHE_MOUNT: PosixPath = PosixPath("/tmp/.cache")
HTTP_URL: TypeAdapter[AnyHttpUrl] = TypeAdapter(AnyHttpUrl)
GLOB_RE = re.compile(r"^[A-Za-z0-9._/\-\*\?\[\]!]+$")
RESOURCE_NAME_RE = re.compile(r"^[a-z]([a-z0-9_.-]*[a-z0-9])?$")
OCI_HOST_LABEL_RE = re.compile(r"^[a-z0-9](?:[a-z0-9-]*[a-z0-9])?$")
OCI_REPO_COMPONENT_PATTERN = r"[a-z0-9]+(?:(?:[._]|__|-+)[a-z0-9]+)*"
OCI_IMAGE_REF_RE = re.compile(
    rf"^(?P<registry>[^/@\s]+)/"
    rf"(?P<path>{OCI_REPO_COMPONENT_PATTERN}(?:/{OCI_REPO_COMPONENT_PATTERN})*)"
    r"(?::(?P<tag>[A-Za-z0-9_][A-Za-z0-9_.-]{0,127}))?"
    r"(?:@(?P<digest>sha256:[0-9a-f]{64}))?$"
)
SANITIZE_RE = re.compile(r"[^a-zA-Z0-9._]+")


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


def _check_oci_image_ref(value: str) -> str:
    # match regex
    ref = value.strip()
    if not ref:
        raise ValueError("OCI image reference cannot be empty")
    match = OCI_IMAGE_REF_RE.fullmatch(ref)
    if match is None:
        raise ValueError(f"invalid OCI image reference: '{ref}'")

    # verify OCI registry
    registry = match.group("registry")
    host, sep, port = registry.rpartition(":")
    if not sep:  # no port
        host = port
        port = ""
    if not host or (port and not port.isdigit()):
        raise ValueError(f"invalid registry host/port in OCI image reference: '{ref}'")
    if not all(
        OCI_HOST_LABEL_RE.fullmatch(part) for part in host.split(".")
    ):
        raise ValueError(f"invalid registry host in OCI image reference: '{ref}'")
    if host != "localhost" and "." not in host and not port:
        raise ValueError(
            "OCI image reference registry must be explicit: use 'localhost', "
            "a dotted hostname, or a host with ':<port>'"
        )

    # enforce at least one of tag or digest to ensure portability
    if match.group("tag") is None and match.group("digest") is None:
        raise ValueError(
            "OCI image reference must include a tag or sha256 digest pin "
            f"(got '{ref}')"
        )
    return ref


type NonEmpty[SequenceT: Sequence[Any]] = Annotated[SequenceT, Field(min_length=1)]
type Unique[SequenceT: Sequence[Any]] = Annotated[
    SequenceT,
    AfterValidator(lambda x: len(set(x)) == len(x))
]
type Trimmed = Annotated[str, StringConstraints(strip_whitespace=True)]
type NoCRLF = Annotated[  # pylint: disable=invalid-name
    str,
    StringConstraints(strip_whitespace=True, pattern=r"^[^\r\n]*$")
]
type NoWhiteSpace = Annotated[
    str,
    StringConstraints(strip_whitespace=True, pattern=r"^\S*$")
]
type SnakeCase = Annotated[str, StringConstraints(
    strip_whitespace=True,
    pattern=r"^([a-zA-Z]([a-zA-Z0-9_]*[a-zA-Z0-9])?)?$"
)]
type LowerSnakeCase = Annotated[str, StringConstraints(
    strip_whitespace=True,
    pattern=r"^([a-z]([a-z0-9_]*[a-z0-9])?)?$"
)]
type UpperSnakeCase = Annotated[str, StringConstraints(
    strip_whitespace=True,
    pattern=r"^([A-Z]([A-Z0-9_]*[A-Z0-9])?)?$"
)]
type TOMLKey = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    pattern=r"^[a-zA-Z]([a-zA-Z0-9_-]*[a-zA-Z0-9])?$"
)]
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
type OCIImageRef = Annotated[
    NonEmpty[NoWhiteSpace],
    AfterValidator(_check_oci_image_ref)
]


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

    async def render(self, config: Config, tag: TOMLKey | None) -> None:
        """A render function that writes content for this resource during
        `Config.sync()`.

        Parameters
        ----------
        config : Config
            The active configuration context, which provides access to the valid
            outputs from the `validate()` phase.
        tag : str | None
            The active image tag for the configured environment, which is used to
            search the `config.get(Bertrand).image` table for tag-specific overrides
            during image builds.  If None, then it means this hook was invoked during
            a `bertrand init` command, and should therefore not attempt to render any
            out-of-tree artifacts that would require access to a container filesystem.

        Notes
        -----
        This is used to generate derived artifacts from a validated config without
        coupling to any particular output schema.
        """

    @dataclass(frozen=True)
    class Volume:
        """A resource-owned cache volume declaration used for build and runtime args.

        Attributes
        ----------
        target : PosixPath
            Absolute in-container mount target.
        fingerprint : Mapping[str, Any]
            JSON-compatible semantic payload that determines cache coherence for this
            volume.
        """

        target: PosixPath
        fingerprint: Mapping[str, Any]

    async def volumes(self, config: Config, tag: TOMLKey) -> list[Resource.Volume]:
        """Declare resource-owned cache volumes for a given image tag.

        Parameters
        ----------
        config : Config
            The active configuration context.
        tag : str
            The active image tag.

        Returns
        -------
        list[Resource.Volume]
            A list of volume declarations owned by this resource.  Empty by default.
        """
        return []

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
        RESOURCES.add(self)
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

    repo: GitRepository
    worktree: RelativePath
    timeout: float
    resources: dict[ResourceName, BaseModel | None] = field(
        default_factory=lambda: {"bertrand": None}
    )
    init: Init | None = field(default=None, repr=False)
    _entered: int = field(default=0, repr=False)

    @property
    def root(self) -> AbsolutePath:
        """
        Returns
        -------
        AbsolutePath
            The absolute path to the root of the environment, which concatenates the
            repository root and the worktree relative path.
        """
        return self.repo.root / self.worktree

    @classmethod
    async def load(
        cls,
        worktree: Path,
        *,
        repo: GitRepository | None = None,
        timeout: float = TIMEOUT
    ) -> Self:
        """Load a worktree configuration by scanning the environment root for known
        resource placements based on their managed paths, and resolving any collisions
        or invalid placements.

        Parameters
        ----------
        worktree : Path
            The root path of the environment directory.
        repo : GitRepository | None, optional
            An optional parent git repository containing the worktree, which determines
            the project root for the environment.  If not provided, then it will be
            inferred from `worktree`, which must include a repository as a parent.
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
        if repo is None:
            repo = await GitRepository.discover(worktree)
            if repo is None:
                raise ValueError(f"no git repository found for worktree: {worktree}")
        if not any(wt.path == worktree for wt in await repo.worktrees()):
            raise ValueError(
                f"worktree {worktree} is not a valid worktree for repository at "
                f"{repo.root}"
            )
        if not worktree.is_relative_to(repo.root):
            raise ValueError(
                f"worktree {worktree} is not a subdirectory of repository root at "
                f"{repo.root}"
            )

        async with Lock(worktree / METADATA_LOCK, timeout=timeout, mode="cluster"):
            self = cls(
                repo=repo,
                worktree=worktree.relative_to(repo.root),
                timeout=timeout
            )
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
            async with Lock(self.root / METADATA_LOCK, timeout=TIMEOUT, mode="cluster"):
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

    def __hash__(self) -> int:
        return hash(self.root)

    def __eq__(self, other: object) -> bool:
        if isinstance(other, Config):
            return self.root == other.root
        return NotImplemented

    def __contains__(self, key: ResourceName | Resource | type[Resource]) -> bool:
        """Check if a resource ID is present in the environment.

        Parameters
        ----------
        key : ResourceName | Resource | type[Resource]
            The stable identifier of the resource to check for, as defined in the
            global catalog.

        Returns
        -------
        bool
            True if the referenced resource is present in the environment, False
            otherwise.
        """
        if isinstance(key, Resource) or (isinstance(key, type) and issubclass(key, Resource)):
            key = key.name
        return key in self.resources

    def get(
        self,
        r: _ResourceLike[_ResourceModel_co] | type[_ResourceLike[_ResourceModel_co]]
    ) -> _ResourceModel_co | None:
        """Retrieve the parsed config model for the given resource ID, assuming it is
        present in the environment.

        Parameters
        ----------
        r : Resource | type[Resource]
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

    async def sync(self, tag: TOMLKey | None) -> None:
        """Render and write derived artifact resources from active context snapshot.

        This requires an active config context (`async with config:`).

        Parameters
        ----------
        tag : str | None
            The active image tag for the configured environment, which is used to
            search the `bertrand.image` table for tag-specific overrides during
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
        async with Lock(self.root / METADATA_LOCK, timeout=TIMEOUT, mode="cluster"):
            for name in sorted(self.resources):
                r = RESOURCE_NAMES[name]
                try:
                    await r.render(self, tag)
                except Exception as err:
                    raise OSError(f"failed to render resource '{r.name}': {err}") from err

    async def build(self, tag: TOMLKey) -> None:
        """Invoke Bertrand's PEP517 backend from within an image or container context.

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

        # confirm tag is declared and has a matching optional-dependencies group, which
        # is the simplest and most efficient way to get pip to install the correct set
        # of Python dependencies for this build, without needing a multi-stage build
        if tag not in bertrand.build:
            raise OSError(
                f"build() received unknown active tag '{tag}' (declared tags: "
                f"{', '.join(sorted(repr(name) for name in bertrand.build))})"
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

        # render output artifacts, update lockfile, and invoke PEP517/660 backend
        async with Lock(self.root / METADATA_LOCK, timeout=TIMEOUT, mode="cluster"):
            await self.sync(tag)  # render artifacts to container filesystem
            await run(["uv", "lock"], cwd=self.root)  # update lockfile
            await run(sync_cmd, cwd=self.root)  # orchestrate build
