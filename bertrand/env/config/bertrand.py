"""A configuration resource for Bertrand itself, which defines the build matrix and
other podman-related settings that affect how Bertrand images and containers are built.

The metadata for this resource is expected to be found under the `[bertrand]` key in
project configuration, which is usually provided by `pyproject.toml`.  It is
responsible for rendering the basic directory structure and container/repository
artifacts needed by Bertrand's core functionality.
"""
from __future__ import annotations

import ipaddress
import re
from collections.abc import Callable
from pathlib import Path
from typing import Annotated, Any, Literal, Self

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

from ..run import (
    METADATA_DIR,
    Scalar,
    atomic_write_text,
    sanitize_name,
)
from ..version import VERSION
from .conan import (
    ConanConf,
    ConanConfig,
    ConanOptions,
)
from .core import (
    AbsolutePosixPath,
    Config,
    Glob,
    NoCRLF,
    NonEmpty,
    NoWhiteSpace,
    PosixPath,
    RelativePosixPath,
    Resource,
    TagName,
    Trimmed,
    locate_template,
    resource,
)
from .python import PyProject, _validate_dependency_groups

# Configuration options that affect CLI behavior
DEFAULT_TAG: str = "default"
SHELLS: dict[str, tuple[str, ...]] = {
    # NOTE: values are raw commands that override a container's normal entry point.
    "bash": ("bash", "-l"),
}
DEFAULT_SHELL: str = "bash"
if DEFAULT_SHELL not in SHELLS:
    raise RuntimeError(f"default shell is unsupported: {DEFAULT_SHELL}")
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
        "/Applications/Visual Studio Code - Insiders.app/Contents/Resources/app/bin/code-insiders",

        # WSL/Windows common locations
        "/mnt/c/Program Files/Microsoft VS Code/bin/code.cmd",
        "/mnt/c/Program Files/Microsoft VS Code Insiders/bin/code-insiders.cmd",
    ]
}
DEFAULT_EDITOR: str = "vscode"
if DEFAULT_EDITOR not in EDITORS:
    raise RuntimeError(f"default editor is unsupported: {DEFAULT_EDITOR}")
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
        raise ValueError(
            f"unsupported shell: '{shell}' (supported shells: {', '.join(SHELLS)})"
        )
    return shell


def _check_editor(editor: str) -> str:
    if editor not in EDITORS:
        raise ValueError(
            f"unsupported editor: '{editor}' (supported editors: {', '.join(EDITORS)})"
        )
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
        raise ValueError(f"invalid IP address: {address}") from err


def _check_network_alias(alias: str) -> str:
    alias = alias.lower()
    if alias.startswith(".") or alias.endswith(".") or ".." in alias:
        raise ValueError(
            f"invalid network alias '{alias}' (cannot start/end with '.', or contain '..')"
        )
    for label in alias.split("."):
        if not NETWORK_ALIAS_LABEL_RE.fullmatch(label):
            raise ValueError(
                f"invalid network alias label '{label}' in '{alias}' (labels must "
                "match [a-z0-9-], max length 63, and cannot start/end with '-')"
            )
    return alias


def _check_capability(capability: str) -> str:
    if capability == "ALL":
        return capability
    if not CAPABILITY_TOKEN_RE.fullmatch(capability):
        raise ValueError(
            f"invalid capability token '{capability}' (expected exact CAP_* token or ALL)"
        )
    if LINUX_CAPABILITIES is not None and capability not in LINUX_CAPABILITIES:
        raise ValueError(
            f"unknown Linux capability '{capability}' according to local capability header"
        )
    return capability


def _check_security_opt(option: str) -> str:
    if option == "no-new-privileges":
        return option
    if "=" not in option:
        raise ValueError(
            f"invalid security-opt '{option}' (expected 'no-new-privileges' or 'key=value')"
        )
    key, value = option.split("=", maxsplit=1)
    if not key or not value:
        raise ValueError(f"invalid security-opt '{option}' (missing key or value)")
    if not SECURITY_OPT_KEY_RE.fullmatch(key):
        raise ValueError(f"invalid security-opt key '{key}' in '{option}'")
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
    allow_zero: bool
) -> None:
    if not value.isdigit():
        raise ValueError(
            f"invalid userns '{userns}': {key} must be a non-negative integer"
        )
    number = int(value)
    if not allow_zero and number <= 0:
        raise ValueError(
            f"invalid userns '{userns}': {key} must be greater than zero"
        )


