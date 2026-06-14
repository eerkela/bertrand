"""MetalLB load-balancer helpers for Bertrand cluster networking."""

from __future__ import annotations

import asyncio
from typing import TYPE_CHECKING, Any, cast

from pydantic import Field

from bertrand.env.git import (
    BERTRAND_LABEL,
    BERTRAND_LABEL_MANAGED,
    EMPTY_MAPPING,
    Deadline,
    until,
)
from bertrand.env.kube.api.client import kubectl
from bertrand.env.kube.crd import (
    CustomResourceDefinition,
)
from bertrand.env.kube.custom_object import (
    CustomObjectManifest,
    CustomObjectMetadata,
    CustomResource,
    custom_resource,
)
from bertrand.env.kube.daemonset import DaemonSet
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.namespace import Namespace, NamespaceManifest

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


class IPAddressPoolManifest(CustomObjectManifest):
    """Push-side manifest for one MetalLB IPAddressPool.

    Parameters
    ----------
    name : str
        IPAddressPool name.
    addresses : Sequence[str]
        Address ranges delegated to MetalLB.
    auto_assign : bool
        Whether MetalLB may automatically allocate from this pool.
    """

    api_version: str = Field(
        default=f"{METALLB_GROUP}/{METALLB_V1BETA1}",
        alias="apiVersion",
    )
    kind: str = "IPAddressPool"
    addresses: Sequence[str]
    auto_assign: bool
    status: Mapping[str, object] = EMPTY_MAPPING

    def manifest(self) -> Mapping[str, object]:
        """Render the Kubernetes IPAddressPool manifest.

        Returns
        -------
        Mapping[str, object]
            Complete Kubernetes custom-object manifest.
        """
        return {
            "apiVersion": f"{METALLB_GROUP}/{METALLB_V1BETA1}",
            "kind": "IPAddressPool",
            "metadata": self.metadata.manifest(),
            "spec": {
                "addresses": list(self.addresses),
                "autoAssign": self.auto_assign,
            },
        }


@custom_resource(
    manifest=IPAddressPoolManifest,
    group=METALLB_GROUP,
    version=METALLB_V1BETA1,
    kind="IPAddressPool",
    plural="ipaddresspools",
    default_namespace=METALLB_NAMESPACE,
)
class IPAddressPool(CustomResource[IPAddressPoolManifest]):
    """Wrapper around one MetalLB IPAddressPool custom object."""


class L2AdvertisementManifest(CustomObjectManifest):
    """Push-side manifest for one MetalLB L2Advertisement.

    Parameters
    ----------
    name : str
        L2Advertisement name.
    pool : str
        IPAddressPool name to advertise.
    interfaces : Sequence[str]
        Optional interface names to advertise from.
    """

    api_version: str = Field(
        default=f"{METALLB_GROUP}/{METALLB_V1BETA1}",
        alias="apiVersion",
    )
    kind: str = "L2Advertisement"
    pool: str
    interfaces: Sequence[str]
    status: Mapping[str, object] = EMPTY_MAPPING

    def manifest(self) -> Mapping[str, object]:
        """Render the Kubernetes L2Advertisement manifest.

        Returns
        -------
        Mapping[str, object]
            Complete Kubernetes custom-object manifest.
        """
        spec: dict[str, object] = {"ipAddressPools": [self.pool]}
        if self.interfaces:
            spec["interfaces"] = list(self.interfaces)
        return {
            "apiVersion": f"{METALLB_GROUP}/{METALLB_V1BETA1}",
            "kind": "L2Advertisement",
            "metadata": self.metadata.manifest(),
            "spec": spec,
        }


@custom_resource(
    manifest=L2AdvertisementManifest,
    group=METALLB_GROUP,
    version=METALLB_V1BETA1,
    kind="L2Advertisement",
    plural="l2advertisements",
    default_namespace=METALLB_NAMESPACE,
)
class L2Advertisement(CustomResource[L2AdvertisementManifest]):
    """Wrapper around one MetalLB L2Advertisement custom object."""


