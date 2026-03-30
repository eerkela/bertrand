"""Layout schema and init/build-time rendering for Bertrand environments.

Config resources can implement any combination of the following 4 orchestration hooks:

    1.  `init(ctx)`, which renders the initial state of the resource as a snapshot
        fragment from normalized `bertrand init` CLI input.
    2.  `parse(config)`, which requires at least one path, reads file(es) from disk,
        and loads the results into a shared snapshot during `Config.__aenter__()`,
        merging with results from the previous step.
    3.  `validate(config, fragment)`, which is invoked for every resource whose name
        appears as a primary key in the merged snapshot.  The result of this method
        must be a pydantic model that will be stored under `config.tool` as a validated
        attribute with the same name as the resource.
    4.  `render(config, tag)`, which writes the validated config state back to disk
        during `bertrand build`.

Resources are free to define any combination of these hooks with whatever logic they
require, as long as they do not conflict with each other.  The only special resource
is `"bertrand"` itself, which is implicitly added to all configurations in order to
bootstrap the bertrand CLI.
"""
from __future__ import annotations

import json
import ipaddress
import os
import re
import string
from dataclasses import dataclass, field, fields as dataclass_fields
from importlib import resources as importlib_resources
from pathlib import Path, PosixPath
from types import TracebackType
from typing import Annotated, Any, Callable, Literal, Self, Sequence

from conan.api.model.list import ListPattern, VersionRange
from conan.api.model.refs import RecipeReference
from conan.errors import ConanException
from conan.internal.model.conf import ConfDefinition
from email_validator import EmailNotValidError, validate_email
from packaging.licenses import InvalidLicenseExpression, canonicalize_license_expression
from packaging.requirements import InvalidRequirement, Requirement
from packaging.specifiers import Specifier, InvalidSpecifier
from packaging.utils import InvalidName, canonicalize_name
import packaging.version
from pydantic import (
    AfterValidator,
    AnyHttpUrl,
    BaseModel,
    ConfigDict,
    NonNegativeInt,
    NonNegativeFloat,
    PositiveInt,
    StringConstraints,
    TypeAdapter,
    ValidationError,
    Field,
    model_validator
)
import jinja2
import tomlkit
from tomlkit.exceptions import TOMLKitError
import yaml

from .run import (
    BERTRAND_ENV,
    CONTAINER_TMP_MOUNT,
    ENV_ID_ENV,
    IMAGE_TAG_ENV,
    LOCK_TIMEOUT,
    METADATA_DIR,
    WORKTREE_MOUNT,
    GitRepository,
    Scalar,
    atomic_write_text,
    inside_container,
    inside_image,
    lock_worktree,
    run,
    sanitize_name,
)
from .version import __version__, VERSION

# pylint: disable=bare-except


# TODO: maybe cache paths should be provided by each resource, so that they can also
# be folded into the resource contract, along with rendering sections in
# `pyproject.toml` depending on the environment's capabilities?


# Canonical path definitions for worktree control
VSCODE_WORKSPACE_FILE: PosixPath = PosixPath(".vscode/vscode.code-workspace")
CACHE_MOUNT: PosixPath = PosixPath("/tmp/.cache")
CCACHE_CACHE: PosixPath = CACHE_MOUNT / "ccache"
BERTRAND_CACHE: PosixPath = CACHE_MOUNT / "bertrand"
UV_CACHE: PosixPath = CACHE_MOUNT / "uv"
CONAN_CACHE: PosixPath = PosixPath("/opt/conan")
CONAN_HOME: PosixPath = PosixPath("/opt/conan")


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


# Validation primitives for config fields
RESOURCE_NAME_RE = re.compile(r"^[a-z]([a-z0-9_]*[a-z0-9])?$")
ALIAS_NAME_RE = re.compile(r"^[a-z]([a-z0-9_.-]*[a-z0-9])?$")
KUBE_MAX_LENGTH = 63
GLOB_RE = re.compile(r"^[A-Za-z0-9._/\-\*\?\[\]!]+$")
HTTP_URL = TypeAdapter(AnyHttpUrl)
NS_PATH_RE = re.compile(r"^ns:\S+$")
NETWORK_ALIAS_LABEL_RE = re.compile(r"^(?!-)[a-z0-9-]{1,63}(?<!-)$")
USERNS_CONTAINER_REF_RE = re.compile(r"^[A-Za-z0-9._-]+$")
USERNS_MAPPING_RE = re.compile(r"^(?P<container>\d+):(?P<host>@?\d+):(?P<length>\d+)$")
CAPABILITY_TOKEN_RE = re.compile(r"^CAP_[A-Z0-9_]+$")
CAPABILITY_DEFINE_RE = re.compile(r"^\s*#define\s+(CAP_[A-Z0-9_]+)\s+([0-9]+)\b")
SECURITY_OPT_KEY_RE = re.compile(r"^[a-z0-9][a-z0-9_.-]*$")
CONAN_REF_TOKEN_RE = re.compile(r"^[a-z0-9_][a-z0-9_+.-]{1,100}\Z")
KUBE_SUB_RE = re.compile(r"[^a-z0-9-]+")
KUBE_TRIM_RE = re.compile(r"^-+|(?<=-)-+|-+$")
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


def _check_pep440_specifier(version: str) -> str:
    try:
        Specifier(version)
    except InvalidSpecifier as err:
        raise ValueError(f"invalid PEP 440 requires-python specifier: {version}") from err
    return version


def _check_license(expression: str) -> str:
    try:
        return canonicalize_license_expression(expression)
    except InvalidLicenseExpression as err:
        raise ValueError(f"invalid PEP 639 SPDX license expression: {expression}") from err


def _check_glob(pattern: str) -> str:
    if not GLOB_RE.fullmatch(pattern):
        raise ValueError(f"invalid glob pattern: '{pattern}'")
    if pattern.startswith("/"):
        raise ValueError(f"glob pattern cannot be absolute: '{pattern}'")
    if any(part in ("..", ".") for part in pattern.split("/")):
        raise ValueError(f"glob pattern cannot contain '.' or '..' segments: '{pattern}'")
    return pattern


def _check_email(email: str) -> str:
    try:
        return validate_email(email, check_deliverability=False).normalized
    except EmailNotValidError as err:
        raise ValueError(f"invalid email address: {email}") from err


def _check_url(url: str) -> str:
    try:
        return str(HTTP_URL.validate_python(url))
    except ValidationError as err:
        raise ValueError(f"invalid URL: {url}") from err


def _check_url_label(label: str) -> str:
    chars_to_remove = string.punctuation + string.whitespace
    removal_map = str.maketrans("", "", chars_to_remove)
    return label.translate(removal_map).lower()


def _check_pep508_requirement(requirement: str) -> str:
    try:
        return str(Requirement(requirement))
    except InvalidRequirement as err:
        raise ValueError(f"invalid PEP 508 requirement: {requirement}") from err


def _check_pep508_name(name: str) -> str:
    try:
        return canonicalize_name(name, validate=True)
    except InvalidName as err:
        raise ValueError(f"invalid PEP 508 name: {name}") from err


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


def _deduplicate_ignore_list(ignore: list[Glob]) -> list[Glob]:
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


def _check_host_ip(address: str) -> str:
    if address == "host-gateway":
        return address
    return _check_ip_address(address)


def _check_sanitized_name(name: str) -> str:
    sanitized = sanitize_name(name)
    if name != sanitized:
        raise OSError(f"invalid name: '{name}' (sanitizes to: '{sanitized}')")
    return name


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


def _check_text_file(path: Path, *, tag: str | None = None) -> None:
    suffix = f" for tag '{tag}'" if tag else ""
    if not path.exists():
        raise OSError(f"path does not exist{suffix}: {path}")
    if not path.is_file():
        raise OSError(f"path is not a file{suffix}: {path}")
    try:
        path.read_text(encoding="utf-8")
    except UnicodeDecodeError as err:
        raise OSError(f"file is not UTF-8 encoded{suffix}: {path}") from err


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
            f"{'|'.join(sorted(DEVICE_PERMISSIONS, key=len))})"
        )
    return permission


def _check_conan_requirement(requirement: str) -> str:
    # Use conan's own parser to validate the requirement string and extract/verify its
    # components
    try:
        ref = RecipeReference.loads(requirement)
    except ConanException as err:
        raise ValueError(
            f"invalid conan requirement '{requirement}' "
            "(expected name/version[@user/channel][#revision])"
        ) from err
    if ref.timestamp is not None:
        raise ValueError(
            f"invalid conan requirement '{requirement}' (timestamp suffixes are not "
            "supported)"
        )
    if not CONAN_REF_TOKEN_RE.fullmatch(ref.name):
        raise ValueError(
            f"invalid conan package name '{ref.name}' in requirement '{requirement}'"
        )
    if ref.user and not CONAN_REF_TOKEN_RE.fullmatch(ref.user):
        raise ValueError(
            f"invalid conan package user '{ref.user}' in requirement '{requirement}'"
        )
    if ref.channel and not CONAN_REF_TOKEN_RE.fullmatch(ref.channel):
        raise ValueError(
            f"invalid conan package channel '{ref.channel}' in requirement '{requirement}'"
        )

    # validate version ranges
    version = repr(ref.version)
    if version.startswith("(") and version.endswith(")"):
        raise ValueError(
            f"invalid conan requirement '{requirement}' (alias references are not "
            "supported)"
        )
    if version.startswith("[") and version.endswith("]"):
        expression = version[1:-1].strip()
        if not expression:
            raise ValueError(
                f"invalid conan requirement '{requirement}' (empty version range)"
            )
        try:
            VersionRange(expression)
        except ConanException as err:
            raise ValueError(
                f"invalid conan version range in requirement '{requirement}'"
            ) from err
    else:
        try:
            ref.validate_ref()
        except ConanException as err:
            raise ValueError(f"invalid conan requirement '{requirement}'") from err

    # return normalized requirement string and strip any timestamp suffix
    return ref.repr_notime()


def _check_conan_conf(
    conf: dict[ConanConfNamespace, dict[ConanConfName, ConanConfValue]]
) -> None:
    for namespace, values in conf.items():
        for key, value in values.items():
            entry = f"{namespace}:{key}"
            conf_def = ConfDefinition()
            try:
                conf_def.update(entry, value, profile=True)
            except ConanException as err:
                raise ValueError(
                    f"invalid conan conf entry '{entry}' with value {value!r}"
                ) from err


def _check_conan_allowed_pattern(pattern: str) -> str:
    if pattern.startswith(("!", "~")) or pattern == "&":
        raise ValueError(
            f"invalid conan allowed-packages pattern '{pattern}' (negation/consumer "
            "patterns are not supported)"
        )

    # Use conan's own parser to validate the pattern string and extract/verify its
    # components.  We require at least a name and version component
    try:
        parsed = ListPattern(pattern, only_recipe=True)
    except ConanException as err:
        raise ValueError(f"invalid conan allowed-packages pattern '{pattern}'") from err
    if not parsed.name or not parsed.version:
        raise ValueError(
            f"invalid conan allowed-packages pattern '{pattern}' (expected a recipe "
            "pattern with name/version)"
        )
    return pattern


def _check_regex_pattern(value: str) -> str:
    try:
        re.compile(value)
    except re.error as err:
        raise ValueError(f"invalid regex pattern '{value}': {err}") from err
    return value


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


type NonEmpty[SequenceT: Sequence[Any]] = Annotated[SequenceT, Field(min_length=1)]
type ResourceName = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    pattern=RESOURCE_NAME_RE.pattern
)]
type AliasName = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    pattern=ALIAS_NAME_RE.pattern
)]
type ConanConfValue = Scalar | list[ConanConfValue] | dict[str, ConanConfValue]
type Trimmed = Annotated[str, StringConstraints(strip_whitespace=True)]
type NoCRLF = Annotated[  # pylint: disable=invalid-name
    str,
    StringConstraints(strip_whitespace=True, pattern=r"^[^\r\n]*$")
]
type NoWhiteSpace = Annotated[
    str,
    StringConstraints(strip_whitespace=True, pattern=r"^\S*$")
]
type RegexPattern = Annotated[NonEmpty[NoCRLF], AfterValidator(_check_regex_pattern)]
type Glob = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_glob)]
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
type URL = Annotated[  # pylint: disable=invalid-name
    NonEmpty[NoCRLF],
    AfterValidator(_check_url)
]
type URLLabel = Annotated[NonEmpty[Trimmed], AfterValidator(_check_url_label)]
type PEP508Requirement = Annotated[
    NonEmpty[Trimmed],
    AfterValidator(_check_pep508_requirement)
]
type PEP508Name = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_pep508_name)]
type TagName = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    pattern=r"^[a-z0-9]+(?:-[a-z0-9]+)*$"
)]
type KubeName = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    max_length=KUBE_MAX_LENGTH,
    pattern=r"^[a-z0-9]+(?:-[a-z0-9]+)*$"
)]
type KubeLabelValue = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    max_length=KUBE_MAX_LENGTH,
    pattern=r"^[A-Za-z0-9](?:[-_.A-Za-z0-9]*[A-Za-z0-9])?$"
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
type Shell = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_shell)]
type Editor = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_editor)]
type IgnoreList = Annotated[list[Glob], AfterValidator(_deduplicate_ignore_list)]
type NetworkMode = Annotated[
    str,
    StringConstraints(
        strip_whitespace=True,
        min_length=1,
        pattern=rf"^(none|host|private|slirp4netns|pasta|{NS_PATH_RE.pattern})$"
    ),
]
type IPAddress = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_ip_address)]
type HostIP = Annotated[  # pylint: disable=invalid-name
    NonEmpty[NoWhiteSpace],
    AfterValidator(_check_host_ip)
]
type HostName = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    max_length=253,
    pattern=
        r"^[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?"
        r"(?:\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*$"
)]
type SanitizedName = Annotated[str, AfterValidator(_check_sanitized_name)]
type AbsolutePath = Annotated[Path, AfterValidator(_check_absolute_path)]
type AbsolutePosixPath = Annotated[PosixPath, AfterValidator(_check_absolute_path)]
type RelativePath = Annotated[Path, AfterValidator(_check_relative_path)]
type RelativePosixPath = Annotated[PosixPath, AfterValidator(_check_relative_path)]
type BuildContextPath = Annotated[PosixPath, AfterValidator(_check_build_context_path)]
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
type ConanRequirement = Annotated[
    NonEmpty[NoCRLF],
    AfterValidator(_check_conan_requirement)
]
type ConanOptionName = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    pattern=r"^[A-Za-z_][A-Za-z0-9_]*$"
)]
type ConanConfNamespace = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    pattern=r"^[^:\s]+$"
)]
type ConanConfName = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    pattern=r"^[^:\s]+$"
)]
type ConanConf = Annotated[
    dict[ConanConfNamespace, dict[ConanConfName, ConanConfValue]],
    AfterValidator(_check_conan_conf)
]
type ConanRemoteName = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    pattern=r"^[A-Za-z0-9][A-Za-z0-9_.-]*$"
)]
type ConanAllowedPattern = Annotated[
    NonEmpty[NoWhiteSpace],
    AfterValidator(_check_conan_allowed_pattern)
]
type ConanOptionPattern = Annotated[
    NonEmpty[NoWhiteSpace],
    AfterValidator(_check_conan_allowed_pattern)
]
type ConanOptions = dict[ConanOptionPattern, dict[ConanOptionName, Scalar]]
type Timeout = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    pattern=r"^\d+(\.\d+)?[smhd]?$"
)]
type HealthLogDestination = Annotated[
    NonEmpty[NoCRLF],
    AfterValidator(_check_health_log_destination)
]
type ClangTidyCheckPattern = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    pattern=r"^[^,\s\r\n]+$"
)]
type ClangTidyOptionName = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    pattern=r"^[A-Za-z_][A-Za-z0-9_]*$"
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


