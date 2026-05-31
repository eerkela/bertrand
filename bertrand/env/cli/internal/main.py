"""Bertrand's in-container CLI endpoints."""

from __future__ import annotations

import argparse
import asyncio
import json
import math
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import cast

from bertrand.env.cli.internal._helper import live_project_context
from bertrand.env.cli.internal.build import InternalBuildCommand
from bertrand.env.cli.internal.run import InternalRunCommand
from bertrand.env.cli.util import cli
from bertrand.env.git import (
    CONTAINER_TMP,
    WORKTREE_MOUNT,
    Deadline,
    inside_container,
)
from bertrand.env.kube.dev import send_code_open_request
from bertrand.env.version import __version__


@dataclass(frozen=True, slots=True)
class InternalCodeCommand:
    """Internal `bertrand code` command closure."""

    editor: str | None
    block: bool

    @classmethod
    def from_args(cls, args: argparse.Namespace) -> InternalCodeCommand:
        """Create a command object from parsed arguments.

        Returns
        -------
        InternalCodeCommand
            Command object for this parser leaf.
        """
        return cls(editor=args.editor or None, block=args.block)

    async def __call__(self, deadline: Deadline) -> None:
        """Run the command.

        Raises
        ------
        RuntimeError
            If the command is not running inside a live dev Pod.
        """
        if not inside_container():
            msg = (
                "`bertrand code` requires a live Bertrand dev Pod context. Run "
                "`bertrand enter` first."
            )
            raise RuntimeError(msg)
        deadline.check("timed out before editor request could be submitted")
        await send_code_open_request(editor=self.editor, block=self.block)


@dataclass(frozen=True, slots=True)
class InternalCheckCommand:
    """Internal `bertrand check` command closure."""

    @classmethod
    def from_args(cls, _args: argparse.Namespace) -> InternalCheckCommand:
        """Create a command object from parsed arguments.

        Returns
        -------
        InternalCheckCommand
            Command object for this parser leaf.
        """
        return cls()

    async def __call__(self, deadline: Deadline) -> None:
        """Run the command.

        Raises
        ------
        RuntimeError
            If the command is not running inside a live container context.
        SystemExit
            If any check command exits non-zero.
        """
        if not inside_container():
            msg = "`bertrand check` requires a live container context"
            raise RuntimeError(msg)

        await _refresh_artifacts("check", deadline=deadline)
        files = _compile_command_sources()
        artifact_root = str(CONTAINER_TMP)
        clang_tidy_config = CONTAINER_TMP / ".clang-tidy"

        for cmd in (["ruff", "check", "."], ["ty", "check", "."]):
            result = subprocess.run(cmd, check=False, cwd=WORKTREE_MOUNT)
            if result.returncode != 0:
                raise SystemExit(result.returncode)

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


@dataclass(frozen=True, slots=True)
class InternalFormatCommand:
    """Internal `bertrand format` command closure."""

    @classmethod
    def from_args(cls, _args: argparse.Namespace) -> InternalFormatCommand:
        """Create a command object from parsed arguments.

        Returns
        -------
        InternalFormatCommand
            Command object for this parser leaf.
        """
        return cls()

    async def __call__(self, deadline: Deadline) -> None:
        """Run the command.

        Raises
        ------
        RuntimeError
            If the command is not running inside a live container context.
        SystemExit
            If formatting exits non-zero.
        """
        if not inside_container():
            msg = "`bertrand format` requires a live container context"
            raise RuntimeError(msg)

        await _refresh_artifacts("format", deadline=deadline)
        files = _compile_command_sources()
        clang_format_config = CONTAINER_TMP / ".clang-format"

        result = subprocess.run(
            ["ruff", "format", "."],
            check=False,
            cwd=WORKTREE_MOUNT,
        )
        if result.returncode != 0:
            raise SystemExit(result.returncode)

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


@dataclass(frozen=True, slots=True)
class InternalTestCommand:
    """Internal `bertrand test` command closure."""

    @classmethod
    def from_args(cls, _args: argparse.Namespace) -> InternalTestCommand:
        """Create a command object from parsed arguments.

        Returns
        -------
        InternalTestCommand
            Command object for this parser leaf.
        """
        return cls()

    async def __call__(self, _deadline: Deadline) -> None:
        """Run the command.

        Raises
        ------
        RuntimeError
            If the command is not running inside a live container context.
        SystemExit
            If the test process exits non-zero.
        """
        if not inside_container():
            msg = "`bertrand test` requires a live container context"
            raise RuntimeError(msg)
        result = subprocess.run(["pytest", "-q"], check=False, cwd=WORKTREE_MOUNT)
        if result.returncode != 0:
            raise SystemExit(result.returncode)


def _internal_build_command(args: argparse.Namespace) -> InternalBuildCommand:
    return InternalBuildCommand(build_arg=tuple(args.build_arg))


def _internal_run_command(args: argparse.Namespace) -> InternalRunCommand:
    return InternalRunCommand(detach=args.detach, tty=args.tty, args=tuple(args.args))


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
            command.set_defaults(
                command_factory=InternalCodeCommand.from_args,
                command_timeout=math.inf,
            )

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
            command.set_defaults(
                command_factory=_internal_build_command,
                command_timeout=math.inf,
            )

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
            command.set_defaults(
                command="run",
                command_factory=_internal_run_command,
                command_timeout=math.inf,
            )

        def check(self) -> None:
            """Add the 'check' command to the parser."""
            command = self.commands.add_parser(
                "check",
                help="Run cross-language static checks for the current workspace: "
                "Ruff, Ty, and clang-tidy (requires compile_commands.json).",
            )
            command.set_defaults(
                command_factory=InternalCheckCommand.from_args,
                command_timeout=math.inf,
            )

        def test(self) -> None:
            """Add the 'test' command to the parser."""
            command = self.commands.add_parser(
                "test",
                help="Run the workspace test suite with pytest.",
            )
            command.set_defaults(
                command_factory=InternalTestCommand.from_args,
                command_timeout=math.inf,
            )

        def format(self) -> None:
            """Add the 'format' command to the parser."""
            command = self.commands.add_parser(
                "format",
                help="Run cross-language formatting for the current workspace: "
                "Ruff and clang-format (requires compile_commands.json).",
            )
            command.set_defaults(
                command_factory=InternalFormatCommand.from_args,
                command_timeout=math.inf,
            )

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

    def __call__(self) -> None:
        """Parse and dispatch the selected internal command."""
        parser = Internal.Parser()
        args = parser()
        if args.command is None:
            parser.root.print_help()
            return
        command = args.command_factory(args)
        asyncio.run(cli(command, timeout=args.command_timeout))


async def _refresh_artifacts(command: str, *, deadline: Deadline) -> None:
    async with live_project_context(
        command,
        deadline=deadline,
    ) as (_kube, config, _repo_id):
        await config.sync(image_build=True, deadline=deadline)


def _compile_command_sources() -> list[Path]:
    path = CONTAINER_TMP / "compile_commands.json"
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
