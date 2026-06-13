"""Native Kubernetes networking helpers for Bertrand workloads."""

from __future__ import annotations

import hashlib
from typing import TYPE_CHECKING, cast

from bertrand.env.git import BERTRAND_NAMESPACE, Deadline
from bertrand.env.kube.network.gateway import (
    HTTP_ROUTE_LABEL,
    HTTP_ROUTE_LABEL_VALUE,
    HTTP_ROUTE_LABELS,
    HTTP_ROUTE_RESOURCE,
    bertrand_gateway_parent_refs,
    ensure_bertrand_gateway,
    gateway_api_crd_missing,
    upsert_http_route,
)
from bertrand.env.kube.network_policy import NetworkPolicy, NetworkPolicyManifest
from bertrand.env.kube.service import Service, ServiceManifest, ServicePortView

if TYPE_CHECKING:
    from bertrand.env.config.bertrand import BertrandModel
    from bertrand.env.kube.api.client import Kube
    from bertrand.env.kube.custom_object import CustomObject
    from bertrand.env.kube.workload.base import WorkloadIdentity, WorkloadPod

_SERVICE_PROTOCOLS = frozenset({"TCP", "UDP", "SCTP"})
_HTTP_ROUTE_HASH_CHARS = 12
_MAX_KUBE_NAME_CHARS = 63

type _HTTPRoutePlan = dict[str, tuple[BertrandModel.Network.Route, int]]


async def ensure_workload_service(
    kube: Kube,
    *,
    workload: WorkloadPod,
    deadline: Deadline,
) -> Service | None:
    """Converge this workload's canonical internal Kubernetes Service.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    workload : WorkloadPod
        Workload pod intent whose declared container ports define the Service shape.
    deadline : Deadline
        Maximum convergence budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    Service | None
        Converged ClusterIP Service, or `None` when the workload declares no ports
        and any stale managed Service has been removed.

    """
    identity = workload.identity
    ports = workload_service_ports(workload)
    if not ports:
        await delete_workload_service(
            kube,
            identity=identity,
            deadline=deadline,
        )
        return None

    service = await Service.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=identity.name,
        deadline=deadline,
    )
    _assert_managed(service, identity=identity, kind="Service")
    return await Service.upsert(
        kube,
        intent=ServiceManifest(
            namespace=BERTRAND_NAMESPACE,
            name=identity.name,
            selector=identity.selector,
            ports=ports,
            labels=identity.labels,
            service_type="ClusterIP",
        ),
        deadline=deadline,
    )


async def delete_workload_service(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    deadline: Deadline,
) -> None:
    """Delete this workload's managed Service, if present.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    identity : WorkloadIdentity
        Stable workload identity used to locate and validate the Service.
    deadline : Deadline
        Maximum deletion budget in seconds. If infinite, wait indefinitely.

    """
    service = await Service.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=identity.name,
        deadline=deadline,
    )
    _assert_managed(service, identity=identity, kind="Service")
    if service is not None:
        await service.delete(
            kube,
            deadline=deadline,
        )


async def ensure_workload_network_policy(
    kube: Kube,
    *,
    network: BertrandModel.Network,
    workload: WorkloadPod,
    deadline: Deadline,
    route_plan: _HTTPRoutePlan | None = None,
) -> NetworkPolicy | None:
    """Converge this workload's Kubernetes NetworkPolicy intent.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    network : BertrandModel.Network
        Validated Bertrand network config whose policy selects the NetworkPolicy
        shape.
    workload : WorkloadPod
        Workload pod intent whose stable selector identifies the protected Pods.
    deadline : Deadline
        Maximum convergence budget in seconds. If infinite, wait indefinitely.
    route_plan : dict[str, tuple[BertrandModel.Network.Route, int]] | None, optional
        Prepared HTTPRoute data whose backend ports are used for route-aware isolated
        ingress.

    Returns
    -------
    NetworkPolicy | None
        Converged ingress-only NetworkPolicy, or `None` when policy is `"open"`
        and any stale managed NetworkPolicy has been removed.

    Raises
    ------
    ValueError
        If the validated network config contains an unsupported policy value, or an
        HTTPRoute targets a non-TCP Service port.
    """
    identity = workload.identity
    policy = network.policy.strip().lower()
    if policy == "open":
        await delete_workload_network_policy(
            kube,
            identity=identity,
            deadline=deadline,
        )
        return None
    if policy != "isolated":
        msg = f"unsupported workload network policy: {network.policy!r}"
        raise ValueError(msg)

    network_policy = await NetworkPolicy.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=identity.name,
        deadline=deadline,
    )
    _assert_managed(network_policy, identity=identity, kind="NetworkPolicy")
    return await NetworkPolicy.upsert(
        kube,
        intent=NetworkPolicyManifest(
            namespace=BERTRAND_NAMESPACE,
            name=identity.name,
            pod_selector=identity.selector,
            labels=identity.labels,
            policy_types=("Ingress",),
            ingress=_isolated_ingress_rules(network, workload, route_plan=route_plan),
        ),
        deadline=deadline,
    )


