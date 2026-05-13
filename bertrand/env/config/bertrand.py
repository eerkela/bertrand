"""Define Bertrand's project configuration resource.

The metadata for this resource is expected to be found under the `[bertrand]` key in
project configuration, which is usually provided by `pyproject.toml`.  It is
responsible for rendering the basic directory structure and container/repository
artifacts needed by Bertrand's core functionality.
"""

from __future__ import annotations

import ipaddress
import re
import tomllib
from pathlib import Path
from typing import TYPE_CHECKING, Annotated, Any, Literal, Self

import jinja2
import packaging.version
from pydantic import (
    AfterValidator,
    BaseModel,
    ConfigDict,
    Field,
    NonNegativeFloat,
    NonNegativeInt,
    StringConstraints,
    model_validator,
)

from bertrand.env.git import METADATA_DIR, Scalar, atomic_write_text
from bertrand.env.version import VERSION

from .conan import (
    ConanConf,
    ConanConfig,
    ConanOptions,
)
from .core import (
    SANITIZE_RE,
    AbsolutePosixPath,
    Config,
    Glob,
    KubeName,
    NoCRLF,
    NonEmpty,
    NoWhiteSpace,
    OCIImageRef,
    PosixPath,
    RelativePosixPath,
    Resource,
    SnakeCase,
    TOMLKey,
    Trimmed,
    UpperSnakeCase,
    locate_template,
    resource,
)
from .python import PyProject, _validate_dependency_groups

if TYPE_CHECKING:
    from collections.abc import Callable

# Configuration options that affect CLI behavior
DEFAULT_TAG: str = ""
SHELLS: dict[str, tuple[str, ...]] = {
    # NOTE: values are raw commands that override a container's normal entry point.
    "bash": ("bash", "-l"),
}
DEFAULT_SHELL: str = "bash"
if DEFAULT_SHELL not in SHELLS:
    msg = f"default shell is unsupported: {DEFAULT_SHELL}"
    raise RuntimeError(msg)
EDITORS: dict[str, list[str]] = {
    # NOTE: values are ordered lists of host commands/paths where the editor may be
    # found when servicing RPC requests.  The first entry that passes a `which` check
    # will be invoked together with the proper arguments to attach to the requested
    # container and mount its internal tools.
    "vscode": [
        # PATH-resolved command names
        "code",
        "code-insiders",
        "code.cmd",
        "code-insiders.cmd",
        "code.exe",
        "code-insiders.exe",
        # Linux common absolute install paths
        "/usr/bin/code",
        "/usr/local/bin/code",
        "/snap/bin/code",
        "/usr/share/code/bin/code",
        "/usr/share/code-insiders/bin/code-insiders",
        # macOS app bundle shims
        "/Applications/Visual Studio Code.app/Contents/Resources/app/bin/code",
        (
            "/Applications/Visual Studio Code - Insiders.app/Contents/Resources/app"
            "/bin/code-insiders"
        ),
        # WSL/Windows common locations
        "/mnt/c/Program Files/Microsoft VS Code/bin/code.cmd",
        "/mnt/c/Program Files/Microsoft VS Code Insiders/bin/code-insiders.cmd",
    ]
}
DEFAULT_EDITOR: str = "vscode"
if DEFAULT_EDITOR not in EDITORS:
    msg = f"default editor is unsupported: {DEFAULT_EDITOR}"
    raise RuntimeError(msg)
INSTRUMENTS: dict[str, Callable[[dict[str, Any]], Callable[[list[str]], list[str]]]] = {
    # NOTE: instruments are identified by a unique name, which limits what can appear
    # in a tag's `instruments` field as part of a configured build matrix.  The values
    # are functions that accept the instrument's configuration as a parsed mapping,
    # validate it, and then return another function that transforms the container's
    # entry point command (as a list of strings) before execution.
}


NS_PATH_RE = re.compile(r"^ns:\S+$")
NETWORK_ALIAS_LABEL_RE = re.compile(r"^(?!-)[a-z0-9-]{1,63}(?<!-)$")
USERNS_CONTAINER_REF_RE = re.compile(r"^[A-Za-z0-9._-]+$")
USERNS_MAPPING_RE = re.compile(r"^(?P<container>\d+):(?P<host>@?\d+):(?P<length>\d+)$")
CAPABILITY_TOKEN_RE = re.compile(r"^CAP_[A-Z0-9_]+$")
CAPABILITY_DEFINE_RE = re.compile(r"^\s*#define\s+(CAP_[A-Z0-9_]+)\s+([0-9]+)\b")
SECURITY_OPT_KEY_RE = re.compile(r"^[a-z0-9][a-z0-9_.-]*$")
BERTRAND_TAG_RE = re.compile(r"^(?:[a-zA-Z](?:[a-zA-Z0-9_-]*[a-zA-Z0-9])?)?$")
IMAGE_TAG_RE = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_.-]{0,127}$")
DEVICE_PERMISSIONS: frozenset[str] = frozenset({"r", "w", "m", "rw", "rm", "wm", "rwm"})
LINUX_CAPABILITY_HEADERS: tuple[Path, ...] = (
    Path("/usr/include/linux/capability.h"),
    Path("/usr/include/uapi/linux/capability.h"),
    Path("/usr/src/linux/include/uapi/linux/capability.h"),
)


def _load_linux_capabilities() -> frozenset[str] | None:
    for path in LINUX_CAPABILITY_HEADERS:
        if not path.is_file():
            continue
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except OSError:
            continue
        found = {
            match.group(1)
            for line in lines
            if (match := CAPABILITY_DEFINE_RE.match(line)) is not None
        }
        if found:
            return frozenset(found)
    return None


LINUX_CAPABILITIES: frozenset[str] | None = _load_linux_capabilities()


def _check_shell(shell: str) -> str:
    if shell not in SHELLS:
        msg = f"unsupported shell: '{shell}' (supported shells: {', '.join(SHELLS)})"
        raise ValueError(msg)
    return shell


def _check_editor(editor: str) -> str:
    if editor not in EDITORS:
        msg = (
            f"unsupported editor: '{editor}' (supported editors: {', '.join(EDITORS)})"
        )
        raise ValueError(msg)
    return editor


def _check_ignore_list(ignore: list[Glob]) -> list[Glob]:
    out: list[Glob] = []
    seen: set[Glob] = set()
    for pattern in ignore:
        if pattern in seen:
            continue
        out.append(pattern)
        seen.add(pattern)
    return out


def _check_ip_address(address: str) -> str:
    try:
        return str(ipaddress.ip_address(address))
    except ValueError as err:
        msg = f"invalid IP address: {address}"
        raise ValueError(msg) from err


def _check_network_alias(alias: str) -> str:
    alias = alias.lower()
    if alias.startswith(".") or alias.endswith(".") or ".." in alias:
        msg = (
            f"invalid network alias '{alias}' "
            "(cannot start/end with '.', or contain '..')"
        )
        raise ValueError(msg)
    for label in alias.split("."):
        if not NETWORK_ALIAS_LABEL_RE.fullmatch(label):
            msg = (
                f"invalid network alias label '{label}' in '{alias}' (labels must "
                "match [a-z0-9-], max length 63, and cannot start/end with '-')"
            )
            raise ValueError(msg)
    return alias


def _check_capability(capability: str) -> str:
    if capability == "ALL":
        return capability
    if not CAPABILITY_TOKEN_RE.fullmatch(capability):
        msg = (
            f"invalid capability token '{capability}' "
            "(expected exact CAP_* token or ALL)"
        )
        raise ValueError(msg)
    if LINUX_CAPABILITIES is not None and capability not in LINUX_CAPABILITIES:
        msg = (
            f"unknown Linux capability '{capability}' "
            "according to local capability header"
        )
        raise ValueError(msg)
    return capability


def _check_security_opt(option: str) -> str:
    if option == "no-new-privileges":
        return option
    if "=" not in option:
        msg = (
            f"invalid security-opt '{option}' "
            "(expected 'no-new-privileges' or 'key=value')"
        )
        raise ValueError(msg)
    key, value = option.split("=", maxsplit=1)
    if not key or not value:
        msg = f"invalid security-opt '{option}' (missing key or value)"
        raise ValueError(msg)
    if not SECURITY_OPT_KEY_RE.fullmatch(key):
        msg = f"invalid security-opt key '{key}' in '{option}'"
        raise ValueError(msg)
    return option


def _extract_container_ref(mode: str) -> str | None:
    if not mode.startswith("container:"):
        return None
    _, _, ref = mode.partition(":")
    return ref or None


