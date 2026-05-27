"""LSP tool support for Bertrand's editor-owned MCP server."""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import shutil
import signal
import sys
from collections import deque
from collections.abc import Callable
from contextlib import suppress
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING, Any, Literal, cast
from urllib.parse import unquote, urlparse

from bertrand.env.git import CONTAINER_ARTIFACT_MOUNT, WORKTREE_MOUNT

from .constants import LSP_SOCKET_PATH, MCP_ARTIFACTS_ENV, MCP_WORKSPACE_ENV
from .jsonrpc import (
    JsonRpcError,
    RequestID,
    encode_content_length_message,
    format_error,
    params,
    read_content_length_message,
)

if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence

type Language = Literal["python", "cpp"]
type JSON = dict[str, Any]
type _DocumentLookup = Callable[[str], _DocumentState | None]

_LANGUAGES: frozenset[Language] = frozenset({"python", "cpp"})
_REQUEST_TIMEOUT_SECONDS = 30.0
_STARTUP_TIMEOUT_SECONDS = 15.0
_DIAGNOSTIC_SETTLE_SECONDS = 0.25


class LSPError(RuntimeError):
    """Raised when a managed language server cannot satisfy a request."""


@dataclass
class _DocumentState:
    path: Path
    uri: str
    language_id: str
    text: str
    mtime_ns: int
    size: int
    version: int = 1


@dataclass(frozen=True)
class _ServerSpec:
    language: Language
    command: tuple[str, ...]
    workspace: Path
    artifacts: Path
    required_artifacts: tuple[Path, ...] = ()

    @property
    def executable(self) -> str:
        return self.command[0]


