"""Wrappers for Kubernetes RBAC resources."""

from __future__ import annotations

from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING, Self

from kubernetes import client as kube_client

if TYPE_CHECKING:
    from collections.abc import Collection, Mapping

    from .api import Kube


@dataclass(frozen=True)
class PolicyRuleSpec:
    """Intent description for one Kubernetes RBAC policy rule.

    Parameters
    ----------
    api_groups : Collection[str]
        API groups covered by the rule. Use `""` for the core API group.
    resources : Collection[str]
        Resource names covered by the rule.
    verbs : Collection[str]
        Verbs granted by the rule.
    """

    api_groups: Collection[str]
    resources: Collection[str]
    verbs: Collection[str]


@dataclass(frozen=True)
class ClusterRole:
    """General-purpose wrapper around one Kubernetes ClusterRole object.

    Parameters
    ----------
    obj : kube_client.V1ClusterRole
        Typed Kubernetes ClusterRole payload returned by the cluster API.
    """

    obj: kube_client.V1ClusterRole

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
            "rules": [
                {
                    "apiGroups": list(rule.api_groups),
                    "resources": list(rule.resources),
                    "verbs": list(rule.verbs),
                }
                for rule in rules
            ],
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


@dataclass(frozen=True)
class ClusterRoleBinding:
    """General-purpose wrapper around one Kubernetes ClusterRoleBinding object.

    Parameters
    ----------
    obj : kube_client.V1ClusterRoleBinding
        Typed Kubernetes ClusterRoleBinding payload returned by the cluster API.
    """

    obj: kube_client.V1ClusterRoleBinding

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
