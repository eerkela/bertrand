"""External CLI helpers for scoped Bertrand Secret capabilities."""

from __future__ import annotations

import asyncio
import json
import sys
from pathlib import Path
from typing import TYPE_CHECKING

from bertrand.env.cli.external._helper import (
    prune_repository_mounts_quietly,
    resolve_project_scope,
)
from bertrand.env.git import BERTRAND_NAMESPACE, Deadline
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.capability.base import (
    CapabilityKind,
    CapabilityRef,
    CapabilityScope,
    capability_ref_from_secret,
    list_capability_secrets,
)
from bertrand.env.kube.node_identity import ensure_local_bertrand_node
from bertrand.env.kube.secret import SECRET_RESOURCE, Secret

if TYPE_CHECKING:
    from bertrand.env.config.core import KubeName


async def bertrand_secret_add(
    *,
    path: str,
    kind: CapabilityKind,
    capability_id: KubeName,
    source: str | None,
    deadline: Deadline,
) -> None:
    """Create or update a path-scoped secret capability."""
    with Kube.external() as kube:
        ref = await _path_capability_ref(
            kube,
            path,
            kind=kind,
            capability_id=capability_id,
            deadline=deadline,
        )
        await prune_repository_mounts_quietly(kube, deadline=deadline)
        await _add_capability(
            kube,
            ref,
            source=source,
            deadline=deadline,
        )


async def bertrand_secret_rm(
    *,
    path: str,
    kind: CapabilityKind,
    capability_id: KubeName,
    deadline: Deadline,
) -> None:
    """Remove a path-scoped secret capability."""
    with Kube.external() as kube:
        ref = await _path_capability_ref(
            kube,
            path,
            kind=kind,
            capability_id=capability_id,
            deadline=deadline,
        )
        await prune_repository_mounts_quietly(kube, deadline=deadline)
        await _remove_capability(kube, ref, deadline=deadline)


async def bertrand_secret_list(
    *,
    path: str,
    kind: CapabilityKind | None,
    json_output: bool,
    deadline: Deadline,
) -> None:
    """List path-relevant secret capabilities."""
    with Kube.external() as kube:
        repo, worktree = await resolve_project_scope(
            kube,
            Path(path),
            deadline=deadline,
        )
        local_node = await ensure_local_bertrand_node(
            kube,
            deadline=deadline,
        )
        targets: list[tuple[CapabilityScope, str | None]] = []
        if worktree is not None:
            targets.append(("worktree", worktree.id))
        targets.append(("repository", repo.id))
        targets.append(("node", local_node.host_id))
        targets.append(("shared", None))
        await prune_repository_mounts_quietly(kube, deadline=deadline)
        await _list_capabilities(
            kube,
            tuple(targets),
            kind=kind,
            json_output=json_output,
            deadline=deadline,
        )


async def bertrand_shared_secret_add(
    *,
    kind: CapabilityKind,
    capability_id: KubeName,
    source: str | None,
    deadline: Deadline,
) -> None:
    """Create or update a shared cluster secret capability."""
    with Kube.external() as kube:
        ref = CapabilityRef(kind=kind, capability_id=capability_id, scope="shared")
        await _add_capability(
            kube,
            ref,
            source=source,
            deadline=deadline,
        )


async def bertrand_shared_secret_rm(
    *,
    kind: CapabilityKind,
    capability_id: KubeName,
    deadline: Deadline,
) -> None:
    """Remove a shared cluster secret capability."""
    with Kube.external() as kube:
        ref = CapabilityRef(kind=kind, capability_id=capability_id, scope="shared")
        await _remove_capability(kube, ref, deadline=deadline)


async def bertrand_shared_secret_list(
    *,
    kind: CapabilityKind | None,
    json_output: bool,
    deadline: Deadline,
) -> None:
    """List shared cluster secret capabilities."""
    with Kube.external() as kube:
        await _list_capabilities(
            kube,
            (("shared", None),),
            kind=kind,
            json_output=json_output,
            deadline=deadline,
        )


async def bertrand_node_secret_add(
    *,
    kind: CapabilityKind,
    capability_id: KubeName,
    source: str | None,
    deadline: Deadline,
) -> None:
    """Create or update a local-node secret capability."""
    with Kube.external() as kube:
        node = await ensure_local_bertrand_node(
            kube,
            deadline=deadline,
        )
        ref = CapabilityRef(
            kind=kind,
            capability_id=capability_id,
            scope="node",
            value=node.host_id,
        )
        await _add_capability(
            kube,
            ref,
            source=source,
            deadline=deadline,
        )


async def bertrand_node_secret_rm(
    *,
    kind: CapabilityKind,
    capability_id: KubeName,
    deadline: Deadline,
) -> None:
    """Remove a local-node secret capability."""
    with Kube.external() as kube:
        node = await ensure_local_bertrand_node(
            kube,
            deadline=deadline,
        )
        ref = CapabilityRef(
            kind=kind,
            capability_id=capability_id,
            scope="node",
            value=node.host_id,
        )
        await _remove_capability(kube, ref, deadline=deadline)


async def bertrand_node_secret_list(
    *,
    kind: CapabilityKind | None,
    json_output: bool,
    deadline: Deadline,
) -> None:
    """List local-node secret capabilities and shared fallbacks."""
    with Kube.external() as kube:
        node = await ensure_local_bertrand_node(
            kube,
            deadline=deadline,
        )
        await _list_capabilities(
            kube,
            (("node", node.host_id), ("shared", None)),
            kind=kind,
            json_output=json_output,
            deadline=deadline,
        )


async def _add_capability(
    kube: Kube,
    ref: CapabilityRef,
    *,
    source: str | None,
    deadline: Deadline,
) -> None:
    payload = _read_payload(source)
    secret = await Secret.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=ref.name,
        labels=ref.labels,
        annotations=ref.annotations,
        payload=payload,
        deadline=deadline,
    )
    capability_ref_from_secret(secret, expected=ref)
    print(_format_capability_line(ref, secret))


async def _remove_capability(
    kube: Kube, ref: CapabilityRef, *, deadline: Deadline
) -> None:
    secret = await SECRET_RESOURCE.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=ref.name,
        deadline=deadline,
    )
    if secret is None:
        print(f"{ref.kind} {ref.capability_id}: not found")
        return
    capability_ref_from_secret(secret, expected=ref)
    await SECRET_RESOURCE.delete(kube, secret, deadline=deadline)
    print(f"{ref.kind} {ref.capability_id}: deleted")


async def _list_capabilities(
    kube: Kube,
    targets: tuple[tuple[CapabilityScope, str | None], ...],
    *,
    kind: CapabilityKind | None,
    json_output: bool,
    deadline: Deadline,
) -> None:
    groups = await asyncio.gather(
        *(
            list_capability_secrets(
                kube,
                kind=kind,
                scope=target[0],
                scope_value=target[1],
                deadline=deadline,
            )
            for target in targets
        )
    )
    capabilities = [capability for group in groups for capability in group]
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
    deadline: Deadline,
) -> CapabilityRef:
    repo, worktree = await resolve_project_scope(
        kube,
        Path(path),
        deadline=deadline,
    )
    if worktree is None:
        return CapabilityRef(
            kind=kind,
            capability_id=capability_id,
            scope="repository",
            value=repo.id,
        )
    return CapabilityRef(
        kind=kind,
        capability_id=capability_id,
        scope="worktree",
        value=worktree.id,
    )


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
