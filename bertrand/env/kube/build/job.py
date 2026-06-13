"""Short-lived BuildKit client Jobs for publishing OCI images."""

from __future__ import annotations

import hashlib
import json
import uuid
from contextlib import asynccontextmanager
from typing import TYPE_CHECKING

from bertrand.env.git import (
    BERTRAND_LABEL,
    BERTRAND_LABEL_MANAGED,
    BERTRAND_NAMESPACE,
    Deadline,
)
from bertrand.env.kube.api.manifest import ContainerSpec, PodTemplateSpec, VolumeSpec
from bertrand.env.kube.build.daemon import (
    BUILDKIT_IMAGE,
    BUILDKIT_SOCKET_ADDR,
    BUILDKIT_SOCKET_DIR,
    BUILDKIT_SOCKET_VOLUME,
    ready_buildkit_platform_nodes,
)
from bertrand.env.kube.build.refs import (
    DIGEST_RE,
    digest_ref,
    platform_output_ref,
)
from bertrand.env.kube.build.repository import current_buildkit_config_hash
from bertrand.env.kube.capability.base import (
    CapabilityKind,
    resolve_capability_secret,
)
from bertrand.env.kube.capability.device import (
    allocated_selector_script,
    create_resource_claim_templates,
    pod_resource_claim,
    resource_claim_name,
    select_device_claims,
)
from bertrand.env.kube.ceph.snapshot import prepared_repository_build_source
from bertrand.env.kube.configmap import ConfigMap, ConfigMapManifest
from bertrand.env.kube.job import Job

if TYPE_CHECKING:
    from collections.abc import AsyncIterator, Awaitable, Callable, Mapping

    from bertrand.env.config.core import KubeName
    from bertrand.env.kube.api.client import Kube
    from bertrand.env.kube.build.request import BuildKitBuildSpec

BUILD_JOB_CONTEXT_MOUNT = "/workspace"
BUILD_JOB_CONTEXT_VOLUME = "context"
BUILD_JOB_DOCKERFILE_KEY = "Containerfile"
BUILD_JOB_DOCKERFILE_MOUNT = "/run/bertrand/buildkit/dockerfile"
BUILD_JOB_DOCKERFILE_VOLUME = "dockerfile"
BUILD_JOB_METADATA_FILE = "/run/bertrand/buildkit/metadata/metadata.json"
BUILD_JOB_METADATA_MOUNT = "/run/bertrand/buildkit/metadata"
BUILD_JOB_METADATA_VOLUME = "metadata"
BUILD_JOB_DIGEST_SENTINEL = "BERTRAND_BUILDKIT_DIGEST="
BUILD_JOB_SECRET_MOUNT = "/run/bertrand/buildkit/secrets"
BUILD_JOB_SSH_MOUNT = "/run/bertrand/buildkit/ssh"
BUILD_JOB_LABEL = "bertrand.dev/build-job"
BUILD_JOB_LABEL_VALUE = "v1"
BUILD_JOB_TTL_SECONDS = 3600
BUILD_JOB_LOG_TAIL_LINES = 120
BUILD_JOB_DIAGNOSTIC_TIMEOUT_SECONDS = 10.0
BUILD_JOB_CLEANUP_TIMEOUT_SECONDS = 10.0
BUILD_PROGRESS = "plain"
BUILD_CONTEXT_PREFIX = "bertrand-project-image"
CAPABILITY_VALUE_KEY = "value"
BUILD_PLATFORM_RUN_ID_BYTES = 12


def _build_labels() -> dict[str, str]:
    return {
        "app.kubernetes.io/name": "bertrand-build",
        "app.kubernetes.io/part-of": "bertrand",
        BERTRAND_LABEL: BERTRAND_LABEL_MANAGED,
        BUILD_JOB_LABEL: BUILD_JOB_LABEL_VALUE,
    }


def _build_job_name(spec: BuildKitBuildSpec) -> str:
    payload = spec.model_dump(mode="json")
    text = json.dumps(
        payload,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=False,
    )
    digest = hashlib.sha256(text.encode("utf-8")).hexdigest()[:16]
    nonce = uuid.uuid4().hex[:8]
    return f"bertrand-build-{digest}-{nonce}"


