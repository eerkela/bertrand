"""Native Kubernetes networking helpers for Bertrand workloads."""

from __future__ import annotations

import asyncio
import hashlib
from typing import TYPE_CHECKING, Protocol, cast

from bertrand.env.git import BERTRAND_NAMESPACE
from bertrand.env.kube.api.spec import ServicePortSpec
from bertrand.env.kube.gateway import HTTPRoute
from bertrand.env.kube.network.gateway import (
    HTTP_ROUTE_LABEL,
    HTTP_ROUTE_LABEL_VALUE,
    HTTP_ROUTE_LABELS,
    bertrand_gateway_parent_refs,
    ensure_bertrand_gateway,
    gateway_api_crd_missing,
)
from bertrand.env.kube.network_policy import NetworkPolicy
from bertrand.env.kube.service import Service
from bertrand.env.kube.workload.base import (
    WORKLOAD_ID_LABEL,
    WORKLOAD_LABEL,
    WORKLOAD_LABEL_VALUE,
    WorkloadIdentity,
    WorkloadPod,
)

if TYPE_CHECKING:
    from collections.abc import Sequence

    from bertrand.env.kube.api.client import Kube
    from bertrand.env.kube.api.spec import PortProtocol

_SERVICE_PROTOCOLS = frozenset({"TCP", "UDP", "SCTP"})
_HTTP_ROUTE_HASH_CHARS = 12
_MAX_KUBE_NAME_CHARS = 63


class _RouteConfig(Protocol):
    """Structural subset of Bertrand HTTPRoute config used by renderers."""

    @property
    def host(self) -> str: ...

    @property
    def port(self) -> str: ...

    @property
    def path(self) -> str: ...


class _NetworkConfig(Protocol):
    """Structural subset of Bertrand network config used by renderers."""

    @property
    def policy(self) -> str: ...

    @property
    def routes(self) -> Sequence[_RouteConfig]: ...


class _WorkloadNetworkConfig(Protocol):
    """Structural subset of Bertrand workload config used by renderers."""

    @property
    def network(self) -> _NetworkConfig: ...


async def ensure_workload_service(
    kube: Kube,
    *,
    workload: WorkloadPod,
    timeout: float,
) -> Service | None:
    """Converge this workload's canonical internal Kubernetes Service.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    workload : WorkloadPod
        Workload pod intent whose declared container ports define the Service shape.
    timeout : float
        Maximum convergence budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    Service | None
        Converged ClusterIP Service, or ``None`` when the workload declares no ports
        and any stale managed Service has been removed.

    Raises
    ------
    TimeoutError
        If convergence cannot start before `timeout` expires.
    """
    if timeout <= 0:
        msg = "workload Service convergence timeout must be non-negative"
        raise TimeoutError(msg)

    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    identity = workload.identity
    ports = workload_service_ports(workload)
    if not ports:
        await delete_workload_service(
            kube,
            identity=identity,
            timeout=deadline - loop.time(),
        )
        return None

    service = await Service.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=identity.name,
        timeout=deadline - loop.time(),
    )
    _assert_managed_service(service, identity=identity)
    return await Service.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=identity.name,
        selector=identity.selector,
        ports=ports,
        labels=identity.labels,
        service_type="ClusterIP",
        timeout=deadline - loop.time(),
    )


async def delete_workload_service(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    timeout: float,
) -> None:
    """Delete this workload's managed Service, if present.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    identity : WorkloadIdentity
        Stable workload identity used to locate and validate the Service.
    timeout : float
        Maximum deletion budget in seconds. If infinite, wait indefinitely.

    Raises
    ------
    TimeoutError
        If deletion cannot start before `timeout` expires.
    """
    if timeout <= 0:
        msg = "workload Service deletion timeout must be non-negative"
        raise TimeoutError(msg)

    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    service = await Service.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=identity.name,
        timeout=deadline - loop.time(),
    )
    _assert_managed_service(service, identity=identity)
    if service is not None:
        await service.delete(kube, timeout=deadline - loop.time())