def _check_userns_uint(
    *,
    userns: str,
    key: str,
    value: str,
    allow_zero: bool,
) -> None:
    if not value.isdigit():
        msg = f"invalid userns '{userns}': {key} must be a non-negative integer"
        raise ValueError(msg)
    number = int(value)
    if not allow_zero and number <= 0:
        msg = f"invalid userns '{userns}': {key} must be greater than zero"
        raise ValueError(msg)


def _check_userns_options(
    *,
    userns: str,
    mode: Literal["keep-id", "auto"],
    options: str,
) -> None:
    if not options:
        msg = f"invalid userns '{userns}': '{mode}' options cannot be empty"
        raise ValueError(msg)

    seen: set[str] = set()
    tokens = options.split(",")
    for token in tokens:
        if not token or "=" not in token:
            msg = (
                f"invalid userns '{userns}': expected comma-separated key=value options"
            )
            raise ValueError(msg)
        key, value = token.split("=", maxsplit=1)
        if not key or not value:
            msg = f"invalid userns '{userns}': expected non-empty key=value options"
            raise ValueError(msg)
        if key in seen:
            msg = f"invalid userns '{userns}': duplicate option key '{key}'"
            raise ValueError(msg)
        seen.add(key)

        if mode == "keep-id":
            if key in ("uid", "gid"):
                _check_userns_uint(userns=userns, key=key, value=value, allow_zero=True)
                continue
            if key == "size":
                _check_userns_uint(
                    userns=userns,
                    key=key,
                    value=value,
                    allow_zero=False,
                )
                continue
            msg = (
                f"invalid userns '{userns}': unsupported keep-id option '{key}' "
                "(allowed: uid, gid, size)"
            )
            raise ValueError(msg)

        if key == "size":
            _check_userns_uint(userns=userns, key=key, value=value, allow_zero=False)
            continue
        if key in ("uidmapping", "gidmapping"):
            match = USERNS_MAPPING_RE.fullmatch(value)
            if match is None:
                msg = (
                    f"invalid userns '{userns}': {key} must be "
                    "'<container-id>:<host-id>:<size>' or "
                    "'<container-id>:@<host-id>:<size>'"
                )
                raise ValueError(msg)
            length = int(match.group("length"))
            if length <= 0:
                msg = (
                    f"invalid userns '{userns}': {key} mapping size must be "
                    "greater than zero"
                )
                raise ValueError(msg)
            continue
        msg = (
            f"invalid userns '{userns}': unsupported auto option '{key}' "
            "(allowed: size, uidmapping, gidmapping)"
        )
        raise ValueError(msg)


def _check_userns(userns: str) -> str:
    if userns in ("host", "keep-id", "auto", "nomap"):
        return userns
    if userns.startswith("ns:"):
        if NS_PATH_RE.fullmatch(userns):
            return userns
        msg = f"invalid userns '{userns}' (expected 'ns:<path>' with no spaces)"
        raise ValueError(msg)
    if userns.startswith("container:"):
        ref = _extract_container_ref(userns)
        if ref is None:
            msg = (
                f"invalid userns '{userns}' "
                "(expected 'container:<tag>' with non-empty tag)"
            )
            raise ValueError(msg)
        if not USERNS_CONTAINER_REF_RE.fullmatch(ref):
            msg = f"invalid userns '{userns}' (container tag must use [A-Za-z0-9._-]+)"
            raise ValueError(msg)
        sanitized = SANITIZE_RE.sub("-", ref).strip("-")
        if ref != sanitized:
            msg = (
                f"invalid userns '{userns}' (container tag sanitizes to '{sanitized}')"
            )
            raise ValueError(msg)
        return userns
    if ":" in userns:
        mode, _, options = userns.partition(":")
        if mode == "keep-id":
            _check_userns_options(userns=userns, mode="keep-id", options=options)
            return userns
        if mode == "auto":
            _check_userns_options(userns=userns, mode="auto", options=options)
            return userns
        if mode == "nomap":
            msg = f"invalid userns '{userns}' ('nomap' does not accept options)"
            raise ValueError(msg)
    msg = (
        f"invalid userns '{userns}' (expected one of: host|keep-id[:<opts>]|"
        "auto[:<opts>]|nomap|container:<tag>|ns:<path>)"
    )
    raise ValueError(msg)


def _check_namespace_mode(
    mode: str,
    *,
    option: str,
    literals: tuple[str, ...],
    allow_empty: bool = False,
) -> str:
    if mode == "" and allow_empty:
        return mode
    if not mode:
        msg = f"{option} entry cannot be empty"
        raise ValueError(msg)
    if mode in literals:
        return mode
    if mode.startswith("ns:"):
        if NS_PATH_RE.fullmatch(mode):
            return mode
        msg = f"invalid {option} '{mode}' (expected 'ns:<path>' with no spaces)"
        raise ValueError(msg)
    ref = _extract_container_ref(mode)
    if ref is not None:
        if not USERNS_CONTAINER_REF_RE.fullmatch(ref):
            msg = f"invalid {option} '{mode}' (container tag must use [A-Za-z0-9._-]+)"
            raise ValueError(msg)
        sanitized = SANITIZE_RE.sub("-", ref).strip("-")
        if ref != sanitized:
            msg = (
                f"invalid {option} '{mode}' (container tag sanitizes to '{sanitized}')"
            )
            raise ValueError(msg)
        return mode
    expected = "|".join(literals)
    empty = '""|' if allow_empty else ""
    msg = (
        f"invalid {option} '{mode}' (expected one of: {empty}{expected}|"
        "container:<tag>|ns:<path>)"
    )
    raise ValueError(msg)


def _check_ipc(ipc: str) -> str:
    return _check_namespace_mode(
        ipc,
        option="ipc",
        literals=("none", "host", "private", "shareable"),
        allow_empty=True,
    )


def _check_pid(pid: str) -> str:
    return _check_namespace_mode(
        pid,
        option="pid",
        literals=("host", "private"),
        allow_empty=False,
    )


def _check_uts(uts: str) -> str:
    return _check_namespace_mode(
        uts,
        option="uts",
        literals=("host", "private"),
        allow_empty=False,
    )


def _check_device_permission(permission: str) -> str:
    if permission not in DEVICE_PERMISSIONS:
        msg = (
            f"invalid device permissions '{permission}' (expected one of: "
            f"{'|'.join(sorted(DEVICE_PERMISSIONS, key=lambda x: len(x)))}"
        )
        raise ValueError(msg)
    return permission


def _check_health_log_destination(value: str) -> str:
    if value in ("local", "events_logger"):
        return value
    if "\\" in value:
        msg = (
            f"invalid healthcheck.log.destination '{value}' "
            "(expected POSIX-style path separators)"
        )
        raise ValueError(msg)
    if any(part in (".", "..") for part in value.split("/")):
        msg = (
            f"invalid healthcheck.log.destination '{value}' "
            "(path cannot contain '.' or '..' segments)"
        )
        raise ValueError(msg)
    path = PosixPath(value)
    if path.is_absolute():
        msg = (
            f"invalid healthcheck.log.destination '{value}' "
            "(path must be project-root-relative)"
        )
        raise ValueError(msg)
    return path.as_posix()


def _check_build_context_path(path: PosixPath) -> PosixPath:
    if path.is_absolute():
        msg = f"build context path cannot be absolute: '{path}'"
        raise ValueError(msg)
    if path == PosixPath("."):
        return path
    parts = path.parts
    if not parts:
        msg = "build context path cannot be empty"
        raise ValueError(msg)
    if any(p == "." or p == ".." for p in parts):
        msg = f"build context path cannot contain '.' or '..' segments: '{path}'"
        raise ValueError(msg)
    return path


def project_image_tag(project_version: str, image_key: str) -> str:
    """Derive the OCI tag for a configured project image.

    Parameters
    ----------
    project_version : str
        Version from the worktree's ``[project].version`` field.
    image_key : str
        Key from the ``[tool.bertrand.image]`` table. The empty key is the default
        image and maps directly to ``project_version``.

    Returns
    -------
    str
        OCI tag used for the internal and external image manifests.

    Raises
    ------
    ValueError
        If the image key or derived OCI tag is invalid.
    """
    version = project_version.strip()
    key = image_key.strip()
    if not BERTRAND_TAG_RE.fullmatch(key):
        msg = f"invalid image key in 'tool.bertrand.image': {image_key!r}"
        raise ValueError(msg)
    tag = version if not key else f"{version}-{key}"
    if not IMAGE_TAG_RE.fullmatch(tag):
        msg = (
            "project version and image key do not form a valid OCI tag: "
            f"{project_version!r}, {image_key!r} -> {tag!r}"
        )
        raise ValueError(msg)
    return tag


