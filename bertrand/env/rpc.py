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
from pathlib import Path
from typing import Annotated, Callable, Literal, Mapping, NoReturn, Protocol, Self
from warnings import warn

from pydantic import (
    AfterValidator,
    BaseModel,
    ConfigDict,
    PositiveFloat,
    ValidationError,
    model_validator,
)

from .config import (
    CONTAINER_BIN_ENV,
    CONTAINER_ID_ENV,
    CONTAINER_SOCKET,
    EDITOR_BIN_ENV,
    HOST_SOCKET,
    IMAGE_TAG_ENV,
    SOCKET_ENV,
    VSCODE_WORKSPACE_FILE,
    WORKTREE_ENV,
    WORKTREE_MOUNT,
    Config,
    Editor,
    inside_image,
)
from .pipeline import (
    JSONValue,
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


JSON_RPC_VERSION: JSONRPCVersion = "2.0"
JSON_RPC_PARSE_ERROR: int = -32700              # Invalid JSON was received by the server
JSON_RPC_INTERNAL_ERROR: int = -32603           # Internal JSON-RPC error
JSON_RPC_INVALID_PARAMS: int = -32602           # Invalid method parameter(s)
JSON_RPC_METHOD_NOT_FOUND: int = -32601         # The method does not exist / is not available
JSON_RPC_INVALID_REQUEST: int = -32600          # The JSON sent is not a valid Request object
JSON_RPC_TIMEOUT_ERROR: int = -32000            # Custom error code for timeouts
MAX_REQUEST_BYTES: int = 1024 * 1024            # 1 MiB, to prevent malicious payloads
RPC_SERVICE_NAME: str = "bertrand-rpc"
RPC_SERVICE_FILE: Path = User().home / ".config" / "systemd" / "user" / RPC_SERVICE_NAME
RPC_TIMEOUT: float = 30.0


def _check_container_id(container_id: str) -> str:
    container_id = container_id.strip()
    if not container_id:
        raise ValueError("container_id must be non-empty")
    return container_id


def _check_method_name(method_name: str) -> str:
    method_name = method_name.strip()
    if method_name not in METHODS:
        raise NotImplementedError(f"unknown method: {method_name}")
    return method_name


def _check_request_id(request_id: str) -> str:
    request_id = request_id.strip()
    if not request_id:
        raise ValueError("id must be non-empty")
    return request_id


def _check_worktree(worktree: Path) -> Path:
    if not worktree.is_absolute():
        raise ValueError(f"worktree must be an absolute path: {worktree}")
    if not worktree.exists():
        raise ValueError(f"worktree does not exist: {worktree}")
    if not worktree.is_dir():
        raise ValueError(f"worktree must be a directory: {worktree}")
    return worktree.expanduser().resolve()


type ContainerID = Annotated[  # pylint: disable=invalid-name
    str,
    AfterValidator(_check_container_id)
]
type JSONRPCVersion = Literal["2.0"]
type MethodName = Annotated[str, AfterValidator(_check_method_name)]
type RequestID = Annotated[  # pylint: disable=invalid-name
    str,
    AfterValidator(_check_request_id)
]
type Worktree = Annotated[Path, AfterValidator(_check_worktree)]


class RPCRequest(BaseModel):
    """A validated JSON-RPC 2.0 request to Bertrand's host daemon service."""
    model_config = ConfigDict(extra="forbid")
    jsonrpc: JSONRPCVersion
    id: RequestID
    method: MethodName

    class CodeOpenRequest(BaseModel):
        """Typed params payload for `code.open` JSON-RPC requests."""
        model_config = ConfigDict(extra="forbid")
        container_id: ContainerID
        worktree: Worktree
        editor: Editor
        deadline: PositiveFloat

    type Params = CodeOpenRequest
    params: Params


class RPCResponse(BaseModel):
    """JSON-RPC response schema returned to in-container `bertrand code` calls."""
    model_config = ConfigDict(extra="forbid")
    jsonrpc: JSONRPCVersion
    id: RequestID | None

    class CodeOpenResult(BaseModel):
        """Typed result payload for successful `code.open` JSON-RPC responses."""
        model_config = ConfigDict(extra="forbid")
        success: bool

    type Result = CodeOpenResult
    result: Result | None = None

    class Error(BaseModel):
        """JSON-RPC 2.0 error object."""
        model_config = ConfigDict(extra="forbid")
        code: int
        message: str

        class ParseError(BaseModel):
            """Additional metadata for JSON-RPC parse errors."""
            model_config = ConfigDict(extra="forbid")
            doc: str
            pos: int

        type Data = ParseError
        data: Data | None = None

    error: Error | None = None

    @model_validator(mode="after")
    def _validate_result_xor_error(self) -> Self:
        if (self.result is None) == (self.error is None):
            raise ValueError(
                "JSON-RPC response must include exactly one of result or error"
            )
        return self


class RPCMethod(Protocol):
    """A type hint for a function object that can be used as an RPC method handler.

    Methods
    -------
    request() -> RPCRequest
        A method that forms an `RPCRequest` object with the appropriate method name and
        parameters, which will be serialized and sent to the host listener when the
        method is invoked.  The class instance can be used as a closure capturing any
        context needed to form the request.
    response(request: RPCRequest) -> RPCResponse
        A static method that handles an incoming `RPCRequest` produced by this class's
        `request()` method, and returns the appropriate `RPCResponse` to send back to
        the client.  This method will be registered as the handler for the RPC request
        returned by `request()`.
    """
    # pylint: disable=missing-function-docstring
    def request(self) -> RPCRequest: ...
    @staticmethod
    def response(request: RPCRequest) -> RPCResponse: ...


METHODS: dict[str, Callable[[RPCRequest], RPCResponse]] = {}


def rpc_method[RPCMethodT: RPCMethod](
    name: MethodName
) -> Callable[[type[RPCMethodT]], type[RPCMethodT]]:
    """Register an RPC method with the given name.

    Parameters
    ----------
    name : MethodName
        The name of the RPC method to register, which must be unique among registered
        methods.

    Returns
    -------
    Callable[[type[RPCMethodT]], type[RPCMethodT]]
        A class decorator that registers the decorated class as an RPC method handler,
        using the class's `response()` static method as the handler function.

    Raises
    ------
    ValueError
        If a method is already registered with the given name.
    """
    if name in METHODS:
        raise ValueError(f"RPC method already registered with name: {name}")

    def _decorator(cls: type[RPCMethodT]) -> type[RPCMethodT]:
        METHODS[name] = cls.response
        return cls

    return _decorator


def _throw_internal_error(err: RPCResponse.Error) -> NoReturn:
    raise RuntimeError(err.message)


def _throw_invalid_params(err: RPCResponse.Error) -> NoReturn:
    raise ValueError(err.message)


def _throw_invalid_request(err: RPCResponse.Error) -> NoReturn:
    raise TypeError(err.message)


def _throw_method_not_found(err: RPCResponse.Error) -> NoReturn:
    raise NotImplementedError(err.message)


def _throw_parse_error(err: RPCResponse.Error) -> NoReturn:
    if isinstance(err.data, RPCResponse.Error.ParseError):
        raise json.JSONDecodeError(err.message, doc=err.data.doc, pos=err.data.pos)
    raise TypeError(err.message)


def _throw_timeout_error(err: RPCResponse.Error) -> NoReturn:
    raise TimeoutError(err.message)


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
            data=RPCResponse.Error.ParseError(doc=err.doc, pos=err.pos)
        )
    return RPCResponse.Error(code=JSON_RPC_PARSE_ERROR, message=str(err))


