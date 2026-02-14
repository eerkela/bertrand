"""Text editor integrations for Bertrand's containerized development environments.

This module currently provides a minimal host-side RPC listener skeleton that can be
used by future in-container `bertrand code` clients.
"""
from __future__ import annotations

import json
import os
import shlex
import shutil
import socket
import stat
import subprocess
import sys
import time
import urllib.parse

from dataclasses import dataclass, field
from pathlib import Path, PosixPath
from typing import Any, Annotated, Literal, Self, TypeAlias

from pydantic import AfterValidator, BaseModel, ConfigDict, Field, ValidationError, model_validator

from .config import (
    AGENTS,
    ASSISTS,
    CONTAINER_BIN_ENV,
    CONTAINER_ID_ENV,
    EDITOR_BIN_ENV,
    EDITORS,
    HOST_ENV,
    MOUNT,
    Config
)
from .mcp import sync_vscode_mcp_config
from .pipeline import (
    Pipeline,
    ReloadDaemon,
    StartService,
    WriteText,
)
from .run import (
    CommandError,
    TimeoutExpired,
    User,
    atomic_write_text,
    mkdir_private,
    run
)

# pylint: disable=broad-exception-caught


# code RPC protocol and systemd details
CODE_LAUNCH_TIMEOUT: float = 10.0
CODE_PROBE_TIMEOUT: float = 10.0
CODE_RPC_CLIENT_HEADROOM: float = 2.0
CODE_RPC_CLIENT_TIMEOUT: float = CODE_LAUNCH_TIMEOUT + CODE_RPC_CLIENT_HEADROOM
CODE_RPC_READ_TIMEOUT: float = CODE_RPC_CLIENT_TIMEOUT
CODE_SOCKET_VERSION: int = 1
CODE_SOCKET_OP_OPEN: str = "open_editor"
CODE_SOCKET: Path = User().home / ".local" / "share" / "bertrand" / "code-rpc" / "listener.sock"
MAX_REQUEST_BYTES: int = 1024 * 1024  # 1 MiB
CONTAINER_SOCKET: Path = Path("/run/bertrand/code-rpc") / "listener.sock"
CODE_SERVICE_NAME = "bertrand-code.service"
CODE_SERVICE_FILE = User().home / ".config" / "systemd" / "user" / CODE_SERVICE_NAME
CODE_SERVICE_ENV: str = "BERTRAND_CODE_SERVICE"


# vscode integration details
VSCODE_MANAGED_WORKSPACE_FILE: Path = Path(".vscode") / "bertrand.code-workspace"
VSCODE_REMOTE_EXTENSION = "ms-vscode-remote.remote-containers"
VSCODE_CLANGD_EXTENSION = "llvm-vs-code-extensions.vscode-clangd"
VSCODE_PYTHON_EXTENSION = "ms-python.python"
VSCODE_RUFF_EXTENSION = "charliermarsh.ruff"
VSCODE_TY_EXTENSION = "astral-sh.ty"
VSCODE_EXECUTABLE_CANDIDATES: tuple[str, ...] = (
    "code",
    "com.visualstudio.code",
    "code-insiders",
    "com.visualstudio.code-insiders",
)
VSCODE_BASE_RECOMMENDED_EXTENSIONS: list[str] = [
    VSCODE_REMOTE_EXTENSION,
    VSCODE_CLANGD_EXTENSION,
    VSCODE_PYTHON_EXTENSION,
    VSCODE_RUFF_EXTENSION,
    VSCODE_TY_EXTENSION,
]
VSCODE_MANAGED_SETTINGS: dict[str, Any] = {
    "C_Cpp.intelliSenseEngine": "disabled",
    "clangd.path": "clangd",
    "[python]": {
        "editor.defaultFormatter": VSCODE_RUFF_EXTENSION,
        "editor.formatOnSave": True,
        "editor.codeActionsOnSave": {
            "source.fixAll.ruff": "explicit",
            "source.organizeImports.ruff": "explicit",
        },
    },
    "ty.serverMode": "languageServer",
    "ty.disableLanguageServices": True,
    "python.testing.pytestEnabled": True,
    "python.testing.unittestEnabled": False,
    "python.testing.pytestPath": "pytest",
    "python.testing.pytestArgs": ["."],
}
VSCODE_MANAGED_TASKS: dict[str, Any] = {
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Bertrand: pytest",
            "type": "shell",
            "command": "pytest -q",
            "options": {"cwd": "${workspaceFolder}"},
            "problemMatcher": [],
        },
        {
            "label": "Bertrand: ruff check",
            "type": "shell",
            "command": "ruff check .",
            "options": {"cwd": "${workspaceFolder}"},
            "problemMatcher": [],
        },
        {
            "label": "Bertrand: ruff format",
            "type": "shell",
            "command": "ruff format .",
            "options": {"cwd": "${workspaceFolder}"},
            "problemMatcher": [],
        },
        {
            "label": "Bertrand: ty check",
            "type": "shell",
            "command": "ty check .",
            "options": {"cwd": "${workspaceFolder}"},
            "problemMatcher": [],
        }
    ],
}