async def delete_workload_network_policy(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    deadline: Deadline,
) -> None:
    """Delete this workload's managed NetworkPolicy, if present.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    identity : WorkloadIdentity
        Stable workload identity used to locate and validate the NetworkPolicy.
    deadline : Deadline
        Maximum deletion budget in seconds. If infinite, wait indefinitely.

    """
    network_policy = await NetworkPolicy.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=identity.name,
        deadline=deadline,
    )
    _assert_managed(network_policy, identity=identity, kind="NetworkPolicy")
    if network_policy is not None:
        await network_policy.delete(
            kube,
            deadline=deadline,
        )


async def ensure_workload_http_routes(
    kube: Kube,
    *,
    network: BertrandModel.Network,
    workload: WorkloadPod,
    deadline: Deadline,
    route_plan: _HTTPRoutePlan | None = None,
) -> tuple[CustomObject, ...]:
    """Converge this workload's managed Gateway API HTTPRoutes.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    network : BertrandModel.Network
        Validated Bertrand network config whose route intents select the HTTPRoute
        shape.
    workload : WorkloadPod
        Workload pod intent whose canonical Service receives route traffic.
    deadline : Deadline
        Maximum convergence budget in seconds. If infinite, wait indefinitely.
    route_plan : dict[str, tuple[BertrandModel.Network.Route, int]] | None, optional
        Prepared route data. If omitted, route validation and Gateway preflight are
        performed inside this function.

    Returns
    -------
    tuple[CustomObject, ...]
        Managed HTTPRoutes matching the workload's configured external routes.

    Raises
    ------
    OSError
        If Gateway API HTTPRoute resources are unavailable.
    """
    plan = (
        route_plan
        if route_plan is not None
        else await prepare_workload_http_routes(
            kube,
            network=network,
            workload=workload,
            deadline=deadline,
        )
    )
    identity = workload.identity
    await prune_workload_http_routes(
        kube,
        identity=identity,
        route_plan=plan,
        deadline=deadline,
    )
    if not plan:
        return ()
    out: list[CustomObject] = []
    for name, (route, service_port) in plan.items():
        current = await HTTP_ROUTE_RESOURCE.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=name,
            deadline=deadline,
        )
        _assert_managed(current, identity=identity, kind="HTTPRoute")
        try:
            route_obj = await upsert_http_route(
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
                deadline=deadline,
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
    network: BertrandModel.Network,
    workload: WorkloadPod,
    deadline: Deadline,
) -> _HTTPRoutePlan:
    """Validate HTTPRoute intent and preflight shared Gateway readiness.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    network : BertrandModel.Network
        Validated Bertrand network config whose route intents select the HTTPRoute
        shape.
    workload : WorkloadPod
        Workload pod intent whose canonical Service receives route traffic.
    deadline : Deadline
        Maximum preparation budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    dict[str, tuple[BertrandModel.Network.Route, int]]
        Resolved route data keyed by HTTPRoute name. Empty plans do not require
        Gateway API availability.

    Raises
    ------
    OSError
        If configured routes require Gateway API resources that are unavailable.
    """
    plan = _workload_http_route_plan(network, workload)
    if not plan:
        return plan
    try:
        await ensure_bertrand_gateway(kube, deadline=deadline)
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
    identity: WorkloadIdentity,
    route_plan: _HTTPRoutePlan,
    deadline: Deadline,
) -> None:
    """Delete stale managed HTTPRoutes before desired route convergence.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    identity : WorkloadIdentity
        Stable workload identity used to select managed routes.
    route_plan : dict[str, tuple[BertrandModel.Network.Route, int]]
        Prepared route data whose desired names should remain.
    deadline : Deadline
        Maximum pruning budget in seconds. If infinite, wait indefinitely.

    """
    require_gateway_api = bool(route_plan)
    for stale in await _list_workload_http_routes(
        kube,
        identity=identity,
        deadline=deadline,
        require_gateway_api=require_gateway_api,
    ):
        if stale.name in route_plan:
            continue
        _assert_managed(stale, identity=identity, kind="HTTPRoute")
        await HTTP_ROUTE_RESOURCE.delete(
            kube,
            resource=stale,
            deadline=deadline,
        )