class _LanguageServerSession:
    def __init__(self, spec: _ServerSpec) -> None:
        self.spec = spec
        self._proc: asyncio.subprocess.Process | None = None
        self._next_id = 1
        self._pending: dict[RequestID, asyncio.Future[Any]] = {}
        self._reader: asyncio.Task[None] | None = None
        self._stderr: asyncio.Task[None] | None = None
        self._background: set[asyncio.Task[None]] = set()
        self._stderr_tail: deque[str] = deque(maxlen=200)
        self._documents: dict[Path, _DocumentState] = {}
        self._diagnostics: dict[str, list[JSON]] = {}
        self._lock = asyncio.Lock()
        self._document_lock = asyncio.Lock()
        self._initialized = False

    @property
    def running(self) -> bool:
        return self._proc is not None and self._proc.returncode is None

    def status(self) -> JSON:
        executable = shutil.which(self.spec.executable)
        missing = [
            path.as_posix()
            for path in self.spec.required_artifacts
            if not path.exists()
        ]
        return {
            "language": self.spec.language,
            "running": self.running,
            "command": list(self.spec.command),
            "executable": executable,
            "workspace": self.spec.workspace.as_posix(),
            "artifacts": self.spec.artifacts.as_posix(),
            "required_artifacts": [
                path.as_posix() for path in self.spec.required_artifacts
            ],
            "missing_artifacts": missing,
            "stderr_tail": list(self._stderr_tail),
        }

    def document(self, path: Path) -> _DocumentState | None:
        return self._documents.get(path)

    async def start(self) -> None:
        async with self._lock:
            if self.running and self._initialized:
                return
            if self._proc is not None and self._proc.returncode is not None:
                self._raise_crashed()
            executable = shutil.which(self.spec.executable)
            if executable is None:
                msg = (
                    f"{self.spec.language} language server executable "
                    f"{self.spec.executable!r} was not found in PATH"
                )
                raise LSPError(msg)
            missing = [
                path for path in self.spec.required_artifacts if not path.exists()
            ]
            if missing:
                paths = ", ".join(path.as_posix() for path in missing)
                msg = (
                    f"{self.spec.language} language server requires generated "
                    f"artifact(s): {paths}. Run `bertrand build` or refresh "
                    "artifacts before requesting navigation."
                )
                raise LSPError(msg)
            self._proc = await asyncio.create_subprocess_exec(
                *self.spec.command,
                cwd=self.spec.workspace,
                env=_server_env(),
                stdin=asyncio.subprocess.PIPE,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
            )
            self._reader = asyncio.create_task(self._read_loop())
            self._stderr = asyncio.create_task(self._stderr_loop())
            result = await self._request_started(
                "initialize",
                _initialize_params(self.spec),
                timeout=_STARTUP_TIMEOUT_SECONDS,
            )
            await self._send({"jsonrpc": "2.0", "method": "initialized", "params": {}})
            self._initialized = True
            if isinstance(result, dict):
                self._record_server_info(result)

    async def request(
        self,
        method: str,
        params_: JSON | None = None,
        *,
        timeout: float = _REQUEST_TIMEOUT_SECONDS,
    ) -> Any:
        await self.start()
        return await self._request_started(method, params_, timeout=timeout)

    async def notify(self, method: str, params_: JSON | None = None) -> None:
        await self.start()
        await self._send({"jsonrpc": "2.0", "method": method, **params(params_)})

    async def ensure_document(self, path: Path) -> _DocumentState:
        await self.start()
        async with self._document_lock:
            document = _read_document(path, workspace=self.spec.workspace)
            current = self._documents.get(document.path)
            if current is None:
                self._documents[document.path] = document
                await self._send(
                    {
                        "jsonrpc": "2.0",
                        "method": "textDocument/didOpen",
                        "params": {
                            "textDocument": {
                                "uri": document.uri,
                                "languageId": document.language_id,
                                "version": document.version,
                                "text": document.text,
                            },
                        },
                    }
                )
                return document
            if current.mtime_ns == document.mtime_ns and current.size == document.size:
                return current

            document.version = current.version + 1
            self._documents[document.path] = document
            await self._send(
                {
                    "jsonrpc": "2.0",
                    "method": "textDocument/didChange",
                    "params": {
                        "textDocument": {
                            "uri": document.uri,
                            "version": document.version,
                        },
                        "contentChanges": [{"text": document.text}],
                    },
                }
            )
            return document

    def diagnostics(self, document: _DocumentState) -> list[JSON]:
        return list(self._diagnostics.get(document.uri, ()))

    async def close(self) -> None:
        proc = self._proc
        if proc is None:
            return
        if proc.returncode is None:
            try:
                if self._initialized:
                    await self._request_started("shutdown", timeout=5)
                await self._send({"jsonrpc": "2.0", "method": "exit"})
            except (LSPError, TimeoutError, OSError):
                pass
            try:
                await asyncio.wait_for(proc.wait(), timeout=5)
            except TimeoutError:
                proc.terminate()
                try:
                    await asyncio.wait_for(proc.wait(), timeout=5)
                except TimeoutError:
                    proc.kill()
                    await proc.wait()
        for task in (self._reader, self._stderr):
            if task is not None:
                task.cancel()
        self._proc = None
        self._initialized = False

    async def _request_started(
        self,
        method: str,
        params_: JSON | None = None,
        *,
        timeout: float = _REQUEST_TIMEOUT_SECONDS,
    ) -> Any:
        future = asyncio.get_running_loop().create_future()
        request_id = self._next_id
        self._next_id += 1
        self._pending[request_id] = future
        await self._send(
            {
                "jsonrpc": "2.0",
                "id": request_id,
                "method": method,
                **params(params_),
            }
        )
        try:
            return await asyncio.wait_for(future, timeout=timeout)
        finally:
            self._pending.pop(request_id, None)

    async def _send(self, payload: JSON) -> None:
        proc = self._proc
        if proc is None or proc.stdin is None or proc.returncode is not None:
            self._raise_crashed()
        assert proc is not None
        stdin = proc.stdin
        assert stdin is not None
        stdin.write(encode_content_length_message(payload))
        await stdin.drain()

    async def _read_loop(self) -> None:
        proc = self._proc
        if proc is None or proc.stdout is None:
            return
        try:
            while True:
                message = await read_content_length_message(proc.stdout)
                self._handle_message(message)
        except asyncio.CancelledError:
            raise
        except (JsonRpcError, asyncio.IncompleteReadError) as err:
            for future in self._pending.values():
                if not future.done():
                    future.set_exception(LSPError(str(err)))

    async def _stderr_loop(self) -> None:
        proc = self._proc
        if proc is None or proc.stderr is None:
            return
        while True:
            line = await proc.stderr.readline()
            if not line:
                return
            self._stderr_tail.append(line.decode("utf-8", errors="replace").rstrip())

    def _handle_message(self, message: JSON) -> None:
        if "id" in message and ("result" in message or "error" in message):
            raw_id = message.get("id")
            if isinstance(raw_id, int | str):
                future = self._pending.get(raw_id)
                if future is not None and not future.done():
                    if "error" in message:
                        future.set_exception(LSPError(format_error(message["error"])))
                    else:
                        future.set_result(message.get("result"))
            return

        method = message.get("method")
        if method == "textDocument/publishDiagnostics":
            self._record_diagnostics(message.get("params"))
            return

        raw_id = message.get("id")
        if isinstance(raw_id, int | str) and isinstance(method, str):
            task = asyncio.create_task(
                self._send(
                    {
                        "jsonrpc": "2.0",
                        "id": raw_id,
                        "result": _server_request_result(
                            method,
                            message.get("params"),
                        ),
                    }
                )
            )
            self._background.add(task)
            task.add_done_callback(self._background.discard)

    def _record_diagnostics(self, params_: Any) -> None:
        if not isinstance(params_, dict):
            return
        uri = params_.get("uri")
        diagnostics = params_.get("diagnostics")
        if isinstance(uri, str) and isinstance(diagnostics, list):
            self._diagnostics[uri] = [
                cast("JSON", item) for item in diagnostics if isinstance(item, dict)
            ]

    def _record_server_info(self, result: Mapping[str, Any]) -> None:
        server_info = result.get("serverInfo")
        if not isinstance(server_info, dict):
            return
        name = server_info.get("name")
        version = server_info.get("version")
        detail = " ".join(str(x) for x in (name, version) if x)
        if detail:
            self._stderr_tail.append(f"initialized {detail}")

    def _raise_crashed(self) -> None:
        code = None if self._proc is None else self._proc.returncode
        tail = "\n".join(self._stderr_tail)
        msg = f"{self.spec.language} language server is not running"
        if code is not None:
            msg = f"{msg} (exit code {code})"
        if tail:
            msg = f"{msg}\n{tail}"
        raise LSPError(msg)