@dataclass(frozen=True)
class Resource:
    """A base class describing a single configuration entity that can be parsed,
    validated, and/or rendered by Bertrand's layout system.

    Attributes
    ----------
    name : ResourceName
        The globally unique name (lowercase alphanumeric with underscores, beginning
        with a letter and not ending with an underscore) for this resource, which
        serves as a stable CLI identifier, allows it to be validated from a `parse()`
        snapshot during `Config.__aenter__()`, and forms the resource's `Config.tool`
        attribute name.
    paths : frozenset[RelativePath]
        The set of relative paths that this resource manages within the project
        worktree.  If not empty, then `Config.load()` will attempt to discover this
        resource by searching for the given paths within the worktree, and will add
        the resource to its context if ALL paths are found.
    aliases : frozenset[AliasName]
        A set of alternate spellings for this resource, which are not as restricted as
        the base `name` field, and allow this resource to match names that would
        otherwise not be valid when parsing CLI input or TOML table names, for example.
    """
    # pylint: disable=unused-argument, redundant-returns-doc
    name: ResourceName
    paths: frozenset[RelativePath]
    aliases: frozenset[AliasName]

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
            search the `config.tool.bertrand.tags` list for tag-specific overrides
            during image builds.  If None, then it means this hook was invoked during
            a `bertrand init` command, and should therefore not attempt to render any
            out-of-tree artifacts that would require access to a container filesystem.

        Notes
        -----
        This is used to generate derived artifacts from a validated config without
        coupling to any particular output schema.
        """


RESOURCES: set[Resource] = set()
RESOURCE_NAMES: dict[AliasName, Resource] = {}
RESOURCE_PATHS: dict[RelativePath, Resource] = {}


def resource[ResourceT: Resource](
    name: ResourceName,
    *,
    paths: set[RelativePath] | frozenset[RelativePath] = frozenset(),
    aliases: set[AliasName] | frozenset[AliasName] = frozenset(),
) -> Callable[[type[ResourceT]], type[ResourceT]]:
    """A class decorator for defining layout resources.  See `Resource` for more
    details on the parameters and intended semantics of layout resources.

    Parameters
    ----------
    name : ResourceName
        The globally unique name (lowercase alphanumeric with underscores, beginning
        with a letter and not ending with an underscore) for this resource, which
        serves as a stable CLI identifier, allows it to be validated from a `parse()`
        snapshot during `Config.__aenter__()`, and forms the resource's `Config.tool`
        attribute name.
    paths : set[RelativePath] | frozenset[RelativePath], optional
        The relative paths that this resource manages, which allows it to be discovered
        by `Config.load()`, assuming all paths are found.  The paths are relative to
        the worktree root, and must not contain `..` segments.
    aliases : set[AliasName] | frozenset[AliasName], optional
        Additional (relaxed) spellings that can refer to this resource, which may have
        uppercase letters, `.`, and `-` characters that are not allowed in primary
        resource names.  For example, this allows `[tool.clang-tidy]` TOML tables to
        map to the `Config.clang_tidy` resource.

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
        self = cls(name=name, paths=frozenset(paths), aliases=frozenset(aliases))
        RESOURCES.add(self)
        if not RESOURCE_NAME_RE.fullmatch(name):
            raise TypeError(
                f"invalid resource name {name!r} (must match regex "
                f"{RESOURCE_NAME_RE.pattern})"
            )
        if RESOURCE_NAMES.setdefault(name, self) is not self:
            raise TypeError(f"duplicate resource name: {name!r}")

        for alias in aliases:
            if not ALIAS_NAME_RE.fullmatch(alias):
                raise TypeError(
                    f"invalid alias {alias!r} for resource {name!r} (must match regex "
                    f"{ALIAS_NAME_RE.pattern})"
                )
            if RESOURCE_NAMES.setdefault(alias, self) is not self:
                raise TypeError(
                    f"invalid alias {alias!r} for resource {name!r}: conflicts with "
                    "an existing resource"
                )

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
        return cls

    return _decorator


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


