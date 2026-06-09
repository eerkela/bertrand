"""Teardown Bertrand-managed local runtime and owned k0s cluster state."""

from __future__ import annotations

import contextlib
import os
from dataclasses import dataclass
from uuid import UUID

from bertrand.env.git import (
    BERTRAND_NAMESPACE,
    STATE,
    Deadline,
    confirm,
    until,
    warn,
)
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.ceph.api import (
    ceph_health,
    delete_loop_fallback_substrate,
    delete_lvm_osd_substrate,
    drain_ceph_osd,
    purge_ceph_osd,
)
from bertrand.env.kube.ceph.capacity import (
    CephStorageOSD,
    patch_storage_osd_status,
    read_storage_state,
)
from bertrand.env.kube.ceph.rook import (
    delete_osd_claims,
    patch_rook_device_sets,
    wait_osd_claims_gone,
    wait_osd_workloads_gone,
)
from bertrand.env.kube.ceph.volume import (
    REPOSITORY_STATE_RESOURCE,
    delete_all_repository_volumes,
    retire_repository_mount_record,
)
from bertrand.env.kube.dashboard import delete_dashboard_backend
from bertrand.env.kube.dev import delete_dev_backend_state
from bertrand.env.kube.lock.cluster import ClusterLock
from bertrand.env.kube.node import Node
from bertrand.env.kube.node_identity import list_bertrand_nodes, retire_bertrand_node


@dataclass
class CleanState:
    """In-memory convergence state for `bertrand clean` stages.

    Attributes
    ----------
    yes : bool
        Whether interactive confirmations should be auto-accepted.
    force : bool
        Whether intermediate stage errors should be downgraded to warnings.
    deadline : Deadline
        Shared cleanup deadline.
    kube : Kube | None
        Optional Kubernetes API context.  When unavailable under `--force`, cleanup
        is limited to local host state.
    host_id : str | None
        Durable Bertrand host UUID used to retire this host's mount records.
    last_node : bool
        Whether this host is the final active Bertrand node in the cluster.
    """

    yes: bool
    force: bool
    deadline: Deadline
    kube: Kube | None
    host_id: str | None
    last_node: bool = False


_CLEAN_ERROR_TYPES = (OSError, RuntimeError, ValueError)


def _handle_clean_error(
    state: CleanState,
    stage: str,
    err: Exception,
    *,
    final: bool = False,
) -> None:
    if final or not state.force:
        msg = f"bertrand clean stage {stage!r} failed: {err}"
        raise OSError(msg) from err
    warn(f"clean stage {stage!r} failed; continuing due to --force: {err}")


async def _retire_repository_mount_records(state: CleanState) -> None:
    if state.kube is not None and state.host_id is not None:
        # This is the only durable cluster mutation in `bertrand clean`: retire
        # this host's mount aliases while preserving repository volumes for
        # recovery or future explicit destructive cleanup.
        await REPOSITORY_STATE_RESOURCE.ensure_crd(
            state.kube,
            deadline=state.deadline,
        )
        states = await REPOSITORY_STATE_RESOURCE.list(
            state.kube,
            namespace=BERTRAND_NAMESPACE,
            deadline=state.deadline,
        )
        records = [
            record
            for repository in states
            for record in repository.status.mounts.values()
            if record.host_id == state.host_id and record.phase == "Active"
        ]
        for record in records:
            await retire_repository_mount_record(
                state.kube,
                record=record,
                deadline=state.deadline,
            )


async def _clean_dashboard_backend(state: CleanState) -> None:
    if state.kube is None:
        return
    await delete_dashboard_backend(state.kube, deadline=state.deadline)


async def _clean_dev_backend(state: CleanState) -> None:
    if state.kube is None:
        return
    await delete_dev_backend_state(
        state.kube,
        host_id=state.host_id,
        deadline=state.deadline,
    )


async def _retire_local_node_record(state: CleanState) -> None:
    if state.kube is None or state.host_id is None:
        return
    await retire_bertrand_node(
        state.kube,
        host_id=state.host_id,
        deadline=state.deadline,
    )


async def _active_peer_hosts(state: CleanState) -> set[str]:
    if state.kube is None or state.host_id is None:
        return set()
    nodes = await list_bertrand_nodes(state.kube, deadline=state.deadline)
    return {
        node.host_id
        for node in nodes
        if node.phase == "Active" and node.host_id != state.host_id
    }


async def _wait_ceph_clean(state: CleanState) -> None:
    async def healthy(attempt_deadline: Deadline) -> None:
        clean, detail, status = await ceph_health(deadline=attempt_deadline)
        if clean:
            return
        raise TimeoutError(detail or status or "Ceph health is not clean")

    await until(healthy, deadline=state.deadline, delay=2.0)


