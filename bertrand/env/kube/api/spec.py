"""Intent-level Kubernetes specs."""

from __future__ import annotations

from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING, Literal, Self

if TYPE_CHECKING:
    from collections.abc import Collection, Mapping, Sequence
    from pathlib import Path

type PortProtocol = Literal["TCP", "UDP", "SCTP"]


@dataclass(frozen=True)
class ServicePortSpec:
    """Intent-level Kubernetes Service port specification.

    Parameters
    ----------
    name : str
        Stable port name exposed by the Service.
    port : int
        Service port number.
    target_port : int | str
        Container port number or named port selected by the Service.
    protocol : {"TCP", "UDP", "SCTP"}, optional
        Network protocol for the Service port.
    node_port : int | None, optional
        Fixed NodePort value for `NodePort` Services.
    """

    name: str
    port: int
    target_port: int | str
    protocol: PortProtocol = "TCP"
    node_port: int | None = None


@dataclass(frozen=True)
class PolicyRuleSpec:
    """Intent-level Kubernetes RBAC policy rule specification.

    Parameters
    ----------
    api_groups : Collection[str]
        API groups covered by the rule. Use `""` for the core API group.
    resources : Collection[str]
        Resource names covered by the rule.
    verbs : Collection[str]
        Verbs granted by the rule.
    """

    api_groups: Collection[str]
    resources: Collection[str]
    verbs: Collection[str]


@dataclass(frozen=True)
class ContainerPortSpec:
    """Intent-level Kubernetes container port specification.

    Parameters
    ----------
    name : str
        Stable name for the container port.
    container_port : int
        Port number exposed by the container.
    protocol : {"TCP", "UDP", "SCTP"}, optional
        Network protocol for the container port.
    """

    name: str
    container_port: int
    protocol: PortProtocol = "TCP"


@dataclass(frozen=True)
class EnvVarSpec:
    """Intent-level Kubernetes container environment variable.

    Parameters
    ----------
    name : str
        Environment variable name.
    value : str | None, optional
        Literal environment variable value.
    field_path : str | None, optional
        Kubernetes field path to project with `valueFrom.fieldRef`.
    secret_name : str | None, optional
        Secret name to project with `valueFrom.secretKeyRef`.
    secret_key : str | None, optional
        Secret key to project with `valueFrom.secretKeyRef`.
    secret_optional : bool | None, optional
        Whether the Secret key reference is optional.
    config_map_name : str | None, optional
        ConfigMap name to project with `valueFrom.configMapKeyRef`.
    config_map_key : str | None, optional
        ConfigMap key to project with `valueFrom.configMapKeyRef`.
    config_map_optional : bool | None, optional
        Whether the ConfigMap key reference is optional.
    """

    name: str
    value: str | None = None
    field_path: str | None = None
    secret_name: str | None = None
    secret_key: str | None = None
    secret_optional: bool | None = None
    config_map_name: str | None = None
    config_map_key: str | None = None
    config_map_optional: bool | None = None

    @classmethod
    def field_ref(cls, name: str, *, field_path: str) -> Self:
        """Create an environment variable from a Kubernetes field reference.

        Parameters
        ----------
        name : str
            Environment variable name.
        field_path : str
            Kubernetes field path to project into the environment variable.

        Returns
        -------
        Self
            Environment variable specification.
        """
        return cls(name=name, field_path=field_path)

    @classmethod
    def secret_key_ref(
        cls,
        name: str,
        *,
        secret_name: str,
        key: str,
        optional: bool | None = None,
    ) -> Self:
        """Create an environment variable from a Secret key reference.

        Parameters
        ----------
        name : str
            Environment variable name.
        secret_name : str
            Secret name containing the key.
        key : str
            Secret key to project into the environment variable.
        optional : bool | None, optional
            Whether the Secret key reference is optional.

        Returns
        -------
        Self
            Environment variable specification.
        """
        return cls(
            name=name,
            secret_name=secret_name,
            secret_key=key,
            secret_optional=optional,
        )

    @classmethod
    def config_map_key_ref(
        cls,
        name: str,
        *,
        config_map_name: str,
        key: str,
        optional: bool | None = None,
    ) -> Self:
        """Create an environment variable from a ConfigMap key reference.

        Parameters
        ----------
        name : str
            Environment variable name.
        config_map_name : str
            ConfigMap name containing the key.
        key : str
            ConfigMap key to project into the environment variable.
        optional : bool | None, optional
            Whether the ConfigMap key reference is optional.

        Returns
        -------
        Self
            Environment variable specification.
        """
        return cls(
            name=name,
            config_map_name=config_map_name,
            config_map_key=key,
            config_map_optional=optional,
        )


