"""External CLI endpoint for running Bertrand workloads."""

from __future__ import annotations

import asyncio
import contextlib
import json
import os
import select
import shutil
import signal
import sys
import termios
import tty as tty_module
from typing import TYPE_CHECKING, Any, Literal

from bertrand.env.cli.external._helper import (
    _project_command_context,
)
from bertrand.env.cli.external.build import _publish_project_image
from bertrand.env.config.bertrand import Bertrand
from bertrand.env.git import BERTRAND_NAMESPACE, INFINITY
from bertrand.env.kube.build.execution import job_pod_diagnostics
from bertrand.env.kube.cronjob import CronJob
from bertrand.env.kube.deployment import Deployment
from bertrand.env.kube.pod import Pod
from bertrand.env.kube.workload.project import (
    create_project_workload_job_run,
    ensure_project_workload_controller,
)

if TYPE_CHECKING:
    from collections.abc import Awaitable, Callable, Sequence
    from pathlib import Path

    from bertrand.env.config.core import Config
    from bertrand.env.kube.api.client import Kube
    from bertrand.env.kube.job import Job

_RUN_LOG_POLL_SECONDS = 1.0
_RUN_LOG_READ_TIMEOUT_SECONDS = 5.0
_RUN_LOG_TAIL_LINES = 1000
_RUN_ATTACH_POLL_SECONDS = 0.5
_TTY_PUMP_SECONDS = 0.05
_STDIN_CHANNEL = 0
_STDOUT_CHANNEL = 1
_STDERR_CHANNEL = 2
_RESIZE_CHANNEL = 4
type _AttachMode = Literal["logs", "tty"]
type _PodSource = Callable[[float], Awaitable[Sequence[Pod]]]


async def bertrand_run(
    target: Path,
    *,
    detach: bool,
    tty: bool | None,
    args: Sequence[str],
) -> None:
    """Build and run the configured Kubernetes workload for a project target.

    Parameters
    ----------
    target : Path
        Project repository or worktree path.
    detach : bool
        Whether to return after workload submission/convergence without attaching
        foreground log streaming.
    tty : bool | None
        Foreground attachment mode. ``True`` forces TTY attachment, ``False`` forces
        log streaming, and ``None`` selects TTY attachment only when it is safe and
        the caller has an interactive terminal.
    args : Sequence[str]
        Runtime arguments to append to the configured primary container command.

    Raises
    ------
    ValueError
        If detached mode is combined with forced TTY attachment.
    """
    if detach and tty is True:
        msg = (
            "`bertrand run --detach --tty` is invalid because detached runs do not "
            "attach"
        )
        raise ValueError(msg)
    async with _project_command_context(target, timeout=INFINITY) as context:
        await run_configured_project(
            context.kube,
            config=context.config,
            repo_id=context.config.repo.repo_id,
            detach=detach,
            tty=tty,
            args=args,
            ensure_build_crds=True,
        )


async def run_configured_project(
    kube: Kube,
    *,
    config: Config,
    repo_id: str,
    detach: bool,
    tty: bool | None,
    args: Sequence[str],
    ensure_build_crds: bool,
) -> None:
    """Build and schedule the active project config on a Kubernetes cluster.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    config : Config
        Active project configuration context.
    repo_id : str
        Stable repository UUID for image and workload identity.
    detach : bool
        Whether to return after workload submission/convergence.
    tty : bool | None
        Foreground attachment mode. ``True`` forces TTY attachment, ``False`` forces
        log streaming, and ``None`` selects automatically.
    args : Sequence[str]
        Runtime arguments appended to the configured primary container command.
    ensure_build_crds : bool
        Whether to converge BuildKit/image lifecycle CRDs before submitting the
        build request. Host-side commands should enable this; in-cluster dev commands
        should rely on ``bertrand init`` and avoid CRD-definition write privileges.

    Raises
    ------
    RuntimeError
        If ``config`` is inactive.
    ValueError
        If detached TTY mode is requested or the project has no runnable workload.
    """
    if not config:
        msg = "`bertrand run` requires an active project config context"
        raise RuntimeError(msg)
    if detach and tty is True:
        msg = (
            "`bertrand run --detach --tty` is invalid because detached runs do not "
            "attach"
        )
        raise ValueError(msg)

    bertrand = config.get(Bertrand)
    if bertrand is None or not bertrand.containers:
        msg = (
            "`bertrand run` requires at least one `[[tool.bertrand.containers]]` entry"
        )
        raise ValueError(msg)

    runtime_args = tuple(args)
    topology = bertrand.topology.kind
    attach_mode = _resolve_attach_mode(
        bertrand,
        detach=detach,
        tty=tty,
    )
    primary_container = _primary_container_name(bertrand)

    publication = await _publish_project_image(
        kube,
        config=config,
        repo_id=repo_id,
        timeout=INFINITY,
        quiet=detach,
        ensure_crds=ensure_build_crds,
    )
    image_ref = publication.record.digest_ref
    interactive = attach_mode == "tty"

    if topology == "job":
        job = await create_project_workload_job_run(
            kube,
            config=config,
            repo_id=repo_id,
            timeout=INFINITY,
            image_ref=image_ref,
            primary_args=runtime_args,
            interactive=interactive,
        )
        if detach:
            print(f"job: {job.name}")
            return
        await _run_job_foreground(
            kube,
            job,
            primary_container=primary_container,
            attach_tty=interactive,
            explicit_tty=tty is True,
        )
        return

    controller = await ensure_project_workload_controller(
        kube,
        config=config,
        repo_id=repo_id,
        timeout=INFINITY,
        image_ref=image_ref,
        primary_args=runtime_args,
        interactive=interactive,
    )
    if isinstance(controller, Deployment):
        if detach:
            print(f"deployment: {controller.name}")
            return
        await _run_deployment_foreground(
            kube,
            controller,
            primary_container=primary_container,
            attach_tty=interactive,
            explicit_tty=tty is True,
        )
        return
    if isinstance(controller, CronJob):
        print(f"cronjob: {controller.name}")
        return


