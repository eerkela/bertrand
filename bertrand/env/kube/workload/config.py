"""Adapters from validated Bertrand workload config to native workload intent."""

from __future__ import annotations

from typing import TYPE_CHECKING, Protocol, cast

from bertrand.env.git.bertrand_git import WORKTREE_ID_ENV
from bertrand.env.kube.api.spec import (
    ContainerPortSpec,
    ContainerResourcesSpec,
    ContainerSpec,
    PodTemplateSpec,
    ProbeSpec,
    SecurityContextSpec,
    TolerationSpec,
)
from bertrand.env.kube.workload.base import (
    WorkloadIdentity,
    WorkloadPod,
    WorkloadRepository,
)
from bertrand.env.kube.workload.capability import resolve_workload_capabilities

if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence
    from pathlib import PurePosixPath

    from bertrand.env.config.bertrand import WorkloadTopology
    from bertrand.env.kube.api.client import Kube
    from bertrand.env.kube.api.spec import PortProtocol
    from bertrand.env.kube.workload.capability import (
        WorkloadDeviceRequest,
        WorkloadSecretRequest,
        WorkloadSSHRequest,
    )


class WorkloadPortConfig(Protocol):
    """Typed view of one configured container port."""

    name: str
    port: int
    protocol: str


class WorkloadResourcesConfig(Protocol):
    """Typed view of configured container resource requirements."""

    @property
    def requests(self) -> Mapping[str, str]:
        """Resource requests keyed by Kubernetes resource name."""
        ...

    @property
    def limits(self) -> Mapping[str, str]:
        """Resource limits keyed by Kubernetes resource name."""
        ...


class WorkloadProbeHTTPConfig(Protocol):
    """Typed view of an HTTP probe target."""

    path: str
    port: int | str


class WorkloadProbeConfig(Protocol):
    """Typed view of a configured startup, readiness, or liveness probe."""

    cmd: Sequence[str]
    http: WorkloadProbeHTTPConfig | None
    tcp: int | str | None
    delay: int | None
    period: int | None
    timeout: int | None
    success: int | None
    failure: int | None


class WorkloadLinuxCapabilitiesConfig(Protocol):
    """Typed view of Linux capability additions and drops."""

    add: Sequence[str]
    drop: Sequence[str]


class WorkloadSeccompConfig(Protocol):
    """Typed view of a configured seccomp profile."""

    type: str
    profile: str | None


class WorkloadSecurityConfig(Protocol):
    """Typed view of one container security context."""

    privileged: bool | None
    allow_privilege_escalation: bool | None
    read_only_root_filesystem: bool | None
    run_as_user: int | None
    run_as_group: int | None
    run_as_non_root: bool | None
    capabilities: WorkloadLinuxCapabilitiesConfig
    seccomp: WorkloadSeccompConfig | None


class WorkloadContainerConfig(Protocol):
    """Typed view of one runnable workload container."""

    name: str
    cmd: Sequence[str]
    resources: WorkloadResourcesConfig | None
    startup: WorkloadProbeConfig | None
    readiness: WorkloadProbeConfig | None
    liveness: WorkloadProbeConfig | None
    security: WorkloadSecurityConfig | None
    ports: Sequence[WorkloadPortConfig]
    secrets: Sequence[WorkloadSecretRequest]
    ssh: Sequence[WorkloadSSHRequest]
    devices: Sequence[WorkloadDeviceRequest]


class WorkloadExecutionConfig(Protocol):
    """Typed view of workload execution and Job retry settings."""

    restart: str
    retries: int
    timeout: int | None
    ttl: int | None
    parallelism: int
    completions: int | None
    completion: str


class WorkloadScheduleHistoryConfig(Protocol):
    """Typed view of CronJob history retention settings."""

    success: int | None
    failure: int | None


class WorkloadScheduleConfig(Protocol):
    """Typed view of CronJob schedule settings."""

    cron: str
    timezone: str | None
    concurrency: str
    start_deadline: int | None
    suspend: bool | None
    history: WorkloadScheduleHistoryConfig


class WorkloadScaleConfig(Protocol):
    """Typed view of stable Deployment replica settings."""

    replicas: int


class WorkloadRolloutConfig(Protocol):
    """Typed view of Deployment rollout settings."""

    strategy: str
    max_surge: int | str | None
    max_unavailable: int | str | None
    min_ready: int | None
    timeout: int | None
    history: int | None
    paused: bool | None


class WorkloadRouteConfig(Protocol):
    """Typed view of one HTTP route intent."""

    @property
    def host(self) -> str:
        """External hostname matched by the route."""
        ...

    @property
    def port(self) -> str:
        """Named Service port targeted by the route."""
        ...

    @property
    def path(self) -> str:
        """HTTP path prefix matched by the route."""
        ...