def _catch_timeout_error(err: Exception) -> RPCResponse.Error:
    return RPCResponse.Error(code=JSON_RPC_TIMEOUT_ERROR, message=str(err))


JSON_RPC_THROW_ERR: Mapping[int, Callable[[RPCResponse.Error], NoReturn]] = {
    JSON_RPC_INTERNAL_ERROR: _throw_internal_error,
    JSON_RPC_INVALID_PARAMS: _throw_invalid_params,
    JSON_RPC_INVALID_REQUEST: _throw_invalid_request,
    JSON_RPC_METHOD_NOT_FOUND: _throw_method_not_found,
    JSON_RPC_PARSE_ERROR: _throw_parse_error,
    JSON_RPC_TIMEOUT_ERROR: _throw_timeout_error,
}
JSON_RPC_CATCH_ERR: Mapping[type[Exception], Callable[[Exception], RPCResponse.Error]] = {
    json.JSONDecodeError: _catch_parse_error,
    KeyError: _catch_method_not_found,
    NotImplementedError: _catch_method_not_found,
    RuntimeError: _catch_internal_error,
    TimeoutError: _catch_timeout_error,
    TimeoutExpired: _catch_timeout_error,
    TypeError: _catch_invalid_request,
    ValidationError: _catch_invalid_request,
    ValueError: _catch_invalid_params,
}


