"""k3s bootstrap helpers for Bertrand's owned Kubernetes API substrate."""

from __future__ import annotations

import contextlib
import ipaddress
import json
import os
import shutil
import tempfile
from collections.abc import Mapping
from pathlib import Path
from typing import Literal, cast
from urllib.parse import urlparse, urlunparse

import yaml

from bertrand.env.git import (
    BERTRAND_LABEL,
    BERTRAND_LABEL_MANAGED,
    BERTRAND_NAMESPACE,
    NO_DEADLINE,
    CommandError,
    CompletedProcess,
    Deadline,
    HostLock,
    TimeoutExpired,
    atomic_write_text,
    confirm,
    run,
    sudo,
    until,
)
from bertrand.env.host import BERTRAND_GROUP, HOST_ID_FILE, RUN_DIR, STATE_DIR

type K3sRole = Literal["server", "agent"]

KUBE_CONFIG_FILE = STATE_DIR / "kubeconfig"
K3S_CONTEXT = "bertrand"
K3S_INSTALL_URL = "https://get.k3s.io"
K3S_CHANNEL = "stable"
K3S_SERVICE_BASENAME = "bertrand"
K3S_SERVICE_NAME = "k3s-bertrand"
K3S_API_PORT = 16443
K3S_BIN_DIR = STATE_DIR / "bin"
K3S_BINARY = K3S_BIN_DIR / "k3s"
K3S_UNINSTALL = K3S_BIN_DIR / f"{K3S_SERVICE_NAME}-uninstall.sh"
K3S_DATA_DIR = STATE_DIR / "k3s"
K3S_REGISTRIES_FILE = STATE_DIR / "k3s-registries.yaml"
K3S_ROLE_FILE = STATE_DIR / "k3s.role"
KUBE_LOCK_FILE = RUN_DIR / "k3s.lock"
K3S_ROLES: tuple[K3sRole, ...] = ("server", "agent")


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
        If the payload is malformed or does not describe Bertrand's k3s context.
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
    if current_context != K3S_CONTEXT:
        msg = (
            f"{source} kubeconfig must use current-context {K3S_CONTEXT!r}, "
            f"got {current_context!r}"
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


def normalize_k3s_role(role: str) -> K3sRole:
    """Return a normalized k3s node role.

    Returns
    -------
    K3sRole
        Normalized k3s role.

    Raises
    ------
    ValueError
        If the role is neither `server` nor `agent`.
    """
    normalized = role.strip().lower()
    if normalized in K3S_ROLES:
        return cast("K3sRole", normalized)
    msg = f"k3s role must be one of {', '.join(K3S_ROLES)}, got {role!r}"
    raise ValueError(msg)


def k3s_node_name() -> str:
    """Return the deterministic Kubernetes node name for this host.

    Returns
    -------
    str
        Kubernetes node name derived from the local Bertrand host ID.

    Raises
    ------
    OSError
        If the local host ID file is missing or empty.
    """
    try:
        host_id = HOST_ID_FILE.read_text(encoding="utf-8").strip()
    except OSError as err:
        msg = f"failed to read Bertrand host identity at {HOST_ID_FILE}"
        raise OSError(msg) from err
    if not host_id:
        msg = f"Bertrand host identity at {HOST_ID_FILE} is empty"
        raise OSError(msg)
    return f"bertrand-{host_id[:32]}"


def k3s_config_payload(*, source: str = "managed kubeconfig") -> str:
    """Return the current Bertrand-managed k3s kubeconfig payload.

    Returns
    -------
    str
        Kubeconfig YAML payload.

    Raises
    ------
    OSError
        If the kubeconfig is missing or empty.
    """
    try:
        text = KUBE_CONFIG_FILE.read_text(encoding="utf-8").strip()
    except OSError as err:
        msg = f"failed to read {source} at {KUBE_CONFIG_FILE}: {err}"
        raise OSError(msg) from err
    if not text:
        msg = f"{source} at {KUBE_CONFIG_FILE} is empty"
        raise OSError(msg)
    return text if text.endswith("\n") else f"{text}\n"


async def kubectl(
    argv: list[str],
    *,
    check: bool = True,
    capture_output: bool | None = False,
    stdin: str | None = None,
    deadline: Deadline = NO_DEADLINE,
    attempts: int = 1,
    delay: float = 0.1,
    cwd: Path | None = None,
    env: Mapping[str, str] | None = None,
) -> CompletedProcess:
    """Invoke the Bertrand-owned k3s kubectl client.

    Parameters
    ----------
    argv : list[str]
        `kubectl` arguments without the `k3s kubectl` prefix.
    check : bool, optional
        Whether nonzero command exits raise `CommandError`.
    capture_output : bool | None, optional
        Whether to capture, inherit, or tee subprocess output.
    stdin : str | None, optional
        Optional text to pass to command stdin.
    deadline : Deadline, optional
        Maximum command runtime.
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

    Raises
    ------
    OSError
        If the managed k3s binary is not installed.
    """
    if not K3S_BINARY.is_file():
        msg = f"Bertrand k3s binary is missing at {K3S_BINARY}; run `bertrand init`"
        raise OSError(msg)
    return await run(
        [
            str(K3S_BINARY),
            "kubectl",
            "--kubeconfig",
            str(KUBE_CONFIG_FILE),
            *argv,
        ],
        check=check,
        capture_output=capture_output,
        input=stdin,
        deadline=deadline,
        attempts=attempts,
        delay=delay,
        cwd=cwd,
        env=env,
    )


async def _systemctl(
    *args: str,
    check: bool = True,
    deadline: Deadline,
) -> CompletedProcess:
    return await run(
        sudo(["systemctl", *args]),
        check=check,
        capture_output=True,
        deadline=deadline,
    )


async def k3s_service_active(*, deadline: Deadline) -> bool:
    """Return whether Bertrand's k3s systemd service is active.

    Returns
    -------
    bool
        Whether the managed k3s systemd unit is active.
    """
    if not shutil.which("systemctl"):
        return False
    result = await _systemctl(
        "is-active",
        "--quiet",
        K3S_SERVICE_NAME,
        check=False,
        deadline=deadline,
    )
    return result.returncode == 0


async def k3s_cluster_ready(*, deadline: Deadline) -> bool:
    """Return whether the managed k3s API reports ready.

    Returns
    -------
    bool
        Whether the managed k3s API responds successfully to `/readyz`.
    """
    if not K3S_BINARY.is_file() or not KUBE_CONFIG_FILE.is_file():
        return False
    return (
        await kubectl(
            ["get", "--raw=/readyz"],
            check=False,
            capture_output=True,
            deadline=deadline,
        )
    ).returncode == 0


async def _wait_k3s_ready(
    *,
    deadline: Deadline,
    interval: float,
    message: str,
    action: str,
) -> None:
    async def ready(attempt_deadline: Deadline) -> None:
        if await k3s_cluster_ready(deadline=attempt_deadline):
            return
        raise TimeoutError(message)

    try:
        await until(ready, deadline=deadline, delay=interval)
    except TimeoutError as err:
        raise TimeoutError(action) from err


async def _add_bertrand_kube_namespace(*, deadline: Deadline) -> None:
    manifest = {
        "apiVersion": "v1",
        "kind": "Namespace",
        "metadata": {
            "name": BERTRAND_NAMESPACE,
            "labels": {
                BERTRAND_LABEL: BERTRAND_LABEL_MANAGED,
                "app.kubernetes.io/part-of": "bertrand",
            },
        },
    }
    await kubectl(
        ["apply", "--server-side", "-f", "-"],
        stdin=json.dumps(manifest, separators=(",", ":")),
        capture_output=True,
        deadline=deadline,
    )


def _normalize_server_url(value: str) -> str:
    text = value.strip()
    if not text:
        msg = "k3s server URL cannot be empty"
        raise ValueError(msg)
    if "://" not in text:
        text = f"https://{text}"
    parsed = urlparse(text)
    if parsed.scheme != "https" or not parsed.hostname:
        msg = f"k3s server URL must be an HTTPS host:port URL, got {value!r}"
        raise ValueError(msg)
    netloc = parsed.netloc
    if parsed.port is None:
        netloc = f"{parsed.hostname}:{K3S_API_PORT}"
    return urlunparse(("https", netloc, "", "", "", ""))


def _server_is_loopback(server_url: str) -> bool:
    host = urlparse(server_url).hostname or ""
    if host.lower() in {"localhost", "localhost.localdomain"}:
        return True
    try:
        return ipaddress.ip_address(host).is_loopback
    except ValueError:
        return False


def _kubeconfig_with_server(payload: str, server_url: str) -> str:
    raw = yaml.safe_load(payload)
    if not isinstance(raw, dict):
        msg = "cannot rewrite malformed kubeconfig payload"
        raise OSError(msg)
    clusters = raw.get("clusters")
    if not isinstance(clusters, list):
        msg = "cannot rewrite kubeconfig without cluster list"
        raise OSError(msg)
    for entry in clusters:
        if not isinstance(entry, dict):
            continue
        cluster = entry.get("cluster")
        if isinstance(cluster, dict):
            cluster["server"] = server_url
    raw["current-context"] = K3S_CONTEXT
    return yaml.safe_dump(raw, sort_keys=False)


async def _normalize_file_permissions(path: Path, *, deadline: Deadline) -> None:
    if not path.exists():
        return
    await run(
        sudo(["chgrp", BERTRAND_GROUP, str(path)]),
        check=False,
        capture_output=True,
        deadline=deadline,
    )
    await run(
        sudo(["chmod", "0640", str(path)]),
        check=False,
        capture_output=True,
        deadline=deadline,
    )


async def _normalize_kubeconfig_permissions(*, deadline: Deadline) -> None:
    await _normalize_file_permissions(KUBE_CONFIG_FILE, deadline=deadline)


def _k3s_exec_args(
    *,
    role: K3sRole,
    node_name: str,
    server_url: str | None,
    token: str | None,
) -> list[str]:
    common = [
        "--node-name",
        node_name,
        "--data-dir",
        str(K3S_DATA_DIR),
        "--private-registry",
        str(K3S_REGISTRIES_FILE),
    ]
    if role == "agent":
        if not server_url or not token:
            msg = "k3s agent join requires server URL and token"
            raise ValueError(msg)
        return ["agent", *common, "--server", server_url, "--token", token]

    args = [
        "server",
        *common,
        "--https-listen-port",
        str(K3S_API_PORT),
        "--write-kubeconfig",
        str(KUBE_CONFIG_FILE),
        "--write-kubeconfig-mode",
        "0640",
        "--disable",
        "traefik",
        "--disable",
        "servicelb",
        "--disable",
        "local-storage",
    ]
    if server_url and token:
        args.extend(["--server", server_url, "--token", token])
    else:
        args.append("--cluster-init")
    return args


async def _ensure_default_registries_config(*, deadline: Deadline) -> None:
    K3S_REGISTRIES_FILE.parent.mkdir(parents=True, exist_ok=True)
    if K3S_REGISTRIES_FILE.exists():
        return
    atomic_write_text(
        K3S_REGISTRIES_FILE,
        "mirrors: {}\nconfigs: {}\n",
        encoding="utf-8",
    )
    await _normalize_file_permissions(K3S_REGISTRIES_FILE, deadline=deadline)


async def _download_k3s_installer(*, deadline: Deadline) -> Path:
    if not shutil.which("curl"):
        msg = "k3s installation requires `curl`; install host prerequisites first"
        raise OSError(msg)
    fd, name = tempfile.mkstemp(prefix="bertrand-k3s-install.", suffix=".sh")
    os.close(fd)
    script = Path(name)
    try:
        await run(
            ["curl", "-sfL", K3S_INSTALL_URL, "-o", str(script)],
            capture_output=True,
            deadline=deadline,
        )
    except BaseException:
        script.unlink(missing_ok=True)
        raise
    else:
        return script


async def install_k3s(
    *,
    role: K3sRole = "server",
    server_url: str | None = None,
    token: str | None = None,
    assume_yes: bool,
    force: bool = False,
    deadline: Deadline,
) -> None:
    """Install or refresh Bertrand's owned k3s systemd service.

    Raises
    ------
    PermissionError
        If the user declines k3s installation.
    """
    role = normalize_k3s_role(role)
    node_name = k3s_node_name()
    if (
        not force
        and await k3s_service_active(deadline=deadline)
        and K3S_BINARY.is_file()
    ):
        K3S_ROLE_FILE.parent.mkdir(parents=True, exist_ok=True)
        atomic_write_text(K3S_ROLE_FILE, f"{role}\n", encoding="utf-8")
        return
    if not confirm(
        "Bertrand uses an owned k3s service as its local Kubernetes runtime. "
        f"Install or refresh {K3S_SERVICE_NAME!r} now (requires sudo)?\n[y/N] ",
        assume_yes=assume_yes,
    ):
        msg = "k3s installation declined by user."
        raise PermissionError(msg)
    await _ensure_default_registries_config(deadline=deadline)
    script = await _download_k3s_installer(deadline=deadline)
    try:
        exec_args = _k3s_exec_args(
            role=role,
            node_name=node_name,
            server_url=server_url,
            token=token,
        )
        env = {
            "INSTALL_K3S_NAME": K3S_SERVICE_BASENAME,
            "INSTALL_K3S_BIN_DIR": str(K3S_BIN_DIR),
            "INSTALL_K3S_CHANNEL": K3S_CHANNEL,
            "INSTALL_K3S_SYMLINK": "skip",
            "INSTALL_K3S_EXEC": " ".join(exec_args),
        }
        await run(
            sudo(
                [
                    "env",
                    *(f"{key}={value}" for key, value in env.items()),
                    "sh",
                    str(script),
                ]
            ),
            capture_output=None,
            deadline=deadline,
        )
    finally:
        script.unlink(missing_ok=True)

    K3S_ROLE_FILE.parent.mkdir(parents=True, exist_ok=True)
    atomic_write_text(K3S_ROLE_FILE, f"{role}\n", encoding="utf-8")
    await _normalize_kubeconfig_permissions(deadline=deadline)


def assert_k3s_installed() -> None:
    """Raise with actionable diagnostics when the managed k3s runtime is unusable.

    Raises
    ------
    OSError
        If the managed k3s binary or kubeconfig is missing.
    """
    if not K3S_BINARY.is_file():
        msg = f"Bertrand k3s binary is missing at {K3S_BINARY}; rerun `bertrand init`"
        raise OSError(msg)
    if not KUBE_CONFIG_FILE.is_file():
        msg = (
            f"Bertrand kubeconfig is missing at {KUBE_CONFIG_FILE}; rerun "
            "`bertrand init` or `bertrand cluster join`"
        )
        raise OSError(msg)


async def start_k3s(*, deadline: Deadline) -> None:
    """Ensure Bertrand's owned k3s service is running and namespace exists.

    Raises
    ------
    OSError
        If the managed k3s runtime cannot be started.
    TimeoutError
        If the managed k3s API does not become ready before the deadline.
    """
    assert_k3s_installed()
    if await k3s_cluster_ready(deadline=deadline):
        await _add_bertrand_kube_namespace(deadline=deadline)
        return

    lock = HostLock(KUBE_LOCK_FILE)
    try:
        await lock.lock(deadline)
        try:
            if await k3s_cluster_ready(deadline=deadline):
                await _add_bertrand_kube_namespace(deadline=deadline)
                return

            await _systemctl("start", K3S_SERVICE_NAME, deadline=deadline)
            try:
                await _wait_k3s_ready(
                    deadline=deadline,
                    interval=0.25,
                    message="k3s is not ready yet",
                    action="waiting for k3s to become ready",
                )
            except TimeoutError as err:
                msg = (
                    "timed out waiting for k3s to become ready after "
                    f"{deadline.timeout} seconds"
                )
                raise TimeoutError(msg) from err
            await _add_bertrand_kube_namespace(deadline=deadline)
            return
        finally:
            await lock.unlock(ignore_errors=True)
    except TimeoutExpired as err:
        msg = (
            "timed out waiting for k3s to become ready after "
            f"{deadline.timeout} seconds"
        )
        raise TimeoutError(msg) from err
    except CommandError as err:
        msg = (
            "Failed to start Bertrand's k3s service. Re-run `bertrand init` to "
            f"repair the managed runtime.\n{err}"
        )
        raise OSError(msg) from err


async def k3s_join_bundle(
    *,
    server_url: str | None,
    deadline: Deadline,
) -> tuple[str, str, str]:
    """Return `(server_url, token, kubeconfig)` for a joining node.

    Returns
    -------
    tuple[str, str, str]
        Reachable server URL, k3s join token, and rewritten kubeconfig payload.

    Raises
    ------
    OSError
        If k3s is not ready, the API URL is not reachable by peers, or the join token
        cannot be read.
    """
    if not await k3s_cluster_ready(deadline=deadline):
        msg = "k3s must be running before generating a join token"
        raise OSError(msg)
    payload = k3s_config_payload()
    default_server, _ca = kubeconfig_identity(payload, source="managed kubeconfig")
    explicit_server = server_url is not None and bool(server_url.strip())
    chosen_source = (
        server_url.strip() if explicit_server and server_url else default_server
    )
    chosen_server = _normalize_server_url(chosen_source)
    if not explicit_server and _server_is_loopback(chosen_server):
        msg = (
            "managed kubeconfig points at a loopback k3s API endpoint. Re-run "
            "`bertrand cluster invite` with --server-url https://HOST:PORT for a "
            "reachable control-plane address."
        )
        raise OSError(msg)
    token_file = K3S_DATA_DIR / "server" / "token"
    result = await run(
        sudo(["cat", str(token_file)]),
        capture_output=True,
        deadline=deadline,
    )
    token = result.stdout.strip()
    if not token:
        msg = f"k3s server token is empty at {token_file}"
        raise OSError(msg)
    return chosen_server, token, _kubeconfig_with_server(payload, chosen_server)


async def join_k3s_cluster(
    *,
    server_url: str,
    token: str,
    role: K3sRole,
    kubeconfig: str,
    assume_yes: bool,
    deadline: Deadline,
) -> None:
    """Join the local host to an existing Bertrand k3s cluster.

    Raises
    ------
    OSError
        If the local host already has a ready k3s cluster or the join payload is
        invalid.
    """
    if await k3s_cluster_ready(deadline=deadline):
        msg = (
            "local k3s already reports a ready cluster; refusing to join it to "
            "another cluster. Run `bertrand clean --force` first if this host should "
            "join a different Bertrand cluster."
        )
        raise OSError(msg)
    role = normalize_k3s_role(role)
    server = _normalize_server_url(server_url)
    token = token.strip()
    if not token:
        msg = "k3s join token cannot be empty"
        raise OSError(msg)
    kubeconfig_identity(kubeconfig, source="join bundle kubeconfig")
    lock = HostLock(KUBE_LOCK_FILE)
    await lock.lock(deadline)
    try:
        atomic_write_text(
            KUBE_CONFIG_FILE,
            _kubeconfig_with_server(kubeconfig, server),
            encoding="utf-8",
            private=True,
        )
        await install_k3s(
            role=role,
            server_url=server,
            token=token,
            assume_yes=assume_yes,
            force=True,
            deadline=deadline,
        )
        await _wait_k3s_ready(
            deadline=deadline,
            interval=0.5,
            message="k3s has not joined the cluster yet",
            action="waiting for k3s cluster join",
        )
    finally:
        await lock.unlock(ignore_errors=True)


async def ensure_k3s_kubeconfig(*, deadline: Deadline) -> Path:
    """Return Bertrand's managed k3s kubeconfig path after validating it.

    Returns
    -------
    Path
        Path to Bertrand's managed kubeconfig.
    """
    assert_k3s_installed()
    payload = k3s_config_payload()
    kubeconfig_identity(payload, source=f"managed kubeconfig {KUBE_CONFIG_FILE}")
    await _normalize_kubeconfig_permissions(deadline=deadline)
    return KUBE_CONFIG_FILE


async def configure_k3s_registries(
    *,
    hosts: tuple[str, ...],
    deadline: Deadline,
) -> None:
    """Converge k3s/containerd registry mirror configuration for local pulls."""
    mirrors = {
        host: {"endpoint": [f"http://{host}"]}
        for host in sorted({item.strip() for item in hosts if item.strip()})
    }
    payload = yaml.safe_dump({"mirrors": mirrors}, sort_keys=True)
    current = ""
    with contextlib.suppress(OSError):
        current = K3S_REGISTRIES_FILE.read_text(encoding="utf-8")
    if current == payload:
        return
    atomic_write_text(K3S_REGISTRIES_FILE, payload, encoding="utf-8")
    await _normalize_file_permissions(K3S_REGISTRIES_FILE, deadline=deadline)
    if await k3s_service_active(deadline=deadline):
        await _systemctl("restart", K3S_SERVICE_NAME, deadline=deadline)


async def uninstall_k3s(*, deadline: Deadline) -> None:
    """Uninstall Bertrand's owned k3s runtime from this host."""
    if K3S_UNINSTALL.is_file():
        await run(
            sudo([str(K3S_UNINSTALL)]),
            check=False,
            capture_output=True,
            deadline=deadline,
        )
    elif shutil.which("systemctl"):
        await _systemctl(
            "disable",
            "--now",
            K3S_SERVICE_NAME,
            check=False,
            deadline=deadline,
        )
        unit = Path("/etc/systemd/system") / f"{K3S_SERVICE_NAME}.service"
        await run(
            sudo(["rm", "-f", str(unit)]),
            check=False,
            capture_output=True,
            deadline=deadline,
        )
        await _systemctl("daemon-reload", check=False, deadline=deadline)