class LSPManager:
    """Manage long-lived language-server subprocesses for Bertrand MCP tools."""

    def __init__(self, *, workspace: Path, artifacts: Path) -> None:
        self.workspace = _check_root(workspace)
        self.artifacts = artifacts.expanduser().resolve()
        self._sessions: dict[Language, _LanguageServerSession] = {}
        self._cleanup_tasks: set[asyncio.Task[None]] = set()

    @classmethod
    def from_environment(cls) -> LSPManager:
        """Create a manager from MCP environment variables.

        Returns
        -------
        LSPManager
            Manager rooted at the configured workspace and artifact directories.
        """
        workspace = Path(os.environ.get(MCP_WORKSPACE_ENV, WORKTREE_MOUNT.as_posix()))
        artifacts = Path(
            os.environ.get(MCP_ARTIFACTS_ENV, CONTAINER_ARTIFACT_MOUNT.as_posix())
        )
        return cls(workspace=workspace, artifacts=artifacts)

    def status(self, language: str | None = None) -> JSON:
        """Return language-server status without starting new subprocesses.

        Parameters
        ----------
        language : str | None, optional
            Optional language name to inspect.  Defaults to all languages.

        Returns
        -------
        JSON
            Serializable status payload.
        """
        if language is not None:
            lang = _check_language(language)
            return self._session(lang).status()
        return {lang: self._session(lang).status() for lang in sorted(_LANGUAGES)}

    async def warm(self, language: str) -> None:
        """Start one language server without issuing a semantic request.

        Parameters
        ----------
        language : str
            Language-server key to start.
        """
        await self._session(_check_language(language)).start()

    async def hover(
        self,
        language: str,
        path: str,
        line: int,
        column: int,
    ) -> JSON | None:
        """Return hover information for a workspace position.

        Parameters
        ----------
        language : str
            Language-server key.
        path : str
            Workspace-relative source path.
        line : int
            One-based source line.
        column : int
            One-based source column.

        Returns
        -------
        JSON | None
            Normalized hover payload when the server has hover information.

        Raises
        ------
        LSPError
            If the request cannot be completed.
        """
        session, document, position = await self._document_position(
            language,
            path,
            line,
            column,
        )
        result = await session.request(
            "textDocument/hover",
            {
                "textDocument": {"uri": document.uri},
                "position": position,
            },
        )
        if result is None:
            return None
        if not isinstance(result, dict):
            msg = f"hover response must be an object, got {type(result).__name__}"
            raise LSPError(msg)
        payload = _range_for_document(document, result.get("range"))
        payload["contents"] = _markup_text(result.get("contents"))
        return payload

    async def definition(
        self,
        language: str,
        path: str,
        line: int,
        column: int,
    ) -> list[JSON]:
        """Return definition locations for a workspace position.

        Parameters
        ----------
        language : str
            Language-server key.
        path : str
            Workspace-relative source path.
        line : int
            One-based source line.
        column : int
            One-based source column.

        Returns
        -------
        list[JSON]
            Normalized definition locations.
        """
        return await self._locations(
            language,
            path,
            line,
            column,
            method="textDocument/definition",
            params={},
        )

    async def references(
        self,
        language: str,
        path: str,
        line: int,
        column: int,
        *,
        include_declaration: bool = True,
    ) -> list[JSON]:
        """Return reference locations for a workspace position.

        Parameters
        ----------
        language : str
            Language-server key.
        path : str
            Workspace-relative source path.
        line : int
            One-based source line.
        column : int
            One-based source column.
        include_declaration : bool, optional
            Whether to include the declaration location in the response.

        Returns
        -------
        list[JSON]
            Normalized reference locations.
        """
        return await self._locations(
            language,
            path,
            line,
            column,
            method="textDocument/references",
            params={"context": {"includeDeclaration": include_declaration}},
        )

    async def document_symbols(self, language: str, path: str) -> list[JSON]:
        """Return document symbols for one workspace file.

        Parameters
        ----------
        language : str
            Language-server key.
        path : str
            Workspace-relative source path.

        Returns
        -------
        list[JSON]
            Normalized document symbols.

        Raises
        ------
        LSPError
            If the language server returns an unsupported response shape.
        """
        session = self._session(_check_language(language))
        document = await session.ensure_document(self._path(path))
        result = await session.request(
            "textDocument/documentSymbol",
            {"textDocument": {"uri": document.uri}},
        )
        if result is None:
            return []
        if not isinstance(result, list):
            msg = f"documentSymbol response must be a list, got {type(result).__name__}"
            raise LSPError(msg)
        return _document_symbols(
            result,
            document=document,
            document_lookup=self._document,
        )

    async def workspace_symbols(self, language: str, query: str) -> list[JSON]:
        """Return workspace symbols matching a query.

        Parameters
        ----------
        language : str
            Language-server key.
        query : str
            Symbol query.

        Returns
        -------
        list[JSON]
            Normalized workspace symbols.

        Raises
        ------
        LSPError
            If the language server returns an unsupported response shape.
        """
        session = self._session(_check_language(language))
        result = await session.request("workspace/symbol", {"query": query})
        if result is None:
            return []
        if not isinstance(result, list):
            actual = type(result).__name__
            msg = f"workspace/symbol response must be a list, got {actual}"
            raise LSPError(msg)
        return [
            payload
            for item in result
            if isinstance(item, dict)
            for payload in [_symbol_information(item, document_lookup=self._document)]
            if payload is not None
        ]

    async def diagnostics(self, language: str, path: str) -> list[JSON]:
        """Return cached diagnostics for one workspace file.

        Parameters
        ----------
        language : str
            Language-server key.
        path : str
            Workspace-relative source path.

        Returns
        -------
        list[JSON]
            Normalized diagnostics published by the server.
        """
        session = self._session(_check_language(language))
        document = await session.ensure_document(self._path(path))
        await asyncio.sleep(_DIAGNOSTIC_SETTLE_SECONDS)
        return [
            _diagnostic_payload(
                item,
                document=document,
                document_lookup=self._document,
            )
            for item in session.diagnostics(document)
        ]

    async def completion(
        self,
        language: str,
        path: str,
        line: int,
        column: int,
    ) -> list[JSON]:
        """Return completion items for a workspace position.

        Parameters
        ----------
        language : str
            Language-server key.
        path : str
            Workspace-relative source path.
        line : int
            One-based source line.
        column : int
            One-based source column.

        Returns
        -------
        list[JSON]
            Normalized completion candidates.

        Raises
        ------
        LSPError
            If the language server returns an unsupported response shape.
        """
        session, document, position = await self._document_position(
            language,
            path,
            line,
            column,
        )
        result = await session.request(
            "textDocument/completion",
            {
                "textDocument": {"uri": document.uri},
                "position": position,
            },
        )
        items: Sequence[Any]
        if result is None:
            items = ()
        elif isinstance(result, list):
            items = result
        elif isinstance(result, dict) and isinstance(result.get("items"), list):
            items = cast("Sequence[Any]", result["items"])
        else:
            msg = f"completion response has unsupported shape: {type(result).__name__}"
            raise LSPError(msg)
        return [_completion_payload(item) for item in items if isinstance(item, dict)]

    async def aclose(self) -> None:
        """Close all running language-server subprocesses."""
        await asyncio.gather(
            *(session.close() for session in self._sessions.values()),
            return_exceptions=True,
        )

    def close_sync(self) -> None:
        """Best-effort synchronous cleanup hook for process shutdown."""
        if not self._sessions:
            return
        try:
            loop = asyncio.get_running_loop()
        except RuntimeError:
            asyncio.run(self.aclose())
        else:
            task = loop.create_task(self.aclose())
            self._cleanup_tasks.add(task)
            task.add_done_callback(self._cleanup_tasks.discard)

    async def _locations(
        self,
        language: str,
        path: str,
        line: int,
        column: int,
        *,
        method: str,
        params: JSON,
    ) -> list[JSON]:
        session, document, position = await self._document_position(
            language,
            path,
            line,
            column,
        )
        result = await session.request(
            method,
            {
                "textDocument": {"uri": document.uri},
                "position": position,
                **params,
            },
        )
        if result is None:
            return []
        if isinstance(result, dict):
            items: Sequence[Any] = (result,)
        elif isinstance(result, list):
            items = result
        else:
            msg = f"{method} response has unsupported shape: {type(result).__name__}"
            raise LSPError(msg)
        return [
            location
            for item in items
            if isinstance(item, dict)
            for location in [_location_payload(item, document_lookup=self._document)]
            if location is not None
        ]

    async def _document_position(
        self,
        language: str,
        path: str,
        line: int,
        column: int,
    ) -> tuple[_LanguageServerSession, _DocumentState, JSON]:
        lang = _check_language(language)
        session = self._session(lang)
        document = await session.ensure_document(self._path(path))
        position = _position_payload(document.text, line=line, column=column)
        return session, document, position

    def _session(self, language: Language) -> _LanguageServerSession:
        session = self._sessions.get(language)
        if session is None:
            session = _LanguageServerSession(
                _server_spec(
                    language,
                    workspace=self.workspace,
                    artifacts=self.artifacts,
                )
            )
            self._sessions[language] = session
        return session

    def _path(self, path: str) -> Path:
        candidate = Path(path)
        if candidate.is_absolute():
            resolved = candidate.resolve()
        else:
            resolved = (self.workspace / candidate).resolve()
        try:
            resolved.relative_to(self.workspace)
        except ValueError as err:
            msg = f"workspace path escapes {self.workspace}: {path!r}"
            raise LSPError(msg) from err
        return resolved

    def _document(self, uri: str) -> _DocumentState | None:
        path = _path_from_uri(uri)
        if path is None:
            return None
        for session in self._sessions.values():
            document = session.document(path)
            if document is not None:
                return document
        try:
            checked = self._path(path.as_posix())
        except LSPError:
            return None
        if not checked.exists():
            return None
        return _read_document(checked, workspace=self.workspace)