class CodeError(OSError):
    """A structured error used to keep RPC failure categories stable."""
    Category: TypeAlias = Literal[
        "probe_timeout",
        "invalid_service_environment",
        "invalid_request",
        "container_not_found",
        "prereq_missing",
        "invalid_config",
        "attach_failed",
        "unsupported_editor",
    ]

    def __init__(self, category: CodeError.Category, message: str) -> None:
        self.category = category
        self.message = message
        super().__init__(f"{category}: {message}")

    def __str__(self) -> str:
        return f"{self.category}: {self.message}"


def _container_bin() -> Path:
    value = os.environ.get(CONTAINER_BIN_ENV)
    if value is None:
        raise CodeError(
            "invalid_service_environment",
            "systemd service could not locate the container executable (usually "
            f"indicates a corrupted {CODE_SERVICE_NAME} unit).  This should never "
            "occur; if you see this message, try re-running the `$ bertrand code` or "
            "`$ bertrand enter` CLI commands to regenerate the unit."
        )
    candidate = Path(value)
    if not candidate.is_absolute():
        raise CodeError(
            "invalid_service_environment",
            f"{CONTAINER_BIN_ENV} must be an absolute path: {value}"
        )
    return candidate.expanduser().resolve()


def _editor_bin() -> Path:
    value = os.environ.get(EDITOR_BIN_ENV)
    if value is None:
        raise CodeError(
            "invalid_service_environment",
            "systemd service could not locate the editor executable (usually "
            f"indicates a corrupted {CODE_SERVICE_NAME} unit).  This should never "
            "occur; if you see this message, try re-running the `$ bertrand code` or "
            "`$ bertrand enter` CLI commands to regenerate the unit."
        )
    candidate = Path(value)
    if not candidate.is_absolute():
        raise CodeError(
            "invalid_service_environment",
            f"{EDITOR_BIN_ENV} must be an absolute path: {value}"
        )
    return candidate.expanduser().resolve()


def _validate_version(version: int) -> None:
    if version != CODE_SOCKET_VERSION:
        raise ValidationError(f"unsupported protocol version: {version}")


def _validate_env_root(env_root: str) -> None:
    path = Path(env_root)
    if not path.is_absolute():
        raise ValidationError(f"env_root must be an absolute path: {env_root}")
    if not path.exists():
        raise ValidationError(f"env_root does not exist: {path}")
    if not path.is_dir():
        raise ValidationError(f"env_root must be a directory: {path}")


def _validate_container_id(container_id: str) -> None:
    container_id = container_id.strip()
    if not container_id:
        raise ValidationError("container_id must be non-empty")


Version = Annotated[int, AfterValidator(_validate_version)]
Operation = Literal["open_editor"]
EnvRoot = Annotated[str, AfterValidator(_validate_env_root)]
ContainerID = Annotated[str, AfterValidator(_validate_container_id)]


# TODO: These requests should should conform to JSON-RPC 2.0 spec


class CodeRequest(BaseModel):
    """JSON-RPC request schema sent to the host's code RPC service."""
    model_config = ConfigDict(extra="forbid")
    version: Version
    op: Operation
    env_root: EnvRoot
    container_id: ContainerID

    @model_validator(mode="after")
    def _validate(self) -> Self:
        return self


class CodeResponse(BaseModel):
    """JSON-RPC response schema returned to a container's `bertrand code` command."""
    model_config = ConfigDict(extra="forbid")
    ok: bool
    error: str
    warnings: list[str] = Field(default_factory=list)


