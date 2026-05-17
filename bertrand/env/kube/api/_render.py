"""Private rendering for Kubernetes intent specs."""

from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from .spec import (
        ContainerSpec,
        PodResourceClaimSpec,
        PodTemplateSpec,
        ProbeSpec,
        SecurityContextSpec,
        TolerationSpec,
        VolumeSpec,
    )


def _probe_manifest(probe: ProbeSpec) -> dict[str, object]:
    sources = sum(
        (
            probe.tcp_port is not None,
            probe.http_path is not None or probe.http_port is not None,
        )
    )
    if sources != 1:
        msg = "probe must define exactly one source"
        raise ValueError(msg)
    payload: dict[str, object]
    if probe.tcp_port is not None:
        payload = {"tcpSocket": {"port": probe.tcp_port}}
    else:
        if probe.http_path is None or probe.http_port is None:
            msg = "HTTP probe requires path and port"
            raise ValueError(msg)
        payload = {"httpGet": {"path": probe.http_path, "port": probe.http_port}}
    if probe.initial_delay_seconds is not None:
        payload["initialDelaySeconds"] = probe.initial_delay_seconds
    if probe.period_seconds is not None:
        payload["periodSeconds"] = probe.period_seconds
    if probe.failure_threshold is not None:
        payload["failureThreshold"] = probe.failure_threshold
    return payload


def _seccomp_profile_manifest(
    *,
    profile_type: str | None,
    localhost_profile: str | None,
) -> dict[str, object] | None:
    if profile_type is None and localhost_profile is None:
        return None
    payload: dict[str, object] = {}
    if profile_type is not None:
        payload["type"] = profile_type
    if localhost_profile is not None:
        payload["localhostProfile"] = localhost_profile
    return payload


def _security_context_manifest(
    security_context: SecurityContextSpec,
) -> dict[str, object]:
    payload: dict[str, object] = {}
    if security_context.privileged is not None:
        payload["privileged"] = security_context.privileged
    if security_context.run_as_user is not None:
        payload["runAsUser"] = security_context.run_as_user
    if security_context.run_as_group is not None:
        payload["runAsGroup"] = security_context.run_as_group
    if security_context.run_as_non_root is not None:
        payload["runAsNonRoot"] = security_context.run_as_non_root
    if security_context.read_only_root_filesystem is not None:
        payload["readOnlyRootFilesystem"] = security_context.read_only_root_filesystem
    if security_context.allow_privilege_escalation is not None:
        payload["allowPrivilegeEscalation"] = (
            security_context.allow_privilege_escalation
        )
    capabilities: dict[str, object] = {}
    if security_context.capabilities_add:
        capabilities["add"] = list(security_context.capabilities_add)
    if security_context.capabilities_drop:
        capabilities["drop"] = list(security_context.capabilities_drop)
    if capabilities:
        payload["capabilities"] = capabilities
    seccomp_profile = _seccomp_profile_manifest(
        profile_type=security_context.seccomp_profile_type,
        localhost_profile=security_context.seccomp_profile_localhost_profile,
    )
    if seccomp_profile is not None:
        payload["seccompProfile"] = seccomp_profile
    return payload


def _toleration_manifest(toleration: TolerationSpec) -> dict[str, object]:
    payload: dict[str, object] = {}
    if toleration.key is not None:
        payload["key"] = toleration.key
    if toleration.operator is not None:
        payload["operator"] = toleration.operator
    if toleration.value is not None:
        payload["value"] = toleration.value
    if toleration.effect is not None:
        payload["effect"] = toleration.effect
    if toleration.toleration_seconds is not None:
        if toleration.toleration_seconds < 0:
            msg = "toleration seconds cannot be negative"
            raise ValueError(msg)
        payload["tolerationSeconds"] = toleration.toleration_seconds
    return payload


def _container_manifest(container: ContainerSpec) -> dict[str, object]:
    payload: dict[str, object] = {
        "name": container.name,
        "image": container.image,
    }
    if container.image_pull_policy is not None:
        payload["imagePullPolicy"] = container.image_pull_policy
    if container.command is not None:
        payload["command"] = list(container.command)
    if container.args is not None:
        payload["args"] = list(container.args)
    if container.ports:
        payload["ports"] = [
            {
                "name": port.name,
                "containerPort": port.container_port,
                "protocol": port.protocol,
            }
            for port in container.ports
        ]
    if container.env:
        env: list[dict[str, object]] = []
        for var in container.env:
            secret_source = var.secret_name is not None or var.secret_key is not None
            config_map_source = (
                var.config_map_name is not None or var.config_map_key is not None
            )
            sources = sum(
                (
                    var.value is not None,
                    var.field_path is not None,
                    secret_source,
                    config_map_source,
                )
            )
            if sources != 1:
                msg = "environment variable must define exactly one source"
                raise ValueError(msg)
            item: dict[str, object] = {"name": var.name}
            if var.value is not None:
                item["value"] = var.value
            elif var.field_path is not None:
                item["valueFrom"] = {"fieldRef": {"fieldPath": var.field_path}}
            elif secret_source:
                if var.secret_name is None or var.secret_key is None:
                    msg = "Secret environment variable source requires name and key"
                    raise ValueError(msg)
                secret_ref: dict[str, object] = {
                    "name": var.secret_name,
                    "key": var.secret_key,
                }
                if var.secret_optional is not None:
                    secret_ref["optional"] = var.secret_optional
                item["valueFrom"] = {"secretKeyRef": secret_ref}
            elif config_map_source:
                if var.config_map_name is None or var.config_map_key is None:
                    msg = "ConfigMap environment variable source requires name and key"
                    raise ValueError(msg)
                config_map_ref: dict[str, object] = {
                    "name": var.config_map_name,
                    "key": var.config_map_key,
                }
                if var.config_map_optional is not None:
                    config_map_ref["optional"] = var.config_map_optional
                item["valueFrom"] = {"configMapKeyRef": config_map_ref}
            env.append(item)
        payload["env"] = env
    if container.readiness_probe is not None:
        payload["readinessProbe"] = _probe_manifest(container.readiness_probe)
    if container.liveness_probe is not None:
        payload["livenessProbe"] = _probe_manifest(container.liveness_probe)
    if container.volume_mounts:
        payload["volumeMounts"] = [
            {
                key: value
                for key, value in {
                    "name": mount.name,
                    "mountPath": mount.mount_path,
                    "readOnly": mount.read_only,
                    "subPath": mount.sub_path,
                }.items()
                if value is not None
            }
            for mount in container.volume_mounts
        ]
    if container.security_context is not None:
        security_context = _security_context_manifest(container.security_context)
        if security_context:
            payload["securityContext"] = security_context
    if container.resources is not None:
        claims = [
            {"name": name}
            for claim in container.resources.claims
            if (name := claim.strip())
        ]
        if claims:
            payload["resources"] = {"claims": claims}
    return payload