def _project_version(config: Config, pyproject: PyProject.Model | None) -> str | None:
    if pyproject is not None:
        return pyproject.project.version
    path = config.root / "pyproject.toml"
    try:
        payload = tomllib.loads(path.read_text(encoding="utf-8"))
    except (OSError, tomllib.TOMLDecodeError):
        return None
    project = payload.get("project")
    if isinstance(project, dict):
        version = project.get("version")
        if isinstance(version, str):
            return version
    return None


def _image_key_label(tag: str) -> str:
    return '""' if tag == "" else tag


def _image_table(tag: str) -> str:
    return f'[tool.bertrand.image.{_image_key_label(tag)}]'


def _image_subtable(tag: str, name: str) -> str:
    return f'[tool.bertrand.image.{_image_key_label(tag)}.{name}]'


def _image_array_table(tag: str, name: str) -> str:
    return f'[[tool.bertrand.image.{_image_key_label(tag)}.{name}]]'


type Shell = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_shell)]
type Editor = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_editor)]
type IgnoreList = Annotated[list[Glob], AfterValidator(_check_ignore_list)]
type IPAddress = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_ip_address)]
type HostName = Annotated[
    str,
    StringConstraints(
        strip_whitespace=True,
        min_length=1,
        max_length=253,
        pattern=r"^[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?"
        r"(?:\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*$",
    ),
]
type HostIP = Literal["host-gateway"] | IPAddress  # pylint: disable=invalid-name
type BertrandTag = Annotated[
    str,
    StringConstraints(strip_whitespace=True, pattern=BERTRAND_TAG_RE.pattern),
]
type OCIImageTag = Annotated[
    str,
    StringConstraints(
        strip_whitespace=True,
        min_length=1,
        pattern=IMAGE_TAG_RE.pattern,
    ),
]
type NetworkAlias = Annotated[
    NonEmpty[NoWhiteSpace],
    AfterValidator(_check_network_alias),
]
type ImageNetworkMode = Literal["default", "none", "host"]
type Memory = Annotated[
    str,
    StringConstraints(strip_whitespace=True, pattern=r"^\d+[bkmg]?$"),
]
type ULimitName = Annotated[
    str,
    StringConstraints(
        strip_whitespace=True,
        min_length=1,
        pattern=r"^[a-z][a-z0-9_]*$",
    ),
]
type Capability = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_capability)]
type SecurityOpt = Annotated[
    NonEmpty[NoWhiteSpace],
    AfterValidator(_check_security_opt),
]
type UserNS = Annotated[  # pylint: disable=invalid-name
    NonEmpty[NoWhiteSpace],
    AfterValidator(_check_userns),
]
type IPCMode = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_ipc)]
type PIDMode = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_pid)]
type UTSMode = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_uts)]
type InstrumentTool = Annotated[
    str,
    StringConstraints(
        strip_whitespace=True,
        min_length=1,
        pattern=r"^[A-Za-z0-9][A-Za-z0-9_.-]*$",
    ),
]
type DevicePermission = Annotated[
    NonEmpty[NoWhiteSpace],
    AfterValidator(_check_device_permission),
]
type Timeout = Annotated[
    str,
    StringConstraints(
        strip_whitespace=True,
        min_length=1,
        pattern=r"^\d+(\.\d+)?[smhd]?$",
    ),
]
type HealthLogDestination = Annotated[
    NonEmpty[NoCRLF],
    AfterValidator(_check_health_log_destination),
]
type BuildContextPath = Annotated[PosixPath, AfterValidator(_check_build_context_path)]


def _dump_ignore_list(patterns: list[str]) -> str:
    lines = [
        "# This file is managed by Bertrand.  Direct edits may be overwritten by",
        "# bertrand sync/build flows.",
    ]
    seen: set[str] = set()
    for pattern in patterns:
        if pattern in seen:
            continue
        seen.add(pattern)
        lines.append(pattern)
    return "\n".join(lines) + "\n"