class BGPPeerManifest(CustomObjectManifest):
    """Push-side manifest for one MetalLB BGPPeer.

    Parameters
    ----------
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
    """

    api_version: str = Field(
        default=f"{METALLB_GROUP}/{METALLB_V1BETA2}",
        alias="apiVersion",
    )
    kind: str = "BGPPeer"
    peer_address: str
    peer_asn: int
    local_asn: int
    peer_port: int | None
    source_address: str | None
    password_secret: str | None

    status: Mapping[str, object] = EMPTY_MAPPING

    def manifest(self) -> Mapping[str, object]:
        """Render the Kubernetes BGPPeer manifest.

        Returns
        -------
        Mapping[str, object]
            Complete Kubernetes custom-object manifest.
        """
        spec: dict[str, object] = {
            "peerAddress": self.peer_address,
            "peerASN": self.peer_asn,
            "myASN": self.local_asn,
        }
        if self.peer_port is not None:
            spec["peerPort"] = self.peer_port
        if self.source_address:
            spec["sourceAddress"] = self.source_address
        if self.password_secret:
            spec["passwordSecret"] = {"name": self.password_secret}
        return {
            "apiVersion": f"{METALLB_GROUP}/{METALLB_V1BETA2}",
            "kind": "BGPPeer",
            "metadata": self.metadata.manifest(),
            "spec": spec,
        }


@custom_resource(
    manifest=BGPPeerManifest,
    group=METALLB_GROUP,
    version=METALLB_V1BETA2,
    kind="BGPPeer",
    plural="bgppeers",
    default_namespace=METALLB_NAMESPACE,
)
class BGPPeer(CustomResource[BGPPeerManifest]):
    """Wrapper around one MetalLB BGPPeer custom object."""


class BGPAdvertisementManifest(CustomObjectManifest):
    """Push-side manifest for one MetalLB BGPAdvertisement.

    Parameters
    ----------
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
    """

    api_version: str = Field(
        default=f"{METALLB_GROUP}/{METALLB_V1BETA1}",
        alias="apiVersion",
    )
    kind: str = "BGPAdvertisement"
    pool: str
    peers: Sequence[str]
    local_pref: int | None
    communities: Sequence[str]

    status: Mapping[str, object] = EMPTY_MAPPING

    def manifest(self) -> Mapping[str, object]:
        """Render the Kubernetes BGPAdvertisement manifest.

        Returns
        -------
        Mapping[str, object]
            Complete Kubernetes custom-object manifest.
        """
        spec: dict[str, object] = {"ipAddressPools": [self.pool]}
        if self.peers:
            spec["peers"] = list(self.peers)
        if self.local_pref is not None:
            spec["localPref"] = self.local_pref
        if self.communities:
            spec["communities"] = list(self.communities)
        return {
            "apiVersion": f"{METALLB_GROUP}/{METALLB_V1BETA1}",
            "kind": "BGPAdvertisement",
            "metadata": self.metadata.manifest(),
            "spec": spec,
        }


@custom_resource(
    manifest=BGPAdvertisementManifest,
    group=METALLB_GROUP,
    version=METALLB_V1BETA1,
    kind="BGPAdvertisement",
    plural="bgpadvertisements",
    default_namespace=METALLB_NAMESPACE,
)
class BGPAdvertisement(CustomResource[BGPAdvertisementManifest]):
    """Wrapper around one MetalLB BGPAdvertisement custom object."""


class ConfigurationStateManifest(CustomObjectManifest):
    """Pull-side manifest for one MetalLB ConfigurationState."""

    api_version: str = Field(
        default=f"{METALLB_GROUP}/{METALLB_V1BETA1}",
        alias="apiVersion",
    )
    kind: str = "ConfigurationState"
    spec: Mapping[str, object] = EMPTY_MAPPING
    status: Mapping[str, object] = EMPTY_MAPPING


@custom_resource(
    manifest=ConfigurationStateManifest,
    group=METALLB_GROUP,
    version=METALLB_V1BETA1,
    kind="ConfigurationState",
    plural="configurationstates",
)
class ConfigurationState(CustomResource[ConfigurationStateManifest]):
    """Wrapper around one MetalLB ConfigurationState custom object."""


class ServiceL2StatusManifest(CustomObjectManifest):
    """Pull-side manifest for one MetalLB ServiceL2Status."""

    api_version: str = Field(
        default=f"{METALLB_GROUP}/{METALLB_V1BETA1}",
        alias="apiVersion",
    )
    kind: str = "ServiceL2Status"
    spec: Mapping[str, object] = EMPTY_MAPPING
    status: Mapping[str, object] = EMPTY_MAPPING


@custom_resource(
    manifest=ServiceL2StatusManifest,
    group=METALLB_GROUP,
    version=METALLB_V1BETA1,
    kind="ServiceL2Status",
    plural="servicel2statuses",
)
class ServiceL2Status(CustomResource[ServiceL2StatusManifest]):
    """Wrapper around one MetalLB ServiceL2Status custom object."""


class ServiceBGPStatusManifest(CustomObjectManifest):
    """Pull-side manifest for one MetalLB ServiceBGPStatus."""

    api_version: str = Field(
        default=f"{METALLB_GROUP}/{METALLB_V1BETA1}",
        alias="apiVersion",
    )
    kind: str = "ServiceBGPStatus"
    spec: Mapping[str, object] = EMPTY_MAPPING
    status: Mapping[str, object] = EMPTY_MAPPING


