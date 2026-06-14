"""Node-local Ceph storage agent for Bertrand-managed OSD actions."""

from __future__ import annotations

import asyncio
import os
import platform
import sys
from contextlib import suppress
from datetime import UTC, datetime
from typing import TYPE_CHECKING

from bertrand.env.git import BERTRAND_NAMESPACE, NO_DEADLINE, Deadline
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.ceph.api import (
    PreparedOSD,
    delete_loop_fallback_substrate,
    delete_lvm_osd_substrate,
    discover_loop_fallback_osd,
    discover_lvm_osds,
    drain_ceph_osd,
    host_capacity_snapshot,
    host_id_from_host_state,
    prepare_loop_fallback_osd,
    prepare_lvm_osd,
    purge_ceph_osd,
)
from bertrand.env.kube.ceph.capacity import (
    CephStorageAction,
    CephStorageOSD,
    StorageOSDOrigin,
    StorageOSDPhase,
    patch_storage_action_status,
    patch_storage_osd_status,
    pending_storage_actions,
    read_storage_state,
    storage_loop_osd_name,
    upsert_storage_node_report,
    upsert_storage_osd,
)
from bertrand.env.kube.ceph.rook import (
    delete_osd_claims,
    observe_rook_osd,
    patch_rook_device_sets,
    resize_osd_claim,
    storage_osd_spec,
    wait_osd_claims_gone,
    wait_osd_workloads_gone,
)

if TYPE_CHECKING:
    from collections.abc import Mapping

STORAGE_WATCH_RESTART_DELAY_SECONDS = 1.0
STORAGE_AGENT_SYNC_INTERVAL_SECONDS = 5.0


