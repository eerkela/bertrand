"""Wrappers for Kubernetes RBAC resources."""

from __future__ import annotations

from collections.abc import Collection, Mapping
from dataclasses import dataclass
from types import MappingProxyType
from typing import Self

from kubernetes import client as kube_client

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

    def manifest(self) -> dict[str, object]:
        """Render this rule into a Kubernetes RBAC manifest fragment.

        Returns
        -------
        dict[str, object]
            Kubernetes `PolicyRule` payload.
        """
        return {
            "apiGroups": list(self.api_groups),
            "resources": list(self.resources),
            "verbs": list(self.verbs),
        }


@dataclass(frozen=True)
class ClusterRole:
    """General-purpose wrapper around one Kubernetes ClusterRole object."""

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
            "rules": [rule.manifest() for rule in rules],
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
        """Create or patch one Kubernetes ClusterRole."""
        name = name.strip()
        if not name:
            raise OSError("ClusterRole upsert requires non-empty name")
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
            if not isinstance(created, kube_client.V1ClusterRole):
                raise OSError(f"malformed Kubernetes ClusterRole payload while creating {name!r}")
            return cls(obj=created)
        except OSError as err:
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise

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
            raise OSError(f"malformed Kubernetes ClusterRole payload while patching {name!r}")
        return cls(obj=patched)

    @property
    def name(self) -> str:
        """
        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def labels(self) -> Mapping[str, str]:
        """
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
    """General-purpose wrapper around one Kubernetes ClusterRoleBinding object."""

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
        """Create or patch one Kubernetes ClusterRoleBinding."""
        name = name.strip()
        if not name:
            raise OSError("ClusterRoleBinding upsert requires non-empty name")
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
            if not isinstance(created, kube_client.V1ClusterRoleBinding):
                raise OSError(
                    f"malformed Kubernetes ClusterRoleBinding payload while creating {name!r}"
                )
            return cls(obj=created)
        except OSError as err:
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise

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
            raise OSError(
                f"malformed Kubernetes ClusterRoleBinding payload while patching {name!r}"
            )
        return cls(obj=patched)

    @property
    def name(self) -> str:
        """
        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""