####################
####    HOST    ####
####################


@dataclass
class CodeServer:
    """A minimal, host-side listener that handles JSON-RPC requests from in-container
    `bertrand code` commands over a Unix socket.  When a request is received, the
    server will attempt to launch the specified editor on the host, pointed at the
    container's `MOUNT` directory.  The server will then return a JSON-RPC response
    indicating success or failure, as well as an error message or soft warnings if
    applicable.

    Attributes
    ----------
    path : Path, optional
        The host path to the server's Unix socket file.  Defaults to `CODE_SOCKET`,
        which is held in user scope.  This path must be absolute, and the code server
        will only instantiate a socket at this location when its `listen()` method is
        called.
    """
    path: Path = field(default=CODE_SOCKET)
    _sock: socket.socket | None = field(default=None, repr=False)

    def __post_init__(self) -> None:
        if not self.path.is_absolute():
            raise CodeError("invalid_request", f"socket path must be absolute: {self.path}")

    def _ensure_socket(self) -> None:
        # make private directory and clear existing socket file if needed
        self.path = self.path.expanduser().resolve()
        mkdir_private(self.path.parent)
        if self.path.exists():
            mode = self.path.lstat().st_mode
            if not stat.S_ISSOCK(mode):
                raise OSError(f"socket path occupied: {self.path}")
            self.path.unlink(missing_ok=True)

        # create Unix socket with restrictive permissions and bind to path
        server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            server.bind(str(self.path))
            server.listen()
            self.path.chmod(0o600)
        except Exception:
            server.close()
            raise

        self._sock = server

    def _read_line(self) -> tuple[socket.socket, str]:
        assert self._sock is not None, "server socket is not initialized"
        while True:
            # accept one connection at a time
            conn, _ = self._sock.accept()
            conn.settimeout(CODE_RPC_READ_TIMEOUT)

            # read a single line of input from the connection, rejecting malformed or
            # maliciously-sized payloads
            try:
                reader = conn.makefile("r", encoding="utf-8", newline="\n")
                try:
                    line = reader.readline(MAX_REQUEST_BYTES + 1)
                finally:
                    reader.close()
                return conn, line

            # drop stalled/broken clients without taking down the listener loop
            except (TimeoutError, socket.timeout, OSError, UnicodeError):
                conn.close()
                continue

            # more serious errors should be raised to the caller
            except Exception:
                conn.close()
                raise

    def _handle_request(self, line: str) -> CodeResponse:
        if not line:
            raise CodeError("invalid_request", "empty request")
        if len(line) > MAX_REQUEST_BYTES:
            raise CodeError("invalid_request", "request exceeds maximum size")
        if not line.endswith("\n"):
            raise CodeError("invalid_request", "request must be newline-terminated")
        text = line.removesuffix("\n").strip()
        if not text:
            raise CodeError("invalid_request", "empty request")

        # parse JSON
        try:
            payload = json.loads(text)
        except json.JSONDecodeError as err:
            raise CodeError("invalid_request", f"malformed JSON request: {err.msg}") from err
        if not isinstance(payload, dict):
            raise CodeError("invalid_request", "request must be a JSON object")

        # validate schema
        try:
            request = CodeRequest.model_validate(payload)
        except ValidationError as err:
            raise CodeError("invalid_request", str(err)) from err

        # launch editor
        try:
            warnings = self._launch_editor(
                Path(request.env_root).expanduser().resolve(),
                request.container_id.strip(),
            )
            return CodeResponse.model_construct(ok=True, error="", warnings=warnings)
        except CodeError as err:
            return CodeResponse.model_construct(ok=False, error=str(err))
        except Exception as err:
            return CodeResponse.model_construct(
                ok=False,
                error=f"internal_error: failed to launch editor: {err}"
            )

    def _remaining_probe_timeout(self, deadline: float, *, step: str) -> float:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise CodeError(
                "probe_timeout",
                f"launch budget exhausted before probe step '{step}' could run",
            )
        return min(CODE_PROBE_TIMEOUT, remaining)

    def _ensure_running_container(
        self,
        container_bin: Path,
        container_id: str,
        *,
        deadline: float
    ) -> None:
        # get container state
        try:
            result = run(
                [
                    str(container_bin),
                    "container", "inspect",
                    "--format", "{{.State.Status}}",
                    container_id
                ],
                timeout=self._remaining_probe_timeout(
                    deadline,
                    step="container inspect"
                ),
            )
        except TimeoutExpired as err:
            raise CodeError(
                "container_not_found",
                f"timed out while checking status of container '{container_id}'"
            ) from err
        except CommandError as err:
            raise CodeError(
                "container_not_found",
                f"container '{container_id}' is not available: {err}"
            ) from err

        # ensure running
        stdout = result.stdout.strip()
        stderr = result.stderr.strip()
        if result.returncode != 0:
            detail = stderr or stdout
            suffix = f": {detail}" if detail else ""
            raise CodeError(
                "container_not_found",
                f"container '{container_id}' is not available{suffix}",
            )
        if not stdout:
            raise CodeError(
                "container_not_found",
                f"container '{container_id}' did not report a state",
            )
        if stdout not in ("running", "restarting"):
            raise CodeError(
                "container_not_found",
                f"container '{container_id}' is '{stdout}' (expected running)",
            )

    def _ensure_remote_containers_extension(
        self,
        editor_bin: Path,
        *,
        deadline: float
    ) -> None:
        # list editor extensions
        try:
            result = run(
                [str(editor_bin), "--list-extensions"],
                timeout=self._remaining_probe_timeout(
                    deadline,
                    step="vscode extension listing"
                ),
            )
        except TimeoutExpired as err:
            raise CodeError(
                "prereq_missing",
                "timed out while checking for required VS Code extensions"
            ) from err
        except CommandError as err:
            stdout = err.stdout.strip()
            stderr = err.stderr.strip()
            detail: list[str] = []
            if stderr:
                detail.append(stderr)
            if stdout:
                detail.append(stdout)
            suffix = f": {'\n'.join(detail)}" if detail else ""
            raise CodeError(
                "prereq_missing",
                f"failed to query VS Code extensions{suffix}"
            ) from err

        # check for remote extensions in output list
        search = VSCODE_REMOTE_EXTENSION.lower()
        for ext in result.stdout.splitlines():
            if ext.strip().lower() == search:
                return
        raise CodeError(
            "prereq_missing",
            f"required VS Code extension is missing: {VSCODE_REMOTE_EXTENSION}",
        )

    # TODO: check for timeout deadline while writing the workspace file?

    def _ensure_managed_workspace_file(
        self,
        env_root: Path,
    ) -> None:
        # gather recommended extensions
        recommended_extensions: list[str] = []
        clangd_arguments: list[str] = []
        try:
            with Config(env_root) as config:
                # pre-validated on config enter:
                clangd_arguments = config["tool", "clangd", "arguments"]
                agent = config["tool", "bertrand", "agent"]
                assist = config["tool", "bertrand", "assist"]
                seen: set[str] = set()
                for ext in (
                    *VSCODE_BASE_RECOMMENDED_EXTENSIONS,
                    *AGENTS[agent],
                    *ASSISTS[assist]
                ):
                    if ext not in seen:
                        seen.add(ext)
                        recommended_extensions.append(ext)
        except KeyError as err:
            raise CodeError(
                "invalid_config",
                f"unsupported configuration option in pyproject.toml: {err}"
            ) from err
        except OSError as err:
            raise CodeError("invalid_config", str(err)) from err

        # render managed workspace content
        file = env_root / VSCODE_MANAGED_WORKSPACE_FILE
        settings = VSCODE_MANAGED_SETTINGS.copy()
        settings["clangd.arguments"] = clangd_arguments
        payload = {
            "folders": [{"path": str(MOUNT)}],
            "settings": settings,
            "extensions": {"recommendations": recommended_extensions},
            "tasks": VSCODE_MANAGED_TASKS,
        }
        text = json.dumps(payload, indent=2, sort_keys=True) + "\n"

        # only write if content changed
        if file.exists():
            if not file.is_file():
                raise CodeError(
                    "invalid_config",
                    f"cannot write VS Code workspace file; path occupied: {file}"
                )
            try:
                current = file.read_text(encoding="utf-8")
            except OSError as err:
                raise CodeError(
                    "invalid_config",
                    f"failed to read managed workspace file: {file}: {err}",
                ) from err
            if current == text:
                return

        # atomically write new content
        try:
            atomic_write_text(file, text, encoding="utf-8")
        except OSError as err:
            raise CodeError(
                "invalid_config",
                f"failed to write managed workspace file: {file}: {err}",
            ) from err

    def _check_for_container_tool(
        self,
        container_bin: Path,
        container_id: str,
        *,
        tool: str,
        deadline: float,
    ) -> None:
        # check for tool in container by invoking `command -v` through `container exec`
        try:
            result = run(
                [
                    str(container_bin),
                    "exec",
                    container_id,
                    "sh", "-lc", f"command -v {shlex.quote(tool)}"
                ],
                timeout=self._remaining_probe_timeout(
                    deadline,
                    step=f"{tool} lookup",
                ),
            )
        except TimeoutExpired as err:
            raise CodeError(
                "prereq_missing",
                f"timed out while checking for {tool} in container '{container_id}'"
            ) from err
        except CommandError as err:
            stdout = err.stdout.strip()
            stderr = err.stderr.strip()
            detail: list[str] = []
            if stderr:
                detail.append(stderr)
            if stdout:
                detail.append(stdout)
            suffix = f": {'\n'.join(detail)}" if detail else ""
            raise CodeError(
                "prereq_missing",
                f"failed to check for {tool} in container '{container_id}'{suffix}"
            ) from err

        # no output implies tool is missing
        stdout = result.stdout.strip()
        if not stdout:
            stderr = result.stderr.strip()
            suffix = f": {stderr}" if stderr else ""
            raise CodeError(
                "prereq_missing",
                f"{tool} is not available in container '{container_id}'{suffix}",
            )

    def _vscode_workspace_uri(self, container_id: str) -> str:
        # NOTE: this URI allows the VSCode Remote Containers extension to attach to a
        # running container by ID, but is not technically part of the public API.  It is
        # well-documented in various issues and discussions, but may be subject to change
        # without notice.  If this breaks, it should have been replaced by something more
        # stable that we can use instead.
        container_id = urllib.parse.quote(container_id, safe="")
        workspace_file = urllib.parse.quote(str(MOUNT / VSCODE_MANAGED_WORKSPACE_FILE), safe="/")
        return f"vscode-remote://attached-container+{container_id}{workspace_file}"

    def _launch_editor(self, env_root: Path, container_id: str) -> list[str]:
        deadline = time.monotonic() + CODE_LAUNCH_TIMEOUT

        # get container and editor executables from systemd environment variables
        container_bin = _container_bin()
        editor_bin = _editor_bin()

        # ensure container is running and we can attach it to the container's toolchain
        # without altering any user-owned .vscode/settings.json
        self._ensure_running_container(container_bin, container_id, deadline=deadline)
        self._ensure_remote_containers_extension(editor_bin, deadline=deadline)
        self._ensure_managed_workspace_file(env_root)

        # check for tools inside container and warn if any are missing
        warnings: list[str] = []
        for tool, warning_hint in (
            ("clangd", "C/C++ language features may be degraded in this editor session."),
            ("ruff", "Python linting/formatting features may be degraded in this editor session."),
            ("ty", (
                "Python type-checking/language-service features may be degraded in "
                "this editor session."
            )),
            ("pytest", (
                "Python test discovery/execution features may be degraded in this "
                "editor session."
            )),
            ("bertrand-mcp", (
                "MCP server integration may be unavailable in this editor session."
            )),
        ):
            try:
                self._check_for_container_tool(
                    container_bin,
                    container_id,
                    tool=tool,
                    deadline=deadline,
                )
            except CodeError as err:
                warnings.append(f"{str(err)}\n\t{warning_hint}")

        # add Bertrand's MCP entry into workspace-level VSCode MCP config, without
        # touching other entries
        mcp_warning = sync_vscode_mcp_config(env_root)
        if mcp_warning is not None:
            warnings.append(mcp_warning)

        # sync tooling files with `pyproject.toml` before launching editor, so its
        # extensions always see the latest config
        with Config(env_root) as config:
            config.sync()

        # open editor in detached (non-blocking) process
        try:
            subprocess.Popen(
                [str(editor_bin), "--file-uri", self._vscode_workspace_uri(container_id)],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                start_new_session=True,
            )
        except OSError as err:
            raise CodeError(
                "attach_failed",
                f"failed to launch VS Code container attach session: {err}",
            ) from err
        return warnings

    def listen(self) -> None:
        """Serve requests indefinitely until `close()` is called."""
        self._ensure_socket()
        while self._sock is not None:
            # read a single line of input from the socket, blocking until a client
            # connects
            conn, line = self._read_line()
            try:
                # launch editor and prepare response
                try:
                    response = self._handle_request(line)
                except CodeError as err:
                    response = CodeResponse.model_construct(
                        ok=False,
                        error=str(err)
                    )
                except Exception as err:
                    response = CodeResponse.model_construct(
                        ok=False,
                        error=f"internal server error: {err}"
                    )

                # send response back to client
                try:
                    payload = json.dumps(
                        response.model_dump(mode="json"),
                        separators=(",", ":")
                    ) + "\n"
                    conn.sendall(payload.encode("utf-8"))
                except OSError:
                    pass

            # close connection to client and return to listening for next request
            finally:
                conn.close()

    def close(self) -> None:
        """Close the server and clean up the socket file."""
        if self._sock is None:
            return
        self._sock.close()
        self._sock = None
        try:
            if self.path.exists() and stat.S_ISSOCK(self.path.lstat().st_mode):
                self.path.unlink(missing_ok=True)
        except OSError:
            pass


