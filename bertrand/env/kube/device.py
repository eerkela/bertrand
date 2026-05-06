"""ConfigMap wrappers and device selector resolution for Kubernetes."""

from __future__ import annotations

import builtins
import hashlib
import sys
from typing import Literal, Self

from kubernetes import client as kube_client

from ..config.core import KubeName, _check_kube_name, _check_uuid
from ..run import BERTRAND_NAMESPACE
from .api import Kube
from .configmap import ConfigMap

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


class DeviceConfigMap(ConfigMap):
    """ConfigMap wrapper with Bertrand device selector helpers.

    Notes
    -----
    Device selectors are stored in managed ConfigMaps and resolved into build
    command flags. The generic ConfigMap API behavior is inherited from
    :class:`bertrand.env.kube.configmap.ConfigMap`.
    """

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
            raise OSError(f"cluster ConfigMap {name!r} does not define required key 'data.value'")
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
                f"bertrand: optional device selector {id!r} was not found; continuing without it",
                file=sys.stderr,
            )
            return []

        config_map, name = resolved
        selector = config_map.value_text(name).strip()
        if not selector:
            raise OSError(f"cluster ConfigMap {name!r} key 'data.value' cannot be empty")
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
            raise OSError(f"cluster ConfigMap {name!r} is missing annotation {CAPABILITY_ID_V1!r}")
        if _check_kube_name(annotation_id) != _check_kube_name(id):
            raise OSError(
                f"cluster ConfigMap {name!r} has mismatched {CAPABILITY_ID_V1!r}: "
                f"expected {id!r}, got {annotation_id!r}"
            )
