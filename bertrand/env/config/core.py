"""Define project configuration resource plumbing."""

from __future__ import annotations

import re
import string
import uuid
from collections.abc import AsyncIterator, Callable, Sequence
from contextlib import asynccontextmanager
from dataclasses import dataclass, field
from functools import total_ordering
from pathlib import Path, PosixPath
from typing import (
    TYPE_CHECKING,
    Annotated,
    Any,
    ClassVar,
    Self,
    cast,
    get_args,
    get_origin,
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

from bertrand.env.git import NO_DEADLINE, Deadline, GitRepository
from bertrand.env.kube.lock.cluster import ClusterLock

if TYPE_CHECKING:
    from types import TracebackType

    from bertrand.env.kube.api.client import Kube

CACHE_MOUNT: PosixPath = PosixPath("/tmp/.cache")
SNAKE_CASE_RE = re.compile(r"^([a-zA-Z]([a-zA-Z0-9_]*[a-zA-Z0-9])?)?$")
LOWER_SNAKE_CASE_RE = re.compile(r"^([a-z]([a-z0-9_]*[a-z0-9])?)?$")
UPPER_SNAKE_CASE_RE = re.compile(r"^([A-Z]([A-Z0-9_]*[A-Z0-9])?)?$")
TOML_KEY_RE = re.compile(r"^[a-zA-Z]([a-zA-Z0-9_-]*[a-zA-Z0-9])?$")
KUBE_NAME_RE = re.compile(r"^[a-z0-9]([a-z0-9.-]*[a-z0-9])?$")
KUBE_SANITIZE_RE = re.compile(r"[^a-z0-9.-]+")
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


def _check_kube_name(value: str) -> str:
    if not KUBE_NAME_RE.fullmatch(value):
        msg = (
            f"invalid Kubernetes name: {value!r} (must be lowercase alphanumeric, "
            "'.', or '-', must not be empty, and must start with a letter and end with "
            "a letter or number)"
        )
        raise ValueError(msg)
    return value


def _check_uuid(value: str) -> str:
    try:
        return uuid.UUID(value).hex
    except ValueError as err:
        msg = f"invalid UUID hex string: {value}"
        raise ValueError(msg) from err


def _metadata_lock_key(repo: GitRepository, worktree: Path) -> str:
    return f"config:{repo.root}:{worktree}:metadata"


def _check_glob(pattern: str) -> str:
    if not GLOB_RE.fullmatch(pattern):
        msg = f"invalid glob pattern: '{pattern}'"
        raise ValueError(msg)
    if pattern.startswith("/"):
        msg = f"glob pattern cannot be absolute: '{pattern}'"
        raise ValueError(msg)
    if any(part in ("..", ".") for part in pattern.split("/")):
        msg = f"glob pattern cannot contain '.' or '..' segments: '{pattern}'"
        raise ValueError(msg)
    return pattern


def _check_absolute_path[PathT: Path](path: PathT) -> PathT:
    if not path.is_absolute():
        msg = f"path must be absolute: '{path}'"
        raise ValueError(msg)
    parts = path.parts
    if not parts:
        msg = "path cannot be empty"
        raise ValueError(msg)
    if any(p == "." or p == ".." for p in parts):
        msg = f"path cannot contain '.' or '..' segments: '{path}'"
        raise ValueError(msg)
    return path


def _check_relative_path(path: PosixPath) -> PosixPath:
    if path.is_absolute():
        msg = f"path cannot be absolute: '{path}'"
        raise ValueError(msg)
    parts = path.parts
    if not parts:
        msg = "path cannot be empty"
        raise ValueError(msg)
    if any(p == "." or p == ".." for p in parts):
        msg = f"path cannot contain '.' or '..' segments: '{path}'"
        raise ValueError(msg)
    return path


def _check_regex_pattern(value: str) -> str:
    try:
        re.compile(value)
    except re.error as err:
        msg = f"invalid regex pattern '{value}': {err}"
        raise ValueError(msg) from err
    return value


def _check_url(url: str) -> str:
    try:
        return str(HTTP_URL.validate_python(url))
    except ValidationError as err:
        msg = f"invalid URL: {url}"
        raise ValueError(msg) from err


def _check_url_label(label: str) -> str:
    chars_to_remove = string.punctuation + string.whitespace
    removal_map = str.maketrans("", "", chars_to_remove)
    return label.translate(removal_map).lower()


def _check_oci_image_ref(value: str) -> str:
    # match regex
    ref = value.strip()
    if not ref:
        msg = "OCI image reference cannot be empty"
        raise ValueError(msg)
    match = OCI_IMAGE_REF_RE.fullmatch(ref)
    if match is None:
        msg = f"invalid OCI image reference: '{ref}'"
        raise ValueError(msg)

    # verify OCI registry
    registry = match.group("registry")
    host, sep, port = registry.rpartition(":")
    if not sep:  # no port
        host = port
        port = ""
    if not host or (port and not port.isdigit()):
        msg = f"invalid registry host/port in OCI image reference: '{ref}'"
        raise ValueError(msg)
    if not all(OCI_HOST_LABEL_RE.fullmatch(part) for part in host.split(".")):
        msg = f"invalid registry host in OCI image reference: '{ref}'"
        raise ValueError(msg)
    if host != "localhost" and "." not in host and not port:
        msg = (
            "OCI image reference registry must be explicit: use 'localhost', "
            "a dotted hostname, or a host with ':<port>'"
        )
        raise ValueError(msg)

    # enforce at least one of tag or digest to ensure portability
    if match.group("tag") is None and match.group("digest") is None:
        msg = (
            f"OCI image reference must include a tag or sha256 digest pin (got '{ref}')"
        )
        raise ValueError(msg)
    return ref


type NonEmpty[SequenceT: Sequence[Any]] = Annotated[SequenceT, Field(min_length=1)]
type Unique[SequenceT: Sequence[Any]] = Annotated[
    SequenceT, AfterValidator(lambda x: len(set(x)) == len(x))
]
type Trimmed = Annotated[str, StringConstraints(strip_whitespace=True)]
type NoCRLF = Annotated[
    str, StringConstraints(strip_whitespace=True, pattern=r"^[^\r\n]*$")
]
type NoWhiteSpace = Annotated[
    str, StringConstraints(strip_whitespace=True, pattern=r"^\S*$")
]
type SnakeCase = Annotated[
    str, StringConstraints(strip_whitespace=True, pattern=SNAKE_CASE_RE.pattern)
]
type LowerSnakeCase = Annotated[
    str, StringConstraints(strip_whitespace=True, pattern=LOWER_SNAKE_CASE_RE.pattern)
]
type UpperSnakeCase = Annotated[
    str, StringConstraints(strip_whitespace=True, pattern=UPPER_SNAKE_CASE_RE.pattern)
]
type TOMLKey = Annotated[
    str,
    StringConstraints(strip_whitespace=True, min_length=1, pattern=TOML_KEY_RE.pattern),
]
type KubeName = Annotated[
    str,
    StringConstraints(
        strip_whitespace=True, min_length=1, pattern=KUBE_NAME_RE.pattern
    ),
]
type UUIDHex = Annotated[str, AfterValidator(_check_uuid)]
type Glob = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_glob)]
type AbsolutePath = Annotated[Path, AfterValidator(_check_absolute_path)]
type AbsolutePosixPath = Annotated[PosixPath, AfterValidator(_check_absolute_path)]
type RelativePath = Annotated[Path, AfterValidator(_check_relative_path)]
type RelativePosixPath = Annotated[PosixPath, AfterValidator(_check_relative_path)]
type RegexPattern = Annotated[NonEmpty[NoCRLF], AfterValidator(_check_regex_pattern)]
type ResourceName = Annotated[
    str,
    StringConstraints(
        strip_whitespace=True, min_length=1, pattern=RESOURCE_NAME_RE.pattern
    ),
]
type URL = Annotated[NonEmpty[NoCRLF], AfterValidator(_check_url)]
type URLLabel = Annotated[NonEmpty[Trimmed], AfterValidator(_check_url_label)]
type OCIImageRef = Annotated[
    NonEmpty[NoWhiteSpace], AfterValidator(_check_oci_image_ref)
]