def buildctl_args(
    spec: BuildKitBuildSpec,
    *,
    image: str | None = None,
    context_path: str = BUILD_JOB_CONTEXT_MOUNT,
    dockerfile_path: str = BUILD_JOB_CONTEXT_MOUNT,
    secret_paths: Mapping[str, str] | None = None,
    ssh_paths: Mapping[str, str] | None = None,
    metadata_file: str | None = None,
) -> list[str]:
    """Render `buildctl` command arguments for one build request.

    Parameters
    ----------
    spec : BuildKitBuildSpec
        Build request contract to execute.
    image : str | None, optional
        Optional output image override.
    context_path : str, optional
        Mounted BuildKit context path.
    dockerfile_path : str, optional
        Mounted Dockerfile context path.
    secret_paths : Mapping[str, str] | None, optional
        BuildKit secret mount paths keyed by capability ID.
    ssh_paths : Mapping[str, str] | None, optional
        BuildKit SSH mount paths keyed by capability ID.
    metadata_file : str | None, optional
        Optional BuildKit metadata output file path.

    Returns
    -------
    list[str]
        `buildctl` argument vector.

    Raises
    ------
    ValueError
        If the output image or required context paths are empty.
    """
    image = spec.image if image is None else image.strip()
    if not image:
        msg = "BuildKit output image reference cannot be empty"
        raise ValueError(msg)
    context_path = context_path.strip()
    dockerfile_path = dockerfile_path.strip()
    if not context_path or not dockerfile_path:
        msg = "BuildKit context and Dockerfile paths cannot be empty"
        raise ValueError(msg)
    args = [
        "--addr",
        BUILDKIT_SOCKET_ADDR,
        "build",
        "--progress",
        BUILD_PROGRESS,
        "--frontend",
        "dockerfile.v0",
        "--local",
        f"context={context_path}",
        "--local",
        f"dockerfile={dockerfile_path}",
        "--opt",
        "filename=Containerfile",
    ]
    if spec.target is not None:
        args.extend(["--opt", f"target={spec.target}"])
    if spec.pull != "missing":
        mode = "pull" if spec.pull == "always" else "local"
        args.extend(["--opt", f"image-resolve-mode={mode}"])
    for key, value in sorted(spec.build_args.items()):
        args.extend(["--opt", f"build-arg:{key}={value}"])
    for key, value in sorted(spec.image_labels.items()):
        args.extend(["--opt", f"label:{key}={value}"])
    for capability_id, path in sorted((secret_paths or {}).items()):
        args.extend(["--secret", f"id={capability_id},src={path}"])
    for capability_id, path in sorted((ssh_paths or {}).items()):
        args.extend(["--ssh", f"{capability_id}={path}"])
    if metadata_file is not None:
        metadata_file = metadata_file.strip()
        if metadata_file:
            args.extend(["--metadata-file", metadata_file])
    args.extend(["--output", f"type=image,name={image},push=true"])
    return args


async def publish_project_platforms(
    kube: Kube,
    spec: BuildKitBuildSpec,
    *,
    build_name: str,
    deadline: Deadline,
    job_observer: Callable[[str, Job], Awaitable[None]] | None = None,
) -> dict[str, str]:
    """Publish one native platform image per scheduled BuildKit target.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    spec : BuildKitBuildSpec
        Build request contract to execute.
    build_name : str
        Durable build request name used for temporary source preparation.
    deadline : Deadline
        Maximum execution budget in seconds.
    job_observer : Callable[[str, Job], Awaitable[None]] | None, optional
        Optional callback invoked with platform and Job snapshots.

    Returns
    -------
    dict[str, str]
        Digest-pinned platform image refs keyed by OCI platform.

    Raises
    ------
    ValueError
        If a generated platform output ref is empty.
    """
    async with _prepared_source(
        kube,
        spec,
        build_name=build_name,
        deadline=deadline,
    ) as (source_volumes, source_mounts):
        (
            capability_volumes,
            capability_mounts,
            secret_paths,
            ssh_paths,
        ) = await _capability_mounts(
            kube,
            spec,
            deadline=deadline,
        )
        targets = await _schedule(kube, spec, deadline=deadline)
        run_id = uuid.uuid4().hex[:BUILD_PLATFORM_RUN_ID_BYTES]
        results: dict[str, str] = {}
        for platform, dra_claims in sorted(targets.items()):
            image = platform_output_ref(spec.image, platform, run_id)
            image = image.strip()
            if not image:
                msg = f"BuildKit platform output ref is empty for platform {platform!r}"
                raise ValueError(msg)
            digest = await _publish_target(
                kube,
                spec,
                platform=platform,
                dra_claims=dra_claims,
                image=image,
                source_volumes=source_volumes,
                source_mounts=source_mounts,
                capability_volumes=tuple(capability_volumes),
                capability_mounts=tuple(capability_mounts),
                secret_paths=secret_paths,
                ssh_paths=ssh_paths,
                job_observer=(
                    None
                    if job_observer is None
                    else lambda job, platform=platform: job_observer(platform, job)
                ),
                deadline=deadline,
            )
            results[platform] = digest_ref(image, digest)
        return dict(sorted(results.items()))


