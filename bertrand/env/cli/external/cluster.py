"""External CLI endpoints for distributed Bertrand cluster operations."""

from __future__ import annotations

import asyncio
import base64
import json
import socket
from collections.abc import Mapping, Sequence
from datetime import UTC, datetime
from typing import TYPE_CHECKING, cast

from bertrand.env.cli.external.secret import (
    add_capability,
    list_capabilities,
    remove_capability,
    shared_capability_ref,
    shared_scope_targets,
)
from bertrand.env.git import BERTRAND_NAMESPACE, INFINITY, run
from bertrand.env.kube.api.bootstrap import (
    ensure_microk8s_kubeconfig,
    join_microk8s_cluster,
    microk8s_cluster_ready,
    microk8s_join_token,
)
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.build.daemon import BUILDKIT_POOL
from bertrand.env.kube.build.repository import IMAGES
from bertrand.env.kube.capability.device import (
    BertrandDeviceRecord,
    list_device_inventory,
)
from bertrand.env.kube.ceph.bootstrap import (
    join_microceph_cluster,
    link_kube_ceph,
    microceph_cluster_ready,
    microceph_join_token,
)
from bertrand.env.kube.crd import CustomResourceDefinition
from bertrand.env.kube.dev.mailbox import CODE_OPEN_PLURAL, DEV_GROUP
from bertrand.env.kube.gateway import Gateway, GatewayClass, HTTPRoute
from bertrand.env.kube.namespace import Namespace
from bertrand.env.kube.network.bootstrap import (
    ENVOY_GATEWAY_DEPLOYMENT,
    ENVOY_GATEWAY_NAMESPACE,
)
from bertrand.env.kube.network.cni import inspect_cni
from bertrand.env.kube.network.gateway import (
    BERTRAND_GATEWAY,
    BERTRAND_GATEWAY_CLASS,
    HTTP_ROUTE_LABEL,
    HTTP_ROUTE_LABEL_VALUE,
)
from bertrand.env.kube.network.load_balancer import (
    BGPAdvertisement,
    BGPPeer,
    IPAddressPool,
    L2Advertisement,
    ensure_metallb,
    metallb_status,
)
from bertrand.env.kube.network.profile import NETWORK_PROFILE_NAME, NetworkProfile
from bertrand.env.kube.node_identity import BertrandNodeRecord, list_bertrand_nodes

if TYPE_CHECKING:
    import argparse
    from collections.abc import Awaitable, Callable

JOIN_BUNDLE_VERSION = 1


def _flatten(values: Sequence[Sequence[str]] | None) -> tuple[str, ...]:
    if values is None:
        return ()
    return tuple(item for group in values for item in group)


async def bertrand_cluster(args: argparse.Namespace) -> None:
    """Execute a ``bertrand cluster`` subcommand.

    Parameters
    ----------
    args : argparse.Namespace
        Parsed external CLI arguments.

    Raises
    ------
    ValueError
        If the parsed cluster subcommand is unsupported.
    """
    command = args.cluster_command
    if command == "status":
        await bertrand_cluster_status(json_output=args.json)
        return
    if command == "invite":
        await bertrand_cluster_invite(
            name=args.name,
            worker=args.worker,
            timeout=args.timeout,
        )
        return
    if command == "join":
        await bertrand_cluster_join(
            token=args.token,
            worker=args.worker,
            microceph_ip=args.microceph_ip,
            timeout=args.timeout,
        )
        return
    if command == "secret":
        await _bertrand_cluster_secret(args)
        return
    if command == "device":
        await _bertrand_cluster_device(args)
        return
    if command == "network":
        await _bertrand_cluster_network(args)
        return
    msg = f"unsupported cluster command: {command!r}"
    raise ValueError(msg)


