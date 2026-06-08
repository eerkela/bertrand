"""MetalLB load-balancer helpers for Bertrand cluster networking."""

from __future__ import annotations

import asyncio
from typing import TYPE_CHECKING

from bertrand.env.git import BERTRAND_LABEL, BERTRAND_LABEL_MANAGED, Deadline, until
from bertrand.env.kube.api.client import kubectl
from bertrand.env.kube.crd import CustomResourceDefinition
from bertrand.env.kube.custom_object import (
    CustomObject,
    CustomObjectResource,
)
from bertrand.env.kube.daemonset import DaemonSet
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.namespace import Namespace

if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence

    from bertrand.env.kube.api.client import Kube

METALLB_VERSION = "v0.16.0"
METALLB_INSTALL_URL = (
    "https://raw.githubusercontent.com/metallb/metallb/"
    f"{METALLB_VERSION}/config/manifests/metallb-frr-k8s.yaml"
)
METALLB_NAMESPACE = "metallb-system"
METALLB_CONTROLLER_DEPLOYMENT = "controller"
METALLB_SPEAKER_DAEMONSET = "speaker"
METALLB_GROUP = "metallb.io"
METALLB_V1BETA1 = "v1beta1"
METALLB_V1BETA2 = "v1beta2"
METALLB_LABEL = "bertrand.dev/load-balancer"
METALLB_LABEL_VALUE = "v1"
_METALLB_MANAGED_SELECTOR = {
    BERTRAND_LABEL: BERTRAND_LABEL_MANAGED,
    METALLB_LABEL: METALLB_LABEL_VALUE,
}
METALLB_LABELS = {
    "app.kubernetes.io/name": "metallb",
    "app.kubernetes.io/part-of": "bertrand",
    "app.kubernetes.io/component": "load-balancer",
    **_METALLB_MANAGED_SELECTOR,
}
METALLB_NAMESPACE_LABELS = {
    **METALLB_LABELS,
    "pod-security.kubernetes.io/enforce": "privileged",
    "pod-security.kubernetes.io/audit": "privileged",
    "pod-security.kubernetes.io/warn": "privileged",
}
METALLB_CRDS = (
    "ipaddresspools.metallb.io",
    "l2advertisements.metallb.io",
    "bgpadvertisements.metallb.io",
    "bgppeers.metallb.io",
)
METALLB_WAIT_POLL_INTERVAL_SECONDS = 0.5


_IP_ADDRESS_POOL_RESOURCE = CustomObjectResource[CustomObject](
    group=METALLB_GROUP,
    version=METALLB_V1BETA1,
    kind="IPAddressPool",
    plural="ipaddresspools",
    default_namespace=METALLB_NAMESPACE,
)
_L2_ADVERTISEMENT_RESOURCE = CustomObjectResource[CustomObject](
    group=METALLB_GROUP,
    version=METALLB_V1BETA1,
    kind="L2Advertisement",
    plural="l2advertisements",
    default_namespace=METALLB_NAMESPACE,
)
_BGP_PEER_RESOURCE = CustomObjectResource[CustomObject](
    group=METALLB_GROUP,
    version=METALLB_V1BETA2,
    kind="BGPPeer",
    plural="bgppeers",
    default_namespace=METALLB_NAMESPACE,
)
_BGP_ADVERTISEMENT_RESOURCE = CustomObjectResource[CustomObject](
    group=METALLB_GROUP,
    version=METALLB_V1BETA1,
    kind="BGPAdvertisement",
    plural="bgpadvertisements",
    default_namespace=METALLB_NAMESPACE,
)
_CONFIGURATION_STATE_RESOURCE: CustomObjectResource[CustomObject] = (
    CustomObjectResource(
        group=METALLB_GROUP,
        version=METALLB_V1BETA1,
        kind="ConfigurationState",
        plural="configurationstates",
    )
)
_SERVICE_L2_STATUS_RESOURCE: CustomObjectResource[CustomObject] = CustomObjectResource(
    group=METALLB_GROUP,
    version=METALLB_V1BETA1,
    kind="ServiceL2Status",
    plural="servicel2statuses",
)
_SERVICE_BGP_STATUS_RESOURCE: CustomObjectResource[CustomObject] = CustomObjectResource(
    group=METALLB_GROUP,
    version=METALLB_V1BETA1,
    kind="ServiceBGPStatus",
    plural="servicebgpstatuses",
)