async def _run_daemon(
    *,
    socket: Path,
    manager: LSPManager,
    stop: asyncio.Event,
) -> None:
    _prepare_daemon_socket(socket)
    server = await asyncio.start_unix_server(
        lambda reader, writer: _handle_daemon_client(
            reader,
            writer,
            manager=manager,
            stop=stop,
        ),
        path=socket,
    )
    try:
        await _warm_daemon_servers(manager)
        async with server:
            await stop.wait()
    finally:
        server.close()
        await server.wait_closed()
        await manager.aclose()
        with suppress(OSError):
            socket.unlink()


async def _warm_daemon_servers(manager: LSPManager) -> None:
    try:
        await manager.warm("python")
    except LSPError as err:
        _warn(f"Python LSP startup failed: {err}")

    status = manager.status("cpp")
    missing = status.get("missing_artifacts")
    if isinstance(missing, list) and missing:
        paths = ", ".join(str(path) for path in missing)
        _warn(f"C++ LSP artifacts are missing; clangd was not started: {paths}")
        return
    try:
        await manager.warm("cpp")
    except LSPError as err:
        _warn(f"C++ LSP startup failed: {err}")


async def _handle_daemon_client(
    reader: asyncio.StreamReader,
    writer: asyncio.StreamWriter,
    *,
    manager: LSPManager,
    stop: asyncio.Event,
) -> None:
    try:
        while line := await reader.readline():
            response = await _dispatch_daemon_line(line, manager=manager, stop=stop)
            writer.write(json.dumps(response, separators=(",", ":")).encode())
            writer.write(b"\n")
            await writer.drain()
    finally:
        writer.close()
        await writer.wait_closed()


