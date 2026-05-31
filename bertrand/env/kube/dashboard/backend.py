"""Headlamp dashboard convergence for Bertrand's Kubernetes control plane."""

from __future__ import annotations

import asyncio
from collections.abc import Mapping
from types import MappingProxyType
from typing import TYPE_CHECKING, cast

from bertrand.env.git import (
    BERTRAND_LABEL,
    BERTRAND_LABEL_MANAGED,
    BERTRAND_NAMESPACE,
    Deadline,
)
from bertrand.env.kube.api.spec import ContainerSpec, PodTemplateSpec
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.rbac import (
    CLUSTER_ROLE_BINDING_RESOURCE,
    CLUSTER_ROLE_RESOURCE,
    ROLE_BINDING_RESOURCE,
    ROLE_RESOURCE,
    rbac_role_manifest,
    rbac_service_account_binding_manifest,
)
from bertrand.env.kube.service import Service, ServicePortView
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
        BERTRAND_LABEL: BERTRAND_LABEL_MANAGED,
        "app.kubernetes.io/name": DASHBOARD_NAME,
        "app.kubernetes.io/part-of": "bertrand",
        "app.kubernetes.io/component": "dashboard",
        DASHBOARD_LABEL: DASHBOARD_LABEL_VALUE,
    }
)
DASHBOARD_SELECTOR = MappingProxyType(
    {
        BERTRAND_LABEL: BERTRAND_LABEL_MANAGED,
        DASHBOARD_LABEL: DASHBOARD_LABEL_VALUE,
    }
)
_DASHBOARD_PORT_NAME = "http"
_WAIT_MINIMUM_REPLICAS = 1


async def ensure_dashboard_backend(kube: Kube, *, deadline: Deadline) -> Deployment:
    """Install or update the Bertrand Headlamp dashboard backend.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    deadline : Deadline
        Maximum convergence budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    Deployment
        Available Headlamp Deployment.
    """
    await _assert_existing_resources(kube, deadline=deadline)

    await asyncio.gather(
        ServiceAccount.upsert(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=DASHBOARD_NAME,
            labels=DASHBOARD_LABELS,
            deadline=deadline,
        ),
        ROLE_RESOURCE.upsert(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=DASHBOARD_NAME,
            manifest=rbac_role_manifest(
                kind="Role",
                namespace=BERTRAND_NAMESPACE,
                name=DASHBOARD_NAME,
                rules=_namespace_rules(),
                labels=DASHBOARD_LABELS,
            ),
            deadline=deadline,
        ),
        CLUSTER_ROLE_RESOURCE.upsert(
            kube,
            name=DASHBOARD_NAME,
            manifest=rbac_role_manifest(
                kind="ClusterRole",
                namespace=None,
                name=DASHBOARD_NAME,
                rules=_cluster_read_rules(),
                labels=DASHBOARD_LABELS,
            ),
            deadline=deadline,
        ),
    )
    await asyncio.gather(
        ROLE_BINDING_RESOURCE.upsert(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=DASHBOARD_NAME,
            manifest=rbac_service_account_binding_manifest(
                kind="RoleBinding",
                namespace=BERTRAND_NAMESPACE,
                name=DASHBOARD_NAME,
                role_kind="Role",
                role_name=DASHBOARD_NAME,
                service_account_name=DASHBOARD_NAME,
                service_account_namespace=BERTRAND_NAMESPACE,
                labels=DASHBOARD_LABELS,
            ),
            deadline=deadline,
        ),
        CLUSTER_ROLE_BINDING_RESOURCE.upsert(
            kube,
            name=DASHBOARD_NAME,
            manifest=rbac_service_account_binding_manifest(
                kind="ClusterRoleBinding",
                namespace=None,
                name=DASHBOARD_NAME,
                role_kind="ClusterRole",
                role_name=DASHBOARD_NAME,
                service_account_name=DASHBOARD_NAME,
                service_account_namespace=BERTRAND_NAMESPACE,
                labels=DASHBOARD_LABELS,
            ),
            deadline=deadline,
        ),
    )

    deployment, _service = await asyncio.gather(
        Deployment.upsert(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=DASHBOARD_NAME,
            labels=DASHBOARD_LABELS,
            selector=DASHBOARD_SELECTOR,
            pod_template=_pod_template(),
            replicas=1,
            deadline=deadline,
        ),
        Service.upsert(
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
            deadline=deadline,
        ),
    )
    return await deployment.wait_available(
        kube,
        minimum=_WAIT_MINIMUM_REPLICAS,
        deadline=deadline,
    )


