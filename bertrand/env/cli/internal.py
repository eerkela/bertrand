"""Bertrand's in-container CLI entry point."""
from __future__ import annotations

import argparse
import asyncio
import os

from ..config import Config
from ..rpc import CodeOpen, rpc
from ..run import (
    CONTAINER_TMP_MOUNT,
    IMAGE_TAG_ENV,
    WORKTREE_MOUNT,
    inside_container,
)
from ..version import __version__


def _require_active_image_tag() -> str:
    tag = os.environ.get(IMAGE_TAG_ENV, "").strip()
    if not tag:
        raise OSError(
            f"missing active image tag in container environment: '{IMAGE_TAG_ENV}'"
        )
    return tag


class Internal:
    """Internal CLI for Bertrand."""

    class Parser:
        """Internal command-line parser for Bertrand's CLI.

        This parser is only used when Bertrand is invoked from within a containerized
        environment, and is responsible for managing the internal toolchain and
        development environment.  A separate parser is used outside a containerized
        environment to manage the environments themselves.
        """

        def __init__(self) -> None:
            self.root = argparse.ArgumentParser(
                description="Command line utilities for bertrand.",
            )
            self.commands = self.root.add_subparsers(
                dest="command",
                title="commands",
                description=(
                    "Control the internal toolchain and development environment within "
                    "a Bertrand container."
                ),
                prog="bertrand",
                metavar="(command)",
            )
            self.version()
            self.code()
            self.build()
            self.check()
            self.test()
            self.format()

        def version(self) -> None:
            """Add the 'version' query to the parser."""
            self.root.add_argument(
                "-v", "--version",
                action="version",
                version=__version__
            )

        def code(self) -> None:
            """Add the 'code' command to the parser."""
            command = self.commands.add_parser(
                "code",
                help=
                    "Launch a text editor rooted at this container's environment "
                    "directory and mount its internal toolchain using remote "
                    "development extensions over the host RPC sidecar.",
            )
            command.add_argument(
                "--editor",
                default=None,
                metavar="EDITOR",
                help=
                    "Override the configured host editor alias for this request.  "
                    "Validation is performed at runtime by RPC/config resolution.",
            )
            command.add_argument(
                "--block",
                action="store_true",
                help=
                    "Block until the launched host editor exits.  If omitted, this "
                    "command returns after the editor launch request is submitted.",
            )
            command.set_defaults(handler=Internal.code)

        def build(self) -> None:
            """Add the 'build' command to the parser."""
            command = self.commands.add_parser(
                "build",
                help=
                    "Build and install the current workspace into this container "
                    "using Bertrand's default 'uv install' command.",
            )
            command.set_defaults(handler=Internal.build)

        def check(self) -> None:
            """Add the 'check' command to the parser."""
            command = self.commands.add_parser(
                "check",
                help=
                    "Run cross-language static checks for the current workspace: "
                    "Ruff, Ty, and clang-tidy (requires compile_commands.json).",
            )
            command.set_defaults(handler=Internal.check)

        def test(self) -> None:
            """Add the 'test' command to the parser."""
            command = self.commands.add_parser(
                "test",
                help="Run the workspace test suite with pytest.",
            )
            command.set_defaults(handler=Internal.test)

        def format(self) -> None:
            """Add the 'format' command to the parser."""
            command = self.commands.add_parser(
                "format",
                help=
                    "Run cross-language formatting for the current workspace: "
                    "Ruff and clang-format (requires compile_commands.json).",
            )
            command.set_defaults(handler=Internal.format)

        def __call__(self) -> argparse.Namespace:
            """Run the command-line parser.

            Returns
            -------
            argparse.Namespace
                The parsed command-line arguments.
            """
            return self.root.parse_args()

    @staticmethod
    def version(args: argparse.Namespace) -> None:
        """Execute the `bertrand --version` CLI command from within a containerized
        environment.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.
        """
        print(__version__)

    @staticmethod
    def code(args: argparse.Namespace) -> None:
        """Execute the `bertrand code` CLI command from within a containerized
        environment.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        RuntimeError
            If not invoked from within a containerized environment.
        """
        if not inside_container():
            raise RuntimeError(
                "`bertrand code` requires a live container context.  Run "
                "`bertrand enter` first."
            )
        with asyncio.Runner() as runner:
            runner.run(rpc(CodeOpen(
                editor=args.editor or None,
                block=args.block,
            )))

    @staticmethod
    def build(args: argparse.Namespace) -> None:
        """Execute the `bertrand build` CLI command from within a containerized
        environment.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        OSError
            If artifact sync or Python dependency/build orchestration fails.
        """
        tag = os.environ.get(IMAGE_TAG_ENV, "").strip()
        if not tag:
            raise OSError(
                "could not determine active image tag in container environment: "
                f"'{IMAGE_TAG_ENV}'"
            )
        with Config.load(WORKTREE_MOUNT) as config:
            config.build(tag)

    @staticmethod
    def check(args: argparse.Namespace) -> None:
        """Execute the `bertrand check` CLI command from within a containerized
        environment.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        OSError
            If source discovery fails, e.g. due to malformed `compile_commands.json`.
        SystemExit
            If any check command exits non-zero.
        """
        _require_active_image_tag()
        with Config.load(WORKTREE_MOUNT) as config:
            files = config.sources()
        artifact_root = str(CONTAINER_TMP_MOUNT)
        clang_tidy_config = CONTAINER_TMP_MOUNT / ".clang-tidy"

        # Python static checks
        for cmd in (["ruff", "check", "."], ["ty", "check", "."]):
            result = subprocess.run(cmd, check=False, cwd=WORKTREE_MOUNT)
            if result.returncode != 0:
                raise SystemExit(result.returncode)

        # C++ static checks over resolved compilation sources.
        for source in files:
            result = subprocess.run(
                [
                    "clang-tidy",
                    "-p", artifact_root,
                    f"--config-file={clang_tidy_config}",
                    str(source),
                ],
                check=False,
                cwd=WORKTREE_MOUNT,
            )
            if result.returncode != 0:
                raise SystemExit(result.returncode)

    @staticmethod
    def format(args: argparse.Namespace) -> None:
        """Execute the `bertrand format` CLI command from within a containerized
        environment.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        OSError
            If source discovery fails, e.g. due to malformed `compile_commands.json`.
        SystemExit
            If formatting exits non-zero.
        """
        _require_active_image_tag()
        with Config.load(WORKTREE_MOUNT) as config:
            files = config.sources()
        clang_format_config = CONTAINER_TMP_MOUNT / ".clang-format"

        # Python formatting
        result = subprocess.run(["ruff", "format", "."], check=False, cwd=WORKTREE_MOUNT)
        if result.returncode != 0:
            raise SystemExit(result.returncode)

        # C++ formatting over resolved compilation sources.
        for source in files:
            result = subprocess.run(
                ["clang-format", f"--style=file:{clang_format_config}", "-i", str(source)],
                check=False,
                cwd=WORKTREE_MOUNT
            )
            if result.returncode != 0:
                raise SystemExit(result.returncode)

    @staticmethod
    def test(args: argparse.Namespace) -> None:
        """Execute the `bertrand test` CLI command from within a containerized
        environment.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        SystemExit
            If the test process exits non-zero.
        """
        result = subprocess.run(["pytest", "-q"], check=False, cwd=WORKTREE_MOUNT)
        if result.returncode != 0:
            raise SystemExit(result.returncode)

    def __call__(self) -> None:
        parser = Internal.Parser()
        args = parser()
        if args.command is None:
            parser.root.print_help()
            return
        args.handler(args)
