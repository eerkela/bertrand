"""Legacy nerdctl and local BuildKit helpers for the old container workflow."""

from __future__ import annotations

import asyncio
import contextlib
import os
import platform
import shutil
import signal
import uuid
from collections.abc import Mapping, Sequence
from pathlib import Path
from typing import Literal

from bertrand.env.run import (
    BERTRAND_ENV,
    BERTRAND_NAMESPACE,
    CACHE_DIR,
    INFINITY,
    NORMALIZE_ARCH,
    RUN_DIR,
    TOOLS_DIR,
    CommandError,
    CompletedProcess,
    Lock,
    TimeoutExpired,
    atomic_write_text,
    download_file,
    file_digest,
    pid_alive,
    run,
)
from bertrand.env.run.bertrand_git import tail_lines

type ContainerState = Literal[
    "created",
    "restarting",
    "running",
    "removing",
    "paused",
    "exited",
    "dead",
]

TIMEOUT = INFINITY
MICROK8S_NAMESPACE = "k8s.io"
MICROK8S_CONTAINERD_SOCKET = Path("/var/snap/microk8s/common/run/containerd.sock")
MICROK8S_CONTAINERD_ADDRESS = f"unix://{MICROK8S_CONTAINERD_SOCKET.as_posix()}"
NERDCTL_VERSION = "2.2.2"
NERDCTL_BASE_URL = (
    f"https://github.com/containerd/nerdctl/releases/download/v{NERDCTL_VERSION}"
)
NERDCTL_CHECKSUM: dict[str, str] = {
    "amd64": "8a477f35533c6cc1120c19558d8142967c74f25a4b952b481f48104e030de914",
    "arm64": "55d68d2613b5f065021146bac21f620cde9e7fdd4bd3eff74cd324f5462e107a",
}
NERDCTL_INSTALL_DIR = TOOLS_DIR / f"nerdctl-{NERDCTL_VERSION}"
NERDCTL_BIN = NERDCTL_INSTALL_DIR / "bin" / "nerdctl"
BUILDCTL_BIN = NERDCTL_INSTALL_DIR / "bin" / "buildctl"
BUILDKITD_BIN: Path = NERDCTL_INSTALL_DIR / "bin" / "buildkitd"
BUILDKIT_SOCKET = RUN_DIR / "buildkitd.sock"
BUILDKIT_ADDRESS = f"unix://{BUILDKIT_SOCKET.as_posix()}"
BUILDKIT_PID_FILE = RUN_DIR / "buildkitd.pid"
BUILDKIT_LOG_FILE = RUN_DIR / "buildkitd.log"
BUILDKIT_LOCK_FILE = RUN_DIR / "buildkitd.lock"
NERDCTL_REQUIRED_PATHS = (
    NERDCTL_BIN,
    BUILDCTL_BIN,
    BUILDKITD_BIN,
)


async def install_nerdctl() -> None:
    """Install the legacy managed nerdctl toolchain."""
    if all(path.exists() for path in NERDCTL_REQUIRED_PATHS):
        return

    arch = NORMALIZE_ARCH.get(platform.machine().strip().lower())
    if not arch:
        msg = (
            "Unsupported CPU architecture for pinned nerdctl artifact: "
            f"{platform.machine()!r} (supported: {sorted(NORMALIZE_ARCH)})"
        )
        raise OSError(msg)
    archive_name = f"nerdctl-full-{NERDCTL_VERSION}-linux-{arch}.tar.gz"
    archive_path = CACHE_DIR / archive_name
    archive_url = f"{NERDCTL_BASE_URL}/{archive_name}"
    expected_sha = NERDCTL_CHECKSUM[arch]

    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    needs_download = True
    if archive_path.exists() and file_digest(archive_path) == expected_sha:
        needs_download = False
    if needs_download:
        await download_file(archive_url, archive_path)
        actual_sha = file_digest(archive_path)
        if actual_sha != expected_sha:
            with contextlib.suppress(OSError):
                archive_path.unlink()
            msg = (
                f"Checksum mismatch for {archive_name}: expected {expected_sha}, "
                f"got {actual_sha}."
            )
            raise OSError(msg)

    TOOLS_DIR.mkdir(parents=True, exist_ok=True)
    staged = TOOLS_DIR / f".nerdctl-{uuid.uuid4().hex}.tmp"
    if staged.exists():
        shutil.rmtree(staged, ignore_errors=True)
    staged.mkdir(parents=True, exist_ok=True)
    try:
        await run(["tar", "-xzf", str(archive_path), "-C", str(staged)])
        if not (staged / "bin" / "nerdctl").exists():
            msg = (
                "Pinned nerdctl archive extracted successfully, but expected binary "
                f"was not found at {(staged / 'bin' / 'nerdctl')}."
            )
            raise OSError(msg)
        if NERDCTL_INSTALL_DIR.exists():
            shutil.rmtree(NERDCTL_INSTALL_DIR, ignore_errors=True)
        staged.replace(NERDCTL_INSTALL_DIR)
    finally:
        if staged.exists():
            shutil.rmtree(staged, ignore_errors=True)

    if not all(path.exists() for path in NERDCTL_REQUIRED_PATHS):
        msg = (
            "Managed nerdctl toolchain installation completed, but required binaries "
            "are still missing."
        )
        raise OSError(msg)


