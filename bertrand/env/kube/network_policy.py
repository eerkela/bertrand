"""Wrappers for the Kubernetes NetworkPolicy API."""

from __future__ import annotations

from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING, Literal, Self, cast

import kubernetes

from .api.metadata import NamespacedKubeMetadata
from .api.resource import ResourceClient

if TYPE_CHECKING:
    import builtins
    from collections.abc import Collection, Mapping

    from .api.client import Kube

type NetworkPolicyType = Literal["Ingress", "Egress"]

_POLICY_TYPES = frozenset({"Ingress", "Egress"})


@dataclass(frozen=True)
class NetworkPolicy(NamespacedKubeMetadata[kubernetes.client.V1NetworkPolicy]):
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

    @classmethod
    def _client(cls) -> ResourceClient[kubernetes.client.V1NetworkPolicy, Self]:
        return ResourceClient(
            scope="namespaced",
            kind="NetworkPolicy",
            expected=kubernetes.client.V1NetworkPolicy,
            list_type=kubernetes.client.V1NetworkPolicyList,
            wrapper=lambda payload: cls(_obj=payload),
            read=lambda kube, namespace, name, request_timeout: (
                kube.networking.read_namespaced_network_policy(
                    name=name,
                    namespace=namespace,
                    _request_timeout=request_timeout,
                )
            ),
            list_all=lambda kube, label_selector, field_selector, request_timeout: (
                kube.networking.list_network_policy_for_all_namespaces(
                    label_selector=label_selector,
                    field_selector=field_selector,
                    _request_timeout=request_timeout,
                )
            ),
            list_namespace=lambda kube, namespace, labels, fields, timeout: (
                kube.networking.list_namespaced_network_policy(
                    namespace=namespace,
                    label_selector=labels,
                    field_selector=fields,
                    _request_timeout=timeout,
                )
            ),
            create=lambda kube, namespace, _name, manifest, request_timeout: (
                kube.networking.create_namespaced_network_policy(
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                )
            ),
            patch=lambda kube, namespace, name, manifest, request_timeout: (
                kube.networking.patch_namespaced_network_policy(
                    name=name,
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                )
            ),
            delete=lambda kube, namespace, name, request_timeout: (
                kube.networking.delete_namespaced_network_policy(
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
        """Read one Kubernetes NetworkPolicy by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the NetworkPolicy.
        name : str
            NetworkPolicy name to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        NetworkPolicy | None
            Wrapped Kubernetes NetworkPolicy, or `None` if it does not exist.
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
        """List Kubernetes NetworkPolicies with optional filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        namespaces : Collection[str] | None, optional
            Optional namespace filters. `None` queries all namespaces. Otherwise,
            entries are trimmed, deduplicated, and queried individually.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.

        Returns
        -------
        list[NetworkPolicy]
            Wrapped Kubernetes NetworkPolicies matching the requested filters.
        """
        return await cls._client().list(
            kube,
            timeout=timeout,
            namespaces=namespaces,
            labels=labels,
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
        return await cls._client().upsert(
            kube,
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

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this NetworkPolicy by its metadata namespace and name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        NetworkPolicy | None
            Fresh wrapper for the same NetworkPolicy, or `None` if deleted.
        """
        namespace, name = self._require_namespace_name("refresh NetworkPolicy")
        return await type(self).get(
            kube,
            namespace=namespace,
            name=name,
            timeout=timeout,
        )

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this NetworkPolicy from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        namespace, name = self._require_namespace_name("delete NetworkPolicy")
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
        """Wait until this NetworkPolicy is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait time in seconds. Must be positive.
        """
        namespace, name = self._require_namespace_name(
            "wait for NetworkPolicy deletion"
        )
        await (
            type(self)
            ._client()
            .wait_deleted(
                label=self._object_label(name=name, namespace=namespace),
                timeout=timeout,
                refresh=lambda remaining: self.refresh(kube, timeout=remaining),
            )
        )


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