@dataclass(frozen=True)
class VolumeMountSpec:
    """Intent-level Kubernetes container volume mount.

    Parameters
    ----------
    name : str
        Name of the pod volume to mount.
    mount_path : str
        Container path where the volume is mounted.
    read_only : bool | None, optional
        Whether the mount is read-only. `None` leaves the Kubernetes default.
    sub_path : str | None, optional
        Optional single file or directory within the volume to mount.
    """

    name: str
    mount_path: str
    read_only: bool | None = None
    sub_path: str | None = None


@dataclass(frozen=True)
class SecretVolumeItemSpec:
    """Intent-level Kubernetes Secret volume item projection.

    Parameters
    ----------
    key : str
        Secret data key to project into the volume.
    path : str
        Relative file path to create inside the mounted volume.
    mode : int | None, optional
        Optional POSIX mode for this projected file.
    """

    key: str
    path: str
    mode: int | None = None


@dataclass(frozen=True)
class ProbeSpec:
    """Intent-level Kubernetes container health probe.

    Parameters
    ----------
    exec_command : Sequence[str] | None, optional
        Command to execute inside the container.
    tcp_port : int | str | None, optional
        TCP socket port to probe.
    http_path : str | None, optional
        HTTP path to request.
    http_port : int | str | None, optional
        HTTP port to request.
    initial_delay_seconds : int | None, optional
        Delay before the first probe.
    period_seconds : int | None, optional
        Interval between probes.
    timeout_seconds : int | None, optional
        Probe timeout in seconds.
    success_threshold : int | None, optional
        Number of consecutive successes required after failure.
    failure_threshold : int | None, optional
        Number of failed probes before Kubernetes marks the container unhealthy.
    """

    exec_command: Sequence[str] | None = None
    tcp_port: int | str | None = None
    http_path: str | None = None
    http_port: int | str | None = None
    initial_delay_seconds: int | None = None
    period_seconds: int | None = None
    timeout_seconds: int | None = None
    success_threshold: int | None = None
    failure_threshold: int | None = None

    @classmethod
    def exec(
        cls,
        *,
        command: Sequence[str],
        initial_delay_seconds: int | None = None,
        period_seconds: int | None = None,
        timeout_seconds: int | None = None,
        success_threshold: int | None = None,
        failure_threshold: int | None = None,
    ) -> Self:
        """Create an exec probe.

        Parameters
        ----------
        command : Sequence[str]
            Command to execute inside the container.
        initial_delay_seconds : int | None, optional
            Delay before the first probe.
        period_seconds : int | None, optional
            Interval between probes.
        timeout_seconds : int | None, optional
            Probe timeout in seconds.
        success_threshold : int | None, optional
            Number of consecutive successes required after failure.
        failure_threshold : int | None, optional
            Number of failed probes before Kubernetes marks the container unhealthy.

        Returns
        -------
        Self
            Probe specification.
        """
        return cls(
            exec_command=command,
            initial_delay_seconds=initial_delay_seconds,
            period_seconds=period_seconds,
            timeout_seconds=timeout_seconds,
            success_threshold=success_threshold,
            failure_threshold=failure_threshold,
        )

    @classmethod
    def tcp(
        cls,
        *,
        port: int | str,
        initial_delay_seconds: int | None = None,
        period_seconds: int | None = None,
        timeout_seconds: int | None = None,
        success_threshold: int | None = None,
        failure_threshold: int | None = None,
    ) -> Self:
        """Create a TCP socket probe.

        Parameters
        ----------
        port : int | str
            Container port number or named port to probe.
        initial_delay_seconds : int | None, optional
            Delay before the first probe.
        period_seconds : int | None, optional
            Interval between probes.
        timeout_seconds : int | None, optional
            Probe timeout in seconds.
        success_threshold : int | None, optional
            Number of consecutive successes required after failure.
        failure_threshold : int | None, optional
            Number of failed probes before Kubernetes marks the container unhealthy.

        Returns
        -------
        Self
            Probe specification.
        """
        return cls(
            tcp_port=port,
            initial_delay_seconds=initial_delay_seconds,
            period_seconds=period_seconds,
            timeout_seconds=timeout_seconds,
            success_threshold=success_threshold,
            failure_threshold=failure_threshold,
        )

    @classmethod
    def http(
        cls,
        *,
        path: str,
        port: int | str,
        initial_delay_seconds: int | None = None,
        period_seconds: int | None = None,
        timeout_seconds: int | None = None,
        success_threshold: int | None = None,
        failure_threshold: int | None = None,
    ) -> Self:
        """Create an HTTP GET probe.

        Parameters
        ----------
        path : str
            HTTP path to request.
        port : int | str
            Container port number or named port to probe.
        initial_delay_seconds : int | None, optional
            Delay before the first probe.
        period_seconds : int | None, optional
            Interval between probes.
        timeout_seconds : int | None, optional
            Probe timeout in seconds.
        success_threshold : int | None, optional
            Number of consecutive successes required after failure.
        failure_threshold : int | None, optional
            Number of failed probes before Kubernetes marks the container unhealthy.

        Returns
        -------
        Self
            Probe specification.
        """
        return cls(
            http_path=path,
            http_port=port,
            initial_delay_seconds=initial_delay_seconds,
            period_seconds=period_seconds,
            timeout_seconds=timeout_seconds,
            success_threshold=success_threshold,
            failure_threshold=failure_threshold,
        )


