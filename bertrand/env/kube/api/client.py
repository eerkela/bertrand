"""Kubernetes API client context for Bertrand runtime orchestration."""

from __future__ import annotations

import asyncio
import base64
import contextlib
import ipaddress
import json
import math
import os
import platform
import shlex
import shutil
import tempfile
from collections.abc import Mapping
from contextlib import suppress
from dataclasses import dataclass, field
from pathlib import Path
from typing import TYPE_CHECKING, Literal, Self
from urllib.parse import urlparse, urlunparse

import kubernetes
import yaml
from kubernetes.client.rest import ApiException
from kubernetes.config.config_exception import ConfigException

from bertrand.env.git import (
    BERTRAND_LABEL,
    BERTRAND_LABEL_MANAGED,
    BERTRAND_NAMESPACE,
    NO_DEADLINE,
    ROOT_DIR,
    STATE,
    CommandError,
    CompletedProcess,
    Deadline,
    HostLock,
    confirm,
    run,
    sudo,
    until,
)

if TYPE_CHECKING:
    from collections.abc import Callable

CLUSTER_REGISTRY_READY_LABEL = "bertrand.dev/registry-ready"
CLUSTER_REGISTRY_READY_VALUE = "true"
_KUBE_CONFIG_ERRORS = (ConfigException, OSError, ValueError)

type K0sRole = Literal["controller", "worker"]

K0S_CONTEXT = "bertrand"
K0S_VERSION = "v1.35.4+k0s.0"
K0S_SERVICE_NAME = "bertrand-k0s"
K0S_API_PORT = 16443
K0S_CONTROLLER_API_PORT = 19443
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


