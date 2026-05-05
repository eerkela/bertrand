"""Secret wrappers and secret/SSH build-flag resolution for Kubernetes."""
from __future__ import annotations

import base64
import binascii
import builtins
import hashlib
import shutil
import sys
import uuid
from collections.abc import Collection, Mapping
from dataclasses import dataclass
from pathlib import Path
from typing import Literal, Self

from kubernetes import client as kube_client

from ..config.core import KubeName, _check_kube_name, _check_uuid
from ..run import BERTRAND_NAMESPACE, CACHE_DIR, atomic_write_bytes
from .api import Kube, _label_selector

CAPABILITY_DIR = CACHE_DIR / "capabilities"
CAPABILITY_MANAGED_V1 = "bertrand.dev/capability-managed.v1"
CAPABILITY_ENV_ID_V1 = "bertrand.dev/capability-env-id.v1"
CAPABILITY_ID_V1 = "bertrand.dev/capability-id.v1"


def _secret_name(id: KubeName, env_id: str | None) -> KubeName:
    id = _check_kube_name(id)
    if env_id is not None:
        env_id = _check_uuid(env_id)

    parts: tuple[str, ...]
    if env_id is None:
        parts = ("shared", id)
    else:
        parts = (env_id, id)

    # include lengths before each token to keep hash input boundaries explicit
    digest = hashlib.sha256()
    for part in parts:
        encoded = part.encode("utf-8")
        digest.update(len(encoded).to_bytes(8, "big"))
        digest.update(encoded)

    return f"bertrand-secret-{digest.hexdigest()}"


