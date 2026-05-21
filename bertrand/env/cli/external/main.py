"""Bertrand's out-of-container CLI endpoints."""

from __future__ import annotations

import argparse
import asyncio
import math
import sys
import time
from datetime import UTC, datetime
from pathlib import Path

from bertrand.env.git import INFINITY, TimeoutExpired
from bertrand.env.version import __version__


def _parse_target(path: str) -> tuple[Path, str | None, str | None]:
    target, _, tag = path.partition(":")
    worktree, _, workload = target.partition("@")
    return Path(worktree).expanduser().resolve(), workload or None, tag or None


def _recover_spec(worktree: Path, workload: str | None, tag: str | None) -> str:
    spec = str(worktree)
    if workload:
        spec += f"@{workload}"
    if tag:
        spec += f":{tag}"
    return spec


class External:
    """External CLI for Bertrand."""

    class Parser:
        """External command-line parser for Bertrand's CLI.

        This parser is only used when Bertrand is invoked from outside a containerized
        environment, and is responsible for managing the environments themselves.  A
        separate parser is used within a containerized environment to control its
        internal toolchain.
        """

        def __init__(self) -> None:
            self.root = argparse.ArgumentParser(
                description="Command line utilities for bertrand.",
            )
            self.commands = self.root.add_subparsers(
                dest="command",
                title="commands",
                description=(
                    "Create and manage Python/C/C++ virtual environments and "
                    "containerized toolchains."
                ),
                prog="bertrand",
                metavar="(command)",
            )
            self.version()
            self.init()
            self.build()
            self.network()
            self.run()
            self.dashboard()
            self.enter()
            self.code()
            self.kill()
            self.rm()
            self.clean()

        # TODO: path arguments should be updated to new workload/tag syntax  # noqa: FIX002

        def version(self) -> None:
            """Add the 'version' query to the parser."""
            self.root.add_argument(
                "-v", "--version", action="version", version=__version__
            )

        def init(self) -> None:
            """Add the 'init' command to the parser."""
            command = self.commands.add_parser(
                "init",
                help="Bootstrap Bertrand host prerequisites and optionally converge a "
                "repository/worktree target.",
            )
            command.add_argument(
                "path",
                metavar="REPO[/WORKTREE]",
                nargs="?",
                help="Optional git repository/worktree path.  Omit for host-only "
                "cluster bootstrap.  If provided, then the bootstrap will proceed "
                "to converge the target repository into the local cluster, moving "
                "it into a CephFS volume with a standardized worktree layout.  "
                "This may be destructive to untracked files in the repository, "
                "and will prompt for confirmation beforehand.",
            )
            command.add_argument(
                "-y",
                "--yes",
                action="store_true",
                help="Auto-accept confirmation prompts during host/bootstrap "
                "convergence.  Primarily intended for non-interactive use.",
            )
            command.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for repository convergence.  Use inf to "
                "wait indefinitely.",
            )
            command.add_argument(
                "-e",
                "--enable",
                action="append",
                default=[],
                help="Resource names to enable (repeatable).  Each value may be a "
                "comma-separated list, and will be deduplicated and validated "
                "against the known resource list (or `all`) to enable specific "
                "tools within the environment.  If provided, then the `path` "
                "argument must point to a repository or worktree target to "
                "configure.  "
                "Repository paths will target all worktrees within the repository, "
                "while worktree paths will only enable resources for that "
                "worktree.",
            )
            command.add_argument(
                "-d",
                "--disable",
                action="append",
                default=[],
                help="Resource names to disable (repeatable).  Each value may be a "
                "comma-separated list, and will be deduplicated and validated "
                "against the known resource list (or `all`) to disable specific "
                "tools within the environment.  `all` excludes protected core "
                "resources.  If provided, then the `path` argument must "
                "point to a repository or worktree target to configure.  "
                "Repository paths will target all worktrees within the repository, "
                "while worktree paths will only disable resources for that "
                "worktree.  Disabled resources always take precedence over enabled "
                "ones.",
            )
            command.set_defaults(handler=External.init)

        def build(self) -> None:
            """Add the 'build' command to the parser."""
            command = self.commands.add_parser(
                "build",
                help="Build all declared Bertrand images at the specified worktree "
                "through the in-cluster BuildKit service.",
            )
            command.add_argument(
                "path",
                metavar="REPO[/WORKTREE]",
                help="A path to the project repository or worktree.  Repository "
                "roots target the worktree attached to HEAD; image and workload "
                "suffix syntax is not parsed by `bertrand build`.",
            )
            command.add_argument(
                "--publish",
                nargs="?",
                const="",
                metavar="OCI_REPO",
                default=None,
                help="Optional external OCI repository root where the same image "
                "manifests should be published, for example 'ghcr.io/owner/repo'. "
                "If omitted after --publish, Bertrand infers a GHCR repository from "
                "the GitHub remote.",
            )
            command.add_argument(
                "--auth",
                metavar="CAPABILITY",
                default=None,
                help="Secret-backed capability ID containing Docker auth JSON for "
                "the external registry.  Requires --publish.",
            )
            command.add_argument(
                "--detach",
                action="store_true",
                help="Submit durable build requests and exit without waiting for "
                "publication.",
            )
            command.set_defaults(handler=External.build)

        def network(self) -> None:
            """Add the 'network' command namespace to the parser."""
            command = self.commands.add_parser(
                "network",
                help="Inspect and configure cluster-level Bertrand networking.",
            )
            subcommands = command.add_subparsers(
                dest="network_command",
                title="network commands",
                metavar="(command)",
                required=True,
            )

            status = subcommands.add_parser(
                "status",
                help="Show the cluster networking profile and BuildKit config state.",
            )
            status.add_argument(
                "--json",
                action="store_true",
                help="Emit machine-readable JSON instead of human-readable text.",
            )
            status.set_defaults(handler=External.network)

            dns = subcommands.add_parser(
                "dns",
                help="Configure cluster DNS facts consumed by Bertrand infrastructure.",
            )
            dns_commands = dns.add_subparsers(
                dest="dns_command",
                title="dns commands",
                metavar="(command)",
                required=True,
            )
            dns_set = dns_commands.add_parser(
                "set",
                help="Replace the cluster DNS profile and roll BuildKit builders.",
            )
            dns_set.add_argument(
                "--server",
                action="append",
                nargs="+",
                required=True,
                metavar="IP",
                help="DNS nameserver IP address. Repeat or pass multiple values.",
            )
            dns_set.add_argument(
                "--search",
                action="append",
                nargs="+",
                default=[],
                metavar="DOMAIN",
                help="DNS search domain. Repeat or pass multiple values.",
            )
            dns_set.add_argument(
                "--option",
                action="append",
                nargs="+",
                default=[],
                metavar="OPTION",
                help="Resolver option. Repeat or pass multiple values.",
            )
            dns_set.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for DNS profile convergence.",
            )
            dns_set.set_defaults(handler=External.network)

            dns_clear = dns_commands.add_parser(
                "clear",
                help="Clear cluster DNS overrides and roll BuildKit builders.",
            )
            dns_clear.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for DNS profile convergence.",
            )
            dns_clear.set_defaults(handler=External.network)

        def run(self) -> None:
            """Add the 'run' command to the parser."""
            command = self.commands.add_parser(
                "run",
                help="Build and schedule the configured Kubernetes workload for a "
                "repository/worktree target.",
            )
            command.add_argument(
                "path",
                metavar="REPO[/WORKTREE]",
                help="Project repository or worktree path. Repository roots target "
                "the worktree attached to HEAD.",
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
            command.set_defaults(command="run", handler=External.run)

        def enter(self) -> None:
            """Add the 'enter' command to the parser."""
            command = self.commands.add_parser(
                "enter",
                help="Launch an interactive shell inside a Kubernetes dev-session "
                "Pod for a project worktree. Use 'exit' to leave the shell and "
                "return to the host system.",
            )
            command.add_argument(
                "path",
                metavar="REPO[/WORKTREE]",
                help="Project repository or worktree path. Repository roots target "
                "the worktree attached to HEAD.",
            )
            command.add_argument(
                "--shell",
                default=None,
                metavar="SHELL",
                help="Override the default shell for this enter session.  "
                "Validation is performed at runtime by `bertrand_enter` against "
                "the configured shell map.",
            )
            command.set_defaults(handler=External.enter)

        def code(self) -> None:
            """Add the 'code' command to the parser."""
            command = self.commands.add_parser(
                "code",
                help="Launch a host editor against a generated Kubernetes "
                "dev-session Pod for a project worktree.",
            )
            command.add_argument(
                "path",
                metavar="REPO[/WORKTREE]",
                help="Project repository or worktree path. Repository roots target "
                "the worktree attached to HEAD.",
            )
            command.add_argument(
                "--editor",
                default=None,
                metavar="EDITOR",
                help="Override the configured host editor alias for this command.  "
                "Validation is performed at runtime by dev-session config "
                "resolution.",
            )
            command.set_defaults(handler=External.code)

        def kill(self) -> None:
            """Add the 'kill' command to the parser."""
            command = self.commands.add_parser(
                "kill",
                help="Terminate active Kubernetes workload processes for a "
                "repository/worktree target.",
            )
            command.add_argument(
                "path",
                metavar="REPO[/WORKTREE]",
                help="Project repository or worktree path. Repository roots target "
                "the worktree attached to HEAD.",
            )
            command.add_argument(
                "-t",
                "--timeout",
                type=int,
                default=10,
                help="Kubernetes pod termination grace period in seconds. Kubelet "
                "forcefully kills containers that remain after this window.",
            )
            command.set_defaults(handler=External.kill)

        def rm(self) -> None:
            """Add the 'rm' command to the parser."""
            command = self.commands.add_parser(
                "rm",
                help="Delete Bertrand images and/or containers at the specified path, "
                "scoping to specific images or containers if desired.  Note that "
                "the environment directory is unaffected, but any data stored in "
                "a container's writable layer will be permanently lost.",
            )
            command.add_argument(
                "path",
                nargs="?",
                metavar="ENV[:IMAGE[:CONTAINER]]",
                help="A path to the specified environment directory.  This may be an "
                "absolute or relative path, and must point to an environment "
                "directory produced by 'bertrand init'.  The path may include "
                "optional image and container tags (e.g. "
                "'/path/to/env:image:container'), which can be used to scope the "
                "command to specific images or containers within the environment.  "
                "If an image or environment scope is given, then all images and/or "
                "containers matching that scope will be deleted.  If no path is "
                "given, then all Bertrand images and containers on the host system "
                "will be deleted, after prompting the user to confirm.",
            )
            command.add_argument(
                "-t",
                "--timeout",
                type=int,
                default=INFINITY,
                help="Maximum duration (in seconds) to wait for this command.  May be "
                "rounded up to the nearest second depending on the underlying "
                "implementation.",
            )
            command.add_argument(
                "-f",
                "--force",
                action="store_true",
                help="Force deletion of containers, even if they are currently "
                "running.  This will stop the container if necessary before "
                "deleting it, which may lead to data loss or corruption if it is "
                "in the middle of writing to a file or network connection.  If "
                "omitted, Bertrand will prevent deletion and exit with an error "
                "instead.  Use with extreme caution.",
            )
            command.set_defaults(handler=External.rm)

        def dashboard(self) -> None:
            """Add the 'dashboard' command to the parser."""
            command = self.commands.add_parser(
                "dashboard",
                help="Open the Bertrand Kubernetes dashboard through a local "
                "Headlamp port-forward.",
            )
            command.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for dashboard convergence and "
                "port-forward readiness. Use inf to wait indefinitely.",
            )
            command.add_argument(
                "--port",
                type=int,
                default=0,
                help="Localhost port for the dashboard tunnel. Use 0 to select a "
                "free port automatically.",
            )
            browser = command.add_mutually_exclusive_group()
            browser.add_argument(
                "--open",
                dest="open_browser",
                action="store_true",
                default=None,
                help="Open the dashboard URL in the default browser.",
            )
            browser.add_argument(
                "--no-open",
                dest="open_browser",
                action="store_false",
                help="Print the dashboard URL without opening a browser.",
            )
            command.set_defaults(handler=External.dashboard)

        def clean(self) -> None:
            """Add the 'clean' command to the parser."""
            command = self.commands.add_parser(
                "clean",
                help="Remove host-local Bertrand runtime state in the shared "
                "MicroK8s/MicroCeph model while preserving repository PVCs and "
                "snap installations.",
            )
            command.add_argument(
                "-y",
                "--yes",
                action="store_true",
                help="Bypass confirmation prompts and proceed with Bertrand runtime "
                "artifact cleanup.  Use with caution.",
            )
            command.add_argument(
                "-f",
                "--force",
                action="store_true",
                help="Continue cleanup when non-fatal teardown steps fail, printing "
                "warnings and attempting subsequent cleanup stages.  Strict "
                "residual verification still fails if managed artifacts remain.",
            )
            command.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for cleanup convergence.  Use inf to "
                "wait indefinitely.",
            )
            command.set_defaults(handler=External.clean)

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

        Parameters
        ----------
        _args : argparse.Namespace
            The parsed command-line arguments.
        """
        print(__version__)

    @staticmethod
    def init(args: argparse.Namespace) -> None:
        """Execute the `bertrand init` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        OSError
            If timeout is invalid (must be > 0 or inf).
        """
        from bertrand.env.cli.external.init import bertrand_init

        with asyncio.Runner() as runner:
            timeout = args.timeout
            if math.isnan(timeout) or timeout <= 0:
                msg = f"invalid init timeout: {timeout} (must be > 0 seconds or inf)"
                raise OSError(msg)
            runner.run(
                bertrand_init(
                    None
                    if args.path is None
                    else Path(args.path).expanduser().resolve(),
                    timeout=timeout,
                    enable=args.enable,
                    disable=args.disable,
                    yes=args.yes,
                )
            )

    @staticmethod
    def build(args: argparse.Namespace) -> None:
        """Execute the `bertrand build` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        TimeoutExpired
            If a nested command times out while building the container.  This should
            never occur under normal circumstances, and the 'build' command
            intentionally does not accept a timeout argument, so this can only be
            surfaced from an internal error.
        """
        from bertrand.env.cli.external.build import bertrand_build

        now = time.time()
        with asyncio.Runner() as runner:
            worktree = Path(args.path).expanduser().resolve()
            try:
                runner.run(
                    bertrand_build(
                        worktree,
                        publish=args.publish,
                        auth=args.auth,
                        quiet=False,
                        detach=args.detach,
                    )
                )
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now, UTC)
                cmd = ["bertrand", "build", str(worktree)]
                if args.publish is not None:
                    cmd.append("--publish")
                    if args.publish:
                        cmd.append(args.publish)
                if args.auth:
                    cmd.extend(["--auth", args.auth])
                if args.detach:
                    cmd.append("--detach")
                raise TimeoutExpired(
                    cmd=cmd,
                    timeout=0.0,  # indefinite
                    output=None,
                    stderr=f"started: {start}\nstopped: {datetime.now(UTC)}\n",
                ) from err

    @staticmethod
    def network(args: argparse.Namespace) -> None:
        """Execute a `bertrand network` CLI subcommand.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        OSError
            If timeout is invalid.
        TimeoutExpired
            If the network command does not complete within its timeout.
        """
        from bertrand.env.cli.external.network import bertrand_network

        now = time.time()
        timeout = getattr(args, "timeout", INFINITY)
        if math.isnan(timeout) or timeout <= 0:
            msg = f"invalid network timeout: {timeout} (must be > 0 seconds or inf)"
            raise OSError(msg)
        with asyncio.Runner() as runner:
            try:
                runner.run(bertrand_network(args))
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now, UTC)
                cmd = ["bertrand", "network", args.network_command]
                if args.network_command == "dns":
                    cmd.append(args.dns_command)
                raise TimeoutExpired(
                    cmd=cmd,
                    timeout=timeout,
                    output=None,
                    stderr=f"started: {start}\nstopped: {datetime.now(UTC)}\n",
                ) from err

    @staticmethod
    def run(args: argparse.Namespace) -> None:
        """Execute the `bertrand run` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        TimeoutExpired
            If a nested command times out while building or running the workload.
        """
        from bertrand.env.cli.external.run import bertrand_run

        now = time.time()
        with asyncio.Runner() as runner:
            try:
                runner.run(
                    bertrand_run(
                        Path(args.path).expanduser().resolve(),
                        detach=args.detach,
                        tty=args.tty,
                        args=args.args,
                    )
                )
            except KeyboardInterrupt:
                return
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now, UTC)
                cmd = ["bertrand", "run", str(Path(args.path).expanduser())]
                if args.detach:
                    cmd.append("--detach")
                if args.tty is True:
                    cmd.append("--tty")
                elif args.tty is False:
                    cmd.append("--no-tty")
                if args.args:
                    cmd.append("--")
                    cmd.extend(args.args)
                raise TimeoutExpired(
                    cmd=cmd,
                    timeout=0.0,  # indefinite
                    output=None,
                    stderr=f"started: {start}\nstopped: {datetime.now(UTC)}\n",
                ) from err

    @staticmethod
    def enter(args: argparse.Namespace) -> None:
        """Execute the `bertrand enter` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        TimeoutExpired
            If a nested command times out while entering the container.  This should
            never occur under normal circumstances, and the 'enter' command
            intentionally does not accept a timeout argument, so this can only be
            surfaced from an internal error.
        """
        from bertrand.env.cli.external.enter import bertrand_enter

        now = time.time()
        with asyncio.Runner() as runner:
            target = Path(args.path).expanduser().resolve()
            try:
                runner.run(
                    bertrand_enter(
                        target,
                        shell=args.shell or None,
                    )
                )
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now, UTC)
                cmd = ["bertrand", "enter", str(Path(args.path).expanduser())]
                if args.shell:
                    cmd.extend(["--shell", args.shell])
                raise TimeoutExpired(
                    cmd=cmd,
                    timeout=0.0,  # indefinite
                    output=None,
                    stderr=f"started: {start}\nstopped: {datetime.now(UTC)}\n",
                ) from err

    @staticmethod
    def code(args: argparse.Namespace) -> None:
        """Execute the `bertrand code` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        TimeoutExpired
            If a nested command times out while launching the editor.  This should
            never occur under normal circumstances, and the 'code' command
            intentionally does not accept a timeout argument, so this can only be
            surfaced from an internal error.
        """
        from bertrand.env.cli.external.code import bertrand_code

        now = time.time()
        with asyncio.Runner() as runner:
            target = Path(args.path).expanduser().resolve()
            try:
                runner.run(
                    bertrand_code(
                        target,
                        editor=args.editor or None,
                    )
                )
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now, UTC)
                cmd = ["bertrand", "code", str(Path(args.path).expanduser())]
                if args.editor:
                    cmd.extend(["--editor", args.editor])
                raise TimeoutExpired(
                    cmd=cmd,
                    timeout=0.0,  # indefinite
                    output=None,
                    stderr=f"started: {start}\nstopped: {datetime.now(UTC)}\n",
                ) from err

    @staticmethod
    def kill(args: argparse.Namespace) -> None:
        """Execute the `bertrand kill` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        TimeoutExpired
            If the command does not complete within the specified timeout.
        OSError
            If the grace period is invalid.
        """
        from bertrand.env.cli.external.kill import bertrand_kill

        now = time.time()
        if args.timeout < 0:
            msg = "kill timeout must be non-negative"
            raise OSError(msg)
        with asyncio.Runner() as runner:
            target = Path(args.path).expanduser().resolve()
            try:
                runner.run(
                    bertrand_kill(
                        target,
                        grace_period_seconds=args.timeout,
                    )
                )
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now, UTC)
                cmd = ["bertrand", "kill", str(Path(args.path).expanduser())]
                if args.timeout != 10:
                    cmd.extend(["--timeout", str(args.timeout)])
                raise TimeoutExpired(
                    cmd=cmd,
                    timeout=args.timeout,
                    output=None,
                    stderr=f"started: {start}\nstopped: {datetime.now(UTC)}\n",
                ) from err

    @staticmethod
    def rm(args: argparse.Namespace) -> None:
        """Execute the `bertrand rm` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        TimeoutExpired
            If the command does not complete within the specified timeout.
        """
        from bertrand.env.cli.external.rm import bertrand_rm

        now = time.time()
        deadline = now + args.timeout
        with asyncio.Runner() as runner:
            worktree, workload, tag = _parse_target(args.path)
            try:
                runner.run(
                    bertrand_rm(
                        worktree,
                        workload,
                        tag,
                        deadline=deadline,
                        force=args.force,
                    )
                )
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now, UTC)
                stop = datetime.fromtimestamp(deadline, UTC)
                cmd = ["bertrand", "rm", _recover_spec(worktree, workload, tag)]
                if args.force:
                    cmd.append("--force")
                raise TimeoutExpired(
                    cmd=cmd,
                    timeout=args.timeout,
                    output=None,
                    stderr=f"started: {start}\nstopped: {stop}\n",
                ) from err

    @staticmethod
    def dashboard(args: argparse.Namespace) -> None:
        """Execute the `bertrand dashboard` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        TimeoutExpired
            If the command does not complete within the specified timeout.
        """
        from bertrand.env.cli.external.dashboard import bertrand_dashboard

        now = time.time()
        with asyncio.Runner() as runner:
            try:
                runner.run(
                    bertrand_dashboard(
                        port=args.port,
                        open_browser=args.open_browser,
                        timeout=args.timeout,
                    )
                )
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now, UTC)
                cmd = ["bertrand", "dashboard"]
                if args.port:
                    cmd.extend(["--port", str(args.port)])
                if args.open_browser is True:
                    cmd.append("--open")
                elif args.open_browser is False:
                    cmd.append("--no-open")
                if not math.isinf(args.timeout):
                    cmd.extend(["--timeout", str(args.timeout)])
                raise TimeoutExpired(
                    cmd=cmd,
                    timeout=args.timeout,
                    output=None,
                    stderr=f"started: {start}\nstopped: {datetime.now(UTC)}\n",
                ) from err

    @staticmethod
    def clean(args: argparse.Namespace) -> None:
        """Execute the `bertrand clean` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        OSError
            If timeout is invalid (must be > 0 or inf), or cleanup fails to
            converge.
        """
        from bertrand.env.cli.external.clean import bertrand_clean

        with asyncio.Runner() as runner:
            timeout = args.timeout
            if math.isnan(timeout) or timeout <= 0:
                msg = f"invalid clean timeout: {timeout} (must be > 0 seconds or inf)"
                raise OSError(msg)
            runner.run(
                bertrand_clean(
                    timeout=timeout,
                    assume_yes=args.yes,
                    force=args.force,
                )
            )

    def __call__(self) -> None:
        """Parse and dispatch the selected external command."""
        parser = External.Parser()
        args = parser()
        if args.command is None:
            parser.root.print_help()
            return
        args.handler(args)
