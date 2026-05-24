"""External CLI helpers for scoped Bertrand Secret capabilities."""

from __future__ import annotations

import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING

from bertrand.env.cli.external._helper import (
    prune_repository_mounts_quietly,
    resolve_project_scope,
)
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.build.lifecycle import worktree_identity
from bertrand.env.kube.build.project import project_worktree_id
from bertrand.env.kube.capability.base import (
    Capability,
    CapabilityKind,
    CapabilityRef,
    CapabilityScope,
)
from bertrand.env.kube.node_identity import ensure_local_bertrand_node

if TYPE_CHECKING:
    import argparse

    from bertrand.env.config.core import KubeName


@dataclass(frozen=True)
class _ScopeTarget:
    scope: CapabilityScope
    value: str | None
    label: str


async def bertrand_secret(args: argparse.Namespace) -> None:
    """Execute a top-level path-scoped ``bertrand secret`` subcommand.

    Parameters
    ----------
    args : argparse.Namespace
        Parsed external CLI arguments.

    Raises
    ------
    ValueError
        If the parsed secret subcommand is unsupported.
    """
    command = args.secret_command
    if command == "add":
        ref = await path_capability_ref(
            args.path,
            kind=args.kind,
            capability_id=args.id,
            timeout=args.timeout,
        )
        await add_capability(ref, source=args.source, timeout=args.timeout)
        return
    if command == "rm":
        ref = await path_capability_ref(
            args.path,
            kind=args.kind,
            capability_id=args.id,
            timeout=args.timeout,
        )
        await remove_capability(ref, timeout=args.timeout)
        return
    if command == "list":
        targets = await path_scope_targets(
            args.path,
            include_local_node=True,
            timeout=args.timeout,
        )
        await list_capabilities(
            targets,
            kind=args.kind,
            json_output=args.json,
            timeout=args.timeout,
        )
        return
    msg = f"unsupported secret command: {command!r}"
    raise ValueError(msg)


async def add_capability(
    ref: CapabilityRef,
    *,
    source: str | None,
    timeout: float,
) -> None:
    """Create or patch one scoped capability payload.

    Parameters
    ----------
    ref : CapabilityRef
        Capability identity to create or update.
    source : str | None
        File path, ``-`` for stdin, or ``None`` to read piped stdin.
    timeout : float
        Maximum Kubernetes request budget in seconds.
    """
    payload = _read_payload(source)
    with await Kube.host(timeout=timeout) as kube:
        capability = await Capability.upsert(
            kube,
            ref=ref,
            payload=payload,
            timeout=timeout,
        )
    print(_format_capability_line(capability))


async def remove_capability(ref: CapabilityRef, *, timeout: float) -> None:
    """Delete one scoped capability if it exists.

    Parameters
    ----------
    ref : CapabilityRef
        Capability identity to delete.
    timeout : float
        Maximum Kubernetes request budget in seconds.
    """
    with await Kube.host(timeout=timeout) as kube:
        capability = await Capability.get(kube, ref=ref, timeout=timeout)
        if capability is None:
            print(f"{ref.kind} {ref.capability_id}: not found")
            return
        await capability.delete(kube, timeout=timeout)
    print(f"{ref.kind} {ref.capability_id}: deleted")


