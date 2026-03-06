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
import ipaddress
import os
import re
import shutil
import string
import tomllib

from dataclasses import asdict, dataclass, field
from collections.abc import Mapping, Sequence
from importlib import resources as importlib_resources
from pathlib import Path, PosixPath
from types import MappingProxyType, TracebackType
from typing import Annotated, Any, Callable, Literal, Self

from jinja2 import Environment, StrictUndefined
from conan.api.model.list import ListPattern, VersionRange
from conan.api.model.refs import RecipeReference
from conan.errors import ConanException
from conan.internal.model.conf import ConfDefinition
from email_validator import EmailNotValidError, validate_email
from packaging.licenses import InvalidLicenseExpression, canonicalize_license_expression
from packaging.requirements import InvalidRequirement, Requirement
from packaging.specifiers import Specifier, InvalidSpecifier
from packaging.utils import InvalidName, canonicalize_name
from pydantic import (
    AfterValidator,
    AnyHttpUrl,
    BaseModel,
    ConfigDict,
    PositiveInt,
    StringConstraints,
    TypeAdapter,
    ValidationError,
    Field,
    model_validator
)
import yaml

from .pipeline import on_init
from .run import LOCK_TIMEOUT, Lock, atomic_write_text, sanitize_name
from .version import __version__, VERSION

# pylint: disable=bare-except


# Canonical path definitions for environment control
ENV_DIR: PosixPath = PosixPath(".bertrand")
ENV_LOCK: PosixPath = ENV_DIR / ".lock"
ENV_METADATA: PosixPath = ENV_DIR / "env.json"
ENV_MOUNT: PosixPath = PosixPath("/env")
ENV_TMP: PosixPath = ENV_DIR / "tmp"
ENV_COMMITS: PosixPath = ENV_DIR / "commits"


# Global resource catalog.  Extensions can add resources here with associated behavior,
# and then update the capabilities and/or profiles to place them in the generated
# layouts, without needing to change any of the core layout parsing or rendering logic.
CATALOG: dict[str,  Resource] = {}


# In-container environment variables for relevant configuration, which are set either
# at build time or upon starting the container context, and used to control the
# behavior of the bertrand CLI both inside and outside the container.
CONTAINER_BIN_ENV: str = "BERTRAND_CONTAINER_BIN"
CONTAINER_ID_ENV: str = "BERTRAND_CONTAINER_ID"
CONTAINER_TAG_ENV: str = "BERTRAND_CONTAINER_TAG"
EDITOR_BIN_ENV: str = "BERTRAND_EDITOR_BIN"
ENV_ID_ENV: str = "BERTRAND_ENV_ID"
ENV_NAME_ENV: str = "BERTRAND_ENV_NAME"
ENV_ROOT_ENV: str = "BERTRAND_ENV_ROOT"
IMAGE_ID_ENV: str = "BERTRAND_IMAGE_ID"
IMAGE_TAG_ENV: str = "BERTRAND_IMAGE_TAG"


# Configuration options that affect CLI behavior
DEFAULT_MAX_COMMITS: int = 10
SHELLS: dict[str, tuple[str, ...]] = {
    "bash": ("bash", "-l"),
}
DEFAULT_SHELL: str = "bash"
if DEFAULT_SHELL not in SHELLS:
    raise RuntimeError(f"default shell is unsupported: {DEFAULT_SHELL}")
INSTRUMENTS: dict[str, Callable[[dict[str, Any]], Callable[[list[str]], list[str]]]] = {
    # NOTE: instruments are identified by a unique name, which limits what can appear
    # in a tag's `instruments` field as part of a configured build matrix.  They map
    # to functions which accept the instrument's configuration as a parsed mapping,
    # validate it, and return another function that transforms the container's normal
    # entry point command (list of strings) before execution.
}


# Validation primitives for config fields
GLOB_RE = re.compile(r"^[A-Za-z0-9._/\-\*\?\[\]!]+$")
HTTP_URL = TypeAdapter(AnyHttpUrl)
NS_PATH_RE = re.compile(r"^ns:\S+$")
NETWORK_MODE_RE = re.compile(rf"^(none|host|private|slirp4netns|pasta|{NS_PATH_RE.pattern})$")
HOSTNAME_RE = re.compile(
    r"^(?=.{1,253}$)(?!-)[A-Za-z0-9-]{1,63}(?<!-)(?:\.(?!-)[A-Za-z0-9-]{1,63}(?<!-))*$"
)
NETWORK_ALIAS_LABEL_RE = re.compile(r"^(?!-)[a-z0-9-]{1,63}(?<!-)$")
USERNS_CONTAINER_REF_RE = re.compile(r"^[A-Za-z0-9._-]+$")
USERNS_MAPPING_RE = re.compile(r"^(?P<container>\d+):(?P<host>@?\d+):(?P<length>\d+)$")
ULIMIT_NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")
CAPABILITY_TOKEN_RE = re.compile(r"^CAP_[A-Z0-9_]+$")
CAPABILITY_DEFINE_RE = re.compile(r"^\s*#define\s+(CAP_[A-Z0-9_]+)\s+([0-9]+)\b")
SECURITY_OPT_KEY_RE = re.compile(r"^[a-z0-9][a-z0-9_.-]*$")
CONAN_REF_TOKEN_RE = re.compile(r"^[a-z0-9_][a-z0-9_+.-]{1,100}\Z")
CONAN_OPTION_NAME_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
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


def _check_semver(version: str) -> str:
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


