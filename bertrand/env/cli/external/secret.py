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
from bertrand.env.git import BERTRAND_NAMESPACE, Deadline, ensure_worktree_id
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
    deadline = Deadline.from_timeout(
        timeout,
        message="path secret capability add timeout must be positive",
    )
    with await Kube.host(timeout=deadline.remaining()) as kube:
        ref = await _path_capability_ref(
            kube,
            path,
            kind=kind,
            capability_id=capability_id,
            timeout=deadline.remaining(),
        )
        await prune_repository_mounts_quietly(kube, timeout=deadline.remaining())
        await _add_capability(
            kube,
            ref,
            source=source,
            timeout=deadline.remaining(),
        )


async def bertrand_secret_rm(
    *,
    path: str,
    kind: CapabilityKind,
    capability_id: KubeName,
    timeout: float,
) -> None:
    """Remove a path-scoped secret capability."""
    deadline = Deadline.from_timeout(
        timeout,
        message="path secret capability removal timeout must be positive",
    )
    with await Kube.host(timeout=deadline.remaining()) as kube:
        ref = await _path_capability_ref(
            kube,
            path,
            kind=kind,
            capability_id=capability_id,
            timeout=deadline.remaining(),
        )
        await prune_repository_mounts_quietly(kube, timeout=deadline.remaining())
        await _remove_capability(kube, ref, timeout=deadline.remaining())


async def bertrand_secret_list(
    *,
    path: str,
    kind: CapabilityKind | None,
    json_output: bool,
    timeout: float,
) -> None:
    """List path-relevant secret capabilities.

    Raises
    ------
    OSError
        If the target path is not inside an initialized Git repository.
    """
    deadline = Deadline.from_timeout(
        timeout,
        message="path secret capability list timeout must be positive",
    )
    with await Kube.host(timeout=deadline.remaining()) as kube:
        repo, worktree = await resolve_project_scope(
            kube,
            Path(path),
            timeout=deadline.remaining(),
        )
        local_node = await ensure_local_bertrand_node(
            kube,
            timeout=deadline.remaining(),
        )
        if not repo:
            msg = f"no initialized Git repository found for target: {path}"
            raise OSError(msg)
        targets: list[tuple[CapabilityScope, str | None]] = []
        if worktree != Path():
            targets.append(("worktree", ensure_worktree_id(repo.root / worktree)))
        targets.append(("repository", repo.repo_id))
        targets.append(("node", local_node.host_id))
        targets.append(("shared", None))
        await prune_repository_mounts_quietly(kube, timeout=deadline.remaining())
        await _list_capabilities(
            kube,
            tuple(targets),
            kind=kind,
            json_output=json_output,
            timeout=deadline.remaining(),
        )


async def bertrand_shared_secret_add(
    *,
    kind: CapabilityKind,
    capability_id: KubeName,
    source: str | None,
    timeout: float,
) -> None:
    """Create or update a shared cluster secret capability."""
    deadline = Deadline.from_timeout(
        timeout,
        message="shared secret capability add timeout must be positive",
    )
    with await Kube.host(timeout=deadline.remaining()) as kube:
        ref = CapabilityRef(kind=kind, capability_id=capability_id, scope="shared")
        await _add_capability(
            kube,
            ref,
            source=source,
            timeout=deadline.remaining(),
        )


async def bertrand_shared_secret_rm(
    *,
    kind: CapabilityKind,
    capability_id: KubeName,
    timeout: float,
) -> None:
    """Remove a shared cluster secret capability."""
    deadline = Deadline.from_timeout(
        timeout,
        message="shared secret capability removal timeout must be positive",
    )
    with await Kube.host(timeout=deadline.remaining()) as kube:
        ref = CapabilityRef(kind=kind, capability_id=capability_id, scope="shared")
        await _remove_capability(kube, ref, timeout=deadline.remaining())


async def bertrand_shared_secret_list(
    *,
    kind: CapabilityKind | None,
    json_output: bool,
    timeout: float,
) -> None:
    """List shared cluster secret capabilities."""
    deadline = Deadline.from_timeout(
        timeout,
        message="shared secret capability list timeout must be positive",
    )
    with await Kube.host(timeout=deadline.remaining()) as kube:
        await _list_capabilities(
            kube,
            (("shared", None),),
            kind=kind,
            json_output=json_output,
            timeout=deadline.remaining(),
        )


async def bertrand_node_secret_add(
    *,
    kind: CapabilityKind,
    capability_id: KubeName,
    source: str | None,
    timeout: float,
) -> None:
    """Create or update a local-node secret capability."""
    deadline = Deadline.from_timeout(
        timeout,
        message="node secret capability add timeout must be positive",
    )
    with await Kube.host(timeout=deadline.remaining()) as kube:
        node = await ensure_local_bertrand_node(
            kube,
            timeout=deadline.remaining(),
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
            timeout=deadline.remaining(),
        )


async def bertrand_node_secret_rm(
    *,
    kind: CapabilityKind,
    capability_id: KubeName,
    timeout: float,
) -> None:
    """Remove a local-node secret capability."""
    deadline = Deadline.from_timeout(
        timeout,
        message="node secret capability removal timeout must be positive",
    )
    with await Kube.host(timeout=deadline.remaining()) as kube:
        node = await ensure_local_bertrand_node(
            kube,
            timeout=deadline.remaining(),
        )
        ref = CapabilityRef(
            kind=kind,
            capability_id=capability_id,
            scope="node",
            value=node.host_id,
        )
        await _remove_capability(kube, ref, timeout=deadline.remaining())


async def bertrand_node_secret_list(
    *,
    kind: CapabilityKind | None,
    json_output: bool,
    timeout: float,
) -> None:
    """List local-node secret capabilities and shared fallbacks."""
    deadline = Deadline.from_timeout(
        timeout,
        message="node secret capability list timeout must be positive",
    )
    with await Kube.host(timeout=deadline.remaining()) as kube:
        node = await ensure_local_bertrand_node(
            kube,
            timeout=deadline.remaining(),
        )
        await _list_capabilities(
            kube,
            (("node", node.host_id), ("shared", None)),
            kind=kind,
            json_output=json_output,
            timeout=deadline.remaining(),
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
    deadline = Deadline.from_timeout(
        timeout,
        message="capability Secret list timeout must be positive",
    )
    groups = await asyncio.gather(
        *(
            list_capability_secrets(
                kube,
                kind=kind,
                scope=target[0],
                scope_value=target[1],
                timeout=deadline.remaining(),
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
        return CapabilityRef(
            kind=kind,
            capability_id=capability_id,
            scope="repository",
            value=repo.repo_id,
        )
    worktree_id = ensure_worktree_id(repo.root / worktree)
    return CapabilityRef(
        kind=kind,
        capability_id=capability_id,
        scope="worktree",
        value=worktree_id,
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
