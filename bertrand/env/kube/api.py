"""Shared Kubernetes API primitives for Bertrand's runtime orchestration.

This module centralizes Kubernetes API access utilities used across Bertrand's
kube subsystems.
"""
from __future__ import annotations

import asyncio
import base64
import binascii
import math
import os
import re
from collections.abc import Callable
from dataclasses import dataclass, field
from decimal import Decimal, InvalidOperation
from pathlib import Path
from typing import Literal, Self, overload

from kubernetes import client as kube_client
from kubernetes import config as kube_config
from kubernetes.client.rest import ApiException

from ..config.core import KubeName
from ..run import BERTRAND_NAMESPACE, STATE_DIR, JSONValue, atomic_write_text, run

PVC_GROW_RETRIES = 4
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
_HOST_CLIENT: kube_client.ApiClient | None = None


def _normalize_timeout(timeout: float) -> float | None:
    if timeout <= 0:
        raise TimeoutError("timeout must be non-negative")
    if math.isinf(timeout):
        return None
    return timeout


@overload
async def _run_api[T](
    fn: Callable[[], T],
    *,
    timeout: float,
    context: str,
    allow_not_found: Literal[False] = False,
) -> T: ...
@overload
async def _run_api[T](
    fn: Callable[[], T],
    *,
    timeout: float,
    context: str,
    allow_not_found: Literal[True],
) -> T | None: ...
async def _run_api[T](
    fn: Callable[[], T],
    *,
    timeout: float,
    context: str,
    allow_not_found: bool = False,
) -> T | None:
    if timeout <= 0:
        raise TimeoutError(f"{context} timed out before request could start")
    try:
        return await asyncio.wait_for(
            asyncio.to_thread(fn),
            timeout=None if math.isinf(timeout) else timeout,
        )
    except TimeoutError as err:
        raise TimeoutError(f"{context} timed out after {timeout} seconds") from err
    except ApiException as err:
        if allow_not_found and err.status == 404:
            return None
        detail = (err.body or err.reason or str(err)).strip()
        raise OSError(
            f"{context} failed with kubernetes API status {err.status}: {detail}"
        ) from err


def _host_client() -> kube_client.ApiClient:
    global _HOST_CLIENT
    if _HOST_CLIENT is not None:
        return _HOST_CLIENT
    if not KUBE_CONFIG_FILE.is_file():
        raise OSError(
            f"kubernetes config is missing at {KUBE_CONFIG_FILE}.  Run `bertrand init` "
            "to converge MicroK8s API access first."
        )
    try:
        _HOST_CLIENT = kube_config.new_client_from_config(config_file=str(KUBE_CONFIG_FILE))
        return _HOST_CLIENT
    except Exception as err:
        raise OSError(
            f"failed to initialize kubernetes client from {KUBE_CONFIG_FILE}: {err}"
        ) from err


def _label_selector(labels: dict[str, str] | None) -> str | None:
    """Render Kubernetes label selectors from key-value dictionaries."""
    if labels is None:
        return None
    if not labels:
        return None
    return ",".join(f"{k}={v}" for k, v in labels.items())


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
    payload = text if text.endswith("\n") else f"{text}\n"

    global _HOST_CLIENT
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
    if _HOST_CLIENT is not None:
        try:
            _HOST_CLIENT.close()
        except Exception:
            pass
        _HOST_CLIENT = None
    return KUBE_CONFIG_FILE


