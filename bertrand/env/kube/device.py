"""ConfigMap wrappers and device selector resolution for Kubernetes."""
from __future__ import annotations

import builtins
import hashlib
import sys
from collections.abc import Mapping
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


@dataclass(frozen=True)
class ConfigMap:
    """General-purpose wrapper around one Kubernetes ConfigMap object."""

    obj: kube_client.V1ConfigMap

    @classmethod
    async def get(
        cls,
        *,
        kube: Kube,
        namespace: str,
        timeout: float,
        name: KubeName,
    ) -> Self | None:
        """Read one Kubernetes ConfigMap by name."""
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
        *,
        kube: Kube,
        namespace: str,
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[Self]:
        """List Kubernetes ConfigMaps in one namespace with optional label filtering."""
        payload = await kube.run(
            lambda request_timeout: kube.core.list_namespaced_config_map(
                namespace=namespace,
                label_selector=_label_selector(labels),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to list Kubernetes ConfigMaps in namespace {namespace!r}",
        )
        if payload is None:
            return []
        if not isinstance(payload, kube_client.V1ConfigMapList):
            raise OSError(
                f"malformed Kubernetes ConfigMap list payload in namespace {namespace!r}"
            )
        out: builtins.list[Self] = []
        for item in payload.items or []:
            if not isinstance(item, kube_client.V1ConfigMap):
                raise OSError("malformed Kubernetes ConfigMap entry in list payload")
            out.append(cls(obj=item))
        return out

    @classmethod
    async def upsert(
        cls,
        *,
        kube: Kube,
        namespace: str,
        timeout: float,
        name: KubeName,
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
        value: str,
    ) -> Self:
        """Create or patch one Kubernetes ConfigMap payload."""
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

    async def refresh(self, *, kube: Kube, timeout: float) -> Self | None:
        """Re-read this ConfigMap by identity."""
        namespace, name = self.identity
        return await type(self).get(
            kube=kube,
            namespace=namespace,
            timeout=timeout,
            name=name,
        )

    async def delete(self, *, kube: Kube, timeout: float) -> None:
        """Delete this ConfigMap from the cluster."""
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
        """Return text payload from `data[\"value\"]`."""
        value = (self.obj.data or {}).get("value")
        if value is None:
            raise OSError(
                f"cluster ConfigMap {name!r} does not define required key 'data.value'"
            )
        return str(value)

    @property
    def identity(self) -> tuple[str, str]:
        """Return `(namespace, name)` identity for this ConfigMap."""
        metadata = self.obj.metadata or kube_client.V1ObjectMeta()
        namespace = (metadata.namespace or "").strip()
        name = (metadata.name or "").strip()
        if not namespace or not name:
            raise OSError("ConfigMap metadata is missing namespace/name identity")
        return namespace, name

    @classmethod
    async def build_flags(
        cls,
        *,
        kube: Kube,
        env_id: str,
        build: object,
        timeout: float,
    ) -> tuple[str, ...]:
        """Build device flags for one build request."""
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
        *,
        kube: Kube,
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
        *,
        kube: Kube,
        id: KubeName,
        env_id: str,
        timeout: float,
    ) -> tuple[Self, KubeName] | None:
        # NOTE: env scope takes precedence over shared scope.
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


def _device_name(id: KubeName, env_id: str | None) -> KubeName:
    id = _check_kube_name(id)
    if env_id is not None:
        env_id = _check_uuid(env_id)

    parts: tuple[str, ...]
    if env_id is None:
        parts = ("shared", id)
    else:
        parts = (env_id, id)

    # NOTE: include lengths before each token so hash input boundaries stay unambiguous.
    h = hashlib.sha256()
    for part in parts:
        encoded = part.encode("utf-8")
        h.update(len(encoded).to_bytes(8, "big"))
        h.update(encoded)

    return f"bertrand-device-{h.hexdigest()}"
