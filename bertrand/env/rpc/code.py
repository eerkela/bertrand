"""An extension for Bertrand's RPC infrastructure that allows containerized workloads
to open text editors on the host machine and mount the container's workspace using
remote container extensions.
"""
from __future__ import annotations

import asyncio
import os
import shutil
import subprocess
import sys
import time
import urllib.parse
import uuid
from collections.abc import Awaitable, Callable
from dataclasses import dataclass, field
from pathlib import Path

from ..config import (
    EDITORS,
    VSCODE_WORKSPACE_FILE,
    Bertrand,
    Config,
)
from ..config.bertrand import Editor
from ..run import (
    IMAGE_TAG_ENV,
    PROJECT_ENV,
    PROJECT_MOUNT,
    WORKTREE_ENV,
    WORKTREE_MOUNT,
    GitRepository,
    run,
)
from .listener import (
    JSON_RPC_VERSION,
    Listener,
    MethodName,
    RPCRequest,
    RPCResponse,
    rpc_method,
)

CODE_OPEN_TIMEOUT: float = 30.0
CODE_OPEN_METHOD: MethodName = "code.open"
VSCODE_REMOTE_EXTENSION = "ms-vscode-remote.remote-containers"


def _resolve_editor_bin(editor: Editor) -> Path:
    candidates = EDITORS.get(editor, [])
    for candidate in candidates:
        resolved = shutil.which(candidate)
        if resolved:
            path = Path(resolved).expanduser().resolve()
            if path.is_file():
                return path
    raise RuntimeError(
        f"failed to resolve host editor alias '{editor}' from configured candidates: "
        f"{candidates}"
    )


async def _vscode_open_request_prereqs(method: CodeOpen, config: Config) -> None:
    _ = config
    if not VSCODE_WORKSPACE_FILE.exists() or not VSCODE_WORKSPACE_FILE.is_file():
        raise RuntimeError(
            "VSCode workspace file not found at expected container path: "
            f"{VSCODE_WORKSPACE_FILE}\nThis file should be automatically created as a "
            "configuration artifact.  If you see this message, try re-running the "
            "`$ bertrand code` command to regenerate the workspace file."
        )

    # check for mounted tools and warn if any are missing
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
        try:
            if method.deadline - time.time() <= 0:
                print(
                    f"bertrand: deadline exhausted before the RPC service could locate "
                    f"'{tool}' inside the container context\n\t{hint}",
                    file=sys.stderr
                )
            elif shutil.which(tool) is None:
                print(
                    f"bertrand: could not locate tool '{tool}' inside container "
                    f"context\n\t{hint}",
                    file=sys.stderr
                )
        except Exception as err:
            print(f"{str(err)}\n\t{hint}", file=sys.stderr)