async def delete_workload_http_routes(
    kube: Kube,
    *,
    identity: WorkloadIdentity,
    deadline: Deadline,
    require_gateway_api: bool = False,
) -> None:
    """Delete this workload's managed HTTPRoutes, if present.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    identity : WorkloadIdentity
        Stable workload identity used to locate and validate HTTPRoutes.
    deadline : Deadline
        Maximum deletion budget in seconds. If infinite, wait indefinitely.
    require_gateway_api : bool, optional
        Whether missing Gateway API CRDs should raise. Cleanup paths normally leave
        this disabled so stale-route cleanup does not require Gateway installation.

    """
    for route in await _list_workload_http_routes(
        kube,
        identity=identity,
        deadline=deadline,
        require_gateway_api=require_gateway_api,
    ):
        _assert_managed(route, identity=identity, kind="HTTPRoute")
        await HTTP_ROUTE_RESOURCE.delete(
            kube,
            resource=route,
            deadline=deadline,
        )


def workload_service_ports(workload: WorkloadPod) -> tuple[ServicePortView, ...]:
    """Return the canonical Service ports for a workload.

    Parameters
    ----------
    workload : WorkloadPod
        Workload pod intent containing container port declarations.

    Returns
    -------
    tuple[ServicePortView, ...]
        Deterministic Service port declarations. Each Service port exposes the same
        port number as the container and targets the container's named port.

    Raises
    ------
    ValueError
        If any port name is empty, duplicated, out of range, or has an unsupported
        protocol.
    """
    seen: set[str] = set()
    ports: list[ServicePortView] = []
    for container in workload.template.containers:
        for port in container.ports:
            name = str(port.get("name", "")).strip()
            if not name:
                msg = f"workload container {container.name!r} has an unnamed port"
                raise ValueError(msg)
            if name in seen:
                msg = f"duplicate workload Service port name: {name!r}"
                raise ValueError(msg)
            seen.add(name)

            container_port = int(cast("int | str", port.get("containerPort", 0)))
            if container_port < 1 or container_port > 65535:
                msg = (
                    f"workload Service port {name!r} is out of range: {container_port}"
                )
                raise ValueError(msg)

            protocol = str(port.get("protocol", "TCP")).strip().upper()
            if protocol not in _SERVICE_PROTOCOLS:
                msg = f"unsupported workload Service port protocol: {protocol!r}"
                raise ValueError(msg)
            ports.append(
                ServicePortView(
                    name=name,
                    port=container_port,
                    target_port=name,
                    protocol=protocol,
                )
            )
    return tuple(sorted(ports, key=lambda item: item.name))


def workload_http_route_name(
    identity: WorkloadIdentity,
    route: BertrandModel.Network.Route,
) -> str:
    """Return the stable HTTPRoute resource name for one route intent.

    Parameters
    ----------
    identity : WorkloadIdentity
        Stable workload identity that owns the route.
    route : BertrandModel.Network.Route
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
    deadline: Deadline,
    require_gateway_api: bool,
) -> tuple[CustomObject, ...]:
    try:
        return tuple(
            await HTTP_ROUTE_RESOURCE.list(
                kube,
                namespace=BERTRAND_NAMESPACE,
                labels={
                    **identity.managed_selector,
                    HTTP_ROUTE_LABEL: HTTP_ROUTE_LABEL_VALUE,
                },
                deadline=deadline,
            )
        )
    except OSError as err:
        if not require_gateway_api and gateway_api_crd_missing(err):
            return ()
        raise


def _workload_http_route_plan(
    network: BertrandModel.Network,
    workload: WorkloadPod,
) -> _HTTPRoutePlan:
    identity = workload.identity
    ports = {port.name: port for port in workload_service_ports(workload)}
    desired: _HTTPRoutePlan = {}
    for route in network.routes:
        service_port = _service_port_for_http_route(route, ports)
        name = workload_http_route_name(identity, route)
        if name in desired:
            msg = f"duplicate workload HTTPRoute resource name: {name!r}"
            raise ValueError(msg)
        desired[name] = (route, service_port.port)
    return desired


def _isolated_ingress_rules(
    network: BertrandModel.Network,
    workload: WorkloadPod,
    *,
    route_plan: _HTTPRoutePlan | None,
) -> tuple[dict[str, object], ...]:
    plan = (
        route_plan
        if route_plan is not None
        else _workload_http_route_plan(
            network,
            workload,
        )
    )
    allowed_ports = {service_port for _route, service_port in plan.values()}
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
    route: BertrandModel.Network.Route,
    ports: dict[str, ServicePortView],
) -> ServicePortView:
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
    route: BertrandModel.Network.Route,
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


def _assert_managed(
    resource: Service | NetworkPolicy | CustomObject | None,
    *,
    identity: WorkloadIdentity,
    kind: str,
) -> None:
    if resource is None:
        return
    labels = resource.labels
    expected = dict(identity.managed_selector)
    if kind == "HTTPRoute":
        expected[HTTP_ROUTE_LABEL] = HTTP_ROUTE_LABEL_VALUE
    if all(labels.get(key) == value for key, value in expected.items()):
        return
    name = resource.name or identity.name
    msg = (
        f"{kind} {BERTRAND_NAMESPACE}/{name} exists but is not managed by this "
        "Bertrand workload"
    )
    raise OSError(msg)