class WorkloadNetworkConfig(Protocol):
    """Typed view of workload network policy and route settings."""

    @property
    def policy(self) -> str:
        """Configured workload network policy."""
        ...

    @property
    def routes(self) -> Sequence[WorkloadRouteConfig]:
        """Configured external HTTP route intents."""
        ...


class WorkloadTolerationConfig(Protocol):
    """Typed view of one Kubernetes toleration setting."""

    key: str | None
    operator: str
    value: str | None
    effect: str | None
    seconds: int | None


class WorkloadConfig(Protocol):
    """Typed view of the workload-related Bertrand project config."""

    containers: Sequence[WorkloadContainerConfig]
    execution: WorkloadExecutionConfig | None
    topology: WorkloadTopology
    termination_grace: int | None
    service_account: str | None
    node: str | None
    node_selector: Mapping[str, str]
    priority_class: str | None
    tolerations: Sequence[WorkloadTolerationConfig]

    @property
    def schedule(self) -> WorkloadScheduleConfig | None:
        """CronJob schedule config, if configured."""
        ...

    @property
    def scale(self) -> WorkloadScaleConfig | None:
        """Deployment replica config, if configured."""
        ...

    @property
    def rollout(self) -> WorkloadRolloutConfig | None:
        """Deployment rollout config, if configured."""
        ...

    @property
    def network(self) -> WorkloadNetworkConfig:
        """Workload network config."""
        ...


async def workload_pod_from_config(
    kube: Kube,
    *,
    config: WorkloadConfig | None,
    repo_id: str,
    worktree: str | PurePosixPath,
    worktree_id: str,
    image: str,
    host_id: str | None = None,
    timeout: float,
) -> WorkloadPod | None:
    """Render validated Bertrand workload config into a native pod intent.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    config : WorkloadConfig
        Validated `[tool.bertrand]` config object, or `None` for image/library-only
        worktrees.
    repo_id : str
        Stable repository UUID used to mount the managed Ceph repository PVC.
    worktree : str | PurePosixPath
        Relative worktree path inside the repository volume.
    worktree_id : str
        Persistent worktree UUID used for capability resolution.
    image : str
        Container image reference to run.
    host_id : str | None, optional
        Bertrand host UUID used for node-scoped capability resolution.
    timeout : float
        Maximum capability resolution budget in seconds.

    Returns
    -------
    WorkloadPod | None
        Pod intent, or `None` when no workload is configured.

    Raises
    ------
    ValueError
        If `image` or any workload container command is empty.
    """
    if config is None:
        return None
    image = image.strip()
    if not image:
        msg = "workload image cannot be empty"
        raise ValueError(msg)
    containers = tuple(config.containers)
    if not containers:
        return None

    identity = WorkloadIdentity(
        repo_id=repo_id,
        worktree_id=worktree_id,
        worktree=worktree,
    )
    capabilities = await resolve_workload_capabilities(
        kube,
        containers=containers,
        worktree_id=worktree_id,
        repo_id=repo_id,
        claim_owner=identity.name,
        host_id=host_id,
        node_name=config.node,
        timeout=timeout,
    )
    rendered: list[ContainerSpec] = []
    for container in containers:
        claims = tuple(
            claim.claim_name
            for claim in capabilities.resource_claims
            if claim.container_name == container.name
        )
        rendered.append(
            ContainerSpec(
                name=container.name,
                image=image,
                command=_workload_command(container.cmd, container=container.name),
                ports=_container_ports(container.ports),
                startup_probe=_probe(container.startup),
                readiness_probe=_probe(container.readiness),
                liveness_probe=_probe(container.liveness),
                volume_mounts=capabilities.mounts_by_container.get(
                    container.name,
                    (),
                ),
                security_context=_security_context(container.security),
                resources=_resources(container.resources, claims=claims),
            )
        )
    rendered_containers = tuple(rendered)
    return WorkloadPod(
        template=PodTemplateSpec(
            containers=rendered_containers,
            volumes=capabilities.volumes,
            resource_claims=tuple(
                claim.pod_claim() for claim in capabilities.resource_claims
            ),
            restart_policy=_restart_policy(config),
            service_account_name=config.service_account,
            node_selector=config.node_selector or None,
            node_name=config.node,
            tolerations=_tolerations(config.tolerations),
            priority_class_name=config.priority_class,
            termination_grace_period_seconds=config.termination_grace,
        ),
        primary_container=containers[0].name,
        repository=WorkloadRepository(
            repo_id=repo_id,
            worktree_id=worktree_id,
            worktree=worktree,
        ),
        resource_claim_templates=capabilities.resource_claims,
        runtime_env={WORKTREE_ID_ENV: worktree_id},
    )


