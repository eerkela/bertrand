"""Kubernetes API client context for Bertrand runtime orchestration."""

from __future__ import annotations

import asyncio
import contextlib
import ipaddress
import json
import math
import os
import platform
import shlex
import shutil
import socket
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING, Literal, Self
from urllib.parse import urlparse

import kubernetes
import yaml
from kubernetes.client.rest import ApiException
from kubernetes.config import (
    ConfigException,
    load_incluster_config,
    new_client_from_config,
)

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
    inside_image,
    run,
    sudo,
    until,
)

if TYPE_CHECKING:
    from collections.abc import Callable, Mapping

_KUBE_CONFIG_ERRORS = (ConfigException, OSError, ValueError)
K0S_VERSION = "v1.35.4+k0s.0"
K0S_SERVICE_NAME = "bertrand-k0s"
K0S_SERVICE_FILE = (
    ROOT_DIR / "etc" / "systemd" / "system" / f"{K0S_SERVICE_NAME}.service"
)
K0S_API_PORT = 16443
K0S_CONTROLLER_API_PORT = 19443

type K0sRole = Literal["controller", "worker"]
K0S_ROLES: tuple[K0sRole, ...] = ("controller", "worker")


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


@dataclass(frozen=True)
class Kube:
    """Context-managed Kubernetes client wrapper for Bertrand runtime operations.

    Attributes
    ----------
    client : kubernetes.client.ApiClient
        Underlying Kubernetes API transport instance.
    """

    class APIError(OSError):
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

        @property
        def missing_api_resource(self) -> bool:
            """Return whether the API server could not resolve the resource type.

            Returns
            -------
            bool
                Whether this error means the requested Kubernetes API endpoint is
                unavailable, rather than a single object instance being absent.
            """
            return (
                self.status == 404
                and "the server could not find the requested resource"
                in self.detail.lower()
            )

    client: kubernetes.client.ApiClient

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
    def _default_route_ipv4() -> str | None:
        port = 80
        for ip in ("8.8.8.8", "1.1.1.1"):
            try:
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                    sock.connect((ip, port))
                    host_addr, _ = sock.getsockname()
                host = str(host_addr)
                address = ipaddress.ip_address(host)
            except (OSError, ValueError):
                continue
            if (
                address.version == 4
                and not address.is_loopback
                and not address.is_unspecified
            ):
                return host
        return None

    @staticmethod
    def _parse_join_url(value: str) -> tuple[str, int]:
        text = value.strip()
        if not text:
            msg = "k0s join URL cannot be empty"
            raise ValueError(msg)
        if "://" not in text:
            text = f"https://{text}"

        # parse and validate URL structure
        parsed = urlparse(text)
        if parsed.scheme != "https" or not parsed.hostname:
            msg = f"k0s join URL must be an HTTPS host:port URL, got {value!r}"
            raise ValueError(msg)
        if (
            parsed.username
            or parsed.password
            or parsed.path
            or parsed.query
            or parsed.fragment
        ):
            msg = "k0s join URL must not include credentials, path, query, or fragment"
            raise ValueError(msg)

        # use specified port if given or fall back to a global default
        try:
            port = parsed.port or K0S_API_PORT
        except ValueError as err:
            msg = f"k0s join URL has invalid port: {value!r}"
            raise ValueError(msg) from err

        # ensure no host loopback or unspecified address
        host = parsed.hostname.strip()
        if host.lower() in {"localhost", "localhost.localdomain"}:
            msg = "k0s join URL must not use a loopback host"
            raise ValueError(msg)
        try:
            address = ipaddress.ip_address(host)
        except ValueError:
            pass
        else:
            if address.is_loopback or address.is_unspecified:
                msg = "k0s join URL must not use a loopback or unspecified address"
                raise ValueError(msg)

        return host, port

    @staticmethod
    async def _dump_k0s_config(
        *,
        join_address: tuple[str, int] | None,
        deadline: Deadline,
    ) -> None:
        api: dict[str, object] = {
            "port": K0S_API_PORT,
            "k0sApiPort": K0S_CONTROLLER_API_PORT,
        }
        if join_address is not None:
            host, port = join_address
            api["port"] = port
            api["externalAddress"] = host
            api["sans"] = [host]
        await STATE.write(
            STATE.kube.bootstrap,
            yaml.safe_dump(
                {
                    "apiVersion": "k0s.k0sproject.io/v1beta1",
                    "kind": "ClusterConfig",
                    "metadata": {"name": "bertrand"},
                    "spec": {
                        "api": api,
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

        fd, name = tempfile.mkstemp(prefix="bertrand-k0s.", suffix=".service")
        temp_unit = Path(name)
        try:
            unit = "\n".join(
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
            )
            os.write(fd, unit.encode("utf-8"))
            os.fsync(fd)
            os.close(fd)
            fd = -1
            await run(
                sudo(
                    [
                        "install",
                        "-D",
                        "-m",
                        "0644",
                        "-o",
                        "root",
                        "-g",
                        "root",
                        str(temp_unit),
                        str(K0S_SERVICE_FILE),
                    ],
                    non_interactive=True,
                ),
                deadline=deadline,
            )
        finally:
            if fd >= 0:
                with contextlib.suppress(OSError):
                    os.close(fd)
            temp_unit.unlink(missing_ok=True)

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
        return inside_image() or (
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
        join_url: str | None = None,
        yes: bool,
        force: bool = False,
        deadline: Deadline,
    ) -> None:
        """Bootstrap Bertrand's private k0s cluster.

        Parameters
        ----------
        role : K0sRole, optional
            k0s role for the local runtime.
        token : str | None, optional
            Join token for joining an existing cluster.
        join_url : str | None, optional
            Optional stable HTTPS k0s API URL override for joining hosts.  If omitted
            for the initial controller, Bertrand attempts to use the host's
            default-route IPv4 address.
        yes : bool
            Whether interactive confirmations should be auto-accepted.
        force : bool, optional
            Whether to refresh the local service even if it appears active.
        deadline : Deadline
            Active operation deadline.

        Raises
        ------
        OSError
            If called from within the cluster, or installation fails.
        PermissionError
            If the user declines installation.
        """
        if inside_image():
            msg = "Bertrand cluster initialization cannot run from inside the cluster."
            raise OSError(msg)
        join_address: tuple[str, int] | None = None
        if join_url is not None:
            join_address = cls._parse_join_url(join_url)
        elif role == "controller" and token is None:
            host = cls._default_route_ipv4()
            if host is not None:
                join_address = (host, K0S_API_PORT)

        # short-circuit if the managed runtime artifacts are already in-place
        if (
            not force
            and join_url is None
            and STATE.kube.bin.is_file()
            and STATE.kube.bootstrap.is_file()
            and K0S_SERVICE_FILE.is_file()
        ):
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
        await cls._dump_k0s_config(join_address=join_address, deadline=deadline)
        token_file = await cls._dump_join_token(token, deadline)
        await cls._dump_unit(role, token_file, deadline)

        # refresh systemd after writing the managed unit
        await run(
            sudo(["systemctl", "daemon-reload"], non_interactive=True),
            deadline=deadline,
        )

    @classmethod
    async def start(
        cls,
        *,
        deadline: Deadline,
        yes: bool,
        restart: bool = False,
    ) -> None:
        """Start Bertrand's private k0s cluster if not already running.

        Parameters
        ----------
        deadline : Deadline
            Active operation deadline.
        yes : bool
            Whether interactive confirmations should be auto-accepted.
        restart : bool, optional
            Whether to restart the k0s service even if it appears active.  This is
            useful when the cluster is already running but may not be healthy, such
            as when the API is unresponsive or returning errors.

        Raises
        ------
        OSError
            If called from within the cluster, the service fails to start, or the API
            fails to become ready.
        TimeoutError
            If the API fails to become ready before the deadline.
        """
        if inside_image():
            msg = "Bertrand cluster startup cannot run from inside the cluster."
            raise OSError(msg)

        ignore_errors = False
        await STATE.lock.lock(deadline=deadline)
        try:
            try:
                if not (
                    STATE.kube.bin.is_file()
                    and STATE.kube.bootstrap.is_file()
                    and K0S_SERVICE_FILE.is_file()
                ):
                    msg = (
                        f"{K0S_SERVICE_NAME} does not appear to be installed. Run "
                        "`bertrand init` to install the k0s service first."
                    )
                    raise OSError(msg)
                await run(
                    sudo(["systemctl", "daemon-reload"], non_interactive=True),
                    deadline=deadline,
                )

                # short-circuit if the cluster is already running
                if not restart and await cls._cluster_ready(deadline):
                    await run(
                        sudo(
                            ["systemctl", "enable", K0S_SERVICE_NAME],
                            non_interactive=True,
                        ),
                        deadline=deadline,
                    )
                    await cls._register_bertrand_namespace(deadline=deadline)
                    return

                # start systemd service and wait for k0s to report ready
                if restart:
                    await run(
                        sudo(
                            ["systemctl", "enable", K0S_SERVICE_NAME],
                            non_interactive=True,
                        ),
                        deadline=deadline,
                    )
                    await run(
                        sudo(
                            ["systemctl", "restart", K0S_SERVICE_NAME],
                            non_interactive=True,
                        ),
                        deadline=deadline,
                    )
                else:
                    await run(
                        sudo(
                            ["systemctl", "enable", "--now", K0S_SERVICE_NAME],
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
                    f"{payload}\n",
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
    def external(cls, config: Path = STATE.kube.config) -> Self:
        """Build a host-side API client from Bertrand's managed kubeconfig.

        Parameters
        ----------
        config : Path, optional
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
        if not config.is_file():
            msg = (
                f"kubernetes config is missing at {config}.  Run `bertrand init` "
                "to converge k0s API access first."
            )
            raise OSError(msg)
        try:
            client = new_client_from_config(config_file=str(config))
            return cls(client=client)
        except _KUBE_CONFIG_ERRORS as err:
            msg = f"failed to initialize kubernetes client from {config}: {err}"
            raise OSError(msg) from err

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
            load_incluster_config(client_configuration=configuration)
        except _KUBE_CONFIG_ERRORS as err:
            msg = f"failed to load in-cluster kubernetes configuration: {err}"
            raise OSError(msg) from err
        return cls(client=kubernetes.client.ApiClient(configuration=configuration))

    @classmethod
    async def join_bundle(cls, *, role: K0sRole, deadline: Deadline) -> tuple[str, str]:
        """Return `(token, kubeconfig)` for a joining node.

        Parameters
        ----------
        role : K0sRole
            k0s role for the joining host.
        deadline : Deadline
            Active operation deadline.

        Returns
        -------
        tuple[str, str]
            Raw kubernetes join token and managed kubeconfig.

        Raises
        ------
        OSError
            If called from within the cluster, kubernetes is not ready, or a join token
            cannot be generated.
        """
        if inside_image():
            msg = "joining a cluster must be done from outside a kubernetes environment"
            raise OSError(msg)
        if not await cls._cluster_ready(deadline):
            msg = "kubernetes must be running before generating a join token"
            raise OSError(msg)

        # extract join URL from k0s config
        api = {}
        with contextlib.suppress(OSError, KeyError, TypeError, yaml.YAMLError):
            raw = yaml.safe_load(STATE.kube.bootstrap.read_text(encoding="utf-8"))
            if isinstance(raw, dict):
                spec = raw.get("spec")
                if isinstance(spec, dict):
                    api = spec.get("api", {})
                    if not isinstance(api, dict):
                        api = {}
        if not isinstance(api, dict) or not api.get("externalAddress", "").strip():
            msg = (
                "cluster invite requires a configured kubernetes join URL. Re-run "
                "`bertrand init --join-url https://HOST:PORT` on the controller "
                "before inviting additional hosts."
            )
            raise OSError(msg)

        # load kubeconfig
        kubeconfig = STATE.kube.config
        try:
            text = kubeconfig.read_text(encoding="utf-8")
        except OSError as err:
            msg = f"failed to read managed kubeconfig at {kubeconfig}: {err}"
            raise OSError(msg) from err
        if not text.strip():
            msg = f"managed kubeconfig at {kubeconfig} is empty"
            raise OSError(msg)
        payload = text if text.endswith("\n") else f"{text}\n"

        # generate join token
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
            msg = "`kubernetes token create` returned an empty join token"
            raise OSError(msg)

        return token, payload

    @classmethod
    async def join_cluster(
        cls,
        *,
        token: str,
        role: K0sRole,
        kubeconfig: str,
        yes: bool,
        deadline: Deadline,
    ) -> None:
        """Join the local host to an existing Bertrand kubernetes cluster.

        Parameters
        ----------
        token : str
            kubernetes join token.
        role : K0sRole
            kubernetes role for the local host.
        kubeconfig : str
            Kubeconfig payload from the invite bundle.
        yes : bool
            Whether interactive confirmations should be auto-accepted.
        deadline : Deadline
            Active operation deadline.

        Raises
        ------
        OSError
            If called from within the cluster, the local runtime is already ready, or
            the join payload is invalid.
        """
        if inside_image():
            msg = "joining a cluster must be done from outside a kubernetes environment"
            raise OSError(msg)
        if await cls.ready(deadline=deadline):
            msg = (
                "local k0s already reports a ready cluster; refusing to join it to "
                "another cluster. Run `bertrand clean --force` first if this host "
                "should join a different Bertrand cluster."
            )
            raise OSError(msg)

        # validate token and kubeconfig
        token = token.strip()
        if not token:
            msg = "k0s join token cannot be empty"
            raise OSError(msg)
        if not kubeconfig.strip():
            msg = "join bundle kubeconfig cannot be empty"
            raise OSError(msg)
        kubeconfig = kubeconfig if kubeconfig.endswith("\n") else f"{kubeconfig}\n"

        # lock and re-initialize cluster with the provided join bundle
        lock = HostLock(STATE.kube.lock)
        await lock.lock(deadline)
        ignore_errors = False
        try:
            await STATE.write(
                STATE.kube.config,
                kubeconfig,
                deadline=deadline,
            )
            await Kube.init(
                role=role,
                token=token,
                yes=yes,
                force=True,
                deadline=deadline,
            )
            await Kube.start(deadline=deadline, yes=yes)
        except:
            ignore_errors = True
            raise
        finally:
            await lock.unlock(ignore_errors=ignore_errors)

    @classmethod
    async def stop(cls, *, deadline: Deadline) -> None:
        """Stop Bertrand's private k0s cluster without deleting runtime state.

        Parameters
        ----------
        deadline : Deadline
            Active operation deadline.

        Raises
        ------
        OSError
            If called from within the cluster, systemd is unavailable, or the service
            fails to stop.
        """
        if inside_image():
            msg = "Bertrand cluster shutdown cannot run from inside the cluster."
            raise OSError(msg)
        systemctl = shutil.which("systemctl")
        if systemctl is None:
            msg = "systemctl is required to stop Bertrand's k0s service"
            raise OSError(msg)
        ignore_errors = False
        await STATE.lock.lock(deadline=deadline)
        try:
            await run(
                sudo(
                    [systemctl, "disable", "--now", K0S_SERVICE_NAME],
                    non_interactive=True,
                ),
                deadline=deadline,
            )
        except:
            ignore_errors = True
            raise
        finally:
            await STATE.lock.unlock(ignore_errors=ignore_errors)

    @classmethod
    async def clean(cls, *, deadline: Deadline) -> None:
        """Uninstall Bertrand's owned k0s runtime from this host.

        Parameters
        ----------
        deadline : Deadline
            Active operation deadline.

        Raises
        ------
        OSError
            If systemd is unavailable or the service fails to stop, or if runtime
            files cannot be removed.
        """
        if inside_image():
            msg = "Bertrand cluster cleanup cannot run from inside the cluster."
            raise OSError(msg)
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
                    str(K0S_SERVICE_FILE),
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

    def close(self) -> None:
        """Close the Kubernetes API connection.

        This method is identical to the context manager exit handler, for cases when
        explicit context management is not possible due to logical constraints.  In
        almost all cases, using `Kube` as a context manager is preferred.
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
        Kube.APIError
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
            raise Kube.APIError(
                context=context,
                status=int(err.status or 0),
                detail=detail,
            ) from err