@dataclass(frozen=True)
class SecurityContextSpec:
    """Intent-level Kubernetes container security context.

    Parameters
    ----------
    privileged : bool | None, optional
        Whether the container should run privileged.
    run_as_user : int | None, optional
        Container user ID.
    run_as_group : int | None, optional
        Container group ID.
    run_as_non_root : bool | None, optional
        Whether Kubernetes should require a non-root user.
    read_only_root_filesystem : bool | None, optional
        Whether the root filesystem should be mounted read-only.
    allow_privilege_escalation : bool | None, optional
        Whether privilege escalation is allowed.
    capabilities_add : Collection[str]
        Linux capabilities to add.
    capabilities_drop : Collection[str]
        Linux capabilities to drop.
    seccomp_profile_type : str | None, optional
        Seccomp profile type.
    seccomp_profile_localhost_profile : str | None, optional
        Localhost seccomp profile name.
    """

    privileged: bool | None = None
    run_as_user: int | None = None
    run_as_group: int | None = None
    run_as_non_root: bool | None = None
    read_only_root_filesystem: bool | None = None
    allow_privilege_escalation: bool | None = None
    capabilities_add: Collection[str] = ()
    capabilities_drop: Collection[str] = ()
    seccomp_profile_type: str | None = None
    seccomp_profile_localhost_profile: str | None = None


@dataclass(frozen=True)
class ContainerResourcesSpec:
    """Intent-level Kubernetes container resource requirements.

    Parameters
    ----------
    requests : Mapping[str, str], optional
        Kubernetes resource requests keyed by resource name, such as ``"cpu"``.
    limits : Mapping[str, str], optional
        Kubernetes resource limits keyed by resource name, such as ``"memory"``.
    claims : Collection[str], optional
        Pod-level resource claim names referenced by this container.
    """

    requests: Mapping[str, str] = MappingProxyType({})
    limits: Mapping[str, str] = MappingProxyType({})
    claims: Collection[str] = ()


@dataclass(frozen=True)
class PodResourceClaimSpec:
    """Intent-level Kubernetes pod resource claim reference.

    Parameters
    ----------
    name : str
        Pod-local resource claim name.
    resource_claim_name : str | None, optional
        Existing `ResourceClaim` name.
    resource_claim_template_name : str | None, optional
        `ResourceClaimTemplate` used to instantiate a claim for the pod.
    """

    name: str
    resource_claim_name: str | None = None
    resource_claim_template_name: str | None = None