def managed_kubeconfig_payload(*, source: str = "managed kubeconfig") -> str:
    """Return the current Bertrand-managed k0s kubeconfig payload.

    Parameters
    ----------
    source : str, optional
        Human-readable source label for diagnostics.

    Returns
    -------
    str
        Kubeconfig payload.

    Raises
    ------
    OSError
        If the kubeconfig is missing or empty.
    """
    kubeconfig = STATE.kube.config
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

    Parameters
    ----------
    argv : list[str]
        Arguments passed to `k0s kubectl`.
    check : bool, optional
        Whether a non-zero exit status should raise.
    capture_output : bool | None, optional
        Whether to capture command output.
    stdin : str | None, optional
        Input text forwarded to the command.
    deadline : Deadline, optional
        Active operation deadline.
    attempts : int, optional
        Maximum command attempts.
    delay : float, optional
        Delay between attempts.
    cwd : Path | None, optional
        Working directory for the subprocess.
    env : Mapping[str, str] | None, optional
        Environment overrides for the subprocess.

    Returns
    -------
    CompletedProcess
        Completed command result.

    Raises
    ------
    OSError
        If the managed k0s binary is missing.
    """
    k0s_binary = STATE.kube.bin
    if not k0s_binary.is_file():
        msg = f"Bertrand k0s binary is missing at {k0s_binary}; run `bertrand init`"
        raise OSError(msg)
    return await run(
        [
            str(k0s_binary),
            "kubectl",
            "--kubeconfig",
            str(STATE.kube.config),
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


# TODO: rewrite_kubeconfig_payload is an absolute mess, and should be replaced with
# something simpler


def _rewrite_kubeconfig_payload(payload: str, *, server_url: str | None) -> str:
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
    rewritten = _rewrite_kubeconfig_payload(decoded, server_url=server_url)
    return base64.b64encode(rewritten.encode("utf-8")).decode("ascii")


def _client_from_config(config_file: Path) -> kubernetes.client.ApiClient:
    try:
        return kubernetes.config.new_client_from_config(config_file=str(config_file))
    except _KUBE_CONFIG_ERRORS as err:
        msg = f"failed to initialize kubernetes client from {config_file}: {err}"
        raise OSError(msg) from err


class KubeApiError(OSError):
    """Structured Kubernetes API failure raised by :meth:`Kube.run`.

    Parameters
    ----------
    context : str
        Human-readable operation context.
    status : int
        Kubernetes API HTTP status code.
    detail : str
        Kubernetes API failure detail.
    """

    context: str
    status: int
    detail: str

    def __init__(self, *, context: str, status: int, detail: str) -> None:
        """Initialize a structured Kubernetes API error."""
        self.context = context
        self.status = status
        self.detail = detail
        super().__init__(
            f"{context} failed with kubernetes API status {status}: {detail}"
        )


def is_missing_api_resource(err: OSError) -> bool:
    """Return whether a Kubernetes 404 means the REST resource is unavailable.

    Parameters
    ----------
    err : OSError
        Error raised by :meth:`Kube.run`.

    Returns
    -------
    bool
        Whether the API server could not resolve the requested resource endpoint,
        rather than simply reporting a missing object instance.
    """
    if not isinstance(err, KubeApiError) or err.status != 404:
        return False
    return "the server could not find the requested resource" in err.detail.lower()


@dataclass
class Kube:
    """Context-managed Kubernetes client wrapper for Bertrand runtime operations.

    Attributes
    ----------
    client : kubernetes.client.ApiClient
        Underlying Kubernetes API transport instance.
    core : kubernetes.client.CoreV1Api
        Core v1 API surface for typed built-in resources.
    apps : kubernetes.client.AppsV1Api
        Apps v1 API surface for Deployments and related workload resources.
    custom : kubernetes.client.CustomObjectsApi
        Custom object API surface for CRD interactions.
    batch : kubernetes.client.BatchV1Api
        Batch v1 API surface for Jobs and related execution resources.
    networking : kubernetes.client.NetworkingV1Api
        Networking v1 API surface for NetworkPolicy resources.
    apiextensions : kubernetes.client.ApiextensionsV1Api
        API extensions v1 surface for CRDs.
    rbac : kubernetes.client.RbacAuthorizationV1Api
        RBAC authorization v1 API surface.
    storage : kubernetes.client.StorageV1Api
        Storage v1 API surface for StorageClass resources.
    coordination : kubernetes.client.CoordinationV1Api
        Coordination v1 API surface for Lease resources.
    """

    client: kubernetes.client.ApiClient = field(repr=False)
    core: kubernetes.client.CoreV1Api = field(init=False, repr=False)
    apps: kubernetes.client.AppsV1Api = field(init=False, repr=False)
    custom: kubernetes.client.CustomObjectsApi = field(init=False, repr=False)
    batch: kubernetes.client.BatchV1Api = field(init=False, repr=False)
    networking: kubernetes.client.NetworkingV1Api = field(init=False, repr=False)
    apiextensions: kubernetes.client.ApiextensionsV1Api = field(init=False, repr=False)
    rbac: kubernetes.client.RbacAuthorizationV1Api = field(init=False, repr=False)
    storage: kubernetes.client.StorageV1Api = field(init=False, repr=False)
    coordination: kubernetes.client.CoordinationV1Api = field(init=False, repr=False)

    def __post_init__(self) -> None:
        """Initialize typed Kubernetes API handles from the shared transport.

        Raises
        ------
        AttributeError
            If the API client is missing attributes required by Kubernetes wrappers.
        TypeError
            If the API client cannot be used to construct Kubernetes wrappers.
        ValueError
            If the API client is rejected while constructing Kubernetes wrappers.
        """
        try:
            self.core = kubernetes.client.CoreV1Api(self.client)
            self.apps = kubernetes.client.AppsV1Api(self.client)
            self.custom = kubernetes.client.CustomObjectsApi(self.client)
            self.batch = kubernetes.client.BatchV1Api(self.client)
            self.networking = kubernetes.client.NetworkingV1Api(self.client)
            self.apiextensions = kubernetes.client.ApiextensionsV1Api(self.client)
            self.rbac = kubernetes.client.RbacAuthorizationV1Api(self.client)
            self.storage = kubernetes.client.StorageV1Api(self.client)
            self.coordination = kubernetes.client.CoordinationV1Api(self.client)
        except (AttributeError, TypeError, ValueError):
            with suppress(OSError, RuntimeError, ValueError):
                self.client.close()
            raise

    @staticmethod
    async def _systemd_ready(deadline: Deadline) -> bool:
        return (
            await run(
                sudo(["systemctl", "is-active", "--quiet", K0S_SERVICE_NAME]),
                check=False,
                deadline=deadline,
            )
        ).returncode == 0

    @staticmethod
    async def _cluster_ready(deadline: Deadline) -> bool:
        return (
            await kubectl(
                ["get", "--raw=/readyz"],
                check=False,
                capture_output=True,
                deadline=deadline,
            )
        ).returncode == 0

    @staticmethod
    async def _download_k0s(deadline: Deadline) -> None:
        fd, name = tempfile.mkstemp(prefix="bertrand-k0s.", suffix=".download")
        os.close(fd)
        download = Path(name)
        arch = platform.machine().lower()
        if arch in {"x86_64", "amd64"}:
            norm = "amd64"
        elif arch in {"aarch64", "arm64"}:
            norm = "arm64"
        elif arch in {"armv7l", "armv7", "armhf", "arm"}:
            norm = "arm"
        else:
            msg = f"unsupported architecture for k0s binary download: {arch!r}"
            raise OSError(msg)
        try:
            await run(
                [
                    "curl",
                    "-fL",
                    (
                        "https://github.com/k0sproject/k0s/releases/download/"
                        f"{K0S_VERSION}/k0s-{K0S_VERSION}-{norm}"
                    ),
                    "-o",
                    str(download),
                ],
                capture_output=True,
                deadline=deadline,
            )
            await STATE.install(
                download,
                STATE.kube.bin,
                deadline=deadline,
            )
        finally:
            download.unlink(missing_ok=True)

    @staticmethod
    async def _dump_k0s_config(deadline: Deadline) -> None:
        await STATE.write(
            STATE.kube.bootstrap,
            yaml.safe_dump(
                {
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
                },
                sort_keys=False,
            ),
            deadline=deadline,
        )

    @staticmethod
    async def _dump_join_token(token: str | None, deadline: Deadline) -> Path | None:
        if token is None:
            return None

        token = token.strip()
        if not token:
            msg = "k0s join token cannot be empty"
            raise OSError(msg)
        await STATE.write(
            STATE.kube.token,
            f"{token}\n",
            deadline=deadline,
        )
        return STATE.kube.token

    @staticmethod
    async def _dump_unit(
        role: K0sRole,
        token_file: Path | None,
        deadline: Deadline,
    ) -> None:
        node_name = f"bertrand-{STATE.id[:32]}"
        if role == "controller":
            cmd = [
                str(STATE.kube.bin),
                "controller",
                "--config",
                str(STATE.kube.bootstrap),
                "--data-dir",
                str(STATE.kube.cache),
                "--enable-worker",
                "--no-taints",
                f"--kubelet-extra-args=--hostname-override={node_name}",
            ]
            if token_file is not None:
                cmd.append("--token-file")
                cmd.append(str(token_file))
        else:
            if token_file is None:
                msg = "k0s worker join requires a token file"
                raise ValueError(msg)
            cmd = [
                str(STATE.kube.bin),
                "worker",
                "--data-dir",
                str(STATE.kube.cache),
                "--token-file",
                str(token_file),
                f"--kubelet-extra-args=--hostname-override={node_name}",
            ]

        await STATE.write(
            STATE.kube.runtime,
            "\n".join(
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
                    f"ExecStart={' '.join(shlex.quote(part) for part in cmd)}",
                    "",
                    "[Install]",
                    "WantedBy=multi-user.target",
                    "",
                ),
            ),
            deadline=deadline,
        )

    @classmethod
    async def _k0s_ready(cls, deadline: Deadline) -> None:
        if await cls._systemd_ready(deadline) and await cls._cluster_ready(deadline):
            return
        raise TimeoutError

    @staticmethod
    async def _register_bertrand_namespace(*, deadline: Deadline) -> None:
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

    @classmethod
    async def _start_k0s(cls, *, deadline: Deadline, yes: bool) -> None:
        ignore_errors = False
        await STATE.lock.lock(deadline=deadline)
        try:
            try:
                # short-circuit if the cluster is already running
                if await cls._cluster_ready(deadline):
                    await cls._register_bertrand_namespace(deadline=deadline)
                    return

                # start systemd service and wait for k0s to report ready
                await run(
                    sudo(
                        ["systemctl", "start", K0S_SERVICE_NAME],
                        non_interactive=True,
                    ),
                    deadline=deadline,
                )
                try:
                    await until(cls._k0s_ready, deadline=deadline, delay=0.25)
                except TimeoutError as err:
                    msg = (
                        "timed out waiting for k0s to become ready after "
                        f"{deadline.timeout} seconds"
                    )
                    raise TimeoutError(msg) from err

                # refresh kubeconfig
                result = await run(
                    sudo(
                        [
                            str(STATE.kube.bin),
                            "kubeconfig",
                            "admin",
                            "--config",
                            str(STATE.kube.bootstrap),
                            "--data-dir",
                            str(STATE.kube.cache),
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
                    STATE.kube.config,
                    _rewrite_kubeconfig_payload(payload, server_url=None),
                    deadline=deadline,
                )

                # register Bertrand namespace
                await cls._register_bertrand_namespace(deadline=deadline)
            except:
                ignore_errors = True
                raise
            finally:
                await STATE.lock.unlock(ignore_errors=ignore_errors)
        except CommandError as err:
            msg = (
                "Failed to start Bertrand's k0s service. Re-run `bertrand init` to "
                f"repair the managed runtime.\n{err}"
            )
            raise OSError(msg) from err

    @classmethod
    async def ready(cls, *, deadline: Deadline) -> bool:
        """Return whether the managed k0s API reports ready.

        Parameters
        ----------
        deadline : Deadline
            Active operation deadline.

        Returns
        -------
        bool
            Whether the managed k0s API responds successfully to `/readyz`.
        """
        return (
            STATE.kube.bin.is_file()
            and STATE.kube.config.is_file()
            and await cls._systemd_ready(deadline)
            and await cls._cluster_ready(deadline)
        )

    @classmethod
    async def init(
        cls,
        *,
        role: K0sRole = "controller",
        token: str | None = None,
        yes: bool,
        force: bool = False,
        deadline: Deadline,
    ) -> None:
        """Bootstrap and start Bertrand's private k0s cluster.

        Parameters
        ----------
        role : K0sRole, optional
            k0s role for the local runtime.
        token : str | None, optional
            Join token for joining an existing cluster.
        yes : bool
            Whether interactive confirmations should be auto-accepted.
        force : bool, optional
            Whether to refresh the local service even if it appears active.
        deadline : Deadline
            Active operation deadline.

        Raises
        ------
        PermissionError
            If the user declines installation.
        """
        # short-circuit if the service appears active and the k0s binary and config
        # are in-place
        if (
            not force
            and STATE.kube.bin.is_file()
            and STATE.kube.config.is_file()
            and await cls._systemd_ready(deadline)
        ):
            await cls._start_k0s(deadline=deadline, yes=yes)
            return

        if not confirm(
            "Bertrand uses an owned k0s service as its local Kubernetes runtime. "
            f"Install or refresh {K0S_SERVICE_NAME!r} now (requires sudo)?\n[y/N] ",
            yes=yes,
        ):
            msg = "k0s installation declined by user."
            raise PermissionError(msg)

        # get k0s binary if missing or if we're forcing a refresh
        if force or not STATE.kube.bin.is_file():
            await cls._download_k0s(deadline)

        # write output artifacts
        await cls._dump_k0s_config(deadline)
        token_file = await cls._dump_join_token(token, deadline)
        await cls._dump_unit(role, token_file, deadline)

        # refresh systemd + enable unit on future startup
        await run(
            sudo(["systemctl", "daemon-reload"], non_interactive=True),
            deadline=deadline,
        )
        await run(
            sudo(["systemctl", "enable", K0S_SERVICE_NAME], non_interactive=True),
            deadline=deadline,
        )

        # start the service and wait for the API to become ready
        await cls._start_k0s(deadline=deadline, yes=yes)

    # TODO: split start() out of init() and implement a separate stop() method for
    # stopping the cluster, but not necessarily deleting it, unlike clean()

    @classmethod
    def external(cls, *, config_file: Path = STATE.kube.config) -> Self:
        """Build a host-side API client from Bertrand's managed kubeconfig.

        Parameters
        ----------
        config_file : Path, optional
            Path to the kubeconfig file used for host-side API access.

        Returns
        -------
        Kube
            Configured Kubernetes API wrapper.

        Raises
        ------
        OSError
            If the kubeconfig is missing or cannot be loaded.
        """
        if not config_file.is_file():
            msg = (
                f"kubernetes config is missing at {config_file}.  Run `bertrand init` "
                "to converge k0s API access first."
            )
            raise OSError(msg)
        return cls(client=_client_from_config(config_file))

    @classmethod
    def internal(cls) -> Self:
        """Build an in-cluster API client from projected ServiceAccount credentials.

        Returns
        -------
        Kube
            Configured in-cluster Kubernetes API wrapper.

        Raises
        ------
        OSError
            If the in-cluster configuration cannot be loaded.
        """
        configuration = kubernetes.client.Configuration()
        try:
            kubernetes.config.load_incluster_config(client_configuration=configuration)
        except _KUBE_CONFIG_ERRORS as err:
            msg = f"failed to load in-cluster kubernetes configuration: {err}"
            raise OSError(msg) from err
        return cls(client=kubernetes.client.ApiClient(configuration=configuration))

    # TODO: review join logic and how/if it should interact with other bootstrap steps

    @classmethod
    async def join_bundle(
        cls,
        *,
        role: K0sRole,
        server_url: str | None,
        deadline: Deadline,
    ) -> tuple[str, str, str]:
        """Return `(server_url, token, kubeconfig)` for a joining node.

        Parameters
        ----------
        role : K0sRole
            k0s role for the joining host.
        server_url : str | None
            Optional externally reachable server URL.
        deadline : Deadline
            Active operation deadline.

        Returns
        -------
        tuple[str, str, str]
            Reachable server URL, rewritten join token, and rewritten kubeconfig.

        Raises
        ------
        OSError
            If k0s is not ready or a join token cannot be generated.
        """
        if not await cls._cluster_ready(deadline):
            msg = "k0s must be running before generating a join token"
            raise OSError(msg)

        payload = managed_kubeconfig_payload()
        default_server, _ca = kubeconfig_identity(payload, source="managed kubeconfig")
        explicit_server = server_url is not None and bool(server_url.strip())
        chosen_source = (
            server_url.strip() if explicit_server and server_url else default_server
        )
        chosen_server = _normalize_server_url(chosen_source)
        if not explicit_server:
            host = urlparse(chosen_server).hostname or ""
            is_loopback = host.lower() in {"localhost", "localhost.localdomain"}
            if not is_loopback:
                with contextlib.suppress(ValueError):
                    is_loopback = ipaddress.ip_address(host).is_loopback
            if is_loopback:
                msg = (
                    "managed kubeconfig points at a loopback k0s API endpoint. Re-run "
                    "`bertrand cluster invite` with --server-url https://HOST:PORT for a "
                    "reachable controller address."
                )
                raise OSError(msg)

        result = await run(
            sudo(
                [
                    str(STATE.kube.bin),
                    "token",
                    "create",
                    "--role",
                    role,
                    "--config",
                    str(STATE.kube.bootstrap),
                    "--data-dir",
                    str(STATE.kube.cache),
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
            _rewrite_kubeconfig_payload(payload, server_url=chosen_server),
        )

    @classmethod
    async def join_cluster(
        cls,
        *,
        server_url: str,
        token: str,
        role: K0sRole,
        kubeconfig: str,
        yes: bool,
        deadline: Deadline,
    ) -> None:
        """Join the local host to an existing Bertrand k0s cluster.

        Parameters
        ----------
        server_url : str
            Reachable server URL for the existing cluster.
        token : str
            k0s join token.
        role : K0sRole
            k0s role for the local host.
        kubeconfig : str
            Kubeconfig payload from the invite bundle.
        yes : bool
            Whether interactive confirmations should be auto-accepted.
        deadline : Deadline
            Active operation deadline.

        Raises
        ------
        OSError
            If the local runtime is already ready or the join payload is invalid.
        """
        if await cls._cluster_ready(deadline):
            msg = (
                "local k0s already reports a ready cluster; refusing to join it to "
                "another cluster. Run `bertrand clean --force` first if this host should "
                "join a different Bertrand cluster."
            )
            raise OSError(msg)

        server = _normalize_server_url(server_url)
        token = token.strip()
        if not token:
            msg = "k0s join token cannot be empty"
            raise OSError(msg)
        kubeconfig_identity(kubeconfig, source="join bundle kubeconfig")
        token = _token_with_server(token, server)

        lock = HostLock(STATE.kube.lock)
        await lock.lock(deadline)
        ignore_errors = False
        try:
            await STATE.write(
                STATE.kube.config,
                _rewrite_kubeconfig_payload(kubeconfig, server_url=server),
                deadline=deadline,
            )
            await Kube.init(
                role=role,
                token=token,
                yes=yes,
                force=True,
                deadline=deadline,
            )
        except:
            ignore_errors = True
            raise
        finally:
            await lock.unlock(ignore_errors=ignore_errors)

    @classmethod
    async def clean(cls, *, deadline: Deadline) -> None:
        """Uninstall Bertrand's owned k0s runtime from this host.

        Parameters
        ----------
        deadline : Deadline
            Active operation deadline.
        """
        systemctl = shutil.which("systemctl")
        if systemctl:
            await run(
                sudo(
                    [systemctl, "disable", "--now", K0S_SERVICE_NAME],
                    non_interactive=True,
                ),
                check=False,
                deadline=deadline,
            )
        if STATE.kube.bin.is_file():
            await run(
                sudo(
                    [
                        str(STATE.kube.bin),
                        "reset",
                        "--force",
                        "--data-dir",
                        str(STATE.kube.cache),
                    ]
                ),
                check=False,
                capture_output=True,
                deadline=deadline,
            )
        await run(
            sudo(
                [
                    "rm",
                    "-rf",
                    str(
                        ROOT_DIR
                        / "etc"
                        / "systemd"
                        / "system"
                        / f"{K0S_SERVICE_NAME}.service"
                    ),
                    str(STATE.kube.cache),
                    str(STATE.kube.runtime),
                    str(STATE.kube.bootstrap),
                    str(STATE.kube.token),
                    str(STATE.kube.config),
                ]
            ),
            check=False,
            capture_output=True,
            deadline=deadline,
        )
        if systemctl:
            await run(
                sudo([systemctl, "daemon-reload"], non_interactive=True),
                check=False,
                deadline=deadline,
            )

    def __enter__(self) -> Self:
        """Enter the Kubernetes client context manager.

        Returns
        -------
        Kube
            This Kubernetes client instance.
        """
        return self

    def __exit__(self, exc_type: object, exc: object, exc_tb: object) -> None:
        """Close the Kubernetes API transport when leaving the context manager.

        Parameters
        ----------
        exc_type : object
            Exception type if raised within the context, else `None`.
        exc : object
            Exception instance if raised within the context, else `None`.
        exc_tb : object
            Exception traceback if raised within the context, else `None`.
        """
        self.client.close()

    # TODO: try to eliminate `close()` in favor of strict context management, but this
    # cannot be done until `bertrand init` is fully reviewed

    def close(self) -> None:
        """Close the Kubernetes API connection.

        This method is identical to the context manager exit handler, for cases when
        explicit context management is not possible due to logical constraints.
        """
        self.client.close()

    async def run[T](
        self,
        fn: Callable[[float | None], T],
        *,
        deadline: Deadline,
        context: str,
        missing_ok: bool = True,
    ) -> T | None:
        """Run one Kubernetes API operation across the sync/async boundary.

        Parameters
        ----------
        fn : Callable[[float | None], T]
            Callable that performs one Kubernetes API operation and accepts the
            normalized Kubernetes request timeout (`None` for infinite waits).
        deadline : Deadline
            Maximum runtime budget.  If infinite, wait indefinitely.
        context : str
            Human-readable context for timeout and API error messages.
        missing_ok : bool, optional
            Whether HTTP 404 should be returned as `None` instead of raised as a
            structured API error.

        Returns
        -------
        T | None
            The API payload, or `None` if the operation returned HTTP 404 and
            `missing_ok` is true.

        Raises
        ------
        TimeoutError
            If the operation exceeds the deadline.
        KubeApiError
            If the API call fails with any non-404 error.
        """
        remaining = deadline.check(f"{context} timed out before request could start")
        request_timeout = None if math.isinf(remaining) else remaining
        try:
            return await asyncio.wait_for(
                asyncio.to_thread(fn, request_timeout),
                timeout=request_timeout,
            )
        except TimeoutError as err:
            msg = f"{context} timed out after {deadline.timeout} seconds"
            raise TimeoutError(msg) from err
        except ApiException as err:
            if err.status == 404 and missing_ok:
                return None
            detail = (err.body or err.reason or str(err)).strip()
            raise KubeApiError(
                context=context,
                status=int(err.status or 0),
                detail=detail,
            ) from err