def _check_userns_options(
    *,
    userns: str,
    mode: Literal["keep-id", "auto"],
    options: str
) -> None:
    if not options:
        raise ValueError(f"invalid userns '{userns}': '{mode}' options cannot be empty")

    seen: set[str] = set()
    tokens = options.split(",")
    for token in tokens:
        if not token or "=" not in token:
            raise ValueError(
                f"invalid userns '{userns}': expected comma-separated key=value options"
            )
        key, value = token.split("=", maxsplit=1)
        if not key or not value:
            raise ValueError(
                f"invalid userns '{userns}': expected non-empty key=value options"
            )
        if key in seen:
            raise ValueError(
                f"invalid userns '{userns}': duplicate option key '{key}'"
            )
        seen.add(key)

        if mode == "keep-id":
            if key in ("uid", "gid"):
                _check_userns_uint(userns=userns, key=key, value=value, allow_zero=True)
                continue
            if key == "size":
                _check_userns_uint(userns=userns, key=key, value=value, allow_zero=False)
                continue
            raise ValueError(
                f"invalid userns '{userns}': unsupported keep-id option '{key}' "
                "(allowed: uid, gid, size)"
            )

        if key == "size":
            _check_userns_uint(userns=userns, key=key, value=value, allow_zero=False)
            continue
        if key in ("uidmapping", "gidmapping"):
            match = USERNS_MAPPING_RE.fullmatch(value)
            if match is None:
                raise ValueError(
                    f"invalid userns '{userns}': {key} must be "
                    "'<container-id>:<host-id>:<size>' or '<container-id>:@<host-id>:<size>'"
                )
            length = int(match.group("length"))
            if length <= 0:
                raise ValueError(
                    f"invalid userns '{userns}': {key} mapping size must be greater than zero"
                )
            continue
        raise ValueError(
            f"invalid userns '{userns}': unsupported auto option '{key}' "
            "(allowed: size, uidmapping, gidmapping)"
        )


def _check_userns(userns: str) -> str:
    if userns in ("host", "keep-id", "auto", "nomap"):
        return userns
    if userns.startswith("ns:"):
        if NS_PATH_RE.fullmatch(userns):
            return userns
        raise ValueError(f"invalid userns '{userns}' (expected 'ns:<path>' with no spaces)")
    if userns.startswith("container:"):
        ref = _extract_container_ref(userns)
        if ref is None:
            raise ValueError(
                f"invalid userns '{userns}' (expected 'container:<tag>' with non-empty tag)"
            )
        if not USERNS_CONTAINER_REF_RE.fullmatch(ref):
            raise ValueError(
                f"invalid userns '{userns}' (container tag must use [A-Za-z0-9._-]+)"
            )
        sanitized = sanitize_name(ref)
        if ref != sanitized:
            raise ValueError(
                f"invalid userns '{userns}' (container tag sanitizes to '{sanitized}')"
            )
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
            raise ValueError(
                f"invalid userns '{userns}' ('nomap' does not accept options)"
            )
    raise ValueError(
        f"invalid userns '{userns}' (expected one of: host|keep-id[:<opts>]|"
        "auto[:<opts>]|nomap|container:<tag>|ns:<path>)"
    )


def _check_namespace_mode(
    mode: str,
    *,
    option: str,
    literals: tuple[str, ...],
    allow_empty: bool = False
) -> str:
    if mode == "" and allow_empty:
        return mode
    if not mode:
        raise ValueError(f"{option} entry cannot be empty")
    if mode in literals:
        return mode
    if mode.startswith("ns:"):
        if NS_PATH_RE.fullmatch(mode):
            return mode
        raise ValueError(f"invalid {option} '{mode}' (expected 'ns:<path>' with no spaces)")
    ref = _extract_container_ref(mode)
    if ref is not None:
        if not USERNS_CONTAINER_REF_RE.fullmatch(ref):
            raise ValueError(
                f"invalid {option} '{mode}' (container tag must use [A-Za-z0-9._-]+)"
            )
        sanitized = sanitize_name(ref)
        if ref != sanitized:
            raise ValueError(
                f"invalid {option} '{mode}' (container tag sanitizes to '{sanitized}')"
            )
        return mode
    expected = "|".join(literals)
    empty = '""|' if allow_empty else ""
    raise ValueError(
        f"invalid {option} '{mode}' (expected one of: {empty}{expected}|"
        "container:<tag>|ns:<path>)"
    )


def _check_ipc(ipc: str) -> str:
    return _check_namespace_mode(
        ipc,
        option="ipc",
        literals=("none", "host", "private", "shareable"),
        allow_empty=True
    )


def _check_pid(pid: str) -> str:
    return _check_namespace_mode(
        pid,
        option="pid",
        literals=("host", "private"),
        allow_empty=False
    )


def _check_uts(uts: str) -> str:
    return _check_namespace_mode(
        uts,
        option="uts",
        literals=("host", "private"),
        allow_empty=False
    )


def _check_device_permission(permission: str) -> str:
    if permission not in DEVICE_PERMISSIONS:
        raise ValueError(
            f"invalid device permissions '{permission}' (expected one of: "
            f"{'|'.join(sorted(DEVICE_PERMISSIONS, key=lambda x: len(x)))}"
        )
    return permission


