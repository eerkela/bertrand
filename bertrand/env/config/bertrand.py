"""Define Bertrand's project configuration resource.

The metadata for this resource is expected to be found under the `[bertrand]` key in
project configuration, which is usually provided by `pyproject.toml`.  It is
responsible for rendering the basic directory structure and container/repository
artifacts needed by Bertrand's core functionality.
"""

from __future__ import annotations

import re
import tomllib
from dataclasses import dataclass
from pathlib import Path
from typing import Annotated, Any, Literal, Self

from pydantic import (
    AfterValidator,
    BaseModel,
    ConfigDict,
    Field,
    NonNegativeInt,
    StringConstraints,
    model_validator,
)

from bertrand.env.git import METADATA_DIR, Scalar, atomic_write_text, ensure_worktree_id
from bertrand.env.kube.build.request import worktree_identity
from bertrand.env.kube.ceph.volume import ensure_repository_worktree_record

from .core import (
    Config,
    Glob,
    KubeName,
    NoCRLF,
    NonEmpty,
    NoWhiteSpace,
    OCIImageRef,
    RelativePosixPath,
    Resource,
    SnakeCase,
    TOMLKey,
    Trimmed,
    resource,
)
from .python import PyProject, PyProjectModel

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
    # found when servicing dev-session requests.  The first entry that passes a
    # `which` check will be invoked together with the proper arguments to attach to
    # the requested container and mount its internal tools.
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


SERVICE_PORT_NAME_RE = re.compile(r"^[a-z0-9]([-a-z0-9]{0,13}[a-z0-9])?$")
CAPABILITY_TOKEN_RE = re.compile(r"^CAP_[A-Z0-9_]+$")
CAPABILITY_DEFINE_RE = re.compile(r"^\s*#define\s+(CAP_[A-Z0-9_]+)\s+([0-9]+)\b")
IMAGE_TAG_RE = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_.-]{0,127}$")
LINUX_CAPABILITY_HEADERS: tuple[Path, ...] = (
    Path("/usr/include/linux/capability.h"),
    Path("/usr/include/uapi/linux/capability.h"),
    Path("/usr/src/linux/include/uapi/linux/capability.h"),
)
type WorkloadKind = Literal["none", "job", "cronjob", "deployment"]


@dataclass(frozen=True)
class WorkloadTopology:
    """Inferred Kubernetes controller topology for one Bertrand workload.

    Parameters
    ----------
    kind : {"none", "job", "cronjob", "deployment"}
        Kubernetes workload controller family selected from semantic config.
    signals : tuple[str, ...], optional
        Config surfaces that caused this topology to be selected.
    """

    kind: WorkloadKind
    signals: tuple[str, ...] = ()

    @property
    def is_workload(self) -> bool:
        """Return whether this topology materializes a Kubernetes workload.

        Returns
        -------
        bool
            `True` for Job, CronJob, and Deployment topologies.
        """
        return self.kind != "none"


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
    value = capability.removeprefix("CAP_")
    token = f"CAP_{value}"
    if value == "ALL":
        return value
    if not CAPABILITY_TOKEN_RE.fullmatch(token):
        msg = (
            f"invalid capability token '{capability}' "
            "(expected Kubernetes capability name, CAP_* token, or ALL)"
        )
        raise ValueError(msg)
    if LINUX_CAPABILITIES is not None and token not in LINUX_CAPABILITIES:
        msg = (
            f"unknown Linux capability '{capability}' "
            "according to local capability header"
        )
        raise ValueError(msg)
    return value


