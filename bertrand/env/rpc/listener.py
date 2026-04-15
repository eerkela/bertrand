"""A host sidecar process that runs alongside a containerized workload and processes
JSON-RPC requests from it over an asynchronous unix socket.  The RPC service is used to
remotely invoke host utilities (such as text editors), which would otherwise be
difficult to launch from inside the container context.

This module defines the core of Bertrand's RPC infrastructure, including the main
`Listener`, `Request`, and `Response` classes, as well as an `@rpc_method` decorator
that registers new RPC method handlers.  The `rpc()` function can be called to send a
request to the listener service and receive the response.  The RPC service itself will
be launched via the `main()` function.
"""
from __future__ import annotations

import argparse
import asyncio
import json
import stat
import time
from collections.abc import Awaitable, Callable, Mapping, Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Annotated, Literal, NoReturn, Protocol, Self, cast

from pydantic import (
    AfterValidator,
    BaseModel,
    ConfigDict,
    PositiveFloat,
    ValidationError,
    model_validator,
)

from ..config.bertrand import Editor
from ..config.core import AbsolutePath
from ..run import (
    CONTAINER_SOCKET,
    JSONValue,
    TimeoutExpired,
    inside_image,
    run,
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
RPC_TIMEOUT: float = 30.0
RPC_WATCHDOG_INTERVAL: float = 10.0


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


def _check_worktree(worktree: AbsolutePath) -> AbsolutePath:
    if not worktree.exists():
        raise ValueError(f"worktree does not exist: {worktree}")
    if not worktree.is_dir():
        raise ValueError(f"worktree must be a directory: {worktree}")
    return worktree.expanduser().resolve()


type ContainerID = Annotated[  # pylint: disable=invalid-name
    str,
    AfterValidator(_check_container_id),
]
type JSONRPCVersion = Literal["2.0"]
type MethodName = Annotated[str, AfterValidator(_check_method_name)]
type RequestID = Annotated[  # pylint: disable=invalid-name
    str,
    AfterValidator(_check_request_id),
]
type WorktreePath = Annotated[AbsolutePath, AfterValidator(_check_worktree)]


class RPCRequest(BaseModel):
    """A validated JSON-RPC 2.0 request to Bertrand's host sidecar."""
    model_config = ConfigDict(extra="forbid")
    jsonrpc: JSONRPCVersion
    id: RequestID
    method: MethodName

    class CodeOpenRequest(BaseModel):
        """Typed params payload for `code.open` JSON-RPC requests."""
        model_config = ConfigDict(extra="forbid")
        worktree: WorktreePath
        editor: Editor
        deadline: PositiveFloat
        block: bool

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
    response(listener: Listener, request: RPCRequest) -> RPCResponse
        A static method that handles an incoming `RPCRequest` produced by this class's
        `request()` method, and returns the appropriate `RPCResponse` to send back to
        the client.  This method will be registered as the handler for the RPC request
        returned by `request()`.
    """
    # pylint: disable=missing-function-docstring
    async def request(self) -> RPCRequest: ...
    @staticmethod
    async def response(listener: Listener, request: RPCRequest) -> RPCResponse: ...


METHODS: dict[str, Callable[[Listener, RPCRequest], Awaitable[RPCResponse]]] = {}


def rpc_method[T: RPCMethod](name: MethodName) -> Callable[[type[T]], type[T]]:
    """Register an RPC method with the given name.

    Parameters
    ----------
    name : MethodName
        The name of the RPC method to register, which must be unique among registered
        methods.

    Returns
    -------
    Callable[[type[T]], type[T]]
        A class decorator that registers the decorated class as an RPC method handler,
        using the class's `response()` static method as the handler function.

    Raises
    ------
    ValueError
        If a method is already registered with the given name.
    """
    if name in METHODS:
        raise ValueError(f"RPC method already registered with name: {name}")

    def _decorator(cls: type[T]) -> type[T]:
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
            data=RPCResponse.Error.ParseError(doc=err.doc, pos=err.pos),
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
    asyncio.TimeoutError: _catch_timeout_error,
    json.JSONDecodeError: _catch_parse_error,
    KeyError: _catch_method_not_found,
    NotImplementedError: _catch_method_not_found,
    RuntimeError: _catch_internal_error,
    TimeoutError: _catch_timeout_error,
    TimeoutExpired: _catch_timeout_error,
    UnicodeError: _catch_parse_error,
    TypeError: _catch_invalid_request,
    ValidationError: _catch_invalid_request,
    ValueError: _catch_invalid_params,
}


def _rpc_catch(err: Exception, *, request: str | None) -> RPCResponse:
    request_id: str | None = None
    if request is not None:
        try:
            request_id = _check_request_id(request)
        except Exception:
            request_id = None
    handler = next(
        (handler for cls, handler in JSON_RPC_CATCH_ERR.items() if isinstance(err, cls)),
        _catch_internal_error,
    )
    return RPCResponse(jsonrpc=JSON_RPC_VERSION, id=request_id, error=handler(err))


def _rpc_throw(err: RPCResponse.Error) -> NoReturn:
    handler = JSON_RPC_THROW_ERR.get(err.code, _throw_internal_error)
    handler(err)


@dataclass
class Listener:
    """A minimal, host-side listener sidecar process that handles JSON-RPC requests
    from in-container CLI commands over a Unix socket, which is mounted as part of
    container creation.

    Currently, the only supported request is `code.open`, which is used to launch a
    host text editor pointed at the container's `WORKTREE_MOUNT`.  Future requests may
    be added to allow other host-side operations that are not easily performed from the
    container context, such as GUI applications or (restricted) filesystem access.

    Attributes
    ----------
    container_id : ContainerID
        The unique OCI container ID of the attached container, used to verify liveness
        of the container before processing any requests.
    container_bin : AbsolutePath
        The absolute path to the host container runtime executable, used in combination
        with the container ID to interact with the container while processing requests.
    socket_path : AbsolutePath
        The path to the host's Unix socket file.  This path must be absolute, and the
        code server will only instantiate a socket at this location when its `listen()`
        method is called.
    """
    container_id: ContainerID
    container_bin: AbsolutePath
    socket_path: AbsolutePath
    _server: asyncio.AbstractServer | None = field(default=None, repr=False)
    _watchdog_task: asyncio.Task[None] | None = field(default=None, repr=False)
    _active_blocking_requests: int = field(default=0, repr=False)
    _blocking_idle: asyncio.Event = field(init=False, repr=False)

    def __post_init__(self) -> None:
        self.container_id = _check_container_id(self.container_id)

        # validate paths
        if not self.container_bin.is_absolute():
            raise RuntimeError(
                f"container binary path must be absolute: {self.container_bin}"
            )
        if not self.socket_path.is_absolute():
            raise RuntimeError(f"socket path must be absolute: {self.socket_path}")
        self.container_bin = self.container_bin.expanduser().resolve()
        self.socket_path = self.socket_path.expanduser().resolve()

        # set up blocking request tracking, which is used to delay sidecar shutdown
        # until all blocking requests have completed
        self._blocking_idle = asyncio.Event()
        self._blocking_idle.set()

    async def _wait_for_blocking_requests(self) -> None:
        while self._active_blocking_requests > 0:
            await self._blocking_idle.wait()

    async def _container_state(self, *, timeout: float | None = None) -> str | None:
        result = await run(
            [
                str(self.container_bin),
                "container",
                "inspect",
                "--format",
                "{{.State.Status}}",
                self.container_id,
            ],
            check=False,
            capture_output=True,
            timeout=timeout,
        )
        if result.returncode != 0:
            return None
        state = result.stdout.strip()
        if not state:
            return None
        return state

    async def _container_watchdog(self) -> None:
        while True:
            alive = False
            try:
                state = await self._container_state(timeout=RPC_TIMEOUT)
                alive = state in ("running", "restarting")
            except Exception:
                alive = False
            if not alive:
                if self._server is not None:
                    self._server.close()
                return
            await asyncio.sleep(RPC_WATCHDOG_INTERVAL)

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

    async def _check_running_container(self, request: RPCRequest) -> None:
        timeout = request.params.deadline - time.time()
        state = await self._container_state(timeout=timeout)
        if state is None:
            raise RuntimeError(
                f"container '{self.container_id}' is not available"
            )
        if state not in ("running", "restarting"):
            raise RuntimeError(
                f"container '{self.container_id}' is '{state}' (expected running)"
            )

    async def _handle_client(
        self,
        reader: asyncio.StreamReader,
        writer: asyncio.StreamWriter,
    ) -> None:
        request_id: str | None = None
        blocked = False
        try:
            try:
                # read line and eagerly extract ID for diagnostic purposes, if possible
                line = await asyncio.wait_for(reader.readline(), timeout=RPC_TIMEOUT)
                data = self._parse_request(line.decode("utf-8"))
                if isinstance(data, Mapping):
                    _request_id = cast(Mapping[JSONValue, JSONValue], data).get("id")
                    if isinstance(_request_id, str):
                        request_id = _request_id

                # validate rest of the request and ensure attached container is running
                request = RPCRequest.model_validate(data)
                await self._check_running_container(request)

                # record blocking requests
                blocked = request.params.block
                if blocked:
                    self._active_blocking_requests += 1
                    self._blocking_idle.clear()

                # dispatch to handler
                response = await METHODS[request.method](self, request)
            except Exception as err:
                response = _rpc_catch(err, request=request_id)
            finally:
                if blocked:
                    self._active_blocking_requests = max(
                        0,
                        self._active_blocking_requests - 1,
                    )
                    if self._active_blocking_requests == 0:
                        self._blocking_idle.set()

            # send response (may be an error)
            try:
                payload = json.dumps(
                    response.model_dump(mode="json", exclude_none=True),
                    separators=(",", ":"),
                ) + "\n"
                writer.write(payload.encode("utf-8"))
                await writer.drain()
            except OSError:
                pass

        # close stream connections
        finally:
            writer.close()
            try:
                await writer.wait_closed()
            except OSError:
                pass

    async def listen(self) -> None:
        """Begin serving requests over the RPC socket.  This is meant to be called from
        the main entry point of the RPC service, which is invoked as a sidecar
        whenever a container that may require RPC communication is started.  This
        method will block indefinitely while the service is running, and will return
        when the service is stopped or interrupted.  The socket file will be created
        when the service starts and removed when it stops.

        Raises
        ------
        OSError
            If there was an error creating or cleaning up the socket file.
        """
        try:
            # create socket
            self.socket_path.parent.mkdir(parents=True, exist_ok=True)
            if self.socket_path.exists():
                mode = self.socket_path.lstat().st_mode
                if not stat.S_ISSOCK(mode):
                    raise OSError(f"socket path occupied: {self.socket_path}")
                self.socket_path.unlink(missing_ok=True)

            # start async server to handle concurrent requests
            self._server = await asyncio.start_unix_server(
                self._handle_client,
                path=str(self.socket_path),
                limit=MAX_REQUEST_BYTES + 1,
            )
            self.socket_path.chmod(0o600)
            self._watchdog_task = asyncio.create_task(self._container_watchdog())
            try:
                await self._server.serve_forever()
            except asyncio.CancelledError:
                pass

        finally:
            # shut down server to stop accepting new requests
            if self._server is not None:
                self._server.close()
                await self._server.wait_closed()
                self._server = None

            # cancel container liveness watchdog
            if self._watchdog_task is not None:
                self._watchdog_task.cancel()
                try:
                    await self._watchdog_task
                except asyncio.CancelledError:
                    pass
                self._watchdog_task = None

            # wait until all blocking requests have completed
            await asyncio.shield(self._wait_for_blocking_requests())

            # clean up socket
            try:
                if (
                    self.socket_path.exists()
                    and stat.S_ISSOCK(self.socket_path.lstat().st_mode)
                ):
                    self.socket_path.unlink(missing_ok=True)
            except OSError:
                pass


def main(argv: Sequence[str] | None = None) -> None:
    """Entry point for the RPC service, which starts the sidecar and begins listening
    for RPC requests from the attached container.

    Parameters
    ----------
    argv : Sequence[str], optional
        Command-line arguments to start the listener.  If not provided, defaults to
        `sys.argv`.
    """
    # define argv parser
    parser = argparse.ArgumentParser(
        prog="bertrand-rpc",
        description="Bertrand host-side RPC sidecar listener",
    )
    parser.add_argument("--socket", required=True, help="absolute host RPC socket path")
    parser.add_argument("--container-id", required=True, help="target running container id")
    parser.add_argument(
        "--container-bin",
        required=True,
        help="absolute path to host container runtime executable",
    )

    # parse arguments
    args = parser.parse_args(argv)

    # construct listener, and start serving requests until interrupted
    listener = Listener(
        socket_path=Path(args.socket),
        container_id=args.container_id,
        container_bin=Path(args.container_bin),
    )
    try:
        asyncio.run(listener.listen())
    except KeyboardInterrupt:
        return


async def rpc(method: RPCMethod) -> RPCResponse.Result:
    """Send a request to the host RPC listener and return the result, or raise an
    appropriate Python exception if the request fails or the listener is unavailable.

    Parameters
    ----------
    method : RPCMethod
        The method to invoke, which is a protocol dataclass with a `request()` method
        that takes no arguments and returns an `RPCRequest`, which will be serialized
        and sent over the wire.

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
            "regenerate the sidecar service, or report an issue if the problem "
            "persists."
        )

    # validate container-side rpc socket from bootstrapped runtime dir
    if not CONTAINER_SOCKET.exists():
        raise RuntimeError(f"RPC socket does not exist: {CONTAINER_SOCKET}")
    if not stat.S_ISSOCK(CONTAINER_SOCKET.lstat().st_mode):
        raise RuntimeError(
            f"RPC socket path does not point to a valid socket: {CONTAINER_SOCKET}"
        )

    # form request, then serialize to newline-delimited JSON
    request = await method.request()
    serial = json.dumps(request.model_dump(mode="json"), separators=(",", ":")) + "\n"
    remaining = request.params.deadline - time.time()
    if remaining <= 0:
        raise TimeoutError(
            f"deadline exhausted before '{request.method}' RPC request could be sent"
        )
    try:
        # asynchronously connect to socket
        reader, writer = await asyncio.wait_for(
            asyncio.open_unix_connection(str(CONTAINER_SOCKET), limit=MAX_REQUEST_BYTES + 1),
            timeout=max(0.001, remaining),
        )
    except TimeoutError as err:
        raise TimeoutError(
            f"deadline exhausted before '{request.method}' RPC request could connect"
        ) from err
    try:
        # send request over socket
        remaining = request.params.deadline - time.time()
        if remaining <= 0:
            raise TimeoutError(
                f"deadline exhausted before '{request.method}' RPC request could be sent"
            )
        writer.write(serial.encode("utf-8"))
        await asyncio.wait_for(writer.drain(), timeout=max(0.001, remaining))

        # read response line from socket
        remaining = request.params.deadline - time.time()
        if remaining <= 0:
            raise TimeoutError(
                f"deadline exhausted before '{request.method}' RPC response could be read"
            )
        line_bytes = await asyncio.wait_for(
            reader.readline(),
            timeout=max(0.001, remaining),
        )
    except TimeoutError as err:
        raise TimeoutError(
            f"deadline exhausted while processing '{request.method}' RPC request"
        ) from err
    finally:
        # close socket connection
        writer.close()
        try:
            await writer.wait_closed()
        except OSError:
            pass

    # parse + validate response line
    line = line_bytes.decode("utf-8")
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
