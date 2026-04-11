"""Host-agnostic capability resolution and staging via Kubernetes Secrets."""
from __future__ import annotations

import base64
import binascii
import hashlib
import json
import shutil
import sys
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Annotated, Literal, Self, cast

from pydantic import BaseModel, ConfigDict, Field, StrictStr, ValidationError

from ..config.core import KUBE_SECRET_RE, SecretName
from ..run import BERTRAND_NAMESPACE, TOOLS_TMP_DIR, atomic_write_bytes, kubectl

type CapabilityKind = Literal["secret", "ssh", "device"]
DEVICE_PERMISSIONS = frozenset({"r", "w", "m", "rw", "rm", "wm", "rwm"})
CAPABILITY_DIR = TOOLS_TMP_DIR / "capabilities"
CAPABILITY_MANAGED_V1 = "bertrand.dev/capability-managed.v1"
CAPABILITY_KIND_V1 = "bertrand.dev/capability-kind.v1"
CAPABILITY_ENV_ID_V1 = "bertrand.dev/capability-env-id.v1"
CAPABILITY_ID_V1 = "bertrand.dev/capability-id.v1"
CAPABILITY_DATA_KEY: dict[CapabilityKind, Literal["value", "private_key", "selector"]] = {
    "secret": "value",
    "ssh": "private_key",
    "device": "selector",
}


class KubeSecret(BaseModel):
    """Validated subset of a Kubernetes Secret payload."""
    class Metadata(BaseModel):
        """Validated subset of Kubernetes Secret metadata."""
        model_config = ConfigDict(extra="ignore")
        name: str = ""
        labels: Annotated[dict[StrictStr, StrictStr], Field(default_factory=dict)]
        annotations: Annotated[dict[StrictStr, StrictStr], Field(default_factory=dict)]

    class List(BaseModel):
        """Validated subset of a Kubernetes Secret list payload."""
        model_config = ConfigDict(extra="ignore")
        items: list[KubeSecret] = Field(default_factory=list)

    model_config = ConfigDict(extra="ignore")
    metadata: Metadata = Field(default_factory=Metadata.model_construct)
    data: Annotated[dict[StrictStr, StrictStr], Field(default_factory=dict)]

    @classmethod
    async def get(cls, name: SecretName, timeout: float) -> Self | None:
        """Load a Kubernetes Secret by name and validate its structure.

        Parameters
        ----------
        name : str
            The name of the Kubernetes Secret, which must be lowercase with `-` and/or
            `.` separators.
        timeout : float
            The maximum time to wait for the Kubernetes Secret to be retrieved, in
            seconds.

        Returns
        -------
        KubeSecret | None
            The validated Kubernetes Secret data, or `None` if the Secret is not found.
        """
        payload = (await kubectl(
            [
                "get",
                "secret",
                name,
                "-n", BERTRAND_NAMESPACE,
                "-o", "json",
                "--ignore-not-found=true",
            ],
            capture_output=True,
            timeout=timeout,
        )).stdout.strip()
        if not payload:
            return None
        try:
            return cls.model_validate_json(payload)
        except ValidationError as err:
            raise OSError(
                f"cluster secret {name!r} returned malformed JSON payload"
            ) from err

    def decode(
        self,
        kind: Literal["value", "private_key", "selector"],
        name: str
    ) -> bytes:
        """Decode a base64-encoded value from the validated Kube Secret.

        Parameters
        ----------
        kind : str
            The key within the Kubernetes Secret data to decode.
        name : str
            The name of the Kubernetes Secret, for error messages.

        Returns
        -------
        bytes
            The decoded value of the specified key.

        Raises
        ------
        OSError
            If the specified key is not defined in the Secret data, or if the value is
            not valid base64-encoded data.
        """
        if kind not in self.data:
            raise OSError(
                f"cluster secret {name!r} does not define required key 'data.{kind}'"
            )
        try:
            return base64.b64decode(self.data[kind], validate=True)
        except (binascii.Error, ValueError) as err:
            raise OSError(
                f"cluster secret {name!r} contains invalid base64 data for key "
                f"'data.{kind}'"
            ) from err