def project_image_tag(project_version: str) -> str:
    """Derive the OCI tag for the configured project image.

    Parameters
    ----------
    project_version : str
        Version from the worktree's `[project].version` field.

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


def _project_version(config: Config, pyproject: PyProjectModel | None) -> str | None:
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
type ResourceQuantity = NonEmpty[NoWhiteSpace]
type PositiveCount = Annotated[int, Field(ge=1)]
type ProbePortNumber = Annotated[int, Field(ge=1, le=65535)]
type ProbePort = ProbePortNumber | ServicePortName
type ProbePath = Annotated[
    str,
    StringConstraints(strip_whitespace=True, min_length=1, pattern=r"^/.*$"),
]
type SeccompType = Literal["runtime-default", "unconfined", "localhost"]
type ExecutionRestart = Literal["never", "on-failure"]
type CompletionMode = Literal["all", "indexed"]
type ScheduleConcurrency = Literal["allow", "forbid", "replace"]
type RolloutStrategy = Literal["recreate", "rolling"]
type PercentOrCount = (
    Annotated[int, Field(ge=0)]
    | Annotated[
        str,
        StringConstraints(strip_whitespace=True, pattern=r"^\d+%$"),
    ]
)
type TolerationOperator = Literal["equal", "exists"]
type TolerationEffect = Literal["no-schedule", "prefer-no-schedule", "no-execute"]
type Capability = Annotated[NonEmpty[NoWhiteSpace], AfterValidator(_check_capability)]
type Timeout = Annotated[
    str,
    StringConstraints(
        strip_whitespace=True,
        min_length=1,
        pattern=r"^\d+(\.\d+)?[smhd]?$",
    ),
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


def _remove_legacy_publish_workflow(root: Path) -> None:
    workflow = root / ".github" / "workflows" / "publish.yml"
    try:
        text = workflow.read_text(encoding="utf-8")
    except OSError:
        return
    legacy_build = " ".join(("bertrand", "publish", "--repo"))
    legacy_manifest = " ".join(("bertrand", "publish", "--manifest"))
    if legacy_build in text and legacy_manifest in text:
        workflow.unlink()


class BertrandModel(BaseModel):
    """Validate the `[bertrand]` table."""

    class Network(BaseModel):
        """Validate the `[bertrand.network]` table."""

        class Route(BaseModel):
            """Validate an HTTPRoute intent."""

            model_config = ConfigDict(extra="forbid")
            host: Annotated[
                HostName,
                Field(
                    examples=["app.local"],
                    description=(
                        "External hostname that the rendered HTTPRoute should "
                        "match for this workload."
                    ),
                ),
            ]
            port: Annotated[
                ServicePortName,
                Field(
                    examples=["http"],
                    description=(
                        "Named container port that the rendered HTTPRoute targets "
                        "through the workload's canonical Service.  The target "
                        "port must use TCP."
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
                        "HTTP path prefix matched by the rendered HTTPRoute."
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
                    "restrictive NetworkPolicy, while `isolated` renders an "
                    "ingress-only Kubernetes NetworkPolicy for Deployment "
                    "workloads, allowing only explicitly routed HTTP backend "
                    "ports and leaving egress open."
                ),
            ),
        ]
        routes: Annotated[
            list[Route],
            Field(
                default_factory=list,
                examples=[[{"host": "app.local", "port": "http", "path": "/"}]],
                description=(
                    "HTTPRoute intents for publishing this workload outside the "
                    "cluster through Bertrand's managed Envoy Gateway substrate."
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
            list[BertrandModel.Secret],
            AfterValidator(
                lambda x: BertrandModel._check_unique(x, where="image secret")
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
            list[BertrandModel.SSH],
            AfterValidator(
                lambda x: BertrandModel._check_unique(x, where="image ssh")
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
            list[BertrandModel.Device],
            AfterValidator(
                lambda x: BertrandModel._check_unique(x, where="image device")
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
                    "Stable Kubernetes Service port name.  Service and future "
                    "HTTPRoute rendering use this name as the durable target for "
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

    class Resources(BaseModel):
        """Validate one container's Kubernetes resource requirements."""

        model_config = ConfigDict(extra="forbid")
        cpu: Annotated[
            ResourceQuantity | None,
            Field(
                default=None,
                examples=["500m", "1"],
                description=(
                    "Baseline CPU reserved for this container.  Bertrand renders "
                    "this as the Kubernetes `resources.requests.cpu` scheduler "
                    "request."
                ),
            ),
        ]
        memory: Annotated[
            ResourceQuantity | None,
            Field(
                default=None,
                examples=["512Mi", "2Gi"],
                description=(
                    "Baseline memory reserved for this container.  Bertrand "
                    "renders this as the Kubernetes "
                    "`resources.requests.memory` scheduler request."
                ),
            ),
        ]
        max_cpu: Annotated[
            ResourceQuantity | None,
            Field(
                default=None,
                alias="max-cpu",
                examples=["2"],
                description=(
                    "Maximum CPU this container may consume.  Bertrand renders "
                    "this as the Kubernetes `resources.limits.cpu` runtime cap."
                ),
            ),
        ]
        max_memory: Annotated[
            ResourceQuantity | None,
            Field(
                default=None,
                alias="max-memory",
                examples=["2Gi"],
                description=(
                    "Maximum memory this container may consume before Kubernetes "
                    "may terminate it.  Bertrand renders this as "
                    "`resources.limits.memory`."
                ),
            ),
        ]

        @property
        def requests(self) -> dict[str, str]:
            """Return Kubernetes resource requests for this container.

            Returns
            -------
            dict[str, str]
                Non-empty resource requests keyed by Kubernetes resource name.
            """
            out: dict[str, str] = {}
            if self.cpu is not None:
                out["cpu"] = self.cpu
            if self.memory is not None:
                out["memory"] = self.memory
            return out

        @property
        def limits(self) -> dict[str, str]:
            """Return Kubernetes resource limits for this container.

            Returns
            -------
            dict[str, str]
                Non-empty resource limits keyed by Kubernetes resource name.
            """
            out: dict[str, str] = {}
            if self.max_cpu is not None:
                out["cpu"] = self.max_cpu
            if self.max_memory is not None:
                out["memory"] = self.max_memory
            return out

        @model_validator(mode="after")
        def _validate_non_empty(self) -> Self:
            if not self.requests and not self.limits:
                msg = (
                    "container resources must define cpu, memory, max-cpu, "
                    "or max-memory"
                )
                raise ValueError(msg)
            return self

    class Probe(BaseModel):
        """Validate a Kubernetes startup, readiness, or liveness probe."""

        class HTTP(BaseModel):
            """Validate an HTTP probe source."""

            model_config = ConfigDict(extra="forbid")
            path: Annotated[
                ProbePath,
                Field(
                    default="/",
                    examples=["/healthz", "/ready"],
                    description=(
                        "HTTP path to request when probing container health.  "
                        "Bertrand renders this as a Kubernetes HTTP GET probe."
                    ),
                ),
            ]
            port: Annotated[
                ProbePort,
                Field(
                    examples=["http", 8080],
                    description=(
                        "Named container port or numeric port to probe with the "
                        "HTTP request."
                    ),
                ),
            ]

        model_config = ConfigDict(extra="forbid")
        cmd: Annotated[
            list[NonEmpty[Trimmed]],
            Field(
                default_factory=list,
                examples=[["python", "-m", "bertrand.health"]],
                description=(
                    "Command to run inside the container for a health check.  "
                    "Use exactly one of `cmd`, `http`, or `tcp`."
                ),
            ),
        ]
        http: Annotated[
            BertrandModel.Probe.HTTP | None,
            Field(
                default=None,
                examples=[{"path": "/healthz", "port": "http"}],
                description=(
                    "HTTP probe source.  Use this when the container exposes a "
                    "health endpoint on one of its declared ports."
                ),
            ),
        ]
        tcp: Annotated[
            ProbePort | None,
            Field(
                default=None,
                examples=["http", 5432],
                description=(
                    "TCP socket probe port.  Kubernetes treats a successful "
                    "connection as a healthy check result."
                ),
            ),
        ]
        delay: Annotated[
            NonNegativeInt | None,
            Field(
                default=None,
                examples=[10],
                description=(
                    "Seconds to wait before the first probe.  Bertrand renders "
                    "this as Kubernetes `initialDelaySeconds`."
                ),
            ),
        ]
        period: Annotated[
            PositiveCount | None,
            Field(
                default=None,
                examples=[5],
                description=(
                    "Seconds between probe attempts, rendered as Kubernetes "
                    "`periodSeconds`."
                ),
            ),
        ]
        timeout: Annotated[
            PositiveCount | None,
            Field(
                default=None,
                examples=[2],
                description=(
                    "Seconds before one probe attempt times out, rendered as "
                    "Kubernetes `timeoutSeconds`."
                ),
            ),
        ]
        success: Annotated[
            PositiveCount | None,
            Field(
                default=None,
                examples=[1],
                description=(
                    "Consecutive successes required after a failure, rendered as "
                    "Kubernetes `successThreshold`."
                ),
            ),
        ]
        failure: Annotated[
            PositiveCount | None,
            Field(
                default=None,
                examples=[3],
                description=(
                    "Consecutive failures required before Kubernetes marks the "
                    "probe failed."
                ),
            ),
        ]

        @model_validator(mode="after")
        def _validate_source(self) -> Self:
            sources = sum(
                (bool(self.cmd), self.http is not None, self.tcp is not None)
            )
            if sources != 1:
                msg = "probe must define exactly one of cmd, http, or tcp"
                raise ValueError(msg)
            return self

    class Security(BaseModel):
        """Validate one container's Kubernetes security context."""

        class Capabilities(BaseModel):
            """Validate Linux capabilities for a Kubernetes security context."""

            model_config = ConfigDict(extra="forbid")
            add: Annotated[
                list[Capability],
                AfterValidator(
                    lambda x: BertrandModel._check_unique(
                        x, where="capability add entry"
                    )
                ),
                Field(
                    default_factory=list,
                    examples=[["NET_ADMIN"]],
                    description=(
                        "Linux capabilities to add to the container security "
                        "context.  Values may be written with or without the "
                        "`CAP_` prefix."
                    ),
                ),
            ]
            drop: Annotated[
                list[Capability],
                AfterValidator(
                    lambda x: BertrandModel._check_unique(
                        x, where="capability drop entry"
                    )
                ),
                Field(
                    default_factory=list,
                    examples=[["ALL"]],
                    description=(
                        "Linux capabilities to drop from the container security "
                        "context.  Use `ALL` for a minimal capability baseline."
                    ),
                ),
            ]

            @model_validator(mode="after")
            def _validate_conflicts(self) -> Self:
                if "ALL" in self.add and len(self.add) > 1:
                    msg = (
                        "security.capabilities.add cannot combine ALL with entries"
                    )
                    raise ValueError(msg)
                if "ALL" in self.drop and len(self.drop) > 1:
                    msg = (
                        "security.capabilities.drop cannot combine ALL with entries"
                    )
                    raise ValueError(msg)
                overlap = {cap for cap in self.add if cap != "ALL"}.intersection(
                    cap for cap in self.drop if cap != "ALL"
                )
                if overlap:
                    msg = (
                        "security.capabilities.add and drop cannot contain the "
                        f"same capability: {', '.join(sorted(overlap))}"
                    )
                    raise ValueError(msg)
                return self

        class Seccomp(BaseModel):
            """Validate a Kubernetes seccomp profile selector."""

            model_config = ConfigDict(extra="forbid")
            type: Annotated[
                SeccompType,
                Field(
                    default="runtime-default",
                    examples=["runtime-default", "localhost"],
                    description=(
                        "Seccomp profile source.  Bertrand renders this as "
                        "Kubernetes `RuntimeDefault`, `Unconfined`, or "
                        "`Localhost`."
                    ),
                ),
            ]
            profile: Annotated[
                NonEmpty[NoWhiteSpace] | None,
                Field(
                    default=None,
                    examples=["profiles/web.json"],
                    description=(
                        "Node-local seccomp profile path.  Required only when "
                        "`type = \"localhost\"`."
                    ),
                ),
            ]

            @model_validator(mode="after")
            def _validate_profile(self) -> Self:
                if self.type == "localhost":
                    if self.profile is None:
                        msg = "localhost seccomp requires a profile"
                        raise ValueError(msg)
                elif self.profile is not None:
                    msg = "seccomp profile can only be set for type='localhost'"
                    raise ValueError(msg)
                return self

        model_config = ConfigDict(extra="forbid")
        privileged: Annotated[
            bool | None,
            Field(
                default=None,
                examples=[False],
                description=(
                    "Run the container in privileged mode.  This maps directly "
                    "to the Kubernetes container security context."
                ),
            ),
        ]
        allow_privilege_escalation: Annotated[
            bool | None,
            Field(
                default=None,
                alias="allow-privilege-escalation",
                examples=[False],
                description=(
                    "Allow a process to gain more privileges than its parent.  "
                    "Bertrand renders this as `allowPrivilegeEscalation`."
                ),
            ),
        ]
        read_only_root_filesystem: Annotated[
            bool | None,
            Field(
                default=None,
                alias="read-only-root-filesystem",
                examples=[True],
                description=(
                    "Mount the container root filesystem read-only when possible."
                ),
            ),
        ]
        run_as_user: Annotated[
            NonNegativeInt | None,
            Field(
                default=None,
                alias="run-as-user",
                examples=[1000],
                description=(
                    "Linux UID to run as inside the container, rendered as "
                    "Kubernetes `runAsUser`."
                ),
            ),
        ]
        run_as_group: Annotated[
            NonNegativeInt | None,
            Field(
                default=None,
                alias="run-as-group",
                examples=[1000],
                description=(
                    "Linux primary GID to run as inside the container, rendered "
                    "as Kubernetes `runAsGroup`."
                ),
            ),
        ]
        run_as_non_root: Annotated[
            bool | None,
            Field(
                default=None,
                alias="run-as-non-root",
                examples=[True],
                description=(
                    "Require the container to run as a non-root user.  "
                    "Kubernetes rejects startup if the effective user is root."
                ),
            ),
        ]
        capabilities: Annotated[
            BertrandModel.Security.Capabilities,
            Field(
                default_factory=lambda: (
                    BertrandModel.Security.Capabilities.model_construct()
                ),
                description=(
                    "Linux capability adjustments for the container security "
                    "context."
                ),
            ),
        ]
        seccomp: Annotated[
            BertrandModel.Security.Seccomp | None,
            Field(
                default=None,
                examples=[{"type": "runtime-default"}],
                description=(
                    "Seccomp profile selection for the container security context."
                ),
            ),
        ]

    class Container(BaseModel):
        """Validate one native workload container entry."""

        model_config = ConfigDict(extra="forbid")
        name: Annotated[
            KubeName,
            Field(
                examples=["main", "worker"],
                description=(
                    "Stable container name inside the workload pod.  Container "
                    "names must be unique within `[tool.bertrand.containers]`."
                ),
            ),
        ]
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
        resources: Annotated[
            BertrandModel.Resources | None,
            Field(
                default=None,
                examples=[
                    {
                        "cpu": "500m",
                        "memory": "512Mi",
                        "max-cpu": "2",
                        "max-memory": "2Gi",
                    }
                ],
                description=(
                    "CPU and memory intent for this container.  `cpu` and "
                    "`memory` reserve baseline Kubernetes requests; `max-cpu` "
                    "and `max-memory` set Kubernetes limits."
                ),
            ),
        ]
        startup: Annotated[
            BertrandModel.Probe | None,
            Field(
                default=None,
                examples=[{"http": {"path": "/healthz", "port": "http"}}],
                description=(
                    "Probe that gates slow-starting containers.  Kubernetes "
                    "disables readiness and liveness checks until startup passes."
                ),
            ),
        ]
        readiness: Annotated[
            BertrandModel.Probe | None,
            Field(
                default=None,
                examples=[{"http": {"path": "/ready", "port": "http"}}],
                description=(
                    "Probe that controls whether the pod should receive Service "
                    "traffic."
                ),
            ),
        ]
        liveness: Annotated[
            BertrandModel.Probe | None,
            Field(
                default=None,
                examples=[{"tcp": "http", "failure": 3}],
                description=(
                    "Probe that lets Kubernetes restart a container that appears "
                    "stuck or unhealthy."
                ),
            ),
        ]
        security: Annotated[
            BertrandModel.Security | None,
            Field(
                default=None,
                examples=[
                    {
                        "run-as-non-root": True,
                        "read-only-root-filesystem": True,
                        "capabilities": {"drop": ["ALL"]},
                    }
                ],
                description=(
                    "Container-level security controls rendered as a Kubernetes "
                    "security context."
                ),
            ),
        ]
        ports: Annotated[
            list[BertrandModel.Port],
            AfterValidator(lambda x: BertrandModel._check_ports(x)),
            Field(
                default_factory=list,
                examples=[[{"name": "http", "port": 8080, "protocol": "tcp"}]],
                description=(
                    "Named listeners exposed by this container.  Any declared "
                    "port selects Deployment topology and participates in the "
                    "canonical internal Service."
                ),
            ),
        ]
        ssh: Annotated[
            list[BertrandModel.SSH],
            AfterValidator(
                lambda x: BertrandModel._check_unique(x, where="container ssh")
            ),
            Field(
                default_factory=list,
                examples=[[{"id": "git_deploy_key", "required": True}]],
                description=(
                    "Runtime SSH credential capabilities mounted into this "
                    "container."
                ),
            ),
        ]
        devices: Annotated[
            list[BertrandModel.Device],
            AfterValidator(
                lambda x: BertrandModel._check_unique(x, where="container device")
            ),
            Field(
                default_factory=list,
                examples=[[{"id": "gpu", "required": False}]],
                description=(
                    "Runtime DRA device capabilities requested for this container."
                ),
            ),
        ]
        secrets: Annotated[
            list[BertrandModel.Secret],
            AfterValidator(
                lambda x: BertrandModel._check_unique(x, where="container secret")
            ),
            Field(
                default_factory=list,
                examples=[[{"id": "pypi_token", "required": True}]],
                description=(
                    "Runtime secret capabilities mounted into this container."
                ),
            ),
        ]
        metadata: Annotated[
            dict[NonEmpty[SnakeCase], Scalar],
            Field(
                default_factory=dict,
                examples=[{"role": "api"}],
                description=(
                    "Bertrand-local metadata for future workload integrations.  "
                    "This is not rendered into Kubernetes labels yet."
                ),
            ),
        ]

    class Execution(BaseModel):
        """Validate Job and CronJob run-to-completion behavior."""

        model_config = ConfigDict(extra="forbid")
        restart: Annotated[
            ExecutionRestart,
            Field(
                default="never",
                examples=["never", "on-failure"],
                description=(
                    "Pod restart behavior for Job and CronJob pods.  Bertrand "
                    "renders this as Kubernetes `Never` or `OnFailure`."
                ),
            ),
        ]
        retries: Annotated[
            NonNegativeInt,
            Field(
                default=0,
                examples=[3],
                description=(
                    "Number of retry attempts before a Job is treated as failed, "
                    "rendered as Kubernetes `backoffLimit`."
                ),
            ),
        ]
        timeout: Annotated[
            NonNegativeInt | None,
            Field(
                default=None,
                examples=[3600],
                description=(
                    "Maximum runtime in seconds before the Job is stopped, "
                    "rendered as Kubernetes `activeDeadlineSeconds`."
                ),
            ),
        ]
        ttl: Annotated[
            NonNegativeInt | None,
            Field(
                default=None,
                examples=[86400],
                description=(
                    "Seconds to retain finished Job objects, rendered as "
                    "Kubernetes `ttlSecondsAfterFinished`."
                ),
            ),
        ]
        parallelism: Annotated[
            int,
            Field(
                default=1,
                ge=1,
                examples=[4],
                description=(
                    "Maximum Job pods allowed to run at once, rendered as "
                    "Kubernetes `parallelism`."
                ),
            ),
        ]
        completions: Annotated[
            int | None,
            Field(
                default=None,
                ge=1,
                examples=[10],
                description=(
                    "Successful pod completions required for the Job, rendered "
                    "as Kubernetes `completions`."
                ),
            ),
        ]
        completion: Annotated[
            CompletionMode,
            Field(
                default="all",
                examples=["all", "indexed"],
                description=(
                    "Job completion tracking mode.  `all` renders Kubernetes "
                    "`NonIndexed`; `indexed` renders `Indexed` and requires "
                    "`completions`."
                ),
            ),
        ]

        @model_validator(mode="after")
        def _validate_indexed_completion(self) -> Self:
            if self.completion == "indexed" and self.completions is None:
                msg = "indexed completion requires completions"
                raise ValueError(msg)
            return self

    class Schedule(BaseModel):
        """Validate CronJob scheduling behavior."""

        class History(BaseModel):
            """Validate CronJob history retention limits."""

            model_config = ConfigDict(extra="forbid")
            success: Annotated[
                NonNegativeInt | None,
                Field(
                    default=None,
                    examples=[3],
                    description=(
                        "Successful CronJob runs to retain, rendered as "
                        "Kubernetes `successfulJobsHistoryLimit`."
                    ),
                ),
            ]
            failure: Annotated[
                NonNegativeInt | None,
                Field(
                    default=None,
                    examples=[1],
                    description=(
                        "Failed CronJob runs to retain, rendered as Kubernetes "
                        "`failedJobsHistoryLimit`."
                    ),
                ),
            ]

        model_config = ConfigDict(extra="forbid")
        cron: Annotated[
            NonEmpty[NoCRLF],
            Field(
                examples=["0 9 * * 1-5"],
                description=(
                    "Cron expression for recurring execution.  Presence of this "
                    "table selects CronJob topology and renders Kubernetes "
                    "`schedule`."
                ),
            ),
        ]
        timezone: Annotated[
            NonEmpty[NoWhiteSpace] | None,
            Field(
                default=None,
                examples=["America/Los_Angeles"],
                description=(
                    "IANA time zone for interpreting the cron expression, "
                    "rendered as Kubernetes `timeZone`."
                ),
            ),
        ]
        concurrency: Annotated[
            ScheduleConcurrency,
            Field(
                default="forbid",
                examples=["forbid", "allow", "replace"],
                description=(
                    "How overlapping scheduled runs are handled, rendered as "
                    "Kubernetes `concurrencyPolicy`."
                ),
            ),
        ]
        start_deadline: Annotated[
            NonNegativeInt | None,
            Field(
                default=None,
                alias="start-deadline",
                examples=[300],
                description=(
                    "Seconds Kubernetes may still start a missed schedule after "
                    "its nominal time, rendered as `startingDeadlineSeconds`."
                ),
            ),
        ]
        suspend: Annotated[
            bool | None,
            Field(
                default=None,
                examples=[False],
                description=(
                    "Pause future scheduled runs without deleting the CronJob, "
                    "rendered as Kubernetes `suspend`."
                ),
            ),
        ]
        history: Annotated[
            BertrandModel.Schedule.History,
            Field(
                default_factory=lambda: (
                    BertrandModel.Schedule.History.model_construct()
                ),
                description="CronJob run history retention limits.",
            ),
        ]

    class Scale(BaseModel):
        """Validate Deployment scale behavior."""

        model_config = ConfigDict(extra="forbid")
        replicas: Annotated[
            NonNegativeInt,
            Field(
                default=1,
                examples=[2],
                description=(
                    "Desired number of long-lived pod replicas.  Presence of "
                    "`[tool.bertrand.scale]` selects Deployment topology."
                ),
            ),
        ]

    class Rollout(BaseModel):
        """Validate Deployment rollout behavior."""

        model_config = ConfigDict(extra="forbid")
        strategy: Annotated[
            RolloutStrategy,
            Field(
                default="rolling",
                examples=["rolling", "recreate"],
                description=(
                    "Deployment update strategy.  `rolling` maps to Kubernetes "
                    "`RollingUpdate`; `recreate` maps to `Recreate`."
                ),
            ),
        ]
        max_surge: Annotated[
            PercentOrCount | None,
            Field(
                default=None,
                alias="max-surge",
                examples=["25%", 1],
                description=(
                    "Extra pods allowed during a rolling update, rendered as "
                    "Kubernetes `maxSurge`."
                ),
            ),
        ]
        max_unavailable: Annotated[
            PercentOrCount | None,
            Field(
                default=None,
                alias="max-unavailable",
                examples=["25%", 0],
                description=(
                    "Pods allowed to be unavailable during a rolling update, "
                    "rendered as Kubernetes `maxUnavailable`."
                ),
            ),
        ]
        min_ready: Annotated[
            NonNegativeInt | None,
            Field(
                default=None,
                alias="min-ready",
                examples=[10],
                description=(
                    "Seconds a pod must be ready before it counts as available, "
                    "rendered as Kubernetes `minReadySeconds`."
                ),
            ),
        ]
        timeout: Annotated[
            NonNegativeInt | None,
            Field(
                default=None,
                examples=[600],
                description=(
                    "Seconds before a stalled rollout is reported as failed, "
                    "rendered as Kubernetes `progressDeadlineSeconds`."
                ),
            ),
        ]
        history: Annotated[
            NonNegativeInt | None,
            Field(
                default=None,
                examples=[10],
                description=(
                    "Old ReplicaSet revisions to retain, rendered as Kubernetes "
                    "`revisionHistoryLimit`."
                ),
            ),
        ]
        paused: Annotated[
            bool | None,
            Field(
                default=None,
                examples=[False],
                description=(
                    "Pause rollout progress without deleting the Deployment, "
                    "rendered as Kubernetes `paused`."
                ),
            ),
        ]

        @model_validator(mode="after")
        def _validate_strategy(self) -> Self:
            if self.strategy == "recreate" and (
                self.max_surge is not None or self.max_unavailable is not None
            ):
                msg = "recreate rollout cannot set max-surge or max-unavailable"
                raise ValueError(msg)
            return self

    class Toleration(BaseModel):
        """Validate one Kubernetes pod toleration."""

        model_config = ConfigDict(extra="forbid")
        key: Annotated[
            NonEmpty[NoWhiteSpace] | None,
            Field(
                default=None,
                examples=["workload"],
                description=(
                    "Taint key this pod can tolerate.  Omit with "
                    "`operator = \"exists\"` to tolerate any matching effect."
                ),
            ),
        ]
        operator: Annotated[
            TolerationOperator,
            Field(
                default="equal",
                examples=["equal", "exists"],
                description=(
                    "Toleration matching mode, rendered as Kubernetes `Equal` "
                    "or `Exists`."
                ),
            ),
        ]
        value: Annotated[
            NonEmpty[NoWhiteSpace] | None,
            Field(
                default=None,
                examples=["gpu"],
                description=("Taint value to match when `operator = \"equal\"`."),
            ),
        ]
        effect: Annotated[
            TolerationEffect | None,
            Field(
                default=None,
                examples=["no-schedule"],
                description=(
                    "Taint effect this toleration matches, rendered as "
                    "Kubernetes `NoSchedule`, `PreferNoSchedule`, or `NoExecute`."
                ),
            ),
        ]
        seconds: Annotated[
            NonNegativeInt | None,
            Field(
                default=None,
                examples=[3600],
                description=(
                    "Seconds a pod may remain bound after a matching `NoExecute` "
                    "taint is added."
                ),
            ),
        ]

        @model_validator(mode="after")
        def _validate_operator(self) -> Self:
            if self.operator == "exists" and self.value is not None:
                msg = "toleration value cannot be set when operator='exists'"
                raise ValueError(msg)
            return self

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
    def _check_ports(ports: list[Port]) -> list[Port]:
        seen: set[ServicePortName] = set()
        for port in ports:
            if port.name in seen:
                msg = f"duplicate workload port name: '{port.name}'"
                raise ValueError(msg)
            seen.add(port.name)
        return ports

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
                "interpreted using the same rules as those files.  Bertrand "
                "always adds `.bertrand/` before these patterns to protect "
                "local metadata."
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
            description=(
                "Workload-level networking intent.  Declared container ports "
                "drive internal Service rendering, isolation policy, and "
                "Gateway API HTTPRoute publication."
            ),
        ),
    ]
    image: Annotated[
        Image,
        Field(
            default_factory=Image.model_construct,
            description=(
                "Image build intent for this worktree's single workload image."
            ),
        ),
    ]
    containers: Annotated[
        list[Container],
        AfterValidator(_check_containers),
        Field(
            default_factory=list,
            examples=[
                [
                    {
                        "name": "main",
                        "cmd": ["python", "-m", "app"],
                        "ports": [{"name": "http", "port": 8080}],
                    }
                ]
            ],
            description=(
                "Container processes in this workload pod.  No containers means "
                "no Kubernetes workload; otherwise topology is inferred from "
                "schedule, deployment signals, and run-to-completion settings."
            ),
        ),
    ]
    execution: Annotated[
        Execution | None,
        Field(
            default=None,
            examples=[{"restart": "never", "retries": 3, "timeout": 3600}],
            description=(
                "Run-to-completion behavior for Job and CronJob workloads.  This "
                "conflicts with Deployment topology."
            ),
        ),
    ]
    schedule: Annotated[
        Schedule | None,
        Field(
            default=None,
            examples=[{"cron": "0 9 * * 1-5", "timezone": "UTC"}],
            description=(
                "Cron scheduling behavior.  Presence selects CronJob topology "
                "unless conflicting Deployment signals are also present."
            ),
        ),
    ]
    scale: Annotated[
        Scale | None,
        Field(
            default=None,
            examples=[{"replicas": 2}],
            description=(
                "Long-lived replica behavior.  Presence selects Deployment "
                "topology."
            ),
        ),
    ]
    rollout: Annotated[
        Rollout | None,
        Field(
            default=None,
            examples=[{"strategy": "rolling", "max-surge": "25%"}],
            description=(
                "Deployment rollout behavior for long-lived workloads.  Presence "
                "selects Deployment topology."
            ),
        ),
    ]
    termination_grace: Annotated[
        NonNegativeInt | None,
        Field(
            default=None,
            alias="termination-grace",
            examples=[30],
            description=(
                "Seconds Kubernetes gives the workload pod to shut down cleanly, "
                "rendered as `terminationGracePeriodSeconds`."
            ),
        ),
    ]
    service_account: Annotated[
        KubeName | None,
        Field(
            default=None,
            alias="service-account",
            examples=["bertrand-runtime"],
            description=(
                "Kubernetes ServiceAccount name used by the workload pod."
            ),
        ),
    ]
    node: Annotated[
        KubeName | None,
        Field(
            default=None,
            examples=["worker-a"],
            description=(
                "Pin the workload pod to one Kubernetes node by `nodeName`.  "
                "Prefer selectors or DRA requests unless exact placement is "
                "required."
            ),
        ),
    ]
    node_selector: Annotated[
        dict[NonEmpty[NoWhiteSpace], NonEmpty[NoWhiteSpace]],
        Field(
            default_factory=dict,
            alias="node-selector",
            examples=[{"kubernetes.io/os": "linux"}],
            description=(
                "Node label constraints for scheduling the workload pod, "
                "rendered as Kubernetes `nodeSelector`."
            ),
        ),
    ]
    priority_class: Annotated[
        KubeName | None,
        Field(
            default=None,
            alias="priority-class",
            examples=["batch-low"],
            description=(
                "Kubernetes PriorityClass name used for workload pod scheduling "
                "and preemption behavior."
            ),
        ),
    ]
    tolerations: Annotated[
        list[Toleration],
        Field(
            default_factory=list,
            examples=[
                [
                    {
                        "key": "workload",
                        "operator": "equal",
                        "value": "gpu",
                        "effect": "no-schedule",
                    }
                ]
            ],
            description=(
                "Kubernetes pod tolerations used with node taints and placement "
                "policy."
            ),
        ),
    ]

    @property
    def topology(self) -> WorkloadTopology:
        """Return the inferred workload topology.

        Returns
        -------
        WorkloadTopology
            Controller family selected from this config's semantic topology.
        """
        if not self.containers:
            return WorkloadTopology(kind="none")
        if self.schedule is not None:
            return WorkloadTopology(kind="cronjob", signals=("schedule",))
        signals = _deployment_topology_signals(self)
        if signals:
            return WorkloadTopology(kind="deployment", signals=signals)
        return WorkloadTopology(kind="job")

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
        seen_routes: set[tuple[HostName, str, ServicePortName]] = set()
        for route in self.network.routes:
            if route.port not in ports:
                msg = (
                    f"network route for host {route.host!r} references unknown "
                    f"port {route.port!r}"
                )
                raise ValueError(msg)
            key = (route.host, route.path, route.port)
            if key in seen_routes:
                msg = (
                    f"duplicate network route for host {route.host!r}, path "
                    f"{route.path!r}, and port {route.port!r}"
                )
                raise ValueError(msg)
            seen_routes.add(key)
        return self

    @model_validator(mode="after")
    def _validate_workload_topology(self) -> Self:
        signals = _deployment_topology_signals(self)
        topology_fields = (
            self.execution is not None,
            self.schedule is not None,
            self.scale is not None,
            self.rollout is not None,
            bool(signals),
        )
        if not self.containers:
            if any(topology_fields):
                msg = "workload topology fields require at least one container"
                raise ValueError(msg)
            return self
        if self.schedule is not None and signals:
            msg = (
                f"schedule conflicts with Deployment topology: {', '.join(signals)}"
            )
            raise ValueError(msg)
        if self.execution is not None and signals:
            msg = (
                "execution config applies only to Job/CronJob workloads and "
                f"conflicts with Deployment topology: {', '.join(signals)}"
            )
            raise ValueError(msg)
        return self


