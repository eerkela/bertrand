"""Wrappers for Kubernetes RBAC resources."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Literal, Self

from kubernetes import client as kube_client

from .api import (
    KubeMetadata,
    NamespacedKubeMetadata,
    PolicyRuleSpec,
    _label_selector,
    _validate_delete_status,
    _wait_until_deleted,
)

if TYPE_CHECKING:
    import builtins
    from collections.abc import Collection, Mapping

    from .api import Kube

type RoleBindingRoleKind = Literal["Role", "ClusterRole"]


def _rule_manifests(rules: Collection[PolicyRuleSpec]) -> list[dict[str, object]]:
    return [
        {
            "apiGroups": list(rule.api_groups),
            "resources": list(rule.resources),
            "verbs": list(rule.verbs),
        }
        for rule in rules
    ]


def _is_conflict(err: OSError) -> bool:
    detail = str(err).lower()
    return "status 409" in detail or "already exists" in detail


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

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or the API call fails.
        """
        payload = await kube.run(
            lambda request_timeout: kube.rbac.read_cluster_role(
                name=name,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to read ClusterRole {name!r}",
        )
        if payload is None:
            return None
        if not isinstance(payload, kube_client.V1ClusterRole):
            msg = f"malformed Kubernetes ClusterRole payload for {name!r}"
            raise OSError(msg)
        return cls(_obj=payload)

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

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or a list call fails.
        """
        payload = await kube.run(
            lambda request_timeout: kube.rbac.list_cluster_role(
                label_selector=_label_selector(labels),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context="failed to list ClusterRoles",
        )
        if payload is None:
            return []
        if not isinstance(payload, kube_client.V1ClusterRoleList):
            msg = "malformed Kubernetes ClusterRole list payload"
            raise OSError(msg)
        out: builtins.list[Self] = []
        for item in payload.items or []:
            if not isinstance(item, kube_client.V1ClusterRole):
                msg = "malformed Kubernetes ClusterRole entry in list payload"
                raise OSError(msg)
            out.append(cls(_obj=item))
        return out

    @staticmethod
    def _manifest(
        *,
        name: str,
        rules: Collection[PolicyRuleSpec],
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
    ) -> dict[str, object]:
        return {
            "apiVersion": "rbac.authorization.k8s.io/v1",
            "kind": "ClusterRole",
            "metadata": {
                "name": name,
                "labels": dict(labels or {}),
                "annotations": dict(annotations or {}),
            },
            "rules": _rule_manifests(rules),
        }

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
        manifest = cls._manifest(
            name=name,
            rules=rules,
            labels=labels,
            annotations=annotations,
        )
        try:
            created = await kube.run(
                lambda request_timeout: kube.rbac.create_cluster_role(
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=f"failed to create ClusterRole {name}",
            )
        except OSError as err:
            if not _is_conflict(err):
                raise
        else:
            if not isinstance(created, kube_client.V1ClusterRole):
                msg = (
                    f"malformed Kubernetes ClusterRole payload while creating {name!r}"
                )
                raise OSError(msg)
            return cls(_obj=created)

        patched = await kube.run(
            lambda request_timeout: kube.rbac.patch_cluster_role(
                name=name,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to patch ClusterRole {name}",
        )
        if not isinstance(patched, kube_client.V1ClusterRole):
            msg = f"malformed Kubernetes ClusterRole payload while patching {name!r}"
            raise OSError(msg)
        return cls(_obj=patched)

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
        payload = await kube.run(
            lambda request_timeout: kube.rbac.delete_cluster_role(
                name=name,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete ClusterRole {name}",
        )
        _validate_delete_status(payload, label=self._object_label(name))

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this ClusterRole is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        Raises
        ------
        TimeoutError
            If the ClusterRole still exists when `timeout` expires.
        """
        name = self._require_name("wait for ClusterRole deletion")
        try:
            await _wait_until_deleted(
                label=self._object_label(name),
                timeout=timeout,
                refresh=lambda remaining: self.refresh(kube, timeout=remaining),
            )
        except TimeoutError as err:
            raise TimeoutError(str(err)) from err


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

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or the API call fails.
        """
        payload = await kube.run(
            lambda request_timeout: kube.rbac.read_cluster_role_binding(
                name=name,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to read ClusterRoleBinding {name!r}",
        )
        if payload is None:
            return None
        if not isinstance(payload, kube_client.V1ClusterRoleBinding):
            msg = f"malformed Kubernetes ClusterRoleBinding payload for {name!r}"
            raise OSError(msg)
        return cls(_obj=payload)

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

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or a list call fails.
        """
        payload = await kube.run(
            lambda request_timeout: kube.rbac.list_cluster_role_binding(
                label_selector=_label_selector(labels),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context="failed to list ClusterRoleBindings",
        )
        if payload is None:
            return []
        if not isinstance(payload, kube_client.V1ClusterRoleBindingList):
            msg = "malformed Kubernetes ClusterRoleBinding list payload"
            raise OSError(msg)
        out: builtins.list[Self] = []
        for item in payload.items or []:
            if not isinstance(item, kube_client.V1ClusterRoleBinding):
                msg = "malformed Kubernetes ClusterRoleBinding entry in list payload"
                raise OSError(msg)
            out.append(cls(_obj=item))
        return out

    @staticmethod
    def _manifest(
        *,
        name: str,
        role_name: str,
        service_account_name: str,
        service_account_namespace: str,
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
    ) -> dict[str, object]:
        return {
            "apiVersion": "rbac.authorization.k8s.io/v1",
            "kind": "ClusterRoleBinding",
            "metadata": {
                "name": name,
                "labels": dict(labels or {}),
                "annotations": dict(annotations or {}),
            },
            "roleRef": {
                "apiGroup": "rbac.authorization.k8s.io",
                "kind": "ClusterRole",
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
        manifest = cls._manifest(
            name=name,
            role_name=role_name,
            service_account_name=service_account_name,
            service_account_namespace=service_account_namespace,
            labels=labels,
            annotations=annotations,
        )
        try:
            created = await kube.run(
                lambda request_timeout: kube.rbac.create_cluster_role_binding(
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=f"failed to create ClusterRoleBinding {name}",
            )
        except OSError as err:
            if not _is_conflict(err):
                raise
        else:
            if not isinstance(created, kube_client.V1ClusterRoleBinding):
                msg = (
                    "malformed Kubernetes ClusterRoleBinding payload while creating "
                    f"{name!r}"
                )
                raise OSError(msg)
            return cls(_obj=created)

        patched = await kube.run(
            lambda request_timeout: kube.rbac.patch_cluster_role_binding(
                name=name,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to patch ClusterRoleBinding {name}",
        )
        if not isinstance(patched, kube_client.V1ClusterRoleBinding):
            msg = (
                "malformed Kubernetes ClusterRoleBinding payload while patching "
                f"{name!r}"
            )
            raise OSError(msg)
        return cls(_obj=patched)

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
        payload = await kube.run(
            lambda request_timeout: kube.rbac.delete_cluster_role_binding(
                name=name,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete ClusterRoleBinding {name}",
        )
        _validate_delete_status(payload, label=self._object_label(name))

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this ClusterRoleBinding is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        Raises
        ------
        TimeoutError
            If the ClusterRoleBinding still exists when `timeout` expires.
        """
        name = self._require_name("wait for ClusterRoleBinding deletion")
        try:
            await _wait_until_deleted(
                label=self._object_label(name),
                timeout=timeout,
                refresh=lambda remaining: self.refresh(kube, timeout=remaining),
            )
        except TimeoutError as err:
            raise TimeoutError(str(err)) from err


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

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or the API call fails.
        """
        payload = await kube.run(
            lambda request_timeout: kube.rbac.read_namespaced_role(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to read Role {name!r} in namespace {namespace!r}",
        )
        if payload is None:
            return None
        if not isinstance(payload, kube_client.V1Role):
            msg = (
                f"malformed Kubernetes Role payload for {name!r} in namespace "
                f"{namespace!r}"
            )
            raise OSError(msg)
        return cls(_obj=payload)

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

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or a list call fails.
        """
        label_selector = _label_selector(labels)
        payloads: builtins.list[kube_client.V1RoleList] = []
        if namespaces is None:
            payload = await kube.run(
                lambda request_timeout: kube.rbac.list_role_for_all_namespaces(
                    label_selector=label_selector,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context="failed to list Roles across all namespaces",
            )
            if payload is not None:
                payloads.append(payload)
        else:
            normalized = {namespace.strip() for namespace in namespaces}
            normalized.discard("")
            if not normalized:
                return []
            for namespace in sorted(normalized):
                payload = await kube.run(
                    lambda request_timeout, namespace=namespace: (
                        kube.rbac.list_namespaced_role(
                            namespace=namespace,
                            label_selector=label_selector,
                            _request_timeout=request_timeout,
                        )
                    ),
                    timeout=timeout,
                    context=f"failed to list Roles in namespace {namespace!r}",
                )
                if payload is not None:
                    payloads.append(payload)

        out: builtins.list[Self] = []
        for payload in payloads:
            if not isinstance(payload, kube_client.V1RoleList):
                msg = "malformed Kubernetes Role list payload"
                raise OSError(msg)
            for item in payload.items or []:
                if not isinstance(item, kube_client.V1Role):
                    msg = "malformed Kubernetes Role entry in list payload"
                    raise OSError(msg)
                out.append(cls(_obj=item))
        return out

    @staticmethod
    def _manifest(
        *,
        namespace: str,
        name: str,
        rules: Collection[PolicyRuleSpec],
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
    ) -> dict[str, object]:
        return {
            "apiVersion": "rbac.authorization.k8s.io/v1",
            "kind": "Role",
            "metadata": {
                "name": name,
                "namespace": namespace,
                "labels": dict(labels or {}),
                "annotations": dict(annotations or {}),
            },
            "rules": _rule_manifests(rules),
        }

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
        manifest = cls._manifest(
            namespace=namespace,
            name=name,
            rules=rules,
            labels=labels,
            annotations=annotations,
        )
        try:
            created = await kube.run(
                lambda request_timeout: kube.rbac.create_namespaced_role(
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=f"failed to create Role {namespace}/{name}",
            )
        except OSError as err:
            if not _is_conflict(err):
                raise
        else:
            if not isinstance(created, kube_client.V1Role):
                msg = (
                    "malformed Kubernetes Role payload while creating "
                    f"{namespace}/{name}"
                )
                raise OSError(msg)
            return cls(_obj=created)

        patched = await kube.run(
            lambda request_timeout: kube.rbac.patch_namespaced_role(
                name=name,
                namespace=namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to patch Role {namespace}/{name}",
        )
        if not isinstance(patched, kube_client.V1Role):
            msg = f"malformed Kubernetes Role payload while patching {namespace}/{name}"
            raise OSError(msg)
        return cls(_obj=patched)

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
        payload = await kube.run(
            lambda request_timeout: kube.rbac.delete_namespaced_role(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete Role {namespace}/{name}",
        )
        _validate_delete_status(
            payload, label=self._object_label(name=name, namespace=namespace)
        )

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this Role is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        Raises
        ------
        TimeoutError
            If the Role still exists when `timeout` expires.
        """
        namespace, name = self._require_namespace_name("wait for Role deletion")
        try:
            await _wait_until_deleted(
                label=self._object_label(name=name, namespace=namespace),
                timeout=timeout,
                refresh=lambda remaining: self.refresh(kube, timeout=remaining),
            )
        except TimeoutError as err:
            raise TimeoutError(str(err)) from err


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

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or the API call fails.
        """
        payload = await kube.run(
            lambda request_timeout: kube.rbac.read_namespaced_role_binding(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to read RoleBinding {name!r} in namespace {namespace!r}",
        )
        if payload is None:
            return None
        if not isinstance(payload, kube_client.V1RoleBinding):
            msg = (
                f"malformed Kubernetes RoleBinding payload for {name!r} in namespace "
                f"{namespace!r}"
            )
            raise OSError(msg)
        return cls(_obj=payload)

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

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or a list call fails.
        """
        label_selector = _label_selector(labels)
        payloads: builtins.list[kube_client.V1RoleBindingList] = []
        if namespaces is None:
            payload = await kube.run(
                lambda request_timeout: kube.rbac.list_role_binding_for_all_namespaces(
                    label_selector=label_selector,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context="failed to list RoleBindings across all namespaces",
            )
            if payload is not None:
                payloads.append(payload)
        else:
            normalized = {namespace.strip() for namespace in namespaces}
            normalized.discard("")
            if not normalized:
                return []
            for namespace in sorted(normalized):
                payload = await kube.run(
                    lambda request_timeout, namespace=namespace: (
                        kube.rbac.list_namespaced_role_binding(
                            namespace=namespace,
                            label_selector=label_selector,
                            _request_timeout=request_timeout,
                        )
                    ),
                    timeout=timeout,
                    context=f"failed to list RoleBindings in namespace {namespace!r}",
                )
                if payload is not None:
                    payloads.append(payload)

        out: builtins.list[Self] = []
        for payload in payloads:
            if not isinstance(payload, kube_client.V1RoleBindingList):
                msg = "malformed Kubernetes RoleBinding list payload"
                raise OSError(msg)
            for item in payload.items or []:
                if not isinstance(item, kube_client.V1RoleBinding):
                    msg = "malformed Kubernetes RoleBinding entry in list payload"
                    raise OSError(msg)
                out.append(cls(_obj=item))
        return out

    @staticmethod
    def _manifest(
        *,
        namespace: str,
        name: str,
        role_name: str,
        service_account_name: str,
        service_account_namespace: str,
        role_kind: RoleBindingRoleKind,
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
    ) -> dict[str, object]:
        return {
            "apiVersion": "rbac.authorization.k8s.io/v1",
            "kind": "RoleBinding",
            "metadata": {
                "name": name,
                "namespace": namespace,
                "labels": dict(labels or {}),
                "annotations": dict(annotations or {}),
            },
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
        manifest = cls._manifest(
            namespace=namespace,
            name=name,
            role_name=role_name,
            service_account_name=service_account_name,
            service_account_namespace=service_account_namespace,
            role_kind=role_kind,
            labels=labels,
            annotations=annotations,
        )
        try:
            created = await kube.run(
                lambda request_timeout: kube.rbac.create_namespaced_role_binding(
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=f"failed to create RoleBinding {namespace}/{name}",
            )
        except OSError as err:
            if not _is_conflict(err):
                raise
        else:
            if not isinstance(created, kube_client.V1RoleBinding):
                msg = (
                    "malformed Kubernetes RoleBinding payload while creating "
                    f"{namespace}/{name}"
                )
                raise OSError(msg)
            return cls(_obj=created)

        patched = await kube.run(
            lambda request_timeout: kube.rbac.patch_namespaced_role_binding(
                name=name,
                namespace=namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to patch RoleBinding {namespace}/{name}",
        )
        if not isinstance(patched, kube_client.V1RoleBinding):
            msg = (
                "malformed Kubernetes RoleBinding payload while patching "
                f"{namespace}/{name}"
            )
            raise OSError(msg)
        return cls(_obj=patched)

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
        payload = await kube.run(
            lambda request_timeout: kube.rbac.delete_namespaced_role_binding(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete RoleBinding {namespace}/{name}",
        )
        _validate_delete_status(
            payload, label=self._object_label(name=name, namespace=namespace)
        )

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this RoleBinding is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.

        Raises
        ------
        TimeoutError
            If the RoleBinding still exists when `timeout` expires.
        """
        namespace, name = self._require_namespace_name("wait for RoleBinding deletion")
        try:
            await _wait_until_deleted(
                label=self._object_label(name=name, namespace=namespace),
                timeout=timeout,
                refresh=lambda remaining: self.refresh(kube, timeout=remaining),
            )
        except TimeoutError as err:
            raise TimeoutError(str(err)) from err