async def upsert_ip_address_pool(
    kube: Kube,
    *,
    name: str,
    addresses: Sequence[str],
    auto_assign: bool,
    deadline: Deadline,
) -> CustomObject:
    """Create or patch one Bertrand-managed MetalLB IPAddressPool.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    name : str
        IPAddressPool name.
    addresses : Sequence[str]
        Address ranges delegated to MetalLB.
    auto_assign : bool
        Whether MetalLB may automatically allocate from this pool.
    deadline : Deadline
        Maximum request budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    CustomObject
        Created or patched IPAddressPool object.
    """
    name = _required_name(name, kind="IPAddressPool")
    normalized = _required_values(addresses, kind="IPAddressPool addresses")
    return await _managed_upsert(
        _IP_ADDRESS_POOL_RESOURCE,
        kube,
        kind="IPAddressPool",
        name=name,
        spec={"addresses": list(normalized), "autoAssign": auto_assign},
        deadline=deadline,
    )


async def upsert_l2_advertisement(
    kube: Kube,
    *,
    name: str,
    pool: str,
    interfaces: Sequence[str],
    deadline: Deadline,
) -> CustomObject:
    """Create or patch one Bertrand-managed MetalLB L2Advertisement.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    name : str
        L2Advertisement name.
    pool : str
        IPAddressPool name to advertise.
    interfaces : Sequence[str]
        Optional interface names to advertise from.
    deadline : Deadline
        Maximum request budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    CustomObject
        Created or patched L2Advertisement object.
    """
    name = _required_name(name, kind="L2Advertisement")
    pool = _required_name(pool, kind="IPAddressPool")
    spec: dict[str, object] = {"ipAddressPools": [pool]}
    normalized_interfaces = _string_tuple(interfaces)
    if normalized_interfaces:
        spec["interfaces"] = list(normalized_interfaces)
    return await _managed_upsert(
        _L2_ADVERTISEMENT_RESOURCE,
        kube,
        kind="L2Advertisement",
        name=name,
        spec=spec,
        deadline=deadline,
    )


async def upsert_bgp_peer(
    kube: Kube,
    *,
    name: str,
    peer_address: str,
    peer_asn: int,
    local_asn: int,
    peer_port: int | None,
    source_address: str | None,
    password_secret: str | None,
    deadline: Deadline,
) -> CustomObject:
    """Create or patch one Bertrand-managed MetalLB BGPPeer.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    name : str
        BGPPeer name.
    peer_address : str
        Router address MetalLB should connect to.
    peer_asn : int
        Remote router ASN.
    local_asn : int
        ASN MetalLB should use for the local side.
    peer_port : int | None
        Optional remote BGP port.
    source_address : str | None
        Optional local source address.
    password_secret : str | None
        Optional same-namespace basic-auth Secret for TCP MD5.
    deadline : Deadline
        Maximum request budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    CustomObject
        Created or patched BGPPeer object.
    """
    name = _required_name(name, kind="BGPPeer")
    peer_address = _required_name(peer_address, kind="peer address")
    spec: dict[str, object] = {
        "peerAddress": peer_address,
        "peerASN": peer_asn,
        "myASN": local_asn,
    }
    if peer_port is not None:
        spec["peerPort"] = peer_port
    if source_address:
        spec["sourceAddress"] = source_address
    if password_secret:
        spec["passwordSecret"] = {"name": password_secret}
    return await _managed_upsert(
        _BGP_PEER_RESOURCE,
        kube,
        kind="BGPPeer",
        name=name,
        spec=spec,
        deadline=deadline,
    )


async def upsert_bgp_advertisement(
    kube: Kube,
    *,
    name: str,
    pool: str,
    peers: Sequence[str],
    local_pref: int | None,
    communities: Sequence[str],
    deadline: Deadline,
) -> CustomObject:
    """Create or patch one Bertrand-managed MetalLB BGPAdvertisement.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    name : str
        BGPAdvertisement name.
    pool : str
        IPAddressPool name to advertise.
    peers : Sequence[str]
        Optional BGPPeer names to target.
    local_pref : int | None
        Optional BGP local preference.
    communities : Sequence[str]
        Optional BGP communities to attach.
    deadline : Deadline
        Maximum request budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    CustomObject
        Created or patched BGPAdvertisement object.
    """
    name = _required_name(name, kind="BGPAdvertisement")
    pool = _required_name(pool, kind="IPAddressPool")
    spec: dict[str, object] = {"ipAddressPools": [pool]}
    normalized_peers = _string_tuple(peers)
    normalized_communities = _string_tuple(communities)
    if normalized_peers:
        spec["peers"] = list(normalized_peers)
    if local_pref is not None:
        spec["localPref"] = local_pref
    if normalized_communities:
        spec["communities"] = list(normalized_communities)
    return await _managed_upsert(
        _BGP_ADVERTISEMENT_RESOURCE,
        kube,
        kind="BGPAdvertisement",
        name=name,
        spec=spec,
        deadline=deadline,
    )


