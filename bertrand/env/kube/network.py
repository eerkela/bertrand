"""Network argument formatting helpers for kube runtime assembly."""
from __future__ import annotations

from typing import Protocol


class NetworkTableLike(Protocol):
    """Structural type for network table argument emitters."""
    mode: str
    options: list[str]
    dns: list[str]
    dns_search: list[str]
    dns_options: list[str]
    add_host: dict[str, str]


def format_network(network: NetworkTableLike) -> list[str]:
    """Format network configuration into CLI flags."""
    args: list[str] = ["--network"]
    if network.options:
        args.append(f"{network.mode}:{','.join(network.options)}")
    else:
        args.append(network.mode)
    for dns in network.dns:
        args.extend(["--dns", dns])
    for search in network.dns_search:
        args.extend(["--dns-search", search])
    for option in network.dns_options:
        args.extend(["--dns-option", option])
    for host in sorted(network.add_host):
        args.extend(["--add-host", f"{host}:{network.add_host[host]}"])
    return args


def format_cpus(cpus: float) -> list[str]:
    """Format CPU quota into CLI flags."""
    return ["--cpus", str(cpus)] if cpus > 0 else []
