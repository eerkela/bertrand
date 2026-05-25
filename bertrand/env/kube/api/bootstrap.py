"""MicroK8s bootstrap helpers for Bertrand's shared Kubernetes API substrate.

Bertrand v1 targets the supported default MicroK8s snap.  Existing clusters are
allowed and treated as shared; Bertrand scopes its state with namespaces, labels,
CRDs, and deterministic resource names rather than snap ownership.
"""

from __future__ import annotations

import re
import shutil
from collections.abc import Mapping
from typing import TYPE_CHECKING
from urllib.parse import urlparse

import yaml

from bertrand.env.git import (
    BERTRAND_NAMESPACE,
    INFINITY,
    CommandError,
    CompletedProcess,
    Deadline,
    GroupStatus,
    HostLock,
    TimeoutExpired,
    atomic_write_text,
    confirm,
    run,
    until,
)
from bertrand.env.host import RUN_DIR, STATE_DIR
from bertrand.env.host.snap import (
    ensure_snapd,
    install_or_refresh_snap,
    snap_package_ready,
)

if TYPE_CHECKING:
    from pathlib import Path

KUBE_CONFIG_FILE = STATE_DIR / "kubeconfig"
MICROK8S_KUBECONFIG_CONTEXT = "microk8s"
MICROK8S_CHANNEL = "1.33/stable"
MICROK8S_GROUP = "microk8s"
KUBE_LOCK_FILE = RUN_DIR / "microk8s.lock"
MICROK8S_JOIN_PATTERN = re.compile(r"\bmicrok8s\s+join\s+([^\s]+)(?:\s+(--worker))?")


def kubeconfig_identity(payload: str, *, source: str) -> tuple[str, str]:
    """Return the API server identity encoded in a kubeconfig payload.

    Parameters
    ----------
    payload : str
        Raw kubeconfig YAML.
    source : str
        Human-readable source label for diagnostics.

    Returns
    -------
    tuple[str, str]
        API server URL and certificate-authority data.

    Raises
    ------
    OSError
        If the payload is malformed or does not describe Bertrand's MicroK8s
        context.
    """
    try:
        raw = yaml.safe_load(payload)
    except yaml.YAMLError as err:
        msg = f"{source} is not valid kubeconfig YAML: {err}"
        raise OSError(msg) from err
    if not isinstance(raw, Mapping):
        msg = f"{source} kubeconfig must deserialize into a mapping"
        raise OSError(msg)

    current_context = str(raw.get("current-context") or "").strip()
    if not current_context:
        msg = f"{source} kubeconfig is missing 'current-context'"
        raise OSError(msg)
    if current_context != MICROK8S_KUBECONFIG_CONTEXT:
        msg = (
            f"{source} kubeconfig must use current-context "
            f"{MICROK8S_KUBECONFIG_CONTEXT!r}, got {current_context!r}"
        )
        raise OSError(msg)

    cluster_name = ""
    contexts = raw.get("contexts")
    if not isinstance(contexts, list):
        msg = f"{source} kubeconfig is missing context list"
        raise OSError(msg)
    for entry in contexts:
        if not isinstance(entry, Mapping):
            continue
        if str(entry.get("name") or "").strip() != current_context:
            continue
        context = entry.get("context")
        if isinstance(context, Mapping):
            cluster_name = str(context.get("cluster") or "").strip()
            break
    if not cluster_name:
        msg = f"{source} kubeconfig has no cluster bound to context {current_context!r}"
        raise OSError(msg)

    clusters = raw.get("clusters")
    if not isinstance(clusters, list):
        msg = f"{source} kubeconfig is missing cluster list"
        raise OSError(msg)
    cluster_payload: Mapping[str, object] | None = None
    for entry in clusters:
        if not isinstance(entry, Mapping):
            continue
        if str(entry.get("name") or "").strip() != cluster_name:
            continue
        cluster = entry.get("cluster")
        if isinstance(cluster, Mapping):
            cluster_payload = cluster
            break
    if cluster_payload is None:
        msg = f"{source} kubeconfig has no cluster payload named {cluster_name!r}"
        raise OSError(msg)

    server = str(cluster_payload.get("server") or "").strip()
    if not server:
        msg = f"{source} kubeconfig is missing cluster.server"
        raise OSError(msg)
    parsed = urlparse(server)
    if parsed.scheme != "https" or not parsed.hostname:
        msg = (
            f"{source} kubeconfig cluster.server must be a valid HTTPS URL, "
            f"got {server!r}"
        )
        raise OSError(msg)

    ca_data = str(cluster_payload.get("certificate-authority-data") or "").strip()
    if not ca_data:
        msg = f"{source} kubeconfig is missing cluster.certificate-authority-data"
        raise OSError(msg)
    return server, ca_data


