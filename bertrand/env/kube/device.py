"""ConfigMap wrappers and device selector resolution for Kubernetes."""
from __future__ import annotations

import builtins
import hashlib
import sys
from collections.abc import Collection, Mapping
from dataclasses import dataclass
from typing import Literal, Self

from kubernetes import client as kube_client

from ..config.core import KubeName, _check_kube_name, _check_uuid
from ..run import BERTRAND_NAMESPACE
from .api import Kube, _label_selector

type DevicePermission = Literal["r", "w", "m", "rw", "rm", "wm", "rwm"]
DEVICE_PERMISSIONS = frozenset({"r", "w", "m", "rw", "rm", "wm", "rwm"})
CAPABILITY_MANAGED_V1 = "bertrand.dev/capability-managed.v1"
CAPABILITY_ENV_ID_V1 = "bertrand.dev/capability-env-id.v1"
CAPABILITY_ID_V1 = "bertrand.dev/capability-id.v1"


def _device_name(id: KubeName, env_id: str | None) -> KubeName:
    id = _check_kube_name(id)
    if env_id is not None:
        env_id = _check_uuid(env_id)

    parts: tuple[str, ...]
    if env_id is None:
        parts = ("shared", id)
    else:
        parts = (env_id, id)

    # include lengths before each token so hash input boundaries stay unambiguous
    h = hashlib.sha256()
    for part in parts:
        encoded = part.encode("utf-8")
        h.update(len(encoded).to_bytes(8, "big"))
        h.update(encoded)

    return f"bertrand-device-{h.hexdigest()}"