async def _dispatch_daemon_line(
    line: bytes,
    *,
    manager: LSPManager,
    stop: asyncio.Event,
) -> JSON:
    request_id: object = None
    try:
        request_id, method, request_params = _decode_request(line)
        result = await _dispatch_daemon_request(
            method,
            request_params,
            manager=manager,
            stop=stop,
        )
    except (
        json.JSONDecodeError,
        LSPError,
        OSError,
        TimeoutError,
        TypeError,
        UnicodeDecodeError,
        ValueError,
    ) as err:
        return {
            "id": request_id,
            "error": {
                "message": str(err),
                "type": type(err).__name__,
            },
        }
    return {"id": request_id, "result": result}


async def _dispatch_daemon_request(
    method: str,
    request_params: JSON,
    *,
    manager: LSPManager,
    stop: asyncio.Event,
) -> Any:
    match method.removeprefix("lsp_"):
        case "status":
            language = request_params.get("language")
            return manager.status(language if isinstance(language, str) else None)
        case "hover":
            return await manager.hover(**_position_params(request_params))
        case "definition":
            return await manager.definition(**_position_params(request_params))
        case "references":
            return await manager.references(
                **_position_params(request_params),
                include_declaration=bool(
                    request_params.get("include_declaration", True),
                ),
            )
        case "document_symbols":
            return await manager.document_symbols(**_path_params(request_params))
        case "workspace_symbols":
            return await manager.workspace_symbols(
                _language(request_params),
                _string(request_params, "query"),
            )
        case "diagnostics":
            return await manager.diagnostics(**_path_params(request_params))
        case "completion":
            return await manager.completion(**_position_params(request_params))
        case "shutdown":
            stop.set()
            return {"ok": True}
        case _:
            msg = f"unsupported LSP daemon method: {method}"
            raise ValueError(msg)


def _prepare_daemon_socket(socket: Path) -> None:
    socket.parent.mkdir(parents=True, exist_ok=True)
    if not socket.exists():
        return
    if socket.is_socket():
        socket.unlink()
        return
    msg = f"refusing to replace non-socket LSP daemon path: {socket}"
    raise OSError(msg)


