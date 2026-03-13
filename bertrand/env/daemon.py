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
import uuid

from dataclasses import dataclass, field
from pathlib import Path, PosixPath
from typing import Annotated, Callable, Literal, Mapping, NoReturn, Self

from pydantic import (
    AfterValidator,
    BaseModel,
    ConfigDict,
    Field,
    ValidationError,
    model_validator,
)

from .config import (
    CAPABILITIES,
    CONTAINER_BIN_ENV,
    CONTAINER_ID_ENV,
    CONTAINER_SOCKET,
    EDITOR_BIN_ENV,
    HOST_SOCKET,
    PROJECT_ROOT_ENV,
    SOCKET_ENV,
    VSCODE_RESOURCE,
    WORKTREE_MOUNT,
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
    mkdir_private,
    run
)

# pylint: disable=bare-except, broad-exception-caught


def _check_env_root(env_root: str) -> str:
    path = Path(env_root)
    if not path.is_absolute():
        raise ValueError(f"env_root must be an absolute path: {env_root}")
    if not path.exists():
        raise ValueError(f"env_root does not exist: {path}")
    if not path.is_dir():
        raise ValueError(f"env_root must be a directory: {path}")
    return str(path)


def _check_container_id(container_id: str) -> str:
    container_id = container_id.strip()
    if not container_id:
        raise ValueError("container_id must be non-empty")
    return container_id


def _check_request_id(request_id: str) -> str:
    request_id = request_id.strip()
    if not request_id:
        raise ValueError("id must be non-empty")
    return request_id


type JSONRPCVersion = Literal["2.0"]
type Method = Literal["code.open"]
type ContainerID = Annotated[  # pylint: disable=invalid-name
    str,
    AfterValidator(_check_container_id)
]
type EnvRoot = Annotated[str, AfterValidator(_check_env_root)]
type RequestID = Annotated[  # pylint: disable=invalid-name
    str,
    AfterValidator(_check_request_id)
]


class CodeError(OSError):
    """A structured error used to keep RPC failure categories stable."""
    type Category = Literal[
        "probe_timeout",
        "invalid_service_environment",
        "invalid_request",
        "container_not_found",
        "prereq_missing",
        "invalid_config",
        "attach_failed",
        "unsupported_editor",
    ]
    category: Category
    detail: str

    def __init__(self, category: Category, detail: str) -> None:
        self.category = category
        self.detail = detail
        super().__init__(f"{category}: {detail}")

    def __str__(self) -> str:
        return f"{self.category}: {self.detail}"


class RPCRequest(BaseModel):
    """A validated JSON-RPC 2.0 request to Bertrand's host daemon service."""
    model_config = ConfigDict(extra="forbid")
    jsonrpc: JSONRPCVersion
    id: RequestID
    method: Method

    class CodeOpen(BaseModel):
        """Typed params payload for `code.open` JSON-RPC requests."""
        model_config = ConfigDict(extra="forbid")
        env_root: EnvRoot
        container_id: ContainerID

    params: CodeOpen


class RPCResponse(BaseModel):
    """JSON-RPC response schema returned to in-container `bertrand code` calls."""
    model_config = ConfigDict(extra="forbid")
    jsonrpc: JSONRPCVersion
    id: RequestID | None

    class CodeOpen(BaseModel):
        """Typed result payload for successful `code.open` JSON-RPC responses."""
        model_config = ConfigDict(extra="forbid")
        warnings: list[str] = Field(default_factory=list)

    result: CodeOpen | None = None

    class Error(BaseModel):
        """JSON-RPC 2.0 error object."""
        model_config = ConfigDict(extra="forbid")
        code: int
        message: str

        class Parse(BaseModel):
            """Additional metadata for JSON-RPC parse errors."""
            model_config = ConfigDict(extra="forbid")
            doc: str
            pos: int

        class Code(BaseModel):
            """Additional metadata for JSON-RPC error responses."""
            model_config = ConfigDict(extra="forbid")
            category: CodeError.Category
            detail: str

        data: Parse | Code | None = None

    error: Error | None = None

    @model_validator(mode="after")
    def _validate_result_xor_error(self) -> Self:
        if (self.result is None) == (self.error is None):
            raise ValueError("JSON-RPC response must include exactly one of result or error")
        return self


