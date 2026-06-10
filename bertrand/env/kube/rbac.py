"""Helpers for composing Kubernetes RBAC resources."""

from __future__ import annotations

from typing import TYPE_CHECKING, Literal

from kubernetes import client as kube_client

from .api.resource import BuiltinResource, raw_payload

if TYPE_CHECKING:
    from collections.abc import Collection, Mapping

    from .api.spec import PolicyRuleManifest

type RoleBindingRoleKind = Literal["Role", "ClusterRole"]
type _RoleKind = Literal["ClusterRole", "Role"]
type _BindingKind = Literal["ClusterRoleBinding", "RoleBinding"]

CLUSTER_ROLE_RESOURCE: BuiltinResource[
    kube_client.V1ClusterRole, kube_client.V1ClusterRole
] = BuiltinResource(
    scope="cluster",
    api="rbac",
    kind="ClusterRole",
    slug="cluster_role",
    expected=kube_client.V1ClusterRole,
    list_type=kube_client.V1ClusterRoleList,
    wrapper=raw_payload,
    can_create=True,
    can_patch=True,
    can_delete=True,
)
CLUSTER_ROLE_BINDING_RESOURCE: BuiltinResource[
    kube_client.V1ClusterRoleBinding, kube_client.V1ClusterRoleBinding
] = BuiltinResource(
    scope="cluster",
    api="rbac",
    kind="ClusterRoleBinding",
    slug="cluster_role_binding",
    expected=kube_client.V1ClusterRoleBinding,
    list_type=kube_client.V1ClusterRoleBindingList,
    wrapper=raw_payload,
    can_create=True,
    can_patch=True,
    can_delete=True,
)
ROLE_RESOURCE: BuiltinResource[kube_client.V1Role, kube_client.V1Role] = (
    BuiltinResource(
        scope="namespaced",
        api="rbac",
        kind="Role",
        slug="role",
        expected=kube_client.V1Role,
        list_type=kube_client.V1RoleList,
        wrapper=raw_payload,
        can_create=True,
        can_patch=True,
        can_delete=True,
    )
)
ROLE_BINDING_RESOURCE: BuiltinResource[
    kube_client.V1RoleBinding, kube_client.V1RoleBinding
] = BuiltinResource(
    scope="namespaced",
    api="rbac",
    kind="RoleBinding",
    slug="role_binding",
    expected=kube_client.V1RoleBinding,
    list_type=kube_client.V1RoleBindingList,
    wrapper=raw_payload,
    can_create=True,
    can_patch=True,
    can_delete=True,
)


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


def rbac_role_manifest(
    *,
    kind: _RoleKind,
    namespace: str | None,
    name: str,
    rules: Collection[PolicyRuleManifest],
    labels: Mapping[str, str] | None = None,
    annotations: Mapping[str, str] | None = None,
) -> dict[str, object]:
    """Return a Kubernetes Role or ClusterRole manifest.

    Parameters
    ----------
    kind : {"ClusterRole", "Role"}
        RBAC role kind to render.
    namespace : str | None
        Namespace for Role manifests; ignored for ClusterRole manifests.
    name : str
        Role or ClusterRole name.
    rules : Collection[PolicyRuleManifest]
        Kubernetes RBAC policy rules.
    labels : Mapping[str, str] | None, optional
        Labels to apply to `metadata.labels`.
    annotations : Mapping[str, str] | None, optional
        Annotations to apply to `metadata.annotations`.

    Returns
    -------
    dict[str, object]
        Kubernetes RBAC role manifest.

    Raises
    ------
    OSError
        If required identity fields are empty.
    """
    name = name.strip()
    if kind == "ClusterRole":
        namespace = None
        if not name:
            msg = "ClusterRole manifest requires non-empty name"
            raise OSError(msg)
    else:
        namespace = (namespace or "").strip()
        if not namespace or not name:
            msg = "Role manifest requires non-empty namespace and name"
            raise OSError(msg)
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


def rbac_service_account_binding_manifest(
    *,
    kind: _BindingKind,
    namespace: str | None,
    name: str,
    role_kind: RoleBindingRoleKind,
    role_name: str,
    service_account_name: str,
    service_account_namespace: str,
    labels: Mapping[str, str] | None = None,
    annotations: Mapping[str, str] | None = None,
) -> dict[str, object]:
    """Return a RoleBinding or ClusterRoleBinding for one ServiceAccount.

    Parameters
    ----------
    kind : {"ClusterRoleBinding", "RoleBinding"}
        RBAC binding kind to render.
    namespace : str | None
        Namespace for RoleBinding manifests; ignored for ClusterRoleBinding
        manifests.
    name : str
        Binding name.
    role_kind : {"Role", "ClusterRole"}
        Kind of the referenced role.
    role_name : str
        Referenced Role or ClusterRole name.
    service_account_name : str
        Bound ServiceAccount name.
    service_account_namespace : str
        Bound ServiceAccount namespace.
    labels : Mapping[str, str] | None, optional
        Labels to apply to `metadata.labels`.
    annotations : Mapping[str, str] | None, optional
        Annotations to apply to `metadata.annotations`.

    Returns
    -------
    dict[str, object]
        Kubernetes RBAC binding manifest.

    Raises
    ------
    OSError
        If required identity fields are empty, or `role_kind` is invalid.
    """
    name = name.strip()
    role_name = role_name.strip()
    service_account_name = service_account_name.strip()
    service_account_namespace = service_account_namespace.strip()
    if role_kind not in ("Role", "ClusterRole"):
        msg = "RBAC binding role kind must be 'Role' or 'ClusterRole'"
        raise OSError(msg)
    if kind == "ClusterRoleBinding":
        namespace = None
        if not name:
            msg = "ClusterRoleBinding manifest requires non-empty name"
            raise OSError(msg)
    else:
        namespace = (namespace or "").strip()
        if not namespace or not name:
            msg = "RoleBinding manifest requires non-empty namespace and name"
            raise OSError(msg)
    if not role_name or not service_account_name or not service_account_namespace:
        msg = "RBAC binding manifest requires non-empty role and service account names"
        raise OSError(msg)
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
