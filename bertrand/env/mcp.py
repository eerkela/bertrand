"""MCP transport bootstrap and VS Code MCP workspace configuration management."""
from __future__ import annotations

import argparse
import asyncio
import json
import math
import os
import time

from pathlib import Path
from typing import Any, Literal

from mcp.server.fastmcp import FastMCP
from mcp.types import ToolAnnotations
from pydantic import BaseModel, ConfigDict

from .config import MOUNT
from .run import atomic_write_text

# pylint: disable=broad-exception-caught


MCP_SERVER_KEY: str = "bertrand"
MCP_COMMAND: str = "bertrand-mcp"
MCP_ARGS: list[str] = ["--transport", "stdio"]
VSCODE_MCP_FILE: Path = Path(".vscode") / "mcp.json"


# TODO: turn this into a builder class where each tool is a subclass with its own
# constants and request/response models.  This is part of a general cleanup of code.py
# as well, which revolves around better documenting everything and improving
# locality of related code.


# TODO: I also need to make sure that all of these tools respect the configuration in
# pyproject.toml when executed from the MCP server, as well as in the CLI.  This may
# mean updating clang-* files as well, and making sure the Python tools are set up to
# read pyproject.toml config on their own.



PYTHON_TYPECHECK_TOOL: str = "python_typecheck"
TYPECHECK_ENGINE: str = "ty"
TYPECHECK_BIN: str = "ty"
TYPECHECK_OUTPUT_FORMAT: Literal["gitlab"] = "gitlab"
TYPECHECK_TIMEOUT_DEFAULT: float = 30.0
TYPECHECK_TIMEOUT_MIN: float = 0.1
TYPECHECK_TIMEOUT_MAX: float = 120.0
STREAM_CAP_BYTES: int = 1024 * 1024


class ResolvedPath(BaseModel):
    """A validated path passed to the type checker."""
    model_config = ConfigDict(extra="forbid")
    input: str
    relative: str
    absolute: str


class TypecheckResult(BaseModel):
    """Structured response payload for the `python_typecheck` MCP tool."""
    model_config = ConfigDict(extra="forbid")
    status: Literal["ok", "findings", "error"]
    engine: str = TYPECHECK_ENGINE
    exit_code: int | None
    cwd: str
    command: list[str]
    elapsed_ms: int
    paths: list[ResolvedPath]
    diagnostics_format: Literal["gitlab"] | None
    diagnostics: Any | None
    stdout: str
    stderr: str
    stdout_truncated: bool
    stderr_truncated: bool
    stdout_bytes: int
    stderr_bytes: int
    error: str | None


def _mcp_server_config() -> dict[str, Any]:
    return {
        "type": "stdio",
        "command": MCP_COMMAND,
        "args": list(MCP_ARGS),
    }


def _warning(path: Path, reason: str) -> str:
    return f"failed to sync VSCode MCP config ({path}): {reason}"


def sync_vscode_mcp_config(env_root: Path) -> str | None:
    """Update `.vscode/mcp.json` to register the Bertrand MCP server.  Once configured,
    VSCode will automatically start the MCP server when the user opens the workspace,
    and stop it when the user closes the workspace.  This allows the MCP server to
    stay alive for the duration of the VSCode session.

    Parameters
    ----------
    env_root : Path
        Host path to the environment root directory.

    Returns
    -------
    str | None
        A non-fatal warning message if sync fails, otherwise None.
    """
    root = env_root.expanduser().resolve()
    if not root.exists():
        return _warning(root, "environment root does not exist")
    if not root.is_dir():
        return _warning(root, "environment root is not a directory")

    # Read existing config if it exists, otherwise start with empty config.
    path = root / VSCODE_MCP_FILE
    data: dict[str, Any] = {}
    if path.exists():
        if not path.is_file():
            return _warning(path, "path exists but is not a file")
        try:
            text = path.read_text(encoding="utf-8")
        except OSError as err:
            return _warning(path, f"failed to read file: {err}")
        try:
            loaded = json.loads(text)
        except json.JSONDecodeError as err:
            return _warning(
                path,
                f"invalid JSON (JSONC comments are not supported): {err.msg}",
            )
        if not isinstance(loaded, dict):
            return _warning(path, "root value must be a JSON object")
        data = loaded

    # get current servers, so that we only modify the key owned by Bertrand
    servers = data.get("servers")
    if servers is None:
        merged_servers: dict[str, Any] = {}
    elif isinstance(servers, dict):
        merged_servers = dict(servers)
    else:
        return _warning(path, "field 'servers' must be a JSON object")

    # update bertrand server config
    merged_servers[MCP_SERVER_KEY] = _mcp_server_config()
    merged = dict(data)
    merged["servers"] = merged_servers
    if data == merged:
        return None  # skip rewrite when semantic content is unchanged

    # write merged config back to file
    rendered = json.dumps(merged, sort_keys=True, indent=2) + "\n"
    try:
        atomic_write_text(path, rendered, encoding="utf-8")
    except OSError as err:
        return _warning(path, f"failed to write file: {err}")
    return None


