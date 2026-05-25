"""Native Kubernetes networking helpers for Bertrand workloads."""

from __future__ import annotations

import hashlib
from dataclasses import dataclass
from typing import TYPE_CHECKING, cast

from bertrand.env.git import BERTRAND_NAMESPACE, Deadline
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
from bertrand.env.kube.workload_refs import (
    WORKLOAD_ID_LABEL,
    WORKLOAD_LABEL,
    WORKLOAD_LABEL_VALUE,
)

if TYPE_CHECKING:
    from bertrand.env.kube.api.client import Kube
    from bertrand.env.kube.api.spec import PortProtocol
    from bertrand.env.kube.workload.base import WorkloadIdentity, WorkloadPod
    from bertrand.env.kube.workload.config import (
        WorkloadNetworkConfig,
        WorkloadRouteConfig,
    )

_SERVICE_PROTOCOLS = frozenset({"TCP", "UDP", "SCTP"})
_HTTP_ROUTE_HASH_CHARS = 12
_MAX_KUBE_NAME_CHARS = 63


@dataclass(frozen=True)
class WorkloadHTTPRouteIntent:
    """Resolved HTTPRoute intent for one workload route.

    Parameters
    ----------
    name : str
        Stable Kubernetes HTTPRoute resource name.
    route : WorkloadRouteConfig
        Validated Bertrand route config.
    service_port : int
        Numeric Service backend port used by Gateway API.
    """

    name: str
    route: WorkloadRouteConfig
    service_port: int


@dataclass(frozen=True)
class WorkloadHTTPRoutePlan:
    """Prepared HTTPRoute convergence plan for one workload.

    Parameters
    ----------
    identity : WorkloadIdentity
        Stable workload identity that owns the routes.
    routes : tuple[WorkloadHTTPRouteIntent, ...]
        Desired HTTPRoute resources with resolved names and backend Service ports.
    """

    identity: WorkloadIdentity
    routes: tuple[WorkloadHTTPRouteIntent, ...]

    @property
    def desired_names(self) -> frozenset[str]:
        """Return desired HTTPRoute resource names.

        Returns
        -------
        frozenset[str]
            Names that should remain after pruning stale managed routes.
        """
        return frozenset(route.name for route in self.routes)


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
    message = "workload Service convergence timeout must be non-negative"
    if timeout <= 0:
        raise TimeoutError(message)
    deadline = Deadline.from_timeout(timeout, message=message)
    identity = workload.identity
    ports = workload_service_ports(workload)
    if not ports:
        await delete_workload_service(
            kube,
            identity=identity,
            timeout=deadline.remaining(),
        )
        return None

    service = await Service.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=identity.name,
        timeout=deadline.remaining(),
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
        timeout=deadline.remaining(),
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
    message = "workload Service deletion timeout must be non-negative"
    if timeout <= 0:
        raise TimeoutError(message)
    deadline = Deadline.from_timeout(timeout, message=message)
    service = await Service.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=identity.name,
        timeout=deadline.remaining(),
    )
    _assert_managed_service(service, identity=identity)
    if service is not None:
        await service.delete(kube, timeout=deadline.remaining())


