"""Wrappers for the Kubernetes NetworkPolicy API."""

from __future__ import annotations

from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING, Literal, cast

import kubernetes

from .api.resource import (
    KubeResource,
    namespaced_resource,
)

if TYPE_CHECKING:
    from collections.abc import Collection, Mapping


type NetworkPolicyType = Literal["Ingress", "Egress"]

_POLICY_TYPES = frozenset({"Ingress", "Egress"})


def _policy_types(
    policy_types: Collection[NetworkPolicyType],
) -> tuple[NetworkPolicyType, ...]:
    result: list[NetworkPolicyType] = []
    for policy_type in policy_types:
        value = str(policy_type).strip()
        if value not in _POLICY_TYPES:
            msg = f"unsupported NetworkPolicy policy type: {value!r}"
            raise ValueError(msg)
        if value not in result:
            result.append(cast("NetworkPolicyType", value))
    if not result:
        msg = "NetworkPolicy requires at least one policy type"
        raise ValueError(msg)
    return tuple(result)


@dataclass(frozen=True)
class NetworkPolicyManifest:
    """Desired state for one Kubernetes NetworkPolicy."""

    namespace: str
    name: str
    pod_selector: Mapping[str, str]
    policy_types: Collection[NetworkPolicyType] = ("Ingress",)
    ingress: Collection[Mapping[str, object]] | None = None
    egress: Collection[Mapping[str, object]] | None = None
    labels: Mapping[str, str] | None = None
    annotations: Mapping[str, str] | None = None

    def manifest(self) -> Mapping[str, object]:
        """Render this desired state as a Kubernetes manifest.

        Returns
        -------
        Mapping[str, object]
            Kubernetes NetworkPolicy manifest payload.

        Raises
        ------
        OSError
            If the pod selector is empty.
        """
        if not self.pod_selector:
            msg = "NetworkPolicy requires a non-empty pod selector"
            raise OSError(msg)
        normalized_types = _policy_types(self.policy_types)
        spec: dict[str, object] = {
            "podSelector": {"matchLabels": dict(self.pod_selector)},
            "policyTypes": list(normalized_types),
        }
        if "Ingress" in normalized_types:
            spec["ingress"] = [dict(rule) for rule in self.ingress or ()]
        if "Egress" in normalized_types:
            spec["egress"] = [dict(rule) for rule in self.egress or ()]
        return {
            "apiVersion": "networking.k8s.io/v1",
            "kind": "NetworkPolicy",
            "metadata": {
                "name": self.name,
                "namespace": self.namespace,
                "labels": dict(self.labels or {}),
                "annotations": dict(self.annotations or {}),
            },
            "spec": spec,
        }


@namespaced_resource(
    api=kubernetes.client.NetworkingV1Api,
    payload=kubernetes.client.V1NetworkPolicy,
    read=kubernetes.client.NetworkingV1Api.read_namespaced_network_policy,
    list=kubernetes.client.NetworkingV1Api.list_namespaced_network_policy,
    list_all=kubernetes.client.NetworkingV1Api.list_network_policy_for_all_namespaces,
    create=kubernetes.client.NetworkingV1Api.create_namespaced_network_policy,
    patch=kubernetes.client.NetworkingV1Api.patch_namespaced_network_policy,
    delete=kubernetes.client.NetworkingV1Api.delete_namespaced_network_policy,
)
@dataclass(frozen=True)
class NetworkPolicy(
    KubeResource[kubernetes.client.V1NetworkPolicy, NetworkPolicyManifest],
):
    """General-purpose wrapper around one Kubernetes NetworkPolicy object.

    Parameters
    ----------
    _obj : kubernetes.client.V1NetworkPolicy
        Typed Kubernetes NetworkPolicy payload returned by the cluster API.

    Notes
    -----
    The public convergence API accepts intent-level selectors and policy direction
    flags while keeping raw Kubernetes manifests internal to this module.
    """

    _obj: kubernetes.client.V1NetworkPolicy

    @property
    def pod_selector(self) -> Mapping[str, str]:
        """Return the NetworkPolicy pod selector.

        Returns
        -------
        Mapping[str, str]
            Read-only view of `spec.podSelector.matchLabels`, or an empty mapping
            when unavailable.
        """
        spec = self._obj.spec
        if (
            spec is None
            or spec.pod_selector is None
            or spec.pod_selector.match_labels is None
        ):
            return MappingProxyType({})
        return MappingProxyType(spec.pod_selector.match_labels)

    @property
    def policy_types(self) -> tuple[NetworkPolicyType, ...]:
        """Return the policy directions governed by this NetworkPolicy.

        Returns
        -------
        tuple[NetworkPolicyType, ...]
            Immutable snapshot of `spec.policyTypes`.
        """
        spec = self._obj.spec
        if spec is None:
            return ()
        return tuple(
            cast("NetworkPolicyType", item)
            for item in spec.policy_types or ()
            if item in _POLICY_TYPES
        )

    def selects(self, selector: Mapping[str, str]) -> bool:
        """Return whether this NetworkPolicy selects exactly the expected Pods.

        Parameters
        ----------
        selector : Mapping[str, str]
            Expected pod selector labels.

        Returns
        -------
        bool
            Whether the NetworkPolicy pod selector exactly matches `selector`.
        """
        return dict(self.pod_selector) == dict(selector)