def _rpc_catch(err: Exception, *, request: RequestID | None) -> RPCResponse:
    handler = next(
        (handler for cls, handler in JSON_RPC_CATCH_ERR.items() if isinstance(err, cls)),
        _catch_internal_error
    )
    return RPCResponse(jsonrpc=JSON_RPC_VERSION, id=request, error=handler(err))


def _rpc_throw(err: RPCResponse.Error) -> NoReturn:
    handler = JSON_RPC_THROW_ERR.get(err.code, _throw_internal_error)
    handler(err)


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
            raise RuntimeError(f"socket path must be absolute: {self.path}")

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
            conn.settimeout(RPC_TIMEOUT)

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

    def _parse_request(self, line: str) -> JSONValue:
        if not line:
            raise TypeError("empty request")
        if len(line) > MAX_REQUEST_BYTES:
            raise TypeError("request exceeds maximum size")
        if not line.endswith("\n"):
            raise TypeError("request must be newline-terminated")
        text = line.removesuffix("\n").strip()
        if not text:
            raise TypeError("empty request")
        return json.loads(text)

    def _check_running_container(self, request: RPCRequest) -> None:
        container_id = request.params.container_id
        container_bin = self.container_bin()
        remaining = request.params.deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(
                "launch deadline exhausted before the RPC service could confirm the "
                "container is still running."
            )

        # inspect container using service's container runtime executable
        try:
            result = run(
                [
                    str(container_bin),
                    "container", "inspect",
                    "--format", "{{.State.Status}}",
                    container_id
                ],
                capture_output=True,
                timeout=remaining,
            )
        except TimeoutExpired as err:
            raise TimeoutError(
                f"timed out while checking status of container '{container_id}'"
            ) from err
        except CommandError as err:
            raise RuntimeError(
                f"container '{container_id}' is not available: {err}"
            ) from err

        # ensure running
        stdout = result.stdout.strip()
        stderr = result.stderr.strip()
        if result.returncode != 0:
            detail = stderr or stdout
            suffix = f": {detail}" if detail else ""
            raise RuntimeError(f"container '{container_id}' is not available{suffix}")
        if not stdout:
            raise RuntimeError(f"container '{container_id}' did not report a state")
        if stdout not in ("running", "restarting"):
            raise RuntimeError(
                f"container '{container_id}' is '{stdout}' (expected running)"
            )

    def _listen(self) -> None:
        """Serve requests indefinitely until `close()` is called."""
        self._ensure_socket()
        while True:
            # read a single line of input from the socket, blocking until a client
            # connects
            conn, line = self._read_line()
            try:
                try:
                    data = self._parse_request(line)
                    try:
                        request = RPCRequest.model_validate(data)
                        self._check_running_container(request)
                        response = METHODS[request.method](request)
                    except Exception as err:
                        request_id: str | None = None
                        if isinstance(data, Mapping):
                            _request_id = data.get("id")
                            if isinstance(_request_id, str):
                                request_id = _request_id
                        response = _rpc_catch(err, request=request_id)
                except Exception as err:
                    response = _rpc_catch(err, request=None)

                # send response back to client (may be an error code)
                try:
                    payload = json.dumps(
                        response.model_dump(mode="json", exclude_none=True),
                        separators=(",", ":")
                    ) + "\n"
                    conn.sendall(payload.encode("utf-8"))
                except OSError:
                    pass

            # close connection to client and return to listening for next request
            finally:
                conn.close()

    def _close(self) -> None:
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

    @staticmethod
    def _render_service(env_vars: dict[str, str]) -> str:
        rpc_bin = shutil.which("bertrand-rpc")
        if rpc_bin is None:
            raise RuntimeError(
                "'bertrand-rpc' executable not found on PATH.  Ensure that Bertrand's "
                "console scripts are installed and discoverable via PATH before "
                "interacting with the RPC server."
            )

        # render unit file with updated environment variables
        return f"""[Unit]
Description=Bertrand RPC Listener

[Service]
Type=simple
ExecStart={shlex.quote(str(rpc_bin))}
Restart=on-failure
RestartSec=1
{'\n'.join(f'Environment=\"{k}={v}\"' for k, v in env_vars.items())}

[Install]
WantedBy=default.target
"""

    @staticmethod
    def _service_reachable(*, deadline: float, interval: float) -> bool:
        if deadline <= 0.0:
            raise ValueError(f"deadline must be positive: {deadline}")
        if interval <= 0.0:
            raise ValueError(f"interval must be positive: {interval}")

        # spin while trying to connect to socket until timeout expires
        path = str(HOST_SOCKET)
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

    @staticmethod
    def start(
        ctx: Pipeline.InProgress,
        *,
        deadline: float,
        strict: bool,
        container_bin: Path,
        editor_bin: Path,
    ) -> bool:
        """Start the RPC systemd service if it is not already enabled.

        Parameters
        ----------
        ctx: Pipeline.InProgress
            The current pipeline context, which is used to record the atomic operations
            used to start the service.
        deadline: float
            The timestamp before which the service must be reachable in order to be
            considered successfully started.  This may be shared with other operations
            that are part of the startup process, in order to enforce an overall
            timeout on RPC commands.
        strict : bool
            If true, raise a hard OSError if the service fails to start or become
            reachable.  Otherwise, print a warning to stderr and return False.
        container_bin: Path
            The path to the container runtime executable (e.g. `docker` or `podman`),
            to be passed to the service environment.
        editor_bin: Path
            The path to the host text editor executable (e.g. `code`), to be passed to
            the service environment.

        Returns
        -------
        bool
            True if the service was started and became reachable, otherwise False.

        Raises
        ------
        RuntimeError
            If `strict` is true and we fail to render or write the unit file, reload
            the systemd daemon, start the service, or probe the socket for reachability.
        Exception
            If any other error occurs during this process and `strict` is true.

        Notes
        -----
        This function must be called on the host process before every (external)
        command that may touch the RPC service.  It should never be called inside
        the container context, since it passes host executable paths to the generated
        systemd service.
        """
        if inside_image():
            raise RuntimeError(
                "RPC service cannot be started from inside a container.  This should "
                "never occur; if you see this message, try re-entering the environment "
                "to regenerate the systemd unit file, or report an issue if the "
                "problem persists."
            )

        try:
            # render up-to-date systemd unit file
            text = Listener._render_service({
                CONTAINER_BIN_ENV: str(container_bin.expanduser().resolve()),
                EDITOR_BIN_ENV: str(editor_bin.expanduser().resolve()),
            })
            if (
                not RPC_SERVICE_FILE.exists() or
                RPC_SERVICE_FILE.read_text(encoding="utf-8") != text
            ):
                try:
                    ctx.do(WriteText(path=RPC_SERVICE_FILE, text=text, replace=None))
                except Exception as err:
                    raise RuntimeError(
                        f"failed to write systemd unit file at {RPC_SERVICE_FILE}\n{str(err)}"
                    ) from err
                try:
                    ctx.do(ReloadDaemon(user=True))
                except Exception as err:
                    raise RuntimeError(
                        "failed to reload systemd user daemon after writing unit file "
                        f"at {RPC_SERVICE_FILE}\n{str(err)}"
                    ) from err

            # start systemd service if it is not already running
            try:
                ctx.do(StartService(name=RPC_SERVICE_NAME, user=True), undo=False)
            except Exception as err:
                raise RuntimeError(
                    f"failed to start systemd user service '{RPC_SERVICE_NAME}'.  Check "
                    f"`systemctl --user status {RPC_SERVICE_NAME}` for details.\n{str(err)}"
                ) from err

            # wait until service is reachable
            try:
                if Listener._service_reachable(deadline=deadline, interval=0.1):
                    return True
            except Exception as err:
                raise RuntimeError(
                    f"failed to probe code RPC socket at {HOST_SOCKET} after starting "
                    f"service '{RPC_SERVICE_NAME}'.  Check `systemctl --user status "
                    f"{RPC_SERVICE_NAME}` for details.\n{str(err)}"
                ) from err

            # timed out
            raise RuntimeError(
                f"'{RPC_SERVICE_NAME}' socket at {HOST_SOCKET} did not become "
                "reachable within the allotted deadline.  Check "
                f"`systemctl --user status {RPC_SERVICE_NAME}` for details."
            )
        except Exception as err:
            if strict:
                raise
            print(
                f"warning: failed to reach the code RPC service\n"
                f"{str(err)}\n\n"
                "In-container RPC commands will fail until you exit and re-enter the "
                "environment.",
                file=sys.stderr
            )
            return False

    @staticmethod
    def container_bin() -> Path:
        """Get the path to the host container runtime executable being used by the RPC
        service environment, which is passed in upon creating the systemd unit file.

        Returns
        -------
        Path
            The absolute host path to the container runtime executable.

        Raises
        ------
        RuntimeError
             If the environment variable is missing or malformed, which should never
             happen if the systemd unit file is rendered correctly.
        """
        value = os.environ.get(CONTAINER_BIN_ENV)
        if value is None:
            raise RuntimeError(
                "systemd service could not locate the container executable (usually "
                f"indicates a corrupted {RPC_SERVICE_NAME} unit).  This should never "
                "occur; if you see this message, try re-entering the environment to "
                "regenerate the unit, or report an issue at if the problem persists."
            )
        candidate = Path(value)
        if not candidate.is_absolute():
            raise RuntimeError(f"{CONTAINER_BIN_ENV} must be an absolute path: {value}")
        return candidate.expanduser().resolve()

    @staticmethod
    def editor_bin() -> Path:
        """Get the path to the host text editor executable being used by the RPC
        service environment, which is passed in upon creating the systemd unit file.

        Returns
        -------
        Path
            The absolute host path to the text editor executable.

        Raises
        ------
        RuntimeError
             If the environment variable is missing or malformed, which should never
             happen if the systemd unit file is rendered correctly.
        """
        value = os.environ.get(EDITOR_BIN_ENV)
        if value is None:
            raise RuntimeError(
                "systemd service could not locate the editor executable (usually "
                f"indicates a corrupted {RPC_SERVICE_NAME} unit).  This should never "
                "occur; if you see this message, try re-entering the environment to "
                "regenerate the unit, or report an issue if the problem persists."
            )
        candidate = Path(value)
        if not candidate.is_absolute():
            raise RuntimeError(f"{EDITOR_BIN_ENV} must be an absolute path: {value}")
        return candidate.expanduser().resolve()


