"""Bertrand's out-of-container CLI endpoints."""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

from bertrand.env.cli.external._runtime import run_async, validate_timeout
from bertrand.env.cli.external.build import bertrand_build
from bertrand.env.cli.external.clean import bertrand_clean
from bertrand.env.cli.external.cluster import bertrand_cluster
from bertrand.env.cli.external.code import bertrand_code
from bertrand.env.cli.external.dashboard import bertrand_dashboard
from bertrand.env.cli.external.enter import bertrand_enter
from bertrand.env.cli.external.init import bertrand_init
from bertrand.env.cli.external.node import bertrand_node
from bertrand.env.cli.external.rm import bertrand_rm
from bertrand.env.cli.external.run import bertrand_run
from bertrand.env.cli.external.scale import bertrand_scale
from bertrand.env.cli.external.secret import bertrand_secret
from bertrand.env.git import INFINITY
from bertrand.env.version import __version__

_JSON_HELP = "Emit machine-readable JSON instead of human-readable text."
_KUBE_TIMEOUT_HELP = "Maximum time in seconds for Kubernetes API convergence."
_PROJECT_PATH_HELP = (
    "Project repository or worktree path. Repository roots target the worktree "
    "attached to HEAD."
)
_CAPABILITY_SOURCE_HELP = (
    "Payload file path, '-' for stdin, or omitted to read piped stdin."
)
_CAPABILITY_KIND_STORE_HELP = "Capability kind to store."
_CAPABILITY_KIND_REMOVE_HELP = "Capability kind to remove."
_CAPABILITY_KIND_FILTER_HELP = "Optional capability kind filter."


def _required_subcommands(
    parser: argparse.ArgumentParser,
    *,
    dest: str,
    title: str,
    metavar: str = "(command)",
) -> argparse._SubParsersAction[argparse.ArgumentParser]:
    return parser.add_subparsers(
        dest=dest,
        title=title,
        metavar=metavar,
        required=True,
    )


def _add_json(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--json",
        action="store_true",
        help=_JSON_HELP,
    )


def _add_timeout(
    parser: argparse.ArgumentParser,
    *,
    help_text: str = _KUBE_TIMEOUT_HELP,
) -> None:
    parser.add_argument(
        "-t",
        "--timeout",
        type=float,
        default=INFINITY,
        help=help_text,
    )


def _add_project_path(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "path",
        metavar="REPO[/WORKTREE]",
        help=_PROJECT_PATH_HELP,
    )


def _add_capability_source(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "source",
        nargs="?",
        metavar="SOURCE",
        help=_CAPABILITY_SOURCE_HELP,
    )


def _add_capability_kind(
    parser: argparse.ArgumentParser,
    *,
    help_text: str,
) -> None:
    parser.add_argument(
        "--kind",
        choices=("secret", "ssh"),
        default="secret",
        help=help_text,
    )


def _add_capability_kind_filter(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--kind",
        choices=("secret", "ssh"),
        default=None,
        help=_CAPABILITY_KIND_FILTER_HELP,
    )


def _set_handler(parser: argparse.ArgumentParser, handler: object) -> None:
    parser.set_defaults(handler=handler)


def _add_secret_namespace(
    subcommands: argparse._SubParsersAction[argparse.ArgumentParser],
    *,
    command_dest: str,
    namespace_help: str,
    add_help: str,
    rm_help: str,
    list_help: str,
    handler: object,
    path_scoped: bool,
) -> None:
    secret = subcommands.add_parser("secret", help=namespace_help)
    secret_subcommands = _required_subcommands(
        secret,
        dest=command_dest,
        title="secret commands",
    )

    add = secret_subcommands.add_parser("add", help=add_help)
    if path_scoped:
        add.add_argument("path", metavar="REPO[/WORKTREE]")
    add.add_argument("id", metavar="ID")
    _add_capability_source(add)
    _add_capability_kind(add, help_text=_CAPABILITY_KIND_STORE_HELP)
    _add_timeout(add)
    _set_handler(add, handler)

    rm = secret_subcommands.add_parser("rm", help=rm_help)
    if path_scoped:
        rm.add_argument("path", metavar="REPO[/WORKTREE]")
    rm.add_argument("id", metavar="ID")
    _add_capability_kind(rm, help_text=_CAPABILITY_KIND_REMOVE_HELP)
    _add_timeout(rm)
    _set_handler(rm, handler)

    listing = secret_subcommands.add_parser("list", help=list_help)
    if path_scoped:
        listing.add_argument("path", metavar="REPO[/WORKTREE]")
    _add_capability_kind_filter(listing)
    _add_json(listing)
    _add_timeout(listing)
    _set_handler(listing, handler)


