"""Wrappers for the Kubernetes Pod API and related pod-scoped operations."""

from __future__ import annotations

import asyncio
from collections.abc import Mapping as MappingABC
from dataclasses import dataclass
from typing import TYPE_CHECKING, Any, Self

import kubernetes
from kubernetes.stream import stream as kubernetes_stream

from bertrand.env.git import Deadline

from .api._helpers import _validate_delete_status
from .api._render import _pod_template_manifest
from .api.metadata import NamespacedKubeMetadata
from .api.resource import ResourceClient

if TYPE_CHECKING:
    import builtins
    from collections.abc import AsyncIterator, Collection, Mapping

    from .api.client import Kube
    from .api.spec import PodTemplateSpec
    from .api.watch import WatchEvent

POD_MIRROR_ANNOTATION = "kubernetes.io/config.mirror"
POD_SUPPORTED_CONTROLLER_KINDS = frozenset(
    {
        "ReplicationController",
        "ReplicaSet",
        "StatefulSet",
        "DaemonSet",
        "Job",
    }
)
POD_ACTIVE_PHASES = frozenset({"Pending", "Running", "Unknown"})
POD_TERMINAL_PHASES = frozenset({"Succeeded", "Failed"})
POD_WAIT_POLL_INTERVAL_SECONDS = 0.5


def _join_status_detail(reason: str, message: str) -> str:
    reason = reason.strip()
    message = message.strip()
    if reason and message:
        return f"{reason}: {message}"
    return reason or message


def _container_status_diagnostics(
    label: str,
    group: str,
    statuses: Collection[kubernetes.client.V1ContainerStatus],
) -> tuple[str, ...]:
    out: list[str] = []
    for status in statuses:
        name = (status.name or "").strip() or "<unknown>"
        image = (status.image or "").strip()
        prefix = f"{label} {group} {name}"
        if image:
            prefix = f"{prefix} image={image!r}"
        state = status.state
        if state is None:
            if not status.ready:
                out.append(f"{prefix} is not ready")
            continue
        waiting = state.waiting
        if waiting is not None:
            detail = _join_status_detail(
                waiting.reason or "",
                waiting.message or "",
            )
            if detail:
                out.append(f"{prefix} waiting: {detail}")
            else:
                out.append(f"{prefix} waiting")
            continue
        terminated = state.terminated
        if terminated is not None and (terminated.exit_code or 0) != 0:
            detail = _join_status_detail(
                terminated.reason or "",
                terminated.message or "",
            )
            exit_code = terminated.exit_code
            if detail:
                out.append(f"{prefix} terminated exit={exit_code}: {detail}")
            else:
                out.append(f"{prefix} terminated exit={exit_code}")
            continue
        if not status.ready:
            out.append(f"{prefix} is not ready")
    return tuple(out)


