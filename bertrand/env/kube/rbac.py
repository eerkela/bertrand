"""Helpers for converging Kubernetes RBAC resources."""

from __future__ import annotations

from typing import TYPE_CHECKING, Literal

from kubernetes import client as kube_client

from .api.resource import BuiltinResource

if TYPE_CHECKING:
    from collections.abc import Collection, Mapping

    from .api.client import Kube
    from .api.spec import PolicyRuleManifest

type RoleBindingRoleKind = Literal["Role", "ClusterRole"]
type _RoleKind = Literal["ClusterRole", "Role"]
type _BindingKind = Literal["ClusterRoleBinding", "RoleBinding"]

_CLUSTER_ROLE_RESOURCE = BuiltinResource.cluster(
    api="rbac",
    kind="ClusterRole",
    slug="cluster_role",
    expected=kube_client.V1ClusterRole,
    list_type=kube_client.V1ClusterRoleList,
    create=True,
    patch=True,
    delete=True,
)
_CLUSTER_ROLE_BINDING_RESOURCE = BuiltinResource.cluster(
    api="rbac",
    kind="ClusterRoleBinding",
    slug="cluster_role_binding",
    expected=kube_client.V1ClusterRoleBinding,
    list_type=kube_client.V1ClusterRoleBindingList,
    create=True,
    patch=True,
    delete=True,
)
_ROLE_RESOURCE = BuiltinResource.namespaced(
    api="rbac",
    kind="Role",
    slug="role",
    expected=kube_client.V1Role,
    list_type=kube_client.V1RoleList,
    create=True,
    patch=True,
    delete=True,
)
_ROLE_BINDING_RESOURCE = BuiltinResource.namespaced(
    api="rbac",
    kind="RoleBinding",
    slug="role_binding",
    expected=kube_client.V1RoleBinding,
    list_type=kube_client.V1RoleBindingList,
    create=True,
    patch=True,
    delete=True,
)


def _raw_payload[T](*, _obj: T) -> T:
    return _obj


def _rule_manifests(rules: Collection[PolicyRuleManifest]) -> list[dict[str, object]]:
    return [
        {
            "apiGroups": list(rule["apiGroups"]),
            "resources": list(rule["resources"]),
            "verbs": list(rule["verbs"]),
        }
        for rule in rules
    ]


def _metadata(
    *,
    name: str,
    namespace: str | None,
    labels: Mapping[str, str] | None,
    annotations: Mapping[str, str] | None,
) -> dict[str, object]:
    metadata: dict[str, object] = {
        "name": name,
        "labels": dict(labels or {}),
        "annotations": dict(annotations or {}),
    }
    if namespace is not None:
        metadata["namespace"] = namespace
    return metadata


def _role_manifest(
    *,
    kind: _RoleKind,
    namespace: str | None,
    name: str,
    rules: Collection[PolicyRuleManifest],
    labels: Mapping[str, str] | None,
    annotations: Mapping[str, str] | None,
) -> dict[str, object]:
    return {
        "apiVersion": "rbac.authorization.k8s.io/v1",
        "kind": kind,
        "metadata": _metadata(
            name=name,
            namespace=namespace,
            labels=labels,
            annotations=annotations,
        ),
        "rules": _rule_manifests(rules),
    }


def _binding_manifest(
    *,
    kind: _BindingKind,
    namespace: str | None,
    name: str,
    role_kind: RoleBindingRoleKind,
    role_name: str,
    service_account_name: str,
    service_account_namespace: str,
    labels: Mapping[str, str] | None,
    annotations: Mapping[str, str] | None,
) -> dict[str, object]:
    return {
        "apiVersion": "rbac.authorization.k8s.io/v1",
        "kind": kind,
        "metadata": _metadata(
            name=name,
            namespace=namespace,
            labels=labels,
            annotations=annotations,
        ),
        "roleRef": {
            "apiGroup": "rbac.authorization.k8s.io",
            "kind": role_kind,
            "name": role_name,
        },
        "subjects": [
            {
                "kind": "ServiceAccount",
                "name": service_account_name,
                "namespace": service_account_namespace,
            }
        ],
    }


