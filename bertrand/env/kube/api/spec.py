"""Private Kubernetes pod-template intents and manifest boundary types."""

from __future__ import annotations

from collections.abc import Collection, Mapping, Sequence
from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING, Literal, NotRequired, Self, TypedDict, cast

if TYPE_CHECKING:
    from pathlib import Path

type PortProtocol = Literal["TCP", "UDP", "SCTP"]


class PolicyRuleManifest(TypedDict):
    """Kubernetes RBAC policy rule manifest fragment."""

    apiGroups: list[str]
    resources: list[str]
    verbs: list[str]


class DeploymentRollingUpdateManifest(TypedDict, total=False):
    """Kubernetes Deployment rolling-update strategy fragment."""

    maxSurge: int | str
    maxUnavailable: int | str


class DeploymentStrategyManifest(TypedDict):
    """Kubernetes Deployment strategy manifest fragment."""

    type: str
    rollingUpdate: NotRequired[DeploymentRollingUpdateManifest | None]


class ContainerPortManifest(TypedDict):
    """Kubernetes container port manifest fragment."""

    name: str
    containerPort: int
    protocol: PortProtocol


class EnvVarManifest(TypedDict, total=False):
    """Kubernetes environment variable manifest fragment."""

    name: str
    value: str
    valueFrom: Mapping[str, object]


class VolumeMountManifest(TypedDict, total=False):
    """Kubernetes volume mount manifest fragment."""

    name: str
    mountPath: str
    readOnly: bool | None
    subPath: str | None
    mountPropagation: str | None


class SecretVolumeItemManifest(TypedDict, total=False):
    """Kubernetes Secret volume item manifest fragment."""

    key: str
    path: str
    mode: int | None


class PodResourceClaimManifest(TypedDict, total=False):
    """Kubernetes pod resource claim manifest fragment."""

    name: str
    resourceClaimName: str
    resourceClaimTemplateName: str


@dataclass(frozen=True)
class ContainerSpec:
    """Intent-level Kubernetes container specification.

    Parameters
    ----------
    name : str
        Container name.
    image : str
        Container image reference.
    image_pull_policy : str | None, optional
        Kubernetes image pull policy.
    command : Sequence[str] | None, optional
        Container entrypoint command.
    args : Sequence[str] | None, optional
        Container command arguments.
    ports : Collection[Mapping[str, object]], optional
        Container port manifest fragments.
    env : Collection[Mapping[str, object]], optional
        Container environment variable manifest fragments.
    startup_probe : Mapping[str, object] | None, optional
        Startup probe manifest fragment.
    readiness_probe : Mapping[str, object] | None, optional
        Readiness probe manifest fragment.
    liveness_probe : Mapping[str, object] | None, optional
        Liveness probe manifest fragment.
    volume_mounts : Collection[Mapping[str, object]], optional
        Container volume mount manifest fragments.
    security_context : Mapping[str, object] | None, optional
        Container security context manifest fragment.
    resources : Mapping[str, object] | None, optional
        Container resource requirements manifest fragment.
    stdin : bool | None, optional
        Whether Kubernetes should allocate an open stdin stream for this container.
    stdin_once : bool | None, optional
        Whether Kubernetes should close stdin after the first attach session
        disconnects.
    tty : bool | None, optional
        Whether Kubernetes should allocate a TTY for the container process.
    """

    name: str
    image: str
    image_pull_policy: str | None = None
    command: Sequence[str] | None = None
    args: Sequence[str] | None = None
    ports: Collection[Mapping[str, object]] = ()
    env: Collection[Mapping[str, object]] = ()
    startup_probe: Mapping[str, object] | None = None
    readiness_probe: Mapping[str, object] | None = None
    liveness_probe: Mapping[str, object] | None = None
    volume_mounts: Collection[Mapping[str, object]] = ()
    security_context: Mapping[str, object] | None = None
    resources: Mapping[str, object] | None = None
    stdin: bool | None = None
    stdin_once: bool | None = None
    tty: bool | None = None

    def _manifest(self) -> dict[str, object]:
        """Render this container as a Kubernetes manifest fragment.

        Returns
        -------
        dict[str, object]
            Kubernetes container fragment.
        """
        payload: dict[str, object] = {
            "name": self.name,
            "image": self.image,
        }
        if self.image_pull_policy is not None:
            payload["imagePullPolicy"] = self.image_pull_policy
        if self.command is not None:
            payload["command"] = list(self.command)
        if self.args is not None:
            payload["args"] = list(self.args)
        if self.stdin is not None:
            payload["stdin"] = self.stdin
        if self.stdin_once is not None:
            payload["stdinOnce"] = self.stdin_once
        if self.tty is not None:
            payload["tty"] = self.tty
        if self.ports:
            payload["ports"] = [_copy_manifest(port) for port in self.ports]
        if self.env:
            payload["env"] = [_env_manifest(var) for var in self.env]
        if self.startup_probe is not None:
            payload["startupProbe"] = _probe_manifest(self.startup_probe)
        if self.readiness_probe is not None:
            payload["readinessProbe"] = _probe_manifest(self.readiness_probe)
        if self.liveness_probe is not None:
            payload["livenessProbe"] = _probe_manifest(self.liveness_probe)
        if self.volume_mounts:
            payload["volumeMounts"] = [
                _copy_manifest(mount) for mount in self.volume_mounts
            ]
        if self.security_context:
            payload["securityContext"] = _copy_manifest(self.security_context)
        if self.resources:
            payload["resources"] = _copy_manifest(self.resources)
        return payload


