"""Host-agnostic capability resolution and staging via Kubernetes Secrets."""
from __future__ import annotations

import base64
import hashlib
import shutil
import sys
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Literal, Self, cast

from kubernetes import client as kube_client

from ..config.core import KubeName, _check_kube_name, _check_uuid
from ..run import BERTRAND_NAMESPACE, CACHE_DIR, atomic_write_bytes
from .api import Kube, KubeSecret

type CapabilityKind = Literal["secret", "ssh", "device"]
DEVICE_PERMISSIONS = frozenset({"r", "w", "m", "rw", "rm", "wm", "rwm"})
CAPABILITY_DIR = CACHE_DIR / "capabilities"
CAPABILITY_MANAGED_V1 = "bertrand.dev/capability-managed.v1"
CAPABILITY_KIND_V1 = "bertrand.dev/capability-kind.v1"
CAPABILITY_ENV_ID_V1 = "bertrand.dev/capability-env-id.v1"
CAPABILITY_ID_V1 = "bertrand.dev/capability-id.v1"


@dataclass(frozen=True)
class CapabilityMetadata:
    """Metadata for one managed capability secret."""
    kind: CapabilityKind
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
    def from_secret(cls, secret: KubeSecret) -> Self:
        """Parse capability metadata from a Kubernetes Secret's annotations.

        Parameters
        ----------
        secret : KubeSecret
            The Kubernetes Secret to parse, which must have the expected labels and
            annotations for a managed capability.

        Returns
        -------
        CapabilityMetadata
            The parsed capability metadata.

        Raises
        ------
        OSError
            If the Secret is missing required labels or annotations, or if the labels or
            annotations have invalid values.
        """
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
        if kind not in ("secret", "ssh", "device"):
            raise OSError(
                f"cluster secret {name!r} has missing/invalid "
                f"{CAPABILITY_KIND_V1!r}"
            )
        kind = cast(CapabilityKind, kind)
        id = annotations.get(CAPABILITY_ID_V1)
        if id is None:
            raise OSError(
                f"cluster secret {name!r} is missing annotation "
                f"{CAPABILITY_ID_V1!r}"
            )
        id = _check_kube_name(id)
        env_id: str | None = labels.get(CAPABILITY_ENV_ID_V1)
        if env_id is None:
            raise OSError(
                f"cluster secret {name!r} is missing label "
                f"{CAPABILITY_ENV_ID_V1!r}"
            )
        env_id = None if env_id == "shared" else _check_uuid(env_id)
        return cls(kind=kind, id=id, env_id=env_id)


def _kube_secret_name(
    kind: CapabilityKind,
    id: KubeName,
    env_id: str | None,
) -> KubeName:
    id = _check_kube_name(id)
    if env_id is not None:
        env_id = _check_uuid(env_id)

    # if env-scoped, salt the hash with the given env id
    if env_id is None:
        parts = ("shared", kind, id)
    else:
        parts = (env_id, kind, id)

    # prefix with component lengths to guard against ambiguities
    h = hashlib.sha256()
    for part in parts:
        encoded = part.encode("utf-8")
        h.update(len(encoded).to_bytes(8, "big"))
        h.update(encoded)

    return f"bertrand-{kind}-{h.hexdigest()}"