def dump_yaml(payload: dict[str, Any], *, resource_name: str) -> str:
    """Serialize a mapping to YAML text.

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
        msg = f"failed to serialize YAML payload for resource '{resource_name}': {err}"
        raise OSError(msg) from err
    if not text.endswith("\n"):
        text += "\n"
    return text


@total_ordering
class Resource[ModelT: BaseModel]:
    """Represent one parseable or renderable configuration entity.

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

    name: ClassVar[ResourceName]
    paths: ClassVar[frozenset[RelativePath]]
    _model: ClassVar[type[BaseModel]] = BaseModel

    def __init_subclass__(cls) -> None:
        """Capture the concrete validation model from the resource type argument.

        Raises
        ------
        TypeError
            If the resource's generic argument is not a Pydantic model subclass.
        """
        super().__init_subclass__()
        cls._model = BaseModel
        for base in getattr(cls, "__orig_bases__", ()):
            if get_origin(base) is not Resource:
                continue
            args = get_args(base)
            if len(args) != 1:
                break
            model = args[0]
            if isinstance(model, type) and issubclass(model, BaseModel):
                cls._model = model
                return
            msg = (
                f"resource {cls.__name__} must parameterize Resource with a "
                "Pydantic BaseModel subclass"
            )
            raise TypeError(msg)

    def __hash__(self) -> int:
        """Hash by resource name.

        Returns
        -------
        int
            Hash of the resource name.
        """
        return hash(self.name)

    def __lt__(self, other: object) -> bool:
        """Compare resource order by name.

        Returns
        -------
        bool
            True if this resource sorts before `other`.
        """
        if isinstance(other, Resource):
            return self.name < other.name
        if isinstance(other, str):
            return self.name < other
        return NotImplemented

    def __eq__(self, other: object) -> bool:
        """Compare resource equality by name.

        Returns
        -------
        bool
            True if `other` identifies the same resource.
        """
        if isinstance(other, Resource):
            return self.name == other.name
        if isinstance(other, str):
            return self.name == other
        return NotImplemented

    async def init(
        self,
        config: Config,  # noqa: ARG002
        cli: Config.Init,  # noqa: ARG002
    ) -> dict[str, Any]:
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
        model = type(self)._model
        if model is not BaseModel:
            return model.model_construct().model_dump(by_alias=True)
        return {}

    async def parse(
        self,
        config: Config,  # noqa: ARG002
    ) -> dict[str, dict[str, Any]]:
        """Extract normalized config data from this resource.

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

    async def validate(
        self,
        config: Config,  # noqa: ARG002
        fragment: Any,
    ) -> ModelT | None:
        """Validate merged `parse()` output for this resource.

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

        Notes
        -----
        If a Pydantic model is returned, then it means this resource should be added to
        the `Config` resource list, even if it is not currently present on disk.  This
        is what allows resources mentioned in config to always be rendered during
        `sync()`, even if their original source files are missing.
        """
        model = type(self)._model
        if model is not BaseModel:
            return cast("ModelT", model.model_validate(fragment))
        return None

    async def render(
        self,
        config: Config,
        *,
        image_build: bool,
        deadline: Deadline,
    ) -> None:
        """Write derived content for this resource during `Config.sync()`.

        Parameters
        ----------
        config : Config
            The active configuration context, which provides access to the valid
            outputs from the `validate()` phase.
        image_build : bool
            Whether this hook is being invoked from an image-build or dev-container
            context. Hooks must use this to choose either source-tree artifacts
            (`False`) or container-local artifacts (`True`), never both.
        deadline : Deadline
            Operation deadline for render hooks that touch shared cluster state.

        Notes
        -----
        This is used to generate derived artifacts from a validated config without
        coupling to any particular output schema.
        """

    async def schema(self) -> dict[str, Any] | None:
        """Return a JSON Schema description for this resource, if available.

        Returns
        -------
        dict[str, Any] | None
            The resource's validation-mode JSON Schema generated from its captured
            Pydantic model using alias-aware keys, or None for output-only resources.

        Notes
        -----
        This is internal docs infrastructure used to expose authoritative resource
        schemas without coupling to CLI/docsite export behavior.
        """
        model = type(self)._model
        if model is not BaseModel:
            return model.model_json_schema(by_alias=True, mode="validation")
        return None


RESOURCES: set[Resource[Any]] = set()
RESOURCE_NAMES: dict[ResourceName, Resource[Any]] = {}
RESOURCE_PATHS: dict[RelativePath, Resource[Any]] = {}
HOOK_ERRORS: tuple[type[Exception], ...] = (
    OSError,
    ValueError,
    RuntimeError,
    ValidationError,
    yaml.YAMLError,
)


def resource[ResourceT: Resource[Any]](
    name: ResourceName,
    *,
    paths: set[RelativePath] | frozenset[RelativePath] = frozenset(),
) -> Callable[[type[ResourceT]], type[ResourceT]]:
    """Define a layout resource class decorator.

    See `Resource` for more details on the parameters and intended semantics of
    layout resources.

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

    """

    def _decorator(cls: type[ResourceT]) -> type[ResourceT]:
        self = cls()

        # reserve name
        if not RESOURCE_NAME_RE.fullmatch(name):
            msg = (
                f"invalid resource name {name!r} (must match regex "
                f"{RESOURCE_NAME_RE.pattern})"
            )
            raise TypeError(msg)
        if RESOURCE_NAMES.setdefault(name, self) is not self:
            msg = f"duplicate resource name: {name!r}"
            raise TypeError(msg)

        # reserve paths
        for path in paths:
            if path.is_absolute():
                msg = f"invalid resource path '{path}': must be relative"
                raise TypeError(msg)
            if any(part == ".." for part in path.parts):
                msg = f"invalid resource path '{path}': cannot contain '..' segments"
                raise TypeError(msg)
            other = RESOURCE_PATHS.setdefault(path, self)
            if other is not self:
                msg = (
                    f"duplicate resource path maps to both {name!r} and "
                    f"{other.name!r}: {path}"
                )
                raise TypeError(msg)

        # stamp variables at class level to simplify `Config.get(T)`
        cls.name = name
        cls.paths = frozenset(paths)
        RESOURCES.add(self)
        return cls

    return _decorator


@dataclass
class Config:
    """Represent resource placements and parsed config data.

    The context holds a read-only view over a worktree without coupling to any
    particular resource schema.
    """

    @dataclass(frozen=True)
    class Init:
        """Represent normalized `bertrand init` input.

        This context is passed to each resource's `init()` hook to drive initial
        values.

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
    kube: Kube | None = field(repr=False)
    resources: dict[ResourceName, BaseModel | None] = field(
        default_factory=lambda: {"bertrand": None}
    )
    init: Init | None = field(default=None, repr=False)
    _entered: int = field(default=0, repr=False)

    @property
    def root(self) -> AbsolutePath:
        """Return the absolute environment root.

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
        root: Path,
        *,
        kube: Kube | None = None,
        repo: GitRepository | None = None,
        deadline: Deadline = NO_DEADLINE,
    ) -> Self:
        """Load a worktree or standalone configuration root.

        This scans the environment root for known resource placements based on their
        managed paths, then resolves collisions and invalid placements.  If `root`
        belongs to a Git repository, then the resulting config is scoped to that
        repository worktree.  Otherwise, `root` is loaded as a standalone config tree
        for offline artifact-rendering contexts.

        Parameters
        ----------
        root : Path
            The root path of the environment directory.
        kube : Kube | None, optional
            Active Kubernetes API context used for cluster-wide metadata locking.  If
            omitted, config parsing and rendering run without acquiring a cluster lock.
        repo : GitRepository | None, optional
            An optional parent git repository containing `root`, which determines the
            project root for the environment.  If not provided, then it will be inferred
            from `root` when possible.  Missing repositories select standalone mode.
        deadline : Deadline, optional
            Operation deadline for acquiring the worktree metadata lock.

        Returns
        -------
        Self
            A resolved `Config` instance containing the discovered resources.  This
            instance must be entered as a context manager to parse and validate config
            data from the discovered resources, and to make that data available as
            attributes on the instance, which are outside the scope of this method.

        Raises
        ------
        FileNotFoundError
            If standalone `root` does not exist.
        NotADirectoryError
            If standalone `root` is not a directory.
        ValueError
            If an explicit or discovered Git repository does not contain `root` as a
            valid worktree.

        """
        root = root.expanduser().resolve()
        resources = {
            r.name: None
            for r in RESOURCES
            if r.paths and all((root / p).exists() for p in r.paths)
        }
        if repo is None:
            if not root.exists():
                msg = f"config root does not exist: {root}"
                raise FileNotFoundError(msg)
            if not root.is_dir():
                msg = f"config root is not a directory: {root}"
                raise NotADirectoryError(msg)
            repo = await GitRepository.discover(root)
            if repo is None:
                self = cls(
                    repo=GitRepository(root / ".git"),
                    worktree=Path(),
                    kube=kube,
                )
                self.resources.update(resources)
                async with self._metadata_lock(deadline):
                    return self

        if not any(wt.path == root for wt in await repo.worktrees()):
            msg = (
                f"worktree {root} is not a valid worktree for repository at {repo.root}"
            )
            raise ValueError(msg)
        if not root.is_relative_to(repo.root):
            msg = (
                f"worktree {root} is not a subdirectory of repository root at "
                f"{repo.root}"
            )
            raise ValueError(msg)

        self = cls(
            repo=repo,
            worktree=root.relative_to(repo.root),
            kube=kube,
        )
        self.resources.update(resources)
        async with self._metadata_lock(deadline):
            return self

    @asynccontextmanager
    async def _metadata_lock(self, deadline: Deadline) -> AsyncIterator[None]:
        if self.kube is None:
            yield
            return
        lock = ClusterLock(self.kube, _metadata_lock_key(self.repo, self.root))
        await lock.lock(deadline)
        try:
            yield
        finally:
            await lock.unlock(ignore_errors=True)

    def _merge_fragment(
        self,
        r: Resource[Any],
        fragment: dict[Any, Any],
        snapshot: dict[str, Any],
        *,
        key_owner: dict[tuple[str, ...], str],
        path_prefix: tuple[str, ...],
    ) -> None:
        for key, value in fragment.items():
            if not isinstance(key, str):
                parent = ".".join(path_prefix)
                msg = (
                    f"parse hook for resource '{r.name}' returned non-string key "
                    f"under '{parent}': '{key}'"
                )
                raise OSError(msg)
            value_is_map = isinstance(value, dict)

            # reserve ownership to prevent collisions with other parsed resources.
            # Note that the default values provided by `init()` hooks are not
            # considered, and will therefore be overwritten
            key_path = (*path_prefix, key)
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
                msg = (
                    f"config parse key collision at '{'.'.join(key_path)}' between "
                    f"resources '{owner}' and '{r.name}'"
                )
                raise OSError(msg)
            else:
                snapshot[key] = value

    async def _init_snapshot(self) -> dict[str, Any]:
        if self.init is None:
            return {}
        return {
            name: await RESOURCE_NAMES[name].init(self, self.init)
            for name in sorted(self.resources)
        }

    async def _parse_resource(
        self,
        r: Resource[Any],
    ) -> dict[str, dict[str, Any]]:
        try:
            fragment = await r.parse(self)
        except HOOK_ERRORS as err:
            msg = f"failed to parse resource {r.name!r}: {err}"
            raise OSError(msg) from err
        if not isinstance(fragment, dict):
            msg = (
                f"parse hook for resource {r.name!r} must return a string "
                f"mapping: {fragment}"
            )
            raise OSError(msg)
        return fragment

    def _merge_resource_snapshot(
        self,
        r: Resource[Any],
        fragment: dict[str, dict[str, Any]],
        snapshot: dict[str, Any],
        key_owner: dict[tuple[str, ...], ResourceName],
    ) -> None:
        for raw_key, table in fragment.items():
            if not isinstance(raw_key, str):
                msg = (
                    f"parse hook for resource {r.name!r} returned "
                    f"non-string key: {raw_key}"
                )
                raise OSError(msg)
            if not isinstance(table, dict):
                msg = (
                    f"parse hook for resource {r.name!r} returned "
                    f"non-mapping value for key '{raw_key}': {table}"
                )
                raise OSError(msg)
            lookup = RESOURCE_NAMES.get(raw_key)
            if lookup is not None:
                self._merge_fragment(
                    r,
                    table,
                    snapshot.setdefault(lookup.name, {}),
                    key_owner=key_owner,
                    path_prefix=(lookup.name,),
                )

    async def _parse_snapshot(self, snapshot: dict[str, Any]) -> None:
        key_owner: dict[tuple[str, ...], ResourceName] = {}
        for name in sorted(self.resources):
            r = RESOURCE_NAMES[name]
            self._merge_resource_snapshot(
                r,
                await self._parse_resource(r),
                snapshot,
                key_owner,
            )

    async def _validate_snapshot(self, snapshot: dict[str, Any]) -> None:
        for key, table in snapshot.items():
            lookup = RESOURCE_NAMES.get(key)
            if lookup is None:
                continue

            model = await lookup.validate(self, table)
            if self.resources.get(lookup.name) is not None:
                msg = (
                    f"config validation collision for resource '{lookup.name}': "
                    "multiple resources writing to the same top-level table"
                )
                raise OSError(msg)
            self.resources[lookup.name] = model

    async def _enter(self, deadline: Deadline) -> Self:
        """Parse and validate resource config data.

        The parsed snapshot remains valid until the outermost context is exited.

        Returns
        -------
        Self
            The active configuration context.

        """
        if self._entered > 0:  # re-entrant case
            self._entered += 1
            return self

        old_resources = self.resources.copy()
        try:
            async with self._metadata_lock(deadline):
                snapshot = await self._init_snapshot()
                await self._parse_snapshot(snapshot)
                await self._validate_snapshot(snapshot)

                self._entered += 1
                return self
        except BaseException:
            self.resources = old_resources
            self._entered = 0
            raise

    async def __aenter__(self) -> Self:
        """Parse and validate resource config data without a deadline.

        Returns
        -------
        Self
            The active configuration context.
        """
        return await self._enter(NO_DEADLINE)

    @asynccontextmanager
    async def activate(
        self, *, deadline: Deadline = NO_DEADLINE
    ) -> AsyncIterator[Self]:
        """Parse and validate resource config data under an operation deadline.

        Parameters
        ----------
        deadline : Deadline, optional
            Operation deadline for cluster metadata locking during parse/validation.

        Yields
        ------
        Self
            The active configuration context.
        """
        await self._enter(deadline)
        try:
            yield self
        finally:
            await self.__aexit__(None, None, None)

    async def __aexit__(
        self,
        exc_type: type[BaseException] | None,
        exc: BaseException | None,
        traceback: TracebackType | None,
    ) -> None:
        """Release one context level and clear snapshot on outermost exit.

        Raises
        ------
        RuntimeError
            If the config context is not active.
        """
        if self._entered <= 0:
            msg = "layout context is not active"
            raise RuntimeError(msg)
        self._entered -= 1
        if self._entered == 0:
            self.resources = dict.fromkeys(self.resources)

    def __bool__(self) -> bool:
        """Return whether the config context is active.

        Returns
        -------
        bool
            True when inside an active config context.
        """
        return self._entered > 0

    def __hash__(self) -> int:
        """Hash by environment root.

        Returns
        -------
        int
            Hash of the absolute environment root.
        """
        return hash(self.root)

    def __eq__(self, other: object) -> bool:
        """Compare configuration equality by environment root.

        Returns
        -------
        bool
            True if both configs refer to the same environment root.
        """
        if isinstance(other, Config):
            return self.root == other.root
        return NotImplemented

    def __contains__(
        self, key: ResourceName | Resource[Any] | type[Resource[Any]]
    ) -> bool:
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
        if isinstance(key, Resource) or (
            isinstance(key, type) and issubclass(key, Resource)
        ):
            key = key.name
        return key in self.resources

    def get[ResourceModel: BaseModel](
        self,
        r: Resource[ResourceModel] | type[Resource[ResourceModel]],
    ) -> ResourceModel | None:
        """Retrieve the parsed config model for the given resource ID.

        This assumes the resource is present in the environment.

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
        return cast("ResourceModel | None", self.resources.get(r.name))

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

    async def sync(
        self,
        *,
        image_build: bool = False,
        deadline: Deadline = NO_DEADLINE,
    ) -> None:
        """Render and write derived artifact resources from active context snapshot.

        This requires an active config context (`async with config.activate(...)`).

        Parameters
        ----------
        image_build : bool, optional
            Whether this render pass is preparing an image-build context.  Normal
            sync calls render only worktree artifacts; image-build sync calls render
            only container-local artifacts and must not mutate source files.
        deadline : Deadline, optional
            Operation deadline for cluster metadata locking and render hooks.

        Raises
        ------
        RuntimeError
            If called outside a a Bertrand image or active config context.
        OSError
            If any render hooks fail.
        """
        if not self:
            msg = "sync() artifact rendering requires an active config context"
            raise RuntimeError(msg)

        # invoke render hooks for all resources in deterministic order
        async with self._metadata_lock(deadline):
            for name in sorted(self.resources):
                r = RESOURCE_NAMES[name]
                try:
                    await r.render(self, image_build=image_build, deadline=deadline)
                except HOOK_ERRORS as err:
                    msg = f"failed to render resource '{r.name}': {err}"
                    raise OSError(msg) from err