@custom_resource(
    manifest=ServiceBGPStatusManifest,
    group=METALLB_GROUP,
    version=METALLB_V1BETA1,
    kind="ServiceBGPStatus",
    plural="servicebgpstatuses",
)
class ServiceBGPStatus(CustomResource[ServiceBGPStatusManifest]):
    """Wrapper around one MetalLB ServiceBGPStatus custom object."""


async def upsert_ip_address_pool(
    kube: Kube,
    *,
    name: str,
    addresses: Sequence[str],
    auto_assign: bool,
    deadline: Deadline,
) -> IPAddressPool:
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
    IPAddressPool
        Created or patched IPAddressPool object.
    """
    name = _required_name(name, kind="IPAddressPool")
    normalized = _required_values(addresses, kind="IPAddressPool addresses")
    return await _managed_upsert(
        IPAddressPool,
        kube,
        kind="IPAddressPool",
        intent=IPAddressPoolManifest(
            metadata=CustomObjectMetadata(
                namespace=METALLB_NAMESPACE,
                name=name,
                labels=METALLB_LABELS,
            ),
            addresses=normalized,
            auto_assign=auto_assign,
        ),
        deadline=deadline,
    )


async def upsert_l2_advertisement(
    kube: Kube,
    *,
    name: str,
    pool: str,
    interfaces: Sequence[str],
    deadline: Deadline,
) -> L2Advertisement:
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
    L2Advertisement
        Created or patched L2Advertisement object.
    """
    name = _required_name(name, kind="L2Advertisement")
    pool = _required_name(pool, kind="IPAddressPool")
    normalized_interfaces = _string_tuple(interfaces)
    return await _managed_upsert(
        L2Advertisement,
        kube,
        kind="L2Advertisement",
        intent=L2AdvertisementManifest(
            metadata=CustomObjectMetadata(
                namespace=METALLB_NAMESPACE,
                name=name,
                labels=METALLB_LABELS,
            ),
            pool=pool,
            interfaces=normalized_interfaces,
        ),
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
) -> BGPPeer:
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
    BGPPeer
        Created or patched BGPPeer object.
    """
    name = _required_name(name, kind="BGPPeer")
    peer_address = _required_name(peer_address, kind="peer address")
    return await _managed_upsert(
        BGPPeer,
        kube,
        kind="BGPPeer",
        intent=BGPPeerManifest(
            metadata=CustomObjectMetadata(
                namespace=METALLB_NAMESPACE,
                name=name,
                labels=METALLB_LABELS,
            ),
            peer_address=peer_address,
            peer_asn=peer_asn,
            local_asn=local_asn,
            peer_port=peer_port,
            source_address=source_address,
            password_secret=password_secret,
        ),
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
) -> BGPAdvertisement:
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
    BGPAdvertisement
        Created or patched BGPAdvertisement object.
    """
    name = _required_name(name, kind="BGPAdvertisement")
    pool = _required_name(pool, kind="IPAddressPool")
    normalized_peers = _string_tuple(peers)
    normalized_communities = _string_tuple(communities)
    return await _managed_upsert(
        BGPAdvertisement,
        kube,
        kind="BGPAdvertisement",
        intent=BGPAdvertisementManifest(
            metadata=CustomObjectMetadata(
                namespace=METALLB_NAMESPACE,
                name=name,
                labels=METALLB_LABELS,
            ),
            pool=pool,
            peers=normalized_peers,
            local_pref=local_pref,
            communities=normalized_communities,
        ),
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
            IPAddressPool,
            kube,
            deadline=deadline,
        ),
        _safe_resource_list(
            L2Advertisement,
            kube,
            deadline=deadline,
        ),
        _safe_resource_list(
            BGPPeer,
            kube,
            deadline=deadline,
        ),
        _safe_resource_list(
            BGPAdvertisement,
            kube,
            deadline=deadline,
        ),
        _safe_resource_list(
            ConfigurationState,
            kube,
            deadline=deadline,
        ),
        _safe_resource_list(
            ServiceL2Status,
            kube,
            deadline=deadline,
        ),
        _safe_resource_list(
            ServiceBGPStatus,
            kube,
            deadline=deadline,
        ),
    )
    pools = cast("list[IPAddressPool]", pools)
    l2 = cast("list[L2Advertisement]", l2)
    peers = cast("list[BGPPeer]", peers)
    bgp = cast("list[BGPAdvertisement]", bgp)
    config_states = cast("list[ConfigurationState]", config_states)
    l2_status = cast("list[ServiceL2Status]", l2_status)
    bgp_status = cast("list[ServiceBGPStatus]", bgp_status)
    managed = _labels_managed(namespace.labels) if namespace is not None else False
    ready = (
        namespace is not None
        and controller is not None
        and controller.available_replicas >= 1
        and speaker is not None
        and speaker.number_available >= 1
        and all(crds.values())
    )
    messages: list[str] = []
    if namespace is None:
        messages.append("MetalLB namespace is missing")
    elif not managed:
        messages.append("MetalLB namespace exists but is not managed by Bertrand")
    if controller is None or controller.available_replicas < 1:
        messages.append("MetalLB controller Deployment is not Available")
    if speaker is None or speaker.number_available < 1:
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
            controller is not None and controller.available_replicas >= 1
        ),
        "speaker_ready": speaker is not None and speaker.number_available >= 1,
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
    namespace = await Namespace.get(
        kube, name=METALLB_NAMESPACE, deadline=deadline
    )
    if namespace is not None and not _labels_managed(namespace.labels):
        msg = (
            f"Namespace {METALLB_NAMESPACE!r} exists but is not managed by "
            "Bertrand. Inspect the existing MetalLB installation or remove it "
            "before running `bertrand cluster network lb install`."
        )
        raise OSError(msg)
    await Namespace.upsert(
        kube,
        intent=NamespaceManifest(
            name=METALLB_NAMESPACE,
            labels=METALLB_NAMESPACE_LABELS,
        ),
        deadline=deadline,
    )