async def list_capabilities(
    targets: tuple[_ScopeTarget, ...],
    *,
    kind: CapabilityKind | None,
    json_output: bool,
    timeout: float,
) -> None:
    """List capability metadata for a set of scope targets.

    Parameters
    ----------
    targets : tuple[_ScopeTarget, ...]
        Ordered scopes to list.
    kind : CapabilityKind | None
        Optional capability kind filter.
    json_output : bool
        Whether to emit machine-readable JSON.
    timeout : float
        Maximum Kubernetes request budget in seconds.
    """
    with await Kube.host(timeout=timeout) as kube:
        capabilities: list[Capability] = []
        for target in targets:
            capabilities.extend(
                await Capability.list(
                    kube,
                    kind=kind,
                    scope=target.scope,
                    scope_value=target.value,
                    timeout=timeout,
                )
            )
    scope_order = {target.scope: index for index, target in enumerate(targets)}
    capabilities.sort(
        key=lambda item: (
            scope_order.get(item.ref.scope, len(scope_order)),
            item.ref.capability_id,
            item.ref.kind,
            item.ref.value or "",
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
    for capability in capabilities:
        print(_format_capability_line(capability))


async def path_capability_ref(
    path: str,
    *,
    kind: CapabilityKind,
    capability_id: KubeName,
    timeout: float,
) -> CapabilityRef:
    """Return the exact repository/worktree-scoped ref for a path target.

    Parameters
    ----------
    path : str
        Repository root or explicit worktree path. Repository-root targets do not
        substitute HEAD.
    kind : CapabilityKind
        Capability category.
    capability_id : KubeName
        Host-agnostic capability ID.
    timeout : float
        Maximum Kubernetes request budget in seconds.

    Returns
    -------
    CapabilityRef
        Repository or worktree scoped reference.

    Raises
    ------
    OSError
        If no initialized repository is found for `path`.
    """
    with await Kube.host(timeout=timeout) as kube:
        repo, worktree = await resolve_project_scope(
            kube,
            Path(path),
            timeout=timeout,
        )
        await prune_repository_mounts_quietly(kube, timeout=timeout)
    if not repo:
        msg = f"no initialized Git repository found for target: {path}"
        raise OSError(msg)
    if worktree == Path():
        return CapabilityRef.repository(kind, capability_id, repo.repo_id)
    worktree_id = project_worktree_id(repo.root / worktree)
    return CapabilityRef.worktree(kind, capability_id, worktree_id)


async def path_scope_targets(
    path: str,
    *,
    include_local_node: bool,
    timeout: float,
) -> tuple[_ScopeTarget, ...]:
    """Return resolution-ordered scope targets for a path.

    Parameters
    ----------
    path : str
        Repository root or explicit worktree path.
    include_local_node : bool
        Whether to include the local node scope before shared cluster scope.
    timeout : float
        Maximum Kubernetes request budget in seconds when local node discovery is
        needed.

    Returns
    -------
    tuple[_ScopeTarget, ...]
        Ordered scope targets matching capability resolution precedence.

    Raises
    ------
    OSError
        If no initialized repository is found for `path`.
    """
    with await Kube.host(timeout=timeout) as kube:
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
        await prune_repository_mounts_quietly(kube, timeout=timeout)
    if not repo:
        msg = f"no initialized Git repository found for target: {path}"
        raise OSError(msg)
    targets: list[_ScopeTarget] = []
    if worktree != Path():
        targets.append(
            _ScopeTarget(
                "worktree",
                project_worktree_id(repo.root / worktree),
                f"worktree {worktree_identity(worktree)}",
            )
        )
    targets.append(_ScopeTarget("repository", repo.repo_id, f"repository {repo.root}"))
    if local_node is not None:
        label = local_node.display_name or local_node.host_id
        targets.append(_ScopeTarget("node", local_node.host_id, f"node {label}"))
    targets.append(_ScopeTarget("shared", None, "shared cluster"))
    return tuple(targets)


def shared_scope_targets() -> tuple[_ScopeTarget, ...]:
    """Return the shared cluster capability target.

    Returns
    -------
    tuple[_ScopeTarget, ...]
        Single shared-cluster scope target.
    """
    return (_ScopeTarget("shared", None, "shared cluster"),)


async def local_node_scope_targets(*, timeout: float) -> tuple[_ScopeTarget, ...]:
    """Return local node and shared capability targets.

    Returns
    -------
    tuple[_ScopeTarget, ...]
        Ordered local-node then shared-cluster scope targets.
    """
    with await Kube.host(timeout=timeout) as kube:
        node = await ensure_local_bertrand_node(kube, timeout=timeout)
    label = node.display_name or node.host_id
    return (
        _ScopeTarget("node", node.host_id, f"node {label}"),
        _ScopeTarget("shared", None, "shared cluster"),
    )


async def local_node_capability_ref(
    *,
    kind: CapabilityKind,
    capability_id: KubeName,
    timeout: float,
) -> CapabilityRef:
    """Return the local node-scoped capability reference.

    Parameters
    ----------
    kind : CapabilityKind
        Capability category.
    capability_id : KubeName
        Host-agnostic capability ID.
    timeout : float
        Maximum Kubernetes request budget in seconds.

    Returns
    -------
    CapabilityRef
        Local node-scoped reference.
    """
    with await Kube.host(timeout=timeout) as kube:
        node = await ensure_local_bertrand_node(kube, timeout=timeout)
    return CapabilityRef.node(kind, capability_id, node.host_id)


def shared_capability_ref(
    *,
    kind: CapabilityKind,
    capability_id: KubeName,
) -> CapabilityRef:
    """Return a shared cluster capability reference.

    Parameters
    ----------
    kind : CapabilityKind
        Capability category.
    capability_id : KubeName
        Host-agnostic capability ID.

    Returns
    -------
    CapabilityRef
        Shared cluster-scoped reference.
    """
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


def _capability_payload(capability: Capability) -> dict[str, str | None]:
    ref = capability.ref
    return {
        "kind": ref.kind,
        "id": ref.capability_id,
        "scope": ref.scope,
        "scope_value": ref.value,
        "secret": capability.secret.name,
    }


def _format_capability_line(capability: Capability) -> str:
    ref = capability.ref
    scope = ref.scope if ref.value is None else f"{ref.scope}:{ref.value}"
    return f"{ref.kind} {ref.capability_id} [{scope}] -> {capability.secret.name}"
