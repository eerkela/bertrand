"""Secret-backed capability records for Bertrand's Kubernetes runtime.

Capabilities are host-agnostic tokens that resolve to payloads supplied by a
cluster. They are stored as managed Kubernetes Secrets in the Bertrand
namespace, with a lookup order of worktree scope, repository scope, node scope,
then shared cluster scope.
"""

from __future__ import annotations

import hashlib
from dataclasses import dataclass
from typing import TYPE_CHECKING, Literal, cast

from bertrand.env.config.core import _check_kube_name, _check_uuid
from bertrand.env.git import BERTRAND_NAMESPACE, Deadline
from bertrand.env.kube.secret import Secret

if TYPE_CHECKING:
    from collections.abc import Mapping

    from bertrand.env.config.core import KubeName
    from bertrand.env.kube.api.client import Kube

type CapabilityKind = Literal["secret", "ssh"]
type CapabilityScope = Literal["worktree", "repository", "node", "shared"]

CAPABILITY_MANAGED_V1 = "bertrand.dev/capability-managed.v1"
CAPABILITY_KIND_V1 = "bertrand.dev/capability-kind.v1"
CAPABILITY_SCOPE_V1 = "bertrand.dev/capability-scope.v1"
CAPABILITY_SCOPE_VALUE_V1 = "bertrand.dev/capability-scope-value.v1"
CAPABILITY_ID_V1 = "bertrand.dev/capability-id.v1"
_CAPABILITY_KINDS = frozenset({"secret", "ssh"})
_CAPABILITY_SCOPES = frozenset({"worktree", "repository", "node", "shared"})


@dataclass(frozen=True)
class CapabilityRef:
    """Identity for one Bertrand capability Secret.

    Parameters
    ----------
    kind : CapabilityKind
        Capability category. Ordinary build secrets and SSH credentials
        intentionally occupy separate identities.
    capability_id : KubeName
        Host-agnostic capability ID from project configuration.
    scope : CapabilityScope
        Resolution scope for the capability.
    value : str | None, default=None
        Scope value for worktree, repository, and node capabilities. Shared
        capabilities must leave this unset.
    """

    kind: CapabilityKind
    capability_id: KubeName
    scope: CapabilityScope
    value: str | None = None

    def __post_init__(self) -> None:
        """Validate and normalize the capability identity.

        Raises
        ------
        ValueError
            If the identity contains an invalid kind, ID, scope, or scope value.
        """
        scope = _check_scope(self.scope)
        object.__setattr__(self, "kind", _check_kind(self.kind))
        object.__setattr__(
            self,
            "capability_id",
            _check_kube_name(self.capability_id),
        )
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
            _check_uuid(self.value)
            if scope in {"worktree", "repository", "node"}
            else _check_kube_name(self.value)
        )
        object.__setattr__(self, "value", value)

    @property
    def name(self) -> KubeName:
        """Return the deterministic Kubernetes Secret name for this reference.

        Returns
        -------
        KubeName
            Stable Secret name derived from kind, ID, scope, and scope value.
        """
        digest = hashlib.sha256()
        for part in (self.kind, self.capability_id, self.scope, self.value or ""):
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
        annotations = {CAPABILITY_ID_V1: self.capability_id}
        if self.value is not None:
            annotations[CAPABILITY_SCOPE_VALUE_V1] = self.value
        return annotations


def capability_ref_from_secret(
    secret: Secret,
    *,
    expected: CapabilityRef | None = None,
) -> CapabilityRef:
    """Return and validate the Bertrand capability identity for a Secret.

    Parameters
    ----------
    secret : Secret
        Kubernetes Secret to validate as a managed Bertrand capability.
    expected : CapabilityRef | None, optional
        Expected capability identity. When supplied, the Secret must match exactly.

    Returns
    -------
    CapabilityRef
        Validated capability identity read from Secret metadata.

    Raises
    ------
    OSError
        If the Secret is unmanaged, malformed, or does not match `expected`.
    """
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
            capability_id=_check_kube_name(capability_id),
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
    return ref


