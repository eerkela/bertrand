"""Wrappers and manifest helpers for Kubernetes RBAC resources."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Literal

from kubernetes import client as kube_client

from .api.resource import (
    KubeResource,
    cluster_resource,
    namespaced_resource,
)

if TYPE_CHECKING:
    from collections.abc import Collection, Mapping

    from .api.manifest import PolicyRuleManifest

type RoleBindingRoleKind = Literal["Role", "ClusterRole"]
type _RoleKind = Literal["ClusterRole", "Role"]
type _BindingKind = Literal["ClusterRoleBinding", "RoleBinding"]


@dataclass(frozen=True)
class ClusterRoleManifest:
    """Desired state for one Kubernetes ClusterRole."""

    name: str
    rules: Collection[PolicyRuleManifest]
    labels: Mapping[str, str] | None = None
    annotations: Mapping[str, str] | None = None

    @property
    def namespace(self) -> None:
        """Return no namespace for this cluster-scoped resource."""
        return None

    def manifest(self) -> Mapping[str, object]:
        """Render this desired state as a Kubernetes manifest.

        Returns
        -------
        Mapping[str, object]
            Kubernetes ClusterRole manifest payload.
        """
        return rbac_role_manifest(
            kind="ClusterRole",
            namespace=None,
            name=self.name,
            rules=self.rules,
            labels=self.labels,
            annotations=self.annotations,
        )


@dataclass(frozen=True)
class RoleManifest:
    """Desired state for one Kubernetes Role."""

    namespace: str
    name: str
    rules: Collection[PolicyRuleManifest]
    labels: Mapping[str, str] | None = None
    annotations: Mapping[str, str] | None = None

    def manifest(self) -> Mapping[str, object]:
        """Render this desired state as a Kubernetes manifest.

        Returns
        -------
        Mapping[str, object]
            Kubernetes Role manifest payload.
        """
        return rbac_role_manifest(
            kind="Role",
            namespace=self.namespace,
            name=self.name,
            rules=self.rules,
            labels=self.labels,
            annotations=self.annotations,
        )


@dataclass(frozen=True)
class ClusterRoleBindingManifest:
    """Desired state for one Kubernetes ClusterRoleBinding."""

    name: str
    role_kind: RoleBindingRoleKind
    role_name: str
    service_account_name: str
    service_account_namespace: str
    labels: Mapping[str, str] | None = None
    annotations: Mapping[str, str] | None = None

    @property
    def namespace(self) -> None:
        """Return no namespace for this cluster-scoped resource."""
        return None

    def manifest(self) -> Mapping[str, object]:
        """Render this desired state as a Kubernetes manifest.

        Returns
        -------
        Mapping[str, object]
            Kubernetes ClusterRoleBinding manifest payload.
        """
        return rbac_service_account_binding_manifest(
            kind="ClusterRoleBinding",
            namespace=None,
            name=self.name,
            role_kind=self.role_kind,
            role_name=self.role_name,
            service_account_name=self.service_account_name,
            service_account_namespace=self.service_account_namespace,
            labels=self.labels,
            annotations=self.annotations,
        )


@dataclass(frozen=True)
class RoleBindingManifest:
    """Desired state for one Kubernetes RoleBinding."""

    namespace: str
    name: str
    role_kind: RoleBindingRoleKind
    role_name: str
    service_account_name: str
    service_account_namespace: str
    labels: Mapping[str, str] | None = None
    annotations: Mapping[str, str] | None = None

    def manifest(self) -> Mapping[str, object]:
        """Render this desired state as a Kubernetes manifest.

        Returns
        -------
        Mapping[str, object]
            Kubernetes RoleBinding manifest payload.
        """
        return rbac_service_account_binding_manifest(
            kind="RoleBinding",
            namespace=self.namespace,
            name=self.name,
            role_kind=self.role_kind,
            role_name=self.role_name,
            service_account_name=self.service_account_name,
            service_account_namespace=self.service_account_namespace,
            labels=self.labels,
            annotations=self.annotations,
        )


@cluster_resource(
    api=kube_client.RbacAuthorizationV1Api,
    read=kube_client.RbacAuthorizationV1Api.read_cluster_role,
    list=kube_client.RbacAuthorizationV1Api.list_cluster_role,
    create=kube_client.RbacAuthorizationV1Api.create_cluster_role,
    patch=kube_client.RbacAuthorizationV1Api.patch_cluster_role,
    delete=kube_client.RbacAuthorizationV1Api.delete_cluster_role,
)
@dataclass(frozen=True)
class ClusterRole(
    KubeResource[kube_client.V1ClusterRole, ClusterRoleManifest],
):
    """Wrapper around one Kubernetes ClusterRole object."""

    _obj: kube_client.V1ClusterRole


@cluster_resource(
    api=kube_client.RbacAuthorizationV1Api,
    read=kube_client.RbacAuthorizationV1Api.read_cluster_role_binding,
    list=kube_client.RbacAuthorizationV1Api.list_cluster_role_binding,
    create=kube_client.RbacAuthorizationV1Api.create_cluster_role_binding,
    patch=kube_client.RbacAuthorizationV1Api.patch_cluster_role_binding,
    delete=kube_client.RbacAuthorizationV1Api.delete_cluster_role_binding,
)
@dataclass(frozen=True)
class ClusterRoleBinding(
    KubeResource[kube_client.V1ClusterRoleBinding, ClusterRoleBindingManifest],
):
    """Wrapper around one Kubernetes ClusterRoleBinding object."""

    _obj: kube_client.V1ClusterRoleBinding


@namespaced_resource(
    api=kube_client.RbacAuthorizationV1Api,
    read=kube_client.RbacAuthorizationV1Api.read_namespaced_role,
    list=kube_client.RbacAuthorizationV1Api.list_namespaced_role,
    list_all=kube_client.RbacAuthorizationV1Api.list_role_for_all_namespaces,
    create=kube_client.RbacAuthorizationV1Api.create_namespaced_role,
    patch=kube_client.RbacAuthorizationV1Api.patch_namespaced_role,
    delete=kube_client.RbacAuthorizationV1Api.delete_namespaced_role,
)
@dataclass(frozen=True)
class Role(
    KubeResource[kube_client.V1Role, RoleManifest],
):
    """Wrapper around one Kubernetes Role object."""

    _obj: kube_client.V1Role


@namespaced_resource(
    api=kube_client.RbacAuthorizationV1Api,
    read=kube_client.RbacAuthorizationV1Api.read_namespaced_role_binding,
    list=kube_client.RbacAuthorizationV1Api.list_namespaced_role_binding,
    list_all=kube_client.RbacAuthorizationV1Api.list_role_binding_for_all_namespaces,
    create=kube_client.RbacAuthorizationV1Api.create_namespaced_role_binding,
    patch=kube_client.RbacAuthorizationV1Api.patch_namespaced_role_binding,
    delete=kube_client.RbacAuthorizationV1Api.delete_namespaced_role_binding,
)
@dataclass(frozen=True)
class RoleBinding(
    KubeResource[kube_client.V1RoleBinding, RoleBindingManifest],
):
    """Wrapper around one Kubernetes RoleBinding object."""

    _obj: kube_client.V1RoleBinding


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
