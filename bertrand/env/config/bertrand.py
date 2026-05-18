"""Define Bertrand's project configuration resource.

The metadata for this resource is expected to be found under the `[bertrand]` key in
project configuration, which is usually provided by `pyproject.toml`.  It is
responsible for rendering the basic directory structure and container/repository
artifacts needed by Bertrand's core functionality.
"""

from __future__ import annotations

import re
import tomllib
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

from bertrand.env.git import METADATA_DIR, Scalar, atomic_write_text
from bertrand.env.version import VERSION

from .core import (
    SANITIZE_RE,
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
    locate_template,
    resource,
)
from .python import PyProject

# Configuration options that affect CLI behavior
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


NS_PATH_RE = re.compile(r"^ns:\S+$")
SERVICE_PORT_NAME_RE = re.compile(r"^[a-z0-9]([-a-z0-9]{0,13}[a-z0-9])?$")
USERNS_CONTAINER_REF_RE = re.compile(r"^[A-Za-z0-9._-]+$")
USERNS_MAPPING_RE = re.compile(r"^(?P<container>\d+):(?P<host>@?\d+):(?P<length>\d+)$")
CAPABILITY_TOKEN_RE = re.compile(r"^CAP_[A-Z0-9_]+$")
CAPABILITY_DEFINE_RE = re.compile(r"^\s*#define\s+(CAP_[A-Z0-9_]+)\s+([0-9]+)\b")
SECURITY_OPT_KEY_RE = re.compile(r"^[a-z0-9][a-z0-9_.-]*$")
IMAGE_TAG_RE = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_.-]{0,127}$")
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


def _check_service_port_name(name: str) -> str:
    if not SERVICE_PORT_NAME_RE.fullmatch(name):
        msg = (
            f"invalid service port name: {name!r} (must be lowercase alphanumeric "
            "or '-', must start and end with an alphanumeric character, and must be "
            "15 characters or fewer)"
        )
        raise ValueError(msg)
    return name


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


def _default_workload_healthcheck() -> Any:
    return Bertrand.Model.Healthcheck.model_construct()


def project_image_tag(project_version: str) -> str:
    """Derive the OCI tag for the configured project image.

    Parameters
    ----------
    project_version : str
        Version from the worktree's ``[project].version`` field.

    Returns
    -------
    str
        OCI tag used for the internal and external image manifests.

    Raises
    ------
    ValueError
        If the project version is not a valid OCI tag.
    """
    version = project_version.strip()
    if not IMAGE_TAG_RE.fullmatch(version):
        msg = f"project version does not form a valid OCI tag: {project_version!r}"
        raise ValueError(msg)
    return version


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