async def bertrand_cluster_status(*, json_output: bool) -> None:
    """Print shared Bertrand cluster status.

    Parameters
    ----------
    json_output : bool
        Whether to emit machine-readable JSON.
    """
    status: dict[str, object] = {
        "microk8s": await _probe_bool(lambda: microk8s_cluster_ready(timeout=INFINITY)),
        "microceph": await _probe_bool(
            lambda: microceph_cluster_ready(timeout=INFINITY)
        ),
        "namespace": await _probe_kube(_namespace_status),
        "buildkit": await _probe_kube(_buildkit_status),
        "gateway": await _probe_kube(_gateway_status),
        "ceph_csi": await _probe_host(_ceph_csi_status),
        "storage": await _probe_kube(_storage_status),
        "dev": await _probe_kube(_dev_status),
    }
    if json_output:
        print(json.dumps(status, indent=2, sort_keys=True))
        return
    print("cluster:")
    for name, value in status.items():
        ready, detail = _status_line(value)
        print(f"  {name}: {ready}")
        if detail:
            print(f"    {detail}")


async def bertrand_cluster_invite(
    *,
    name: str | None,
    worker: bool,
    timeout: float,
) -> None:
    """Generate a sensitive Bertrand distributed-runtime join bundle.

    Parameters
    ----------
    name : str | None
        Desired name for the joining node.
    worker : bool
        Whether the joining MicroK8s node should be a worker.
    timeout : float
        Maximum token generation budget in seconds.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    """
    if timeout <= 0:
        msg = "cluster invite timeout must be non-negative"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    node_name = (
        name or f"bertrand-node-{datetime.now(UTC).strftime('%Y%m%d%H%M%S')}"
    ).strip()
    microk8s = await microk8s_join_token(
        worker=worker,
        timeout=deadline - loop.time(),
    )
    microceph = await microceph_join_token(
        node_name,
        timeout=deadline - loop.time(),
    )
    payload = {
        "version": JOIN_BUNDLE_VERSION,
        "created_at": datetime.now(UTC).isoformat(),
        "node_name": node_name,
        "worker": worker,
        "microk8s": microk8s,
        "microceph": microceph,
    }
    token = _encode_bundle(payload)
    print("Sensitive Bertrand cluster join token:")
    print(token)
    print()
    print("Run on the joining host:")
    worker_flag = " --worker" if worker else ""
    print(f"  bertrand cluster join {token}{worker_flag}")


async def bertrand_cluster_join(
    *,
    token: str,
    worker: bool,
    microceph_ip: str | None,
    timeout: float,
) -> None:
    """Join this host to an existing Bertrand shared runtime cluster.

    Parameters
    ----------
    token : str
        Sensitive join bundle produced by ``bertrand cluster invite``.
    worker : bool
        Whether to force MicroK8s worker join semantics.
    microceph_ip : str | None
        Optional MicroCeph bind address for this host.
    timeout : float
        Maximum join and convergence budget in seconds.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    """
    if timeout <= 0:
        msg = "cluster join timeout must be non-negative"
        raise TimeoutError(msg)
    from bertrand.env.cli.external.init import (
        _converge_cluster_runtime,
        ensure_shared_runtime_installed,
    )

    bundle = _decode_bundle(token)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    await ensure_shared_runtime_installed(timeout=deadline - loop.time(), yes=False)
    await join_microceph_cluster(
        str(bundle["microceph"]),
        microceph_ip=microceph_ip,
        timeout=deadline - loop.time(),
    )
    await join_microk8s_cluster(
        str(bundle["microk8s"]),
        worker=worker or bool(bundle.get("worker")),
        timeout=deadline - loop.time(),
    )
    await ensure_microk8s_kubeconfig(timeout=deadline - loop.time())
    await link_kube_ceph(timeout=deadline - loop.time())
    with await Kube.host(timeout=deadline - loop.time()) as kube:
        await _converge_cluster_runtime(kube, timeout=deadline - loop.time())
    print("Bertrand cluster join complete.")