async def microk8s_config_payload(*, timeout: float) -> str:
    """Return the current MicroK8s kubeconfig payload.

    Parameters
    ----------
    timeout : float
        Maximum command runtime in seconds.

    Returns
    -------
    str
        Normalized kubeconfig text ending in a newline.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    OSError
        If MicroK8s returns an empty payload.
    """
    msg = "kubeconfig timeout must be non-negative"
    if timeout <= 0:
        raise TimeoutError(msg)
    deadline = Deadline.from_timeout(timeout, message=msg)
    result = await run(
        ["microk8s", "config"],
        capture_output=True,
        timeout=deadline.remaining(),
    )
    text = result.stdout.strip()
    if not text:
        msg = "microk8s config returned an empty kubeconfig payload"
        raise OSError(msg)
    return text if text.endswith("\n") else f"{text}\n"


async def kubectl(
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
    """Invoke `microk8s kubectl` against the local MicroK8s cluster.

    Parameters
    ----------
    argv : list[str]
        `kubectl` arguments without the `microk8s kubectl` prefix.
    check : bool, optional
        Whether nonzero command exits raise `CommandError`.
    capture_output : bool | None, optional
        Whether to capture, inherit, or tee subprocess output.
    stdin : str | None, optional
        Optional text to pass to command stdin.
    timeout : float, optional
        Maximum command runtime in seconds.
    attempts : int, optional
        Number of command attempts.
    delay : float, optional
        Delay between attempts in seconds.
    cwd : Path | None, optional
        Optional working directory.
    env : Mapping[str, str] | None, optional
        Optional environment overrides.

    Returns
    -------
    CompletedProcess
        Completed command result.
    """
    return await run(
        ["microk8s", "kubectl", *argv],
        check=check,
        capture_output=capture_output,
        stdin=stdin,
        timeout=timeout,
        attempts=attempts,
        delay=delay,
        cwd=cwd,
        env=env,
    )


async def _microk8s_ready() -> bool:
    return await snap_package_ready("microk8s")


async def install_microk8s(
    *,
    package_manager: str,
    user: str,
    assume_yes: bool,
) -> None:
    """Install or refresh access to the default shared MicroK8s runtime.

    Parameters
    ----------
    package_manager : str
        Host package manager to use for installing dependencies.
    user : str
        Host username to configure for runtime group access.
    assume_yes : bool
        Whether to automatically answer yes to prompts.

    Raises
    ------
    PermissionError
        If installation requires root privileges and they are unavailable or declined.
    OSError
        If MicroK8s cannot be installed, found, or made ready.
    """
    group = GroupStatus.get(user, MICROK8S_GROUP)
    if await _microk8s_ready():
        await group.activate(assume_yes=assume_yes)
        return

    await ensure_snapd(package_manager, assume_yes=assume_yes, component="MicroK8s")
    if not await _microk8s_ready():
        if not confirm(
            "Bertrand uses the default shared MicroK8s snap as its local "
            "kubernetes control plane. Would you like to install/refresh "
            f"MicroK8s now at channel {MICROK8S_CHANNEL!r} (requires sudo)?\n"
            "[y/N] ",
            assume_yes=assume_yes,
        ):
            msg = "MicroK8s installation declined by user."
            raise PermissionError(msg)
        await install_or_refresh_snap(
            "microk8s",
            channel=MICROK8S_CHANNEL,
            assume_yes=assume_yes,
            classic=True,
            component="MicroK8s",
        )
        if not await _microk8s_ready():
            msg = (
                "MicroK8s installation completed, but the shared runtime is still "
                "not available. Check `snap list microk8s` and `microk8s --help` "
                "for diagnostics."
            )
            raise OSError(msg)

    await group.activate(assume_yes=assume_yes)


async def assert_microk8s_installed(*, user: str) -> None:
    """Raise with actionable diagnostics when MicroK8s runtime is unusable.

    Parameters
    ----------
    user : str
        Host username to check for runtime group access.

    Raises
    ------
    OSError
        If MicroK8s is not installed, not usable, or group access is missing.
    """
    if not await _microk8s_ready():
        msg = (
            "MicroK8s is installed but not usable after init bootstrap. Run "
            "`snap list microk8s` and `microk8s --help` for diagnostics."
        )
        raise OSError(msg)
    group = GroupStatus.get(user, MICROK8S_GROUP)
    if not group.configured:
        msg = (
            f"user {user!r} is not in {MICROK8S_GROUP!r}. Rerun `bertrand init` "
            "to configure MicroK8s access."
        )
        raise OSError(msg)


async def microk8s_cluster_ready(*, timeout: float) -> bool:
    """Return whether the local MicroK8s API reports ready.

    Parameters
    ----------
    timeout : float
        Maximum readiness probe runtime in seconds.

    Returns
    -------
    bool
        True when the MicroK8s readiness endpoint succeeds.
    """
    return (
        await run(
            ["microk8s", "kubectl", "get", "--raw=/readyz"],
            check=False,
            capture_output=True,
            timeout=timeout,
        )
    ).returncode == 0


async def _wait_microk8s_ready(
    *,
    timeout: float,
    interval: float,
    message: str,
    action: str,
) -> None:
    async def ready(remaining: float) -> None:
        if await microk8s_cluster_ready(timeout=remaining):
            return
        raise TimeoutError(message)

    await until(
        ready,
        timeout=timeout,
        interval=interval,
        action=action,
    )


async def _add_bertrand_kube_namespace(*, timeout: float) -> None:
    deadline = Deadline.from_timeout(
        timeout,
        message="Bertrand namespace bootstrap timeout must be non-negative",
    )
    existing = await kubectl(
        ["get", "namespace", BERTRAND_NAMESPACE, "-o", "name"],
        check=False,
        capture_output=True,
        timeout=deadline.remaining(),
    )
    if existing.returncode == 0:
        return

    created = await kubectl(
        ["create", "namespace", BERTRAND_NAMESPACE],
        check=False,
        capture_output=True,
        timeout=deadline.remaining(),
    )
    if created.returncode == 0:
        return

    raced = await kubectl(
        ["get", "namespace", BERTRAND_NAMESPACE, "-o", "name"],
        check=False,
        capture_output=True,
        timeout=deadline.remaining(),
    )
    if raced.returncode == 0:
        return
    raise CommandError(created.returncode, created.args, created.stdout, created.stderr)


async def start_microk8s(*, timeout: float) -> None:
    """Ensure that shared MicroK8s is running and Bertrand's namespace exists.

    Parameters
    ----------
    timeout : float
        Maximum startup/readiness budget in seconds.

    Raises
    ------
    TimeoutError
        If readiness checks do not succeed before `timeout`.
    OSError
        If MicroK8s is missing, startup fails, or namespace bootstrap fails.
    """
    message = "MicroK8s timeout must be non-negative."
    if timeout <= 0:
        raise TimeoutError(message)
    if not shutil.which("microk8s"):
        msg = (
            "MicroK8s CLI was not found in PATH. Run `bertrand init` to install "
            "or configure the shared runtime."
        )
        raise OSError(msg)
    deadline = Deadline.from_timeout(timeout, message=message)

    if await microk8s_cluster_ready(timeout=deadline.remaining()):
        await _add_bertrand_kube_namespace(timeout=deadline.remaining())
        return

    try:
        async with HostLock(KUBE_LOCK_FILE, timeout=deadline.remaining()):
            if await microk8s_cluster_ready(timeout=deadline.remaining()):
                await _add_bertrand_kube_namespace(timeout=deadline.remaining())
                return

            await run(
                ["microk8s", "start"],
                capture_output=True,
                timeout=deadline.remaining(),
            )

            try:
                await _wait_microk8s_ready(
                    timeout=deadline.remaining(),
                    interval=0.1,
                    message="MicroK8s is not ready yet",
                    action="waiting for MicroK8s to become ready",
                )
            except TimeoutError as err:
                msg = (
                    f"timed out waiting for MicroK8s to become ready after {timeout} "
                    "seconds"
                )
                raise TimeoutError(msg) from err
            await _add_bertrand_kube_namespace(timeout=deadline.remaining())
            return
    except TimeoutExpired as err:
        msg = f"timed out waiting for MicroK8s to become ready after {timeout} seconds"
        raise TimeoutError(msg) from err
    except CommandError as err:
        msg = (
            "Failed to start MicroK8s. You may need to re-run `bertrand init` to "
            f"ensure proper setup and group membership.\n{err}"
        )
        raise OSError(msg) from err


async def microk8s_join_token(*, worker: bool, timeout: float) -> str:
    """Generate one MicroK8s join token from this control-plane node.

    Parameters
    ----------
    worker : bool
        Whether to prefer a worker-node join command.
    timeout : float
        Maximum command runtime in seconds.

    Returns
    -------
    str
        Join target in ``host:port/token`` form.  The caller decides whether to
        append ``--worker`` when consuming the token.

    Raises
    ------
    OSError
        If MicroK8s is unavailable or no join command can be parsed.
    TimeoutError
        If `timeout` is non-positive.
    """
    msg = "MicroK8s add-node timeout must be non-negative"
    if timeout <= 0:
        raise TimeoutError(msg)
    deadline = Deadline.from_timeout(timeout, message=msg)
    if not await microk8s_cluster_ready(timeout=deadline.remaining()):
        msg = "MicroK8s must be running before generating a join token"
        raise OSError(msg)
    result = await run(
        ["microk8s", "add-node"],
        capture_output=True,
        timeout=deadline.remaining(),
    )
    output = f"{result.stdout}\n{result.stderr}"
    matches = MICROK8S_JOIN_PATTERN.findall(output)
    if not matches:
        msg = f"failed to parse MicroK8s add-node output:\n{output}"
        raise OSError(msg)
    preferred = [
        target for target, worker_flag in matches if bool(worker_flag) == worker
    ]
    return (preferred or [matches[0][0]])[0]


async def join_microk8s_cluster(
    token: str,
    *,
    worker: bool,
    timeout: float,
) -> None:
    """Join the local shared MicroK8s runtime to an existing cluster.

    Parameters
    ----------
    token : str
        MicroK8s join target in ``host:port/token`` form, or a full
        ``microk8s join`` command.
    worker : bool
        Whether to join this node as a worker.
    timeout : float
        Maximum join/readiness budget in seconds.

    Raises
    ------
    OSError
        If this host already appears to belong to a MicroK8s cluster or join fails.
    TimeoutError
        If `timeout` is non-positive.
    """
    message = "MicroK8s join timeout must be non-negative"
    if timeout <= 0:
        raise TimeoutError(message)
    deadline = Deadline.from_timeout(timeout, message=message)
    if await microk8s_cluster_ready(timeout=deadline.remaining()):
        msg = (
            "local MicroK8s already reports a ready cluster; refusing to join it to "
            "another cluster.  Remove or reset the existing MicroK8s membership "
            "outside Bertrand if this host should join a different cluster."
        )
        raise OSError(msg)
    target = token.strip()
    match = MICROK8S_JOIN_PATTERN.search(target)
    if match is not None:
        target = match.group(1)
    argv = ["microk8s", "join", target]
    if worker:
        argv.append("--worker")
    async with HostLock(KUBE_LOCK_FILE, timeout=deadline.remaining()):
        await run(argv, capture_output=True, timeout=deadline.remaining())

        await _wait_microk8s_ready(
            timeout=deadline.remaining(),
            interval=0.5,
            message="MicroK8s has not joined the cluster yet",
            action="waiting for MicroK8s cluster join",
        )


async def ensure_microk8s_kubeconfig(*, timeout: float) -> Path:
    """Converge Bertrand-managed kubeconfig from the shared MicroK8s runtime.

    Parameters
    ----------
    timeout : float
        Maximum runtime budget in seconds. If infinite, wait indefinitely.

    Returns
    -------
    Path
        The managed kubeconfig path that was converged.
    """
    deadline = Deadline.from_timeout(
        timeout,
        message="kubeconfig timeout must be non-negative",
    )
    payload = await microk8s_config_payload(timeout=deadline.remaining())

    if KUBE_CONFIG_FILE.is_file():
        try:
            if KUBE_CONFIG_FILE.read_text(encoding="utf-8") == payload:
                return KUBE_CONFIG_FILE
        except OSError:
            pass

    atomic_write_text(
        KUBE_CONFIG_FILE,
        payload,
        encoding="utf-8",
        private=True,
    )
    return KUBE_CONFIG_FILE
