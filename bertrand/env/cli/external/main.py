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

        def secret(self) -> None:
            """Add the top-level path-scoped 'secret' command namespace."""
            command = self.commands.add_parser(
                "secret",
                help="Manage repository/worktree-scoped secret capabilities.",
            )
            subcommands = command.add_subparsers(
                dest="secret_command",
                title="secret commands",
                metavar="(command)",
                required=True,
            )
            add = subcommands.add_parser(
                "add",
                help="Create or update a repository/worktree-scoped capability.",
            )
            add.add_argument("path", metavar="REPO[/WORKTREE]")
            add.add_argument("id", metavar="ID")
            add.add_argument(
                "source",
                nargs="?",
                metavar="SOURCE",
                help=(
                    "Payload file path, '-' for stdin, or omitted to read piped stdin."
                ),
            )
            add.add_argument(
                "--kind",
                choices=("secret", "ssh"),
                default="secret",
                help="Capability kind to store.",
            )
            add.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for Kubernetes API convergence.",
            )
            add.set_defaults(handler=External.secret)

            rm = subcommands.add_parser(
                "rm",
                help="Delete a repository/worktree-scoped capability.",
            )
            rm.add_argument("path", metavar="REPO[/WORKTREE]")
            rm.add_argument("id", metavar="ID")
            rm.add_argument(
                "--kind",
                choices=("secret", "ssh"),
                default="secret",
                help="Capability kind to remove.",
            )
            rm.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for Kubernetes API convergence.",
            )
            rm.set_defaults(handler=External.secret)

            listing = subcommands.add_parser(
                "list",
                help="List path-relevant capabilities without printing payloads.",
            )
            listing.add_argument("path", metavar="REPO[/WORKTREE]")
            listing.add_argument(
                "--kind",
                choices=("secret", "ssh"),
                default=None,
                help="Optional capability kind filter.",
            )
            listing.add_argument(
                "--json",
                action="store_true",
                help="Emit machine-readable JSON instead of human-readable text.",
            )
            listing.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for Kubernetes API convergence.",
            )
            listing.set_defaults(handler=External.secret)

        def cluster(self) -> None:
            """Add the 'cluster' command namespace to the parser."""
            command = self.commands.add_parser(
                "cluster",
                help="Inspect and manage Bertrand's shared/distributed runtime.",
            )
            subcommands = command.add_subparsers(
                dest="cluster_command",
                title="cluster commands",
                metavar="(command)",
                required=True,
            )

            status = subcommands.add_parser(
                "status",
                help="Show global Bertrand cluster runtime readiness.",
            )
            status.add_argument(
                "--json",
                action="store_true",
                help="Emit machine-readable JSON instead of human-readable text.",
            )
            status.set_defaults(handler=External.cluster)

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
            invite.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for join-token generation.",
            )
            invite.set_defaults(handler=External.cluster)

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
            join.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for cluster join and backend "
                "convergence.",
            )
            join.set_defaults(handler=External.cluster)

            secret = subcommands.add_parser(
                "secret",
                help="Manage shared cluster secret capabilities.",
            )
            secret_subcommands = secret.add_subparsers(
                dest="cluster_secret_command",
                title="secret commands",
                metavar="(command)",
                required=True,
            )
            cluster_secret_add = secret_subcommands.add_parser(
                "add",
                help="Create or update a shared cluster capability.",
            )
            cluster_secret_add.add_argument("id", metavar="ID")
            cluster_secret_add.add_argument(
                "source",
                nargs="?",
                metavar="SOURCE",
                help=(
                    "Payload file path, '-' for stdin, or omitted to read piped stdin."
                ),
            )
            cluster_secret_add.add_argument(
                "--kind",
                choices=("secret", "ssh"),
                default="secret",
                help="Capability kind to store.",
            )
            cluster_secret_add.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for Kubernetes API convergence.",
            )
            cluster_secret_add.set_defaults(handler=External.cluster)

            cluster_secret_rm = secret_subcommands.add_parser(
                "rm",
                help="Delete a shared cluster capability.",
            )
            cluster_secret_rm.add_argument("id", metavar="ID")
            cluster_secret_rm.add_argument(
                "--kind",
                choices=("secret", "ssh"),
                default="secret",
                help="Capability kind to remove.",
            )
            cluster_secret_rm.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for Kubernetes API convergence.",
            )
            cluster_secret_rm.set_defaults(handler=External.cluster)

            cluster_secret_list = secret_subcommands.add_parser(
                "list",
                help="List shared cluster capabilities without printing payloads.",
            )
            cluster_secret_list.add_argument(
                "--kind",
                choices=("secret", "ssh"),
                default=None,
                help="Optional capability kind filter.",
            )
            cluster_secret_list.add_argument(
                "--json",
                action="store_true",
                help="Emit machine-readable JSON instead of human-readable text.",
            )
            cluster_secret_list.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for Kubernetes API convergence.",
            )
            cluster_secret_list.set_defaults(handler=External.cluster)

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
            cluster_device_list.add_argument(
                "--json",
                action="store_true",
                help="Emit machine-readable JSON instead of human-readable text.",
            )
            cluster_device_list.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for Kubernetes API convergence.",
            )
            cluster_device_list.set_defaults(handler=External.cluster)

            storage = subcommands.add_parser(
                "storage",
                help="Inspect cluster-wide Rook/Ceph storage status.",
            )
            storage_subcommands = storage.add_subparsers(
                dest="cluster_storage_command",
                title="storage commands",
                metavar="(command)",
                required=True,
            )
            cluster_storage_status = storage_subcommands.add_parser(
                "status",
                help="Show cluster-wide Rook/Ceph autoscaler state.",
            )
            cluster_storage_status.add_argument(
                "--json",
                action="store_true",
                help="Emit machine-readable JSON instead of human-readable text.",
            )
            cluster_storage_status.set_defaults(handler=External.cluster)
            cluster_storage_doctor = storage_subcommands.add_parser(
                "doctor",
                help="Print actionable Rook/Ceph storage diagnostics.",
            )
            cluster_storage_doctor.add_argument(
                "--json",
                action="store_true",
                help="Emit machine-readable JSON instead of human-readable text.",
            )
            cluster_storage_doctor.set_defaults(handler=External.cluster)

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

            network_status = network_subcommands.add_parser(
                "status",
                help="Show cluster CNI, Gateway, LoadBalancer, route, and DNS state.",
            )
            network_status.add_argument(
                "--json",
                action="store_true",
                help="Emit machine-readable JSON instead of human-readable text.",
            )
            network_status.set_defaults(handler=External.cluster)

            network_doctor = network_subcommands.add_parser(
                "doctor",
                help="Print actionable cluster networking diagnostics.",
            )
            network_doctor.add_argument(
                "--json",
                action="store_true",
                help="Emit machine-readable JSON instead of human-readable text.",
            )
            network_doctor.set_defaults(handler=External.cluster)

            lb = network_subcommands.add_parser(
                "lb",
                help="Inspect and configure Bertrand-managed MetalLB resources.",
            )
            lb_commands = lb.add_subparsers(
                dest="lb_command",
                title="load-balancer commands",
                metavar="(command)",
                required=True,
            )
            lb_status = lb_commands.add_parser(
                "status",
                help="Show MetalLB installation and advertisement state.",
            )
            lb_status.add_argument(
                "--json",
                action="store_true",
                help="Emit machine-readable JSON instead of human-readable text.",
            )
            lb_status.set_defaults(handler=External.cluster)

            lb_install = lb_commands.add_parser(
                "install",
                help="Install Bertrand-managed MetalLB with FRR/BGP support.",
            )
            lb_install.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for MetalLB convergence.",
            )
            lb_install.set_defaults(handler=External.cluster)

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
            lb_pool_upsert.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for IPAddressPool convergence.",
            )
            lb_pool_upsert.set_defaults(handler=External.cluster)

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
            lb_l2_upsert.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for L2Advertisement convergence.",
            )
            lb_l2_upsert.set_defaults(handler=External.cluster)

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
            lb_bgp_peer_upsert.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for BGPPeer convergence.",
            )
            lb_bgp_peer_upsert.set_defaults(handler=External.cluster)

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
            lb_bgp_advertise_upsert.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for BGPAdvertisement convergence.",
            )
            lb_bgp_advertise_upsert.set_defaults(handler=External.cluster)

            dns = network_subcommands.add_parser(
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
            dns_set.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for DNS profile convergence.",
            )
            dns_set.set_defaults(handler=External.cluster)

            dns_clear = dns_commands.add_parser(
                "clear",
                help="Clear BuildKit/container DNS overrides and roll builders.",
            )
            dns_clear.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for DNS profile convergence.",
            )
            dns_clear.set_defaults(handler=External.cluster)

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

            status = subcommands.add_parser(
                "status",
                help="Show local node identity, runtime, storage, and device status.",
            )
            status.add_argument(
                "--json",
                action="store_true",
                help="Emit machine-readable JSON instead of human-readable text.",
            )
            status.set_defaults(handler=External.node)

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
            name_set.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for Kubernetes API convergence.",
            )
            name_set.set_defaults(handler=External.node)

            name_clear = name_subcommands.add_parser(
                "clear",
                help="Clear the local Bertrand node display name.",
            )
            name_clear.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for Kubernetes API convergence.",
            )
            name_clear.set_defaults(handler=External.node)

            storage = subcommands.add_parser(
                "storage",
                help="Inspect local Rook/Ceph storage status.",
            )
            storage_subcommands = storage.add_subparsers(
                dest="storage_command",
                title="storage commands",
                metavar="(command)",
                required=True,
            )
            storage_status = storage_subcommands.add_parser(
                "status",
                help="Show Ceph autoscaler policy/status and node reports.",
            )
            storage_status.add_argument(
                "--json",
                action="store_true",
                help="Emit machine-readable JSON instead of human-readable text.",
            )
            storage_status.set_defaults(handler=External.node)
            doctor = storage_subcommands.add_parser(
                "doctor",
                help="Print local Rook/Ceph storage diagnostics.",
            )
            doctor.add_argument(
                "--json",
                action="store_true",
                help="Emit machine-readable JSON instead of human-readable text.",
            )
            doctor.set_defaults(handler=External.node)

            secret = subcommands.add_parser(
                "secret",
                help="Manage local-node secret capabilities.",
            )
            secret_subcommands = secret.add_subparsers(
                dest="node_secret_command",
                title="secret commands",
                metavar="(command)",
                required=True,
            )
            node_secret_add = secret_subcommands.add_parser(
                "add",
                help="Create or update a local-node capability.",
            )
            node_secret_add.add_argument("id", metavar="ID")
            node_secret_add.add_argument(
                "source",
                nargs="?",
                metavar="SOURCE",
                help=(
                    "Payload file path, '-' for stdin, or omitted to read piped stdin."
                ),
            )
            node_secret_add.add_argument(
                "--kind",
                choices=("secret", "ssh"),
                default="secret",
                help="Capability kind to store.",
            )
            node_secret_add.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for Kubernetes API convergence.",
            )
            node_secret_add.set_defaults(handler=External.node)

            node_secret_rm = secret_subcommands.add_parser(
                "rm",
                help="Delete a local-node capability.",
            )
            node_secret_rm.add_argument("id", metavar="ID")
            node_secret_rm.add_argument(
                "--kind",
                choices=("secret", "ssh"),
                default="secret",
                help="Capability kind to remove.",
            )
            node_secret_rm.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for Kubernetes API convergence.",
            )
            node_secret_rm.set_defaults(handler=External.node)

            node_secret_list = secret_subcommands.add_parser(
                "list",
                help="List local-node capabilities and shared fallbacks.",
            )
            node_secret_list.add_argument(
                "--kind",
                choices=("secret", "ssh"),
                default=None,
                help="Optional capability kind filter.",
            )
            node_secret_list.add_argument(
                "--json",
                action="store_true",
                help="Emit machine-readable JSON instead of human-readable text.",
            )
            node_secret_list.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for Kubernetes API convergence.",
            )
            node_secret_list.set_defaults(handler=External.node)

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
            node_device_list.add_argument(
                "--json",
                action="store_true",
                help="Emit machine-readable JSON instead of human-readable text.",
            )
            node_device_list.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for Kubernetes API convergence.",
            )
            node_device_list.set_defaults(handler=External.node)

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
            node_device_add.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for Kubernetes API convergence.",
            )
            node_device_add.set_defaults(handler=External.node)

            node_device_rm = device_subcommands.add_parser(
                "rm",
                help="Remove one local managed DRA device capability.",
            )
            node_device_rm.add_argument("capability", metavar="CAPABILITY")
            node_device_rm.add_argument("--name", required=True, metavar="NAME")
            node_device_rm.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for Kubernetes API convergence.",
            )
            node_device_rm.set_defaults(handler=External.node)

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

        def scale(self) -> None:
            """Add the 'scale' command to the parser."""
            command = self.commands.add_parser(
                "scale",
                help="Scale active Kubernetes workload execution for a "
                "repository/worktree target.",
            )
            command.add_argument(
                "path",
                metavar="REPO[/WORKTREE]",
                help="Project repository or worktree path. Repository roots target "
                "the worktree attached to HEAD.",
            )
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
            command.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for Kubernetes API convergence. Use "
                "inf to wait indefinitely.",
            )
            command.set_defaults(handler=External.scale)

        def rm(self) -> None:
            """Add the 'rm' command to the parser."""
            command = self.commands.add_parser(
                "rm",
                help="Remove managed Kubernetes workload topology for a "
                "repository/worktree target and retire its internal image records.",
            )
            command.add_argument(
                "path",
                metavar="REPO[/WORKTREE]",
                help="Project repository or worktree path. Repository roots target "
                "the worktree attached to HEAD.",
            )
            command.add_argument(
                "-g",
                "--grace",
                type=int,
                default=10,
                help="Kubernetes pod termination grace period in seconds for active "
                "execution objects removed with the topology.",
            )
            command.add_argument(
                "-t",
                "--timeout",
                type=float,
                default=INFINITY,
                help="Maximum time in seconds for Kubernetes API convergence. Use "
                "inf to wait indefinitely.",
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
    def secret(args: argparse.Namespace) -> None:
        """Execute a top-level ``bertrand secret`` CLI subcommand.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        OSError
            If timeout is invalid.
        TimeoutExpired
            If the secret command does not complete within its timeout.
        """
        from bertrand.env.cli.external.secret import bertrand_secret

        now = time.time()
        timeout = getattr(args, "timeout", INFINITY)
        if math.isnan(timeout) or timeout <= 0:
            msg = f"invalid secret timeout: {timeout} (must be > 0 seconds or inf)"
            raise OSError(msg)
        with asyncio.Runner() as runner:
            try:
                runner.run(bertrand_secret(args))
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now, UTC)
                cmd = ["bertrand", "secret", args.secret_command]
                if hasattr(args, "path"):
                    cmd.append(str(Path(args.path).expanduser()))
                raise TimeoutExpired(
                    cmd=cmd,
                    timeout=timeout,
                    output=None,
                    stderr=f"started: {start}\nstopped: {datetime.now(UTC)}\n",
                ) from err

    @staticmethod
    def cluster(args: argparse.Namespace) -> None:
        """Execute a `bertrand cluster` CLI subcommand.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        OSError
            If timeout is invalid.
        TimeoutExpired
            If the cluster command does not complete within its timeout.
        """
        from bertrand.env.cli.external.cluster import bertrand_cluster

        now = time.time()
        timeout = getattr(args, "timeout", INFINITY)
        if math.isnan(timeout) or timeout <= 0:
            msg = f"invalid cluster timeout: {timeout} (must be > 0 seconds or inf)"
            raise OSError(msg)
        with asyncio.Runner() as runner:
            try:
                runner.run(bertrand_cluster(args))
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now, UTC)
                cmd = ["bertrand", "cluster", args.cluster_command]
                if args.cluster_command == "secret":
                    cmd.append(args.cluster_secret_command)
                if args.cluster_command == "device":
                    cmd.append(args.cluster_device_command)
                if args.cluster_command == "storage":
                    cmd.append(args.cluster_storage_command)
                if args.cluster_command == "network":
                    cmd.append(args.network_command)
                    if args.network_command == "lb":
                        cmd.append(args.lb_command)
                    if args.network_command == "dns":
                        cmd.append(args.dns_command)
                raise TimeoutExpired(
                    cmd=cmd,
                    timeout=timeout,
                    output=None,
                    stderr=f"started: {start}\nstopped: {datetime.now(UTC)}\n",
                ) from err

    @staticmethod
    def node(args: argparse.Namespace) -> None:
        """Execute a `bertrand node` CLI subcommand.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        OSError
            If timeout is invalid.
        TimeoutExpired
            If the node command does not complete within its timeout.
        """
        from bertrand.env.cli.external.node import bertrand_node

        now = time.time()
        timeout = getattr(args, "timeout", INFINITY)
        if math.isnan(timeout) or timeout <= 0:
            msg = f"invalid node timeout: {timeout} (must be > 0 seconds or inf)"
            raise OSError(msg)
        with asyncio.Runner() as runner:
            try:
                runner.run(bertrand_node(args))
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now, UTC)
                cmd = ["bertrand", "node", args.node_command]
                if args.node_command == "name":
                    cmd.append(args.node_name_command)
                if args.node_command == "secret":
                    cmd.append(args.node_secret_command)
                if args.node_command == "device":
                    cmd.append(args.node_device_command)
                if args.node_command == "storage":
                    cmd.append(args.storage_command)
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
    def scale(args: argparse.Namespace) -> None:
        """Execute the `bertrand scale` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        TimeoutExpired
            If the command does not complete within the specified timeout.
        OSError
            If the replica count, grace period, or timeout is invalid.
        """
        from bertrand.env.cli.external.scale import bertrand_scale

        now = time.time()
        if args.replicas < 0:
            msg = "scale replicas cannot be negative"
            raise OSError(msg)
        if args.grace < 0:
            msg = "scale grace period must be non-negative"
            raise OSError(msg)
        if math.isnan(args.timeout) or args.timeout <= 0:
            msg = f"invalid scale timeout: {args.timeout} (must be > 0 seconds or inf)"
            raise OSError(msg)
        with asyncio.Runner() as runner:
            target = Path(args.path).expanduser().resolve()
            try:
                runner.run(
                    bertrand_scale(
                        target,
                        replicas=args.replicas,
                        grace_period_seconds=args.grace,
                        timeout=args.timeout,
                    )
                )
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now, UTC)
                cmd = [
                    "bertrand",
                    "scale",
                    str(Path(args.path).expanduser()),
                    "--replicas",
                    str(args.replicas),
                ]
                if args.grace != 10:
                    cmd.extend(["--grace", str(args.grace)])
                if not math.isinf(args.timeout):
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
        OSError
            If the grace period or timeout is invalid.
        """
        from bertrand.env.cli.external.rm import bertrand_rm

        now = time.time()
        if args.grace < 0:
            msg = "rm grace period must be non-negative"
            raise OSError(msg)
        if math.isnan(args.timeout) or args.timeout <= 0:
            msg = f"invalid rm timeout: {args.timeout} (must be > 0 seconds or inf)"
            raise OSError(msg)
        with asyncio.Runner() as runner:
            target = Path(args.path).expanduser().resolve()
            try:
                runner.run(
                    bertrand_rm(
                        target,
                        grace_period_seconds=args.grace,
                        timeout=args.timeout,
                    )
                )
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now, UTC)
                cmd = ["bertrand", "rm", str(Path(args.path).expanduser())]
                if args.grace != 10:
                    cmd.extend(["--grace", str(args.grace)])
                if not math.isinf(args.timeout):
                    cmd.extend(["--timeout", str(args.timeout)])
                raise TimeoutExpired(
                    cmd=cmd,
                    timeout=args.timeout,
                    output=None,
                    stderr=f"started: {start}\nstopped: {datetime.now(UTC)}\n",
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