async def _publish_target(
    kube: Kube,
    spec: BuildKitBuildSpec,
    *,
    platform: str,
    dra_claims: tuple[str, ...],
    image: str,
    source_volumes: tuple[VolumeSpec, ...],
    source_mounts: tuple[Mapping[str, object], ...],
    capability_volumes: tuple[VolumeSpec, ...],
    capability_mounts: tuple[Mapping[str, object], ...],
    secret_paths: Mapping[str, str],
    ssh_paths: Mapping[str, str],
    deadline: Deadline,
    job_observer: Callable[[Job], Awaitable[None]] | None = None,
) -> str:
    labels = _build_labels()
    job_name = _build_job_name(spec)
    created_claim_templates = await create_resource_claim_templates(
        kube,
        namespace=BERTRAND_NAMESPACE,
        owner=job_name,
        capability_ids=dra_claims,
        container_name="buildctl",
        labels=labels,
        deadline=deadline,
    )
    try:
        job = await Job.create(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=job_name,
            labels=labels,
            pod_template=PodTemplateSpec(
                containers=[
                    ContainerSpec(
                        name="buildctl",
                        image=BUILDKIT_IMAGE,
                        image_pull_policy="IfNotPresent",
                        command=["/bin/sh", "-ec"],
                        args=[
                            _build_job_script(required_dra_claims=len(dra_claims)),
                            "buildctl",
                            *buildctl_args(
                                spec,
                                image=image,
                                context_path=BUILD_JOB_CONTEXT_MOUNT,
                                dockerfile_path=BUILD_JOB_DOCKERFILE_MOUNT,
                                secret_paths=secret_paths,
                                ssh_paths=ssh_paths,
                                metadata_file=BUILD_JOB_METADATA_FILE,
                            ),
                        ],
                        volume_mounts=[
                            *source_mounts,
                            {
                                "name": BUILD_JOB_METADATA_VOLUME,
                                "mountPath": BUILD_JOB_METADATA_MOUNT,
                            },
                            *capability_mounts,
                            {
                                "name": BUILDKIT_SOCKET_VOLUME,
                                "mountPath": BUILDKIT_SOCKET_DIR,
                            },
                        ],
                        resources=(
                            {
                                "claims": [
                                    {
                                        "name": resource_claim_name(
                                            owner=job_name,
                                            capability_id=capability_id,
                                            container_name="buildctl",
                                        )
                                    }
                                    for capability_id in dra_claims
                                ]
                            }
                            if dra_claims
                            else None
                        ),
                    )
                ],
                volumes=[
                    *source_volumes,
                    VolumeSpec.empty_dir(BUILD_JOB_METADATA_VOLUME),
                    *capability_volumes,
                    VolumeSpec.host_path(
                        BUILDKIT_SOCKET_VOLUME,
                        path=BUILDKIT_SOCKET_DIR,
                        host_path_type="Directory",
                    ),
                ],
                resource_claims=tuple(
                    pod_resource_claim(
                        owner=job_name,
                        capability_id=capability_id,
                        container_name="buildctl",
                    )
                    for capability_id in dra_claims
                ),
                node_selector=_platform_node_selector(platform),
            ),
            ttl_seconds_after_finished=BUILD_JOB_TTL_SECONDS,
            deadline=deadline,
        )
        logs = await job.run_observed(
            kube,
            deadline=deadline,
            failure_context=f"BuildKit image build for {image!r} failed",
            log_heading="Build pod logs",
            log_failure_label="BuildKit build pod logs",
            tail_lines=BUILD_JOB_LOG_TAIL_LINES,
            diagnostic_deadline=Deadline(
                min(BUILD_JOB_DIAGNOSTIC_TIMEOUT_SECONDS, deadline.remaining)
            ),
            cleanup_deadline=Deadline(
                min(BUILD_JOB_CLEANUP_TIMEOUT_SECONDS, deadline.remaining)
            ),
            include_log_headers=True,
            observer=job_observer,
        )
        return _parse_build_digest(logs)
    finally:
        for template in created_claim_templates:
            if not template.namespace or not template.name:
                continue
            remaining = deadline.remaining
            if remaining <= 0:
                continue
            try:
                await template.delete(
                    kube,
                    deadline=Deadline(
                        min(BUILD_JOB_CLEANUP_TIMEOUT_SECONDS, remaining)
                    ),
                )
            except (OSError, TimeoutError):
                continue


