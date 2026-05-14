"""External CLI endpoints for cluster-level networking configuration."""

from __future__ import annotations

import asyncio
import json
from typing import TYPE_CHECKING

from bertrand.env.git import BERTRAND_NAMESPACE, INFINITY
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.build.daemon import BUILDKIT_POOL
from bertrand.env.kube.build.repository import IMAGES
from bertrand.env.kube.namespace import Namespace
from bertrand.env.kube.network import NETWORK_PROFILE_NAME, NetworkProfile

if TYPE_CHECKING:
    import argparse
    from collections.abc import Sequence


def _flatten(values: Sequence[Sequence[str]] | None) -> tuple[str, ...]:
    if values is None:
        return ()
    return tuple(item for group in values for item in group)


async def bertrand_network(args: argparse.Namespace) -> None:
    """Execute a ``bertrand network`` subcommand.

    Parameters
    ----------
    args : argparse.Namespace
        Parsed external CLI arguments.

    Raises
    ------
    ValueError
        If the parsed network subcommand is unsupported.
    """
    command = args.network_command
    if command == "status":
        await bertrand_network_status(json_output=args.json)
        return
    if command == "dns":
        await _bertrand_network_dns(args)
        return
    msg = f"unsupported network command: {command!r}"
    raise ValueError(msg)


async def bertrand_network_status(*, json_output: bool) -> None:
    """Print cluster networking profile status.

    Parameters
    ----------
    json_output : bool
        Whether to emit machine-readable JSON.
    """
    with await Kube.host(timeout=INFINITY) as kube:
        profile = await NetworkProfile.get(kube, timeout=INFINITY)
        config_hash = await IMAGES.current_buildkit_config_hash(
            kube,
            timeout=INFINITY,
        )
        registry = await IMAGES.status(kube, timeout=INFINITY)
        buildkit = await BUILDKIT_POOL.status(
            kube,
            timeout=INFINITY,
            config_hash=config_hash,
        )

    if json_output:
        print(
            json.dumps(
                {
                    "profile": {
                        "namespace": BERTRAND_NAMESPACE,
                        "name": NETWORK_PROFILE_NAME,
                        "hash": profile.profile_hash,
                        "dns": profile.model_dump(mode="json"),
                    },
                    "buildkit": {
                        "config_hash": config_hash,
                        "registry_ready": registry.ready,
                        "pool_ready": buildkit.ready,
                        "failures": [*registry.failures, *buildkit.failures],
                    },
                },
                indent=2,
                sort_keys=True,
            )
        )
        return

    print(f"network profile: {BERTRAND_NAMESPACE}/{NETWORK_PROFILE_NAME}")
    print(f"  hash: {profile.profile_hash}")
    print(f"  dns servers: {_display_tuple(profile.nameservers)}")
    print(f"  dns search: {_display_tuple(profile.search_domains)}")
    print(f"  dns options: {_display_tuple(profile.options)}")
    print("buildkit:")
    print(f"  config hash: {config_hash}")
    print(f"  registry: {'ready' if registry.ready else 'not ready'}")
    print(f"  pool: {'ready' if buildkit.ready else 'not ready'}")
    failures = [*registry.failures, *buildkit.failures]
    if failures:
        print("  failures:")
        for failure in failures:
            print(f"    - {failure}")


async def _bertrand_network_dns(args: argparse.Namespace) -> None:
    command = args.dns_command
    if command == "set":
        profile = NetworkProfile(
            nameservers=_flatten(args.server),
            search_domains=_flatten(args.search),
            options=_flatten(args.option),
        )
        await _apply_network_profile(profile, timeout=args.timeout)
        _print_dns_profile(profile)
        return
    if command == "clear":
        cleared = NetworkProfile()
        await _apply_network_profile(cleared, timeout=args.timeout)
        _print_dns_profile(cleared)
        return
    msg = f"unsupported network dns command: {command!r}"
    raise ValueError(msg)


async def _apply_network_profile(profile: NetworkProfile, *, timeout: float) -> None:
    if timeout <= 0:
        msg = "network convergence timeout must be non-negative"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    with await Kube.host(timeout=deadline - loop.time()) as kube:
        await Namespace.upsert(
            kube,
            name=BERTRAND_NAMESPACE,
            timeout=deadline - loop.time(),
        )
        await profile.upsert(kube, timeout=deadline - loop.time())
        await IMAGES.ensure(kube, timeout=deadline - loop.time())
        config_hash = await IMAGES.current_buildkit_config_hash(
            kube,
            timeout=deadline - loop.time(),
        )
        await BUILDKIT_POOL.ensure(
            kube,
            timeout=deadline - loop.time(),
            config_hash=config_hash,
        )


def _display_tuple(values: tuple[str, ...]) -> str:
    return ", ".join(values) if values else "default"


def _print_dns_profile(profile: NetworkProfile) -> None:
    print("network DNS profile updated")
    print(f"  dns servers: {_display_tuple(profile.nameservers)}")
    print(f"  dns search: {_display_tuple(profile.search_domains)}")
    print(f"  dns options: {_display_tuple(profile.options)}")