@dataclass(frozen=True)
class PodTemplateSpec:
    """Intent-level Kubernetes Pod template shared by workload resources.

    Parameters
    ----------
    containers : Collection[ContainerSpec]
        Containers to render into the Pod template.
    volumes : Collection[VolumeSpec], optional
        Volumes to render into the Pod template.
    resource_claims : Collection[Mapping[str, object]], optional
        Pod-level DRA resource claim manifest fragments.
    labels : Mapping[str, str], optional
        Labels to apply to the Pod template metadata.
    annotations : Mapping[str, str] | None, optional
        Annotations to apply to the Pod template metadata.
    restart_policy : str | None, optional
        Optional Pod restart policy, such as `"Never"` or `"OnFailure"`.
    automount_service_account_token : bool, optional
        Whether pods should automount the default service-account token.
    service_account_name : str | None, optional
        Optional pod service-account name.
    node_selector : Mapping[str, str] | None, optional
        Optional node selector.
    node_name : str | None, optional
        Optional exact node name.
    host_pid : bool | None, optional
        Optional `hostPID` value.
    tolerations : Collection[Mapping[str, object]], optional
        Optional pod toleration manifest fragments.
    image_pull_secrets : Collection[str], optional
        Optional image pull Secret names.
    priority_class_name : str | None, optional
        Optional priority class name.
    dns_policy : str | None, optional
        Optional DNS policy.
    host_network : bool | None, optional
        Optional `hostNetwork` value.
    termination_grace_period_seconds : int | None, optional
        Optional pod termination grace period in seconds.
    """

    containers: Collection[ContainerSpec]
    volumes: Collection[VolumeSpec] = ()
    resource_claims: Collection[Mapping[str, object]] = ()
    labels: Mapping[str, str] = MappingProxyType({})
    annotations: Mapping[str, str] | None = None
    restart_policy: str | None = None
    automount_service_account_token: bool = False
    service_account_name: str | None = None
    node_selector: Mapping[str, str] | None = None
    node_name: str | None = None
    host_pid: bool | None = None
    tolerations: Collection[Mapping[str, object]] = ()
    image_pull_secrets: Collection[str] = ()
    priority_class_name: str | None = None
    dns_policy: str | None = None
    host_network: bool | None = None
    termination_grace_period_seconds: int | None = None

    def _manifest(self) -> dict[str, object]:
        """Render this Pod template as a Kubernetes manifest fragment.

        Returns
        -------
        dict[str, object]
            Kubernetes Pod template fragment.

        Raises
        ------
        ValueError
            If the template contains invalid child intents.
        """
        spec: dict[str, object] = {
            "automountServiceAccountToken": self.automount_service_account_token,
            "containers": [container._manifest() for container in self.containers],
            "volumes": [volume._manifest() for volume in self.volumes],
        }
        if self.resource_claims:
            spec["resourceClaims"] = [
                _resource_claim_manifest(claim) for claim in self.resource_claims
            ]
        if self.restart_policy is not None:
            spec["restartPolicy"] = self.restart_policy
        if self.service_account_name is not None:
            service_account_name = self.service_account_name.strip()
            if service_account_name:
                spec["serviceAccountName"] = service_account_name
        if self.node_selector:
            spec["nodeSelector"] = dict(self.node_selector)
        if self.node_name is not None:
            node_name = self.node_name.strip()
            if node_name:
                spec["nodeName"] = node_name
        if self.tolerations:
            spec["tolerations"] = [
                _toleration_manifest(toleration) for toleration in self.tolerations
            ]
        if self.image_pull_secrets:
            spec["imagePullSecrets"] = [
                {"name": name}
                for secret in self.image_pull_secrets
                if (name := secret.strip())
            ]
        if self.priority_class_name is not None:
            priority_class_name = self.priority_class_name.strip()
            if priority_class_name:
                spec["priorityClassName"] = priority_class_name
        if self.dns_policy is not None:
            dns_policy = self.dns_policy.strip()
            if dns_policy:
                spec["dnsPolicy"] = dns_policy
        if self.host_network is not None:
            spec["hostNetwork"] = self.host_network
        if self.host_pid is not None:
            spec["hostPID"] = self.host_pid
        if self.termination_grace_period_seconds is not None:
            if self.termination_grace_period_seconds < 0:
                msg = "termination grace period cannot be negative"
                raise ValueError(msg)
            spec["terminationGracePeriodSeconds"] = (
                self.termination_grace_period_seconds
            )
        metadata: dict[str, object] = {"labels": dict(self.labels)}
        if self.annotations:
            metadata["annotations"] = dict(self.annotations)
        return {"metadata": metadata, "spec": spec}