@dataclass(frozen=True)
class Secret:
    """General-purpose wrapper around one Kubernetes Secret object."""
    obj: kube_client.V1Secret

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: KubeName,
        timeout: float,
    ) -> Self | None:
        """Read one Kubernetes Secret by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the secret.
        name : KubeName
            Name of the secret to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Secret | None
            Wrapped Kubernetes secret, or `None` when the object does not exist.

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds `timeout`.
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
            context=f"failed to read cluster secret {name!r} in namespace {namespace!r}",
        )
        if payload is None:
            return None
        if not isinstance(payload, kube_client.V1Secret):
            raise OSError(
                f"malformed Kubernetes Secret payload for {name!r} in namespace "
                f"{namespace!r}"
            )
        return cls(obj=payload)

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        namespaces: Collection[str] | None = None,
        labels: Mapping[str, str] | None = None,
        timeout: float,
    ) -> builtins.list[Self]:
        """List Kubernetes Secrets with optional namespace and label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespaces : Collection[str] | None, optional
            Optional namespace selector. If `None`, query all namespaces. Otherwise,
            entries are trimmed, deduplicated, and queried individually.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        builtins.list[Secret]
            Validated secret wrappers matching the requested filters.

        Raises
        ------
        TimeoutError
            If any Kubernetes request exceeds `timeout`.
        OSError
            If a Kubernetes API call fails or returns malformed data.
        """
        label_selector = _label_selector(labels)
        payloads: builtins.list[kube_client.V1SecretList] = []

        # search all namespaces
        if namespaces is None:
            payload = await kube.run(
                lambda request_timeout: kube.core.list_secret_for_all_namespaces(
                    label_selector=label_selector,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context="failed to list cluster secrets across all namespaces",
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
                    lambda request_timeout, namespace=namespace: kube.core.list_namespaced_secret(
                        namespace=namespace,
                        label_selector=label_selector,
                        _request_timeout=request_timeout,
                    ),
                    timeout=timeout,
                    context=f"failed to list cluster secrets in namespace {namespace!r}",
                )
                if payload is not None:
                    payloads.append(payload)

        out: builtins.list[Self] = []
        for payload in payloads:
            if not isinstance(payload, kube_client.V1SecretList):
                raise OSError("malformed Kubernetes Secret list payload")
            for item in payload.items or []:
                if not isinstance(item, kube_client.V1Secret):
                    raise OSError("malformed Kubernetes Secret entry in list payload")
                out.append(cls(obj=item))
        return out

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: KubeName,
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
        payload: bytes,
        timeout: float,
    ) -> Self:
        """Create or patch one Kubernetes Secret payload.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the secret.
        name : KubeName
            Secret name to create or patch.
        labels : Mapping[str, str] | None
            Labels to apply to `metadata.labels`.
        annotations : Mapping[str, str] | None
            Annotations to apply to `metadata.annotations`.
        payload : bytes
            Raw payload bytes to store at `data.value` (base64-encoded on write).
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Secret
            Wrapped created or patched Kubernetes secret.

        Raises
        ------
        TimeoutError
            If any Kubernetes request exceeds `timeout`.
        OSError
            If Kubernetes create/patch fails or returns malformed data.
        """
        manifest = {
            "apiVersion": "v1",
            "kind": "Secret",
            "metadata": {
                "name": name,
                "namespace": namespace,
                "labels": dict(labels or {}),
                "annotations": dict(annotations or {}),
            },
            "type": "Opaque",
            "data": {"value": base64.b64encode(payload).decode("ascii")},
        }

        # 
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

    @property
    def namespace(self) -> str:
        """Return this secret's namespace.

        Returns
        -------
        str
            Trimmed namespace from `metadata`.

        Raises
        ------
        OSError
            If `metadata.namespace` is missing or malformed.
        """
        metadata = self.obj.metadata or kube_client.V1ObjectMeta()
        namespace = (metadata.namespace or "").strip()
        if not namespace:
            raise OSError("secret metadata is missing namespace")
        return namespace

    @property
    def name(self) -> KubeName:
        """Return this secret's name.

        Returns
        -------
        KubeName
            Trimmed name from `metadata`.

        Raises
        ------
        OSError
            If `metadata.name` is missing or malformed.
        """
        metadata = self.obj.metadata or kube_client.V1ObjectMeta()
        name = (metadata.name or "").strip()
        if not name:
            raise OSError("secret metadata is missing name")
        return name

    @property
    def value(self) -> bytes:
        """Decode the binary payload stored in the secret's `value`.

        Returns
        -------
        bytes
            Decoded payload bytes.

        Raises
        ------
        OSError
            If the secret's `value` is missing or contains invalid base64.
        """
        name = self.name
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

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this secret by identity.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Secret | None
            Latest secret wrapper if it still exists, otherwise `None`.

        Raises
        ------
        TimeoutError
            If the Kubernetes request exceeds `timeout`.
        OSError
            If identity is malformed, API request fails, or payload is malformed.
        """
        return await type(self).get(
            kube=kube,
            namespace=self.namespace,
            timeout=timeout,
            name=self.name,
        )

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this secret from the cluster.

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
        name = self.name
        await kube.run(
            lambda request_timeout: kube.core.delete_namespaced_secret(
                name=name,
                namespace=self.namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete cluster secret {name!r}",
        )

    def build_flag(
        self,
        *,
        mode: Literal["secret", "ssh"],
        id: KubeName,
        staged_dir: Path,
    ) -> tuple[str, str]:
        """Stage this secret payload and emit one CLI flag pair.

        Parameters
        ----------
        mode : Literal["secret", "ssh"]
            Flag mode to emit. `"secret"` maps to `--secret`; `"ssh"` maps to
            `--ssh`.
        id : KubeName
            Logical secret ID used in the emitted flag value and staged filename.
        staged_dir : Path
            Root staging directory for build-time secret payload files.

        Returns
        -------
        tuple[str, str]
            One `(flag_name, flag_value)` pair suitable for appending directly to
            CLI arguments.

        Raises
        ------
        OSError
            If this secret payload cannot be decoded or written to staged storage.
        ValueError
            If `id` is not a valid Kubernetes name.
        """
        validated_id = _check_kube_name(id)
        target = _stage_secret_payload(staged=staged_dir, id=validated_id, payload=self.value)
        if mode == "secret":
            return "--secret", f"id={validated_id},src={target}"
        return "--ssh", f"id={validated_id},src={target}"


# TODO: review build flags in conjunction with image builds when I get to that point.


async def build_secret_flags(
    kube: Kube,
    *,
    env_id: str,
    build: object,
    timeout: float,
) -> tuple[tuple[str, ...], Path | None]:
    """Build secret and SSH CLI flags for one build request.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    env_id : str
        Environment UUID used for env-first then shared fallback lookups.
    build : object
        Build configuration object with optional `secrets` and `ssh` iterables.
    timeout : float
        Maximum request budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    tuple[tuple[str, ...], Path | None]
        A tuple containing emitted CLI args and the staged payload directory.
        If no secret/ssh requests exist, returns `((), None)`.

    Raises
    ------
    TimeoutError
        If any Kubernetes request exceeds `timeout`.
    OSError
        If required secrets are missing, managed metadata is invalid, or payloads
        are malformed.
    ValueError
        If `env_id` is not a valid UUID.
    """
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
                await _resolve_build_flag(
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
                await _resolve_build_flag(
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
        cleanup_secret_staged(staged)
        raise


def cleanup_secret_staged(path: Path | None) -> None:
    """Delete staged build-time secret payload files.

    Parameters
    ----------
    path : Path | None
        Staging directory path returned by :func:`build_secret_flags`. `None` is a
        no-op.

    Returns
    -------
    None
        This function returns `None`.
    """
    if path is None:
        return
    shutil.rmtree(path, ignore_errors=True)


async def _resolve_build_flag(
    kube: Kube,
    *,
    mode: Literal["secret", "ssh"],
    id: KubeName,
    required: bool,
    env_id: str,
    timeout: float,
    staged: Path,
) -> builtins.list[str]:
    secret = await _resolve_secret(
        kube=kube,
        id=id,
        env_id=env_id,
        timeout=timeout,
    )
    if secret is None:
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

    return builtins.list(secret.build_flag(mode=mode, id=id, staged_dir=staged))


async def _resolve_secret(
    kube: Kube,
    *,
    id: KubeName,
    env_id: str,
    timeout: float,
) -> Secret | None:
    # env scope takes precedence over shared scope
    for scope in (env_id, None):
        name = _secret_name(id=id, env_id=scope)
        secret = await Secret.get(
            kube=kube,
            namespace=BERTRAND_NAMESPACE,
            timeout=timeout,
            name=name,
        )
        if secret is None:
            continue
        _assert_managed_secret(
            secret=secret,
            expected_name=name,
            id=id,
            env_id=scope,
        )
        return secret
    return None


def _assert_managed_secret(
    *,
    secret: Secret,
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


def _stage_secret_payload(
    staged: Path,
    id: KubeName,
    payload: bytes,
) -> Path:
    target = staged / "secrets" / id
    target.parent.mkdir(parents=True, exist_ok=True)
    atomic_write_bytes(target, payload)
    target.chmod(0o600)
    return target