@resource("bertrand")
class Bertrand(Resource):
    """Describe the configuration state needed by Bertrand itself.

    This resource will be implicitly added to any `bertrand init` command as a base
    resource, and can be universally configured from any config provider (e.g.
    `pyproject.toml`) via the `"bertrand"` snapshot key.

    Bertrand is responsible for:
        1.  Initializing the minimal worktree layout needed by Bertrand's tools,
            including `src/`, `tests/`, `docs/`, `Containerfile`, `.containerignore`,
            and `.gitignore`.
        2.  Defining the build matrix targets for `bertrand build`, including
            compilation configuration, runtime harness (e.g. resource limits,
            networking, device passthrough, secrets, etc.), dependencies, and possible
            kubernetes orchestration.
    """

    # pylint: disable=missing-function-docstring, unused-argument, missing-return-doc

    class Model(BaseModel):
        """Validate the `[bertrand]` table."""

        model_config = ConfigDict(extra="forbid")
        shell: Annotated[
            Shell,
            Field(
                default=DEFAULT_SHELL,
                examples=list(SHELLS),
                description=(
                    "Default shell to use when entering a container via `bertrand "
                    "enter`.  This is not a literal shell command, but rather an "
                    "identifier that maps to a backend command to prevent remote code "
                    "execution."
                ),
            ),
        ]
        editor: Annotated[
            Editor,
            Field(
                default=DEFAULT_EDITOR,
                examples=list(EDITORS),
                description=(
                    "Default text editor to use when invoking `bertrand code`.  "
                    "This is not a literal shell command, but rather an identifier "
                    "that maps to a backend command to prevent remote code execution."
                ),
            ),
        ]
        ignore: Annotated[
            IgnoreList,
            Field(
                default_factory=lambda: [
                    ".bertrand/*",
                    "**/.bertrand/",
                    "__pycache__/",
                    "*.py[cod]",
                    "*.egg-info/",
                    ".dist/",
                    ".build/",
                    ".eggs/",
                    ".venv/",
                    "venv/",
                    "*.o",
                    "*.obj",
                    "*.a",
                    "*.lib",
                    "*.so",
                    "*.dylib",
                    "*.dll",
                    ".vscode/",
                ],
                examples=[
                    [
                        ".bertrand/*",
                        "**/.bertrand/",
                        "__pycache__/",
                        "*.py[cod]",
                        "*.egg-info/",
                        ".dist/",
                        ".build/",
                        ".eggs/",
                        ".venv/",
                        "venv/",
                        "*.o",
                        "*.obj",
                        "*.a",
                        "*.lib",
                        "*.so",
                        "*.dylib",
                        "*.dll",
                        ".vscode/",
                    ]
                ],
                description=(
                    "List of patterns to ignore within this project, which are shared "
                    "between both `.gitignore` and `.containerignore`.  Patterns are "
                    "interpreted using the same rules as those files."
                ),
            ),
        ]
        git_ignore: Annotated[
            IgnoreList,
            Field(
                default_factory=list,
                alias="git-ignore",
                examples=[[]],
                description=(
                    "List of `.gitignore`-specific patterns, which are merged with the "
                    "global `ignore` patterns when generating `.gitignore`."
                ),
            ),
        ]
        container_ignore: Annotated[
            IgnoreList,
            Field(
                default_factory=lambda: [
                    ".git/",
                    ".gitignore",
                ],
                alias="container-ignore",
                examples=[
                    [
                        ".git/",
                        ".gitignore",
                    ]
                ],
                description=(
                    "List of `.containerignore`-specific patterns, which are "
                    "merged with the global `ignore` patterns when generating "
                    "`.containerignore`."
                ),
            ),
        ]

        class Network(BaseModel):
            """Validate the `[bertrand.network]` table."""

            model_config = ConfigDict(extra="forbid")

            class Table(BaseModel):
                """Validate common fields in `[bertrand.network.run]` tables."""

                model_config = ConfigDict(extra="forbid")
                mode: Annotated[
                    str,
                    StringConstraints(
                        strip_whitespace=True,
                        min_length=1,
                        pattern=(
                            "^(none|host|private|slirp4netns|pasta|"
                            f"{NS_PATH_RE.pattern})$"
                        ),
                    ),
                    Field(
                        default="private",
                        examples=[
                            "none",
                            "host",
                            "private",
                            "slirp4netns",
                            "pasta",
                            "ns:<path>",
                        ],
                        description=(
                            "The networking driver to use within containers for this "
                            "project.  Equivalent to `podman create --network`\n"
                            "   `none`: disable networking within the container.\n"
                            "   `host`: use the host's network stack directly (best "
                            "performance, no isolation, potentially insecure).\n"
                            "   `private`: (default) create a new, private network "
                            "namespace for the container.\n"
                            "   `slirp4netns`: use slirp4netns for rootless "
                            "networking.  This is often the default for many rootless "
                            "container runtimes, but may be slower than other options "
                            "and has some limitations.  See the podman documentation "
                            "for more details.\n"
                            "   `pasta`: use pasta for rootless networking.  This is "
                            "slightly faster than slirp4netns in some scenarios, but "
                            "is still slower than rootful host networking.  See the "
                            "podman documentation for more details.\n"
                            "   `ns:<path>`: join an existing network namespace "
                            "specified by the given <path>.\n"
                            "Bertrand intentionally omits `bridge`, `<network "
                            "name|ID>`, and `container:id` from its configuration "
                            "layer in order to keep projects portable across hosts."
                        ),
                    ),
                ]
                options: Annotated[
                    list[str],
                    Field(
                        default_factory=list,
                        examples=[
                            [
                                "--ipv4-only",
                                "-a",
                                "10.0.2.0",
                                "-n",
                                "24",
                                "-g",
                                "10.0.2.2",
                                "--dns-forward",
                                "10.0.2.3",
                                "-m",
                                "1500",
                                "--no-ndp",
                                "--no-dhcpv6",
                                "--no-dhcp",
                            ]
                        ],
                        description="Additional `--network` mode options, encoded as "
                        "`mode:opt1,opt2,...`.  These are forwarded to the selected "
                        "network backend.  In Bertrand's global networking contract, "
                        "options are only valid for `slirp4netns` and `pasta` modes.  "
                        "See the podman documentation for more details.",
                    ),
                ]
                dns: Annotated[
                    list[IPAddress | Literal["none"]],
                    Field(
                        default_factory=list,
                        description=(
                            "Set custom DNS servers.  Equivalent to "
                            "`podman create --dns`.  The special value `none` "
                            "disables "
                            "creation of `/etc/resolv.conf` by Podman, so the image's "
                            "`/etc/resolv.conf` is used unchanged."
                        ),
                    ),
                ]
                dns_search: Annotated[
                    list[NonEmpty[NoWhiteSpace]],
                    Field(
                        default_factory=list,
                        alias="dns-search",
                        description="Set custom DNS search domains.  Equivalent to "
                        "`podman create --dns-search`.",
                    ),
                ]
                dns_options: Annotated[
                    list[NonEmpty[NoWhiteSpace]],
                    Field(
                        default_factory=list,
                        alias="dns-options",
                        description="Set custom DNS resolver options.  Equivalent to "
                        "`podman create --dns-option`.",
                    ),
                ]
                add_host: Annotated[
                    dict[HostName, HostIP],
                    Field(
                        default_factory=dict,
                        alias="add-host",
                        description=(
                            "Mapping of additional host entries to add to container "
                            "`/etc/hosts`.  Equivalent to "
                            "`podman create --add-host`.  Keys are hostnames, "
                            "and values are IPv4/IPv6 addresses or the special value "
                            "`host-gateway`."
                        ),
                    ),
                ]

                @model_validator(mode="after")
                def _validate_none_mode(self) -> Self:
                    if self.mode == "none" and (
                        self.options or self.dns or self.dns_search or self.dns_options
                    ):
                        msg = (
                            "network mode 'none' requires empty options, dns, "
                            "dns-search, and dns-options"
                        )
                        raise ValueError(msg)
                    return self

                @model_validator(mode="after")
                def _validate_driver_options_mode(self) -> Self:
                    if self.options and self.mode not in ("slirp4netns", "pasta"):
                        msg = (
                            "network options are only allowed for 'slirp4netns' and "
                            "'pasta' modes"
                        )
                        raise ValueError(msg)
                    return self

                @model_validator(mode="after")
                def _validate_dns_none(self) -> Self:
                    if "none" in self.dns and len(self.dns) > 1:
                        msg = (
                            "dns entry 'none' cannot be combined with other DNS servers"
                        )
                        raise ValueError(msg)
                    return self

            class Run(Table):
                """Validate the `[bertrand.network.run]` table."""

                @model_validator(mode="after")
                def _validate_run_table(self) -> Self:
                    # TODO: run-specific networking restrictions should be added
                    # here when runtime networking coverage expands.
                    return self

            run: Annotated[
                Run,
                Field(
                    default_factory=Run.model_construct,
                    description=(
                        "Global networking policy to use during container creation and "
                        "runtime execution."
                    ),
                ),
            ]

        network: Annotated[
            Network,
            Field(
                default_factory=Network.model_construct,
                description="Networking configuration to use within this project.",
            ),
        ]

        class Image(BaseModel):
            """Validate entries in the `[tool.bertrand.image]` table."""

            model_config = ConfigDict(extra="forbid")
            containerfile: Annotated[
                RelativePosixPath | None,
                Field(
                    default=None,
                    examples=["path/to/Containerfile", None],
                    description=(
                        "Relative path to a Containerfile defining the build steps for "
                        "this image.  This is intended to allow advanced users to "
                        "define "
                        "custom build steps for their projects outside of Bertrand's "
                        "normal bootstrap procedure.  For the vast majority of users, "
                        "this "
                        "should be omitted, and relevant setup should be done through "
                        "standard build tools and package managers defined elsewhere "
                        "in project configuration.  If omitted, Bertrand will "
                        "automatically "
                        "generate a minimal Containerfile based on this information."
                    ),
                ),
            ]
            target: Annotated[
                TOMLKey | None,
                Field(
                    default=None,
                    examples=["stage-name", None],
                    description=(
                        "Optional target stage in a multi-stage Containerfile.  If "
                        "omitted, the final stage will be used by default.  Cannot be "
                        "used unless `containerfile` is also provided."
                    ),
                ),
            ]
            from_: Annotated[
                list[OCIImageRef],
                Field(
                    alias="from",
                    default_factory=list,
                    examples=[
                        ["ghcr.io/acme/toolchain:1.2.3"],
                        [
                            "ghcr.io/acme/toolchain@sha256:"
                            "0123456789abcdef0123456789abcdef"
                            "0123456789abcdef0123456789abcdef"
                        ],
                        [
                            "ghcr.io/acme/toolchain:1.2.3@sha256:"
                            "0123456789abcdef0123456789abcdef"
                            "0123456789abcdef0123456789abcdef"
                        ],
                    ],
                    description=(
                        "List of OCI image dependencies to inject into the generated "
                        "Containerfile.  References must be fully-qualified registry "
                        "refs without transport prefixes, such as "
                        "`ghcr.io/acme/toolchain:1.2.3`, "
                        "`ghcr.io/acme/toolchain@sha256:<digest>`, or "
                        "`ghcr.io/acme/toolchain:1.2.3@sha256:<digest>`.  "
                        "Shorthand refs "
                        "like `ubuntu:24.04` and transport-prefixed refs like "
                        "`docker://ghcr.io/acme/toolchain:1.2.3` are rejected for "
                        "portability.  This is incompatible with custom "
                        "`containerfile`s, and is intended to supplement "
                        "language-specific package managers for multilingual "
                        "projects that make use of advanced features like "
                        "dynamic compilation, which require a supporting toolchain."
                    ),
                ),
            ]
            args: Annotated[
                dict[NonEmpty[SnakeCase], Scalar],
                Field(
                    default_factory=dict,
                    examples=[
                        "\n".join(
                            (
                                _image_table(DEFAULT_TAG),
                                "args = { DEBUG = true, JIT = true }",
                            )
                        ),
                        "\n".join(
                            (
                                _image_subtable(DEFAULT_TAG, "args"),
                                "DEBUG = true",
                                "JIT = true",
                                "...",
                            )
                        ),
                    ],
                    description=(
                        "Mapping of build-time ARG variables to their values, which "
                        "are "
                        "passed to the listed Containerfile.  Bertrand-generated "
                        "Containerfiles include only a small number of ARG "
                        "instructions "
                        "to constrain the base image, but custom Containerfiles are "
                        "unrestricted."
                    ),
                ),
            ]
            network: Annotated[
                ImageNetworkMode,
                Field(
                    default="default",
                    examples=["default", "none", "host"],
                    description=(
                        "BuildKit network mode applied to build-time `RUN` "
                        "instructions.  `default` uses the configured BuildKit "
                        "builder network, `none` disables network access, and "
                        "`host` requests host networking via BuildKit's "
                        "`network.host` entitlement."
                    ),
                ),
            ]

            class Secret(BaseModel):
                """Validate an entry in `[[tool.bertrand.image.<tag>.secrets]]`."""

                model_config = ConfigDict(extra="forbid")
                id: Annotated[
                    KubeName,
                    Field(
                        examples=["pypi_token", "private_pkg_key"],
                        description=(
                            "Host-agnostic capability ID for a build secret.  The ID "
                            "is resolved using a Kubernetes Secret in the \"bertrand\" "
                            "namespace, which provides the secret's payload."
                        ),
                    ),
                ]
                required: Annotated[
                    bool,
                    Field(
                        default=True,
                        description=(
                            "Whether this capability must be available to start the "
                            "build.  If true and unresolved, the build fails before "
                            "execution."
                        ),
                    ),
                ]

            @staticmethod
            def _check_unique_secrets(requests: list[Secret]) -> list[Secret]:
                seen: set[KubeName] = set()
                for req in requests:
                    if req.id in seen:
                        msg = f"duplicate secret id: '{req.id}'"
                        raise ValueError(msg)
                    seen.add(req.id)
                return requests

            secrets: Annotated[
                list[Secret],
                AfterValidator(_check_unique_secrets),
                Field(
                    default_factory=list,
                    examples=[
                        "\n".join(
                            (
                                _image_table(DEFAULT_TAG),
                                "secrets = [{ id = \"pypi_token\", required = true }]",
                            )
                        ),
                        "\n".join(
                            (
                                _image_array_table(DEFAULT_TAG, "secrets"),
                                "id = \"private_pkg_key\"",
                                "required = false",
                            )
                        ),
                    ],
                    description=(
                        "Build-time secrets resolved from the local Kubernetes cluster."
                    ),
                ),
            ]

            class SSH(BaseModel):
                """Validate an entry in `[[tool.bertrand.image.<tag>.ssh]]`."""

                model_config = ConfigDict(extra="forbid")
                id: Annotated[
                    KubeName,
                    Field(
                        examples=["git_deploy_key", "github_readonly"],
                        description=(
                            "Host-agnostic capability ID for a build-time SSH "
                            "credential.  "
                            "The ID is resolved using a Kubernetes Secret in the "
                            "\"bertrand\" namespace, which provides the credential "
                            "payload."
                        ),
                    ),
                ]
                required: Annotated[
                    bool,
                    Field(
                        default=True,
                        description=(
                            "Whether this SSH capability must be available to start "
                            "the "
                            "build.  If true and unresolved, the build fails before "
                            "execution."
                        ),
                    ),
                ]

            @staticmethod
            def _check_unique_ssh(requests: list[SSH]) -> list[SSH]:
                seen: set[KubeName] = set()
                for req in requests:
                    if req.id in seen:
                        msg = f"duplicate ssh id: '{req.id}'"
                        raise ValueError(msg)
                    seen.add(req.id)
                return requests

            ssh: Annotated[
                list[SSH],
                AfterValidator(_check_unique_ssh),
                Field(
                    default_factory=list,
                    examples=[
                        "\n".join(
                            (
                                _image_table(DEFAULT_TAG),
                                "ssh = [{ id = \"git_deploy_key\", required = true }]",
                            )
                        ),
                        "\n".join(
                            (
                                _image_array_table(DEFAULT_TAG, "ssh"),
                                "id = \"github_readonly\"",
                                "required = false",
                            )
                        ),
                    ],
                    description=(
                        "Build-time SSH credentials to apply to `RUN` instructions in "
                        "the associated Containerfile."
                    ),
                ),
            ]

            class Device(BaseModel):
                """Validate an entry in `[[tool.bertrand.image.<tag>.devices]]`."""

                model_config = ConfigDict(extra="forbid")
                id: Annotated[
                    KubeName,
                    Field(
                        examples=["gpu", "cuda0"],
                        description=(
                            "Host-agnostic capability ID for a build-time CDI "
                            "device capability. The ID is resolved using a "
                            "Kubernetes Secret-backed Bertrand capability, whose "
                            "payload supplies the CDI selector."
                        ),
                    ),
                ]
                required: Annotated[
                    bool,
                    Field(
                        default=True,
                        description=(
                            "Whether this device capability must be available to "
                            "start the build.  If true and unresolved, the build "
                            "fails before "
                            "execution."
                        ),
                    ),
                ]

            @staticmethod
            def _check_unique_devices(requests: list[Device]) -> list[Device]:
                seen: set[KubeName] = set()
                for req in requests:
                    if req.id in seen:
                        msg = f"duplicate device id: '{req.id}'"
                        raise ValueError(msg)
                    seen.add(req.id)
                return requests

            devices: Annotated[
                list[Device],
                AfterValidator(_check_unique_devices),
                Field(
                    default_factory=list,
                    examples=[
                        "\n".join(
                            (
                                _image_table(DEFAULT_TAG),
                                "devices = [{ id = \"gpu\", required = true }]",
                            )
                        ),
                        "\n".join(
                            (
                                _image_array_table(DEFAULT_TAG, "devices"),
                                "id = \"fpga0\"",
                                "required = false",
                            )
                        ),
                    ],
                    description=(
                        "Build-time CDI device capabilities resolved from the local "
                        "Kubernetes cluster."
                    ),
                ),
            ]

            # TODO: review + document conan configuration

            class Conan(BaseModel):
                """Validate the `[tool.bertrand.image.<tag>.conan]` table."""

                model_config = ConfigDict(extra="forbid")
                build_type: Annotated[
                    Literal["", "Release", "Debug"],
                    Field(default="", alias="build-type"),
                ]
                conf: Annotated[ConanConf, Field(default_factory=dict)]
                options: Annotated[ConanOptions, Field(default_factory=dict)]
                requires: Annotated[
                    list[ConanConfig.Model.Require],
                    AfterValidator(ConanConfig.Model._check_requires),
                    Field(default_factory=list),
                ]

            conan: Annotated[Conan, Field(default_factory=Conan.model_construct)]

            @model_validator(mode="after")
            def _validate_containerfile(self) -> Self:
                if self.containerfile is None:
                    if self.target is not None:
                        msg = "`target` cannot be set without `containerfile`"
                        raise ValueError(msg)
                else:
                    if self.from_:
                        msg = "`from` cannot be set with a custom `containerfile`"
                        raise ValueError(msg)
                return self

            def resolve_containerfile(self, root: Path, tag: BertrandTag) -> None:
                """Validate the custom Containerfile path for one image key.

                Raises
                ------
                OSError
                    If the Containerfile path is missing, not a file, or not UTF-8.
                """
                if self.containerfile is None:
                    return
                path = root / self.containerfile
                if not path.exists():
                    msg = f"path does not exist for tag '{tag}': {path}"
                    raise OSError(msg)
                if not path.is_file():
                    msg = f"path is not a file for tag '{tag}': {path}"
                    raise OSError(msg)
                try:
                    path.read_text(encoding="utf-8")
                except UnicodeDecodeError as err:
                    msg = f"file is not UTF-8 encoded for tag '{tag}': {path}"
                    raise OSError(msg) from err

        image: Annotated[
            dict[BertrandTag, Image],
            Field(
                default_factory=lambda: {
                    DEFAULT_TAG: Bertrand.Model.Image.model_construct()
                }
            ),
        ]

        class Channel(BaseModel):
            """Validate entries in the `[tool.bertrand.channel]` table."""

            model_config = ConfigDict(extra="forbid")

            image: Annotated[
                BertrandTag,
                Field(
                    description=(
                        "Configured image key from `[tool.bertrand.image]` that this "
                        "moving channel should reference. Use `\"\"` for the base "
                        "image."
                    ),
                ),
            ]

        channel: Annotated[
            dict[OCIImageTag, Channel],
            Field(
                default_factory=dict,
                description=(
                    "Moving image tags, such as `latest` or `stable`, published as "
                    "aliases of configured versioned project images."
                ),
            ),
        ]

        class Workload(BaseModel):
            """Validate entries in the `[tool.bertrand.workload]` table."""

            model_config = ConfigDict(extra="forbid")
            containerfile: Annotated[
                RelativePosixPath | None,
                Field(
                    default=None,
                    examples=["path/to/Containerfile", None],
                    description=(
                        "Relative path to a Containerfile defining the build steps for "
                        "this image.  This is intended to allow advanced users to "
                        "define "
                        "custom build steps for their projects outside of Bertrand's "
                        "normal bootstrap procedure.  For the vast majority of users, "
                        "this "
                        "should be omitted, and relevant setup should be done through "
                        "standard build tools and package managers defined elsewhere "
                        "in project configuration.  If omitted, Bertrand will "
                        "automatically "
                        "generate a minimal Containerfile based on this information."
                    ),
                ),
            ]
            build_args: Annotated[
                dict[NonEmpty[UpperSnakeCase], Scalar],
                Field(
                    default_factory=dict,
                    alias="build-args",
                    examples=[
                        "\n".join(
                            (
                                '[tool.bertrand.workload."".build-args]',
                                "CPUS = 8",
                                "DEBUG = true",
                                "PYTHON_VERSION = \"3.12.4\"",
                                "...",
                            )
                        )
                    ],
                    description=(
                        "Mapping of build-time ARG variables to their values, which "
                        "are "
                        "passed to the listed Containerfile.  Keys must be "
                        "SCREAMING_SNAKE_CASE, and not start with a number or end "
                        "with an underscore."
                    ),
                ),
            ]
            cmd: Annotated[
                list[NonEmpty[Trimmed]],
                Field(
                    default_factory=list,
                    examples=[
                        ["echo", "Hello, world!"],
                        ["greet"],
                    ],
                    description=(
                        "The default entry point for containers built from this image, "
                        "defined as a list of strings representing the command and its "
                        "arguments.  If no override is supplied to `bertrand run`, "
                        "then this command will be used instead.  If it is also "
                        "empty, then the run will fail."
                    ),
                ),
            ]

            class Port(BaseModel):
                """Validate entries in the workload ports table."""

                model_config = ConfigDict(extra="forbid")
                container: Annotated[int, Field(ge=1, le=65535)]
                host: Annotated[int, Field(ge=1, le=65535)]
                host_ip: Annotated[IPAddress, Field(alias="host-ip")]
                protocol: Literal["tcp", "udp"]

            @staticmethod
            def _check_ports(ports: list[Port]) -> list[Port]:
                seen: set[tuple[str, int, str]] = set()
                for port in ports:
                    key = (port.host_ip, port.host, port.protocol)
                    if key in seen:
                        msg = (
                            "duplicate published port binding for "
                            f"{port.host_ip}:{port.host}/{port.protocol}"
                        )
                        raise ValueError(msg)
                    seen.add(key)
                return ports

            @staticmethod
            def _check_network_aliases(
                aliases: list[NetworkAlias],
            ) -> list[NetworkAlias]:
                seen: set[NetworkAlias] = set()
                for alias in aliases:
                    if alias in seen:
                        msg = f"duplicate network alias: '{alias}'"
                        raise ValueError(msg)
                    seen.add(alias)
                return aliases

            ports: Annotated[
                list[Port], AfterValidator(_check_ports), Field(default_factory=list)
            ]
            network_aliases: Annotated[
                list[NetworkAlias],
                AfterValidator(_check_network_aliases),
                Field(default_factory=list, alias="network-aliases"),
            ]
            cpus: Annotated[
                NonNegativeFloat,
                Field(
                    default=0.0,
                    description=(
                        "The number of CPUs to allocate to containers built from this "
                        "image.  0.0 (the default) removes the limit and allows the "
                        "container to use all available resources.  Fractional "
                        "values are allowed to specify partial CPU allocation "
                        "(e.g. 0.5 for half a CPU)."
                    ),
                ),
            ]

            # TODO: maybe the memory limit needs to be split between build and run
            # time?
            memory: Annotated[
                Memory,
                Field(
                    default="0",
                    examples=["1024b", "128k", "512m", "2g"],
                    description=(
                        "The amount of memory to allocate to containers built from "
                        "this "
                        "image.  0 (the default) removes the limit and allows the "
                        "container to use all available resources.  If the machine "
                        "supports swap memory, then the value may be larger than the "
                        "physical memory.  Equivalent to `podman build|create -m`."
                    ),
                ),
            ]
            pids_limit: Annotated[int, Field(default=0, ge=-1, alias="pids-limit")]
            shm_size: Annotated[Memory, Field(default="64m", alias="shm-size")]

            class ULimit(BaseModel):
                """Validate entries in `[tool.bertrand.workload.<tag>.ulimit]`."""

                model_config = ConfigDict(extra="forbid")
                name: ULimitName
                soft: Annotated[int | None, Field(default=None, ge=-1)]
                hard: Annotated[int | None, Field(default=None, ge=-1)]

                @model_validator(mode="after")
                def _validate_limits(self) -> Self:
                    if self.name == "host":
                        if self.soft is not None or self.hard is not None:
                            msg = (
                                "ulimit name 'host' cannot define 'soft' or "
                                "'hard' values"
                            )
                            raise ValueError(msg)
                        return self
                    if self.soft is None or self.hard is None:
                        msg = (
                            "non-'host' ulimit entries must define both 'soft' "
                            "and 'hard'"
                        )
                        raise ValueError(msg)
                    if self.hard >= 0 and self.soft > self.hard:
                        msg = (
                            f"ulimit soft value {self.soft} cannot be greater "
                            f"than hard value {self.hard}"
                        )
                        raise ValueError(msg)
                    return self

            @staticmethod
            def _check_ulimit(entries: list[ULimit]) -> list[ULimit]:
                seen: set[str] = set()
                for entry in entries:
                    if entry.name in seen:
                        msg = f"duplicate ulimit name: '{entry.name}'"
                        raise ValueError(msg)
                    seen.add(entry.name)
                return entries

            @staticmethod
            def _check_unique(value: list[str], *, where: str) -> list[str]:
                seen: set[str] = set()
                for item in value:
                    if item in seen:
                        msg = f"duplicate {where}: '{item}'"
                        raise ValueError(msg)
                    seen.add(item)
                return value

            ulimit: Annotated[
                list[ULimit], AfterValidator(_check_ulimit), Field(default_factory=list)
            ]
            cap_add: Annotated[
                list[Capability],
                AfterValidator(
                    lambda x: Bertrand.Model.Workload._check_unique(
                        x, where="cap-add capability"
                    )
                ),
                Field(default_factory=list, alias="cap-add"),
            ]
            cap_drop: Annotated[
                list[Capability],
                AfterValidator(
                    lambda x: Bertrand.Model.Workload._check_unique(
                        x, where="cap-drop capability"
                    )
                ),
                Field(default_factory=list, alias="cap-drop"),
            ]
            security_opt: Annotated[
                list[SecurityOpt],
                AfterValidator(
                    lambda x: Bertrand.Model.Workload._check_unique(
                        x, where="security-opt entry"
                    )
                ),
                Field(default_factory=list, alias="security-opt"),
            ]
            userns: Annotated[UserNS, Field(default="host")]
            ipc: Annotated[IPCMode, Field(default="private")]
            pid: Annotated[PIDMode, Field(default="private")]
            uts: Annotated[UTSMode, Field(default="private")]
            ssh: Annotated[list[KubeName], Field(default_factory=list)]

            class InstrumentEntry(BaseModel):
                """Validate entries in the image instrument table."""

                model_config = ConfigDict(extra="allow")
                tool: InstrumentTool

            instruments: Annotated[list[InstrumentEntry], Field(default_factory=list)]

            @model_validator(mode="after")
            def _validate_capability_conflicts(self) -> Self:
                if "ALL" in self.cap_add and len(self.cap_add) > 1:
                    msg = "cap-add cannot combine 'ALL' with specific capabilities"
                    raise ValueError(msg)
                if "ALL" in self.cap_drop and len(self.cap_drop) > 1:
                    msg = "cap-drop cannot combine 'ALL' with specific capabilities"
                    raise ValueError(msg)
                overlap = {cap for cap in self.cap_add if cap != "ALL"}
                overlap = overlap.intersection(
                    cap for cap in self.cap_drop if cap != "ALL"
                )
                if overlap:
                    msg = (
                        "cap-add and cap-drop cannot contain the same capability: "
                        f"{', '.join(sorted(overlap))}"
                    )
                    raise ValueError(msg)
                return self

            # TODO: SSH capability design (config-layer contract):
            # - `ssh` is a list of capability IDs only (SCREAMING_SNAKE_CASE),
            #   never key data.
            # - IDs resolve via host-local channels at execution time:
            #     1) BERTRAND_SSH_<ID> env override
            #     2) host profile (e.g. .bertrand/host/ssh.toml)
            # - Preferred source is SSH agent forwarding; key-file source is
            #   fallback only.
            # - Intended mapping target is `podman build --ssh` (build-time),
            #   not runtime mounts.
            # - Security invariants:
            #     * no private key bytes in pyproject/config metadata
            #     * no host key paths committed to VCS
            #     * no secret material written to image layers or persisted state
            # - Runtime wiring/argv synthesis is deferred to container.py refactor.
            # - Final usage is always in the tag's Containerfile, by appending
            #   `RUN --mount=type=ssh,id=id ...`

            class Devices(BaseModel):
                """Validate the `[tool.bertrand.workload.<tag>.devices]` table."""

                model_config = ConfigDict(extra="forbid")

                class Request(BaseModel):
                    """Validate one image device capability request."""

                    model_config = ConfigDict(extra="forbid")
                    id: KubeName
                    required: bool = True
                    container_path: Annotated[
                        AbsolutePosixPath | None,
                        Field(default=None, alias="container-path"),
                    ]
                    permissions: Annotated[DevicePermission, Field(default="rwm")]

                @staticmethod
                def _check_unique_ids(
                    requests: list[Request],
                    *,
                    where: str,
                ) -> list[Request]:
                    seen: set[KubeName] = set()
                    for req in requests:
                        if req.id in seen:
                            msg = f"duplicate {where} device id: '{req.id}'"
                            raise ValueError(msg)
                        seen.add(req.id)
                    return requests

                build: Annotated[
                    list[Request],
                    AfterValidator(
                        lambda x: Bertrand.Model.Workload.Devices._check_unique_ids(
                            x, where="build"
                        )
                    ),
                    Field(default_factory=list),
                ]
                run: Annotated[
                    list[Request],
                    AfterValidator(
                        lambda x: Bertrand.Model.Workload.Devices._check_unique_ids(
                            x, where="run"
                        )
                    ),
                    Field(default_factory=list),
                ]

            # TODO: Device capability design (config-layer contract):
            # - `devices.build` and `devices.run` are host-agnostic capability
            #   requests keyed by SCREAMING_SNAKE_CASE IDs, never raw host paths.
            # - Each request may override container-facing mapping details only:
            #   `container-path`, `permissions`, `required`.
            # - IDs resolve via host-local channels at execution time:
            #     1) BERTRAND_DEVICE_<ID> env override
            #     2) host profile (e.g. .bertrand/host/devices.toml)
            # - Resolver policy is CDI-preferred with host-path fallback for
            #   compatibility across hosts that lack CDI specs.
            # - Security invariants:
            #     * no host device paths committed in project configuration
            #     * no secret host topology persisted in project metadata
            # - Runtime wiring/argv synthesis is deferred to container.py refactor.

            devices: Annotated[Devices, Field(default_factory=Devices.model_construct)]

            class Secrets(BaseModel):
                """Validate the `[tool.bertrand.workload.<tag>.secrets]` table."""

                model_config = ConfigDict(extra="forbid")

                class Request(BaseModel):
                    """Validate an individual secret capability request."""

                    model_config = ConfigDict(extra="forbid")
                    id: KubeName
                    required: bool = True

                @staticmethod
                def _check_unique_ids(
                    requests: list[Request],
                    *,
                    where: str,
                ) -> list[Request]:
                    seen: set[KubeName] = set()
                    for req in requests:
                        if req.id in seen:
                            msg = f"duplicate {where} secret id: '{req.id}'"
                            raise ValueError(msg)
                        seen.add(req.id)
                    return requests

                build: Annotated[
                    list[Request],
                    AfterValidator(
                        lambda x: Bertrand.Model.Workload.Secrets._check_unique_ids(
                            x, where="build"
                        )
                    ),
                    Field(default_factory=list),
                ]
                run: Annotated[
                    list[Request],
                    AfterValidator(
                        lambda x: Bertrand.Model.Workload.Secrets._check_unique_ids(
                            x, where="run"
                        )
                    ),
                    Field(default_factory=list),
                ]

            # TODO: Secrets capability design (config-layer contract):
            # - `secrets.build` and `secrets.run` are capability requests keyed
            #   by SCREAMING_SNAKE_CASE IDs, never secret values.
            # - IDs resolve via host-local channels at execution time:
            #     1) BERTRAND_SECRET_<ID> env override
            #     2) host profile / podman-backed secret resolver
            # - Build-time resolution maps to `podman build --secret`.
            # - Runtime resolution maps to `podman run --secret` and exposes
            #   secrets as files (e.g. under `/run/secrets`), not env vars.
            # - Security invariants:
            #     * no secret bytes in project configuration or metadata
            #     * no secret material persisted in logs or generated state
            #     * unresolved `required=true` entries fail closed at runtime
            # - Runtime wiring/argv synthesis is deferred to container.py refactor.

            secrets: Annotated[Secrets, Field(default_factory=Secrets.model_construct)]

            class Conan(BaseModel):
                """Validate the `[tool.bertrand.workload.<tag>.conan]` table."""

                model_config = ConfigDict(extra="forbid")
                build_type: Annotated[
                    Literal["", "Release", "Debug"],
                    Field(default="", alias="build-type"),
                ]
                conf: Annotated[ConanConf, Field(default_factory=dict)]
                options: Annotated[ConanOptions, Field(default_factory=dict)]
                requires: Annotated[
                    list[ConanConfig.Model.Require],
                    AfterValidator(ConanConfig.Model._check_requires),
                    Field(default_factory=list),
                ]

            conan: Annotated[Conan, Field(default_factory=Conan.model_construct)]

            class Build(BaseModel):
                """Validate the `[tool.bertrand.workload.<tag>.build]` table."""

                model_config = ConfigDict(extra="forbid")
                context: Annotated[BuildContextPath, Field(default=PosixPath("."))]
                target: Annotated[
                    NoWhiteSpace,
                    StringConstraints(pattern=r"^[a-zA-Z0-9_-]*$"),
                    Field(default=""),
                ]
                pull: Annotated[
                    Literal["missing", "always", "never", "newer"],
                    Field(default="missing"),
                ]

            build: Annotated[Build, Field(default_factory=Build.model_construct)]

            class Stop(BaseModel):
                """Validate the `[tool.bertrand.workload.<tag>.stop]` table."""

                model_config = ConfigDict(extra="forbid")
                signal: Annotated[
                    str,
                    StringConstraints(
                        strip_whitespace=True,
                        min_length=1,
                        pattern=r"^\S+$",
                    ),
                    Field(default="SIGTERM"),
                ]
                timeout: Annotated[NonNegativeInt, Field(default=10)]

            stop: Annotated[Stop, Field(default_factory=Stop.model_construct)]

            class Restart(BaseModel):
                """Validate the `[tool.bertrand.workload.<tag>.restart]` table."""

                model_config = ConfigDict(extra="forbid")
                policy: Annotated[
                    Literal["no", "on-failure", "always", "unless-stopped"],
                    Field(default="no"),
                ]
                max_retries: Annotated[
                    NonNegativeInt, Field(default=0, alias="max-retries")
                ]

            restart: Annotated[Restart, Field(default_factory=Restart.model_construct)]

            class Healthcheck(BaseModel):
                """Validate the `[tool.bertrand.workload.<tag>.healthcheck]` table."""

                model_config = ConfigDict(extra="forbid")
                cmd: Annotated[list[str], Field(default_factory=list)]
                on_failure: Annotated[
                    Literal["none", "kill", "stop"],
                    Field(default="kill", alias="on-failure"),
                ]
                retries: Annotated[NonNegativeInt, Field(default=3)]
                interval: Annotated[Timeout, Field(default="30s")]
                timeout: Annotated[Timeout, Field(default="30s")]

                class Startup(BaseModel):
                    """Validate the image startup healthcheck table."""

                    model_config = ConfigDict(extra="forbid")
                    cmd: Annotated[list[str], Field(default_factory=list)]
                    period: Annotated[Timeout, Field(default="0s")]
                    success: Annotated[NonNegativeInt, Field(default=0)]
                    interval: Annotated[Timeout, Field(default="30s")]
                    timeout: Annotated[Timeout, Field(default="30s")]

                startup: Annotated[
                    Startup, Field(default_factory=Startup.model_construct)
                ]

                class Log(BaseModel):
                    """Validate the image healthcheck log table."""

                    model_config = ConfigDict(extra="forbid")
                    destination: Annotated[HealthLogDestination, Field(default="local")]
                    max_count: Annotated[
                        NonNegativeInt, Field(default=0, alias="max-count")
                    ]
                    max_size: Annotated[
                        NonNegativeInt, Field(default=0, alias="max-size")
                    ]

                log: Annotated[Log, Field(default_factory=Log.model_construct)]

            healthcheck: Annotated[
                Healthcheck, Field(default_factory=Healthcheck.model_construct)
            ]

            def resolve_containerfile(self, root: Path, tag: BertrandTag) -> None:
                """Validate the custom Containerfile path for one image key.

                Raises
                ------
                OSError
                    If the Containerfile path is missing, not a file, or not UTF-8.
                """
                if self.containerfile is None:
                    return
                path = root / self.containerfile
                if not path.exists():
                    msg = f"path does not exist for tag '{tag}': {path}"
                    raise OSError(msg)
                if not path.is_file():
                    msg = f"path is not a file for tag '{tag}': {path}"
                    raise OSError(msg)
                try:
                    path.read_text(encoding="utf-8")
                except UnicodeDecodeError as err:
                    msg = f"file is not UTF-8 encoded for tag '{tag}': {path}"
                    raise OSError(msg) from err

        workload: Annotated[
            dict[BertrandTag, Workload],
            Field(
                default_factory=lambda: {
                    DEFAULT_TAG: Bertrand.Model.Workload.model_construct()
                }
            ),
        ]

        @model_validator(mode="after")
        def _validate_image(self) -> Self:
            seen: set[str] = set()
            for tag in self.image:
                if tag in seen:
                    msg = f"duplicate image key in 'tool.bertrand.image': '{tag}'"
                    raise ValueError(msg)
                seen.add(tag)
            if DEFAULT_TAG not in seen:
                msg = (
                    "missing required default image key in 'tool.bertrand.image': "
                    f"'{DEFAULT_TAG}'"
                )
                raise ValueError(msg)
            unknown_channels = sorted(
                name
                for name, channel in self.channel.items()
                if channel.image not in seen
            )
            if unknown_channels:
                formatted = ", ".join(repr(name) for name in unknown_channels)
                msg = (
                    "found channel entries with no matching image in "
                    f"'tool.bertrand.image': {formatted}"
                )
                raise ValueError(msg)
            return self

        # @model_validator(mode="after")
        # def _validate_services(self) -> Self:
        #     unknown_services: list[str] = []
        #     for idx, service in enumerate(self.services):
        #         if any(prev == service for prev in self.services[:idx]):
        #             raise ValueError(
        #                 "duplicate service name in 'tool.bertrand.services': "
        #                 f"'{service}'"
        #             )
        #         if service not in self.image:
        #             unknown_services.append(service)
        #     if unknown_services:
        #         raise ValueError(
        #             "found service names in 'tool.bertrand.services' with no "
        #             f"matching tag in 'tool.bertrand.image': "
        #             f"{', '.join(unknown_services)}"
        #         )
        #     return self

        # @model_validator(mode="after")
        # def _validate_namespace_refs(self) -> Self:
        #     for tag in self.image:
        #         # if the current tag is a service, get its position in the list
        #         curr_pos = next(
        #             (
        #                 pos for pos, name in enumerate(self.services)
        #                 if name == tag.tag
        #             ),
        #             None
        #         )

        #         # for each namespace field that references an external tag, ensure
        #         # that the tag it references is a valid service
        #         for option, mode in (
        #             ("userns", tag.userns),
        #             ("ipc", tag.ipc),
        #             ("pid", tag.pid),
        #             ("uts", tag.uts),
        #         ):
        #             ref = _extract_container_ref(mode)
        #             if ref is None:
        #                 continue

        #             # outlaw self-references
        #             if ref == tag.tag:
        #                 raise ValueError(
        #                     f"{option} for tag '{tag.tag}' cannot reference "
        #                     f"itself via 'container:{ref}'"
        #                 )

        #             # get referenced service position + tag
        #             ref_pos = next(
        #                 (
        #                     pos for pos, name in enumerate(self.services)
        #                     if name == ref
        #                 ),
        #                 None
        #             )
        #             if ref_pos is None:
        #                 raise ValueError(
        #                     f"{option} for tag '{tag.tag}' references '{ref}', "
        #                     f"but '{ref}' is not listed in "
        #                     "'tool.bertrand.services'"
        #                 )
        #             ref_tag = self.image.get(ref)
        #             if ref_tag is None:
        #                 raise ValueError(
        #                     f"{option} for tag '{tag.tag}' references unknown "
        #                     f"tag '{ref}'"
        #                 )

        #             # enforce correct startup ordering
        #             if curr_pos is not None and ref_pos >= curr_pos:
        #                 raise ValueError(
        #                     f"{option} for service tag '{tag.tag}' references "
        #                     f"'container:{ref}', but '{ref}' must appear earlier "
        #                     f"than '{tag.tag}' in 'tool.bertrand.services'"
        #                 )

        #             # ipc requires the referenced tag uses ipc=shareable
        #             if option == "ipc" and ref_tag.ipc != "shareable":
        #                 raise ValueError(
        #                     f"ipc for tag '{tag.tag}' uses 'container:{ref}', "
        #                     f"but referenced tag '{ref}' must set ipc='shareable'"
        #                 )
        #     return self

    async def init(self, config: Config, cli: Config.Init) -> dict[str, Any]:
        """Return the default Bertrand configuration fragment.

        Returns
        -------
        dict[str, Any]
            Default configuration data serialized with TOML aliases.
        """
        del config, cli
        return self.Model.model_construct().model_dump(by_alias=True)

    async def validate(self, config: Config, fragment: Any) -> Model | None:
        """Validate a Bertrand configuration fragment.

        Returns
        -------
        Model | None
            Parsed Bertrand configuration.

        Raises
        ------
        ValueError
            If image keys, image tags, or channel references are invalid.
        """
        result = self.Model.model_validate(fragment)
        pyproject = config.get(PyProject)
        _validate_dependency_groups(pyproject=pyproject, bertrand=result)
        version = _project_version(config, pyproject)
        if version is not None:
            image_tags = {tag: project_image_tag(version, tag) for tag in result.image}
            collisions = sorted(set(result.channel).intersection(image_tags.values()))
            if collisions:
                formatted = ", ".join(repr(name) for name in collisions)
                msg = (
                    f"channel tags collide with version-derived image tags: {formatted}"
                )
                raise ValueError(msg)
        for tag, image in result.image.items():
            image.resolve_containerfile(config.root, tag)
        return result

    async def render(self, config: Config, tag: TOMLKey | None) -> None:
        """Render Bertrand-managed project files."""
        del tag
        bertrand = config.get(Bertrand)
        if bertrand is None:
            return
        jinja = jinja2.Environment(
            autoescape=False,
            undefined=jinja2.StrictUndefined,
            keep_trailing_newline=True,
            trim_blocks=False,
            lstrip_blocks=False,
        )
        bertrand_version = packaging.version.parse(VERSION.bertrand)
        python_version = packaging.version.parse(VERSION.python)

        # render worktree directories
        (config.root / "src").mkdir(parents=True, exist_ok=True)
        (config.root / "tests").mkdir(parents=True, exist_ok=True)
        (config.root / "docs").mkdir(parents=True, exist_ok=True)

        # render ignore files
        # Always ignore Bertrand's metadata directory.
        ignore = [str(METADATA_DIR / "*")]
        ignore.extend(bertrand.ignore)
        containerignore = ignore.copy()
        containerignore.extend(bertrand.container_ignore)
        atomic_write_text(
            config.root / ".containerignore",
            _dump_ignore_list(containerignore),
            encoding="utf-8",
        )
        gitignore = ignore.copy()
        gitignore.extend(bertrand.git_ignore)
        atomic_write_text(
            config.root / ".gitignore", _dump_ignore_list(gitignore), encoding="utf-8"
        )

        # initialize CI publish action
        publish_template = jinja.from_string(
            locate_template("core", "publish.v1").read_text(encoding="utf-8")
        )
        publish_target = config.root / ".github" / "workflows" / "publish.yml"
        publish_target.parent.mkdir(parents=True, exist_ok=True)
        publish_target.write_text(
            publish_template.render(
                python_major=python_version.major,
                python_minor=python_version.minor,
                python_patch=python_version.micro,
                bertrand_major=bertrand_version.major,
                bertrand_minor=bertrand_version.minor,
                bertrand_patch=bertrand_version.micro,
            ),
            encoding="utf-8",
        )

    async def schema(self) -> dict[str, Any]:
        """Return the JSON schema for Bertrand configuration.

        Returns
        -------
        dict[str, Any]
            JSON schema for validation-mode Bertrand configuration.
        """
        return self.Model.model_json_schema(by_alias=True, mode="validation")
