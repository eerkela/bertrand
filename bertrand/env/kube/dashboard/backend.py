"""Headlamp dashboard convergence for Bertrand's Kubernetes control plane."""

from __future__ import annotations

from collections.abc import Mapping
from types import MappingProxyType
from typing import TYPE_CHECKING

from bertrand.env.git import BERTRAND_ENV, BERTRAND_NAMESPACE, Deadline
from bertrand.env.kube.api.spec import ContainerSpec, PodTemplateSpec
from bertrand.env.kube.api.view import ServicePortView
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.rbac import (
    delete_cluster_role,
    delete_cluster_role_binding,
    delete_role,
    delete_role_binding,
    get_cluster_role,
    get_cluster_role_binding,
    get_role,
    get_role_binding,
    upsert_cluster_role,
    upsert_cluster_role_binding,
    upsert_role,
    upsert_role_binding,
)
from bertrand.env.kube.service import Service
from bertrand.env.kube.service_account import ServiceAccount

if TYPE_CHECKING:
    from bertrand.env.kube.api.client import Kube
    from bertrand.env.kube.api.spec import PolicyRuleManifest

DASHBOARD_NAME = "bertrand-headlamp"
HEADLAMP_IMAGE = "ghcr.io/headlamp-k8s/headlamp:v0.42.0"
HEADLAMP_CONTAINER_PORT = 4466
HEADLAMP_SERVICE_PORT = 80
DASHBOARD_LABEL = "bertrand.dev/dashboard"
DASHBOARD_LABEL_VALUE = "headlamp"
DASHBOARD_LABELS = MappingProxyType(
    {
        BERTRAND_ENV: "1",
        "app.kubernetes.io/name": DASHBOARD_NAME,
        "app.kubernetes.io/part-of": "bertrand",
        "app.kubernetes.io/component": "dashboard",
        DASHBOARD_LABEL: DASHBOARD_LABEL_VALUE,
    }
)
DASHBOARD_SELECTOR = MappingProxyType(
    {
        BERTRAND_ENV: "1",
        DASHBOARD_LABEL: DASHBOARD_LABEL_VALUE,
    }
)
_DASHBOARD_PORT_NAME = "http"
_WAIT_MINIMUM_REPLICAS = 1


async def ensure_dashboard_backend(kube: Kube, *, timeout: float) -> Deployment:
    """Install or update the Bertrand Headlamp dashboard backend.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum convergence budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    Deployment
        Available Headlamp Deployment.

    Raises
    ------
    TimeoutError
        If convergence cannot start before `timeout` expires.
    """
    msg = "dashboard convergence timeout must be positive"
    if timeout <= 0:
        raise TimeoutError(msg)
    deadline = Deadline.from_timeout(timeout, message=msg)

    await _assert_existing_resources(kube, timeout=deadline.remaining())

    await ServiceAccount.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=DASHBOARD_NAME,
        labels=DASHBOARD_LABELS,
        timeout=deadline.remaining(),
    )
    await upsert_role(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=DASHBOARD_NAME,
        rules=_namespace_rules(),
        labels=DASHBOARD_LABELS,
        timeout=deadline.remaining(),
    )
    await upsert_role_binding(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=DASHBOARD_NAME,
        role_name=DASHBOARD_NAME,
        service_account_name=DASHBOARD_NAME,
        service_account_namespace=BERTRAND_NAMESPACE,
        labels=DASHBOARD_LABELS,
        timeout=deadline.remaining(),
    )
    await upsert_cluster_role(
        kube,
        name=DASHBOARD_NAME,
        rules=_cluster_read_rules(),
        labels=DASHBOARD_LABELS,
        timeout=deadline.remaining(),
    )
    await upsert_cluster_role_binding(
        kube,
        name=DASHBOARD_NAME,
        role_name=DASHBOARD_NAME,
        service_account_name=DASHBOARD_NAME,
        service_account_namespace=BERTRAND_NAMESPACE,
        labels=DASHBOARD_LABELS,
        timeout=deadline.remaining(),
    )

    deployment = await Deployment.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=DASHBOARD_NAME,
        labels=DASHBOARD_LABELS,
        selector=DASHBOARD_SELECTOR,
        pod_template=_pod_template(),
        replicas=1,
        timeout=deadline.remaining(),
    )
    await Service.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=DASHBOARD_NAME,
        selector=DASHBOARD_SELECTOR,
        ports=(
            ServicePortView(
                name=_DASHBOARD_PORT_NAME,
                port=HEADLAMP_SERVICE_PORT,
                target_port=_DASHBOARD_PORT_NAME,
                protocol="TCP",
            ),
        ),
        labels=DASHBOARD_LABELS,
        timeout=deadline.remaining(),
    )
    return await deployment.wait_available(
        kube,
        minimum=_WAIT_MINIMUM_REPLICAS,
        timeout=deadline.remaining(),
    )