async def _patch_rook_without_osd(
    state: CleanState,
    record: CephStorageOSD,
) -> None:
    if state.kube is None:
        return
    storage = await read_storage_state(state.kube, deadline=state.deadline)
    records = [
        item
        for item in storage.status.osds.values()
        if item.name != record.name and item.phase not in {"Failed", "Retired"}
    ]
    await patch_rook_device_sets(state.kube, records=records, deadline=state.deadline)


async def _retire_local_osd(state: CleanState, record: CephStorageOSD) -> None:
    if state.kube is None:
        return
    await patch_storage_osd_status(
        state.kube,
        osd=record,
        status={"phase": "Retiring", "last_error": ""},
        deadline=state.deadline,
    )
    osd_id = record.ceph_osd_id
    if osd_id is not None:
        await drain_ceph_osd(osd_id, deadline=state.deadline)
    await _patch_rook_without_osd(state, record)
    await wait_osd_workloads_gone(state.kube, record=record, deadline=state.deadline)
    if osd_id is not None:
        await purge_ceph_osd(osd_id, deadline=state.deadline)
    await delete_osd_claims(state.kube, record=record, deadline=state.deadline)
    await wait_osd_claims_gone(state.kube, record=record, deadline=state.deadline)
    if record.origin == "loop-fallback":
        await delete_loop_fallback_substrate(
            loop_file=record.loop_file,
            loop_device=record.loop_device,
            block_path=record.block_path,
            deadline=state.deadline,
        )
    else:
        await delete_lvm_osd_substrate(
            lv_name=record.lv_name,
            block_path=record.block_path,
            deadline=state.deadline,
        )
    await patch_storage_osd_status(
        state.kube,
        osd=record,
        status={"phase": "Retired", "last_error": ""},
        deadline=state.deadline,
    )


async def _evacuate_local_ceph_osds(state: CleanState) -> None:
    if state.kube is None or state.host_id is None:
        return
    storage = await read_storage_state(state.kube, deadline=state.deadline)
    local = [
        record
        for record in sorted(storage.status.osds.values(), key=lambda item: item.name)
        if record.host_id == state.host_id and record.phase not in {"Failed", "Retired"}
    ]
    for record in local:
        await _retire_local_osd(state, record)
    if local and not state.last_node:
        await _wait_ceph_clean(state)


async def _delete_kubernetes_node(state: CleanState) -> None:
    if state.kube is None or state.last_node:
        return
    node = await Node.local(state.kube, deadline=state.deadline)
    await node.drain(state.kube, deadline=state.deadline, force=state.force)
    await Node.resource.delete_by_name(
        state.kube,
        name=node.name,
        deadline=state.deadline,
    )


async def _delete_final_cluster_repository_volumes(state: CleanState) -> None:
    if state.kube is None or not state.last_node:
        return
    deleted = await delete_all_repository_volumes(state.kube, deadline=state.deadline)
    if deleted:
        names = ", ".join(record.spec.repo_id for record in deleted)
        warn(f"deleted final Bertrand repository volume(s): {names}")