def main() -> None:
    """Entry point for the code RPC listener, which starts the server and begins
    listening for editor requests.  This is exported as a script in `pyproject.toml`
    and invoked by systemd when starting the corresponding service, possibly upon
    login.
    """
    try:
        server = CodeServer(CODE_SOCKET)
        try:
            server.listen()
        finally:
            server.close()
    except KeyboardInterrupt:
        return


######################
####    CLIENT    ####
######################


def _service_path(key: str, path: str) -> str:
    escaped = path.replace("\\", "\\\\").replace('"', '\\"')
    return f'Environment="{key}={escaped}"'


def _render_code_service() -> str:
    # TODO: I can't read MOUNT here - this executes on the host, not inside a container,
    # so I have to pass env_root into this context in order to read the config
    # correctly.

    # read editor choice from pyproject.tom
    with Config(MOUNT) as config:
        editor = config["tool", "bertrand", "code"]  # validated on config enter

    # find RPC server, editor, and container executables on PATH
    rpc_bin = shutil.which("bertrand-code-rpc")
    if rpc_bin is None:
        raise CodeError(
            "invalid_service_environment",
            "'bertrand-code-rpc' executable not found on PATH.  Ensure that Bertrand's "
            "console scripts are installed and discoverable via PATH before "
            "interacting with the code server."
        )
    editor_bin = shutil.which(EDITORS[editor])
    if editor_bin is None:
        raise CodeError(
            "invalid_service_environment",
            f"editor executable for '{editor}' not found on PATH.  Ensure that your "
            "chosen editor is installed and discoverable via PATH before "
            "interacting with the code server."
        )
    container_bin = shutil.which("podman")
    if container_bin is None:
        raise CodeError(
            "invalid_service_environment",
            "'podman' executable not found on PATH.  Ensure that podman is installed "
            "and discoverable via PATH before interacting with the code server."
        )

    # render unit file with updated paths to host executables, which are passed to the
    # service as environment variables
    return f"""[Unit]
Description=Bertrand Code RPC Listener

[Service]
Type=simple
ExecStart={shlex.quote(str(rpc_bin))}
Restart=on-failure
RestartSec=1
{_service_path(CONTAINER_BIN_ENV, container_bin)}
{_service_path(EDITOR_BIN_ENV, editor_bin)}

[Install]
WantedBy=default.target
"""