async def delete_dashboard_backend(kube: Kube, *, timeout: float) -> None:
    """Delete Bertrand-managed Headlamp dashboard resources.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum cleanup budget in seconds. If infinite, wait indefinitely.

    Raises
    ------
    TimeoutError
        If cleanup cannot start before `timeout` expires.
    """
    msg = "dashboard cleanup timeout must be positive"
    if timeout <= 0:
        raise TimeoutError(msg)
    deadline = Deadline.from_timeout(timeout, message=msg)

    deployment = await Deployment.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=DASHBOARD_NAME,
        timeout=deadline.remaining(),
    )
    if deployment is not None:
        _assert_managed(deployment, kind="Deployment")
        await deployment.delete(kube, timeout=deadline.remaining())

    service = await Service.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=DASHBOARD_NAME,
        timeout=deadline.remaining(),
    )
    if service is not None:
        _assert_managed(service, kind="Service")
        await service.delete(kube, timeout=deadline.remaining())

    role_binding = await get_role_binding(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=DASHBOARD_NAME,
        timeout=deadline.remaining(),
    )
    if role_binding is not None:
        _assert_managed(role_binding, kind="RoleBinding")
        await delete_role_binding(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=DASHBOARD_NAME,
            timeout=deadline.remaining(),
        )

    role = await get_role(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=DASHBOARD_NAME,
        timeout=deadline.remaining(),
    )
    if role is not None:
        _assert_managed(role, kind="Role")
        await delete_role(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=DASHBOARD_NAME,
            timeout=deadline.remaining(),
        )

    service_account = await ServiceAccount.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=DASHBOARD_NAME,
        timeout=deadline.remaining(),
    )
    if service_account is not None:
        _assert_managed(service_account, kind="ServiceAccount")
        await service_account.delete(kube, timeout=deadline.remaining())

    cluster_role_binding = await get_cluster_role_binding(
        kube,
        name=DASHBOARD_NAME,
        timeout=deadline.remaining(),
    )
    if cluster_role_binding is not None:
        _assert_managed(cluster_role_binding, kind="ClusterRoleBinding")
        await delete_cluster_role_binding(
            kube,
            name=DASHBOARD_NAME,
            timeout=deadline.remaining(),
        )

    cluster_role = await get_cluster_role(
        kube,
        name=DASHBOARD_NAME,
        timeout=deadline.remaining(),
    )
    if cluster_role is not None:
        _assert_managed(cluster_role, kind="ClusterRole")
        await delete_cluster_role(
            kube,
            name=DASHBOARD_NAME,
            timeout=deadline.remaining(),
        )


async def _assert_existing_resources(kube: Kube, *, timeout: float) -> None:
    checks: tuple[tuple[str, object | None], ...] = (
        (
            "ServiceAccount",
            await ServiceAccount.get(
                kube,
                namespace=BERTRAND_NAMESPACE,
                name=DASHBOARD_NAME,
                timeout=timeout,
            ),
        ),
        (
            "Role",
            await get_role(
                kube,
                namespace=BERTRAND_NAMESPACE,
                name=DASHBOARD_NAME,
                timeout=timeout,
            ),
        ),
        (
            "RoleBinding",
            await get_role_binding(
                kube,
                namespace=BERTRAND_NAMESPACE,
                name=DASHBOARD_NAME,
                timeout=timeout,
            ),
        ),
        (
            "ClusterRole",
            await get_cluster_role(kube, name=DASHBOARD_NAME, timeout=timeout),
        ),
        (
            "ClusterRoleBinding",
            await get_cluster_role_binding(
                kube,
                name=DASHBOARD_NAME,
                timeout=timeout,
            ),
        ),
        (
            "Deployment",
            await Deployment.get(
                kube,
                namespace=BERTRAND_NAMESPACE,
                name=DASHBOARD_NAME,
                timeout=timeout,
            ),
        ),
        (
            "Service",
            await Service.get(
                kube,
                namespace=BERTRAND_NAMESPACE,
                name=DASHBOARD_NAME,
                timeout=timeout,
            ),
        ),
    )
    for kind, resource in checks:
        if resource is not None:
            _assert_managed(resource, kind=kind)


