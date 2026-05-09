"""Shared Kubernetes API primitives for Bertrand's runtime orchestration.

This module centralizes Kubernetes API access utilities used across Bertrand's
kube subsystems.
"""

from __future__ import annotations

import asyncio
import math
from collections.abc import (
    AsyncIterator,
    Callable,
    Collection,
    Iterator,
    Mapping,
    Sequence,
)
from contextlib import suppress
from dataclasses import dataclass, field
from pathlib import Path
from types import MappingProxyType
from typing import Literal, Self, cast
from urllib.parse import urlparse

import kubernetes
from kubernetes.client.rest import ApiException

from bertrand.env.run import BERTRAND_NAMESPACE, STATE_DIR, atomic_write_text, run

CLUSTER_REGISTRY_READY_LABEL = "bertrand.dev/registry-ready"
CLUSTER_REGISTRY_READY_VALUE = "true"
KUBE_CONFIG_FILE = STATE_DIR / "kubeconfig"
MICROK8S_KUBECONFIG_CONTEXT = "microk8s"
type PortProtocol = Literal["TCP", "UDP", "SCTP"]
type WatchEventType = Literal["ADDED", "MODIFIED", "DELETED", "BOOKMARK", "ERROR"]
_WATCH_EVENT_TYPES: frozenset[str] = frozenset(
    {"ADDED", "MODIFIED", "DELETED", "BOOKMARK", "ERROR"}
)


@dataclass(frozen=True)
class ObjectReference:
    """Read-only Kubernetes object reference.

    Parameters
    ----------
    kind : str
        Referenced Kubernetes kind.
    namespace : str
        Referenced Kubernetes namespace, or an empty string for cluster-scoped
        objects.
    name : str
        Referenced Kubernetes object name.
    api_version : str
        Referenced Kubernetes API version.
    uid : str
        Referenced Kubernetes UID.
    resource_version : str
        Referenced Kubernetes resource version.
    """

    kind: str
    namespace: str
    name: str
    api_version: str = ""
    uid: str = ""
    resource_version: str = ""


@dataclass(frozen=True)
class ServicePortView:
    """Read-only Kubernetes Service port view.

    Parameters
    ----------
    name : str
        Service port name.
    port : int
        Service port number.
    target_port : int | str
        Target container port number or name.
    protocol : str
        Service port protocol.
    node_port : int | None
        Allocated or requested NodePort value, when present.
    """

    name: str
    port: int
    target_port: int | str
    protocol: str
    node_port: int | None = None


@dataclass(frozen=True)
class TaintView:
    """Read-only Kubernetes Node taint view.

    Parameters
    ----------
    key : str
        Taint key.
    effect : str
        Taint effect, such as `"NoSchedule"`.
    value : str
        Optional taint value.
    """

    key: str
    effect: str
    value: str = ""


@dataclass(frozen=True)
class WatchEvent[T]:
    """Typed Kubernetes watch event.

    Parameters
    ----------
    type : WatchEventType
        Normalized Kubernetes watch event type.
    object : T
        Wrapped Kubernetes resource object carried by the event.
    resource_version : str
        Resource version reported by the event object, or an empty string when
        unavailable.
    raw_type : str
        Raw event type string received from Kubernetes.
    """

    type: WatchEventType
    object: T
    resource_version: str
    raw_type: str


class WatchExpired(OSError):  # noqa: N818
    """Raised when Kubernetes expires a watch resource version."""


@dataclass(frozen=True)
class _WatchEnd:
    pass


_WATCH_END = _WatchEnd()


def _label_selector(labels: Mapping[str, str] | None) -> str | None:
    if labels is None:
        return None
    if not labels:
        return None
    return ",".join(f"{k}={v}" for k, v in labels.items())


def _next_watch_payload(iterator: Iterator[object]) -> object | _WatchEnd:
    try:
        return next(iterator)
    except StopIteration:
        return _WATCH_END


def _watch_resource_version(value: object) -> str:
    attr = getattr(value, "resource_version", None)
    if isinstance(attr, str):
        return attr.strip()
    if isinstance(value, Mapping):
        value = cast("Mapping[str, object]", value)
        metadata = value.get("metadata")
        if isinstance(metadata, Mapping):
            metadata = cast("Mapping[str, object]", metadata)
            return str(metadata.get("resourceVersion") or "").strip()
    return ""


