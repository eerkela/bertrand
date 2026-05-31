"""Adapters from validated Bertrand workload config to native workload intent."""

from __future__ import annotations

from typing import TYPE_CHECKING, cast

from bertrand.env.kube.api.spec import ContainerSpec, PodTemplateSpec
from bertrand.env.kube.workload.base import (
    WorkloadIdentity,
    WorkloadPod,
)
from bertrand.env.kube.workload.capability import resolve_workload_capabilities

if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence
    from pathlib import PurePosixPath

    from bertrand.env.config.bertrand import BertrandModel
    from bertrand.env.git import Deadline
    from bertrand.env.kube.api.client import Kube
    from bertrand.env.kube.api.spec import ContainerPortManifest, PortProtocol


async def workload_pod_from_config(
    kube: Kube,
    *,
    config: BertrandModel | None,
    repo_id: str,
    worktree: str | PurePosixPath,
    worktree_id: str,
    image: str,
    host_id: str | None = None,
    deadline: Deadline,
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
    deadline : Deadline
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
        deadline=deadline,
    )
    rendered: list[ContainerSpec] = []
    for container in containers:
        claims = capabilities.claim_names_by_container.get(container.name, ())
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
            resource_claims=capabilities.resource_claims,
            restart_policy=_restart_policy(config),
            service_account_name=config.service_account,
            node_selector=config.node_selector or None,
            node_name=config.node,
            tolerations=_tolerations(config.tolerations),
            priority_class_name=config.priority_class,
            termination_grace_period_seconds=config.termination_grace,
        ),
        primary_container=containers[0].name,
        identity=identity,
        resource_claim_capabilities_by_container=(
            capabilities.claim_capabilities_by_container
        ),
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
    ports: Sequence[BertrandModel.Port],
) -> tuple[ContainerPortManifest, ...]:
    return tuple(
        cast(
            "ContainerPortManifest",
            {
                "name": port.name,
                "containerPort": port.port,
                "protocol": cast("PortProtocol", port.protocol.upper()),
            },
        )
        for port in ports
    )


def _resources(
    resources: BertrandModel.Resources | None,
    *,
    claims: tuple[str, ...],
) -> dict[str, object] | None:
    requests = dict(resources.requests) if resources is not None else {}
    limits = dict(resources.limits) if resources is not None else {}
    if not requests and not limits and not claims:
        return None
    payload: dict[str, object] = {}
    if requests:
        payload["requests"] = requests
    if limits:
        payload["limits"] = limits
    claim_refs = [{"name": claim} for claim in claims if claim.strip()]
    if claim_refs:
        payload["claims"] = claim_refs
    return payload


def _probe(probe: BertrandModel.Probe | None) -> dict[str, object] | None:
    if probe is None:
        return None
    settings = _probe_settings(probe)
    if probe.cmd:
        return {"exec": {"command": list(_probe_command(probe.cmd))}, **settings}
    if probe.http is not None:
        return {
            "httpGet": {"path": probe.http.path, "port": probe.http.port},
            **settings,
        }
    if probe.tcp is not None:
        return {"tcpSocket": {"port": probe.tcp}, **settings}
    msg = "workload probe must define cmd, http, or tcp"
    raise ValueError(msg)


def _probe_settings(probe: BertrandModel.Probe) -> dict[str, object]:
    return {
        key: value
        for key, value in {
            "initialDelaySeconds": probe.delay,
            "periodSeconds": probe.period,
            "timeoutSeconds": probe.timeout,
            "successThreshold": probe.success,
            "failureThreshold": probe.failure,
        }.items()
        if value is not None
    }


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
    security: BertrandModel.Security | None,
) -> Mapping[str, object] | None:
    if security is None:
        return None
    payload: dict[str, object] = {}
    if security.privileged is not None:
        payload["privileged"] = security.privileged
    if security.run_as_user is not None:
        payload["runAsUser"] = security.run_as_user
    if security.run_as_group is not None:
        payload["runAsGroup"] = security.run_as_group
    if security.run_as_non_root is not None:
        payload["runAsNonRoot"] = security.run_as_non_root
    if security.read_only_root_filesystem is not None:
        payload["readOnlyRootFilesystem"] = security.read_only_root_filesystem
    if security.allow_privilege_escalation is not None:
        payload["allowPrivilegeEscalation"] = security.allow_privilege_escalation
    capabilities: dict[str, object] = {}
    if security.capabilities.add:
        capabilities["add"] = list(security.capabilities.add)
    if security.capabilities.drop:
        capabilities["drop"] = list(security.capabilities.drop)
    if capabilities:
        payload["capabilities"] = capabilities
    if security.seccomp is not None:
        seccomp: dict[str, object] = {"type": _SECCOMP_TYPES[security.seccomp.type]}
        if security.seccomp.profile is not None:
            seccomp["localhostProfile"] = security.seccomp.profile
        payload["seccompProfile"] = seccomp
    return payload


def _restart_policy(config: BertrandModel) -> str | None:
    if config.topology.kind not in ("job", "cronjob"):
        return None
    restart = config.execution.restart if config.execution is not None else "never"
    if restart == "on-failure":
        return "OnFailure"
    return "Never"


def _tolerations(
    tolerations: Sequence[BertrandModel.Toleration],
) -> tuple[Mapping[str, object], ...]:
    return tuple(
        {
            key: value
            for key, value in {
                "key": toleration.key,
                "operator": _TOLERATION_OPERATORS[toleration.operator],
                "value": toleration.value,
                "effect": (
                    _TOLERATION_EFFECTS[toleration.effect]
                    if toleration.effect is not None
                    else None
                ),
                "tolerationSeconds": toleration.seconds,
            }.items()
            if value is not None
        }
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
