"""Shared Kubernetes API primitives for Bertrand's runtime orchestration.

This module centralizes Kubernetes API access utilities used across Bertrand's
kube subsystems.
"""

from __future__ import annotations

import asyncio
import math
from collections.abc import Callable, Collection, Mapping, Sequence
from contextlib import suppress
from dataclasses import dataclass, field
from pathlib import Path
from typing import Literal, Self
from urllib.parse import urlparse

import kubernetes
from kubernetes.client.rest import ApiException

from bertrand.env.run import BERTRAND_NAMESPACE, STATE_DIR, atomic_write_text, run

CLUSTER_REGISTRY_READY_LABEL = "bertrand.dev/registry-ready"
CLUSTER_REGISTRY_READY_VALUE = "true"
KUBE_CONFIG_FILE = STATE_DIR / "kubeconfig"
MICROK8S_KUBECONFIG_CONTEXT = "microk8s"
type PortProtocol = Literal["TCP", "UDP", "SCTP"]


def _label_selector(labels: Mapping[str, str] | None) -> str | None:
    if labels is None:
        return None
    if not labels:
        return None
    return ",".join(f"{k}={v}" for k, v in labels.items())


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


@dataclass(frozen=True)
class ServicePortSpec:
    """Intent-level Kubernetes Service port specification."""

    name: str
    port: int
    target_port: int | str
    protocol: PortProtocol = "TCP"
    node_port: int | None = None


@dataclass(frozen=True)
class ContainerPortSpec:
    """Intent-level Kubernetes container port specification."""

    name: str
    container_port: int
    protocol: PortProtocol = "TCP"


@dataclass(frozen=True)
class EnvVarSpec:
    """Intent-level Kubernetes container environment variable."""

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
    """Intent-level Kubernetes container volume mount."""

    name: str
    mount_path: str
    read_only: bool | None = None


@dataclass(frozen=True)
class ProbeSpec:
    """Intent-level Kubernetes container health probe."""

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
    """Intent-level Kubernetes container specification."""

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
    """Intent-level Kubernetes pod volume specification."""

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