async def ensure_metallb(kube: Kube, *, deadline: Deadline) -> None:
    """Install and verify Bertrand-managed MetalLB.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    deadline : Deadline
        Maximum convergence budget in seconds. If infinite, wait indefinitely.

    """
    await _ensure_managed_metallb_namespace(kube, deadline=deadline)
    await _apply_metallb(deadline=deadline)
    await asyncio.gather(
        *(
            _wait_crd_established(
                kube,
                name=crd_name,
                deadline=deadline,
            )
            for crd_name in METALLB_CRDS
        )
    )
    await asyncio.gather(
        _wait_controller_available(kube, deadline=deadline),
        _wait_speaker_available(kube, deadline=deadline),
    )


async def metallb_status(kube: Kube, *, deadline: Deadline) -> dict[str, object]:
    """Return cluster MetalLB status without mutating it.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    deadline : Deadline
        Maximum request budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    dict[str, object]
        JSON-serializable MetalLB readiness and configuration summary.
    """
    namespace, controller, speaker = await asyncio.gather(
        Namespace.get(kube, name=METALLB_NAMESPACE, deadline=deadline),
        Deployment.get(
            kube,
            namespace=METALLB_NAMESPACE,
            name=METALLB_CONTROLLER_DEPLOYMENT,
            deadline=deadline,
        ),
        DaemonSet.get(
            kube,
            namespace=METALLB_NAMESPACE,
            name=METALLB_SPEAKER_DAEMONSET,
            deadline=deadline,
        ),
    )
    crds = await _crd_status(kube, deadline=deadline)
    pools, l2, peers, bgp, config_states, l2_status, bgp_status = await asyncio.gather(
        _safe_resource_list(
            _IP_ADDRESS_POOL_RESOURCE,
            kube,
            deadline=deadline,
        ),
        _safe_resource_list(
            _L2_ADVERTISEMENT_RESOURCE,
            kube,
            deadline=deadline,
        ),
        _safe_resource_list(
            _BGP_PEER_RESOURCE,
            kube,
            deadline=deadline,
        ),
        _safe_resource_list(
            _BGP_ADVERTISEMENT_RESOURCE,
            kube,
            deadline=deadline,
        ),
        _safe_resource_list(
            _CONFIGURATION_STATE_RESOURCE,
            kube,
            deadline=deadline,
        ),
        _safe_resource_list(
            _SERVICE_L2_STATUS_RESOURCE,
            kube,
            deadline=deadline,
        ),
        _safe_resource_list(
            _SERVICE_BGP_STATUS_RESOURCE,
            kube,
            deadline=deadline,
        ),
    )
    managed = _labels_managed(namespace.labels) if namespace is not None else False
    ready = (
        namespace is not None
        and controller is not None
        and controller.has_available_replicas()
        and speaker is not None
        and speaker.has_available_pods()
        and all(crds.values())
    )
    messages: list[str] = []
    if namespace is None:
        messages.append("MetalLB namespace is missing")
    elif not managed:
        messages.append("MetalLB namespace exists but is not managed by Bertrand")
    if controller is None or not controller.has_available_replicas():
        messages.append("MetalLB controller Deployment is not Available")
    if speaker is None or not speaker.has_available_pods():
        messages.append("MetalLB speaker DaemonSet has no available Pods")
    missing = [name for name, established in crds.items() if not established]
    if missing:
        messages.append(f"MetalLB CRDs are missing or not established: {missing}")
    messages.extend(_configuration_errors(config_states))
    return {
        "ready": ready,
        "managed": managed,
        "namespace": METALLB_NAMESPACE,
        "controller_ready": (
            controller is not None and controller.has_available_replicas()
        ),
        "speaker_ready": speaker is not None and speaker.has_available_pods(),
        "crds": crds,
        "pools": [_pool_status(pool) for pool in pools],
        "l2_advertisements": [_object_summary(item) for item in l2],
        "bgp_peers": [_bgp_peer_status(peer) for peer in peers],
        "bgp_advertisements": [_object_summary(item) for item in bgp],
        "service_l2_status": [_custom_status_summary(item) for item in l2_status],
        "service_bgp_status": [_custom_status_summary(item) for item in bgp_status],
        "configuration": [_custom_status_summary(item) for item in config_states],
        "message": "; ".join(messages),
    }