@dataclass(frozen=True)
class InClusterAPI:
    """In-cluster Kubernetes API client for controlplane pods.

    This avoids depending on ``kubectl`` inside operator pods and relies only on
    ServiceAccount token credentials.

    Attributes
    ----------
    namespace : str
        Namespace where namespaced resources should be managed.
    api_client : kubernetes.client.ApiClient
        Configured API transport for in-cluster requests.
    core : kubernetes.client.CoreV1Api
        Core v1 API surface for typed built-in resources.
    custom : kubernetes.client.CustomObjectsApi
        Custom object API surface for CRD interactions.
    """
    namespace: str
    api_client: kube_client.ApiClient = field(repr=False)
    core: kube_client.CoreV1Api = field(repr=False)
    custom: kube_client.CustomObjectsApi = field(repr=False)

    @classmethod
    def load(cls) -> Self:
        """Construct an in-cluster API client from ServiceAccount files.

        Returns
        -------
        InClusterAPI
            Ready-to-use typed API client handles.

        Raises
        ------
        OSError
            If required serviceaccount files or env vars are missing.
        """
        service_host = os.environ.get("KUBERNETES_SERVICE_HOST", "").strip()
        service_port = os.environ.get("KUBERNETES_SERVICE_PORT_HTTPS", "443").strip()
        if not service_host:
            raise OSError(
                "in-cluster API is unavailable: KUBERNETES_SERVICE_HOST is not set"
            )
        token_path = Path("/var/run/secrets/kubernetes.io/serviceaccount/token")
        namespace_path = Path("/var/run/secrets/kubernetes.io/serviceaccount/namespace")
        ca_path = Path("/var/run/secrets/kubernetes.io/serviceaccount/ca.crt")
        if not token_path.is_file():
            raise OSError(f"serviceaccount token file is missing: {token_path}")
        if not namespace_path.is_file():
            raise OSError(f"serviceaccount namespace file is missing: {namespace_path}")
        if not ca_path.is_file():
            raise OSError(f"serviceaccount CA file is missing: {ca_path}")

        token = token_path.read_text(encoding="utf-8").strip()
        namespace = namespace_path.read_text(encoding="utf-8").strip() or BERTRAND_NAMESPACE
        configuration = kube_client.Configuration(
            host=f"https://{service_host}:{service_port}",
            ssl_ca_cert=str(ca_path),
        )
        configuration.api_key = {"authorization": token}
        configuration.api_key_prefix = {"authorization": "Bearer"}
        configuration.verify_ssl = True
        api_client = kube_client.ApiClient(configuration=configuration)
        return cls(
            namespace=namespace,
            api_client=api_client,
            core=kube_client.CoreV1Api(api_client),
            custom=kube_client.CustomObjectsApi(api_client),
        )

    @overload
    async def run[T](
        self,
        fn: Callable[[], T],
        *,
        timeout: float,
        context: str,
        allow_not_found: Literal[False] = False,
    ) -> T: ...

    @overload
    async def run[T](
        self,
        fn: Callable[[], T],
        *,
        timeout: float,
        context: str,
        allow_not_found: Literal[True],
    ) -> T | None: ...

    async def run[T](
        self,
        fn: Callable[[], T],
        *,
        timeout: float,
        context: str,
        allow_not_found: bool = False,
    ) -> T | None:
        """Run one typed in-cluster API operation with normalized errors."""

        return await _run_api(
            fn,
            timeout=timeout,
            context=context,
            allow_not_found=allow_not_found,
        )