@dataclass(frozen=True)
class TolerationSpec:
    """Intent-level Kubernetes pod toleration.

    Parameters
    ----------
    key : str | None, optional
        Taint key matched by the toleration.
    operator : str | None, optional
        Toleration operator, such as `"Equal"` or `"Exists"`.
    value : str | None, optional
        Taint value matched by the toleration.
    effect : str | None, optional
        Taint effect matched by the toleration.
    toleration_seconds : int | None, optional
        Duration for `NoExecute` tolerations.
    """

    key: str | None = None
    operator: str | None = None
    value: str | None = None
    effect: str | None = None
    toleration_seconds: int | None = None


@dataclass(frozen=True)
class DeploymentStrategySpec:
    """Intent-level Kubernetes Deployment rollout strategy.

    Parameters
    ----------
    kind : str
        Kubernetes strategy type, such as `"Recreate"` or `"RollingUpdate"`.
    max_surge : int | str | None, optional
        Maximum surge replicas during rolling updates.
    max_unavailable : int | str | None, optional
        Maximum unavailable replicas during rolling updates.
    """

    kind: str
    max_surge: int | str | None = None
    max_unavailable: int | str | None = None

    @classmethod
    def recreate(cls) -> Self:
        """Create a Recreate strategy.

        Returns
        -------
        Self
            Deployment rollout strategy.
        """
        return cls(kind="Recreate")

    @classmethod
    def rolling_update(
        cls,
        *,
        max_surge: int | str | None = None,
        max_unavailable: int | str | None = None,
    ) -> Self:
        """Create a RollingUpdate strategy.

        Parameters
        ----------
        max_surge : int | str | None, optional
            Maximum surge replicas during rollout.
        max_unavailable : int | str | None, optional
            Maximum unavailable replicas during rollout.

        Returns
        -------
        Self
            Deployment rollout strategy.
        """
        return cls(
            kind="RollingUpdate",
            max_surge=max_surge,
            max_unavailable=max_unavailable,
        )


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
    ports : Collection[ContainerPortSpec], optional
        Container ports to expose.
    env : Collection[EnvVarSpec], optional
        Container environment variables.
    startup_probe : ProbeSpec | None, optional
        Startup probe intent.
    readiness_probe : ProbeSpec | None, optional
        Readiness probe intent.
    liveness_probe : ProbeSpec | None, optional
        Liveness probe intent.
    volume_mounts : Collection[VolumeMountSpec], optional
        Pod volume mounts for the container.
    security_context : SecurityContextSpec | None, optional
        Container security context intent.
    resources : ContainerResourcesSpec | None, optional
        Container resource requirements.
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
    ports: Collection[ContainerPortSpec] = ()
    env: Collection[EnvVarSpec] = ()
    startup_probe: ProbeSpec | None = None
    readiness_probe: ProbeSpec | None = None
    liveness_probe: ProbeSpec | None = None
    volume_mounts: Collection[VolumeMountSpec] = ()
    security_context: SecurityContextSpec | None = None
    resources: ContainerResourcesSpec | None = None
    stdin: bool | None = None
    stdin_once: bool | None = None
    tty: bool | None = None