async def ensure_workload_network_policy(
    kube: Kube,
    *,
    network: WorkloadNetworkConfig,
    workload: WorkloadPod,
    timeout: float,
    route_plan: WorkloadHTTPRoutePlan | None = None,
) -> NetworkPolicy | None:
    """Converge this workload's Kubernetes NetworkPolicy intent.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    network : WorkloadNetworkConfig
        Validated Bertrand network config whose policy selects the NetworkPolicy
        shape.
    workload : WorkloadPod
        Workload pod intent whose stable selector identifies the protected Pods.
    timeout : float
        Maximum convergence budget in seconds. If infinite, wait indefinitely.
    route_plan : WorkloadHTTPRoutePlan | None, optional
        Prepared HTTPRoute plan whose backend ports are used for route-aware
        isolated ingress.

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
        If the validated network config contains an unsupported policy value, or an
        HTTPRoute targets a non-TCP Service port.
    """
    message = "workload NetworkPolicy convergence timeout must be positive"
    if timeout <= 0:
        raise TimeoutError(message)
    deadline = Deadline.from_timeout(timeout, message=message)
    identity = workload.identity
    policy = network.policy.strip().lower()
    if policy == "open":
        await delete_workload_network_policy(
            kube,
            identity=identity,
            timeout=deadline.remaining(),
        )
        return None
    if policy != "isolated":
        msg = f"unsupported workload network policy: {network.policy!r}"
        raise ValueError(msg)

    network_policy = await NetworkPolicy.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=identity.name,
        timeout=deadline.remaining(),
    )
    _assert_managed_network_policy(network_policy, identity=identity)
    return await NetworkPolicy.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=identity.name,
        pod_selector=identity.selector,
        labels=identity.labels,
        policy_types=("Ingress",),
        ingress=_isolated_ingress_rules(network, workload, route_plan=route_plan),
        timeout=deadline.remaining(),
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
    message = "workload NetworkPolicy deletion timeout must be positive"
    if timeout <= 0:
        raise TimeoutError(message)
    deadline = Deadline.from_timeout(timeout, message=message)
    network_policy = await NetworkPolicy.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=identity.name,
        timeout=deadline.remaining(),
    )
    _assert_managed_network_policy(network_policy, identity=identity)
    if network_policy is not None:
        await network_policy.delete(kube, timeout=deadline.remaining())