@dataclass(frozen=True)
class VolumeSpec:
    """Intent-level Kubernetes pod volume specification.

    Parameters
    ----------
    name : str
        Pod volume name.
    source : Mapping[str, object]
        Single Kubernetes volume source fragment, such as `{"emptyDir": {}}` or
        `{"persistentVolumeClaim": {"claimName": "repo"}}`.
    """

    name: str
    source: Mapping[str, object]

    @classmethod
    def empty_dir(
        cls,
        name: str,
        *,
        medium: str | None = None,
        size_limit: str | None = None,
    ) -> Self:
        """Create an `emptyDir` volume specification.

        Parameters
        ----------
        name : str
            Pod volume name.
        medium : str | None, optional
            Storage medium, such as `"Memory"`.
        size_limit : str | None, optional
            Kubernetes quantity limiting the volume size.

        Returns
        -------
        Self
            Volume specification.
        """
        empty_dir: dict[str, object] = {}
        if medium is not None:
            empty_dir["medium"] = medium
        if size_limit is not None:
            empty_dir["sizeLimit"] = size_limit
        return cls(name=name, source={"emptyDir": empty_dir})

    @classmethod
    def config_map(
        cls,
        name: str,
        *,
        config_map_name: str,
        optional: bool | None = None,
    ) -> Self:
        """Create a ConfigMap-backed volume specification.

        Parameters
        ----------
        name : str
            Pod volume name.
        config_map_name : str
            Name of the ConfigMap to mount.
        optional : bool | None, optional
            Whether the ConfigMap reference is optional.

        Returns
        -------
        Self
            Volume specification.
        """
        config_map = _copy_manifest(
            {
                "name": config_map_name,
                "optional": optional,
            }
        )
        return cls(name=name, source={"configMap": config_map})

    @classmethod
    def secret(
        cls,
        name: str,
        *,
        secret_name: str,
        optional: bool | None = None,
        default_mode: int | None = None,
        items: Collection[Mapping[str, object]] = (),
    ) -> Self:
        """Create a Secret-backed volume specification.

        Parameters
        ----------
        name : str
            Pod volume name.
        secret_name : str
            Name of the Secret to mount.
        optional : bool | None, optional
            Whether the Secret reference is optional.
        default_mode : int | None, optional
            Default POSIX mode for Secret-backed volume files.
        items : Collection[Mapping[str, object]], optional
            Optional Secret key-to-path projections. If omitted, Kubernetes projects
            every Secret data key.

        Returns
        -------
        Self
            Volume specification.
        """
        secret = _copy_manifest(
            {
                "secretName": secret_name,
                "optional": optional,
                "defaultMode": default_mode,
            }
        )
        if items:
            secret["items"] = [_secret_volume_item_manifest(item) for item in items]
        return cls(name=name, source={"secret": secret})

    @classmethod
    def pvc(cls, name: str, *, claim_name: str) -> Self:
        """Create a PVC-backed volume specification.

        Parameters
        ----------
        name : str
            Pod volume name.
        claim_name : str
            Name of the PersistentVolumeClaim to mount.

        Returns
        -------
        Self
            Volume specification.
        """
        return cls(
            name=name,
            source={"persistentVolumeClaim": {"claimName": claim_name}},
        )

    @classmethod
    def host_path(
        cls,
        name: str,
        *,
        path: str | Path,
        host_path_type: str | None = None,
    ) -> Self:
        """Create a hostPath-backed volume specification.

        Parameters
        ----------
        name : str
            Pod volume name.
        path : str | Path
            Host path to mount.
        host_path_type : str | None, optional
            Optional Kubernetes hostPath type constraint.

        Returns
        -------
        Self
            Volume specification.
        """
        host_path = _copy_manifest(
            {
                "path": str(path),
                "type": host_path_type,
            }
        )
        return cls(name=name, source={"hostPath": host_path})

    def _manifest(self) -> dict[str, object]:
        """Render this volume as a Kubernetes manifest fragment.

        Returns
        -------
        dict[str, object]
            Kubernetes volume fragment.

        Raises
        ------
        ValueError
            If the volume source is invalid.
        """
        source = _copy_manifest(self.source)
        if len(source) != 1:
            msg = "Kubernetes volume must define exactly one source"
            raise ValueError(msg)

        payload: dict[str, object] = {"name": self.name}
        key, value = next(iter(source.items()))
        payload[key] = dict(value) if isinstance(value, Mapping) else value
        return payload