def _deduplicate_ignore_list(ignore: list[Glob]) -> list[Glob]:
    out: list[Glob] = []
    seen: set[Glob] = set()
    for pattern in ignore:
        if pattern in seen:
            continue
        out.append(pattern)
        seen.add(pattern)
    return out


def _check_network_mode(mode: str) -> str:
    if not NETWORK_MODE_RE.fullmatch(mode):
        raise ValueError(
            "invalid network mode (expected one of: "
            "none|host|private|slirp4netns|pasta|ns:<path>)"
        )
    if mode.startswith("ns:") and not NS_PATH_RE.fullmatch(mode):
        raise ValueError("invalid namespace network mode, expected 'ns:<path>'")
    return mode


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


def _check_absolute_path(path: PosixPath) -> PosixPath:
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


def _check_ulimit_name(name: str) -> str:
    name = name.lower()
    if not ULIMIT_NAME_RE.fullmatch(name):
        raise ValueError(
            f"invalid ulimit name '{name}' (expected lowercase POSIX-style token, "
            "e.g. nofile, nproc, host)"
        )
    return name


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


def _check_instrument(instrument: str) -> str:
    if instrument not in INSTRUMENTS:
        raise ValueError(
            f"unknown instrument '{instrument}' (available instruments: "
            f"{', '.join(INSTRUMENTS)})"
        )
    return instrument


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


