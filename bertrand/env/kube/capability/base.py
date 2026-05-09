"""Secret-backed capability records for Bertrand's Kubernetes runtime.

Capabilities are host-agnostic tokens that resolve to payloads supplied by a
cluster. They are stored as managed Kubernetes Secrets in the Bertrand
namespace, with a simple lookup order of environment scope, then node scope,
then shared cluster scope.
"""

from __future__ import annotations

import hashlib
from dataclasses import dataclass
from typing import TYPE_CHECKING, Literal, Self, cast

from bertrand.env.config.core import _check_kube_name, _check_uuid
from bertrand.env.kube.secret import Secret
from bertrand.env.run import BERTRAND_NAMESPACE

if TYPE_CHECKING:
    import builtins
    from collections.abc import Mapping

    from bertrand.env.config.core import KubeName
    from bertrand.env.kube.api import Kube

type CapabilityKind = Literal["secret", "ssh", "device"]
type CapabilityScope = Literal["env", "node", "shared"]

CAPABILITY_MANAGED_V1 = "bertrand.dev/capability-managed.v1"
CAPABILITY_KIND_V1 = "bertrand.dev/capability-kind.v1"
CAPABILITY_SCOPE_V1 = "bertrand.dev/capability-scope.v1"
CAPABILITY_SCOPE_VALUE_V1 = "bertrand.dev/capability-scope-value.v1"
CAPABILITY_ID_V1 = "bertrand.dev/capability-id.v1"
_CAPABILITY_KINDS = frozenset({"secret", "ssh", "device"})
_CAPABILITY_SCOPES = frozenset({"env", "node", "shared"})


@dataclass(frozen=True)
class CapabilityRef:
    """Identity for one Bertrand capability Secret.

    Parameters
    ----------
    kind : CapabilityKind
        Capability category. Ordinary build secrets, SSH credentials, and device
        selectors intentionally occupy separate identities.
    id : KubeName
        Host-agnostic capability ID from project configuration.
    scope : CapabilityScope
        Resolution scope for the capability.
    value : str | None, default=None
        Scope value for `env` and `node` capabilities. Shared capabilities must
        leave this unset.
    """

    kind: CapabilityKind
    id: KubeName
    scope: CapabilityScope
    value: str | None = None

    def __post_init__(self) -> None:
        """Validate and normalize the capability identity.

        Raises
        ------
        ValueError
            If the identity contains an invalid kind, ID, scope, or scope value.
        """
        kind = _check_kind(self.kind)
        scope = _check_scope(self.scope)
        object.__setattr__(self, "kind", kind)
        object.__setattr__(self, "id", _check_kube_name(self.id))
        object.__setattr__(self, "scope", scope)

        if scope == "shared":
            if self.value is not None:
                msg = "shared capability references cannot define a scope value"
                raise ValueError(msg)
            return

        if self.value is None:
            msg = f"{scope!r} capability references require a scope value"
            raise ValueError(msg)
        value = (
            _check_uuid(self.value) if scope == "env" else _check_kube_name(self.value)
        )
        object.__setattr__(self, "value", value)

    @classmethod
    def env(cls, kind: CapabilityKind, capability_id: KubeName, env_id: str) -> Self:
        """Create an environment-scoped capability reference.

        Parameters
        ----------
        kind : CapabilityKind
            Capability category.
        capability_id : KubeName
            Host-agnostic capability ID.
        env_id : str
            Environment UUID used as the scope value.

        Returns
        -------
        CapabilityRef
            Validated environment-scoped reference.
        """
        return cls(kind=kind, id=capability_id, scope="env", value=env_id)

    @classmethod
    def node(
        cls,
        kind: CapabilityKind,
        capability_id: KubeName,
        node: KubeName,
    ) -> Self:
        """Create a node-scoped capability reference.

        Parameters
        ----------
        kind : CapabilityKind
            Capability category.
        capability_id : KubeName
            Host-agnostic capability ID.
        node : KubeName
            Kubernetes node name that owns the host-local capability value.

        Returns
        -------
        CapabilityRef
            Validated node-scoped reference.
        """
        return cls(kind=kind, id=capability_id, scope="node", value=node)

    @classmethod
    def shared(cls, kind: CapabilityKind, capability_id: KubeName) -> Self:
        """Create a shared cluster capability reference.

        Parameters
        ----------
        kind : CapabilityKind
            Capability category.
        capability_id : KubeName
            Host-agnostic capability ID.

        Returns
        -------
        CapabilityRef
            Validated shared reference.
        """
        return cls(kind=kind, id=capability_id, scope="shared")

    @property
    def name(self) -> KubeName:
        """Return the deterministic Kubernetes Secret name for this reference.

        Returns
        -------
        KubeName
            Stable Secret name derived from kind, ID, scope, and scope value.
        """
        digest = hashlib.sha256()
        for part in (self.kind, self.id, self.scope, self.value or ""):
            encoded = part.encode("utf-8")
            digest.update(len(encoded).to_bytes(8, "big"))
            digest.update(encoded)
        return f"bertrand-capability-{digest.hexdigest()}"

    @property
    def labels(self) -> Mapping[str, str]:
        """Return Kubernetes labels for the managed capability Secret.

        Returns
        -------
        Mapping[str, str]
            Labels used for ownership checks and server-side filtering.
        """
        labels = {
            CAPABILITY_MANAGED_V1: "true",
            CAPABILITY_KIND_V1: self.kind,
            CAPABILITY_SCOPE_V1: self.scope,
        }
        if self.value is not None:
            labels[CAPABILITY_SCOPE_VALUE_V1] = self.value
        return labels

    @property
    def annotations(self) -> Mapping[str, str]:
        """Return Kubernetes annotations for the managed capability Secret.

        Returns
        -------
        Mapping[str, str]
            Annotations containing the capability ID and scope value.
        """
        annotations = {CAPABILITY_ID_V1: self.id}
        if self.value is not None:
            annotations[CAPABILITY_SCOPE_VALUE_V1] = self.value
        return annotations


