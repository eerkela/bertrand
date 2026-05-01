"""Shared Kubernetes API primitives for Bertrand's runtime orchestration.

This module centralizes Kubernetes API access utilities used across Bertrand's
kube subsystems.
"""
from __future__ import annotations

import asyncio
import base64
import binascii
import builtins
import math
import re
from collections.abc import Callable, Mapping
from dataclasses import dataclass, field
from decimal import Decimal, InvalidOperation
from pathlib import Path
from typing import Self
from urllib.parse import urlparse

import kubernetes
from kubernetes.client.rest import ApiException

from ..config.core import KubeName
from ..run import BERTRAND_NAMESPACE, STATE_DIR, JSONValue, atomic_write_text, run

PVC_GROW_RETRIES = 4
CLUSTER_REGISTRY_READY_LABEL = "bertrand.dev/registry-ready"
CLUSTER_REGISTRY_READY_VALUE = "true"
QUANTITY_RE = re.compile(r"^([0-9]+(?:\.[0-9]+)?)([A-Za-z]{0,2})$")
STORAGE_FACTORS: dict[str, Decimal] = {
    "": Decimal(1),
    "m": Decimal("0.001"),
    "k": Decimal(10) ** 3,
    "K": Decimal(10) ** 3,
    "M": Decimal(10) ** 6,
    "G": Decimal(10) ** 9,
    "T": Decimal(10) ** 12,
    "P": Decimal(10) ** 15,
    "E": Decimal(10) ** 18,
    "Ki": Decimal(2) ** 10,
    "Mi": Decimal(2) ** 20,
    "Gi": Decimal(2) ** 30,
    "Ti": Decimal(2) ** 40,
    "Pi": Decimal(2) ** 50,
    "Ei": Decimal(2) ** 60,
}
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