def main() -> None:
    """Entry point for the RPC service, which starts the server and begins listening
    for RPC requests.  This is exported as a script in `pyproject.toml` and invoked by
    the systemd unit file rendered in `Listener.start()`, which should be called on the
    host before any in-container commands can touch the RPC service.
    """
    # pylint: disable=protected-access
    try:
        server = Listener(path=HOST_SOCKET)
        try:
            server._listen()
        finally:
            server._close()
    except KeyboardInterrupt:
        return


def rpc(method: Callable[[], RPCRequest]) -> RPCResponse.Result:
    """Send a request to the host RPC listener and return the result, or raise an
    appropriate Python exception if the request fails or the listener is unavailable.

    Parameters
    ----------
    method : Method
        The method to invoke, which is a closure that takes no arguments and returns an
        `RPCRequest`, which will be serialized and sent over the wire.

    Returns
    -------
    JSONValue
        The `result` field from the JSON-RPC response, if the request was successful.

    Raises
    ------
    TypeError
        If the request or the response could not be parsed.
    json.JSONDecodeError
        If the request or response contained invalid JSON.
    NotImplementedError
        If the requested method is not recognized by the listener.
    ValueError
        If the request parameters were invalid for the chosen method.
    TimeoutError
        If the request times out before a response is received.
    RuntimeError
        If any other error occurred during request handling.
    """
    if not inside_image():
        raise RuntimeError(
            "RPC client cannot be used from the host environment.  This should never "
            "occur; if you see this message, try re-entering the environment to "
            "regenerate the systemd unit file, or report an issue if the problem "
            "persists."
        )
    if os.environ.get(SOCKET_ENV, "") != "1":
        raise RuntimeError(
            "Code server not available.  Please exit the environment and re-enter "
            "to restart the RPC service."
        )

    # form and serialize JSON-RPC request
    request = method()
    serial = json.dumps(request.model_dump(mode="json"), separators=(",", ":")) + "\n"
    timeout = request.params.deadline - time.monotonic()
    if timeout <= 0:
        raise TimeoutError(
            f"deadline exhausted before '{request.method}' RPC request could be sent"
        )

    # send serialized request to socket
    path = CONTAINER_SOCKET.expanduser()
    if not path.is_absolute():
        raise RuntimeError(f"RPC socket path must be absolute: {path}")
    if not path.exists():
        raise RuntimeError(f"RPC socket does not exist: {path}")
    if not path.is_socket():
        raise RuntimeError(f"RPC socket path is not a socket: {path}")
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    client.settimeout(timeout)
    try:
        client.connect(str(path))
        client.sendall(serial.encode("utf-8"))
        reader = client.makefile("r", encoding="utf-8", newline="\n")
        try:
            line = reader.readline(MAX_REQUEST_BYTES + 1)
        finally:
            reader.close()  # close socket file wrapper
    finally:
        client.close()  # close client socket

    # parse + validate response line
    if not line:
        raise TypeError("empty response from RPC server")
    if len(line) > MAX_REQUEST_BYTES:
        raise TypeError("RPC response exceeds maximum size")
    if not line.endswith("\n"):
        raise TypeError("RPC response must be newline-terminated")
    text = line.removesuffix("\n").strip()
    if not text:
        raise TypeError("empty response from RPC server")
    response = RPCResponse.model_validate(json.loads(text))
    if response.id is not None and response.id != request.id:
        raise RuntimeError(
            f"RPC response id mismatch: expected {request.id!r}, got {response.id!r}"
        )

    # handle errors
    if response.error is not None:
        _rpc_throw(response.error)
    if response.result is None:
        raise RuntimeError("RPC response missing result")
    return response.result


