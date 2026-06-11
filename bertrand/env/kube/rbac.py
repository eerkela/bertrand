"""Wrappers and manifest helpers for Kubernetes RBAC resources."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Literal, Self

from kubernetes import client as kube_client

from .api.resource import (
    DeclarativeResource,
    KubeResource,
    builtin_resource,
)

if TYPE_CHECKING:
    from collections.abc import Collection, Mapping

    from bertrand.env.git import Deadline

    from .api.client import Kube
    from .api.spec import PolicyRuleManifest

type RoleBindingRoleKind = Literal["Role", "ClusterRole"]
type _RoleKind = Literal["ClusterRole", "Role"]
type _BindingKind = Literal["ClusterRoleBinding", "RoleBinding"]


@builtin_resource(api="rbac", scope="cluster", endpoint="cluster_role")
@dataclass(frozen=True)
class ClusterRole(
    KubeResource[kube_client.V1ClusterRole],
    DeclarativeResource,
):
    """Wrapper around one Kubernetes ClusterRole object."""

    _obj: kube_client.V1ClusterRole

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        name: str,
        rules: Collection[PolicyRuleManifest],
        deadline: Deadline,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> Self:
        """Create or patch one ClusterRole from policy rules.

        Returns
        -------
        ClusterRole
            Wrapped created or patched ClusterRole.
        """
        manifest = rbac_role_manifest(
            kind="ClusterRole",
            namespace=None,
            name=name,
            rules=rules,
            labels=labels,
            annotations=annotations,
        )
        return await cls.upsert_manifest(
            kube,
            name=name,
            manifest=manifest,
            deadline=deadline,
        )


@builtin_resource(api="rbac", scope="cluster", endpoint="cluster_role_binding")
@dataclass(frozen=True)
class ClusterRoleBinding(
    KubeResource[kube_client.V1ClusterRoleBinding],
    DeclarativeResource,
):
    """Wrapper around one Kubernetes ClusterRoleBinding object."""

    _obj: kube_client.V1ClusterRoleBinding

    @classmethod
    async def bind_service_account(
        cls,
        kube: Kube,
        *,
        name: str,
        role_kind: RoleBindingRoleKind,
        role_name: str,
        service_account_name: str,
        service_account_namespace: str,
        deadline: Deadline,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> Self:
        """Create or patch one ClusterRoleBinding for a ServiceAccount.

        Returns
        -------
        ClusterRoleBinding
            Wrapped created or patched ClusterRoleBinding.
        """
        manifest = rbac_service_account_binding_manifest(
            kind="ClusterRoleBinding",
            namespace=None,
            name=name,
            role_kind=role_kind,
            role_name=role_name,
            service_account_name=service_account_name,
            service_account_namespace=service_account_namespace,
            labels=labels,
            annotations=annotations,
        )
        return await cls.upsert_manifest(
            kube,
            name=name,
            manifest=manifest,
            deadline=deadline,
        )


@builtin_resource(api="rbac", scope="namespaced", endpoint="role")
@dataclass(frozen=True)
class Role(
    KubeResource[kube_client.V1Role],
    DeclarativeResource,
):
    """Wrapper around one Kubernetes Role object."""

    _obj: kube_client.V1Role

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        rules: Collection[PolicyRuleManifest],
        deadline: Deadline,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> Self:
        """Create or patch one Role from policy rules.

        Returns
        -------
        Role
            Wrapped created or patched Role.
        """
        manifest = rbac_role_manifest(
            kind="Role",
            namespace=namespace,
            name=name,
            rules=rules,
            labels=labels,
            annotations=annotations,
        )
        return await cls.upsert_manifest(
            kube,
            namespace=namespace,
            name=name,
            manifest=manifest,
            deadline=deadline,
        )


@builtin_resource(api="rbac", scope="namespaced", endpoint="role_binding")
@dataclass(frozen=True)
class RoleBinding(
    KubeResource[kube_client.V1RoleBinding],
    DeclarativeResource,
):
    """Wrapper around one Kubernetes RoleBinding object."""

    _obj: kube_client.V1RoleBinding

    @classmethod
    async def bind_service_account(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        role_kind: RoleBindingRoleKind,
        role_name: str,
        service_account_name: str,
        service_account_namespace: str,
        deadline: Deadline,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> Self:
        """Create or patch one RoleBinding for a ServiceAccount.

        Returns
        -------
        RoleBinding
            Wrapped created or patched RoleBinding.
        """
        manifest = rbac_service_account_binding_manifest(
            kind="RoleBinding",
            namespace=namespace,
            name=name,
            role_kind=role_kind,
            role_name=role_name,
            service_account_name=service_account_name,
            service_account_namespace=service_account_namespace,
            labels=labels,
            annotations=annotations,
        )
        return await cls.upsert_manifest(
            kube,
            namespace=namespace,
            name=name,
            manifest=manifest,
            deadline=deadline,
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
