"""Wrappers for Kubernetes RBAC resources."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING, Literal, Self

from kubernetes import client as kube_client

from .api import PolicyRuleSpec, _label_selector

if TYPE_CHECKING:
    import builtins
    from collections.abc import Collection, Mapping

    from .api import Kube

RBAC_WAIT_POLL_INTERVAL_SECONDS = 0.5
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


@dataclass(frozen=True)
class ClusterRole:
    """General-purpose wrapper around one Kubernetes ClusterRole object.

    Parameters
    ----------
    obj : kube_client.V1ClusterRole
        Typed Kubernetes ClusterRole payload returned by the cluster API.
    """

    obj: kube_client.V1ClusterRole

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
        return cls(obj=payload)

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
            out.append(cls(obj=item))
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
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise
        else:
            if not isinstance(created, kube_client.V1ClusterRole):
                msg = (
                    f"malformed Kubernetes ClusterRole payload while creating {name!r}"
                )
                raise OSError(msg)
            return cls(obj=created)

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
        return cls(obj=patched)

    @property
    def name(self) -> str:
        """Return the ClusterRole name.

        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def labels(self) -> Mapping[str, str]:
        """Return the ClusterRole labels.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.labels`, or an empty mapping when unavailable.
        """
        metadata = self.obj.metadata
        if metadata is None or metadata.labels is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.labels)

    @property
    def annotations(self) -> Mapping[str, str]:
        """Return the ClusterRole annotations.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.annotations`, or an empty mapping when
            unavailable.
        """
        metadata = self.obj.metadata
        if metadata is None or metadata.annotations is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.annotations)

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

        Raises
        ------
        OSError
            If this wrapper does not contain enough metadata to identify the
            ClusterRole, or if Kubernetes returns malformed data.
        """
        name = self.name
        if not name:
            msg = "cannot refresh ClusterRole with missing metadata.name"
            raise OSError(msg)
        return await type(self).get(kube, name=name, timeout=timeout)

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this ClusterRole from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Raises
        ------
        OSError
            If this wrapper does not contain enough metadata to identify the
            ClusterRole, if the delete request fails, or if Kubernetes returns
            malformed data.
        """
        name = self.name
        if not name:
            msg = "cannot delete ClusterRole with missing metadata.name"
            raise OSError(msg)
        payload = await kube.run(
            lambda request_timeout: kube.rbac.delete_cluster_role(
                name=name,
                body=kube_client.V1DeleteOptions(),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete ClusterRole {name}",
        )
        if payload is not None and not isinstance(payload, kube_client.V1Status):
            msg = f"malformed Kubernetes response while deleting ClusterRole {name}"
            raise OSError(msg)

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
        OSError
            If this wrapper does not contain enough metadata to identify the
            ClusterRole, or if a refresh request returns malformed data.
        TimeoutError
            If the ClusterRole still exists when `timeout` expires.
        """
        name = self.name
        if not name:
            msg = "cannot wait for ClusterRole deletion with missing metadata.name"
            raise OSError(msg)
        if timeout <= 0:
            msg = f"timed out waiting for ClusterRole {name} deletion"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                msg = f"timed out waiting for ClusterRole {name} deletion"
                raise TimeoutError(msg)
            live = await self.refresh(kube, timeout=remaining)
            if live is None:
                return
            await asyncio.sleep(min(RBAC_WAIT_POLL_INTERVAL_SECONDS, remaining))


@dataclass(frozen=True)
class ClusterRoleBinding:
    """General-purpose wrapper around one Kubernetes ClusterRoleBinding object.

    Parameters
    ----------
    obj : kube_client.V1ClusterRoleBinding
        Typed Kubernetes ClusterRoleBinding payload returned by the cluster API.
    """

    obj: kube_client.V1ClusterRoleBinding

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
        return cls(obj=payload)

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
            out.append(cls(obj=item))
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
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise
        else:
            if not isinstance(created, kube_client.V1ClusterRoleBinding):
                msg = (
                    "malformed Kubernetes ClusterRoleBinding payload while creating "
                    f"{name!r}"
                )
                raise OSError(msg)
            return cls(obj=created)

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
        return cls(obj=patched)

    @property
    def name(self) -> str:
        """Return the ClusterRoleBinding name.

        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def labels(self) -> Mapping[str, str]:
        """Return the ClusterRoleBinding labels.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.labels`, or an empty mapping when unavailable.
        """
        metadata = self.obj.metadata
        if metadata is None or metadata.labels is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.labels)

    @property
    def annotations(self) -> Mapping[str, str]:
        """Return the ClusterRoleBinding annotations.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.annotations`, or an empty mapping when
            unavailable.
        """
        metadata = self.obj.metadata
        if metadata is None or metadata.annotations is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.annotations)

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

        Raises
        ------
        OSError
            If this wrapper does not contain enough metadata to identify the
            ClusterRoleBinding, or if Kubernetes returns malformed data.
        """
        name = self.name
        if not name:
            msg = "cannot refresh ClusterRoleBinding with missing metadata.name"
            raise OSError(msg)
        return await type(self).get(kube, name=name, timeout=timeout)

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this ClusterRoleBinding from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Raises
        ------
        OSError
            If this wrapper does not contain enough metadata to identify the
            ClusterRoleBinding, if the delete request fails, or if Kubernetes returns
            malformed data.
        """
        name = self.name
        if not name:
            msg = "cannot delete ClusterRoleBinding with missing metadata.name"
            raise OSError(msg)
        payload = await kube.run(
            lambda request_timeout: kube.rbac.delete_cluster_role_binding(
                name=name,
                body=kube_client.V1DeleteOptions(),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete ClusterRoleBinding {name}",
        )
        if payload is not None and not isinstance(payload, kube_client.V1Status):
            msg = (
                "malformed Kubernetes response while deleting ClusterRoleBinding "
                f"{name}"
            )
            raise OSError(msg)

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
        OSError
            If this wrapper does not contain enough metadata to identify the
            ClusterRoleBinding, or if a refresh request returns malformed data.
        TimeoutError
            If the ClusterRoleBinding still exists when `timeout` expires.
        """
        name = self.name
        if not name:
            msg = (
                "cannot wait for ClusterRoleBinding deletion with missing metadata.name"
            )
            raise OSError(msg)
        if timeout <= 0:
            msg = f"timed out waiting for ClusterRoleBinding {name} deletion"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                msg = f"timed out waiting for ClusterRoleBinding {name} deletion"
                raise TimeoutError(msg)
            live = await self.refresh(kube, timeout=remaining)
            if live is None:
                return
            await asyncio.sleep(min(RBAC_WAIT_POLL_INTERVAL_SECONDS, remaining))


@dataclass(frozen=True)
class Role:
    """General-purpose wrapper around one Kubernetes Role object.

    Parameters
    ----------
    obj : kube_client.V1Role
        Typed Kubernetes Role payload returned by the cluster API.
    """

    obj: kube_client.V1Role

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
        return cls(obj=payload)

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
                out.append(cls(obj=item))
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
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise
        else:
            if not isinstance(created, kube_client.V1Role):
                msg = (
                    "malformed Kubernetes Role payload while creating "
                    f"{namespace}/{name}"
                )
                raise OSError(msg)
            return cls(obj=created)

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
        return cls(obj=patched)

    @property
    def name(self) -> str:
        """Return the Role name.

        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def namespace(self) -> str:
        """Return the Role namespace.

        Returns
        -------
        str
            Trimmed `metadata.namespace`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.namespace or "").strip() if metadata is not None else ""

    @property
    def labels(self) -> Mapping[str, str]:
        """Return the Role labels.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.labels`, or an empty mapping when unavailable.
        """
        metadata = self.obj.metadata
        if metadata is None or metadata.labels is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.labels)

    @property
    def annotations(self) -> Mapping[str, str]:
        """Return the Role annotations.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.annotations`, or an empty mapping when
            unavailable.
        """
        metadata = self.obj.metadata
        if metadata is None or metadata.annotations is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.annotations)

    @property
    def resource_version(self) -> str:
        """Return the Role resource version.

        Returns
        -------
        str
            Kubernetes `metadata.resourceVersion`, or an empty string when
            unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.resource_version or "").strip() if metadata is not None else ""

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

        Raises
        ------
        OSError
            If this wrapper does not contain enough metadata to identify the Role,
            or if Kubernetes returns malformed data.
        """
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = "cannot refresh Role with missing metadata.name/namespace"
            raise OSError(msg)
        return await type(self).get(
            kube,
            namespace=namespace,
            name=name,
            timeout=timeout,
        )

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this Role from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Raises
        ------
        OSError
            If this wrapper does not contain enough metadata to identify the Role,
            if the delete request fails, or if Kubernetes returns malformed data.
        """
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = "cannot delete Role with missing metadata.name/namespace"
            raise OSError(msg)
        payload = await kube.run(
            lambda request_timeout: kube.rbac.delete_namespaced_role(
                name=name,
                namespace=namespace,
                body=kube_client.V1DeleteOptions(),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete Role {namespace}/{name}",
        )
        if payload is not None and not isinstance(payload, kube_client.V1Status):
            msg = (
                f"malformed Kubernetes response while deleting Role {namespace}/{name}"
            )
            raise OSError(msg)

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
        OSError
            If this wrapper does not contain enough metadata to identify the Role,
            or if a refresh request returns malformed data.
        TimeoutError
            If the Role still exists when `timeout` expires.
        """
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = "cannot wait for Role deletion with missing metadata.name/namespace"
            raise OSError(msg)
        if timeout <= 0:
            msg = f"timed out waiting for Role {namespace}/{name} deletion"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                msg = f"timed out waiting for Role {namespace}/{name} deletion"
                raise TimeoutError(msg)
            live = await self.refresh(kube, timeout=remaining)
            if live is None:
                return
            await asyncio.sleep(min(RBAC_WAIT_POLL_INTERVAL_SECONDS, remaining))


@dataclass(frozen=True)
class RoleBinding:
    """General-purpose wrapper around one Kubernetes RoleBinding object.

    Parameters
    ----------
    obj : kube_client.V1RoleBinding
        Typed Kubernetes RoleBinding payload returned by the cluster API.
    """

    obj: kube_client.V1RoleBinding

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
        return cls(obj=payload)

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
                out.append(cls(obj=item))
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
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise
        else:
            if not isinstance(created, kube_client.V1RoleBinding):
                msg = (
                    "malformed Kubernetes RoleBinding payload while creating "
                    f"{namespace}/{name}"
                )
                raise OSError(msg)
            return cls(obj=created)

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
        return cls(obj=patched)

    @property
    def name(self) -> str:
        """Return the RoleBinding name.

        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def namespace(self) -> str:
        """Return the RoleBinding namespace.

        Returns
        -------
        str
            Trimmed `metadata.namespace`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.namespace or "").strip() if metadata is not None else ""

    @property
    def labels(self) -> Mapping[str, str]:
        """Return the RoleBinding labels.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.labels`, or an empty mapping when unavailable.
        """
        metadata = self.obj.metadata
        if metadata is None or metadata.labels is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.labels)

    @property
    def annotations(self) -> Mapping[str, str]:
        """Return the RoleBinding annotations.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.annotations`, or an empty mapping when
            unavailable.
        """
        metadata = self.obj.metadata
        if metadata is None or metadata.annotations is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.annotations)

    @property
    def resource_version(self) -> str:
        """Return the RoleBinding resource version.

        Returns
        -------
        str
            Kubernetes `metadata.resourceVersion`, or an empty string when
            unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.resource_version or "").strip() if metadata is not None else ""

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

        Raises
        ------
        OSError
            If this wrapper does not contain enough metadata to identify the
            RoleBinding, or if Kubernetes returns malformed data.
        """
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = "cannot refresh RoleBinding with missing metadata.name/namespace"
            raise OSError(msg)
        return await type(self).get(
            kube,
            namespace=namespace,
            name=name,
            timeout=timeout,
        )

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this RoleBinding from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Raises
        ------
        OSError
            If this wrapper does not contain enough metadata to identify the
            RoleBinding, if the delete request fails, or if Kubernetes returns
            malformed data.
        """
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = "cannot delete RoleBinding with missing metadata.name/namespace"
            raise OSError(msg)
        payload = await kube.run(
            lambda request_timeout: kube.rbac.delete_namespaced_role_binding(
                name=name,
                namespace=namespace,
                body=kube_client.V1DeleteOptions(),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete RoleBinding {namespace}/{name}",
        )
        if payload is not None and not isinstance(payload, kube_client.V1Status):
            msg = (
                "malformed Kubernetes response while deleting RoleBinding "
                f"{namespace}/{name}"
            )
            raise OSError(msg)

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
        OSError
            If this wrapper does not contain enough metadata to identify the
            RoleBinding, or if a refresh request returns malformed data.
        TimeoutError
            If the RoleBinding still exists when `timeout` expires.
        """
        namespace = self.namespace
        name = self.name
        if not namespace or not name:
            msg = (
                "cannot wait for RoleBinding deletion with missing "
                "metadata.name/namespace"
            )
            raise OSError(msg)
        if timeout <= 0:
            msg = f"timed out waiting for RoleBinding {namespace}/{name} deletion"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                msg = f"timed out waiting for RoleBinding {namespace}/{name} deletion"
                raise TimeoutError(msg)
            live = await self.refresh(kube, timeout=remaining)
            if live is None:
                return
            await asyncio.sleep(min(RBAC_WAIT_POLL_INTERVAL_SECONDS, remaining))