@dataclass(frozen=True)
class Pod(NamespacedKubeMetadata[kubernetes.client.V1Pod]):
    """General-purpose wrapper around one Kubernetes Pod object.

    Parameters
    ----------
    _obj : kubernetes.client.V1Pod
        Typed Kubernetes Pod payload returned by the cluster API.
    """

    _obj: kubernetes.client.V1Pod

    @classmethod
    def _client(cls) -> ResourceClient[kubernetes.client.V1Pod, Self]:
        return ResourceClient(
            scope="namespaced",
            kind="Pod",
            expected=kubernetes.client.V1Pod,
            list_type=kubernetes.client.V1PodList,
            wrapper=lambda payload: cls(_obj=payload),
            read=lambda kube, namespace, name, request_timeout: (
                kube.core.read_namespaced_pod(
                    name=name,
                    namespace=namespace,
                    _request_timeout=request_timeout,
                )
            ),
            list_all=lambda kube, label_selector, field_selector, request_timeout: (
                kube.core.list_pod_for_all_namespaces(
                    label_selector=label_selector,
                    field_selector=field_selector,
                    _request_timeout=request_timeout,
                )
            ),
            list_namespace=lambda kube, namespace, labels, fields, timeout: (
                kube.core.list_namespaced_pod(
                    namespace=namespace,
                    label_selector=labels,
                    field_selector=fields,
                    _request_timeout=timeout,
                )
            ),
            delete=lambda kube, namespace, name, request_timeout: (
                kube.core.delete_namespaced_pod(
                    name=name,
                    namespace=namespace,
                    body=kubernetes.client.V1DeleteOptions(),
                    _request_timeout=request_timeout,
                )
            ),
            watch_all=lambda kube: kube.core.list_pod_for_all_namespaces,
            watch_namespace=lambda kube: kube.core.list_namespaced_pod,
        )

    @staticmethod
    def _manifest(
        *,
        namespace: str,
        name: str,
        labels: Mapping[str, str],
        pod_template: PodTemplateSpec,
        annotations: Mapping[str, str] | None,
    ) -> dict[str, object]:
        template = _pod_template_manifest(pod_template)
        pod_labels = dict(labels)
        pod_annotations = dict(annotations or {})
        metadata: dict[str, object] = {
            "name": name,
            "namespace": namespace,
            "labels": pod_labels,
            "annotations": pod_annotations,
        }
        template_metadata = template.get("metadata")
        if isinstance(template_metadata, MappingABC):
            template_metadata = dict(template_metadata)
            template_labels = template_metadata.get("labels")
            if isinstance(template_labels, MappingABC):
                pod_labels.update(
                    {str(key): str(value) for key, value in template_labels.items()}
                )
            template_annotations = template_metadata.get("annotations")
            if isinstance(template_annotations, MappingABC):
                pod_annotations.update(
                    {
                        str(key): str(value)
                        for key, value in template_annotations.items()
                    }
                )
        return {
            "apiVersion": "v1",
            "kind": "Pod",
            "metadata": metadata,
            "spec": template["spec"],
        }

    @classmethod
    async def create(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        labels: Mapping[str, str],
        pod_template: PodTemplateSpec,
        timeout: float,
        annotations: Mapping[str, str] | None = None,
    ) -> Self:
        """Create one Kubernetes Pod from intent-level fields.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the Pod.
        name : str
            Pod name to create.
        labels : Mapping[str, str]
            Labels to apply to the Pod metadata.
        pod_template : PodTemplateSpec
            Pod template to render into the Pod.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to the Pod metadata.

        Returns
        -------
        Pod
            Wrapped created Pod.

        Raises
        ------
        OSError
            If Kubernetes returns malformed data or the API call fails.
        """
        namespace = namespace.strip()
        name = name.strip()
        if not namespace or not name:
            msg = "Pod create requires non-empty namespace and name"
            raise OSError(msg)
        manifest = cls._manifest(
            namespace=namespace,
            name=name,
            labels=labels,
            pod_template=pod_template,
            annotations=annotations,
        )
        created = await kube.run(
            lambda request_timeout: kube.core.create_namespaced_pod(
                namespace=namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to create Pod {namespace}/{name}",
        )
        if not isinstance(created, kubernetes.client.V1Pod):
            msg = f"malformed Kubernetes Pod payload while creating {name!r}"
            raise OSError(msg)
        return cls(_obj=created)

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        timeout: float,
        name: str,
    ) -> Self | None:
        """Read one Kubernetes Pod by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            The namespace to search within.
        timeout : float
            The maximum time to wait for Kubernetes pod query in seconds.  If
            infinite, wait indefinitely.
        name : str
            Pod name to read.

        Returns
        -------
        Pod | None
            Validated Kubernetes pod wrapper, or `None` if the pod does not exist.
        """
        return await cls._client().get(
            kube,
            namespace=namespace,
            name=name,
            timeout=timeout,
        )

    @classmethod
    async def list(
        cls,
        kube: Kube,
        *,
        timeout: float,
        namespaces: Collection[str] | None = None,
        labels: Mapping[str, str] | None = None,
        field_selector: str | None = None,
    ) -> builtins.list[Self]:
        """List Kubernetes Pods with optional namespace and label filtering.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            The maximum time to wait for Kubernetes pod list queries in seconds.  If
            infinite, wait indefinitely.
        namespaces : Collection[str] | None, optional
            Optional namespace filters.  `None` queries all namespaces.  Otherwise,
            names are normalized (trimmed), deduplicated, and queried individually.
        labels : Mapping[str, str] | None, optional
            Optional label filters.
        field_selector : str | None, optional
            Raw Kubernetes field selector.

        Returns
        -------
        builtins.list[Pod]
            Validated Kubernetes pod wrappers.
        """
        return await cls._client().list(
            kube,
            timeout=timeout,
            namespaces=namespaces,
            labels=labels,
            field_selector=field_selector,
        )

    @classmethod
    async def watch(
        cls,
        kube: Kube,
        *,
        timeout: float,
        namespace: str | None = None,
        labels: Mapping[str, str] | None = None,
        field_selector: str | None = None,
        resource_version: str | None = None,
    ) -> AsyncIterator[WatchEvent[Self]]:
        """Watch Kubernetes Pods.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum watch budget in seconds. If infinite, wait indefinitely.
        namespace : str | None, optional
            Namespace to watch. If omitted, watches Pods across all namespaces.
        labels : Mapping[str, str] | None, optional
            Optional label selector key/value pairs.
        field_selector : str | None, optional
            Raw Kubernetes field selector.
        resource_version : str | None, optional
            Resource version to watch from.

        Yields
        ------
        WatchEvent[Pod]
            Typed watch events containing wrapped Pods.
        """
        async for event in cls._client().watch(
            kube,
            timeout=timeout,
            namespace=namespace,
            labels=labels,
            field_selector=field_selector,
            resource_version=resource_version,
        ):
            yield event

    @property
    def phase(self) -> str:
        """Return the current Pod phase.

        Returns
        -------
        str
            Current pod phase value, or an empty string when unavailable.
        """
        status = self._obj.status
        return (status.phase or "").strip() if status is not None else ""

    @property
    def node_name(self) -> str:
        """Return the node this Pod is assigned to.

        Returns
        -------
        str
            Kubernetes node name from `spec.nodeName`, or an empty string when the
            pod has not been scheduled.
        """
        spec = self._obj.spec
        return (spec.node_name or "").strip() if spec is not None else ""

    @property
    def pod_ip(self) -> str:
        """Return this Pod's cluster IP address.

        Returns
        -------
        str
            Pod IP reported by Kubernetes, or an empty string when unavailable.
        """
        status = self._obj.status
        return (status.pod_ip or "").strip() if status is not None else ""

    @property
    def is_ready(self) -> bool:
        """Return whether this Pod currently reports Ready.

        Returns
        -------
        bool
            ``True`` when the Pod is active and has a Ready condition with status
            ``True``.
        """
        if not self.is_active:
            return False
        status = self._obj.status
        for condition in (status.conditions or []) if status is not None else []:
            if (condition.type or "").strip() != "Ready":
                continue
            return (condition.status or "").strip().lower() == "true"
        return False

    @property
    def is_terminating(self) -> bool:
        """Return whether the Pod is terminating.

        Returns
        -------
        bool
            `True` when `metadata.deletion_timestamp` is present.
        """
        metadata = self._obj.metadata
        return bool(metadata.deletion_timestamp) if metadata is not None else False

    @property
    def is_active(self) -> bool:
        """Return whether the Pod is active.

        Returns
        -------
        bool
            `True` when the pod is not terminating and phase is one of
            `Pending|Running|Unknown`.
        """
        return not self.is_terminating and self.phase in POD_ACTIVE_PHASES

    @property
    def is_terminal(self) -> bool:
        """Return whether the Pod is terminal.

        Returns
        -------
        bool
            `True` when pod phase is `Succeeded` or `Failed`.
        """
        return self.phase in POD_TERMINAL_PHASES

    def container_running(self, name: str) -> bool:
        """Return whether a regular container is currently running.

        Parameters
        ----------
        name : str
            Container name to inspect.

        Returns
        -------
        bool
            `True` when the named regular container reports a running state.
        """
        target = name.strip()
        if not target:
            return False
        status = self._obj.status
        for container in (
            (status.container_statuses or ()) if status is not None else ()
        ):
            if (container.name or "").strip() != target:
                continue
            state = container.state
            return state is not None and state.running is not None
        return False

    @property
    def is_mirror(self) -> bool:
        """Return whether the Pod is a static mirror pod.

        Returns
        -------
        bool
            `True` when this pod is a static mirror pod.
        """
        return POD_MIRROR_ANNOTATION in self.annotations

    @property
    def is_daemonset_controlled(self) -> bool:
        """Return whether the Pod is controlled by a DaemonSet.

        Returns
        -------
        bool
            `True` when a controller owner-reference of kind `DaemonSet` exists.
        """
        metadata = self._obj.metadata
        owners = (metadata.owner_references or []) if metadata is not None else []
        for owner in owners:
            if (owner.kind or "").strip() != "DaemonSet":
                continue
            if owner.controller is None or owner.controller:
                return True
        return False

    def has_supported_controller(
        self,
        kinds: frozenset[str] = POD_SUPPORTED_CONTROLLER_KINDS,
    ) -> bool:
        """Return whether this pod is controlled by a supported owner kind.

        Parameters
        ----------
        kinds : frozenset[str], optional
            Allowed owner kinds considered safe for drain orchestration.

        Returns
        -------
        bool
            `True` when a controller owner-reference matches one of `kinds`.
        """
        metadata = self._obj.metadata
        owners = (metadata.owner_references or []) if metadata is not None else []
        for owner in owners:
            if (owner.kind or "").strip() not in kinds:
                continue
            if owner.controller is None or owner.controller:
                return True
        return False

    @property
    def uses_emptydir(self) -> bool:
        """Return whether the Pod uses any `emptyDir` volume.

        Returns
        -------
        bool
            `True` when this pod spec includes at least one `emptyDir` volume.
        """
        spec = self._obj.spec
        for volume in (spec.volumes or []) if spec is not None else []:
            if volume.empty_dir is not None:
                return True
        return False

    @property
    def persistent_volume_claim_names(self) -> tuple[str, ...]:
        """Return referenced PersistentVolumeClaim names.

        Returns
        -------
        tuple[str, ...]
            Distinct PVC claim names referenced by this pod, preserving spec order.
        """
        spec = self._obj.spec
        seen: set[str] = set()
        out: builtins.list[str] = []
        for volume in (spec.volumes or []) if spec is not None else []:
            pvc = volume.persistent_volume_claim
            if pvc is None:
                continue
            claim_name = (pvc.claim_name or "").strip()
            if not claim_name or claim_name in seen:
                continue
            seen.add(claim_name)
            out.append(claim_name)
        return tuple(out)

    @property
    def image_refs(self) -> tuple[str, ...]:
        """Return image references used by this Pod.

        Returns
        -------
        tuple[str, ...]
            Distinct image references from regular, init, and ephemeral containers,
            preserving pod-spec order.
        """
        spec = self._obj.spec
        seen: set[str] = set()
        out: builtins.list[str] = []
        if spec is None:
            return ()
        groups = (
            spec.init_containers or [],
            spec.containers or [],
            spec.ephemeral_containers or [],
        )
        for containers in groups:
            for container in containers:
                image = (container.image or "").strip()
                if not image or image in seen:
                    continue
                seen.add(image)
                out.append(image)
        return tuple(out)

    @property
    def status_diagnostics(self) -> tuple[str, ...]:
        """Return concise status diagnostics for this Pod.

        Returns
        -------
        tuple[str, ...]
            Human-readable status lines derived from pod conditions and container
            waiting or failed termination states.
        """
        status = self._obj.status
        if status is None:
            return ()

        label = self._object_label()
        out: builtins.list[str] = []
        phase = self.phase
        if phase:
            out.append(f"{label} phase={phase}")
        for condition in status.conditions or []:
            state = (condition.status or "").strip()
            if state.lower() == "true":
                continue
            kind = (condition.type or "").strip() or "Unknown"
            reason = (condition.reason or "").strip()
            message = (condition.message or "").strip()
            detail = _join_status_detail(reason, message)
            if detail:
                out.append(f"{label} condition {kind}={state or 'False'}: {detail}")
            else:
                out.append(f"{label} condition {kind}={state or 'False'}")
        for group, statuses in (
            ("init", status.init_container_statuses),
            ("container", status.container_statuses),
            ("ephemeral", status.ephemeral_container_statuses),
        ):
            out.extend(_container_status_diagnostics(label, group, statuses or ()))
        return tuple(out)

    async def logs(
        self,
        kube: Kube,
        *,
        timeout: float,
        container: str | None = None,
        tail_lines: int | None = None,
    ) -> str:
        """Read this Pod's logs.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        container : str | None, optional
            Optional container name. If omitted, Kubernetes selects the default
            container when possible.
        tail_lines : int | None, optional
            Optional number of log lines to return from the end of the stream.

        Returns
        -------
        str
            Pod log text returned by Kubernetes.

        Raises
        ------
        ValueError
            If `tail_lines` is not positive.
        OSError
            If Kubernetes returns malformed log data.
        """
        namespace, name = self._require_namespace_name("read pod logs")
        if tail_lines is not None and tail_lines <= 0:
            msg = "pod log tail_lines must be positive"
            raise ValueError(msg)
        container = container.strip() if container is not None else None
        if container == "":
            container = None

        payload = await kube.run(
            lambda request_timeout: kube.core.read_namespaced_pod_log(
                name=name,
                namespace=namespace,
                container=container,
                tail_lines=tail_lines,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to read logs for pod {namespace}/{name}",
        )
        if payload is None:
            return ""
        if isinstance(payload, bytes):
            return payload.decode("utf-8", errors="replace")
        if not isinstance(payload, str):
            msg = f"malformed Kubernetes log payload for pod {namespace}/{name}"
            raise OSError(msg)
        return payload

    async def attach(
        self,
        kube: Kube,
        *,
        timeout: float,
        container: str,
        stdin: bool = True,
        stdout: bool = True,
        stderr: bool = True,
        tty: bool = True,
    ) -> Any:
        """Open a Kubernetes attach stream for this Pod.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget for opening the stream in seconds. If infinite,
            wait indefinitely.
        container : str
            Container name to attach to.
        stdin : bool, optional
            Whether to request the Kubernetes stdin channel.
        stdout : bool, optional
            Whether to request the Kubernetes stdout channel.
        stderr : bool, optional
            Whether to request the Kubernetes stderr channel.
        tty : bool, optional
            Whether to request TTY mode.

        Returns
        -------
        Any
            Kubernetes websocket client returned by the Python stream helper.

        Raises
        ------
        OSError
            If the pod metadata is incomplete or the Kubernetes API refuses the
            attach request.
        """
        namespace, name = self._require_namespace_name("attach to pod")
        container = container.strip()
        if not container:
            msg = "pod attach requires a non-empty container name"
            raise OSError(msg)
        return await kube.run(
            lambda request_timeout: kubernetes_stream(
                kube.core.connect_get_namespaced_pod_attach,
                name=name,
                namespace=namespace,
                container=container,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
                tty=tty,
                binary=True,
                _preload_content=False,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to attach to pod {namespace}/{name} container {container}",
        )

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this pod by its metadata namespace and name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.

        Returns
        -------
        Pod | None
            Fresh pod wrapper, or `None` if the pod no longer exists.
        """
        namespace, name = self._require_namespace_name("refresh pod")
        return await type(self).get(
            kube,
            namespace=namespace,
            timeout=timeout,
            name=name,
        )

    async def wait_deleted(self, kube: Kube, *, timeout: float) -> None:
        """Wait until this pod is deleted from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum runtime budget in seconds. If infinite, wait indefinitely.
        """
        namespace, name = self._require_namespace_name("wait for pod deletion")
        await (
            type(self)
            ._client()
            .wait_deleted(
                label=self._object_label(name=name, namespace=namespace),
                timeout=timeout,
                refresh=lambda remaining: self.refresh(kube, timeout=remaining),
            )
        )

    async def wait_terminal(self, kube: Kube, *, timeout: float) -> Self:
        """Wait until this pod reaches a terminal phase.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.

        Returns
        -------
        Pod
            Latest pod wrapper once phase converges to `Succeeded` or `Failed`.

        Raises
        ------
        TimeoutError
            If terminal phase convergence does not complete within `timeout`.
        OSError
            If the pod is deleted before reaching a terminal phase.
        """
        namespace, name = self._require_namespace_name("wait for pod terminal phase")
        if timeout <= 0:
            msg = f"timed out waiting for pod {namespace}/{name} terminal phase"
            raise TimeoutError(msg)
        deadline = Deadline.from_timeout(
            timeout,
            message=f"timed out waiting for pod {namespace}/{name} terminal phase",
        )
        while True:
            remaining = deadline.remaining()
            if remaining <= 0:
                msg = f"timed out waiting for pod {namespace}/{name} terminal phase"
                raise TimeoutError(msg)
            live = await self.refresh(kube, timeout=remaining)
            if live is None:
                msg = (
                    f"pod {namespace}/{name} was deleted before reaching a "
                    "terminal phase"
                )
                raise OSError(msg)
            if live.is_terminal:
                return live
            await asyncio.sleep(deadline.bounded(POD_WAIT_POLL_INTERVAL_SECONDS))

    async def evict(
        self,
        kube: Kube,
        *,
        timeout: float,
    ) -> None:
        """Evict this pod via the policy eviction subresource.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.
        """
        namespace, name = self._require_namespace_name("evict pod")

        # policy/v1 eviction is preferred over delete because it respects
        # PodDisruptionBudgets and communicates scheduling intent explicitly.
        body = kubernetes.client.V1Eviction(
            api_version="policy/v1",
            kind="Eviction",
            metadata=kubernetes.client.V1ObjectMeta(name=name, namespace=namespace),
            delete_options=None,
        )
        await kube.run(
            lambda request_timeout: kube.core.create_namespaced_pod_eviction(
                name=name,
                namespace=namespace,
                body=body,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to evict pod {namespace}/{name}",
        )

    async def delete(
        self,
        kube: Kube,
        *,
        timeout: float,
        grace_period_seconds: int | None = None,
    ) -> None:
        """Delete this pod from the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum runtime budget in seconds.  If infinite, wait indefinitely.
        grace_period_seconds : int | None, optional
            Optional Kubernetes termination grace period.  When provided,
            kubelet sends normal termination signals and forcefully kills the
            container after this many seconds if it is still running.

        Raises
        ------
        ValueError
            If `grace_period_seconds` is negative.
        """
        namespace, name = self._require_namespace_name("delete pod")
        if grace_period_seconds is not None and grace_period_seconds < 0:
            msg = "Pod deletion grace period cannot be negative"
            raise ValueError(msg)

        payload = await kube.run(
            lambda request_timeout: kube.core.delete_namespaced_pod(
                name=name,
                namespace=namespace,
                body=kubernetes.client.V1DeleteOptions(
                    grace_period_seconds=grace_period_seconds,
                ),
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to delete Pod {namespace}/{name}",
        )
        _validate_delete_status(
            payload,
            label=self._object_label(name=name, namespace=namespace),
        )
