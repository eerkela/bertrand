"""Shared constants for Bertrand's editor-owned MCP and LSP helpers."""

from __future__ import annotations

from bertrand.env.git import CONTAINER_TMP

MCP_SERVER_KEY: str = "bertrand"
MCP_COMMAND: str = "bertrand-mcp"
MCP_ARGS: tuple[str, ...] = ("--transport", "stdio")
MCP_WORKSPACE_ENV: str = "BERTRAND_MCP_WORKSPACE"
MCP_ARTIFACTS_ENV: str = "BERTRAND_MCP_ARTIFACTS"
MCP_CACHE_ENV: str = "XDG_CACHE_HOME"

LSP_DAEMON_COMMAND: str = "bertrand-lsp-daemon"
LSP_SOCKET_ENV: str = "BERTRAND_LSP_SOCKET"
LSP_SOCKET_PATH = CONTAINER_TMP / "run" / "lsp.sock"