def _resolve_attach_mode(
    bertrand: Bertrand.Model | None,
    *,
    detach: bool,
    tty: bool | None,
) -> _AttachMode:
    if detach or tty is False:
        return "logs"
    if tty is True:
        _require_local_tty()
        _require_tty_topology(bertrand)
        return "tty"
    if not _has_local_tty():
        return "logs"
    return "tty" if _auto_tty_topology(bertrand) else "logs"


def _has_local_tty() -> bool:
    return sys.stdin.isatty() and sys.stdout.isatty()


def _require_local_tty() -> None:
    if _has_local_tty():
        return
    msg = "`bertrand run --tty` requires both stdin and stdout to be attached to a TTY"
    raise OSError(msg)


def _require_tty_topology(bertrand: Bertrand.Model | None) -> None:
    if bertrand is None or not bertrand.containers:
        msg = "`bertrand run --tty` requires a configured immediate workload"
        raise OSError(msg)
    topology = bertrand.topology.kind
    if topology == "job" and _job_has_single_attach_target(bertrand):
        return
    if topology == "deployment" and _deployment_replicas(bertrand) == 1:
        return
    if topology == "cronjob":
        msg = (
            "`bertrand run --tty` cannot attach to CronJob topology; no pod runs "
            "immediately"
        )
        raise OSError(msg)
    if topology == "deployment":
        msg = (
            "`bertrand run --tty` requires Deployment topology to have exactly one "
            "replica"
        )
        raise OSError(msg)
    msg = "`bertrand run --tty` requires a single immediate Job or Deployment pod"
    raise OSError(msg)


def _auto_tty_topology(bertrand: Bertrand.Model | None) -> bool:
    if bertrand is None or not bertrand.containers:
        return False
    topology = bertrand.topology.kind
    if topology == "job":
        return _job_has_single_attach_target(bertrand)
    return topology == "deployment" and _deployment_replicas(bertrand) == 1


def _job_has_single_attach_target(bertrand: Bertrand.Model) -> bool:
    execution = bertrand.execution
    if execution is None:
        return True
    return execution.parallelism == 1 and execution.completions in (None, 1)


def _deployment_replicas(bertrand: Bertrand.Model) -> int:
    return 1 if bertrand.scale is None else bertrand.scale.replicas


def _primary_container_name(bertrand: Bertrand.Model | None) -> str:
    if bertrand is None or not bertrand.containers:
        return ""
    return str(bertrand.containers[0].name).strip()


async def _run_job_foreground(
    kube: Kube,
    job: Job,
    *,
    primary_container: str,
    attach_tty: bool,
    explicit_tty: bool,
) -> None:
    if attach_tty:
        attached = await _try_attach_job(
            kube,
            job,
            primary_container=primary_container,
            explicit_tty=explicit_tty,
        )
        if attached:
            await _wait_job_complete(kube, job)
            return
    follower = _WorkloadLogFollower(
        kube,
        source=lambda remaining: job.pods(kube, timeout=remaining),
    )
    await follower.start()
    try:
        await _wait_job_complete(kube, job)
    finally:
        await follower.poll()
        await follower.close()


async def _wait_job_complete(kube: Kube, job: Job) -> None:
    try:
        await job.wait_complete(kube, timeout=INFINITY)
    except (OSError, TimeoutError) as err:
        diagnostics = await job_pod_diagnostics(
            kube,
            job,
            timeout=_RUN_LOG_READ_TIMEOUT_SECONDS,
            failure_label="workload Job pod status diagnostics",
        )
        diagnostics = diagnostics.strip()
        if diagnostics:
            msg = f"{err}\n\nPod status:\n{diagnostics}"
            if isinstance(err, TimeoutError):
                raise TimeoutError(msg) from err
            raise OSError(msg) from err
        raise