type Shell = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_shell)]
type Editor = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_editor)]
type IgnoreList = Annotated[list[Glob], AfterValidator(_check_ignore_list)]
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
type ServicePortName = Annotated[
    NonEmpty[NoWhiteSpace],
    AfterValidator(_check_service_port_name),
]
type NetworkPolicy = Literal["open", "isolated"]
type NetworkProtocol = Literal["tcp", "udp", "sctp"]
type ImagePullPolicy = Literal["missing", "always", "never"]
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
type UserNS = Annotated[
    NonEmpty[NoWhiteSpace],
    AfterValidator(_check_userns),
]
type IPCMode = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_ipc)]
type PIDMode = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_pid)]
type UTSMode = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_uts)]
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

    class Model(BaseModel):
        """Validate the `[bertrand]` table."""

        class Network(BaseModel):
            """Validate the `[bertrand.network]` table."""

            # TODO: Native workload networking backend architecture:
            # - Kubernetes networking is pod-scoped.  Every container in a rendered
            #   workload Pod shares one network namespace, one Pod IP, and one
            #   localhost interface.  Container config should only declare the ports
            #   that each process listens on; Service discovery, isolation, and
            #   publication belong to this workload-level network model.
            # - The backend should render a canonical internal ClusterIP Service
            #   automatically whenever any `[[tool.bertrand.containers]].ports` entry
            #   exists.  There should be no `service = true|false|auto` config flag:
            #   declared ports imply a Service, and no declared ports imply no
            #   Service.  The Service should select the workload's Pods via stable
            #   Bertrand labels and expose every unique named container port as a
            #   Service port.  Duplicate port names are invalid because Gateway
            #   routes and Service target ports need stable symbolic references.
            # - `policy = "open"` should render no restrictive NetworkPolicy, which
            #   keeps development ergonomics predictable.  `policy = "isolated"` is
            #   reserved for a later NetworkPolicy pass that creates explicit
            #   Kubernetes NetworkPolicy objects around the workload.  That policy
            #   backend should remain best-effort/actionable when the CNI does not
            #   enforce NetworkPolicy, because Kubernetes delegates enforcement to the
            #   installed network plugin.
            # - `routes` is the future external-publication surface and should target
            #   Gateway API, not Ingress, NodePort, or LoadBalancer project config.
            #   Route entries should become HTTPRoute/TCPRoute/etc. intents that bind
            #   external host/path/listener rules to the canonical internal Service.
            #   Gateway API support should be validated during workload convergence,
            #   with actionable diagnostics if the cluster lacks the needed CRDs or
            #   GatewayClass.
            # - Aliases are intentionally omitted for now.  The one-worktree,
            #   one-workload model gives each workload one canonical Service DNS
            #   identity, and extra in-cluster Service aliases can be added later if
            #   dependency overlays or compatibility hostnames create a concrete need.
            # - Podman-shaped fields are intentionally omitted: no network namespace
            #   mode, host bindings, host IPs, DNS override table, host aliases,
            #   NodePort, LoadBalancer, or Ingress flags.  Those concepts either do
            #   not map cleanly to Kubernetes workload networking or are inferior to
            #   Service + NetworkPolicy + Gateway API for Bertrand's target model.

            class Route(BaseModel):
                """Validate a future Gateway API route intent."""

                model_config = ConfigDict(extra="forbid")
                host: Annotated[
                    HostName,
                    Field(
                        examples=["app.local"],
                        description=(
                            "External hostname that a future Gateway API route should "
                            "match for this workload."
                        ),
                    ),
                ]
                port: Annotated[
                    ServicePortName,
                    Field(
                        examples=["http"],
                        description=(
                            "Named container port that the future Gateway API route "
                            "should target through the workload's canonical Service."
                        ),
                    ),
                ]
                path: Annotated[
                    str,
                    StringConstraints(strip_whitespace=True, pattern=r"^/.*$"),
                    Field(
                        default="/",
                        examples=["/"],
                        description=(
                            "HTTP path prefix reserved for future Gateway API route "
                            "rendering."
                        ),
                    ),
                ]

            model_config = ConfigDict(extra="forbid")
            policy: Annotated[
                NetworkPolicy,
                Field(
                    default="open",
                    examples=["open", "isolated"],
                    description=(
                        "Workload network isolation policy.  `open` renders no "
                        "restrictive NetworkPolicy, while `isolated` is reserved for "
                        "a future Kubernetes NetworkPolicy backend."
                    ),
                ),
            ]
            routes: Annotated[
                list[Route],
                Field(
                    default_factory=list,
                    examples=[[{"host": "app.local", "port": "http", "path": "/"}]],
                    description=(
                        "Future Gateway API route intents for publishing this "
                        "workload outside the cluster.  Route support is config-only "
                        "until the native Gateway backend is implemented."
                    ),
                ),
            ]

        class Secret(BaseModel):
            """Validate one Kubernetes Secret capability request."""

            model_config = ConfigDict(extra="forbid")
            id: Annotated[
                KubeName,
                Field(
                    examples=["pypi_token", "private_pkg_key"],
                    description=(
                        "Host-agnostic capability ID for a secret credential or "
                        "payload.  The ID is resolved using a managed Kubernetes "
                        "Secret capability in the Bertrand namespace."
                    ),
                ),
            ]
            required: Annotated[
                bool,
                Field(
                    default=True,
                    description=(
                        "Whether this secret capability must be available before "
                        "build or runtime execution.  If true and unresolved, "
                        "execution fails before starting."
                    ),
                ),
            ]

        class SSH(BaseModel):
            """Validate one Kubernetes SSH credential capability request."""

            model_config = ConfigDict(extra="forbid")
            id: Annotated[
                KubeName,
                Field(
                    examples=["git_deploy_key", "github_readonly"],
                    description=(
                        "Host-agnostic capability ID for an SSH credential.  The ID "
                        "is resolved using a managed Kubernetes Secret capability in "
                        "the Bertrand namespace."
                    ),
                ),
            ]
            required: Annotated[
                bool,
                Field(
                    default=True,
                    description=(
                        "Whether this SSH capability must be available before build "
                        "or runtime execution.  If true and unresolved, execution "
                        "fails before starting."
                    ),
                ),
            ]

        class Device(BaseModel):
            """Validate one Kubernetes DRA device capability request."""

            model_config = ConfigDict(extra="forbid")
            id: Annotated[
                KubeName,
                Field(
                    examples=["gpu", "cuda0"],
                    description=(
                        "Host-agnostic capability ID for a DRA device capability.  "
                        "Kubernetes allocates the concrete device and Bertrand "
                        "consumes the allocated CDI selector."
                    ),
                ),
            ]
            required: Annotated[
                bool,
                Field(
                    default=True,
                    description=(
                        "Whether this device capability must be available before "
                        "build or runtime execution.  If true and unresolved, "
                        "execution fails before starting."
                    ),
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
                        "define custom build steps for their projects outside of "
                        "Bertrand's normal bootstrap procedure.  For the vast majority "
                        "of users, this should be omitted, and relevant setup should "
                        "be done through standard build tools and package managers "
                        "defined elsewhere in project configuration.  If omitted, "
                        "Bertrand will automatically generate a minimal Containerfile "
                        "based on this information."
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
            pull: Annotated[
                ImagePullPolicy,
                Field(
                    default="missing",
                    examples=["missing", "always", "never"],
                    description=(
                        "BuildKit base-image resolution policy.  `missing` uses "
                        "BuildKit's default resolver behavior, `always` forces remote "
                        "resolution for base images, and `never` restricts resolution "
                        "to images already present in the builder's local cache."
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
                        "`ghcr.io/acme/toolchain:1.2.3@sha256:<digest>`.  Shorthand "
                        "refs like `ubuntu:24.04` and transport-prefixed refs like "
                        "`docker://ghcr.io/acme/toolchain:1.2.3` are rejected for "
                        "portability.  This is incompatible with custom "
                        "`containerfile`s, and is intended to supplement "
                        "language-specific package managers for multilingual projects "
                        "that make use of advanced features like dynamic compilation, "
                        "which require a supporting toolchain."
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
                                "[tool.bertrand.image]",
                                "args = { DEBUG = true, JIT = true }",
                            )
                        ),
                        "\n".join(
                            (
                                "[tool.bertrand.image.args]",
                                "DEBUG = true",
                                "JIT = true",
                                "...",
                            )
                        ),
                    ],
                    description=(
                        "Mapping of build-time ARG variables to their values, which "
                        "are passed to the listed Containerfile.  Bertrand-generated "
                        "Containerfiles include only a small number of ARG "
                        "instructions to constrain the base image, but custom "
                        "Containerfiles are unrestricted."
                    ),
                ),
            ]
            secrets: Annotated[
                list[Bertrand.Model.Secret],
                AfterValidator(
                    lambda x: Bertrand.Model._check_unique_requests(
                        x, where="image secret"
                    )
                ),
                Field(
                    default_factory=list,
                    examples=[
                        "\n".join(
                            (
                                "[tool.bertrand.image]",
                                "secrets = [{ id = \"pypi_token\", required = true }]",
                            )
                        ),
                        "\n".join(
                            (
                                "[[tool.bertrand.image.secrets]]",
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
            ssh: Annotated[
                list[Bertrand.Model.SSH],
                AfterValidator(
                    lambda x: Bertrand.Model._check_unique_requests(
                        x, where="image ssh"
                    )
                ),
                Field(
                    default_factory=list,
                    examples=[
                        "\n".join(
                            (
                                "[tool.bertrand.image]",
                                "ssh = [{ id = \"git_deploy_key\", required = true }]",
                            )
                        ),
                        "\n".join(
                            (
                                "[[tool.bertrand.image.ssh]]",
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
            devices: Annotated[
                list[Bertrand.Model.Device],
                AfterValidator(
                    lambda x: Bertrand.Model._check_unique_requests(
                        x, where="image device"
                    )
                ),
                Field(
                    default_factory=list,
                    examples=[
                        "\n".join(
                            (
                                "[tool.bertrand.image]",
                                "devices = [{ id = \"gpu\", required = true }]",
                            )
                        ),
                        "\n".join(
                            (
                                "[[tool.bertrand.image.devices]]",
                                "id = \"fpga0\"",
                                "required = false",
                            )
                        ),
                    ],
                    description=(
                        "Build-time DRA device capabilities resolved from the local "
                        "Kubernetes cluster."
                    ),
                ),
            ]

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

            def resolve_containerfile(self, root: Path) -> None:
                """Validate the custom Containerfile path.

                Raises
                ------
                OSError
                    If the Containerfile path is missing, not a file, or not UTF-8.
                """
                if self.containerfile is None:
                    return
                path = root / self.containerfile
                if not path.exists():
                    msg = f"custom Containerfile path does not exist: {path}"
                    raise OSError(msg)
                if not path.is_file():
                    msg = f"custom Containerfile path is not a file: {path}"
                    raise OSError(msg)
                try:
                    path.read_text(encoding="utf-8")
                except UnicodeDecodeError as err:
                    msg = f"custom Containerfile is not UTF-8 encoded: {path}"
                    raise OSError(msg) from err

        class Port(BaseModel):
            """Validate entries in the workload ports table."""

            model_config = ConfigDict(extra="forbid")
            name: Annotated[
                ServicePortName,
                Field(
                    examples=["http"],
                    description=(
                        "Stable Kubernetes Service port name.  Future Service and "
                        "Gateway rendering uses this name as the durable target for "
                        "the container listener."
                    ),
                ),
            ]
            port: Annotated[
                int,
                Field(
                    ge=1,
                    le=65535,
                    examples=[8080],
                    description="Container port number exposed by this process.",
                ),
            ]
            protocol: Annotated[
                NetworkProtocol,
                Field(
                    default="tcp",
                    examples=["tcp", "udp", "sctp"],
                    description="Transport protocol for this named container port.",
                ),
            ]

        class ULimit(BaseModel):
            """Validate entries in a workload container ulimit table."""

            model_config = ConfigDict(extra="forbid")
            name: ULimitName
            soft: Annotated[int | None, Field(default=None, ge=-1)]
            hard: Annotated[int | None, Field(default=None, ge=-1)]

            @model_validator(mode="after")
            def _validate_limits(self) -> Self:
                if self.name == "host":
                    if self.soft is not None or self.hard is not None:
                        msg = "ulimit name 'host' cannot define 'soft' or 'hard' values"
                        raise ValueError(msg)
                    return self
                if self.soft is None or self.hard is None:
                    msg = "non-'host' ulimit entries must define both 'soft' and 'hard'"
                    raise ValueError(msg)
                if self.hard >= 0 and self.soft > self.hard:
                    msg = (
                        f"ulimit soft value {self.soft} cannot be greater "
                        f"than hard value {self.hard}"
                    )
                    raise ValueError(msg)
                return self

        class Stop(BaseModel):
            """Validate the `[tool.bertrand.stop]` table."""

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

        class Restart(BaseModel):
            """Validate the `[tool.bertrand.restart]` table."""

            model_config = ConfigDict(extra="forbid")
            policy: Annotated[
                Literal["no", "on-failure", "always", "unless-stopped"],
                Field(default="no"),
            ]
            max_retries: Annotated[
                NonNegativeInt, Field(default=0, alias="max-retries")
            ]

        class Healthcheck(BaseModel):
            """Validate a workload container healthcheck table."""

            class Startup(BaseModel):
                """Validate a workload container startup healthcheck table."""

                model_config = ConfigDict(extra="forbid")
                cmd: Annotated[list[str], Field(default_factory=list)]
                period: Annotated[Timeout, Field(default="0s")]
                success: Annotated[NonNegativeInt, Field(default=0)]
                interval: Annotated[Timeout, Field(default="30s")]
                timeout: Annotated[Timeout, Field(default="30s")]

            class Log(BaseModel):
                """Validate a workload container healthcheck log table."""

                model_config = ConfigDict(extra="forbid")
                destination: Annotated[HealthLogDestination, Field(default="local")]
                max_count: Annotated[
                    NonNegativeInt, Field(default=0, alias="max-count")
                ]
                max_size: Annotated[NonNegativeInt, Field(default=0, alias="max-size")]

            model_config = ConfigDict(extra="forbid")
            cmd: Annotated[list[str], Field(default_factory=list)]
            on_failure: Annotated[
                Literal["none", "kill", "stop"],
                Field(default="kill", alias="on-failure"),
            ]
            retries: Annotated[NonNegativeInt, Field(default=3)]
            interval: Annotated[Timeout, Field(default="30s")]
            timeout: Annotated[Timeout, Field(default="30s")]
            startup: Annotated[Startup, Field(default_factory=Startup.model_construct)]
            log: Annotated[Log, Field(default_factory=Log.model_construct)]

        class Container(BaseModel):
            """Validate one native workload container entry."""

            model_config = ConfigDict(extra="forbid")
            name: KubeName
            cmd: Annotated[
                NonEmpty[list[NonEmpty[Trimmed]]],
                Field(
                    examples=[
                        ["echo", "Hello, world!"],
                        ["greet"],
                    ],
                    description=(
                        "The explicit command to execute for this container, "
                        "defined as a list of strings representing the executable "
                        "and its arguments.  Bertrand requires this command so it "
                        "can wrap the container with repository bootstrap before "
                        "execution."
                    ),
                ),
            ]
            cpus: Annotated[
                NonNegativeFloat,
                Field(
                    default=0.0,
                    description=(
                        "The number of CPUs to allocate to containers built from "
                        "this image.  0.0 (the default) removes the limit and "
                        "allows the container to use all available resources.  "
                        "Fractional values are allowed to specify partial CPU "
                        "allocation (e.g. 0.5 for half a CPU)."
                    ),
                ),
            ]
            memory: Annotated[
                Memory,
                Field(
                    default="0",
                    examples=["1024b", "128k", "512m", "2g"],
                    description=(
                        "The amount of memory to allocate to containers built "
                        "from this image.  0 (the default) removes the limit and "
                        "allows the container to use all available resources.  If "
                        "the machine supports swap memory, then the value may be "
                        "larger than the physical memory.  Equivalent to `podman "
                        "build|create -m`."
                    ),
                ),
            ]
            pids_limit: Annotated[int, Field(default=0, ge=-1, alias="pids-limit")]
            shm_size: Annotated[Memory, Field(default="64m", alias="shm-size")]
            ulimit: Annotated[
                list[Bertrand.Model.ULimit],
                AfterValidator(lambda x: Bertrand.Model._check_ulimit(x)),
                Field(default_factory=list),
            ]
            cap_add: Annotated[
                list[Capability],
                AfterValidator(
                    lambda x: Bertrand.Model._check_unique(
                        x, where="cap-add capability"
                    )
                ),
                Field(default_factory=list, alias="cap-add"),
            ]
            cap_drop: Annotated[
                list[Capability],
                AfterValidator(
                    lambda x: Bertrand.Model._check_unique(
                        x, where="cap-drop capability"
                    )
                ),
                Field(default_factory=list, alias="cap-drop"),
            ]
            security_opt: Annotated[
                list[SecurityOpt],
                AfterValidator(
                    lambda x: Bertrand.Model._check_unique(
                        x, where="security-opt entry"
                    )
                ),
                Field(default_factory=list, alias="security-opt"),
            ]
            ports: Annotated[
                list[Bertrand.Model.Port],
                AfterValidator(lambda x: Bertrand.Model._check_ports(x)),
                Field(default_factory=list),
            ]
            ssh: Annotated[
                list[Bertrand.Model.SSH],
                AfterValidator(
                    lambda x: Bertrand.Model._check_unique_requests(
                        x, where="container ssh"
                    )
                ),
                Field(default_factory=list),
            ]
            devices: Annotated[
                list[Bertrand.Model.Device],
                AfterValidator(
                    lambda x: Bertrand.Model._check_unique_requests(
                        x, where="container device"
                    )
                ),
                Field(default_factory=list),
            ]
            secrets: Annotated[
                list[Bertrand.Model.Secret],
                AfterValidator(
                    lambda x: Bertrand.Model._check_unique_requests(
                        x, where="container secret"
                    )
                ),
                Field(default_factory=list),
            ]
            healthcheck: Annotated[
                Bertrand.Model.Healthcheck,
                Field(default_factory=_default_workload_healthcheck),
            ]
            metadata: Annotated[
                dict[NonEmpty[SnakeCase], Scalar],
                Field(default_factory=dict),
            ]

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

        @staticmethod
        def _check_ports(ports: list[Port]) -> list[Port]:
            seen: set[ServicePortName] = set()
            for port in ports:
                if port.name in seen:
                    msg = f"duplicate workload port name: '{port.name}'"
                    raise ValueError(msg)
                seen.add(port.name)
            return ports

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

        @staticmethod
        def _check_unique_requests(
            requests: list[Any],
            *,
            where: str,
        ) -> list[Any]:
            seen: set[KubeName] = set()
            for req in requests:
                if req.id in seen:
                    msg = f"duplicate {where} capability id: '{req.id}'"
                    raise ValueError(msg)
                seen.add(req.id)
            return requests

        @staticmethod
        def _check_containers(containers: list[Container]) -> list[Container]:
            seen: set[KubeName] = set()
            for container in containers:
                if container.name in seen:
                    msg = f"duplicate workload container name: '{container.name}'"
                    raise ValueError(msg)
                seen.add(container.name)
            return containers

        model_config = ConfigDict(extra="forbid")
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
                    "List of `.containerignore`-specific patterns, which are merged "
                    "with the global `ignore` patterns when generating "
                    "`.containerignore`."
                ),
            ),
        ]
        shell: Annotated[
            Shell,
            Field(
                default=DEFAULT_SHELL,
                examples=list(SHELLS),
                description=(
                    "Default shell to use when entering a container via "
                    "`bertrand enter`.  This is not a literal shell command, but "
                    "rather an identifier that maps to a backend command to prevent "
                    "remote code execution."
                ),
            ),
        ]
        editor: Annotated[
            Editor,
            Field(
                default=DEFAULT_EDITOR,
                examples=list(EDITORS),
                description=(
                    "Default text editor to use when invoking `bertrand code`.  This "
                    "is not a literal shell command, but rather an identifier that "
                    "maps to a backend command to prevent remote code execution."
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
        image: Annotated[
            Image,
            Field(default_factory=Image.model_construct),
        ]
        containers: Annotated[
            list[Container],
            AfterValidator(_check_containers),
            Field(default_factory=list),
        ]
        userns: Annotated[UserNS, Field(default="host")]
        ipc: Annotated[IPCMode, Field(default="private")]
        pid: Annotated[PIDMode, Field(default="private")]
        uts: Annotated[UTSMode, Field(default="private")]
        stop: Annotated[
            Stop,
            Field(default_factory=Stop.model_construct),
        ]
        restart: Annotated[Restart, Field(default_factory=Restart.model_construct)]

        @model_validator(mode="after")
        def _validate_network_routes(self) -> Self:
            ports: dict[ServicePortName, KubeName] = {}
            for container in self.containers:
                for port in container.ports:
                    owner = ports.get(port.name)
                    if owner is not None:
                        msg = (
                            f"duplicate workload port name {port.name!r} in "
                            f"containers {owner!r} and {container.name!r}"
                        )
                        raise ValueError(msg)
                    ports[port.name] = container.name
            for route in self.network.routes:
                if route.port not in ports:
                    msg = (
                        f"network route for host {route.host!r} references unknown "
                        f"port {route.port!r}"
                    )
                    raise ValueError(msg)
            return self

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
        """
        result = self.Model.model_validate(fragment)
        pyproject = config.get(PyProject)
        version = _project_version(config, pyproject)
        if version is not None:
            project_image_tag(version)
        result.image.resolve_containerfile(config.root)
        return result

    async def render(self, config: Config, *, image_build: bool) -> None:
        """Render Bertrand-managed project files."""
        del image_build
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