def _warn_code_service_unavailable(reason: str) -> None:
    print(
        f"warning: failed to reach the code RPC service\n"
        f"{reason}\n\n"
        "In-container `bertrand code` will fail until you exit and re-enter the "
        "environment.",
        file=sys.stderr
    )


def _code_service_reachable(
    *,
    timeout: float = CODE_PROBE_TIMEOUT,
    interval: float = 0.1,
) -> bool:
    if timeout <= 0.0:
        raise ValueError(f"timeout must be positive: {timeout}")
    if interval <= 0.0:
        raise ValueError(f"interval must be positive: {interval}")

    # spin while trying to connect to socket until timeout expires
    path = str(CODE_SOCKET)
    deadline = time.monotonic() + timeout
    while True:
        client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        client.settimeout(max(0.001, deadline - time.monotonic()))
        try:
            client.connect(path)
            break
        except OSError:
            if time.monotonic() >= deadline:
                return False
            time.sleep(max(0.0, min(interval, deadline - time.monotonic())))
        finally:
            client.close()
    return True


def start_code_service(ctx: Pipeline.InProgress, *, strict: bool) -> bool:
    """Start the code RPC listener as a systemd user service, and verify that it
    becomes reachable within a bounded timeout.

    Parameters
    ----------
    ctx : Pipeline.InProgress
        The current pipeline context, used to record the atomic operations used to
        start the service.
    strict : bool
        If true, raise a hard OSError if the service fails to start or become
        reachable.  Otherwise, print a warning to stderr and return False.

    Returns
    -------
    bool
        True if the service was started and became reachable, otherwise False.

    Raises
    ------
    CodeError
        If `strict` is true and we fail to render or write the unit file, reload the
        systemd daemon, start the service, or probe the socket for reachability.
    Exception
        If `strict` is true and any unexpected error occurs during this process.

    Notes
    -----
    This function must be called on the host process before every (external)
    `bertrand code` or `bertrand enter` command.  It should never be called inside the
    container context, since it passes host executable paths to the generated systemd
    service.
    """
    try:
        # render up-to-date unit file
        unit_text = _render_code_service()
        if (
            not CODE_SERVICE_FILE.exists() or
            CODE_SERVICE_FILE.read_text(encoding="utf-8") != unit_text
        ):
            try:
                ctx.do(WriteText(path=CODE_SERVICE_FILE, text=unit_text, replace=None))
            except Exception as err:
                raise CodeError(
                    "invalid_service_environment",
                    f"failed to write systemd unit file at {CODE_SERVICE_FILE}\n{str(err)}"
                ) from err
            try:
                ctx.do(ReloadDaemon(user=True))
            except Exception as err:
                raise CodeError(
                    "invalid_service_environment",
                    "failed to reload systemd user daemon after writing unit file at "
                    f"{CODE_SERVICE_FILE}\n{str(err)}"
                ) from err

        # start code service if it is not already running
        try:
            ctx.do(StartService(name=CODE_SERVICE_NAME, user=True), undo=False)
        except Exception as err:
            raise CodeError(
                "invalid_service_environment",
                f"failed to start systemd user service '{CODE_SERVICE_NAME}'.  Check "
                f"`systemctl --user status {CODE_SERVICE_NAME}` for details.\n{str(err)}"
            ) from err

        # wait until service is reachable or we time out
        try:
            if _code_service_reachable(timeout=CODE_PROBE_TIMEOUT):
                return True
        except Exception as err:
            raise CodeError(
                "invalid_service_environment",
                f"failed to probe code RPC socket at {CODE_SOCKET} after starting "
                f"service '{CODE_SERVICE_NAME}'.  Check `systemctl --user status "
                f"{CODE_SERVICE_NAME}` for details.\n{str(err)}"
            ) from err

        raise CodeError(
            "probe_timeout",
            f"code RPC socket at {CODE_SOCKET} did not become reachable within "
            f"{CODE_PROBE_TIMEOUT} seconds after starting service "
            f"'{CODE_SERVICE_NAME}'.  Check `systemctl --user status {CODE_SERVICE_NAME}` "
            "for details."
        )

    except Exception as err:
        if strict:
            raise err
        _warn_code_service_unavailable(str(err))
        return False


