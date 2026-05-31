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
from kubernetes.config.config_exception import ConfigException

from bertrand.env.git import BERTRAND_NAMESPACE, Deadline
from bertrand.env.kube.api.bootstrap import (
    KUBE_CONFIG_FILE,
    ensure_microk8s_kubeconfig,
    kubeconfig_identity,
    microk8s_config_payload,
)

if TYPE_CHECKING:
    from collections.abc import Callable

CLUSTER_REGISTRY_READY_LABEL = "bertrand.dev/registry-ready"
CLUSTER_REGISTRY_READY_VALUE = "true"
_KUBE_CONFIG_ERRORS = (ConfigException, OSError, ValueError)


def _client_from_config(config_file: Path) -> kubernetes.client.ApiClient:
    try:
        return kubernetes.config.new_client_from_config(config_file=str(config_file))
    except _KUBE_CONFIG_ERRORS as err:
        msg = f"failed to initialize kubernetes client from {config_file}: {err}"
        raise OSError(msg) from err


def _incluster_configuration() -> kubernetes.client.Configuration:
    configuration = kubernetes.client.Configuration()
    try:
        kubernetes.config.load_incluster_config(client_configuration=configuration)
    except _KUBE_CONFIG_ERRORS as err:
        msg = f"failed to load in-cluster kubernetes configuration: {err}"
        raise OSError(msg) from err
    return configuration


class KubeApiError(OSError):
    """Structured Kubernetes API failure raised by :meth:`Kube.run`.

    Parameters
    ----------
    context : str
        Human-readable operation context.
    status : int
        Kubernetes API HTTP status code.
    detail : str
        Kubernetes API failure detail.
    """

    context: str
    status: int
    detail: str

    def __init__(self, *, context: str, status: int, detail: str) -> None:
        """Initialize a structured Kubernetes API error."""
        self.context = context
        self.status = status
        self.detail = detail
        super().__init__(
            f"{context} failed with kubernetes API status {status}: {detail}"
        )