JSON_RPC_VERSION: JSONRPCVersion = "2.0"
JSON_RPC_PARSE_ERROR: int = -32700              # Invalid JSON was received by the server
JSON_RPC_INVALID_REQUEST: int = -32600          # The JSON sent is not a valid Request object
JSON_RPC_METHOD_NOT_FOUND: int = -32601         # The method does not exist / is not available
JSON_RPC_INVALID_PARAMS: int = -32602           # Invalid method parameter(s)
JSON_RPC_INTERNAL_ERROR: int = -32603           # Internal JSON-RPC error
JSON_RPC_CODE_ERROR: int = 1                    # Custom error code for CodeError exceptions
MAX_REQUEST_BYTES: int = 1024 * 1024            # 1 MiB


def _throw_internal_error(err: RPCResponse.Error) -> NoReturn:
    raise RuntimeError(err.message)


def _throw_invalid_params(err: RPCResponse.Error) -> NoReturn:
    raise ValueError(err.message)


def _throw_invalid_request(err: RPCResponse.Error) -> NoReturn:
    raise TypeError(err.message)


def _throw_method_not_found(err: RPCResponse.Error) -> NoReturn:
    raise NotImplementedError(err.message)


def _throw_parse_error(err: RPCResponse.Error) -> NoReturn:
    if isinstance(err.data, RPCResponse.Error.Parse):
        raise json.JSONDecodeError(err.message, doc=err.data.doc, pos=err.data.pos)
    raise ValueError(err.message)


def _throw_code_error(err: RPCResponse.Error) -> NoReturn:
    if isinstance(err.data, RPCResponse.Error.Code):
        raise CodeError(category=err.data.category, detail=err.data.detail)
    raise OSError(err.message)


def _catch_internal_error(err: Exception) -> RPCResponse.Error:
    return RPCResponse.Error(code=JSON_RPC_INTERNAL_ERROR, message=str(err))


def _catch_invalid_params(err: Exception) -> RPCResponse.Error:
    return RPCResponse.Error(code=JSON_RPC_INVALID_PARAMS, message=str(err))


def _catch_invalid_request(err: Exception) -> RPCResponse.Error:
    return RPCResponse.Error(code=JSON_RPC_INVALID_REQUEST, message=str(err))


def _catch_method_not_found(err: Exception) -> RPCResponse.Error:
    return RPCResponse.Error(code=JSON_RPC_METHOD_NOT_FOUND, message=str(err))


def _catch_parse_error(err: Exception) -> RPCResponse.Error:
    if isinstance(err, json.JSONDecodeError):
        return RPCResponse.Error(
            code=JSON_RPC_PARSE_ERROR,
            message=str(err),
            data=RPCResponse.Error.Parse(doc=err.doc, pos=err.pos)
        )
    return RPCResponse.Error(code=JSON_RPC_PARSE_ERROR, message=str(err))


def _catch_code_error(err: Exception) -> RPCResponse.Error:
    if isinstance(err, CodeError):
        return RPCResponse.Error(
            code=JSON_RPC_CODE_ERROR,
            message=str(err),
            data=RPCResponse.Error.Code(category=err.category, detail=err.detail)
        )
    return RPCResponse.Error(code=JSON_RPC_CODE_ERROR, message=str(err))


JSON_RPC_THROW_ERR: Mapping[int, Callable[[RPCResponse.Error], NoReturn]] = {
    JSON_RPC_INTERNAL_ERROR: _throw_internal_error,
    JSON_RPC_INVALID_PARAMS: _throw_invalid_params,
    JSON_RPC_INVALID_REQUEST: _throw_invalid_request,
    JSON_RPC_METHOD_NOT_FOUND: _throw_method_not_found,
    JSON_RPC_PARSE_ERROR: _throw_parse_error,
    JSON_RPC_CODE_ERROR: _throw_code_error,
}
JSON_RPC_CATCH_ERR: Mapping[type[Exception], Callable[[Exception], RPCResponse.Error]] = {
    TypeError: _catch_invalid_request,
    json.JSONDecodeError: _catch_parse_error,
    NotImplementedError: _catch_method_not_found,
    RuntimeError: _catch_internal_error,
    ValidationError: _catch_invalid_request,
    ValueError: _catch_invalid_params,
    CodeError: _catch_code_error,
}