async def assert_nerdctl_installed() -> None:
    """Raise with diagnostics when the legacy nerdctl toolchain is unusable."""
    if not all(path.exists() for path in NERDCTL_REQUIRED_PATHS):
        msg = (
            "Managed nerdctl toolchain is not installed or incomplete. Missing paths:\n"
            f"{'\n'.join(str(path) for path in NERDCTL_REQUIRED_PATHS)}."
        )
        raise OSError(msg)


async def buildctl(
    argv: list[str],
    *,
    check: bool = True,
    capture_output: bool | None = False,
    stdin: str | None = None,
    timeout: float = INFINITY,
    attempts: int = 1,
    delay: float = 0.1,
    cwd: Path | None = None,
    env: Mapping[str, str] | None = None,
) -> CompletedProcess:
    """Invoke the legacy managed buildctl binary."""
    return await run(
        [str(BUILDCTL_BIN), "--addr", BUILDKIT_ADDRESS, *argv],
        check=check,
        capture_output=capture_output,
        stdin=stdin,
        timeout=timeout,
        attempts=attempts,
        delay=delay,
        cwd=cwd,
        env=env,
    )


async def nerdctl(
    argv: list[str],
    *,
    check: bool = True,
    capture_output: bool | None = False,
    stdin: str | None = None,
    timeout: float = INFINITY,
    attempts: int = 1,
    delay: float = 0.1,
    cwd: Path | None = None,
    env: Mapping[str, str] | None = None,
) -> CompletedProcess:
    """Invoke the legacy managed nerdctl binary against MicroK8s containerd."""
    merged_env = os.environ.copy()
    if env is not None:
        merged_env.update(env)
    merged_env["BUILDKIT_HOST"] = BUILDKIT_ADDRESS
    return await run(
        [
            str(NERDCTL_BIN),
            "--address",
            MICROK8S_CONTAINERD_ADDRESS,
            "--namespace",
            BERTRAND_NAMESPACE,
            *argv,
        ],
        check=check,
        capture_output=capture_output,
        stdin=stdin,
        timeout=timeout,
        attempts=attempts,
        delay=delay,
        cwd=cwd,
        env=merged_env,
    )


async def nerdctl_ids(
    mode: Literal["container", "image", "volume", "network", "secret"],
    labels: Mapping[str, str],
    *,
    status: Sequence[ContainerState] | None = None,
    timeout: float = INFINITY,
) -> list[str]:
    """Retrieve matching legacy nerdctl resource IDs."""
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout

    cmd: list[str] = []
    if mode == "container":
        cmd.extend(
            [
                "container",
                "ls",
                "-a",
                "-q",
                "--no-trunc",
                "--filter",
                f"label={BERTRAND_ENV}=1",
            ]
        )
    elif mode == "image":
        cmd.extend(
            [
                "image",
                "ls",
                "-a",
                "-q",
                "--no-trunc",
                "--filter",
                f"label={BERTRAND_ENV}=1",
            ]
        )
    elif mode == "volume":
        cmd.extend(["volume", "ls", "-q", "--filter", f"label={BERTRAND_ENV}=1"])
    elif mode == "network":
        cmd.extend(["network", "ls", "-q", "--filter", f"label={BERTRAND_ENV}=1"])
    elif mode == "secret":
        cmd.extend(["secret", "ls", "-q", "--filter", f"label={BERTRAND_ENV}=1"])
    else:
        msg = f"invalid mode: {mode}"
        raise ValueError(msg)

    for key, value in labels.items():
        cmd.extend(["--filter", f"label={key}={value}"])

    out: list[str] = []
    seen: set[str] = set()
    try:
        statuses: Sequence[ContainerState | None] = (None,) if status is None else status
        for stat in statuses:
            query = cmd if stat is None else [*cmd, "--filter", f"status={stat}"]
            result = await nerdctl(
                query,
                capture_output=True,
                check=False,
                timeout=deadline - loop.time(),
            )
            if result.returncode != 0:
                continue
            for raw_id in result.stdout.splitlines():
                resource_id = raw_id.strip()
                if resource_id and resource_id not in seen:
                    seen.add(resource_id)
                    out.append(resource_id)
    except (TimeoutError, TimeoutExpired, KeyboardInterrupt, SystemExit):
        raise
    except Exception:  # noqa: BLE001
        pass
    return out


