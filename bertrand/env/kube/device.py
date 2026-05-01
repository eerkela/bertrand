"""ConfigMap wrappers and device capability resolution for Kubernetes."""
from __future__ import annotations

import builtins
import hashlib
import sys
from collections.abc import Mapping
from dataclasses import dataclass, field
from typing import Literal, Self

from kubernetes import client as kube_client

from ..config.core import KubeName, _check_kube_name, _check_uuid
from ..run import BERTRAND_NAMESPACE
from .api import Kube, _label_selector

type DevicePermission = Literal["r", "w", "m", "rw", "rm", "wm", "rwm"]
DEVICE_PERMISSIONS = frozenset({"r", "w", "m", "rw", "rm", "wm", "rwm"})
CAPABILITY_MANAGED_V1 = "bertrand.dev/capability-managed.v1"
CAPABILITY_KIND_V1 = "bertrand.dev/capability-kind.v1"
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
    async def create_or_patch(
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


@dataclass(frozen=True)
class DeviceCapabilityMetadata:
    """Metadata for one managed ConfigMap-backed device capability."""
    id: KubeName
    env_id: str | None
    name: KubeName = field(init=False, repr=False)

    def __post_init__(self) -> None:
        object.__setattr__(self, "id", _check_kube_name(self.id))
        if self.env_id is not None:
            object.__setattr__(self, "env_id", _check_uuid(self.env_id))
        object.__setattr__(self, "name", _kube_config_map_name(
            id=self.id,
            env_id=self.env_id,
        ))

    @classmethod
    def from_config_map(cls, config_map: ConfigMap) -> Self:
        """Parse device capability metadata from a managed ConfigMap."""
        metadata = config_map.obj.metadata or kube_client.V1ObjectMeta()
        name = metadata.name or "<unknown>"
        labels = metadata.labels or {}
        annotations = metadata.annotations or {}
        if labels.get(CAPABILITY_MANAGED_V1) != "true":
            raise OSError(
                f"cluster ConfigMap {name!r} collides with a Bertrand "
                "capability name but is unmanaged"
            )
        if labels.get(CAPABILITY_KIND_V1) != "device":
            raise OSError(
                f"cluster ConfigMap {name!r} has missing/invalid "
                f"{CAPABILITY_KIND_V1!r}"
            )
        id_value = annotations.get(CAPABILITY_ID_V1)
        if id_value is None:
            raise OSError(
                f"cluster ConfigMap {name!r} is missing annotation "
                f"{CAPABILITY_ID_V1!r}"
            )
        parsed_id = _check_kube_name(id_value)
        env_id: str | None = labels.get(CAPABILITY_ENV_ID_V1)
        if env_id is None:
            raise OSError(
                f"cluster ConfigMap {name!r} is missing label "
                f"{CAPABILITY_ENV_ID_V1!r}"
            )
        parsed_env = None if env_id == "shared" else _check_uuid(env_id)
        return cls(id=parsed_id, env_id=parsed_env)


def _kube_config_map_name(
    id: KubeName,
    env_id: str | None,
) -> KubeName:
    id = _check_kube_name(id)
    if env_id is not None:
        env_id = _check_uuid(env_id)

    parts: tuple[str, ...]
    if env_id is None:
        parts = ("shared", "device", id)
    else:
        parts = (env_id, "device", id)

    # NOTE: include lengths before each token so hash input boundaries stay unambiguous.
    h = hashlib.sha256()
    for part in parts:
        encoded = part.encode("utf-8")
        h.update(len(encoded).to_bytes(8, "big"))
        h.update(encoded)

    return f"bertrand-device-{h.hexdigest()}"


@dataclass
class DeviceCapabilities:
    """Builder-style resolver for ConfigMap-backed device capability flags."""
    @dataclass
    class DeviceRequest:
        """A stored device capability request."""
        required: bool
        permissions: str

    env_id: str
    timeout: float
    _devices: dict[str, DeviceRequest] = field(default_factory=dict, repr=False)
    _finalized: bool = field(default=False, repr=False)

    def __post_init__(self) -> None:
        self.env_id = _check_uuid(self.env_id)
        if self.timeout <= 0:
            raise TimeoutError("capability timeout must be non-negative")

    def device(
        self,
        *,
        id: KubeName,
        required: bool,
        permissions: str,
    ) -> None:
        """Register one build device capability request."""
        if self._finalized:
            raise RuntimeError("capabilities are already finalized and cannot be modified")
        id = _check_kube_name(id)
        if id in self._devices:
            raise ValueError(f"duplicate device capability ID: {id!r}")
        if permissions not in DEVICE_PERMISSIONS:
            raise ValueError(
                f"invalid device permissions {permissions!r}; must be a non-empty "
                "combination of 'r', 'w', and 'm'"
            )
        self._devices[id] = self.DeviceRequest(required=required, permissions=permissions)

    async def _resolve(
        self,
        id: KubeName,
        *,
        kube: Kube,
    ) -> tuple[ConfigMap, DeviceCapabilityMetadata] | None:
        # NOTE: resolve env scope first, then shared scope, so local overrides win.
        expected = DeviceCapabilityMetadata(id=id, env_id=self.env_id)
        config_map = await ConfigMap.get(
            kube=kube,
            namespace=BERTRAND_NAMESPACE,
            timeout=self.timeout,
            name=expected.name,
        )
        if config_map is not None:
            if expected != DeviceCapabilityMetadata.from_config_map(config_map):
                raise OSError(
                    f"environment ConfigMap {expected.name!r} metadata does not match "
                    f"requested device capability {expected.id!r}"
                )
            return config_map, expected

        expected = DeviceCapabilityMetadata(id=id, env_id=None)
        config_map = await ConfigMap.get(
            kube=kube,
            namespace=BERTRAND_NAMESPACE,
            timeout=self.timeout,
            name=expected.name,
        )
        if config_map is not None:
            if expected != DeviceCapabilityMetadata.from_config_map(config_map):
                raise OSError(
                    f"cluster ConfigMap {expected.name!r} metadata does not match "
                    f"requested device capability {expected.id!r}"
                )
            return config_map, expected

        return None

    async def _resolve_device(
        self,
        id: KubeName,
        request: DeviceRequest,
        *,
        kube: Kube,
    ) -> builtins.list[str]:
        resolved = await self._resolve(id=id, kube=kube)
        if resolved is None:
            if request.required:
                raise OSError(f"missing required device selector: {id!r}")
            print(
                f"bertrand: optional device selector {id!r} was not found; continuing "
                "without it",
                file=sys.stderr,
            )
            return []

        config_map, meta = resolved
        selector = config_map.value_text(meta.name).strip()
        if not selector:
            raise OSError(
                f"cluster ConfigMap {meta.name!r} key 'data.value' cannot be empty"
            )
        return ["--device", f"{selector}:{request.permissions}"]

    async def finalize(self) -> tuple[str, ...]:
        """Resolve all registered device requests into CLI flags."""
        if self._finalized:
            raise RuntimeError("capabilities are already finalized")

        flags: builtins.list[str] = []
        with await Kube.host(timeout=self.timeout) as kube:
            for id, request in self._devices.items():
                flags.extend(await self._resolve_device(id, request, kube=kube))

        self._finalized = True
        return tuple(flags)


async def build_device_capability_flags(
    *,
    env_id: str,
    tag: object,
    build: object,
    timeout: float,
) -> tuple[str, ...]:
    """Build device capability CLI flags for one build tag."""
    del tag  # currently unused, reserved for future diagnostics context.

    requests = tuple(getattr(build, "devices", ()))
    if not requests:
        return ()

    resolver = DeviceCapabilities(env_id=env_id, timeout=timeout)
    for req in requests:
        resolver.device(id=req.id, required=req.required, permissions=req.permissions)
    return await resolver.finalize()


async def get_device_capability(
    *,
    id: KubeName,
    env_id: str | None,
    timeout: float,
) -> tuple[ConfigMap, DeviceCapabilityMetadata] | None:
    """Read one ConfigMap-backed device capability using explicit scope."""
    expected = DeviceCapabilityMetadata(id=id, env_id=env_id)
    with await Kube.host(timeout=timeout) as kube:
        config_map = await ConfigMap.get(
            kube=kube,
            namespace=BERTRAND_NAMESPACE,
            timeout=timeout,
            name=expected.name,
        )
        if config_map is None:
            return None
        if expected != DeviceCapabilityMetadata.from_config_map(config_map):
            raise OSError(
                f"cluster ConfigMap {expected.name!r} metadata does not match requested "
                f"device capability {expected.id!r}"
            )
        return config_map, expected


async def put_device_capability(
    *,
    id: KubeName,
    env_id: str | None,
    timeout: float,
    selector: str,
) -> DeviceCapabilityMetadata:
    """Create or update one managed ConfigMap-backed device capability."""
    expected = DeviceCapabilityMetadata(id=id, env_id=env_id)
    with await Kube.host(timeout=timeout) as kube:
        existing = await ConfigMap.get(
            kube=kube,
            namespace=BERTRAND_NAMESPACE,
            timeout=timeout,
            name=expected.name,
        )
        if existing is not None and expected != DeviceCapabilityMetadata.from_config_map(existing):
            raise OSError(
                f"cluster ConfigMap {expected.name!r} metadata does not match requested "
                f"device capability {expected.id!r}"
            )

        await ConfigMap.create_or_patch(
            kube=kube,
            namespace=BERTRAND_NAMESPACE,
            timeout=timeout,
            name=expected.name,
            labels={
                CAPABILITY_MANAGED_V1: "true",
                CAPABILITY_KIND_V1: "device",
                CAPABILITY_ENV_ID_V1: expected.env_id or "shared",
            },
            annotations={CAPABILITY_ID_V1: expected.id},
            value=selector,
        )
        return expected


async def delete_device_capability(
    *,
    id: KubeName,
    env_id: str | None,
    timeout: float,
) -> bool:
    """Delete one managed ConfigMap-backed device capability."""
    expected = DeviceCapabilityMetadata(id=id, env_id=env_id)
    with await Kube.host(timeout=timeout) as kube:
        existing = await ConfigMap.get(
            kube=kube,
            namespace=BERTRAND_NAMESPACE,
            timeout=timeout,
            name=expected.name,
        )
        if existing is None:
            return False
        if expected != DeviceCapabilityMetadata.from_config_map(existing):
            raise OSError(
                f"cluster ConfigMap {expected.name!r} metadata does not match requested "
                f"device capability {expected.id!r}"
            )

        await existing.delete(kube=kube, timeout=timeout)
        return True


async def list_device_capabilities(
    *,
    env_id: str | None,
    timeout: float,
) -> builtins.list[tuple[ConfigMap, DeviceCapabilityMetadata]]:
    """List managed ConfigMap-backed device capability metadata."""
    if env_id is not None:
        env_id = _check_uuid(env_id)
    with await Kube.host(timeout=timeout) as kube:
        parsed = await ConfigMap.list(
            kube=kube,
            namespace=BERTRAND_NAMESPACE,
            timeout=timeout,
            labels={
                CAPABILITY_MANAGED_V1: "true",
                CAPABILITY_KIND_V1: "device",
                CAPABILITY_ENV_ID_V1: env_id or "shared",
            },
        )
        out = [(item, DeviceCapabilityMetadata.from_config_map(item)) for item in parsed]
        out.sort(key=lambda entry: entry[1].name)
        return out
