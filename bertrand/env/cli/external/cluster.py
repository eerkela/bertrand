"""External CLI endpoints for distributed Bertrand cluster operations."""

from __future__ import annotations

import asyncio
import base64
import json
import socket
from collections.abc import Mapping, Sequence
from datetime import UTC, datetime
from typing import TYPE_CHECKING, cast

from bertrand.env.cli.external._runtime import emit_json
from bertrand.env.cli.external._storage import (
    print_cluster_storage_doctor,
    print_cluster_storage_status,
    storage_cli_snapshot,
)
from bertrand.env.cli.external.init import (
    _converge_host_cluster_runtime,
    ensure_shared_runtime_installed,
)
from bertrand.env.git import BERTRAND_NAMESPACE, INFINITY, Deadline
from bertrand.env.kube.api.bootstrap import (
    join_microk8s_cluster,
    microk8s_cluster_ready,
    microk8s_join_token,
)
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.build.daemon import (
    buildkit_pool_status,
    ensure_buildkit_pool,
)
from bertrand.env.kube.build.repository import (
    current_buildkit_config_hash,
    ensure_image_repository,
    image_repository_status,
)
from bertrand.env.kube.capability.device import list_device_inventory
from bertrand.env.kube.ceph.bootstrap import rook_ceph_ready
from bertrand.env.kube.ceph.capacity import read_storage_state
from bertrand.env.kube.crd import CustomResourceDefinition
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.dev.mailbox import CODE_OPEN_PLURAL, DEV_GROUP
from bertrand.env.kube.namespace import Namespace
from bertrand.env.kube.network.bootstrap import (
    ENVOY_GATEWAY_DEPLOYMENT,
    ENVOY_GATEWAY_NAMESPACE,
)
from bertrand.env.kube.network.cni import inspect_cni
from bertrand.env.kube.network.gateway import (
    BERTRAND_GATEWAY,
    BERTRAND_GATEWAY_CLASS,
    GATEWAY_CLASS_RESOURCE,
    GATEWAY_RESOURCE,
    HTTP_ROUTE_LABEL,
    HTTP_ROUTE_LABEL_VALUE,
    HTTP_ROUTE_RESOURCE,
    gateway_addresses,
    gateway_class_acceptance_message,
    gateway_class_accepted,
    http_route_hostnames,
)
from bertrand.env.kube.network.load_balancer import (
    ensure_metallb,
    metallb_status,
    upsert_bgp_advertisement,
    upsert_bgp_peer,
    upsert_ip_address_pool,
    upsert_l2_advertisement,
)
from bertrand.env.kube.network.profile import NETWORK_PROFILE_NAME, NetworkProfile
from bertrand.env.kube.node_identity import (
    BertrandNodeRecord,
    list_bertrand_nodes,
)
from bertrand.env.kube.volume import StorageClass

if TYPE_CHECKING:
    from collections.abc import Awaitable, Callable

    from bertrand.env.kube.capability.device import BertrandDeviceRecord

JOIN_BUNDLE_VERSION = 1


def _flatten(values: Sequence[Sequence[str]] | None) -> tuple[str, ...]:
    if values is None:
        return ()
    return tuple(item for group in values for item in group)