def _watch_error_status(value: object) -> tuple[int | None, str]:
    if isinstance(value, Mapping):
        value = cast("Mapping[str, object]", value)
        raw_code = value.get("code")
        reason = str(value.get("reason") or "").strip()
        message = str(value.get("message") or "").strip()
    else:
        raw_code = getattr(value, "code", None)
        reason = str(getattr(value, "reason", "") or "").strip()
        message = str(getattr(value, "message", "") or "").strip()

    code: int | None = None
    if isinstance(raw_code, int):
        code = raw_code
    elif raw_code is not None:
        with suppress(ValueError):
            code = int(str(raw_code).strip())

    detail = message or reason or str(value).strip()
    return code, detail


def _kubeconfig_identity(payload: str, *, source: str) -> tuple[str, str]:
    try:
        raw = kubernetes.config.kube_config.yaml.safe_load(payload)
    except Exception as err:
        msg = f"{source} is not valid kubeconfig YAML: {err}"
        raise OSError(msg) from err
    if not isinstance(raw, Mapping):
        msg = f"{source} kubeconfig must deserialize into a mapping"
        raise OSError(msg)

    current_context = str(raw.get("current-context") or "").strip()
    if not current_context:
        msg = f"{source} kubeconfig is missing 'current-context'"
        raise OSError(msg)

    # NOTE: we lock to MicroK8s' canonical context name so a mismatched config
    # cannot silently retarget Bertrand to another cluster.
    if current_context != MICROK8S_KUBECONFIG_CONTEXT:
        msg = (
            f"{source} kubeconfig must use current-context "
            f"{MICROK8S_KUBECONFIG_CONTEXT!r}, got {current_context!r}"
        )
        raise OSError(msg)

    cluster_name = ""
    contexts = raw.get("contexts")
    if not isinstance(contexts, list):
        msg = f"{source} kubeconfig is missing context list"
        raise OSError(msg)
    for entry in contexts:
        if not isinstance(entry, Mapping):
            continue
        if str(entry.get("name") or "").strip() != current_context:
            continue
        context = entry.get("context")
        if isinstance(context, Mapping):
            cluster_name = str(context.get("cluster") or "").strip()
            break
    if not cluster_name:
        msg = f"{source} kubeconfig has no cluster bound to context {current_context!r}"
        raise OSError(msg)

    clusters = raw.get("clusters")
    if not isinstance(clusters, list):
        msg = f"{source} kubeconfig is missing cluster list"
        raise OSError(msg)
    cluster_payload: Mapping[str, object] | None = None
    for entry in clusters:
        if not isinstance(entry, Mapping):
            continue
        if str(entry.get("name") or "").strip() != cluster_name:
            continue
        cluster = entry.get("cluster")
        if isinstance(cluster, Mapping):
            cluster_payload = cluster
            break
    if cluster_payload is None:
        msg = f"{source} kubeconfig has no cluster payload named {cluster_name!r}"
        raise OSError(msg)

    server = str(cluster_payload.get("server") or "").strip()
    if not server:
        msg = f"{source} kubeconfig is missing cluster.server"
        raise OSError(msg)
    parsed = urlparse(server)
    if parsed.scheme != "https" or not parsed.hostname:
        msg = (
            f"{source} kubeconfig cluster.server must be a valid HTTPS URL, "
            f"got {server!r}"
        )
        raise OSError(msg)

    ca_data = str(cluster_payload.get("certificate-authority-data") or "").strip()
    if not ca_data:
        msg = f"{source} kubeconfig is missing cluster.certificate-authority-data"
        raise OSError(msg)

    return server, ca_data


async def _microk8s_config_payload(*, timeout: float) -> str:
    if timeout <= 0:
        msg = "kubeconfig timeout must be non-negative"
        raise TimeoutError(msg)
    result = await run(
        ["microk8s", "config"],
        capture_output=True,
        timeout=timeout,
    )
    text = result.stdout.strip()
    if not text:
        msg = "microk8s config returned an empty kubeconfig payload"
        raise OSError(msg)
    return text if text.endswith("\n") else f"{text}\n"


async def ensure_microk8s_kubeconfig(*, timeout: float) -> Path:
    """Converge Bertrand-managed kubeconfig from the local MicroK8s runtime.

    Parameters
    ----------
    timeout : float
        Maximum runtime budget in seconds.  If infinite, wait indefinitely.

    Returns
    -------
    Path
        The managed kubeconfig path that was converged.
    """
    payload = await _microk8s_config_payload(timeout=timeout)

    if KUBE_CONFIG_FILE.is_file():
        try:
            if KUBE_CONFIG_FILE.read_text(encoding="utf-8") == payload:
                return KUBE_CONFIG_FILE
        except OSError:
            pass

    atomic_write_text(
        KUBE_CONFIG_FILE,
        payload,
        encoding="utf-8",
        private=True,
    )
    return KUBE_CONFIG_FILE