def daemon_main(argv: list[str] | None = None) -> None:
    """Console entry point for Bertrand's shell LSP daemon.

    Parameters
    ----------
    argv : list[str] | None
        Command-line arguments. Defaults to None, which means ``sys.argv``.
    """
    parser = argparse.ArgumentParser(
        prog="bertrand-lsp-daemon",
        description="Run Bertrand's internal shell-scoped LSP daemon.",
    )
    parser.add_argument(
        "--socket",
        type=Path,
        default=LSP_SOCKET_PATH,
        help="Unix socket path for normalized LSP requests.",
    )
    args = parser.parse_args(argv)
    manager = LSPManager(
        workspace=Path(os.environ.get(MCP_WORKSPACE_ENV, WORKTREE_MOUNT.as_posix())),
        artifacts=Path(
            os.environ.get(MCP_ARTIFACTS_ENV, CONTAINER_ARTIFACT_MOUNT.as_posix())
        ),
    )
    stop = asyncio.Event()
    loop = asyncio.new_event_loop()
    try:
        asyncio.set_event_loop(loop)
        _install_signal_handlers(loop, stop)
        loop.run_until_complete(
            _run_daemon(socket=args.socket, manager=manager, stop=stop)
        )
    finally:
        asyncio.set_event_loop(None)
        loop.close()


def _server_spec(
    language: Language,
    *,
    workspace: Path,
    artifacts: Path,
) -> _ServerSpec:
    if language == "python":
        return _ServerSpec(
            language=language,
            command=("ty", "server"),
            workspace=workspace,
            artifacts=artifacts,
        )
    return _ServerSpec(
        language=language,
        command=(
            "clangd",
            f"--compile-commands-dir={artifacts.as_posix()}",
            f"--config-file={artifacts / '.clangd'}",
        ),
        workspace=workspace,
        artifacts=artifacts,
        required_artifacts=(
            artifacts / "compile_commands.json",
            artifacts / ".clangd",
        ),
    )


def _check_language(language: str) -> Language:
    normalized = language.strip().lower()
    if normalized not in _LANGUAGES:
        options = ", ".join(sorted(_LANGUAGES))
        msg = f"unsupported LSP language {language!r}; expected one of: {options}"
        raise LSPError(msg)
    return cast("Language", normalized)


def _check_root(path: Path) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.exists():
        msg = f"MCP workspace root does not exist: {resolved}"
        raise LSPError(msg)
    if not resolved.is_dir():
        msg = f"MCP workspace root is not a directory: {resolved}"
        raise LSPError(msg)
    return resolved


def _server_env() -> dict[str, str]:
    env = dict(os.environ)
    env.setdefault("XDG_CACHE_HOME", "/tmp/.cache")
    return env


def _initialize_params(spec: _ServerSpec) -> JSON:
    root_uri = spec.workspace.as_uri()
    return {
        "processId": os.getpid(),
        "rootUri": root_uri,
        "workspaceFolders": [
            {
                "uri": root_uri,
                "name": "bertrand",
            },
        ],
        "capabilities": {
            "workspace": {
                "workspaceFolders": True,
                "symbol": {"dynamicRegistration": False},
                "configuration": False,
            },
            "textDocument": {
                "synchronization": {
                    "dynamicRegistration": False,
                    "willSave": False,
                    "willSaveWaitUntil": False,
                    "didSave": False,
                },
                "hover": {
                    "dynamicRegistration": False,
                    "contentFormat": ["markdown", "plaintext"],
                },
                "definition": {"dynamicRegistration": False},
                "references": {"dynamicRegistration": False},
                "documentSymbol": {
                    "dynamicRegistration": False,
                    "hierarchicalDocumentSymbolSupport": True,
                },
                "publishDiagnostics": {
                    "relatedInformation": True,
                    "versionSupport": True,
                },
                "completion": {
                    "dynamicRegistration": False,
                    "completionItem": {
                        "documentationFormat": ["markdown", "plaintext"],
                    },
                },
            },
            "general": {"positionEncodings": ["utf-16"]},
        },
    }


def _read_document(path: Path, *, workspace: Path) -> _DocumentState:
    resolved = path.resolve()
    if not resolved.is_file():
        msg = f"workspace file does not exist: {resolved}"
        raise LSPError(msg)
    try:
        resolved.relative_to(workspace)
    except ValueError as err:
        msg = f"workspace file escapes {workspace}: {resolved}"
        raise LSPError(msg) from err
    stat = resolved.stat()
    text = resolved.read_text(encoding="utf-8")
    return _DocumentState(
        path=resolved,
        uri=resolved.as_uri(),
        language_id=_language_id(resolved),
        text=text,
        mtime_ns=stat.st_mtime_ns,
        size=stat.st_size,
    )


def _position_payload(text: str, *, line: int, column: int) -> JSON:
    if line < 1:
        msg = "LSP line numbers are 1-based and must be positive"
        raise LSPError(msg)
    if column < 1:
        msg = "LSP columns are 1-based and must be positive"
        raise LSPError(msg)
    lines = text.splitlines()
    if line > len(lines) + (0 if text.endswith("\n") else 1):
        msg = f"line {line} is outside document range"
        raise LSPError(msg)
    line_text = lines[line - 1] if line - 1 < len(lines) else ""
    return {
        "line": line - 1,
        "character": _utf16_units(line_text[: column - 1]),
    }


