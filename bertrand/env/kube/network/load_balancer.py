"""MetalLB load-balancer helpers for Bertrand cluster networking."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
from typing import TYPE_CHECKING, Self

from bertrand.env.git import BERTRAND_ENV, until
from bertrand.env.kube.api.bootstrap import kubectl
from bertrand.env.kube.crd import CustomResourceDefinition
from bertrand.env.kube.custom_object import (
    CustomObject,
    CustomObjectClient,
    CustomObjectSpec,
)
from bertrand.env.kube.daemonset import DaemonSet
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.namespace import Namespace

if TYPE_CHECKING:
    import builtins
    from collections.abc import Awaitable, Callable, Mapping, Sequence

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
METALLB_LABELS = {
    "app.kubernetes.io/name": "metallb",
    "app.kubernetes.io/part-of": "bertrand",
    "app.kubernetes.io/component": "load-balancer",
    BERTRAND_ENV: "1",
    METALLB_LABEL: METALLB_LABEL_VALUE,
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

_IP_ADDRESS_POOL_CLIENT = CustomObjectClient(
    CustomObjectSpec(
        group=METALLB_GROUP,
        version=METALLB_V1BETA1,
        kind="IPAddressPool",
        plural="ipaddresspools",
    )
)
_L2_ADVERTISEMENT_CLIENT = CustomObjectClient(
    CustomObjectSpec(
        group=METALLB_GROUP,
        version=METALLB_V1BETA1,
        kind="L2Advertisement",
        plural="l2advertisements",
    )
)
_BGP_ADVERTISEMENT_CLIENT = CustomObjectClient(
    CustomObjectSpec(
        group=METALLB_GROUP,
        version=METALLB_V1BETA1,
        kind="BGPAdvertisement",
        plural="bgpadvertisements",
    )
)
_BGP_PEER_CLIENT = CustomObjectClient(
    CustomObjectSpec(
        group=METALLB_GROUP,
        version=METALLB_V1BETA2,
        kind="BGPPeer",
        plural="bgppeers",
    )
)
_CONFIGURATION_STATE_CLIENT = CustomObjectClient(
    CustomObjectSpec(
        group=METALLB_GROUP,
        version=METALLB_V1BETA1,
        kind="ConfigurationState",
        plural="configurationstates",
    )
)
_SERVICE_L2_STATUS_CLIENT = CustomObjectClient(
    CustomObjectSpec(
        group=METALLB_GROUP,
        version=METALLB_V1BETA1,
        kind="ServiceL2Status",
        plural="servicel2statuses",
    )
)
_SERVICE_BGP_STATUS_CLIENT = CustomObjectClient(
    CustomObjectSpec(
        group=METALLB_GROUP,
        version=METALLB_V1BETA1,
        kind="ServiceBGPStatus",
        plural="servicebgpstatuses",
    )
)


@dataclass(frozen=True)
class IPAddressPool:
    """Wrapper around one MetalLB IPAddressPool.

    Parameters
    ----------
    _obj : CustomObject
        Generic MetalLB custom object payload returned by Kubernetes.
    """

    _obj: CustomObject

    @classmethod
    async def get(cls, kube: Kube, *, name: str, timeout: float) -> Self | None:
        """Read one IPAddressPool by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            IPAddressPool name.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        IPAddressPool | None
            Wrapped IPAddressPool, or ``None`` if it does not exist.
        """
        obj = await _IP_ADDRESS_POOL_CLIENT.get(
            kube,
            namespace=METALLB_NAMESPACE,
            name=name,
            timeout=timeout,
        )
        return cls(obj) if obj is not None else None

    @classmethod
    async def list(cls, kube: Kube, *, timeout: float) -> builtins.list[Self]:
        """List MetalLB IPAddressPools.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        list[IPAddressPool]
            Wrapped IPAddressPools.
        """
        objects = await _IP_ADDRESS_POOL_CLIENT.list(
            kube,
            namespace=METALLB_NAMESPACE,
            timeout=timeout,
        )
        return [cls(obj) for obj in objects]

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        name: str,
        addresses: Sequence[str],
        auto_assign: bool,
        timeout: float,
    ) -> Self:
        """Create or patch one Bertrand-managed IPAddressPool.

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
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        IPAddressPool
            Wrapped created or patched IPAddressPool.
        """
        name = _required_name(name, kind="IPAddressPool")
        normalized = _required_values(addresses, kind="IPAddressPool addresses")
        await _require_managed_metallb_namespace(kube, timeout=timeout)
        current = await cls.get(kube, name=name, timeout=timeout)
        _assert_managed(current, kind="IPAddressPool")
        obj = await _IP_ADDRESS_POOL_CLIENT.upsert(
            kube,
            namespace=METALLB_NAMESPACE,
            name=name,
            spec={"addresses": list(normalized), "autoAssign": auto_assign},
            labels=METALLB_LABELS,
            timeout=timeout,
        )
        return cls(obj)

    @property
    def name(self) -> str:
        """Return the IPAddressPool name.

        Returns
        -------
        str
            Kubernetes `metadata.name`.
        """
        return self._obj.name

    @property
    def labels(self) -> Mapping[str, str]:
        """Return IPAddressPool labels.

        Returns
        -------
        Mapping[str, str]
            Read-only Kubernetes labels.
        """
        return self._obj.labels

    @property
    def addresses(self) -> tuple[str, ...]:
        """Return configured IP address ranges.

        Returns
        -------
        tuple[str, ...]
            Values from `spec.addresses`.
        """
        return _string_tuple(self._obj.spec.get("addresses", ()))

    @property
    def auto_assign(self) -> bool:
        """Return whether MetalLB may auto-allocate from this pool.

        Returns
        -------
        bool
            Value of `spec.autoAssign`, defaulting to ``True``.
        """
        return bool(self._obj.spec.get("autoAssign", True))

    @property
    def available_ipv4(self) -> int:
        """Return available IPv4 addresses reported by MetalLB.

        Returns
        -------
        int
            `status.availableIPv4`, or zero when unavailable.
        """
        return _int_status(self._obj.status, "availableIPv4")

    @property
    def assigned_ipv4(self) -> int:
        """Return assigned IPv4 addresses reported by MetalLB.

        Returns
        -------
        int
            `status.assignedIPv4`, or zero when unavailable.
        """
        return _int_status(self._obj.status, "assignedIPv4")


@dataclass(frozen=True)
class L2Advertisement:
    """Wrapper around one MetalLB L2Advertisement.

    Parameters
    ----------
    _obj : CustomObject
        Generic MetalLB custom object payload returned by Kubernetes.
    """

    _obj: CustomObject

    @classmethod
    async def get(cls, kube: Kube, *, name: str, timeout: float) -> Self | None:
        """Read one L2Advertisement by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            L2Advertisement name.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        L2Advertisement | None
            Wrapped L2Advertisement, or ``None`` if absent.
        """
        obj = await _L2_ADVERTISEMENT_CLIENT.get(
            kube,
            namespace=METALLB_NAMESPACE,
            name=name,
            timeout=timeout,
        )
        return cls(obj) if obj is not None else None

    @classmethod
    async def list(cls, kube: Kube, *, timeout: float) -> builtins.list[Self]:
        """List MetalLB L2Advertisements.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        list[L2Advertisement]
            Wrapped L2Advertisements.
        """
        objects = await _L2_ADVERTISEMENT_CLIENT.list(
            kube,
            namespace=METALLB_NAMESPACE,
            timeout=timeout,
        )
        return [cls(obj) for obj in objects]

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        name: str,
        pool: str,
        interfaces: Sequence[str],
        timeout: float,
    ) -> Self:
        """Create or patch one Bertrand-managed L2Advertisement.

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
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        L2Advertisement
            Wrapped created or patched L2Advertisement.
        """
        name = _required_name(name, kind="L2Advertisement")
        pool = _required_name(pool, kind="IPAddressPool")
        await _require_managed_metallb_namespace(kube, timeout=timeout)
        current = await cls.get(kube, name=name, timeout=timeout)
        _assert_managed(current, kind="L2Advertisement")
        spec: dict[str, object] = {"ipAddressPools": [pool]}
        normalized_interfaces = _string_tuple(interfaces)
        if normalized_interfaces:
            spec["interfaces"] = list(normalized_interfaces)
        obj = await _L2_ADVERTISEMENT_CLIENT.upsert(
            kube,
            namespace=METALLB_NAMESPACE,
            name=name,
            spec=spec,
            labels=METALLB_LABELS,
            timeout=timeout,
        )
        return cls(obj)

    @property
    def name(self) -> str:
        """Return the L2Advertisement name.

        Returns
        -------
        str
            Kubernetes `metadata.name`.
        """
        return self._obj.name

    @property
    def labels(self) -> Mapping[str, str]:
        """Return L2Advertisement labels.

        Returns
        -------
        Mapping[str, str]
            Read-only Kubernetes labels.
        """
        return self._obj.labels

    @property
    def pools(self) -> tuple[str, ...]:
        """Return selected IPAddressPools.

        Returns
        -------
        tuple[str, ...]
            Values from `spec.ipAddressPools`.
        """
        return _string_tuple(self._obj.spec.get("ipAddressPools", ()))


@dataclass(frozen=True)
class BGPPeer:
    """Wrapper around one MetalLB BGPPeer.

    Parameters
    ----------
    _obj : CustomObject
        Generic MetalLB custom object payload returned by Kubernetes.
    """

    _obj: CustomObject

    @classmethod
    async def get(cls, kube: Kube, *, name: str, timeout: float) -> Self | None:
        """Read one BGPPeer by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            BGPPeer name.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        BGPPeer | None
            Wrapped BGPPeer, or ``None`` if absent.
        """
        obj = await _BGP_PEER_CLIENT.get(
            kube,
            namespace=METALLB_NAMESPACE,
            name=name,
            timeout=timeout,
        )
        return cls(obj) if obj is not None else None

    @classmethod
    async def list(cls, kube: Kube, *, timeout: float) -> builtins.list[Self]:
        """List MetalLB BGPPeers.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        list[BGPPeer]
            Wrapped BGPPeers.
        """
        objects = await _BGP_PEER_CLIENT.list(
            kube,
            namespace=METALLB_NAMESPACE,
            timeout=timeout,
        )
        return [cls(obj) for obj in objects]

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        name: str,
        peer_address: str,
        peer_asn: int,
        local_asn: int,
        peer_port: int | None,
        source_address: str | None,
        password_secret: str | None,
        timeout: float,
    ) -> Self:
        """Create or patch one Bertrand-managed BGPPeer.

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
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        BGPPeer
            Wrapped created or patched BGPPeer.
        """
        name = _required_name(name, kind="BGPPeer")
        peer_address = _required_name(peer_address, kind="peer address")
        await _require_managed_metallb_namespace(kube, timeout=timeout)
        current = await cls.get(kube, name=name, timeout=timeout)
        _assert_managed(current, kind="BGPPeer")
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
        obj = await _BGP_PEER_CLIENT.upsert(
            kube,
            namespace=METALLB_NAMESPACE,
            name=name,
            spec=spec,
            labels=METALLB_LABELS,
            timeout=timeout,
        )
        return cls(obj)

    @property
    def name(self) -> str:
        """Return the BGPPeer name.

        Returns
        -------
        str
            Kubernetes `metadata.name`.
        """
        return self._obj.name

    @property
    def labels(self) -> Mapping[str, str]:
        """Return BGPPeer labels.

        Returns
        -------
        Mapping[str, str]
            Read-only Kubernetes labels.
        """
        return self._obj.labels

    @property
    def peer_address(self) -> str:
        """Return the remote router address.

        Returns
        -------
        str
            Value from `spec.peerAddress`.
        """
        return str(self._obj.spec.get("peerAddress") or "").strip()


@dataclass(frozen=True)
class BGPAdvertisement:
    """Wrapper around one MetalLB BGPAdvertisement.

    Parameters
    ----------
    _obj : CustomObject
        Generic MetalLB custom object payload returned by Kubernetes.
    """

    _obj: CustomObject

    @classmethod
    async def get(cls, kube: Kube, *, name: str, timeout: float) -> Self | None:
        """Read one BGPAdvertisement by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            BGPAdvertisement name.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        BGPAdvertisement | None
            Wrapped BGPAdvertisement, or ``None`` if absent.
        """
        obj = await _BGP_ADVERTISEMENT_CLIENT.get(
            kube,
            namespace=METALLB_NAMESPACE,
            name=name,
            timeout=timeout,
        )
        return cls(obj) if obj is not None else None

    @classmethod
    async def list(cls, kube: Kube, *, timeout: float) -> builtins.list[Self]:
        """List MetalLB BGPAdvertisements.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        list[BGPAdvertisement]
            Wrapped BGPAdvertisements.
        """
        objects = await _BGP_ADVERTISEMENT_CLIENT.list(
            kube,
            namespace=METALLB_NAMESPACE,
            timeout=timeout,
        )
        return [cls(obj) for obj in objects]

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        name: str,
        pool: str,
        peers: Sequence[str],
        local_pref: int | None,
        communities: Sequence[str],
        timeout: float,
    ) -> Self:
        """Create or patch one Bertrand-managed BGPAdvertisement.

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
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        BGPAdvertisement
            Wrapped created or patched BGPAdvertisement.
        """
        name = _required_name(name, kind="BGPAdvertisement")
        pool = _required_name(pool, kind="IPAddressPool")
        await _require_managed_metallb_namespace(kube, timeout=timeout)
        current = await cls.get(kube, name=name, timeout=timeout)
        _assert_managed(current, kind="BGPAdvertisement")
        spec: dict[str, object] = {"ipAddressPools": [pool]}
        normalized_peers = _string_tuple(peers)
        normalized_communities = _string_tuple(communities)
        if normalized_peers:
            spec["peers"] = list(normalized_peers)
        if local_pref is not None:
            spec["localPref"] = local_pref
        if normalized_communities:
            spec["communities"] = list(normalized_communities)
        obj = await _BGP_ADVERTISEMENT_CLIENT.upsert(
            kube,
            namespace=METALLB_NAMESPACE,
            name=name,
            spec=spec,
            labels=METALLB_LABELS,
            timeout=timeout,
        )
        return cls(obj)

    @property
    def name(self) -> str:
        """Return the BGPAdvertisement name.

        Returns
        -------
        str
            Kubernetes `metadata.name`.
        """
        return self._obj.name

    @property
    def labels(self) -> Mapping[str, str]:
        """Return BGPAdvertisement labels.

        Returns
        -------
        Mapping[str, str]
            Read-only Kubernetes labels.
        """
        return self._obj.labels

    @property
    def pools(self) -> tuple[str, ...]:
        """Return selected IPAddressPools.

        Returns
        -------
        tuple[str, ...]
            Values from `spec.ipAddressPools`.
        """
        return _string_tuple(self._obj.spec.get("ipAddressPools", ()))


async def ensure_metallb(kube: Kube, *, timeout: float) -> None:
    """Install and verify Bertrand-managed MetalLB.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum convergence budget in seconds. If infinite, wait indefinitely.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    """
    if timeout <= 0:
        msg = "MetalLB convergence timeout must be positive"
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    await _ensure_managed_metallb_namespace(kube, timeout=deadline - loop.time())
    await _apply_metallb(timeout=deadline - loop.time())
    for crd_name in METALLB_CRDS:
        await _wait_crd_established(
            kube,
            name=crd_name,
            timeout=deadline - loop.time(),
        )
    await _wait_controller_available(kube, timeout=deadline - loop.time())
    await _wait_speaker_available(kube, timeout=deadline - loop.time())


async def metallb_status(kube: Kube, *, timeout: float) -> dict[str, object]:
    """Return cluster MetalLB status without mutating it.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum request budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    dict[str, object]
        JSON-serializable MetalLB readiness and configuration summary.
    """
    namespace = await Namespace.get(kube, name=METALLB_NAMESPACE, timeout=timeout)
    managed = _labels_managed(namespace.labels) if namespace is not None else False
    controller = await Deployment.get(
        kube,
        namespace=METALLB_NAMESPACE,
        name=METALLB_CONTROLLER_DEPLOYMENT,
        timeout=timeout,
    )
    speaker = await DaemonSet.get(
        kube,
        namespace=METALLB_NAMESPACE,
        name=METALLB_SPEAKER_DAEMONSET,
        timeout=timeout,
    )
    crds = await _crd_status(kube, timeout=timeout)
    pools = await _safe_list(lambda: IPAddressPool.list(kube, timeout=timeout))
    l2 = await _safe_list(lambda: L2Advertisement.list(kube, timeout=timeout))
    peers = await _safe_list(lambda: BGPPeer.list(kube, timeout=timeout))
    bgp = await _safe_list(lambda: BGPAdvertisement.list(kube, timeout=timeout))
    config_states = await _safe_custom_list(
        _CONFIGURATION_STATE_CLIENT,
        kube,
        timeout=timeout,
    )
    l2_status = await _safe_custom_list(
        _SERVICE_L2_STATUS_CLIENT,
        kube,
        timeout=timeout,
    )
    bgp_status = await _safe_custom_list(
        _SERVICE_BGP_STATUS_CLIENT,
        kube,
        timeout=timeout,
    )
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
    timeout: float,
) -> None:
    namespace = await Namespace.get(kube, name=METALLB_NAMESPACE, timeout=timeout)
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
        timeout=timeout,
    )


async def _require_managed_metallb_namespace(kube: Kube, *, timeout: float) -> None:
    namespace = await Namespace.get(kube, name=METALLB_NAMESPACE, timeout=timeout)
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


async def _apply_metallb(*, timeout: float) -> None:
    try:
        await kubectl(
            ["apply", "--server-side", "-f", METALLB_INSTALL_URL],
            capture_output=True,
            timeout=timeout,
        )
    except OSError as err:
        msg = f"failed to apply MetalLB install manifest {METALLB_INSTALL_URL!r}"
        raise OSError(msg) from err


async def _wait_crd_established(
    kube: Kube,
    *,
    name: str,
    timeout: float,
) -> CustomResourceDefinition:
    async def established(remaining: float) -> CustomResourceDefinition:
        crd = await CustomResourceDefinition.get(kube, name=name, timeout=remaining)
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
            timeout=timeout,
            interval=METALLB_WAIT_POLL_INTERVAL_SECONDS,
            action=f"waiting for MetalLB CRD {name!r}",
        )
    except TimeoutError as err:
        msg = f"MetalLB CRD {name!r} did not become Established"
        raise OSError(msg) from err


async def _wait_controller_available(kube: Kube, *, timeout: float) -> Deployment:
    async def available(remaining: float) -> Deployment:
        deployment = await Deployment.get(
            kube,
            namespace=METALLB_NAMESPACE,
            name=METALLB_CONTROLLER_DEPLOYMENT,
            timeout=remaining,
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
            timeout=timeout,
            interval=METALLB_WAIT_POLL_INTERVAL_SECONDS,
            action="waiting for MetalLB controller Deployment",
        )
    except TimeoutError as err:
        msg = "MetalLB controller Deployment did not become Available"
        raise OSError(msg) from err


async def _wait_speaker_available(kube: Kube, *, timeout: float) -> DaemonSet:
    async def available(remaining: float) -> DaemonSet:
        daemonset = await DaemonSet.get(
            kube,
            namespace=METALLB_NAMESPACE,
            name=METALLB_SPEAKER_DAEMONSET,
            timeout=remaining,
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
            timeout=timeout,
            interval=METALLB_WAIT_POLL_INTERVAL_SECONDS,
            action="waiting for MetalLB speaker DaemonSet",
        )
    except TimeoutError as err:
        msg = "MetalLB speaker DaemonSet did not become Available"
        raise OSError(msg) from err


async def _crd_status(kube: Kube, *, timeout: float) -> dict[str, bool]:
    out: dict[str, bool] = {}
    for name in METALLB_CRDS:
        crd = await CustomResourceDefinition.get(kube, name=name, timeout=timeout)
        out[name] = crd is not None and crd.is_established
    return out


async def _safe_list[T](fn: Callable[[], Awaitable[Sequence[T]]]) -> list[T]:
    try:
        result = await fn()  # type: ignore[operator]
    except (OSError, TimeoutError, ValueError):
        return []
    return list(result)


async def _safe_custom_list(
    client: CustomObjectClient,
    kube: Kube,
    *,
    timeout: float,
) -> list[CustomObject]:
    try:
        return await client.list(kube, namespace=METALLB_NAMESPACE, timeout=timeout)
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
    return (
        labels.get(BERTRAND_ENV) == "1"
        and labels.get(METALLB_LABEL) == METALLB_LABEL_VALUE
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
        "addresses": list(pool.addresses),
        "auto_assign": pool.auto_assign,
        "available_ipv4": pool.available_ipv4,
        "assigned_ipv4": pool.assigned_ipv4,
    }


def _bgp_peer_status(peer: BGPPeer) -> dict[str, object]:
    return {
        "name": peer.name,
        "managed": _labels_managed(peer.labels),
        "peer_address": peer.peer_address,
    }


def _object_summary(resource: object) -> dict[str, object]:
    name = getattr(resource, "name", "")
    labels = getattr(resource, "labels", {})
    pools = getattr(resource, "pools", ())
    return {
        "name": name,
        "managed": _labels_managed(labels),
        "pools": list(pools),
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