async def _bertrand_cluster_secret(args: argparse.Namespace) -> None:
    command = args.cluster_secret_command
    if command == "add":
        ref = shared_capability_ref(kind=args.kind, capability_id=args.id)
        await add_capability(ref, source=args.source, timeout=args.timeout)
        return
    if command == "rm":
        ref = shared_capability_ref(kind=args.kind, capability_id=args.id)
        await remove_capability(ref, timeout=args.timeout)
        return
    if command == "list":
        await list_capabilities(
            shared_scope_targets(),
            kind=args.kind,
            json_output=args.json,
            timeout=args.timeout,
        )
        return
    msg = f"unsupported cluster secret command: {command!r}"
    raise ValueError(msg)


async def _bertrand_cluster_device(args: argparse.Namespace) -> None:
    command = args.cluster_device_command
    if command == "list":
        await bertrand_cluster_device_list(
            node=args.node,
            capability_id=args.capability,
            json_output=args.json,
            timeout=args.timeout,
        )
        return
    msg = f"unsupported cluster device command: {command!r}"
    raise ValueError(msg)


async def bertrand_cluster_device_list(
    *,
    node: str | None,
    capability_id: str | None,
    json_output: bool,
    timeout: float,
) -> None:
    """Print managed DRA inventory across the cluster.

    Parameters
    ----------
    node : str | None
        Optional Bertrand host UUID filter.
    capability_id : str | None
        Optional device capability ID filter.
    json_output : bool
        Whether to emit machine-readable JSON.
    timeout : float
        Maximum Kubernetes request budget in seconds.
    """
    with await Kube.host(timeout=timeout) as kube:
        records = await list_device_inventory(
            kube,
            capability_id=capability_id,
            host_ids=None if node is None else (node,),
            timeout=timeout,
        )
        nodes = {
            item.host_id: item
            for item in await list_bertrand_nodes(kube, timeout=timeout)
        }
    payload = [
        _device_record_payload(record, node=nodes.get(record.host_id))
        for record in records
    ]
    if json_output:
        print(json.dumps(payload, indent=2, sort_keys=True))
        return
    if not records:
        print("no DRA devices")
        return
    for record in records:
        display = nodes.get(record.host_id)
        owner = (
            f"{record.host_id}"
            if display is None or not display.display_name
            else f"{display.display_name} ({record.host_id})"
        )
        print(
            f"{record.capability_id} {record.spec.device_name} "
            f"[{owner}; kube={record.node_name}] -> {record.cdi_selector}"
        )


async def _bertrand_cluster_network(args: argparse.Namespace) -> None:
    command = args.network_command
    if command == "status":
        await bertrand_cluster_network_status(json_output=args.json)
        return
    if command == "doctor":
        await bertrand_cluster_network_doctor(json_output=args.json)
        return
    if command == "lb":
        await _bertrand_cluster_network_lb(args)
        return
    if command == "dns":
        await _bertrand_cluster_network_dns(args)
        return
    msg = f"unsupported cluster network command: {command!r}"
    raise ValueError(msg)


async def bertrand_cluster_network_status(*, json_output: bool) -> None:
    """Print cluster networking status.

    Parameters
    ----------
    json_output : bool
        Whether to emit machine-readable JSON.
    """
    report = await _network_report()
    if json_output:
        print(json.dumps(report, indent=2, sort_keys=True))
        return
    _print_network_report(report)


async def bertrand_cluster_network_doctor(*, json_output: bool) -> None:
    """Print actionable cluster networking diagnostics.

    Parameters
    ----------
    json_output : bool
        Whether to emit machine-readable JSON.
    """
    report = await _network_report()
    issues = _network_issues(report)
    if json_output:
        print(
            json.dumps(
                {"ready": not issues, "issues": issues, "status": report},
                indent=2,
                sort_keys=True,
            )
        )
        return
    _print_network_report(report)
    print("doctor:")
    if not issues:
        print("  no networking issues detected")
        return
    for issue in issues:
        print(f"  - {issue}")


