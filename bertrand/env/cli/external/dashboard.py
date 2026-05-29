"""External CLI endpoint for the Bertrand Kubernetes dashboard."""

from __future__ import annotations

import asyncio
import contextlib
import os
import shutil
import socket
import sys
import webbrowser
from typing import TYPE_CHECKING

from bertrand.env.git import BERTRAND_NAMESPACE, Deadline
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.dashboard import DASHBOARD_NAME, ensure_dashboard_backend

if TYPE_CHECKING:
    from asyncio import StreamReader
    from typing import TextIO

_LOCALHOST = "127.0.0.1"
_PORT_FORWARD_READINESS_SECONDS = 15.0


async def bertrand_dashboard(
    *,
    port: int,
    open_browser: bool | None,
    timeout: float,
) -> None:
    """Launch the Bertrand dashboard through a local port-forward.

    Parameters
    ----------
    port : int
        Local TCP port to bind. Zero selects a free localhost port.
    open_browser : bool | None
        Whether to open the dashboard URL in a browser. `None` enables an
        automatic GUI-capability check.
    timeout : float
        Maximum backend convergence and port-forward readiness budget in seconds.

    Raises
    ------
    ValueError
        If the requested port is invalid.
    """
    if port < 0 or port > 65535:
        msg = "dashboard port must be between 0 and 65535"
        raise ValueError(msg)
    deadline = Deadline.from_timeout(
        timeout,
        message="dashboard timeout must be positive",
    )

    local_port = _free_port() if port == 0 else port
    with await Kube.host(timeout=deadline.remaining()) as kube:
        await ensure_dashboard_backend(kube, timeout=deadline.remaining())

    await _port_forward_dashboard(
        local_port,
        open_browser=open_browser,
        timeout=deadline.check("timed out before dashboard port-forward startup"),
    )


async def _port_forward_dashboard(
    local_port: int,
    *,
    open_browser: bool | None,
    timeout: float,
) -> None:
    process = await asyncio.create_subprocess_exec(
        "microk8s",
        "kubectl",
        "port-forward",
        "--namespace",
        BERTRAND_NAMESPACE,
        f"service/{DASHBOARD_NAME}",
        f"{local_port}:80",
        "--address",
        _LOCALHOST,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )
    ready = asyncio.Event()
    output: list[str] = []
    pumps = (
        asyncio.create_task(
            _pump_port_forward(process.stdout, sys.stderr, ready, output)
        ),
        asyncio.create_task(
            _pump_port_forward(process.stderr, sys.stderr, ready, output)
        ),
    )
    try:
        await _wait_port_forward_ready(
            process,
            ready=ready,
            output=output,
            timeout=min(timeout, _PORT_FORWARD_READINESS_SECONDS),
        )
        url = f"http://{_LOCALHOST}:{local_port}"
        print(url)
        if _should_open_browser(open_browser=open_browser):
            webbrowser.open(url, new=2)
        print(
            "bertrand: dashboard tunnel is active; press Ctrl-C to stop",
            file=sys.stderr,
        )
        await process.wait()
        if process.returncode:
            msg = _port_forward_error(process.returncode, output)
            raise OSError(msg)
    except asyncio.CancelledError:
        _terminate_process(process)
        raise
    except KeyboardInterrupt:
        _terminate_process(process)
    finally:
        _terminate_process(process)
        await asyncio.gather(*pumps, return_exceptions=True)


async def _pump_port_forward(
    reader: StreamReader | None,
    sink: TextIO,
    ready: asyncio.Event,
    output: list[str],
) -> None:
    if reader is None:
        return
    while True:
        raw = await reader.readline()
        if not raw:
            return
        text = raw.decode("utf-8", errors="replace")
        output.append(text)
        if "Forwarding from" in text:
            ready.set()
        elif "error" in text.lower() or "unable" in text.lower():
            print(text, end="", file=sink)


async def _wait_port_forward_ready(
    process: asyncio.subprocess.Process,
    *,
    ready: asyncio.Event,
    output: list[str],
    timeout: float,
) -> None:
    try:
        await asyncio.wait_for(ready.wait(), timeout=timeout)
    except TimeoutError as err:
        if process.returncode is None:
            _terminate_process(process)
            msg = "timed out waiting for dashboard port-forward readiness"
            raise TimeoutError(msg) from err
        msg = _port_forward_error(process.returncode, output)
        raise OSError(msg) from err


def _port_forward_error(returncode: int | None, output: list[str]) -> str:
    detail = "".join(output).strip()
    if not detail:
        detail = "no diagnostic output was captured"
    return f"dashboard port-forward exited with status {returncode}: {detail}"


def _terminate_process(process: asyncio.subprocess.Process) -> None:
    if process.returncode is not None:
        return
    with contextlib.suppress(ProcessLookupError):
        process.terminate()


def _free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind((_LOCALHOST, 0))
        return int(sock.getsockname()[1])


def _should_open_browser(*, open_browser: bool | None) -> bool:
    if open_browser is not None:
        return open_browser
    if sys.platform in {"darwin", "win32"}:
        return True
    if shutil.which("xdg-open") is None:
        return False
    return bool(os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY"))