async def _vscode_open_response(
    listener: Listener,
    params: RPCRequest.CodeOpenRequest,
) -> RPCResponse.CodeOpenResult:
    editor_bin = _resolve_editor_bin(params.editor)

    # check for required remote containers extension on host vscode
    result = await run(
        [str(editor_bin), "--list-extensions"],
        capture_output=True,
        timeout=params.deadline - time.time(),
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

    # form vscode remote containers attach URI
    uri = (
        "vscode-remote://attached-container+"
        f"{urllib.parse.quote(listener.container_id, safe='')}"
        f"{urllib.parse.quote(str(VSCODE_WORKSPACE_FILE), safe='/')}"
    )
    expired = False
    remaining = params.deadline - time.time()
    if remaining <= 0:
        expired = True
    else:
        # if directed, block as long as the editor is open, but do not preventing
        # concurrent requests in the meantime.  Otherwise, open in a non-blocking child
        # process
        if params.block:
            try:
                proc = await asyncio.wait_for(
                    asyncio.create_subprocess_exec(
                        str(editor_bin),
                        "--file-uri",
                        uri,
                        stdin=subprocess.DEVNULL,
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                    ),
                    timeout=max(0.001, remaining),
                )
                returncode = await proc.wait()
                if returncode != 0:
                    raise RuntimeError(
                        f"VSCode process exited with status {returncode} while "
                        "handling block=True request"
                    )
            except TimeoutError:
                expired = True
        else:
            try:
                subprocess.Popen(
                    [str(editor_bin), "--file-uri", uri],
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    start_new_session=True,
                )
            except OSError as err:
                raise RuntimeError(
                    f"failed to launch VSCode container attach session: {err}",
                ) from err
    if expired:
        raise TimeoutError(
            "deadline exhausted before the RPC service could launch the VSCode editor"
        )

    return RPCResponse.CodeOpenResult(success=True)


CODE_OPEN_PREREQS: dict[str, Callable[[CodeOpen, Config], Awaitable[None]]] = {
    "vscode": _vscode_open_request_prereqs,
}
CODE_OPEN: dict[
    str,
    Callable[[Listener, RPCRequest.CodeOpenRequest], Awaitable[RPCResponse.CodeOpenResult]],
] = {
    "vscode": _vscode_open_response,
}


@rpc_method(CODE_OPEN_METHOD)
@dataclass(frozen=True)
class CodeOpen:
    """Request object for the `code.open` RPC method."""
    deadline: float = field(default_factory=lambda: time.time() + CODE_OPEN_TIMEOUT)
    block: bool = False
    editor: Editor | None = None

    async def request(self) -> RPCRequest:
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
        ValueError
            If the editor specified in the configuration is not supported, or if any
            required parameters are invalid.
        """
        # load host project path from environment
        _project = os.environ.get(PROJECT_ENV)
        if _project is None:
            raise RuntimeError(
                "project environment variable is missing.  This should never "
                "occur; if you see this message, try re-entering the environment to "
                "regenerate its environment variables, or report an issue if the "
                "problem persists."
            )
        project = Path(_project.strip())
        if not project.is_absolute():
            raise RuntimeError(f"project path must be absolute: {project}")
        project = project.expanduser().resolve()
        if not project.exists() or not project.is_dir():
            raise RuntimeError(f"project path does not exist or is not a directory: {project}")

        # extend project path with relative worktree path from environment
        _worktree = os.environ.get(WORKTREE_ENV)
        if _worktree is None:
            raise RuntimeError(
                "worktree environment variable is missing.  This should never "
                "occur; if you see this message, try re-entering the environment to "
                "regenerate its environment variables, or report an issue if the "
                "problem persists."
            )
        raw_worktree = _worktree.strip()
        if not raw_worktree:
            raise RuntimeError("worktree path must not be empty")
        rel_worktree = Path(raw_worktree)
        if rel_worktree.is_absolute():
            raise RuntimeError(f"worktree path must be relative to project root: {raw_worktree}")
        if any(part == ".." for part in rel_worktree.parts):
            raise RuntimeError(f"worktree path cannot traverse parents: {raw_worktree}")
        if raw_worktree != "." and any(part == "." for part in rel_worktree.parts):
            raise RuntimeError(f"worktree path cannot contain '.' segments: {raw_worktree}")
        worktree = (project / rel_worktree).resolve()
        if not worktree.is_relative_to(project):
            raise RuntimeError(
                f"resolved worktree escapes project root: {worktree} (project={project})"
            )
        if not worktree.exists() or not worktree.is_dir():
            raise RuntimeError(f"resolved worktree does not exist: {worktree}")

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
        async with await Config.load(WORKTREE_MOUNT, repo=GitRepository(
            git_dir=PROJECT_MOUNT / ".git",
        )) as config:
            await config.sync(image_tag)  # ensure config is up-to-date
            bertrand = config.get(Bertrand)
            if not bertrand:
                raise RuntimeError(
                    f"Bertrand configuration is missing from the worktree config at "
                    f"{worktree}.  This should never occur; if you see this message, "
                    "try re-running `bertrand init` to regenerate your project "
                    "configuration, or report an issue if the problem persists."
                )

            # run editor-specific prechecks while inside container context
            editor = self.editor or bertrand.editor
            prereqs = CODE_OPEN_PREREQS.get(editor)
            if prereqs is None or editor not in CODE_OPEN:
                raise ValueError(f"unsupported editor for code.open RPC method: {editor}")
            await prereqs(self, config)

        return RPCRequest(
            jsonrpc=JSON_RPC_VERSION,
            id=uuid.uuid4().hex,
            method=CODE_OPEN_METHOD,
            params=RPCRequest.CodeOpenRequest(
                worktree=worktree,
                editor=editor,
                deadline=self.deadline,
                block=self.block,
            ),
        )

    @staticmethod
    async def response(listener: Listener, request: RPCRequest) -> RPCResponse:
        """Handle the `code.open` RPC request on the host side.

        Parameters
        ----------
        listener : Listener
            The host-side RPC listener instance that is handling this request, which
            can be used to access shared metadata and utilities needed to process the
            request.
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
            result=await CODE_OPEN[request.params.editor](listener, request.params),
        )
