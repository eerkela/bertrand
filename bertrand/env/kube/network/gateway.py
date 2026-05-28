"""Bertrand-managed Gateway API substrate convergence."""

from __future__ import annotations

import asyncio
from typing import TYPE_CHECKING, Any, cast

from bertrand.env.git import BERTRAND_ENV, BERTRAND_NAMESPACE, Deadline
from bertrand.env.kube.api.client import is_missing_api_resource
from bertrand.env.kube.custom_object import CustomObject, CustomObjectResource

if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence

    from bertrand.env.kube.api.client import Kube

GATEWAY_API_GROUP = "gateway.networking.k8s.io"
GATEWAY_API_VERSION = "v1"
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
GATEWAY_CLASS_RESOURCE = CustomObjectResource[CustomObject](
    group=GATEWAY_API_GROUP,
    version=GATEWAY_API_VERSION,
    kind="GatewayClass",
    plural="gatewayclasses",
    scope="cluster",
)
GATEWAY_RESOURCE = CustomObjectResource[CustomObject](
    group=GATEWAY_API_GROUP,
    version=GATEWAY_API_VERSION,
    kind="Gateway",
    plural="gateways",
)
HTTP_ROUTE_RESOURCE = CustomObjectResource[CustomObject](
    group=GATEWAY_API_GROUP,
    version=GATEWAY_API_VERSION,
    kind="HTTPRoute",
    plural="httproutes",
)


async def upsert_gateway_class(
    kube: Kube,
    *,
    name: str,
    controller_name: str,
    timeout: float,
    labels: Mapping[str, str] | None = None,
) -> CustomObject:
    """Create or patch one GatewayClass.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    name : str
        GatewayClass name to create or patch.
    controller_name : str
        Gateway controller name that should reconcile the class.
    timeout : float
        Maximum request budget in seconds. If infinite, wait indefinitely.
    labels : Mapping[str, str] | None, optional
        Labels to apply to the GatewayClass.

    Returns
    -------
    CustomObject
        Created or patched GatewayClass.
    """
    return await GATEWAY_CLASS_RESOURCE.upsert(
        kube,
        name=name,
        spec={"controllerName": controller_name},
        labels=labels,
        timeout=timeout,
    )


async def upsert_gateway(
    kube: Kube,
    *,
    namespace: str,
    name: str,
    gateway_class: str,
    listeners: Sequence[Mapping[str, object]],
    timeout: float,
    labels: Mapping[str, str] | None = None,
) -> CustomObject:
    """Create or patch one Gateway.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    namespace : str
        Namespace that owns the Gateway.
    name : str
        Gateway name to create or patch.
    gateway_class : str
        GatewayClass name used by the Gateway.
    listeners : Sequence[Mapping[str, object]]
        Gateway listener specs to render under `spec.listeners`.
    timeout : float
        Maximum request budget in seconds. If infinite, wait indefinitely.
    labels : Mapping[str, str] | None, optional
        Labels to apply to the Gateway.

    Returns
    -------
    CustomObject
        Created or patched Gateway.
    """
    return await GATEWAY_RESOURCE.upsert(
        kube,
        namespace=namespace,
        name=name,
        spec={
            "gatewayClassName": gateway_class,
            "listeners": [dict(listener) for listener in listeners],
        },
        labels=labels,
        timeout=timeout,
    )


async def upsert_http_route(
    kube: Kube,
    *,
    namespace: str,
    name: str,
    parent_refs: Sequence[Mapping[str, object]],
    hostnames: Sequence[str],
    rules: Sequence[Mapping[str, object]],
    timeout: float,
    labels: Mapping[str, str] | None = None,
) -> CustomObject:
    """Create or patch one HTTPRoute.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    namespace : str
        Namespace that owns the HTTPRoute.
    name : str
        HTTPRoute name to create or patch.
    parent_refs : Sequence[Mapping[str, object]]
        Gateway parent references to render under `spec.parentRefs`.
    hostnames : Sequence[str]
        Hostnames matched by the route.
    rules : Sequence[Mapping[str, object]]
        HTTPRoute rules to render under `spec.rules`.
    timeout : float
        Maximum request budget in seconds. If infinite, wait indefinitely.
    labels : Mapping[str, str] | None, optional
        Labels to apply to the HTTPRoute.

    Returns
    -------
    CustomObject
        Created or patched HTTPRoute.
    """
    return await HTTP_ROUTE_RESOURCE.upsert(
        kube,
        namespace=namespace,
        name=name,
        spec={
            "parentRefs": [dict(ref) for ref in parent_refs],
            "hostnames": list(hostnames),
            "rules": [dict(rule) for rule in rules],
        },
        labels=labels,
        timeout=timeout,
    )


def gateway_class_accepted(gateway_class: CustomObject) -> bool:
    """Return whether the GatewayClass is accepted by its controller.

    Returns
    -------
    bool
        ``True`` when the GatewayClass has `Accepted=True`.
    """
    return _condition_status(gateway_class.status, "Accepted") == "true"


def gateway_class_acceptance_message(gateway_class: CustomObject) -> str:
    """Return a diagnostic message for GatewayClass acceptance.

    Returns
    -------
    str
        Reason/message from the `Accepted` condition, or an empty string.
    """
    return _condition_message(gateway_class.status, "Accepted")


def gateway_addresses(gateway: CustomObject) -> tuple[str, ...]:
    """Return Gateway external addresses.

    Returns
    -------
    tuple[str, ...]
        Non-empty values from `status.addresses`.
    """
    addresses = gateway.status.get("addresses", ())
    if not isinstance(addresses, list):
        return ()
    out: list[str] = []
    for address in addresses:
        if not isinstance(address, dict):
            continue
        value = str(address.get("value") or "").strip()
        if value:
            out.append(value)
    return tuple(out)


