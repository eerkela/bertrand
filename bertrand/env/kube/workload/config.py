"""Adapters from validated Bertrand workload config to native workload intent."""

from __future__ import annotations

from typing import TYPE_CHECKING, Protocol, cast

from bertrand.env.git.bertrand_git import ENV_ID_ENV
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


class _WorkloadPort(Protocol):
    name: str
    port: int
    protocol: str


class _WorkloadResources(Protocol):
    @property
    def requests(self) -> Mapping[str, str]: ...

    @property
    def limits(self) -> Mapping[str, str]: ...


class _WorkloadProbeHTTP(Protocol):
    path: str
    port: int | str


class _WorkloadProbe(Protocol):
    cmd: Sequence[str]
    http: _WorkloadProbeHTTP | None
    tcp: int | str | None
    delay: int | None
    period: int | None
    timeout: int | None
    success: int | None
    failure: int | None


class _WorkloadCapabilities(Protocol):
    add: Sequence[str]
    drop: Sequence[str]


class _WorkloadSeccomp(Protocol):
    type: str
    profile: str | None


class _WorkloadSecurity(Protocol):
    privileged: bool | None
    allow_privilege_escalation: bool | None
    read_only_root_filesystem: bool | None
    run_as_user: int | None
    run_as_group: int | None
    run_as_non_root: bool | None
    capabilities: _WorkloadCapabilities
    seccomp: _WorkloadSeccomp | None


class _WorkloadContainer(Protocol):
    name: str
    cmd: Sequence[str]
    resources: _WorkloadResources | None
    startup: _WorkloadProbe | None
    readiness: _WorkloadProbe | None
    liveness: _WorkloadProbe | None
    security: _WorkloadSecurity | None
    ports: Sequence[_WorkloadPort]
    secrets: Sequence[WorkloadSecretRequest]
    ssh: Sequence[WorkloadSSHRequest]
    devices: Sequence[WorkloadDeviceRequest]


class _WorkloadExecution(Protocol):
    restart: str


class _WorkloadToleration(Protocol):
    key: str | None
    operator: str
    value: str | None
    effect: str | None
    seconds: int | None


class _WorkloadConfig(Protocol):
    containers: Sequence[_WorkloadContainer]
    execution: _WorkloadExecution | None
    topology: WorkloadTopology
    termination_grace: int | None
    service_account: str | None
    node: str | None
    node_selector: Mapping[str, str]
    priority_class: str | None
    tolerations: Sequence[_WorkloadToleration]


async def workload_pod_from_config(
    kube: Kube,
    *,
    config: _WorkloadConfig | None,
    repo_id: str,
    worktree: str | PurePosixPath,
    env_id: str,
    image: str,
    node: str | None = None,
    timeout: float,
) -> WorkloadPod | None:
    """Render validated Bertrand workload config into a native pod intent.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    config : _WorkloadConfig
        Validated `[tool.bertrand]` config object, or `None` for image/library-only
        worktrees.
    repo_id : str
        Stable repository UUID used to mount the managed Ceph repository PVC.
    worktree : str | PurePosixPath
        Relative worktree path inside the repository volume.
    env_id : str
        Environment UUID used for capability resolution.
    image : str
        Container image reference to run.
    node : str | None, optional
        Kubernetes node name used for node-scoped capability resolution.
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

    identity = WorkloadIdentity(repo_id=repo_id, worktree=worktree)
    capabilities = await resolve_workload_capabilities(
        kube,
        containers=containers,
        env_id=env_id,
        claim_owner=identity.name,
        node=node,
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
        repository=WorkloadRepository(repo_id=repo_id, worktree=worktree),
        resource_claim_templates=capabilities.resource_claims,
        runtime_env={ENV_ID_ENV: env_id},
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
    ports: Sequence[_WorkloadPort],
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
    resources: _WorkloadResources | None,
    *,
    claims: tuple[str, ...],
) -> ContainerResourcesSpec | None:
    requests = dict(resources.requests) if resources is not None else {}
    limits = dict(resources.limits) if resources is not None else {}
    if not requests and not limits and not claims:
        return None
    return ContainerResourcesSpec(requests=requests, limits=limits, claims=claims)


def _probe(probe: _WorkloadProbe | None) -> ProbeSpec | None:
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
    security: _WorkloadSecurity | None,
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


def _restart_policy(config: _WorkloadConfig) -> str | None:
    if config.topology.kind not in ("job", "cronjob"):
        return None
    restart = config.execution.restart if config.execution is not None else "never"
    if restart == "on-failure":
        return "OnFailure"
    return "Never"


def _tolerations(
    tolerations: Sequence[_WorkloadToleration],
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