####################
####    CODE    ####
####################


# TODO: vscode recommended extensions are currently hardcoded in the generated
# workspace file, but `config.py` should have a more general mechanism for recommending
# editor extensions based on which tools are actually present in the container.
# -> This would be a good thing to tackle during the MCP refactor, and I should
# generally make sure that the editor integration is solid by the time I complete that.
# Just like the container runtime, it may require future edits to `config.py` to
# support everything.


CODE_OPEN_TIMEOUT: float = 30.0
CODE_OPEN_METHOD: MethodName = "code.open"
VSCODE_REMOTE_EXTENSION = "ms-vscode-remote.remote-containers"


def _vscode_open_prereqs(method: CodeOpen, config: Config) -> None:
    _ = config

    # check for managed workspace file
    if not VSCODE_WORKSPACE_FILE.exists() or not VSCODE_WORKSPACE_FILE.is_file():
        raise RuntimeError(
            "VSCode workspace file not found at expected container path: "
            f"{VSCODE_WORKSPACE_FILE}\nThis file should be automatically created as a "
            "configuration artifact.  If you see this message, try re-running the "
            "`$ bertrand code` command to regenerate the workspace file, or report an "
            "issue if the problem persists."
        )

    # check for mounted tools and warn if any are missing
    expired = False
    for tool, hint in (
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
        ("bertrand-mcp", "MCP server integration may be unavailable in this editor session."),
    ):
        remaining = method.deadline - time.monotonic()
        try:
            if remaining <= 0:
                expired = True
            else:
                run(["which", tool], capture_output=True, timeout=remaining)
        except CommandError as err:
            warn(f"{str(err)}\n\t{hint}", UserWarning)
        except TimeoutExpired:
            expired = True
        if expired:
            raise TimeoutError(
                "deadline exhausted before the RPC service could confirm the presence "
                f"of '{tool}' inside the container context"
            )