@dataclass(frozen=True)
class ConfigMap:
    """General-purpose wrapper around one Kubernetes ConfigMap object."""

    obj: kube_client.V1ConfigMap

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        timeout: float,
        name: KubeName,
    ) -> Self | None:
        """Read one Kubernetes ConfigMap by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the ConfigMap.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        name : KubeName
            ConfigMap name to read.

        Returns
        -------
        ConfigMap | None
            Wrapped Kubernetes ConfigMap, or `None` when not found.

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds `timeout`.
        OSError
            If Kubernetes returns malformed data or the API call fails.
        """
        payload = await kube.run(
            lambda request_timeout: kube.core.read_namespaced_config_map(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=(
                f"failed to read cluster ConfigMap {name!r} in namespace "
                f"{namespace!r}"
            ),
        )
        if payload is None:
            return None
        if not isinstance(payload, kube_client.V1ConfigMap):
            raise OSError(
                f"malformed Kubernetes ConfigMap payload for {name!r} in "
                f"namespace {namespace!r}"
            )
        return cls(obj=payload)

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        timeout: float,
        namespaces: Collection[str] | None = None,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[Self]:
        """List Kubernetes ConfigMaps with optional namespace and label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        namespaces : Collection[str] | None, optional
            Optional namespace selector. If `None`, query all namespaces. Otherwise,
            entries are trimmed, deduplicated, and queried individually.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.

        Returns
        -------
        builtins.list[ConfigMap]
            Validated ConfigMap wrappers matching the requested filters.

        Raises
        ------
        TimeoutError
            If any Kubernetes request exceeds `timeout`.
        OSError
            If Kubernetes returns malformed list/item payloads or a list call fails.
        """
        label_selector = _label_selector(labels)
        payloads: builtins.list[kube_client.V1ConfigMapList] = []

        # search all namespaces
        if namespaces is None:
            payload = await kube.run(
                lambda request_timeout: kube.core.list_config_map_for_all_namespaces(
                    label_selector=label_selector,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context="failed to list cluster ConfigMaps across all namespaces",
            )
            if payload is not None:
                payloads.append(payload)

        # search specific namespaces
        else:
            normalized = {namespace.strip() for namespace in namespaces}
            normalized.discard("")
            if not normalized:
                return []
            for namespace in sorted(normalized):
                payload = await kube.run(
                    lambda request_timeout, namespace=namespace: (
                        kube.core.list_namespaced_config_map(
                            namespace=namespace,
                            label_selector=label_selector,
                            _request_timeout=request_timeout,
                        )
                    ),
                    timeout=timeout,
                    context=f"failed to list cluster ConfigMaps in namespace {namespace!r}",
                )
                if payload is not None:
                    payloads.append(payload)

        out: builtins.list[Self] = []
        for payload in payloads:
            if not isinstance(payload, kube_client.V1ConfigMapList):
                raise OSError("malformed Kubernetes ConfigMap list payload")
            for item in payload.items or []:
                if not isinstance(item, kube_client.V1ConfigMap):
                    raise OSError("malformed Kubernetes ConfigMap entry in list payload")
                out.append(cls(obj=item))
        return out

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        namespace: str,
        timeout: float,
        name: KubeName,
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
        value: str,
    ) -> Self:
        """Create or patch one Kubernetes ConfigMap payload.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the ConfigMap.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        name : KubeName
            ConfigMap name to create or patch.
        labels : Mapping[str, str] | None
            Labels to apply to `metadata.labels`.
        annotations : Mapping[str, str] | None
            Annotations to apply to `metadata.annotations`.
        value : str
            Text payload stored at `data.value`.

        Returns
        -------
        ConfigMap
            Wrapped created or patched ConfigMap.

        Raises
        ------
        TimeoutError
            If any Kubernetes request exceeds `timeout`.
        OSError
            If Kubernetes create/patch fails or returns malformed data.
        """
        manifest = {
            "apiVersion": "v1",
            "kind": "ConfigMap",
            "metadata": {
                "name": name,
                "namespace": namespace,
                "labels": dict(labels or {}),
                "annotations": dict(annotations or {}),
            },
            "data": {"value": value},
        }

        try:
            created = await kube.run(
                lambda request_timeout: kube.core.create_namespaced_config_map(
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=f"failed to create cluster ConfigMap {name!r}",
            )
            if not isinstance(created, kube_client.V1ConfigMap):
                raise OSError(
                    f"malformed Kubernetes ConfigMap payload while creating {name!r}"
                )
            return cls(obj=created)
        except OSError as err:
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise

        updated = await kube.run(
            lambda request_timeout: kube.core.patch_namespaced_config_map(
                name=name,
                namespace=namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to update cluster ConfigMap {name!r}",
        )
        if not isinstance(updated, kube_client.V1ConfigMap):
            raise OSError(
                f"malformed Kubernetes ConfigMap payload while updating {name!r}"
            )
        return cls(obj=updated)

    @property
    def identity(self) -> tuple[str, str]:
        """Return canonical `(namespace, name)` identity.

        Returns
        -------
        tuple[str, str]
            Trimmed `(namespace, name)` from `metadata`.

        Raises
        ------
        OSError
            If either namespace or name is missing.
        """
        metadata = self.obj.metadata or kube_client.V1ObjectMeta()
        namespace = (metadata.namespace or "").strip()
        name = (metadata.name or "").strip()
        if not namespace or not name:
            raise OSError("ConfigMap metadata is missing namespace/name identity")
        return namespace, name

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this ConfigMap by identity.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        ConfigMap | None
            Latest ConfigMap wrapper if the object still exists, otherwise `None`.

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds `timeout`.
        OSError
            If identity is malformed, the API request fails, or payload is malformed.
        """
        namespace, name = self.identity
        return await type(self).get(
            kube=kube,
            namespace=namespace,
            timeout=timeout,
            name=name,
        )

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this ConfigMap from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds `timeout`.
        OSError
            If identity is malformed or Kubernetes delete fails.
        """
        namespace, name = self.identity
        await kube.run(
            lambda request_timeout: kube.core.delete_namespaced_config_map(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete cluster ConfigMap {name!r}",
        )

    def value_text(self, name: KubeName) -> str:
        """Read text payload from `data.value`.

        Parameters
        ----------
        name : KubeName
            ConfigMap name used for contextual error messages.

        Returns
        -------
        str
            Text payload stored under `data.value`.

        Raises
        ------
        OSError
            If `data.value` is missing.
        """
        value = (self.obj.data or {}).get("value")
        if value is None:
            raise OSError(
                f"cluster ConfigMap {name!r} does not define required key 'data.value'"
            )
        return str(value)

    @classmethod
    async def build_flags(
        cls,
        kube: Kube,
        *,
        env_id: str,
        build: object,
        timeout: float,
    ) -> tuple[str, ...]:
        """Build device CLI flags for one build request.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        env_id : str
            Environment UUID used for env-first then shared fallback lookups.
        build : object
            Build configuration object with optional `devices` iterable.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        tuple[str, ...]
            Flattened `--device` CLI args for the configured device requests.

        Raises
        ------
        TimeoutError
            If any Kubernetes request exceeds `timeout`.
        OSError
            If a required selector is missing, managed metadata is invalid, or a
            selector value is empty.
        ValueError
            If `env_id` is invalid, a request id is duplicated, or permissions are
            invalid.
        """
        requests = tuple(getattr(build, "devices", ()))
        if not requests:
            return ()

        validated_env = _check_uuid(env_id)
        seen: set[KubeName] = set()
        flags: builtins.list[str] = []
        for request in requests:
            id = _check_kube_name(request.id)
            if id in seen:
                raise ValueError(f"duplicate device capability ID: {id!r}")
            seen.add(id)

            permissions = str(request.permissions)
            if permissions not in DEVICE_PERMISSIONS:
                raise ValueError(
                    f"invalid device permissions {permissions!r}; must be a non-empty "
                    "combination of 'r', 'w', and 'm'"
                )

            flags.extend(
                await cls._resolve_flag(
                    kube=kube,
                    id=id,
                    required=request.required,
                    permissions=permissions,
                    env_id=validated_env,
                    timeout=timeout,
                )
            )

        return tuple(flags)

    @classmethod
    async def _resolve_flag(
        cls,
        kube: Kube,
        *,
        id: KubeName,
        required: bool,
        permissions: str,
        env_id: str,
        timeout: float,
    ) -> builtins.list[str]:
        resolved = await cls._resolve(
            kube=kube,
            id=id,
            env_id=env_id,
            timeout=timeout,
        )
        if resolved is None:
            if required:
                raise OSError(f"missing required device selector: {id!r}")
            print(
                f"bertrand: optional device selector {id!r} was not found; continuing "
                "without it",
                file=sys.stderr,
            )
            return []

        config_map, name = resolved
        selector = config_map.value_text(name).strip()
        if not selector:
            raise OSError(
                f"cluster ConfigMap {name!r} key 'data.value' cannot be empty"
            )
        return ["--device", f"{selector}:{permissions}"]

    @classmethod
    async def _resolve(
        cls,
        kube: Kube,
        *,
        id: KubeName,
        env_id: str,
        timeout: float,
    ) -> tuple[Self, KubeName] | None:
        # env scope takes precedence over shared scope
        for scope in (env_id, None):
            name = _device_name(id=id, env_id=scope)
            config_map = await cls.get(
                kube=kube,
                namespace=BERTRAND_NAMESPACE,
                timeout=timeout,
                name=name,
            )
            if config_map is None:
                continue
            cls._assert_managed(
                config_map=config_map,
                expected_name=name,
                id=id,
                env_id=scope,
            )
            return config_map, name
        return None

    @classmethod
    def _assert_managed(
        cls,
        *,
        config_map: Self,
        expected_name: KubeName,
        id: KubeName,
        env_id: str | None,
    ) -> None:
        metadata = config_map.obj.metadata or kube_client.V1ObjectMeta()
        name = (metadata.name or "").strip() or expected_name
        labels = metadata.labels or {}
        annotations = metadata.annotations or {}

        if labels.get(CAPABILITY_MANAGED_V1) != "true":
            raise OSError(
                f"cluster ConfigMap {name!r} collides with a Bertrand device request "
                "but is unmanaged"
            )
        label_env = labels.get(CAPABILITY_ENV_ID_V1)
        expected_env = env_id or "shared"
        if label_env != expected_env:
            raise OSError(
                f"cluster ConfigMap {name!r} has mismatched {CAPABILITY_ENV_ID_V1!r}: "
                f"expected {expected_env!r}, got {label_env!r}"
            )
        annotation_id = annotations.get(CAPABILITY_ID_V1)
        if annotation_id is None:
            raise OSError(
                f"cluster ConfigMap {name!r} is missing annotation {CAPABILITY_ID_V1!r}"
            )
        if _check_kube_name(annotation_id) != _check_kube_name(id):
            raise OSError(
                f"cluster ConfigMap {name!r} has mismatched {CAPABILITY_ID_V1!r}: "
                f"expected {id!r}, got {annotation_id!r}"
            )
