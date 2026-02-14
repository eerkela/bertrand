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
import time
import urllib.parse

from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
from typing import Any

from pydantic import BaseModel, ConfigDict, Field, ValidationError

from .config import (
    AGENTS,
    ASSISTS,
    CONTAINER_BIN_ENV,
    EDITOR_BIN_ENV,
    EDITORS,
    MOUNT,
    Config
)
from .mcp import sync_vscode_mcp_config
from .run import User, atomic_write_text, mkdir_private

# pylint: disable=broad-exception-caught


CODE_LAUNCH_TIMEOUT: float = 10.0
CODE_PROBE_TIMEOUT: float = 10.0
CODE_RPC_CLIENT_HEADROOM: float = 2.0
CODE_RPC_CLIENT_TIMEOUT: float = CODE_LAUNCH_TIMEOUT + CODE_RPC_CLIENT_HEADROOM
CODE_RPC_READ_TIMEOUT: float = CODE_RPC_CLIENT_TIMEOUT
CODE_SOCKET_VERSION: int = 1
CODE_SOCKET_OP_OPEN: str = "open_editor"
CODE_SOCKET: Path = User().home / ".local" / "share" / "bertrand" / "code-rpc" / "listener.sock"
MAX_REQUEST_BYTES: int = 1024 * 1024  # 1 MiB
CONTAINER_SOCKET_DIR: Path = Path("/run/bertrand/code-rpc")
CONTAINER_SOCKET: Path = CONTAINER_SOCKET_DIR / "listener.sock"

VSCODE_MANAGED_WORKSPACE_FILE: Path = Path(".bertrand") / "vscode" / "bertrand.code-workspace"
VSCODE_MANAGED_WORKSPACE_FILE_POSIX: PurePosixPath = (
    PurePosixPath(".bertrand") / "vscode" / "bertrand.code-workspace"
)
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

class LaunchError(OSError):
    """A structured error used to keep RPC failure categories stable."""

    def __init__(self, category: str, message: str) -> None:
        self.category = category
        self.message = message
        super().__init__(f"{category}: {message}")


