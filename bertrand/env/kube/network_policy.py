"""Wrappers for the Kubernetes NetworkPolicy API."""

from __future__ import annotations

from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING, ClassVar, Literal, Self, cast

import kubernetes

from .api.metadata import NamespacedKubeMetadata
from .api.resource import BuiltinResource, BuiltinResourceObject

if TYPE_CHECKING:
    from collections.abc import Collection, Mapping

    from .api.client import Kube

type NetworkPolicyType = Literal["Ingress", "Egress"]

_POLICY_TYPES = frozenset({"Ingress", "Egress"})


@dataclass(frozen=True)
class NetworkPolicy(
    BuiltinResourceObject[kubernetes.client.V1NetworkPolicy],
    NamespacedKubeMetadata[kubernetes.client.V1NetworkPolicy],
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

    resource: ClassVar[BuiltinResource[kubernetes.client.V1NetworkPolicy]] = (
        BuiltinResource.namespaced(
            api="networking",
            kind="NetworkPolicy",
            slug="network_policy",
            expected=kubernetes.client.V1NetworkPolicy,
            list_type=kubernetes.client.V1NetworkPolicyList,
            create=True,
            patch=True,
            delete=True,
        )
    )

    @staticmethod
    def _manifest(
        *,
        namespace: str,
        name: str,
        pod_selector: Mapping[str, str],
        policy_types: Collection[NetworkPolicyType],
        ingress: Collection[Mapping[str, object]] | None,
        egress: Collection[Mapping[str, object]] | None,
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
    ) -> dict[str, object]:
        normalized_types = _policy_types(policy_types)
        spec: dict[str, object] = {
            "podSelector": {"matchLabels": dict(pod_selector)},
            "policyTypes": list(normalized_types),
        }
        if "Ingress" in normalized_types:
            spec["ingress"] = [dict(rule) for rule in ingress or ()]
        if "Egress" in normalized_types:
            spec["egress"] = [dict(rule) for rule in egress or ()]
        return {
            "apiVersion": "networking.k8s.io/v1",
            "kind": "NetworkPolicy",
            "metadata": {
                "name": name,
                "namespace": namespace,
                "labels": dict(labels or {}),
                "annotations": dict(annotations or {}),
            },
            "spec": spec,
        }

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        pod_selector: Mapping[str, str],
        timeout: float,
        policy_types: Collection[NetworkPolicyType] = ("Ingress",),
        ingress: Collection[Mapping[str, object]] | None = None,
        egress: Collection[Mapping[str, object]] | None = None,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> Self:
        """Create or patch one Kubernetes NetworkPolicy from intent fields.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the NetworkPolicy.
        name : str
            NetworkPolicy name to create or patch.
        pod_selector : Mapping[str, str]
            Pod label selector for the NetworkPolicy `spec.podSelector.matchLabels`.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        policy_types : Collection[NetworkPolicyType], optional
            Network directions governed by this policy.
        ingress : Collection[Mapping[str, object]] | None, optional
            Raw Kubernetes ingress rule dictionaries. An empty collection with
            ``policyTypes = ["Ingress"]`` denies all ingress to selected Pods.
        egress : Collection[Mapping[str, object]] | None, optional
            Raw Kubernetes egress rule dictionaries.
        labels : Mapping[str, str] | None, optional
            Labels to apply to `metadata.labels`.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to `metadata.annotations`.

        Returns
        -------
        NetworkPolicy
            Wrapped created or patched NetworkPolicy.

        Raises
        ------
        OSError
            If Kubernetes create/patch fails or the intent is missing identity data.
        """
        namespace = namespace.strip()
        name = name.strip()
        if not namespace or not name:
            msg = "NetworkPolicy upsert requires non-empty namespace and name"
            raise OSError(msg)
        if not pod_selector:
            msg = "NetworkPolicy upsert requires a non-empty pod selector"
            raise OSError(msg)

        manifest = cls._manifest(
            namespace=namespace,
            name=name,
            pod_selector=pod_selector,
            policy_types=policy_types,
            ingress=ingress,
            egress=egress,
            labels=labels,
            annotations=annotations,
        )
        return await cls.resource.upsert(
            kube,
            owner=cls,
            namespace=namespace,
            name=name,
            manifest=manifest,
            timeout=timeout,
        )

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
