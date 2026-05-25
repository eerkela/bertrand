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
    if command not in {"add", "rm", "list"}:
        msg = f"unsupported secret command: {command!r}"
        raise ValueError(msg)
    with await Kube.host(timeout=args.timeout) as kube:
        if command == "add":
            ref = await _path_capability_ref(
                kube,
                args.path,
                kind=args.kind,
                capability_id=args.id,
                timeout=args.timeout,
            )
            await prune_repository_mounts_quietly(kube, timeout=args.timeout)
            await _add_capability(
                kube,
                ref,
                source=args.source,
                timeout=args.timeout,
            )
            return
        if command == "rm":
            ref = await _path_capability_ref(
                kube,
                args.path,
                kind=args.kind,
                capability_id=args.id,
                timeout=args.timeout,
            )
            await prune_repository_mounts_quietly(kube, timeout=args.timeout)
            await _remove_capability(kube, ref, timeout=args.timeout)
            return
        if command == "list":
            targets = await _path_scope_targets(
                kube,
                args.path,
                include_local_node=True,
                timeout=args.timeout,
            )
            await prune_repository_mounts_quietly(kube, timeout=args.timeout)
            await _list_capabilities(
                kube,
                targets,
                kind=args.kind,
                json_output=args.json,
                timeout=args.timeout,
            )
            return


async def _add_capability(
    kube: Kube,
    ref: CapabilityRef,
    *,
    source: str | None,
    timeout: float,
) -> None:
    payload = _read_payload(source)
    capability = await Capability.upsert(
        kube,
        ref=ref,
        payload=payload,
        timeout=timeout,
    )
    print(_format_capability_line(capability))


async def _remove_capability(kube: Kube, ref: CapabilityRef, *, timeout: float) -> None:
    capability = await Capability.get(kube, ref=ref, timeout=timeout)
    if capability is None:
        print(f"{ref.kind} {ref.capability_id}: not found")
        return
    await capability.delete(kube, timeout=timeout)
    print(f"{ref.kind} {ref.capability_id}: deleted")


async def _list_capabilities(
    kube: Kube,
    targets: tuple[_ScopeTarget, ...],
    *,
    kind: CapabilityKind | None,
    json_output: bool,
    timeout: float,
) -> None:
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
    worktree_id = project_worktree_id(repo.root / worktree)
    return CapabilityRef.worktree(kind, capability_id, worktree_id)


async def _path_scope_targets(
    kube: Kube,
    path: str,
    *,
    include_local_node: bool,
    timeout: float,
) -> tuple[_ScopeTarget, ...]:
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


def _shared_scope_targets() -> tuple[_ScopeTarget, ...]:
    return (_ScopeTarget("shared", None, "shared cluster"),)


async def _local_node_scope_targets(
    kube: Kube,
    *,
    timeout: float,
) -> tuple[_ScopeTarget, ...]:
    node = await ensure_local_bertrand_node(kube, timeout=timeout)
    label = node.display_name or node.host_id
    return (
        _ScopeTarget("node", node.host_id, f"node {label}"),
        _ScopeTarget("shared", None, "shared cluster"),
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
