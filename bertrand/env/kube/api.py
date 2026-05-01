"""Shared Kubernetes API primitives for Bertrand's runtime orchestration.

This module centralizes Kubernetes API access utilities used across Bertrand's
kube subsystems.
"""
from __future__ import annotations

import asyncio
import math
from collections.abc import Callable, Mapping
from dataclasses import dataclass, field
from pathlib import Path
from typing import Self
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
        raise OSError(
            f"{source} kubeconfig has no cluster bound to context {current_context!r}"
        )

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
        raise OSError(
            f"{source} kubeconfig has no cluster payload named {cluster_name!r}"
        )

    server = str(cluster_payload.get("server") or "").strip()
    if not server:
        raise OSError(f"{source} kubeconfig is missing cluster.server")
    parsed = urlparse(server)
    if parsed.scheme != "https" or not parsed.hostname:
        raise OSError(
            f"{source} kubeconfig cluster.server must be a valid HTTPS URL, "
            f"got {server!r}"
        )

    ca_data = str(cluster_payload.get("certificate-authority-data") or "").strip()
    if not ca_data:
        raise OSError(
            f"{source} kubeconfig is missing cluster.certificate-authority-data"
        )

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
    custom : kubernetes.client.CustomObjectsApi
        Custom object API surface for CRD interactions.
    storage : kubernetes.client.StorageV1Api
        Storage v1 API surface for StorageClass resources.
    """
    namespace: str
    client: kubernetes.client.ApiClient = field(repr=False)
    core: kubernetes.client.CoreV1Api = field(init=False, repr=False)
    custom: kubernetes.client.CustomObjectsApi = field(init=False, repr=False)
    storage: kubernetes.client.StorageV1Api = field(init=False, repr=False)

    def __post_init__(self) -> None:
        try:
            self.core = kubernetes.client.CoreV1Api(self.client)
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
                client=kubernetes.config.new_client_from_config(
                    config_file=str(config_file)
                ),
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
            raise OSError(
                f"failed to read managed kubeconfig at {config_file}: {err}"
            ) from err
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
                client=kubernetes.config.new_client_from_config(
                    config_file=str(config_file)
                ),
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