def _check_health_log_destination(value: str) -> str:
    if value in ("local", "events_logger"):
        return value
    if "\\" in value:
        raise ValueError(
            f"invalid healthcheck.log.destination '{value}' "
            "(expected POSIX-style path separators)"
        )
    if any(part in (".", "..") for part in value.split("/")):
        raise ValueError(
            f"invalid healthcheck.log.destination '{value}' "
            "(path cannot contain '.' or '..' segments)"
        )
    path = PosixPath(value)
    if path.is_absolute():
        raise ValueError(
            f"invalid healthcheck.log.destination '{value}' "
            "(path must be project-root-relative)"
        )
    return path.as_posix()


def _check_build_context_path(path: PosixPath) -> PosixPath:
    if path.is_absolute():
        raise ValueError(f"build context path cannot be absolute: '{path}'")
    if path == PosixPath("."):
        return path
    parts = path.parts
    if not parts:
        raise ValueError("build context path cannot be empty")
    if any(p == "." or p == ".." for p in parts):
        raise ValueError(
            "build context path cannot contain '.' or '..' segments: "
            f"'{path}'"
        )
    return path


type Shell = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_shell)]
type Editor = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_editor)]
type IgnoreList = Annotated[list[Glob], AfterValidator(_check_ignore_list)]
type IPAddress = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_ip_address)]
type HostName = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    max_length=253,
    pattern=
        r"^[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?"
        r"(?:\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*$"
)]
type HostIP = Literal["host-gateway"] | IPAddress  # pylint: disable=invalid-name
type BuildArgName = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    pattern=r"^[A-Za-z_][A-Za-z0-9_]*$"
)]
type NetworkAlias = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_network_alias)]
type Memory = Annotated[str, StringConstraints(strip_whitespace=True, pattern=r"^\d+[bkmg]?$")]
type ULimitName = Annotated[
    str,
    StringConstraints(
        strip_whitespace=True,
        min_length=1,
        pattern=r"^[a-z][a-z0-9_]*$"
    ),
]
type Capability = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_capability)]
type SecurityOpt = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_security_opt)]
type UserNS = Annotated[  # pylint: disable=invalid-name
    NonEmpty[NoWhiteSpace],
    AfterValidator(_check_userns)
]
type IPCMode = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_ipc)]
type PIDMode = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_pid)]
type UTSMode = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_uts)]
type InstrumentTool = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    pattern=r"^[A-Za-z0-9][A-Za-z0-9_.-]*$"
)]
type ScreamingSnakeCase = Annotated[str, StringConstraints(
    strip_whitespace=True,
    pattern=r"^[A-Z][A-Z0-9_]*$"
)]
type DevicePermission = Annotated[
    NonEmpty[NoWhiteSpace],
    AfterValidator(_check_device_permission)
]
type Timeout = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    pattern=r"^\d+(\.\d+)?[smhd]?$"
)]
type HealthLogDestination = Annotated[
    NonEmpty[NoCRLF],
    AfterValidator(_check_health_log_destination)
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
    """A resource describing the configuration state needed by Bertrand itself, which
    will be implicitly added to any `bertrand init` command as a base resource, and
    can be universally configured from any config provider (e.g. `pyproject.toml`) via
    the `"bertrand"` snapshot key.

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
        shell: Annotated[Shell, Field(
            default=DEFAULT_SHELL,
            examples=list(SHELLS),
            description=
                "Default shell to use when entering a container via `bertrand enter`.  "
                "This is not a literal shell command, but rather an identifier that "
                "maps to a backend command to prevent remote code execution."
        )]
        editor: Annotated[Editor, Field(
            default=DEFAULT_EDITOR,
            examples=list(EDITORS),
            description=
                "Default text editor to use when invoking `bertrand code`.  This is "
                "not a literal shell command, but rather an identifier that maps to a "
                "backend command to prevent remote code execution."
        )]
        ignore: Annotated[IgnoreList, Field(
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
            examples=[[
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
            ]],
            description=
                "List of patterns to ignore within this project, which are shared "
                "between both `.gitignore` and `.containerignore`.  Patterns are "
                "interpreted using the same rules as those files."
        )]
        git_ignore: Annotated[IgnoreList, Field(
            default_factory=list,
            examples=[[]],
            alias="git-ignore",
            description=
                "List of `.gitignore`-specific patterns, which are merged with the "
                "global `ignore` patterns when generating `.gitignore`."
        )]
        container_ignore: Annotated[IgnoreList, Field(
            default_factory=lambda: [
                ".git/",
                ".gitignore",
            ],
            examples=[[".git/", ".gitignore",]],
            alias="container-ignore",
            description=
                "List of `.containerignore`-specific patterns, which are merged with "
                "the global `ignore` patterns when generating `.containerignore`."
        )]

        class Network(BaseModel):
            """Validate the `[bertrand.network]` table."""
            model_config = ConfigDict(extra="forbid")

            class Table(BaseModel):
                """Validate common fields in `[bertrand.network.*]` tables."""
                model_config = ConfigDict(extra="forbid")
                mode: Annotated[
                    str,
                    StringConstraints(
                        strip_whitespace=True,
                        min_length=1,
                        pattern=
                            rf"^(none|host|private|slirp4netns|pasta|{NS_PATH_RE.pattern})$",
                    ),
                    Field(
                        default="private",
                        examples=["none", "host", "private", "slirp4netns", "pasta", "ns:<path>"],
                        description=
                            "The networking driver to use within containers for this "
                            "project.  Equivalent to `podman build|create --network`\n"
                            "   `none`: disable networking within the container.\n"
                            "   `host`: use the host's network stack directly (best "
                            "performance, no isolation, potentially insecure).\n"
                            "   `private`: (default) create a new, private network "
                            "namespace for the container.\n"
                            "   `slirp4netns`: use slirp4netns for rootless networking.  "
                            "This is often the default for many rootless container "
                            "runtimes, but may be slower than other options and has "
                            "some limitations.  See the podman documentation for more "
                            "details.\n"
                            "   `pasta`: use pasta for rootless networking.  This is "
                            "slightly faster than slirp4netns in some scenarios, but is "
                            "still slower than rootful host networking.  See the podman "
                            "documentation for more details.\n"
                            "   `ns:<path>`: join an existing network namespace specified "
                            "by the given <path>.\n"
                            "Bertrand intentionally omits `bridge`, `<network name|ID>`, "
                            "and `container:id` from its configuration layer in order to "
                            "keep projects portable across hosts.",
                    )
                ]
                options: Annotated[list[str], Field(
                    default_factory=list,
                    examples=[[
                        "--ipv4-only",
                        "-a", "10.0.2.0",
                        "-n", "24",
                        "-g", "10.0.2.2",
                        "--dns-forward", "10.0.2.3",
                        "-m", "1500",
                        "--no-ndp",
                        "--no-dhcpv6",
                        "--no-dhcp",
                    ]],
                    description=
                        "Additional `--network` mode options, encoded as "
                        "`mode:opt1,opt2,...`.  These are forwarded to the selected "
                        "network backend.  In Bertrand's global networking contract, "
                        "options are only valid for `slirp4netns` and `pasta` modes.  "
                        "See the podman documentation for more details.",
                )]
                dns: Annotated[list[IPAddress | Literal["none"]], Field(
                    default_factory=list,
                    description=
                        "Set custom DNS servers.  Equivalent to "
                        "`podman build|create --dns`.  The special value `none` disables "
                        "creation of `/etc/resolv.conf` by Podman, so the image's "
                        "`/etc/resolv.conf` is used unchanged.  For builds, this setting "
                        "only affects `RUN` instructions and does not change "
                        "`/etc/resolv.conf` in the final image.",
                )]
                dns_search: Annotated[list[NonEmpty[NoWhiteSpace]], Field(
                    default_factory=list,
                    alias="dns-search",
                    description=
                        "Set custom DNS search domains.  Equivalent to "
                        "`podman build|create --dns-search`.  For builds, this setting "
                        "only affects `RUN` instructions and does not change "
                        "`/etc/resolv.conf` in the final image.",
                )]
                dns_options: Annotated[list[NonEmpty[NoWhiteSpace]], Field(
                    default_factory=list,
                    alias="dns-options",
                    description=
                        "Set custom DNS resolver options.  Equivalent to "
                        "`podman build|create --dns-option`.  For builds, this setting "
                        "only affects `RUN` instructions and does not change "
                        "`/etc/resolv.conf` in the final image.",
                )]
                add_host: Annotated[dict[HostName, HostIP], Field(
                    default_factory=dict,
                    alias="add-host",
                    description=
                        "Mapping of additional host entries to add to container "
                        "`/etc/hosts`.  Equivalent to `podman build|create --add-host`.  "
                        "Keys are hostnames, and values are IPv4/IPv6 addresses or the "
                        "special value `host-gateway`.",
                )]

                @model_validator(mode="after")
                def _validate_none_mode(self) -> Self:
                    if self.mode == "none" and (
                        self.options or
                        self.dns or
                        self.dns_search or
                        self.dns_options
                    ):
                        raise ValueError(
                            "network mode 'none' requires empty options, dns, "
                            "dns-search, and dns-options"
                        )
                    return self

                @model_validator(mode="after")
                def _validate_driver_options_mode(self) -> Self:
                    if self.options and self.mode not in ("slirp4netns", "pasta"):
                        raise ValueError(
                            "network options are only allowed for 'slirp4netns' and "
                            "'pasta' modes"
                        )
                    return self

                @model_validator(mode="after")
                def _validate_dns_none(self) -> Self:
                    if "none" in self.dns and len(self.dns) > 1:
                        raise ValueError(
                            "dns entry 'none' cannot be combined with other DNS servers"
                        )
                    return self

            class Build(Table):
                """Validate the `[bertrand.network.build]` table."""

                @model_validator(mode="after")
                def _validate_build_table(self) -> Self:
                    # TODO: build-specific networking restrictions should be added
                    # here when build and run contracts diverge further.
                    return self

            class Run(Table):
                """Validate the `[bertrand.network.run]` table."""

                @model_validator(mode="after")
                def _validate_run_table(self) -> Self:
                    # TODO: run-specific networking restrictions should be added
                    # here when runtime networking coverage expands.
                    return self

            build: Annotated[Build, Field(
                default_factory=Build.model_construct,
                description=
                    "Global networking policy to use during build-time `RUN` "
                    "instructions.",
            )]
            run: Annotated[Run, Field(
                default_factory=Run.model_construct,
                description=
                    "Global networking policy to use during container creation and "
                    "runtime execution.",
            )]

        network: Annotated[Network, Field(
            default_factory=Network.model_construct,
            description="Networking configuration to use within this project.",
        )]

        # TODO: continue documenting and pruning the tags

        class Tag(BaseModel):
            """Validate entries in the `[[tool.bertrand.tags]]` table."""
            model_config = ConfigDict(extra="forbid")
            tag: TagName
            containerfile: Annotated[
                RelativePosixPath | None,
                Field(default=None)
            ]
            build_args: Annotated[
                dict[BuildArgName, Scalar],
                Field(default_factory=dict, alias="build-args")
            ]
            entry_point: Annotated[
                list[NonEmpty[Trimmed]],
                Field(default_factory=list, alias="entry-point")
            ]

            class Port(BaseModel):
                """Validate entries in the `[[bertrand.tags.ports]]` table."""
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
                        raise ValueError(
                            "duplicate published port binding for "
                            f"{port.host_ip}:{port.host}/{port.protocol}"
                        )
                    seen.add(key)
                return ports

            @staticmethod
            def _check_network_aliases(aliases: list[NetworkAlias]) -> list[NetworkAlias]:
                seen: set[NetworkAlias] = set()
                for alias in aliases:
                    if alias in seen:
                        raise ValueError(f"duplicate network alias: '{alias}'")
                    seen.add(alias)
                return aliases

            ports: Annotated[
                list[Port],
                AfterValidator(_check_ports),
                Field(default_factory=list)
            ]
            network_aliases: Annotated[
                list[NetworkAlias],
                AfterValidator(_check_network_aliases),
                Field(default_factory=list, alias="network-aliases")
            ]
            cpus: Annotated[NonNegativeFloat, Field(default=0.0)]
            memory: Annotated[Memory, Field(default="0")]
            pids_limit: Annotated[
                int,
                Field(default=0, ge=-1, alias="pids-limit")
            ]
            shm_size: Annotated[Memory, Field(default="64m", alias="shm-size")]

            class ULimit(BaseModel):
                """Validate entries in the `[[bertrand.tags.ulimit]]` table."""
                model_config = ConfigDict(extra="forbid")
                name: ULimitName
                soft: Annotated[int | None, Field(default=None, ge=-1)]
                hard: Annotated[int | None, Field(default=None, ge=-1)]

                @model_validator(mode="after")
                def _validate_limits(self) -> Self:
                    if self.name == "host":
                        if self.soft is not None or self.hard is not None:
                            raise ValueError(
                                "ulimit name 'host' cannot define 'soft' or 'hard' values"
                            )
                        return self
                    if self.soft is None or self.hard is None:
                        raise ValueError(
                            "non-'host' ulimit entries must define both 'soft' and 'hard'"
                        )
                    if self.hard >= 0 and self.soft > self.hard:
                        raise ValueError(
                            f"ulimit soft value {self.soft} cannot be greater than hard "
                            f"value {self.hard}"
                        )
                    return self

            @staticmethod
            def _check_ulimit(entries: list[ULimit]) -> list[ULimit]:
                seen: set[str] = set()
                for entry in entries:
                    if entry.name in seen:
                        raise ValueError(f"duplicate ulimit name: '{entry.name}'")
                    seen.add(entry.name)
                return entries

            @staticmethod
            def _check_unique(value: list[str], *, where: str) -> list[str]:
                seen: set[str] = set()
                for item in value:
                    if item in seen:
                        raise ValueError(f"duplicate {where}: '{item}'")
                    seen.add(item)
                return value

            ulimit: Annotated[
                list[ULimit],
                AfterValidator(_check_ulimit),
                Field(default_factory=list)
            ]
            cap_add: Annotated[
                list[Capability],
                AfterValidator(lambda x: Bertrand.Model.Tag._check_unique(
                    x,
                    where="cap-add capability"
                )),
                Field(default_factory=list, alias="cap-add")
            ]
            cap_drop: Annotated[
                list[Capability],
                AfterValidator(lambda x: Bertrand.Model.Tag._check_unique(
                    x,
                    where="cap-drop capability"
                )),
                Field(default_factory=list, alias="cap-drop")
            ]
            security_opt: Annotated[
                list[SecurityOpt],
                AfterValidator(lambda x: Bertrand.Model.Tag._check_unique(
                    x,
                    where="security-opt entry"
                )),
                Field(default_factory=list, alias="security-opt")
            ]
            userns: Annotated[UserNS, Field(default="host")]
            ipc: Annotated[IPCMode, Field(default="private")]
            pid: Annotated[PIDMode, Field(default="private")]
            uts: Annotated[UTSMode, Field(default="private")]
            ssh: Annotated[list[ScreamingSnakeCase], Field(default_factory=list)]

            class InstrumentEntry(BaseModel):
                """Validate entries in the `[[bertrand.tags.instruments]]` AoT."""
                model_config = ConfigDict(extra="allow")
                tool: InstrumentTool

            instruments: Annotated[list[InstrumentEntry], Field(default_factory=list)]

            @model_validator(mode="after")
            def _validate_capability_conflicts(self) -> Self:
                if "ALL" in self.cap_add and len(self.cap_add) > 1:
                    raise ValueError(
                        "cap-add cannot combine 'ALL' with specific capabilities"
                    )
                if "ALL" in self.cap_drop and len(self.cap_drop) > 1:
                    raise ValueError(
                        "cap-drop cannot combine 'ALL' with specific capabilities"
                    )
                overlap = set(cap for cap in self.cap_add if cap != "ALL")
                overlap = overlap.intersection(
                    cap for cap in self.cap_drop if cap != "ALL"
                )
                if overlap:
                    raise ValueError(
                        "cap-add and cap-drop cannot contain the same capability: "
                        f"{', '.join(sorted(overlap))}"
                    )
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
                """Validate the `[bertrand.tags.devices]` table."""
                model_config = ConfigDict(extra="forbid")

                class Request(BaseModel):
                    """Validate one entry in `[[bertrand.tags.devices.*]]`."""
                    model_config = ConfigDict(extra="forbid")
                    id: ScreamingSnakeCase
                    required: bool = True
                    container_path: Annotated[
                        AbsolutePosixPath | None,
                        Field(default=None, alias="container-path")
                    ]
                    permissions: Annotated[DevicePermission, Field(default="rwm")]

                @staticmethod
                def _check_unique_ids(requests: list[Request], *, where: str) -> list[Request]:
                    seen: set[ScreamingSnakeCase] = set()
                    for req in requests:
                        if req.id in seen:
                            raise ValueError(f"duplicate {where} device id: '{req.id}'")
                        seen.add(req.id)
                    return requests

                build: Annotated[
                    list[Request],
                    AfterValidator(
                        lambda x: Bertrand.Model.Tag.Devices._check_unique_ids(
                            x,
                            where="build"
                        )
                    ),
                    Field(default_factory=list)
                ]
                run: Annotated[
                    list[Request],
                    AfterValidator(
                        lambda x: Bertrand.Model.Tag.Devices._check_unique_ids(
                            x,
                            where="run"
                        )
                    ),
                    Field(default_factory=list)
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
                """Validate the `[tool.bertrand.tags.secrets]` table."""
                model_config = ConfigDict(extra="forbid")

                class Request(BaseModel):
                    """Validate an individual secret capability request."""
                    model_config = ConfigDict(extra="forbid")
                    id: ScreamingSnakeCase
                    required: bool = True

                @staticmethod
                def _check_unique_ids(requests: list[Request], *, where: str) -> list[Request]:
                    seen: set[ScreamingSnakeCase] = set()
                    for req in requests:
                        if req.id in seen:
                            raise ValueError(f"duplicate {where} secret id: '{req.id}'")
                        seen.add(req.id)
                    return requests

                build: Annotated[
                    list[Request],
                    AfterValidator(
                        lambda x: Bertrand.Model.Tag.Secrets._check_unique_ids(
                            x,
                            where="build"
                        )
                    ),
                    Field(default_factory=list)
                ]
                run: Annotated[
                    list[Request],
                    AfterValidator(
                        lambda x: Bertrand.Model.Tag.Secrets._check_unique_ids(
                            x,
                            where="run"
                        )
                    ),
                    Field(default_factory=list)
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
                """Validate the `[bertrand.tags.conan]` table."""
                model_config = ConfigDict(extra="forbid")
                build_type: Annotated[
                    Literal["", "Release", "Debug"],
                    Field(default="", alias="build-type")
                ]
                conf: Annotated[ConanConf, Field(default_factory=dict)]
                options: Annotated[ConanOptions, Field(default_factory=dict)]
                requires: Annotated[
                    list[ConanConfig.Model.Require],
                    AfterValidator(ConanConfig.Model._check_requires),
                    Field(default_factory=list)
                ]

            conan: Annotated[Conan, Field(default_factory=Conan.model_construct)]

            class Build(BaseModel):
                """Validate the `[bertrand.tags.build]` table."""
                model_config = ConfigDict(extra="forbid")
                context: Annotated[BuildContextPath, Field(default=PosixPath("."))]
                target: Annotated[
                    NoWhiteSpace,
                    StringConstraints(pattern=r"^[a-zA-Z0-9_-]*$"),
                    Field(default="")
                ]
                pull: Annotated[
                    Literal["missing", "always", "never", "newer"],
                    Field(default="missing")
                ]

            build: Annotated[Build, Field(default_factory=Build.model_construct)]

            class Stop(BaseModel):
                """Validate the `[bertrand.tags.stop]` table."""
                model_config = ConfigDict(extra="forbid")
                signal: Annotated[
                    str,
                    StringConstraints(strip_whitespace=True, min_length=1, pattern=r"^\S+$"),
                    Field(default="SIGTERM")
                ]
                timeout: Annotated[NonNegativeInt, Field(default=10)]

            stop: Annotated[Stop, Field(default_factory=Stop.model_construct)]

            class Restart(BaseModel):
                """Validate the `[bertrand.tags.restart]` table."""
                model_config = ConfigDict(extra="forbid")
                policy: Annotated[
                    Literal["no", "on-failure", "always", "unless-stopped"],
                    Field(default="no")
                ]
                max_retries: Annotated[NonNegativeInt, Field(default=0, alias="max-retries")]

            restart: Annotated[Restart, Field(default_factory=Restart.model_construct)]

            class Healthcheck(BaseModel):
                """Validate the `[bertrand.tags.healthcheck]` table."""
                model_config = ConfigDict(extra="forbid")
                cmd: Annotated[list[str], Field(default_factory=list)]
                on_failure: Annotated[
                    Literal["none", "kill", "stop"],
                    Field(default="kill", alias="on-failure")
                ]
                retries: Annotated[NonNegativeInt, Field(default=3)]
                interval: Annotated[Timeout, Field(default="30s")]
                timeout: Annotated[Timeout, Field(default="30s")]

                class Startup(BaseModel):
                    """Validate the `[bertrand.tags.healthcheck.startup]` table."""
                    model_config = ConfigDict(extra="forbid")
                    cmd: Annotated[list[str], Field(default_factory=list)]
                    period: Annotated[Timeout, Field(default="0s")]
                    success: Annotated[NonNegativeInt, Field(default=0)]
                    interval: Annotated[Timeout, Field(default="30s")]
                    timeout: Annotated[Timeout, Field(default="30s")]

                startup: Annotated[Startup, Field(default_factory=Startup.model_construct)]

                class Log(BaseModel):
                    """Validate the `[bertrand.tags.healthcheck.log]` table."""
                    model_config = ConfigDict(extra="forbid")
                    destination: Annotated[HealthLogDestination, Field(default="local")]
                    max_count: Annotated[NonNegativeInt, Field(default=0, alias="max-count")]
                    max_size: Annotated[NonNegativeInt, Field(default=0, alias="max-size")]

                log: Annotated[Log, Field(default_factory=Log.model_construct)]

            healthcheck: Annotated[
                Healthcheck,
                Field(default_factory=Healthcheck.model_construct)
            ]

            def resolve_containerfile(self, root: Path) -> None:
                if self.containerfile is None:
                    return
                path = root / self.containerfile
                suffix = f" for tag '{self.tag}'" if self.tag else ""
                if not path.exists():
                    raise OSError(f"path does not exist{suffix}: {path}")
                if not path.is_file():
                    raise OSError(f"path is not a file{suffix}: {path}")
                try:
                    path.read_text(encoding="utf-8")
                except UnicodeDecodeError as err:
                    raise OSError(f"file is not UTF-8 encoded{suffix}: {path}") from err

        tags: Annotated[list[Tag], Field(default_factory=lambda: [
            Bertrand.Model.Tag.model_construct(tag=DEFAULT_TAG)
        ])]

        @model_validator(mode="after")
        def _validate_tags(self) -> Self:
            seen: set[str] = set()
            for tag in self.tags:
                if tag.tag in seen:
                    raise ValueError(
                        f"duplicate tag name in 'tool.bertrand.tags': '{tag.tag}'"
                    )
                seen.add(tag.tag)
            if DEFAULT_TAG not in seen:
                raise ValueError(
                    "missing required default tag in 'tool.bertrand.tags': "
                    f"'{DEFAULT_TAG}'"
                )
            return self

        @model_validator(mode="after")
        def _validate_services(self) -> Self:
            unknown_services: list[str] = []
            for idx, service in enumerate(self.services):
                if any(prev == service for prev in self.services[:idx]):
                    raise ValueError(
                        "duplicate service name in 'tool.bertrand.services': "
                        f"'{service}'"
                    )
                if not any(tag.tag == service for tag in self.tags):
                    unknown_services.append(service)
            if unknown_services:
                raise ValueError(
                    "found service names in 'tool.bertrand.services' with no "
                    f"matching tag in 'tool.bertrand.tags': "
                    f"{', '.join(unknown_services)}"
                )
            return self

        @model_validator(mode="after")
        def _validate_namespace_refs(self) -> Self:
            for tag in self.tags:
                # if the current tag is a service, get its position in the list
                curr_pos = next(
                    (pos for pos, name in enumerate(self.services) if name == tag.tag),
                    None
                )

                # for each namespace field that references an external tag, ensure
                # that the tag it references is a valid service
                for option, mode in (
                    ("userns", tag.userns),
                    ("ipc", tag.ipc),
                    ("pid", tag.pid),
                    ("uts", tag.uts),
                ):
                    ref = _extract_container_ref(mode)
                    if ref is None:
                        continue

                    # outlaw self-references
                    if ref == tag.tag:
                        raise ValueError(
                            f"{option} for tag '{tag.tag}' cannot reference "
                            f"itself via 'container:{ref}'"
                        )

                    # get referenced service position + tag
                    ref_pos = next(
                        (pos for pos, name in enumerate(self.services) if name == ref),
                        None
                    )
                    if ref_pos is None:
                        raise ValueError(
                            f"{option} for tag '{tag.tag}' references '{ref}', "
                            f"but '{ref}' is not listed in "
                            "'tool.bertrand.services'"
                        )
                    ref_tag = next((t for t in self.tags if t.tag == ref), None)
                    if ref_tag is None:
                        raise ValueError(
                            f"{option} for tag '{tag.tag}' references unknown "
                            f"tag '{ref}'"
                        )

                    # enforce correct startup ordering
                    if curr_pos is not None and ref_pos >= curr_pos:
                        raise ValueError(
                            f"{option} for service tag '{tag.tag}' references "
                            f"'container:{ref}', but '{ref}' must appear earlier "
                            f"than '{tag.tag}' in 'tool.bertrand.services'"
                        )

                    # ipc requires the referenced tag uses ipc=shareable
                    if option == "ipc" and ref_tag.ipc != "shareable":
                        raise ValueError(
                            f"ipc for tag '{tag.tag}' uses 'container:{ref}', "
                            f"but referenced tag '{ref}' must set ipc='shareable'"
                        )
            return self

    async def init(self, config: Config, cli: Config.Init) -> dict[str, Any]:
        return self.Model.model_construct().model_dump(by_alias=True)

    async def validate(self, config: Config, fragment: Any) -> Model | None:
        result = self.Model.model_validate(fragment)
        _validate_dependency_groups(pyproject=config.get(PyProject), bertrand=result)
        for tag in result.tags:
            tag.resolve_containerfile(config.root)
        return result

    async def render(self, config: Config, tag: str | None) -> None:
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
        ignore = [str(METADATA_DIR / "*")]  # always ignore Bertrand's metadata directory
        ignore.extend(bertrand.ignore)
        containerignore = ignore.copy()
        containerignore.extend(bertrand.container_ignore)
        atomic_write_text(
            config.root / ".containerignore",
            _dump_ignore_list(containerignore),
            encoding="utf-8"
        )
        gitignore = ignore.copy()
        gitignore.extend(bertrand.git_ignore)
        atomic_write_text(
            config.root / ".gitignore",
            _dump_ignore_list(gitignore),
            encoding="utf-8"
        )

        # initialize CI publish action
        publish_template = jinja.from_string(
            locate_template("core", "publish.v1").read_text(encoding="utf-8")
        )
        publish_target = config.root / ".github" / "workflows" / "publish.yml"
        publish_target.parent.mkdir(parents=True, exist_ok=True)
        publish_target.write_text(publish_template.render(
            python_major=python_version.major,
            python_minor=python_version.minor,
            python_patch=python_version.micro,
            bertrand_major=bertrand_version.major,
            bertrand_minor=bertrand_version.minor,
            bertrand_patch=bertrand_version.micro,
        ), encoding="utf-8")

    async def schema(self) -> dict[str, Any]:
        return self.Model.model_json_schema(by_alias=True, mode="validation")