@dataclass
class Capabilities:
    """Builder-style resolver for host-agnostic capability names, which are stored as
    Kubernetes Secrets.

    This class is designed to be used as an async context manager, which stages any
    requested payload files (via builder-style `.secret()`, `.ssh()`, `.device()`) in a
    temporary directory and cleans them up on exit.  The `finalize()` method resolves
    all requests and produces the corresponding CLI flags to be passed back to the
    bootstrap context.
    """
    @dataclass
    class Secret:
        """A stored secret capability request."""
        required: bool

    @dataclass
    class SSH:
        """A stored SSH capability request."""
        required: bool

    @dataclass
    class Device:
        """A stored device capability request."""
        required: bool
        permissions: str

    env_id: str
    timeout: float
    run_id: str = field(default_factory=lambda: uuid.uuid4().hex)
    _secrets: dict[str, Secret] = field(default_factory=dict, repr=False)
    _ssh: dict[str, SSH] = field(default_factory=dict, repr=False)
    _devices: dict[str, Device] = field(default_factory=dict, repr=False)
    _entered: bool = field(default=False, repr=False)
    _finalized: bool = field(default=False, repr=False)

    def __post_init__(self) -> None:
        self.env_id = _check_uuid(self.env_id)
        if self.timeout <= 0:
            raise TimeoutError("capability timeout must be non-negative")

    async def __aenter__(self) -> Self:
        if self._entered:
            raise RuntimeError("capabilities context cannot be entered twice")
        self._entered = True
        return self

    async def __aexit__(
        self,
        exc_type: type[BaseException] | None,
        _exc_value: BaseException | None,
        _traceback: object | None,
    ) -> None:
        self._entered = False
        try:
            path = CAPABILITY_DIR / self.run_id
            if path.exists():
                shutil.rmtree(path)
        except OSError:
            if exc_type is None:
                raise

    def secret(self, *, id: KubeName, required: bool) -> None:
        """Register one build secret capability request.

        Parameters
        ----------
        id : str
            The logical name of the requested capability, which is opaque to Bertrand
            and must be provisioned by the user as a cluster Secret.
        required : bool
            Whether this capability is required for the build to proceed. If `True`
            and the capability cannot be resolved, `finalize()` will raise an error.
            Otherwise, it will print a warning and continue without the capability.

        Raises
        ------
        RuntimeError
            If called after `finalize()`.
        ValueError
            If the `id` is invalid or duplicates a previously registered capability.
        """
        if self._finalized:
            raise RuntimeError(
                "capabilities are already finalized and cannot be modified"
            )
        id = _check_kube_name(id)
        if id in self._secrets:
            raise ValueError(f"duplicate secret capability ID: {id!r}")
        if id in self._ssh:
            raise ValueError(
                f"capability ID {id!r} is already registered as an SSH capability"
            )
        if id in self._devices:
            raise ValueError(
                f"capability ID {id!r} is already registered as a device capability"
            )
        self._secrets[id] = self.Secret(required=required)

    def ssh(self, *, id: KubeName, required: bool) -> None:
        """Register one build SSH capability request.

        Parameters
        ----------
        id : str
            The logical name of the requested capability, which is opaque to Bertrand
            and must be provisioned by the user as a cluster Secret.
        required : bool
            Whether this capability is required for the build to proceed. If `True`
            and the capability cannot be resolved, `finalize()` will raise an error.
            Otherwise, it will print a warning and continue without the capability.

        Raises
        ------
        RuntimeError
            If called after `finalize()`.
        ValueError
            If the `id` is invalid or duplicates a previously registered capability.
        """
        if self._finalized:
            raise RuntimeError(
                "capabilities are already finalized and cannot be modified"
            )
        id = _check_kube_name(id)
        if id in self._secrets:
            raise ValueError(
                f"capability ID {id!r} is already registered as a secret capability"
            )
        if id in self._ssh:
            raise ValueError(f"duplicate SSH capability ID: {id!r}")
        if id in self._devices:
            raise ValueError(
                f"capability ID {id!r} is already registered as a device capability"
            )
        self._ssh[id] = self.SSH(required=required)

    def device(
        self,
        *,
        id: KubeName,
        required: bool,
        permissions: str,
    ) -> None:
        """Register one build device capability request.

        Parameters
        ----------
        id : str
            The logical name of the requested capability, which is opaque to Bertrand
            and must be provisioned by the user as a cluster Secret.
        required : bool
            Whether this capability is required for the build to proceed. If `True`
            and the capability cannot be resolved, `finalize()` will raise an error.
            Otherwise, it will print a warning and continue without the capability.
        permissions : str
            The device permissions to request, which must be a non-empty combination of
            "r" (read), "w" (write), and "m" (make).

        Raises
        ------
        RuntimeError
            If called after `finalize()`.
        ValueError
            If the `id` is invalid or duplicates a previously registered capability, or
            if the `permissions` string is invalid.
        """
        if self._finalized:
            raise RuntimeError(
                "capabilities are already finalized and cannot be modified"
            )
        id = _check_kube_name(id)
        if id in self._secrets:
            raise ValueError(
                f"capability ID {id!r} is already registered as a secret capability"
            )
        if id in self._ssh:
            raise ValueError(
                f"capability ID {id!r} is already registered as an SSH capability"
            )
        if id in self._devices:
            raise ValueError(f"duplicate device capability ID: {id!r}")
        if permissions not in DEVICE_PERMISSIONS:
            raise ValueError(
                f"invalid device permissions {permissions!r}; must be a non-empty "
                "combination of 'r', 'w', and 'm'"
            )
        self._devices[id] = self.Device(required=required, permissions=permissions)

    def _stage_payload(
        self,
        kind: Literal["secrets", "ssh"],
        id: KubeName,
        payload: bytes,
    ) -> Path:
        target = CAPABILITY_DIR / self.run_id / kind / id
        target.parent.mkdir(parents=True, exist_ok=True)
        atomic_write_bytes(target, payload)
        target.chmod(0o600)
        return target

    async def _resolve(
        self,
        kind: CapabilityKind,
        id: KubeName,
        *,
        kube: Kube,
    ) -> tuple[KubeSecret, CapabilityMetadata] | None:
        # check local secrets first
        expected = CapabilityMetadata(kind=kind, id=id, env_id=self.env_id)
        secret = await KubeSecret.get(
            kube=kube,
            namespace=BERTRAND_NAMESPACE,
            timeout=self.timeout,
            name=expected.name,
        )
        if secret is not None:
            if expected != CapabilityMetadata.from_secret(secret):
                raise OSError(
                    f"environment secret {expected.name!r} metadata does not match "
                    f"requested {expected.kind} capability {expected.id!r}"
                )
            return secret, expected

        # fall back to cluster-wide secrets if env-scoped and not found
        expected = CapabilityMetadata(kind=kind, id=id, env_id=None)
        secret = await KubeSecret.get(
            kube=kube,
            namespace=BERTRAND_NAMESPACE,
            timeout=self.timeout,
            name=expected.name,
        )
        if secret is not None:
            if expected != CapabilityMetadata.from_secret(secret):
                raise OSError(
                    f"cluster secret {expected.name!r} metadata does not match "
                    f"requested {expected.kind} capability {expected.id!r}"
                )
            return secret, expected

        # missing
        return None

    async def _resolve_secret(self, id: KubeName, request: Secret, *, kube: Kube) -> list[str]:
        resolved = await self._resolve(
            kind="secret",
            id=id,
            kube=kube,
        )
        if resolved is None:
            if request.required:
                raise OSError(f"missing required secret: {id!r}")
            print(
                f"bertrand: optional secret {id!r} was not found; continuing without it",
                file=sys.stderr,
            )
            return []

        data, meta = resolved
        token = meta.name
        payload = data.decode(token)
        target = self._stage_payload("secrets", id, payload)
        return ["--secret", f"id={id},src={target}"]

    async def _resolve_ssh(self, id: KubeName, request: SSH, *, kube: Kube) -> list[str]:
        resolved = await self._resolve(
            kind="ssh",
            id=id,
            kube=kube,
        )
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
        token = meta.name
        payload = data.decode(token)
        target = self._stage_payload("ssh", id, payload)
        return ["--ssh", f"id={id},src={target}"]

    async def _resolve_device(self, id: KubeName, request: Device, *, kube: Kube) -> list[str]:
        resolved = await self._resolve(
            kind="device",
            id=id,
            kube=kube,
        )
        if resolved is None:
            if request.required:
                raise OSError(f"missing required device selector: {id!r}")
            print(
                f"bertrand: optional device selector {id!r} was not found; continuing "
                "without it",
                file=sys.stderr,
            )
            return []

        data, meta = resolved
        token = meta.name
        try:
            selector = data.decode(token).decode("utf-8").strip()
        except UnicodeDecodeError as err:
            raise OSError(
                f"cluster secret {token!r} key 'data.value' must decode as UTF-8 text"
            ) from err
        if not selector:
            raise OSError(f"cluster secret {token!r} key 'data.value' cannot be empty")
        return ["--device", f"{selector}:{request.permissions}"]

    async def finalize(self) -> tuple[str, ...]:
        """Resolve all registered requests and stage required payload files.

        Returns
        -------
        tuple[str, ...]
            A sequence of CLI flags for all registered capabilities, which can be
            passed to `nerdctl build` to produce the desired image.

        Raises
        ------
        RuntimeError
            If called more than once, or if called outside of a capabilities context.
        """
        if self._finalized:
            raise RuntimeError("capabilities are already finalized")
        if not self._entered:
            raise RuntimeError("finalize() must be called inside a capabilities context")

        try:
            flags: list[str] = []
            with await Kube.host(timeout=self.timeout) as kube:
                for id, secret in self._secrets.items():
                    flags.extend(await self._resolve_secret(id, secret, kube=kube))
                for id, ssh in self._ssh.items():
                    flags.extend(await self._resolve_ssh(id, ssh, kube=kube))
                for id, device in self._devices.items():
                    flags.extend(await self._resolve_device(id, device, kube=kube))

            self._finalized = True
            return tuple(flags)
        except Exception:
            shutil.rmtree(CAPABILITY_DIR / self.run_id, ignore_errors=True)
            raise