def _copy_manifest(fragment: Mapping[str, object]) -> dict[str, object]:
    return {key: value for key, value in fragment.items() if value is not None}


def _env_manifest(fragment: Mapping[str, object]) -> dict[str, object]:
    payload = _copy_manifest(fragment)
    has_value = "value" in payload
    value_from = payload.get("valueFrom")
    has_value_from = value_from is not None
    if has_value == has_value_from:
        msg = "environment variable must define exactly one source"
        raise ValueError(msg)
    if not has_value_from:
        return payload

    value_from = _mapping_or_none(value_from)
    if value_from is None:
        msg = "environment variable must define exactly one source"
        raise ValueError(msg)
    sources = sum(
        (
            value_from.get("fieldRef") is not None,
            value_from.get("secretKeyRef") is not None,
            value_from.get("configMapKeyRef") is not None,
        )
    )
    if sources != 1:
        msg = "environment variable must define exactly one source"
        raise ValueError(msg)

    raw_secret = value_from.get("secretKeyRef")
    secret = _mapping_or_none(raw_secret)
    if raw_secret is not None and (
        secret is None or not secret.get("name") or not secret.get("key")
    ):
        msg = "Secret environment variable source requires name and key"
        raise ValueError(msg)

    raw_config_map = value_from.get("configMapKeyRef")
    config_map = _mapping_or_none(raw_config_map)
    if raw_config_map is not None and (
        config_map is None or not config_map.get("name") or not config_map.get("key")
    ):
        msg = "ConfigMap environment variable source requires name and key"
        raise ValueError(msg)
    return payload


