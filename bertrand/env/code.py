"""Text editor integrations for Bertrand's containerized development environments.

This module currently provides a minimal host-side RPC listener skeleton that can be
used by future in-container `bertrand code` clients.
"""
from __future__ import annotations

import json
import shutil
import socket
import stat
import subprocess
import urllib.parse

from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
from typing import Any

from pydantic import BaseModel, ConfigDict, ValidationError

from .run import User, atomic_write_text, mkdir_private, run

# pylint: disable=broad-exception-caught


CODE_SOCKET_VERSION: int = 1
CODE_SOCKET_OP_OPEN: str = "open_editor"
CODE_SOCKET_DIR: Path = User().home / ".local" / "share" / "bertrand" / "code-rpc"
CODE_SOCKET: Path = CODE_SOCKET_DIR / "listener.sock"
MAX_REQUEST_BYTES: int = 1024 * 1024  # 1 MiB
CONTAINER_SOCKET_DIR: Path = Path("/run/bertrand/code-rpc")
CONTAINER_SOCKET: Path = CONTAINER_SOCKET_DIR / "listener.sock"
SOCKET_ENV: str = "BERTRAND_CODE_SOCKET"
HOST_ENV: str = "BERTRAND_HOST_ENV"
CONTAINER_ID_ENV: str = "BERTRAND_CONTAINER_ID"
WORKSPACE_ENV: str = "BERTRAND_WORKSPACE"
WORKSPACE_MOUNT: str = "/env"
VSCODE_REMOTE_EXTENSION = "ms-vscode-remote.remote-containers"
VSCODE_MANAGED_SETTINGS: dict[str, Any] = {
    "C_Cpp.intelliSenseEngine": "disabled",
    "clangd.path": "clangd",
    "clangd.arguments": [
        "--background-index",
        "--clang-tidy",
        "--completion-style=detailed",
        "--header-insertion=iwyu",
    ],
}
EDITORS: dict[str, tuple[str, ...]] = {
    "vscode": ("code",),
}


class LaunchError(OSError):
    """A structured error used to keep RPC failure categories stable."""

    def __init__(self, category: str, message: str) -> None:
        self.category = category
        self.message = message
        super().__init__(f"{category}: {message}")


def _prepare_socket(path: Path) -> Path:
    path = path.expanduser().resolve()
    mkdir_private(path.parent)
    if not path.exists():
        return path

    mode = path.lstat().st_mode
    if not stat.S_ISSOCK(mode):
        raise OSError(f"socket path occupied: {path}")
    path.unlink(missing_ok=True)
    return path


def _parse_response_line(line: str) -> CodeServer.Response:
    if not line:
        raise OSError("empty response from RPC server")
    if len(line) > MAX_REQUEST_BYTES:
        raise OSError("RPC response exceeds maximum size")
    if not line.endswith("\n"):
        raise OSError("RPC response must be newline-terminated")

    # strip newline delimiter and whitespace
    text = line.removesuffix("\n").strip()
    if not text:
        raise OSError("empty response from RPC server")

    # parse JSON
    try:
        payload = json.loads(text)
    except json.JSONDecodeError as err:
        raise OSError(f"malformed RPC response: {err.msg}") from err
    if not isinstance(payload, dict):
        raise OSError("RPC response must be a JSON object")

    # validate schema
    try:
        return CodeServer.Response.model_validate(payload)
    except ValidationError as err:
        raise OSError(f"invalid RPC response: {err}") from err


def send_request(
    request: CodeServer.Request,
    *,
    socket_path: Path = CODE_SOCKET,
    timeout: float = 10.0,
) -> CodeServer.Response:
    """Send a single request to the code RPC server and wait for a single response.

    Parameters
    ----------
    request : CodeServer.Request
        The request to send to the server.  This will be serialized as JSON and sent as
        a single line of text, terminated by a newline character.
    socket_path : Path, optional
        The path to the server's Unix socket file.  Defaults to `CODE_SOCKET`, which is
        a host path to the socket file.  If executing in-container, this should be
        replaced with `CODE_CONTAINER_SOCKET`, which is the corresponding path to the
        bind-mounted socket file inside the container context.
    timeout : float, optional
        The maximum number of seconds to wait for a response from the server before
        raising a timeout error.  Defaults to 10 seconds.

    Returns
    -------
    CodeServer.Response
        The response from the server, parsed from JSON and validated against the
        `CodeServer.Response` schema.

    Raises
    ------
    OSError
        If there is an error connecting to the server, sending the request, or if the
        response is malformed or indicates an error.
    """
    path = socket_path.expanduser()
    if not path.is_absolute():
        raise OSError(f"RPC socket path must be absolute: {path}")

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

    # close client socket and return response
    finally:
        client.close()

    return _parse_response_line(line)