@dataclass(frozen=True)
class CapabilityMetadata:
    """Metadata for one managed capability secret."""
    kind: CapabilityKind
    id: SecretName
    env_id: str | None
    name: SecretName = field(init=False, repr=False)

    def __post_init__(self) -> None:
        object.__setattr__(self, "id", _normalize_id(self.id))
        object.__setattr__(self, "env_id", _normalize_env_id(self.env_id))
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
        if secret.metadata.labels.get(CAPABILITY_MANAGED_V1) != "true":
            raise OSError(
                f"cluster secret {secret.metadata.name!r} collides with a Bertrand "
                "capability name but is unmanaged"
            )
        kind = secret.metadata.labels.get(CAPABILITY_KIND_V1)
        if kind not in ("secret", "ssh", "device"):
            raise OSError(
                f"cluster secret {secret.metadata.name!r} has missing/invalid "
                f"{CAPABILITY_KIND_V1!r}"
            )
        kind = cast(CapabilityKind, kind)
        id = secret.metadata.annotations.get(CAPABILITY_ID_V1)
        if id is None:
            raise OSError(
                f"cluster secret {secret.metadata.name!r} is missing annotation "
                f"{CAPABILITY_ID_V1!r}"
            )
        id = _normalize_id(id)
        env_id: str | None = secret.metadata.labels.get(CAPABILITY_ENV_ID_V1)
        if env_id is None:
            raise OSError(
                f"cluster secret {secret.metadata.name!r} is missing annotation "
                f"{CAPABILITY_ENV_ID_V1!r}"
            )
        env_id = _normalize_env_id(None if env_id == "shared" else env_id)
        return cls(kind=kind, id=id, env_id=env_id)


def _normalize_id(value: str) -> SecretName:
    normalized = value.strip()
    if not normalized:
        raise ValueError("capability ID cannot be empty")
    match = KUBE_SECRET_RE.fullmatch(normalized)
    if not match:
        raise ValueError(
            "capability ID must be a valid Kubernetes secret name (lowercase "
            f"alphanumeric with `-` and `.`): {value!r}"
        )
    return normalized


def _normalize_env_id(value: str | None) -> str | None:
    if value is None:
        return None
    try:
        return uuid.UUID(value.strip()).hex
    except ValueError as err:
        raise ValueError(f"environment ID must be a valid UUID: {value!r}") from err


def _kube_secret_name(
    kind: CapabilityKind,
    id: SecretName,
    env_id: str | None,
) -> SecretName:
    id = _normalize_id(id)
    env_id = _normalize_env_id(env_id)

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
        self.env_id = self.env_id.strip()
        if not self.env_id:
            raise ValueError("environment ID cannot be empty")
        if self.timeout < 0:
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

    def secret(self, *, id: SecretName, required: bool) -> None:
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
        id = _normalize_id(id)
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

    def ssh(self, *, id: SecretName, required: bool) -> None:
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
        id = _normalize_id(id)
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
        id: SecretName,
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
        id = _normalize_id(id)
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

    @staticmethod
    def _missing_required(
        kind: Literal["secret", "ssh", "device"],
        id: SecretName,
        env_token: str,
        shared_token: str
    ) -> OSError:
        return OSError(
            f"missing required build {kind} capability {id!r} (cluster secrets "
            f"{env_token!r} and {shared_token!r} not found in namespace "
            f"{BERTRAND_NAMESPACE!r})"
        )

    @staticmethod
    def _warn_optional(kind: str, id: SecretName) -> None:
        print(
            f"bertrand: optional {kind} capability {id!r} was not found; "
            "continuing without it",
            file=sys.stderr,
        )

    def _stage_payload(
        self,
        kind: Literal["secrets", "ssh"],
        id: SecretName,
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
        id: SecretName
    ) -> tuple[KubeSecret, CapabilityMetadata] | None:
        # check local secrets first
        expected = CapabilityMetadata(kind=kind, id=id, env_id=self.env_id)
        secret = await KubeSecret.get(expected.name, timeout=self.timeout)
        if secret is None:
            return None
        if expected != CapabilityMetadata.from_secret(secret):
            raise OSError(
                f"cluster secret {expected.name!r} metadata does not match requested "
                f"{expected.kind} capability {expected.id!r}"
            )

        # fall back to cluster-wide secrets if env-scoped and not found
        if self.env_id is None:
            return secret, expected
        expected = CapabilityMetadata(kind=kind, id=id, env_id=None)
        secret = await KubeSecret.get(expected.name, timeout=self.timeout)
        if secret is None:
            return None
        if expected != CapabilityMetadata.from_secret(secret):
            raise OSError(
                f"cluster secret {expected.name!r} metadata does not match requested "
                f"{expected.kind} capability {expected.id!r}"
            )
        return secret, expected

    async def _resolve_secret(self, id: SecretName, request: Secret) -> list[str]:
        resolved = await self._resolve(
            kind="secret",
            id=id,
        )
        if resolved is None:
            if request.required:
                raise self._missing_required(
                    "secret",
                    id,
                    _kube_secret_name("secret", id, self.env_id),
                    _kube_secret_name("secret", id, None),
                )
            self._warn_optional("secret", id)
            return []

        data, meta = resolved
        token = meta.name
        payload = data.decode("value", token)
        target = self._stage_payload("secrets", id, payload)
        return ["--secret", f"id={id},src={target}"]

    async def _resolve_ssh(self, id: SecretName, request: SSH) -> list[str]:
        resolved = await self._resolve(
            kind="ssh",
            id=id,
        )
        if resolved is None:
            if request.required:
                raise self._missing_required(
                    "ssh",
                    id,
                    _kube_secret_name("ssh", id, self.env_id),
                    _kube_secret_name("ssh", id, None),
                )
            self._warn_optional("ssh", id)
            return []

        data, meta = resolved
        token = meta.name
        payload = data.decode("private_key", token)
        target = self._stage_payload("ssh", id, payload)
        return ["--ssh", f"id={id},src={target}"]

    async def _resolve_device(self, id: SecretName, request: Device) -> list[str]:
        resolved = await self._resolve(
            kind="device",
            id=id,
        )
        if resolved is None:
            if request.required:
                raise self._missing_required(
                    "device",
                    id,
                    _kube_secret_name("device", id, self.env_id),
                    _kube_secret_name("device", id, None),
                )
            self._warn_optional("device", id)
            return []

        data, meta = resolved
        token = meta.name
        try:
            selector = data.decode(
                "selector",
                token
            ).decode("utf-8").strip()
        except UnicodeDecodeError as err:
            raise OSError(
                f"cluster secret {token!r} key 'data.selector' must decode as UTF-8 text"
            ) from err
        if not selector:
            raise OSError(f"cluster secret {token!r} key 'data.selector' cannot be empty")
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
            for id, secret in self._secrets.items():
                flags.extend(await self._resolve_secret(id, secret))
            for id, ssh in self._ssh.items():
                flags.extend(await self._resolve_ssh(id, ssh))
            for id, device in self._devices.items():
                flags.extend(await self._resolve_device(id, device))

            self._finalized = True
            return tuple(flags)
        except Exception:
            shutil.rmtree(CAPABILITY_DIR / self.run_id, ignore_errors=True)
            raise