def _buildkit_pid() -> int | None:
    try:
        value = BUILDKIT_PID_FILE.read_text(encoding="utf-8").strip()
        return int(value) if value else None
    except (OSError, ValueError):
        return None


async def _buildkit_workers_ready(*, timeout: float) -> bool:
    if not BUILDKIT_SOCKET.exists():
        return False
    return (
        await buildctl(
            ["debug", "workers"],
            check=False,
            capture_output=True,
            timeout=timeout,
        )
    ).returncode == 0


async def start_buildkit(*, timeout: float) -> None:
    """Ensure that the legacy local BuildKit daemon is running."""
    if timeout <= 0:
        msg = "BuildKit timeout must be non-negative."
        raise TimeoutError(msg)
    if not BUILDCTL_BIN.exists() or not BUILDKITD_BIN.exists():
        msg = (
            "Managed BuildKit binaries are missing. Run `bertrand init` to install "
            "the pinned legacy toolchain."
        )
        raise OSError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    if await _buildkit_workers_ready(timeout=deadline - loop.time()):
        return

    async with Lock(BUILDKIT_LOCK_FILE, timeout=deadline - loop.time(), mode="local"):
        if await _buildkit_workers_ready(timeout=deadline - loop.time()):
            return

        pid = _buildkit_pid()
        if pid is None or not pid_alive(pid):
            BUILDKIT_PID_FILE.unlink(missing_ok=True)
        BUILDKIT_SOCKET.unlink(missing_ok=True)

        BUILDKIT_LOG_FILE.parent.mkdir(parents=True, exist_ok=True)
        with BUILDKIT_LOG_FILE.open("ab") as log:
            process = await asyncio.create_subprocess_exec(
                str(BUILDKITD_BIN),
                "--addr",
                BUILDKIT_ADDRESS,
                "--containerd-worker=true",
                "--containerd-worker-addr",
                str(MICROK8S_CONTAINERD_SOCKET),
                "--containerd-worker-namespace",
                BERTRAND_NAMESPACE,
                stdin=asyncio.subprocess.DEVNULL,
                stdout=log,
                stderr=log,
                start_new_session=True,
            )
        pid = process.pid
        atomic_write_text(BUILDKIT_PID_FILE, f"{pid}\n", encoding="utf-8")

        timestamp = loop.time()
        while pid_alive(pid) and timestamp <= deadline:
            if await _buildkit_workers_ready(timeout=deadline - timestamp):
                return
            await asyncio.sleep(0.1)
            timestamp = loop.time()

    detail = tail_lines(BUILDKIT_LOG_FILE, count=40)
    msg = (
        f"Failed to start legacy BuildKit daemon at {BUILDKIT_ADDRESS}. Check log "
        f"at {BUILDKIT_LOG_FILE}."
    )
    if detail:
        msg += f"\n\nLast buildkitd log lines:\n{detail}"
    raise OSError(msg)


async def stop_buildkit(*, timeout: float) -> None:
    """Stop the legacy local BuildKit daemon if it is running."""
    if timeout <= 0:
        msg = "BuildKit timeout must be non-negative."
        raise TimeoutError(msg)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout

    pid = _buildkit_pid()
    if pid is None or not pid_alive(pid):
        BUILDKIT_PID_FILE.unlink(missing_ok=True)
        BUILDKIT_SOCKET.unlink(missing_ok=True)
        return

    async with Lock(BUILDKIT_LOCK_FILE, timeout=deadline - loop.time(), mode="local"):
        pid = _buildkit_pid()
        if pid is None or not pid_alive(pid):
            BUILDKIT_PID_FILE.unlink(missing_ok=True)
            BUILDKIT_SOCKET.unlink(missing_ok=True)
            return

        with contextlib.suppress(ProcessLookupError):
            os.kill(pid, signal.SIGTERM)
        while pid_alive(pid) and loop.time() <= deadline:
            await asyncio.sleep(0.1)

        if pid_alive(pid):
            with contextlib.suppress(ProcessLookupError):
                os.kill(pid, signal.SIGKILL)
            while pid_alive(pid) and loop.time() <= deadline:
                await asyncio.sleep(0.1)
            if pid_alive(pid):
                msg = f"failed to stop legacy BuildKit daemon with PID {pid}"
                raise OSError(msg)

        BUILDKIT_PID_FILE.unlink(missing_ok=True)
        BUILDKIT_SOCKET.unlink(missing_ok=True)