async def bertrand_cluster_status(*, json_output: bool) -> None:
    """Print shared Bertrand cluster status.

    Parameters
    ----------
    json_output : bool
        Whether to emit machine-readable JSON.
    """
    status: dict[str, object] = {
        "microk8s": await _probe_bool(lambda: microk8s_cluster_ready(timeout=INFINITY)),
    }
    kube_checks = (
        ("rook_ceph", _rook_ceph_status),
        ("namespace", _namespace_status),
        ("buildkit", _buildkit_status),
        ("gateway", _gateway_status),
        ("ceph_csi", _ceph_csi_status),
        ("storage", _storage_status),
        ("dev", _dev_status),
    )
    try:
        with await Kube.host(timeout=INFINITY) as kube:
            for name, probe in kube_checks:
                try:
                    status[name] = await probe(kube)
                except (OSError, TimeoutError, RuntimeError, ValueError) as err:
                    status[name] = {"ready": False, "message": str(err)}
    except (OSError, TimeoutError, RuntimeError, ValueError) as err:
        failure = {"ready": False, "message": str(err)}
        for name, _probe in kube_checks:
            status[name] = dict(failure)
    if json_output:
        emit_json(status)
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

    """
    deadline = Deadline.from_timeout(
        timeout,
        message="cluster invite timeout must be positive",
    )
    microk8s = await microk8s_join_token(
        worker=worker,
        timeout=deadline.remaining(),
    )
    node_name = (
        name or f"bertrand-node-{datetime.now(UTC).strftime('%Y%m%d%H%M%S')}"
    ).strip()
    payload = {
        "version": JOIN_BUNDLE_VERSION,
        "created_at": datetime.now(UTC).isoformat(),
        "node_name": node_name,
        "worker": worker,
        "microk8s": microk8s,
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
    timeout: float,
) -> None:
    """Join this host to an existing Bertrand shared runtime cluster.

    Parameters
    ----------
    token : str
        Sensitive join bundle produced by ``bertrand cluster invite``.
    worker : bool
        Whether to force MicroK8s worker join semantics.
    timeout : float
        Maximum join and convergence budget in seconds.

    """
    bundle = _decode_bundle(token)
    deadline = Deadline.from_timeout(
        timeout,
        message="cluster join timeout must be positive",
    )
    await ensure_shared_runtime_installed(timeout=deadline.remaining(), yes=False)
    await join_microk8s_cluster(
        str(bundle["microk8s"]),
        worker=worker or bool(bundle.get("worker")),
        timeout=deadline.remaining(),
    )
    await _converge_host_cluster_runtime(deadline, start=False)
    print("Bertrand cluster join complete.")


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
        _cluster_device_payload(
            record,
            owner=nodes.get(record.host_id),
        )
        for record in records
    ]
    if json_output:
        emit_json(payload)
        return
    if not records:
        print("no DRA devices")
        return
    for record in records:
        print(_cluster_device_line(record, owner=nodes.get(record.host_id)))


async def bertrand_cluster_storage_status(
    *,
    json_output: bool,
    doctor: bool = False,
) -> None:
    """Print cluster-wide Rook/Ceph storage status and diagnostics.

    Parameters
    ----------
    json_output : bool
        Whether to emit machine-readable JSON.
    doctor : bool
        Whether to print diagnostic guidance in addition to status.
    """
    with await Kube.host(timeout=INFINITY) as kube:
        snapshot = await storage_cli_snapshot(kube)
    payload = snapshot.status_payload()
    if json_output:
        emit_json(payload)
        return
    print_cluster_storage_status(snapshot)
    if doctor:
        print_cluster_storage_doctor(snapshot)


async def bertrand_cluster_storage_doctor(*, json_output: bool) -> None:
    """Print cluster-wide Rook/Ceph storage status and diagnostics."""
    await bertrand_cluster_storage_status(json_output=json_output, doctor=True)


async def bertrand_cluster_network_status(*, json_output: bool) -> None:
    """Print cluster networking status.

    Parameters
    ----------
    json_output : bool
        Whether to emit machine-readable JSON.
    """
    report = await _network_report()
    if json_output:
        emit_json(report)
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
        emit_json({"ready": not issues, "issues": issues, "status": report})
        return
    _print_network_report(report)
    print("doctor:")
    if not issues:
        print("  no networking issues detected")
        return
    for issue in issues:
        print(f"  - {issue}")


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
        emit_json(status)
        return
    _print_metallb_status(status)


async def bertrand_cluster_network_lb_install(*, timeout: float) -> None:
    """Install and verify Bertrand-managed MetalLB.

    Parameters
    ----------
    timeout : float
        Maximum convergence budget in seconds.

    """
    deadline = Deadline.from_timeout(
        timeout,
        message="MetalLB install timeout must be positive",
    )
    with await Kube.host(timeout=deadline.remaining()) as kube:
        await ensure_metallb(kube, timeout=deadline.remaining())
    print("MetalLB installed and ready.")


async def bertrand_cluster_network_lb_pool_upsert(
    *,
    name: str,
    address: Sequence[Sequence[str]] | None,
    auto_assign: bool,
    timeout: float,
) -> None:
    """Create or patch a Bertrand-managed MetalLB IPAddressPool."""
    with await Kube.host(timeout=timeout) as kube:
        pool = await upsert_ip_address_pool(
            kube,
            name=name,
            addresses=_flatten(address),
            auto_assign=auto_assign,
            timeout=timeout,
        )
    print(f"MetalLB IPAddressPool {pool.name!r} converged.")


async def bertrand_cluster_network_lb_l2_upsert(
    *,
    name: str,
    pool: str,
    interface: Sequence[str],
    timeout: float,
) -> None:
    """Create or patch a Bertrand-managed L2Advertisement."""
    with await Kube.host(timeout=timeout) as kube:
        advertisement = await upsert_l2_advertisement(
            kube,
            name=name,
            pool=pool,
            interfaces=tuple(interface or ()),
            timeout=timeout,
        )
    print(f"MetalLB L2Advertisement {advertisement.name!r} converged.")


async def bertrand_cluster_network_lb_bgp_peer_upsert(
    *,
    name: str,
    peer_address: str,
    peer_asn: int,
    local_asn: int,
    peer_port: int | None,
    source_address: str | None,
    password_secret: str | None,
    timeout: float,
) -> None:
    """Create or patch a Bertrand-managed BGPPeer."""
    with await Kube.host(timeout=timeout) as kube:
        peer = await upsert_bgp_peer(
            kube,
            name=name,
            peer_address=peer_address,
            peer_asn=peer_asn,
            local_asn=local_asn,
            peer_port=peer_port,
            source_address=source_address,
            password_secret=password_secret,
            timeout=timeout,
        )
    print(f"MetalLB BGPPeer {peer.name!r} converged.")


async def bertrand_cluster_network_lb_bgp_advertise_upsert(
    *,
    name: str,
    pool: str,
    peer: Sequence[str],
    local_pref: int | None,
    community: Sequence[str],
    timeout: float,
) -> None:
    """Create or patch a Bertrand-managed BGPAdvertisement."""
    with await Kube.host(timeout=timeout) as kube:
        advertisement = await upsert_bgp_advertisement(
            kube,
            name=name,
            pool=pool,
            peers=tuple(peer or ()),
            local_pref=local_pref,
            communities=tuple(community or ()),
            timeout=timeout,
        )
    print(f"MetalLB BGPAdvertisement {advertisement.name!r} converged.")


async def _network_report() -> dict[str, object]:
    with await Kube.host(timeout=INFINITY) as kube:
        (
            profile,
            config_hash,
            registry,
            gateway,
            cni,
            load_balancer,
        ) = await asyncio.gather(
            NetworkProfile.get(kube, timeout=INFINITY),
            current_buildkit_config_hash(kube, timeout=INFINITY),
            image_repository_status(kube, timeout=INFINITY),
            _network_gateway_status(kube),
            inspect_cni(kube, timeout=INFINITY),
            metallb_status(kube, timeout=INFINITY),
        )
        buildkit, routes = await asyncio.gather(
            buildkit_pool_status(
                kube,
                timeout=INFINITY,
                config_hash=config_hash,
            ),
            _route_dns_status(
                kube,
                gateway_addresses=_object_tuple(gateway.get("addresses", ())),
            ),
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
        "cni": cni,
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
        gateway_class, gateway = await asyncio.gather(
            GATEWAY_CLASS_RESOURCE.get(
                kube,
                name=BERTRAND_GATEWAY_CLASS,
                timeout=INFINITY,
            ),
            GATEWAY_RESOURCE.get(
                kube,
                namespace=BERTRAND_NAMESPACE,
                name=BERTRAND_GATEWAY,
                timeout=INFINITY,
            ),
        )
    except OSError as err:
        return {
            "ready": False,
            "class_accepted": False,
            "addresses": [],
            "message": str(err),
        }
    addresses = gateway_addresses(gateway) if gateway is not None else ()
    class_accepted = gateway_class is not None and gateway_class_accepted(gateway_class)
    ready = bool(class_accepted and addresses)
    message = ""
    if gateway_class is None:
        message = "Bertrand GatewayClass is missing"
    elif not class_accepted:
        message = (
            gateway_class_acceptance_message(gateway_class)
            or "Bertrand GatewayClass is not accepted"
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
        routes = await HTTP_ROUTE_RESOURCE.list(
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
    route_hosts = tuple(
        (route.name, host) for route in routes for host in http_route_hostnames(route)
    )
    resolved_hosts = await asyncio.gather(
        *(_resolve_host(host) for _route, host in route_hosts)
    )
    for (route_name, host), resolved in zip(
        route_hosts,
        resolved_hosts,
        strict=True,
    ):
        resolved_set = {item.lower() for item in resolved}
        if not resolved:
            unresolved.append(host)
        elif gateway_set and not (gateway_set & resolved_set):
            mismatched.append(host)
        items.append(
            {
                "route": route_name,
                "host": host,
                "resolved": list(resolved),
                "matches_gateway": bool(gateway_set & resolved_set),
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


async def bertrand_cluster_network_dns_set(
    *,
    server: Sequence[Sequence[str]] | None,
    search: Sequence[Sequence[str]] | None,
    option: Sequence[Sequence[str]] | None,
    timeout: float,
) -> None:
    """Replace BuildKit/container DNS overrides and roll builders."""
    profile = NetworkProfile(
        nameservers=_flatten(server),
        search_domains=_flatten(search),
        options=_flatten(option),
    )
    await _apply_network_profile(profile, timeout=timeout)
    _print_dns_profile(profile)


async def bertrand_cluster_network_dns_clear(*, timeout: float) -> None:
    """Clear BuildKit/container DNS overrides and roll builders."""
    cleared = NetworkProfile()
    await _apply_network_profile(cleared, timeout=timeout)
    _print_dns_profile(cleared)


async def _apply_network_profile(profile: NetworkProfile, *, timeout: float) -> None:
    deadline = Deadline.from_timeout(
        timeout,
        message="network convergence timeout must be positive",
    )
    with await Kube.host(timeout=deadline.remaining()) as kube:
        await Namespace.upsert(
            kube,
            name=BERTRAND_NAMESPACE,
            timeout=deadline.remaining(),
        )
        await profile.upsert(kube, timeout=deadline.remaining())
        await ensure_image_repository(kube, timeout=deadline.remaining())
        config_hash = await current_buildkit_config_hash(
            kube,
            timeout=deadline.remaining(),
        )
        await ensure_buildkit_pool(
            kube,
            timeout=deadline.remaining(),
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
    for key in ("microk8s",):
        if not isinstance(payload.get(key), str) or not payload[key]:
            msg = f"Bertrand cluster join token is missing {key!r}"
            raise ValueError(msg)
    return payload


async def _probe_bool(fn: Callable[[], Awaitable[bool]]) -> dict[str, object]:
    try:
        return {"ready": await fn(), "message": ""}
    except (OSError, TimeoutError, RuntimeError, ValueError) as err:
        return {"ready": False, "message": str(err)}


async def _namespace_status(kube: Kube) -> dict[str, object]:
    namespace = await Namespace.get(kube, name=BERTRAND_NAMESPACE, timeout=INFINITY)
    return {
        "ready": namespace is not None,
        "message": "" if namespace is not None else "Bertrand namespace is missing",
    }


async def _buildkit_status(kube: Kube) -> dict[str, object]:
    config_hash = await current_buildkit_config_hash(kube, timeout=INFINITY)
    registry = await image_repository_status(kube, timeout=INFINITY)
    buildkit = await buildkit_pool_status(
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


async def _rook_ceph_status(kube: Kube) -> dict[str, object]:
    ready = await rook_ceph_ready(kube, timeout=INFINITY)
    return {
        "ready": ready,
        "message": "" if ready else "Rook Ceph substrate is not ready",
    }


async def _ceph_csi_status(kube: Kube) -> dict[str, object]:
    classes = await StorageClass.list(kube, timeout=INFINITY)
    names = [storage.name for storage in classes if storage.is_cephfs]
    return {
        "ready": bool(names),
        "storage_classes": sorted(names),
        "message": "" if names else "no Ceph CSI StorageClass discovered",
    }


async def _storage_status(kube: Kube) -> dict[str, object]:
    storage = await read_storage_state(kube, timeout=INFINITY)
    status = storage.policy_status
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


def _cluster_device_payload(
    record: BertrandDeviceRecord,
    *,
    owner: BertrandNodeRecord | None,
) -> dict[str, object]:
    return {
        "name": record.name,
        "capability_id": record.capability_id,
        "host_id": record.host_id,
        "node_name": record.node_name,
        "device_name": record.spec.device_name,
        "cdi_selector": record.cdi_selector,
        "attributes": dict(record.spec.attributes),
        "display_name": "" if owner is None else owner.display_name,
    }


def _cluster_device_line(
    record: BertrandDeviceRecord,
    *,
    owner: BertrandNodeRecord | None,
) -> str:
    if owner is None or not owner.display_name:
        location = f"{record.host_id}; kube={record.node_name}"
    else:
        location = f"{owner.display_name} ({record.host_id}); kube={record.node_name}"
    return (
        f"{record.capability_id} {record.spec.device_name} "
        f"[{location}] -> {record.cdi_selector}"
    )


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