@asynccontextmanager
async def _prepared_source(
    kube: Kube,
    spec: BuildKitBuildSpec,
    *,
    build_name: str,
    deadline: Deadline,
) -> AsyncIterator[tuple[tuple[VolumeSpec, ...], tuple[Mapping[str, object], ...]]]:
    config_name: str | None = None
    async with prepared_repository_build_source(
        kube,
        repo_id=spec.repo_id,
        build_name=build_name,
        deadline=deadline,
    ) as repo_source:
        config_name = _dockerfile_config_name()
        await ConfigMap.upsert(
            kube,
            intent=ConfigMapManifest(
                namespace=BERTRAND_NAMESPACE,
                name=config_name,
                labels=_build_labels(),
                data={BUILD_JOB_DOCKERFILE_KEY: spec.dockerfile},
            ),
            deadline=deadline,
        )
        try:
            yield (
                (
                    VolumeSpec.pvc(
                        BUILD_JOB_CONTEXT_VOLUME,
                        claim_name=repo_source,
                    ),
                    VolumeSpec.config_map(
                        BUILD_JOB_DOCKERFILE_VOLUME,
                        config_map_name=config_name,
                    ),
                ),
                (
                    {
                        "name": BUILD_JOB_CONTEXT_VOLUME,
                        "mountPath": BUILD_JOB_CONTEXT_MOUNT,
                        "readOnly": True,
                        "subPath": _worktree_sub_path(spec.worktree),
                    },
                    {
                        "name": BUILD_JOB_DOCKERFILE_VOLUME,
                        "mountPath": BUILD_JOB_DOCKERFILE_MOUNT,
                        "readOnly": True,
                    },
                ),
            )
        finally:
            remaining = deadline.remaining
            await _delete_config_map(
                kube,
                name=config_name,
                deadline=(
                    Deadline(min(BUILD_JOB_CLEANUP_TIMEOUT_SECONDS, remaining))
                    if remaining > 0
                    else None
                ),
            )


async def _capability_mounts(
    kube: Kube,
    spec: BuildKitBuildSpec,
    *,
    deadline: Deadline,
) -> tuple[
    list[VolumeSpec],
    list[Mapping[str, object]],
    dict[str, str],
    dict[str, str],
]:
    volumes: list[VolumeSpec] = []
    mounts: list[Mapping[str, object]] = []
    secret_paths: dict[str, str] = {}
    ssh_paths: dict[str, str] = {}
    groups: tuple[
        tuple[CapabilityKind, Mapping[KubeName, bool], str, dict[str, str]],
        ...,
    ] = (
        ("secret", spec.secrets, BUILD_JOB_SECRET_MOUNT, secret_paths),
        ("ssh", spec.ssh, BUILD_JOB_SSH_MOUNT, ssh_paths),
    )

    for kind, requests, mount_root, paths in groups:
        for capability_id, required in sorted(requests.items()):
            secret = await resolve_capability_secret(
                kube,
                kind=kind,
                capability_id=capability_id,
                worktree_id=spec.worktree_id,
                repo_id=spec.repo_id,
                required=required,
                deadline=deadline,
            )
            if secret is None:
                continue
            volume_name = _capability_volume_name(kind, capability_id)
            mount_path = f"{mount_root}/{capability_id}"
            volumes.append(
                VolumeSpec.secret(
                    volume_name,
                    secret_name=secret.name,
                    default_mode=0o400,
                )
            )
            mounts.append(
                {"name": volume_name, "mountPath": mount_path, "readOnly": True}
            )
            paths[capability_id] = f"{mount_path}/{CAPABILITY_VALUE_KEY}"

    return volumes, mounts, secret_paths, ssh_paths


