"""External CLI helpers for scoped Bertrand Secret capabilities."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import TYPE_CHECKING

from bertrand.env.cli.external._helper import (
    prune_repository_mounts_quietly,
    resolve_project_scope,
)
from bertrand.env.git import BERTRAND_NAMESPACE, ensure_worktree_id
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.capability.base import (
    CapabilityKind,
    CapabilityRef,
    CapabilityScope,
    capability_ref_from_secret,
    list_capability_secrets,
)
from bertrand.env.kube.node_identity import ensure_local_bertrand_node
from bertrand.env.kube.secret import Secret

if TYPE_CHECKING:
    from bertrand.env.config.core import KubeName


async def bertrand_secret_add(
    *,
    path: str,
    kind: CapabilityKind,
    capability_id: KubeName,
    source: str | None,
    timeout: float,
) -> None:
    """Create or update a path-scoped secret capability."""
    with await Kube.host(timeout=timeout) as kube:
        ref = await _path_capability_ref(
            kube,
            path,
            kind=kind,
            capability_id=capability_id,
            timeout=timeout,
        )
        await prune_repository_mounts_quietly(kube, timeout=timeout)
        await _add_capability(kube, ref, source=source, timeout=timeout)


async def bertrand_secret_rm(
    *,
    path: str,
    kind: CapabilityKind,
    capability_id: KubeName,
    timeout: float,
) -> None:
    """Remove a path-scoped secret capability."""
    with await Kube.host(timeout=timeout) as kube:
        ref = await _path_capability_ref(
            kube,
            path,
            kind=kind,
            capability_id=capability_id,
            timeout=timeout,
        )
        await prune_repository_mounts_quietly(kube, timeout=timeout)
        await _remove_capability(kube, ref, timeout=timeout)


async def bertrand_secret_list(
    *,
    path: str,
    kind: CapabilityKind | None,
    json_output: bool,
    timeout: float,
) -> None:
    """List path-relevant secret capabilities."""
    with await Kube.host(timeout=timeout) as kube:
        targets = await _path_scope_targets(
            kube,
            path,
            include_local_node=True,
            timeout=timeout,
        )
        await prune_repository_mounts_quietly(kube, timeout=timeout)
        await _list_capabilities(
            kube,
            targets,
            kind=kind,
            json_output=json_output,
            timeout=timeout,
        )


async def bertrand_shared_secret_add(
    *,
    kind: CapabilityKind,
    capability_id: KubeName,
    source: str | None,
    timeout: float,
) -> None:
    """Create or update a shared cluster secret capability."""
    with await Kube.host(timeout=timeout) as kube:
        ref = _shared_capability_ref(kind=kind, capability_id=capability_id)
        await _add_capability(kube, ref, source=source, timeout=timeout)


async def bertrand_shared_secret_rm(
    *,
    kind: CapabilityKind,
    capability_id: KubeName,
    timeout: float,
) -> None:
    """Remove a shared cluster secret capability."""
    with await Kube.host(timeout=timeout) as kube:
        ref = _shared_capability_ref(kind=kind, capability_id=capability_id)
        await _remove_capability(kube, ref, timeout=timeout)


async def bertrand_shared_secret_list(
    *,
    kind: CapabilityKind | None,
    json_output: bool,
    timeout: float,
) -> None:
    """List shared cluster secret capabilities."""
    with await Kube.host(timeout=timeout) as kube:
        await _list_capabilities(
            kube,
            _shared_scope_targets(),
            kind=kind,
            json_output=json_output,
            timeout=timeout,
        )


async def bertrand_node_secret_add(
    *,
    kind: CapabilityKind,
    capability_id: KubeName,
    source: str | None,
    timeout: float,
) -> None:
    """Create or update a local-node secret capability."""
    with await Kube.host(timeout=timeout) as kube:
        ref = await _local_node_capability_ref(
            kube,
            kind=kind,
            capability_id=capability_id,
            timeout=timeout,
        )
        await _add_capability(kube, ref, source=source, timeout=timeout)


async def bertrand_node_secret_rm(
    *,
    kind: CapabilityKind,
    capability_id: KubeName,
    timeout: float,
) -> None:
    """Remove a local-node secret capability."""
    with await Kube.host(timeout=timeout) as kube:
        ref = await _local_node_capability_ref(
            kube,
            kind=kind,
            capability_id=capability_id,
            timeout=timeout,
        )
        await _remove_capability(kube, ref, timeout=timeout)


async def bertrand_node_secret_list(
    *,
    kind: CapabilityKind | None,
    json_output: bool,
    timeout: float,
) -> None:
    """List local-node secret capabilities and shared fallbacks."""
    with await Kube.host(timeout=timeout) as kube:
        await _list_capabilities(
            kube,
            await _local_node_scope_targets(kube, timeout=timeout),
            kind=kind,
            json_output=json_output,
            timeout=timeout,
        )


async def _add_capability(
    kube: Kube,
    ref: CapabilityRef,
    *,
    source: str | None,
    timeout: float,
) -> None:
    payload = _read_payload(source)
    secret = await Secret.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=ref.name,
        labels=ref.labels,
        annotations=ref.annotations,
        payload=payload,
        timeout=timeout,
    )
    capability_ref_from_secret(secret, expected=ref)
    print(_format_capability_line(ref, secret))


async def _remove_capability(kube: Kube, ref: CapabilityRef, *, timeout: float) -> None:
    secret = await Secret.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=ref.name,
        timeout=timeout,
    )
    if secret is None:
        print(f"{ref.kind} {ref.capability_id}: not found")
        return
    capability_ref_from_secret(secret, expected=ref)
    await secret.delete(kube, timeout=timeout)
    print(f"{ref.kind} {ref.capability_id}: deleted")


async def _list_capabilities(
    kube: Kube,
    targets: tuple[tuple[CapabilityScope, str | None], ...],
    *,
    kind: CapabilityKind | None,
    json_output: bool,
    timeout: float,
) -> None:
    capabilities: list[tuple[CapabilityRef, Secret]] = []
    for target in targets:
        capabilities.extend(
            await list_capability_secrets(
                kube,
                kind=kind,
                scope=target[0],
                scope_value=target[1],
                timeout=timeout,
            )
        )
    scope_order = {target[0]: index for index, target in enumerate(targets)}
    capabilities.sort(
        key=lambda item: (
            scope_order.get(item[0].scope, len(scope_order)),
            item[0].capability_id,
            item[0].kind,
            item[0].value or "",
        )
    )
    if json_output:
        print(
            json.dumps(
                [_capability_payload(item) for item in capabilities],
                indent=2,
                sort_keys=True,
            )
        )
        return
    if not capabilities:
        print("no capabilities")
        return
    for ref, secret in capabilities:
        print(_format_capability_line(ref, secret))


async def _path_capability_ref(
    kube: Kube,
    path: str,
    *,
    kind: CapabilityKind,
    capability_id: KubeName,
    timeout: float,
) -> CapabilityRef:
    repo, worktree = await resolve_project_scope(
        kube,
        Path(path),
        timeout=timeout,
    )
    if not repo:
        msg = f"no initialized Git repository found for target: {path}"
        raise OSError(msg)
    if worktree == Path():
        return CapabilityRef.repository(kind, capability_id, repo.repo_id)
    worktree_id = ensure_worktree_id(repo.root / worktree)
    return CapabilityRef.worktree(kind, capability_id, worktree_id)


async def _path_scope_targets(
    kube: Kube,
    path: str,
    *,
    include_local_node: bool,
    timeout: float,
) -> tuple[tuple[CapabilityScope, str | None], ...]:
    repo, worktree = await resolve_project_scope(
        kube,
        Path(path),
        timeout=timeout,
    )
    local_node = (
        await ensure_local_bertrand_node(kube, timeout=timeout)
        if include_local_node
        else None
    )
    if not repo:
        msg = f"no initialized Git repository found for target: {path}"
        raise OSError(msg)
    targets: list[tuple[CapabilityScope, str | None]] = []
    if worktree != Path():
        targets.append(
            (
                "worktree",
                ensure_worktree_id(repo.root / worktree),
            )
        )
    targets.append(("repository", repo.repo_id))
    if local_node is not None:
        targets.append(("node", local_node.host_id))
    targets.append(("shared", None))
    return tuple(targets)


def _shared_scope_targets() -> tuple[tuple[CapabilityScope, str | None], ...]:
    return (("shared", None),)


async def _local_node_scope_targets(
    kube: Kube,
    *,
    timeout: float,
) -> tuple[tuple[CapabilityScope, str | None], ...]:
    node = await ensure_local_bertrand_node(kube, timeout=timeout)
    return (
        ("node", node.host_id),
        ("shared", None),
    )


async def _local_node_capability_ref(
    kube: Kube,
    *,
    kind: CapabilityKind,
    capability_id: KubeName,
    timeout: float,
) -> CapabilityRef:
    node = await ensure_local_bertrand_node(kube, timeout=timeout)
    return CapabilityRef.node(kind, capability_id, node.host_id)


def _shared_capability_ref(
    *,
    kind: CapabilityKind,
    capability_id: KubeName,
) -> CapabilityRef:
    return CapabilityRef.shared(kind, capability_id)


def _read_payload(source: str | None) -> bytes:
    if source is None:
        if sys.stdin.isatty():
            msg = "secret SOURCE is required unless stdin is piped"
            raise OSError(msg)
        return sys.stdin.buffer.read()
    if source == "-":
        return sys.stdin.buffer.read()
    return Path(source).expanduser().read_bytes()


def _capability_payload(item: tuple[CapabilityRef, Secret]) -> dict[str, str | None]:
    ref, secret = item
    return {
        "kind": ref.kind,
        "id": ref.capability_id,
        "scope": ref.scope,
        "scope_value": ref.value,
        "secret": secret.name,
    }


def _format_capability_line(ref: CapabilityRef, secret: Secret) -> str:
    scope = ref.scope if ref.value is None else f"{ref.scope}:{ref.value}"
    return f"{ref.kind} {ref.capability_id} [{scope}] -> {secret.name}"
