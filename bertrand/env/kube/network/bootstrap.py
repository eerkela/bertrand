"""Cluster networking backend bootstrap for Bertrand."""

from __future__ import annotations

import asyncio
from typing import TYPE_CHECKING

from bertrand.env.git import until
from bertrand.env.kube.api.bootstrap import kubectl
from bertrand.env.kube.crd import CustomResourceDefinition
from bertrand.env.kube.deployment import Deployment

if TYPE_CHECKING:
    from bertrand.env.kube.api.client import Kube

ENVOY_GATEWAY_VERSION = "v1.7.2"
ENVOY_GATEWAY_INSTALL_URL = (
    "https://github.com/envoyproxy/gateway/releases/download/"
    f"{ENVOY_GATEWAY_VERSION}/install.yaml"
)
ENVOY_GATEWAY_NAMESPACE = "envoy-gateway-system"
ENVOY_GATEWAY_DEPLOYMENT = "envoy-gateway"
GATEWAY_API_CRDS = (
    "gatewayclasses.gateway.networking.k8s.io",
    "gateways.gateway.networking.k8s.io",
    "httproutes.gateway.networking.k8s.io",
)
NETWORK_BACKEND_WAIT_POLL_INTERVAL_SECONDS = 0.5


async def ensure_network_backend(kube: Kube, *, timeout: float) -> None:
    """Install and verify Bertrand's cluster networking backend.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum convergence budget in seconds. If infinite, wait indefinitely.

    Raises
    ------
    TimeoutError
        If convergence cannot start before `timeout` expires.
    """
    if timeout <= 0:
        msg = "network backend convergence timeout must be positive"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    await _apply_envoy_gateway(timeout=deadline - loop.time())
    for crd_name in GATEWAY_API_CRDS:
        await _wait_crd_established(
            kube,
            name=crd_name,
            timeout=deadline - loop.time(),
        )
    await _wait_envoy_gateway_available(kube, timeout=deadline - loop.time())


async def _apply_envoy_gateway(*, timeout: float) -> None:
    try:
        await kubectl(
            ["apply", "--server-side", "-f", ENVOY_GATEWAY_INSTALL_URL],
            capture_output=True,
            timeout=timeout,
        )
    except OSError as err:
        msg = (
            "failed to apply Envoy Gateway install manifest "
            f"{ENVOY_GATEWAY_INSTALL_URL!r}"
        )
        raise OSError(msg) from err


async def _wait_crd_established(
    kube: Kube,
    *,
    name: str,
    timeout: float,
) -> CustomResourceDefinition:
    async def established(remaining: float) -> CustomResourceDefinition:
        crd = await CustomResourceDefinition.get(kube, name=name, timeout=remaining)
        if crd is None:
            msg = f"Gateway API CRD {name!r} is not installed yet"
            raise TimeoutError(msg)
        if not crd.is_established:
            msg = f"Gateway API CRD {name!r} is not established yet"
            raise TimeoutError(msg)
        return crd

    try:
        return await until(
            established,
            timeout=timeout,
            interval=NETWORK_BACKEND_WAIT_POLL_INTERVAL_SECONDS,
            action=f"waiting for Gateway API CRD {name!r}",
        )
    except TimeoutError as err:
        msg = (
            f"Gateway API CRD {name!r} did not become Established. Check the Envoy "
            "Gateway install output and Kubernetes API extension controller status."
        )
        raise OSError(msg) from err


async def _wait_envoy_gateway_available(
    kube: Kube,
    *,
    timeout: float,
) -> Deployment:
    async def available(remaining: float) -> Deployment:
        deployment = await Deployment.get(
            kube,
            namespace=ENVOY_GATEWAY_NAMESPACE,
            name=ENVOY_GATEWAY_DEPLOYMENT,
            timeout=remaining,
        )
        if deployment is None:
            msg = (
                f"Envoy Gateway Deployment {ENVOY_GATEWAY_NAMESPACE}/"
                f"{ENVOY_GATEWAY_DEPLOYMENT} is not created yet"
            )
            raise TimeoutError(msg)
        if not deployment.has_available_replicas():
            msg = (
                f"Envoy Gateway Deployment {ENVOY_GATEWAY_NAMESPACE}/"
                f"{ENVOY_GATEWAY_DEPLOYMENT} is not Available yet"
            )
            raise TimeoutError(msg)
        return deployment

    try:
        return await until(
            available,
            timeout=timeout,
            interval=NETWORK_BACKEND_WAIT_POLL_INTERVAL_SECONDS,
            action=(
                f"waiting for Envoy Gateway Deployment {ENVOY_GATEWAY_NAMESPACE}/"
                f"{ENVOY_GATEWAY_DEPLOYMENT}"
            ),
        )
    except TimeoutError as err:
        msg = (
            f"Envoy Gateway Deployment {ENVOY_GATEWAY_NAMESPACE}/"
            f"{ENVOY_GATEWAY_DEPLOYMENT} did not become Available. Check the "
            "`envoy-gateway-system` namespace events and controller logs."
        )
        raise OSError(msg) from err