async def _ensure_managed_metallb_namespace(
    kube: Kube,
    *,
    deadline: Deadline,
) -> None:
    namespace = await Namespace.get(kube, name=METALLB_NAMESPACE, deadline=deadline)
    if namespace is not None and not _labels_managed(namespace.labels):
        msg = (
            f"Namespace {METALLB_NAMESPACE!r} exists but is not managed by "
            "Bertrand. Inspect the existing MetalLB installation or remove it "
            "before running `bertrand cluster network lb install`."
        )
        raise OSError(msg)
    await Namespace.upsert(
        kube,
        name=METALLB_NAMESPACE,
        labels=METALLB_NAMESPACE_LABELS,
        deadline=deadline,
    )


async def _require_managed_metallb_namespace(kube: Kube, *, deadline: Deadline) -> None:
    namespace = await Namespace.get(kube, name=METALLB_NAMESPACE, deadline=deadline)
    if namespace is None:
        msg = (
            "MetalLB is not installed by Bertrand yet. Run "
            "`bertrand cluster network lb install` before configuring pools or "
            "advertisements."
        )
        raise OSError(msg)
    if not _labels_managed(namespace.labels):
        msg = (
            f"Namespace {METALLB_NAMESPACE!r} exists but is not managed by "
            "Bertrand; refusing to mutate MetalLB configuration in a shared "
            "unmanaged installation."
        )
        raise OSError(msg)


async def _apply_metallb(*, deadline: Deadline) -> None:
    try:
        await kubectl(
            ["apply", "--server-side", "-f", METALLB_INSTALL_URL],
            capture_output=True,
            deadline=deadline,
        )
    except OSError as err:
        msg = f"failed to apply MetalLB install manifest {METALLB_INSTALL_URL!r}"
        raise OSError(msg) from err


async def _wait_crd_established(
    kube: Kube,
    *,
    name: str,
    deadline: Deadline,
) -> CustomResourceDefinition:
    async def established(attempt_deadline: Deadline) -> CustomResourceDefinition:
        crd = await CustomResourceDefinition.get(
            kube,
            name=name,
            deadline=attempt_deadline,
        )
        if crd is None:
            msg = f"MetalLB CRD {name!r} is not installed yet"
            raise TimeoutError(msg)
        if not crd.is_established:
            msg = f"MetalLB CRD {name!r} is not established yet"
            raise TimeoutError(msg)
        return crd

    try:
        return await until(
            established,
            deadline=deadline,
            delay=METALLB_WAIT_POLL_INTERVAL_SECONDS,
        )
    except TimeoutError as err:
        msg = f"MetalLB CRD {name!r} did not become Established"
        raise OSError(msg) from err


async def _wait_controller_available(kube: Kube, *, deadline: Deadline) -> Deployment:
    async def available(attempt_deadline: Deadline) -> Deployment:
        deployment = await Deployment.get(
            kube,
            namespace=METALLB_NAMESPACE,
            name=METALLB_CONTROLLER_DEPLOYMENT,
            deadline=attempt_deadline,
        )
        if deployment is None:
            msg = "MetalLB controller Deployment is not created yet"
            raise TimeoutError(msg)
        if not deployment.has_available_replicas():
            msg = "MetalLB controller Deployment is not Available yet"
            raise TimeoutError(msg)
        return deployment

    try:
        return await until(
            available,
            deadline=deadline,
            delay=METALLB_WAIT_POLL_INTERVAL_SECONDS,
        )
    except TimeoutError as err:
        msg = "MetalLB controller Deployment did not become Available"
        raise OSError(msg) from err


async def _wait_speaker_available(kube: Kube, *, deadline: Deadline) -> DaemonSet:
    async def available(attempt_deadline: Deadline) -> DaemonSet:
        daemonset = await DaemonSet.get(
            kube,
            namespace=METALLB_NAMESPACE,
            name=METALLB_SPEAKER_DAEMONSET,
            deadline=attempt_deadline,
        )
        if daemonset is None:
            msg = "MetalLB speaker DaemonSet is not created yet"
            raise TimeoutError(msg)
        if not daemonset.has_available_pods():
            msg = "MetalLB speaker DaemonSet has no available Pods yet"
            raise TimeoutError(msg)
        return daemonset

    try:
        return await until(
            available,
            deadline=deadline,
            delay=METALLB_WAIT_POLL_INTERVAL_SECONDS,
        )
    except TimeoutError as err:
        msg = "MetalLB speaker DaemonSet did not become Available"
        raise OSError(msg) from err