async def get_capability(
    *,
    kind: CapabilityKind,
    id: KubeName,
    env_id: str | None,
    timeout: float,
) -> tuple[KubeSecret, CapabilityMetadata] | None:
    """Read one capability secret using explicit scope.

    Parameters
    ----------
    kind : str
        The kind of capability to read, which controls the expected payload key and
        format.
    id : str
        The logical name of the capability to read, which is opaque to Bertrand and
        must be provisioned via `put_capability()`.
    env_id : str | None
        The environment ID to scope to.  If None, only cluster-wide capabilities will
        be considered.
    timeout : float
        The maximum time to wait for the Kubernetes Secret to be retrieved, in
        seconds.

    Returns
    -------
    tuple[KubeSecret, CapabilityMetadata] | None
        The validated Kubernetes Secret data for the requested capability (which holds
        the sensitive payload) along with its metadata (which is safe to log or
        display), or `None` if no matching Secret is found.

    Raises
    ------
    OSError
        If a matching Kubernetes Secret is found but has malformed metadata.
    """
    expected = CapabilityMetadata(kind=kind, id=id, env_id=env_id)
    with await Kube.host(timeout=timeout) as kube:
        secret = await KubeSecret.get(
            kube=kube,
            namespace=BERTRAND_NAMESPACE,
            timeout=timeout,
            name=expected.name,
        )
        if secret is None:
            return None
        if expected != CapabilityMetadata.from_secret(secret):
            raise OSError(
                f"cluster secret {expected.name!r} metadata does not match requested "
                f"{expected.kind} capability {expected.id!r}"
            )
        return secret, expected


