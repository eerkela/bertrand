"""Render VS Code workspace configuration.

This resource generates ephemeral VS Code workspace artifacts for development
containers.  The artifact is rendered inside the container context and is not written
to the project worktree.
"""

from __future__ import annotations

import json

from pydantic import BaseModel

from bertrand.env.git import (
    CONTAINER_TMP,
    WORKTREE_MOUNT,
    atomic_write_text,
)
from bertrand.env.mcp.constants import (
    LSP_DAEMON_COMMAND,
    LSP_SOCKET_ENV,
    LSP_SOCKET_PATH,
    MCP_ARGS,
    MCP_ARTIFACTS_ENV,
    MCP_CACHE_ENV,
    MCP_COMMAND,
    MCP_SERVER_KEY,
    MCP_WORKSPACE_ENV,
)

from .core import (
    CACHE_MOUNT,
    Config,
    Resource,
    resource,
)

VSCODE_ARTIFACT_DIR = CONTAINER_TMP / ".vscode"
VSCODE_WORKSPACE_FILE = VSCODE_ARTIFACT_DIR / "workspace.code-workspace"
VSCODE_MCP_FILE = VSCODE_ARTIFACT_DIR / "mcp.json"
DEV_BIN_DIR = CONTAINER_TMP / "bin"
DEV_SHELL_ENTRYPOINT = DEV_BIN_DIR / "bertrand-dev-shell"


def _workspace_config() -> dict[str, object]:
    artifact_path = CONTAINER_TMP.as_posix()
    workspace_path = WORKTREE_MOUNT.as_posix()
    return {
        "folders": [
            {
                "name": "source",
                "path": workspace_path,
            },
            {
                "name": "bertrand-artifacts",
                "path": artifact_path,
            },
        ],
        "settings": {
            "C_Cpp.intelliSenseEngine": "disabled",
            "clangd.path": "clangd",
            "clangd.arguments": [
                f"--compile-commands-dir={artifact_path}",
                f"--config-file={artifact_path}/.clangd",
            ],
            "[python]": {
                "editor.defaultFormatter": "charliermarsh.ruff",
                "editor.formatOnSave": True,
                "editor.codeActionsOnSave": {
                    "source.fixAll.ruff": "explicit",
                    "source.organizeImports.ruff": "explicit",
                },
            },
            "ty.serverMode": "languageServer",
            "python.testing.pytestEnabled": True,
            "python.testing.unittestEnabled": False,
            "python.testing.pytestPath": "pytest",
            "python.testing.pytestArgs": [
                workspace_path,
            ],
        },
        "extensions": {
            "recommendations": [
                "ms-vscode-remote.remote-containers",
                "llvm-vs-code-extensions.vscode-clangd",
                "ms-python.python",
                "charliermarsh.ruff",
                "astral-sh.ty",
            ],
        },
    }


def _mcp_config() -> dict[str, object]:
    return {
        "servers": {
            MCP_SERVER_KEY: {
                "type": "stdio",
                "command": MCP_COMMAND,
                "args": list(MCP_ARGS),
                "env": {
                    MCP_WORKSPACE_ENV: WORKTREE_MOUNT.as_posix(),
                    MCP_ARTIFACTS_ENV: CONTAINER_TMP.as_posix(),
                    MCP_CACHE_ENV: CACHE_MOUNT.as_posix(),
                },
            },
        },
    }


def _dev_shell_script() -> str:
    socket = LSP_SOCKET_PATH.as_posix()
    cache = CACHE_MOUNT.as_posix()
    return f"""#!/usr/bin/env sh
set -u

socket="${{{LSP_SOCKET_ENV}:-{socket}}}"
export {LSP_SOCKET_ENV}="$socket"
export XDG_CACHE_HOME="${{XDG_CACHE_HOME:-{cache}}}"

mkdir -p "$(dirname "$socket")" "$XDG_CACHE_HOME"

if command -v {LSP_DAEMON_COMMAND} >/dev/null 2>&1; then
  {LSP_DAEMON_COMMAND} --socket "$socket" >&2 &
  daemon_pid="$!"
  i=0
  while [ "$i" -lt 50 ]; do
    if [ -S "$socket" ]; then
      break
    fi
    if ! kill -0 "$daemon_pid" 2>/dev/null; then
      break
    fi
    sleep 0.1
    i=$((i + 1))
  done
  if [ ! -S "$socket" ]; then
    echo "bertrand: warning: shell LSP daemon did not publish $socket;" \\
      "semantic shell helpers may be unavailable" >&2
  fi
else
  echo "bertrand: warning: {LSP_DAEMON_COMMAND} not found;" \\
    "semantic shell helpers are unavailable" >&2
fi

if [ "$#" -eq 0 ]; then
  set -- sh
fi

exec "$@"
"""


@resource("vscode")
class VSCodeWorkspace(Resource[BaseModel]):
    """Render VS Code managed workspace artifacts.

    The workspace lets VS Code attach to a running container context through the
    remote-containers extension and mount its internal toolchain without writing
    editor state into the source worktree.
    """

    async def render(
        self,
        config: Config,  # noqa: ARG002
        *,
        image_build: bool,
    ) -> None:
        """Render the workspace and MCP artifacts inside image/dev contexts."""
        if not image_build:
            return
        atomic_write_text(
            VSCODE_WORKSPACE_FILE,
            json.dumps(_workspace_config(), indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        atomic_write_text(
            VSCODE_MCP_FILE,
            json.dumps(_mcp_config(), indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        atomic_write_text(
            DEV_SHELL_ENTRYPOINT,
            _dev_shell_script(),
            encoding="utf-8",
        )
        DEV_SHELL_ENTRYPOINT.chmod(0o755)
