"""Headlamp dashboard convergence for Bertrand's Kubernetes control plane."""

from __future__ import annotations

from types import MappingProxyType
from typing import TYPE_CHECKING, Protocol

from bertrand.env.git import BERTRAND_ENV, BERTRAND_NAMESPACE, Deadline
from bertrand.env.kube.api.spec import (
    ContainerPortSpec,
    ContainerSpec,
    PodTemplateSpec,
    PolicyRuleSpec,
    ProbeSpec,
    ServicePortSpec,
)
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.rbac import ClusterRole, ClusterRoleBinding, Role, RoleBinding
from bertrand.env.kube.service import Service
from bertrand.env.kube.service_account import ServiceAccount

if TYPE_CHECKING:
    from collections.abc import Mapping

    from bertrand.env.kube.api.client import Kube

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


class _ManagedResource(Protocol):
    @property
    def name(self) -> str: ...

    @property
    def labels(self) -> Mapping[str, str]: ...


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
    await Role.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=DASHBOARD_NAME,
        rules=_namespace_rules(),
        labels=DASHBOARD_LABELS,
        timeout=deadline.remaining(),
    )
    await RoleBinding.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=DASHBOARD_NAME,
        role_name=DASHBOARD_NAME,
        service_account_name=DASHBOARD_NAME,
        service_account_namespace=BERTRAND_NAMESPACE,
        labels=DASHBOARD_LABELS,
        timeout=deadline.remaining(),
    )
    await ClusterRole.upsert(
        kube,
        name=DASHBOARD_NAME,
        rules=_cluster_read_rules(),
        labels=DASHBOARD_LABELS,
        timeout=deadline.remaining(),
    )
    await ClusterRoleBinding.upsert(
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
            ServicePortSpec(
                name=_DASHBOARD_PORT_NAME,
                port=HEADLAMP_SERVICE_PORT,
                target_port=_DASHBOARD_PORT_NAME,
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

    role_binding = await RoleBinding.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=DASHBOARD_NAME,
        timeout=deadline.remaining(),
    )
    if role_binding is not None:
        _assert_managed(role_binding, kind="RoleBinding")
        await role_binding.delete(kube, timeout=deadline.remaining())

    role = await Role.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=DASHBOARD_NAME,
        timeout=deadline.remaining(),
    )
    if role is not None:
        _assert_managed(role, kind="Role")
        await role.delete(kube, timeout=deadline.remaining())

    service_account = await ServiceAccount.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=DASHBOARD_NAME,
        timeout=deadline.remaining(),
    )
    if service_account is not None:
        _assert_managed(service_account, kind="ServiceAccount")
        await service_account.delete(kube, timeout=deadline.remaining())

    cluster_role_binding = await ClusterRoleBinding.get(
        kube,
        name=DASHBOARD_NAME,
        timeout=deadline.remaining(),
    )
    if cluster_role_binding is not None:
        _assert_managed(cluster_role_binding, kind="ClusterRoleBinding")
        await cluster_role_binding.delete(kube, timeout=deadline.remaining())

    cluster_role = await ClusterRole.get(
        kube,
        name=DASHBOARD_NAME,
        timeout=deadline.remaining(),
    )
    if cluster_role is not None:
        _assert_managed(cluster_role, kind="ClusterRole")
        await cluster_role.delete(kube, timeout=deadline.remaining())


async def _assert_existing_resources(kube: Kube, *, timeout: float) -> None:
    checks: tuple[tuple[str, _ManagedResource | None], ...] = (
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
            await Role.get(
                kube,
                namespace=BERTRAND_NAMESPACE,
                name=DASHBOARD_NAME,
                timeout=timeout,
            ),
        ),
        (
            "RoleBinding",
            await RoleBinding.get(
                kube,
                namespace=BERTRAND_NAMESPACE,
                name=DASHBOARD_NAME,
                timeout=timeout,
            ),
        ),
        (
            "ClusterRole",
            await ClusterRole.get(kube, name=DASHBOARD_NAME, timeout=timeout),
        ),
        (
            "ClusterRoleBinding",
            await ClusterRoleBinding.get(kube, name=DASHBOARD_NAME, timeout=timeout),
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
                    ContainerPortSpec(
                        name=_DASHBOARD_PORT_NAME,
                        container_port=HEADLAMP_CONTAINER_PORT,
                    ),
                ),
                readiness_probe=ProbeSpec.http(
                    path="/",
                    port=_DASHBOARD_PORT_NAME,
                    initial_delay_seconds=10,
                    timeout_seconds=10,
                    failure_threshold=6,
                ),
                liveness_probe=ProbeSpec.http(
                    path="/",
                    port=_DASHBOARD_PORT_NAME,
                    initial_delay_seconds=30,
                    timeout_seconds=10,
                    failure_threshold=6,
                ),
            ),
        ),
        labels=DASHBOARD_LABELS,
        service_account_name=DASHBOARD_NAME,
        automount_service_account_token=True,
        node_selector={"kubernetes.io/os": "linux"},
    )


def _namespace_rules() -> tuple[PolicyRuleSpec, ...]:
    return (
        PolicyRuleSpec(
            api_groups=("*",),
            resources=("*",),
            verbs=(
                "get",
                "list",
                "watch",
                "create",
                "update",
                "patch",
                "delete",
                "deletecollection",
            ),
        ),
    )


def _cluster_read_rules() -> tuple[PolicyRuleSpec, ...]:
    read = ("get", "list", "watch")
    return (
        PolicyRuleSpec(
            api_groups=("",),
            resources=("namespaces", "nodes", "persistentvolumes"),
            verbs=read,
        ),
        PolicyRuleSpec(
            api_groups=("apiextensions.k8s.io",),
            resources=("customresourcedefinitions",),
            verbs=read,
        ),
        PolicyRuleSpec(
            api_groups=("storage.k8s.io",),
            resources=(
                "csidrivers",
                "csinodes",
                "storageclasses",
                "volumeattachments",
            ),
            verbs=read,
        ),
        PolicyRuleSpec(
            api_groups=("gateway.networking.k8s.io",),
            resources=("gatewayclasses",),
            verbs=read,
        ),
        PolicyRuleSpec(
            api_groups=("metrics.k8s.io",),
            resources=("nodes", "pods"),
            verbs=read,
        ),
    )


def _assert_managed(resource: _ManagedResource, *, kind: str) -> None:
    if resource.labels.get(DASHBOARD_LABEL) == DASHBOARD_LABEL_VALUE:
        return
    msg = (
        f"{kind} {resource.name!r} exists but is not managed by Bertrand's "
        "dashboard backend"
    )
    raise OSError(msg)
