"""Helpers for converging Kubernetes RBAC resources."""

from __future__ import annotations

from typing import TYPE_CHECKING, Literal, overload

from kubernetes import client as kube_client

from .api.resource import BuiltinResource

if TYPE_CHECKING:
    from collections.abc import Collection, Mapping

    from .api.client import Kube
    from .api.spec import PolicyRuleManifest

type RoleBindingRoleKind = Literal["Role", "ClusterRole"]
type _RoleKind = Literal["ClusterRole", "Role"]
type _BindingKind = Literal["ClusterRoleBinding", "RoleBinding"]
type RbacResourceKind = _RoleKind | _BindingKind

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


@overload
async def get_rbac_resource(
    kube: Kube,
    *,
    kind: Literal["ClusterRole"],
    namespace: str | None = None,
    name: str,
    timeout: float,
) -> kube_client.V1ClusterRole | None: ...


@overload
async def get_rbac_resource(
    kube: Kube,
    *,
    kind: Literal["ClusterRoleBinding"],
    namespace: str | None = None,
    name: str,
    timeout: float,
) -> kube_client.V1ClusterRoleBinding | None: ...


@overload
async def get_rbac_resource(
    kube: Kube,
    *,
    kind: Literal["Role"],
    namespace: str | None = None,
    name: str,
    timeout: float,
) -> kube_client.V1Role | None: ...


@overload
async def get_rbac_resource(
    kube: Kube,
    *,
    kind: Literal["RoleBinding"],
    namespace: str | None = None,
    name: str,
    timeout: float,
) -> kube_client.V1RoleBinding | None: ...


async def get_rbac_resource(
    kube: Kube,
    *,
    kind: RbacResourceKind,
    namespace: str | None = None,
    name: str,
    timeout: float,
) -> (
    kube_client.V1ClusterRole
    | kube_client.V1ClusterRoleBinding
    | kube_client.V1Role
    | kube_client.V1RoleBinding
    | None
):
    """Read one Kubernetes RBAC resource by kind and identity.

    Returns
    -------
    kube_client.V1ClusterRole | kube_client.V1ClusterRoleBinding | \
            kube_client.V1Role | kube_client.V1RoleBinding | None
        RBAC payload, or `None` when absent.
    """
    if kind == "ClusterRole":
        return await _CLUSTER_ROLE_RESOURCE.get(
            kube,
            owner=_raw_payload,
            name=name,
            timeout=timeout,
        )
    if kind == "ClusterRoleBinding":
        return await _CLUSTER_ROLE_BINDING_RESOURCE.get(
            kube,
            owner=_raw_payload,
            name=name,
            timeout=timeout,
        )
    if kind == "Role":
        return await _ROLE_RESOURCE.get(
            kube,
            owner=_raw_payload,
            namespace=namespace or "",
            name=name,
            timeout=timeout,
        )
    return await _ROLE_BINDING_RESOURCE.get(
        kube,
        owner=_raw_payload,
        namespace=namespace or "",
        name=name,
        timeout=timeout,
    )


@overload
async def upsert_rbac_role(
    kube: Kube,
    *,
    kind: Literal["ClusterRole"],
    namespace: str | None = None,
    name: str,
    rules: Collection[PolicyRuleManifest],
    timeout: float,
    labels: Mapping[str, str] | None = None,
    annotations: Mapping[str, str] | None = None,
) -> kube_client.V1ClusterRole: ...


@overload
async def upsert_rbac_role(
    kube: Kube,
    *,
    kind: Literal["Role"],
    namespace: str | None = None,
    name: str,
    rules: Collection[PolicyRuleManifest],
    timeout: float,
    labels: Mapping[str, str] | None = None,
    annotations: Mapping[str, str] | None = None,
) -> kube_client.V1Role: ...


async def upsert_rbac_role(
    kube: Kube,
    *,
    kind: _RoleKind,
    namespace: str | None = None,
    name: str,
    rules: Collection[PolicyRuleManifest],
    timeout: float,
    labels: Mapping[str, str] | None = None,
    annotations: Mapping[str, str] | None = None,
) -> kube_client.V1ClusterRole | kube_client.V1Role:
    """Create or patch one Kubernetes Role or ClusterRole.

    Returns
    -------
    kube_client.V1ClusterRole | kube_client.V1Role
        Created or patched RBAC role payload.

    Raises
    ------
    OSError
        If required identity fields are empty.
    """
    name = name.strip()
    if kind == "ClusterRole":
        if not name:
            msg = "ClusterRole upsert requires non-empty name"
            raise OSError(msg)
        return await _CLUSTER_ROLE_RESOURCE.upsert(
            kube,
            owner=_raw_payload,
            name=name,
            manifest=_role_manifest(
                kind=kind,
                namespace=None,
                name=name,
                rules=rules,
                labels=labels,
                annotations=annotations,
            ),
            timeout=timeout,
        )

    namespace = (namespace or "").strip()
    if not namespace or not name:
        msg = "Role upsert requires non-empty namespace and name"
        raise OSError(msg)
    return await _ROLE_RESOURCE.upsert(
        kube,
        owner=_raw_payload,
        namespace=namespace,
        name=name,
        manifest=_role_manifest(
            kind=kind,
            namespace=namespace,
            name=name,
            rules=rules,
            labels=labels,
            annotations=annotations,
        ),
        timeout=timeout,
    )