async def get_cluster_role(
    kube: Kube,
    *,
    name: str,
    timeout: float,
) -> kube_client.V1ClusterRole | None:
    """Read one Kubernetes ClusterRole by name.

    Returns
    -------
    kube_client.V1ClusterRole | None
        ClusterRole payload, or `None` when absent.
    """
    return await _CLUSTER_ROLE_RESOURCE.get(
        kube,
        owner=_raw_payload,
        name=name,
        timeout=timeout,
    )


async def upsert_cluster_role(
    kube: Kube,
    *,
    name: str,
    rules: Collection[PolicyRuleManifest],
    timeout: float,
    labels: Mapping[str, str] | None = None,
    annotations: Mapping[str, str] | None = None,
) -> kube_client.V1ClusterRole:
    """Create or patch one Kubernetes ClusterRole.

    Returns
    -------
    kube_client.V1ClusterRole
        Created or patched ClusterRole payload.

    Raises
    ------
    OSError
        If `name` is empty.
    """
    name = name.strip()
    if not name:
        msg = "ClusterRole upsert requires non-empty name"
        raise OSError(msg)
    return await _CLUSTER_ROLE_RESOURCE.upsert(
        kube,
        owner=_raw_payload,
        name=name,
        manifest=_role_manifest(
            kind="ClusterRole",
            namespace=None,
            name=name,
            rules=rules,
            labels=labels,
            annotations=annotations,
        ),
        timeout=timeout,
    )


async def delete_cluster_role(kube: Kube, *, name: str, timeout: float) -> None:
    """Delete one Kubernetes ClusterRole by name."""
    await _CLUSTER_ROLE_RESOURCE.delete_by_name(kube, name=name, timeout=timeout)


async def get_cluster_role_binding(
    kube: Kube,
    *,
    name: str,
    timeout: float,
) -> kube_client.V1ClusterRoleBinding | None:
    """Read one Kubernetes ClusterRoleBinding by name.

    Returns
    -------
    kube_client.V1ClusterRoleBinding | None
        ClusterRoleBinding payload, or `None` when absent.
    """
    return await _CLUSTER_ROLE_BINDING_RESOURCE.get(
        kube,
        owner=_raw_payload,
        name=name,
        timeout=timeout,
    )


async def upsert_cluster_role_binding(
    kube: Kube,
    *,
    name: str,
    role_name: str,
    service_account_name: str,
    service_account_namespace: str,
    timeout: float,
    labels: Mapping[str, str] | None = None,
    annotations: Mapping[str, str] | None = None,
) -> kube_client.V1ClusterRoleBinding:
    """Create or patch one Kubernetes ClusterRoleBinding.

    Returns
    -------
    kube_client.V1ClusterRoleBinding
        Created or patched ClusterRoleBinding payload.

    Raises
    ------
    OSError
        If `name` is empty.
    """
    name = name.strip()
    if not name:
        msg = "ClusterRoleBinding upsert requires non-empty name"
        raise OSError(msg)
    return await _CLUSTER_ROLE_BINDING_RESOURCE.upsert(
        kube,
        owner=_raw_payload,
        name=name,
        manifest=_binding_manifest(
            kind="ClusterRoleBinding",
            namespace=None,
            name=name,
            role_kind="ClusterRole",
            role_name=role_name,
            service_account_name=service_account_name,
            service_account_namespace=service_account_namespace,
            labels=labels,
            annotations=annotations,
        ),
        timeout=timeout,
    )


async def delete_cluster_role_binding(
    kube: Kube,
    *,
    name: str,
    timeout: float,
) -> None:
    """Delete one Kubernetes ClusterRoleBinding by name."""
    await _CLUSTER_ROLE_BINDING_RESOURCE.delete_by_name(
        kube,
        name=name,
        timeout=timeout,
    )


async def get_role(
    kube: Kube,
    *,
    namespace: str,
    name: str,
    timeout: float,
) -> kube_client.V1Role | None:
    """Read one Kubernetes Role by namespace/name.

    Returns
    -------
    kube_client.V1Role | None
        Role payload, or `None` when absent.
    """
    return await _ROLE_RESOURCE.get(
        kube,
        owner=_raw_payload,
        namespace=namespace,
        name=name,
        timeout=timeout,
    )


