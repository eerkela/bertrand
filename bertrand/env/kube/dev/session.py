"""Kubernetes dev-session substrate for Bertrand editor workflows."""

from __future__ import annotations

import asyncio
import hashlib
import uuid
from dataclasses import dataclass, replace
from typing import TYPE_CHECKING

from bertrand.env.git import (
    BERTRAND_NAMESPACE,
    CONTAINER_ID_ENV,
    IMAGE_ID_ENV,
    Deadline,
)
from bertrand.env.kube.api.spec import EnvVarSpec, PolicyRuleSpec
from bertrand.env.kube.build.project import project_image_build
from bertrand.env.kube.build.refs import digest_from_ref, digest_ref
from bertrand.env.kube.dev.mailbox import (
    DEV_GROUP,
    code_open_host_labels,
    delete_code_open_request,
    ensure_code_open_request_crd,
    list_code_open_requests,
)
from bertrand.env.kube.pod import Pod
from bertrand.env.kube.rbac import ClusterRole, ClusterRoleBinding, Role, RoleBinding
from bertrand.env.kube.service_account import ServiceAccount
from bertrand.env.kube.workload.controller import ensure_workload_claim_templates
from bertrand.env.kube.workload.project import (
    _project_workload_config,
    _render_project_workload_pod,
)

if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence

    from bertrand.env.config.core import Config
    from bertrand.env.kube.api.client import Kube
    from bertrand.env.kube.api.spec import ContainerSpec, PodTemplateSpec
    from bertrand.env.kube.workload.base import WorkloadPod

DEV_SERVICE_ACCOUNT = "bertrand-dev"
DEV_SESSION_LABEL = "bertrand.dev/dev-session"
DEV_SESSION_LABEL_VALUE = "v1"
DEV_SESSION_ID_LABEL = "bertrand.dev/dev-session-id"
DEV_SESSION_HOST_LABEL = "bertrand.dev/dev-session-host"
DEV_SESSION_NAME_PREFIX = "bertrand-dev-"
DEV_SESSION_POLL_SECONDS = 0.5
DEV_SESSION_API_TIMEOUT_SECONDS = 5.0
DEV_SESSION_ENV = "BERTRAND_DEV_SESSION"
DEV_HOST_ID_ENV = "BERTRAND_DEV_HOST_ID"
DEV_POD_NAME_ENV = "BERTRAND_DEV_POD_NAME"
DEV_PRIMARY_CONTAINER_ENV = "BERTRAND_DEV_PRIMARY_CONTAINER"

_DEV_LABELS = {
    "app.kubernetes.io/part-of": "bertrand",
    "app.kubernetes.io/component": "dev",
}