def _range_payload(value: Any) -> JSON:
    if not isinstance(value, dict):
        return {}
    start = value.get("start")
    end = value.get("end")
    if not isinstance(start, dict) or not isinstance(end, dict):
        return {}
    return {
        "line": _lsp_line(start),
        "column": _lsp_character(start),
        "end_line": _lsp_line(end),
        "end_column": _lsp_character(end),
    }


def _range_for_document(document: _DocumentState, value: Any) -> JSON:
    if not isinstance(value, dict):
        return {}
    start = value.get("start")
    end = value.get("end")
    if not isinstance(start, dict) or not isinstance(end, dict):
        return {}
    start_line = _lsp_line(start)
    end_line = _lsp_line(end)
    lines = document.text.splitlines()
    start_text = lines[start_line - 1] if start_line - 1 < len(lines) else ""
    end_text = lines[end_line - 1] if end_line - 1 < len(lines) else ""
    return {
        "line": start_line,
        "column": _column_from_utf16(start_text, _lsp_character(start) - 1),
        "end_line": end_line,
        "end_column": _column_from_utf16(end_text, _lsp_character(end) - 1),
    }


def _range_for_uri(
    uri: str,
    value: JSON,
    *,
    document_lookup: _DocumentLookup,
) -> JSON:
    start = value.get("start")
    end = value.get("end")
    if not isinstance(start, dict) or not isinstance(end, dict):
        return {}
    document = document_lookup(uri)
    start_line = _lsp_line(start)
    end_line = _lsp_line(end)
    if document is None:
        return {
            "line": start_line,
            "column": _lsp_character(start),
            "end_line": end_line,
            "end_column": _lsp_character(end),
        }
    lines = document.text.splitlines()
    start_text = lines[start_line - 1] if start_line - 1 < len(lines) else ""
    end_text = lines[end_line - 1] if end_line - 1 < len(lines) else ""
    return {
        "line": start_line,
        "column": _column_from_utf16(start_text, _lsp_character(start) - 1),
        "end_line": end_line,
        "end_column": _column_from_utf16(end_text, _lsp_character(end) - 1),
    }


def _location_payload(
    value: JSON,
    *,
    document_lookup: _DocumentLookup,
) -> JSON | None:
    uri = value.get("targetUri") if "targetUri" in value else value.get("uri")
    range_value = (
        value.get("targetSelectionRange")
        or value.get("targetRange")
        or value.get("range")
    )
    if not isinstance(uri, str) or not isinstance(range_value, dict):
        return None
    payload = _range_for_uri(uri, range_value, document_lookup=document_lookup)
    path = _path_from_uri(uri)
    if path is not None:
        payload["path"] = _display_path(path)
    return payload


def _document_symbols(
    values: Sequence[Any],
    *,
    document: _DocumentState | None,
    document_lookup: _DocumentLookup,
) -> list[JSON]:
    out: list[JSON] = []
    for item in values:
        if not isinstance(item, dict):
            continue
        if "location" in item:
            symbol = _symbol_information(item, document_lookup=document_lookup)
            if symbol is not None:
                out.append(symbol)
            continue
        name = item.get("name")
        if not isinstance(name, str):
            continue
        payload: JSON = {
            "name": name,
            "kind": item.get("kind"),
        }
        detail = item.get("detail")
        if isinstance(detail, str) and detail:
            payload["detail"] = detail
        selection = item.get("selectionRange") or item.get("range")
        if isinstance(selection, dict):
            if document is None:
                payload.update(_range_payload(selection))
            else:
                payload.update(_range_for_document(document, selection))
        children = item.get("children")
        if isinstance(children, list):
            nested = _document_symbols(
                children,
                document=document,
                document_lookup=document_lookup,
            )
            if nested:
                payload["children"] = nested
        out.append(payload)
    return out


def _symbol_information(
    value: JSON,
    *,
    document_lookup: _DocumentLookup,
) -> JSON | None:
    name = value.get("name")
    location = value.get("location")
    if not isinstance(name, str) or not isinstance(location, dict):
        return None
    payload: JSON = {
        "name": name,
        "kind": value.get("kind"),
    }
    container = value.get("containerName")
    if isinstance(container, str) and container:
        payload["detail"] = container
    loc = _location_payload(location, document_lookup=document_lookup)
    if loc is not None:
        payload.update(loc)
    return payload