def _pod_template() -> PodTemplateSpec:
    return PodTemplateSpec(
        containers=(
            ContainerSpec(
                name="headlamp",
                image=HEADLAMP_IMAGE,
                args=(
                    "-in-cluster",
                    "-plugins-dir=/headlamp/plugins",
                    "-watch-plugins-changes=false",
                ),
                ports=(
                    {
                        "name": _DASHBOARD_PORT_NAME,
                        "containerPort": HEADLAMP_CONTAINER_PORT,
                        "protocol": "TCP",
                    },
                ),
                readiness_probe={
                    "httpGet": {"path": "/", "port": _DASHBOARD_PORT_NAME},
                    "initialDelaySeconds": 10,
                    "timeoutSeconds": 10,
                    "failureThreshold": 6,
                },
                liveness_probe={
                    "httpGet": {"path": "/", "port": _DASHBOARD_PORT_NAME},
                    "initialDelaySeconds": 30,
                    "timeoutSeconds": 10,
                    "failureThreshold": 6,
                },
            ),
        ),
        labels=DASHBOARD_LABELS,
        service_account_name=DASHBOARD_NAME,
        automount_service_account_token=True,
        node_selector={"kubernetes.io/os": "linux"},
    )


def _namespace_rules() -> tuple[PolicyRuleManifest, ...]:
    return (
        {
            "apiGroups": ["*"],
            "resources": ["*"],
            "verbs": [
                "get",
                "list",
                "watch",
                "create",
                "update",
                "patch",
                "delete",
                "deletecollection",
            ],
        },
    )


def _cluster_read_rules() -> tuple[PolicyRuleManifest, ...]:
    read = ["get", "list", "watch"]
    return (
        {
            "apiGroups": [""],
            "resources": ["namespaces", "nodes", "persistentvolumes"],
            "verbs": read,
        },
        {
            "apiGroups": ["apiextensions.k8s.io"],
            "resources": ["customresourcedefinitions"],
            "verbs": read,
        },
        {
            "apiGroups": ["storage.k8s.io"],
            "resources": [
                "csidrivers",
                "csinodes",
                "storageclasses",
                "volumeattachments",
            ],
            "verbs": read,
        },
        {
            "apiGroups": ["gateway.networking.k8s.io"],
            "resources": ["gatewayclasses"],
            "verbs": read,
        },
        {
            "apiGroups": ["metrics.k8s.io"],
            "resources": ["nodes", "pods"],
            "verbs": read,
        },
    )


def _object_name(resource: object) -> str:
    raw_name = getattr(resource, "name", "")
    name = str(raw_name or "").strip()
    if name:
        return name
    metadata = getattr(resource, "metadata", None)
    return str(getattr(metadata, "name", "") or "").strip()


def _object_labels(resource: object) -> Mapping[str, str]:
    labels = getattr(resource, "labels", None)
    if labels is None:
        metadata = getattr(resource, "metadata", None)
        labels = getattr(metadata, "labels", None)
    if isinstance(labels, Mapping):
        return MappingProxyType({str(key): str(value) for key, value in labels.items()})
    return MappingProxyType({})


def _assert_managed(resource: object, *, kind: str) -> None:
    if _object_labels(resource).get(DASHBOARD_LABEL) == DASHBOARD_LABEL_VALUE:
        return
    msg = (
        f"{kind} {_object_name(resource)!r} exists but is not managed by Bertrand's "
        "dashboard backend"
    )
    raise OSError(msg)