async def ensure_workload_network_policy(
    kube: Kube,
    *,
    config: _WorkloadNetworkConfig,
    workload: WorkloadPod,
    timeout: float,
) -> NetworkPolicy | None:
    """Converge this workload's Kubernetes NetworkPolicy intent.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    config : _WorkloadNetworkConfig
        Validated Bertrand workload config whose network policy selects the
        NetworkPolicy shape.
    workload : WorkloadPod
        Workload pod intent whose stable selector identifies the protected Pods.
    timeout : float
        Maximum convergence budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    NetworkPolicy | None
        Converged ingress-only NetworkPolicy, or ``None`` when policy is ``"open"``
        and any stale managed NetworkPolicy has been removed.

    Raises
    ------
    TimeoutError
        If convergence cannot start before `timeout` expires.
    ValueError
        If the validated config contains an unsupported policy value.
    """
    if timeout <= 0:
        msg = "workload NetworkPolicy convergence timeout must be positive"
        raise TimeoutError(msg)

    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    identity = workload.identity
    policy = config.network.policy.strip().lower()
    if policy == "open":
        await delete_workload_network_policy(
            kube,
            identity=identity,
            timeout=deadline - loop.time(),
        )
        return None
    if policy != "isolated":
        msg = f"unsupported workload network policy: {config.network.policy!r}"
        raise ValueError(msg)

    network_policy = await NetworkPolicy.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=identity.name,
        timeout=deadline - loop.time(),
    )
    _assert_managed_network_policy(network_policy, identity=identity)
    return await NetworkPolicy.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=identity.name,
        pod_selector=identity.selector,
        labels=identity.labels,
        policy_types=("Ingress",),
        ingress=(),
        timeout=deadline - loop.time(),
    )


async def delete_workload_network_policy(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    timeout: float,
) -> None:
    """Delete this workload's managed NetworkPolicy, if present.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    identity : WorkloadIdentity
        Stable workload identity used to locate and validate the NetworkPolicy.
    timeout : float
        Maximum deletion budget in seconds. If infinite, wait indefinitely.

    Raises
    ------
    TimeoutError
        If deletion cannot start before `timeout` expires.
    """
    if timeout <= 0:
        msg = "workload NetworkPolicy deletion timeout must be positive"
        raise TimeoutError(msg)

    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    network_policy = await NetworkPolicy.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=identity.name,
        timeout=deadline - loop.time(),
    )
    _assert_managed_network_policy(network_policy, identity=identity)
    if network_policy is not None:
        await network_policy.delete(kube, timeout=deadline - loop.time())


async def ensure_workload_http_routes(
    kube: Kube,
    *,
    config: _WorkloadNetworkConfig,
    workload: WorkloadPod,
    timeout: float,
) -> tuple[HTTPRoute, ...]:
    """Converge this workload's managed Gateway API HTTPRoutes.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    config : _WorkloadNetworkConfig
        Validated Bertrand workload config whose route intents select the
        HTTPRoute shape.
    workload : WorkloadPod
        Workload pod intent whose canonical Service receives route traffic.
    timeout : float
        Maximum convergence budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    tuple[HTTPRoute, ...]
        Managed HTTPRoutes matching the workload's configured external routes.

    Raises
    ------
    TimeoutError
        If convergence cannot start before `timeout` expires.
    OSError
        If Gateway API HTTPRoute resources are unavailable.
    ValueError
        If a route references an unknown named Service port.
    """
    if timeout <= 0:
        msg = "workload HTTPRoute convergence timeout must be positive"
        raise TimeoutError(msg)

    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    identity = workload.identity
    routes = tuple(config.network.routes)
    if not routes:
        await delete_workload_http_routes(
            kube,
            identity=identity,
            timeout=deadline - loop.time(),
            require_gateway_api=False,
        )
        return ()

    ports = {port.name: port.port for port in workload_service_ports(workload)}
    desired_names: set[str] = set()
    desired: list[tuple[str, _RouteConfig, int]] = []
    for route in routes:
        service_port = ports.get(route.port)
        if service_port is None:
            msg = (
                f"workload HTTPRoute for host {route.host!r} references unknown "
                f"Service port {route.port!r}"
            )
            raise ValueError(msg)
        name = workload_http_route_name(identity, route)
        if name in desired_names:
            msg = f"duplicate workload HTTPRoute resource name: {name!r}"
            raise ValueError(msg)
        desired_names.add(name)
        desired.append((name, route, service_port))

    await ensure_bertrand_gateway(kube, timeout=deadline - loop.time())
    out: list[HTTPRoute] = []
    for name, route, service_port in desired:
        current = await HTTPRoute.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=name,
            timeout=deadline - loop.time(),
        )
        _assert_managed_http_route(current, identity=identity)
        try:
            route_obj = await HTTPRoute.upsert(
                kube,
                namespace=BERTRAND_NAMESPACE,
                name=name,
                parent_refs=bertrand_gateway_parent_refs(),
                hostnames=(route.host,),
                rules=(
                    _http_route_rule(
                        route=route,
                        identity=identity,
                        port=service_port,
                    ),
                ),
                labels=_http_route_labels(identity),
                timeout=deadline - loop.time(),
            )
        except OSError as err:
            if gateway_api_crd_missing(err):
                msg = (
                    "Gateway API HTTPRoute CRD is missing while publishing Bertrand "
                    "routes. Run `bertrand init` to install Envoy Gateway and its "
                    "Gateway API CRDs, or install Envoy Gateway manually before "
                    "publishing workload routes."
                )
                raise OSError(msg) from err
            raise
        out.append(route_obj)

    for stale in await _list_workload_http_routes(
        kube,
        identity=identity,
        timeout=deadline - loop.time(),
        require_gateway_api=True,
    ):
        if stale.name not in desired_names:
            _assert_managed_http_route(stale, identity=identity)
            await stale.delete(kube, timeout=deadline - loop.time())
    return tuple(out)