def _rpc_catch(err: Exception, *, request: RequestID | None) -> RPCResponse:
    handler = JSON_RPC_CATCH_ERR.get(type(err), _catch_internal_error)
    return RPCResponse(jsonrpc=JSON_RPC_VERSION, id=request, error=handler(err))


def _rpc_throw(err: RPCResponse.Error) -> NoReturn:
    handler = JSON_RPC_THROW_ERR.get(err.code, _throw_internal_error)
    handler(err)


####################
####    CODE    ####
####################


CODE_LAUNCH_TIMEOUT: float = 10.0
CODE_PROBE_TIMEOUT: float = 10.0
CODE_RPC_CLIENT_HEADROOM: float = 2.0
CODE_RPC_CLIENT_TIMEOUT: float = CODE_LAUNCH_TIMEOUT + CODE_RPC_CLIENT_HEADROOM
CODE_RPC_METHOD_OPEN: Method = "code.open"
CODE_RPC_READ_TIMEOUT: float = CODE_RPC_CLIENT_TIMEOUT
CODE_SERVICE_NAME = "bertrand-code.service"
CODE_SERVICE_FILE = User().home / ".config" / "systemd" / "user" / CODE_SERVICE_NAME
VSCODE_REMOTE_EXTENSION = "ms-vscode-remote.remote-containers"
VSCODE_EXECUTABLE_CANDIDATES: tuple[str, ...] = (
    "code",
    "com.visualstudio.code",
    "code-insiders",
    "com.visualstudio.code-insiders",
)


