"""MCP transport bootstrap for Bertrand's editor-owned server."""

from __future__ import annotations

import argparse
import atexit
from typing import Any, Literal, cast

from mcp.server.fastmcp import FastMCP

from .constants import MCP_SERVER_KEY
from .lsp import LSPManager

MCP_TRANSPORTS = ("stdio",)
MCPTransport = Literal["stdio"]


def _register_lsp_tools(server: FastMCP, lsp: LSPManager) -> None:
    def lsp_status(language: str | None = None) -> dict[str, Any]:
        return lsp.status(language)

    async def lsp_hover(
        language: str,
        path: str,
        line: int,
        column: int,
    ) -> dict[str, Any] | None:
        return await lsp.hover(language, path, line, column)

    async def lsp_definition(
        language: str,
        path: str,
        line: int,
        column: int,
    ) -> list[dict[str, Any]]:
        return await lsp.definition(language, path, line, column)

    async def lsp_references(
        language: str,
        path: str,
        line: int,
        column: int,
        *,
        include_declaration: bool = True,
    ) -> list[dict[str, Any]]:
        return await lsp.references(
            language,
            path,
            line,
            column,
            include_declaration=include_declaration,
        )

    async def lsp_document_symbols(
        language: str,
        path: str,
    ) -> list[dict[str, Any]]:
        return await lsp.document_symbols(language, path)

    async def lsp_workspace_symbols(
        language: str,
        query: str,
    ) -> list[dict[str, Any]]:
        return await lsp.workspace_symbols(language, query)

    async def lsp_diagnostics(
        language: str,
        path: str,
    ) -> list[dict[str, Any]]:
        return await lsp.diagnostics(language, path)

    async def lsp_completion(
        language: str,
        path: str,
        line: int,
        column: int,
    ) -> list[dict[str, Any]]:
        return await lsp.completion(language, path, line, column)

    server.tool(
        name="lsp_status",
        description="Return status for managed Bertrand language-server sessions.",
    )(lsp_status)
    server.tool(
        name="lsp_hover",
        description="Return hover information for a workspace source position.",
    )(lsp_hover)
    server.tool(
        name="lsp_definition",
        description="Return definition locations for a workspace source position.",
    )(lsp_definition)
    server.tool(
        name="lsp_references",
        description="Return reference locations for a workspace source position.",
    )(lsp_references)
    server.tool(
        name="lsp_document_symbols",
        description="Return semantic symbols for one workspace source file.",
    )(lsp_document_symbols)
    server.tool(
        name="lsp_workspace_symbols",
        description="Return workspace symbols matching a query string.",
    )(lsp_workspace_symbols)
    server.tool(
        name="lsp_diagnostics",
        description="Return diagnostics for one workspace source file.",
    )(lsp_diagnostics)
    server.tool(
        name="lsp_completion",
        description="Return completion candidates for a workspace source position.",
    )(lsp_completion)


def build_server() -> FastMCP:
    """Construct Bertrand's MCP server.

    Returns
    -------
    FastMCP
        Bertrand MCP server with curated tool endpoints registered.
    """
    server = FastMCP(MCP_SERVER_KEY)
    lsp = LSPManager.from_environment()
    atexit.register(lsp.close_sync)
    _register_lsp_tools(server, lsp)
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
        """Add the transport argument to the parser."""
        self._parser.add_argument(
            "--transport",
            choices=MCP_TRANSPORTS,
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
    """Console entry point for Bertrand's MCP server process.

    Parameters
    ----------
    argv : list[str] | None
        Command-line arguments.  Defaults to None, which means to use sys.argv.
    """
    parser = Parser()
    args = parser(argv)
    transport = cast("MCPTransport", args.transport)
    build_server().run(transport=transport)