async def delete_workload_http_routes(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    timeout: float,
    require_gateway_api: bool = False,
) -> None:
    """Delete this workload's managed HTTPRoutes, if present.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    identity : WorkloadIdentity
        Stable workload identity used to locate and validate HTTPRoutes.
    timeout : float
        Maximum deletion budget in seconds. If infinite, wait indefinitely.
    require_gateway_api : bool, optional
        Whether missing Gateway API CRDs should raise. Cleanup paths normally leave
        this disabled so stale-route cleanup does not require Gateway installation.

    Raises
    ------
    TimeoutError
        If deletion cannot start before `timeout` expires.
    """
    if timeout <= 0:
        msg = "workload HTTPRoute deletion timeout must be positive"
        raise TimeoutError(msg)

    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    for route in await _list_workload_http_routes(
        kube,
        identity=identity,
        timeout=deadline - loop.time(),
        require_gateway_api=require_gateway_api,
    ):
        _assert_managed_http_route(route, identity=identity)
        await route.delete(kube, timeout=deadline - loop.time())


def workload_service_ports(workload: WorkloadPod) -> tuple[ServicePortSpec, ...]:
    """Return the canonical Service ports for a workload.

    Parameters
    ----------
    workload : WorkloadPod
        Workload pod intent containing container port declarations.

    Returns
    -------
    tuple[ServicePortSpec, ...]
        Deterministic Service port declarations. Each Service port exposes the same
        port number as the container and targets the container's named port.

    Raises
    ------
    ValueError
        If any port name is empty, duplicated, out of range, or has an unsupported
        protocol.
    """
    seen: set[str] = set()
    ports: list[ServicePortSpec] = []
    for container in workload.template.containers:
        for port in container.ports:
            name = port.name.strip()
            if not name:
                msg = f"workload container {container.name!r} has an unnamed port"
                raise ValueError(msg)
            if name in seen:
                msg = f"duplicate workload Service port name: {name!r}"
                raise ValueError(msg)
            seen.add(name)

            container_port = int(port.container_port)
            if container_port < 1 or container_port > 65535:
                msg = (
                    f"workload Service port {name!r} is out of range: {container_port}"
                )
                raise ValueError(msg)

            protocol = port.protocol.strip().upper()
            if protocol not in _SERVICE_PROTOCOLS:
                msg = f"unsupported workload Service port protocol: {protocol!r}"
                raise ValueError(msg)
            ports.append(
                ServicePortSpec(
                    name=name,
                    port=container_port,
                    target_port=name,
                    protocol=cast("PortProtocol", protocol),
                )
            )
    return tuple(sorted(ports, key=lambda item: item.name))


