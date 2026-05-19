"""Wrappers for Gateway API resources managed by Bertrand workloads."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
from typing import TYPE_CHECKING, Any, Self, cast

from bertrand.env.git import BERTRAND_ENV, BERTRAND_NAMESPACE
from bertrand.env.kube.custom_object import (
    CustomObject,
    CustomObjectClient,
    CustomObjectSpec,
)

if TYPE_CHECKING:
    import builtins
    from collections.abc import Mapping

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

_GATEWAY_CLASS_CLIENT = CustomObjectClient(
    CustomObjectSpec(
        group=GATEWAY_API_GROUP,
        version=GATEWAY_API_VERSION,
        kind="GatewayClass",
        plural="gatewayclasses",
        scope="cluster",
        labels=GATEWAY_LABELS,
    )
)
_GATEWAY_CLIENT = CustomObjectClient(
    CustomObjectSpec(
        group=GATEWAY_API_GROUP,
        version=GATEWAY_API_VERSION,
        kind="Gateway",
        plural="gateways",
        labels=GATEWAY_LABELS,
    )
)
_HTTP_ROUTE_CLIENT = CustomObjectClient(
    CustomObjectSpec(
        group=GATEWAY_API_GROUP,
        version=GATEWAY_API_VERSION,
        kind="HTTPRoute",
        plural="httproutes",
        labels={
            "app.kubernetes.io/part-of": "bertrand",
            "app.kubernetes.io/component": "workload-route",
            BERTRAND_ENV: "1",
            HTTP_ROUTE_LABEL: HTTP_ROUTE_LABEL_VALUE,
        },
    )
)


@dataclass(frozen=True)
class GatewayClass:
    """Wrapper around one Gateway API GatewayClass.

    Parameters
    ----------
    _obj : CustomObject
        Generic custom object payload returned by Kubernetes.
    """

    _obj: CustomObject

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        name: str,
        timeout: float,
    ) -> Self | None:
        """Read one GatewayClass by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            GatewayClass name to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        GatewayClass | None
            Wrapped GatewayClass, or `None` if it does not exist.
        """
        obj = await _GATEWAY_CLASS_CLIENT.get(
            kube,
            name=name,
            timeout=timeout,
        )
        return cls(obj) if obj is not None else None

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        name: str,
        controller_name: str,
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> Self:
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
            Labels to merge over Bertrand's Gateway labels.

        Returns
        -------
        GatewayClass
            Wrapped created or patched GatewayClass.
        """
        obj = await _GATEWAY_CLASS_CLIENT.upsert(
            kube,
            name=name,
            spec={"controllerName": controller_name},
            labels=labels,
            timeout=timeout,
        )
        return cls(obj)

    @property
    def name(self) -> str:
        """Return the GatewayClass name.

        Returns
        -------
        str
            Kubernetes `metadata.name`.
        """
        return self._obj.name

    @property
    def labels(self) -> Mapping[str, str]:
        """Return GatewayClass labels.

        Returns
        -------
        Mapping[str, str]
            Read-only labels applied to the GatewayClass.
        """
        return self._obj.labels

    @property
    def accepted(self) -> bool:
        """Return whether the GatewayClass is accepted by its controller.

        Returns
        -------
        bool
            ``True`` when the GatewayClass has `Accepted=True`.
        """
        return _condition_status(self._obj.status, "Accepted") == "true"

    @property
    def acceptance_message(self) -> str:
        """Return a diagnostic message for GatewayClass acceptance.

        Returns
        -------
        str
            Reason/message from the `Accepted` condition, or an empty string.
        """
        return _condition_message(self._obj.status, "Accepted")


