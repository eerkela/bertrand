"""Passive CNI diagnostics for Bertrand's shared Kubernetes runtime."""

from __future__ import annotations

import asyncio
from typing import TYPE_CHECKING, Any, cast

from bertrand.env.kube.daemonset import DaemonSet
from bertrand.env.kube.node import Node

if TYPE_CHECKING:
    from collections.abc import Mapping

    from bertrand.env.git import Deadline
    from bertrand.env.kube.api.client import Kube

CNI_NAMESPACES = frozenset(
    {
        "kube-system",
        "calico-system",
        "cilium",
        "kube-flannel",
        "kube-ovn",
    }
)
NETWORK_POLICY_CNI = frozenset({"calico", "cilium", "kube-ovn"})


async def inspect_cni(kube: Kube, *, deadline: Deadline) -> dict[str, object]:
    """Inspect the cluster CNI without mutating it.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    deadline : Deadline
        Maximum request budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    dict[str, object]
        Passive CNI diagnostic payload for status and doctor commands.
    """
    daemonset_batches = await asyncio.gather(
        *(
            DaemonSet.list(kube, deadline=deadline, namespace=namespace)
            for namespace in CNI_NAMESPACES
        )
    )
    daemonsets = [
        daemonset for batch in daemonset_batches for daemonset in batch
    ]
    names = tuple(
        sorted(
            f"{daemonset.namespace}/{daemonset.name}"
            for daemonset in daemonsets
            if daemonset.namespace and daemonset.name
        )
    )
    cni, confidence = _detect_cni(names)
    pod_cidrs = await _pod_cidrs(kube, deadline=deadline)
    service_cidrs = await _service_cidrs(kube, deadline=deadline)
    warnings: list[str] = []
    if cni == "unknown":
        warnings.append(
            "unable to detect a known CNI; NetworkPolicy enforcement is unknown"
        )
    if cni not in NETWORK_POLICY_CNI:
        warnings.append(f"NetworkPolicy enforcement is not confirmed for CNI {cni!r}")
    if not pod_cidrs:
        warnings.append("no Pod CIDRs were reported by Kubernetes Nodes")
    return {
        "ready": cni != "unknown",
        "name": cni,
        "confidence": confidence,
        "network_policy": "confirmed" if cni in NETWORK_POLICY_CNI else "unknown",
        "pod_cidrs": list(pod_cidrs),
        "service_cidrs": list(service_cidrs),
        "daemonsets": list(names),
        "warnings": list(warnings),
        "message": "; ".join(warnings),
    }


def _detect_cni(daemonsets: tuple[str, ...]) -> tuple[str, str]:
    names = {item.split("/", maxsplit=1)[-1] for item in daemonsets}
    if "cilium" in names:
        return ("cilium", "daemonset")
    if "calico-node" in names:
        return ("calico", "daemonset")
    if "kube-flannel-ds" in names or "kube-flannel" in names:
        return ("flannel", "daemonset")
    if "kube-ovn-cni" in names:
        return ("kube-ovn", "daemonset")
    return ("unknown", "none")


async def _pod_cidrs(kube: Kube, *, deadline: Deadline) -> tuple[str, ...]:
    cidrs: list[str] = []
    for node in await Node.list(kube, deadline=deadline):
        for cidr in node.pod_cidrs:
            if cidr not in cidrs:
                cidrs.append(cidr)
    return tuple(cidrs)


async def _service_cidrs(kube: Kube, *, deadline: Deadline) -> tuple[str, ...]:
    try:
        payload = await kube.run(
            lambda request_timeout: kube.client.call_api(
                "/apis/networking.k8s.io/v1/servicecidrs",
                "GET",
                _return_http_data_only=True,
                _request_timeout=request_timeout,
            ),
            deadline=deadline,
            context="failed to list ServiceCIDRs",
        )
    except OSError:
        return ()
    if not isinstance(payload, dict):
        return ()
    items = payload.get("items", ())
    if not isinstance(items, list):
        return ()
    out: list[str] = []
    for item in items:
        if not isinstance(item, dict):
            continue
        spec = item.get("spec", {})
        if not isinstance(spec, dict):
            continue
        cidrs = cast("Mapping[str, Any]", spec).get("cidrs", ())
        if not isinstance(cidrs, list):
            continue
        for cidr in cidrs:
            value = str(cidr or "").strip()
            if value and value not in out:
                out.append(value)
    return tuple(out)
