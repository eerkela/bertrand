"""Bertrand-managed Gateway API substrate convergence."""

from __future__ import annotations

import asyncio
from typing import TYPE_CHECKING

from bertrand.env.git import BERTRAND_ENV, BERTRAND_NAMESPACE
from bertrand.env.kube.gateway import Gateway, GatewayClass

if TYPE_CHECKING:
    from bertrand.env.kube.api.client import Kube

ENVOY_GATEWAY_CONTROLLER = "gateway.envoyproxy.io/gatewayclass-controller"
BERTRAND_GATEWAY_CLASS = "bertrand-envoy"
BERTRAND_GATEWAY = "bertrand-gateway"
BERTRAND_GATEWAY_LISTENER = "http"
BERTRAND_GATEWAY_PORT = 80
GATEWAY_LABEL = "bertrand.dev/gateway"
GATEWAY_LABEL_VALUE = "v1"
HTTP_ROUTE_LABEL = "bertrand.dev/http-route"
HTTP_ROUTE_LABEL_VALUE = "v1"
GATEWAY_LABELS = {
    "app.kubernetes.io/name": BERTRAND_GATEWAY,
    "app.kubernetes.io/part-of": "bertrand",
    "app.kubernetes.io/component": "gateway",
    BERTRAND_ENV: "1",
    GATEWAY_LABEL: GATEWAY_LABEL_VALUE,
}
HTTP_ROUTE_LABELS = {
    "app.kubernetes.io/part-of": "bertrand",
    "app.kubernetes.io/component": "workload-route",
    BERTRAND_ENV: "1",
    HTTP_ROUTE_LABEL: HTTP_ROUTE_LABEL_VALUE,
}


async def ensure_bertrand_gateway(kube: Kube, *, timeout: float) -> Gateway:
    """Converge Bertrand's shared Gateway API substrate.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum convergence budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    Gateway
        Accepted Bertrand Gateway with at least one external address.

    Raises
    ------
    TimeoutError
        If Gateway convergence exceeds `timeout`.
    OSError
        If Gateway API CRDs, Envoy Gateway acceptance, or external address
        assignment are unavailable.
    """
    if timeout <= 0:
        msg = "Bertrand Gateway convergence timeout must be positive"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    try:
        current_class = await GatewayClass.get(
            kube,
            name=BERTRAND_GATEWAY_CLASS,
            timeout=deadline - loop.time(),
        )
        _assert_managed_gateway_resource(current_class, kind="GatewayClass")
        await GatewayClass.upsert(
            kube,
            name=BERTRAND_GATEWAY_CLASS,
            controller_name=ENVOY_GATEWAY_CONTROLLER,
            labels=GATEWAY_LABELS,
            timeout=deadline - loop.time(),
        )
    except OSError as err:
        message = _gateway_api_error_message("upsert GatewayClass", err)
        if message is not None:
            raise OSError(message) from err
        raise
    await _wait_gateway_class_accepted(
        kube,
        timeout=deadline - loop.time(),
    )
    try:
        current_gateway = await Gateway.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=BERTRAND_GATEWAY,
            timeout=deadline - loop.time(),
        )
        _assert_managed_gateway_resource(current_gateway, kind="Gateway")
        await Gateway.upsert(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=BERTRAND_GATEWAY,
            gateway_class=BERTRAND_GATEWAY_CLASS,
            listeners=_bertrand_gateway_listeners(),
            labels=GATEWAY_LABELS,
            timeout=deadline - loop.time(),
        )
    except OSError as err:
        message = _gateway_api_error_message("upsert Gateway", err)
        if message is not None:
            raise OSError(message) from err
        raise
    return await _wait_gateway_address(kube, timeout=deadline - loop.time())


def gateway_api_crd_missing(err: OSError) -> bool:
    """Return whether an error looks like missing Gateway API CRDs.

    Parameters
    ----------
    err : OSError
        Kubernetes API error raised while accessing Gateway API resources.

    Returns
    -------
    bool
        ``True`` when the error suggests the Gateway API resource type is not
        installed in the cluster.
    """
    detail = str(err).lower()
    return (
        "gateway api crds are missing" in detail
        or "status 404" in detail
        or "the server could not find the requested resource" in detail
        or "not found" in detail
    )


def bertrand_gateway_parent_refs() -> tuple[dict[str, object], ...]:
    """Return parent references for Bertrand-managed HTTPRoutes.

    Returns
    -------
    tuple[dict[str, object], ...]
        Gateway API `parentRefs` payload that attaches routes to Bertrand's shared
        HTTP listener.
    """
    return (
        {
            "name": BERTRAND_GATEWAY,
            "namespace": BERTRAND_NAMESPACE,
            "sectionName": BERTRAND_GATEWAY_LISTENER,
        },
    )


def _bertrand_gateway_listeners() -> tuple[dict[str, object], ...]:
    return (
        {
            "name": BERTRAND_GATEWAY_LISTENER,
            "protocol": "HTTP",
            "port": BERTRAND_GATEWAY_PORT,
            "allowedRoutes": {"namespaces": {"from": "Same"}},
        },
    )


async def _wait_gateway_class_accepted(kube: Kube, *, timeout: float) -> GatewayClass:
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    last: GatewayClass | None = None
    while True:
        remaining = deadline - loop.time()
        if remaining <= 0:
            detail = f": {last.acceptance_message}" if last is not None else ""
            msg = (
                f"GatewayClass {BERTRAND_GATEWAY_CLASS!r} was not accepted by "
                f"Envoy Gateway controller {ENVOY_GATEWAY_CONTROLLER!r}{detail}. "
                "Install/start Envoy Gateway and ensure it watches this controller."
            )
            raise OSError(msg)
        current = await GatewayClass.get(
            kube,
            name=BERTRAND_GATEWAY_CLASS,
            timeout=remaining,
        )
        if current is not None:
            last = current
            if current.accepted:
                return current
        await asyncio.sleep(min(0.5, max(0.0, deadline - loop.time())))


async def _wait_gateway_address(kube: Kube, *, timeout: float) -> Gateway:
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    while True:
        remaining = deadline - loop.time()
        if remaining <= 0:
            msg = (
                f"Gateway {BERTRAND_NAMESPACE}/{BERTRAND_GATEWAY} has no external "
                "address. Configure a LoadBalancer provider such as MetalLB with "
                "`bertrand cluster network lb install` and an explicit address "
                "pool; Bertrand does not guess address pools."
            )
            raise OSError(msg)
        current = await Gateway.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=BERTRAND_GATEWAY,
            timeout=remaining,
        )
        if current is not None and current.addresses:
            return current
        await asyncio.sleep(min(0.5, max(0.0, deadline - loop.time())))


def _gateway_api_error_message(action: str, err: OSError) -> str | None:
    if gateway_api_crd_missing(err):
        return (
            f"Gateway API CRDs are missing while trying to {action}. Run "
            "`bertrand init` to install Envoy Gateway and its Gateway API CRDs, "
            "or install Envoy Gateway manually before publishing Bertrand routes."
        )
    return None


def _assert_managed_gateway_resource(
    resource: GatewayClass | Gateway | None,
    *,
    kind: str,
) -> None:
    if resource is None:
        return
    labels = resource.labels
    expected = {
        BERTRAND_ENV: "1",
        GATEWAY_LABEL: GATEWAY_LABEL_VALUE,
    }
    if all(labels.get(key) == value for key, value in expected.items()):
        return
    location = resource.name
    if isinstance(resource, Gateway):
        location = f"{resource.namespace}/{resource.name}"
    msg = f"{kind} {location} exists but is not managed by Bertrand"
    raise OSError(msg)