class CephStorageAgent:
    """DaemonSet agent role for node-local Ceph capacity mutation."""

    def __init__(self, *, node_name: str | None = None) -> None:
        self.node_name = node_name or self.resolve_node_name()
        self.host_id = host_id_from_host_state()

    @staticmethod
    def resolve_node_name() -> str:
        """Resolve the Kubernetes node name for this agent process.

        Returns
        -------
        str
            Resolved Kubernetes node name.

        Raises
        ------
        OSError
            If no node name can be inferred from the process environment.
        """
        name = os.environ.get("NODE_NAME", "").strip()
        if name:
            return name
        name = sys.argv[2].strip() if len(sys.argv) > 2 else ""
        if name:
            return name
        name = platform.node().strip()
        if name:
            return name
        msg = "Ceph storage controller agent could not resolve NODE_NAME"
        raise OSError(msg)

    async def _watch_actions(
        self,
        kube: Kube,
        *,
        wake: asyncio.Event,
        deadline: Deadline,
    ) -> None:
        action_client = CephStorageAction
        while True:
            try:
                for action in await action_client.list(
                    kube,
                    namespace=BERTRAND_NAMESPACE,
                    deadline=deadline,
                ):
                    if (
                        action.spec.host_id == self.host_id
                        and action.status.phase == "Pending"
                    ):
                        wake.set()
                async for event in action_client.watch(
                    kube,
                    namespace=BERTRAND_NAMESPACE,
                    deadline=deadline,
                ):
                    action = event.object
                    if (
                        action.spec.host_id == self.host_id
                        and action.status.phase == "Pending"
                    ):
                        wake.set()
                wake.set()
                remaining = deadline.remaining
                if remaining <= 0:
                    return
                await deadline.sleep(STORAGE_WATCH_RESTART_DELAY_SECONDS)
            except asyncio.CancelledError:
                raise
            except (OSError, RuntimeError, ValueError) as err:
                print(
                    "bertrand: warning: Ceph storage controller action watch "
                    f"failed: {err}",
                    file=sys.stderr,
                )
                wake.set()
                remaining = deadline.remaining
                if remaining <= 0:
                    return
                await deadline.sleep(STORAGE_WATCH_RESTART_DELAY_SECONDS)

    async def _upsert_node_report(self, kube: Kube, *, deadline: Deadline) -> None:
        """Report current host free capacity for this node."""
        try:
            status = await host_capacity_snapshot(deadline=deadline)
        except OSError as err:
            status = {
                "free_bytes": 0,
                "path": "",
                "lvm_free_bytes": 0,
                "lvm_pvs": [],
                "lvm_pv_inventory": [],
                "loop_fallback_active": False,
                "heartbeat_at": datetime.now(UTC).isoformat(),
                "last_error": str(err),
            }
        await upsert_storage_node_report(
            kube,
            node_name=self.node_name,
            host_id=self.host_id,
            status=status,
            deadline=deadline,
        )

    async def _recover_loop_devices(self, kube: Kube, *, deadline: Deadline) -> None:
        """Best-effort recreation of this node's loop fallback device."""
        storage = await read_storage_state(kube, deadline=deadline)
        records = sorted(storage.status.osds.values(), key=lambda item: item.name)
        for record in records:
            if (
                record.node_name != self.node_name
                or record.host_id != self.host_id
                or record.origin != "loop-fallback"
                or record.phase not in {"HostPrepared", "Binding", "Ready", "Expanding"}
            ):
                continue
            try:
                await prepare_loop_fallback_osd(
                    name=record.name,
                    target_bytes=record.target_bytes,
                    deadline=deadline,
                )
            except (OSError, TimeoutError, ValueError) as err:
                await patch_storage_osd_status(
                    kube,
                    osd=record,
                    status={
                        "phase": "Failed",
                        "last_error": str(err),
                    },
                    deadline=deadline,
                )

    async def _recover_missing_osd_records(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
    ) -> None:
        """Reconstruct OSD records from live Bertrand host substrates."""
        storage = await read_storage_state(kube, deadline=deadline)
        existing = {record.name: record for record in storage.status.osds.values()}
        for name, prepared in await discover_lvm_osds(deadline=deadline):
            if name in existing:
                continue
            spec = storage_osd_spec(
                name=name,
                origin="lvm-pv",
                node_name=self.node_name,
                host_id=self.host_id,
                prepared=prepared,
                target_bytes=prepared.observed_bytes,
            )
            await upsert_storage_osd(
                kube,
                name=name,
                spec=spec,
                phase="HostPrepared",
                deadline=deadline,
            )
        loop_name = storage_loop_osd_name(self.host_id)
        if loop_name in existing:
            return
        prepared = await discover_loop_fallback_osd(
            name=loop_name,
            deadline=deadline,
        )
        if prepared is None:
            return
        spec = storage_osd_spec(
            name=loop_name,
            origin="loop-fallback",
            node_name=self.node_name,
            host_id=self.host_id,
            prepared=prepared,
            target_bytes=prepared.observed_bytes,
        )
        await upsert_storage_osd(
            kube,
            name=loop_name,
            spec=spec,
            phase="HostPrepared",
            deadline=deadline,
        )

    async def _pending_actions(
        self, kube: Kube, *, deadline: Deadline
    ) -> list[CephStorageAction]:
        """List pending actions assigned to this node.

        Returns
        -------
        list[CephStorageAction]
            Pending actions targeting this agent's node.
        """
        return [
            action
            for action in await pending_storage_actions(
                kube,
                node_name=self.node_name,
                deadline=deadline,
            )
            if action.spec.host_id == self.host_id
        ]

    @staticmethod
    def _shrink_osd_id(action: CephStorageAction) -> int:
        if action.spec.osd_id is None:
            msg = "retire-loop action is missing osd_id"
            raise ValueError(msg)
        return action.spec.osd_id

    @staticmethod
    def _target_bytes(action: CephStorageAction) -> int:
        if action.spec.target_bytes is None:
            msg = f"{action.spec.operation} action is missing target_bytes"
            raise ValueError(msg)
        return action.spec.target_bytes

    @staticmethod
    def _lvm_pv_name(action: CephStorageAction) -> str:
        pv_name = (action.spec.pv_name or "").strip()
        if not pv_name:
            msg = "expand-lvm action is missing pv_name"
            raise ValueError(msg)
        return pv_name

    @staticmethod
    def _storage_osd_name(action: CephStorageAction) -> str:
        name = (action.spec.storage_osd_name or "").strip()
        if name:
            return name
        msg = f"{action.spec.operation} action is missing storage_osd_name"
        raise ValueError(msg)

    async def _claim_action(
        self,
        kube: Kube,
        *,
        action: CephStorageAction,
        deadline: Deadline,
    ) -> None:
        await patch_storage_action_status(
            kube,
            action=action,
            status={
                "phase": "Running",
                "message": "action claimed by node agent",
                "worker_node": self.node_name,
                "started_at": datetime.now(UTC).isoformat(),
            },
            deadline=deadline,
        )

    async def _succeed_action(
        self,
        kube: Kube,
        *,
        action: CephStorageAction,
        status: Mapping[str, object],
        deadline: Deadline,
    ) -> None:
        await patch_storage_action_status(
            kube,
            action=action,
            status={
                "phase": "Succeeded",
                "worker_node": self.node_name,
                **dict(status),
                "finished_at": datetime.now(UTC).isoformat(),
            },
            deadline=deadline,
        )

    async def _fail_action(
        self,
        kube: Kube,
        *,
        action: CephStorageAction,
        error: BaseException,
        deadline: Deadline,
    ) -> None:
        await patch_storage_action_status(
            kube,
            action=action,
            status={
                "phase": "Failed",
                "message": str(error),
                "diagnostics": str(error),
                "worker_node": self.node_name,
                "finished_at": datetime.now(UTC).isoformat(),
            },
            deadline=deadline,
        )

    async def _osd_by_name(
        self,
        kube: Kube,
        *,
        name: str,
        deadline: Deadline,
    ) -> CephStorageOSD | None:
        storage = await read_storage_state(kube, deadline=deadline)
        return storage.status.osds.get(name)

    async def _lvm_osd_by_name(
        self,
        kube: Kube,
        *,
        name: str,
        deadline: Deadline,
    ) -> CephStorageOSD | None:
        storage = await read_storage_state(kube, deadline=deadline)
        osd = storage.status.osds.get(name)
        if osd is not None and osd.origin == "lvm-pv" and osd.host_id == self.host_id:
            return osd
        return None

    async def _loop_osd_by_id(
        self,
        kube: Kube,
        *,
        osd_id: int,
        deadline: Deadline,
    ) -> CephStorageOSD | None:
        storage = await read_storage_state(kube, deadline=deadline)
        return next(
            (
                item
                for item in storage.status.osds.values()
                if item.origin == "loop-fallback" and item.ceph_osd_id == osd_id
            ),
            None,
        )

    async def _patch_rook_current_osds(
        self,
        kube: Kube,
        *,
        deadline: Deadline,
    ) -> None:
        storage = await read_storage_state(kube, deadline=deadline)
        records = sorted(storage.status.osds.values(), key=lambda item: item.name)
        await patch_rook_device_sets(
            kube,
            records=records,
            deadline=deadline,
        )

    async def _admit_prepared_osd(
        self,
        kube: Kube,
        *,
        name: str,
        origin: StorageOSDOrigin,
        prepared: PreparedOSD,
        target_bytes: int,
        phase: StorageOSDPhase,
        resize_claim: bool,
        deadline: Deadline,
    ) -> tuple[CephStorageOSD, int | None, bool]:
        spec = storage_osd_spec(
            name=name,
            origin=origin,
            node_name=self.node_name,
            host_id=self.host_id,
            prepared=prepared,
            target_bytes=target_bytes,
        )
        record = await upsert_storage_osd(
            kube,
            name=name,
            spec=spec,
            phase=phase,
            deadline=deadline,
        )
        await self._patch_rook_current_osds(kube, deadline=deadline)
        if resize_claim:
            await resize_osd_claim(
                kube,
                record=record,
                deadline=deadline,
            )
        observed_id, ready = await observe_rook_osd(
            kube,
            record=record,
            deadline=deadline,
        )
        await patch_storage_osd_status(
            kube,
            osd=record,
            status={
                "phase": "Ready" if ready else "Binding",
                "observed_bytes": prepared.observed_bytes,
                "ceph_osd_id": observed_id,
                "last_error": "",
            },
            deadline=deadline,
        )
        return record, observed_id, ready

    async def _mark_target_osd_failed(
        self,
        kube: Kube,
        *,
        action: CephStorageAction,
        error: BaseException,
        deadline: Deadline,
    ) -> None:
        if action.spec.operation not in {"expand-lvm", "expand-loop", "shrink-lvm"}:
            return
        name = self._storage_osd_name(action)
        record = await self._osd_by_name(kube, name=name, deadline=deadline)
        if record is None:
            return
        await patch_storage_osd_status(
            kube,
            osd=record,
            status={
                "phase": "Failed",
                "last_error": str(error),
            },
            deadline=deadline,
        )

    async def _execute_expand_lvm(
        self,
        kube: Kube,
        *,
        action: CephStorageAction,
        deadline: Deadline,
    ) -> None:
        pv_name = self._lvm_pv_name(action)
        name = self._storage_osd_name(action)
        target_bytes = self._target_bytes(action)
        existing = await self._osd_by_name(kube, name=name, deadline=deadline)
        prepared = await prepare_lvm_osd(
            name=name,
            target_bytes=target_bytes,
            pv_name=pv_name,
            lv_name=action.spec.lv_name,
            deadline=deadline,
        )
        phase: StorageOSDPhase = "Expanding" if existing is not None else "HostPrepared"
        _, observed_id, ready = await self._admit_prepared_osd(
            kube,
            name=name,
            origin="lvm-pv",
            prepared=prepared,
            target_bytes=target_bytes,
            phase=phase,
            resize_claim=True,
            deadline=deadline,
        )
        await self._succeed_action(
            kube,
            action=action,
            status={
                "message": (
                    "Rook LVM OSD expansion submitted"
                    if not ready
                    else "Rook LVM OSD expansion completed"
                ),
                "created_osd_ids": [] if observed_id is None else [observed_id],
                "osd_origin": "lvm-pv",
                "osd_quality": "durable",
                "source_pv": pv_name,
                "source_lv": prepared.lv_name,
                "provisioned_bytes": prepared.observed_bytes,
            },
            deadline=deadline,
        )

    async def _execute_expand_loop(
        self,
        kube: Kube,
        *,
        action: CephStorageAction,
        deadline: Deadline,
    ) -> None:
        name = self._storage_osd_name(action)
        target_bytes = self._target_bytes(action)
        existing = await self._osd_by_name(kube, name=name, deadline=deadline)
        prepared = await prepare_loop_fallback_osd(
            name=name,
            target_bytes=target_bytes,
            deadline=deadline,
        )
        phase: StorageOSDPhase = "Expanding" if existing is not None else "HostPrepared"
        _, observed_id, ready = await self._admit_prepared_osd(
            kube,
            name=name,
            origin="loop-fallback",
            prepared=prepared,
            target_bytes=target_bytes,
            phase=phase,
            resize_claim=True,
            deadline=deadline,
        )
        await self._succeed_action(
            kube,
            action=action,
            status={
                "message": (
                    "Rook loop fallback OSD expansion submitted"
                    if not ready
                    else "Rook loop fallback OSD expansion completed"
                ),
                "created_osd_ids": [] if observed_id is None else [observed_id],
                "osd_origin": "loop-fallback",
                "osd_quality": "elastic",
                "provisioned_bytes": prepared.observed_bytes,
            },
            deadline=deadline,
        )

    async def _execute_shrink_lvm(
        self,
        kube: Kube,
        *,
        action: CephStorageAction,
        deadline: Deadline,
    ) -> None:
        name = self._storage_osd_name(action)
        target_bytes = self._target_bytes(action)
        old_osd_id = self._shrink_osd_id(action)
        record = await self._lvm_osd_by_name(kube, name=name, deadline=deadline)
        if record is None:
            msg = f"could not find managed LVM OSD record {name!r}"
            raise OSError(msg)
        if target_bytes >= record.target_bytes:
            msg = (
                f"shrink target {target_bytes} must be smaller than "
                f"current target {record.target_bytes}"
            )
            raise ValueError(msg)
        if record.ceph_osd_id != old_osd_id:
            msg = (
                f"LVM OSD {name!r} currently maps to osd.{record.ceph_osd_id}; "
                f"action expected osd.{old_osd_id}"
            )
            raise OSError(msg)
        await patch_storage_osd_status(
            kube,
            osd=record,
            status={
                "phase": "Shrinking",
                "last_error": "",
            },
            deadline=deadline,
        )
        await self._patch_rook_current_osds(kube, deadline=deadline)
        await drain_ceph_osd(old_osd_id, deadline=deadline)
        await wait_osd_workloads_gone(
            kube,
            record=record,
            deadline=deadline,
        )
        await purge_ceph_osd(old_osd_id, deadline=deadline)
        await delete_osd_claims(
            kube,
            record=record,
            deadline=deadline,
        )
        await wait_osd_claims_gone(
            kube,
            record=record,
            deadline=deadline,
        )
        await delete_lvm_osd_substrate(
            lv_name=record.lv_name,
            block_path=record.block_path,
            deadline=deadline,
        )
        prepared = await prepare_lvm_osd(
            name=name,
            target_bytes=target_bytes,
            pv_name=record.pv_name,
            lv_name=record.lv_name,
            deadline=deadline,
        )
        record, observed_id, ready = await self._admit_prepared_osd(
            kube,
            name=name,
            origin="lvm-pv",
            prepared=prepared,
            target_bytes=target_bytes,
            phase="HostPrepared",
            resize_claim=False,
            deadline=deadline,
        )
        await self._succeed_action(
            kube,
            action=action,
            status={
                "message": (
                    "Rook LVM OSD shrink submitted"
                    if not ready
                    else "Rook LVM OSD shrink completed"
                ),
                "removed_osd_ids": [old_osd_id],
                "created_osd_ids": [] if observed_id is None else [observed_id],
                "osd_origin": "lvm-pv",
                "osd_quality": "durable",
                "source_pv": record.pv_name,
                "source_lv": prepared.lv_name,
                "provisioned_bytes": prepared.observed_bytes,
            },
            deadline=deadline,
        )

    async def _execute_retire_loop(
        self,
        kube: Kube,
        *,
        action: CephStorageAction,
        deadline: Deadline,
    ) -> None:
        osd_id = self._shrink_osd_id(action)
        record = await self._loop_osd_by_id(kube, osd_id=osd_id, deadline=deadline)
        if record is None:
            msg = f"could not find managed loop fallback record for osd.{osd_id}"
            raise OSError(msg)
        await patch_storage_osd_status(
            kube,
            osd=record,
            status={
                "phase": "Retiring",
                "last_error": "",
            },
            deadline=deadline,
        )
        await drain_ceph_osd(osd_id, deadline=deadline)
        await self._patch_rook_current_osds(kube, deadline=deadline)
        await wait_osd_workloads_gone(
            kube,
            record=record,
            deadline=deadline,
        )
        await purge_ceph_osd(osd_id, deadline=deadline)
        await delete_osd_claims(
            kube,
            record=record,
            deadline=deadline,
        )
        await delete_loop_fallback_substrate(
            loop_file=record.loop_file,
            loop_device=record.loop_device,
            block_path=record.block_path,
            deadline=deadline,
        )
        await patch_storage_osd_status(
            kube,
            osd=record,
            status={
                "phase": "Retired",
                "retired_at": datetime.now(UTC).isoformat(),
                "last_error": "",
            },
            deadline=deadline,
        )
        await self._succeed_action(
            kube,
            action=action,
            status={
                "message": f"Rook loop fallback retirement completed for osd.{osd_id}",
                "removed_osd_ids": [osd_id],
            },
            deadline=deadline,
        )

    async def _execute_action(
        self, kube: Kube, *, action: CephStorageAction, deadline: Deadline
    ) -> None:
        """Claim and execute one pending action on this node.

        Raises
        ------
        asyncio.CancelledError
            If the surrounding task is cancelled.
        """
        try:
            await self._claim_action(kube, action=action, deadline=deadline)
            if action.spec.operation == "expand-lvm":
                await self._execute_expand_lvm(
                    kube,
                    action=action,
                    deadline=deadline,
                )
            elif action.spec.operation == "expand-loop":
                await self._execute_expand_loop(
                    kube,
                    action=action,
                    deadline=deadline,
                )
            elif action.spec.operation == "shrink-lvm":
                await self._execute_shrink_lvm(
                    kube,
                    action=action,
                    deadline=deadline,
                )
            else:
                await self._execute_retire_loop(
                    kube,
                    action=action,
                    deadline=deadline,
                )
        except asyncio.CancelledError:
            raise
        except (OSError, TimeoutError, ValueError, RuntimeError) as err:
            with suppress(OSError, TimeoutError, ValueError):
                await self._mark_target_osd_failed(
                    kube,
                    action=action,
                    error=err,
                    deadline=deadline,
                )
            await self._fail_action(
                kube,
                action=action,
                error=err,
                deadline=deadline,
            )

    async def sync(self, kube: Kube, *, deadline: Deadline) -> None:
        """Run one node-agent synchronization pass.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Event-loop deadline for this synchronization pass.
        """
        await self._recover_missing_osd_records(kube, deadline=deadline)
        await self._recover_loop_devices(kube, deadline=deadline)
        await self._upsert_node_report(kube, deadline=deadline)
        for action in await self._pending_actions(kube, deadline=deadline):
            await self._execute_action(kube, action=action, deadline=deadline)

    async def run(self, *, deadline: Deadline = NO_DEADLINE) -> None:
        """Run the node agent loop until cancelled or timed out.

        Parameters
        ----------
        deadline : Deadline
            Maximum agent runtime in seconds.

        """
        wake = asyncio.Event()
        wake.set()
        with Kube.internal() as kube:
            async with asyncio.TaskGroup() as group:
                group.create_task(
                    self._watch_actions(kube, wake=wake, deadline=deadline)
                )
                while True:
                    if not wake.is_set():
                        remaining = deadline.remaining
                        if remaining <= 0:
                            return
                        wait_timeout = min(
                            STORAGE_AGENT_SYNC_INTERVAL_SECONDS, remaining
                        )
                        with suppress(TimeoutError):
                            await asyncio.wait_for(
                                wake.wait(),
                                timeout=wait_timeout,
                            )
                    wake.clear()
                    await self.sync(kube, deadline=deadline)