def _pod_resource_claim_manifest(claim: PodResourceClaimSpec) -> dict[str, object]:
    name = claim.name.strip()
    if not name:
        msg = "pod resource claim name cannot be empty"
        raise ValueError(msg)
    resource_claim_name = (
        claim.resource_claim_name.strip()
        if claim.resource_claim_name is not None
        else ""
    )
    template_name = (
        claim.resource_claim_template_name.strip()
        if claim.resource_claim_template_name is not None
        else ""
    )
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


def _volume_manifest(volume: VolumeSpec) -> dict[str, object]:
    empty_dir_source = (
        volume.empty_dir_source
        or volume.empty_dir_medium is not None
        or volume.empty_dir_size_limit is not None
    )
    kinds = sum(
        (
            empty_dir_source,
            volume.config_map_name is not None,
            volume.secret_name is not None,
            volume.persistent_volume_claim is not None,
            volume.host_path_path is not None,
        )
    )
    if kinds != 1:
        msg = "Kubernetes volume must define exactly one source"
        raise ValueError(msg)

    payload: dict[str, object] = {"name": volume.name}
    if empty_dir_source:
        empty_dir: dict[str, object] = {}
        if volume.empty_dir_medium is not None:
            empty_dir["medium"] = volume.empty_dir_medium
        if volume.empty_dir_size_limit is not None:
            empty_dir["sizeLimit"] = volume.empty_dir_size_limit
        payload["emptyDir"] = empty_dir
    elif volume.config_map_name is not None:
        config_map: dict[str, object] = {"name": volume.config_map_name}
        if volume.config_map_optional is not None:
            config_map["optional"] = volume.config_map_optional
        payload["configMap"] = config_map
    elif volume.secret_name is not None:
        secret: dict[str, object] = {"secretName": volume.secret_name}
        if volume.secret_optional is not None:
            secret["optional"] = volume.secret_optional
        if volume.secret_default_mode is not None:
            secret["defaultMode"] = volume.secret_default_mode
        payload["secret"] = secret
    elif volume.persistent_volume_claim is not None:
        payload["persistentVolumeClaim"] = {"claimName": volume.persistent_volume_claim}
    elif volume.host_path_path is not None:
        host_path: dict[str, object] = {"path": volume.host_path_path}
        if volume.host_path_type is not None:
            host_path["type"] = volume.host_path_type
        payload["hostPath"] = host_path
    return payload


def _pod_template_manifest(template: PodTemplateSpec) -> dict[str, object]:
    spec: dict[str, object] = {
        "automountServiceAccountToken": template.automount_service_account_token,
        "containers": [
            _container_manifest(container) for container in template.containers
        ],
        "volumes": [_volume_manifest(volume) for volume in template.volumes],
    }
    if template.resource_claims:
        spec["resourceClaims"] = [
            _pod_resource_claim_manifest(claim) for claim in template.resource_claims
        ]
    if template.restart_policy is not None:
        spec["restartPolicy"] = template.restart_policy
    if template.service_account_name is not None:
        service_account_name = template.service_account_name.strip()
        if service_account_name:
            spec["serviceAccountName"] = service_account_name
    if template.node_selector:
        spec["nodeSelector"] = dict(template.node_selector)
    if template.node_name is not None:
        node_name = template.node_name.strip()
        if node_name:
            spec["nodeName"] = node_name
    if template.tolerations:
        spec["tolerations"] = [
            _toleration_manifest(toleration) for toleration in template.tolerations
        ]
    if template.image_pull_secrets:
        spec["imagePullSecrets"] = [
            {"name": name}
            for secret in template.image_pull_secrets
            if (name := secret.strip())
        ]
    if template.priority_class_name is not None:
        priority_class_name = template.priority_class_name.strip()
        if priority_class_name:
            spec["priorityClassName"] = priority_class_name
    if template.dns_policy is not None:
        dns_policy = template.dns_policy.strip()
        if dns_policy:
            spec["dnsPolicy"] = dns_policy
    if template.host_network is not None:
        spec["hostNetwork"] = template.host_network
    if template.host_pid is not None:
        spec["hostPID"] = template.host_pid
    if template.termination_grace_period_seconds is not None:
        if template.termination_grace_period_seconds < 0:
            msg = "termination grace period cannot be negative"
            raise ValueError(msg)
        spec["terminationGracePeriodSeconds"] = (
            template.termination_grace_period_seconds
        )
    metadata: dict[str, object] = {"labels": dict(template.labels)}
    if template.annotations:
        metadata["annotations"] = dict(template.annotations)
    return {"metadata": metadata, "spec": spec}