def _workload_command(command: Sequence[str], *, container: str) -> tuple[str, ...]:
    out: list[str] = []
    for part in command:
        value = part.strip()
        if not value:
            msg = (
                f"workload command entries for container {container!r} cannot be empty"
            )
            raise ValueError(msg)
        out.append(value)
    if not out:
        msg = f"workload container {container!r} requires an explicit command"
        raise ValueError(msg)
    return tuple(out)


def _container_ports(
    ports: Sequence[WorkloadPortConfig],
) -> tuple[ContainerPortSpec, ...]:
    return tuple(
        ContainerPortSpec(
            name=port.name,
            container_port=port.port,
            protocol=cast("PortProtocol", port.protocol.upper()),
        )
        for port in ports
    )


def _resources(
    resources: WorkloadResourcesConfig | None,
    *,
    claims: tuple[str, ...],
) -> ContainerResourcesSpec | None:
    requests = dict(resources.requests) if resources is not None else {}
    limits = dict(resources.limits) if resources is not None else {}
    if not requests and not limits and not claims:
        return None
    return ContainerResourcesSpec(requests=requests, limits=limits, claims=claims)


def _probe(probe: WorkloadProbeConfig | None) -> ProbeSpec | None:
    if probe is None:
        return None
    kwargs = {
        "initial_delay_seconds": probe.delay,
        "period_seconds": probe.period,
        "timeout_seconds": probe.timeout,
        "success_threshold": probe.success,
        "failure_threshold": probe.failure,
    }
    if probe.cmd:
        return ProbeSpec.exec(command=_probe_command(probe.cmd), **kwargs)
    if probe.http is not None:
        return ProbeSpec.http(path=probe.http.path, port=probe.http.port, **kwargs)
    if probe.tcp is not None:
        return ProbeSpec.tcp(port=probe.tcp, **kwargs)
    msg = "workload probe must define cmd, http, or tcp"
    raise ValueError(msg)


def _probe_command(command: Sequence[str]) -> tuple[str, ...]:
    out: list[str] = []
    for part in command:
        value = part.strip()
        if not value:
            msg = "workload probe command entries cannot be empty"
            raise ValueError(msg)
        out.append(value)
    if not out:
        msg = "workload probe command cannot be empty"
        raise ValueError(msg)
    return tuple(out)


def _security_context(
    security: WorkloadSecurityConfig | None,
) -> SecurityContextSpec | None:
    if security is None:
        return None
    seccomp_type: str | None = None
    seccomp_profile: str | None = None
    if security.seccomp is not None:
        seccomp_type = _SECCOMP_TYPES[security.seccomp.type]
        seccomp_profile = security.seccomp.profile
    return SecurityContextSpec(
        privileged=security.privileged,
        run_as_user=security.run_as_user,
        run_as_group=security.run_as_group,
        run_as_non_root=security.run_as_non_root,
        read_only_root_filesystem=security.read_only_root_filesystem,
        allow_privilege_escalation=security.allow_privilege_escalation,
        capabilities_add=tuple(security.capabilities.add),
        capabilities_drop=tuple(security.capabilities.drop),
        seccomp_profile_type=seccomp_type,
        seccomp_profile_localhost_profile=seccomp_profile,
    )


def _restart_policy(config: WorkloadConfig) -> str | None:
    if config.topology.kind not in ("job", "cronjob"):
        return None
    restart = config.execution.restart if config.execution is not None else "never"
    if restart == "on-failure":
        return "OnFailure"
    return "Never"


def _tolerations(
    tolerations: Sequence[WorkloadTolerationConfig],
) -> tuple[TolerationSpec, ...]:
    return tuple(
        TolerationSpec(
            key=toleration.key,
            operator=_TOLERATION_OPERATORS[toleration.operator],
            value=toleration.value,
            effect=(
                _TOLERATION_EFFECTS[toleration.effect]
                if toleration.effect is not None
                else None
            ),
            toleration_seconds=toleration.seconds,
        )
        for toleration in tolerations
    )


_SECCOMP_TYPES = {
    "runtime-default": "RuntimeDefault",
    "unconfined": "Unconfined",
    "localhost": "Localhost",
}
_TOLERATION_OPERATORS = {
    "equal": "Equal",
    "exists": "Exists",
}
_TOLERATION_EFFECTS = {
    "no-schedule": "NoSchedule",
    "prefer-no-schedule": "PreferNoSchedule",
    "no-execute": "NoExecute",
}
