"""Host-agnostic capability resolution and staging via Kubernetes Secrets."""
from __future__ import annotations

import base64
import binascii
import hashlib
import shutil
import sys
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Annotated, Literal, Self

from pydantic import BaseModel, ConfigDict, Field, StrictStr, ValidationError

from ..config.core import KUBE_SECRET_RE, SecretName
from ..run import BERTRAND_NAMESPACE, TIMEOUT, TOOLS_TMP_DIR, atomic_write_bytes, kubectl

DEVICE_PERMISSIONS = frozenset({"r", "w", "m", "rw", "rm", "wm", "rwm"})
CAPABILITY_DIR = TOOLS_TMP_DIR / "capabilities"


class KubeSecret(BaseModel):
    """Validated subset of a Kubernetes Secret payload."""
    model_config = ConfigDict(extra="ignore")
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
                "-n",
                BERTRAND_NAMESPACE,
                "-o",
                "json",
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
    timeout: float = field(default=TIMEOUT)
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

    @staticmethod
    def _normalize_id(value: str) -> str:
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
        id = self._normalize_id(id)
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
        id = self._normalize_id(id)
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
        id = self._normalize_id(id)
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
        token: str
    ) -> OSError:
        return OSError(
            f"missing required build {kind} capability {id!r} (cluster secret "
            f"{token!r} not found in namespace {BERTRAND_NAMESPACE!r})"
        )

    @staticmethod
    def _warn_optional(kind: str, id: SecretName) -> None:
        print(
            f"bertrand: optional {kind} capability {id!r} was not found; "
            "continuing without it",
            file=sys.stderr,
        )

    def _kube_name(
        self,
        kind: Literal["secret", "ssh", "device"],
        id: SecretName,
    ) -> str:
        h = hashlib.sha256()
        for part in (self.env_id, kind, id):
            encoded = part.encode("utf-8")
            h.update(len(encoded).to_bytes(8, "big"))
            h.update(encoded)
        return f"bertrand-{kind}-{h.hexdigest()}"

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

    async def _resolve_secret(self, id: SecretName, request: Secret) -> list[str]:
        token = self._kube_name("secret", id)
        data = await KubeSecret.get(token, timeout=self.timeout)
        if data is None:
            if request.required:
                raise self._missing_required("secret", id, token)
            self._warn_optional("secret", id)
            return []

        payload = data.decode("value", token)
        target = self._stage_payload("secrets", id, payload)
        return ["--secret", f"id={id},src={target}"]

    async def _resolve_ssh(self, id: SecretName, request: SSH) -> list[str]:
        token = self._kube_name("ssh", id)
        data = await KubeSecret.get(token, timeout=self.timeout)
        if data is None:
            if request.required:
                raise self._missing_required("ssh", id, token)
            self._warn_optional("ssh", id)
            return []

        payload = data.decode("private_key", token)
        target = self._stage_payload("ssh", id, payload)
        return ["--ssh", f"id={id},src={target}"]

    async def _resolve_device(self, id: SecretName, request: Device) -> list[str]:
        token = self._kube_name("device", id)
        data = await KubeSecret.get(token, timeout=self.timeout)
        if data is None:
            if request.required:
                raise self._missing_required("device", id, token)
            self._warn_optional("device", id)
            return []

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