@dataclass(frozen=True)
class DevSession:
    """Generated Kubernetes Pod backing a Bertrand development session.

    Parameters
    ----------
    session_id : str
        Host/editor bridge session identifier.
    host_id : str
        Durable Bertrand host identity.
    pod : Pod
        Created development Pod.
    primary_container : str
        Container name targeted by shell attachment and editor requests.
    """

    session_id: str
    host_id: str
    pod: Pod
    primary_container: str

    @property
    def name(self) -> str:
        """Return the generated Pod name.

        Returns
        -------
        str
            Kubernetes Pod name.
        """
        return self.pod.name

    async def wait_running(self, kube: Kube, *, timeout: float) -> Pod:
        """Wait until the dev Pod is running and attachable.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum wait budget in seconds.

        Returns
        -------
        Pod
            Refreshed running Pod.

        Raises
        ------
        TimeoutError
            If the Pod does not become running before the timeout.
        OSError
            If the Pod reaches a terminal phase before it is attachable.
        """
        if timeout <= 0:
            msg = f"timed out waiting for dev session Pod {self.name!r}"
            raise TimeoutError(msg)
        deadline = Deadline.from_timeout(
            timeout,
            message=f"timed out waiting for dev session Pod {self.name!r}",
        )
        current = self.pod
        while True:
            remaining = deadline.remaining()
            if remaining <= 0:
                msg = f"timed out waiting for dev session Pod {self.name!r}"
                raise TimeoutError(msg)
            live = await current.refresh(kube, timeout=remaining)
            if live is None:
                msg = f"dev session Pod {self.name!r} disappeared before running"
                raise OSError(msg)
            if live.is_terminal:
                details = "\n".join(live.status_diagnostics)
                msg = f"dev session Pod {self.name!r} exited before it was attachable"
                if details:
                    msg = f"{msg}\n{details}"
                raise OSError(msg)
            if live.phase == "Running" and live.container_running(
                self.primary_container
            ):
                return live
            current = live
            await asyncio.sleep(deadline.bounded(DEV_SESSION_POLL_SECONDS))

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete the generated dev Pod.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum deletion budget in seconds.
        """
        await self.pod.delete(kube, timeout=timeout, grace_period_seconds=1)


async def ensure_dev_backend(kube: Kube, *, timeout: float) -> None:
    """Converge Kubernetes dev-session CRDs and RBAC.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum convergence budget in seconds.

    Raises
    ------
    TimeoutError
        If backend convergence exceeds the budget.
    """
    if timeout <= 0:
        msg = "dev-session backend convergence timeout must be non-negative"
        raise TimeoutError(msg)
    deadline = Deadline.from_timeout(
        timeout,
        message="dev-session backend convergence timeout must be non-negative",
    )
    await ensure_code_open_request_crd(kube, timeout=deadline.remaining())
    await ServiceAccount.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=DEV_SERVICE_ACCOUNT,
        labels=_DEV_LABELS,
        timeout=deadline.remaining(),
    )
    await Role.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=DEV_SERVICE_ACCOUNT,
        labels=_DEV_LABELS,
        rules=_dev_namespace_rules(),
        timeout=deadline.remaining(),
    )
    await RoleBinding.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=DEV_SERVICE_ACCOUNT,
        role_name=DEV_SERVICE_ACCOUNT,
        service_account_name=DEV_SERVICE_ACCOUNT,
        service_account_namespace=BERTRAND_NAMESPACE,
        labels=_DEV_LABELS,
        timeout=deadline.remaining(),
    )
    await ClusterRole.upsert(
        kube,
        name=DEV_SERVICE_ACCOUNT,
        labels=_DEV_LABELS,
        rules=_dev_cluster_rules(),
        timeout=deadline.remaining(),
    )
    await ClusterRoleBinding.upsert(
        kube,
        name=DEV_SERVICE_ACCOUNT,
        role_name=DEV_SERVICE_ACCOUNT,
        service_account_name=DEV_SERVICE_ACCOUNT,
        service_account_namespace=BERTRAND_NAMESPACE,
        labels=_DEV_LABELS,
        timeout=deadline.remaining(),
    )


async def create_project_dev_session(
    kube: Kube,
    *,
    config: Config,
    repo_id: str,
    image_ref: str,
    session_id: str,
    host_id: str,
    command: Sequence[str],
    interactive: bool,
    timeout: float,
    node: str | None = None,
) -> DevSession:
    """Create one generated Kubernetes Pod for a development session.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    config : Config
        Active project configuration context containing `[tool.bertrand]`.
    repo_id : str
        Stable repository UUID.
    image_ref : str
        Digest-pinned image reference to run.
    session_id : str
        Host/editor bridge session identifier.
    host_id : str
        Durable Bertrand host identity.
    command : Sequence[str]
        Primary-container command override.
    interactive : bool
        Whether to render the primary container for TTY attachment.
    timeout : float
        Maximum creation budget in seconds.
    node : str | None, optional
        Optional Kubernetes node name used for node-scoped capabilities.

    Returns
    -------
    DevSession
        Created dev session.

    Raises
    ------
    RuntimeError
        If the configuration context is inactive.
    TimeoutError
        If the session cannot be created before the timeout.
    ValueError
        If no runnable container configuration exists or the image ref is invalid.
    """
    if timeout <= 0:
        msg = "dev session creation timeout must be positive"
        raise TimeoutError(msg)
    if not config:
        msg = "dev session creation requires an active config context"
        raise RuntimeError(msg)
    deadline = Deadline.from_timeout(
        timeout,
        message="dev session creation timeout must be positive",
    )
    await ensure_dev_backend(kube, timeout=deadline.remaining())

    build = project_image_build(config, repo_id=repo_id)
    image = _validate_image_ref(image_ref, image=build.identity.image)
    bertrand = _project_workload_config(config)
    if bertrand is None or not bertrand.containers:
        msg = "`bertrand enter` and `bertrand code` require configured containers"
        raise ValueError(msg)
    workload = await _render_project_workload_pod(
        kube,
        workload_config=bertrand,
        image_identity=build.identity,
        image=image,
        node=node,
        deadline=deadline,
    )
    if workload is None:
        msg = "dev session creation requires a rendered workload pod"
        raise ValueError(msg)
    await ensure_workload_claim_templates(
        kube,
        workload=workload,
        timeout=deadline.remaining(),
    )
    name = dev_session_name(session_id)
    labels = _dev_session_labels(session_id=session_id, host_id=host_id)
    template = _dev_session_template(
        workload,
        command=command,
        interactive=interactive,
        session_id=session_id,
        host_id=host_id,
        image_ref=image,
        labels=labels,
    )
    pod = await Pod.create(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=name,
        labels=labels,
        pod_template=template,
        timeout=deadline.remaining(),
    )
    return DevSession(
        session_id=session_id,
        host_id=host_id,
        pod=pod,
        primary_container=workload.primary_container,
    )


async def delete_dev_backend_state(
    kube: Kube,
    *,
    host_id: str | None,
    timeout: float,
) -> None:
    """Delete volatile dev-session state for this host.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    host_id : str | None
        Optional host identity. If omitted, all Bertrand-managed dev-session Pods
        are left untouched.
    timeout : float
        Maximum cleanup budget in seconds.

    Raises
    ------
    TimeoutError
        If cleanup cannot start before the timeout.
    """
    if timeout <= 0:
        msg = "dev-session backend cleanup timeout must be non-negative"
        raise TimeoutError(msg)
    if host_id is None:
        return
    deadline = Deadline.from_timeout(
        timeout,
        message="dev-session backend cleanup timeout must be non-negative",
    )
    pod_labels = {
        DEV_SESSION_LABEL: DEV_SESSION_LABEL_VALUE,
        DEV_SESSION_HOST_LABEL: _hash_label(host_id),
    }
    pods = await Pod.list(
        kube,
        namespaces=(BERTRAND_NAMESPACE,),
        labels=pod_labels,
        timeout=deadline.remaining(),
    )
    for pod in pods:
        await pod.delete(
            kube,
            timeout=deadline.remaining(),
            grace_period_seconds=1,
        )

    try:
        requests = await list_code_open_requests(
            kube,
            labels=code_open_host_labels(host_id),
            timeout=deadline.remaining(),
        )
    except OSError:
        return

    for request in requests:
        await delete_code_open_request(
            kube,
            record=request,
            timeout=deadline.remaining(),
        )


def new_session_id() -> str:
    """Return a fresh dev-session identifier.

    Returns
    -------
    str
        Random UUID hex string.
    """
    return uuid.uuid4().hex


def dev_session_name(session_id: str) -> str:
    """Return the generated Pod name for a session.

    Parameters
    ----------
    session_id : str
        Session identifier.

    Returns
    -------
    str
        DNS-label-safe Pod name.
    """
    return f"{DEV_SESSION_NAME_PREFIX}{_hash_label(session_id, chars=48)}"


def _dev_session_template(
    workload: WorkloadPod,
    *,
    command: Sequence[str],
    interactive: bool,
    session_id: str,
    host_id: str,
    image_ref: str,
    labels: Mapping[str, str],
) -> PodTemplateSpec:
    template = workload.pod_template(
        primary_command=command,
        interactive=interactive,
    )
    containers = tuple(
        _dev_container(
            container,
            session_id=session_id,
            host_id=host_id,
            primary_container=workload.primary_container,
            image_ref=image_ref,
        )
        for container in template.containers
    )
    template_labels = dict(template.labels)
    template_labels.update(labels)
    return replace(
        template,
        containers=containers,
        labels=template_labels,
        restart_policy="Never",
        service_account_name=DEV_SERVICE_ACCOUNT,
        automount_service_account_token=True,
    )


def _dev_container(
    container: ContainerSpec,
    *,
    session_id: str,
    host_id: str,
    primary_container: str,
    image_ref: str,
) -> ContainerSpec:
    env = list(container.env)
    for entry in (
        EnvVarSpec(name=DEV_SESSION_ENV, value=session_id),
        EnvVarSpec(name=DEV_HOST_ID_ENV, value=host_id),
        EnvVarSpec(name=DEV_PRIMARY_CONTAINER_ENV, value=primary_container),
        EnvVarSpec(name=CONTAINER_ID_ENV, field_path="metadata.name"),
        EnvVarSpec(name=DEV_POD_NAME_ENV, field_path="metadata.name"),
        EnvVarSpec(name=IMAGE_ID_ENV, value=image_ref),
    ):
        env = [current for current in env if current.name != entry.name]
        env.append(entry)
    return replace(container, env=tuple(env))


def _dev_session_labels(*, session_id: str, host_id: str) -> dict[str, str]:
    return {
        **_DEV_LABELS,
        DEV_SESSION_LABEL: DEV_SESSION_LABEL_VALUE,
        DEV_SESSION_ID_LABEL: _label_value(session_id),
        DEV_SESSION_HOST_LABEL: _hash_label(host_id),
    }


def _dev_namespace_rules() -> tuple[PolicyRuleSpec, ...]:
    read = ("get", "list", "watch")
    mutate = ("get", "list", "watch", "create", "update", "patch", "delete")
    return (
        PolicyRuleSpec(
            api_groups=(DEV_GROUP,),
            resources=("codeopenrequests",),
            verbs=("create", "get", "list", "watch"),
        ),
        PolicyRuleSpec(
            api_groups=("",),
            resources=("pods", "services", "configmaps"),
            verbs=mutate,
        ),
        PolicyRuleSpec(
            api_groups=("",),
            resources=("pods/log",),
            verbs=("get", "list", "watch"),
        ),
        PolicyRuleSpec(
            api_groups=("",),
            resources=("pods/attach",),
            verbs=("get", "create"),
        ),
        PolicyRuleSpec(
            api_groups=("",),
            resources=("secrets", "persistentvolumeclaims"),
            verbs=read,
        ),
        PolicyRuleSpec(
            api_groups=("apps",),
            resources=("deployments", "daemonsets"),
            verbs=mutate,
        ),
        PolicyRuleSpec(
            api_groups=("batch",),
            resources=("jobs", "cronjobs"),
            verbs=mutate,
        ),
        PolicyRuleSpec(
            api_groups=("networking.k8s.io",),
            resources=("networkpolicies",),
            verbs=mutate,
        ),
        PolicyRuleSpec(
            api_groups=("gateway.networking.k8s.io",),
            resources=("gateways", "httproutes"),
            verbs=mutate,
        ),
        PolicyRuleSpec(
            api_groups=("resource.k8s.io",),
            resources=("resourceclaims", "resourceclaimtemplates"),
            verbs=mutate,
        ),
        PolicyRuleSpec(
            api_groups=("coordination.k8s.io",),
            resources=("leases",),
            verbs=mutate,
        ),
        PolicyRuleSpec(
            api_groups=("build.bertrand.dev",),
            resources=("buildkitbuilds",),
            verbs=("create", "get", "list", "watch"),
        ),
        PolicyRuleSpec(
            api_groups=("build.bertrand.dev",),
            resources=("bertrandimages",),
            verbs=read,
        ),
        PolicyRuleSpec(
            api_groups=("dra.bertrand.dev",),
            resources=("bertranddevices",),
            verbs=read,
        ),
        PolicyRuleSpec(
            api_groups=("ceph.bertrand.dev",),
            resources=("cephrepositoryvolumes", "cephrepositoryworktrees"),
            verbs=read,
        ),
    )


def _dev_cluster_rules() -> tuple[PolicyRuleSpec, ...]:
    read = ("get", "list", "watch")
    return (
        PolicyRuleSpec(
            api_groups=("",),
            resources=("nodes",),
            verbs=read,
        ),
        PolicyRuleSpec(
            api_groups=("gateway.networking.k8s.io",),
            resources=("gatewayclasses",),
            verbs=("get", "list", "watch", "create", "update", "patch"),
        ),
        PolicyRuleSpec(
            api_groups=("resource.k8s.io",),
            resources=("deviceclasses", "resourceslices"),
            verbs=read,
        ),
        PolicyRuleSpec(
            api_groups=("node.bertrand.dev",),
            resources=("bertrandnodes",),
            verbs=read,
        ),
    )


def _validate_image_ref(image_ref: str, *, image: str) -> str:
    try:
        digest = digest_from_ref(image_ref, label="dev session image_ref")
        expected = digest_ref(image, digest)
    except ValueError as err:
        msg = f"dev session image_ref must be digest-pinned: {image_ref!r}"
        raise ValueError(msg) from err
    value = image_ref.strip()
    if value != expected:
        msg = (
            "dev session image_ref must match the configured project image "
            f"repository: expected {expected!r}, got {image_ref!r}"
        )
        raise ValueError(msg)
    return value


def _label_value(value: str) -> str:
    text = value.strip()
    if len(text) <= 63:
        return text
    return _hash_label(text)


def _hash_label(value: str, *, chars: int = 16) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()[:chars]
