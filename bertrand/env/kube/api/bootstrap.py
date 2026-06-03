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
    BERTRAND_LABEL,
    BERTRAND_LABEL_MANAGED,
    BERTRAND_NAMESPACE,
    NO_DEADLINE,
    STATE,
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

type K0sRole = Literal["controller", "worker"]

K0S_CONTEXT = "bertrand"
K0S_VERSION = "v1.35.4+k0s.0"
K0S_SERVICE_NAME = "bertrand-k0s"
K0S_API_PORT = 16443
K0S_CONTROLLER_API_PORT = 19443
K0S_CONTAINERD_DROPIN_DIR = Path("/etc/k0s/containerd.d")
K0S_CONTAINERD_CERTS_DIR = K0S_CONTAINERD_DROPIN_DIR / "certs.d"
K0S_REGISTRY_DROPIN_FILE = K0S_CONTAINERD_DROPIN_DIR / "bertrand-registry.toml"
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
    """
    return f"bertrand-{STATE.id[:32]}"


def k0s_config_payload(
    *,
    source: str = "managed kubeconfig",
) -> str:
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
    kubeconfig = STATE.path(STATE.kubeconfig)
    try:
        text = kubeconfig.read_text(encoding="utf-8").strip()
    except OSError as err:
        msg = f"failed to read {source} at {kubeconfig}: {err}"
        raise OSError(msg) from err
    if not text:
        msg = f"{source} at {kubeconfig} is empty"
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
    k0s_binary = STATE.path(STATE.k0s_binary)
    if not k0s_binary.is_file():
        msg = f"Bertrand k0s binary is missing at {k0s_binary}; run `bertrand init`"
        raise OSError(msg)
    return await run(
        [
            str(k0s_binary),
            "kubectl",
            "--kubeconfig",
            str(STATE.path(STATE.kubeconfig)),
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
    if (
        not STATE.path(STATE.k0s_binary).is_file()
        or not STATE.path(STATE.kubeconfig).is_file()
    ):
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


async def _add_bertrand_kube_namespace(
    *,
    deadline: Deadline,
) -> None:
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


def _managed_kubeconfig_payload(
    payload: str,
    *,
    server_url: str | None = None,
) -> str:
    raw = yaml.safe_load(payload)
    if not isinstance(raw, dict):
        msg = "cannot rewrite malformed kubeconfig payload"
        raise OSError(msg)
    clusters = raw.get("clusters")
    if not isinstance(clusters, list):
        msg = "cannot rewrite kubeconfig without cluster list"
        raise OSError(msg)
    cluster_name = ""
    for entry in clusters:
        if not isinstance(entry, dict):
            continue
        if not cluster_name:
            cluster_name = str(entry.get("name") or "").strip()
        cluster = entry.get("cluster")
        if server_url is not None and isinstance(cluster, dict):
            cluster["server"] = server_url
    if not cluster_name:
        msg = "cannot rewrite kubeconfig without a named cluster"
        raise OSError(msg)

    users = raw.get("users")
    user_name = ""
    if isinstance(users, list):
        for entry in users:
            if isinstance(entry, dict):
                user_name = str(entry.get("name") or "").strip()
                if user_name:
                    break

    contexts = raw.get("contexts")
    if not isinstance(contexts, list):
        contexts = []
        raw["contexts"] = contexts
    context_payload: dict[str, str] = {"cluster": cluster_name}
    if user_name:
        context_payload["user"] = user_name
    contexts[:] = [
        entry
        for entry in contexts
        if not isinstance(entry, dict) or entry.get("name") != K0S_CONTEXT
    ]
    contexts.append({"name": K0S_CONTEXT, "context": context_payload})
    raw["current-context"] = K0S_CONTEXT
    return yaml.safe_dump(raw, sort_keys=False)


def _kubeconfig_with_server(payload: str, server_url: str) -> str:
    return _managed_kubeconfig_payload(payload, server_url=server_url)


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
    yes: bool,
    deadline: Deadline,
) -> None:
    for path in (STATE.binary_dir, STATE.k0s_runtime):
        await STATE.mkdir(path, deadline=deadline, yes=yes)


async def _ensure_k0s_config(
    *,
    yes: bool,
    deadline: Deadline,
) -> None:
    await _ensure_k0s_state_paths(yes=yes, deadline=deadline)
    await STATE.write(
        STATE.k0s_config,
        _k0s_cluster_config_payload(),
        yes=yes,
        deadline=deadline,
    )


async def _download_k0s_binary(
    *,
    yes: bool,
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
        await STATE.executable(
            download,
            STATE.k0s_binary,
            yes=yes,
            deadline=deadline,
        )
    finally:
        download.unlink(missing_ok=True)


async def _refresh_k0s_kubeconfig(*, deadline: Deadline, yes: bool) -> None:
    result = await run(
        sudo(
            [
                str(STATE.path(STATE.k0s_binary)),
                "kubeconfig",
                "admin",
                "--config",
                str(STATE.path(STATE.k0s_config)),
                "--data-dir",
                str(STATE.path(STATE.k0s_data)),
            ],
            non_interactive=yes,
        ),
        capture_output=True,
        deadline=deadline,
    )
    payload = result.stdout.strip()
    if not payload:
        msg = "k0s kubeconfig admin returned an empty kubeconfig"
        raise OSError(msg)
    await STATE.write(
        STATE.kubeconfig,
        _managed_kubeconfig_payload(payload),
        yes=yes,
        deadline=deadline,
    )


def _k0s_unit_text(
    *,
    role: K0sRole,
    token_file: Path | None,
) -> str:
    node_name = k0s_node_name()
    if role == "controller":
        exec_start = " ".join(
            (
                str(STATE.path(STATE.k0s_binary)),
                "controller",
                "--config",
                str(STATE.path(STATE.k0s_config)),
                "--data-dir",
                str(STATE.path(STATE.k0s_data)),
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
                str(STATE.path(STATE.k0s_binary)),
                "worker",
                "--data-dir",
                str(STATE.path(STATE.k0s_data)),
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
    yes: bool,
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
        and STATE.path(STATE.k0s_binary).is_file()
        and STATE.path(STATE.k0s_config).is_file()
    ):
        await STATE.write(
            STATE.k0s_role,
            f"{role}\n",
            yes=yes,
            deadline=deadline,
        )
        return
    if not confirm(
        "Bertrand uses an owned k0s service as its local Kubernetes runtime. "
        f"Install or refresh {K0S_SERVICE_NAME!r} now (requires sudo)?\n[y/N] ",
        yes=yes,
    ):
        msg = "k0s installation declined by user."
        raise PermissionError(msg)

    await _ensure_k0s_config(yes=yes, deadline=deadline)
    if force or not STATE.path(STATE.k0s_binary).is_file():
        await _download_k0s_binary(yes=yes, deadline=deadline)

    token_file: Path | None = None
    if token is not None:
        token = token.strip()
        if not token:
            msg = "k0s join token cannot be empty"
            raise OSError(msg)
        await STATE.write(
            STATE.k0s_token,
            f"{token}\n",
            yes=yes,
            deadline=deadline,
        )
        token_file = STATE.path(STATE.k0s_token)

    await _install_k0s_unit(
        role=role,
        token_file=token_file,
        deadline=deadline,
    )
    await STATE.write(
        STATE.k0s_role,
        f"{role}\n",
        yes=yes,
        deadline=deadline,
    )


def assert_k0s_installed() -> None:
    """Raise with actionable diagnostics when the managed k0s runtime is unusable.

    Raises
    ------
    OSError
        If the managed binary or config is missing.
    """
    k0s_binary = STATE.path(STATE.k0s_binary)
    k0s_config = STATE.path(STATE.k0s_config)
    if not k0s_binary.is_file():
        msg = f"Bertrand k0s binary is missing at {k0s_binary}; rerun `bertrand init`"
        raise OSError(msg)
    if not k0s_config.is_file():
        msg = f"Bertrand k0s config is missing at {k0s_config}; rerun `bertrand init`"
        raise OSError(msg)


async def start_k0s(*, deadline: Deadline, yes: bool) -> None:
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

    lock = HostLock(STATE.path(STATE.k0s_lock))
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
            await _refresh_k0s_kubeconfig(deadline=deadline, yes=yes)
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
                str(STATE.path(STATE.k0s_binary)),
                "token",
                "create",
                "--role",
                role,
                "--config",
                str(STATE.path(STATE.k0s_config)),
                "--data-dir",
                str(STATE.path(STATE.k0s_data)),
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
    yes: bool,
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

    lock = HostLock(STATE.path(STATE.k0s_lock))
    await lock.lock(deadline)
    try:
        await STATE.write(
            STATE.kubeconfig,
            _kubeconfig_with_server(kubeconfig, server),
            yes=yes,
            deadline=deadline,
        )
        await install_k0s(
            role=role,
            token=token,
            yes=yes,
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


def ensure_k0s_kubeconfig(
    *,
    deadline: Deadline,
) -> Path:
    """Return Bertrand's managed k0s kubeconfig path after validating it.

    Parameters
    ----------
    deadline : Deadline
        Active operation deadline.

    Returns
    -------
    Path
        Path to Bertrand's managed kubeconfig.
    """
    deadline.check("k0s kubeconfig validation timed out")
    assert_k0s_installed()
    payload = k0s_config_payload()
    kubeconfig = STATE.path(STATE.kubeconfig)
    kubeconfig_identity(payload, source=f"managed kubeconfig {kubeconfig}")
    return kubeconfig


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
    if (
        changed
        and STATE.path(STATE.k0s_binary).is_file()
        and await k0s_service_active(deadline=deadline)
    ):
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
    if STATE.path(STATE.k0s_binary).is_file():
        await run(
            sudo(
                [
                    str(STATE.path(STATE.k0s_binary)),
                    "reset",
                    "--force",
                    "--data-dir",
                    str(STATE.path(STATE.k0s_data)),
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
                str(STATE.path(STATE.k0s_data)),
                str(STATE.path(STATE.k0s_runtime)),
                str(STATE.path(STATE.k0s_config)),
                str(STATE.path(STATE.k0s_role)),
                str(STATE.path(STATE.k0s_token)),
                str(STATE.path(STATE.kubeconfig)),
            ]
        ),
        check=False,
        capture_output=True,
        deadline=deadline,
    )
    if shutil.which("systemctl"):
        await _systemctl("daemon-reload", check=False, deadline=deadline)
