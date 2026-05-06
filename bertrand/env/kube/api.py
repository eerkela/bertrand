"""Shared Kubernetes API primitives for Bertrand's runtime orchestration.

This module centralizes Kubernetes API access utilities used across Bertrand's
kube subsystems.
"""

from __future__ import annotations

import asyncio
import math
from collections.abc import Callable, Collection, Mapping, Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Literal, Self
from urllib.parse import urlparse

import kubernetes
from kubernetes.client.rest import ApiException

from ..run import BERTRAND_NAMESPACE, STATE_DIR, atomic_write_text, run

CLUSTER_REGISTRY_READY_LABEL = "bertrand.dev/registry-ready"
CLUSTER_REGISTRY_READY_VALUE = "true"
KUBE_CONFIG_FILE = STATE_DIR / "kubeconfig"
MICROK8S_KUBECONFIG_CONTEXT = "microk8s"


def _label_selector(labels: Mapping[str, str] | None) -> str | None:
    """Render Kubernetes label selectors from key-value dictionaries."""
    if labels is None:
        return None
    if not labels:
        return None
    return ",".join(f"{k}={v}" for k, v in labels.items())


def _kubeconfig_identity(payload: str, *, source: str) -> tuple[str, str]:
    """Extract `(server, certificate-authority-data)` from one kubeconfig payload."""
    try:
        raw = kubernetes.config.kube_config.yaml.safe_load(payload)
    except Exception as err:
        raise OSError(f"{source} is not valid kubeconfig YAML: {err}") from err
    if not isinstance(raw, Mapping):
        raise OSError(f"{source} kubeconfig must deserialize into a mapping")

    current_context = str(raw.get("current-context") or "").strip()
    if not current_context:
        raise OSError(f"{source} kubeconfig is missing 'current-context'")

    # NOTE: we lock to MicroK8s' canonical context name so a mismatched config
    # cannot silently retarget Bertrand to another cluster.
    if current_context != MICROK8S_KUBECONFIG_CONTEXT:
        raise OSError(
            f"{source} kubeconfig must use current-context "
            f"{MICROK8S_KUBECONFIG_CONTEXT!r}, got {current_context!r}"
        )

    cluster_name = ""
    contexts = raw.get("contexts")
    if not isinstance(contexts, list):
        raise OSError(f"{source} kubeconfig is missing context list")
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
        raise OSError(f"{source} kubeconfig has no cluster bound to context {current_context!r}")

    clusters = raw.get("clusters")
    if not isinstance(clusters, list):
        raise OSError(f"{source} kubeconfig is missing cluster list")
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
        raise OSError(f"{source} kubeconfig has no cluster payload named {cluster_name!r}")

    server = str(cluster_payload.get("server") or "").strip()
    if not server:
        raise OSError(f"{source} kubeconfig is missing cluster.server")
    parsed = urlparse(server)
    if parsed.scheme != "https" or not parsed.hostname:
        raise OSError(
            f"{source} kubeconfig cluster.server must be a valid HTTPS URL, got {server!r}"
        )

    ca_data = str(cluster_payload.get("certificate-authority-data") or "").strip()
    if not ca_data:
        raise OSError(f"{source} kubeconfig is missing cluster.certificate-authority-data")

    return server, ca_data


