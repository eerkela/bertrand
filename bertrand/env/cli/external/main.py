"""Bertrand's out-of-container CLI endpoints."""

from __future__ import annotations

import argparse
import asyncio
import math
import sys
from collections.abc import Awaitable, Callable
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING, Any, cast

from bertrand.env.cli.external.build import bertrand_build
from bertrand.env.cli.external.clean import bertrand_clean
from bertrand.env.cli.external.cluster import (
    bertrand_cluster_device_list,
    bertrand_cluster_invite,
    bertrand_cluster_join,
    bertrand_cluster_network_dns_clear,
    bertrand_cluster_network_dns_set,
    bertrand_cluster_network_doctor,
    bertrand_cluster_network_lb_bgp_advertise_upsert,
    bertrand_cluster_network_lb_bgp_peer_upsert,
    bertrand_cluster_network_lb_install,
    bertrand_cluster_network_lb_l2_upsert,
    bertrand_cluster_network_lb_pool_upsert,
    bertrand_cluster_network_lb_status,
    bertrand_cluster_network_status,
    bertrand_cluster_status,
    bertrand_cluster_storage_doctor,
    bertrand_cluster_storage_status,
)
from bertrand.env.cli.external.code import bertrand_code
from bertrand.env.cli.external.dashboard import bertrand_dashboard
from bertrand.env.cli.external.enter import bertrand_enter
from bertrand.env.cli.external.init import ExternalInit
from bertrand.env.cli.external.node import (
    bertrand_node_device_add,
    bertrand_node_device_list,
    bertrand_node_device_rm,
    bertrand_node_name_set,
    bertrand_node_status,
    bertrand_node_storage_doctor,
    bertrand_node_storage_status,
    parse_device_attrs,
)
from bertrand.env.cli.external.rm import bertrand_rm
from bertrand.env.cli.external.run import bertrand_run
from bertrand.env.cli.external.scale import bertrand_scale
from bertrand.env.cli.external.secret import (
    bertrand_node_secret_add,
    bertrand_node_secret_list,
    bertrand_node_secret_rm,
    bertrand_secret_add,
    bertrand_secret_list,
    bertrand_secret_rm,
    bertrand_shared_secret_add,
    bertrand_shared_secret_list,
    bertrand_shared_secret_rm,
)
from bertrand.env.cli.util import cli
from bertrand.env.version import __version__

if TYPE_CHECKING:
    from bertrand.env.git import Deadline

type _KwargSpec = tuple[str, str] | tuple[str, str, Callable[[Any], Any]]

_CAPABILITY_SOURCE_HELP = (
    "Payload file path, '-' for stdin, or omitted to read piped stdin."
)
_CAPABILITY_KIND_STORE_HELP = "Capability kind to store."
_CAPABILITY_KIND_REMOVE_HELP = "Capability kind to remove."
_CAPABILITY_KIND_FILTER_HELP = "Optional capability kind filter."


def _resolved_path(value: str) -> Path:
    return Path(value).expanduser().resolve()


@dataclass(frozen=True, slots=True)
class _ExternalLeafCommand:
    """External command leaf backed by one async implementation."""

    func: Callable[..., Awaitable[None]]
    kwargs: dict[str, object]
    converters: dict[str, Callable[[Any], Any]]

    async def __call__(self, deadline: Deadline) -> None:
        """Run the command."""
        kwargs = {}
        for key, value in self.kwargs.items():
            converter = self.converters.get(key)
            kwargs[key] = converter(value) if converter is not None else value
        await self.func(deadline=deadline, **kwargs)