@dataclass(frozen=True)
class PodTemplateSpec:
    """Intent-level Kubernetes Pod template shared by workload resources.

    Parameters
    ----------
    containers : Collection[ContainerSpec]
        Containers to render into the Pod template.
    volumes : Collection[VolumeSpec], optional
        Volumes to render into the Pod template.
    resource_claims : Collection[PodResourceClaimSpec], optional
        Pod-level DRA resource claims.
    labels : Mapping[str, str], optional
        Labels to apply to the Pod template metadata.
    annotations : Mapping[str, str] | None, optional
        Annotations to apply to the Pod template metadata.
    restart_policy : str | None, optional
        Optional Pod restart policy, such as ``"Never"`` or ``"OnFailure"``.
    automount_service_account_token : bool, optional
        Whether pods should automount the default service-account token.
    service_account_name : str | None, optional
        Optional pod service-account name.
    node_selector : Mapping[str, str] | None, optional
        Optional node selector.
    node_name : str | None, optional
        Optional exact node name.
    host_pid : bool | None, optional
        Optional ``hostPID`` value.
    tolerations : Collection[TolerationSpec], optional
        Optional pod tolerations.
    image_pull_secrets : Collection[str], optional
        Optional image pull Secret names.
    priority_class_name : str | None, optional
        Optional priority class name.
    dns_policy : str | None, optional
        Optional DNS policy.
    host_network : bool | None, optional
        Optional ``hostNetwork`` value.
    termination_grace_period_seconds : int | None, optional
        Optional pod termination grace period in seconds.
    """

    containers: Collection[ContainerSpec]
    volumes: Collection[VolumeSpec] = ()
    resource_claims: Collection[PodResourceClaimSpec] = ()
    labels: Mapping[str, str] = MappingProxyType({})
    annotations: Mapping[str, str] | None = None
    restart_policy: str | None = None
    automount_service_account_token: bool = False
    service_account_name: str | None = None
    node_selector: Mapping[str, str] | None = None
    node_name: str | None = None
    host_pid: bool | None = None
    tolerations: Collection[TolerationSpec] = ()
    image_pull_secrets: Collection[str] = ()
    priority_class_name: str | None = None
    dns_policy: str | None = None
    host_network: bool | None = None
    termination_grace_period_seconds: int | None = None


@dataclass(frozen=True)
class VolumeSpec:
    """Intent-level Kubernetes pod volume specification.

    Parameters
    ----------
    name : str
        Pod volume name.
    empty_dir_source : bool, optional
        Whether the volume is backed by an `emptyDir` source.
    empty_dir_medium : str | None, optional
        Storage medium for `emptyDir` volumes, such as `"Memory"`.
    empty_dir_size_limit : str | None, optional
        Kubernetes quantity limiting the `emptyDir` volume size.
    config_map_name : str | None, optional
        ConfigMap name for ConfigMap-backed volumes.
    config_map_optional : bool | None, optional
        Whether the ConfigMap reference is optional.
    secret_name : str | None, optional
        Secret name for Secret-backed volumes.
    secret_optional : bool | None, optional
        Whether the Secret reference is optional.
    secret_default_mode : int | None, optional
        Default POSIX mode for Secret-backed volume files.
    secret_items : Collection[SecretVolumeItemSpec], optional
        Optional Secret key-to-path projections.
    persistent_volume_claim : str | None, optional
        PersistentVolumeClaim name for PVC-backed volumes.
    host_path_path : str | None, optional
        Host path for hostPath-backed volumes.
    host_path_type : str | None, optional
        Optional Kubernetes hostPath type constraint.
    """

    name: str
    empty_dir_source: bool = False
    empty_dir_medium: str | None = None
    empty_dir_size_limit: str | None = None
    config_map_name: str | None = None
    config_map_optional: bool | None = None
    secret_name: str | None = None
    secret_optional: bool | None = None
    secret_default_mode: int | None = None
    secret_items: Collection[SecretVolumeItemSpec] = ()
    persistent_volume_claim: str | None = None
    host_path_path: str | None = None
    host_path_type: str | None = None

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
        return cls(
            name=name,
            empty_dir_source=True,
            empty_dir_medium=medium,
            empty_dir_size_limit=size_limit,
        )

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
        return cls(
            name=name,
            config_map_name=config_map_name,
            config_map_optional=optional,
        )

    @classmethod
    def secret(
        cls,
        name: str,
        *,
        secret_name: str,
        optional: bool | None = None,
        default_mode: int | None = None,
        items: Collection[SecretVolumeItemSpec] = (),
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
        items : Collection[SecretVolumeItemSpec], optional
            Optional Secret key-to-path projections. If omitted, Kubernetes projects
            every Secret data key.

        Returns
        -------
        Self
            Volume specification.
        """
        return cls(
            name=name,
            secret_name=secret_name,
            secret_optional=optional,
            secret_default_mode=default_mode,
            secret_items=tuple(items),
        )

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
        return cls(name=name, persistent_volume_claim=claim_name)

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
        return cls(name=name, host_path_path=str(path), host_path_type=host_path_type)
