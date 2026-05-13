"""Wrappers for Kubernetes RBAC resources."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Literal, Self

from kubernetes import client as kube_client

from .api import (
    KubeMetadata,
    NamespacedKubeMetadata,
    PolicyRuleSpec,
    ResourceClient,
)

if TYPE_CHECKING:
    import builtins
    from collections.abc import Collection, Mapping

    from .api import Kube

type RoleBindingRoleKind = Literal["Role", "ClusterRole"]
type _RoleKind = Literal["ClusterRole", "Role"]
type _BindingKind = Literal["ClusterRoleBinding", "RoleBinding"]


def _rule_manifests(rules: Collection[PolicyRuleSpec]) -> list[dict[str, object]]:
    return [
        {
            "apiGroups": list(rule.api_groups),
            "resources": list(rule.resources),
            "verbs": list(rule.verbs),
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
    rules: Collection[PolicyRuleSpec],
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


@dataclass(frozen=True)
class ClusterRole(KubeMetadata[kube_client.V1ClusterRole]):
    """General-purpose wrapper around one Kubernetes ClusterRole object.

    Parameters
    ----------
    _obj : kube_client.V1ClusterRole
        Typed Kubernetes ClusterRole payload returned by the cluster API.
    """

    _obj: kube_client.V1ClusterRole

    @classmethod
    def _client(cls) -> ResourceClient[kube_client.V1ClusterRole, Self]:
        return ResourceClient(
            scope="cluster",
            kind="ClusterRole",
            expected=kube_client.V1ClusterRole,
            list_type=kube_client.V1ClusterRoleList,
            wrapper=lambda payload: cls(_obj=payload),
            read=lambda kube, _namespace, name, request_timeout: (
                kube.rbac.read_cluster_role(
                    name=name,
                    _request_timeout=request_timeout,
                )
            ),
            list_all=lambda kube, label_selector, field_selector, request_timeout: (
                kube.rbac.list_cluster_role(
                    label_selector=label_selector,
                    field_selector=field_selector,
                    _request_timeout=request_timeout,
                )
            ),
            create=lambda kube, _namespace, _name, manifest, request_timeout: (
                kube.rbac.create_cluster_role(
                    body=manifest,
                    _request_timeout=request_timeout,
                )
            ),
            patch=lambda kube, _namespace, name, manifest, request_timeout: (
                kube.rbac.patch_cluster_role(
                    name=name,
                    body=manifest,
                    _request_timeout=request_timeout,
                )
            ),
            delete=lambda kube, _namespace, name, request_timeout: (
                kube.rbac.delete_cluster_role(
                    name=name,
                    _request_timeout=request_timeout,
                )
            ),
        )

    @classmethod
    async def get(cls, kube: Kube, *, name: str, timeout: float) -> Self | None:
        """Read one Kubernetes ClusterRole by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            ClusterRole name to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        ClusterRole | None
            Wrapped Kubernetes ClusterRole, or `None` if it does not exist.
        """
        return await cls._client().get(kube, name=name, timeout=timeout)

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[Self]:
        """List Kubernetes ClusterRoles with optional label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.

        Returns
        -------
        list[ClusterRole]
            Wrapped ClusterRoles matching the requested filters.
        """
        return await cls._client().list(kube, timeout=timeout, labels=labels)

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        name: str,
        rules: Collection[PolicyRuleSpec],
        timeout: float,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> Self:
        """Create or patch one Kubernetes ClusterRole.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            ClusterRole name to create or patch.
        rules : Collection[PolicyRuleSpec]
            RBAC policy rules to grant.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Labels to apply to `metadata.labels`.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to `metadata.annotations`.

        Returns
        -------
        ClusterRole
            Wrapped created or patched ClusterRole.

        Raises
        ------
        OSError
            If Kubernetes create/patch fails or returns malformed data.
        """
        name = name.strip()
        if not name:
            msg = "ClusterRole upsert requires non-empty name"
            raise OSError(msg)
        manifest = _role_manifest(
            kind="ClusterRole",
            namespace=None,
            name=name,
            rules=rules,
            labels=labels,
            annotations=annotations,
        )
        return await cls._client().upsert(
            kube,
            name=name,
            manifest=manifest,
            timeout=timeout,
        )

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this ClusterRole by its metadata name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        ClusterRole | None
            Fresh wrapper for the same ClusterRole, or `None` if it no longer exists.
        """
        name = self._require_name("refresh ClusterRole")
        return await type(self).get(kube, name=name, timeout=timeout)

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this ClusterRole from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        name = self._require_name("delete ClusterRole")
        await type(self)._client().delete_by_name(kube, name=name, timeout=timeout)

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this ClusterRole is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        """
        name = self._require_name("wait for ClusterRole deletion")
        await (
            type(self)
            ._client()
            .wait_deleted(
                label=self._object_label(name),
                timeout=timeout,
                refresh=lambda remaining: self.refresh(kube, timeout=remaining),
            )
        )


@dataclass(frozen=True)
class ClusterRoleBinding(KubeMetadata[kube_client.V1ClusterRoleBinding]):
    """General-purpose wrapper around one Kubernetes ClusterRoleBinding object.

    Parameters
    ----------
    _obj : kube_client.V1ClusterRoleBinding
        Typed Kubernetes ClusterRoleBinding payload returned by the cluster API.
    """

    _obj: kube_client.V1ClusterRoleBinding

    @classmethod
    def _client(
        cls,
    ) -> ResourceClient[kube_client.V1ClusterRoleBinding, Self]:
        return ResourceClient(
            scope="cluster",
            kind="ClusterRoleBinding",
            expected=kube_client.V1ClusterRoleBinding,
            list_type=kube_client.V1ClusterRoleBindingList,
            wrapper=lambda payload: cls(_obj=payload),
            read=lambda kube, _namespace, name, request_timeout: (
                kube.rbac.read_cluster_role_binding(
                    name=name,
                    _request_timeout=request_timeout,
                )
            ),
            list_all=lambda kube, label_selector, field_selector, request_timeout: (
                kube.rbac.list_cluster_role_binding(
                    label_selector=label_selector,
                    field_selector=field_selector,
                    _request_timeout=request_timeout,
                )
            ),
            create=lambda kube, _namespace, _name, manifest, request_timeout: (
                kube.rbac.create_cluster_role_binding(
                    body=manifest,
                    _request_timeout=request_timeout,
                )
            ),
            patch=lambda kube, _namespace, name, manifest, request_timeout: (
                kube.rbac.patch_cluster_role_binding(
                    name=name,
                    body=manifest,
                    _request_timeout=request_timeout,
                )
            ),
            delete=lambda kube, _namespace, name, request_timeout: (
                kube.rbac.delete_cluster_role_binding(
                    name=name,
                    _request_timeout=request_timeout,
                )
            ),
        )

    @classmethod
    async def get(cls, kube: Kube, *, name: str, timeout: float) -> Self | None:
        """Read one Kubernetes ClusterRoleBinding by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            ClusterRoleBinding name to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        ClusterRoleBinding | None
            Wrapped Kubernetes ClusterRoleBinding, or `None` if it does not exist.
        """
        return await cls._client().get(kube, name=name, timeout=timeout)

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[Self]:
        """List Kubernetes ClusterRoleBindings with optional label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.

        Returns
        -------
        list[ClusterRoleBinding]
            Wrapped ClusterRoleBindings matching the requested filters.
        """
        return await cls._client().list(kube, timeout=timeout, labels=labels)

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        name: str,
        role_name: str,
        service_account_name: str,
        service_account_namespace: str,
        timeout: float,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> Self:
        """Create or patch one Kubernetes ClusterRoleBinding.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            ClusterRoleBinding name to create or patch.
        role_name : str
            ClusterRole referenced by the binding.
        service_account_name : str
            ServiceAccount subject name.
        service_account_namespace : str
            ServiceAccount subject namespace.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Labels to apply to `metadata.labels`.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to `metadata.annotations`.

        Returns
        -------
        ClusterRoleBinding
            Wrapped created or patched ClusterRoleBinding.

        Raises
        ------
        OSError
            If Kubernetes create/patch fails or returns malformed data.
        """
        name = name.strip()
        if not name:
            msg = "ClusterRoleBinding upsert requires non-empty name"
            raise OSError(msg)
        manifest = _binding_manifest(
            kind="ClusterRoleBinding",
            namespace=None,
            name=name,
            role_kind="ClusterRole",
            role_name=role_name,
            service_account_name=service_account_name,
            service_account_namespace=service_account_namespace,
            labels=labels,
            annotations=annotations,
        )
        return await cls._client().upsert(
            kube,
            name=name,
            manifest=manifest,
            timeout=timeout,
        )

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this ClusterRoleBinding by its metadata name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        ClusterRoleBinding | None
            Fresh wrapper for the same ClusterRoleBinding, or `None` if it no longer
            exists.
        """
        name = self._require_name("refresh ClusterRoleBinding")
        return await type(self).get(kube, name=name, timeout=timeout)

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this ClusterRoleBinding from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        name = self._require_name("delete ClusterRoleBinding")
        await type(self)._client().delete_by_name(kube, name=name, timeout=timeout)

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this ClusterRoleBinding is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        """
        name = self._require_name("wait for ClusterRoleBinding deletion")
        await (
            type(self)
            ._client()
            .wait_deleted(
                label=self._object_label(name),
                timeout=timeout,
                refresh=lambda remaining: self.refresh(kube, timeout=remaining),
            )
        )


@dataclass(frozen=True)
class Role(NamespacedKubeMetadata[kube_client.V1Role]):
    """General-purpose wrapper around one Kubernetes Role object.

    Parameters
    ----------
    _obj : kube_client.V1Role
        Typed Kubernetes Role payload returned by the cluster API.
    """

    _obj: kube_client.V1Role

    @classmethod
    def _client(cls) -> ResourceClient[kube_client.V1Role, Self]:
        return ResourceClient(
            scope="namespaced",
            kind="Role",
            expected=kube_client.V1Role,
            list_type=kube_client.V1RoleList,
            wrapper=lambda payload: cls(_obj=payload),
            read=lambda kube, namespace, name, request_timeout: (
                kube.rbac.read_namespaced_role(
                    name=name,
                    namespace=namespace,
                    _request_timeout=request_timeout,
                )
            ),
            list_all=lambda kube, label_selector, field_selector, request_timeout: (
                kube.rbac.list_role_for_all_namespaces(
                    label_selector=label_selector,
                    field_selector=field_selector,
                    _request_timeout=request_timeout,
                )
            ),
            list_namespace=lambda kube, namespace, labels, fields, timeout: (
                kube.rbac.list_namespaced_role(
                    namespace=namespace,
                    label_selector=labels,
                    field_selector=fields,
                    _request_timeout=timeout,
                )
            ),
            create=lambda kube, namespace, _name, manifest, request_timeout: (
                kube.rbac.create_namespaced_role(
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                )
            ),
            patch=lambda kube, namespace, name, manifest, request_timeout: (
                kube.rbac.patch_namespaced_role(
                    name=name,
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                )
            ),
            delete=lambda kube, namespace, name, request_timeout: (
                kube.rbac.delete_namespaced_role(
                    name=name,
                    namespace=namespace,
                    _request_timeout=request_timeout,
                )
            ),
        )

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        timeout: float,
    ) -> Self | None:
        """Read one Kubernetes Role by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the Role.
        name : str
            Role name to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Role | None
            Wrapped Kubernetes Role, or `None` if it does not exist.
        """
        return await cls._client().get(
            kube,
            namespace=namespace,
            name=name,
            timeout=timeout,
        )

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        timeout: float,
        namespaces: Collection[str] | None = None,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[Self]:
        """List Kubernetes Roles with optional namespace and label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        namespaces : Collection[str] | None, optional
            Optional namespace filters. `None` queries all namespaces.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.

        Returns
        -------
        list[Role]
            Wrapped Roles matching the requested filters.
        """
        return await cls._client().list(
            kube,
            timeout=timeout,
            namespaces=namespaces,
            labels=labels,
        )

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        rules: Collection[PolicyRuleSpec],
        timeout: float,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> Self:
        """Create or patch one Kubernetes Role.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the Role.
        name : str
            Role name to create or patch.
        rules : Collection[PolicyRuleSpec]
            RBAC policy rules to grant.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Labels to apply to `metadata.labels`.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to `metadata.annotations`.

        Returns
        -------
        Role
            Wrapped created or patched Role.

        Raises
        ------
        OSError
            If required identity fields are empty, or Kubernetes create/patch fails
            or returns malformed data.
        """
        namespace = namespace.strip()
        name = name.strip()
        if not namespace or not name:
            msg = "Role upsert requires non-empty namespace and name"
            raise OSError(msg)
        manifest = _role_manifest(
            kind="Role",
            namespace=namespace,
            name=name,
            rules=rules,
            labels=labels,
            annotations=annotations,
        )
        return await cls._client().upsert(
            kube,
            namespace=namespace,
            name=name,
            manifest=manifest,
            timeout=timeout,
        )

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this Role by its metadata namespace and name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Role | None
            Fresh wrapper for the same Role, or `None` if it no longer exists.
        """
        namespace, name = self._require_namespace_name("refresh Role")
        return await type(self).get(
            kube,
            namespace=namespace,
            timeout=timeout,
            name=name,
        )

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this Role from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        namespace, name = self._require_namespace_name("delete Role")
        await (
            type(self)
            ._client()
            .delete_by_name(
                kube,
                namespace=namespace,
                name=name,
                timeout=timeout,
            )
        )

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this Role is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        """
        namespace, name = self._require_namespace_name("wait for Role deletion")
        await (
            type(self)
            ._client()
            .wait_deleted(
                label=self._object_label(name=name, namespace=namespace),
                timeout=timeout,
                refresh=lambda remaining: self.refresh(kube, timeout=remaining),
            )
        )


@dataclass(frozen=True)
class RoleBinding(NamespacedKubeMetadata[kube_client.V1RoleBinding]):
    """General-purpose wrapper around one Kubernetes RoleBinding object.

    Parameters
    ----------
    _obj : kube_client.V1RoleBinding
        Typed Kubernetes RoleBinding payload returned by the cluster API.
    """

    _obj: kube_client.V1RoleBinding

    @classmethod
    def _client(cls) -> ResourceClient[kube_client.V1RoleBinding, Self]:
        return ResourceClient(
            scope="namespaced",
            kind="RoleBinding",
            expected=kube_client.V1RoleBinding,
            list_type=kube_client.V1RoleBindingList,
            wrapper=lambda payload: cls(_obj=payload),
            read=lambda kube, namespace, name, request_timeout: (
                kube.rbac.read_namespaced_role_binding(
                    name=name,
                    namespace=namespace,
                    _request_timeout=request_timeout,
                )
            ),
            list_all=lambda kube, label_selector, field_selector, request_timeout: (
                kube.rbac.list_role_binding_for_all_namespaces(
                    label_selector=label_selector,
                    field_selector=field_selector,
                    _request_timeout=request_timeout,
                )
            ),
            list_namespace=lambda kube, namespace, labels, fields, timeout: (
                kube.rbac.list_namespaced_role_binding(
                    namespace=namespace,
                    label_selector=labels,
                    field_selector=fields,
                    _request_timeout=timeout,
                )
            ),
            create=lambda kube, namespace, _name, manifest, request_timeout: (
                kube.rbac.create_namespaced_role_binding(
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                )
            ),
            patch=lambda kube, namespace, name, manifest, request_timeout: (
                kube.rbac.patch_namespaced_role_binding(
                    name=name,
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                )
            ),
            delete=lambda kube, namespace, name, request_timeout: (
                kube.rbac.delete_namespaced_role_binding(
                    name=name,
                    namespace=namespace,
                    _request_timeout=request_timeout,
                )
            ),
        )

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        timeout: float,
    ) -> Self | None:
        """Read one Kubernetes RoleBinding by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the RoleBinding.
        name : str
            RoleBinding name to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        RoleBinding | None
            Wrapped Kubernetes RoleBinding, or `None` if it does not exist.
        """
        return await cls._client().get(
            kube,
            namespace=namespace,
            name=name,
            timeout=timeout,
        )

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        timeout: float,
        namespaces: Collection[str] | None = None,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[Self]:
        """List Kubernetes RoleBindings with optional namespace and label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        namespaces : Collection[str] | None, optional
            Optional namespace filters. `None` queries all namespaces.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.

        Returns
        -------
        list[RoleBinding]
            Wrapped RoleBindings matching the requested filters.
        """
        return await cls._client().list(
            kube,
            timeout=timeout,
            namespaces=namespaces,
            labels=labels,
        )

    @classmethod
    async def upsert(
        cls,
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
    ) -> Self:
        """Create or patch one Kubernetes RoleBinding.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the RoleBinding.
        name : str
            RoleBinding name to create or patch.
        role_name : str
            Role or ClusterRole referenced by the binding.
        service_account_name : str
            ServiceAccount subject name.
        service_account_namespace : str
            ServiceAccount subject namespace.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        role_kind : {"Role", "ClusterRole"}, optional
            Kind of role referenced by the binding.
        labels : Mapping[str, str] | None, optional
            Labels to apply to `metadata.labels`.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to `metadata.annotations`.

        Returns
        -------
        RoleBinding
            Wrapped created or patched RoleBinding.

        Raises
        ------
        OSError
            If required identity fields are empty, role kind is invalid, or
            Kubernetes create/patch fails or returns malformed data.
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
        manifest = _binding_manifest(
            kind="RoleBinding",
            namespace=namespace,
            name=name,
            role_name=role_name,
            service_account_name=service_account_name,
            service_account_namespace=service_account_namespace,
            role_kind=role_kind,
            labels=labels,
            annotations=annotations,
        )
        return await cls._client().upsert(
            kube,
            namespace=namespace,
            name=name,
            manifest=manifest,
            timeout=timeout,
        )

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this RoleBinding by its metadata namespace and name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        RoleBinding | None
            Fresh wrapper for the same RoleBinding, or `None` if it no longer exists.
        """
        namespace, name = self._require_namespace_name("refresh RoleBinding")
        return await type(self).get(
            kube,
            namespace=namespace,
            timeout=timeout,
            name=name,
        )

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this RoleBinding from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        namespace, name = self._require_namespace_name("delete RoleBinding")
        await (
            type(self)
            ._client()
            .delete_by_name(
                kube,
                namespace=namespace,
                name=name,
                timeout=timeout,
            )
        )

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this RoleBinding is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        """
        namespace, name = self._require_namespace_name("wait for RoleBinding deletion")
        await (
            type(self)
            ._client()
            .wait_deleted(
                label=self._object_label(name=name, namespace=namespace),
                timeout=timeout,
                refresh=lambda remaining: self.refresh(kube, timeout=remaining),
            )
        )
