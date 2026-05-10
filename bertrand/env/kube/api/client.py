"""Kubernetes API client context for Bertrand runtime orchestration."""

from __future__ import annotations

import asyncio
import math
from contextlib import suppress
from dataclasses import dataclass, field
from pathlib import Path
from typing import TYPE_CHECKING, Self

import kubernetes
from kubernetes.client.rest import ApiException

from bertrand.env.git import BERTRAND_NAMESPACE

if TYPE_CHECKING:
    from collections.abc import Callable

CLUSTER_REGISTRY_READY_LABEL = "bertrand.dev/registry-ready"
CLUSTER_REGISTRY_READY_VALUE = "true"


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
        config_file: Path | None = None,
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
        if config_file is None:
            from .bootstrap import KUBE_CONFIG_FILE

            config_file = KUBE_CONFIG_FILE
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
        from .bootstrap import (
            ensure_microk8s_kubeconfig,
            kubeconfig_identity,
            microk8s_config_payload,
        )

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
        fresh_payload = await microk8s_config_payload(timeout=deadline - loop.time())

        managed_server, managed_ca = kubeconfig_identity(
            managed_payload,
            source=f"managed kubeconfig {config_file}",
        )
        local_server, local_ca = kubeconfig_identity(
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