@overload
async def upsert_rbac_binding(
    kube: Kube,
    *,
    kind: Literal["ClusterRoleBinding"],
    namespace: str | None = None,
    name: str,
    role_kind: RoleBindingRoleKind,
    role_name: str,
    service_account_name: str,
    service_account_namespace: str,
    timeout: float,
    labels: Mapping[str, str] | None = None,
    annotations: Mapping[str, str] | None = None,
) -> kube_client.V1ClusterRoleBinding: ...


@overload
async def upsert_rbac_binding(
    kube: Kube,
    *,
    kind: Literal["RoleBinding"],
    namespace: str | None = None,
    name: str,
    role_kind: RoleBindingRoleKind,
    role_name: str,
    service_account_name: str,
    service_account_namespace: str,
    timeout: float,
    labels: Mapping[str, str] | None = None,
    annotations: Mapping[str, str] | None = None,
) -> kube_client.V1RoleBinding: ...


async def upsert_rbac_binding(
    kube: Kube,
    *,
    kind: _BindingKind,
    namespace: str | None = None,
    name: str,
    role_kind: RoleBindingRoleKind,
    role_name: str,
    service_account_name: str,
    service_account_namespace: str,
    timeout: float,
    labels: Mapping[str, str] | None = None,
    annotations: Mapping[str, str] | None = None,
) -> kube_client.V1ClusterRoleBinding | kube_client.V1RoleBinding:
    """Create or patch one Kubernetes RoleBinding or ClusterRoleBinding.

    Returns
    -------
    kube_client.V1ClusterRoleBinding | kube_client.V1RoleBinding
        Created or patched RBAC binding payload.

    Raises
    ------
    OSError
        If required identity fields are empty, or `role_kind` is invalid.
    """
    name = name.strip()
    if role_kind not in ("Role", "ClusterRole"):
        msg = "RoleBinding role kind must be 'Role' or 'ClusterRole'"
        raise OSError(msg)
    if kind == "ClusterRoleBinding":
        if not name:
            msg = "ClusterRoleBinding upsert requires non-empty name"
            raise OSError(msg)
        return await _CLUSTER_ROLE_BINDING_RESOURCE.upsert(
            kube,
            owner=_raw_payload,
            name=name,
            manifest=_binding_manifest(
                kind=kind,
                namespace=None,
                name=name,
                role_kind=role_kind,
                role_name=role_name,
                service_account_name=service_account_name,
                service_account_namespace=service_account_namespace,
                labels=labels,
                annotations=annotations,
            ),
            timeout=timeout,
        )

    namespace = (namespace or "").strip()
    role_name = role_name.strip()
    service_account_name = service_account_name.strip()
    service_account_namespace = service_account_namespace.strip()
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
            kind=kind,
            namespace=namespace,
            name=name,
            role_kind=role_kind,
            role_name=role_name,
            service_account_name=service_account_name,
            service_account_namespace=service_account_namespace,
            labels=labels,
            annotations=annotations,
        ),
        timeout=timeout,
    )


async def delete_rbac_resource(
    kube: Kube,
    *,
    kind: RbacResourceKind,
    namespace: str | None = None,
    name: str,
    timeout: float,
) -> None:
    """Delete one Kubernetes RBAC resource by kind and identity."""
    if kind == "ClusterRole":
        await _CLUSTER_ROLE_RESOURCE.delete_by_name(kube, name=name, timeout=timeout)
    elif kind == "ClusterRoleBinding":
        await _CLUSTER_ROLE_BINDING_RESOURCE.delete_by_name(
            kube,
            name=name,
            timeout=timeout,
        )
    elif kind == "Role":
        await _ROLE_RESOURCE.delete_by_name(
            kube,
            namespace=namespace or "",
            name=name,
            timeout=timeout,
        )
    else:
        await _ROLE_BINDING_RESOURCE.delete_by_name(
            kube,
            namespace=namespace or "",
            name=name,
            timeout=timeout,
        )