async def _try_attach_job(
    kube: Kube,
    job: Job,
    *,
    primary_container: str,
    explicit_tty: bool,
) -> bool:
    try:
        pod = await _wait_job_attach_pod(
            kube,
            job,
            primary_container=primary_container,
        )
    except (OSError, TimeoutError) as err:
        if explicit_tty:
            raise
        print(
            f"bertrand: interactive attach unavailable ({err}); falling back to logs",
            file=sys.stderr,
        )
        return False
    if pod is None:
        if explicit_tty:
            msg = f"Job {job.name} finished before its primary container was attachable"
            raise OSError(msg)
        return False
    try:
        await _attach_pod(kube, pod, primary_container=primary_container)
    except (OSError, TimeoutError) as err:
        if explicit_tty:
            raise
        print(
            f"bertrand: interactive attach unavailable ({err}); falling back to logs",
            file=sys.stderr,
        )
        return False
    else:
        return True


async def _wait_job_attach_pod(
    kube: Kube,
    job: Job,
    *,
    primary_container: str,
) -> Pod | None:
    while True:
        pods = await job.pods(kube, timeout=_RUN_LOG_READ_TIMEOUT_SECONDS)
        candidates = _attachable_pods(pods, primary_container=primary_container)
        if candidates:
            return candidates[0]
        if pods and all(pod.is_terminal for pod in pods):
            return None
        await asyncio.sleep(_RUN_ATTACH_POLL_SECONDS)


async def _run_deployment_foreground(
    kube: Kube,
    deployment: Deployment,
    *,
    primary_container: str,
    attach_tty: bool,
    explicit_tty: bool,
) -> None:
    replicas = deployment.replicas
    if replicas <= 0:
        print(f"deployment: {deployment.name} (0 replicas)", file=sys.stderr)
        return
    deployment = await deployment.wait_rollout(
        kube,
        timeout=INFINITY,
        minimum=replicas,
    )
    if attach_tty:
        attached = await _try_attach_deployment(
            kube,
            deployment,
            primary_container=primary_container,
            explicit_tty=explicit_tty,
        )
        if attached:
            return
    follower = _WorkloadLogFollower(
        kube,
        source=lambda remaining: Pod.list(
            kube,
            namespaces=(BERTRAND_NAMESPACE,),
            labels=deployment.selector,
            timeout=remaining,
        ),
    )
    await follower.follow_forever()


async def _try_attach_deployment(
    kube: Kube,
    deployment: Deployment,
    *,
    primary_container: str,
    explicit_tty: bool,
) -> bool:
    try:
        pod = await _deployment_attach_pod(
            kube,
            deployment,
            primary_container=primary_container,
        )
    except (OSError, TimeoutError) as err:
        if explicit_tty:
            raise
        print(
            f"bertrand: interactive attach unavailable ({err}); falling back to logs",
            file=sys.stderr,
        )
        return False
    if pod is None:
        if explicit_tty:
            msg = (
                f"Deployment {deployment.name} does not have exactly one "
                "ready attachable pod"
            )
            raise OSError(msg)
        return False
    try:
        await _attach_pod(kube, pod, primary_container=primary_container)
    except (OSError, TimeoutError) as err:
        if explicit_tty:
            raise
        print(
            f"bertrand: interactive attach unavailable ({err}); falling back to logs",
            file=sys.stderr,
        )
        return False
    else:
        return True


async def _deployment_attach_pod(
    kube: Kube,
    deployment: Deployment,
    *,
    primary_container: str,
) -> Pod | None:
    pods = await Pod.list(
        kube,
        namespaces=(BERTRAND_NAMESPACE,),
        labels=deployment.selector,
        timeout=_RUN_LOG_READ_TIMEOUT_SECONDS,
    )
    candidates = tuple(
        pod
        for pod in _attachable_pods(pods, primary_container=primary_container)
        if pod.is_ready
    )
    if len(candidates) != 1:
        return None
    return candidates[0]


def _attachable_pods(
    pods: Sequence[Pod],
    *,
    primary_container: str,
) -> tuple[Pod, ...]:
    if not primary_container:
        return ()
    return tuple(
        pod
        for pod in pods
        if pod.is_active
        and pod.phase == "Running"
        and pod.container_running(primary_container)
    )


