"""Render VS Code workspace configuration.

This resource generates ephemeral VS Code workspace artifacts for development
containers.  The artifact is rendered inside the container context and is not written
to the project worktree.
"""

from __future__ import annotations

import json

from bertrand.env.git import (
    CONTAINER_ARTIFACT_MOUNT,
    WORKTREE_MOUNT,
    atomic_write_text,
)
from bertrand.env.mcp.constants import (
    MCP_ARGS,
    MCP_ARTIFACTS_ENV,
    MCP_COMMAND,
    MCP_SERVER_KEY,
    MCP_WORKSPACE_ENV,
)

from .core import (
    Config,
    Resource,
    resource,
)

VSCODE_ARTIFACT_DIR = CONTAINER_ARTIFACT_MOUNT / ".vscode"
VSCODE_WORKSPACE_FILE = VSCODE_ARTIFACT_DIR / "workspace.code-workspace"
VSCODE_MCP_FILE = VSCODE_ARTIFACT_DIR / "mcp.json"


def _workspace_config() -> dict[str, object]:
    artifact_path = CONTAINER_ARTIFACT_MOUNT.as_posix()
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
            "ty.disableLanguageServices": True,
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
                    MCP_ARTIFACTS_ENV: CONTAINER_ARTIFACT_MOUNT.as_posix(),
                },
            },
        },
    }


@resource("vscode")
class VSCodeWorkspace(Resource):
    """Render VS Code managed workspace artifacts.

    The workspace lets VS Code attach to a running container context through the
    remote-containers extension and mount its internal toolchain without writing
    editor state into the source worktree.
    """

    async def render(self, config: Config, *, image_build: bool) -> None:
        """Render the workspace and MCP artifacts inside image/dev contexts."""
        if not image_build:
            return
        del config
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
