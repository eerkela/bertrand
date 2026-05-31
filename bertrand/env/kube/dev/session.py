"""Kubernetes dev-session substrate for Bertrand editor workflows."""

from __future__ import annotations

import asyncio
import hashlib
import uuid
from dataclasses import replace
from typing import TYPE_CHECKING

from bertrand.env.git import BERTRAND_NAMESPACE, Deadline
from bertrand.env.kube.build.project import project_image_spec
from bertrand.env.kube.build.refs import digest_from_ref, digest_ref
from bertrand.env.kube.dev.mailbox import (
    CODE_OPEN_RESOURCE,
    DEV_GROUP,
    code_open_host_labels,
)
from bertrand.env.kube.pod import Pod
from bertrand.env.kube.rbac import (
    CLUSTER_ROLE_BINDING_RESOURCE,
    CLUSTER_ROLE_RESOURCE,
    ROLE_BINDING_RESOURCE,
    ROLE_RESOURCE,
    rbac_role_manifest,
    rbac_service_account_binding_manifest,
)
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
    from bertrand.env.kube.api.spec import (
        ContainerSpec,
        PodTemplateSpec,
        PolicyRuleManifest,
    )
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


async def ensure_dev_backend(kube: Kube, *, deadline: Deadline) -> None:
    """Converge Kubernetes dev-session CRDs and RBAC.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    deadline : Deadline
        Maximum convergence budget in seconds.

    """
    await asyncio.gather(
        CODE_OPEN_RESOURCE.ensure_crd(kube, deadline=deadline),
        ServiceAccount.upsert(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=DEV_SERVICE_ACCOUNT,
            labels=_DEV_LABELS,
            deadline=deadline,
        ),
        ROLE_RESOURCE.upsert(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=DEV_SERVICE_ACCOUNT,
            manifest=rbac_role_manifest(
                kind="Role",
                namespace=BERTRAND_NAMESPACE,
                name=DEV_SERVICE_ACCOUNT,
                labels=_DEV_LABELS,
                rules=_dev_namespace_rules(),
            ),
            deadline=deadline,
        ),
        CLUSTER_ROLE_RESOURCE.upsert(
            kube,
            name=DEV_SERVICE_ACCOUNT,
            manifest=rbac_role_manifest(
                kind="ClusterRole",
                namespace=None,
                name=DEV_SERVICE_ACCOUNT,
                labels=_DEV_LABELS,
                rules=_dev_cluster_rules(),
            ),
            deadline=deadline,
        ),
    )
    await asyncio.gather(
        ROLE_BINDING_RESOURCE.upsert(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=DEV_SERVICE_ACCOUNT,
            manifest=rbac_service_account_binding_manifest(
                kind="RoleBinding",
                namespace=BERTRAND_NAMESPACE,
                name=DEV_SERVICE_ACCOUNT,
                role_kind="Role",
                role_name=DEV_SERVICE_ACCOUNT,
                service_account_name=DEV_SERVICE_ACCOUNT,
                service_account_namespace=BERTRAND_NAMESPACE,
                labels=_DEV_LABELS,
            ),
            deadline=deadline,
        ),
        CLUSTER_ROLE_BINDING_RESOURCE.upsert(
            kube,
            name=DEV_SERVICE_ACCOUNT,
            manifest=rbac_service_account_binding_manifest(
                kind="ClusterRoleBinding",
                namespace=None,
                name=DEV_SERVICE_ACCOUNT,
                role_kind="ClusterRole",
                role_name=DEV_SERVICE_ACCOUNT,
                service_account_name=DEV_SERVICE_ACCOUNT,
                service_account_namespace=BERTRAND_NAMESPACE,
                labels=_DEV_LABELS,
            ),
            deadline=deadline,
        ),
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
    deadline: Deadline,
    node: str | None = None,
) -> tuple[Pod, str]:
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
    deadline : Deadline
        Maximum creation budget in seconds.
    node : str | None, optional
        Optional Kubernetes node name used for node-scoped capabilities.

    Returns
    -------
    tuple[Pod, str]
        Created dev session Pod and primary container name.

    Raises
    ------
    RuntimeError
        If the configuration context is inactive.
    ValueError
        If no runnable container configuration exists or the image ref is invalid.
    """
    if not config:
        msg = "dev session creation requires an active config context"
        raise RuntimeError(msg)
    await ensure_dev_backend(kube, deadline=deadline)

    spec = project_image_spec(config, repo_id=repo_id)
    image = _validate_image_ref(image_ref, image=spec.image)
    bertrand = _project_workload_config(config)
    if bertrand is None or not bertrand.containers:
        msg = "`bertrand enter` and `bertrand code` require configured containers"
        raise ValueError(msg)
    workload = await _render_project_workload_pod(
        kube,
        workload_config=bertrand,
        image_spec=spec,
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
        deadline=deadline,
    )
    name = dev_session_name(session_id)
    labels = _dev_session_labels(session_id=session_id, host_id=host_id)
    template = _dev_session_template(
        workload,
        command=command,
        interactive=interactive,
        session_id=session_id,
        host_id=host_id,
        labels=labels,
    )
    pod = await Pod.create(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=name,
        labels=labels,
        pod_template=template,
        deadline=deadline,
    )
    return pod, workload.primary_container


async def wait_dev_session_running(
    kube: Kube,
    pod: Pod,
    *,
    primary_container: str,
    deadline: Deadline,
) -> Pod:
    """Wait until a dev Pod is running and attachable.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    pod : Pod
        Created development Pod.
    primary_container : str
        Container name targeted by shell attachment and editor requests.
    deadline : Deadline
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
    current = pod
    while True:
        remaining = deadline.remaining
        if remaining <= 0:
            msg = f"timed out waiting for dev session Pod {pod.name!r}"
            raise TimeoutError(msg)
        live = await current.refresh(kube, deadline=deadline)
        if live is None:
            msg = f"dev session Pod {pod.name!r} disappeared before running"
            raise OSError(msg)
        if live.is_terminal:
            details = "\n".join(live.status_diagnostics)
            msg = f"dev session Pod {pod.name!r} exited before it was attachable"
            if details:
                msg = f"{msg}\n{details}"
            raise OSError(msg)
        if live.phase == "Running" and live.container_running(primary_container):
            return live
        current = live
        await deadline.sleep(DEV_SESSION_POLL_SECONDS)