async def list_capability_secrets(
    kube: Kube,
    *,
    kind: CapabilityKind | None = None,
    scope: CapabilityScope | None = None,
    capability_id: KubeName | None = None,
    scope_value: str | None = None,
    deadline: Deadline,
) -> list[tuple[CapabilityRef, Secret]]:
    """List managed capability Secrets with optional identity filtering.

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
    scope_value : str | None, optional
        Optional scope value filter for worktree, repository, or node scopes.
    deadline : Deadline
        Maximum request budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    list[tuple[CapabilityRef, Secret]]
        Validated capability identities paired with their backing Secrets.

    Raises
    ------
    ValueError
        If a scope-value filter is supplied without a compatible scope.
    """
    labels = {CAPABILITY_MANAGED_V1: "true"}
    if kind is not None:
        labels[CAPABILITY_KIND_V1] = _check_kind(kind)
    if scope is not None:
        labels[CAPABILITY_SCOPE_V1] = _check_scope(scope)
    expected_scope_value: str | None = None
    if scope_value is not None:
        if scope is None:
            msg = "capability scope_value filtering requires a scope"
            raise ValueError(msg)
        if scope == "shared":
            msg = "shared capability scope cannot define a scope value"
            raise ValueError(msg)
        expected_scope_value = (
            _check_uuid(scope_value)
            if scope in {"worktree", "repository", "node"}
            else _check_kube_name(scope_value)
        )
        labels[CAPABILITY_SCOPE_VALUE_V1] = expected_scope_value
    expected_id = _check_kube_name(capability_id) if capability_id is not None else None

    secrets = await Secret.list(
        kube,
        namespaces=(BERTRAND_NAMESPACE,),
        labels=labels,
        deadline=deadline,
    )
    out: list[tuple[CapabilityRef, Secret]] = []
    for secret in secrets:
        ref = capability_ref_from_secret(secret)
        if expected_id is not None and ref.capability_id != expected_id:
            continue
        if expected_scope_value is not None and ref.value != expected_scope_value:
            continue
        out.append((ref, secret))
    return out


async def resolve_capability_secret(
    kube: Kube,
    *,
    kind: CapabilityKind,
    capability_id: KubeName,
    worktree_id: str | None = None,
    repo_id: str | None = None,
    host_id: str | None = None,
    required: bool = True,
    deadline: Deadline,
) -> Secret | None:
    """Resolve a capability Secret using Bertrand's four-tier scope precedence.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    kind : CapabilityKind
        Capability category to resolve.
    capability_id : KubeName
        Host-agnostic capability ID.
    worktree_id : str | None, optional
        Optional persistent worktree UUID for the first lookup tier.
    repo_id : str | None, optional
        Optional stable repository UUID for the second lookup tier.
    host_id : str | None, optional
        Optional Bertrand host UUID for the third lookup tier.
    required : bool, default=True
        If true, raise when no capability is found. If false, return `None`.
    deadline : Deadline
        Maximum request budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    Secret | None
        Resolved capability Secret, or `None` for missing optional capabilities.

    Raises
    ------
    OSError
        If a required capability is missing.
    """
    refs: list[CapabilityRef] = []
    if worktree_id is not None:
        refs.append(
            CapabilityRef(
                kind=kind,
                capability_id=capability_id,
                scope="worktree",
                value=worktree_id,
            )
        )
    if repo_id is not None:
        refs.append(
            CapabilityRef(
                kind=kind,
                capability_id=capability_id,
                scope="repository",
                value=repo_id,
            )
        )
    if host_id is not None:
        refs.append(
            CapabilityRef(
                kind=kind,
                capability_id=capability_id,
                scope="node",
                value=host_id,
            )
        )
    refs.append(CapabilityRef(kind=kind, capability_id=capability_id, scope="shared"))

    for ref in refs:
        secret = await Secret.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=ref.name,
            deadline=deadline,
        )
        if secret is not None:
            capability_ref_from_secret(secret, expected=ref)
            return secret

    if required:
        label = "ssh credential" if kind == "ssh" else "secret"
        msg = f"missing required {label}: {capability_id!r}"
        raise OSError(msg)
    return None


async def delete_capabilities_for_scope(
    kube: Kube,
    *,
    scope: CapabilityScope,
    scope_value: str | None,
    deadline: Deadline,
) -> None:
    """Delete managed capabilities for one exact parent scope.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    scope : CapabilityScope
        Capability scope to delete.
    scope_value : str | None
        Required scope value for worktree, repository, and node scopes. Must be
        omitted for shared scope.
    deadline : Deadline
        Maximum deletion budget.

    Raises
    ------
    ValueError
        If the scope/scope-value combination is invalid.
    """
    if scope == "shared":
        if scope_value is not None:
            msg = "shared capability cleanup cannot include a scope value"
            raise ValueError(msg)
    elif scope_value is None:
        msg = f"{scope!r} capability cleanup requires a scope value"
        raise ValueError(msg)
    capabilities = await list_capability_secrets(
        kube,
        scope=scope,
        scope_value=scope_value,
        deadline=deadline,
    )
    for ref, secret in capabilities:
        capability_ref_from_secret(secret, expected=ref)
        await secret.delete(
            kube,
            namespace=secret.namespace,
            name=secret.name,
            deadline=deadline,
        )


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