async def delete_dashboard_backend(kube: Kube, *, deadline: Deadline) -> None:
    """Delete Bertrand-managed Headlamp dashboard resources.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    deadline : Deadline
        Maximum cleanup budget in seconds. If infinite, wait indefinitely.
    """
    resources = await asyncio.gather(
        Deployment.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=DASHBOARD_NAME,
            deadline=deadline,
        ),
        Service.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=DASHBOARD_NAME,
            deadline=deadline,
        ),
        ROLE_BINDING_RESOURCE.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=DASHBOARD_NAME,
            deadline=deadline,
        ),
        ROLE_RESOURCE.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=DASHBOARD_NAME,
            deadline=deadline,
        ),
        ServiceAccount.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=DASHBOARD_NAME,
            deadline=deadline,
        ),
        CLUSTER_ROLE_BINDING_RESOURCE.get(
            kube,
            name=DASHBOARD_NAME,
            deadline=deadline,
        ),
        CLUSTER_ROLE_RESOURCE.get(
            kube,
            name=DASHBOARD_NAME,
            deadline=deadline,
        ),
    )
    deployment = cast("Deployment | None", resources[0])
    service = cast("Service | None", resources[1])
    role_binding = resources[2]
    role = resources[3]
    service_account = cast("ServiceAccount | None", resources[4])
    cluster_role_binding = resources[5]
    cluster_role = resources[6]
    for kind, resource in (
        ("Deployment", deployment),
        ("Service", service),
        ("RoleBinding", role_binding),
        ("Role", role),
        ("ServiceAccount", service_account),
        ("ClusterRoleBinding", cluster_role_binding),
        ("ClusterRole", cluster_role),
    ):
        if resource is not None:
            _assert_managed(resource, kind=kind)

    if deployment is not None:
        await deployment.delete(kube, deadline=deadline)
    if service is not None:
        await service.delete(kube, deadline=deadline)
    if role_binding is not None:
        await ROLE_BINDING_RESOURCE.delete_by_name(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=DASHBOARD_NAME,
            deadline=deadline,
        )
    if role is not None:
        await ROLE_RESOURCE.delete_by_name(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=DASHBOARD_NAME,
            deadline=deadline,
        )
    if service_account is not None:
        await service_account.delete(kube, deadline=deadline)
    if cluster_role_binding is not None:
        await CLUSTER_ROLE_BINDING_RESOURCE.delete_by_name(
            kube,
            name=DASHBOARD_NAME,
            deadline=deadline,
        )
    if cluster_role is not None:
        await CLUSTER_ROLE_RESOURCE.delete_by_name(
            kube,
            name=DASHBOARD_NAME,
            deadline=deadline,
        )


async def _assert_existing_resources(kube: Kube, *, deadline: Deadline) -> None:
    resources = await asyncio.gather(
        ServiceAccount.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=DASHBOARD_NAME,
            deadline=deadline,
        ),
        ROLE_RESOURCE.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=DASHBOARD_NAME,
            deadline=deadline,
        ),
        ROLE_BINDING_RESOURCE.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=DASHBOARD_NAME,
            deadline=deadline,
        ),
        CLUSTER_ROLE_RESOURCE.get(
            kube,
            name=DASHBOARD_NAME,
            deadline=deadline,
        ),
        CLUSTER_ROLE_BINDING_RESOURCE.get(
            kube,
            name=DASHBOARD_NAME,
            deadline=deadline,
        ),
        Deployment.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=DASHBOARD_NAME,
            deadline=deadline,
        ),
        Service.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=DASHBOARD_NAME,
            deadline=deadline,
        ),
    )
    for kind, resource in zip(
        (
            "ServiceAccount",
            "Role",
            "RoleBinding",
            "ClusterRole",
            "ClusterRoleBinding",
            "Deployment",
            "Service",
        ),
        resources,
        strict=True,
    ):
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
