"""Secret wrappers and secret/SSH build-flag resolution for Kubernetes."""
from __future__ import annotations

import base64
import binascii
import builtins
import hashlib
import shutil
import sys
import uuid
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path
from typing import Self

from kubernetes import client as kube_client

from ..config.core import KubeName, _check_kube_name, _check_uuid
from ..run import BERTRAND_NAMESPACE, CACHE_DIR, atomic_write_bytes
from .api import Kube, _label_selector

CAPABILITY_DIR = CACHE_DIR / "capabilities"
CAPABILITY_MANAGED_V1 = "bertrand.dev/capability-managed.v1"
CAPABILITY_ENV_ID_V1 = "bertrand.dev/capability-env-id.v1"
CAPABILITY_ID_V1 = "bertrand.dev/capability-id.v1"


@dataclass(frozen=True)
class Secret:
    """General-purpose wrapper around one Kubernetes Secret object."""
    obj: kube_client.V1Secret

    @classmethod
    async def get(
        cls,
        *,
        kube: Kube,
        namespace: str,
        timeout: float,
        name: KubeName,
    ) -> Self | None:
        """Read one Kubernetes Secret by name."""
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
        if not isinstance(payload, kube_client.V1Secret):
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
        """List Kubernetes Secrets in one namespace with optional label filtering."""
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
        if not isinstance(payload, kube_client.V1SecretList):
            raise OSError(
                f"malformed Kubernetes Secret list payload in namespace {namespace!r}"
            )
        out: builtins.list[Self] = []
        for item in payload.items or []:
            if not isinstance(item, kube_client.V1Secret):
                raise OSError("malformed Kubernetes Secret entry in list payload")
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
        payload: bytes,
        secret_type: str = "Opaque",
    ) -> Self:
        """Create or patch one Kubernetes Secret payload."""
        manifest = {
            "apiVersion": "v1",
            "kind": "Secret",
            "metadata": {
                "name": name,
                "namespace": namespace,
                "labels": dict(labels or {}),
                "annotations": dict(annotations or {}),
            },
            "type": secret_type,
            "data": {"value": base64.b64encode(payload).decode("ascii")},
        }

        try:
            created = await kube.run(
                lambda request_timeout: kube.core.create_namespaced_secret(
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=f"failed to create cluster secret {name!r}",
            )
            if not isinstance(created, kube_client.V1Secret):
                raise OSError(
                    f"malformed Kubernetes Secret payload while creating {name!r}"
                )
            return cls(obj=created)
        except OSError as err:
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise

        updated = await kube.run(
            lambda request_timeout: kube.core.patch_namespaced_secret(
                name=name,
                namespace=namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to update cluster secret {name!r}",
        )
        if not isinstance(updated, kube_client.V1Secret):
            raise OSError(
                f"malformed Kubernetes Secret payload while updating {name!r}"
            )
        return cls(obj=updated)

    async def refresh(self, *, kube: Kube, timeout: float) -> Self | None:
        """Re-read this secret by identity."""
        namespace, name = self.identity
        return await type(self).get(
            kube=kube,
            namespace=namespace,
            timeout=timeout,
            name=name,
        )

    async def delete(self, *, kube: Kube, timeout: float) -> None:
        """Delete this secret from the cluster."""
        namespace, name = self.identity
        await kube.run(
            lambda request_timeout: kube.core.delete_namespaced_secret(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete cluster secret {name!r}",
        )

    def value_bytes(self, name: KubeName) -> bytes:
        """Decode base64 bytes from `data[\"value\"]`."""
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

    def value_text(self, name: KubeName) -> str:
        """Decode UTF-8 text from `data[\"value\"]`."""
        try:
            return self.value_bytes(name).decode("utf-8")
        except UnicodeDecodeError as err:
            raise OSError(
                f"cluster secret {name!r} key 'data.value' must decode as UTF-8 text"
            ) from err

    @property
    def identity(self) -> tuple[str, str]:
        """Return `(namespace, name)` identity for this secret."""
        metadata = self.obj.metadata or kube_client.V1ObjectMeta()
        namespace = (metadata.namespace or "").strip()
        name = (metadata.name or "").strip()
        if not namespace or not name:
            raise OSError("secret metadata is missing namespace/name identity")
        return namespace, name

    @classmethod
    async def build_flags(
        cls,
        *,
        kube: Kube,
        env_id: str,
        build: object,
        timeout: float,
    ) -> tuple[tuple[str, ...], Path | None]:
        """Build secret and SSH flags for one build request."""
        secrets = tuple(getattr(build, "secrets", ()))
        ssh = tuple(getattr(build, "ssh", ()))
        if not secrets and not ssh:
            return (), None

        validated_env = _check_uuid(env_id)
        staged = CAPABILITY_DIR / uuid.uuid4().hex
        flags: builtins.list[str] = []
        try:
            for request in secrets:
                flags.extend(
                    await cls._resolve_flag(
                        kube=kube,
                        mode="secret",
                        id=request.id,
                        required=request.required,
                        env_id=validated_env,
                        timeout=timeout,
                        staged=staged,
                    )
                )
            for request in ssh:
                flags.extend(
                    await cls._resolve_flag(
                        kube=kube,
                        mode="ssh",
                        id=request.id,
                        required=request.required,
                        env_id=validated_env,
                        timeout=timeout,
                        staged=staged,
                    )
                )
            return tuple(flags), staged
        except Exception:
            cls.cleanup_staged(staged)
            raise

    @classmethod
    def cleanup_staged(cls, path: Path | None) -> None:
        """Delete staged payload files, if present."""
        if path is None:
            return
        shutil.rmtree(path, ignore_errors=True)

    @classmethod
    async def _resolve_flag(
        cls,
        *,
        kube: Kube,
        mode: str,
        id: KubeName,
        required: bool,
        env_id: str,
        timeout: float,
        staged: Path,
    ) -> builtins.list[str]:
        resolved = await cls._resolve(
            kube=kube,
            id=id,
            env_id=env_id,
            timeout=timeout,
        )
        if resolved is None:
            if required:
                if mode == "secret":
                    raise OSError(f"missing required secret: {id!r}")
                raise OSError(f"missing required ssh credential: {id!r}")
            if mode == "secret":
                print(
                    f"bertrand: optional secret {id!r} was not found; continuing "
                    "without it",
                    file=sys.stderr,
                )
            else:
                print(
                    f"bertrand: optional ssh credential {id!r} was not found; "
                    "continuing without it",
                    file=sys.stderr,
                )
            return []

        secret, name = resolved
        payload = secret.value_bytes(name)
        # NOTE: Secrets and SSH now share one storage identity; emit mode only
        # changes the build flag shape.
        target = cls._stage_payload(staged, id, payload)
        if mode == "secret":
            return ["--secret", f"id={id},src={target}"]
        return ["--ssh", f"id={id},src={target}"]

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
            name = _secret_name(id=id, env_id=scope)
            secret = await cls.get(
                kube=kube,
                namespace=BERTRAND_NAMESPACE,
                timeout=timeout,
                name=name,
            )
            if secret is None:
                continue
            cls._assert_managed(
                secret=secret,
                expected_name=name,
                id=id,
                env_id=scope,
            )
            return secret, name
        return None

    @classmethod
    def _assert_managed(
        cls,
        *,
        secret: Self,
        expected_name: KubeName,
        id: KubeName,
        env_id: str | None,
    ) -> None:
        metadata = secret.obj.metadata or kube_client.V1ObjectMeta()
        name = (metadata.name or "").strip() or expected_name
        labels = metadata.labels or {}
        annotations = metadata.annotations or {}

        if labels.get(CAPABILITY_MANAGED_V1) != "true":
            raise OSError(
                f"cluster secret {name!r} collides with a Bertrand secret request "
                "but is unmanaged"
            )
        label_env = labels.get(CAPABILITY_ENV_ID_V1)
        expected_env = env_id or "shared"
        if label_env != expected_env:
            raise OSError(
                f"cluster secret {name!r} has mismatched {CAPABILITY_ENV_ID_V1!r}: "
                f"expected {expected_env!r}, got {label_env!r}"
            )
        annotation_id = annotations.get(CAPABILITY_ID_V1)
        if annotation_id is None:
            raise OSError(
                f"cluster secret {name!r} is missing annotation {CAPABILITY_ID_V1!r}"
            )
        if _check_kube_name(annotation_id) != _check_kube_name(id):
            raise OSError(
                f"cluster secret {name!r} has mismatched {CAPABILITY_ID_V1!r}: "
                f"expected {id!r}, got {annotation_id!r}"
            )

    @staticmethod
    def _stage_payload(
        staged: Path,
        id: KubeName,
        payload: bytes,
    ) -> Path:
        target = staged / "secrets" / id
        target.parent.mkdir(parents=True, exist_ok=True)
        atomic_write_bytes(target, payload)
        target.chmod(0o600)
        return target


def _secret_name(id: KubeName, env_id: str | None) -> KubeName:
    id = _check_kube_name(id)
    if env_id is not None:
        env_id = _check_uuid(env_id)

    parts: tuple[str, ...]
    if env_id is None:
        parts = ("shared", id)
    else:
        parts = (env_id, id)

    # NOTE: include lengths before each token to keep hash input boundaries explicit.
    digest = hashlib.sha256()
    for part in parts:
        encoded = part.encode("utf-8")
        digest.update(len(encoded).to_bytes(8, "big"))
        digest.update(encoded)

    return f"bertrand-secret-{digest.hexdigest()}"