def request_open_editor(
    *,
    editor: str,
    env_root: Path,
    container_id: str,
    container_workspace: str = WORKSPACE_MOUNT,
    socket_path: Path = CODE_SOCKET,
    timeout: float = 10.0,
) -> None:
    """Request that the host-side RPC server launch an editor for `env_root`.

    Parameters
    ----------
    editor : str
        The editor to launch.  This should be a key in the `EDITORS` dictionary, which
        maps editor names to command-line invocation tuples.
    env_root : Path
        The root directory of the environment to open in the editor.  This should be an
        absolute path to a directory on the host filesystem, which will become the
        current working directory of the resulting editor process.  If executing
        in-container, this can be obtained from the `BERTRAND_HOST_ENV` environment
        variable, which is set by `bertrand enter` when the container shell is opened.
    container_id : str
        The target runtime container ID that VS Code should attach to.
    container_workspace : str, optional
        Absolute workspace path inside the container context.  Defaults to `/env`.
    socket_path : Path, optional
        The path to the server's Unix socket file.  Defaults to `CODE_SOCKET`, which
        is a host path to the socket file.  If executing in-container, this should be
        replaced with `CODE_CONTAINER_SOCKET`, which is the corresponding path to the
        bind-mounted socket file inside the container context.
    timeout : float, optional
        The maximum number of seconds to wait for a response from the server before
        raising a timeout error.  Defaults to 10 seconds.

    Raises
    ------
    OSError
        If `env_root` is not an absolute path, or if there is an error processing the
        RPC request.
    """
    if not env_root.is_absolute():
        raise OSError(f"env_root must be absolute: {env_root}")
    if not container_id.strip():
        raise OSError("container_id must be non-empty")
    if not PurePosixPath(container_workspace).is_absolute():
        raise OSError(f"container_workspace must be absolute: {container_workspace}")

    response = send_request(
        CodeServer.Request(
            version=CODE_SOCKET_VERSION,
            op=CODE_SOCKET_OP_OPEN,
            editor=editor,
            env_root=str(env_root),
            container_id=container_id,
            container_workspace=container_workspace,
        ),
        socket_path=socket_path,
        timeout=timeout,
    )
    if not response.ok:
        raise OSError(f"RPC request failed: {response.error}")


def _require_executable(name: str) -> str:
    executable = shutil.which(name)
    if executable is None:
        raise LaunchError("prereq_missing", f"required executable '{name}' not found on PATH")
    return executable


def _normalize_workspace(container_workspace: str) -> str:
    workspace = PurePosixPath(container_workspace.strip() or WORKSPACE_MOUNT)
    if not workspace.is_absolute():
        raise LaunchError(
            "invalid_request",
            f"container_workspace must be absolute: '{container_workspace}'",
        )
    return str(workspace)


def _ensure_running_container(podman_bin: str, container_id: str) -> None:
    result = run(
        [podman_bin, "container", "inspect", "--format", "{{.State.Status}}", container_id],
        check=False,
        capture_output=True,
    )
    stdout = result.stdout.strip()
    stderr = result.stderr.strip()
    if result.returncode != 0:
        detail = stderr or stdout
        suffix = f": {detail}" if detail else ""
        raise LaunchError(
            "container_not_found",
            f"container '{container_id}' is not available{suffix}",
        )
    if not stdout:
        raise LaunchError(
            "container_not_found",
            f"container '{container_id}' did not report a state",
        )
    if stdout not in {"running", "restarting"}:
        raise LaunchError(
            "container_not_running",
            f"container '{container_id}' is '{stdout}' (expected running)",
        )


def _resolve_container_clangd(podman_bin: str, container_id: str) -> str:
    result = run(
        [podman_bin, "exec", container_id, "sh", "-lc", "command -v clangd"],
        check=False,
        capture_output=True,
    )
    stdout = result.stdout.strip()
    stderr = result.stderr.strip()
    if result.returncode != 0 or not stdout:
        detail = stderr or stdout
        suffix = f": {detail}" if detail else ""
        raise LaunchError(
            "clangd_missing",
            f"clangd is not available in container '{container_id}'{suffix}",
        )
    return stdout


def _ensure_vscode_extension(code_bin: str) -> None:
    result = run([code_bin, "--list-extensions"], check=False, capture_output=True)
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        suffix = f": {detail}" if detail else ""
        raise LaunchError(
            "prereq_missing",
            f"failed to query VS Code extensions{suffix}",
        )
    extensions = {
        ext.strip().lower()
        for ext in result.stdout.splitlines()
        if ext.strip()
    }
    if VSCODE_REMOTE_EXTENSION.lower() not in extensions:
        raise LaunchError(
            "prereq_missing",
            f"required VS Code extension is missing: {VSCODE_REMOTE_EXTENSION}",
        )


