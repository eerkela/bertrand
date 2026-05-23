"""MCP transport bootstrap for Bertrand's editor-owned server."""

from __future__ import annotations

import argparse
from typing import Literal, cast

from mcp.server.fastmcp import FastMCP

from .constants import MCP_SERVER_KEY

MCP_TRANSPORTS = ("stdio",)
MCPTransport = Literal["stdio"]


def build_server() -> FastMCP:
    """Construct Bertrand's MCP server.

    Returns
    -------
    FastMCP
        Empty Bertrand MCP server.  Tool endpoints are registered in later feature
        passes after the server lifecycle is stable.
    """
    return FastMCP(MCP_SERVER_KEY)


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
