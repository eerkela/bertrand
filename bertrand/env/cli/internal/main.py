"""Bertrand's in-container CLI endpoints."""

from __future__ import annotations

import argparse
import asyncio
import json
import subprocess
import sys
from pathlib import Path
from typing import cast

from bertrand.env.cli.internal._helper import live_project_context
from bertrand.env.cli.internal.build import bertrand_build
from bertrand.env.cli.internal.run import bertrand_run
from bertrand.env.git import (
    CONTAINER_ARTIFACT_MOUNT,
    WORKTREE_MOUNT,
    inside_container,
)
from bertrand.env.kube.dev import CodeOpen
from bertrand.env.version import __version__


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
            self.run()
            self.check()
            self.test()
            self.format()

        def version(self) -> None:
            """Add the 'version' query to the parser."""
            self.root.add_argument(
                "-v", "--version", action="version", version=__version__
            )

        def code(self) -> None:
            """Add the 'code' command to the parser."""
            command = self.commands.add_parser(
                "code",
                help="Launch a text editor rooted at this container's environment "
                "directory and mount its internal toolchain using a Kubernetes "
                "mailbox request serviced by the host `bertrand enter` bridge.",
            )
            command.add_argument(
                "--editor",
                default=None,
                metavar="EDITOR",
                help="Override the configured host editor alias for this request.  "
                "Validation is performed at runtime by dev-session config "
                "resolution.",
            )
            command.add_argument(
                "--block",
                action="store_true",
                help="Block until the launched host editor exits.  If omitted, this "
                "command returns after the editor launch request is submitted.",
            )
            command.set_defaults(handler=Internal.code)

        def build(self) -> None:
            """Add the 'build' command to the parser."""
            command = self.commands.add_parser(
                "build",
                help="Build and install the current workspace into this container "
                "using Bertrand's default 'uv install' command.",
            )
            command.add_argument(
                "--build-arg",
                action="append",
                default=[],
                metavar="KEY=VALUE",
                help="Image-build-only argument to persist into the image build "
                "contract and forward to the PEP 517/660 backend.",
            )
            command.set_defaults(handler=Internal.build)

        def run(self) -> None:
            """Add the 'run' command to the parser."""
            command = self.commands.add_parser(
                "run",
                help="Build and schedule the current worktree's configured "
                "Kubernetes workload.",
            )
            command.add_argument(
                "--detach",
                action="store_true",
                help="Build and submit/converge the workload without foreground log "
                "streaming.",
            )
            command.add_argument(
                "--tty",
                dest="tty",
                action="store_true",
                default=None,
                help="Force interactive TTY attachment in foreground mode.",
            )
            command.add_argument(
                "--no-tty",
                dest="tty",
                action="store_false",
                help="Disable interactive TTY attachment and use log streaming.",
            )
            command.set_defaults(command="run", handler=Internal.run)

        def check(self) -> None:
            """Add the 'check' command to the parser."""
            command = self.commands.add_parser(
                "check",
                help="Run cross-language static checks for the current workspace: "
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
                help="Run cross-language formatting for the current workspace: "
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
            argv = sys.argv[1:]
            if argv[:1] == ["run"]:
                return self._parse_run(argv[1:])
            return self.root.parse_args()

        def _parse_run(self, argv: list[str]) -> argparse.Namespace:
            separator = argv.index("--") if "--" in argv else -1
            if separator >= 0:
                command_argv = argv[:separator]
                workload_args = argv[separator + 1 :]
            else:
                command_argv = argv
                workload_args = []
            parser = self.commands.choices["run"]
            args = parser.parse_args(command_argv)
            args.args = workload_args
            return args

    @staticmethod
    def version(_args: argparse.Namespace) -> None:
        """Execute the `bertrand --version` CLI command.

        This command runs from within a containerized environment.

        Parameters
        ----------
        _args : argparse.Namespace
            The parsed command-line arguments.
        """
        print(__version__)

    @staticmethod
    def code(args: argparse.Namespace) -> None:
        """Execute the `bertrand code` CLI command.

        This command runs from within a containerized environment.

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
            msg = (
                "`bertrand code` requires a live Bertrand dev Pod context. Run "
                "`bertrand enter` first."
            )
            raise RuntimeError(msg)
        with asyncio.Runner() as runner:
            runner.run(
                CodeOpen(
                    editor=args.editor or None,
                    block=args.block,
                ).send()
            )

    @staticmethod
    def build(args: argparse.Namespace) -> None:
        """Execute the `bertrand build` CLI command.

        This command runs from within a containerized environment.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        """
        bertrand_build(args)

    @staticmethod
    def run(args: argparse.Namespace) -> None:
        """Execute the `bertrand run` CLI command.

        This command runs from within a live containerized dev environment.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.
        """
        bertrand_run(args)

    @staticmethod
    def check(_args: argparse.Namespace) -> None:
        """Execute the `bertrand check` CLI command.

        This command runs from within a containerized environment.

        Parameters
        ----------
        _args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        RuntimeError
            If not invoked from within a live container context.
        SystemExit
            If any check command exits non-zero.
        """
        if not inside_container():
            msg = "`bertrand check` requires a live container context"
            raise RuntimeError(msg)

        asyncio.run(_refresh_artifacts("check"))
        files = _compile_command_sources()
        artifact_root = str(CONTAINER_ARTIFACT_MOUNT)
        clang_tidy_config = CONTAINER_ARTIFACT_MOUNT / ".clang-tidy"

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
                    "-p",
                    artifact_root,
                    f"--config-file={clang_tidy_config}",
                    str(source),
                ],
                check=False,
                cwd=WORKTREE_MOUNT,
            )
            if result.returncode != 0:
                raise SystemExit(result.returncode)

    @staticmethod
    def format(_args: argparse.Namespace) -> None:
        """Execute the `bertrand format` CLI command.

        This command runs from within a containerized environment.

        Parameters
        ----------
        _args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        RuntimeError
            If not invoked from within a live container context.
        SystemExit
            If formatting exits non-zero.
        """
        if not inside_container():
            msg = "`bertrand format` requires a live container context"
            raise RuntimeError(msg)

        asyncio.run(_refresh_artifacts("format"))
        files = _compile_command_sources()
        clang_format_config = CONTAINER_ARTIFACT_MOUNT / ".clang-format"

        # Python formatting
        result = subprocess.run(
            ["ruff", "format", "."],
            check=False,
            cwd=WORKTREE_MOUNT,
        )
        if result.returncode != 0:
            raise SystemExit(result.returncode)

        # C++ formatting over resolved compilation sources.
        for source in files:
            result = subprocess.run(
                [
                    "clang-format",
                    f"--style=file:{clang_format_config}",
                    "-i",
                    str(source),
                ],
                check=False,
                cwd=WORKTREE_MOUNT,
            )
            if result.returncode != 0:
                raise SystemExit(result.returncode)

    @staticmethod
    def test(_args: argparse.Namespace) -> None:
        """Execute the `bertrand test` CLI command.

        This command runs from within a containerized environment.

        Parameters
        ----------
        _args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        RuntimeError
            If not invoked from within a live container context.
        SystemExit
            If the test process exits non-zero.
        """
        if not inside_container():
            msg = "`bertrand test` requires a live container context"
            raise RuntimeError(msg)
        result = subprocess.run(["pytest", "-q"], check=False, cwd=WORKTREE_MOUNT)
        if result.returncode != 0:
            raise SystemExit(result.returncode)

    def __call__(self) -> None:
        """Parse and dispatch the selected internal command."""
        parser = Internal.Parser()
        args = parser()
        if args.command is None:
            parser.root.print_help()
            return
        args.handler(args)


async def _refresh_artifacts(command: str) -> None:
    async with live_project_context(command) as context:
        await context.config.sync(image_build=True)


def _compile_command_sources() -> list[Path]:
    path = CONTAINER_ARTIFACT_MOUNT / "compile_commands.json"
    if not path.is_file():
        return []
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as err:
        msg = f"failed to read compile command artifact at {path}: {err}"
        raise OSError(msg) from err
    if not isinstance(payload, list):
        msg = f"compile command artifact must be a list: {path}"
        raise OSError(msg)

    out: list[Path] = []
    seen: set[Path] = set()
    for index, entry in enumerate(payload):
        if not isinstance(entry, dict):
            msg = f"compile command entry {index} must be a mapping"
            raise OSError(msg)
        entry = cast("dict[str, object]", entry)
        raw_file = entry.get("file")
        if not isinstance(raw_file, str) or not raw_file.strip():
            continue
        source = Path(raw_file)
        if not source.is_absolute():
            raw_directory = entry.get("directory")
            if isinstance(raw_directory, str) and raw_directory.strip():
                source = Path(raw_directory) / source
            else:
                source = WORKTREE_MOUNT / source
        source = source.resolve()
        if source in seen or not source.exists():
            continue
        seen.add(source)
        out.append(source)
    return out