async def _schedule(
    kube: Kube,
    spec: BuildKitBuildSpec,
    *,
    deadline: Deadline,
) -> dict[str, tuple[str, ...]]:
    config_hash = await current_buildkit_config_hash(
        kube,
        deadline=deadline,
    )
    groups = await ready_buildkit_platform_nodes(
        kube,
        deadline=deadline,
        config_hash=config_hash,
    )

    targets: dict[str, tuple[str, ...]] = {}
    for platform, node_names in groups.items():
        targets[platform] = await select_device_claims(
            kube,
            requests=spec.devices,
            node_names=node_names,
            deadline=deadline,
        )
    return targets


def _capability_volume_name(kind: CapabilityKind, capability_id: str) -> str:
    payload = f"{kind}:{capability_id}".encode()
    digest = hashlib.sha256(payload).hexdigest()[:16]
    return f"build-{kind}-{digest}"


def _build_job_script(*, required_dra_claims: int) -> str:
    return "\n".join(
        (
            "set -eu",
            allocated_selector_script(required_count=required_dra_claims),
            '"$0" "$@"',
            (
                "digest=$(sed -n "
                "'s/.*\"containerimage.digest\"[[:space:]]*:[[:space:]]*"
                "\"\\([^\"]*\\)\".*/\\1/p' "
                f"{BUILD_JOB_METADATA_FILE!r} | tail -n 1)"
            ),
            (
                'if [ -z "$digest" ]; then '
                'echo "BuildKit metadata is missing containerimage.digest" >&2; '
                "exit 1; "
                "fi"
            ),
            f"printf '%s%s\\n' {BUILD_JOB_DIGEST_SENTINEL!r} \"$digest\"",
        )
    )


def _platform_node_selector(platform: str) -> dict[str, str]:
    parts = platform.split("/")
    if len(parts) < 2 or not parts[0].strip() or not parts[1].strip():
        msg = f"BuildKit platform is not a supported OCI platform: {platform!r}"
        raise ValueError(msg)
    return {
        "kubernetes.io/os": parts[0].strip(),
        "kubernetes.io/arch": parts[1].strip(),
    }


def _parse_build_digest(logs: str) -> str:
    for line in reversed(logs.splitlines()):
        if not line.startswith(BUILD_JOB_DIGEST_SENTINEL):
            continue
        digest = line[len(BUILD_JOB_DIGEST_SENTINEL) :].strip()
        if not DIGEST_RE.fullmatch(digest):
            msg = f"BuildKit Job emitted malformed image digest: {digest!r}"
            raise OSError(msg)
        return digest
    msg = "BuildKit Job did not emit a pushed image digest"
    raise OSError(msg)


def _worktree_sub_path(worktree: str) -> str | None:
    return None if worktree == "." else worktree


def _dockerfile_config_name() -> str:
    payload = f"{BUILD_CONTEXT_PREFIX}:{uuid.uuid4().hex}".encode()
    digest = hashlib.sha256(payload).hexdigest()[:24]
    return f"bertrand-build-dockerfile-{digest}"


async def _delete_config_map(
    kube: Kube,
    *,
    name: str,
    deadline: Deadline | None,
) -> None:
    if deadline is None:
        return
    try:
        config = await ConfigMap.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=name,
            deadline=deadline,
        )
        if config is not None:
            await config.delete(
                kube,
                deadline=deadline,
            )
    except (OSError, TimeoutError):
        return
