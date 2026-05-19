"""Narrow wrappers for Gateway API resources."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Any, Self, cast

from bertrand.env.kube.custom_object import (
    CustomObject,
    CustomObjectClient,
    CustomObjectSpec,
)

if TYPE_CHECKING:
    import builtins
    from collections.abc import Mapping, Sequence

    from bertrand.env.kube.api.client import Kube

GATEWAY_API_GROUP = "gateway.networking.k8s.io"
GATEWAY_API_VERSION = "v1"

_GATEWAY_CLASS_CLIENT = CustomObjectClient(
    CustomObjectSpec(
        group=GATEWAY_API_GROUP,
        version=GATEWAY_API_VERSION,
        kind="GatewayClass",
        plural="gatewayclasses",
        scope="cluster",
    )
)
_GATEWAY_CLIENT = CustomObjectClient(
    CustomObjectSpec(
        group=GATEWAY_API_GROUP,
        version=GATEWAY_API_VERSION,
        kind="Gateway",
        plural="gateways",
    )
)
_HTTP_ROUTE_CLIENT = CustomObjectClient(
    CustomObjectSpec(
        group=GATEWAY_API_GROUP,
        version=GATEWAY_API_VERSION,
        kind="HTTPRoute",
        plural="httproutes",
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
            Labels to apply to the GatewayClass.

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

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this GatewayClass from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        await _GATEWAY_CLASS_CLIENT.delete_by_name(
            kube,
            name=self.name,
            timeout=timeout,
        )


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
        listeners: Sequence[Mapping[str, object]],
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> Self:
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
        Gateway
            Wrapped created or patched Gateway.
        """
        obj = await _GATEWAY_CLIENT.upsert(
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

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this Gateway from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        await _GATEWAY_CLIENT.delete_by_name(
            kube,
            namespace=self.namespace,
            name=self.name,
            timeout=timeout,
        )


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
        parent_refs: Sequence[Mapping[str, object]],
        hostnames: Sequence[str],
        rules: Sequence[Mapping[str, object]],
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> Self:
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
        HTTPRoute
            Wrapped created or patched HTTPRoute.
        """
        obj = await _HTTP_ROUTE_CLIENT.upsert(
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