def _vscode_open(params: RPCRequest.CodeOpenRequest) -> RPCResponse.CodeOpenResult:
    editor_bin = Listener.editor_bin()
    remaining = params.deadline - time.monotonic()
    expired = False

    # check for required remote containers extension on host vscode
    try:
        if remaining <= 0:
            expired = True
        else:
            result = run(
                [str(editor_bin), "--list-extensions"],
                capture_output=True,
                timeout=remaining
            )
            found = False
            search = VSCODE_REMOTE_EXTENSION.lower()
            for ext in result.stdout.splitlines():
                if ext.strip().lower() == search:
                    found = True
                    break
            if not found:
                raise RuntimeError(
                    f"required VSCode extension is missing: '{VSCODE_REMOTE_EXTENSION}'"
                )
    except TimeoutExpired:
        expired = True
    if expired:
        raise TimeoutError(
            "deadline exhausted before the RPC service could confirm the presence of "
            "required VSCode extensions"
        )

    # open editor in detached (non-blocking) process
    remaining = params.deadline - time.monotonic()
    try:
        if remaining <= 0:
            expired = True
        else:
            # NOTE: this URI allows the VSCode Remote Containers extension to attach to
            # a running container by ID, but is not technically part of the public API.
            # It is well-documented in various issues and discussions, but may be
            # subject to change without notice.  If this breaks, it should have been
            # replaced by something more stable that we can use instead.
            uri = (
                "vscode-remote://attached-container+"
                f"{urllib.parse.quote(params.container_id, safe='')}"
                f"{urllib.parse.quote(str(VSCODE_WORKSPACE_FILE), safe='/')}"
            )
            subprocess.Popen(
                [str(editor_bin), "--file-uri", uri],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                start_new_session=True,
            )
    except OSError as err:
        raise RuntimeError(
            f"failed to launch VS Code container attach session: {err}",
        ) from err
    if expired:
        raise TimeoutError(
            "deadline exhausted before the RPC service could launch the VSCode editor"
        )

    return RPCResponse.CodeOpenResult(success=True)