async def _bertrand_cluster_network_lb(args: argparse.Namespace) -> None:
    command = args.lb_command
    if command == "status":
        await bertrand_cluster_network_lb_status(json_output=args.json)
        return
    if command == "install":
        await bertrand_cluster_network_lb_install(timeout=args.timeout)
        return
    if command == "pool":
        await _bertrand_cluster_network_lb_pool(args)
        return
    if command == "l2":
        await _bertrand_cluster_network_lb_l2(args)
        return
    if command == "bgp":
        await _bertrand_cluster_network_lb_bgp(args)
        return
    msg = f"unsupported cluster network lb command: {command!r}"
    raise ValueError(msg)


async def bertrand_cluster_network_lb_status(*, json_output: bool) -> None:
    """Print MetalLB status.

    Parameters
    ----------
    json_output : bool
        Whether to emit machine-readable JSON.
    """
    with await Kube.host(timeout=INFINITY) as kube:
        status = await metallb_status(kube, timeout=INFINITY)
    if json_output:
        print(json.dumps(status, indent=2, sort_keys=True))
        return
    _print_metallb_status(status)


async def bertrand_cluster_network_lb_install(*, timeout: float) -> None:
    """Install and verify Bertrand-managed MetalLB.

    Parameters
    ----------
    timeout : float
        Maximum convergence budget in seconds.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    """
    if timeout <= 0:
        msg = "MetalLB install timeout must be non-negative"
        raise TimeoutError(msg)
    with await Kube.host(timeout=timeout) as kube:
        await ensure_metallb(kube, timeout=timeout)
    print("MetalLB installed and ready.")


async def _bertrand_cluster_network_lb_pool(args: argparse.Namespace) -> None:
    command = args.lb_pool_command
    if command != "upsert":
        msg = f"unsupported cluster network lb pool command: {command!r}"
        raise ValueError(msg)
    with await Kube.host(timeout=args.timeout) as kube:
        pool = await IPAddressPool.upsert(
            kube,
            name=args.name,
            addresses=_flatten(args.address),
            auto_assign=args.auto_assign,
            timeout=args.timeout,
        )
    print(f"MetalLB IPAddressPool {pool.name!r} converged.")


async def _bertrand_cluster_network_lb_l2(args: argparse.Namespace) -> None:
    command = args.lb_l2_command
    if command != "upsert":
        msg = f"unsupported cluster network lb l2 command: {command!r}"
        raise ValueError(msg)
    with await Kube.host(timeout=args.timeout) as kube:
        advertisement = await L2Advertisement.upsert(
            kube,
            name=args.name,
            pool=args.pool,
            interfaces=tuple(args.interface or ()),
            timeout=args.timeout,
        )
    print(f"MetalLB L2Advertisement {advertisement.name!r} converged.")


async def _bertrand_cluster_network_lb_bgp(args: argparse.Namespace) -> None:
    command = args.lb_bgp_command
    if command == "peer":
        await _bertrand_cluster_network_lb_bgp_peer(args)
        return
    if command == "advertise":
        await _bertrand_cluster_network_lb_bgp_advertise(args)
        return
    msg = f"unsupported cluster network lb bgp command: {command!r}"
    raise ValueError(msg)


async def _bertrand_cluster_network_lb_bgp_peer(
    args: argparse.Namespace,
) -> None:
    command = args.lb_bgp_peer_command
    if command != "upsert":
        msg = f"unsupported cluster network lb bgp peer command: {command!r}"
        raise ValueError(msg)
    with await Kube.host(timeout=args.timeout) as kube:
        peer = await BGPPeer.upsert(
            kube,
            name=args.name,
            peer_address=args.peer_address,
            peer_asn=args.peer_asn,
            local_asn=args.local_asn,
            peer_port=args.peer_port,
            source_address=args.source_address,
            password_secret=args.password_secret,
            timeout=args.timeout,
        )
    print(f"MetalLB BGPPeer {peer.name!r} converged.")