def _check_conan_conf(conf: dict[ConanConfNamespace, dict[ConanConfName, ConanScalar]]) -> None:
    for namespace, values in conf.items():
        for key in values:
            key = f"{namespace}:{key}"
            conf_def = ConfDefinition()
            try:
                conf_def.update(key, 0, profile=True)
            except ConanException as err:
                raise ValueError(f"invalid conan conf key '{key}'") from err


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
type NonNegativeInt = Annotated[int, Field(ge=0)]
type NonNegativeFloat = Annotated[float, Field(ge=0.0)]
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
type SemVer = Annotated[NonEmpty[Trimmed], AfterValidator(_check_semver)]
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
type Entrypoint = Annotated[str, StringConstraints(
    strip_whitespace=True,
    pattern=r"^[A-Za-z_][A-Za-z0-9_]*:[A-Za-z_][A-Za-z0-9_]*$"
)]
type EntrypointName = Annotated[str, StringConstraints(
    strip_whitespace=True,
    pattern=r"^[a-zA-Z_][a-zA-Z0-9_]*$"
)]
type Shell = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_shell)]
type IgnoreList = Annotated[list[Glob], AfterValidator(_deduplicate_ignore_list)]
type NetworkMode = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_network_mode)]
type IPAddress = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_ip_address)]
type HostIP = Annotated[  # pylint: disable=invalid-name
    NonEmpty[NoWhiteSpace],
    AfterValidator(_check_host_ip)
]
type HostName = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    pattern=HOSTNAME_RE.pattern
)]
type SanitizedName = Annotated[str, AfterValidator(_check_sanitized_name)]
type AbsolutePath = Annotated[PosixPath, AfterValidator(_check_absolute_path)]
type RelativePath = Annotated[PosixPath, AfterValidator(_check_relative_path)]
type NetworkAlias = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_network_alias)]
type Memory = Annotated[str, StringConstraints(strip_whitespace=True, pattern=r"^\d+[bkmg]?$")]
type ULimitName = Annotated[
    NonEmpty[NoWhiteSpace],
    AfterValidator(_check_ulimit_name)
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
type Instrument = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_instrument)]
type ScreamingSnakeCase = Annotated[str, StringConstraints(
    strip_whitespace=True,
    pattern=r"^[A-Z_]?[A-Z0-9_]*$"
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
    pattern=CONAN_OPTION_NAME_RE.pattern
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
type ConanScalar = str | bool | int | float
type ConanConf = Annotated[
    dict[ConanConfNamespace, dict[ConanConfName, ConanScalar]],
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


class Template(BaseModel):
    """Stable template reference used by layout resources.

    Canonical templates are packaged with Bertrand under `env/templates` and addressed
    by stable `{namespace}/{name}/{version}` references.  They are lazily hydrated into
    the `on_init` state cache before rendering.
    """
    @staticmethod
    def _validate_non_empty(value: str) -> str:
        text = value.strip()
        if not text:
            raise ValueError("template reference fields must be non-empty")
        return text

    model_config = ConfigDict(extra="forbid")
    namespace: Annotated[str, AfterValidator(_validate_non_empty)]
    name: Annotated[str, AfterValidator(_validate_non_empty)]
    version: Annotated[str, AfterValidator(_validate_non_empty)]


def resource[ResourceT: Resource](
    name: str,
    *,
    template: str | None = None
) -> Callable[[type[ResourceT]], type[ResourceT]]:
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

    def _decorator[U: Resource](cls: type[U]) -> type[U]:
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
    return root.expanduser().resolve() / ENV_DIR


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
    path = root.expanduser().resolve() / ENV_LOCK
    path.parent.mkdir(parents=True, exist_ok=True)
    return Lock(path, timeout=timeout)


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
    """A resource describing a `pyproject.toml` file, which is the primary vehicle for
    configuring a top-level Python project, as well as Bertrand itself and its entire
    toolchain via the `[tool.bertrand]` table.
    """
    # pylint: disable=missing-function-docstring, unused-argument, missing-return-doc

    def parse(self, config: Config) -> dict[Any, Any] | None:
        path = config.path("pyproject")
        try:
            text = path.read_text(encoding="utf-8")
        except OSError as err:
            raise OSError(
                f"failed to read pyproject for resource 'pyproject' at {path}: {err}"
            ) from err

        try:
            parsed = tomllib.loads(text)
        except tomllib.TOMLDecodeError as err:
            raise OSError(
                f"failed to parse pyproject TOML for resource 'pyproject' at {path}: {err}"
            ) from err

        if not isinstance(parsed, dict):
            raise OSError(f"expected mapping at 'pyproject', got {type(parsed).__name__}")
        return parsed


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


# Profiles define only resource placement paths: wildcard baseline + profile diffs.
PROFILES: dict[str, dict[str, PosixPath]] = {
    "flat": {
        "publish": PosixPath(".github") / "workflows" / "publish.yml",
        "gitignore": PosixPath(".gitignore"),
        "containerignore": PosixPath(".containerignore"),
        "containerfile": PosixPath("Containerfile"),
        "docs": PosixPath("docs"),
        "tests": PosixPath("tests"),
    },
    "src": {
        "publish": PosixPath(".github") / "workflows" / "publish.yml",
        "gitignore": PosixPath(".gitignore"),
        "containerignore": PosixPath(".containerignore"),
        "containerfile": PosixPath("Containerfile"),
        "docs": PosixPath("docs"),
        "tests": PosixPath("tests"),
        "src": PosixPath("src"),
    },
}


# Capabilities define only language/tool resource placement paths: wildcard baseline
# + profile-specific diffs.
CAPABILITIES: dict[str, dict[str, dict[str, PosixPath]]] = {
    "python": {
        "flat": {
            "pyproject": PosixPath("pyproject.toml"),
        },
        "src": {
            "pyproject": PosixPath("pyproject.toml"),
        },
    },
    "cpp": {
        "flat": {
            "compile_commands": PosixPath("compile_commands.json"),
            "clang-format": PosixPath(".clang-format"),
            "clang-tidy": PosixPath(".clang-tidy"),
            "clangd": PosixPath(".clangd"),
        },
        "src": {
            "compile_commands": PosixPath("compile_commands.json"),
            "clang-format": PosixPath(".clang-format"),
            "clang-tidy": PosixPath(".clang-tidy"),
            "clangd": PosixPath(".clangd"),
        },
    },
    "vscode": {
        "flat": {
            "vscode-workspace": PosixPath(".vscode/bertrand.code-workspace"),
        },
        "src": {
            "vscode-workspace": PosixPath(".vscode/bertrand.code-workspace"),
        },
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
            version = VERSION.python
            if not version:
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
        max_commits: int = field(default=DEFAULT_MAX_COMMITS)
        shell: str = field(default=DEFAULT_SHELL)
        bertrand_version: str = field(default=__version__)
        python_version: str = field(default_factory=_python_version)
        cpus: int = field(default_factory=lambda: os.cpu_count() or 1)
        page_size_kib: int = field(default_factory=_page_size_kib)
        mount_path: str = field(default=str(ENV_MOUNT))
        cache_dir: str = field(default="/tmp/.cache")

    class BuildSystem(BaseModel):
        """Validate the `[build-system]` table."""
        model_config = ConfigDict(extra="forbid")

        @staticmethod
        def _check_requires(value: list[str]) -> list[str]:
            if value != ["bertrand"]:
                raise ValueError("build-system.requires must be set to ['bertrand']")
            return value

        requires: Annotated[list[str], AfterValidator(_check_requires)]

    class Project(BaseModel):
        """Validate the `[project]` table."""
        model_config = ConfigDict(extra="allow", populate_by_name=True)
        name: str
        version: str
        description: Annotated[str | None, Field(default=None)]
        readme: Annotated[PosixPath | None, Field(default=None)]
        requires_python: Annotated[
            SemVer | None,
            Field(default=VERSION.python, alias="requires-python")
        ]
        license: Annotated[License | None, Field(default=None)]
        license_files: Annotated[
            list[Glob] | None,
            Field(default=None, alias="license-files")
        ]

        class Author(BaseModel):
            """Validate entries in the `authors` and `maintainers` lists."""
            model_config = ConfigDict(extra="forbid")
            name: Annotated[EmailName | None, Field(default=None)]
            email: Annotated[Email | None, Field(default=None)]

            @model_validator(mode="after")
            def _require_name_or_email(self) -> Self:
                if self.name is None and self.email is None:
                    raise ValueError("at least one of 'name' or 'email' must be provided")
                return self

        authors: Annotated[list[Author], Field(default_factory=list)]
        maintainers: Annotated[list[Author], Field(default_factory=list)]
        keywords: Annotated[list[str], Field(default_factory=list)]
        classifiers: Annotated[list[str], Field(default_factory=list)]
        dependencies: Annotated[list[PEP508Requirement], Field(default_factory=list)]
        optional_dependencies: Annotated[dict[PEP508Name, list[PEP508Requirement]], Field(
            default_factory=dict,
            alias="optional-dependencies"
        )]
        scripts: Annotated[dict[EntrypointName, Entrypoint], Field(default_factory=dict)]
        gui_scripts: Annotated[dict[EntrypointName, Entrypoint], Field(
            default_factory=dict,
            alias="gui-scripts"
        )]
        urls: Annotated[dict[URLLabel, URL], Field(default_factory=dict)]

        @model_validator(mode="after")
        def _validate_script_collisions(self) -> Self:
            collisions = set(self.scripts).intersection(set(self.gui_scripts))
            if collisions:
                raise ValueError(
                    "duplicate script names across 'project.scripts' and "
                    f"'project.gui-scripts': {', '.join(sorted(collisions))}"
                )
            return self

        def _resolve_licenses(self, root: Path) -> None:
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
                                f"license file is not UTF-8 encoded '{relative}': {err}"
                            ) from err
                        seen.add(relative)

    class Tool(BaseModel):
        """Validate the `[tool]` table."""
        model_config = ConfigDict(extra="allow")

        class Conan(BaseModel):
            """Validate the `[tool.conan]` table."""
            model_config = ConfigDict(extra="forbid", populate_by_name=True)

            class Require(BaseModel):
                """Validate entries in the `[[tool.conan.requires]]` AoT."""
                model_config = ConfigDict(extra="forbid")
                package: ConanRequirement
                kind: Annotated[Literal["host", "tool"], Field(default="host")]
                options: Annotated[
                    dict[ConanOptionName, ConanScalar],
                    Field(default_factory=dict)
                ]
                conf: Annotated[ConanConf, Field(default_factory=dict)]

            class Remote(BaseModel):
                """Validate entries in the `[[tool.conan.remotes]]` AoT."""
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

                model_config = ConfigDict(extra="forbid", populate_by_name=True)
                name: ConanRemoteName
                url: URL
                verify_ssl: Annotated[bool, Field(default=True, alias="verify-ssl")]
                enabled: Annotated[bool, Field(default=True)]
                recipes_only: Annotated[bool, Field(default=False, alias="recipes-only")]
                allowed_packages: Annotated[
                    list[ConanAllowedPattern],
                    AfterValidator(_check_allowed_packages),
                    Field(default_factory=list, alias="allowed-packages")
                ]

            @staticmethod
            def _check_requires(value: list[Require], *, where: str) -> list[Require]:
                seen: set[tuple[str, str]] = set()
                for req in value:
                    identity = (req.kind, req.package)
                    if identity in seen:
                        raise ValueError(
                            f"duplicate conan requirement identity in {where} for "
                            f"kind='{req.kind}', package='{req.package}'"
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

            build_type: Annotated[
                Literal["Release", "Debug"],
                Field(default="Release", alias="build-type")
            ]
            conf: Annotated[ConanConf, Field(default_factory=dict)]
            requires: Annotated[
                list[Require],
                AfterValidator(lambda x: Config.Tool.Conan._check_requires(
                    x,
                    where="[tool.conan.requires]"
                )),
                Field(default_factory=list)
            ]
            remotes: Annotated[
                list[Remote],
                AfterValidator(_check_remotes),
                Field(default_factory=list)
            ]

        conan: Annotated[Conan | None, Field(default=None)]

        class Bertrand(BaseModel):
            """Validate the `[tool.bertrand]` table."""
            model_config = ConfigDict(extra="forbid")
            max_commits: Annotated[PositiveInt, Field(
                default=DEFAULT_MAX_COMMITS,
                alias="max-commits"
            )]
            shell: Annotated[Shell, Field(default=DEFAULT_SHELL)]
            ignore: Annotated[IgnoreList, Field(default_factory=list)]
            git_ignore: Annotated[
                IgnoreList,
                Field(default_factory=list, alias="git-ignore")
            ]
            container_ignore: Annotated[
                IgnoreList,
                Field(default_factory=list, alias="container-ignore")
            ]
            services: Annotated[list[str], Field(default_factory=list)]

            class Network(BaseModel):
                """Validate the `[tool.bertrand.network]` table."""
                model_config = ConfigDict(extra="forbid")

                class Table(BaseModel):
                    """Validate the `[tool.bertrand.network.build/run]` tables."""
                    model_config = ConfigDict(extra="forbid")
                    mode: Annotated[NetworkMode, Field(default="pasta")]
                    options: Annotated[list[str], Field(default_factory=list)]
                    dns: Annotated[list[IPAddress], Field(default_factory=list)]
                    dns_search: Annotated[
                        list[str],
                        Field(default_factory=list, alias="dns-search")
                    ]
                    dns_options: Annotated[
                        list[str],
                        Field(default_factory=list, alias="dns-options")
                    ]
                    add_host: Annotated[
                        dict[HostName, HostIP],
                        Field(default_factory=dict, alias="add-host")
                    ]

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

                build: Annotated[Table, Field(default_factory=Table.model_construct)]
                run: Annotated[Table, Field(default_factory=Table.model_construct)]

            network: Annotated[Network, Field(default_factory=Network.model_construct)]

            class Tag(BaseModel):
                """Validate entries in the `[[tool.bertrand.tags]]` table."""
                model_config = ConfigDict(extra="forbid")
                tag: SanitizedName
                dependencies: Annotated[list[PEP508Requirement], Field(default_factory=list)]
                containerfile: Annotated[RelativePath, Field(default=PosixPath("Containerfile"))]
                build_args: Annotated[
                    list[str],
                    Field(default_factory=list, alias="build-args")
                ]
                env_file: Annotated[
                    list[RelativePath],
                    Field(default_factory=list, alias="env-file")
                ]

                class Port(BaseModel):
                    """Validate entries in the `[[tool.bertrand.tags.ports]]` table."""
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
                    NonNegativeInt,
                    Field(default=0, alias="pids-limit")
                ]

                class ULimit(BaseModel):
                    """Validate entries in the `[[tool.bertrand.tags.ulimit]]` table."""
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
                    AfterValidator(lambda x: Config.Tool.Bertrand.Tag._check_unique(
                        x,
                        where="cap-add capability"
                    )),
                    Field(default_factory=list, alias="cap-add")
                ]
                cap_drop: Annotated[
                    list[Capability],
                    AfterValidator(lambda x: Config.Tool.Bertrand.Tag._check_unique(
                        x,
                        where="cap-drop capability"
                    )),
                    Field(default_factory=list, alias="cap-drop")
                ]
                security_opt: Annotated[
                    list[SecurityOpt],
                    AfterValidator(lambda x: Config.Tool.Bertrand.Tag._check_unique(
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
                instruments: Annotated[list[Instrument], Field(default_factory=list)]

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
                    """Validate the `[tool.bertrand.tags.devices]` table."""
                    model_config = ConfigDict(extra="forbid")

                    class Request(BaseModel):
                        """Validate one entry in `tool.bertrand.tags.devices.*`."""
                        model_config = ConfigDict(extra="forbid")
                        id: ScreamingSnakeCase
                        required: bool = True
                        container_path: Annotated[
                            AbsolutePath | None,
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
                        AfterValidator(lambda x: Config.Tool.Bertrand.Tag.Devices._check_unique_ids(
                            x,
                            where="build"
                        )),
                        Field(default_factory=list)
                    ]
                    run: Annotated[
                        list[Request],
                        AfterValidator(lambda x: Config.Tool.Bertrand.Tag.Devices._check_unique_ids(
                            x,
                            where="run"
                        )),
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
                        AfterValidator(lambda x: Config.Tool.Bertrand.Tag.Secrets._check_unique_ids(
                            x,
                            where="build"
                        )),
                        Field(default_factory=list)
                    ]
                    run: Annotated[
                        list[Request],
                        AfterValidator(lambda x: Config.Tool.Bertrand.Tag.Secrets._check_unique_ids(
                            x,
                            where="run"
                        )),
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
                    """Validate the `[tool.bertrand.tags.conan]` table."""
                    model_config = ConfigDict(extra="forbid", populate_by_name=True)
                    build_type: Annotated[
                        Literal["", "Release", "Debug"],
                        Field(default="", alias="build-type")
                    ]
                    conf: Annotated[ConanConf, Field(default_factory=dict)]
                    requires: Annotated[
                        list[Config.Tool.Conan.Require],
                        AfterValidator(lambda x: Config.Tool.Conan._check_requires(
                            x,
                            where="[tool.bertrand.tags.conan.requires]"
                        )),
                        Field(default_factory=list)
                    ]

                conan: Annotated[Conan, Field(default_factory=Conan.model_construct)]

                class Build(BaseModel):
                    """Validate the `[tool.bertrand.tags.build]` table."""
                    model_config = ConfigDict(extra="forbid")
                    context: Annotated[RelativePath, Field(default=PosixPath("."))]
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
                    """Validate the `[tool.bertrand.tags.stop]` table."""
                    model_config = ConfigDict(extra="forbid")
                    signal: Annotated[
                        str,
                        StringConstraints(
                            strip_whitespace=True,
                            min_length=1,
                            pattern=r"^\S+$"
                        ),
                        Field(default="SIGTERM")
                    ]
                    timeout: Annotated[NonNegativeInt, Field(default=10)]

                stop: Annotated[Stop, Field(default_factory=Stop.model_construct)]

                class Restart(BaseModel):
                    """Validate the `[tool.bertrand.tags.restart]` table."""
                    model_config = ConfigDict(extra="forbid")
                    policy: Annotated[
                        Literal["no", "on-failure", "always", "unless-stopped"],
                        Field(default="no")
                    ]
                    max_retries: Annotated[
                        NonNegativeInt,
                        Field(default=0, alias="max-retries")
                    ]

                restart: Annotated[Restart, Field(default_factory=Restart.model_construct)]

                class Healthcheck(BaseModel):
                    """Validate the `[tool.bertrand.tags.healthcheck]` table."""
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
                        """Validate the `[tool.bertrand.tags.healthcheck.startup]` table."""
                        model_config = ConfigDict(extra="forbid")
                        cmd: Annotated[list[str], Field(default_factory=list)]
                        period: Annotated[Timeout, Field(default="0s")]
                        success: Annotated[NonNegativeInt, Field(default=0)]
                        interval: Annotated[Timeout, Field(default="30s")]
                        timeout: Annotated[Timeout, Field(default="30s")]

                    startup: Annotated[Startup, Field(default_factory=Startup.model_construct)]

                    class Log(BaseModel):
                        """Validate the `[tool.bertrand.tags.healthcheck.log]` table."""
                        model_config = ConfigDict(extra="forbid")
                        destination: Annotated[HealthLogDestination, Field(default="local")]
                        max_count: Annotated[
                            NonNegativeInt,
                            Field(default=0, alias="max-count")
                        ]
                        max_size: Annotated[
                            NonNegativeInt,
                            Field(default=0, alias="max-size")
                        ]

                    log: Annotated[Log, Field(default_factory=Log.model_construct)]

                healthcheck: Annotated[
                    Healthcheck,
                    Field(default_factory=Healthcheck.model_construct)
                ]

                def _resolve_containerfile(self, root: Path) -> None:
                    _check_text_file(root / self.containerfile, tag=self.tag)

                def _resolve_env_files(self, root: Path) -> None:
                    seen: set[PosixPath] = set()
                    for idx, path in enumerate(self.env_file):
                        if path in seen:
                            raise ValueError(
                                f"duplicate env-file path for tag '{self.tag}' at index "
                                f"{idx}: {path}"
                            )
                        _check_text_file(root / path, tag=self.tag)
                        seen.add(path)

            tags: Annotated[list[Tag], Field(default_factory=lambda: [
                Config.Tool.Bertrand.Tag.model_construct(tag="base")
            ])]

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
                                f"{option} for tag '{tag.tag}' cannot reference itself "
                                f"via 'container:{ref}'"
                            )

                        # get referenced service position + tag
                        ref_pos = next(
                            (pos for pos, name in enumerate(self.services) if name == ref),
                            None
                        )
                        if ref_pos is None:
                            raise ValueError(
                                f"{option} for tag '{tag.tag}' references '{ref}', but "
                                f"'{ref}' is not listed in 'tool.bertrand.services'"
                            )
                        ref_tag = next((t for t in self.tags if t.tag == ref), None)
                        if ref_tag is None:
                            raise ValueError(
                                f"{option} for tag '{tag.tag}' references unknown tag "
                                f"'{ref}'"
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
                                f"ipc for tag '{tag.tag}' uses 'container:{ref}', but "
                                f"referenced tag '{ref}' must set ipc='shareable'"
                            )
                return self

        bertrand: Annotated[Bertrand | None, Field(default=None)]

        class Clangd(BaseModel):
            """Validate the `[tool.clangd]` table."""
            model_config = ConfigDict(extra="forbid")

            class _Diagnostics(BaseModel):
                """Validate the `[tool.clangd.diagnostics]` table."""
                model_config = ConfigDict(extra="forbid")
                UnusedIncludes: Annotated[Literal["None", "Strict"], Field(default="Strict")]
                MissingIncludes: Annotated[Literal["None", "Strict"], Field(default="Strict")]
                Suppress: Annotated[list[NoCRLF], Field(default_factory=list)]

            Diagnostics: Annotated[
                _Diagnostics,
                Field(default_factory=_Diagnostics.model_construct)
            ]

            class _Index(BaseModel):
                """Validate the `[tool.clangd.index]` table."""
                model_config = ConfigDict(extra="forbid")
                Background: Annotated[Literal["Build", "Skip"], Field(default="Build")]
                StandardLibrary: Annotated[bool, Field(default=True)]

            Index: Annotated[
                _Index,
                Field(default_factory=_Index.model_construct)
            ]

            class _Completion(BaseModel):
                """Validate the `[tool.clangd.completion]` table."""
                model_config = ConfigDict(extra="forbid")
                AllScopes: Annotated[bool, Field(default=True)]
                ArgumentLists: Annotated[
                    Literal["None", "OpenDelimiter", "Delimiters", "FullPlaceholders"],
                    Field(default="FullPlaceholders")
                ]
                HeaderInsertion: Annotated[Literal["Never", "IWYU"], Field(default="IWYU")]
                CodePatterns: Annotated[Literal["None", "All"], Field(default="All")]

            Completion: Annotated[
                _Completion,
                Field(default_factory=_Completion.model_construct)
            ]

            class _InlayHints(BaseModel):
                """Validate the `[tool.clangd.inlay-hints]` table."""
                model_config = ConfigDict(extra="forbid")
                Enabled: Annotated[bool, Field(default=True)]
                ParameterNames: Annotated[bool, Field(default=True)]
                DeducedTypes: Annotated[bool, Field(default=True)]
                Designators: Annotated[bool, Field(default=True)]
                BlockEnd: Annotated[bool, Field(default=False)]
                DefaultArguments: Annotated[bool, Field(default=False)]
                TypeNameLimit: Annotated[NonNegativeInt, Field(default=24)]

            InlayHints: Annotated[
                _InlayHints,
                Field(default_factory=_InlayHints.model_construct)
            ]

            class _Hover(BaseModel):
                """Validate the `[tool.clangd.hover]` table."""
                model_config = ConfigDict(extra="forbid")
                ShowAKA: Annotated[bool, Field(default=True)]
                MacroContentsLimit: Annotated[NonNegativeInt, Field(default=2048)]

            Hover: Annotated[
                _Hover,
                Field(default_factory=_Hover.model_construct)
            ]

            class _Documentation(BaseModel):
                """Validate the `[tool.clangd.documentation]` table."""
                model_config = ConfigDict(extra="forbid")
                CommentFormat: Annotated[
                    Literal["PlainText", "Markdown", "Doxygen"],
                    Field(default="Doxygen")
                ]

            Documentation: Annotated[
                _Documentation,
                Field(default_factory=_Documentation.model_construct)
            ]

            class _If(BaseModel):
                """Validate the `[[tool.clangd.if]]` AoT."""
                model_config = ConfigDict(extra="forbid")
                PathMatch: NonEmpty[list[RegexPattern]]
                PathExclude: Annotated[list[RegexPattern], Field(default_factory=list)]

                class _Diagnostics(BaseModel):
                    """Validate the `[tool.clangd.diagnostics]` table."""
                    model_config = ConfigDict(extra="forbid")
                    UnusedIncludes: Annotated[
                        Literal["None", "Strict"] | None,
                        Field(default=None)
                    ]
                    MissingIncludes: Annotated[
                        Literal["None", "Strict"] | None,
                        Field(default=None)
                    ]
                    Suppress: Annotated[
                        NonEmpty[list[NoCRLF]] | None,
                        Field(default=None)
                    ]

                Diagnostics: Annotated[_Diagnostics | None, Field(default=None)]

                class _Index(BaseModel):
                    """Validate the `[tool.clangd.index]` table."""
                    model_config = ConfigDict(extra="forbid")
                    Background: Annotated[
                        Literal["Build", "Skip"] | None,
                        Field(default=None)
                    ]
                    StandardLibrary: Annotated[bool | None, Field(default=None)]

                Index: Annotated[_Index | None, Field(default=None)]

                class _Completion(BaseModel):
                    """Validate the `[tool.clangd.completion]` table."""
                    model_config = ConfigDict(extra="forbid")
                    AllScopes: Annotated[bool | None, Field(default=None)]
                    ArgumentLists: Annotated[
                        Literal["None", "OpenDelimiter", "Delimiters", "FullPlaceholders"] | None,
                        Field(default=None)
                    ]
                    HeaderInsertion: Annotated[
                        Literal["Never", "IWYU"] | None,
                        Field(default=None)
                    ]
                    CodePatterns: Annotated[
                        Literal["None", "All"] | None,
                        Field(default=None)
                    ]

                Completion: Annotated[_Completion | None, Field(default=None)]

                class _InlayHints(BaseModel):
                    """Validate the `[tool.clangd.inlay-hints]` table."""
                    model_config = ConfigDict(extra="forbid")
                    Enabled: Annotated[bool | None, Field(default=None)]
                    ParameterNames: Annotated[bool | None, Field(default=None)]
                    DeducedTypes: Annotated[bool | None, Field(default=None)]
                    Designators: Annotated[bool | None, Field(default=None)]
                    BlockEnd: Annotated[bool | None, Field(default=None)]
                    DefaultArguments: Annotated[bool | None, Field(default=None)]
                    TypeNameLimit: Annotated[NonNegativeInt | None, Field(default=None)]

                InlayHints: Annotated[_InlayHints | None, Field(default=None)]

                class _Hover(BaseModel):
                    """Validate the `[tool.clangd.hover]` table."""
                    model_config = ConfigDict(extra="forbid")
                    ShowAKA: Annotated[bool | None, Field(default=None)]
                    MacroContentsLimit: Annotated[NonNegativeInt | None, Field(default=None)]

                Hover: Annotated[_Hover | None, Field(default=None)]

                class _Documentation(BaseModel):
                    """Validate the `[tool.clangd.documentation]` table."""
                    model_config = ConfigDict(extra="forbid")
                    CommentFormat: Annotated[
                        Literal["PlainText", "Markdown", "Doxygen"] | None,
                        Field(default=None)
                    ]

                Documentation: Annotated[_Documentation | None, Field(default=None)]

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

            If: Annotated[list[_If], Field(default_factory=list)]

        clangd: Annotated[Clangd | None, Field(default=None)]

        # TODO: DisableFormat -> FormatStyle in generated .clang-tidy config.
        # InheritParentConfig is implied to be false, to enforce isolation within
        # the project.

        class ClangTidy(BaseModel):
            """Validate the `[tool.clang-tidy]` table."""
            model_config = ConfigDict(extra="forbid")

            class CheckEntry(BaseModel):
                """Validate entries in the `[[tool.clang-tidy.Checks]]` AoT."""
                model_config = ConfigDict(extra="forbid")
                Check: ClangTidyCheckPattern
                Action: Annotated[Literal["disable", "warn", "error"], Field(default="warn")]
                Options: Annotated[
                    dict[ClangTidyOptionName, ConanScalar],
                    Field(default_factory=dict)
                ]

            @staticmethod
            def _check_duplicate_checks(value: list[CheckEntry]) -> list[CheckEntry]:
                seen: set[ClangTidyCheckPattern] = set()
                for entry in value:
                    if entry.Check in seen:
                        raise ValueError(
                            f"duplicate clang-tidy check entry: '{entry.Check}'"
                        )
                    seen.add(entry.Check)
                return value

            HeaderFilterRegex: Annotated[RegexPattern, Field(default=".*")]
            ExcludeHeaderFilterRegex: Annotated[RegexPattern, Field(default="^$")]
            SystemHeaders: Annotated[bool, Field(default=False)]
            UseColor: Annotated[bool, Field(default=True)]
            Checks: Annotated[
                list[CheckEntry],
                AfterValidator(_check_duplicate_checks),
                Field(default_factory=list)
            ]

        clang_tidy: Annotated[ClangTidy | None, Field(default=None, alias="clang-tidy")]

        class ClangFormat(BaseModel):
            """Validate the `[tool.clang-format]` table."""
            model_config = ConfigDict(extra="forbid")
            # TODO: populate this section

        clang_format: Annotated[ClangFormat | None, Field(default=None, alias="clang-format")]

    class _Parse(BaseModel):
        """Validate the output of the `parse()` pass for all resources."""
        model_config = ConfigDict(extra="forbid")
        build_system: Config.BuildSystem | None = Field(default=None, alias="build-system")
        project: Config.Project | None = Field(default=None)
        tool: Config.Tool | None = Field(default=None)

    root: Path
    resources: dict[str, PosixPath]
    build_system: Config.BuildSystem | None = field(default=None, repr=False)
    project: Config.Project | None = field(default=None, repr=False)
    tool: Config.Tool | None = field(default=None, repr=False)
    _entered: int = field(default=0, repr=False)
    _key_owner: dict[tuple[str, ...], str] = field(
        default_factory=dict,
        init=False,
        repr=False,
    )

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

    def __post_init__(self) -> None:
        self.root = self.root.expanduser().resolve()
        for r_id, path in self.resources.items():
            if r_id not in CATALOG:
                raise ValueError(f"unknown resource id in config: '{r_id}'")
            self._check_relative_path(path, where=f"resource '{r_id}'")

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

    # TODO: init() shouldn't fail if any of the paths are missing during profile
    # detection?

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
            if profile is None:
                # choose the profile with the most matching placements, to prefer src
                # layouts over flat where both would be valid
                n = 0
                for candidate_profile, candidate in PROFILES.items():
                    if len(candidate) > n and all(
                        result.resources.get(r_id) == path
                        for r_id, path in candidate.items()
                    ):
                        profile = candidate_profile
                        n = len(candidate)

                # if we couldn't infer a profile, then we hard error rather than clobbering
                # an existing environment
                if profile is None:
                    raise ValueError(
                        "unable to infer layout profile from environment, please specify "
                        f"explicitly (supported: {', '.join(sorted(p for p in PROFILES))})"
                    )
            else:
                profile = profile.strip()
                if not profile:
                    raise ValueError("layout profile cannot be empty")
                placements = PROFILES.get(profile)
                if placements is None:
                    raise ValueError(
                        f"unknown layout profile: {profile} (supported: "
                        f"{', '.join(sorted(p for p in PROFILES))})"
                    )

                # update the result with the chosen placements, checking for collisions
                for r_id, path in placements.items():
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
                    variants = CAPABILITIES.get(cap)
                    if variants is None:
                        raise ValueError(
                            f"unknown layout capability: '{cap}' (supported: "
                            f"{', '.join(sorted(CAPABILITIES))})"
                        )
                    placements = variants.get(profile, {})

                    # check for collisions during merge
                    for r_id, path in placements.items():
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

    def _merge_fragment(
        self,
        resource_id: str,
        fragment: dict[Any, Any],
        merged: dict[str, Any],
        *,
        key_owner: dict[tuple[str, ...], str],
        path_prefix: tuple[str, ...] = (),
    ) -> None:
        for raw_key, value in fragment.items():
            if not isinstance(raw_key, str):
                if path_prefix:
                    parent = ".".join(path_prefix)
                else:
                    parent = "<root>"
                raise OSError(
                    f"parse hook for resource '{resource_id}' returned non-string key "
                    f"under '{parent}': '{raw_key}'"
                )

            # insert value if key is new, and recurse if value is a nested dict
            key_path = path_prefix + (raw_key,)
            if raw_key not in merged:
                if isinstance(value, dict):
                    child: dict[str, Any] = {}
                    merged[raw_key] = child
                    key_owner[key_path] = resource_id
                    self._merge_fragment(
                        resource_id,
                        value,
                        child,
                        key_owner=key_owner,
                        path_prefix=key_path,
                    )
                else:
                    merged[raw_key] = value
                    key_owner[key_path] = resource_id
                continue

            # if an existing key is present, and both the key and value are nested
            # dicts, then merge recursively
            existing = merged[raw_key]
            if isinstance(existing, dict) and isinstance(value, dict):
                self._merge_fragment(
                    resource_id,
                    value,
                    existing,
                    key_owner=key_owner,
                    path_prefix=key_path,
                )
                continue

            # otherwise, this is a collision
            owner = key_owner.get(key_path, "<unknown>")
            raise OSError(
                f"config parse key collision at '{'.'.join(key_path)}' between "
                f"resources '{owner}' and '{resource_id}'"
            )

    def __enter__(self) -> Self:
        """Load a context-scoped config snapshot from parse-capable resources."""
        if self._entered > 0:  # re-entrant case
            self._entered += 1
            return self

        try:
            with lock_env(self.root):
                merged: dict[str, Any] = {}
                key_owner: dict[tuple[str, ...], str] = {}

                # invoke parse hooks for all resources in deterministic order
                for resource_id in sorted(self.resources):
                    r = CATALOG.get(resource_id)
                    if r is None:
                        raise OSError(
                            f"config references unknown resource ID: '{resource_id}'"
                        )

                    # extract config fragment and ensure key-value mapping
                    try:
                        result = r.parse(self)
                        if result is None:
                            continue
                    except Exception as err:
                        raise OSError(
                            f"failed to parse resource '{resource_id}' at "
                            f"{self.path(resource_id)}: {err}"
                        ) from err
                    if not isinstance(result, dict):
                        raise OSError(
                            f"parse hook for resource '{resource_id}' must return a "
                            f"string mapping: {result}"
                        )

                    # merge fragment into snapshot, checking for key collisions
                    self._merge_fragment(
                        resource_id,
                        result,
                        merged,
                        key_owner=key_owner,
                    )

                # validate merged snapshot against expected schema
                data = Config._Parse.model_validate(merged)
                self.build_system = data.build_system
                self.project = data.project
                if self.project is not None:
                    self.project._resolve_licenses(self.root)
                self.tool = data.tool
                if self.tool is not None and self.tool.bertrand is not None:
                    for tag in self.tool.bertrand.tags:
                        tag._resolve_containerfile(self.root)
                        tag._resolve_env_files(self.root)
                self._key_owner = key_owner
                self._entered += 1
                return self
        except:
            self.build_system = None
            self.project = None
            self.tool = None
            self._key_owner = {}
            raise

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
            self.build_system = None
            self.project = None
            self.tool = None
            self._key_owner = {}

    def __bool__(self) -> bool:
        return self._entered > 0

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
        if not self:
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