def _elapsed_ms(start_ns: int) -> int:
    elapsed = time.monotonic_ns() - start_ns
    return max(0, int(elapsed // 1_000_000))


def _workspace_root() -> Path:
    raw = os.environ.get(WORKSPACE_ENV, "").strip() or str(MOUNT)
    workspace = Path(raw)
    if not workspace.is_absolute():
        raise OSError(f"workspace root must be absolute: {workspace}")
    workspace = workspace.expanduser().resolve()
    if not workspace.exists():
        raise OSError(f"workspace root does not exist: {workspace}")
    if not workspace.is_dir():
        raise OSError(f"workspace root is not a directory: {workspace}")
    return workspace


def _clamp_timeout(timeout_seconds: float | None) -> float:
    if timeout_seconds is None:
        return TYPECHECK_TIMEOUT_DEFAULT
    try:
        value = float(timeout_seconds)
    except (TypeError, ValueError):
        return TYPECHECK_TIMEOUT_DEFAULT
    if not math.isfinite(value):
        return TYPECHECK_TIMEOUT_DEFAULT
    return min(max(value, TYPECHECK_TIMEOUT_MIN), TYPECHECK_TIMEOUT_MAX)


def _cap_stream(raw: bytes) -> tuple[str, bool, int]:
    size = len(raw)
    if size > STREAM_CAP_BYTES:
        return (
            raw[:STREAM_CAP_BYTES].decode("utf-8", errors="replace"),
            True,
            size,
        )
    return raw.decode("utf-8", errors="replace"), False, size


def _resolve_paths(
    workspace: Path,
    paths: list[str] | None,
) -> tuple[list[ResolvedPath], list[str]]:
    requested = paths or ["."]
    validated: list[ResolvedPath] = []
    errors: list[str] = []
    for raw in requested:
        if not isinstance(raw, str):
            errors.append(f"{raw!r}: path must be a string")  # type: ignore
            continue
        cleaned = raw.strip()
        if not cleaned:
            errors.append(f"{raw!r}: path cannot be empty")
            continue
        candidate = Path(cleaned)
        if candidate.is_absolute():
            errors.append(f"{raw!r}: absolute paths are not allowed")
            continue

        resolved = (workspace / candidate).resolve()
        if not resolved.exists():
            errors.append(f"{raw!r}: path does not exist")
            continue
        if not resolved.is_relative_to(workspace):
            errors.append(f"{raw!r}: path resolves outside workspace")
            continue

        validated.append(ResolvedPath(
            input=raw,
            relative=str(resolved.relative_to(workspace)),
            absolute=str(resolved),
        ))
    return validated, errors


def _build_ty_command(paths: list[ResolvedPath]) -> list[str]:
    return [
        TYPECHECK_BIN,
        "check",
        "--output-format",
        TYPECHECK_OUTPUT_FORMAT,
        *[path.relative for path in paths],
    ]


def _status_from_exit_code(exit_code: int) -> Literal["ok", "findings", "error"]:
    if exit_code == 0:
        return "ok"
    if exit_code == 1:
        return "findings"
    return "error"


def _parse_gitlab_diagnostics(
    stdout_full: str,
    exit_code: int,
) -> tuple[Any | None, str | None]:
    if exit_code not in (0, 1):
        return None, None

    if exit_code == 0 and not stdout_full.strip():
        return [], None

    try:
        return json.loads(stdout_full), None
    except json.JSONDecodeError as err:
        return None, f"failed to parse ty gitlab diagnostics: {err.msg}"


def _error_result(
    *,
    start_ns: int,
    cwd: Path,
    command: list[str],
    paths: list[ResolvedPath],
    error: str,
    stdout_raw: bytes = b"",
    stderr_raw: bytes = b"",
) -> TypecheckResult:
    stdout, stdout_truncated, stdout_bytes = _cap_stream(stdout_raw)
    stderr, stderr_truncated, stderr_bytes = _cap_stream(stderr_raw)
    return TypecheckResult(
        status="error",
        exit_code=None,
        cwd=str(cwd),
        command=command,
        elapsed_ms=_elapsed_ms(start_ns),
        paths=paths,
        diagnostics_format=None,
        diagnostics=None,
        stdout=stdout,
        stderr=stderr,
        stdout_truncated=stdout_truncated,
        stderr_truncated=stderr_truncated,
        stdout_bytes=stdout_bytes,
        stderr_bytes=stderr_bytes,
        error=error,
    )


async def _run_ty_check(
    *,
    start_ns: int,
    workspace: Path,
    paths: list[ResolvedPath],
    timeout_seconds: float,
) -> TypecheckResult:
    command = _build_ty_command(paths)
    try:
        process = await asyncio.create_subprocess_exec(
            *command,
            cwd=str(workspace),
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
    except FileNotFoundError:
        return _error_result(
            start_ns=start_ns,
            cwd=workspace,
            command=command,
            paths=paths,
            error="type checker executable not found on PATH: ty",
        )
    except OSError as err:
        return _error_result(
            start_ns=start_ns,
            cwd=workspace,
            command=command,
            paths=paths,
            error=f"failed to start type checker: {err}",
        )

    try:
        stdout_raw, stderr_raw = await asyncio.wait_for(
            process.communicate(),
            timeout=timeout_seconds,
        )
    except TimeoutError:
        process.kill()
        stdout_raw, stderr_raw = await process.communicate()
        return _error_result(
            start_ns=start_ns,
            cwd=workspace,
            command=command,
            paths=paths,
            error=f"type checker timed out after {timeout_seconds:.3g}s",
            stdout_raw=stdout_raw,
            stderr_raw=stderr_raw,
        )

    exit_code = process.returncode
    if exit_code is None:
        return _error_result(
            start_ns=start_ns,
            cwd=workspace,
            command=command,
            paths=paths,
            error="type checker process exited without a return code",
            stdout_raw=stdout_raw,
            stderr_raw=stderr_raw,
        )

    stdout_full = stdout_raw.decode("utf-8", errors="replace")
    status = _status_from_exit_code(exit_code)
    diagnostics_format: Literal["gitlab"] | None = None
    diagnostics: Any | None = None
    error: str | None = None
    if exit_code in (0, 1):
        diagnostics, parse_error = _parse_gitlab_diagnostics(stdout_full, exit_code)
        diagnostics_format = TYPECHECK_OUTPUT_FORMAT
        if parse_error is not None:
            status = "error"
            error = parse_error
    else:
        error = f"type checker exited with code {exit_code}"

    stdout, stdout_truncated, stdout_bytes = _cap_stream(stdout_raw)
    stderr, stderr_truncated, stderr_bytes = _cap_stream(stderr_raw)
    return TypecheckResult(
        status=status,
        exit_code=exit_code,
        cwd=str(workspace),
        command=command,
        elapsed_ms=_elapsed_ms(start_ns),
        paths=paths,
        diagnostics_format=diagnostics_format,
        diagnostics=diagnostics,
        stdout=stdout,
        stderr=stderr,
        stdout_truncated=stdout_truncated,
        stderr_truncated=stderr_truncated,
        stdout_bytes=stdout_bytes,
        stderr_bytes=stderr_bytes,
        error=error,
    )


def build_server() -> FastMCP:
    """Construct the Bertrand MCP server and register built-in tools."""
    server = FastMCP(MCP_SERVER_KEY)

    @server.tool(
        name=PYTHON_TYPECHECK_TOOL,
        description=(
            "Run Python type checking in the current workspace and return parsed "
            "diagnostics from ty's GitLab JSON output along with raw stdout/stderr."
        ),
        annotations=ToolAnnotations(
            readOnlyHint=True,
            idempotentHint=True,
            destructiveHint=False,
        ),
        structured_output=True,
    )
    async def python_typecheck(
        paths: list[str] | None = None,
        timeout_seconds: float | None = None,
    ) -> TypecheckResult:
        """Type check selected workspace paths using ty."""
        start_ns = time.monotonic_ns()
        try:
            workspace = _workspace_root()
        except OSError as err:
            fallback = Path(
                os.environ.get(WORKSPACE_ENV, "").strip() or str(MOUNT)
            ).expanduser()
            return _error_result(
                start_ns=start_ns,
                cwd=fallback,
                command=[],
                paths=[],
                error=str(err),
            )

        validated_paths, validation_errors = _resolve_paths(workspace, paths)
        if validation_errors:
            return _error_result(
                start_ns=start_ns,
                cwd=workspace,
                command=[],
                paths=validated_paths,
                error="invalid path(s): " + "; ".join(validation_errors),
            )

        timeout = _clamp_timeout(timeout_seconds)
        return await _run_ty_check(
            start_ns=start_ns,
            workspace=workspace,
            paths=validated_paths,
            timeout_seconds=timeout,
        )

    return server


class Parser:
    """Argument parser for Bertrand's MCP server script."""

    def __init__(self) -> None:
        self._parser = argparse.ArgumentParser(
            prog="bertrand-mcp",
            description="Run Bertrand's MCP server process.",
        )
        self.transport()

    def transport(self) -> None:
        """Add the 'transport' argument to the parser."""
        self._parser.add_argument(
            "--transport",
            choices=("stdio",),
            default="stdio",
            help="MCP transport to use (currently only 'stdio').",
        )

    def __call__(self, argv: list[str] | None = None) -> argparse.Namespace:
        """Parse command-line arguments.

        Parameters
        ----------
        argv : list[str] | None
            Command-line arguments.  Defaults to None, which means to use sys.argv.

        Returns
        -------
        argparse.Namespace
            The parsed arguments.
        """
        return self._parser.parse_args(argv)


def main(argv: list[str] | None = None) -> None:
    """Console entry point for Bertrand's MCP server transport process.

    Parameters
    ----------
    argv : list[str] | None
        Command-line arguments.  Defaults to None, which means to use sys.argv.
    """
    # parse arguments
    parser = Parser()
    args = parser(argv)

    # start MCP server
    server = build_server()
    server.run(transport=args.transport)