async def _crd_status(kube: Kube, *, deadline: Deadline) -> dict[str, bool]:
    crds = await asyncio.gather(
        *(
            CustomResourceDefinition.get(kube, name=name, deadline=deadline)
            for name in METALLB_CRDS
        )
    )
    return {
        name: crd is not None and crd.is_established
        for name, crd in zip(METALLB_CRDS, crds, strict=True)
    }


async def _managed_upsert[T](
    resource: CustomObjectResource[T],
    kube: Kube,
    *,
    kind: str,
    name: str,
    spec: Mapping[str, object],
    deadline: Deadline,
) -> T:
    await _require_managed_metallb_namespace(kube, deadline=deadline)
    current = await resource.get(
        kube,
        namespace=METALLB_NAMESPACE,
        name=name,
        deadline=deadline,
    )
    _assert_managed(current, kind=kind)
    return await resource.upsert(
        kube,
        namespace=METALLB_NAMESPACE,
        name=name,
        spec=spec,
        labels=METALLB_LABELS,
        deadline=deadline,
    )


async def _safe_resource_list[T](
    resource: CustomObjectResource[T],
    kube: Kube,
    *,
    deadline: Deadline,
) -> list[T]:
    try:
        return await resource.list(kube, namespace=METALLB_NAMESPACE, deadline=deadline)
    except (OSError, TimeoutError, ValueError):
        return []


def _assert_managed(resource: object | None, *, kind: str) -> None:
    if resource is None:
        return
    labels = getattr(resource, "labels", {})
    if _labels_managed(labels):
        return
    name = getattr(resource, "name", "")
    msg = f"{kind} {METALLB_NAMESPACE}/{name} exists but is not managed by Bertrand"
    raise OSError(msg)


def _labels_managed(labels: Mapping[str, str]) -> bool:
    return all(
        labels.get(key) == value for key, value in _METALLB_MANAGED_SELECTOR.items()
    )


def _required_name(value: str, *, kind: str) -> str:
    name = value.strip()
    if not name:
        msg = f"{kind} requires a non-empty value"
        raise ValueError(msg)
    return name


def _required_values(values: Sequence[str], *, kind: str) -> tuple[str, ...]:
    normalized = _string_tuple(values)
    if not normalized:
        msg = f"{kind} requires at least one value"
        raise ValueError(msg)
    return normalized


def _string_tuple(values: object) -> tuple[str, ...]:
    if not isinstance(values, (list, tuple)):
        return ()
    out: list[str] = []
    for value in values:
        text = str(value or "").strip()
        if text and text not in out:
            out.append(text)
    return tuple(out)


def _int_status(status: Mapping[str, object], key: str) -> int:
    value = status.get(key, 0)
    try:
        return int(value) if isinstance(value, int | float | str) and str(value) else 0
    except ValueError:
        return 0


def _pool_status(pool: CustomObject) -> dict[str, object]:
    return {
        "name": pool.name,
        "managed": _labels_managed(pool.labels),
        "addresses": list(_string_tuple(pool.spec.get("addresses", ()))),
        "auto_assign": bool(pool.spec.get("autoAssign", True)),
        "available_ipv4": _int_status(pool.status, "availableIPv4"),
        "assigned_ipv4": _int_status(pool.status, "assignedIPv4"),
    }


def _bgp_peer_status(peer: CustomObject) -> dict[str, object]:
    return {
        "name": peer.name,
        "managed": _labels_managed(peer.labels),
        "peer_address": str(peer.spec.get("peerAddress") or "").strip(),
    }


def _object_summary(resource: CustomObject) -> dict[str, object]:
    return {
        "name": resource.name,
        "managed": _labels_managed(resource.labels),
        "pools": list(_string_tuple(resource.spec.get("ipAddressPools", ()))),
    }


def _custom_status_summary(resource: CustomObject) -> dict[str, object]:
    return {
        "name": resource.name,
        "status": dict(resource.status),
    }


def _configuration_errors(resources: Sequence[CustomObject]) -> list[str]:
    errors: list[str] = []
    for resource in resources:
        status = resource.status
        result = str(status.get("result") or "").strip()
        summary = str(status.get("errorSummary") or "").strip()
        if result and result.lower() != "valid":
            errors.append(f"MetalLB configuration {resource.name}: {summary or result}")
    return errors