async def _microk8s_config_payload(*, timeout: float) -> str:
    if timeout <= 0:
        raise TimeoutError("kubeconfig timeout must be non-negative")
    result = await run(
        ["microk8s", "config"],
        capture_output=True,
        timeout=timeout,
    )
    text = result.stdout.strip()
    if not text:
        raise OSError("microk8s config returned an empty kubeconfig payload")
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

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive or command execution exceeds the budget.
    OSError
        If `microk8s config` returns an empty payload.
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
    storage : kubernetes.client.StorageV1Api
        Storage v1 API surface for StorageClass resources.
    """

    namespace: str
    client: kubernetes.client.ApiClient = field(repr=False)
    core: kubernetes.client.CoreV1Api = field(init=False, repr=False)
    apps: kubernetes.client.AppsV1Api = field(init=False, repr=False)
    custom: kubernetes.client.CustomObjectsApi = field(init=False, repr=False)
    storage: kubernetes.client.StorageV1Api = field(init=False, repr=False)

    def __post_init__(self) -> None:
        try:
            self.core = kubernetes.client.CoreV1Api(self.client)
            self.apps = kubernetes.client.AppsV1Api(self.client)
            self.custom = kubernetes.client.CustomObjectsApi(self.client)
            self.storage = kubernetes.client.StorageV1Api(self.client)
        except Exception:
            try:
                self.client.close()
            except Exception:
                pass
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
            raise OSError(
                f"kubernetes config is missing at {config_file}.  Run `bertrand init` "
                "to converge MicroK8s API access first."
            )
        try:
            return cls(
                namespace=namespace,
                client=kubernetes.config.new_client_from_config(config_file=str(config_file)),
            )
        except Exception as err:
            raise OSError(
                f"failed to initialize kubernetes client from {config_file}: {err}"
            ) from err

    @classmethod
    async def host(
        cls,
        *,
        timeout: float,
        namespace: str = BERTRAND_NAMESPACE,
    ) -> Self:
        """Build a host-side Kubernetes client with strict local MicroK8s identity proof.

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
            raise TimeoutError("kubernetes host-client timeout must be non-negative")
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout

        # NOTE: we always converge from `microk8s config` first so the managed
        # kubeconfig cannot drift from the local control-plane identity.
        config_file = await ensure_microk8s_kubeconfig(timeout=deadline - loop.time())
        try:
            managed_payload = config_file.read_text(encoding="utf-8")
        except OSError as err:
            raise OSError(f"failed to read managed kubeconfig at {config_file}: {err}") from err
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
            raise OSError(
                "managed kubeconfig identity does not match local MicroK8s identity; "
                "run `bertrand init` to reconverge host kube access."
            )

        try:
            return cls(
                namespace=namespace,
                client=kubernetes.config.new_client_from_config(config_file=str(config_file)),
            )
        except Exception as err:
            raise OSError(
                f"failed to initialize kubernetes client from {config_file}: {err}"
            ) from err

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
            raise OSError(f"failed to load in-cluster kubernetes configuration: {err}") from err

        resolved_namespace = namespace
        if resolved_namespace is None:
            namespace_path = Path("/var/run/secrets/kubernetes.io/serviceaccount/namespace")
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
        return self

    def __exit__(self, exc_type: object, exc: object, tb: object) -> None:
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
            raise TimeoutError(f"{context} timed out before request could start")
        request_timeout = None if math.isinf(timeout) else timeout
        try:
            return await asyncio.wait_for(
                asyncio.to_thread(fn, request_timeout),
                timeout=request_timeout,
            )
        except TimeoutError as err:
            raise TimeoutError(f"{context} timed out after {timeout} seconds") from err
        except ApiException as err:
            if err.status == 404:
                return None
            detail = (err.body or err.reason or str(err)).strip()
            raise OSError(
                f"{context} failed with kubernetes API status {err.status}: {detail}"
            ) from err


type PortProtocol = Literal["TCP", "UDP", "SCTP"]


@dataclass(frozen=True)
class ServicePortSpec:
    """Intent-level Kubernetes Service port specification."""

    name: str
    port: int
    target_port: int | str
    protocol: PortProtocol = "TCP"
    node_port: int | None = None

    def manifest(self) -> dict[str, object]:
        """Render this port as a Kubernetes Service port manifest."""
        payload: dict[str, object] = {
            "name": self.name,
            "port": self.port,
            "targetPort": self.target_port,
            "protocol": self.protocol,
        }
        if self.node_port is not None:
            payload["nodePort"] = self.node_port
        return payload


@dataclass(frozen=True)
class ContainerPortSpec:
    """Intent-level Kubernetes container port specification."""

    name: str
    container_port: int
    protocol: PortProtocol = "TCP"

    def manifest(self) -> dict[str, object]:
        """Render this port as a Kubernetes container port manifest."""
        return {
            "name": self.name,
            "containerPort": self.container_port,
            "protocol": self.protocol,
        }


@dataclass(frozen=True)
class EnvVarSpec:
    """Intent-level Kubernetes container environment variable."""

    name: str
    value: str

    def manifest(self) -> dict[str, object]:
        """Render this environment variable as a Kubernetes manifest."""
        return {"name": self.name, "value": self.value}


