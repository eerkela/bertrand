"""Narrow wrappers for Gateway API resources."""

from __future__ import annotations

from typing import TYPE_CHECKING, Any, cast

from bertrand.env.kube.custom_object import (
    CustomObjectResource,
    CustomObjectWrapper,
)

if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence

    from bertrand.env.kube.api.client import Kube

GATEWAY_API_GROUP = "gateway.networking.k8s.io"
GATEWAY_API_VERSION = "v1"


class GatewayClass(CustomObjectWrapper):
    """Wrapper around one Gateway API GatewayClass.

    Parameters
    ----------
    _obj : CustomObject
        Generic custom object payload returned by Kubernetes.
    """

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        name: str,
        controller_name: str,
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> GatewayClass:
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
        return await cls.resource.upsert(
            kube,
            name=name,
            spec={"controllerName": controller_name},
            labels=labels,
            timeout=timeout,
        )

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


GatewayClass.resource = CustomObjectResource(
    group=GATEWAY_API_GROUP,
    version=GATEWAY_API_VERSION,
    kind="GatewayClass",
    plural="gatewayclasses",
    scope="cluster",
    parser=GatewayClass._from_object,
)


class Gateway(CustomObjectWrapper):
    """Wrapper around one Gateway API Gateway.

    Parameters
    ----------
    _obj : CustomObject
        Generic custom object payload returned by Kubernetes.
    """

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
    ) -> Gateway:
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
        return await cls.resource.upsert(
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


Gateway.resource = CustomObjectResource(
    group=GATEWAY_API_GROUP,
    version=GATEWAY_API_VERSION,
    kind="Gateway",
    plural="gateways",
    parser=Gateway._from_object,
)


class HTTPRoute(CustomObjectWrapper):
    """Wrapper around one Gateway API HTTPRoute.

    Parameters
    ----------
    _obj : CustomObject
        Generic custom object payload returned by Kubernetes.
    """

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
    ) -> HTTPRoute:
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
        return await cls.resource.upsert(
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

    @property
    def hostnames(self) -> tuple[str, ...]:
        """Return HTTPRoute hostnames.

        Returns
        -------
        tuple[str, ...]
            Hostnames listed under `spec.hostnames`.
        """
        hostnames = self._obj.spec.get("hostnames", ())
        if not isinstance(hostnames, list):
            return ()
        return tuple(
            value
            for value in (str(hostname or "").strip() for hostname in hostnames)
            if value
        )


HTTPRoute.resource = CustomObjectResource(
    group=GATEWAY_API_GROUP,
    version=GATEWAY_API_VERSION,
    kind="HTTPRoute",
    plural="httproutes",
    parser=HTTPRoute._from_object,
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