def is_missing_api_resource(err: OSError) -> bool:
    """Return whether a Kubernetes 404 means the REST resource is unavailable.

    Parameters
    ----------
    err : OSError
        Error raised by :meth:`Kube.run`.

    Returns
    -------
    bool
        Whether the API server could not resolve the requested resource endpoint,
        rather than simply reporting a missing object instance.
    """
    if not isinstance(err, KubeApiError) or err.status != 404:
        return False
    return "the server could not find the requested resource" in err.detail.lower()


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
    networking : kubernetes.client.NetworkingV1Api
        Networking v1 API surface for NetworkPolicy resources.
    apiextensions : kubernetes.client.ApiextensionsV1Api
        API extensions v1 surface for CRDs.
    rbac : kubernetes.client.RbacAuthorizationV1Api
        RBAC authorization v1 API surface.
    storage : kubernetes.client.StorageV1Api
        Storage v1 API surface for StorageClass resources.
    coordination : kubernetes.client.CoordinationV1Api
        Coordination v1 API surface for Lease resources.
    """

    namespace: str
    client: kubernetes.client.ApiClient = field(repr=False)
    core: kubernetes.client.CoreV1Api = field(init=False, repr=False)
    apps: kubernetes.client.AppsV1Api = field(init=False, repr=False)
    custom: kubernetes.client.CustomObjectsApi = field(init=False, repr=False)
    batch: kubernetes.client.BatchV1Api = field(init=False, repr=False)
    networking: kubernetes.client.NetworkingV1Api = field(init=False, repr=False)
    apiextensions: kubernetes.client.ApiextensionsV1Api = field(init=False, repr=False)
    rbac: kubernetes.client.RbacAuthorizationV1Api = field(init=False, repr=False)
    storage: kubernetes.client.StorageV1Api = field(init=False, repr=False)
    coordination: kubernetes.client.CoordinationV1Api = field(init=False, repr=False)

    def __post_init__(self) -> None:
        """Initialize typed Kubernetes API handles from the shared transport.

        Raises
        ------
        AttributeError
            If the API client is missing attributes required by Kubernetes wrappers.
        TypeError
            If the API client cannot be used to construct Kubernetes wrappers.
        ValueError
            If the API client is rejected while constructing Kubernetes wrappers.
        """
        try:
            self.core = kubernetes.client.CoreV1Api(self.client)
            self.apps = kubernetes.client.AppsV1Api(self.client)
            self.custom = kubernetes.client.CustomObjectsApi(self.client)
            self.batch = kubernetes.client.BatchV1Api(self.client)
            self.networking = kubernetes.client.NetworkingV1Api(self.client)
            self.apiextensions = kubernetes.client.ApiextensionsV1Api(self.client)
            self.rbac = kubernetes.client.RbacAuthorizationV1Api(self.client)
            self.storage = kubernetes.client.StorageV1Api(self.client)
            self.coordination = kubernetes.client.CoordinationV1Api(self.client)
        except (AttributeError, TypeError, ValueError):
            with suppress(OSError, RuntimeError, ValueError):
                self.client.close()
            raise

    # TODO: `external()` is called from outside the cluster to implement the external
    # CLI.  `internal()` is called within the cluster to service the internal CLI.
    # `host()` or `control_plane()` should be called within a cluster control plane
    # context to get a client with permissions out to the host node???  Ideally, we
    # would collapse to just `external()` and `internal()`, but I'm not sure if that's
    # possible.  It might also be affected by

    @classmethod
    def external(
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
            config_file = KUBE_CONFIG_FILE
        if not config_file.is_file():
            msg = (
                f"kubernetes config is missing at {config_file}.  Run `bertrand init` "
                "to converge MicroK8s API access first."
            )
            raise OSError(msg)
        return cls(namespace=namespace, client=_client_from_config(config_file))

    @classmethod
    async def internal(
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
        """
        configuration = _incluster_configuration()
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

    @classmethod
    async def host(
        cls,
        *,
        deadline: Deadline,
        namespace: str = BERTRAND_NAMESPACE,
    ) -> Self:
        """Build a host-side Kubernetes client with strict local MicroK8s identity.

        Parameters
        ----------
        deadline : Deadline
            Maximum runtime budget.  If infinite, wait indefinitely.
        namespace : str, optional
            Default namespace for namespaced operations.

        Returns
        -------
        Kube
            Configured Kubernetes API wrapper.

        Raises
        ------
        OSError
            If managed kubeconfig convergence fails, identity proof fails, or API
            client initialization fails.
        """
        # NOTE: we always converge from `microk8s config` first so the managed
        # kubeconfig cannot drift from the local control-plane identity.
        config_file = await ensure_microk8s_kubeconfig(deadline=deadline)
        try:
            managed_payload = config_file.read_text(encoding="utf-8")
        except OSError as err:
            msg = f"failed to read managed kubeconfig at {config_file}: {err}"
            raise OSError(msg) from err
        fresh_payload = await microk8s_config_payload(deadline=deadline)

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

        return cls(namespace=namespace, client=_client_from_config(config_file))

    def close(self) -> None:
        """Close the Kubernetes API transport.

        This method is identical to the context manager exit handler, for cases when
        explicit context management is not possible due to logical constraints.
        """
        self.client.close()

    def __enter__(self) -> Self:
        """Enter the Kubernetes client context manager.

        Returns
        -------
        Kube
            This Kubernetes client instance.
        """
        return self

    def __exit__(self, exc_type: object, exc: object, exc_tb: object) -> None:
        """Close the Kubernetes API transport when leaving the context manager.

        Parameters
        ----------
        exc_type : object
            Exception type if raised within the context, else `None`.
        exc : object
            Exception instance if raised within the context, else `None`.
        exc_tb : object
            Exception traceback if raised within the context, else `None`.
        """
        self.close()

    async def run[T](
        self,
        fn: Callable[[float | None], T],
        *,
        deadline: Deadline,
        context: str,
        missing_ok: bool = True,
    ) -> T | None:
        """Run one Kubernetes API operation across the sync/async boundary.

        Parameters
        ----------
        fn : Callable[[float | None], T]
            Callable that performs one Kubernetes API operation and accepts the
            normalized Kubernetes request timeout (`None` for infinite waits).
        deadline : Deadline
            Maximum runtime budget.  If infinite, wait indefinitely.
        context : str
            Human-readable context for timeout and API error messages.
        missing_ok : bool, optional
            Whether HTTP 404 should be returned as `None` instead of raised as a
            structured API error.

        Returns
        -------
        T | None
            The API payload, or `None` if the operation returned HTTP 404 and
            `missing_ok` is true.

        Raises
        ------
        TimeoutError
            If the operation exceeds the deadline.
        KubeApiError
            If the API call fails with any non-404 error.
        """
        remaining = deadline.check(f"{context} timed out before request could start")
        request_timeout = None if math.isinf(remaining) else remaining
        try:
            return await asyncio.wait_for(
                asyncio.to_thread(fn, request_timeout),
                timeout=request_timeout,
            )
        except TimeoutError as err:
            msg = f"{context} timed out after {deadline.timeout} seconds"
            raise TimeoutError(msg) from err
        except ApiException as err:
            if err.status == 404 and missing_ok:
                return None
            detail = (err.body or err.reason or str(err)).strip()
            raise KubeApiError(
                context=context,
                status=int(err.status or 0),
                detail=detail,
            ) from err