def _container_bin() -> Path:
    value = os.environ.get(CONTAINER_BIN_ENV)
    if value is None:
        raise CodeError(
            "invalid_service_environment",
            "systemd service could not locate the container executable (usually "
            f"indicates a corrupted {CODE_SERVICE_NAME} unit).  This should never "
            "occur; if you see this message, try re-running the `$ bertrand code` or "
            "`$ bertrand enter` CLI commands to regenerate the unit, or report an "
            "issue at if the problem persists."
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


# TODO: make sure the code open method is correct and minimal.


def _remaining_probe_timeout(deadline: float, *, step: str) -> float:
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise CodeError(
            "probe_timeout",
            f"launch budget exhausted before probe step '{step}' could run",
        )
    return min(CODE_PROBE_TIMEOUT, remaining)


def _ensure_running_container(
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
            timeout=_remaining_probe_timeout(
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
    editor_bin: Path,
    *,
    deadline: float
) -> None:
    # list editor extensions
    try:
        result = run(
            [str(editor_bin), "--list-extensions"],
            timeout=_remaining_probe_timeout(
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


def _check_for_container_tool(
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
            timeout=_remaining_probe_timeout(
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


def _vscode_workspace_uri(container_id: str, workspace: PosixPath) -> str:
    # NOTE: this URI allows the VSCode Remote Containers extension to attach to a
    # running container by ID, but is not technically part of the public API.  It is
    # well-documented in various issues and discussions, but may be subject to change
    # without notice.  If this breaks, it should have been replaced by something more
    # stable that we can use instead.
    container_id = urllib.parse.quote(container_id, safe="")
    workspace_file = urllib.parse.quote(str(WORKTREE_MOUNT / workspace), safe="/")
    return f"vscode-remote://attached-container+{container_id}{workspace_file}"


def _launch_editor(env_root: Path, container_id: str) -> list[str]:
    deadline = time.monotonic() + CODE_LAUNCH_TIMEOUT

    # get container and editor executables from systemd environment variables
    container_bin = _container_bin()
    editor_bin = _editor_bin()

    # ensure container is running and we can attach it to the container's toolchain
    # without altering any user-owned .vscode/settings.json
    _ensure_running_container(container_bin, container_id, deadline=deadline)
    _ensure_remote_containers_extension(editor_bin, deadline=deadline)
    try:
        config = Config.load(env_root)
        if VSCODE_RESOURCE not in config:
            raise CodeError(
                "invalid_config",
                f"layout is missing required '{VSCODE_RESOURCE}' resource"
            )
        workspace_abs_path = config.path(VSCODE_RESOURCE)
        if not workspace_abs_path.exists() or not workspace_abs_path.is_file():
            raise CodeError(
                "invalid_config",
                "missing VS Code workspace file at "
                f"{workspace_abs_path}; rerun `bertrand init` to regenerate it."
            )
    except CodeError:
        raise
    except OSError as err:
        raise CodeError("invalid_config", str(err)) from err

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
            _check_for_container_tool(
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

    # open editor in detached (non-blocking) process
    try:
        workspace_rel_path = CAPABILITIES["vscode"][config.profile][VSCODE_RESOURCE]
        subprocess.Popen(
            [
                str(editor_bin),
                "--file-uri",
                _vscode_workspace_uri(container_id, workspace_rel_path)
            ],
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


def _code_open(request: RPCRequest) -> RPCResponse:
    warnings = _launch_editor(
        Path(request.params.env_root).expanduser().resolve(),
        request.params.container_id.strip(),
    )
    return RPCResponse(
        jsonrpc=JSON_RPC_VERSION,
        id=request.id,
        result=RPCResponse.CodeOpen(warnings=warnings),
    )


####################
####    HOST    ####
####################


METHODS: dict[str, Callable[[RPCRequest], RPCResponse]] = {
    CODE_RPC_METHOD_OPEN: _code_open,
}


@dataclass
class Listener:
    """A minimal, host-side listener that handles JSON-RPC requests from in-container
    CLI commands over a Unix socket, which is mounted as part of container creation.

    Currently, the only supported request is `code.open`, which is used to launch a
    host text editor pointed at the container's `WORKTREE_MOUNT`.  Future requests may
    be added to allow other host-side operations that are not easily performed from the
    container context, such as GUI applications or (restricted) filesystem access.

    Attributes
    ----------
    path : Path
        The path to the host's Unix socket file.  This path must be absolute, and the
        code server will only instantiate a socket at this location when its `listen()`
        method is called.
    """
    path: Path
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
        except:
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
            except:
                conn.close()
                raise

    def _parse_request(self, line: str) -> RPCRequest:
        if not line:
            raise TypeError("empty request")
        if len(line) > MAX_REQUEST_BYTES:
            raise TypeError("request exceeds maximum size")
        if not line.endswith("\n"):
            raise TypeError("request must be newline-terminated")

        text = line.removesuffix("\n").strip()
        if not text:
            raise TypeError("empty request")

        return RPCRequest.model_validate(json.loads(text))

    def listen(self) -> None:
        """Serve requests indefinitely until `close()` is called."""
        self._ensure_socket()
        while True:
            # read a single line of input from the socket, blocking until a client
            # connects
            conn, line = self._read_line()
            try:
                try:
                    request = self._parse_request(line)
                    try:
                        handler = METHODS.get(request.method)
                        if handler is not None:
                            response = handler(request)
                        else:
                            response = _rpc_catch(
                                NotImplementedError(f"unknown method: {request.method}"),
                                request=request.id
                            )
                    except Exception as err:
                        response = _rpc_catch(err, request=request.id)
                except Exception as err:
                    response = _rpc_catch(err, request=None)

                # send response back to client (may be an error code)
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
        server = Listener(path=HOST_SOCKET)
        try:
            server.listen()
        finally:
            server.close()
    except KeyboardInterrupt:
        return


######################
####    CLIENT    ####
######################


# TODO: client should consider using dataclasses to specify what action to take, and
# just provide a generic `rpc_request()` function that takes a request dataclass and
# returns a response or throws an Python exception via `_rpc_throw()`.


def _service_path(key: str, path: str) -> str:
    escaped = path.replace("\\", "\\\\").replace('"', '\\"')
    return f'Environment="{key}={escaped}"'


def _render_code_service(env_root: Path) -> str:
    # Editor integration is resource-driven.  For now, host attach is implemented
    # only for vscode-capable environments.
    config = Config.load(env_root)
    if VSCODE_RESOURCE not in config:
        raise CodeError(
            "invalid_config",
            f"environment is missing required '{VSCODE_RESOURCE}' resource for "
            "host-side editor attach"
        )

    # find RPC server, editor, and container executables on PATH
    rpc_bin = shutil.which("bertrand-code-rpc")
    if rpc_bin is None:
        raise CodeError(
            "invalid_service_environment",
            "'bertrand-code-rpc' executable not found on PATH.  Ensure that Bertrand's "
            "console scripts are installed and discoverable via PATH before "
            "interacting with the code server."
        )
    editor_bin = None
    for candidate in VSCODE_EXECUTABLE_CANDIDATES:
        editor_bin = shutil.which(candidate)
        if editor_bin is not None:
            break
    if editor_bin is None:
        raise CodeError(
            "invalid_service_environment",
            "no supported VSCode executable found on PATH.  Searched: "
            f"{', '.join(VSCODE_EXECUTABLE_CANDIDATES)}"
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
    path = str(HOST_SOCKET)
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


def start_code_service(ctx: Pipeline.InProgress, *, env_root: Path, strict: bool) -> bool:
    """Start the code RPC listener as a systemd user service, and verify that it
    becomes reachable within a bounded timeout.

    Parameters
    ----------
    ctx : Pipeline.InProgress
        The current pipeline context, used to record the atomic operations used to
        start the service.
    env_root : Path
        The root path of the environment, used to read the editor choice from
        `pyproject.toml` and render the systemd unit file with the correct executable
        paths.
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
        unit_text = _render_code_service(env_root)
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
                f"failed to probe code RPC socket at {HOST_SOCKET} after starting "
                f"service '{CODE_SERVICE_NAME}'.  Check `systemctl --user status "
                f"{CODE_SERVICE_NAME}` for details.\n{str(err)}"
            ) from err

        raise CodeError(
            "probe_timeout",
            f"code RPC socket at {HOST_SOCKET} did not become reachable within "
            f"{CODE_PROBE_TIMEOUT} seconds after starting service "
            f"'{CODE_SERVICE_NAME}'.  Check `systemctl --user status {CODE_SERVICE_NAME}` "
            "for details."
        )

    except Exception as err:
        if strict:
            raise
        _warn_code_service_unavailable(str(err))
        return False


def _send_request(
    request: RPCRequest,
    *,
    timeout: float = CODE_RPC_CLIENT_TIMEOUT,
) -> RPCResponse:
    """Send a single request to the code RPC server from inside a container context and
    wait for a single response.

    Parameters
    ----------
    request : RPCRequest
        The request to send to the server.  This will be serialized as JSON and sent as
        a single line of text, terminated by a newline character.
    timeout : float, optional
        The maximum number of seconds to wait for a response from the server before
        raising a timeout error.  Defaults to `CODE_RPC_CLIENT_TIMEOUT`.

    Returns
    -------
    RPCResponse
        The response from the server, parsed from JSON and validated against the
        `RPCResponse` schema.

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
        result = RPCResponse.model_validate(response)
    except ValidationError as err:
        raise OSError(f"invalid RPC response: {err}") from err

    if result.id != request.id:
        raise OSError(
            "RPC response id mismatch: "
            f"expected {request.id!r}, got {result.id!r}"
        )
    return result


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
    if os.environ.get(SOCKET_ENV, "").strip() != "1":
        raise CodeError(
            "invalid_service_environment",
            "Code server not available.  Please exit the environment and re-enter, "
            "or call `bertrand code` outside of a container to start the RPC "
            "service."
        )

    # get host environment path from shell variable set by `bertrand enter`
    env_path = os.environ.get(PROJECT_ROOT_ENV)
    if env_path is None:
        raise CodeError(
            "invalid_service_environment",
            f"{PROJECT_ROOT_ENV} environment variable is not set.  This variable "
            "should be set automatically when you enter the environment with "
            "`bertrand enter`.  If you are seeing this message, ensure that you have "
            "entered the environment and that your shell session has been properly "
            "initialized by the entry process."
        )
    env_root = PosixPath(env_path)
    if env_root != env_root.expanduser().resolve():
        raise OSError(f"{PROJECT_ROOT_ENV} path is not normalized: {env_root}")

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
        RPCRequest(
            jsonrpc=JSON_RPC_VERSION,
            id=str(uuid.uuid4()),
            method=CODE_RPC_METHOD_OPEN,
            params=RPCRequest.CodeOpen(
                env_root=str(env_root),
                container_id=container_id,
            ),
        ),
        timeout=timeout,
    )

    # handle response and return warnings if successful
    if response.error is not None:
        category = (
            response.error.data.category
            if response.error.data is not None else None
        )
        if category is not None:
            raise CodeError(category, response.error.message)
        raise OSError(
            f"RPC request failed ({response.error.code}): {response.error.message}"
        )
    assert response.result is not None
    return response.result.warnings
