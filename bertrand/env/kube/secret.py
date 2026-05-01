"""Secret wrappers and secret-backed capability resolution for Kubernetes."""
from __future__ import annotations

import base64
import binascii
import builtins
import hashlib
import shutil
import sys
import uuid
from collections.abc import Mapping
from dataclasses import dataclass, field
from pathlib import Path
from typing import Literal, Self, cast

from kubernetes import client as kube_client

from ..config.core import KubeName, _check_kube_name, _check_uuid
from ..run import BERTRAND_NAMESPACE, CACHE_DIR, atomic_write_bytes
from .api import Kube, _label_selector

type SecretCapabilityKind = Literal["secret", "ssh"]
CAPABILITY_DIR = CACHE_DIR / "capabilities"
CAPABILITY_MANAGED_V1 = "bertrand.dev/capability-managed.v1"
CAPABILITY_KIND_V1 = "bertrand.dev/capability-kind.v1"
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
    async def create_or_patch(
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

    def decode(self, name: KubeName) -> bytes:
        """Decode a base64-encoded payload from `data[\"value\"]`."""
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

    @property
    def identity(self) -> tuple[str, str]:
        """Return `(namespace, name)` identity for this secret."""
        metadata = self.obj.metadata or kube_client.V1ObjectMeta()
        namespace = (metadata.namespace or "").strip()
        name = (metadata.name or "").strip()
        if not namespace or not name:
            raise OSError("secret metadata is missing namespace/name identity")
        return namespace, name


@dataclass(frozen=True)
class SecretCapabilityMetadata:
    """Metadata for one managed secret-backed capability."""
    kind: SecretCapabilityKind
    id: KubeName
    env_id: str | None
    name: KubeName = field(init=False, repr=False)

    def __post_init__(self) -> None:
        object.__setattr__(self, "id", _check_kube_name(self.id))
        if self.env_id is not None:
            object.__setattr__(self, "env_id", _check_uuid(self.env_id))
        object.__setattr__(self, "name", _kube_secret_name(
            kind=self.kind,
            id=self.id,
            env_id=self.env_id,
        ))

    @classmethod
    def from_secret(cls, secret: Secret) -> Self:
        """Parse capability metadata from a managed secret."""
        metadata = secret.obj.metadata or kube_client.V1ObjectMeta()
        name = metadata.name or "<unknown>"
        labels = metadata.labels or {}
        annotations = metadata.annotations or {}
        if labels.get(CAPABILITY_MANAGED_V1) != "true":
            raise OSError(
                f"cluster secret {name!r} collides with a Bertrand "
                "capability name but is unmanaged"
            )
        kind = labels.get(CAPABILITY_KIND_V1)
        if kind not in ("secret", "ssh"):
            raise OSError(
                f"cluster secret {name!r} has missing/invalid "
                f"{CAPABILITY_KIND_V1!r}"
            )
        parsed_kind = cast(SecretCapabilityKind, kind)
        id_value = annotations.get(CAPABILITY_ID_V1)
        if id_value is None:
            raise OSError(
                f"cluster secret {name!r} is missing annotation "
                f"{CAPABILITY_ID_V1!r}"
            )
        parsed_id = _check_kube_name(id_value)
        env_id: str | None = labels.get(CAPABILITY_ENV_ID_V1)
        if env_id is None:
            raise OSError(
                f"cluster secret {name!r} is missing label {CAPABILITY_ENV_ID_V1!r}"
            )
        parsed_env = None if env_id == "shared" else _check_uuid(env_id)
        return cls(kind=parsed_kind, id=parsed_id, env_id=parsed_env)


def _kube_secret_name(
    kind: SecretCapabilityKind,
    id: KubeName,
    env_id: str | None,
) -> KubeName:
    id = _check_kube_name(id)
    if env_id is not None:
        env_id = _check_uuid(env_id)

    parts: tuple[str, ...]
    if env_id is None:
        parts = ("shared", kind, id)
    else:
        parts = (env_id, kind, id)

    # NOTE: include lengths before each token so hash input boundaries stay unambiguous.
    h = hashlib.sha256()
    for part in parts:
        encoded = part.encode("utf-8")
        h.update(len(encoded).to_bytes(8, "big"))
        h.update(encoded)

    return f"bertrand-{kind}-{h.hexdigest()}"


@dataclass
class SecretCapabilities:
    """Builder-style resolver for secret and SSH capability flags."""
    @dataclass
    class SecretRequest:
        """A stored secret capability request."""
        required: bool

    @dataclass
    class SSHRequest:
        """A stored SSH capability request."""
        required: bool

    env_id: str
    timeout: float
    run_id: str = field(default_factory=lambda: uuid.uuid4().hex)
    _secrets: dict[str, SecretRequest] = field(default_factory=dict, repr=False)
    _ssh: dict[str, SSHRequest] = field(default_factory=dict, repr=False)
    _finalized: bool = field(default=False, repr=False)

    def __post_init__(self) -> None:
        self.env_id = _check_uuid(self.env_id)
        if self.timeout <= 0:
            raise TimeoutError("capability timeout must be non-negative")

    @property
    def path(self) -> Path:
        """Return the staged payload directory for this resolver instance."""
        return CAPABILITY_DIR / self.run_id

    def secret(self, *, id: KubeName, required: bool) -> None:
        """Register one build secret capability request."""
        if self._finalized:
            raise RuntimeError("capabilities are already finalized and cannot be modified")
        id = _check_kube_name(id)
        if id in self._secrets:
            raise ValueError(f"duplicate secret capability ID: {id!r}")
        if id in self._ssh:
            raise ValueError(
                f"capability ID {id!r} is already registered as an SSH capability"
            )
        self._secrets[id] = self.SecretRequest(required=required)

    def ssh(self, *, id: KubeName, required: bool) -> None:
        """Register one build SSH capability request."""
        if self._finalized:
            raise RuntimeError("capabilities are already finalized and cannot be modified")
        id = _check_kube_name(id)
        if id in self._secrets:
            raise ValueError(
                f"capability ID {id!r} is already registered as a secret capability"
            )
        if id in self._ssh:
            raise ValueError(f"duplicate SSH capability ID: {id!r}")
        self._ssh[id] = self.SSHRequest(required=required)

    def _stage_payload(
        self,
        kind: Literal["secrets", "ssh"],
        id: KubeName,
        payload: bytes,
    ) -> Path:
        target = self.path / kind / id
        target.parent.mkdir(parents=True, exist_ok=True)
        atomic_write_bytes(target, payload)
        target.chmod(0o600)
        return target

    async def _resolve(
        self,
        kind: SecretCapabilityKind,
        id: KubeName,
        *,
        kube: Kube,
    ) -> tuple[Secret, SecretCapabilityMetadata] | None:
        # NOTE: resolve env scope first, then shared scope, so local overrides win.
        expected = SecretCapabilityMetadata(kind=kind, id=id, env_id=self.env_id)
        secret = await Secret.get(
            kube=kube,
            namespace=BERTRAND_NAMESPACE,
            timeout=self.timeout,
            name=expected.name,
        )
        if secret is not None:
            if expected != SecretCapabilityMetadata.from_secret(secret):
                raise OSError(
                    f"environment secret {expected.name!r} metadata does not match "
                    f"requested {expected.kind} capability {expected.id!r}"
                )
            return secret, expected

        expected = SecretCapabilityMetadata(kind=kind, id=id, env_id=None)
        secret = await Secret.get(
            kube=kube,
            namespace=BERTRAND_NAMESPACE,
            timeout=self.timeout,
            name=expected.name,
        )
        if secret is not None:
            if expected != SecretCapabilityMetadata.from_secret(secret):
                raise OSError(
                    f"cluster secret {expected.name!r} metadata does not match "
                    f"requested {expected.kind} capability {expected.id!r}"
                )
            return secret, expected

        return None

    async def _resolve_secret(
        self,
        id: KubeName,
        request: SecretRequest,
        *,
        kube: Kube,
    ) -> builtins.list[str]:
        resolved = await self._resolve(kind="secret", id=id, kube=kube)
        if resolved is None:
            if request.required:
                raise OSError(f"missing required secret: {id!r}")
            print(
                f"bertrand: optional secret {id!r} was not found; continuing without it",
                file=sys.stderr,
            )
            return []

        data, meta = resolved
        payload = data.decode(meta.name)
        target = self._stage_payload("secrets", id, payload)
        return ["--secret", f"id={id},src={target}"]

    async def _resolve_ssh(
        self,
        id: KubeName,
        request: SSHRequest,
        *,
        kube: Kube,
    ) -> builtins.list[str]:
        resolved = await self._resolve(kind="ssh", id=id, kube=kube)
        if resolved is None:
            if request.required:
                raise OSError(f"missing required ssh credential: {id!r}")
            print(
                f"bertrand: optional ssh credential {id!r} was not found; continuing "
                "without it",
                file=sys.stderr,
            )
            return []

        data, meta = resolved
        payload = data.decode(meta.name)
        target = self._stage_payload("ssh", id, payload)
        return ["--ssh", f"id={id},src={target}"]

    async def finalize(self) -> tuple[str, ...]:
        """Resolve all registered requests and stage required payload files."""
        if self._finalized:
            raise RuntimeError("capabilities are already finalized")

        try:
            flags: builtins.list[str] = []
            with await Kube.host(timeout=self.timeout) as kube:
                for id, secret in self._secrets.items():
                    flags.extend(await self._resolve_secret(id, secret, kube=kube))
                for id, ssh in self._ssh.items():
                    flags.extend(await self._resolve_ssh(id, ssh, kube=kube))

            self._finalized = True
            return tuple(flags)
        except Exception:
            shutil.rmtree(self.path, ignore_errors=True)
            raise


async def build_secret_capability_flags(
    *,
    env_id: str,
    tag: object,
    build: object,
    timeout: float,
) -> tuple[tuple[str, ...], Path | None]:
    """Build secret and SSH capability CLI flags for one build tag."""
    del tag  # currently unused, reserved for future diagnostics context.

    requests_secret = tuple(getattr(build, "secrets", ()))
    requests_ssh = tuple(getattr(build, "ssh", ()))
    if not requests_secret and not requests_ssh:
        return (), None

    resolver = SecretCapabilities(env_id=env_id, timeout=timeout)
    for req in requests_secret:
        resolver.secret(id=req.id, required=req.required)
    for req in requests_ssh:
        resolver.ssh(id=req.id, required=req.required)

    flags = await resolver.finalize()
    return flags, resolver.path


def cleanup_secret_capability_dir(path: Path | None) -> None:
    """Delete staged secret capability payload files if present."""
    if path is None:
        return
    shutil.rmtree(path, ignore_errors=True)


async def get_secret_capability(
    *,
    kind: SecretCapabilityKind,
    id: KubeName,
    env_id: str | None,
    timeout: float,
) -> tuple[Secret, SecretCapabilityMetadata] | None:
    """Read one secret-backed capability using explicit scope."""
    expected = SecretCapabilityMetadata(kind=kind, id=id, env_id=env_id)
    with await Kube.host(timeout=timeout) as kube:
        secret = await Secret.get(
            kube=kube,
            namespace=BERTRAND_NAMESPACE,
            timeout=timeout,
            name=expected.name,
        )
        if secret is None:
            return None
        if expected != SecretCapabilityMetadata.from_secret(secret):
            raise OSError(
                f"cluster secret {expected.name!r} metadata does not match requested "
                f"{expected.kind} capability {expected.id!r}"
            )
        return secret, expected


async def put_secret_capability(
    *,
    kind: SecretCapabilityKind,
    id: KubeName,
    env_id: str | None,
    timeout: float,
    payload: bytes,
) -> SecretCapabilityMetadata:
    """Create or update one managed secret-backed capability."""
    expected = SecretCapabilityMetadata(kind=kind, id=id, env_id=env_id)
    with await Kube.host(timeout=timeout) as kube:
        existing = await Secret.get(
            kube=kube,
            namespace=BERTRAND_NAMESPACE,
            timeout=timeout,
            name=expected.name,
        )
        if existing is not None and expected != SecretCapabilityMetadata.from_secret(existing):
            raise OSError(
                f"cluster secret {expected.name!r} metadata does not match requested "
                f"{expected.kind} capability {expected.id!r}"
            )

        await Secret.create_or_patch(
            kube=kube,
            namespace=BERTRAND_NAMESPACE,
            timeout=timeout,
            name=expected.name,
            labels={
                CAPABILITY_MANAGED_V1: "true",
                CAPABILITY_KIND_V1: expected.kind,
                CAPABILITY_ENV_ID_V1: expected.env_id or "shared",
            },
            annotations={CAPABILITY_ID_V1: expected.id},
            payload=payload,
        )
        return expected


async def delete_secret_capability(
    *,
    kind: SecretCapabilityKind,
    id: KubeName,
    env_id: str | None,
    timeout: float,
) -> bool:
    """Delete one managed secret-backed capability."""
    expected = SecretCapabilityMetadata(kind=kind, id=id, env_id=env_id)
    with await Kube.host(timeout=timeout) as kube:
        existing = await Secret.get(
            kube=kube,
            namespace=BERTRAND_NAMESPACE,
            timeout=timeout,
            name=expected.name,
        )
        if existing is None:
            return False
        if expected != SecretCapabilityMetadata.from_secret(existing):
            raise OSError(
                f"cluster secret {expected.name!r} metadata does not match requested "
                f"{expected.kind} capability {expected.id!r}"
            )

        await existing.delete(kube=kube, timeout=timeout)
        return True


async def list_secret_capabilities(
    *,
    kind: SecretCapabilityKind,
    env_id: str | None,
    timeout: float,
) -> builtins.list[tuple[Secret, SecretCapabilityMetadata]]:
    """List managed secret-backed capability metadata."""
    if env_id is not None:
        env_id = _check_uuid(env_id)
    with await Kube.host(timeout=timeout) as kube:
        parsed = await Secret.list(
            kube=kube,
            namespace=BERTRAND_NAMESPACE,
            timeout=timeout,
            labels={
                CAPABILITY_MANAGED_V1: "true",
                CAPABILITY_KIND_V1: kind,
                CAPABILITY_ENV_ID_V1: env_id or "shared",
            },
        )
        out = [(secret, SecretCapabilityMetadata.from_secret(secret)) for secret in parsed]
        out.sort(key=lambda item: (item[1].kind, item[1].name))
        return out