def _diagnostic_payload(
    value: JSON,
    *,
    document: _DocumentState,
    document_lookup: _DocumentLookup,
) -> JSON:
    payload = _range_for_document(document, value.get("range"))
    payload["message"] = str(value.get("message", ""))
    for key in ("severity", "source", "code"):
        item = value.get(key)
        if isinstance(item, str | int):
            payload[key] = item
    related = value.get("relatedInformation")
    if isinstance(related, list):
        payload["related"] = [
            item
            for entry in related
            if isinstance(entry, dict)
            for item in [_related_diagnostic(entry, document_lookup=document_lookup)]
            if item is not None
        ]
    return payload


def _related_diagnostic(
    value: JSON,
    *,
    document_lookup: _DocumentLookup,
) -> JSON | None:
    location = value.get("location")
    if not isinstance(location, dict):
        return None
    payload = _location_payload(location, document_lookup=document_lookup)
    if payload is None:
        return None
    message = value.get("message")
    if isinstance(message, str):
        payload["message"] = message
    return payload


def _completion_payload(value: JSON) -> JSON:
    payload: JSON = {"label": str(value.get("label", ""))}
    for key in ("kind", "detail", "sortText", "filterText", "insertText"):
        item = value.get(key)
        if isinstance(item, str | int):
            payload[key] = item
    documentation = value.get("documentation")
    if documentation is not None:
        payload["documentation"] = _markup_text(documentation)
    return payload


def _markup_text(value: Any) -> str:
    if isinstance(value, str):
        return value
    if isinstance(value, dict):
        nested = value.get("value")
        if isinstance(nested, str):
            return nested
    if isinstance(value, list):
        return "\n\n".join(_markup_text(item) for item in value)
    return ""


def _server_request_result(method: str, request_params: Any) -> Any:
    if method == "workspace/configuration":
        items = (
            request_params.get("items") if isinstance(request_params, dict) else None
        )
        if isinstance(items, list):
            return [{} for _ in items]
        return []
    if method == "workspace/applyEdit":
        return {"applied": False}
    return None


def _install_signal_handlers(
    loop: asyncio.AbstractEventLoop,
    stop: asyncio.Event,
) -> None:
    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, stop.set)
        except NotImplementedError:
            signal.signal(sig, lambda *_: stop.set())


def _decode_request(line: bytes) -> tuple[object, str, JSON]:
    payload = json.loads(line.decode("utf-8"))
    if not isinstance(payload, dict):
        msg = "daemon requests must be JSON objects"
        raise TypeError(msg)
    request_id = payload.get("id")
    method = payload.get("method")
    request_params = payload.get("params", {})
    if not isinstance(method, str):
        msg = "daemon request is missing string field 'method'"
        raise TypeError(msg)
    if not isinstance(request_params, dict):
        msg = "daemon request field 'params' must be an object"
        raise TypeError(msg)
    return request_id, method, request_params


def _position_params(request_params: JSON) -> JSON:
    return {
        "language": _language(request_params),
        "path": _string(request_params, "path"),
        "line": _int(request_params, "line"),
        "column": _int(request_params, "column"),
    }


def _path_params(request_params: JSON) -> JSON:
    return {
        "language": _language(request_params),
        "path": _string(request_params, "path"),
    }


def _language(request_params: JSON) -> str:
    return _string(request_params, "language")


def _string(request_params: JSON, key: str) -> str:
    value = request_params.get(key)
    if not isinstance(value, str) or not value:
        msg = f"daemon request param {key!r} must be a non-empty string"
        raise TypeError(msg)
    return value


def _int(request_params: JSON, key: str) -> int:
    value = request_params.get(key)
    if not isinstance(value, int):
        msg = f"daemon request param {key!r} must be an integer"
        raise TypeError(msg)
    return value


def _path_from_uri(uri: str) -> Path | None:
    if not uri.startswith("file://"):
        return None

    parsed = urlparse(uri)
    return Path(unquote(parsed.path)).resolve()


def _display_path(path: Path) -> str:
    try:
        return path.relative_to(WORKTREE_MOUNT).as_posix()
    except ValueError:
        return path.as_posix()


def _lsp_line(position: Mapping[str, Any]) -> int:
    line = position.get("line")
    return int(line) + 1 if isinstance(line, int) else 1


def _lsp_character(position: Mapping[str, Any]) -> int:
    character = position.get("character")
    return int(character) + 1 if isinstance(character, int) else 1


def _column_from_utf16(text: str, character: int) -> int:
    units = 0
    for index, char in enumerate(text):
        width = _utf16_units(char)
        if units + width > character:
            return index + 1
        units += width
    return len(text) + 1


def _language_id(path: Path) -> str:
    suffix = path.suffix.lower()
    if suffix in {".py", ".pyi"}:
        return "python"
    if suffix in {".c", ".h"}:
        return "c"
    if suffix in {".cc", ".cpp", ".cxx", ".hh", ".hpp", ".hxx", ".ixx", ".mxx"}:
        return "cpp"
    return "plaintext"


def _utf16_units(text: str) -> int:
    return len(text.encode("utf-16-le")) // 2


def _warn(message: str) -> None:
    print(f"bertrand-lsp-daemon: warning: {message}", file=sys.stderr)