async def get_capability(
    *,
    kind: CapabilityKind,
    id: SecretName,
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
    secret = await KubeSecret.get(expected.name, timeout=timeout)
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
    id: SecretName,
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
    # search for existing secret at indicated scope
    expected = CapabilityMetadata(kind=kind, id=id, env_id=env_id)
    existing = await KubeSecret.get(expected.name, timeout=timeout)
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
                CAPABILITY_KIND_V1: expected.kind,
                CAPABILITY_ENV_ID_V1: expected.env_id or "shared",
            },
            "annotations": {CAPABILITY_ID_V1: expected.id},
        },
        "type": "Opaque",
        "data": {
            CAPABILITY_DATA_KEY[kind]: base64.b64encode(payload).decode("ascii"),
        },
    }

    # apply updated secret manifest
    await kubectl(["apply", "-f", "-"], input=json.dumps(manifest), timeout=timeout)
    return expected


async def delete_capability(
    *,
    kind: CapabilityKind,
    id: SecretName,
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
    existing = await KubeSecret.get(expected.name, timeout=timeout)
    if existing is None:
        return False
    if expected != CapabilityMetadata.from_secret(existing):
        raise OSError(
            f"cluster secret {expected.name!r} metadata does not match requested "
            f"{expected.kind} capability {expected.id!r}"
        )

    await kubectl(
        [
            "delete",
            "secret",
            expected.name,
            "-n", BERTRAND_NAMESPACE,
            "--ignore-not-found=true",
        ],
        timeout=timeout,
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
        The environment ID to filter by.  If None, capabilities of all environment IDs
        will be returned.
    timeout : float
        The maximum time to wait for the Kubernetes Secret list, in seconds.

    Returns
    -------
    list[tuple[KubeSecret, CapabilityMetadata]]
        The metadata for all matching capabilities, sorted by kind, and name.  The
        first element of each tuple is the raw, sensitve Kubernetes Secret data, and
        the second element is the parsed metadata, which is safe to log or display.
    """
    env_id = _normalize_env_id(env_id)
    payload = (await kubectl(
        [
            "get",
            "secret",
            "-n", BERTRAND_NAMESPACE,
            "-l", f"{CAPABILITY_MANAGED_V1}=true",
            "-l", f"{CAPABILITY_KIND_V1}={kind}",
            "-l", f"{CAPABILITY_ENV_ID_V1}={env_id or 'shared'}",
            "-o", "json",
        ],
        capture_output=True,
        timeout=timeout,
    )).stdout.strip()
    if not payload:
        return []

    try:
        parsed = KubeSecret.List.model_validate_json(payload)
    except ValidationError as err:
        raise OSError("cluster returned malformed capability secret list") from err

    out = [
        (secret, CapabilityMetadata.from_secret(secret))
        for secret in parsed.items
    ]
    out.sort(key=lambda item: (item[1].kind, item[1].name))
    return out