async def _attach_pod(
    kube: Kube,
    pod: Pod,
    *,
    primary_container: str,
) -> None:
    stream = await pod.attach(
        kube,
        timeout=_RUN_LOG_READ_TIMEOUT_SECONDS,
        container=primary_container,
        stdin=True,
        stdout=True,
        stderr=True,
        tty=True,
    )
    _TTYAttachSession(stream).run()


class _TTYAttachSession:
    """Bridge a Kubernetes attach websocket to the local terminal.

    Parameters
    ----------
    stream : Any
        Kubernetes websocket client returned by the stream helper.
    """

    def __init__(self, stream: Any) -> None:
        self._stream = stream
        self._stdin_fd = sys.stdin.fileno()
        self._stdout_fd = sys.stdout.fileno()
        self._stderr_fd = sys.stderr.fileno()

    def run(self) -> None:
        """Pump local terminal I/O until the Kubernetes stream closes."""
        old_terminal = termios.tcgetattr(self._stdin_fd)
        old_sigwinch = signal.getsignal(signal.SIGWINCH)
        try:
            tty_module.setraw(self._stdin_fd)
            signal.signal(signal.SIGWINCH, self._resize)
            self._resize()
            while self._stream.is_open():
                self._stream.update(timeout=0)
                self._drain_output()
                try:
                    readable, _, _ = select.select(
                        (self._stdin_fd,),
                        (),
                        (),
                        _TTY_PUMP_SECONDS,
                    )
                except InterruptedError:
                    continue
                if self._stdin_fd not in readable:
                    continue
                data = os.read(self._stdin_fd, 4096)
                if not data:
                    break
                self._stream.write_channel(_STDIN_CHANNEL, data)
            self._stream.update(timeout=0)
            self._drain_output()
        finally:
            with contextlib.suppress(Exception):
                self._stream.close()
            signal.signal(signal.SIGWINCH, old_sigwinch)
            termios.tcsetattr(self._stdin_fd, termios.TCSADRAIN, old_terminal)

    def _resize(self, _signum: int | None = None, _frame: object | None = None) -> None:
        size = shutil.get_terminal_size(fallback=(80, 24))
        payload = json.dumps({"Width": size.columns, "Height": size.lines}).encode()
        with contextlib.suppress(Exception):
            self._stream.write_channel(_RESIZE_CHANNEL, payload)

    def _drain_output(self) -> None:
        self._write_channel(_STDOUT_CHANNEL, self._stdout_fd)
        self._write_channel(_STDERR_CHANNEL, self._stderr_fd)

    def _write_channel(self, channel: int, fd: int) -> None:
        data = self._stream.read_channel(channel)
        if not data:
            return
        if isinstance(data, str):
            data = data.encode("utf-8", errors="replace")
        os.write(fd, data)


class _WorkloadLogFollower:
    """Best-effort foreground log follower for workload Pods.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    source : Callable[[float], Awaitable[Sequence[Pod]]]
        Async callback that lists the Pods whose logs should be streamed.
    """

    def __init__(self, kube: Kube, *, source: _PodSource) -> None:
        self._kube = kube
        self._source = source
        self._task: asyncio.Task[None] | None = None
        self._printed: dict[str, str] = {}

    async def start(self) -> None:
        """Start log following in the background."""
        if self._task is None:
            self._task = asyncio.create_task(self._run())

    async def follow_forever(self) -> None:
        """Follow logs until the caller interrupts the process."""
        await self.start()
        try:
            while True:
                await asyncio.sleep(_RUN_LOG_POLL_SECONDS)
        finally:
            await self.close()

    async def close(self) -> None:
        """Stop background log following."""
        task = self._task
        self._task = None
        if task is not None:
            task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await task

    async def poll(self) -> None:
        """Read and print one best-effort log snapshot."""
        try:
            pods = await self._source(_RUN_LOG_READ_TIMEOUT_SECONDS)
            include_headers = len(pods) > 1
            for pod in pods:
                log = await pod.logs(
                    self._kube,
                    timeout=_RUN_LOG_READ_TIMEOUT_SECONDS,
                    tail_lines=_RUN_LOG_TAIL_LINES,
                )
                self._print_new(pod, log, include_header=include_headers)
        except (OSError, TimeoutError, ValueError):
            return

    async def _run(self) -> None:
        while True:
            await self.poll()
            await asyncio.sleep(_RUN_LOG_POLL_SECONDS)

    def _print_new(self, pod: Pod, log: str, *, include_header: bool) -> None:
        log = log.strip()
        if not log:
            return
        key = f"{pod.namespace}/{pod.name}"
        previous = self._printed.get(key, "")
        if log == previous:
            return
        if previous and log.startswith(previous):
            chunk = log[len(previous) :].lstrip("\n")
        else:
            chunk = log
        self._printed[key] = log
        if not chunk:
            return
        if include_header:
            print(f"--- {key} ---", flush=True)
        print(chunk, flush=True)