def send_request(
    request: CodeServer.Request,
    *,
    socket_path: Path = CODE_SOCKET,
    timeout: float = CODE_RPC_CLIENT_TIMEOUT,
) -> CodeServer.Response:
    """Send a single request to the code RPC server from inside a container context and
    wait for a single response.

    Parameters
    ----------
    request : CodeServer.Request
        The request to send to the server.  This will be serialized as JSON and sent as
        a single line of text, terminated by a newline character.
    socket_path : Path, optional
        The path to the server's Unix socket file.  Defaults to `CODE_SOCKET`, which is
        a host path to the socket file.  If executing in-container, this should be
        replaced with `CONTAINER_SOCKET`, which is the corresponding path to the
        bind-mounted socket file inside the container context.
    timeout : float, optional
        The maximum number of seconds to wait for a response from the server before
        raising a timeout error.  Defaults to `CODE_RPC_CLIENT_TIMEOUT`.

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
        return CodeServer.Response.model_validate(response)
    except ValidationError as err:
        raise OSError(f"invalid RPC response: {err}") from err


def code_server_reachable(
    *,
    socket_path: Path = CODE_SOCKET,
    timeout: float = CODE_PROBE_TIMEOUT,
    interval: float = 0.1,
) -> bool:
    """Check whether the RPC server socket is reachable within a bounded timeout.

    Parameters
    ----------
    socket_path : Path, optional
        The path to the server's Unix socket file.  Defaults to `CODE_SOCKET`.
    timeout : float, optional
        Maximum time in seconds to wait for the server to become reachable.
    interval : float, optional
        Delay in seconds between failed connection attempts.

    Returns
    -------
    bool
        True if the socket accepted a connection before timeout, otherwise False.

    Raises
    ------
    OSError
        If `socket_path` is not absolute.
    """
    path = socket_path.expanduser()
    if not path.is_absolute():
        raise OSError(f"RPC socket path must be absolute: {path}")

    # spin while trying to connect to socket until timeout expires
    timeout = max(timeout, 0.0)
    deadline = time.monotonic() + timeout
    while True:
        client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        remaining = max(0.0, deadline - time.monotonic())
        client.settimeout(max(remaining, 0.001))
        try:
            client.connect(str(path))
            return True
        except OSError:
            if time.monotonic() >= deadline:
                return False
            time.sleep(min(max(interval, 0.0), max(0.0, deadline - time.monotonic())))
        finally:
            client.close()


def request_open_editor(
    *,
    editor: str,
    env_root: Path,
    container_id: str,
    container_workspace: str = str(MOUNT),
    socket_path: Path = CODE_SOCKET,
    timeout: float = CODE_RPC_CLIENT_TIMEOUT,
) -> list[str]:
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
        replaced with `CONTAINER_SOCKET`, which is the corresponding path to the
        bind-mounted socket file inside the container context.
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
    return response.warnings


def _resolve_executable(
    name: str,
    env_var: str,
    *,
    candidates: tuple[str, ...] | None = None,
) -> str:
    # check for explicit environment variable override
    override = os.environ.get(env_var, "").strip()
    if override:
        path = Path(override).expanduser()
        if not path.is_absolute():
            raise LaunchError(
                "prereq_missing",
                f"{env_var} must be an absolute path: {path}",
            )
        path = path.resolve()
        if not path.exists():
            raise LaunchError(
                "prereq_missing",
                f"{env_var} points to a missing executable: {path}",
            )
        if not path.is_file() or not os.access(path, os.X_OK):
            raise LaunchError(
                "prereq_missing",
                f"{env_var} is not executable: {path}",
            )
        return str(path)

    # test alternatives
    names = candidates or (name,)
    for candidate in names:
        executable = shutil.which(candidate)
        if executable is not None:
            return executable
    if len(names) == 1:
        raise LaunchError("prereq_missing", f"required executable '{name}' not found on PATH")
    tried = ", ".join(names)
    raise LaunchError(
        "prereq_missing",
        f"required executable '{name}' not found on PATH (tried: {tried})",
    )


def _normalize_workspace(container_workspace: str) -> str:
    workspace = PurePosixPath(container_workspace.strip() or MOUNT)
    if not workspace.is_absolute():
        raise LaunchError(
            "invalid_request",
            f"container_workspace must be absolute: '{container_workspace}'",
        )
    return str(workspace)


def _launch_deadline(seconds: float) -> float:
    return time.monotonic() + seconds


def _remaining_probe_timeout(deadline: float, *, timeout_category: str, step: str) -> float:
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise LaunchError(
            timeout_category,
            f"launch budget exhausted before probe step '{step}' could run",
        )
    return min(CODE_PROBE_TIMEOUT, remaining)


def _run_probe(
    argv: list[str],
    *,
    timeout: float,
    timeout_category: str,
) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            argv,
            check=False,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as err:
        raise LaunchError(
            timeout_category,
            f"probe timed out after {timeout:.1f}s: {' '.join(shlex.quote(a) for a in argv)}",
        ) from err
    except OSError as err:
        raise LaunchError(
            "prereq_missing",
            f"failed to execute probe: {' '.join(shlex.quote(a) for a in argv)}: {err}",
        ) from err


def _ensure_running_container(podman_bin: str, container_id: str, *, deadline: float) -> None:
    result = _run_probe(
        [podman_bin, "container", "inspect", "--format", "{{.State.Status}}", container_id],
        timeout=_remaining_probe_timeout(
            deadline,
            timeout_category="container_probe_timeout",
            step="container inspect",
        ),
        timeout_category="container_probe_timeout",
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


def _required_container_tool(
    podman_bin: str,
    container_id: str,
    tool: str,
    *,
    deadline: float,
    timeout_category: str,
    missing_category: str,
) -> str:
    result = _run_probe(
        [podman_bin, "exec", container_id, "sh", "-lc", f"command -v {shlex.quote(tool)}"],
        timeout=_remaining_probe_timeout(
            deadline,
            timeout_category=timeout_category,
            step=f"{tool} lookup",
        ),
        timeout_category=timeout_category,
    )
    stdout = result.stdout.strip()
    stderr = result.stderr.strip()
    if result.returncode != 0 or not stdout:
        detail = stderr or stdout
        suffix = f": {detail}" if detail else ""
        raise LaunchError(
            missing_category,
            f"{tool} is not available in container '{container_id}'{suffix}",
        )
    return stdout


def _optional_container_tool(
    podman_bin: str,
    container_id: str,
    tool: str,
    *,
    deadline: float,
    timeout_category: str,
    missing_category: str,
    warning_hint: str,
) -> str | None:
    try:
        _required_container_tool(
            podman_bin,
            container_id,
            tool,
            deadline=deadline,
            timeout_category=timeout_category,
            missing_category=missing_category,
        )
        return None
    except LaunchError as err:
        return f"{err}; {warning_hint}"


def _ensure_vscode_extension(code_bin: str, *, deadline: float) -> None:
    result = _run_probe(
        [code_bin, "--list-extensions"],
        timeout=_remaining_probe_timeout(
            deadline,
            timeout_category="vscode_probe_timeout",
            step="vscode extension listing",
        ),
        timeout_category="vscode_probe_timeout",
    )
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


def _recommended_extensions(env_root: Path) -> list[str]:
    try:
        with Config(env_root) as config:
            agent = config["tool", "bertrand", "agent"]  # validated on enter
            assist = config["tool", "bertrand", "assist"]  # validated on enter
            exts: list[str] = []
            for ext in (*VSCODE_BASE_RECOMMENDED_EXTENSIONS, *AGENTS[agent], *ASSISTS[assist]):
                if ext not in exts:
                    exts.append(ext)
            return exts
    except KeyError as err:
        raise LaunchError(
            "config_write_failure",
            f"unsupported configuration option in pyproject.toml: {err}"
        ) from err
    except OSError as err:
        raise LaunchError("config_write_failure", str(err)) from err


def _render_managed_workspace(
    container_workspace: str,
    *,
    clangd_arguments: list[str],
    recommended_extensions: list[str],
) -> str:
    settings = dict(VSCODE_MANAGED_SETTINGS)
    settings["clangd.arguments"] = clangd_arguments
    payload = {
        "folders": [{"path": container_workspace}],
        "settings": settings,
        "extensions": {"recommendations": recommended_extensions},
        "tasks": VSCODE_MANAGED_TASKS,
    }
    return json.dumps(payload, indent=2, sort_keys=True) + "\n"


def _ensure_managed_workspace_file(
    env_root: Path,
    container_workspace: str,
    *,
    clangd_arguments: list[str],
    recommended_extensions: list[str],
) -> tuple[Path, str]:
    # reconcile host and container paths to managed workspace file
    host_workspace_file = env_root / VSCODE_MANAGED_WORKSPACE_FILE
    container_workspace_file = str(
        PurePosixPath(container_workspace) / VSCODE_MANAGED_WORKSPACE_FILE_POSIX
    )

    # write workspace file to host filesystem
    text = _render_managed_workspace(
        container_workspace,
        clangd_arguments=clangd_arguments,
        recommended_extensions=recommended_extensions,
    )

    # don't write if content is unchanged
    if host_workspace_file.exists():
        if not host_workspace_file.is_file():
            raise LaunchError(
                "config_write_failure",
                f"managed workspace path is not a file: {host_workspace_file}",
            )
        try:
            current = host_workspace_file.read_text(encoding="utf-8")
        except OSError as err:
            raise LaunchError(
                "config_write_failure",
                f"failed to read managed workspace file: {host_workspace_file}: {err}",
            ) from err
        if current == text:
            return host_workspace_file, container_workspace_file

    # write new content atomically
    try:
        atomic_write_text(host_workspace_file, text, encoding="utf-8")
    except OSError as err:
        raise LaunchError(
            "config_write_failure",
            f"failed to write managed workspace file: {host_workspace_file}: {err}",
        ) from err
    return host_workspace_file, container_workspace_file


def _vscode_workspace_uri(container_id: str, container_workspace_file: str) -> str:
    # NOTE: this URI allows the VSCode Remote Containers extension to attach to a
    # running container by ID, but is not technically part of the public API.  It is
    # well-documented in various issues and discussions, but may be subject to change
    # without notice.  If this breaks, it should have been replaced by something more
    # stable that we can use instead.
    encoded_id = urllib.parse.quote(container_id, safe="")
    encoded_workspace_file = urllib.parse.quote(container_workspace_file, safe="/")
    return f"vscode-remote://attached-container+{encoded_id}{encoded_workspace_file}"


def _launch_editor(
    editor: str,
    env_root: Path,
    container_id: str,
    container_workspace: str,
) -> list[str]:
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
    code_bin = _resolve_executable(
        command[0],
        EDITOR_BIN_ENV,
        candidates=VSCODE_EXECUTABLE_CANDIDATES,
    )
    podman_bin = _resolve_executable("podman", CONTAINER_BIN_ENV)
    deadline = _launch_deadline(CODE_LAUNCH_TIMEOUT)

    # ensure we can remotely attach to the container
    _ensure_vscode_extension(code_bin, deadline=deadline)

    # ensure container is running and tools are available inside it
    _ensure_running_container(podman_bin, container_id, deadline=deadline)
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
        warning = _optional_container_tool(
            podman_bin,
            container_id,
            tool,
            deadline=deadline,
            timeout_category=f"{tool}_probe_timeout",
            missing_category=f"{tool}_missing",
            warning_hint=warning_hint,
        )
        if warning is not None:
            warnings.append(warning)

    # write bertrand-managed settings to a dedicated workspace file and leave any
    # user-owned .vscode/settings.json untouched.
    with Config(env_root) as config:
        clangd_arguments = config["tool", "clangd", "arguments"]  # validated on enter
    recommended_extensions = _recommended_extensions(env_root)
    _, container_workspace_file = _ensure_managed_workspace_file(
        env_root,
        workspace,
        clangd_arguments=clangd_arguments,
        recommended_extensions=recommended_extensions,
    )

    # merge Bertrand's MCP entry into workspace-level VS Code MCP config without
    # touching user-managed server entries.
    mcp_warning = sync_vscode_mcp_config(env_root)
    if mcp_warning is not None:
        warnings.append(mcp_warning)

    # open editor in detached (non-blocking) process
    workspace_uri = _vscode_workspace_uri(container_id, container_workspace_file)
    try:
        subprocess.Popen(
            [code_bin, *command[1:], "--file-uri", workspace_uri],
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
    return warnings


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
        container_workspace: str = str(MOUNT)

    class Response(BaseModel):
        """JSON response schema for the code RPC service."""
        model_config = ConfigDict(extra="forbid")
        ok: bool
        error: str
        warnings: list[str] = Field(default_factory=list)

    socket_path: Path = field(default=CODE_SOCKET)
    _sock: socket.socket | None = field(default=None, init=False, repr=False)

    def _ensure_listening(self) -> None:
        if self._sock is not None:
            return  # already listening

        # make private directory and clear existing socket file if needed
        path = self.socket_path.expanduser().resolve()
        mkdir_private(path.parent)
        if path.exists():
            mode = path.lstat().st_mode
            if not stat.S_ISSOCK(mode):
                raise OSError(f"socket path occupied: {path}")
            path.unlink(missing_ok=True)

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

        # accept one connection at a time; drop stalled/broken clients without
        # taking down the listener loop.
        while True:
            conn, _ = self._sock.accept()
            conn.settimeout(CODE_RPC_READ_TIMEOUT)
            try:
                reader = conn.makefile("r", encoding="utf-8", newline="\n")
                try:
                    line = reader.readline(MAX_REQUEST_BYTES + 1)  # enforce max request size
                finally:
                    reader.close()
                return conn, line
            except (TimeoutError, socket.timeout, OSError, UnicodeError):
                conn.close()
                continue
            except Exception:
                conn.close()
                raise

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
            warnings = _launch_editor(
                request.editor,
                Path(request.env_root),
                request.container_id,
                request.container_workspace,
            )
            return CodeServer.Response(ok=True, error="", warnings=warnings)
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
    """Entry point for the code RPC listener, which starts the server and begins
    listening for editor requests.
    """
    try:
        server = CodeServer(CODE_SOCKET)
        try:
            server.serve_forever()
        finally:
            server.close()
    except KeyboardInterrupt:
        return