def _send_request(
    request: CodeRequest,
    *,
    timeout: float = CODE_RPC_CLIENT_TIMEOUT,
) -> CodeResponse:
    """Send a single request to the code RPC server from inside a container context and
    wait for a single response.

    Parameters
    ----------
    request : CodeRequest
        The request to send to the server.  This will be serialized as JSON and sent as
        a single line of text, terminated by a newline character.
    timeout : float, optional
        The maximum number of seconds to wait for a response from the server before
        raising a timeout error.  Defaults to `CODE_RPC_CLIENT_TIMEOUT`.

    Returns
    -------
    CodeResponse
        The response from the server, parsed from JSON and validated against the
        `CodeResponse` schema.

    Raises
    ------
    OSError
        If there is an error connecting to the server, sending the request, or if the
        response is malformed or indicates an error.
    """
    path = CONTAINER_SOCKET.expanduser()
    if not path.is_absolute():
        raise OSError(f"RPC socket path must be absolute: {path}")
    if not path.exists():
        raise OSError(f"RPC socket does not exist: {path}")
    if not path.is_socket():
        raise OSError(f"RPC socket path is not a socket: {path}")

    # serialize request as JSON and open socket
    payload = json.dumps(request.model_dump(mode="json"), separators=(",", ":")) + "\n"
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    client.settimeout(timeout)
    try:
        # connect to server
        try:
            client.connect(str(path))
        except OSError as err:
            raise OSError(f"failed to connect to RPC socket: {path}") from err

        # send request
        client.sendall(payload.encode("utf-8"))
        reader = client.makefile("r", encoding="utf-8", newline="\n")

        # read response line
        try:
            line = reader.readline(MAX_REQUEST_BYTES + 1)
        finally:
            reader.close()

    # close client socket
    finally:
        client.close()

    # validate response format
    if not line:
        raise OSError("empty response from RPC server")
    if len(line) > MAX_REQUEST_BYTES:
        raise OSError("RPC response exceeds maximum size")
    if not line.endswith("\n"):
        raise OSError("RPC response must be newline-terminated")
    text = line.removesuffix("\n").strip()
    if not text:
        raise OSError("empty response from RPC server")

    # parse JSON
    try:
        response = json.loads(text)
    except json.JSONDecodeError as err:
        raise OSError(f"malformed RPC response: {err.msg}") from err
    if not isinstance(response, dict):
        raise OSError("RPC response must be a JSON object")

    # validate schema
    try:
        return CodeResponse.model_validate(response)
    except ValidationError as err:
        raise OSError(f"invalid RPC response: {err}") from err