def _add_storage_namespace(
    subcommands: argparse._SubParsersAction[argparse.ArgumentParser],
    *,
    dest: str,
    namespace_help: str,
    status_help: str,
    doctor_help: str,
    handler: object,
) -> None:
    storage = subcommands.add_parser("storage", help=namespace_help)
    storage_subcommands = _required_subcommands(
        storage,
        dest=dest,
        title="storage commands",
    )
    status = storage_subcommands.add_parser("status", help=status_help)
    _add_json(status)
    _set_handler(status, handler)
    doctor = storage_subcommands.add_parser("doctor", help=doctor_help)
    _add_json(doctor)
    _set_handler(doctor, handler)


def _add_device_list_options(parser: argparse.ArgumentParser) -> None:
    _add_json(parser)
    _add_timeout(parser)


def _resolved_path(raw: str) -> Path:
    return Path(raw).expanduser().resolve()


def _display_path(raw: str) -> Path:
    return Path(raw).expanduser()


def _append_flag(cmd: list[str], flag: str, *, enabled: bool) -> None:
    if enabled:
        cmd.append(flag)


def _append_option(cmd: list[str], flag: str, value: object | None) -> None:
    if value is not None:
        cmd.extend([flag, str(value)])


def _append_timeout(cmd: list[str], timeout: float) -> None:
    if not math.isinf(timeout):
        cmd.extend(["--timeout", str(timeout)])


def _nested_command(
    prefix: str,
    args: argparse.Namespace,
    *attrs: str,
) -> list[str]:
    cmd = ["bertrand", prefix]
    for attr in attrs:
        value = getattr(args, attr, None)
        if value:
            cmd.append(value)
    return cmd