async def _require_managed_metallb_namespace(kube: Kube, *, deadline: Deadline) -> None:
    namespace = await Namespace.get(
        kube, name=METALLB_NAMESPACE, deadline=deadline
    )
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
        if deployment.available_replicas < 1:
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
        if daemonset.number_available < 1:
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


async def _managed_upsert[T: CustomResource[Any]](
    resource: type[T],
    kube: Kube,
    *,
    kind: str,
    intent: CustomObjectManifest,
    deadline: Deadline,
) -> T:
    await _require_managed_metallb_namespace(kube, deadline=deadline)
    current = await resource.get(
        kube,
        namespace=METALLB_NAMESPACE,
        name=intent.name,
        deadline=deadline,
    )
    _assert_managed(current, kind=kind)
    return await resource.upsert(
        kube,
        intent=intent,
        deadline=deadline,
    )


async def _safe_resource_list[T: CustomResource[Any]](
    resource: type[T],
    kube: Kube,
    *,
    deadline: Deadline,
) -> list[T]:
    try:
        return await resource.list(kube, namespace=METALLB_NAMESPACE, deadline=deadline)
    except (OSError, TimeoutError, ValueError):
        return []


def _assert_managed(resource: CustomResource[Any] | None, *, kind: str) -> None:
    if resource is None:
        return
    if _labels_managed(resource.labels):
        return
    msg = (
        f"{kind} {METALLB_NAMESPACE}/{resource.name} exists but is not managed by "
        "Bertrand"
    )
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


def _pool_status(pool: IPAddressPool) -> dict[str, object]:
    return {
        "name": pool.name,
        "managed": _labels_managed(pool.labels),
        "addresses": list(_string_tuple(pool.spec.get("addresses", ()))),
        "auto_assign": bool(pool.spec.get("autoAssign", True)),
        "available_ipv4": _int_status(pool.status, "availableIPv4"),
        "assigned_ipv4": _int_status(pool.status, "assignedIPv4"),
    }


def _bgp_peer_status(peer: BGPPeer) -> dict[str, object]:
    return {
        "name": peer.name,
        "managed": _labels_managed(peer.labels),
        "peer_address": str(peer.spec.get("peerAddress") or "").strip(),
    }


def _object_summary(resource: CustomResource[Any]) -> dict[str, object]:
    return {
        "name": resource.name,
        "managed": _labels_managed(resource.labels),
        "pools": list(_string_tuple(resource.spec.get("ipAddressPools", ()))),
    }


def _custom_status_summary(resource: CustomResource[Any]) -> dict[str, object]:
    return {
        "name": resource.name,
        "status": dict(resource.status),
    }


def _configuration_errors(resources: Sequence[ConfigurationState]) -> list[str]:
    errors: list[str] = []
    for resource in resources:
        status = resource.status
        result = str(status.get("result") or "").strip()
        summary = str(status.get("errorSummary") or "").strip()
        if result and result.lower() != "valid":
            errors.append(f"MetalLB configuration {resource.name}: {summary or result}")
    return errors
