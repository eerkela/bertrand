"""Host-side bridge for Kubernetes development mailbox requests."""

from __future__ import annotations

import asyncio
import contextlib
import shutil
import subprocess
import sys
import time
from contextlib import asynccontextmanager
from pathlib import Path
from typing import TYPE_CHECKING

from bertrand.env.config.bertrand import EDITORS, Editor
from bertrand.env.git import CommandError, run
from bertrand.env.kube.dev.mailbox import (
    CODE_OPEN_RESOURCE,
    CodeOpenRecord,
    code_open_session_labels,
    patch_code_open_request_status,
)

if TYPE_CHECKING:
    from collections.abc import AsyncIterator

    from bertrand.env.kube.api.client import Kube

BRIDGE_POLL_SECONDS = 0.5
BRIDGE_API_TIMEOUT_SECONDS = 5.0
VSCODE_DEV_CONTAINERS_EXTENSION = "ms-vscode-remote.remote-containers"
VSCODE_KUBERNETES_EXTENSION = "ms-kubernetes-tools.vscode-kubernetes-tools"
_BRIDGE_STATUS_PATCH_ERRORS: tuple[type[Exception], ...] = (
    OSError,
    RuntimeError,
    TimeoutError,
    ValueError,
)


@asynccontextmanager
async def code_open_bridge(
    kube: Kube,
    *,
    session_id: str,
    host_id: str,
) -> AsyncIterator[None]:
    """Run a host bridge for editor-open mailbox requests in one dev session.

    Parameters
    ----------
    kube : Kube
        Active host-side Kubernetes API context.
    session_id : str
        Session identifier this bridge is allowed to service.
    host_id : str
        Durable Bertrand host identity.
    """
    seen: set[str] = set()
    handlers: set[asyncio.Task[None]] = set()
    task = asyncio.create_task(
        _run_bridge(
            kube,
            session_id=session_id,
            host_id=host_id,
            seen=seen,
            handlers=handlers,
        )
    )
    try:
        yield
    finally:
        task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await task
        for handler in tuple(handlers):
            handler.cancel()
        for handler in tuple(handlers):
            with contextlib.suppress(asyncio.CancelledError):
                await handler


async def _run_bridge(
    kube: Kube,
    *,
    session_id: str,
    host_id: str,
    seen: set[str],
    handlers: set[asyncio.Task[None]],
) -> None:
    labels = code_open_session_labels(session_id)
    while True:
        try:
            records = await CODE_OPEN_RESOURCE.list(
                kube,
                labels=labels,
                timeout=BRIDGE_API_TIMEOUT_SECONDS,
            )
        except (OSError, RuntimeError, TimeoutError, ValueError) as err:
            print(
                f"bertrand: warning: failed to poll editor mailbox: {err}",
                file=sys.stderr,
            )
            await asyncio.sleep(BRIDGE_POLL_SECONDS)
            continue
        for record in records:
            if record.name in seen or record.status.terminal:
                continue
            if record.spec.session_id != session_id:
                continue
            seen.add(record.name)
            handler = asyncio.create_task(
                _handle_bridge_record(kube, record=record, host_id=host_id)
            )
            handlers.add(handler)
            handler.add_done_callback(handlers.discard)
        await asyncio.sleep(BRIDGE_POLL_SECONDS)


async def _handle_bridge_record(
    kube: Kube,
    *,
    record: CodeOpenRecord,
    host_id: str,
) -> None:
    try:
        if record.spec.expired:
            await patch_code_open_request_status(
                kube,
                record=record,
                phase="Expired",
                host_id=host_id,
                message="request deadline expired before host bridge accepted it",
                timeout=BRIDGE_API_TIMEOUT_SECONDS,
            )
            return
        record = await patch_code_open_request_status(
            kube,
            record=record,
            phase="Accepted",
            host_id=host_id,
            message="host editor bridge accepted the request",
            timeout=BRIDGE_API_TIMEOUT_SECONDS,
        )
        await _open_editor(record)
    except (OSError, RuntimeError, TimeoutError, ValueError) as err:
        message = str(err)
        with contextlib.suppress(*_BRIDGE_STATUS_PATCH_ERRORS):
            await patch_code_open_request_status(
                kube,
                record=record,
                phase="Failed",
                host_id=host_id,
                message=message,
                timeout=BRIDGE_API_TIMEOUT_SECONDS,
            )
        return
    with contextlib.suppress(*_BRIDGE_STATUS_PATCH_ERRORS):
        await patch_code_open_request_status(
            kube,
            record=record,
            phase="Succeeded",
            host_id=host_id,
            message="editor request completed",
            timeout=BRIDGE_API_TIMEOUT_SECONDS,
        )


async def _open_editor(record: CodeOpenRecord) -> None:
    editor = record.spec.editor
    if editor != "vscode":
        msg = f"unsupported editor for development mailbox request: {editor!r}"
        raise ValueError(msg)
    editor_bin = _resolve_editor_bin(editor)
    await _validate_vscode_extensions(editor_bin)
    message = _vscode_attach_message(record)
    # VS Code documents Kubernetes attach through the Dev Containers/Kubernetes UI
    # flow, but does not expose a stable CLI URI for an exact pod/container attach.
    # Opening the app gives the user the right surface, while the failed status keeps
    # lifecycle semantics honest for external `bertrand code --block`.
    try:
        subprocess.Popen(
            [str(editor_bin), "--new-window"],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
    except OSError as err:
        msg = f"failed to launch VS Code before Kubernetes attach guidance: {err}"
        raise RuntimeError(msg) from err
    raise RuntimeError(message)


def _resolve_editor_bin(editor: Editor | str) -> Path:
    candidates = EDITORS.get(str(editor), [])
    for candidate in candidates:
        resolved = shutil.which(candidate)
        if resolved:
            path = Path(resolved).expanduser().resolve()
            if path.is_file():
                return path
    msg = (
        f"failed to resolve host editor alias {editor!r} from configured candidates: "
        f"{candidates}"
    )
    raise RuntimeError(msg)


async def _validate_vscode_extensions(editor_bin: Path) -> None:
    try:
        result = await run(
            [str(editor_bin), "--list-extensions"],
            capture_output=True,
            timeout=30,
        )
    except CommandError as err:
        msg = f"failed to list VS Code extensions with {editor_bin}: {err}"
        raise RuntimeError(msg) from err
    installed = {line.strip().lower() for line in result.stdout.splitlines()}
    missing = [
        extension
        for extension in (
            VSCODE_DEV_CONTAINERS_EXTENSION,
            VSCODE_KUBERNETES_EXTENSION,
        )
        if extension.lower() not in installed
    ]
    if missing:
        msg = (
            "VS Code Kubernetes attachment requires these host extensions: "
            f"{', '.join(missing)}"
        )
        raise RuntimeError(msg)


def _vscode_attach_message(record: CodeOpenRecord) -> str:
    spec = record.spec
    remaining = max(0.0, spec.deadline - time.time())
    return (
        "VS Code Kubernetes pod attach is available through the documented "
        "Dev Containers/Kubernetes command-palette flow, but Bertrand does not "
        "yet have a stable VS Code CLI URI for exact pod attachment. In VS Code, "
        "run 'Dev Containers: Attach to Running Kubernetes Container...' and choose "
        f"namespace 'bertrand', pod {spec.pod_name!r}, container "
        f"{spec.container_name!r}, then open workspace path "
        f"{spec.workspace_path!r}. Request {record.name!r} has "
        f"{remaining:.1f}s before its deadline."
    )