@dataclass(frozen=True)
class Capability:
    """Managed Secret-backed Bertrand capability.

    Parameters
    ----------
    ref : CapabilityRef
        Expected capability identity.
    secret : Secret
        Wrapped Kubernetes Secret carrying the capability payload.
    """

    ref: CapabilityRef
    secret: Secret

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        ref: CapabilityRef,
        timeout: float,
    ) -> Self | None:
        """Read one managed capability by reference.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        ref : CapabilityRef
            Capability identity to read.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Capability | None
            Managed capability wrapper, or `None` when the Secret does not exist.
        """
        secret = await Secret.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=ref.name,
            timeout=timeout,
        )
        if secret is None:
            return None
        return cls._from_secret(secret=secret, expected=ref)

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        kind: CapabilityKind | None = None,
        scope: CapabilityScope | None = None,
        capability_id: KubeName | None = None,
        timeout: float,
    ) -> builtins.list[Self]:
        """List managed capabilities with optional identity filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        kind : CapabilityKind | None, optional
            Optional capability kind filter.
        scope : CapabilityScope | None, optional
            Optional capability scope filter.
        capability_id : KubeName | None, optional
            Optional host-agnostic capability ID filter.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        builtins.list[Capability]
            Managed capabilities matching the requested filters.
        """
        labels = {CAPABILITY_MANAGED_V1: "true"}
        if kind is not None:
            labels[CAPABILITY_KIND_V1] = _check_kind(kind)
        if scope is not None:
            labels[CAPABILITY_SCOPE_V1] = _check_scope(scope)
        expected_id = (
            _check_kube_name(capability_id) if capability_id is not None else None
        )

        secrets = await Secret.list(
            kube,
            namespaces=(BERTRAND_NAMESPACE,),
            labels=labels,
            timeout=timeout,
        )
        out: builtins.list[Self] = []
        for secret in secrets:
            capability = cls._from_secret(secret=secret)
            if expected_id is not None and capability.ref.id != expected_id:
                continue
            out.append(capability)
        return out

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        ref: CapabilityRef,
        payload: bytes,
        timeout: float,
    ) -> Self:
        """Create or patch a managed capability payload.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        ref : CapabilityRef
            Capability identity to converge.
        payload : bytes
            Raw payload bytes stored in `data.value`.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Capability
            Wrapped created or patched capability.
        """
        secret = await Secret.upsert(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=ref.name,
            labels=ref.labels,
            annotations=ref.annotations,
            payload=payload,
            timeout=timeout,
        )
        return cls._from_secret(secret=secret, expected=ref)

    @classmethod
    async def resolve(
        cls,
        kube: Kube,
        *,
        kind: CapabilityKind,
        capability_id: KubeName,
        env_id: str | None = None,
        node: KubeName | None = None,
        required: bool = True,
        timeout: float,
    ) -> Self | None:
        """Resolve a capability using env, node, then shared scope precedence.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        kind : CapabilityKind
            Capability category to resolve.
        capability_id : KubeName
            Host-agnostic capability ID.
        env_id : str | None, optional
            Optional environment UUID for the first lookup tier.
        node : KubeName | None, optional
            Optional Kubernetes node name for the second lookup tier.
        required : bool, default=True
            If true, raise when no capability is found. If false, return `None`.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Capability | None
            Resolved capability, or `None` for missing optional capabilities.

        Raises
        ------
        OSError
            If a required capability is missing.
        """
        refs: list[CapabilityRef] = []
        if env_id is not None:
            refs.append(
                CapabilityRef.env(
                    kind=kind,
                    capability_id=capability_id,
                    env_id=env_id,
                )
            )
        if node is not None:
            refs.append(
                CapabilityRef.node(
                    kind=kind,
                    capability_id=capability_id,
                    node=node,
                )
            )
        refs.append(CapabilityRef.shared(kind=kind, capability_id=capability_id))

        for ref in refs:
            capability = await cls.get(kube, ref=ref, timeout=timeout)
            if capability is not None:
                return capability

        if required:
            msg = (
                f"missing required {_kind_label(_check_kind(kind))}: {capability_id!r}"
            )
            raise OSError(msg)
        return None

    @property
    def payload(self) -> bytes:
        """Return the raw capability payload bytes.

        Returns
        -------
        bytes
            Decoded bytes stored in the underlying Secret's `data.value`.
        """
        return self.secret.value

    @property
    def text(self) -> str:
        """Return the capability payload as UTF-8 text.

        Returns
        -------
        str
            UTF-8 decoded payload text.

        Raises
        ------
        OSError
            If the payload is not valid UTF-8.
        """
        try:
            return self.payload.decode("utf-8")
        except UnicodeDecodeError as err:
            msg = (
                f"cluster capability {self.ref.id!r} payload must decode as UTF-8 text"
            )
            raise OSError(msg) from err

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this capability by reference.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Capability | None
            Latest managed capability, or `None` when it no longer exists.
        """
        return await type(self).get(kube, ref=self.ref, timeout=timeout)

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this capability from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        await self.secret.delete(kube, timeout=timeout)

    @classmethod
    def _from_secret(
        cls,
        *,
        secret: Secret,
        expected: CapabilityRef | None = None,
    ) -> Self:
        try:
            name = secret.name
        except OSError:
            if expected is None:
                raise
            name = expected.name
        labels = secret.labels
        annotations = secret.annotations

        if labels.get(CAPABILITY_MANAGED_V1) != "true":
            msg = (
                f"cluster Secret {name!r} collides with a Bertrand capability "
                "but is unmanaged"
            )
            raise OSError(msg)

        kind = _label_value(labels, CAPABILITY_KIND_V1, name)
        scope = _label_value(labels, CAPABILITY_SCOPE_V1, name)
        capability_id = annotations.get(CAPABILITY_ID_V1)
        if capability_id is None:
            msg = f"cluster Secret {name!r} is missing annotation {CAPABILITY_ID_V1!r}"
            raise OSError(msg)

        if scope == "shared":
            value = None
        else:
            value = labels.get(CAPABILITY_SCOPE_VALUE_V1) or annotations.get(
                CAPABILITY_SCOPE_VALUE_V1
            )
            if value is None:
                msg = (
                    f"cluster Secret {name!r} is missing scope value "
                    f"{CAPABILITY_SCOPE_VALUE_V1!r}"
                )
                raise OSError(msg)

        try:
            ref = CapabilityRef(
                kind=_check_kind(kind),
                id=_check_kube_name(capability_id),
                scope=_check_scope(scope),
                value=value,
            )
        except ValueError as err:
            msg = f"cluster Secret {name!r} has invalid capability metadata: {err}"
            raise OSError(msg) from err
        if expected is not None and ref != expected:
            msg = (
                f"cluster Secret {name!r} has mismatched capability identity: "
                f"expected {expected!r}, got {ref!r}"
            )
            raise OSError(msg)
        return cls(ref=ref, secret=secret)


def _check_kind(kind: str) -> CapabilityKind:
    if kind not in _CAPABILITY_KINDS:
        msg = (
            f"invalid capability kind {kind!r}; expected one of: "
            f"{', '.join(sorted(_CAPABILITY_KINDS))}"
        )
        raise ValueError(msg)
    return cast("CapabilityKind", kind)


def _check_scope(scope: str) -> CapabilityScope:
    if scope not in _CAPABILITY_SCOPES:
        msg = (
            f"invalid capability scope {scope!r}; expected one of: "
            f"{', '.join(sorted(_CAPABILITY_SCOPES))}"
        )
        raise ValueError(msg)
    return cast("CapabilityScope", scope)


def _label_value(labels: Mapping[str, str], key: str, name: str) -> str:
    value = labels.get(key)
    if value is None:
        msg = f"cluster Secret {name!r} is missing label {key!r}"
        raise OSError(msg)
    return value


def _kind_label(kind: CapabilityKind) -> str:
    if kind == "ssh":
        return "ssh credential"
    if kind == "device":
        return "device selector"
    return "secret"