def _ensure_workspace_settings(env_root: Path) -> None:
    settings_path = env_root / ".vscode" / "settings.json"
    current: dict[str, Any] = {}

    # if settings.json already exists, only modify the keys we manage and preserve any
    # user-managed settings and comments as much as possible
    if settings_path.exists():
        if not settings_path.is_file():
            raise LaunchError(
                "config_write_failure",
                f"settings path is not a file: {settings_path}",
            )
        try:
            text = settings_path.read_text(encoding="utf-8")
        except OSError as err:
            raise LaunchError(
                "config_write_failure",
                f"failed to read workspace settings: {settings_path}: {err}",
            ) from err

        # parse JSON (conservative)
        if text.strip():
            try:
                parsed = json.loads(text)
            except json.JSONDecodeError as err:
                raise LaunchError(
                    "config_write_failure",
                    f"invalid JSON in workspace settings: {settings_path}: {err.msg}",
                ) from err
            if not isinstance(parsed, dict):
                raise LaunchError(
                    "config_write_failure",
                    f"workspace settings must contain a JSON object: {settings_path}",
                )
            current = parsed

    # merge the new keys
    merged = dict(current)
    changed = not settings_path.exists()
    for key, value in VSCODE_MANAGED_SETTINGS.items():
        if merged.get(key) != value:
            merged[key] = value
            changed = True
    if not changed:
        return

    # write the merged settings back to disk, overwriting the existing file
    text = json.dumps(merged, indent=2, sort_keys=True) + "\n"
    try:
        atomic_write_text(settings_path, text, encoding="utf-8")
    except OSError as err:
        raise LaunchError(
            "config_write_failure",
            f"failed to write workspace settings: {settings_path}: {err}",
        ) from err


def _vscode_folder_uri(container_id: str, container_workspace: str) -> str:
    # NOTE: this URI endpoint allows VSCode to manually attach to the running container
    # context, but is not technically a public API and could potentially break in
    # future versions of the Remote Containers extension.  This is unlikely, since it
    # is well-documented in VSCode issues, and if it breaks, there is likely to be a
    # replacement mechanism provided by the extension maintainers.
    encoded_id = urllib.parse.quote(container_id, safe="")
    encoded_workspace = urllib.parse.quote(container_workspace, safe="/")
    return f"vscode-remote://attached-container+{encoded_id}{encoded_workspace}"