def _nested_command_tail(args: argparse.Namespace, *attrs: str) -> list[str]:
    return [str(getattr(args, attr)) for attr in attrs if getattr(args, attr, None)]


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
            _set_handler(command, External.init)

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
            _set_handler(command, External.build)

        def secret(self) -> None:
            """Add the top-level path-scoped 'secret' command namespace."""
            _add_secret_namespace(
                self.commands,
                command_dest="secret_command",
                namespace_help="Manage repository/worktree-scoped secret capabilities.",
                add_help=(
                    "Create or update a repository/worktree-scoped capability."
                ),
                rm_help="Delete a repository/worktree-scoped capability.",
                list_help=(
                    "List path-relevant capabilities without printing payloads."
                ),
                handler=External.secret,
                path_scoped=True,
            )

        def cluster(self) -> None:
            """Add the 'cluster' command namespace to the parser."""
            command = self.commands.add_parser(
                "cluster",
                help="Inspect and manage Bertrand's shared/distributed runtime.",
            )
            subcommands = _required_subcommands(
                command,
                dest="cluster_command",
                title="cluster commands",
            )

            status = subcommands.add_parser(
                "status",
                help="Show global Bertrand cluster runtime readiness.",
            )
            _add_json(status)
            _set_handler(status, External.cluster)

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
                "--worker",
                action="store_true",
                help="Generate/mark the bundle for a MicroK8s worker join.",
            )
            _add_timeout(
                invite,
                help_text="Maximum time in seconds for join-token generation.",
            )
            _set_handler(invite, External.cluster)

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
                "--worker",
                action="store_true",
                help="Force the MicroK8s join to use worker-node semantics.",
            )
            _add_timeout(
                join,
                help_text=(
                    "Maximum time in seconds for cluster join and backend "
                    "convergence."
                ),
            )
            _set_handler(join, External.cluster)

            _add_secret_namespace(
                subcommands,
                command_dest="cluster_secret_command",
                namespace_help="Manage shared cluster secret capabilities.",
                add_help="Create or update a shared cluster capability.",
                rm_help="Delete a shared cluster capability.",
                list_help="List shared cluster capabilities without printing payloads.",
                handler=External.cluster,
                path_scoped=False,
            )

            device = subcommands.add_parser(
                "device",
                help="List managed DRA device capability inventory across the cluster.",
            )
            device_subcommands = _required_subcommands(
                device,
                dest="cluster_device_command",
                title="device commands",
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
            _add_device_list_options(cluster_device_list)
            _set_handler(cluster_device_list, External.cluster)

            _add_storage_namespace(
                subcommands,
                dest="cluster_storage_command",
                namespace_help="Inspect cluster-wide Rook/Ceph storage status.",
                status_help="Show cluster-wide Rook/Ceph autoscaler state.",
                doctor_help="Print actionable Rook/Ceph storage diagnostics.",
                handler=External.cluster,
            )

            network = subcommands.add_parser(
                "network",
                help="Inspect and configure cluster-level Bertrand networking.",
            )
            network_subcommands = _required_subcommands(
                network,
                dest="network_command",
                title="network commands",
            )

            network_status = network_subcommands.add_parser(
                "status",
                help="Show cluster CNI, Gateway, LoadBalancer, route, and DNS state.",
            )
            _add_json(network_status)
            _set_handler(network_status, External.cluster)

            network_doctor = network_subcommands.add_parser(
                "doctor",
                help="Print actionable cluster networking diagnostics.",
            )
            _add_json(network_doctor)
            _set_handler(network_doctor, External.cluster)

            lb = network_subcommands.add_parser(
                "lb",
                help="Inspect and configure Bertrand-managed MetalLB resources.",
            )
            lb_commands = _required_subcommands(
                lb,
                dest="lb_command",
                title="load-balancer commands",
            )
            lb_status = lb_commands.add_parser(
                "status",
                help="Show MetalLB installation and advertisement state.",
            )
            _add_json(lb_status)
            _set_handler(lb_status, External.cluster)

            lb_install = lb_commands.add_parser(
                "install",
                help="Install Bertrand-managed MetalLB with FRR/BGP support.",
            )
            _add_timeout(
                lb_install,
                help_text="Maximum time in seconds for MetalLB convergence.",
            )
            _set_handler(lb_install, External.cluster)

            lb_pool = lb_commands.add_parser(
                "pool",
                help="Manage MetalLB address pools.",
            )
            lb_pool_commands = _required_subcommands(
                lb_pool,
                dest="lb_pool_command",
                title="pool commands",
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
            _add_timeout(
                lb_pool_upsert,
                help_text="Maximum time in seconds for IPAddressPool convergence.",
            )
            _set_handler(lb_pool_upsert, External.cluster)

            lb_l2 = lb_commands.add_parser(
                "l2",
                help="Manage MetalLB Layer 2 advertisements.",
            )
            lb_l2_commands = _required_subcommands(
                lb_l2,
                dest="lb_l2_command",
                title="l2 commands",
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
            _add_timeout(
                lb_l2_upsert,
                help_text="Maximum time in seconds for L2Advertisement convergence.",
            )
            _set_handler(lb_l2_upsert, External.cluster)

            lb_bgp = lb_commands.add_parser(
                "bgp",
                help="Manage MetalLB BGP peers and advertisements.",
            )
            lb_bgp_commands = _required_subcommands(
                lb_bgp,
                dest="lb_bgp_command",
                title="bgp commands",
            )
            lb_bgp_peer = lb_bgp_commands.add_parser(
                "peer",
                help="Manage MetalLB BGPPeer resources.",
            )
            lb_bgp_peer_commands = _required_subcommands(
                lb_bgp_peer,
                dest="lb_bgp_peer_command",
                title="bgp peer commands",
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
            _add_timeout(
                lb_bgp_peer_upsert,
                help_text="Maximum time in seconds for BGPPeer convergence.",
            )
            _set_handler(lb_bgp_peer_upsert, External.cluster)

            lb_bgp_advertise = lb_bgp_commands.add_parser(
                "advertise",
                help="Manage MetalLB BGPAdvertisement resources.",
            )
            lb_bgp_advertise_commands = _required_subcommands(
                lb_bgp_advertise,
                dest="lb_bgp_advertise_command",
                title="bgp advertise commands",
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
            _add_timeout(
                lb_bgp_advertise_upsert,
                help_text="Maximum time in seconds for BGPAdvertisement convergence.",
            )
            _set_handler(lb_bgp_advertise_upsert, External.cluster)

            dns = network_subcommands.add_parser(
                "dns",
                help=(
                    "Configure BuildKit/container DNS facts for Bertrand "
                    "infrastructure."
                ),
            )
            dns_commands = _required_subcommands(
                dns,
                dest="dns_command",
                title="dns commands",
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
            _add_timeout(
                dns_set,
                help_text="Maximum time in seconds for DNS profile convergence.",
            )
            _set_handler(dns_set, External.cluster)

            dns_clear = dns_commands.add_parser(
                "clear",
                help="Clear BuildKit/container DNS overrides and roll builders.",
            )
            _add_timeout(
                dns_clear,
                help_text="Maximum time in seconds for DNS profile convergence.",
            )
            _set_handler(dns_clear, External.cluster)

        def node(self) -> None:
            """Add the 'node' command namespace to the parser."""
            command = self.commands.add_parser(
                "node",
                help="Inspect and mutate this host's Bertrand node state.",
            )
            subcommands = _required_subcommands(
                command,
                dest="node_command",
                title="node commands",
            )

            status = subcommands.add_parser(
                "status",
                help="Show local node identity, runtime, storage, and device status.",
            )
            _add_json(status)
            _set_handler(status, External.node)

            name = subcommands.add_parser(
                "name",
                help="Set or clear this host's optional Bertrand display name.",
            )
            name_subcommands = _required_subcommands(
                name,
                dest="node_name_command",
                title="name commands",
            )
            name_set = name_subcommands.add_parser(
                "set",
                help="Set the local Bertrand node display name.",
            )
            name_set.add_argument("name", metavar="NAME")
            _add_timeout(name_set)
            _set_handler(name_set, External.node)

            name_clear = name_subcommands.add_parser(
                "clear",
                help="Clear the local Bertrand node display name.",
            )
            _add_timeout(name_clear)
            _set_handler(name_clear, External.node)

            _add_storage_namespace(
                subcommands,
                dest="storage_command",
                namespace_help="Inspect local Rook/Ceph storage status.",
                status_help="Show Ceph autoscaler policy/status and node reports.",
                doctor_help="Print local Rook/Ceph storage diagnostics.",
                handler=External.node,
            )

            _add_secret_namespace(
                subcommands,
                command_dest="node_secret_command",
                namespace_help="Manage local-node secret capabilities.",
                add_help="Create or update a local-node capability.",
                rm_help="Delete a local-node capability.",
                list_help="List local-node capabilities and shared fallbacks.",
                handler=External.node,
                path_scoped=False,
            )

            device = subcommands.add_parser(
                "device",
                help="Manage local node DRA device inventory.",
            )
            device_subcommands = _required_subcommands(
                device,
                dest="node_device_command",
                title="device commands",
            )
            node_device_list = device_subcommands.add_parser(
                "list",
                help="List managed DRA device inventory on this node.",
            )
            _add_device_list_options(node_device_list)
            _set_handler(node_device_list, External.node)

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
            _add_timeout(node_device_add)
            _set_handler(node_device_add, External.node)

            node_device_rm = device_subcommands.add_parser(
                "rm",
                help="Remove one local managed DRA device capability.",
            )
            node_device_rm.add_argument("capability", metavar="CAPABILITY")
            node_device_rm.add_argument("--name", required=True, metavar="NAME")
            _add_timeout(node_device_rm)
            _set_handler(node_device_rm, External.node)

        def run(self) -> None:
            """Add the 'run' command to the parser."""
            command = self.commands.add_parser(
                "run",
                help="Build and schedule the configured Kubernetes workload for a "
                "repository/worktree target.",
            )
            _add_project_path(command)
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
            _add_project_path(command)
            command.add_argument(
                "--shell",
                default=None,
                metavar="SHELL",
                help="Override the default shell for this enter session.  "
                "Validation is performed at runtime by `bertrand_enter` against "
                "the configured shell map.",
            )
            _set_handler(command, External.enter)

        def code(self) -> None:
            """Add the 'code' command to the parser."""
            command = self.commands.add_parser(
                "code",
                help="Launch a host editor against a generated Kubernetes "
                "dev-session Pod for a project worktree.",
            )
            _add_project_path(command)
            command.add_argument(
                "--editor",
                default=None,
                metavar="EDITOR",
                help="Override the configured host editor alias for this command.  "
                "Validation is performed at runtime by dev-session config "
                "resolution.",
            )
            _set_handler(command, External.code)

        def scale(self) -> None:
            """Add the 'scale' command to the parser."""
            command = self.commands.add_parser(
                "scale",
                help="Scale active Kubernetes workload execution for a "
                "repository/worktree target.",
            )
            _add_project_path(command)
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
            _add_timeout(
                command,
                help_text=(
                    "Maximum time in seconds for Kubernetes API convergence. Use "
                    "inf to wait indefinitely."
                ),
            )
            _set_handler(command, External.scale)

        def rm(self) -> None:
            """Add the 'rm' command to the parser."""
            command = self.commands.add_parser(
                "rm",
                help="Remove managed Kubernetes workload topology for a "
                "repository/worktree target and retire its internal image records.",
            )
            _add_project_path(command)
            command.add_argument(
                "-g",
                "--grace",
                type=int,
                default=10,
                help="Kubernetes pod termination grace period in seconds for active "
                "execution objects removed with the topology.",
            )
            _add_timeout(
                command,
                help_text=(
                    "Maximum time in seconds for Kubernetes API convergence. Use "
                    "inf to wait indefinitely."
                ),
            )
            _set_handler(command, External.rm)

        def dashboard(self) -> None:
            """Add the 'dashboard' command to the parser."""
            command = self.commands.add_parser(
                "dashboard",
                help="Open the Bertrand Kubernetes dashboard through a local "
                "Headlamp port-forward.",
            )
            _add_timeout(
                command,
                help_text=(
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
            _set_handler(command, External.dashboard)

        def clean(self) -> None:
            """Add the 'clean' command to the parser."""
            command = self.commands.add_parser(
                "clean",
                help="Remove host-local Bertrand runtime state in the shared "
                "MicroK8s/Rook-Ceph model while preserving repository PVCs and "
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
            _add_timeout(
                command,
                help_text=(
                    "Maximum time in seconds for cleanup convergence.  Use inf to "
                    "wait indefinitely."
                ),
            )
            _set_handler(command, External.clean)

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

        """
        timeout = validate_timeout("init", args.timeout)
        run_async(
            bertrand_init(
                None if args.path is None else Path(args.path).expanduser().resolve(),
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

        """
        worktree = _resolved_path(args.path)
        cmd = ["bertrand", "build", str(worktree)]
        if args.publish is not None:
            cmd.append("--publish")
            if args.publish:
                cmd.append(args.publish)
        _append_option(cmd, "--auth", args.auth or None)
        _append_flag(cmd, "--detach", enabled=args.detach)
        run_async(
            bertrand_build(
                worktree,
                publish=args.publish,
                auth=args.auth,
                quiet=False,
                detach=args.detach,
            ),
            cmd=cmd,
            timeout=0.0,
        )

    @staticmethod
    def secret(args: argparse.Namespace) -> None:
        """Execute a top-level ``bertrand secret`` CLI subcommand.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        """
        timeout = validate_timeout("secret", getattr(args, "timeout", INFINITY))
        cmd = ["bertrand", "secret", args.secret_command]
        if hasattr(args, "path"):
            cmd.append(str(_display_path(args.path)))
        run_async(bertrand_secret(args), cmd=cmd, timeout=timeout)

    @staticmethod
    def cluster(args: argparse.Namespace) -> None:
        """Execute a `bertrand cluster` CLI subcommand.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        """
        timeout = validate_timeout("cluster", getattr(args, "timeout", INFINITY))
        cmd = _nested_command("cluster", args, "cluster_command")
        if args.cluster_command == "secret":
            cmd.extend(_nested_command_tail(args, "cluster_secret_command"))
        if args.cluster_command == "device":
            cmd.extend(_nested_command_tail(args, "cluster_device_command"))
        if args.cluster_command == "storage":
            cmd.extend(_nested_command_tail(args, "cluster_storage_command"))
        if args.cluster_command == "network":
            cmd.extend(_nested_command_tail(args, "network_command"))
            if args.network_command == "lb":
                cmd.extend(_nested_command_tail(args, "lb_command"))
            if args.network_command == "dns":
                cmd.extend(_nested_command_tail(args, "dns_command"))
        run_async(bertrand_cluster(args), cmd=cmd, timeout=timeout)

    @staticmethod
    def node(args: argparse.Namespace) -> None:
        """Execute a `bertrand node` CLI subcommand.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        """
        timeout = validate_timeout("node", getattr(args, "timeout", INFINITY))
        cmd = _nested_command("node", args, "node_command")
        if args.node_command == "name":
            cmd.extend(_nested_command_tail(args, "node_name_command"))
        if args.node_command == "secret":
            cmd.extend(_nested_command_tail(args, "node_secret_command"))
        if args.node_command == "device":
            cmd.extend(_nested_command_tail(args, "node_device_command"))
        if args.node_command == "storage":
            cmd.extend(_nested_command_tail(args, "storage_command"))
        run_async(bertrand_node(args), cmd=cmd, timeout=timeout)

    @staticmethod
    def run(args: argparse.Namespace) -> None:
        """Execute the `bertrand run` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        """
        target = _display_path(args.path)
        cmd = ["bertrand", "run", str(target)]
        _append_flag(cmd, "--detach", enabled=args.detach)
        if args.tty is True:
            cmd.append("--tty")
        elif args.tty is False:
            cmd.append("--no-tty")
        if args.args:
            cmd.append("--")
            cmd.extend(args.args)
        run_async(
            bertrand_run(
                _resolved_path(args.path),
                detach=args.detach,
                tty=args.tty,
                args=args.args,
            ),
            cmd=cmd,
            timeout=0.0,
            swallow_keyboard_interrupt=True,
        )

    @staticmethod
    def enter(args: argparse.Namespace) -> None:
        """Execute the `bertrand enter` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        """
        target = _display_path(args.path)
        cmd = ["bertrand", "enter", str(target)]
        _append_option(cmd, "--shell", args.shell or None)
        run_async(
            bertrand_enter(
                _resolved_path(args.path),
                shell=args.shell or None,
            ),
            cmd=cmd,
            timeout=0.0,
        )

    @staticmethod
    def code(args: argparse.Namespace) -> None:
        """Execute the `bertrand code` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        """
        target = _display_path(args.path)
        cmd = ["bertrand", "code", str(target)]
        _append_option(cmd, "--editor", args.editor or None)
        run_async(
            bertrand_code(
                _resolved_path(args.path),
                editor=args.editor or None,
            ),
            cmd=cmd,
            timeout=0.0,
        )

    @staticmethod
    def scale(args: argparse.Namespace) -> None:
        """Execute the `bertrand scale` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        OSError
            If the replica count or grace period is invalid.
        """
        if args.replicas < 0:
            msg = "scale replicas cannot be negative"
            raise OSError(msg)
        if args.grace < 0:
            msg = "scale grace period must be non-negative"
            raise OSError(msg)
        timeout = validate_timeout("scale", args.timeout)
        target = _display_path(args.path)
        cmd = [
            "bertrand",
            "scale",
            str(target),
            "--replicas",
            str(args.replicas),
        ]
        _append_option(cmd, "--grace", args.grace if args.grace != 10 else None)
        _append_timeout(cmd, timeout)
        run_async(
            bertrand_scale(
                _resolved_path(args.path),
                replicas=args.replicas,
                grace_period_seconds=args.grace,
                timeout=timeout,
            ),
            cmd=cmd,
            timeout=timeout,
        )

    @staticmethod
    def rm(args: argparse.Namespace) -> None:
        """Execute the `bertrand rm` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        OSError
            If the grace period is invalid.
        """
        if args.grace < 0:
            msg = "rm grace period must be non-negative"
            raise OSError(msg)
        timeout = validate_timeout("rm", args.timeout)
        target = _display_path(args.path)
        cmd = ["bertrand", "rm", str(target)]
        _append_option(cmd, "--grace", args.grace if args.grace != 10 else None)
        _append_timeout(cmd, timeout)
        run_async(
            bertrand_rm(
                _resolved_path(args.path),
                grace_period_seconds=args.grace,
                timeout=timeout,
            ),
            cmd=cmd,
            timeout=timeout,
        )

    @staticmethod
    def dashboard(args: argparse.Namespace) -> None:
        """Execute the `bertrand dashboard` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        """
        cmd = ["bertrand", "dashboard"]
        _append_option(cmd, "--port", args.port if args.port else None)
        if args.open_browser is True:
            cmd.append("--open")
        elif args.open_browser is False:
            cmd.append("--no-open")
        _append_timeout(cmd, args.timeout)
        run_async(
            bertrand_dashboard(
                port=args.port,
                open_browser=args.open_browser,
                timeout=args.timeout,
            ),
            cmd=cmd,
            timeout=args.timeout,
        )

    @staticmethod
    def clean(args: argparse.Namespace) -> None:
        """Execute the `bertrand clean` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        """
        timeout = validate_timeout("clean", args.timeout)
        run_async(
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