@resource("bertrand")
class Bertrand(Resource[BertrandModel]):
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

    async def init(
        self,
        config: Config,  # noqa: ARG002
        cli: Config.Init,  # noqa: ARG002
    ) -> dict[str, Any]:
        """Return the default Bertrand configuration fragment.

        Returns
        -------
        dict[str, Any]
            Default configuration data serialized with TOML aliases.
        """
        return BertrandModel.model_construct().model_dump(
            by_alias=True,
            exclude_none=True,
        )

    async def validate(
        self,
        config: Config,
        fragment: Any,
    ) -> BertrandModel | None:
        """Validate a Bertrand configuration fragment.

        Returns
        -------
        BertrandModel | None
            Parsed Bertrand configuration.
        """
        result = BertrandModel.model_validate(fragment)
        pyproject = config.get(PyProject)
        version = _project_version(config, pyproject)
        if version is not None:
            project_image_tag(version)
        result.image.resolve_containerfile(config.root)
        return result

    async def render(self, config: Config, *, image_build: bool) -> None:
        """Render Bertrand-managed project files."""
        if image_build:
            return
        bertrand = config.get(Bertrand)
        if bertrand is None:
            return

        # render worktree directories
        (config.root / "src").mkdir(parents=True, exist_ok=True)
        (config.root / "tests").mkdir(parents=True, exist_ok=True)
        (config.root / "docs").mkdir(parents=True, exist_ok=True)
        worktree_id = ensure_worktree_id(config.root)
        if config.kube is not None:
            await ensure_repository_worktree_record(
                config.kube,
                repo_id=config.repo.id,
                worktree_id=worktree_id,
                worktree=worktree_identity(config.worktree),
                timeout=config.timeout,
            )

        # render ignore files
        # Always ignore Bertrand's metadata directory.
        ignore = [f"{METADATA_DIR.as_posix()}/"]
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
        _remove_legacy_publish_workflow(config.root)


def workload_topology(config: BertrandModel | None) -> WorkloadTopology:
    """Return the inferred Kubernetes workload topology for a config.

    Parameters
    ----------
    config : BertrandModel | None
        Parsed Bertrand configuration, or `None` when the worktree does not define
        Bertrand workload configuration.

    Returns
    -------
    WorkloadTopology
        Inferred controller family.  Missing config selects `"none"`.
    """
    if config is None:
        return WorkloadTopology(kind="none")
    return config.topology


def _deployment_topology_signals(config: BertrandModel) -> tuple[str, ...]:
    signals: list[str] = []
    if config.scale is not None:
        signals.append("scale")
    if config.rollout is not None:
        signals.append("rollout")
    if any(container.ports for container in config.containers):
        signals.append("ports")
    if config.network.routes:
        signals.append("network.routes")
    return tuple(signals)