@dataclass
class Kube:
    """Context-managed Kubernetes client wrapper for Bertrand runtime operations.

    Attributes
    ----------
    namespace : str
        Default namespace used for namespaced Kubernetes resources.
    client : kubernetes.client.ApiClient
        Underlying Kubernetes API transport instance.
    core : kubernetes.client.CoreV1Api
        Core v1 API surface for typed built-in resources.
    apps : kubernetes.client.AppsV1Api
        Apps v1 API surface for Deployments and related workload resources.
    custom : kubernetes.client.CustomObjectsApi
        Custom object API surface for CRD interactions.
    batch : kubernetes.client.BatchV1Api
        Batch v1 API surface for Jobs and related execution resources.
    apiextensions : kubernetes.client.ApiextensionsV1Api
        API extensions v1 surface for CRDs.
    rbac : kubernetes.client.RbacAuthorizationV1Api
        RBAC authorization v1 API surface.
    storage : kubernetes.client.StorageV1Api
        Storage v1 API surface for StorageClass resources.
    events : kubernetes.client.EventsV1Api
        Events v1 API surface for diagnostic Event resources.
    coordination : kubernetes.client.CoordinationV1Api
        Coordination v1 API surface for Lease resources.
    """

    namespace: str
    client: kubernetes.client.ApiClient = field(repr=False)
    core: kubernetes.client.CoreV1Api = field(init=False, repr=False)
    apps: kubernetes.client.AppsV1Api = field(init=False, repr=False)
    custom: kubernetes.client.CustomObjectsApi = field(init=False, repr=False)
    batch: kubernetes.client.BatchV1Api = field(init=False, repr=False)
    apiextensions: kubernetes.client.ApiextensionsV1Api = field(init=False, repr=False)
    rbac: kubernetes.client.RbacAuthorizationV1Api = field(init=False, repr=False)
    storage: kubernetes.client.StorageV1Api = field(init=False, repr=False)
    events: kubernetes.client.EventsV1Api = field(init=False, repr=False)
    coordination: kubernetes.client.CoordinationV1Api = field(init=False, repr=False)

    def __post_init__(self) -> None:
        """Initialize typed Kubernetes API handles from the shared transport."""
        try:
            self.core = kubernetes.client.CoreV1Api(self.client)
            self.apps = kubernetes.client.AppsV1Api(self.client)
            self.custom = kubernetes.client.CustomObjectsApi(self.client)
            self.batch = kubernetes.client.BatchV1Api(self.client)
            self.apiextensions = kubernetes.client.ApiextensionsV1Api(self.client)
            self.rbac = kubernetes.client.RbacAuthorizationV1Api(self.client)
            self.storage = kubernetes.client.StorageV1Api(self.client)
            self.events = kubernetes.client.EventsV1Api(self.client)
            self.coordination = kubernetes.client.CoordinationV1Api(self.client)
        except Exception:
            with suppress(Exception):
                self.client.close()
            raise

    @classmethod
    def outside_cluster(
        cls,
        *,
        namespace: str = BERTRAND_NAMESPACE,
        config_file: Path = KUBE_CONFIG_FILE,
    ) -> Self:
        """Build a host-side API client from Bertrand's managed kubeconfig.

        Parameters
        ----------
        namespace : str, optional
            Default namespace for namespaced operations.
        config_file : Path, optional
            Path to the kubeconfig file used for host-side API access.

        Returns
        -------
        Kube
            Configured Kubernetes API wrapper.

        Raises
        ------
        OSError
            If the kubeconfig is missing or cannot be loaded.  This constructor does
            not run `microk8s config` convergence checks; use :meth:`host` for the
            strict Bertrand-managed path.
        """
        if not config_file.is_file():
            msg = (
                f"kubernetes config is missing at {config_file}.  Run `bertrand init` "
                "to converge MicroK8s API access first."
            )
            raise OSError(msg)
        try:
            return cls(
                namespace=namespace,
                client=kubernetes.config.new_client_from_config(
                    config_file=str(config_file)
                ),
            )
        except Exception as err:
            msg = f"failed to initialize kubernetes client from {config_file}: {err}"
            raise OSError(msg) from err

    @classmethod
    async def host(
        cls,
        *,
        timeout: float,
        namespace: str = BERTRAND_NAMESPACE,
    ) -> Self:
        """Build a host-side Kubernetes client with strict local MicroK8s identity.

        Parameters
        ----------
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.
        namespace : str, optional
            Default namespace for namespaced operations.

        Returns
        -------
        Kube
            Configured Kubernetes API wrapper.

        Raises
        ------
        TimeoutError
            If convergence/proof exceed the timeout budget.
        OSError
            If managed kubeconfig convergence fails, identity proof fails, or API
            client initialization fails.
        """
        if timeout <= 0:
            msg = "kubernetes host-client timeout must be non-negative"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout

        # NOTE: we always converge from `microk8s config` first so the managed
        # kubeconfig cannot drift from the local control-plane identity.
        config_file = await ensure_microk8s_kubeconfig(timeout=deadline - loop.time())
        try:
            managed_payload = config_file.read_text(encoding="utf-8")
        except OSError as err:
            msg = f"failed to read managed kubeconfig at {config_file}: {err}"
            raise OSError(msg) from err
        fresh_payload = await _microk8s_config_payload(timeout=deadline - loop.time())

        managed_server, managed_ca = _kubeconfig_identity(
            managed_payload,
            source=f"managed kubeconfig {config_file}",
        )
        local_server, local_ca = _kubeconfig_identity(
            fresh_payload,
            source="`microk8s config`",
        )
        if managed_server != local_server or managed_ca != local_ca:
            msg = (
                "managed kubeconfig identity does not match local MicroK8s identity; "
                "run `bertrand init` to reconverge host kube access."
            )
            raise OSError(msg)

        try:
            return cls(
                namespace=namespace,
                client=kubernetes.config.new_client_from_config(
                    config_file=str(config_file)
                ),
            )
        except Exception as err:
            msg = f"failed to initialize kubernetes client from {config_file}: {err}"
            raise OSError(msg) from err

    @classmethod
    def inside_cluster(
        cls,
        *,
        namespace: str | None = None,
    ) -> Self:
        """Build an in-cluster API client from projected ServiceAccount credentials.

        Parameters
        ----------
        namespace : str | None, optional
            Default namespace for namespaced operations.  If omitted, this is read
            from the projected ServiceAccount namespace file.

        Returns
        -------
        Kube
            Configured in-cluster Kubernetes API wrapper.

        Raises
        ------
        OSError
            If in-cluster configuration cannot be loaded.
        """
        configuration = kubernetes.client.Configuration()
        try:
            kubernetes.config.load_incluster_config(client_configuration=configuration)
        except Exception as err:
            msg = f"failed to load in-cluster kubernetes configuration: {err}"
            raise OSError(msg) from err

        resolved_namespace = namespace
        if resolved_namespace is None:
            namespace_path = Path(
                "/var/run/secrets/kubernetes.io/serviceaccount/namespace"
            )
            resolved_namespace = (
                namespace_path.read_text(encoding="utf-8").strip()
                if namespace_path.is_file()
                else ""
            )
        if not resolved_namespace:
            resolved_namespace = BERTRAND_NAMESPACE

        return cls(
            namespace=str(resolved_namespace),
            client=kubernetes.client.ApiClient(configuration=configuration),
        )

    def __enter__(self) -> Self:
        """Enter the Kubernetes client context manager.

        Returns
        -------
        Kube
            This Kubernetes API wrapper.
        """
        return self

    def __exit__(self, exc_type: object, exc: object, tb: object) -> None:
        """Close the Kubernetes API transport when leaving the context manager."""
        self.client.close()

    async def run[T](
        self,
        fn: Callable[[float | None], T],
        *,
        timeout: float,
        context: str,
    ) -> T | None:
        """Run one Kubernetes API operation across the sync/async boundary.

        Parameters
        ----------
        fn : Callable[[float | None], T]
            Callable that performs one Kubernetes API operation and accepts the
            normalized Kubernetes request timeout (`None` for infinite waits).
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.
        context : str
            Human-readable context for timeout and API error messages.

        Returns
        -------
        T | None
            The API payload, or `None` if the operation returned HTTP 404.

        Raises
        ------
        TimeoutError
            If the operation exceeds the timeout budget.
        OSError
            If the API call fails with any non-404 error.
        """
        if timeout <= 0:
            msg = f"{context} timed out before request could start"
            raise TimeoutError(msg)
        request_timeout = None if math.isinf(timeout) else timeout
        try:
            return await asyncio.wait_for(
                asyncio.to_thread(fn, request_timeout),
                timeout=request_timeout,
            )
        except TimeoutError as err:
            msg = f"{context} timed out after {timeout} seconds"
            raise TimeoutError(msg) from err
        except ApiException as err:
            if err.status == 404:
                return None
            detail = (err.body or err.reason or str(err)).strip()
            msg = f"{context} failed with kubernetes API status {err.status}: {detail}"
            raise OSError(msg) from err

    async def watch[T](
        self,
        fn: Callable[..., object],
        *,
        wrapper: Callable[[object], T],
        timeout: float,
        context: str,
        resource_version: str | None = None,
        labels: Mapping[str, str] | None = None,
        field_selector: str | None = None,
        api_kwargs: Mapping[str, object] | None = None,
    ) -> AsyncIterator[WatchEvent[T]]:
        """Stream typed Kubernetes watch events across the sync/async boundary.

        Parameters
        ----------
        fn : Callable[..., object]
            Generated Kubernetes list/watch API function.
        wrapper : Callable[[object], T]
            Callback that validates and wraps each raw event object.
        timeout : float
            Maximum watch budget in seconds. If infinite, wait indefinitely.
        context : str
            Human-readable context for timeout and API error messages.
        resource_version : str | None, optional
            Resource version to watch from.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.
        field_selector : str | None, optional
            Raw Kubernetes field selector.
        api_kwargs : Mapping[str, object] | None, optional
            Additional keyword arguments for the generated API function.

        Yields
        ------
        WatchEvent[T]
            Typed watch events containing wrapped Kubernetes objects.

        Raises
        ------
        TimeoutError
            If the watch exceeds the timeout budget.
        WatchExpired
            If Kubernetes returns HTTP 410 for an expired resource version.
        OSError
            If Kubernetes returns malformed watch data or any non-410 API failure.
        """
        if timeout <= 0:
            msg = f"{context} watch timed out before request could start"
            raise TimeoutError(msg)

        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        kwargs = dict(api_kwargs or {})
        label_selector = _label_selector(labels)
        if label_selector is not None:
            kwargs["label_selector"] = label_selector
        if field_selector is not None:
            field_selector = field_selector.strip()
            if field_selector:
                kwargs["field_selector"] = field_selector
        if resource_version is not None:
            resource_version = resource_version.strip()
            if resource_version:
                kwargs["resource_version"] = resource_version
        if not math.isinf(timeout):
            kwargs.setdefault("timeout_seconds", max(1, math.ceil(timeout)))

        watcher = kubernetes.watch.Watch()
        iterator = watcher.stream(fn, **kwargs)
        try:
            while True:
                remaining = deadline - loop.time()
                if remaining <= 0:
                    msg = f"{context} watch timed out after {timeout} seconds"
                    raise TimeoutError(msg)
                try:
                    payload = await asyncio.wait_for(
                        asyncio.to_thread(_next_watch_payload, iterator),
                        timeout=None if math.isinf(remaining) else remaining,
                    )
                except TimeoutError as err:
                    msg = f"{context} watch timed out after {timeout} seconds"
                    raise TimeoutError(msg) from err
                except ApiException as err:
                    if err.status == 410:
                        detail = (err.body or err.reason or str(err)).strip()
                        msg = f"{context} watch expired: {detail}"
                        raise WatchExpired(msg) from err
                    detail = (err.body or err.reason or str(err)).strip()
                    msg = (
                        f"{context} watch failed with kubernetes API status "
                        f"{err.status}: {detail}"
                    )
                    raise OSError(msg) from err

                if payload is _WATCH_END:
                    return
                if not isinstance(payload, Mapping):
                    msg = f"{context} watch returned malformed event payload"
                    raise OSError(msg)
                payload = cast("Mapping[str, object]", payload)

                raw_type = str(payload.get("type") or "").strip()
                if raw_type not in _WATCH_EVENT_TYPES:
                    msg = f"{context} watch returned unknown event type {raw_type!r}"
                    raise OSError(msg)
                event_type = cast("WatchEventType", raw_type)

                raw_object = payload.get("object")
                if raw_object is None:
                    raw_object = payload.get("raw_object")
                if raw_object is None:
                    msg = f"{context} watch event is missing object payload"
                    raise OSError(msg)
                if event_type == "ERROR":
                    code, detail = _watch_error_status(raw_object)
                    if code == 410:
                        msg = f"{context} watch expired: {detail}"
                        raise WatchExpired(msg)
                    if detail:
                        msg = f"{context} watch returned an error event: {detail}"
                    else:
                        msg = f"{context} watch returned an error event"
                    raise OSError(msg)
                obj = wrapper(raw_object)
                yield WatchEvent(
                    type=event_type,
                    object=obj,
                    resource_version=_watch_resource_version(obj)
                    or _watch_resource_version(raw_object),
                    raw_type=raw_type,
                )
        finally:
            watcher.stop()