async def _bertrand_cluster_network_lb_bgp_advertise(
    args: argparse.Namespace,
) -> None:
    command = args.lb_bgp_advertise_command
    if command != "upsert":
        msg = f"unsupported cluster network lb bgp advertise command: {command!r}"
        raise ValueError(msg)
    with await Kube.host(timeout=args.timeout) as kube:
        advertisement = await BGPAdvertisement.upsert(
            kube,
            name=args.name,
            pool=args.pool,
            peers=tuple(args.peer or ()),
            local_pref=args.local_pref,
            communities=tuple(args.community or ()),
            timeout=args.timeout,
        )
    print(f"MetalLB BGPAdvertisement {advertisement.name!r} converged.")


async def _network_report() -> dict[str, object]:
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
        gateway = await _network_gateway_status(kube)
        cni = await inspect_cni(kube, timeout=INFINITY)
        load_balancer = await metallb_status(kube, timeout=INFINITY)
        routes = await _route_dns_status(
            kube,
            gateway_addresses=_object_tuple(gateway.get("addresses", ())),
        )
    return {
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
        "cni": cni.model_dump(),
        "gateway": gateway,
        "load_balancer": load_balancer,
        "routes": routes,
    }


def _print_network_report(report: Mapping[str, object]) -> None:
    profile = cast("Mapping[str, object]", report["profile"])
    dns = cast("Mapping[str, object]", profile["dns"])
    print(f"network profile: {BERTRAND_NAMESPACE}/{NETWORK_PROFILE_NAME}")
    print(f"  hash: {profile['hash']}")
    servers = _display_tuple(_object_tuple(dns["nameservers"]))
    search = _display_tuple(_object_tuple(dns["search_domains"]))
    options = _display_tuple(_object_tuple(dns["options"]))
    print(f"  buildkit dns servers: {servers}")
    print(f"  buildkit dns search: {search}")
    print(f"  buildkit dns options: {options}")
    cni = cast("Mapping[str, object]", report["cni"])
    print("cni:")
    print(f"  name: {cni['name']} ({cni['confidence']})")
    print(f"  network policy: {cni['network_policy']}")
    print(f"  pod cidrs: {_display_tuple(_object_tuple(cni['pod_cidrs']))}")
    print(f"  service cidrs: {_display_tuple(_object_tuple(cni['service_cidrs']))}")
    gateway = cast("Mapping[str, object]", report["gateway"])
    print("gateway:")
    print(f"  class accepted: {gateway['class_accepted']}")
    print(f"  gateway ready: {gateway['ready']}")
    print(f"  addresses: {_display_tuple(_object_tuple(gateway['addresses']))}")
    _print_metallb_status(cast("Mapping[str, object]", report["load_balancer"]))
    routes = cast("Mapping[str, object]", report["routes"])
    print("routes:")
    print(f"  count: {routes['count']}")
    unresolved = cast("Sequence[object]", routes["unresolved"])
    mismatched = cast("Sequence[object]", routes["mismatched"])
    if unresolved:
        print(f"  unresolved hosts: {', '.join(str(item) for item in unresolved)}")
    if mismatched:
        print(f"  mismatched hosts: {', '.join(str(item) for item in mismatched)}")
    buildkit = cast("Mapping[str, object]", report["buildkit"])
    print("buildkit:")
    print(f"  config hash: {buildkit['config_hash']}")
    print(f"  registry: {_ready(value=bool(buildkit['registry_ready']))}")
    print(f"  pool: {_ready(value=bool(buildkit['pool_ready']))}")
    failures = cast("Sequence[object]", buildkit["failures"])
    if failures:
        print("  failures:")
        for failure in failures:
            print(f"    - {failure}")