CODE_OPEN_PREREQS: dict[str, Callable[[CodeOpen, Config], None]] = {
    "vscode": _vscode_open_prereqs,
}
CODE_OPEN: dict[str, Callable[[RPCRequest.CodeOpenRequest], RPCResponse.CodeOpenResult]] = {
    "vscode": _vscode_open,
}


@rpc_method(CODE_OPEN_METHOD)
@dataclass(frozen=True)
class CodeOpen:
    """A request object for the `code.open` RPC method, which opens a text editor on
    the host system, pointed at a given container workspace.
    """
    deadline: float = field(default_factory=lambda: time.monotonic() + CODE_OPEN_TIMEOUT)

    def request(self) -> RPCRequest:
        """Form a `code.open` RPC request on the client side.

        Returns
        -------
        RPCRequest
            The request to send to the host listener.

        Raises
        ------
        RuntimeError
            If the project configuration cannot be loaded or is missing required
            fields.
        """
        # load host worktree path from environment
        _worktree = os.environ.get(WORKTREE_ENV)
        if _worktree is None:
            raise RuntimeError(
                "worktree environment variable is missing.  This should never "
                "occur; if you see this message, try re-entering the environment to "
                "regenerate its environment variables, or report an issue if the "
                "problem persists."
            )
        worktree = Path(_worktree.strip())
        if not worktree.is_absolute():
            raise RuntimeError(f"worktree path must be absolute: {worktree}")
        worktree = worktree.expanduser().resolve()

        # load current container id from environment
        container_id = os.environ.get(CONTAINER_ID_ENV)
        if container_id is None:
            raise RuntimeError(
                "container ID environment variable is missing.  This should never "
                "occur; if you see this message, try re-entering the environment to "
                "regenerate its environment variables, or report an issue if the "
                "problem persists."
            )
        container_id = container_id.strip()

        # load current image tag from environment
        image_tag = os.environ.get(IMAGE_TAG_ENV)
        if image_tag is None:
            raise RuntimeError(
                "image tag environment variable is missing.  This should never "
                "occur; if you see this message, try re-entering the environment to "
                "reset its environment variables, or report an issue if the problem "
                "persists."
            )

        # load editor selection from worktree config
        with Config.load(WORKTREE_MOUNT) as config:
            config.sync(image_tag)
            if not config.bertrand:
                raise RuntimeError(
                    f"Bertrand configuration is missing from the worktree config at "
                    f"{worktree}.  This should never occur; if you see this message, "
                    "try re-running `bertrand init` to regenerate your project "
                    "configuration, or report an issue if the problem persists."
                )
            editor = config.bertrand.editor

            # run editor-specific prechecks inside container context
            CODE_OPEN_PREREQS[editor](self, config)

        return RPCRequest(
            jsonrpc=JSON_RPC_VERSION,
            id=uuid.uuid4().hex,
            method=CODE_OPEN_METHOD,
            params=RPCRequest.CodeOpenRequest(
                worktree=worktree,
                container_id=container_id,
                editor=editor,
                deadline=self.deadline,
            )
        )

    @staticmethod
    def response(request: RPCRequest) -> RPCResponse:
        """Handle the `code.open` RPC request on the host side.

        Parameters
        ----------
        request : RPCRequest
            A request produced by this class's `__call__` operator.

        Returns
        -------
        RPCResponse
            The response to send back to the client.
        """
        return RPCResponse(
            jsonrpc=JSON_RPC_VERSION,
            id=request.id,
            result=CODE_OPEN[request.params.editor](request.params),
        )