@dataclass(frozen=True, slots=True)
class _ExternalLeafFactory:
    """Create an external command leaf from parsed arguments."""

    func: Callable[..., Awaitable[None]]
    kwargs: tuple[_KwargSpec, ...] = ()
    fixed: dict[str, object] | None = None

    def __call__(self, args: argparse.Namespace) -> _ExternalLeafCommand:
        values: dict[str, object] = dict(self.fixed or {})
        converters: dict[str, Callable[[Any], Any]] = {}
        for spec in self.kwargs:
            if len(spec) == 2:
                dest, source = cast("tuple[str, str]", spec)
                converter = None
            else:
                dest, source, converter = cast(
                    "tuple[str, str, Callable[[Any], Any]]",
                    spec,
                )
            if dest == "timeout":
                continue
            value = getattr(args, source)
            if converter is not None:
                converters[dest] = converter
            values[dest] = value
        return _ExternalLeafCommand(
            func=self.func,
            kwargs=values,
            converters=converters,
        )


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
            self.secret()
            self.cluster()
            self.node()
            self.run()
            self.dashboard()
            self.enter()
            self.code()
            self.scale()
            self.rm()
            self.clean()

        # TODO: these helpers are terrible for clarity and readability.  We should
        # revert to the first design, where all the methods here are just flat
        # descriptions of the command structure, without any helper abstractions or
        # constants/type hints.

        @staticmethod
        def _add_project_path(parser: argparse.ArgumentParser) -> None:
            parser.add_argument(
                "path",
                metavar="REPO[/WORKTREE]",
                help=(
                    "Project repository or worktree path. Repository roots target the "
                    "worktree attached to HEAD."
                ),
            )

        @staticmethod
        def _add_json(parser: argparse.ArgumentParser) -> None:
            parser.add_argument(
                "--json",
                action="store_true",
                help="Emit machine-readable JSON instead of human-readable text.",
            )

        @staticmethod
        def _add_timeout(
            parser: argparse.ArgumentParser,
            *,
            help: str,  # noqa: A002
        ) -> None:
            parser.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=math.inf,
                help=help,
            )

        def timeout(self, parser: argparse.ArgumentParser) -> None:
            """Add the shared command timeout option."""
            self._add_timeout(
                parser,
                help="Maximum time in seconds for command convergence.",
            )

        @staticmethod
        def terminal(
            parser: argparse.ArgumentParser,
            func: Callable[..., Awaitable[None]],
            *,
            cmd: tuple[str, ...],
            timeout_scope: str,
            kwargs: tuple[_KwargSpec, ...] = (),
            fixed: dict[str, object] | None = None,
            display_args: tuple[str, ...] = (),
        ) -> None:
            """Attach a terminal async command leaf."""
            _ = (cmd, timeout_scope, display_args)
            timeout_attr = (
                "timeout" if any(spec[0] == "timeout" for spec in kwargs) else None
            )
            parser.set_defaults(
                command_factory=_ExternalLeafFactory(
                    func=func,
                    kwargs=tuple(spec for spec in kwargs if spec[0] != "timeout"),
                    fixed=fixed or {},
                ),
                command_timeout_attr=timeout_attr,
                command_timeout=math.inf,
            )

        @staticmethod
        def capability_source(parser: argparse.ArgumentParser) -> None:
            """Add the shared capability payload source argument."""
            parser.add_argument(
                "source",
                nargs="?",
                metavar="SOURCE",
                help=_CAPABILITY_SOURCE_HELP,
            )

        @staticmethod
        def capability_kind(
            parser: argparse.ArgumentParser,
            *,
            help_text: str,
            default: str | None = "secret",
        ) -> None:
            """Add a capability kind option."""
            parser.add_argument(
                "--kind",
                choices=("secret", "ssh"),
                default=default,
                help=help_text,
            )

        def device_list_options(self, parser: argparse.ArgumentParser) -> None:
            """Add shared DRA device list flags."""
            self._add_json(parser)
            self.timeout(parser)

        def json_command(
            self,
            subcommands: argparse._SubParsersAction[argparse.ArgumentParser],
            name: str,
            *,
            help_text: str,
            func: Callable[..., Awaitable[None]],
            cmd: tuple[str, ...],
            timeout_scope: str,
        ) -> argparse.ArgumentParser:
            """Add a simple JSON-capable status command.

            Returns
            -------
            argparse.ArgumentParser
                The command parser.
            """
            command = subcommands.add_parser(name, help=help_text)
            self._add_json(command)
            self.terminal(
                command,
                func,
                cmd=cmd,
                timeout_scope=timeout_scope,
                kwargs=(("json_output", "json"),),
            )
            return command

        def secret_namespace(
            self,
            subcommands: argparse._SubParsersAction[argparse.ArgumentParser],
            *,
            command_dest: str,
            namespace_help: str,
            add_help: str,
            rm_help: str,
            list_help: str,
            add_func: Callable[..., Awaitable[None]],
            rm_func: Callable[..., Awaitable[None]],
            list_func: Callable[..., Awaitable[None]],
            cmd_prefix: tuple[str, ...],
            timeout_scope: str,
            path_scoped: bool,
        ) -> None:
            """Add a secret capability namespace."""
            secret = subcommands.add_parser("secret", help=namespace_help)
            secret_subcommands = secret.add_subparsers(
                dest=command_dest,
                title="secret commands",
                metavar="(command)",
                required=True,
            )

            add = secret_subcommands.add_parser("add", help=add_help)
            if path_scoped:
                add.add_argument("path", metavar="REPO[/WORKTREE]")
            add.add_argument("id", metavar="ID")
            self.capability_source(add)
            self.capability_kind(add, help_text=_CAPABILITY_KIND_STORE_HELP)
            self.timeout(add)
            add_kwargs: list[_KwargSpec] = [
                ("kind", "kind"),
                ("capability_id", "id"),
                ("source", "source"),
                ("timeout", "timeout"),
            ]
            if path_scoped:
                add_kwargs.insert(0, ("path", "path"))
            self.terminal(
                add,
                add_func,
                cmd=(*cmd_prefix, "add"),
                timeout_scope=timeout_scope,
                kwargs=tuple(add_kwargs),
                display_args=("path",) if path_scoped else (),
            )

            rm = secret_subcommands.add_parser("rm", help=rm_help)
            if path_scoped:
                rm.add_argument("path", metavar="REPO[/WORKTREE]")
            rm.add_argument("id", metavar="ID")
            self.capability_kind(rm, help_text=_CAPABILITY_KIND_REMOVE_HELP)
            self.timeout(rm)
            rm_kwargs: list[_KwargSpec] = [
                ("kind", "kind"),
                ("capability_id", "id"),
                ("timeout", "timeout"),
            ]
            if path_scoped:
                rm_kwargs.insert(0, ("path", "path"))
            self.terminal(
                rm,
                rm_func,
                cmd=(*cmd_prefix, "rm"),
                timeout_scope=timeout_scope,
                kwargs=tuple(rm_kwargs),
                display_args=("path",) if path_scoped else (),
            )

            listing = secret_subcommands.add_parser("list", help=list_help)
            if path_scoped:
                listing.add_argument("path", metavar="REPO[/WORKTREE]")
            self.capability_kind(
                listing,
                help_text=_CAPABILITY_KIND_FILTER_HELP,
                default=None,
            )
            self._add_json(listing)
            self.timeout(listing)
            list_kwargs: list[_KwargSpec] = [
                ("kind", "kind"),
                ("json_output", "json"),
                ("timeout", "timeout"),
            ]
            if path_scoped:
                list_kwargs.insert(0, ("path", "path"))
            self.terminal(
                listing,
                list_func,
                cmd=(*cmd_prefix, "list"),
                timeout_scope=timeout_scope,
                kwargs=tuple(list_kwargs),
                display_args=("path",) if path_scoped else (),
            )

        def storage_namespace(
            self,
            subcommands: argparse._SubParsersAction[argparse.ArgumentParser],
            *,
            dest: str,
            namespace_help: str,
            status_help: str,
            doctor_help: str,
            status_func: Callable[..., Awaitable[None]],
            doctor_func: Callable[..., Awaitable[None]],
            cmd_prefix: tuple[str, ...],
            timeout_scope: str,
        ) -> None:
            """Add a storage status/doctor namespace."""
            storage = subcommands.add_parser("storage", help=namespace_help)
            storage_subcommands = storage.add_subparsers(
                dest=dest,
                title="storage commands",
                metavar="(command)",
                required=True,
            )
            status = storage_subcommands.add_parser("status", help=status_help)
            self._add_json(status)
            self.terminal(
                status,
                status_func,
                cmd=(*cmd_prefix, "status"),
                timeout_scope=timeout_scope,
                kwargs=(("json_output", "json"),),
            )
            doctor = storage_subcommands.add_parser("doctor", help=doctor_help)
            self._add_json(doctor)
            self.terminal(
                doctor,
                doctor_func,
                cmd=(*cmd_prefix, "doctor"),
                timeout_scope=timeout_scope,
                kwargs=(("json_output", "json"),),
            )

        def cluster_lifecycle(
            self,
            subcommands: argparse._SubParsersAction[argparse.ArgumentParser],
        ) -> None:
            """Add cluster lifecycle/status commands."""
            self.json_command(
                subcommands,
                "status",
                help_text="Show global Bertrand cluster runtime readiness.",
                func=bertrand_cluster_status,
                cmd=("bertrand", "cluster", "status"),
                timeout_scope="cluster",
            )

            invite = subcommands.add_parser(
                "invite",
                help="Generate a sensitive join bundle for another Bertrand host.",
            )
            invite.add_argument(
                "--name",
                default=None,
                metavar="NODE",
                help="Display name hint for the joining Bertrand host.",
            )
            invite.add_argument(
                "--role",
                choices=("controller", "worker"),
                default="worker",
                help="k0s role for the joining Bertrand host.",
            )
            invite.add_argument(
                "--server-url",
                default=None,
                metavar="URL",
                help="Externally reachable k0s controller URL for joining hosts.",
            )
            self._add_timeout(
                invite,
                help="Maximum time in seconds for join-token generation.",
            )
            self.terminal(
                invite,
                bertrand_cluster_invite,
                cmd=("bertrand", "cluster", "invite"),
                timeout_scope="cluster",
                kwargs=(
                    ("name", "name"),
                    ("role", "role"),
                    ("server_url", "server_url"),
                    ("timeout", "timeout"),
                ),
            )

            join = subcommands.add_parser(
                "join",
                help="Join this host to a Bertrand cluster using an invite token.",
            )
            join.add_argument(
                "token",
                metavar="TOKEN",
                help="Sensitive token produced by `bertrand cluster invite`.",
            )
            join.add_argument(
                "--role",
                choices=("controller", "worker"),
                default=None,
                help="Override the k0s role embedded in the join token.",
            )
            self._add_timeout(
                join,
                help=(
                    "Maximum time in seconds for cluster join and backend convergence."
                ),
            )
            self.terminal(
                join,
                bertrand_cluster_join,
                cmd=("bertrand", "cluster", "join"),
                timeout_scope="cluster",
                kwargs=(
                    ("token", "token"),
                    ("role", "role"),
                    ("timeout", "timeout"),
                ),
            )

        def cluster_device(
            self,
            subcommands: argparse._SubParsersAction[argparse.ArgumentParser],
        ) -> None:
            """Add cluster device inventory commands."""
            device = subcommands.add_parser(
                "device",
                help="List managed DRA device capability inventory across the cluster.",
            )
            device_subcommands = device.add_subparsers(
                dest="cluster_device_command",
                title="device commands",
                metavar="(command)",
                required=True,
            )
            cluster_device_list = device_subcommands.add_parser(
                "list",
                help="List cluster-wide managed DRA inventory.",
            )
            cluster_device_list.add_argument(
                "--node",
                default=None,
                metavar="HOST_ID",
                help="Optional Bertrand host UUID filter.",
            )
            cluster_device_list.add_argument(
                "--capability",
                default=None,
                metavar="ID",
                help="Optional device capability ID filter.",
            )
            self.device_list_options(cluster_device_list)
            self.terminal(
                cluster_device_list,
                bertrand_cluster_device_list,
                cmd=("bertrand", "cluster", "device", "list"),
                timeout_scope="cluster",
                kwargs=(
                    ("node", "node"),
                    ("capability_id", "capability"),
                    ("json_output", "json"),
                    ("timeout", "timeout"),
                ),
            )

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
                default=math.inf,
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
            command.set_defaults(
                command_factory=lambda args: ExternalInit(
                    path=args.path,
                    enable=args.enable,
                    disable=args.disable,
                    yes=args.yes,
                ),
                command_timeout_attr="timeout",
                command_timeout=math.inf,
            )

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
            command.set_defaults(
                command_factory=_ExternalLeafFactory(
                    func=bertrand_build,
                    kwargs=(
                        ("path", "path", _resolved_path),
                        ("publish", "publish"),
                        ("auth", "auth"),
                        ("detach", "detach"),
                    ),
                    fixed={"quiet": False},
                ),
                command_timeout=math.inf,
                command_timeout_attr=None,
            )

        def secret(self) -> None:
            """Add the top-level path-scoped 'secret' command namespace."""
            self.secret_namespace(
                self.commands,
                command_dest="secret_command",
                namespace_help="Manage repository/worktree-scoped secret capabilities.",
                add_help=("Create or update a repository/worktree-scoped capability."),
                rm_help="Delete a repository/worktree-scoped capability.",
                list_help=(
                    "List path-relevant capabilities without printing payloads."
                ),
                add_func=bertrand_secret_add,
                rm_func=bertrand_secret_rm,
                list_func=bertrand_secret_list,
                cmd_prefix=("bertrand", "secret"),
                timeout_scope="secret",
                path_scoped=True,
            )

        def cluster(self) -> None:
            """Add the 'cluster' command namespace to the parser."""
            command = self.commands.add_parser(
                "cluster",
                help="Inspect and manage Bertrand's shared/distributed runtime.",
            )
            subcommands = command.add_subparsers(
                metavar="(command)",
                dest="cluster_command",
                title="cluster commands",
                required=True,
            )
            self.cluster_lifecycle(subcommands)
            self.secret_namespace(
                subcommands,
                command_dest="cluster_secret_command",
                namespace_help="Manage shared cluster secret capabilities.",
                add_help="Create or update a shared cluster capability.",
                rm_help="Delete a shared cluster capability.",
                list_help="List shared cluster capabilities without printing payloads.",
                add_func=bertrand_shared_secret_add,
                rm_func=bertrand_shared_secret_rm,
                list_func=bertrand_shared_secret_list,
                cmd_prefix=("bertrand", "cluster", "secret"),
                timeout_scope="cluster",
                path_scoped=False,
            )
            self.cluster_device(subcommands)
            self.storage_namespace(
                subcommands,
                dest="cluster_storage_command",
                namespace_help="Inspect cluster-wide Rook/Ceph storage status.",
                status_help="Show cluster-wide Rook/Ceph autoscaler state.",
                doctor_help="Print actionable Rook/Ceph storage diagnostics.",
                status_func=bertrand_cluster_storage_status,
                doctor_func=bertrand_cluster_storage_doctor,
                cmd_prefix=("bertrand", "cluster", "storage"),
                timeout_scope="cluster",
            )
            network = subcommands.add_parser(
                "network",
                help="Inspect and configure cluster-level Bertrand networking.",
            )
            network_subcommands = network.add_subparsers(
                dest="network_command",
                title="network commands",
                metavar="(command)",
                required=True,
            )
            self.json_command(
                network_subcommands,
                "status",
                help_text=(
                    "Show cluster CNI, Gateway, LoadBalancer, route, and DNS state."
                ),
                func=bertrand_cluster_network_status,
                cmd=("bertrand", "cluster", "network", "status"),
                timeout_scope="cluster",
            )
            self.json_command(
                network_subcommands,
                "doctor",
                help_text="Print actionable cluster networking diagnostics.",
                func=bertrand_cluster_network_doctor,
                cmd=("bertrand", "cluster", "network", "doctor"),
                timeout_scope="cluster",
            )
            lb = subcommands.add_parser(
                "lb",
                help="Inspect and configure Bertrand-managed MetalLB resources.",
            )
            lb_commands = lb.add_subparsers(
                dest="lb_command",
                title="load-balancer commands",
                metavar="(command)",
                required=True,
            )
            self.json_command(
                lb_commands,
                "status",
                help_text="Show MetalLB installation and advertisement state.",
                func=bertrand_cluster_network_lb_status,
                cmd=("bertrand", "cluster", "network", "lb", "status"),
                timeout_scope="cluster",
            )

            lb_install = lb_commands.add_parser(
                "install",
                help="Install Bertrand-managed MetalLB with FRR/BGP support.",
            )
            self._add_timeout(
                lb_install,
                help="Maximum time in seconds for MetalLB convergence.",
            )
            self.terminal(
                lb_install,
                bertrand_cluster_network_lb_install,
                cmd=("bertrand", "cluster", "network", "lb", "install"),
                timeout_scope="cluster",
                kwargs=(("timeout", "timeout"),),
            )

            lb_pool = lb_commands.add_parser(
                "pool",
                help="Manage MetalLB address pools.",
            )
            lb_pool_commands = lb_pool.add_subparsers(
                dest="lb_pool_command",
                title="pool commands",
                metavar="(command)",
                required=True,
            )
            lb_pool_upsert = lb_pool_commands.add_parser(
                "upsert",
                help="Create or patch a Bertrand-managed MetalLB IPAddressPool.",
            )
            lb_pool_upsert.add_argument("name", metavar="NAME")
            lb_pool_upsert.add_argument(
                "--address",
                action="append",
                nargs="+",
                required=True,
                metavar="RANGE",
                help="CIDR or start-end address range. Repeat or pass multiple.",
            )
            auto_assign = lb_pool_upsert.add_mutually_exclusive_group()
            auto_assign.add_argument(
                "--auto-assign",
                dest="auto_assign",
                action="store_true",
                default=True,
                help=(
                    "Allow MetalLB to allocate addresses from this pool automatically."
                ),
            )
            auto_assign.add_argument(
                "--no-auto-assign",
                dest="auto_assign",
                action="store_false",
                help="Require Services to request this pool explicitly.",
            )
            self._add_timeout(
                lb_pool_upsert,
                help="Maximum time in seconds for IPAddressPool convergence.",
            )
            self.terminal(
                lb_pool_upsert,
                bertrand_cluster_network_lb_pool_upsert,
                cmd=("bertrand", "cluster", "network", "lb", "pool", "upsert"),
                timeout_scope="cluster",
                kwargs=(
                    ("name", "name"),
                    ("address", "address"),
                    ("auto_assign", "auto_assign"),
                    ("timeout", "timeout"),
                ),
            )

            lb_l2 = lb_commands.add_parser(
                "l2",
                help="Manage MetalLB Layer 2 advertisements.",
            )
            lb_l2_commands = lb_l2.add_subparsers(
                dest="lb_l2_command",
                title="l2 commands",
                metavar="(command)",
                required=True,
            )
            lb_l2_upsert = lb_l2_commands.add_parser(
                "upsert",
                help="Create or patch a Bertrand-managed L2Advertisement.",
            )
            lb_l2_upsert.add_argument("name", metavar="NAME")
            lb_l2_upsert.add_argument("--pool", required=True, metavar="POOL")
            lb_l2_upsert.add_argument(
                "--interface",
                action="append",
                default=[],
                metavar="IFACE",
                help="Optional interface to advertise from. Repeat for several.",
            )
            self._add_timeout(
                lb_l2_upsert,
                help="Maximum time in seconds for L2Advertisement convergence.",
            )
            self.terminal(
                lb_l2_upsert,
                bertrand_cluster_network_lb_l2_upsert,
                cmd=("bertrand", "cluster", "network", "lb", "l2", "upsert"),
                timeout_scope="cluster",
                kwargs=(
                    ("name", "name"),
                    ("pool", "pool"),
                    ("interface", "interface"),
                    ("timeout", "timeout"),
                ),
            )

            lb_bgp = lb_commands.add_parser(
                "bgp",
                help="Manage MetalLB BGP peers and advertisements.",
            )
            lb_bgp_commands = lb_bgp.add_subparsers(
                dest="lb_bgp_command",
                title="bgp commands",
                metavar="(command)",
                required=True,
            )
            lb_bgp_peer = lb_bgp_commands.add_parser(
                "peer",
                help="Manage MetalLB BGPPeer resources.",
            )
            lb_bgp_peer_commands = lb_bgp_peer.add_subparsers(
                dest="lb_bgp_peer_command",
                title="bgp peer commands",
                metavar="(command)",
                required=True,
            )
            lb_bgp_peer_upsert = lb_bgp_peer_commands.add_parser(
                "upsert",
                help="Create or patch a Bertrand-managed BGPPeer.",
            )
            lb_bgp_peer_upsert.add_argument("name", metavar="NAME")
            lb_bgp_peer_upsert.add_argument(
                "--peer-address",
                required=True,
                metavar="IP",
            )
            lb_bgp_peer_upsert.add_argument(
                "--peer-asn",
                required=True,
                type=int,
                metavar="ASN",
            )
            lb_bgp_peer_upsert.add_argument(
                "--local-asn",
                required=True,
                type=int,
                metavar="ASN",
            )
            lb_bgp_peer_upsert.add_argument("--peer-port", type=int, default=None)
            lb_bgp_peer_upsert.add_argument(
                "--source-address",
                default=None,
                metavar="IP",
            )
            lb_bgp_peer_upsert.add_argument(
                "--password-secret",
                default=None,
                metavar="SECRET",
            )
            self._add_timeout(
                lb_bgp_peer_upsert,
                help="Maximum time in seconds for BGPPeer convergence.",
            )
            self.terminal(
                lb_bgp_peer_upsert,
                bertrand_cluster_network_lb_bgp_peer_upsert,
                cmd=(
                    "bertrand",
                    "cluster",
                    "network",
                    "lb",
                    "bgp",
                    "peer",
                    "upsert",
                ),
                timeout_scope="cluster",
                kwargs=(
                    ("name", "name"),
                    ("peer_address", "peer_address"),
                    ("peer_asn", "peer_asn"),
                    ("local_asn", "local_asn"),
                    ("peer_port", "peer_port"),
                    ("source_address", "source_address"),
                    ("password_secret", "password_secret"),
                    ("timeout", "timeout"),
                ),
            )

            lb_bgp_advertise = lb_bgp_commands.add_parser(
                "advertise",
                help="Manage MetalLB BGPAdvertisement resources.",
            )
            lb_bgp_advertise_commands = lb_bgp_advertise.add_subparsers(
                dest="lb_bgp_advertise_command",
                title="bgp advertise commands",
                metavar="(command)",
                required=True,
            )
            lb_bgp_advertise_upsert = lb_bgp_advertise_commands.add_parser(
                "upsert",
                help="Create or patch a Bertrand-managed BGPAdvertisement.",
            )
            lb_bgp_advertise_upsert.add_argument("name", metavar="NAME")
            lb_bgp_advertise_upsert.add_argument(
                "--pool",
                required=True,
                metavar="POOL",
            )
            lb_bgp_advertise_upsert.add_argument(
                "--peer",
                action="append",
                default=[],
                metavar="PEER",
                help="Optional BGPPeer name to target. Repeat for several.",
            )
            lb_bgp_advertise_upsert.add_argument(
                "--local-pref",
                type=int,
                default=None,
                metavar="N",
            )
            lb_bgp_advertise_upsert.add_argument(
                "--community",
                action="append",
                default=[],
                metavar="VALUE",
                help="Optional BGP community. Repeat for several.",
            )
            self._add_timeout(
                lb_bgp_advertise_upsert,
                help="Maximum time in seconds for BGPAdvertisement convergence.",
            )
            self.terminal(
                lb_bgp_advertise_upsert,
                bertrand_cluster_network_lb_bgp_advertise_upsert,
                cmd=(
                    "bertrand",
                    "cluster",
                    "network",
                    "lb",
                    "bgp",
                    "advertise",
                    "upsert",
                ),
                timeout_scope="cluster",
                kwargs=(
                    ("name", "name"),
                    ("pool", "pool"),
                    ("peer", "peer"),
                    ("local_pref", "local_pref"),
                    ("community", "community"),
                    ("timeout", "timeout"),
                ),
            )
            dns = subcommands.add_parser(
                "dns",
                help=(
                    "Configure BuildKit/container DNS facts for Bertrand "
                    "infrastructure."
                ),
            )
            dns_commands = dns.add_subparsers(
                dest="dns_command",
                title="dns commands",
                metavar="(command)",
                required=True,
            )
            dns_set = dns_commands.add_parser(
                "set",
                help="Replace BuildKit/container DNS overrides and roll builders.",
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
            self._add_timeout(
                dns_set,
                help="Maximum time in seconds for DNS profile convergence.",
            )
            self.terminal(
                dns_set,
                bertrand_cluster_network_dns_set,
                cmd=("bertrand", "cluster", "network", "dns", "set"),
                timeout_scope="cluster",
                kwargs=(
                    ("server", "server"),
                    ("search", "search"),
                    ("option", "option"),
                    ("timeout", "timeout"),
                ),
            )

            dns_clear = dns_commands.add_parser(
                "clear",
                help="Clear BuildKit/container DNS overrides and roll builders.",
            )
            self._add_timeout(
                dns_clear,
                help="Maximum time in seconds for DNS profile convergence.",
            )
            self.terminal(
                dns_clear,
                bertrand_cluster_network_dns_clear,
                cmd=("bertrand", "cluster", "network", "dns", "clear"),
                timeout_scope="cluster",
                kwargs=(("timeout", "timeout"),),
            )

        def node(self) -> None:
            """Add the 'node' command namespace to the parser."""
            command = self.commands.add_parser(
                "node",
                help="Inspect and mutate this host's Bertrand node state.",
            )
            subcommands = command.add_subparsers(
                dest="node_command",
                title="node commands",
                metavar="(command)",
                required=True,
            )
            self.json_command(
                subcommands,
                "status",
                help_text=(
                    "Show local node identity, runtime, storage, and device status."
                ),
                func=bertrand_node_status,
                cmd=("bertrand", "node", "status"),
                timeout_scope="node",
            )
            name = subcommands.add_parser(
                "name",
                help="Set or clear this host's optional Bertrand display name.",
            )
            name_subcommands = name.add_subparsers(
                dest="node_name_command",
                title="name commands",
                metavar="(command)",
                required=True,
            )
            name_set = name_subcommands.add_parser(
                "set",
                help="Set the local Bertrand node display name.",
            )
            name_set.add_argument("name", metavar="NAME")
            self._add_timeout(
                name_set,
                help="Maximum time in seconds for node name convergence.",
            )
            self.terminal(
                name_set,
                bertrand_node_name_set,
                cmd=("bertrand", "node", "name", "set"),
                timeout_scope="node",
                kwargs=(("display_name", "name"), ("timeout", "timeout")),
            )

            name_clear = name_subcommands.add_parser(
                "clear",
                help="Clear the local Bertrand node display name.",
            )
            self._add_timeout(
                name_clear,
                help="Maximum time in seconds for node name clearance convergence.",
            )
            self.terminal(
                name_clear,
                bertrand_node_name_set,
                cmd=("bertrand", "node", "name", "clear"),
                timeout_scope="node",
                kwargs=(("timeout", "timeout"),),
                fixed={"display_name": ""},
            )
            self.storage_namespace(
                subcommands,
                dest="storage_command",
                namespace_help="Inspect local Rook/Ceph storage status.",
                status_help="Show Ceph autoscaler policy/status and node reports.",
                doctor_help="Print local Rook/Ceph storage diagnostics.",
                status_func=bertrand_node_storage_status,
                doctor_func=bertrand_node_storage_doctor,
                cmd_prefix=("bertrand", "node", "storage"),
                timeout_scope="node",
            )

            self.secret_namespace(
                subcommands,
                command_dest="node_secret_command",
                namespace_help="Manage local-node secret capabilities.",
                add_help="Create or update a local-node capability.",
                rm_help="Delete a local-node capability.",
                list_help="List local-node capabilities and shared fallbacks.",
                add_func=bertrand_node_secret_add,
                rm_func=bertrand_node_secret_rm,
                list_func=bertrand_node_secret_list,
                cmd_prefix=("bertrand", "node", "secret"),
                timeout_scope="node",
                path_scoped=False,
            )
            device = subcommands.add_parser(
                "device",
                help="Manage local node DRA device inventory.",
            )
            device_subcommands = device.add_subparsers(
                dest="node_device_command",
                title="device commands",
                metavar="(command)",
                required=True,
            )
            node_device_list = device_subcommands.add_parser(
                "list",
                help="List managed DRA device inventory on this node.",
            )
            self.device_list_options(node_device_list)
            self.terminal(
                node_device_list,
                bertrand_node_device_list,
                cmd=("bertrand", "node", "device", "list"),
                timeout_scope="node",
                kwargs=(("json_output", "json"), ("timeout", "timeout")),
            )

            node_device_add = device_subcommands.add_parser(
                "add",
                help="Add or update one local managed DRA device capability.",
            )
            node_device_add.add_argument("capability", metavar="CAPABILITY")
            node_device_add.add_argument("--name", required=True, metavar="NAME")
            node_device_add.add_argument("--cdi", required=True, metavar="SELECTOR")
            node_device_add.add_argument(
                "--attr",
                action="append",
                default=[],
                metavar="KEY=VALUE",
                help="Optional DRA device attribute. Repeat for several.",
            )
            self._add_timeout(
                node_device_add,
                help="Maximum time in seconds for node device convergence.",
            )
            self.terminal(
                node_device_add,
                bertrand_node_device_add,
                cmd=("bertrand", "node", "device", "add"),
                timeout_scope="node",
                kwargs=(
                    ("capability_id", "capability"),
                    ("device_name", "name"),
                    ("cdi_selector", "cdi"),
                    ("attributes", "attr", parse_device_attrs),
                    ("timeout", "timeout"),
                ),
            )

            node_device_rm = device_subcommands.add_parser(
                "rm",
                help="Remove one local managed DRA device capability.",
            )
            node_device_rm.add_argument("capability", metavar="CAPABILITY")
            node_device_rm.add_argument("--name", required=True, metavar="NAME")
            self._add_timeout(
                node_device_rm,
                help="Maximum time in seconds for node device removal convergence.",
            )
            self.terminal(
                node_device_rm,
                bertrand_node_device_rm,
                cmd=("bertrand", "node", "device", "rm"),
                timeout_scope="node",
                kwargs=(
                    ("capability_id", "capability"),
                    ("device_name", "name"),
                    ("timeout", "timeout"),
                ),
            )

        def run(self) -> None:
            """Add the 'run' command to the parser."""
            command = self.commands.add_parser(
                "run",
                help="Build and schedule the configured Kubernetes workload for a "
                "repository/worktree target.",
            )
            self._add_project_path(command)
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
                command_factory=_ExternalLeafFactory(
                    func=bertrand_run,
                    kwargs=(
                        ("target", "path", _resolved_path),
                        ("detach", "detach"),
                        ("tty", "tty"),
                        ("args", "args", tuple),
                    ),
                ),
                command_timeout=math.inf,
                command_timeout_attr=None,
            )

        def enter(self) -> None:
            """Add the 'enter' command to the parser."""
            command = self.commands.add_parser(
                "enter",
                help="Launch an interactive shell inside a Kubernetes dev-session "
                "Pod for a project worktree. Use 'exit' to leave the shell and "
                "return to the host system.",
            )
            self._add_project_path(command)
            command.add_argument(
                "--shell",
                default=None,
                metavar="SHELL",
                help="Override the default shell for this enter session.  "
                "Validation is performed at runtime by `bertrand_enter` against "
                "the configured shell map.",
            )
            command.set_defaults(
                command_factory=_ExternalLeafFactory(
                    func=bertrand_enter,
                    kwargs=(
                        ("target", "path", _resolved_path),
                        ("shell", "shell"),
                    ),
                ),
                command_timeout=math.inf,
                command_timeout_attr=None,
            )

        def code(self) -> None:
            """Add the 'code' command to the parser."""
            command = self.commands.add_parser(
                "code",
                help="Launch a host editor against a generated Kubernetes "
                "dev-session Pod for a project worktree.",
            )
            self._add_project_path(command)
            command.add_argument(
                "--editor",
                default=None,
                metavar="EDITOR",
                help="Override the configured host editor alias for this command.  "
                "Validation is performed at runtime by dev-session config "
                "resolution.",
            )
            command.set_defaults(
                command_factory=_ExternalLeafFactory(
                    func=bertrand_code,
                    kwargs=(
                        ("target", "path", _resolved_path),
                        ("editor", "editor"),
                    ),
                ),
                command_timeout=math.inf,
                command_timeout_attr=None,
            )

        def scale(self) -> None:
            """Add the 'scale' command to the parser."""
            command = self.commands.add_parser(
                "scale",
                help="Scale active Kubernetes workload execution for a "
                "repository/worktree target.",
            )
            self._add_project_path(command)
            command.add_argument(
                "-r",
                "--replicas",
                type=int,
                required=True,
                help="Requested logical workload replica count. Deployments scale to "
                "this value, CronJobs accept 0/1 as suspend/resume, and Job "
                "topology accepts only 0.",
            )
            command.add_argument(
                "-g",
                "--grace",
                type=int,
                default=10,
                help="Kubernetes pod termination grace period in seconds when "
                "scaling active execution down.",
            )
            self._add_timeout(
                command,
                help=(
                    "Maximum time in seconds for Kubernetes API convergence. Use "
                    "inf to wait indefinitely."
                ),
            )
            command.set_defaults(
                command_factory=_ExternalLeafFactory(
                    func=bertrand_scale,
                    kwargs=(
                        ("target", "path", _resolved_path),
                        ("replicas", "replicas"),
                        ("grace_period_seconds", "grace"),
                    ),
                ),
                command_timeout_attr="timeout",
                command_timeout=math.inf,
            )

        def rm(self) -> None:
            """Add the 'rm' command to the parser."""
            command = self.commands.add_parser(
                "rm",
                help="Remove managed Kubernetes workload topology for a "
                "repository/worktree target and retire its internal image records.",
            )
            self._add_project_path(command)
            command.add_argument(
                "-g",
                "--grace",
                type=int,
                default=10,
                help="Kubernetes pod termination grace period in seconds for active "
                "execution objects removed with the topology.",
            )
            self._add_timeout(
                command,
                help=(
                    "Maximum time in seconds for Kubernetes API convergence. Use "
                    "inf to wait indefinitely."
                ),
            )
            command.set_defaults(
                command_factory=_ExternalLeafFactory(
                    func=bertrand_rm,
                    kwargs=(
                        ("target", "path", _resolved_path),
                        ("grace_period_seconds", "grace"),
                    ),
                ),
                command_timeout_attr="timeout",
                command_timeout=math.inf,
            )

        def dashboard(self) -> None:
            """Add the 'dashboard' command to the parser."""
            command = self.commands.add_parser(
                "dashboard",
                help="Open the Bertrand Kubernetes dashboard through a local "
                "Headlamp port-forward.",
            )
            self._add_timeout(
                command,
                help=(
                    "Maximum time in seconds for dashboard convergence and "
                    "port-forward readiness. Use inf to wait indefinitely."
                ),
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
            command.set_defaults(
                command_factory=_ExternalLeafFactory(
                    func=bertrand_dashboard,
                    kwargs=(
                        ("port", "port"),
                        ("open_browser", "open_browser"),
                    ),
                ),
                command_timeout_attr="timeout",
                command_timeout=math.inf,
            )

        def clean(self) -> None:
            """Add the 'clean' command to the parser."""
            command = self.commands.add_parser(
                "clean",
                help="Remove this host from Bertrand's owned k0s/Rook-Ceph runtime; "
                "the final active node also deletes durable repository volumes.",
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
            self._add_timeout(
                command,
                help=(
                    "Maximum time in seconds for cleanup convergence.  Use inf to "
                    "wait indefinitely."
                ),
            )
            command.set_defaults(
                command_factory=_ExternalLeafFactory(
                    func=bertrand_clean,
                    kwargs=(
                        ("yes", "yes"),
                        ("force", "force"),
                    ),
                ),
                command_timeout_attr="timeout",
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
        """Parse and dispatch the selected external command."""
        parser = External.Parser()
        args = parser()
        if args.command is None:
            parser.root.print_help()
            return
        command = args.command_factory(args)
        timeout_attr = args.command_timeout_attr
        timeout = getattr(args, timeout_attr) if timeout_attr else args.command_timeout
        asyncio.run(cli(command, timeout=float(timeout)))