@resource("python", paths={Path("pyproject.toml")}, aliases={"pyproject"})
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
        result = self.Model.model_validate(fragment)
        result.project.resolve_licenses(config.worktree)
        _validate_dependency_groups(pyproject=result, bertrand=config.tool.bertrand)
        return result

    async def render(self, config: Config, tag: str | None) -> None:
        if config.tool.python is None:
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
        doc["build-system"] = config.tool.python.build_system.model_dump(
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
        for key, value in config.tool.python.project.model_dump(
            by_alias=True,
            exclude_none=True
        ).items():
            project_table[key] = value

        # Render all Config.Tool models (except python) to [tool.<resource>] using
        # canonical resource names
        tool_models: dict[Resource, BaseModel] = {}
        for attr in dataclass_fields(config.tool):
            resource_name = attr.name
            if resource_name == "python":
                continue
            model = getattr(config.tool, resource_name)
            if model is None or not isinstance(model, BaseModel):
                continue
            lookup = RESOURCE_NAMES.get(resource_name)
            if lookup is None or lookup.name != resource_name:
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
                # normalize [tool.<alias>] tables to canonical [tool.<resource_name>]
                for alias in sorted(resource.aliases):
                    if alias != resource.name and RESOURCE_NAMES.get(alias) is resource:
                        tool_table.pop(alias, None)

                # overwrite managed [tool.<resource_name>] content from validated model
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


@resource("conan")
class ConanConfig(Resource):
    """A resource that validates Conan configuration data sourced from project config
    files (e.g. `[tool.conan]` in `pyproject.toml`) and renders derived `conanfile.py`,
    `remotes.json`, and default profile artifacts.
    """
    # pylint: disable=missing-function-docstring, unused-argument, missing-return-doc

    GENERATORS: tuple[str, ...] = (
        "CMakeDeps",
        "CMakeToolchain",
        "VirtualBuildEnv",
        "VirtualRunEnv",
    )

    ARCH_MAP: dict[str, str] = {
        "x86_64": "x86_64",
        "amd64": "x86_64",
        "aarch64": "armv8",
        "arm64": "armv8",
    }

    class Model(BaseModel):
        """Validate the global `[tool.conan]` table."""
        model_config = ConfigDict(extra="forbid")
        build_type: Annotated[Literal["Release", "Debug"], Field(
            default="Release",
            alias="build-type",
            examples=["Release", "Debug"],
            description=
                "Global default build type for Conan builds, which can be overridden "
                "by individual tags.",
        )]
        conf: Annotated[ConanConf, Field(
            default_factory=dict,
            description=
                "Global mapping of namespace tables to conf entries, which get "
                "converted into '<namespace>:<name>=<value>' format in the generated "
                "Conan profile.  Individual tags can specify additional entries that "
                "merge with these global defaults.",
        )]
        options: Annotated[ConanOptions, Field(
            default_factory=dict,
            description=
                "Global mapping of namespace tables to package options, which get "
                "converted into '<package-pattern>:<option>=<value>' format in the "
                "generated Conan profile.  Individual tags can specify additional "
                "options that merge with these global defaults.",
        )]

        class Require(BaseModel):
            """Validate entries in `[[tool.conan.requires]]`."""
            model_config = ConfigDict(extra="forbid")
            package: Annotated[ConanRequirement, Field(
                description="A Conan package specifier."
            )]
            kind: Annotated[Literal["host", "tool"], Field(
                default="host",
                examples=["host", "tool"],
                description=
                    "The kind of requirement, which controls how the requirement gets "
                    "injected into the generated Conan profile.  'tool' requirements "
                    "apply only to the build context, whereas 'host' requirements "
                    "specify runtime dependencies."
            )]
            options: Annotated[dict[ConanOptionName, Scalar], Field(
                default_factory=dict,
                description=
                    "Mapping of option names to their values for this requirement, "
                    "which get converted into '<package>:<option>=<value>' format in "
                    "the generated Conan profile.  These options merge with any global "
                    "options specified in the `options` field, and take precedence "
                    "over any conflicting values."
            )]

        class Remote(BaseModel):
            """Validate entries in `[[tool.conan.remotes]]`."""
            @staticmethod
            def _check_allowed_packages(
                value: list[ConanAllowedPattern]
            ) -> list[ConanAllowedPattern]:
                seen: set[ConanAllowedPattern] = set()
                for pattern in value:
                    if pattern in seen:
                        raise ValueError(
                            f"duplicate conan allowed-packages pattern: '{pattern}'"
                        )
                    seen.add(pattern)
                return value

            model_config = ConfigDict(extra="forbid")
            name: Annotated[ConanRemoteName, Field(
                description="Unique name for this Conan remote.",
            )]
            url: Annotated[URL, Field(
                description="Public URL for this Conan remote.",
            )]
            verify_ssl: Annotated[bool, Field(
                default=True,
                alias="verify-ssl",
                description=
                    "Whether to verify SSL certificates when communicating with this "
                    "remote.  Should generally be left enabled unless the remote is "
                    "known to have an invalid certificate, or is only accessible over "
                    "insecure HTTP.",
            )]
            enabled: Annotated[bool, Field(
                default=True,
                description=
                    "Whether to include this remote in Conan operations.  This can be "
                    "used to temporarily disable a remote without removing it from "
                    "config.",
            )]
            recipes_only: Annotated[bool, Field(
                default=False,
                alias="recipes-only",
                description=
                    "If true, only recipes will be loaded from this remote, and no "
                    "binaries will be downloaded.",
            )]
            allowed_packages: Annotated[
                list[ConanAllowedPattern],
                AfterValidator(_check_allowed_packages),
                Field(
                    default_factory=list,
                    alias="allowed-packages",
                    description=
                        "List of recipes that are allowed to be downloaded from this "
                        "remote. If the list is empty or not present, all packages "
                        "are allowed. Uses fnmatch rules.",
                )
            ]

        @staticmethod
        def _check_requires(value: list[Require]) -> list[Require]:
            seen: set[tuple[str, str]] = set()
            for req in value:
                identity = (req.kind, req.package)
                if identity in seen:
                    raise ValueError(
                        f"duplicate conan requirement identity for kind='{req.kind}', "
                        f"package='{req.package}'"
                    )
                seen.add(identity)
            return value

        @staticmethod
        def _check_remotes(value: list[Remote]) -> list[Remote]:
            seen: set[ConanRemoteName] = set()
            for remote in value:
                if remote.name in seen:
                    raise ValueError(
                        f"duplicate conan remote name in [tool.conan]: '{remote.name}'"
                    )
                seen.add(remote.name)
            return value

        requires: Annotated[list[Require], AfterValidator(_check_requires), Field(
            default_factory=list,
            description=
                "Global list of Conan dependencies to install for the project, which "
                "can be extended for individual tags.",
        )]
        remotes: Annotated[list[Remote], AfterValidator(_check_remotes), Field(
            default_factory=list,
            description=
                "List of Conan remotes to use when resolving Conan dependencies for "
                "this project.  NOTE: Conan remotes are resolved in declaration "
                "order.  Prefer private remotes first, then optional public fallback "
                "remotes last.  Credentials are host-local and must never be stored "
                "here; resolve through env/secret channels (e.g. "
                "CONAN_LOGIN_USERNAME_<REMOTE>, CONAN_PASSWORD_<REMOTE>, with remote "
                "names normalized to SCREAMING_SNAKE_CASE)",
        )]

    async def init(self, config: Config, cli: Config.Init) -> dict[str, Any]:
        return self.Model.model_construct().model_dump(by_alias=True)

    async def validate(self, config: Config, fragment: Any) -> Model | None:
        return self.Model.model_validate(fragment)

    @staticmethod
    def _merge_options(out: dict[str, Scalar], options: ConanOptions) -> None:
        for pattern, pattern_options in sorted(options.items(), key=lambda i: i[0]):
            for option, value in sorted(pattern_options.items()):
                out[f"{pattern}:{option}"] = value

    async def _render_conanfile(self, config: Config, tag: str) -> None:
        assert config.tool.conan is not None

        # start with global requirements, then merge tag-specific additions if
        # applicable
        active = None
        requires = list(config.tool.conan.requires)
        if config.tool.bertrand is not None:
            active = next((t for t in config.tool.bertrand.tags if t.tag == tag), None)
            if active is not None:
                requires.extend(active.conan.requires)

        # check merged requirement identities and sort into host/tool requirements
        _requires: list[ConanConfig.Model.Require] = []
        tool_requires: list[ConanConfig.Model.Require] = []
        seen: set[tuple[str, str]] = set()
        for req in requires:
            identity = (req.kind, req.package)
            if identity in seen:
                raise OSError(
                    f"duplicate effective conan requirement identity for tag '{tag}': "
                    f"kind='{req.kind}', package='{req.package}'"
                )
            if req.kind == "host":
                _requires.append(req)
            elif req.kind == "tool":
                tool_requires.append(req)
            seen.add(identity)
        requires = sorted(_requires, key=lambda r: r.package)
        tool_requires.sort(key=lambda r: r.package)

        # merge global + tag-level Conan options mapping for global/tag tables, then
        # merge any per-require options
        default_options: dict[str, Scalar] = {}
        self._merge_options(default_options, config.tool.conan.options)
        if active is not None:
            self._merge_options(default_options, active.conan.options)
        for req in requires + tool_requires:
            try:
                package = RecipeReference.loads(req.package).name
            except ConanException:
                package = req.package.split("/", maxsplit=1)[0]
            for option, value in sorted(req.options.items()):
                key = f"{package}/*:{option}"
                inserted = default_options.setdefault(key, value)
                if inserted != value:
                    raise OSError(
                        f"conflicting conan option values for '{key}' across "
                        f"requirements: {inserted!r} vs {value!r}"
                    )

        # render lines for Conanfile.py
        lines: list[str] = [
            "from conan import ConanFile",
            "",
            "class BertrandConanFile(ConanFile):",
        ]
        if config.tool.python is not None:
            project = config.tool.python.project
            lines.append(f"    name = {project.name!r}")
            lines.append(f"    version = {project.version!r}")
            if project.license is not None:
                lines.append(f"    license = {project.license!r}")
            if project.description is not None:
                lines.append(f"    description = {project.description!r}")
            url = next((str(project.urls[key]) for key in (
                "homepage",
                "repository",
                "documentation"
            ) if key in project.urls), None)
            if url is not None:
                lines.append(f"    url = {url!r}")
            topics: list[str] = []
            seen_topics: set[str] = set()
            for keyword in project.keywords:
                value = keyword.strip()
                if not value or value in seen_topics:
                    continue
                seen_topics.add(value)
                topics.append(value)
            if topics:
                lines.append(f"    topics = {tuple(topics)!r}")
        lines.append("    settings = \"os\", \"arch\", \"compiler\", \"build_type\"")
        lines.append(f"    generators = {self.GENERATORS!r}")
        lines.append(f"    requires = {tuple(req.package for req in requires)!r}")
        lines.append(f"    tool_requires = {tuple(req.package for req in tool_requires)!r}")
        if default_options:
            lines.append("    default_options = {")
            for key, value in default_options.items():
                lines.append(f"        {key!r}: {value!r},")
            lines.append("    }")
        lines.append("")

        atomic_write_text(
            CONTAINER_TMP_MOUNT / "conanfile.py",
            "\n".join(lines),
            encoding="utf-8"
        )

    @staticmethod
    def _merge_conf(
        base: ConanConf,
        override: ConanConf,
    ) -> dict[ConanConfNamespace, dict[ConanConfName, ConanConfValue]]:
        merged: dict[ConanConfNamespace, dict[ConanConfName, ConanConfValue]] = {
            namespace: values.copy() for namespace, values in base.items()
        }
        for namespace, values in override.items():
            merged.setdefault(namespace, {}).update(values)
        return merged

    def _arch(self) -> str:
        raw_arch = os.uname().machine.strip().lower()
        arch = self.ARCH_MAP.get(raw_arch)
        if arch is None:
            raise OSError(
                f"unsupported Conan architecture for current runtime machine: '{raw_arch}'"
            )
        return arch

    @staticmethod
    async def _clang_major() -> str:
        result = await run(["/opt/llvm/bin/clang", "-dumpversion"], capture_output=True)
        version = result.stdout.strip()
        major = version.split(".", maxsplit=1)[0]
        if not major or not major.isdigit():
            raise OSError(
                "failed to parse clang major version during conan profile generation: "
                f"{version!r}"
            )
        return major

    @staticmethod
    def _cppstd() -> str:
        value = "" if VERSION.cxx_std is None else str(VERSION.cxx_std).strip()
        if not value or not value.isdigit():
            raise OSError(
                "invalid VERSION.cxx_std during conan profile generation: "
                f"{VERSION.cxx_std!r}"
            )
        return value

    async def _render_conanprofile(self, config: Config, tag: str) -> None:
        assert config.tool.conan is not None

        # merge global and tag-specific build_type + conf settings
        build_type = config.tool.conan.build_type
        conf = self._merge_conf(
            {
                "tools.cmake.cmaketoolchain": {"generator": "Ninja"},
                "tools.build": {
                    "compiler_executables": {
                        "c": "/opt/llvm/bin/clang",
                        "cpp": "/opt/llvm/bin/clang++",
                    }
                },
            },
            config.tool.conan.conf,
        )
        if config.tool.bertrand is not None:
            active = next((t for t in config.tool.bertrand.tags if t.tag == tag), None)
            if active is not None:
                if active.conan.build_type:
                    build_type = active.conan.build_type
                conf = self._merge_conf(conf, active.conan.conf)
        conf_def = ConfDefinition()
        for namespace in sorted(conf):
            for key, value in sorted(conf[namespace].items()):
                conf_def.update(f"{namespace}:{key}", value, profile=True)
        conf_text = conf_def.dumps()

        # render lines
        clang_major = await self._clang_major()
        arch = self._arch()
        cppstd = self._cppstd()
        lines = [
            "[settings]",
            "os=Linux",
            f"arch={arch}",
            "compiler=clang",
            f"compiler.version={clang_major}",
            "compiler.libcxx=libc++",
            f"compiler.cppstd={cppstd}",
            f"build_type={build_type}",
            "",
            "[conf]",
        ]
        if conf_text:
            lines.extend(conf_text.splitlines())
        lines.extend([
            "",
            "[buildenv]",
            "CC=ccache /opt/llvm/bin/clang",
            "CXX=ccache /opt/llvm/bin/clang++",
            "",
        ])

        atomic_write_text(
            CONAN_HOME / "profiles" / "default",
            "\n".join(lines),
            encoding="utf-8"
        )

    async def _render_conanremotes(self, config: Config, tag: str) -> None:
        assert config.tool.conan is not None

        payload: dict[str, list[dict[str, Any]]] = {"remotes": []}
        for remote in config.tool.conan.remotes:
            entry: dict[str, Any] = {
                "name": remote.name,
                "url": str(remote.url),
                "verify_ssl": remote.verify_ssl,
                "disabled": not remote.enabled,
                "recipes_only": remote.recipes_only,
            }
            if remote.allowed_packages:
                entry["allowed_packages"] = list(remote.allowed_packages)
            payload["remotes"].append(entry)

        try:
            text = json.dumps(payload, indent=4, ensure_ascii=False)
        except (TypeError, ValueError) as err:
            raise OSError(
                f"failed to serialize JSON payload for resource '{self.name}': {err}"
            ) from err
        if not text.endswith("\n"):
            text += "\n"

        atomic_write_text(
            CONAN_HOME / "remotes.json",
            text,
            encoding="utf-8"
        )

    async def render(self, config: Config, tag: str | None) -> None:
        if tag is None or config.tool.conan is None:
            return
        await self._render_conanfile(config, tag)
        await self._render_conanprofile(config, tag)
        await self._render_conanremotes(config, tag)


# TODO: continue adding descriptions and simplifying the structure of the Bertrand
# model + checking default values.

# TODO: add `examples` for any fields that have a constrained set of values.


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
                "Default shell to use when entering a container via `bertrand enter` "
                "within this project.  This is not a literal shell command, but "
                "rather an identifier that maps to a backend command to prevent "
                "remote code execution."
        )]
        editor: Annotated[Editor, Field(
            default=DEFAULT_EDITOR,
            examples=list(EDITORS),
            description=
                "Default text editor to use when invoking `bertrand code` within this "
                "project.  This is not a literal shell command, but rather an "
                "identifier that maps to a backend command to prevent remote code "
                "execution."
        )]
        ignore: Annotated[IgnoreList, Field(
            default_factory=list,
            description=
                "List of patterns to ignore within this project, which are shared "
                "between both `.gitignore` and `.containerignore`.  Patterns are "
                "interpreted using the same rules as those files."
        )]
        git_ignore: Annotated[IgnoreList, Field(
            default_factory=list,
            alias="git-ignore",
            description=
                "List of `.gitignore`-specific patterns, which are merged with the "
                "global `ignore` patterns when generating `.gitignore`."
        )]
        container_ignore: Annotated[IgnoreList, Field(
            default_factory=list,
            alias="container-ignore",
            description=
                "List of `.containerignore`-specific patterns, which are merged with "
                "the global `ignore` patterns when generating `.containerignore`."
        )]

        class Network(BaseModel):
            """Validate the `[bertrand.network]` table."""
            model_config = ConfigDict(extra="forbid")

            # TODO: add examples here

            class Table(BaseModel):
                """Validate the `[bertrand.network.build/run]` tables."""
                model_config = ConfigDict(extra="forbid")
                mode: Annotated[NetworkMode, Field(
                    default="pasta",
                    description=
                        "The networking driver to use within containers for this "
                        "project.  'pasta' is the default for efficient rootless "
                        "networking, but 'host' may be preferred for "
                        "performance-sensitive applications where security is not a "
                        "primary concern.  Equivalent to `podman create --network`.",
                )]
                options: Annotated[list[str], Field(
                    default_factory=list,
                    description=
                        "Additional options to pass to the networking driver.  See "
                        "the podman documentation for the relevant 'mode' for "
                        "supported options.",
                )]
                dns: Annotated[list[IPAddress], Field(
                    default_factory=list,
                    description=
                        "List of custom DNS server IP addresses to use within "
                        "containers.  Equivalent to `podman create --dns`.",
                )]
                dns_search: Annotated[list[str], Field(
                    default_factory=list,
                    alias="dns-search",
                    description=
                        "List of custom DNS search domains to use within containers.  "
                        "Equivalent to `podman create --dns-search`.",
                )]
                dns_options: Annotated[list[str], Field(
                    default_factory=list,
                    alias="dns-options",
                    description=
                        "List of custom DNS options to use within containers.  "
                        "Equivalent to `podman create --dns-option`.",
                )]
                add_host: Annotated[dict[HostName, HostIP], Field(
                    default_factory=dict,
                    alias="add-host",
                    description=
                        "Mapping of additional host entries to add to container "
                        "/etc/hosts, where keys are the hostnames and values are the "
                        "corresponding IP addresses.  Equivalent to `podman create "
                        "--add-host`.",
                )]

                @model_validator(mode="after")
                def _validate_none_mode(self) -> Self:
                    if self.mode == "none" and (
                        self.options or
                        self.dns or
                        self.dns_search or
                        self.dns_options or
                        self.add_host
                    ):
                        raise ValueError(
                            "network mode 'none' requires empty options, dns, "
                            "dns-search, dns-options, and add-host"
                        )
                    return self

            build: Annotated[Table, Field(
                default_factory=Table.model_construct,
                description=
                    "Networking configuration to use during build-time "
                    "`Containerfile` execution.",
            )]
            run: Annotated[Table, Field(
                default_factory=Table.model_construct,
                description=
                    "Networking configuration to use during run-time container "
                    "execution.",
            )]

        network: Annotated[Network, Field(
            default_factory=Network.model_construct,
            description="Networking configuration to use within this project.",
        )]

        # TODO: continue documenting and pruning the tag

        class Tag(BaseModel):
            """Validate entries in the `[[tool.bertrand.tags]]` table."""
            model_config = ConfigDict(extra="forbid")
            tag: TagName
            containerfile: Annotated[
                RelativePosixPath,
                Field(default=PosixPath("Containerfile"))
            ]
            build_args: Annotated[
                dict[BuildArgName, Scalar],
                Field(default_factory=dict, alias="build-args")
            ]
            env_file: Annotated[
                list[RelativePosixPath],
                Field(default_factory=list, alias="env-file")
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
                _check_text_file(root / self.containerfile, tag=self.tag)

            def resolve_env_files(self, root: Path) -> None:
                seen: set[PosixPath] = set()
                for idx, path in enumerate(self.env_file):
                    if path in seen:
                        raise ValueError(
                            f"duplicate env-file path for tag '{self.tag}' at "
                            f"index {idx}: {path}"
                        )
                    _check_text_file(root / path, tag=self.tag)
                    seen.add(path)

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
        _validate_dependency_groups(pyproject=config.tool.python, bertrand=result)
        for tag in result.tags:
            tag.resolve_containerfile(config.worktree)
            tag.resolve_env_files(config.worktree)
        return result

    async def render(self, config: Config, tag: str | None) -> None:
        if config.tool.bertrand is None:
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
        uv_version = packaging.version.parse(VERSION.uv)

        # render worktree directories
        (config.worktree / "src").mkdir(parents=True, exist_ok=True)
        (config.worktree / "tests").mkdir(parents=True, exist_ok=True)
        (config.worktree / "docs").mkdir(parents=True, exist_ok=True)

        # render ignore files
        ignore = [str(METADATA_DIR / "*")]  # always ignore Bertrand's metadata directory
        ignore.extend(config.tool.bertrand.ignore)
        containerignore = ignore.copy()
        containerignore.extend(config.tool.bertrand.container_ignore)
        atomic_write_text(
            config.worktree / ".containerignore",
            _dump_ignore_list(containerignore),
            encoding="utf-8"
        )
        gitignore = ignore.copy()
        gitignore.extend(config.tool.bertrand.git_ignore)
        atomic_write_text(
            config.worktree / ".gitignore",
            _dump_ignore_list(gitignore),
            encoding="utf-8"
        )

        # TODO: add a render() section that can update the preamble for the
        # Containerfile based on config changes, including volume mounts and OCI
        # dependencies.  The user can extend the Containerfile as long as they don't
        # modify the preamble, which is delimited by comments.

        # initialize Containerfile
        containerfile_template = jinja.from_string(
            locate_template("core", "containerfile.v1").read_text(encoding="utf-8")
        )
        containerfile_target = config.worktree / "Containerfile"
        containerfile_target.parent.mkdir(parents=True, exist_ok=True)
        containerfile_target.write_text(containerfile_template.render(
            python_major=python_version.major,
            python_minor=python_version.minor,
            python_patch=python_version.micro,
            bertrand_major=bertrand_version.major,
            bertrand_minor=bertrand_version.minor,
            bertrand_patch=bertrand_version.micro,
            cpus=0,
            page_size_kib=os.sysconf("SC_PAGE_SIZE") // 1024,
            # TODO: pretty sure this is wrong, since the symlinks haven't been set up
            # yet, but we also just need to rethink Containerfile rendering in general
            # to better interact with the build system, since I now have the context
            # necessary to do so with the init staging refactor.
            env_mount=str(WORKTREE_MOUNT),
            uv_cache=str(UV_CACHE),
            bertrand_cache=str(BERTRAND_CACHE),
            ccache_cache=str(CCACHE_CACHE),
            conan_cache=str(CONAN_CACHE),
        ), encoding="utf-8")

        # initialize CI publish action
        publish_template = jinja.from_string(
            locate_template("core", "publish.v1").read_text(encoding="utf-8")
        )
        publish_target = config.worktree / ".github" / "workflows" / "publish.yml"
        publish_target.parent.mkdir(parents=True, exist_ok=True)
        publish_target.write_text(publish_template.render(
            python_major=python_version.major,
            python_minor=python_version.minor,
            python_patch=python_version.micro,
            bertrand_major=bertrand_version.major,
            bertrand_minor=bertrand_version.minor,
            bertrand_patch=bertrand_version.micro,
        ), encoding="utf-8")


# TODO: I need extra resources/models for `uv`, `ty`, `ruff`, and `pytest`, so that
# they are rendered correctly as well for new projects.


@resource("clangd")
class Clangd(Resource):
    """A resource describing a `.clangd` file, which is used to configure clangd for
    C++ language server features in editors.  This expects native clangd keys in
    `[tool.clangd]`; legacy `arguments` aliasing is intentionally unsupported.
    """
    # pylint: disable=missing-function-docstring, unused-argument, missing-return-doc

    class Model(BaseModel):
        """Validate the `[clangd]` table."""
        model_config = ConfigDict(extra="forbid")

        class _Diagnostics(BaseModel):
            """Validate the `[tool.clangd.diagnostics]` table."""
            model_config = ConfigDict(extra="forbid")
            UnusedIncludes: Annotated[Literal["None", "Strict"], Field(
                default="Strict",
                examples=["None", "Strict"],
                description="Warn on unused #include directives.",
            )]
            MissingIncludes: Annotated[Literal["None", "Strict"], Field(
                default="Strict",
                examples=["None", "Strict"],
                description=
                    "Warn on missing #include directives, including transitive ones "
                    "that are inherited from earlier includes, but are not explicitly "
                    "listed.",
            )]
            Suppress: Annotated[list[NoCRLF], Field(
                default_factory=list,
                description=
                    "List of diagnostics to suppress by code/category.  Note that "
                    "this also includes clang-tidy rules that will be excluded from "
                    "syntax highlighting.  '*' can be used to turn off all "
                    "diagnostics.",
            )]

        Diagnostics: Annotated[_Diagnostics, Field(
            default_factory=_Diagnostics.model_construct,
            description="clangd diagnostics options.",
        )]

        class _Index(BaseModel):
            """Validate the `[tool.clangd.index]` table."""
            model_config = ConfigDict(extra="forbid")
            Background: Annotated[Literal["Build", "Skip"], Field(
                default="Build",
                examples=["Build", "Skip"],
                description=
                    "Control whether clangd should build a symbolic index in the "
                    "background.  This is required for features like cross-file rename "
                    "and efficient code navigation (including by AI agents), but can "
                    "be disabled to reduce resource usage on large codebases with "
                    "heavy churn.",
            )]
            StandardLibrary: Annotated[bool, Field(
                default=True,
                description=
                    "Control whether clangd should eagerly index the standard "
                    "library, which enables features like code completions in "
                    "new/empty files, without needing to build an index first."
            )]

        Index: Annotated[_Index, Field(
            default_factory=_Index.model_construct,
            description="clangd symbolic indexing options.",
        )]

        class _Completion(BaseModel):
            """Validate the `[tool.clangd.completion]` table."""
            model_config = ConfigDict(extra="forbid")
            AllScopes: Annotated[bool, Field(
                default=True,
                description=
                    "Whether code completion should include suggestions from scopes "
                    "that are not directly visible.  The required scope prefix will "
                    "be automatically inserted.",
            )]
            ArgumentLists: Annotated[
                Literal["None", "OpenDelimiter", "Delimiters", "FullPlaceholders"],
                Field(
                    default="Delimiters",
                    examples=["None", "OpenDelimiter", "Delimiters", "FullPlaceholders"],
                    description=
                        "Determines what is inserted in argument list position when "
                        "completing a call to a function.  Some examples:\n"
                        "   - `None`: `fo^` -> `foo^`\n"
                        "   - `OpenDelimiter`: `fo^` -> `foo(^`\n"
                        "   - `Delimiters`: `fo^` -> `foo(^)`\n"
                        "   - `FullPlaceholders`: `fo^` -> `foo(int arg)`, with "
                        "`int arg` selected\n"
                )
            ]
            HeaderInsertion: Annotated[Literal["Never", "IWYU"], Field(
                default="IWYU",
                examples=["Never", "IWYU"],
                description=
                    "Control whether clangd should automatically insert missing "
                    "#include directives when completing symbols.  If set to `IWYU` "
                    "(Include What You Use), clangd will insert headers for symbols "
                    "that are referenced in the current file, but are not already "
                    "included.",
            )]
            CodePatterns: Annotated[Literal["None", "All"], Field(
                default="All",
                examples=["None", "All"],
                description=
                    "Control whether code completion should include snippets that "
                    "insert multiple tokens or lines of code.  Setting this to `None` "
                    "restricts completions to single identifiers or member accesses, "
                    "while `All` allows clangd to suggest more complex code patterns.",
            )]

        Completion: Annotated[_Completion, Field(
            default_factory=_Completion.model_construct,
            description="clangd code completion options.",
        )]

        class _InlayHints(BaseModel):
            """Validate the `[tool.clangd.inlay-hints]` table."""
            model_config = ConfigDict(extra="forbid")
            Enabled: Annotated[bool, Field(
                default=True,
                description=
                    "Enable inlay hints, which are inline annotations shown in the "
                    "editor for various code elements, such as parameter names.",
            )]
            ParameterNames: Annotated[bool, Field(
                default=True,
                description="Show parameter name hints for arguments in function calls.",
            )]
            DeducedTypes: Annotated[bool, Field(
                default=True,
                description="Show deduced type hints for variables declared with `auto`.",
            )]
            Designators: Annotated[bool, Field(
                default=True,
                description="Show name/index hints for elements of braced initializers.",
            )]
            BlockEnd: Annotated[bool, Field(
                default=False,
                description=
                    "Show block end hints that indicate which code block a closing "
                    "brace corresponds to.",
            )]
            DefaultArguments: Annotated[bool, Field(
                default=False,
                description="Show default value hints for implicit function parameters.",
            )]
            TypeNameLimit: Annotated[NonNegativeInt, Field(
                default=24,
                description="Limit the maximum length of type inlay hints.",
            )]

        InlayHints: Annotated[_InlayHints, Field(
            default_factory=_InlayHints.model_construct,
            description="clangd inlay hints options.",
        )]

        class _Hover(BaseModel):
            """Validate the `[tool.clangd.hover]` table."""
            model_config = ConfigDict(extra="forbid")
            ShowAKA: Annotated[bool, Field(
                default=True,
                description=
                    "Resolve type aliases and show their desugared names in hover "
                    "tooltips.",
            )]
            MacroContentsLimit: Annotated[NonNegativeInt, Field(
                default=2048,
                description=
                    "Limit the maximum length of macro contents in hover tooltips.",
            )]

        Hover: Annotated[_Hover, Field(
            default_factory=_Hover.model_construct,
            description="clangd hover options.",
        )]

        class _Documentation(BaseModel):
            """Validate the `[tool.clangd.documentation]` table."""
            model_config = ConfigDict(extra="forbid")
            CommentFormat: Annotated[Literal["PlainText", "Markdown", "Doxygen"], Field(
                default="Doxygen",
                examples=["PlainText", "Markdown", "Doxygen"],
                description=
                    "Control the format of documentation comments in hover tooltips.  "
                    "Doxygen format is a superset of the previous two, and supports "
                    "rich formatting, but PlainText or Markdown can be used to "
                    "disable formatting if it causes issues in some editors.",
            )]

        Documentation: Annotated[_Documentation, Field(
            default_factory=_Documentation.model_construct,
            description="clangd documentation options.",
        )]

        class _If(BaseModel):
            """Validate the `[[tool.clangd.if]]` AoT."""
            model_config = ConfigDict(extra="forbid")
            PathMatch: Annotated[NonEmpty[list[RegexPattern]], Field(
                description=
                    "List of regex patterns to enable this configuration block.  If "
                    "any pattern matches, the configuration will be applied.  If "
                    "multiple `[[tool.clangd.if]]` entries match, their configurations "
                    "will be merged, with later entries taking precedence.",
            )]
            PathExclude: Annotated[list[RegexPattern], Field(
                default_factory=list,
                description=
                    "List of regex patterns to exclude from this configuration block.  "
                    "If any pattern matches, the configuration will not be applied, "
                    "even if `PathMatch` matches.  This allows for fine-grained "
                    "control to apply configurations to specific subdirectories or "
                    "files within a larger codebase."
            )]

            class _Diagnostics(BaseModel):
                """Validate the `[tool.clangd.diagnostics]` table."""
                model_config = ConfigDict(extra="forbid")
                UnusedIncludes: Annotated[Literal["None", "Strict"] | None, Field(
                    default=None,
                    examples=["None", "Strict"],
                    description="See global 'Diagnostics.UnusedIncludes' option.",
                )]
                MissingIncludes: Annotated[Literal["None", "Strict"] | None, Field(
                    default=None,
                    examples=["None", "Strict"],
                    description="See global 'Diagnostics.MissingIncludes' option.",
                )]
                Suppress: Annotated[NonEmpty[list[NoCRLF]] | None, Field(
                    default=None,
                    description=
                        "See global 'Diagnostics.Suppress' option.  Note that this is "
                        "additive to the global list, so it can only be used to "
                        "suppress additional diagnostics, not to re-enable diagnostics "
                        "that are turned off globally.",
                )]

            Diagnostics: Annotated[_Diagnostics | None, Field(
                default=None,
                description="clangd diagnostics options for this conditional block.",
            )]

            class _Index(BaseModel):
                """Validate the `[tool.clangd.index]` table."""
                model_config = ConfigDict(extra="forbid")
                Background: Annotated[Literal["Build", "Skip"] | None, Field(
                    default=None,
                    examples=["Build", "Skip"],
                    description="See global 'Index.Background' option.",
                )]
                StandardLibrary: Annotated[bool | None, Field(
                    default=None,
                    description="See global 'Index.StandardLibrary' option.",
                )]

            Index: Annotated[_Index | None, Field(
                default=None,
                description="clangd index options for this conditional block.",
            )]

            class _Completion(BaseModel):
                """Validate the `[tool.clangd.completion]` table."""
                model_config = ConfigDict(extra="forbid")
                AllScopes: Annotated[bool | None, Field(
                    default=None,
                    description="See global 'Completion.AllScopes' option.",
                )]
                ArgumentLists: Annotated[
                    Literal["None", "OpenDelimiter", "Delimiters", "FullPlaceholders"] | None,
                    Field(
                        default=None,
                        examples=["None", "OpenDelimiter", "Delimiters", "FullPlaceholders"],
                        description="See global 'Completion.ArgumentLists' option.",
                    )
                ]
                HeaderInsertion: Annotated[Literal["Never", "IWYU"] | None, Field(
                    default=None,
                    examples=["Never", "IWYU"],
                    description="See global 'Completion.HeaderInsertion' option.",
                )]
                CodePatterns: Annotated[Literal["None", "All"] | None, Field(
                    default=None,
                    examples=["None", "All"],
                    description="See global 'Completion.CodePatterns' option.",
                )]

            Completion: Annotated[_Completion | None, Field(
                default=None,
                description="clangd code completion options for this conditional block.",
            )]

            class _InlayHints(BaseModel):
                """Validate the `[tool.clangd.inlay-hints]` table."""
                model_config = ConfigDict(extra="forbid")
                Enabled: Annotated[bool | None, Field(
                    default=None,
                    description="See global 'InlayHints.Enabled' option.",
                )]
                ParameterNames: Annotated[bool | None, Field(
                    default=None,
                    description="See global 'InlayHints.ParameterNames' option.",
                )]
                DeducedTypes: Annotated[bool | None, Field(
                    default=None,
                    description="See global 'InlayHints.DeducedTypes' option.",
                )]
                Designators: Annotated[bool | None, Field(
                    default=None,
                    description="See global 'InlayHints.Designators' option.",
                )]
                BlockEnd: Annotated[bool | None, Field(
                    default=None,
                    description="See global 'InlayHints.BlockEnd' option.",
                )]
                DefaultArguments: Annotated[bool | None, Field(
                    default=None,
                    description="See global 'InlayHints.DefaultArguments' option.",
                )]
                TypeNameLimit: Annotated[NonNegativeInt | None, Field(
                    default=None,
                    description="See global 'InlayHints.TypeNameLimit' option.",
                )]

            InlayHints: Annotated[_InlayHints | None, Field(
                default=None,
                description="clangd inlay hints options for this conditional block.",
            )]

            class _Hover(BaseModel):
                """Validate the `[tool.clangd.hover]` table."""
                model_config = ConfigDict(extra="forbid")
                ShowAKA: Annotated[bool | None, Field(
                    default=None,
                    description="See global 'Hover.ShowAKA' option.",
                )]
                MacroContentsLimit: Annotated[NonNegativeInt | None, Field(
                    default=None,
                    description="See global 'Hover.MacroContentsLimit' option.",
                )]

            Hover: Annotated[_Hover | None, Field(
                default=None,
                description="clangd hover options for this conditional block.",
            )]

            class _Documentation(BaseModel):
                """Validate the `[tool.clangd.documentation]` table."""
                model_config = ConfigDict(extra="forbid")
                CommentFormat: Annotated[
                    Literal["PlainText", "Markdown", "Doxygen"] | None,
                    Field(
                        default=None,
                        examples=["PlainText", "Markdown", "Doxygen"],
                        description="See global 'Documentation.CommentFormat' option.",
                    )
                ]

            Documentation: Annotated[_Documentation | None, Field(
                default=None,
                description="clangd documentation options for this conditional block.",
            )]

            @model_validator(mode="after")
            def _validate_nonempty(self) -> Self:
                if (
                    self.Diagnostics is None and
                    self.Index is None and
                    self.Completion is None and
                    self.InlayHints is None and
                    self.Hover is None and
                    self.Documentation is None
                ):
                    raise ValueError(
                        "each [[tool.clangd.if]] entry must define at least one "
                        "of 'Diagnostics', 'Index', 'Completion', 'InlayHints', "
                        "'Hover', or 'Documentation'"
                    )
                return self

        If: Annotated[list[_If], Field(
            default_factory=list,
            description=
                "List of conditional blocks for file-specific clangd configuration.",
        )]

    async def init(self, config: Config, cli: Config.Init) -> dict[str, Any]:
        return self.Model.model_construct().model_dump(by_alias=True)

    async def validate(self, config: Config, fragment: Any) -> Model | None:
        return self.Model.model_validate(fragment)

    async def render(self, config: Config, tag: str | None) -> None:
        if tag is None or config.tool.clangd is None:
            return
        model = config.tool.clangd

        # define top-level config
        top_level: dict[str, Any] = {
            "Diagnostics": {
                "UnusedIncludes": model.Diagnostics.UnusedIncludes,
                "MissingIncludes": model.Diagnostics.MissingIncludes,
                "Suppress": model.Diagnostics.Suppress,
            },
            "Index": {
                "Background": model.Index.Background,
                "StandardLibrary": model.Index.StandardLibrary,
            },
            "Completion": {
                "AllScopes": model.Completion.AllScopes,
                "ArgumentLists": model.Completion.ArgumentLists,
                "HeaderInsertion": model.Completion.HeaderInsertion,
                "CodePatterns": model.Completion.CodePatterns,
            },
            "InlayHints": {
                "Enabled": model.InlayHints.Enabled,
                "ParameterNames": model.InlayHints.ParameterNames,
                "DeducedTypes": model.InlayHints.DeducedTypes,
                "Designators": model.InlayHints.Designators,
                "BlockEnd": model.InlayHints.BlockEnd,
                "DefaultArguments": model.InlayHints.DefaultArguments,
                "TypeNameLimit": model.InlayHints.TypeNameLimit,
            },
            "Hover": {
                "ShowAKA": model.Hover.ShowAKA,
                "MacroContentsLimit": model.Hover.MacroContentsLimit,
            },
            "Documentation": {
                "CommentFormat": model.Documentation.CommentFormat,
            },
        }
        content = _dump_yaml(top_level, resource_id=self.name)

        # Add fragments for each `If` section
        for section in model.If:
            fragment: dict[str, Any] = {
                "If": {
                    "PathMatch": section.PathMatch,
                    "PathExclude": section.PathExclude,
                },
            }
            if section.Diagnostics is not None:
                fragment["Diagnostics"] = {
                    "UnusedIncludes": section.Diagnostics.UnusedIncludes,
                    "MissingIncludes": section.Diagnostics.MissingIncludes,
                    "Suppress": section.Diagnostics.Suppress,
                }
            if section.Index is not None:
                fragment["Index"] = {
                    "Background": section.Index.Background,
                    "StandardLibrary": section.Index.StandardLibrary,
                }
            if section.Completion is not None:
                fragment["Completion"] = {
                    "AllScopes": section.Completion.AllScopes,
                    "ArgumentLists": section.Completion.ArgumentLists,
                    "HeaderInsertion": section.Completion.HeaderInsertion,
                    "CodePatterns": section.Completion.CodePatterns,
                }
            if section.InlayHints is not None:
                fragment["InlayHints"] = {
                    "Enabled": section.InlayHints.Enabled,
                    "ParameterNames": section.InlayHints.ParameterNames,
                    "DeducedTypes": section.InlayHints.DeducedTypes,
                    "Designators": section.InlayHints.Designators,
                    "BlockEnd": section.InlayHints.BlockEnd,
                    "DefaultArguments": section.InlayHints.DefaultArguments,
                    "TypeNameLimit": section.InlayHints.TypeNameLimit,
                }
            if section.Hover is not None:
                fragment["Hover"] = {
                    "ShowAKA": section.Hover.ShowAKA,
                    "MacroContentsLimit": section.Hover.MacroContentsLimit,
                }
            if section.Documentation is not None:
                fragment["Documentation"] = {
                    "CommentFormat": section.Documentation.CommentFormat,
                }
            content += "---\n" + _dump_yaml(fragment, resource_id=self.name)

        atomic_write_text(
            CONTAINER_TMP_MOUNT / ".clangd",
            content,
            encoding="utf-8"
        )


@resource("clang_tidy", aliases={"clang-tidy"})
class ClangTidy(Resource):
    """A resource describing a `.clang-tidy` file, which is used to configure
    clang-tidy for C++ linting.  This expects native clang-tidy key names in TOML.
    `Checks` and `WarningsAsErrors` may be specified as arrays for convenience and
    will be joined to comma-separated strings.
    """
    # pylint: disable=missing-function-docstring, unused-argument, missing-return-doc

    class Model(BaseModel):
        """Validate the `[clang-tidy]` table."""
        model_config = ConfigDict(extra="forbid")
        DisableFormat: Annotated[bool, Field(default=False)]
        HeaderFilterRegex: Annotated[RegexPattern, Field(default="^.*$")]
        ExcludeHeaderFilterRegex: Annotated[RegexPattern, Field(default="^$")]
        SystemHeaders: Annotated[bool, Field(default=False)]
        UseColor: Annotated[bool, Field(default=True)]

        class Check(BaseModel):
            """Validate entries in the `[[tool.clang-tidy.Checks]]` AoT."""
            model_config = ConfigDict(extra="forbid")
            Enable: Annotated[ClangTidyCheckPattern | None, Field(default=None)]
            Disable: Annotated[ClangTidyCheckPattern | None, Field(default=None)]
            Action: Annotated[Literal["disable", "warn", "error"], Field(default="warn")]
            Options: Annotated[
                dict[ClangTidyOptionName, Scalar],
                Field(default_factory=dict)
            ]

            @model_validator(mode="after")
            def _validate_enable_or_disable(self) -> Self:
                if (self.Enable is None) == (self.Disable is None):
                    raise ValueError(
                        "each entry in [[tool.clang-tidy.Checks]] must define "
                        "exactly one of 'Enable' or 'Disable'"
                    )
                return self

        @staticmethod
        def _check_duplicate_checks(value: list[Check]) -> list[Check]:
            seen: set[ClangTidyCheckPattern] = set()
            for entry in value:
                if entry.Enable is not None:
                    if entry.Enable in seen:
                        raise ValueError(
                            f"duplicate clang-tidy check entry: '{entry.Enable}'"
                        )
                    seen.add(entry.Enable)
                if entry.Disable is not None:
                    if entry.Disable in seen:
                        raise ValueError(
                            f"duplicate clang-tidy check entry: '{entry.Disable}'"
                        )
                    seen.add(entry.Disable)
            return value

        Checks: Annotated[
            list[Check],
            AfterValidator(_check_duplicate_checks),
            Field(default_factory=list)
        ]

    async def init(self, config: Config, cli: Config.Init) -> dict[str, Any]:
        return self.Model.model_construct().model_dump(by_alias=True)

    async def validate(self, config: Config, fragment: Any) -> Model | None:
        return self.Model.model_validate(fragment)

    async def render(self, config: Config, tag: str | None) -> None:
        if tag is None or config.tool.clang_tidy is None:
            return
        model = config.tool.clang_tidy

        # define top-level config
        content: dict[str, Any] = {
            "InheritParentConfig": False,  # enforce deterministic configs at project scope
            "FormatStyle": "none" if model.DisableFormat else "file",
            "HeaderFilterRegex": model.HeaderFilterRegex,
            "ExcludeHeaderFilterRegex": model.ExcludeHeaderFilterRegex,
            "SystemHeaders": model.SystemHeaders,
            "UseColor": model.UseColor,
        }

        # define checks
        checks: list[str] = []
        check_options: dict[str, Scalar] = {}
        warnings_as_errors: list[str] = []
        for check in model.Checks:
            if check.Enable is not None:
                checks.append(check.Enable)
                if check.Options:
                    for key, value in check.Options.items():
                        option_key = f"{check.Enable}: {key}"
                        if option_key in check_options:
                            raise OSError(
                                f"duplicate clang-tidy check option '{option_key}' "
                                "(check names must be unique across all checks)"
                            )
                        check_options[option_key] = value
                if check.Action == "error":
                    warnings_as_errors.append(check.Enable)
            if check.Disable is not None:
                checks.append(f"-{check.Disable}")

        # render yaml
        if checks:
            content["Checks"] = ",".join(checks)
        if warnings_as_errors:
            content["WarningsAsErrors"] = ",".join(warnings_as_errors)
        if check_options:
            content["CheckOptions"] = check_options

        atomic_write_text(
            CONTAINER_TMP_MOUNT / ".clang-tidy",
            _dump_yaml(content, resource_id=self.name),
            encoding="utf-8"
        )


@resource("clang_format", aliases={"clang-format"})
class ClangFormat(Resource):
    """A resource describing a `.clang-format` file, which is used to configure
    clang-format for C++ code formatting.  The `[tool.clang-format]` table is
    projected directly to YAML with no key remapping.
    """
    # pylint: disable=missing-function-docstring, unused-argument, missing-return-doc

    class Model(BaseModel):
        """Validate the `[clang-format]` table."""
        model_config = ConfigDict(extra="forbid")
        DisableFormat: Annotated[bool, Field(default=False)]
        BasedOnStyle: Annotated[
            Literal["LLVM", "Google", "Chromium", "Mozilla", "WebKit", "Microsoft", "GNU"],
            Field(default="Mozilla")
        ]
        AccessModifierOffset: Annotated[NonNegativeInt, Field(default=0)]
        AlwaysBreakBeforeMultilineStrings: Annotated[bool, Field(default=True)]
        AttributeMacros: Annotated[list[NoWhiteSpace], Field(default_factory=list)]
        BinPackArguments: Annotated[bool, Field(default=False)]
        BinPackLongBracedList: Annotated[bool, Field(default=True)]
        BinPackParameters: Annotated[
            Literal["BinPack", "OnePerLine", "AlwaysOnePerLine"],
            Field(default="OnePerLine")
        ]
        BitFieldColonSpacing: Annotated[
            Literal["None", "Before", "After", "Both"],
            Field(default="Both")
        ]
        ColumnLimit: Annotated[NonNegativeInt, Field(default=88)]
        CompactNamespaces: Annotated[bool, Field(default=False)]
        Cpp11BracedListStyle: Annotated[
            Literal["Block", "FunctionCall", "AlignFirstComment"],
            Field(default="FunctionCall")
        ]
        EmptyLineAfterAccessModifier: Annotated[
            Literal["Never", "Leave", "Always"],
            Field(default="Leave")
        ]
        EmptyLineBeforeAccessModifier: Annotated[
            Literal["Never", "Leave", "LogicalBlock", "Always"],
            Field(default="Leave")
        ]
        FixNamespaceComments: Annotated[bool, Field(default=True)]
        ForEachMacros: Annotated[list[NoWhiteSpace], Field(default_factory=list)]
        IfMacros: Annotated[list[NoWhiteSpace], Field(default_factory=list)]
        IncludeBlocks: Annotated[
            Literal["Preserve", "Merge", "Regroup"],
            Field(default="Preserve")
        ]
        InsertBraces: Annotated[bool, Field(default=True)]
        InsertNewlineAtEOF: Annotated[bool, Field(default=True)]
        LineEnding: Annotated[
            Literal["LF", "CRLF", "DeriveLF", "DeriveCRLF"],
            Field(default="DeriveLF")
        ]
        NamespaceIndentation: Annotated[
            Literal["None", "Inner", "All"],
            Field(default="None")
        ]
        NamespaceMacros: Annotated[list[NoWhiteSpace], Field(default_factory=list)]
        OneLineFormatOffRegex: Annotated[RegexPattern, Field(default="NOFORMAT")]
        PackConstructorInitializers: Annotated[
            Literal["Never", "BinPack", "CurrentLine", "NextLine", "NextLineOnly"],
            Field(default="CurrentLine")
        ]
        PointerAlignment: Annotated[
            Literal["Left", "Right", "Middle"],
            Field(default="Left")
        ]

        @staticmethod
        def _check_qualifier_order(value: list[str]) -> list[str]:
            seen: set[str] = set()
            for qualifier in value:
                if qualifier in seen:
                    raise ValueError(
                        "duplicate qualifier in ClangFormat.QualifierOrder: "
                        f"'{qualifier}'"
                    )
                seen.add(qualifier)
            if "type" not in seen:
                raise ValueError(
                    "ClangFormat.QualifierOrder must include a 'type' qualifier"
                )
            return value

        QualifierOrder: Annotated[
            list[Literal[
                "inline", "static", "constexpr", "friend", "const", "volatile",
                "restrict", "type"
            ]],
            AfterValidator(_check_qualifier_order),
            Field(default_factory=lambda: [
                "inline", "static", "constexpr", "friend", "const", "volatile",
                "restrict", "type"
            ])
        ]
        ReferenceAlignment: Annotated[
            Literal["Left", "Right", "Middle"],
            Field(default="Left")
        ]
        ReflowComments: Annotated[
            Literal["Never", "IndentOnly", "Always"],
            Field(default="Always")
        ]
        RemoveEmptyLinesInUnwrappedLines: Annotated[bool, Field(default=True)]
        RequiresClausePosition: Annotated[
            Literal[
                "OwnLine", "OwnLineWithBrace", "WithPreceding", "WithFollowing",
                "SingleLine"
            ],
            Field(default="WithPreceding")
        ]
        RequiresExpressionIndentation: Annotated[
            Literal["OuterScope", "Keyword"],
            Field(default="OuterScope")
        ]
        SeparateDefinitionBlocks: Annotated[
            Literal["Never", "Leave", "Always"],
            Field(default="Leave")
        ]
        SortUsingDeclarations: Annotated[
            Literal["Never", "Lexicographic", "LexicographicNumeric"],
            Field(default="Lexicographic")
        ]
        SpacesBeforeTrailingComments: Annotated[NonNegativeInt, Field(default=2)]
        SpacesInAngles: Annotated[
            Literal["Never", "Leave", "Always"],
            Field(default="Never")
        ]
        SpacesInContainerLiterals: Annotated[bool, Field(default=False)]

        class _SpacesInLineCommentPrefix(BaseModel):
            """Validate the `[tool.clang-format.spaces-in-line-comment-prefix]` table."""
            model_config = ConfigDict(extra="forbid")
            Minimum: Annotated[NonNegativeInt, Field(default=1)]
            Maximum: Annotated[int, Field(default=-1, ge=-1)]

        SpacesInLineCommentPrefix: Annotated[
            _SpacesInLineCommentPrefix,
            Field(default_factory=_SpacesInLineCommentPrefix.model_construct)
        ]
        SpacesInSquareBrackets: Annotated[bool, Field(default=False)]
        TabWidth: Annotated[NonNegativeInt, Field(default=4)]
        TemplateNames: Annotated[list[NoWhiteSpace], Field(default_factory=list)]
        TypeNames: Annotated[list[NoWhiteSpace], Field(default_factory=list)]
        TypenameMacros: Annotated[list[NoWhiteSpace], Field(default_factory=list)]
        UseTab: Annotated[
            Literal[
                "Never", "ForIndentation", "ForContinuationAndIndentation",
                "AlignWithSpaces", "Always"
            ],
            Field(default="Never")
        ]
        WrapNamespaceBodyWithEmptyLines: Annotated[
            Literal["Never", "Leave", "Always"],
            Field(default="Leave")
        ]

        class _Align(BaseModel):
            """Validate the `[tool.clang-format.Align]` table."""
            model_config = ConfigDict(extra="forbid")
            AfterOpenBracket: Annotated[bool, Field(default=False)]
            ArrayOfStructures: Annotated[
                Literal["Left", "Right", "None"],
                Field(default="None")
            ]
            EscapedNewlines: Annotated[
                Literal["DontAlign", "Left", "LeftWithLastLine", "Right"],
                Field(default="Right")
            ]
            Operands: Annotated[
                Literal[
                    "DontAlign", "Align", "BreakBeforeBinaryOperators",
                    "AlignAfterOperator",
                ],
                Field(default="Align")
            ]

            class _ConsecutiveAssignments(BaseModel):
                """Validate the `[tool.clang-format.align.ConsecutiveAssignments]`
                table.
                """
                model_config = ConfigDict(extra="forbid")
                Enabled: Annotated[bool, Field(default=False)]
                AcrossEmptyLines: Annotated[bool, Field(default=False)]
                AcrossComments: Annotated[bool, Field(default=False)]
                AlignCompound: Annotated[bool, Field(default=False)]
                PadOperators: Annotated[bool, Field(default=False)]

            ConsecutiveAssignments: Annotated[
                _ConsecutiveAssignments,
                Field(default_factory=_ConsecutiveAssignments.model_construct)
            ]

            class _ConsecutiveBitFields(BaseModel):
                """Validate the `[tool.clang-format.align.ConsecutiveBitFields]`
                table.
                """
                model_config = ConfigDict(extra="forbid")
                Enabled: Annotated[bool, Field(default=False)]
                AcrossEmptyLines: Annotated[bool, Field(default=False)]
                AcrossComments: Annotated[bool, Field(default=False)]

            ConsecutiveBitFields: Annotated[
                _ConsecutiveBitFields,
                Field(default_factory=_ConsecutiveBitFields.model_construct)
            ]

            class _ConsecutiveDeclarations(BaseModel):
                """Validate the `[tool.clang-format.align.ConsecutiveDeclarations]`
                table.
                """
                model_config = ConfigDict(extra="forbid")
                Enabled: Annotated[bool, Field(default=False)]
                AcrossEmptyLines: Annotated[bool, Field(default=False)]
                AcrossComments: Annotated[bool, Field(default=False)]
                AlignFunctionDeclarations: Annotated[bool, Field(default=False)]
                AlignFunctionPointers: Annotated[bool, Field(default=False)]

            ConsecutiveDeclarations: Annotated[
                _ConsecutiveDeclarations,
                Field(default_factory=_ConsecutiveDeclarations.model_construct)
            ]

            class _ConsecutiveMacros(BaseModel):
                """Validate the `[tool.clang-format.align.ConsecutiveMacros]`
                table.
                """
                model_config = ConfigDict(extra="forbid")
                Enabled: Annotated[bool, Field(default=False)]
                AcrossEmptyLines: Annotated[bool, Field(default=False)]
                AcrossComments: Annotated[bool, Field(default=False)]

            ConsecutiveMacros: Annotated[
                _ConsecutiveMacros,
                Field(default_factory=_ConsecutiveMacros.model_construct)
            ]

            class _ConsecutiveShortCaseStatements(BaseModel):
                """Validate the
                `[tool.clang-format.align.ConsecutiveShortCaseStatements]` table.
                """
                model_config = ConfigDict(extra="forbid")
                Enabled: Annotated[bool, Field(default=False)]
                AcrossEmptyLines: Annotated[bool, Field(default=False)]
                AcrossComments: Annotated[bool, Field(default=False)]
                AlignCaseArrows: Annotated[bool, Field(default=False)]
                AlignCaseColons: Annotated[bool, Field(default=False)]

            ConsecutiveShortCaseStatements: Annotated[
                _ConsecutiveShortCaseStatements,
                Field(default_factory=_ConsecutiveShortCaseStatements.model_construct)
            ]

            class _TrailingComments(BaseModel):
                """Validate the `[tool.clang-format.align.TrailingComments]`
                table.
                """
                model_config = ConfigDict(extra="forbid")
                Kind: Annotated[
                    Literal["Never", "Leave", "Always"],
                    Field(default="Leave")
                ]
                OverEmptyLines: Annotated[bool, Field(default=False)]
                AlignPPAndNotPP: Annotated[bool, Field(default=True)]

            TrailingComments: Annotated[
                _TrailingComments,
                Field(default_factory=_TrailingComments.model_construct)
            ]

        Align: Annotated[_Align, Field(default_factory=_Align.model_construct)]

        class _Allow(BaseModel):
            """Validate the `[tool.clang-format.Allow]` table."""
            model_config = ConfigDict(extra="forbid")
            AllArgumentsOnNextLine: Annotated[bool, Field(default=False)]
            AllParametersOfDeclarationOnNextLine: Annotated[bool, Field(default=False)]
            BreakBeforeNoexceptSpecifier: Annotated[
                Literal["Never", "OnlyWithParen", "Always"],
                Field(default="OnlyWithParen")
            ]
            ShortBlocksOnASingleLine: Annotated[
                Literal["Never", "Empty", "Always"],
                Field(default="Empty")
            ]
            ShortCaseExpressionsOnASingleLine: Annotated[bool, Field(default=True)]
            ShortCaseLabelsOnASingleLine: Annotated[bool, Field(default=True)]
            ShortCompoundRequirementsOnASingleLine: Annotated[bool, Field(default=True)]
            ShortEnumsOnASingleLine: Annotated[bool, Field(default=True)]
            ShortFunctionsOnASingleLine: Annotated[
                Literal["None", "InlineOnly", "Empty", "Inline", "All"],
                Field(default="All")
            ]
            ShortIfStatementsOnASingleLine: Annotated[
                Literal["None", "WithoutElse", "OnlyFirstIf", "AllIfsAndElse"],
                Field(default="WithoutElse")
            ]
            ShortLambdasOnASingleLine: Annotated[
                Literal["None", "Empty", "Inline", "All"],
                Field(default="All")
            ]
            ShortLoopsOnASingleLine: Annotated[bool, Field(default=True)]
            ShortNamespacesOnASingleLine: Annotated[bool, Field(default=False)]

        Allow: Annotated[_Allow, Field(default_factory=_Allow.model_construct)]

        class _Break(BaseModel):
            """Validate the `[tool.clang-format.Break]` table."""
            model_config = ConfigDict(extra="forbid")
            AdjacentStringLiterals: Annotated[bool, Field(default=True)]
            AfterAttributes: Annotated[
                Literal["Never", "Leave", "Always"],
                Field(default="Never")
            ]
            AfterOpenBracketBracedList: Annotated[bool, Field(default=True)]
            AfterOpenBracketFunction: Annotated[bool, Field(default=True)]
            AfterOpenBracketIf: Annotated[bool, Field(default=True)]
            AfterOpenBracketLoop: Annotated[bool, Field(default=True)]
            AfterOpenBracketSwitch: Annotated[bool, Field(default=True)]
            AfterReturnType: Annotated[
                Literal[
                    "Automatic", "ExceptShortType", "TopLevel",
                    "TopLevelDefinitions", "All", "AllDefinitions",
                ],
                Field(default="ExceptShortType")
            ]
            BeforeBinaryOperators: Annotated[
                Literal["None", "NonAssignment", "All"],
                Field(default="None")
            ]
            BeforeCloseBracketBracedList: Annotated[bool, Field(default=True)]
            BeforeCloseBracketFunction: Annotated[bool, Field(default=True)]
            BeforeCloseBracketIf: Annotated[bool, Field(default=True)]
            BeforeCloseBracketLoop: Annotated[bool, Field(default=True)]
            BeforeCloseBracketSwitch: Annotated[bool, Field(default=True)]
            BeforeConceptDeclarations: Annotated[
                Literal["Never", "Allowed", "Always"],
                Field(default="Always")
            ]
            BeforeInlineASMColon: Annotated[
                Literal["Never", "OnlyMultiline", "Always"],
                Field(default="OnlyMultiline")
            ]
            BeforeTemplateCloser: Annotated[bool, Field(default=True)]
            BeforeTernaryOperators: Annotated[bool, Field(default=False)]
            BinaryOperations: Annotated[
                Literal["Never", "OnePerLine", "RespectPrecedence"],
                Field(default="Never")
            ]
            ConstructorInitializers: Annotated[
                Literal["BeforeColon", "AfterColon", "BeforeComma", "AfterComma"],
                Field(default="AfterColon")
            ]
            FunctionDefinitionParameters: Annotated[bool, Field(default=False)]
            InheritanceList: Annotated[
                Literal["BeforeColon", "AfterColon", "BeforeComma", "AfterComma"],
                Field(default="AfterColon")
            ]
            StringLiterals: Annotated[bool, Field(default=True)]
            TemplateDeclarations: Annotated[
                Literal["Leave", "No", "Multiline", "Yes"],
                Field(default="Yes")
            ]

        Break: Annotated[_Break, Field(default_factory=_Break.model_construct)]

        class _BraceWrapping(BaseModel):
            """Validate the `[tool.clang-format.BraceWrapping]` table."""
            model_config = ConfigDict(extra="forbid")
            AfterCaseLabel: Annotated[bool, Field(default=False)]
            AfterClass: Annotated[bool, Field(default=False)]
            AfterControlStatement: Annotated[
                Literal["Never", "Multiline", "Always"],
                Field(default="Never")
            ]
            AfterEnum: Annotated[bool, Field(default=False)]
            AfterFunction: Annotated[bool, Field(default=False)]
            AfterNamespace: Annotated[bool, Field(default=False)]
            AfterStruct: Annotated[bool, Field(default=False)]
            AfterUnion: Annotated[bool, Field(default=False)]
            AfterExternBlock: Annotated[bool, Field(default=False)]
            BeforeCatch: Annotated[bool, Field(default=False)]
            BeforeElse: Annotated[bool, Field(default=False)]
            BeforeLambdaBody: Annotated[bool, Field(default=False)]
            BeforeWhile: Annotated[bool, Field(default=False)]
            IndentBraces: Annotated[bool, Field(default=False)]
            SplitEmptyFunction: Annotated[bool, Field(default=False)]
            SplitEmptyRecord: Annotated[bool, Field(default=False)]
            SplitEmptyNamespace: Annotated[bool, Field(default=False)]

        BraceWrapping: Annotated[
            _BraceWrapping,
            Field(default_factory=_BraceWrapping.model_construct)
        ]

        class _Indent(BaseModel):
            """Validate the `[tool.clang-format.Indent]` table."""
            model_config = ConfigDict(extra="forbid")
            CaseLabels: Annotated[bool, Field(default=True)]
            ExportBlock: Annotated[bool, Field(default=True)]
            ExternBlock: Annotated[bool, Field(default=True)]
            GotoLabels: Annotated[bool, Field(default=True)]
            PPDirectives: Annotated[
                Literal["None", "BeforeHash", "AfterHash", "Both"],
                Field(default="BeforeHash")
            ]
            RequiresClause: Annotated[bool, Field(default=True)]
            Width: Annotated[NonNegativeInt, Field(default=4)]
            WrappedFunctionNames: Annotated[bool, Field(default=False)]

        Indent: Annotated[_Indent, Field(default_factory=_Indent.model_construct)]

        class _IntegerLiteralSeparator(BaseModel):
            """Validate the `[tool.clang-format.IntegerLiteralSeparator]`
            table.
            """
            model_config = ConfigDict(extra="forbid")
            Binary: Annotated[int, Field(default=8, ge=-1)]
            Decimal: Annotated[int, Field(default=-1, ge=-1)]
            Hex: Annotated[int, Field(default=4, ge=-1)]

        IntegerLiteralSeparator: Annotated[
            _IntegerLiteralSeparator,
            Field(default_factory=_IntegerLiteralSeparator.model_construct)
        ]

        class _KeepEmptyLines(BaseModel):
            """Validate the `[tool.clang-format.KeepEmptyLines]` table."""
            model_config = ConfigDict(extra="forbid")
            AtEndOfFile: Annotated[bool, Field(default=False)]
            AtStartOfBlock: Annotated[bool, Field(default=False)]
            AtStartOfFile: Annotated[bool, Field(default=False)]

        KeepEmptyLines: Annotated[
            _KeepEmptyLines,
            Field(default_factory=_KeepEmptyLines.model_construct)
        ]

        class _NumericLiteralCase(BaseModel):
            """Validate the `[tool.clang-format.NumericLiteralCase]` table."""
            model_config = ConfigDict(extra="forbid")
            ExponentLetter: Annotated[
                Literal["Leave", "Upper", "Lower"],
                Field(default="Lower")
            ]
            HexDigit: Annotated[
                Literal["Leave", "Upper", "Lower"],
                Field(default="Upper")
            ]
            Prefix: Annotated[
                Literal["Leave", "Upper", "Lower"],
                Field(default="Lower")
            ]
            Suffix: Annotated[
                Literal["Leave", "Upper", "Lower"],
                Field(default="Lower")
            ]

        NumericLiteralCase: Annotated[
            _NumericLiteralCase,
            Field(default_factory=_NumericLiteralCase.model_construct)
        ]

        class _SortIncludes(BaseModel):
            """Validate the `[tool.clang-format.SortIncludes]` table."""
            model_config = ConfigDict(extra="forbid")
            Enabled: Annotated[bool, Field(default=True)]
            IgnoreCase: Annotated[bool, Field(default=False)]
            IgnoreExtension: Annotated[bool, Field(default=False)]

        SortIncludes: Annotated[
            _SortIncludes,
            Field(default_factory=_SortIncludes.model_construct)
        ]

        class _Space(BaseModel):
            """Validate the `[tool.clang-format.Space]` table."""
            model_config = ConfigDict(extra="forbid")
            AfterCStyleCast: Annotated[bool, Field(default=False)]
            AfterLogicalNot: Annotated[bool, Field(default=False)]
            AfterOperatorKeyword: Annotated[bool, Field(default=False)]
            AfterTemplateKeyword: Annotated[bool, Field(default=False)]
            BeforeAssignmentOperators: Annotated[bool, Field(default=True)]
            BeforeCaseColon: Annotated[bool, Field(default=False)]
            BeforeCpp11BracedList: Annotated[bool, Field(default=False)]
            BeforeCtorInitializerColon: Annotated[bool, Field(default=True)]
            BeforeInheritanceColon: Annotated[bool, Field(default=True)]
            BeforeJsonColon: Annotated[bool, Field(default=False)]
            BeforeRangeBasedForLoopColon: Annotated[bool, Field(default=True)]
            BeforeSquareBrackets: Annotated[bool, Field(default=False)]
            InEmptyBraces: Annotated[
                Literal["Never", "Block", "Always"],
                Field(default="Never")
            ]

            class _BeforeParensOptions(BaseModel):
                """Validate the `[tool.clang-format.Space.BeforeParensOptions]`
                table.
                """
                model_config = ConfigDict(extra="forbid")
                AfterControlStatements: Annotated[bool, Field(default=True)]
                AfterForeachMacros: Annotated[bool, Field(default=True)]
                AfterFunctionDeclarationName: Annotated[bool, Field(default=False)]
                AfterFunctionDefinitionName: Annotated[bool, Field(default=False)]
                AfterIfMacros: Annotated[bool, Field(default=True)]
                AfterNot: Annotated[bool, Field(default=True)]
                AfterOverloadedOperator: Annotated[bool, Field(default=False)]
                AfterPlacementOperator: Annotated[bool, Field(default=True)]
                AfterRequiresInClause: Annotated[bool, Field(default=False)]
                AfterRequiresInExpression: Annotated[bool, Field(default=False)]
                BeforeNonEmptyParentheses: Annotated[bool, Field(default=False)]

            BeforeParensOptions: Annotated[
                _BeforeParensOptions,
                Field(default_factory=_BeforeParensOptions.model_construct)
            ]

            class _InParensOptions(BaseModel):
                """Validate the `[tool.clang-format.Space.InParensOptions]`
                table.
                """
                model_config = ConfigDict(extra="forbid")
                ExceptDoubleParentheses: Annotated[bool, Field(default=False)]
                InConditionalStatements: Annotated[bool, Field(default=False)]
                InCStyleCasts: Annotated[bool, Field(default=False)]
                InEmptyParentheses: Annotated[bool, Field(default=False)]
                Other: Annotated[bool, Field(default=False)]

            InParensOptions: Annotated[
                _InParensOptions,
                Field(default_factory=_InParensOptions.model_construct)
            ]

        Space: Annotated[_Space, Field(default_factory=_Space.model_construct)]

    async def init(self, config: Config, cli: Config.Init) -> dict[str, Any]:
        return self.Model.model_construct().model_dump(by_alias=True)

    async def validate(self, config: Config, fragment: Any) -> Model | None:
        return self.Model.model_validate(fragment)

    async def render(self, config: Config, tag: str | None) -> None:
        if tag is None or config.tool.clang_format is None:
            return
        model = config.tool.clang_format

        content: dict[str, Any] = {
            "DisableFormat": model.DisableFormat,
            "BasedOnStyle": model.BasedOnStyle,
            "AccessModifierOffset": model.AccessModifierOffset,
            "AlignAfterOpenBracket": model.Align.AfterOpenBracket,
            "AlignArrayOfStructures": model.Align.ArrayOfStructures,
            "AlignEscapedNewlines": model.Align.EscapedNewlines,
            "AlignOperands": model.Align.Operands,
            "AlignConsecutiveAssignments": {
                "Enabled": model.Align.ConsecutiveAssignments.Enabled,
                "AcrossEmptyLines": model.Align.ConsecutiveAssignments.AcrossEmptyLines,
                "AcrossComments": model.Align.ConsecutiveAssignments.AcrossComments,
                "AlignCompound": model.Align.ConsecutiveAssignments.AlignCompound,
                "PadOperators": model.Align.ConsecutiveAssignments.PadOperators,
            },
            "AlignConsecutiveBitFields": {
                "Enabled": model.Align.ConsecutiveBitFields.Enabled,
                "AcrossEmptyLines": model.Align.ConsecutiveBitFields.AcrossEmptyLines,
                "AcrossComments": model.Align.ConsecutiveBitFields.AcrossComments,
            },
            "AlignConsecutiveDeclarations": {
                "Enabled": model.Align.ConsecutiveDeclarations.Enabled,
                "AcrossEmptyLines": model.Align.ConsecutiveDeclarations.AcrossEmptyLines,
                "AcrossComments": model.Align.ConsecutiveDeclarations.AcrossComments,
                "AlignFunctionDeclarations":
                    model.Align.ConsecutiveDeclarations.AlignFunctionDeclarations,
                "AlignFunctionPointers":
                    model.Align.ConsecutiveDeclarations.AlignFunctionPointers,
            },
            "AlignConsecutiveMacros": {
                "Enabled": model.Align.ConsecutiveMacros.Enabled,
                "AcrossEmptyLines": model.Align.ConsecutiveMacros.AcrossEmptyLines,
                "AcrossComments": model.Align.ConsecutiveMacros.AcrossComments,
            },
            "AlignConsecutiveShortCaseStatements": {
                "Enabled": model.Align.ConsecutiveShortCaseStatements.Enabled,
                "AcrossEmptyLines": model.Align.ConsecutiveShortCaseStatements.AcrossEmptyLines,
                "AcrossComments": model.Align.ConsecutiveShortCaseStatements.AcrossComments,
                "AlignCaseArrows": model.Align.ConsecutiveShortCaseStatements.AlignCaseArrows,
                "AlignCaseColons": model.Align.ConsecutiveShortCaseStatements.AlignCaseColons,
            },
            "AlignTrailingComments": {
                "Kind": model.Align.TrailingComments.Kind,
                "OverEmptyLines": model.Align.TrailingComments.OverEmptyLines,
                "AlignPPAndNotPP": model.Align.TrailingComments.AlignPPAndNotPP,
            },
            "AllowAllArgumentsOnNextLine": model.Allow.AllArgumentsOnNextLine,
            "AllowAllParametersOfDeclarationOnNextLine":
                model.Allow.AllParametersOfDeclarationOnNextLine,
            "AllowBreakBeforeNoexceptSpecifier": model.Allow.BreakBeforeNoexceptSpecifier,
            "AllowShortBlocksOnASingleLine": model.Allow.ShortBlocksOnASingleLine,
            "AllowShortCaseExpressionsOnASingleLine":
                model.Allow.ShortCaseExpressionsOnASingleLine,
            "AllowShortCaseLabelsOnASingleLine": model.Allow.ShortCaseLabelsOnASingleLine,
            "AllowShortCompoundRequirementsOnASingleLine":
                model.Allow.ShortCompoundRequirementsOnASingleLine,
            "AllowShortEnumsOnASingleLine": model.Allow.ShortEnumsOnASingleLine,
            "AllowShortFunctionsOnASingleLine": model.Allow.ShortFunctionsOnASingleLine,
            "AllowShortIfStatementsOnASingleLine": model.Allow.ShortIfStatementsOnASingleLine,
            "AllowShortLambdasOnASingleLine": model.Allow.ShortLambdasOnASingleLine,
            "AllowShortLoopsOnASingleLine": model.Allow.ShortLoopsOnASingleLine,
            "AllowShortNamespacesOnASingleLine": model.Allow.ShortNamespacesOnASingleLine,
            "AlwaysBreakBeforeMultilineStrings": model.AlwaysBreakBeforeMultilineStrings,
            "AttributeMacros": model.AttributeMacros,
            "BinPackArguments": model.BinPackArguments,
            "BinPackLongBracedList": model.BinPackLongBracedList,
            "BinPackParameters": model.BinPackParameters,
            "BitFieldColonSpacing": model.BitFieldColonSpacing,
            "BreakAdjacentStringLiterals": model.Break.AdjacentStringLiterals,
            "BreakAfterAttributes": model.Break.AfterAttributes,
            "BreakAfterOpenBracketBracedList": model.Break.AfterOpenBracketBracedList,
            "BreakAfterOpenBracketFunction": model.Break.AfterOpenBracketFunction,
            "BreakAfterOpenBracketIf": model.Break.AfterOpenBracketIf,
            "BreakAfterOpenBracketLoop": model.Break.AfterOpenBracketLoop,
            "BreakAfterOpenBracketSwitch": model.Break.AfterOpenBracketSwitch,
            "BreakAfterReturnType": model.Break.AfterReturnType,
            "BreakBeforeBinaryOperators": model.Break.BeforeBinaryOperators,
            "BreakBeforeCloseBracketBracedList": model.Break.BeforeCloseBracketBracedList,
            "BreakBeforeCloseBracketFunction": model.Break.BeforeCloseBracketFunction,
            "BreakBeforeCloseBracketIf": model.Break.BeforeCloseBracketIf,
            "BreakBeforeCloseBracketLoop": model.Break.BeforeCloseBracketLoop,
            "BreakBeforeCloseBracketSwitch": model.Break.BeforeCloseBracketSwitch,
            "BreakBeforeConceptDeclarations": model.Break.BeforeConceptDeclarations,
            "BreakBeforeInlineASMColon": model.Break.BeforeInlineASMColon,
            "BreakBeforeTemplateCloser": model.Break.BeforeTemplateCloser,
            "BreakBeforeTernaryOperators": model.Break.BeforeTernaryOperators,
            "BreakBinaryOperations": model.Break.BinaryOperations,
            "BreakConstructorInitializers": model.Break.ConstructorInitializers,
            "BreakFunctionDefinitionParameters": model.Break.FunctionDefinitionParameters,
            "BreakInheritanceList": model.Break.InheritanceList,
            "BreakStringLiterals": model.Break.StringLiterals,
            "BreakTemplateDeclarations": model.Break.TemplateDeclarations,
            "BraceWrapping": {
                "AfterCaseLabel": model.BraceWrapping.AfterCaseLabel,
                "AfterClass": model.BraceWrapping.AfterClass,
                "AfterControlStatement": model.BraceWrapping.AfterControlStatement,
                "AfterEnum": model.BraceWrapping.AfterEnum,
                "AfterFunction": model.BraceWrapping.AfterFunction,
                "AfterNamespace": model.BraceWrapping.AfterNamespace,
                "AfterStruct": model.BraceWrapping.AfterStruct,
                "AfterUnion": model.BraceWrapping.AfterUnion,
                "AfterExternBlock": model.BraceWrapping.AfterExternBlock,
                "BeforeCatch": model.BraceWrapping.BeforeCatch,
                "BeforeElse": model.BraceWrapping.BeforeElse,
                "BeforeLambdaBody": model.BraceWrapping.BeforeLambdaBody,
                "BeforeWhile": model.BraceWrapping.BeforeWhile,
                "IndentBraces": model.BraceWrapping.IndentBraces,
                "SplitEmptyFunction": model.BraceWrapping.SplitEmptyFunction,
                "SplitEmptyRecord": model.BraceWrapping.SplitEmptyRecord,
                "SplitEmptyNamespace": model.BraceWrapping.SplitEmptyNamespace,
            },
            "ColumnLimit": model.ColumnLimit,
            "CompactNamespaces": model.CompactNamespaces,
            "Cpp11BracedListStyle": model.Cpp11BracedListStyle,
            "EmptyLineAfterAccessModifier": model.EmptyLineAfterAccessModifier,
            "EmptyLineBeforeAccessModifier": model.EmptyLineBeforeAccessModifier,
            "FixNamespaceComments": model.FixNamespaceComments,
            "ForEachMacros": model.ForEachMacros,
            "IfMacros": model.IfMacros,
            "IncludeBlocks": model.IncludeBlocks,
            "IndentCaseLabels": model.Indent.CaseLabels,
            "IndentExportBlock": model.Indent.ExportBlock,
            "IndentExternBlock": model.Indent.ExternBlock,
            "IndentGotoLabels": model.Indent.GotoLabels,
            "IndentPPDirectives": model.Indent.PPDirectives,
            "IndentRequiresClause": model.Indent.RequiresClause,
            "IndentWidth": model.Indent.Width,
            "IndentWrappedFunctionNames": model.Indent.WrappedFunctionNames,
            "InsertBraces": model.InsertBraces,
            "InsertNewlineAtEOF": model.InsertNewlineAtEOF,
            "IntegerLiteralSeparator": {
                "Binary": model.IntegerLiteralSeparator.Binary,
                "Decimal": model.IntegerLiteralSeparator.Decimal,
                "Hex": model.IntegerLiteralSeparator.Hex,
            },
            "KeepEmptyLines": {
                "AtEndOfFile": model.KeepEmptyLines.AtEndOfFile,
                "AtStartOfBlock": model.KeepEmptyLines.AtStartOfBlock,
                "AtStartOfFile": model.KeepEmptyLines.AtStartOfFile,
            },
            "LineEnding": model.LineEnding,
            "NamespaceIndentation": model.NamespaceIndentation,
            "NamespaceMacros": model.NamespaceMacros,
            "NumericLiteralCase": {
                "ExponentLetter": model.NumericLiteralCase.ExponentLetter,
                "HexDigit": model.NumericLiteralCase.HexDigit,
                "Prefix": model.NumericLiteralCase.Prefix,
                "Suffix": model.NumericLiteralCase.Suffix,
            },
            "OneLineFormatOffRegex": model.OneLineFormatOffRegex,
            "PackConstructorInitializers": model.PackConstructorInitializers,
            "PointerAlignment": model.PointerAlignment,
            "QualifierOrder": model.QualifierOrder,
            "ReferenceAlignment": model.ReferenceAlignment,
            "ReflowComments": model.ReflowComments,
            "RemoveEmptyLinesInUnwrappedLines": model.RemoveEmptyLinesInUnwrappedLines,
            "RequiresClausePosition": model.RequiresClausePosition,
            "RequiresExpressionIndentation": model.RequiresExpressionIndentation,
            "SeparateDefinitionBlocks": model.SeparateDefinitionBlocks,
            "SortIncludes": {
                "Enabled": model.SortIncludes.Enabled,
                "IgnoreCase": model.SortIncludes.IgnoreCase,
                "IgnoreExtension": model.SortIncludes.IgnoreExtension,
            },
            "SortUsingDeclarations": model.SortUsingDeclarations,
            "SpaceAfterCStyleCast": model.Space.AfterCStyleCast,
            "SpaceAfterLogicalNot": model.Space.AfterLogicalNot,
            "SpaceAfterOperatorKeyword": model.Space.AfterOperatorKeyword,
            "SpaceAfterTemplateKeyword": model.Space.AfterTemplateKeyword,
            "SpaceBeforeAssignmentOperators": model.Space.BeforeAssignmentOperators,
            "SpaceBeforeCaseColon": model.Space.BeforeCaseColon,
            "SpaceBeforeCpp11BracedList": model.Space.BeforeCpp11BracedList,
            "SpaceBeforeCtorInitializerColon": model.Space.BeforeCtorInitializerColon,
            "SpaceBeforeInheritanceColon": model.Space.BeforeInheritanceColon,
            "SpaceBeforeJsonColon": model.Space.BeforeJsonColon,
            "SpaceBeforeParens": "Custom",  # always "Custom" to use Space.BeforeParensOptions
            "SpaceBeforeParensOptions": {
                "AfterControlStatements":
                    model.Space.BeforeParensOptions.AfterControlStatements,
                "AfterForeachMacros":
                    model.Space.BeforeParensOptions.AfterForeachMacros,
                "AfterFunctionDeclarationName":
                    model.Space.BeforeParensOptions.AfterFunctionDeclarationName,
                "AfterFunctionDefinitionName":
                    model.Space.BeforeParensOptions.AfterFunctionDefinitionName,
                "AfterIfMacros": model.Space.BeforeParensOptions.AfterIfMacros,
                "AfterNot": model.Space.BeforeParensOptions.AfterNot,
                "AfterOverloadedOperator":
                    model.Space.BeforeParensOptions.AfterOverloadedOperator,
                "AfterPlacementOperator":
                    model.Space.BeforeParensOptions.AfterPlacementOperator,
                "AfterRequiresInClause":
                    model.Space.BeforeParensOptions.AfterRequiresInClause,
                "AfterRequiresInExpression":
                    model.Space.BeforeParensOptions.AfterRequiresInExpression,
                "BeforeNonEmptyParentheses":
                    model.Space.BeforeParensOptions.BeforeNonEmptyParentheses,
            },
            "SpaceBeforeRangeBasedForLoopColon": model.Space.BeforeRangeBasedForLoopColon,
            "SpaceBeforeSquareBrackets": model.Space.BeforeSquareBrackets,
            "SpacesBeforeTrailingComments": model.SpacesBeforeTrailingComments,
            "SpacesInAngles": model.SpacesInAngles,
            "SpacesInContainerLiterals": model.SpacesInContainerLiterals,
            "SpacesInLineCommentPrefix": {
                "Minimum": model.SpacesInLineCommentPrefix.Minimum,
                "Maximum": model.SpacesInLineCommentPrefix.Maximum,
            },
            "SpacesInParens": "Custom",  # always "Custom" to use Space.BeforeParensOptions
            "SpacesInParensOptions": {
                "ExceptDoubleParentheses":
                    model.Space.InParensOptions.ExceptDoubleParentheses,
                "InConditionalStatements":
                    model.Space.InParensOptions.InConditionalStatements,
                "InCStyleCasts": model.Space.InParensOptions.InCStyleCasts,
                "InEmptyParentheses": model.Space.InParensOptions.InEmptyParentheses,
                "Other": model.Space.InParensOptions.Other,
            },
            "SpacesInSquareBrackets": model.SpacesInSquareBrackets,
            "Standard": "Auto",  # always "Auto" to detect C++ standard from source files
            "TabWidth": model.TabWidth,
            "TemplateNames": model.TemplateNames,
            "TypeNames": model.TypeNames,
            "TypenameMacros": model.TypenameMacros,
            "UseTab": model.UseTab,
            "WrapNamespaceBodyWithEmptyLines": model.WrapNamespaceBodyWithEmptyLines,
        }
        atomic_write_text(
            CONTAINER_TMP_MOUNT / ".clang-format",
            _dump_yaml(content, resource_id=self.name),
            encoding="utf-8",
        )


@resource("vscode", paths={VSCODE_WORKSPACE_FILE})
class VSCodeWorkspace(Resource):
    """A resource representing a VSCode managed workspace JSON file, which allows
    VSCode to attach to a running container context via the remote-containers
    extension, and mount its internal toolchain.
    """

    async def render(self, config: Config, tag: str | None) -> None:
        jinja = jinja2.Environment(
            autoescape=False,
            undefined=jinja2.StrictUndefined,
            keep_trailing_newline=True,
            trim_blocks=False,
            lstrip_blocks=False,
        )
        template = jinja.from_string(
            locate_template("core", "vscode-workspace.v1").read_text(encoding="utf-8")
        )
        target = config.worktree / VSCODE_WORKSPACE_FILE
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(template.render(
            mount_path=WORKTREE_MOUNT.as_posix(),
        ), encoding="utf-8")


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

    @dataclass
    class Tool:
        """A nested namespace storing validated config data for each supported
        resource.  Attributes are populated by invoking `validate()` hooks for each
        resource whose name or alias appears in the merged `parse()` snapshot loaded
        during `__aenter__()`.  Type hints are provided for built-in resources, whose
        names must exactly match their corresponding resource IDs.  Other resources
        will be attached if present, but will not have full type hints unless manually
        accessed and casted by the caller.
        """
        python: PyProject.Model | None = field(default=None, repr=False)
        conan: ConanConfig.Model | None = field(default=None, repr=False)
        bertrand: Bertrand.Model | None = field(default=None, repr=False)
        clangd: Clangd.Model | None = field(default=None, repr=False)
        clang_tidy: ClangTidy.Model | None = field(default=None, repr=False)
        clang_format: ClangFormat.Model | None = field(default=None, repr=False)

    worktree: Path
    timeout: float
    resources: set[Resource] = field(default_factory=lambda: {RESOURCE_NAMES["bertrand"]})
    tool: Tool = field(default_factory=Tool, repr=False)
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
                r for r in RESOURCES
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
                    r.name: await r.init(self, self.init)
                    for r in sorted(self.resources)
                }

                # invoke parse hooks for all resources in deterministic order
                key_owner: dict[tuple[str, ...], ResourceName] = {}
                for r in sorted(self.resources):
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
                    if model is not None:
                        if getattr(self.tool, lookup.name, None) is not None:
                            raise AttributeError(f"Config.Tool.{lookup.name} already exists")
                        setattr(self.tool, lookup.name, model)
                    self.resources.add(lookup)

                self._entered += 1
                return self
        except:
            self.resources = old_resources
            self._entered = 0
            self.tool = self.Tool()
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
            self.tool = self.Tool()

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

    def resource(self, resource_id: ResourceName) -> Resource:
        """Retrieve the resource specification for the given resource ID.

        Parameters
        ----------
        resource_id : ResourceName
            The stable identifier of the resource to retrieve, as defined in the
            global catalog.

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
        result = RESOURCE_NAMES.get(resource_id)
        if result is None:
            raise KeyError(f"resource ID '{resource_id}' is not defined in the catalog")
        return result

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
        if self.tool.bertrand is None:
            raise TypeError(
                f"missing 'bertrand' configuration for environment at {self.worktree}"
            )
        cfg = next((t for t in self.tool.bertrand.tags if t.tag == tag), None)
        if cfg is None:
            raise ValueError(
                f"unknown image tag '{tag}' for environment at {self.worktree}"
            )
        worktree = worktree.expanduser().resolve()
        containerfile = worktree / cfg.containerfile
        _check_text_file(containerfile, tag=tag)

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
        if self.tool.bertrand is None:
            raise TypeError(
                f"missing 'bertrand' configuration for environment at {self.worktree}"
            )
        cfg = next((t for t in self.tool.bertrand.tags if t.tag == tag), None)
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
            except:
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
            for r in sorted(self.resources):
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
        if not inside_image():
            raise RuntimeError("build() requires access to a container filesystem")
        if not self:
            raise RuntimeError("build() requires an active config context")
        if self.tool.python is None:
            raise OSError("build() requires parsed 'pyproject' configuration")
        if self.tool.bertrand is None:
            raise OSError("build() requires parsed 'bertrand' configuration")
        tags = {entry.tag for entry in self.tool.bertrand.tags}
        if tag not in tags:
            raise OSError(
                f"build() received unknown active tag '{tag}' (declared tags: "
                f"{', '.join(sorted(repr(name) for name in tags))})"
            )
        groups = self.tool.python.project.optional_dependencies
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
            "--no-build-isolation-package", self.tool.python.project.name,  # no isolation
        ]
        if not inside_container():
            sync_cmd.append("--no-editable")  # image build context -> non-editable

        async with lock_worktree(self.worktree):
            await run(["uv", "lock"], cwd=self.worktree)  # update lockfile
            await run(sync_cmd, cwd=self.worktree)  # orchestrate build