def workload_http_route_name(
    identity: WorkloadIdentity,
    route: _RouteConfig,
) -> str:
    """Return the stable HTTPRoute resource name for one route intent.

    Parameters
    ----------
    identity : WorkloadIdentity
        Stable workload identity that owns the route.
    route : _RouteConfig
        External route intent.

    Returns
    -------
    str
        DNS-label-safe HTTPRoute name derived from workload identity and route
        fields.
    """
    payload = f"{route.host}\0{route.path}\0{route.port}".encode()
    digest = hashlib.sha256(payload).hexdigest()[:_HTTP_ROUTE_HASH_CHARS]
    prefix_chars = _MAX_KUBE_NAME_CHARS - len(digest) - 1
    prefix = identity.name[:prefix_chars].rstrip("-")
    return f"{prefix}-{digest}"


async def _list_workload_http_routes(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    timeout: float,
    require_gateway_api: bool,
) -> tuple[HTTPRoute, ...]:
    try:
        return tuple(
            await HTTPRoute.list(
                kube,
                namespace=BERTRAND_NAMESPACE,
                labels=_http_route_selector(identity),
                timeout=timeout,
            )
        )
    except OSError as err:
        if not require_gateway_api and gateway_api_crd_missing(err):
            return ()
        raise


def _http_route_rule(
    *,
    route: _RouteConfig,
    identity: WorkloadIdentity,
    port: int,
) -> dict[str, object]:
    return {
        "matches": [
            {
                "path": {
                    "type": "PathPrefix",
                    "value": route.path,
                }
            }
        ],
        "backendRefs": [
            {
                "kind": "Service",
                "name": identity.name,
                "port": port,
            }
        ],
    }


def _http_route_labels(identity: WorkloadIdentity) -> dict[str, str]:
    labels = dict(HTTP_ROUTE_LABELS)
    labels.update(identity.labels)
    labels[HTTP_ROUTE_LABEL] = HTTP_ROUTE_LABEL_VALUE
    return labels


def _http_route_selector(identity: WorkloadIdentity) -> dict[str, str]:
    return {
        WORKLOAD_LABEL: WORKLOAD_LABEL_VALUE,
        WORKLOAD_ID_LABEL: identity.workload_id,
        HTTP_ROUTE_LABEL: HTTP_ROUTE_LABEL_VALUE,
    }


def _assert_managed_service(
    service: Service | None,
    *,
    identity: WorkloadIdentity,
) -> None:
    if service is None:
        return
    labels = service.labels
    expected = {
        WORKLOAD_LABEL: WORKLOAD_LABEL_VALUE,
        WORKLOAD_ID_LABEL: identity.workload_id,
    }
    expected.update(identity.selector)
    if all(labels.get(key) == value for key, value in expected.items()):
        return
    msg = (
        f"Service {BERTRAND_NAMESPACE}/{identity.name} exists but is not managed by "
        "this Bertrand workload"
    )
    raise OSError(msg)


def _assert_managed_http_route(
    route: HTTPRoute | None,
    *,
    identity: WorkloadIdentity,
) -> None:
    if route is None:
        return
    labels = route.labels
    expected = _http_route_selector(identity)
    expected.update(identity.selector)
    if all(labels.get(key) == value for key, value in expected.items()):
        return
    msg = (
        f"HTTPRoute {BERTRAND_NAMESPACE}/{route.name or identity.name} exists but is "
        "not managed by this Bertrand workload"
    )
    raise OSError(msg)


def _assert_managed_network_policy(
    network_policy: NetworkPolicy | None,
    *,
    identity: WorkloadIdentity,
) -> None:
    if network_policy is None:
        return
    labels = network_policy.labels
    expected = {
        WORKLOAD_LABEL: WORKLOAD_LABEL_VALUE,
        WORKLOAD_ID_LABEL: identity.workload_id,
    }
    expected.update(identity.selector)
    if all(labels.get(key) == value for key, value in expected.items()):
        return
    msg = (
        f"NetworkPolicy {BERTRAND_NAMESPACE}/{identity.name} exists but is not "
        "managed by this Bertrand workload"
    )
    raise OSError(msg)