def http_route_hostnames(route: CustomObject) -> tuple[str, ...]:
    """Return HTTPRoute hostnames.

    Returns
    -------
    tuple[str, ...]
        Hostnames listed under `spec.hostnames`.
    """
    hostnames = route.spec.get("hostnames", ())
    if not isinstance(hostnames, list):
        return ()
    return tuple(
        value
        for value in (str(hostname or "").strip() for hostname in hostnames)
        if value
    )


async def ensure_bertrand_gateway(kube: Kube, *, timeout: float) -> CustomObject:
    """Converge Bertrand's shared Gateway API substrate.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum convergence budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    CustomObject
        Accepted Bertrand Gateway with at least one external address.

    Raises
    ------
    OSError
        If Gateway API CRDs, Envoy Gateway acceptance, or external address
        assignment are unavailable.
    """
    deadline = Deadline.from_timeout(
        timeout,
        message="Bertrand Gateway convergence timeout must be positive",
    )
    try:
        current_class = await GATEWAY_CLASS_RESOURCE.get(
            kube,
            name=BERTRAND_GATEWAY_CLASS,
            timeout=deadline.remaining(),
        )
        _assert_managed_gateway_resource(current_class, kind="GatewayClass")
        await upsert_gateway_class(
            kube,
            name=BERTRAND_GATEWAY_CLASS,
            controller_name=ENVOY_GATEWAY_CONTROLLER,
            labels=GATEWAY_LABELS,
            timeout=deadline.remaining(),
        )
    except OSError as err:
        message = _gateway_api_error_message("upsert GatewayClass", err)
        if message is not None:
            raise OSError(message) from err
        raise
    await _wait_gateway_class_accepted(
        kube,
        deadline=deadline,
    )
    try:
        current_gateway = await GATEWAY_RESOURCE.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=BERTRAND_GATEWAY,
            timeout=deadline.remaining(),
        )
        _assert_managed_gateway_resource(current_gateway, kind="Gateway")
        await upsert_gateway(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=BERTRAND_GATEWAY,
            gateway_class=BERTRAND_GATEWAY_CLASS,
            listeners=_bertrand_gateway_listeners(),
            labels=GATEWAY_LABELS,
            timeout=deadline.remaining(),
        )
    except OSError as err:
        message = _gateway_api_error_message("upsert Gateway", err)
        if message is not None:
            raise OSError(message) from err
        raise
    return await _wait_gateway_address(kube, deadline=deadline)


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
    return is_missing_api_resource(err)


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


async def _wait_gateway_class_accepted(
    kube: Kube,
    *,
    deadline: Deadline,
) -> CustomObject:
    last: CustomObject | None = None
    while True:
        remaining = deadline.remaining()
        if remaining <= 0:
            detail = (
                f": {gateway_class_acceptance_message(last)}"
                if last is not None
                else ""
            )
            msg = (
                f"GatewayClass {BERTRAND_GATEWAY_CLASS!r} was not accepted by "
                f"Envoy Gateway controller {ENVOY_GATEWAY_CONTROLLER!r}{detail}. "
                "Install/start Envoy Gateway and ensure it watches this controller."
            )
            raise OSError(msg)
        current = await GATEWAY_CLASS_RESOURCE.get(
            kube,
            name=BERTRAND_GATEWAY_CLASS,
            timeout=remaining,
        )
        if current is not None:
            last = current
            if gateway_class_accepted(current):
                return current
        await asyncio.sleep(deadline.bounded(0.5))


async def _wait_gateway_address(kube: Kube, *, deadline: Deadline) -> CustomObject:
    while True:
        remaining = deadline.remaining()
        if remaining <= 0:
            msg = (
                f"Gateway {BERTRAND_NAMESPACE}/{BERTRAND_GATEWAY} has no external "
                "address. Configure a LoadBalancer provider such as MetalLB with "
                "`bertrand cluster network lb install` and an explicit address "
                "pool; Bertrand does not guess address pools."
            )
            raise OSError(msg)
        current = await GATEWAY_RESOURCE.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=BERTRAND_GATEWAY,
            timeout=remaining,
        )
        if current is not None and gateway_addresses(current):
            return current
        await asyncio.sleep(deadline.bounded(0.5))


def _gateway_api_error_message(action: str, err: OSError) -> str | None:
    if gateway_api_crd_missing(err):
        return (
            f"Gateway API CRDs are missing while trying to {action}. Run "
            "`bertrand init` to install Envoy Gateway and its Gateway API CRDs, "
            "or install Envoy Gateway manually before publishing Bertrand routes."
        )
    return None


def _assert_managed_gateway_resource(
    resource: CustomObject | None,
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
    if resource.namespace:
        location = f"{resource.namespace}/{resource.name}"
    msg = f"{kind} {location} exists but is not managed by Bertrand"
    raise OSError(msg)


def _condition_status(status: Mapping[str, Any], kind: str) -> str:
    for condition in _conditions(status):
        if str(condition.get("type") or "").strip() == kind:
            return str(condition.get("status") or "").strip().lower()
    return ""


def _condition_message(status: Mapping[str, Any], kind: str) -> str:
    for condition in _conditions(status):
        if str(condition.get("type") or "").strip() != kind:
            continue
        reason = str(condition.get("reason") or "").strip()
        message = str(condition.get("message") or "").strip()
        if reason and message:
            return f"{reason}: {message}"
        return reason or message
    return ""


def _conditions(status: Mapping[str, Any]) -> tuple[Mapping[str, object], ...]:
    conditions = status.get("conditions", ())
    if not isinstance(conditions, list):
        return ()
    return tuple(
        cast("Mapping[str, object]", condition)
        for condition in conditions
        if isinstance(condition, dict)
    )