def _probe_manifest(fragment: Mapping[str, object]) -> dict[str, object]:
    payload = _copy_manifest(fragment)
    sources = sum(
        (
            payload.get("exec") is not None,
            payload.get("tcpSocket") is not None,
            payload.get("httpGet") is not None,
        )
    )
    if sources != 1:
        msg = "probe must define exactly one source"
        raise ValueError(msg)

    exec_probe = payload.get("exec")
    if exec_probe is not None:
        exec_probe = _mapping_or_none(exec_probe)
        if exec_probe is None:
            msg = "exec probe command cannot be empty"
            raise ValueError(msg)
        command = exec_probe.get("command")
        if not isinstance(command, Sequence) or isinstance(command, str):
            msg = "exec probe command cannot be empty"
            raise ValueError(msg)
        rendered = [str(part) for part in command if str(part).strip()]
        if not rendered:
            msg = "exec probe command cannot be empty"
            raise ValueError(msg)
        payload["exec"] = {"command": rendered}

    raw_http_probe = payload.get("httpGet")
    http_probe = _mapping_or_none(raw_http_probe)
    if raw_http_probe is not None and (
        http_probe is None
        or http_probe.get("path") is None
        or http_probe.get("port") is None
    ):
        msg = "HTTP probe requires path and port"
        raise ValueError(msg)
    return payload


def _resource_claim_manifest(fragment: Mapping[str, object]) -> dict[str, object]:
    name = str(fragment.get("name", "")).strip()
    if not name:
        msg = "pod resource claim name cannot be empty"
        raise ValueError(msg)
    resource_claim_name = str(fragment.get("resourceClaimName", "")).strip()
    template_name = str(fragment.get("resourceClaimTemplateName", "")).strip()
    if bool(resource_claim_name) == bool(template_name):
        msg = (
            "pod resource claim must reference exactly one existing claim or "
            "claim template"
        )
        raise ValueError(msg)
    payload: dict[str, object] = {"name": name}
    if resource_claim_name:
        payload["resourceClaimName"] = resource_claim_name
    else:
        payload["resourceClaimTemplateName"] = template_name
    return payload


def _toleration_manifest(fragment: Mapping[str, object]) -> dict[str, object]:
    payload = _copy_manifest(fragment)
    seconds = payload.get("tolerationSeconds")
    if seconds is not None and int(cast("int", seconds)) < 0:
        msg = "toleration seconds cannot be negative"
        raise ValueError(msg)
    return payload


def _secret_volume_item_manifest(fragment: Mapping[str, object]) -> dict[str, object]:
    key = str(fragment.get("key", "")).strip()
    path = str(fragment.get("path", "")).strip()
    if not key or not path:
        msg = "Secret volume item key and path cannot be empty"
        raise ValueError(msg)
    payload: dict[str, object] = {"key": key, "path": path}
    if "mode" in fragment and fragment["mode"] is not None:
        mode = int(cast("int", fragment["mode"]))
        if mode < 0:
            msg = "Secret volume item mode cannot be negative"
            raise ValueError(msg)
        payload["mode"] = mode
    return payload


def _mapping_or_none(value: object) -> Mapping[str, object] | None:
    if not isinstance(value, Mapping):
        return None
    return cast("Mapping[str, object]", value)