async def bertrand_clean(*, deadline: Deadline, yes: bool, force: bool) -> None:
    """Clean Bertrand-managed runtime objects and local state on the host.

    Parameters
    ----------
    deadline : Deadline
        Overall deadline before cleanup stages time out and raise an error.
    yes : bool
        Whether to auto-accept prompts during cleanup.
    force : bool
        Whether to continue through intermediate stage failures, logging warnings
        and attempting subsequent stages.

    Raises
    ------
    PermissionError
        If the user lacks root privileges or they decline cleanup.
    OSError
        If cleanup fails to converge.
    """
    # require root privileges for global cleanup
    if os.geteuid() != 0:
        msg = (
            "Global Bertrand cleanup requires root privileges.  Re-run this command "
            "with sudo."
        )
        raise PermissionError(msg)
    kube: Kube | None = None
    try:
        kube = Kube.external()
    except _CLEAN_ERROR_TYPES as err:
        if not force:
            msg = (
                "failed to connect to Bertrand's owned k0s runtime. Normal cleanup "
                "needs cluster access so it can distinguish node departure from "
                f"final cluster teardown safely: {err}"
            )
            raise OSError(msg) from err
        warn(
            "continuing local cleanup without Kubernetes access due to --force. "
            "Durable cluster cleanup could not be verified, and repository volumes "
            f"may remain in a remote Bertrand cluster: {err}"
        )

    try:
        host_id = UUID(STATE.id).hex
    except (OSError, ValueError) as err:
        if kube is not None and not force:
            msg = (
                f"failed to read Bertrand host identity at {STATE.id_file}; "
                "cannot "
                "retire this host's repository mount records safely"
            )
            raise OSError(msg) from err
        host_id = None

    state = CleanState(
        yes=yes,
        force=force,
        deadline=deadline,
        kube=kube,
        host_id=host_id,
    )
    manager = kube if kube is not None else contextlib.nullcontext()
    with manager:
        lock: ClusterLock | None = None
        try:
            if kube is not None:
                lock = ClusterLock(kube, "bertrand-clean")
                await lock.lock(deadline)
                peers = await _active_peer_hosts(state)
                state.last_node = not peers

            if kube is None:
                prompt = (
                    "Kubernetes access is unavailable, so --force cleanup can only "
                    "remove local reconstructible state and uninstall the local k0s "
                    "service. Durable cluster cleanup cannot be verified. Do you "
                    "want to proceed?\n[y/N] "
                )
            elif state.last_node:
                prompt = (
                    "This is the last active Bertrand node in the owned k0s cluster. "
                    "Cleanup will delete all remaining Bertrand repository volumes, "
                    "credentials, snapshots, cluster resources, local mounts, the "
                    "managed k0s service, and local state in "
                    f"{STATE.root}. This is "
                    "destructive. Do you want to proceed?\n[y/N] "
                )
            else:
                prompt = (
                    "This will remove this host from Bertrand's owned k0s cluster, "
                    "evacuate this host's Ceph OSDs, retire this host's mount and "
                    "node records, delete volatile dev/dashboard resources, uninstall "
                    "the local k0s service, and delete local state in "
                    f"{STATE.root}. "
                    "Durable repository volumes are preserved because other active "
                    "Bertrand nodes remain. Do you want to proceed?\n[y/N] "
                )
            if not confirm(prompt, yes=yes):
                msg = "Cleanup declined by user."
                raise PermissionError(msg)

            try:
                state.deadline.check(
                    "bertrand clean stage 'delete_final_cluster_repository_volumes' "
                    "timed out before execution"
                )
                await _delete_final_cluster_repository_volumes(state)
            except _CLEAN_ERROR_TYPES as err:
                _handle_clean_error(
                    state, "delete_final_cluster_repository_volumes", err
                )

            try:
                state.deadline.check(
                    "bertrand clean stage 'evacuate_local_ceph_osds' timed out before "
                    "execution"
                )
                await _evacuate_local_ceph_osds(state)
            except _CLEAN_ERROR_TYPES as err:
                _handle_clean_error(state, "evacuate_local_ceph_osds", err)

            try:
                state.deadline.check(
                    "bertrand clean stage 'clean_dev_backend' timed out before "
                    "execution"
                )
                await _clean_dev_backend(state)
            except _CLEAN_ERROR_TYPES as err:
                _handle_clean_error(state, "clean_dev_backend", err)

            try:
                state.deadline.check(
                    "bertrand clean stage 'clean_dashboard_backend' timed out before "
                    "execution"
                )
                await _clean_dashboard_backend(state)
            except _CLEAN_ERROR_TYPES as err:
                _handle_clean_error(state, "clean_dashboard_backend", err)

            try:
                state.deadline.check(
                    "bertrand clean stage 'retire_repository_mount_records' timed "
                    "out before execution"
                )
                await _retire_repository_mount_records(state)
            except _CLEAN_ERROR_TYPES as err:
                _handle_clean_error(state, "retire_repository_mount_records", err)

            try:
                state.deadline.check(
                    "bertrand clean stage 'delete_kubernetes_node' timed out before "
                    "execution"
                )
                await _delete_kubernetes_node(state)
            except _CLEAN_ERROR_TYPES as err:
                _handle_clean_error(state, "delete_kubernetes_node", err)

            try:
                state.deadline.check(
                    "bertrand clean stage 'retire_local_node_record' timed out before "
                    "execution"
                )
                await _retire_local_node_record(state)
            except _CLEAN_ERROR_TYPES as err:
                _handle_clean_error(state, "retire_local_node_record", err)

            try:
                state.deadline.check(
                    "bertrand clean stage 'uninstall_owned_k0s' timed out before "
                    "execution"
                )
                await Kube.clean(deadline=state.deadline)
            except _CLEAN_ERROR_TYPES as err:
                _handle_clean_error(state, "uninstall_owned_k0s", err)

            try:
                state.deadline.check(
                    "bertrand clean stage 'clean_local_state' timed out before "
                    "execution"
                )
                await STATE.clean(deadline=state.deadline)
            except _CLEAN_ERROR_TYPES as err:
                _handle_clean_error(state, "clean_local_state", err, final=True)
        finally:
            if lock is not None:
                await lock.unlock(ignore_errors=True)