@dataclass(frozen=True)
class Gateway:
    """Wrapper around one Gateway API Gateway.

    Parameters
    ----------
    _obj : CustomObject
        Generic custom object payload returned by Kubernetes.
    """

    _obj: CustomObject

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        timeout: float,
    ) -> Self | None:
        """Read one Gateway by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the Gateway.
        name : str
            Gateway name to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Gateway | None
            Wrapped Gateway, or `None` if it does not exist.
        """
        obj = await _GATEWAY_CLIENT.get(
            kube,
            namespace=namespace,
            name=name,
            timeout=timeout,
        )
        return cls(obj) if obj is not None else None

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        gateway_class: str,
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> Self:
        """Create or patch one Bertrand-managed Gateway.

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
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Labels to merge over Bertrand's Gateway labels.

        Returns
        -------
        Gateway
            Wrapped created or patched Gateway.
        """
        obj = await _GATEWAY_CLIENT.upsert(
            kube,
            namespace=namespace,
            name=name,
            spec={
                "gatewayClassName": gateway_class,
                "listeners": [
                    {
                        "name": BERTRAND_GATEWAY_LISTENER,
                        "protocol": "HTTP",
                        "port": BERTRAND_GATEWAY_PORT,
                        "allowedRoutes": {"namespaces": {"from": "Same"}},
                    }
                ],
            },
            labels=labels,
            timeout=timeout,
        )
        return cls(obj)

    @property
    def name(self) -> str:
        """Return the Gateway name.

        Returns
        -------
        str
            Kubernetes `metadata.name`.
        """
        return self._obj.name

    @property
    def namespace(self) -> str:
        """Return the Gateway namespace.

        Returns
        -------
        str
            Kubernetes `metadata.namespace`.
        """
        return self._obj.namespace

    @property
    def labels(self) -> Mapping[str, str]:
        """Return Gateway labels.

        Returns
        -------
        Mapping[str, str]
            Read-only labels applied to the Gateway.
        """
        return self._obj.labels

    @property
    def addresses(self) -> tuple[str, ...]:
        """Return Gateway external addresses.

        Returns
        -------
        tuple[str, ...]
            Non-empty values from `status.addresses`.
        """
        addresses = self._obj.status.get("addresses", ())
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


@dataclass(frozen=True)
class HTTPRoute:
    """Wrapper around one Gateway API HTTPRoute.

    Parameters
    ----------
    _obj : CustomObject
        Generic custom object payload returned by Kubernetes.
    """

    _obj: CustomObject

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        timeout: float,
    ) -> Self | None:
        """Read one HTTPRoute by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the HTTPRoute.
        name : str
            HTTPRoute name to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        HTTPRoute | None
            Wrapped HTTPRoute, or `None` if it does not exist.
        """
        obj = await _HTTP_ROUTE_CLIENT.get(
            kube,
            namespace=namespace,
            name=name,
            timeout=timeout,
        )
        return cls(obj) if obj is not None else None

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        namespace: str,
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[Self]:
        """List HTTPRoutes with optional label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the HTTPRoutes.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.

        Returns
        -------
        list[HTTPRoute]
            Wrapped HTTPRoutes matching the requested filters.
        """
        objects = await _HTTP_ROUTE_CLIENT.list(
            kube,
            namespace=namespace,
            labels=labels,
            timeout=timeout,
        )
        return [cls(obj) for obj in objects]

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        host: str,
        path: str,
        service: str,
        service_port: int,
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> Self:
        """Create or patch one Bertrand-managed HTTPRoute.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the HTTPRoute.
        name : str
            HTTPRoute name to create or patch.
        host : str
            External hostname matched by the route.
        path : str
            HTTP path prefix matched by the route.
        service : str
            Backend Kubernetes Service name.
        service_port : int
            Backend Service port number.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Labels to merge over Bertrand's HTTPRoute labels.

        Returns
        -------
        HTTPRoute
            Wrapped created or patched HTTPRoute.
        """
        obj = await _HTTP_ROUTE_CLIENT.upsert(
            kube,
            namespace=namespace,
            name=name,
            spec={
                "parentRefs": [
                    {
                        "name": BERTRAND_GATEWAY,
                        "namespace": BERTRAND_NAMESPACE,
                        "sectionName": BERTRAND_GATEWAY_LISTENER,
                    }
                ],
                "hostnames": [host],
                "rules": [
                    {
                        "matches": [
                            {
                                "path": {
                                    "type": "PathPrefix",
                                    "value": path,
                                }
                            }
                        ],
                        "backendRefs": [
                            {
                                "kind": "Service",
                                "name": service,
                                "port": service_port,
                            }
                        ],
                    }
                ],
            },
            labels=labels,
            timeout=timeout,
        )
        return cls(obj)

    @property
    def name(self) -> str:
        """Return the HTTPRoute name.

        Returns
        -------
        str
            Kubernetes `metadata.name`.
        """
        return self._obj.name

    @property
    def namespace(self) -> str:
        """Return the HTTPRoute namespace.

        Returns
        -------
        str
            Kubernetes `metadata.namespace`.
        """
        return self._obj.namespace

    @property
    def labels(self) -> Mapping[str, str]:
        """Return HTTPRoute labels.

        Returns
        -------
        Mapping[str, str]
            Read-only labels applied to the HTTPRoute.
        """
        return self._obj.labels

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this HTTPRoute from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        await _HTTP_ROUTE_CLIENT.delete_by_name(
            kube,
            namespace=self.namespace,
            name=self.name,
            timeout=timeout,
        )


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
                "address. Configure a LoadBalancer provider such as MetalLB; "
                "Bertrand does not auto-configure address pools."
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
            f"Gateway API CRDs are missing while trying to {action}. Install Envoy "
            "Gateway and its Gateway API CRDs before publishing Bertrand routes."
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