async def upsert_role(
    kube: Kube,
    *,
    namespace: str,
    name: str,
    rules: Collection[PolicyRuleManifest],
    timeout: float,
    labels: Mapping[str, str] | None = None,
    annotations: Mapping[str, str] | None = None,
) -> kube_client.V1Role:
    """Create or patch one Kubernetes Role.

    Returns
    -------
    kube_client.V1Role
        Created or patched Role payload.

    Raises
    ------
    OSError
        If `namespace` or `name` is empty.
    """
    namespace = namespace.strip()
    name = name.strip()
    if not namespace or not name:
        msg = "Role upsert requires non-empty namespace and name"
        raise OSError(msg)
    return await _ROLE_RESOURCE.upsert(
        kube,
        owner=_raw_payload,
        namespace=namespace,
        name=name,
        manifest=_role_manifest(
            kind="Role",
            namespace=namespace,
            name=name,
            rules=rules,
            labels=labels,
            annotations=annotations,
        ),
        timeout=timeout,
    )


async def delete_role(
    kube: Kube,
    *,
    namespace: str,
    name: str,
    timeout: float,
) -> None:
    """Delete one Kubernetes Role by namespace/name."""
    await _ROLE_RESOURCE.delete_by_name(
        kube,
        namespace=namespace,
        name=name,
        timeout=timeout,
    )


async def get_role_binding(
    kube: Kube,
    *,
    namespace: str,
    name: str,
    timeout: float,
) -> kube_client.V1RoleBinding | None:
    """Read one Kubernetes RoleBinding by namespace/name.

    Returns
    -------
    kube_client.V1RoleBinding | None
        RoleBinding payload, or `None` when absent.
    """
    return await _ROLE_BINDING_RESOURCE.get(
        kube,
        owner=_raw_payload,
        namespace=namespace,
        name=name,
        timeout=timeout,
    )


async def upsert_role_binding(
    kube: Kube,
    *,
    namespace: str,
    name: str,
    role_name: str,
    service_account_name: str,
    service_account_namespace: str,
    timeout: float,
    role_kind: RoleBindingRoleKind = "Role",
    labels: Mapping[str, str] | None = None,
    annotations: Mapping[str, str] | None = None,
) -> kube_client.V1RoleBinding:
    """Create or patch one Kubernetes RoleBinding.

    Returns
    -------
    kube_client.V1RoleBinding
        Created or patched RoleBinding payload.

    Raises
    ------
    OSError
        If required identity fields are empty, or `role_kind` is invalid.
    """
    namespace = namespace.strip()
    name = name.strip()
    role_name = role_name.strip()
    service_account_name = service_account_name.strip()
    service_account_namespace = service_account_namespace.strip()
    if role_kind not in ("Role", "ClusterRole"):
        msg = "RoleBinding role kind must be 'Role' or 'ClusterRole'"
        raise OSError(msg)
    if not all((namespace, name, role_name, service_account_name)):
        msg = "RoleBinding upsert requires non-empty names and namespace"
        raise OSError(msg)
    if not service_account_namespace:
        msg = "RoleBinding upsert requires non-empty service account namespace"
        raise OSError(msg)
    return await _ROLE_BINDING_RESOURCE.upsert(
        kube,
        owner=_raw_payload,
        namespace=namespace,
        name=name,
        manifest=_binding_manifest(
            kind="RoleBinding",
            namespace=namespace,
            name=name,
            role_name=role_name,
            service_account_name=service_account_name,
            service_account_namespace=service_account_namespace,
            role_kind=role_kind,
            labels=labels,
            annotations=annotations,
        ),
        timeout=timeout,
    )


async def delete_role_binding(
    kube: Kube,
    *,
    namespace: str,
    name: str,
    timeout: float,
) -> None:
    """Delete one Kubernetes RoleBinding by namespace/name."""
    await _ROLE_BINDING_RESOURCE.delete_by_name(
        kube,
        namespace=namespace,
        name=name,
        timeout=timeout,
    )