async def _network_gateway_status(kube: Kube) -> dict[str, object]:
    try:
        gateway_class = await GatewayClass.get(
            kube,
            name=BERTRAND_GATEWAY_CLASS,
            timeout=INFINITY,
        )
        gateway = await Gateway.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=BERTRAND_GATEWAY,
            timeout=INFINITY,
        )
    except OSError as err:
        return {
            "ready": False,
            "class_accepted": False,
            "addresses": [],
            "message": str(err),
        }
    addresses = gateway.addresses if gateway is not None else ()
    class_accepted = gateway_class is not None and gateway_class.accepted
    ready = bool(class_accepted and addresses)
    message = ""
    if gateway_class is None:
        message = "Bertrand GatewayClass is missing"
    elif not class_accepted:
        message = (
            gateway_class.acceptance_message or "Bertrand GatewayClass is not accepted"
        )
    elif gateway is None:
        message = "Bertrand Gateway is missing"
    elif not addresses:
        message = (
            "Bertrand Gateway has no external address; configure MetalLB with "
            "`bertrand cluster network lb ...`"
        )
    return {
        "ready": ready,
        "class_accepted": class_accepted,
        "addresses": list(addresses),
        "message": message,
    }


async def _route_dns_status(
    kube: Kube,
    *,
    gateway_addresses: tuple[str, ...],
) -> dict[str, object]:
    try:
        routes = await HTTPRoute.list(
            kube,
            namespace=BERTRAND_NAMESPACE,
            labels={HTTP_ROUTE_LABEL: HTTP_ROUTE_LABEL_VALUE},
            timeout=INFINITY,
        )
    except OSError as err:
        return {
            "ready": False,
            "count": 0,
            "items": [],
            "unresolved": [],
            "mismatched": [],
            "message": str(err),
        }
    items: list[dict[str, object]] = []
    unresolved: list[str] = []
    mismatched: list[str] = []
    gateway_set = {address.lower() for address in gateway_addresses}
    for route in routes:
        for host in route.hostnames:
            resolved = await _resolve_host(host)
            if not resolved:
                unresolved.append(host)
            elif gateway_set and not (
                gateway_set & {item.lower() for item in resolved}
            ):
                mismatched.append(host)
            items.append(
                {
                    "route": route.name,
                    "host": host,
                    "resolved": list(resolved),
                    "matches_gateway": bool(
                        gateway_set & {item.lower() for item in resolved}
                    ),
                }
            )
    messages = []
    if unresolved:
        messages.append(f"unresolved route hosts: {unresolved}")
    if mismatched:
        messages.append(
            f"route hosts do not resolve to Gateway addresses: {mismatched}"
        )
    return {
        "ready": not unresolved and not mismatched,
        "count": len(routes),
        "items": items,
        "unresolved": unresolved,
        "mismatched": mismatched,
        "message": "; ".join(messages),
    }


async def _resolve_host(host: str) -> tuple[str, ...]:
    try:
        infos = await asyncio.wait_for(
            asyncio.to_thread(socket.getaddrinfo, host, 80, type=socket.SOCK_STREAM),
            timeout=2.0,
        )
    except (OSError, TimeoutError):
        return ()
    out: list[str] = []
    for info in infos:
        sockaddr = info[4]
        if not sockaddr:
            continue
        value = str(sockaddr[0]).strip()
        if value and value not in out:
            out.append(value)
    return tuple(out)


def _print_metallb_status(status: Mapping[str, object]) -> None:
    print("load balancer:")
    print(f"  namespace: {status['namespace']}")
    print(f"  managed: {status['managed']}")
    print(f"  ready: {_ready(value=bool(status['ready']))}")
    print(f"  controller: {_ready(value=bool(status['controller_ready']))}")
    print(f"  speaker: {_ready(value=bool(status['speaker_ready']))}")
    pools = cast("Sequence[Mapping[str, object]]", status["pools"])
    if pools:
        print("  pools:")
        for pool in pools:
            addresses = ", ".join(_object_tuple(pool["addresses"]))
            print(
                f"    - {pool['name']}: {addresses} "
                f"(available IPv4: {pool['available_ipv4']})"
            )
    bgp_peers = cast("Sequence[Mapping[str, object]]", status["bgp_peers"])
    if bgp_peers:
        print("  bgp peers:")
        for peer in bgp_peers:
            print(f"    - {peer['name']}: {peer['peer_address']}")
    message = str(status.get("message") or "")
    if message:
        print(f"  message: {message}")