def _launch_editor(
    editor: str,
    env_root: Path,
    container_id: str,
    container_workspace: str,
) -> None:
    command = EDITORS.get(editor)
    if command is None or editor != "vscode":
        raise LaunchError("prereq_missing", f"unsupported editor: '{editor}'")

    # parse target environment root
    env_root = env_root.expanduser().resolve()
    if not env_root.exists():
        raise LaunchError("invalid_request", f"env_root does not exist: {env_root}")
    if not env_root.is_dir():
        raise LaunchError("invalid_request", f"env_root must be a directory: {env_root}")

    # extract target container ID and mounted workspace path
    container_id = container_id.strip()
    if not container_id:
        raise LaunchError("invalid_request", "container_id must be non-empty")
    workspace = _normalize_workspace(container_workspace)

    # find host editor and podman executables
    code_bin = _require_executable(command[0])
    podman_bin = _require_executable("podman")

    # ensure we can remotely attach to the container
    _ensure_vscode_extension(code_bin)

    # ensure container is running and tools are available inside it
    _ensure_running_container(podman_bin, container_id)
    _resolve_container_clangd(podman_bin, container_id)

    # write toolchain configuration to mounted workspace to wire LSPs, etc. into the editor
    _ensure_workspace_settings(env_root)

    # open editor in detached (non-blocking) process
    folder_uri = _vscode_folder_uri(container_id, workspace)
    try:
        subprocess.Popen(
            [code_bin, *command[1:], "--folder-uri", folder_uri],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
    except OSError as err:
        raise LaunchError(
            "vscode_attach_failed",
            f"failed to launch VS Code container attach session: {err}",
        ) from err


@dataclass
class CodeServer:
    """A minimal, host-side Unix socket listener that handles RPC requests from
    in-container `bertrand code` commands.  When a request is received, the server
    will attempt to launch the specified editor on the host, pointed at the specified
    environment root directory.  The server will respond with a JSON object indicating
    success or failure, and an error message if applicable.
    """

    class Request(BaseModel):
        """JSON request schema for the code RPC service."""
        model_config = ConfigDict(extra="forbid")
        version: int
        op: str
        editor: str
        env_root: str
        container_id: str
        container_workspace: str = WORKSPACE_MOUNT

    class Response(BaseModel):
        """JSON response schema for the code RPC service."""
        model_config = ConfigDict(extra="forbid")
        ok: bool
        error: str

    socket_path: Path = field(default=CODE_SOCKET)
    _sock: socket.socket | None = field(default=None, init=False, repr=False)

    def _ensure_listening(self) -> None:
        if self._sock is not None:
            return  # already listening

        # make private directory and clear existing socket file if needed
        path = _prepare_socket(self.socket_path)

        # create Unix socket with restrictive permissions and bind to path
        server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            server.bind(str(path))
            server.listen()
            path.chmod(0o600)
        except Exception:
            server.close()
            raise

        self._sock = server

    def _accept_and_read_line(self) -> tuple[socket.socket, str]:
        self._ensure_listening()
        assert self._sock is not None

        # accept a single connection and read a line of input
        conn, _ = self._sock.accept()
        try:
            reader = conn.makefile("r", encoding="utf-8", newline="\n")  # newline-terminated
            try:
                line = reader.readline(MAX_REQUEST_BYTES + 1)  # enforce max request size
            finally:
                reader.close()
        except Exception:
            conn.close()
            raise

        return conn, line

    def _handle_request_line(self, line: str) -> CodeServer.Response:
        if not line:
            return CodeServer.Response(ok=False, error="empty request")
        if len(line) > MAX_REQUEST_BYTES:
            return CodeServer.Response(ok=False, error="request exceeds maximum size")
        if not line.endswith("\n"):
            return CodeServer.Response(ok=False, error="request must be newline-terminated")
        text = line.removesuffix("\n").strip()
        if not text:
            return CodeServer.Response(ok=False, error="empty request")

        # parse JSON
        try:
            payload = json.loads(text)
        except json.JSONDecodeError as err:
            return CodeServer.Response(ok=False, error=f"malformed JSON request: {err.msg}")
        if not isinstance(payload, dict):
            return CodeServer.Response(ok=False, error="request must be a JSON object")

        # validate schema
        try:
            request = CodeServer.Request.model_validate(payload)
        except ValidationError as err:
            return CodeServer.Response(ok=False, error=f"invalid request: {err}")

        # check protocol version
        if request.version != CODE_SOCKET_VERSION:
            return CodeServer.Response(
                ok=False,
                error=
                    f"unsupported protocol version: {request.version} "
                    f"(expected {CODE_SOCKET_VERSION})"
            )

        # confirm operation
        if request.op != CODE_SOCKET_OP_OPEN:
            return CodeServer.Response(
                ok=False,
                error=f"unsupported operation: '{request.op}'"
            )

        # launch editor
        try:
            _launch_editor(
                request.editor,
                Path(request.env_root),
                request.container_id,
                request.container_workspace,
            )
            return CodeServer.Response(ok=True, error="")
        except LaunchError as err:
            return CodeServer.Response(ok=False, error=str(err))
        except Exception as err:
            return CodeServer.Response(
                ok=False,
                error=f"internal_error: failed to launch editor: {err}"
            )

    @staticmethod
    def _send_response(conn: socket.socket, response: CodeServer.Response) -> None:
        payload = json.dumps(response.model_dump(mode="json"), separators=(",", ":")) + "\n"
        conn.sendall(payload.encode("utf-8"))

    def serve_once(self) -> None:
        """Serve a single request from the socket.  This method will block until a
        request is received, and will return after the request has been handled and a
        response has been sent.
        """
        # read a single line of input from the socket
        conn, line = self._accept_and_read_line()
        try:
            # launch editor and prepare response
            try:
                response = self._handle_request_line(line)
            except Exception as err:
                response = CodeServer.Response(ok=False, error=f"internal server error: {err}")

            # send response back to client
            try:
                self._send_response(conn, response)
            except OSError:
                pass

        # close connection to client and return to listening for next request
        finally:
            conn.close()

    def serve_forever(self) -> None:
        """Serve requests indefinitely until `close()` is called."""
        self._ensure_listening()
        while self._sock is not None:
            self.serve_once()  # block until a request is received

    def close(self) -> None:
        """Close the server and clean up the socket file."""
        if self._sock is None:
            return
        self._sock.close()
        self._sock = None
        try:
            if self.socket_path.exists() and stat.S_ISSOCK(self.socket_path.lstat().st_mode):
                self.socket_path.unlink(missing_ok=True)
        except OSError:
            pass


def main() -> None:
    """Console entry point for the code RPC listener, which starts the server and
    begins listening for requests.
    """
    try:
        server = CodeServer(CODE_SOCKET)
        try:
            server.serve_forever()
        finally:
            server.close()
    except KeyboardInterrupt:
        return
