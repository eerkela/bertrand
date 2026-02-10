"""Text editor integrations for Bertrand's containerized development environments.

This module currently provides a minimal host-side RPC listener skeleton that can be
used by future in-container `bertrand code` clients.
"""
from __future__ import annotations

import json
import socket
import stat
import subprocess

from dataclasses import dataclass, field
from pathlib import Path

from pydantic import BaseModel, ConfigDict, ValidationError

from .run import User, mkdir_private

# pylint: disable=broad-exception-caught


CODE_SOCKET_VERSION: int = 1
CODE_SOCKET_OP_OPEN: str = "open_editor"
CODE_SOCKET_DIR: Path = User().home / ".local" / "share" / "bertrand" / "code-rpc"
CODE_SOCKET: Path = CODE_SOCKET_DIR / "listener.sock"
MAX_REQUEST_BYTES: int = 1024 * 1024  # 1 MiB

EDITORS: dict[str, tuple[str, ...]] = {
    "vscode": ("code",),
}


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


def _launch_editor(editor: str, env_root: Path) -> None:
    command = EDITORS.get(editor)
    if command is None:
        raise KeyError(f"unsupported editor: '{editor}'")

    env_root = env_root.expanduser().resolve()
    if not env_root.exists():
        raise FileNotFoundError(f"env_root does not exist: {env_root}")
    if not env_root.is_dir():
        raise NotADirectoryError(f"env_root must be a directory: {env_root}")

    # open editor in detached (non-blocking) process
    subprocess.Popen(
        [*command, str(env_root)],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )


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
            _launch_editor(request.editor, Path(request.env_root))
            return CodeServer.Response(ok=True, error="")
        except Exception as err:
            return CodeServer.Response(ok=False, error=f"failed to launch editor: {err}")

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


def listen(socket_path: Path = CODE_SOCKET) -> None:
    """Launch the code RPC server and begin listening for requests.

    Parameters
    ----------
    socket_path : Path, optional
        The path to the Unix socket file to listen on.  Defaults to `CODE_SOCKET`.
    """
    server = CodeServer(socket_path)
    try:
        server.serve_forever()
    finally:
        server.close()


def main() -> None:
    """Console entry point for the code RPC listener."""
    try:
        listen()
    except KeyboardInterrupt:
        return