def _network_issues(report: Mapping[str, object]) -> list[str]:
    issues: list[str] = []
    for section in ("cni", "gateway", "load_balancer", "routes"):
        payload = report.get(section)
        if not isinstance(payload, dict):
            continue
        payload_map = cast("dict[str, object]", payload)
        message = str(payload_map.get("message") or "")
        if message:
            issues.append(f"{section}: {message}")
    buildkit = report.get("buildkit")
    if isinstance(buildkit, dict):
        buildkit_map = cast("dict[str, object]", buildkit)
        failures = buildkit_map.get("failures", ())
        if isinstance(failures, Sequence) and failures:
            issues.append(f"buildkit: {', '.join(str(item) for item in failures)}")
    return issues


def _ready(*, value: bool) -> str:
    return "ready" if value else "not ready"


def _device_record_payload(
    record: BertrandDeviceRecord,
    *,
    node: BertrandNodeRecord | None = None,
) -> dict[str, object]:
    return {
        "name": record.name,
        "capability_id": record.capability_id,
        "host_id": record.host_id,
        "display_name": "" if node is None else node.display_name,
        "node_name": record.node_name,
        "device_name": record.spec.device_name,
        "cdi_selector": record.cdi_selector,
        "attributes": dict(record.spec.attributes),
    }


async def _bertrand_cluster_network_dns(args: argparse.Namespace) -> None:
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
    msg = f"unsupported cluster network dns command: {command!r}"
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


def _encode_bundle(payload: Mapping[str, object]) -> str:
    encoded = json.dumps(payload, separators=(",", ":"), sort_keys=True).encode()
    return base64.urlsafe_b64encode(encoded).decode().rstrip("=")


def _decode_bundle(token: str) -> dict[str, object]:
    padded = token.strip() + "=" * (-len(token.strip()) % 4)
    try:
        payload = json.loads(base64.urlsafe_b64decode(padded).decode())
    except (TypeError, ValueError) as err:
        msg = "invalid Bertrand cluster join token"
        raise ValueError(msg) from err
    if not isinstance(payload, dict) or payload.get("version") != JOIN_BUNDLE_VERSION:
        msg = "unsupported Bertrand cluster join token"
        raise ValueError(msg)
    for key in ("microk8s", "microceph"):
        if not isinstance(payload.get(key), str) or not payload[key]:
            msg = f"Bertrand cluster join token is missing {key!r}"
            raise ValueError(msg)
    return payload


async def _probe_bool(fn: Callable[[], Awaitable[bool]]) -> dict[str, object]:
    try:
        return {"ready": await fn(), "message": ""}
    except (OSError, TimeoutError, RuntimeError, ValueError) as err:
        return {"ready": False, "message": str(err)}


async def _probe_host(
    fn: Callable[[], Awaitable[dict[str, object]]],
) -> dict[str, object]:
    try:
        return await fn()
    except (OSError, TimeoutError, RuntimeError, ValueError) as err:
        return {"ready": False, "message": str(err)}


async def _probe_kube(
    fn: Callable[[Kube], Awaitable[dict[str, object]]],
) -> dict[str, object]:
    try:
        with await Kube.host(timeout=INFINITY) as kube:
            return await fn(kube)
    except (OSError, TimeoutError, RuntimeError, ValueError) as err:
        return {"ready": False, "message": str(err)}


async def _namespace_status(kube: Kube) -> dict[str, object]:
    namespace = await Namespace.get(kube, name=BERTRAND_NAMESPACE, timeout=INFINITY)
    return {
        "ready": namespace is not None,
        "message": "" if namespace is not None else "Bertrand namespace is missing",
    }