async def ensure_workload_http_routes(
    kube: Kube,
    *,
    network: WorkloadNetworkConfig,
    workload: WorkloadPod,
    timeout: float,
    route_plan: WorkloadHTTPRoutePlan | None = None,
) -> tuple[HTTPRoute, ...]:
    """Converge this workload's managed Gateway API HTTPRoutes.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    network : WorkloadNetworkConfig
        Validated Bertrand network config whose route intents select the HTTPRoute
        shape.
    workload : WorkloadPod
        Workload pod intent whose canonical Service receives route traffic.
    timeout : float
        Maximum convergence budget in seconds. If infinite, wait indefinitely.
    route_plan : WorkloadHTTPRoutePlan | None, optional
        Prepared route plan. If omitted, route validation and Gateway preflight are
        performed inside this function.

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
    """
    message = "workload HTTPRoute convergence timeout must be positive"
    if timeout <= 0:
        raise TimeoutError(message)
    deadline = Deadline.from_timeout(timeout, message=message)
    plan = route_plan or await prepare_workload_http_routes(
        kube,
        network=network,
        workload=workload,
        timeout=deadline.remaining(),
    )
    if not plan.routes:
        await prune_workload_http_routes(
            kube,
            route_plan=plan,
            timeout=deadline.remaining(),
        )
        return ()
    await prune_workload_http_routes(
        kube,
        route_plan=plan,
        timeout=deadline.remaining(),
    )
    out: list[HTTPRoute] = []
    for desired in plan.routes:
        current = await HTTPRoute.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=desired.name,
            timeout=deadline.remaining(),
        )
        _assert_managed_http_route(current, identity=plan.identity)
        try:
            route_obj = await HTTPRoute.upsert(
                kube,
                namespace=BERTRAND_NAMESPACE,
                name=desired.name,
                parent_refs=bertrand_gateway_parent_refs(),
                hostnames=(desired.route.host,),
                rules=(
                    _http_route_rule(
                        route=desired.route,
                        identity=plan.identity,
                        port=desired.service_port,
                    ),
                ),
                labels=_http_route_labels(plan.identity),
                timeout=deadline.remaining(),
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

    return tuple(out)


async def prepare_workload_http_routes(
    kube: Kube,
    *,
    network: WorkloadNetworkConfig,
    workload: WorkloadPod,
    timeout: float,
) -> WorkloadHTTPRoutePlan:
    """Validate HTTPRoute intent and preflight shared Gateway readiness.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    network : WorkloadNetworkConfig
        Validated Bertrand network config whose route intents select the HTTPRoute
        shape.
    workload : WorkloadPod
        Workload pod intent whose canonical Service receives route traffic.
    timeout : float
        Maximum preparation budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    WorkloadHTTPRoutePlan
        Resolved route plan. Empty plans do not require Gateway API availability.

    Raises
    ------
    TimeoutError
        If preparation cannot start before `timeout` expires.
    OSError
        If configured routes require Gateway API resources that are unavailable.
    """
    message = "workload HTTPRoute preparation timeout must be positive"
    if timeout <= 0:
        raise TimeoutError(message)
    deadline = Deadline.from_timeout(timeout, message=message)
    plan = _workload_http_route_plan(network, workload)
    if not plan.routes:
        return plan
    try:
        await ensure_bertrand_gateway(kube, timeout=deadline.remaining())
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
    return plan


async def prune_workload_http_routes(
    kube: Kube,
    *,
    route_plan: WorkloadHTTPRoutePlan,
    timeout: float,
) -> None:
    """Delete stale managed HTTPRoutes before desired route convergence.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    route_plan : WorkloadHTTPRoutePlan
        Prepared route plan whose desired names should remain.
    timeout : float
        Maximum pruning budget in seconds. If infinite, wait indefinitely.

    Raises
    ------
    TimeoutError
        If pruning cannot start before `timeout` expires.
    """
    message = "workload HTTPRoute pruning timeout must be positive"
    if timeout <= 0:
        raise TimeoutError(message)
    deadline = Deadline.from_timeout(timeout, message=message)
    require_gateway_api = bool(route_plan.routes)
    for stale in await _list_workload_http_routes(
        kube,
        identity=route_plan.identity,
        timeout=deadline.remaining(),
        require_gateway_api=require_gateway_api,
    ):
        if stale.name in route_plan.desired_names:
            continue
        _assert_managed_http_route(stale, identity=route_plan.identity)
        await stale.delete(kube, timeout=deadline.remaining())


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
    message = "workload HTTPRoute deletion timeout must be positive"
    if timeout <= 0:
        raise TimeoutError(message)
    deadline = Deadline.from_timeout(timeout, message=message)
    for route in await _list_workload_http_routes(
        kube,
        identity=identity,
        timeout=deadline.remaining(),
        require_gateway_api=require_gateway_api,
    ):
        _assert_managed_http_route(route, identity=identity)
        await route.delete(kube, timeout=deadline.remaining())


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
    route: WorkloadRouteConfig,
) -> str:
    """Return the stable HTTPRoute resource name for one route intent.

    Parameters
    ----------
    identity : WorkloadIdentity
        Stable workload identity that owns the route.
    route : WorkloadRouteConfig
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


def _workload_http_route_plan(
    network: WorkloadNetworkConfig,
    workload: WorkloadPod,
) -> WorkloadHTTPRoutePlan:
    identity = workload.identity
    ports = {port.name: port for port in workload_service_ports(workload)}
    desired_names: set[str] = set()
    desired: list[WorkloadHTTPRouteIntent] = []
    for route in network.routes:
        service_port = _service_port_for_http_route(route, ports)
        name = workload_http_route_name(identity, route)
        if name in desired_names:
            msg = f"duplicate workload HTTPRoute resource name: {name!r}"
            raise ValueError(msg)
        desired_names.add(name)
        desired.append(
            WorkloadHTTPRouteIntent(
                name=name,
                route=route,
                service_port=service_port.port,
            )
        )
    return WorkloadHTTPRoutePlan(identity=identity, routes=tuple(desired))


def _isolated_ingress_rules(
    network: WorkloadNetworkConfig,
    workload: WorkloadPod,
    *,
    route_plan: WorkloadHTTPRoutePlan | None,
) -> tuple[dict[str, object], ...]:
    plan = route_plan or _workload_http_route_plan(network, workload)
    allowed_ports = {route.service_port for route in plan.routes}
    if not allowed_ports:
        return ()
    return (
        {
            "ports": [
                {"protocol": "TCP", "port": port} for port in sorted(allowed_ports)
            ],
        },
    )


def _service_port_for_http_route(
    route: WorkloadRouteConfig,
    ports: dict[str, ServicePortSpec],
) -> ServicePortSpec:
    service_port = ports.get(route.port)
    if service_port is None:
        msg = (
            f"workload HTTPRoute for host {route.host!r} references unknown "
            f"Service port {route.port!r}"
        )
        raise ValueError(msg)
    protocol = str(service_port.protocol).strip().upper()
    if protocol != "TCP":
        msg = (
            f"workload HTTPRoute for host {route.host!r} targets non-TCP Service "
            f"port {route.port!r} ({protocol}); HTTPRoute backends require TCP"
        )
        raise ValueError(msg)
    return service_port


def _http_route_rule(
    *,
    route: WorkloadRouteConfig,
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