@dataclass(frozen=True)
class KubeSecret:
    """Thin wrapper around one Kubernetes Secret object."""
    obj: kubernetes.client.V1Secret

    @classmethod
    async def get(
        cls,
        *,
        kube: Kube,
        namespace: str,
        timeout: float,
        name: KubeName,
    ) -> Self | None:
        """Read one Kubernetes Secret by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            The namespace to search within.
        timeout : float
            The maximum time to wait for Kubernetes Secret query in seconds.  If
            infinite, wait indefinitely.
        name : str
            Secret name to read.

        Returns
        -------
        KubeSecret | None
            Validated Kubernetes Secret wrapper, or `None` if the secret does not
            exist.

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds the timeout budget.
        OSError
            If the Kubernetes API call fails or returns malformed data.
        """
        payload = await kube.run(
            lambda request_timeout: kube.core.read_namespaced_secret(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=(
                f"failed to read cluster secret {name!r} in namespace "
                f"{namespace!r}"
            ),
        )
        if payload is None:
            return None
        if not isinstance(payload, kubernetes.client.V1Secret):
            raise OSError(
                f"malformed Kubernetes Secret payload for {name!r} in "
                f"namespace {namespace!r}"
            )
        return cls(obj=payload)

    @classmethod
    async def list(
        cls,
        *,
        kube: Kube,
        namespace: str,
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[Self]:
        """List Kubernetes Secrets in one namespace with optional label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            The namespace to search within.
        timeout : float
            The maximum time to wait for Kubernetes Secret list queries in seconds.
            If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Optional label filters.

        Returns
        -------
        builtins.list[KubeSecret]
            Validated Kubernetes Secret wrappers.

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds the timeout budget.
        OSError
            If the Kubernetes API call fails or returns malformed data.
        """
        payload = await kube.run(
            lambda request_timeout: kube.core.list_namespaced_secret(
                namespace=namespace,
                label_selector=_label_selector(labels),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to list Kubernetes Secrets in namespace {namespace!r}",
        )
        if payload is None:
            return []
        if not isinstance(payload, kubernetes.client.V1SecretList):
            raise OSError(
                f"malformed Kubernetes Secret list payload in namespace {namespace!r}"
            )
        out: builtins.list[Self] = []
        for item in payload.items or []:
            if not isinstance(item, kubernetes.client.V1Secret):
                raise OSError("malformed Kubernetes Secret entry in list payload")
            out.append(cls(obj=item))
        return out

    def decode(self, name: KubeName) -> bytes:
        """Decode a base64-encoded value from the wrapped Secret payload.

        Parameters
        ----------
        name : str
            Secret name for diagnostic errors.

        Returns
        -------
        bytes
            Decoded payload from `data["value"]`.

        Raises
        ------
        OSError
            If required key is missing or invalid base64.
        """
        value = (self.obj.data or {}).get("value")
        if value is None:
            raise OSError(
                f"cluster secret {name!r} does not define required key 'data.value'"
            )
        try:
            return base64.b64decode(value, validate=True)
        except (binascii.Error, ValueError) as err:
            raise OSError(
                f"cluster secret {name!r} contains invalid base64 data for key "
                f"'data.value'"
            ) from err


@dataclass(frozen=True)
class StorageClass:
    """Thin wrapper around one Kubernetes StorageClass object."""
    obj: kubernetes.client.V1StorageClass

    @classmethod
    async def get(
        cls,
        *,
        kube: Kube,
        timeout: float,
        name: KubeName,
    ) -> Self | None:
        """Read one Kubernetes StorageClass by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            The maximum time to wait for Kubernetes StorageClass queries in seconds.
            If infinite, wait indefinitely.
        name : str
            StorageClass name to read.

        Returns
        -------
        StorageClass | None
            Validated Kubernetes StorageClass wrapper, or `None` if it does not exist.

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds the timeout budget.
        OSError
            If the Kubernetes API call fails or returns malformed data.
        """
        payload = await kube.run(
            lambda request_timeout: kube.storage.read_storage_class(
                name=name,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to read StorageClass {name!r}",
        )
        if payload is None:
            return None
        if not isinstance(payload, kubernetes.client.V1StorageClass):
            raise OSError(f"malformed Kubernetes StorageClass payload for {name!r}")
        return cls(obj=payload)

    @classmethod
    async def list(
        cls,
        *,
        kube: Kube,
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[Self]:
        """List Kubernetes StorageClasses with optional label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            The maximum time to wait for Kubernetes StorageClass list queries in
            seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Optional label filters.

        Returns
        -------
        builtins.list[StorageClass]
            Validated Kubernetes StorageClass wrappers.

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds the timeout budget.
        OSError
            If the Kubernetes API call fails or returns malformed data.
        """
        payload = await kube.run(
            lambda request_timeout: kube.storage.list_storage_class(
                label_selector=_label_selector(labels),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context="failed to list Kubernetes StorageClasses",
        )
        if payload is None:
            return []
        if not isinstance(payload, kubernetes.client.V1StorageClassList):
            raise OSError("malformed Kubernetes StorageClass list payload")
        out: builtins.list[Self] = []
        for item in payload.items or []:
            if not isinstance(item, kubernetes.client.V1StorageClass):
                raise OSError("malformed Kubernetes StorageClass entry in list payload")
            out.append(cls(obj=item))
        return out


@dataclass(frozen=True)
class PersistentVolumeClaim:
    """Thin wrapper around one Kubernetes PersistentVolumeClaim object."""
    obj: kubernetes.client.V1PersistentVolumeClaim

    @staticmethod
    def _requested_storage(spec: kubernetes.client.V1PersistentVolumeClaimSpec) -> str:
        resources = spec.resources or kubernetes.client.V1VolumeResourceRequirements()
        requests = resources.requests or {}
        value = str(requests.get("storage") or "").strip()
        if not value:
            raise OSError("PVC does not expose a valid storage request quantity")
        return value

    @classmethod
    async def get(
        cls,
        *,
        kube: Kube,
        namespace: str,
        timeout: float,
        name: KubeName,
    ) -> Self | None:
        """Read one Kubernetes PersistentVolumeClaim by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            The namespace to search within.
        timeout : float
            The maximum time to wait for Kubernetes PVC query in seconds.  If
            infinite, wait indefinitely.
        name : str
            PersistentVolumeClaim name to read.

        Returns
        -------
        PersistentVolumeClaim | None
            Validated Kubernetes PersistentVolumeClaim wrapper, or `None` if it does
            not exist.

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds the timeout budget.
        OSError
            If the Kubernetes API call fails or returns malformed data.
        """
        payload = await kube.run(
            lambda request_timeout: kube.core.read_namespaced_persistent_volume_claim(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to read PVC {name!r} in namespace {namespace!r}",
        )
        if payload is None:
            return None
        if not isinstance(payload, kubernetes.client.V1PersistentVolumeClaim):
            raise OSError(
                f"malformed Kubernetes PVC payload for {name!r} in namespace "
                f"{namespace!r}"
            )
        return cls(obj=payload)

    @classmethod
    async def list(
        cls,
        *,
        kube: Kube,
        namespace: str,
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[Self]:
        """List Kubernetes PersistentVolumeClaims with optional label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            The namespace to search within.
        timeout : float
            The maximum time to wait for Kubernetes PVC list queries in seconds.  If
            infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Optional label filters.

        Returns
        -------
        builtins.list[PersistentVolumeClaim]
            Validated Kubernetes PersistentVolumeClaim wrappers.

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds the timeout budget.
        OSError
            If the Kubernetes API call fails or returns malformed data.
        """
        payload = await kube.run(
            lambda request_timeout: kube.core.list_namespaced_persistent_volume_claim(
                namespace=namespace,
                label_selector=_label_selector(labels),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=(
                "failed to list Kubernetes PersistentVolumeClaims in namespace "
                f"{namespace!r}"
            ),
        )
        if payload is None:
            return []
        if not isinstance(payload, kubernetes.client.V1PersistentVolumeClaimList):
            raise OSError(
                "malformed Kubernetes PersistentVolumeClaim list payload in "
                f"namespace {namespace!r}"
            )
        out: builtins.list[Self] = []
        for item in payload.items or []:
            if not isinstance(item, kubernetes.client.V1PersistentVolumeClaim):
                raise OSError(
                    "malformed Kubernetes PersistentVolumeClaim entry in list payload"
                )
            out.append(cls(obj=item))
        return out

    @classmethod
    async def create(
        cls,
        data: dict[str, JSONValue],
        *,
        kube: Kube,
        timeout: float,
    ) -> Self:
        """Create a Kubernetes PersistentVolumeClaim from a manifest payload."""
        metadata = data.get("metadata")
        name = ""
        namespace = ""
        if isinstance(metadata, Mapping):
            # NOTE: keep a concrete `dict[str, object]` to avoid type-inference
            # ambiguity from recursive JSON unions when indexing metadata keys.
            metadata_fields: dict[str, object] = {}
            for key, value in metadata.items():
                if isinstance(key, str):
                    metadata_fields[key] = value
            name = str(metadata_fields.get("name") or "").strip()
            namespace = str(metadata_fields.get("namespace") or "").strip()
        if not namespace:
            raise OSError("PVC creation payload must define metadata.namespace")

        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        try:
            payload = await kube.run(
                lambda request_timeout: kube.core.create_namespaced_persistent_volume_claim(
                    namespace=namespace,
                    body=data,
                    _request_timeout=request_timeout,
                ),
                timeout=deadline - loop.time(),
                context="failed to create PersistentVolumeClaim",
            )
            if payload is None:
                raise OSError("kubernetes returned empty payload during PVC creation")
            if not isinstance(payload, kubernetes.client.V1PersistentVolumeClaim):
                raise OSError("malformed Kubernetes payload during PVC creation")
            return cls(obj=payload)
        except OSError as err:
            text = str(err).lower()
            if "status 409" not in text and "already exists" not in text:
                raise

        # race condition; attempt to retrieve existing PVC
        if name and namespace:
            existing = await cls.get(
                kube=kube,
                namespace=namespace,
                timeout=deadline - loop.time(),
                name=name,
            )
            if existing is not None:
                return existing
        raise OSError(
            "kubernetes accepted PVC creation, but no valid PVC payload was returned"
        )

    async def grow(
        self,
        requested: str,
        *,
        kube: Kube,
        timeout: float,
    ) -> None:
        """Resize the PVC if current requested storage is below target.

        Parameters
        ----------
        requested : str
            Target Kubernetes storage quantity (for example `1Gi`).
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum API timeout in seconds.  If infinite, wait indefinitely.

        Raises
        ------
        TimeoutError
            If any resize request exceeds timeout.
        OSError
            If the PVC disappears or fails to converge after retries.
        """
        new_size = parse_pvc_size(requested)
        meta = self.obj.metadata or kubernetes.client.V1ObjectMeta()
        name = meta.name or ""
        namespace = meta.namespace or ""
        if not name:
            raise OSError("cannot resize PVC with missing metadata.name")
        if not namespace:
            raise OSError(f"cannot resize PVC {name!r} with missing metadata.namespace")

        patch = {"spec": {"resources": {"requests": {"storage": requested}}}}
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout

        for attempt in range(PVC_GROW_RETRIES):
            live = await type(self).get(
                kube=kube,
                namespace=namespace,
                timeout=deadline - loop.time(),
                name=name,
            )
            if live is None:
                raise OSError(f"PVC {name!r} disappeared during resize lifecycle")

            live_spec = live.obj.spec or kubernetes.client.V1PersistentVolumeClaimSpec()
            current_size = parse_pvc_size(type(self)._requested_storage(live_spec))
            if current_size >= new_size:
                return

            try:
                await kube.run(
                    lambda request_timeout: kube.core.patch_namespaced_persistent_volume_claim(
                        name=name,
                        namespace=namespace,
                        body=patch,
                        _request_timeout=request_timeout,
                    ),
                    timeout=deadline - loop.time(),
                    context=f"failed to patch PVC {name!r} during resize lifecycle",
                )
            except OSError as err:
                detail = str(err).lower()
                if "status 404" in detail or "not found" in detail:
                    raise OSError(
                        f"PVC {name!r} disappeared during resize lifecycle"
                    ) from err
                if (
                    "status 409" in detail or
                    "conflict" in detail or
                    "the object has been modified" in detail
                ) and attempt + 1 < PVC_GROW_RETRIES:
                    continue
                raise

            live = await type(self).get(
                kube=kube,
                namespace=namespace,
                timeout=deadline - loop.time(),
                name=name,
            )
            if live is None:
                raise OSError(f"PVC {name!r} disappeared during resize lifecycle")

            live_spec = live.obj.spec or kubernetes.client.V1PersistentVolumeClaimSpec()
            current_size = parse_pvc_size(type(self)._requested_storage(live_spec))
            if current_size >= new_size:
                return
            if attempt + 1 < PVC_GROW_RETRIES:
                continue
            raise OSError(
                f"PVC {name!r} did not converge to requested size {requested!r} "
                f"after {PVC_GROW_RETRIES} attempts"
            )

    async def delete(self, *, kube: Kube, timeout: float) -> None:
        """Delete the PVC from the cluster."""
        meta = self.obj.metadata or kubernetes.client.V1ObjectMeta()
        name = meta.name or ""
        namespace = meta.namespace or ""
        await kube.run(
            lambda request_timeout: kube.core.delete_namespaced_persistent_volume_claim(
                name=name,
                namespace=namespace,
                body=kubernetes.client.V1DeleteOptions(),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete PVC {name!r}",
        )


@dataclass(frozen=True)
class PersistentVolume:
    """Thin wrapper around one Kubernetes PersistentVolume object."""
    obj: kubernetes.client.V1PersistentVolume

    @classmethod
    async def get(
        cls,
        *,
        kube: Kube,
        timeout: float,
        name: KubeName,
    ) -> Self | None:
        """Read one Kubernetes PersistentVolume by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            The maximum time to wait for Kubernetes PersistentVolume query in seconds.
            If infinite, wait indefinitely.
        name : str
            PersistentVolume name to read.

        Returns
        -------
        PersistentVolume | None
            Validated Kubernetes PersistentVolume wrapper, or `None` if it does not
            exist.

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds the timeout budget.
        OSError
            If the Kubernetes API call fails or returns malformed data.
        """
        payload = await kube.run(
            lambda request_timeout: kube.core.read_persistent_volume(
                name=name,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to read PersistentVolume {name!r}",
        )
        if payload is None:
            return None
        if not isinstance(payload, kubernetes.client.V1PersistentVolume):
            raise OSError(f"malformed Kubernetes PersistentVolume payload for {name!r}")
        return cls(obj=payload)

    @classmethod
    async def list(
        cls,
        *,
        kube: Kube,
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[Self]:
        """List Kubernetes PersistentVolumes with optional label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            The maximum time to wait for Kubernetes PersistentVolume list queries in
            seconds. If infinite, wait indefinitely.
        labels : Mapping[str, str] | None, optional
            Optional label filters.

        Returns
        -------
        builtins.list[PersistentVolume]
            Validated Kubernetes PersistentVolume wrappers.

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds the timeout budget.
        OSError
            If the Kubernetes API call fails or returns malformed data.
        """
        payload = await kube.run(
            lambda request_timeout: kube.core.list_persistent_volume(
                label_selector=_label_selector(labels),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context="failed to list Kubernetes PersistentVolumes",
        )
        if payload is None:
            return []
        if not isinstance(payload, kubernetes.client.V1PersistentVolumeList):
            raise OSError("malformed Kubernetes PersistentVolume list payload")
        out: builtins.list[Self] = []
        for item in payload.items or []:
            if not isinstance(item, kubernetes.client.V1PersistentVolume):
                raise OSError("malformed Kubernetes PersistentVolume entry in list payload")
            out.append(cls(obj=item))
        return out


def parse_pvc_size(value: str) -> Decimal:
    """Parse a Kubernetes PVC request size string into a Decimal value.

    Parameters
    ----------
    value : str
        Kubernetes PVC request size string (e.g., "1Gi", "500Mi").

    Returns
    -------
    Decimal
        The parsed size as a Decimal value.

    Raises
    ------
    ValueError
        If the input string is not a valid Kubernetes PVC request size.
    """
    match = QUANTITY_RE.fullmatch(value.strip())
    if not match:
        raise ValueError(f"invalid Kubernetes PVC request size: {value!r}")

    number, suffix = match.groups()
    factor = STORAGE_FACTORS.get(suffix)
    if factor is None:
        raise ValueError(
            f"invalid Kubernetes memory unit for PVC request: {suffix!r} (options are "
            f"{', '.join(repr(s) for s in STORAGE_FACTORS)})"
        )

    try:
        return Decimal(number) * factor
    except (InvalidOperation, ValueError) as err:
        raise ValueError(
            f"invalid Kubernetes memory quantity for PVC request: {value!r}"
        ) from err