async def _buildkit_status(kube: Kube) -> dict[str, object]:
    config_hash = await IMAGES.current_buildkit_config_hash(kube, timeout=INFINITY)
    registry = await IMAGES.status(kube, timeout=INFINITY)
    buildkit = await BUILDKIT_POOL.status(
        kube,
        timeout=INFINITY,
        config_hash=config_hash,
    )
    failures = [*registry.failures, *buildkit.failures]
    return {
        "ready": registry.ready and buildkit.ready,
        "config_hash": config_hash,
        "registry_ready": registry.ready,
        "pool_ready": buildkit.ready,
        "failures": failures,
        "message": "; ".join(failures),
    }


async def _gateway_status(kube: Kube) -> dict[str, object]:
    from bertrand.env.kube.deployment import Deployment

    deployment = await Deployment.get(
        kube,
        namespace=ENVOY_GATEWAY_NAMESPACE,
        name=ENVOY_GATEWAY_DEPLOYMENT,
        timeout=INFINITY,
    )
    ready = deployment is not None and deployment.has_available_replicas()
    return {
        "ready": ready,
        "message": "" if ready else "Envoy Gateway Deployment is not Available",
    }


async def _ceph_csi_status() -> dict[str, object]:
    result = await run(
        ["microk8s", "kubectl", "get", "storageclass", "-o", "json"],
        check=False,
        capture_output=True,
        timeout=INFINITY,
    )
    if result.returncode != 0:
        return {"ready": False, "message": str(result)}
    payload = json.loads(result.stdout)
    items = payload.get("items", []) if isinstance(payload, dict) else []
    names: list[str] = []
    for item in items if isinstance(items, list) else []:
        if not isinstance(item, dict):
            continue
        provisioner = str(item.get("provisioner") or "").lower()
        metadata = item.get("metadata")
        name = metadata.get("name") if isinstance(metadata, dict) else None
        if "csi.ceph.com" in provisioner and isinstance(name, str):
            names.append(name)
    return {
        "ready": bool(names),
        "storage_classes": sorted(names),
        "message": "" if names else "no Ceph CSI StorageClass discovered",
    }


async def _storage_status(kube: Kube) -> dict[str, object]:
    from bertrand.env.kube.ceph.capacity import read_storage_policy

    policy = await read_storage_policy(kube, timeout=INFINITY)
    status = policy.status
    ready = status is not None and not status.last_error
    return {
        "ready": ready,
        "message": "" if ready else (status.last_error if status else "missing status"),
        "status": status.model_dump(mode="json") if status is not None else None,
    }


async def _dev_status(kube: Kube) -> dict[str, object]:
    crd = await CustomResourceDefinition.get(
        kube,
        name=f"{CODE_OPEN_PLURAL}.{DEV_GROUP}",
        timeout=INFINITY,
    )
    ready = crd is not None and crd.is_established
    return {
        "ready": ready,
        "message": "" if ready else "CodeOpenRequest CRD is not established",
    }


def _status_line(value: object) -> tuple[str, str]:
    if not isinstance(value, dict):
        return ("unknown", "")
    status = cast("dict[str, object]", value)
    ready = bool(status.get("ready"))
    detail = str(status.get("message") or "")
    return ("ready" if ready else "not ready", detail)


def _display_tuple(values: tuple[str, ...]) -> str:
    return ", ".join(values) if values else "default"


def _object_tuple(values: object) -> tuple[str, ...]:
    if not isinstance(values, Sequence) or isinstance(values, str):
        return ()
    return tuple(str(value) for value in values if str(value))


def _print_dns_profile(profile: NetworkProfile) -> None:
    print("BuildKit/container DNS profile updated")
    print(f"  dns servers: {_display_tuple(profile.nameservers)}")
    print(f"  dns search: {_display_tuple(profile.search_domains)}")
    print(f"  dns options: {_display_tuple(profile.options)}")