def open_editor(timeout: float = CODE_RPC_CLIENT_TIMEOUT) -> list[str]:
    """Send a request from a container context to the code RPC service in order to
    launch an editor session on the host and attach it to the current container.

    Parameters
    ----------
    timeout : float, optional
        The maximum number of seconds to wait for a response from the server before
        raising a timeout error.  Defaults to `CODE_RPC_CLIENT_TIMEOUT`.

    Returns
    -------
    list[str]
        Non-fatal warnings returned by the host-side launch flow.

    Raises
    ------
    OSError
        If `env_root` is not an absolute path, or if there is an error processing the
        RPC request.
    """
    # check whether `start_code_service()` returned true when we entered the container
    # shell
    if os.environ.get(CODE_SERVICE_ENV, "").strip() != "1":
        raise CodeError(
            "invalid_service_environment",
            "Code server not available.  Please exit the environment and re-enter, "
            "or call `bertrand code` outside of a container to start the RPC "
            "service."
        )

    # get host environment path from shell variable set by `bertrand enter`
    env_path = os.environ.get(HOST_ENV)
    if env_path is None:
        raise CodeError(
            "invalid_service_environment",
            f"{HOST_ENV} environment variable is not set.  This variable should be set "
            "automatically when you enter the environment with `bertrand enter`.  If "
            "you are seeing this message, ensure that you have entered the environment "
            "and that your shell session has been properly initialized by the entry "
            "process."
        )
    env_root = PosixPath(env_path)
    if env_root != env_root.expanduser().resolve():
        raise OSError(f"{HOST_ENV} path is not normalized: {env_root}")

    # get container ID from shell variable set by `bertrand enter`
    container_id = os.environ.get(CONTAINER_ID_ENV)
    if container_id is None:
        raise CodeError(
            "invalid_service_environment",
            f"{CONTAINER_ID_ENV} environment variable is not set.  This variable "
            "should be set automatically when you enter the environment with "
            "`bertrand enter`.  If you are seeing this message, ensure that you have "
            "entered the environment and that your shell session has been properly "
            "initialized by the entry process."
        )

    # construct and send request
    response = _send_request(
        CodeRequest.model_construct(
            version=CODE_SOCKET_VERSION,
            op="open_editor",
            env_root=str(env_root),
            container_id=container_id,
        ),
        timeout=timeout,
    )

    # handle response and return warnings if successful
    if not response.ok:
        raise CodeError(
            "attach_failed",
            f"RPC request failed: {response.error}"
        )
    return response.warnings
