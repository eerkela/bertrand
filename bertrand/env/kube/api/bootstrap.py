"""k0s bootstrap helpers for Bertrand's owned Kubernetes API substrate."""

from __future__ import annotations

import base64
import contextlib
import ipaddress
import json
import os
import platform
import shutil
import tempfile
from collections.abc import Mapping
from pathlib import Path
from typing import Literal, cast
from urllib.parse import urlparse, urlunparse

import yaml

from bertrand.env.git import (
    BERTRAND_GROUP,
    BERTRAND_LABEL,
    BERTRAND_LABEL_MANAGED,
    BERTRAND_NAMESPACE,
    HOST_ID_FILE,
    NO_DEADLINE,
    RUN_DIR,
    STATE_DIR,
    CommandError,
    CompletedProcess,
    Deadline,
    HostLock,
    TimeoutExpired,
    confirm,
    run,
    sudo,
    until,
)
from bertrand.env.host.state import (
    STATE_DIR_MODE,
    install_state_dir,
    normalize_state_executable,
    normalize_state_file,
    write_state_file,
)

type K0sRole = Literal["controller", "worker"]

KUBE_CONFIG_FILE = STATE_DIR / "kubeconfig"
K0S_CONTEXT = "bertrand"
K0S_VERSION = "v1.35.4+k0s.0"
K0S_SERVICE_NAME = "bertrand-k0s"
K0S_API_PORT = 16443
K0S_CONTROLLER_API_PORT = 19443
K0S_BIN_DIR = STATE_DIR / "bin"
K0S_BINARY = K0S_BIN_DIR / "k0s"
K0S_DATA_DIR = STATE_DIR / "k0s"
K0S_CONFIG_FILE = STATE_DIR / "k0s.yaml"
K0S_ROLE_FILE = STATE_DIR / "k0s.role"
K0S_JOIN_TOKEN_FILE = STATE_DIR / "k0s.join-token"
K0S_RUNTIME_DIR = RUN_DIR / "k0s"
K0S_CONTAINERD_DROPIN_DIR = Path("/etc/k0s/containerd.d")
K0S_CONTAINERD_CERTS_DIR = K0S_CONTAINERD_DROPIN_DIR / "certs.d"
K0S_REGISTRY_DROPIN_FILE = K0S_CONTAINERD_DROPIN_DIR / "bertrand-registry.toml"
KUBE_LOCK_FILE = RUN_DIR / "k0s.lock"
K0S_ROLES: tuple[K0sRole, ...] = ("controller", "worker")


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
        If the payload is malformed or does not describe Bertrand's k0s context.
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
    if current_context != K0S_CONTEXT:
        msg = (
            f"{source} kubeconfig must use current-context {K0S_CONTEXT!r}, "
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


def normalize_k0s_role(role: str) -> K0sRole:
    """Return a normalized k0s node role.

    Returns
    -------
    K0sRole
        Normalized k0s role.

    Raises
    ------
    ValueError
        If the role is not supported.
    """
    normalized = role.strip().lower()
    if normalized in K0S_ROLES:
        return cast("K0sRole", normalized)
    msg = f"k0s role must be one of {', '.join(K0S_ROLES)}, got {role!r}"
    raise ValueError(msg)


def k0s_node_name() -> str:
    """Return the deterministic Kubernetes node name for this host.

    Returns
    -------
    str
        Kubernetes node name derived from the host ID.

    Raises
    ------
    OSError
        If the host ID is missing or empty.
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


def k0s_config_payload(*, source: str = "managed kubeconfig") -> str:
    """Return the current Bertrand-managed k0s kubeconfig payload.

    Returns
    -------
    str
        Kubeconfig payload.

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
    """Invoke the Bertrand-owned k0s kubectl client.

    Returns
    -------
    CompletedProcess
        Completed command result.

    Raises
    ------
    OSError
        If the managed k0s binary is missing.
    """
    if not K0S_BINARY.is_file():
        msg = f"Bertrand k0s binary is missing at {K0S_BINARY}; run `bertrand init`"
        raise OSError(msg)
    return await run(
        [
            str(K0S_BINARY),
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


async def k0s_service_active(*, deadline: Deadline) -> bool:
    """Return whether Bertrand's k0s systemd service is active.

    Returns
    -------
    bool
        Whether the managed k0s unit is active.
    """
    if not shutil.which("systemctl"):
        return False
    result = await _systemctl(
        "is-active",
        "--quiet",
        K0S_SERVICE_NAME,
        check=False,
        deadline=deadline,
    )
    return result.returncode == 0


async def k0s_cluster_ready(*, deadline: Deadline) -> bool:
    """Return whether the managed k0s API reports ready.

    Returns
    -------
    bool
        Whether the managed k0s API responds successfully to `/readyz`.
    """
    if not K0S_BINARY.is_file() or not KUBE_CONFIG_FILE.is_file():
        return False
    return (
        await kubectl(
            ["get", "--raw=/readyz"],
            check=False,
            capture_output=True,
            deadline=deadline,
        )
    ).returncode == 0


async def _wait_k0s_ready(
    *,
    deadline: Deadline,
    interval: float,
    message: str,
    action: str,
) -> None:
    async def ready(attempt_deadline: Deadline) -> None:
        if await k0s_service_active(
            deadline=attempt_deadline
        ) and await k0s_cluster_ready(deadline=attempt_deadline):
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
        msg = "k0s server URL cannot be empty"
        raise ValueError(msg)
    if "://" not in text:
        text = f"https://{text}"
    parsed = urlparse(text)
    if parsed.scheme != "https" or not parsed.hostname:
        msg = f"k0s server URL must be an HTTPS host:port URL, got {value!r}"
        raise ValueError(msg)
    netloc = parsed.netloc
    if parsed.port is None:
        netloc = f"{parsed.hostname}:{K0S_API_PORT}"
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
    raw["current-context"] = K0S_CONTEXT
    return yaml.safe_dump(raw, sort_keys=False)


def _token_with_server(token: str, server_url: str) -> str:
    token = token.strip()
    try:
        decoded = base64.b64decode(
            token + "=" * (-len(token) % 4),
            validate=False,
        ).decode("utf-8")
    except (ValueError, UnicodeDecodeError) as err:
        msg = "k0s join token must be a base64-encoded kubeconfig"
        raise OSError(msg) from err
    rewritten = _kubeconfig_with_server(decoded, server_url)
    return base64.b64encode(rewritten.encode("utf-8")).decode("ascii")


def _k0s_cluster_config_payload() -> str:
    payload = {
        "apiVersion": "k0s.k0sproject.io/v1beta1",
        "kind": "ClusterConfig",
        "metadata": {"name": "bertrand"},
        "spec": {
            "api": {
                "port": K0S_API_PORT,
                "k0sApiPort": K0S_CONTROLLER_API_PORT,
            },
            "storage": {"type": "etcd"},
            "network": {"provider": "calico"},
        },
    }
    return yaml.safe_dump(payload, sort_keys=False)


def _machine_arch() -> str:
    arch = platform.machine().lower()
    if arch in {"x86_64", "amd64"}:
        return "amd64"
    if arch in {"aarch64", "arm64"}:
        return "arm64"
    if arch in {"armv7l", "armv7", "armhf", "arm"}:
        return "arm"
    msg = f"unsupported architecture for k0s binary download: {arch!r}"
    raise OSError(msg)


def _k0s_download_url() -> str:
    return (
        "https://github.com/k0sproject/k0s/releases/download/"
        f"{K0S_VERSION}/k0s-{K0S_VERSION}-{_machine_arch()}"
    )


async def _install_root_file(
    path: Path,
    payload: str,
    *,
    mode: str = "0644",
    group: str = "root",
    deadline: Deadline,
) -> bool:
    with contextlib.suppress(OSError):
        if path.read_text(encoding="utf-8") == payload:
            return False
    fd: int | None = None
    temp_file: Path | None = None
    try:
        fd, name = tempfile.mkstemp(prefix="bertrand-k0s.", suffix=".tmp")
        temp_file = Path(name)
        os.write(fd, payload.encode("utf-8"))
        os.fsync(fd)
        os.close(fd)
        fd = None
        await run(
            sudo(
                [
                    "install",
                    "-D",
                    "-m",
                    mode,
                    "-o",
                    "root",
                    "-g",
                    group,
                    str(temp_file),
                    str(path),
                ]
            ),
            deadline=deadline,
        )
    finally:
        if fd is not None:
            with contextlib.suppress(OSError):
                os.close(fd)
        if temp_file is not None:
            temp_file.unlink(missing_ok=True)
    return True


async def _ensure_k0s_state_paths(
    *,
    assume_yes: bool,
    deadline: Deadline,
) -> None:
    for path in (K0S_BIN_DIR, K0S_RUNTIME_DIR):
        await install_state_dir(
            path,
            mode=STATE_DIR_MODE,
            assume_yes=assume_yes,
            deadline=deadline,
        )


async def _ensure_k0s_config(
    *,
    assume_yes: bool,
    deadline: Deadline,
) -> None:
    await _ensure_k0s_state_paths(assume_yes=assume_yes, deadline=deadline)
    await write_state_file(
        K0S_CONFIG_FILE,
        _k0s_cluster_config_payload(),
        assume_yes=assume_yes,
        deadline=deadline,
    )


async def _download_k0s_binary(
    *,
    assume_yes: bool,
    deadline: Deadline,
) -> None:
    if not shutil.which("curl"):
        msg = "k0s installation requires `curl`; install host prerequisites first"
        raise OSError(msg)
    fd, name = tempfile.mkstemp(prefix="bertrand-k0s.", suffix=".download")
    os.close(fd)
    download = Path(name)
    try:
        await run(
            ["curl", "-fL", _k0s_download_url(), "-o", str(download)],
            capture_output=True,
            deadline=deadline,
        )
        await run(
            sudo(
                [
                    "install",
                    "-D",
                    "-m",
                    "0750",
                    "-o",
                    "root",
                    "-g",
                    BERTRAND_GROUP,
                    str(download),
                    str(K0S_BINARY),
                ],
                non_interactive=assume_yes,
            ),
            deadline=deadline,
        )
        await normalize_state_executable(
            K0S_BINARY,
            assume_yes=assume_yes,
            deadline=deadline,
        )
    finally:
        download.unlink(missing_ok=True)


def _k0s_unit_text(*, role: K0sRole, token_file: Path | None) -> str:
    node_name = k0s_node_name()
    if role == "controller":
        exec_start = " ".join(
            (
                str(K0S_BINARY),
                "controller",
                "--config",
                str(K0S_CONFIG_FILE),
                "--data-dir",
                str(K0S_DATA_DIR),
                "--enable-worker",
                "--no-taints",
                f"--kubelet-extra-args=--hostname-override={node_name}",
                *(("--token-file", str(token_file)) if token_file is not None else ()),
            )
        )
    else:
        if token_file is None:
            msg = "k0s worker join requires a token file"
            raise ValueError(msg)
        exec_start = " ".join(
            (
                str(K0S_BINARY),
                "worker",
                "--data-dir",
                str(K0S_DATA_DIR),
                "--token-file",
                str(token_file),
                f"--kubelet-extra-args=--hostname-override={node_name}",
            )
        )

    return "\n".join(
        (
            "[Unit]",
            "Description=Bertrand owned k0s Kubernetes runtime",
            "Wants=network-online.target",
            "After=network-online.target",
            "",
            "[Service]",
            "Type=simple",
            "Delegate=yes",
            "KillMode=process",
            "Restart=always",
            "RestartSec=5",
            "LimitNOFILE=1048576",
            f"ExecStart={exec_start}",
            "",
            "[Install]",
            "WantedBy=multi-user.target",
            "",
        )
    )


async def _install_k0s_unit(
    *,
    role: K0sRole,
    token_file: Path | None,
    deadline: Deadline,
) -> None:
    unit = Path("/etc/systemd/system") / f"{K0S_SERVICE_NAME}.service"
    await _install_root_file(
        unit,
        _k0s_unit_text(role=role, token_file=token_file),
        deadline=deadline,
    )
    await _systemctl("daemon-reload", deadline=deadline)
    await _systemctl("enable", K0S_SERVICE_NAME, deadline=deadline)


async def install_k0s(
    *,
    role: K0sRole = "controller",
    token: str | None = None,
    assume_yes: bool,
    force: bool = False,
    deadline: Deadline,
) -> None:
    """Install or refresh Bertrand's owned k0s systemd service.

    Raises
    ------
    OSError
        If runtime installation cannot be prepared.
    PermissionError
        If the user declines installation.
    """
    role = normalize_k0s_role(role)
    if (
        not force
        and await k0s_service_active(deadline=deadline)
        and K0S_BINARY.is_file()
        and K0S_CONFIG_FILE.is_file()
    ):
        await write_state_file(
            K0S_ROLE_FILE,
            f"{role}\n",
            assume_yes=assume_yes,
            deadline=deadline,
        )
        return
    if not confirm(
        "Bertrand uses an owned k0s service as its local Kubernetes runtime. "
        f"Install or refresh {K0S_SERVICE_NAME!r} now (requires sudo)?\n[y/N] ",
        assume_yes=assume_yes,
    ):
        msg = "k0s installation declined by user."
        raise PermissionError(msg)

    await _ensure_k0s_config(assume_yes=assume_yes, deadline=deadline)
    if force or not K0S_BINARY.is_file():
        await _download_k0s_binary(assume_yes=assume_yes, deadline=deadline)

    token_file: Path | None = None
    if token is not None:
        token = token.strip()
        if not token:
            msg = "k0s join token cannot be empty"
            raise OSError(msg)
        await write_state_file(
            K0S_JOIN_TOKEN_FILE,
            f"{token}\n",
            assume_yes=assume_yes,
            deadline=deadline,
        )
        token_file = K0S_JOIN_TOKEN_FILE

    await _install_k0s_unit(role=role, token_file=token_file, deadline=deadline)
    await write_state_file(
        K0S_ROLE_FILE,
        f"{role}\n",
        assume_yes=assume_yes,
        deadline=deadline,
    )


def assert_k0s_installed() -> None:
    """Raise with actionable diagnostics when the managed k0s runtime is unusable.

    Raises
    ------
    OSError
        If the managed binary or config is missing.
    """
    if not K0S_BINARY.is_file():
        msg = f"Bertrand k0s binary is missing at {K0S_BINARY}; rerun `bertrand init`"
        raise OSError(msg)
    if not K0S_CONFIG_FILE.is_file():
        msg = (
            f"Bertrand k0s config is missing at {K0S_CONFIG_FILE}; rerun "
            "`bertrand init`"
        )
        raise OSError(msg)


async def start_k0s(*, deadline: Deadline) -> None:
    """Ensure Bertrand's owned k0s service is running and namespace exists.

    Raises
    ------
    OSError
        If the managed runtime cannot be started.
    TimeoutError
        If the managed API does not become ready before the deadline.
    """
    assert_k0s_installed()
    if await k0s_cluster_ready(deadline=deadline):
        await _add_bertrand_kube_namespace(deadline=deadline)
        return

    lock = HostLock(KUBE_LOCK_FILE)
    try:
        await lock.lock(deadline)
        try:
            if await k0s_cluster_ready(deadline=deadline):
                await _add_bertrand_kube_namespace(deadline=deadline)
                return

            await _systemctl("start", K0S_SERVICE_NAME, deadline=deadline)
            try:
                await _wait_k0s_ready(
                    deadline=deadline,
                    interval=0.25,
                    message="k0s is not ready yet",
                    action="waiting for k0s to become ready",
                )
            except TimeoutError as err:
                msg = (
                    "timed out waiting for k0s to become ready after "
                    f"{deadline.timeout} seconds"
                )
                raise TimeoutError(msg) from err
            await normalize_state_file(KUBE_CONFIG_FILE, deadline=deadline)
            await _add_bertrand_kube_namespace(deadline=deadline)
        finally:
            await lock.unlock(ignore_errors=True)
    except TimeoutExpired as err:
        msg = (
            "timed out waiting for k0s to become ready after "
            f"{deadline.timeout} seconds"
        )
        raise TimeoutError(msg) from err
    except CommandError as err:
        msg = (
            "Failed to start Bertrand's k0s service. Re-run `bertrand init` to "
            f"repair the managed runtime.\n{err}"
        )
        raise OSError(msg) from err


async def k0s_join_bundle(
    *,
    role: K0sRole,
    server_url: str | None,
    deadline: Deadline,
) -> tuple[str, str, str]:
    """Return `(server_url, token, kubeconfig)` for a joining node.

    Returns
    -------
    tuple[str, str, str]
        Reachable server URL, rewritten join token, and rewritten kubeconfig.

    Raises
    ------
    OSError
        If k0s is not ready or a join token cannot be generated.
    """
    role = normalize_k0s_role(role)
    if not await k0s_cluster_ready(deadline=deadline):
        msg = "k0s must be running before generating a join token"
        raise OSError(msg)
    payload = k0s_config_payload()
    default_server, _ca = kubeconfig_identity(payload, source="managed kubeconfig")
    explicit_server = server_url is not None and bool(server_url.strip())
    chosen_source = (
        server_url.strip() if explicit_server and server_url else default_server
    )
    chosen_server = _normalize_server_url(chosen_source)
    if not explicit_server and _server_is_loopback(chosen_server):
        msg = (
            "managed kubeconfig points at a loopback k0s API endpoint. Re-run "
            "`bertrand cluster invite` with --server-url https://HOST:PORT for a "
            "reachable controller address."
        )
        raise OSError(msg)
    result = await run(
        sudo(
            [
                str(K0S_BINARY),
                "token",
                "create",
                "--role",
                role,
                "--config",
                str(K0S_CONFIG_FILE),
                "--data-dir",
                str(K0S_DATA_DIR),
            ]
        ),
        capture_output=True,
        deadline=deadline,
    )
    token = result.stdout.strip()
    if not token:
        msg = "k0s token create returned an empty join token"
        raise OSError(msg)
    return (
        chosen_server,
        _token_with_server(token, chosen_server),
        _kubeconfig_with_server(payload, chosen_server),
    )


async def join_k0s_cluster(
    *,
    server_url: str,
    token: str,
    role: K0sRole,
    kubeconfig: str,
    assume_yes: bool,
    deadline: Deadline,
) -> None:
    """Join the local host to an existing Bertrand k0s cluster.

    Raises
    ------
    OSError
        If the local runtime is already ready or the join payload is invalid.
    """
    if await k0s_cluster_ready(deadline=deadline):
        msg = (
            "local k0s already reports a ready cluster; refusing to join it to "
            "another cluster. Run `bertrand clean --force` first if this host should "
            "join a different Bertrand cluster."
        )
        raise OSError(msg)
    role = normalize_k0s_role(role)
    server = _normalize_server_url(server_url)
    token = token.strip()
    if not token:
        msg = "k0s join token cannot be empty"
        raise OSError(msg)
    kubeconfig_identity(kubeconfig, source="join bundle kubeconfig")
    token = _token_with_server(token, server)

    lock = HostLock(KUBE_LOCK_FILE)
    await lock.lock(deadline)
    try:
        await write_state_file(
            KUBE_CONFIG_FILE,
            _kubeconfig_with_server(kubeconfig, server),
            assume_yes=assume_yes,
            deadline=deadline,
        )
        await install_k0s(
            role=role,
            token=token,
            assume_yes=assume_yes,
            force=True,
            deadline=deadline,
        )
        await _systemctl("start", K0S_SERVICE_NAME, deadline=deadline)
        await _wait_k0s_ready(
            deadline=deadline,
            interval=0.5,
            message="k0s has not joined the cluster yet",
            action="waiting for k0s cluster join",
        )
    finally:
        await lock.unlock(ignore_errors=True)


async def ensure_k0s_kubeconfig(*, deadline: Deadline) -> Path:
    """Return Bertrand's managed k0s kubeconfig path after validating it.

    Returns
    -------
    Path
        Path to Bertrand's managed kubeconfig.
    """
    assert_k0s_installed()
    payload = k0s_config_payload()
    kubeconfig_identity(payload, source=f"managed kubeconfig {KUBE_CONFIG_FILE}")
    await normalize_state_file(KUBE_CONFIG_FILE, deadline=deadline)
    return KUBE_CONFIG_FILE


def _containerd_dropin_payload() -> str:
    return "\n".join(
        (
            'version = 2',
            '',
            '[plugins."io.containerd.grpc.v1.cri".registry]',
            f'  config_path = "{K0S_CONTAINERD_CERTS_DIR}"',
            '',
        )
    )


def _hosts_toml_payload(host: str) -> str:
    endpoint = f"http://{host}"
    return "\n".join(
        (
            f'server = "{endpoint}"',
            '',
            f'[host."{endpoint}"]',
            '  capabilities = ["pull", "resolve"]',
            '',
        )
    )


async def configure_k0s_registries(
    *,
    hosts: tuple[str, ...],
    deadline: Deadline,
) -> None:
    """Converge k0s/containerd registry mirror configuration for local pulls."""
    normalized = tuple(sorted({item.strip() for item in hosts if item.strip()}))
    await run(
        sudo(
            [
                "install",
                "-d",
                "-m",
                "0755",
                "-o",
                "root",
                "-g",
                "root",
                str(K0S_CONTAINERD_DROPIN_DIR),
            ]
        ),
        deadline=deadline,
    )
    changed = await _install_root_file(
        K0S_REGISTRY_DROPIN_FILE,
        _containerd_dropin_payload(),
        deadline=deadline,
    )
    for host in normalized:
        cert_dir = K0S_CONTAINERD_CERTS_DIR / host
        await run(
            sudo(
                [
                    "install",
                    "-d",
                    "-m",
                    "0755",
                    "-o",
                    "root",
                    "-g",
                    "root",
                    str(cert_dir),
                ]
            ),
            deadline=deadline,
        )
        host_changed = await _install_root_file(
            cert_dir / "hosts.toml",
            _hosts_toml_payload(host),
            deadline=deadline,
        )
        changed = changed or host_changed
    if changed and await k0s_service_active(deadline=deadline):
        await _systemctl("restart", K0S_SERVICE_NAME, deadline=deadline)


async def uninstall_k0s(*, deadline: Deadline) -> None:
    """Uninstall Bertrand's owned k0s runtime from this host."""
    if shutil.which("systemctl"):
        await _systemctl(
            "disable",
            "--now",
            K0S_SERVICE_NAME,
            check=False,
            deadline=deadline,
        )
    if K0S_BINARY.is_file():
        await run(
            sudo(
                [
                    str(K0S_BINARY),
                    "reset",
                    "--force",
                    "--data-dir",
                    str(K0S_DATA_DIR),
                ]
            ),
            check=False,
            capture_output=True,
            deadline=deadline,
        )
    unit = Path("/etc/systemd/system") / f"{K0S_SERVICE_NAME}.service"
    await run(
        sudo(
            [
                "rm",
                "-rf",
                str(unit),
                str(K0S_DATA_DIR),
                str(K0S_RUNTIME_DIR),
                str(K0S_CONFIG_FILE),
                str(K0S_ROLE_FILE),
                str(K0S_JOIN_TOKEN_FILE),
                str(KUBE_CONFIG_FILE),
            ]
        ),
        check=False,
        capture_output=True,
        deadline=deadline,
    )
    if shutil.which("systemctl"):
        await _systemctl("daemon-reload", check=False, deadline=deadline)