@dataclass(frozen=True)
class KubeSecret:
    """Thin wrapper around one Kubernetes Secret object."""
    obj: kube_client.V1Secret

    @property
    def metadata(self) -> kube_client.V1ObjectMeta:
        return self.obj.metadata or kube_client.V1ObjectMeta()

    @property
    def data(self) -> dict[str, str]:
        return self.obj.data or {}

    @classmethod
    async def query(
        cls,
        *,
        namespace: str,
        timeout: float,
        name: KubeName | None = None,
        labels: dict[str, str] | None = None,
    ) -> list[Self]:
        """Load Kubernetes Secrets and validate their structure.

        Parameters
        ----------
        namespace : str
            The namespace to search within.
        timeout : float
            The maximum time to wait for Kubernetes Secret queries in seconds.  If
            infinite, wait indefinitely.
        name : str | None, optional
            Optional Secret name.  When given, this performs an exact name lookup and
            returns either a one-item list or an empty list.
        labels : dict[str, str] | None, optional
            Optional label filters.  Only supported for list lookups.

        Returns
        -------
        list[KubeSecret]
            Validated Kubernetes Secret wrappers.  Name lookups return either one item
            or an empty list.

        Raises
        ------
        ValueError
            If both `name` and `labels` are provided.
        TimeoutError
            If the Kubernetes request exceeds the timeout budget.
        OSError
            If the Kubernetes API call fails or returns malformed data.
        """
        if name is not None and labels is not None:
            raise ValueError("secret query cannot combine both name and labels filters")

        core = kube_client.CoreV1Api(_host_client())
        if name is not None:
            payload = await _run_api(
                lambda: core.read_namespaced_secret(
                    name=name,
                    namespace=namespace,
                    _request_timeout=_normalize_timeout(timeout),
                ),
                timeout=timeout,
                context=(
                    f"failed to read cluster secret {name!r} in namespace "
                    f"{namespace!r}"
                ),
                allow_not_found=True,
            )
            if payload is None:
                return []
            return [cls(obj=payload)]

        payload = await _run_api(
            lambda: core.list_namespaced_secret(
                namespace=namespace,
                label_selector=_label_selector(labels),
                _request_timeout=_normalize_timeout(timeout),
            ),
            timeout=timeout,
            context=f"failed to list Kubernetes Secrets in namespace {namespace!r}",
        )
        return [cls(obj=item) for item in payload.items or []]

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
        value = self.data.get("value")
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
    obj: kube_client.V1StorageClass

    @property
    def metadata(self) -> kube_client.V1ObjectMeta:
        return self.obj.metadata or kube_client.V1ObjectMeta()

    @property
    def provisioner(self) -> str:
        return self.obj.provisioner or ""

    @property
    def allow_volume_expansion(self) -> bool:
        return bool(self.obj.allow_volume_expansion)

    @classmethod
    async def query(
        cls,
        *,
        timeout: float,
        name: KubeName | None = None,
        labels: dict[str, str] | None = None,
    ) -> list[Self]:
        """Load Kubernetes StorageClasses and validate their structure.

        Parameters
        ----------
        timeout : float
            The maximum time to wait for Kubernetes StorageClass queries in seconds.
            If infinite, wait indefinitely.
        name : str | None, optional
            Optional StorageClass name.  When given, this performs an exact name lookup
            and returns either a one-item list or an empty list.
        labels : dict[str, str] | None, optional
            Optional label filters.  Only supported for list lookups.

        Returns
        -------
        list[StorageClass]
            Validated Kubernetes StorageClass wrappers.  Name lookups return either one
            item or an empty list.

        Raises
        ------
        ValueError
            If both `name` and `labels` are provided.
        TimeoutError
            If the Kubernetes request exceeds the timeout budget.
        OSError
            If the Kubernetes API call fails or returns malformed data.
        """
        if name is not None and labels is not None:
            raise ValueError(
                "storage class query cannot combine both name and labels filters"
            )

        storage = kube_client.StorageV1Api(_host_client())
        if name is not None:
            payload = await _run_api(
                lambda: storage.read_storage_class(
                    name=name,
                    _request_timeout=_normalize_timeout(timeout),
                ),
                timeout=timeout,
                context=f"failed to read StorageClass {name!r}",
                allow_not_found=True,
            )
            if payload is None:
                return []
            return [cls(obj=payload)]

        payload = await _run_api(
            lambda: storage.list_storage_class(
                label_selector=_label_selector(labels),
                _request_timeout=_normalize_timeout(timeout),
            ),
            timeout=timeout,
            context="failed to list Kubernetes StorageClasses",
        )
        return [cls(obj=item) for item in payload.items or []]