@dataclass(frozen=True)
class VolumeMountSpec:
    """Intent-level Kubernetes container volume mount."""

    name: str
    mount_path: str
    read_only: bool | None = None

    def manifest(self) -> dict[str, object]:
        """Render this mount as a Kubernetes volume mount manifest."""
        payload: dict[str, object] = {
            "name": self.name,
            "mountPath": self.mount_path,
        }
        if self.read_only is not None:
            payload["readOnly"] = self.read_only
        return payload


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
        """Create a TCP socket probe."""
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
        """Create an HTTP GET probe."""
        return cls(
            handler={"httpGet": {"path": path, "port": port}},
            initial_delay_seconds=initial_delay_seconds,
            period_seconds=period_seconds,
            failure_threshold=failure_threshold,
        )

    def manifest(self) -> dict[str, object]:
        """Render this probe as a Kubernetes manifest."""
        payload = dict(self.handler)
        if self.initial_delay_seconds is not None:
            payload["initialDelaySeconds"] = self.initial_delay_seconds
        if self.period_seconds is not None:
            payload["periodSeconds"] = self.period_seconds
        if self.failure_threshold is not None:
            payload["failureThreshold"] = self.failure_threshold
        return payload


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

    def manifest(self) -> dict[str, object]:
        """Render this container as a Kubernetes manifest."""
        payload: dict[str, object] = {
            "name": self.name,
            "image": self.image,
        }
        if self.image_pull_policy is not None:
            payload["imagePullPolicy"] = self.image_pull_policy
        if self.command is not None:
            payload["command"] = list(self.command)
        if self.args is not None:
            payload["args"] = list(self.args)
        if self.ports:
            payload["ports"] = [port.manifest() for port in self.ports]
        if self.env:
            payload["env"] = [var.manifest() for var in self.env]
        if self.readiness_probe is not None:
            payload["readinessProbe"] = self.readiness_probe.manifest()
        if self.liveness_probe is not None:
            payload["livenessProbe"] = self.liveness_probe.manifest()
        if self.volume_mounts:
            payload["volumeMounts"] = [mount.manifest() for mount in self.volume_mounts]
        if self.security_context is not None:
            payload["securityContext"] = dict(self.security_context)
        return payload


@dataclass(frozen=True)
class VolumeSpec:
    """Intent-level Kubernetes pod volume specification."""

    name: str
    empty_dir_config: Mapping[str, object] | None = None
    config_map_name: str | None = None
    config_map_optional: bool | None = None
    persistent_volume_claim: str | None = None

    @classmethod
    def empty_dir(
        cls,
        name: str,
        config: Mapping[str, object] | None = None,
    ) -> Self:
        """Create an `emptyDir` volume specification."""
        return cls(name=name, empty_dir_config=dict(config or {}))

    @classmethod
    def config_map(
        cls,
        name: str,
        *,
        config_map_name: str,
        optional: bool | None = None,
    ) -> Self:
        """Create a ConfigMap-backed volume specification."""
        return cls(name=name, config_map_name=config_map_name, config_map_optional=optional)

    @classmethod
    def pvc(cls, name: str, *, claim_name: str) -> Self:
        """Create a PVC-backed volume specification."""
        return cls(name=name, persistent_volume_claim=claim_name)

    def manifest(self) -> dict[str, object]:
        """Render this volume as a Kubernetes manifest."""
        kinds = sum(
            value is not None
            for value in (
                self.empty_dir_config,
                self.config_map_name,
                self.persistent_volume_claim,
            )
        )
        if kinds != 1:
            raise ValueError("Kubernetes volume must define exactly one source")

        payload: dict[str, object] = {"name": self.name}
        if self.empty_dir_config is not None:
            payload["emptyDir"] = dict(self.empty_dir_config)
        elif self.config_map_name is not None:
            config_map: dict[str, object] = {"name": self.config_map_name}
            if self.config_map_optional is not None:
                config_map["optional"] = self.config_map_optional
            payload["configMap"] = config_map
        elif self.persistent_volume_claim is not None:
            payload["persistentVolumeClaim"] = {"claimName": self.persistent_volume_claim}
        return payload
