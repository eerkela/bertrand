"""Shared constants for Bertrand's editor-owned MCP server."""

from __future__ import annotations

MCP_SERVER_KEY: str = "bertrand"
MCP_COMMAND: str = "bertrand-mcp"
MCP_ARGS: tuple[str, ...] = ("--transport", "stdio")
MCP_WORKSPACE_ENV: str = "BERTRAND_MCP_WORKSPACE"
MCP_ARTIFACTS_ENV: str = "BERTRAND_MCP_ARTIFACTS"

__all__ = [
    "MCP_ARGS",
    "MCP_ARTIFACTS_ENV",
    "MCP_COMMAND",
    "MCP_SERVER_KEY",
    "MCP_WORKSPACE_ENV",
]