@dataclass(frozen=True)
class PersistentVolumeClaim:
    """Thin wrapper around one Kubernetes PersistentVolumeClaim object."""
    obj: kube_client.V1PersistentVolumeClaim

    @property
    def metadata(self) -> kube_client.V1ObjectMeta:
        return self.obj.metadata or kube_client.V1ObjectMeta()

    @property
    def spec(self) -> kube_client.V1PersistentVolumeClaimSpec:
        return self.obj.spec or kube_client.V1PersistentVolumeClaimSpec()

    @staticmethod
    def _requested_storage(spec: kube_client.V1PersistentVolumeClaimSpec) -> str:
        resources = spec.resources or kube_client.V1VolumeResourceRequirements()
        requests = resources.requests or {}
        value = str(requests.get("storage") or "").strip()
        if not value:
            raise OSError("PVC does not expose a valid storage request quantity")
        return value

    @classmethod
    async def query(
        cls,
        *,
        namespace: str,
        timeout: float,
        name: KubeName | None = None,
        labels: dict[str, str] | None = None,
    ) -> list[Self]:
        """Load Kubernetes PersistentVolumeClaims and validate their structure.

        Parameters
        ----------
        namespace : str
            The namespace to search within.
        timeout : float
            The maximum time to wait for Kubernetes PVC queries in seconds.  If
            infinite, wait indefinitely.
        name : str | None, optional
            Optional PersistentVolumeClaim name.  When given, this performs an exact
            name lookup and returns either a one-item list or an empty list.
        labels : dict[str, str] | None, optional
            Optional label filters.  Only supported for list lookups.

        Returns
        -------
        list[PersistentVolumeClaim]
            Validated Kubernetes PersistentVolumeClaim wrappers.  Name lookups return
            either one item or an empty list.

        Raises
        ------
        ValueError
            If both `name` and `labels` are provided.
        TimeoutError
            If the Kubernetes request exceeds the timeout budget.
        OSError
            If the Kubernetes API call fails or returns malformed data.
        """
        if name is not None and labels is not None:
            raise ValueError("PVC query cannot combine both name and labels filters")

        core = kube_client.CoreV1Api(_host_client())
        if name is not None:
            payload = await _run_api(
                lambda: core.read_namespaced_persistent_volume_claim(
                    name=name,
                    namespace=namespace,
                    _request_timeout=_normalize_timeout(timeout),
                ),
                timeout=timeout,
                context=f"failed to read PVC {name!r} in namespace {namespace!r}",
                allow_not_found=True,
            )
            if payload is None:
                return []
            return [cls(obj=payload)]

        payload = await _run_api(
            lambda: core.list_namespaced_persistent_volume_claim(
                namespace=namespace,
                label_selector=_label_selector(labels),
                _request_timeout=_normalize_timeout(timeout),
            ),
            timeout=timeout,
            context=(
                "failed to list Kubernetes PersistentVolumeClaims in namespace "
                f"{namespace!r}"
            ),
        )
        return [cls(obj=item) for item in payload.items or []]

    @classmethod
    async def create(
        cls,
        data: dict[str, JSONValue],
        *,
        timeout: float,
    ) -> Self:
        """Create a Kubernetes PersistentVolumeClaim from a manifest payload."""
        metadata = data.get("metadata")
        name = ""
        namespace = ""
        if isinstance(metadata, dict):
            name = str(metadata.get("name") or "").strip()
            namespace = str(metadata.get("namespace") or "").strip()
        if not namespace:
            raise OSError("PVC creation payload must define metadata.namespace")

        core = kube_client.CoreV1Api(_host_client())
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        try:
            payload = await _run_api(
                lambda: core.create_namespaced_persistent_volume_claim(
                    namespace=namespace,
                    body=data,
                    _request_timeout=_normalize_timeout(deadline - loop.time()),
                ),
                timeout=deadline - loop.time(),
                context="failed to create PersistentVolumeClaim",
            )
            assert payload is not None
            return cls(obj=payload)
        except OSError as err:
            text = str(err).lower()
            if "status 409" not in text and "already exists" not in text:
                raise

        # race condition; attempt to retrieve existing PVC
        if name and namespace:
            matches = await cls.query(
                namespace=namespace,
                timeout=deadline - loop.time(),
                name=name,
            )
            if matches:
                return matches[0]
        raise OSError(
            "kubernetes accepted PVC creation, but no valid PVC payload was returned"
        )

    async def grow(
        self,
        requested: str,
        *,
        timeout: float,
    ) -> None:
        """Resize the PVC if current requested storage is below target.

        Parameters
        ----------
        requested : str
            Target Kubernetes storage quantity (for example `1Gi`).
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
        name = self.metadata.name or ""
        namespace = self.metadata.namespace or ""
        if not name:
            raise OSError("cannot resize PVC with missing metadata.name")
        if not namespace:
            raise OSError(f"cannot resize PVC {name!r} with missing metadata.namespace")

        patch = {"spec": {"resources": {"requests": {"storage": requested}}}}
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        core = kube_client.CoreV1Api(_host_client())

        for attempt in range(PVC_GROW_RETRIES):
            matches = await type(self).query(
                namespace=namespace,
                timeout=deadline - loop.time(),
                name=name,
            )
            if not matches:
                raise OSError(f"PVC {name!r} disappeared during resize lifecycle")
            live = matches[0]

            current_size = parse_pvc_size(type(self)._requested_storage(live.spec))
            if current_size >= new_size:
                return

            try:
                await _run_api(
                    lambda: core.patch_namespaced_persistent_volume_claim(
                        name=name,
                        namespace=namespace,
                        body=patch,
                        _request_timeout=_normalize_timeout(deadline - loop.time()),
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

            matches = await type(self).query(
                namespace=namespace,
                timeout=deadline - loop.time(),
                name=name,
            )
            if not matches:
                raise OSError(f"PVC {name!r} disappeared during resize lifecycle")
            live = matches[0]

            current_size = parse_pvc_size(type(self)._requested_storage(live.spec))
            if current_size >= new_size:
                return
            if attempt + 1 < PVC_GROW_RETRIES:
                continue
            raise OSError(
                f"PVC {name!r} did not converge to requested size {requested!r} "
                f"after {PVC_GROW_RETRIES} attempts"
            )

    async def delete(self, *, timeout: float) -> None:
        """Delete the PVC from the cluster."""
        core = kube_client.CoreV1Api(_host_client())
        await _run_api(
            lambda: core.delete_namespaced_persistent_volume_claim(
                name=self.metadata.name or "",
                namespace=self.metadata.namespace or "",
                body=kube_client.V1DeleteOptions(),
                _request_timeout=_normalize_timeout(timeout),
            ),
            timeout=timeout,
            context=f"failed to delete PVC {self.metadata.name!r}",
            allow_not_found=True,
        )


@dataclass(frozen=True)
class Pod:
    """Thin wrapper around one Kubernetes Pod object."""
    obj: kube_client.V1Pod

    @property
    def metadata(self) -> kube_client.V1ObjectMeta:
        return self.obj.metadata or kube_client.V1ObjectMeta()

    @property
    def status(self) -> kube_client.V1PodStatus:
        return self.obj.status or kube_client.V1PodStatus()

    @property
    def spec(self) -> kube_client.V1PodSpec:
        return self.obj.spec or kube_client.V1PodSpec(containers=[])

    @classmethod
    async def query(
        cls,
        *,
        namespace: str,
        timeout: float,
        name: KubeName | None = None,
        labels: dict[str, str] | None = None,
    ) -> list[Self]:
        """Load Kubernetes Pods and validate their structure.

        Parameters
        ----------
        namespace : str
            The namespace to search within.
        timeout : float
            The maximum time to wait for Kubernetes pod queries in seconds.  If
            infinite, wait indefinitely.
        name : str | None, optional
            Optional pod name.  When given, this performs an exact name lookup and
            returns either a one-item list or an empty list.
        labels : dict[str, str] | None, optional
            Optional label filters.  Only supported for list lookups.

        Returns
        -------
        list[Pod]
            Validated Kubernetes pod wrappers.  Name lookups return either one item or
            an empty list.

        Raises
        ------
        ValueError
            If both `name` and `labels` are provided.
        TimeoutError
            If the Kubernetes request exceeds the timeout budget.
        OSError
            If the Kubernetes API call fails or returns malformed data.
        """
        if name is not None and labels is not None:
            raise ValueError("pod query cannot combine both name and labels filters")

        core = kube_client.CoreV1Api(_host_client())
        if name is not None:
            payload = await _run_api(
                lambda: core.read_namespaced_pod(
                    name=name,
                    namespace=namespace,
                    _request_timeout=_normalize_timeout(timeout),
                ),
                timeout=timeout,
                context=f"failed to read pod {name!r} in namespace {namespace!r}",
                allow_not_found=True,
            )
            if payload is None:
                return []
            return [cls(obj=payload)]

        payload = await _run_api(
            lambda: core.list_namespaced_pod(
                namespace=namespace,
                label_selector=_label_selector(labels),
                _request_timeout=_normalize_timeout(timeout),
            ),
            timeout=timeout,
            context=f"failed to list pods in namespace {namespace!r}",
        )
        return [cls(obj=item) for item in payload.items or []]


@dataclass(frozen=True)
class PersistentVolume:
    """Thin wrapper around one Kubernetes PersistentVolume object."""
    obj: kube_client.V1PersistentVolume

    @property
    def metadata(self) -> kube_client.V1ObjectMeta:
        return self.obj.metadata or kube_client.V1ObjectMeta()

    @property
    def spec(self) -> kube_client.V1PersistentVolumeSpec:
        return self.obj.spec or kube_client.V1PersistentVolumeSpec()

    @classmethod
    async def query(
        cls,
        *,
        timeout: float,
        name: KubeName | None = None,
        labels: dict[str, str] | None = None,
    ) -> list[Self]:
        """Load Kubernetes PersistentVolumes and validate their structure.

        Parameters
        ----------
        timeout : float
            The maximum time to wait for Kubernetes PersistentVolume queries in seconds.
            If infinite, wait indefinitely.
        name : str | None, optional
            Optional PersistentVolume name.  When given, this performs an exact name
            lookup and returns either a one-item list or an empty list.
        labels : dict[str, str] | None, optional
            Optional label filters.  Only supported for list lookups.

        Returns
        -------
        list[PersistentVolume]
            Validated Kubernetes PersistentVolume wrappers.  Name lookups return either
            one item or an empty list.

        Raises
        ------
        ValueError
            If both `name` and `labels` are provided.
        TimeoutError
            If the Kubernetes request exceeds the timeout budget.
        OSError
            If the Kubernetes API call fails or returns malformed data.
        """
        if name is not None and labels is not None:
            raise ValueError(
                "persistent volume query cannot combine both name and labels filters"
            )

        core = kube_client.CoreV1Api(_host_client())
        if name is not None:
            payload = await _run_api(
                lambda: core.read_persistent_volume(
                    name=name,
                    _request_timeout=_normalize_timeout(timeout),
                ),
                timeout=timeout,
                context=f"failed to read PersistentVolume {name!r}",
                allow_not_found=True,
            )
            if payload is None:
                return []
            return [cls(obj=payload)]

        payload = await _run_api(
            lambda: core.list_persistent_volume(
                label_selector=_label_selector(labels),
                _request_timeout=_normalize_timeout(timeout),
            ),
            timeout=timeout,
            context="failed to list Kubernetes PersistentVolumes",
        )
        return [cls(obj=item) for item in payload.items or []]


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