async def delete_dev_backend_state(
    kube: Kube,
    *,
    host_id: str | None,
    deadline: Deadline,
) -> None:
    """Delete volatile dev-session state for this host.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    host_id : str | None
        Optional host identity. If omitted, all Bertrand-managed dev-session Pods
        are left untouched.
    deadline : Deadline
        Maximum cleanup budget in seconds.

    """
    if host_id is None:
        return
    pod_labels = {
        DEV_SESSION_LABEL: DEV_SESSION_LABEL_VALUE,
        DEV_SESSION_HOST_LABEL: _hash_label(host_id),
    }
    pods = await Pod.list(
        kube,
        namespaces=(BERTRAND_NAMESPACE,),
        labels=pod_labels,
        deadline=deadline,
    )
    for pod in pods:
        await pod.delete(
            kube,
            deadline=deadline,
            grace_period_seconds=1,
        )

    try:
        requests = await CODE_OPEN_RESOURCE.list(
            kube,
            labels=code_open_host_labels(host_id),
            deadline=deadline,
        )
    except OSError:
        return

    for request in requests:
        await CODE_OPEN_RESOURCE.delete_by_name(
            kube,
            name=request.name,
            deadline=deadline,
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
) -> ContainerSpec:
    env = list(container.env)
    for entry in (
        {"name": DEV_SESSION_ENV, "value": session_id},
        {"name": DEV_HOST_ID_ENV, "value": host_id},
        {"name": DEV_PRIMARY_CONTAINER_ENV, "value": primary_container},
        {
            "name": DEV_POD_NAME_ENV,
            "valueFrom": {"fieldRef": {"fieldPath": "metadata.name"}},
        },
    ):
        env = [current for current in env if current.get("name") != entry["name"]]
        env.append(entry)
    return replace(container, env=tuple(env))


def _dev_session_labels(*, session_id: str, host_id: str) -> dict[str, str]:
    return {
        **_DEV_LABELS,
        DEV_SESSION_LABEL: DEV_SESSION_LABEL_VALUE,
        DEV_SESSION_ID_LABEL: _label_value(session_id),
        DEV_SESSION_HOST_LABEL: _hash_label(host_id),
    }


def _dev_namespace_rules() -> tuple[PolicyRuleManifest, ...]:
    read = ["get", "list", "watch"]
    mutate = ["get", "list", "watch", "create", "update", "patch", "delete"]
    return (
        {
            "apiGroups": [DEV_GROUP],
            "resources": ["codeopenrequests"],
            "verbs": ["create", "get", "list", "watch"],
        },
        {
            "apiGroups": [""],
            "resources": ["pods", "services", "configmaps"],
            "verbs": mutate,
        },
        {"apiGroups": [""], "resources": ["pods/log"], "verbs": read},
        {"apiGroups": [""], "resources": ["pods/attach"], "verbs": ["get", "create"]},
        {
            "apiGroups": [""],
            "resources": ["secrets", "persistentvolumeclaims"],
            "verbs": read,
        },
        {
            "apiGroups": ["apps"],
            "resources": ["deployments", "daemonsets"],
            "verbs": mutate,
        },
        {
            "apiGroups": ["batch"],
            "resources": ["jobs", "cronjobs"],
            "verbs": mutate,
        },
        {
            "apiGroups": ["networking.k8s.io"],
            "resources": ["networkpolicies"],
            "verbs": mutate,
        },
        {
            "apiGroups": ["gateway.networking.k8s.io"],
            "resources": ["gateways", "httproutes"],
            "verbs": mutate,
        },
        {
            "apiGroups": ["resource.k8s.io"],
            "resources": ["resourceclaims", "resourceclaimtemplates"],
            "verbs": mutate,
        },
        {
            "apiGroups": ["coordination.k8s.io"],
            "resources": ["leases"],
            "verbs": mutate,
        },
        {
            "apiGroups": ["build.bertrand.dev"],
            "resources": ["buildkitbuilds"],
            "verbs": ["create", "get", "list", "watch"],
        },
        {
            "apiGroups": ["dra.bertrand.dev"],
            "resources": ["bertranddevices"],
            "verbs": read,
        },
        {
            "apiGroups": ["ceph.bertrand.dev"],
            "resources": ["cephrepositorystates"],
            "verbs": read,
        },
    )


def _dev_cluster_rules() -> tuple[PolicyRuleManifest, ...]:
    read = ["get", "list", "watch"]
    return (
        {"apiGroups": [""], "resources": ["nodes"], "verbs": read},
        {
            "apiGroups": ["gateway.networking.k8s.io"],
            "resources": ["gatewayclasses"],
            "verbs": ["get", "list", "watch", "create", "update", "patch"],
        },
        {
            "apiGroups": ["resource.k8s.io"],
            "resources": ["deviceclasses", "resourceslices"],
            "verbs": read,
        },
        {
            "apiGroups": ["node.bertrand.dev"],
            "resources": ["bertrandnodes"],
            "verbs": read,
        },
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