async def put_capability(
    *,
    kind: CapabilityKind,
    id: KubeName,
    env_id: str | None,
    timeout: float,
    payload: bytes,
) -> CapabilityMetadata:
    """Create or update one managed capability secret.

    Parameters
    ----------
    kind : str
        The kind of capability to write, which controls the expected payload key and
        format.
    id : str
        The logical name of the capability to write, which is opaque to Bertrand, and
        must be a valid Kubernetes Secret name.
    env_id : str | None
        The environment ID to scope to.  If None, the capabilities will be written
        cluster-wide.
    timeout : float
        The maximum time to wait for the Kubernetes Secret to be applied, in
        seconds.
    payload : bytes
        The payload data for the capability.  Given as an encoded byte string, usually
        in UTF-8 if the payload is textual.  The data will be base64-encoded and stored
        in the Kubernetes Secret's `data` field.

    Returns
    -------
    CapabilityMetadata
        The metadata for the created or updated capability.

    Raises
    ------
    OSError
        If a matching Kubernetes Secret is found but has malformed metadata.
    """
    expected = CapabilityMetadata(kind=kind, id=id, env_id=env_id)
    with await Kube.host(timeout=timeout) as kube:
        # search for existing secret at indicated scope
        existing = await KubeSecret.get(
            kube=kube,
            namespace=BERTRAND_NAMESPACE,
            timeout=timeout,
            name=expected.name,
        )
        if existing is not None and expected != CapabilityMetadata.from_secret(existing):
            raise OSError(
                f"cluster secret {expected.name!r} metadata does not match requested "
                f"{expected.kind} capability {expected.id!r}"
            )

        # form annotations for updated secret
        manifest = {
            "apiVersion": "v1",
            "kind": "Secret",
            "metadata": {
                "name": expected.name,
                "namespace": BERTRAND_NAMESPACE,
                "labels": {
                    CAPABILITY_MANAGED_V1: "true",
                    CAPABILITY_KIND_V1: expected.kind,
                    CAPABILITY_ENV_ID_V1: expected.env_id or "shared",
                },
                "annotations": {CAPABILITY_ID_V1: expected.id},
            },
            "type": "Opaque",
            "data": {
                "value": base64.b64encode(payload).decode("ascii"),
            },
        }

        if existing is None:
            try:
                await kube.run(
                    lambda request_timeout: kube.core.create_namespaced_secret(
                        namespace=BERTRAND_NAMESPACE,
                        body=manifest,
                        _request_timeout=request_timeout,
                    ),
                    timeout=timeout,
                    context=f"failed to create cluster secret {expected.name!r}",
                )
            except OSError as err:
                detail = str(err).lower()
                if "status 409" not in detail and "already exists" not in detail:
                    raise
                await kube.run(
                    lambda request_timeout: kube.core.patch_namespaced_secret(
                        name=expected.name,
                        namespace=BERTRAND_NAMESPACE,
                        body=manifest,
                        _request_timeout=request_timeout,
                    ),
                    timeout=timeout,
                    context=f"failed to update cluster secret {expected.name!r}",
                )
        else:
            await kube.run(
                lambda request_timeout: kube.core.patch_namespaced_secret(
                    name=expected.name,
                    namespace=BERTRAND_NAMESPACE,
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=f"failed to update cluster secret {expected.name!r}",
            )

        return expected


async def delete_capability(
    *,
    kind: CapabilityKind,
    id: KubeName,
    env_id: str | None,
    timeout: float,
) -> bool:
    """Delete one managed capability secret.

    Parameters
    ----------
    kind : str
        The kind of capability to delete, which controls the expected metadata format.
    id : str
        The logical name of the capability to delete, which is opaque to Bertrand and
        must be a valid Kubernetes Secret name.
    env_id : str | None
        The environment ID to scope to.  If None, only cluster-wide capabilities will
        be considered for deletion.
    timeout : float
        The maximum time to wait for the Kubernetes Secret to be deleted, in seconds.

    Returns
    -------
    bool
        True if the capability was deleted, False if it did not exist.

    Raises
    ------
    OSError
        If a matching Kubernetes Secret is found but has malformed metadata.
    """
    expected = CapabilityMetadata(kind=kind, id=id, env_id=env_id)
    with await Kube.host(timeout=timeout) as kube:
        existing = await KubeSecret.get(
            kube=kube,
            namespace=BERTRAND_NAMESPACE,
            timeout=timeout,
            name=expected.name,
        )
        if existing is None:
            return False
        if expected != CapabilityMetadata.from_secret(existing):
            raise OSError(
                f"cluster secret {expected.name!r} metadata does not match requested "
                f"{expected.kind} capability {expected.id!r}"
            )

        await kube.run(
            lambda request_timeout: kube.core.delete_namespaced_secret(
                name=expected.name,
                namespace=BERTRAND_NAMESPACE,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete cluster secret {expected.name!r}",
        )
        return True


async def list_capabilities(
    *,
    kind: CapabilityKind,
    env_id: str | None,
    timeout: float,
) -> list[tuple[KubeSecret, CapabilityMetadata]]:
    """List managed capability metadata.

    Parameters
    ----------
    kind : CapabilityKind
        The kind of capability to filter by.
    env_id : str | None
        The environment ID to filter by.  If None, cluster-wide capabilities will be
        returned.  Note that final build system bootstrap will always proceed from
        environment scope to cluster scope, so the effective set of candidates for a
        given environment is the union of both scopes.  This function only lists one
        scope at a time.
    timeout : float
        The maximum time to wait for the Kubernetes Secret list, in seconds.

    Returns
    -------
    list[tuple[KubeSecret, CapabilityMetadata]]
        The metadata for all matching capabilities, sorted by kind, and name.  The
        first element of each tuple is the raw, sensitve Kubernetes Secret data, and
        the second element is the parsed metadata, which is safe to log or display.
    """
    if env_id is not None:
        env_id = _check_uuid(env_id)
    with await Kube.host(timeout=timeout) as kube:
        parsed = await KubeSecret.list(
            kube=kube,
            namespace=BERTRAND_NAMESPACE,
            timeout=timeout,
            labels={
                CAPABILITY_MANAGED_V1: "true",
                CAPABILITY_KIND_V1: kind,
                CAPABILITY_ENV_ID_V1: env_id or "shared",
            },
        )
        out = [
            (secret, CapabilityMetadata.from_secret(secret))
            for secret in parsed
        ]
        out.sort(key=lambda item: (item[1].kind, item[1].name))
        return out