@dataclass(frozen=True)
class ServicePortSpec:
    """Intent-level Kubernetes Service port specification.

    Parameters
    ----------
    name : str
        Stable port name exposed by the Service.
    port : int
        Service port number.
    target_port : int | str
        Container port number or named port selected by the Service.
    protocol : {"TCP", "UDP", "SCTP"}, optional
        Network protocol for the Service port.
    node_port : int | None, optional
        Fixed NodePort value for `NodePort` Services.
    """

    name: str
    port: int
    target_port: int | str
    protocol: PortProtocol = "TCP"
    node_port: int | None = None


@dataclass(frozen=True)
class PolicyRuleSpec:
    """Intent-level Kubernetes RBAC policy rule specification.

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
class CustomResourceSpec:
    """Intent-level namespaced Kubernetes custom resource specification.

    Parameters
    ----------
    group : str
        Kubernetes API group that owns the resource.
    version : str
        Served API version for the resource.
    kind : str
        Kubernetes kind name.
    plural : str
        Plural REST resource name.
    labels : Mapping[str, str], optional
        Default labels to apply to objects created through this spec.
    """

    group: str
    version: str
    kind: str
    plural: str
    labels: Mapping[str, str] = MappingProxyType({})

    @property
    def api_version(self) -> str:
        """Return the fully qualified Kubernetes API version.

        Returns
        -------
        str
            Fully qualified Kubernetes API version for objects of this type.
        """
        return f"{self.group}/{self.version}"


@dataclass(frozen=True)
class ContainerPortSpec:
    """Intent-level Kubernetes container port specification.

    Parameters
    ----------
    name : str
        Stable name for the container port.
    container_port : int
        Port number exposed by the container.
    protocol : {"TCP", "UDP", "SCTP"}, optional
        Network protocol for the container port.
    """

    name: str
    container_port: int
    protocol: PortProtocol = "TCP"


@dataclass(frozen=True)
class EnvVarSpec:
    """Intent-level Kubernetes container environment variable.

    Parameters
    ----------
    name : str
        Environment variable name.
    value : str | None, optional
        Literal environment variable value.
    field_path : str | None, optional
        Kubernetes field path to project with `valueFrom.fieldRef`.
    """

    name: str
    value: str | None = None
    field_path: str | None = None

    @classmethod
    def field_ref(cls, name: str, *, field_path: str) -> Self:
        """Create an environment variable from a Kubernetes field reference.

        Parameters
        ----------
        name : str
            Environment variable name.
        field_path : str
            Kubernetes field path to project into the environment variable.

        Returns
        -------
        Self
            Environment variable specification.
        """
        return cls(name=name, field_path=field_path)


@dataclass(frozen=True)
class VolumeMountSpec:
    """Intent-level Kubernetes container volume mount.

    Parameters
    ----------
    name : str
        Name of the pod volume to mount.
    mount_path : str
        Container path where the volume is mounted.
    read_only : bool | None, optional
        Whether the mount is read-only. `None` leaves the Kubernetes default.
    """

    name: str
    mount_path: str
    read_only: bool | None = None


@dataclass(frozen=True)
class ProbeSpec:
    """Intent-level Kubernetes container health probe.

    Parameters
    ----------
    handler : Mapping[str, object]
        Kubernetes probe handler payload, such as `tcpSocket` or `httpGet`.
    initial_delay_seconds : int | None, optional
        Delay before the first probe.
    period_seconds : int | None, optional
        Interval between probes.
    failure_threshold : int | None, optional
        Number of failed probes before Kubernetes marks the container unhealthy.
    """

    handler: Mapping[str, object]
    initial_delay_seconds: int | None = None
    period_seconds: int | None = None
    failure_threshold: int | None = None

    @classmethod
    def tcp(
        cls,
        *,
        port: int | str,
        initial_delay_seconds: int | None = None,
        period_seconds: int | None = None,
        failure_threshold: int | None = None,
    ) -> Self:
        """Create a TCP socket probe.

        Parameters
        ----------
        port : int | str
            Container port number or named port to probe.
        initial_delay_seconds : int | None, optional
            Delay before the first probe.
        period_seconds : int | None, optional
            Interval between probes.
        failure_threshold : int | None, optional
            Number of failed probes before Kubernetes marks the container unhealthy.

        Returns
        -------
        Self
            Probe specification.
        """
        return cls(
            handler={"tcpSocket": {"port": port}},
            initial_delay_seconds=initial_delay_seconds,
            period_seconds=period_seconds,
            failure_threshold=failure_threshold,
        )

    @classmethod
    def http(
        cls,
        *,
        path: str,
        port: int | str,
        initial_delay_seconds: int | None = None,
        period_seconds: int | None = None,
        failure_threshold: int | None = None,
    ) -> Self:
        """Create an HTTP GET probe.

        Parameters
        ----------
        path : str
            HTTP path to request.
        port : int | str
            Container port number or named port to probe.
        initial_delay_seconds : int | None, optional
            Delay before the first probe.
        period_seconds : int | None, optional
            Interval between probes.
        failure_threshold : int | None, optional
            Number of failed probes before Kubernetes marks the container unhealthy.

        Returns
        -------
        Self
            Probe specification.
        """
        return cls(
            handler={"httpGet": {"path": path, "port": port}},
            initial_delay_seconds=initial_delay_seconds,
            period_seconds=period_seconds,
            failure_threshold=failure_threshold,
        )


@dataclass(frozen=True)
class ContainerSpec:
    """Intent-level Kubernetes container specification.

    Parameters
    ----------
    name : str
        Container name.
    image : str
        Container image reference.
    image_pull_policy : str | None, optional
        Kubernetes image pull policy.
    command : Sequence[str] | None, optional
        Container entrypoint command.
    args : Sequence[str] | None, optional
        Container command arguments.
    ports : Collection[ContainerPortSpec], optional
        Container ports to expose.
    env : Collection[EnvVarSpec], optional
        Container environment variables.
    readiness_probe : ProbeSpec | None, optional
        Readiness probe intent.
    liveness_probe : ProbeSpec | None, optional
        Liveness probe intent.
    volume_mounts : Collection[VolumeMountSpec], optional
        Pod volume mounts for the container.
    security_context : Mapping[str, object] | None, optional
        Kubernetes container security context payload.
    """

    name: str
    image: str
    image_pull_policy: str | None = None
    command: Sequence[str] | None = None
    args: Sequence[str] | None = None
    ports: Collection[ContainerPortSpec] = ()
    env: Collection[EnvVarSpec] = ()
    readiness_probe: ProbeSpec | None = None
    liveness_probe: ProbeSpec | None = None
    volume_mounts: Collection[VolumeMountSpec] = ()
    security_context: Mapping[str, object] | None = None


@dataclass(frozen=True)
class VolumeSpec:
    """Intent-level Kubernetes pod volume specification.

    Parameters
    ----------
    name : str
        Pod volume name.
    empty_dir_config : Mapping[str, object] | None, optional
        `emptyDir` volume configuration.
    config_map_name : str | None, optional
        ConfigMap name for ConfigMap-backed volumes.
    config_map_optional : bool | None, optional
        Whether the ConfigMap reference is optional.
    persistent_volume_claim : str | None, optional
        PersistentVolumeClaim name for PVC-backed volumes.
    host_path_path : str | None, optional
        Host path for hostPath-backed volumes.
    host_path_type : str | None, optional
        Optional Kubernetes hostPath type constraint.
    """

    name: str
    empty_dir_config: Mapping[str, object] | None = None
    config_map_name: str | None = None
    config_map_optional: bool | None = None
    persistent_volume_claim: str | None = None
    host_path_path: str | None = None
    host_path_type: str | None = None

    @classmethod
    def empty_dir(
        cls,
        name: str,
        config: Mapping[str, object] | None = None,
    ) -> Self:
        """Create an `emptyDir` volume specification.

        Parameters
        ----------
        name : str
            Pod volume name.
        config : Mapping[str, object] | None, optional
            Raw `emptyDir` configuration.

        Returns
        -------
        Self
            Volume specification.
        """
        return cls(name=name, empty_dir_config=dict(config or {}))

    @classmethod
    def config_map(
        cls,
        name: str,
        *,
        config_map_name: str,
        optional: bool | None = None,
    ) -> Self:
        """Create a ConfigMap-backed volume specification.

        Parameters
        ----------
        name : str
            Pod volume name.
        config_map_name : str
            Name of the ConfigMap to mount.
        optional : bool | None, optional
            Whether the ConfigMap reference is optional.

        Returns
        -------
        Self
            Volume specification.
        """
        return cls(
            name=name,
            config_map_name=config_map_name,
            config_map_optional=optional,
        )

    @classmethod
    def pvc(cls, name: str, *, claim_name: str) -> Self:
        """Create a PVC-backed volume specification.

        Parameters
        ----------
        name : str
            Pod volume name.
        claim_name : str
            Name of the PersistentVolumeClaim to mount.

        Returns
        -------
        Self
            Volume specification.
        """
        return cls(name=name, persistent_volume_claim=claim_name)

    @classmethod
    def host_path(
        cls,
        name: str,
        *,
        path: str | Path,
        host_path_type: str | None = None,
    ) -> Self:
        """Create a hostPath-backed volume specification.

        Parameters
        ----------
        name : str
            Pod volume name.
        path : str | Path
            Host path to mount.
        host_path_type : str | None, optional
            Optional Kubernetes hostPath type constraint.

        Returns
        -------
        Self
            Volume specification.
        """
        return cls(name=name, host_path_path=str(path), host_path_type=host_path_type)


def _probe_manifest(probe: ProbeSpec) -> dict[str, object]:
    payload = dict(probe.handler)
    if probe.initial_delay_seconds is not None:
        payload["initialDelaySeconds"] = probe.initial_delay_seconds
    if probe.period_seconds is not None:
        payload["periodSeconds"] = probe.period_seconds
    if probe.failure_threshold is not None:
        payload["failureThreshold"] = probe.failure_threshold
    return payload


def _container_manifest(container: ContainerSpec) -> dict[str, object]:
    payload: dict[str, object] = {
        "name": container.name,
        "image": container.image,
    }
    if container.image_pull_policy is not None:
        payload["imagePullPolicy"] = container.image_pull_policy
    if container.command is not None:
        payload["command"] = list(container.command)
    if container.args is not None:
        payload["args"] = list(container.args)
    if container.ports:
        payload["ports"] = [
            {
                "name": port.name,
                "containerPort": port.container_port,
                "protocol": port.protocol,
            }
            for port in container.ports
        ]
    if container.env:
        env: list[dict[str, object]] = []
        for var in container.env:
            sources = sum(value is not None for value in (var.value, var.field_path))
            if sources != 1:
                msg = "environment variable must define exactly one source"
                raise ValueError(msg)
            item: dict[str, object] = {"name": var.name}
            if var.value is not None:
                item["value"] = var.value
            elif var.field_path is not None:
                item["valueFrom"] = {"fieldRef": {"fieldPath": var.field_path}}
            env.append(item)
        payload["env"] = env
    if container.readiness_probe is not None:
        payload["readinessProbe"] = _probe_manifest(container.readiness_probe)
    if container.liveness_probe is not None:
        payload["livenessProbe"] = _probe_manifest(container.liveness_probe)
    if container.volume_mounts:
        payload["volumeMounts"] = [
            {
                key: value
                for key, value in {
                    "name": mount.name,
                    "mountPath": mount.mount_path,
                    "readOnly": mount.read_only,
                }.items()
                if value is not None
            }
            for mount in container.volume_mounts
        ]
    if container.security_context is not None:
        payload["securityContext"] = dict(container.security_context)
    return payload


def _volume_manifest(volume: VolumeSpec) -> dict[str, object]:
    kinds = sum(
        value is not None
        for value in (
            volume.empty_dir_config,
            volume.config_map_name,
            volume.persistent_volume_claim,
            volume.host_path_path,
        )
    )
    if kinds != 1:
        msg = "Kubernetes volume must define exactly one source"
        raise ValueError(msg)

    payload: dict[str, object] = {"name": volume.name}
    if volume.empty_dir_config is not None:
        payload["emptyDir"] = dict(volume.empty_dir_config)
    elif volume.config_map_name is not None:
        config_map: dict[str, object] = {"name": volume.config_map_name}
        if volume.config_map_optional is not None:
            config_map["optional"] = volume.config_map_optional
        payload["configMap"] = config_map
    elif volume.persistent_volume_claim is not None:
        payload["persistentVolumeClaim"] = {"claimName": volume.persistent_volume_claim}
    elif volume.host_path_path is not None:
        host_path: dict[str, object] = {"path": volume.host_path_path}
        if volume.host_path_type is not None:
            host_path["type"] = volume.host_path_type
        payload["hostPath"] = host_path
    return payload


def _pod_template_manifest(
    *,
    labels: Mapping[str, str],
    containers: Collection[ContainerSpec],
    volumes: Collection[VolumeSpec],
    automount_service_account_token: bool,
    service_account_name: str | None,
    node_selector: Mapping[str, str] | None,
    host_pid: bool | None,
) -> dict[str, object]:
    spec: dict[str, object] = {
        "automountServiceAccountToken": automount_service_account_token,
        "containers": [_container_manifest(container) for container in containers],
        "volumes": [_volume_manifest(volume) for volume in volumes],
    }
    if service_account_name is not None:
        service_account_name = service_account_name.strip()
        if service_account_name:
            spec["serviceAccountName"] = service_account_name
    if node_selector:
        spec["nodeSelector"] = dict(node_selector)
    if host_pid is not None:
        spec["hostPID"] = host_pid
    return {
        "metadata": {"labels": dict(labels)},
        "spec": spec,
    }
