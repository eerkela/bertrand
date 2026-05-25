"""OCI manifest assembly for Bertrand's in-cluster build runtime."""

from __future__ import annotations

import shlex
import uuid
from types import MappingProxyType
from typing import TYPE_CHECKING

from bertrand.env.config.core import _check_kube_name, _check_uuid
from bertrand.env.git import BERTRAND_ENV, BERTRAND_NAMESPACE, Deadline
from bertrand.env.kube.api.spec import ContainerSpec, EnvVarSpec, PodTemplateSpec
from bertrand.env.kube.build.execution import run_observed_job
from bertrand.env.kube.build.lifecycle import (
    ProjectImagePublication,
    record_project_image,
)
from bertrand.env.kube.build.refs import (
    DIGEST_REF_RE,
    tagged_repository,
    validate_tagged_ref,
)
from bertrand.env.kube.build.repository import IMAGES, ImageRepository
from bertrand.env.kube.capability.base import Capability
from bertrand.env.kube.job import Job

if TYPE_CHECKING:
    from collections.abc import Awaitable, Callable, Mapping

    from bertrand.env.config.core import KubeName
    from bertrand.env.kube.api.client import Kube
    from bertrand.env.kube.build.request import ProjectImageIdentity

MANIFEST_JOB_IMAGE = "ghcr.io/regclient/regctl:v0.10.0-alpine"
MANIFEST_JOB_LABEL = "bertrand.dev/manifest-job"
MANIFEST_JOB_LABEL_VALUE = "v1"
MANIFEST_JOB_TTL_SECONDS = 3600
MANIFEST_JOB_LOG_TAIL_LINES = 120
MANIFEST_JOB_DIAGNOSTIC_TIMEOUT_SECONDS = 10.0
MANIFEST_JOB_CLEANUP_TIMEOUT_SECONDS = 10.0
MANIFEST_INTERNAL_SENTINEL = "BERTRAND_INTERNAL_DIGEST_REF="
MANIFEST_EXTERNAL_SENTINEL = "BERTRAND_EXTERNAL_DIGEST_REF="
MANIFEST_AUTH_ENV = "DOCKER_AUTH_CONFIG"
MANIFEST_AUTH_KEY = "value"


async def _publish_project_image_manifest(
    kube: Kube,
    *,
    identity: ProjectImageIdentity,
    platform_refs: Mapping[str, str],
    timeout: float,
    external_image: str | None = None,
    auth_id: KubeName | None = None,
    repository: ImageRepository = IMAGES,
    job_observer: Callable[[Job], Awaitable[None]] | None = None,
) -> ProjectImagePublication:
    """Assemble, copy, and record one project image manifest.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    identity : ProjectImageIdentity
        Executed project image identity to record after manifest publication.
    platform_refs : Mapping[str, str]
        Digest-pinned platform image refs returned by native BuildKit executions,
        keyed by OCI platform.
    timeout : float
        Maximum runtime budget in seconds. If infinite, wait indefinitely.
    external_image : str | None, optional
        Optional external mutable image reference to copy the manifest to.
    auth_id : KubeName | None, optional
        Secret capability ID containing Docker auth JSON for the external registry.
    repository : ImageRepository, optional
        Internal image repository that owns `image` and platform digest refs.
    job_observer : Callable[[Job], Awaitable[None]] | None, optional
        Callback invoked after the manifest assembly Job is created.

    Returns
    -------
    ProjectImagePublication
        Record-backed publication result for CLI output and downstream callers.

    Raises
    ------
    TimeoutError
        If the assembly Job cannot complete before `timeout`.
    ValueError
        If refs, platforms, or auth inputs are malformed.
    """
    if timeout <= 0:
        msg = "image manifest publish timeout must be non-negative"
        raise TimeoutError(msg)
    image = validate_tagged_ref(identity.image, label="internal manifest target")
    internal_service_ref = repository.service_ref(image)
    if repository.pull_ref(internal_service_ref) != image:
        msg = f"internal manifest target is not canonical: {image!r}"
        raise ValueError(msg)
    external_image = (
        validate_tagged_ref(external_image, label="external manifest target")
        if external_image is not None
        else None
    )
    if auth_id is not None:
        auth_id = _check_kube_name(auth_id)
    worktree_id = _check_uuid(identity.worktree_id)
    repo_id = _check_uuid(identity.repo_id)

    platform_refs = _validate_platform_refs(platform_refs, repository)
    deadline = Deadline.from_timeout(
        timeout,
        message="image manifest publish timeout must be non-negative",
    )
    auth_secret = await _resolve_auth_secret(
        kube,
        auth_id=auth_id,
        worktree_id=worktree_id,
        repo_id=repo_id,
        timeout=deadline.remaining(),
    )
    script = _manifest_script(
        image=internal_service_ref,
        platform_refs={
            platform: repository.service_ref(ref)
            for platform, ref in platform_refs.items()
        },
        external_image=external_image,
        repository=repository,
    )
    job = await Job.create(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=_manifest_job_name(),
        labels={
            BERTRAND_ENV: "1",
            MANIFEST_JOB_LABEL: MANIFEST_JOB_LABEL_VALUE,
        },
        pod_template=PodTemplateSpec(
            containers=[
                ContainerSpec(
                    name="regctl",
                    image=MANIFEST_JOB_IMAGE,
                    image_pull_policy="IfNotPresent",
                    command=["/bin/sh", "-ec"],
                    args=[script],
                    env=_manifest_env(auth_secret),
                )
            ],
        ),
        ttl_seconds_after_finished=MANIFEST_JOB_TTL_SECONDS,
        timeout=deadline.remaining(),
    )
    logs = await run_observed_job(
        kube,
        job,
        timeout=deadline.remaining(),
        failure_context=f"image manifest assembly failed for {image!r}",
        log_heading="manifest Job logs",
        log_failure_label="manifest Job pod logs",
        tail_lines=MANIFEST_JOB_LOG_TAIL_LINES,
        diagnostic_timeout=MANIFEST_JOB_DIAGNOSTIC_TIMEOUT_SECONDS,
        cleanup_timeout=MANIFEST_JOB_CLEANUP_TIMEOUT_SECONDS,
        observer=job_observer,
    )

    internal_digest_ref = repository.pull_ref(
        _parse_sentinel(logs, MANIFEST_INTERNAL_SENTINEL)
    )
    external_digest_ref = (
        _parse_sentinel(logs, MANIFEST_EXTERNAL_SENTINEL)
        if external_image is not None
        else None
    )
    record = await record_project_image(
        kube,
        identity=identity,
        image_digest_ref=internal_digest_ref,
        platform_images=MappingProxyType(dict(platform_refs)),
        timeout=deadline.remaining(),
    )
    return ProjectImagePublication(
        record=record,
        external_digest_ref=external_digest_ref,
    )


def _validate_platform_refs(
    platform_refs: Mapping[str, str],
    repository: ImageRepository,
) -> dict[str, str]:
    if not platform_refs:
        msg = "image manifest assembly requires at least one platform result"
        raise ValueError(msg)
    validated: dict[str, str] = {}
    for raw_platform, raw_ref in platform_refs.items():
        platform = raw_platform.strip()
        if not platform:
            msg = "image manifest platform cannot be empty"
            raise ValueError(msg)
        if platform in validated:
            msg = f"duplicate image manifest platform: {platform!r}"
            raise ValueError(msg)
        digest_ref = raw_ref.strip()
        if not DIGEST_REF_RE.fullmatch(digest_ref):
            msg = f"platform image ref must include a sha256 digest: {digest_ref!r}"
            raise ValueError(msg)
        repository.service_ref(digest_ref)
        validated[platform] = digest_ref
    return dict(sorted(validated.items()))


async def _resolve_auth_secret(
    kube: Kube,
    *,
    auth_id: str | None,
    worktree_id: str,
    repo_id: str,
    timeout: float,
) -> str | None:
    if auth_id is None:
        return None
    capability = await Capability.resolve(
        kube,
        kind="secret",
        capability_id=auth_id,
        worktree_id=worktree_id,
        repo_id=repo_id,
        required=True,
        timeout=timeout,
    )
    if capability is None:
        return None
    return capability.secret.name


def _manifest_env(auth_secret: str | None) -> tuple[EnvVarSpec, ...]:
    env = [EnvVarSpec(name="HOME", value="/tmp/regctl")]
    if auth_secret is not None:
        env.append(
            EnvVarSpec.secret_key_ref(
                MANIFEST_AUTH_ENV,
                secret_name=auth_secret,
                key=MANIFEST_AUTH_KEY,
            )
        )
    return tuple(env)


def _manifest_script(
    *,
    image: str,
    platform_refs: Mapping[str, str],
    external_image: str | None,
    repository: ImageRepository,
) -> str:
    host = f"reg={repository.service_addr},tls=disabled"
    internal_repo = tagged_repository(image)
    lines = [
        "set -eu",
        'mkdir -p "$HOME/.docker"',
        (
            f'if [ -n "${{{MANIFEST_AUTH_ENV}:-}}" ]; then '
            f'printf "%s" "${{{MANIFEST_AUTH_ENV}}}" > "$HOME/.docker/config.json"; '
            "fi"
        ),
        _regctl(host, "index", "create", image),
    ]
    for platform, ref in platform_refs.items():
        lines.append(
            _regctl(
                host,
                "index",
                "add",
                image,
                "--ref",
                ref,
                "--desc-platform",
                platform,
            )
        )
    lines.extend(
        [
            f"internal_digest=$({_regctl(host, 'image', 'digest', image)})",
            (
                f"printf '%s%s@%s\\n' {shlex.quote(MANIFEST_INTERNAL_SENTINEL)} "
                f"{shlex.quote(internal_repo)} \"$internal_digest\""
            ),
        ]
    )
    if external_image is not None:
        external_repo = tagged_repository(external_image)
        lines.extend(
            [
                _regctl(host, "image", "copy", image, external_image),
                (
                    "external_digest="
                    f"$({_regctl(host, 'image', 'digest', external_image)})"
                ),
                (
                    f"printf '%s%s@%s\\n' {shlex.quote(MANIFEST_EXTERNAL_SENTINEL)} "
                    f"{shlex.quote(external_repo)} \"$external_digest\""
                ),
            ]
        )
    return "\n".join(lines)


def _regctl(host: str, *args: str) -> str:
    return " ".join(
        [
            "regctl",
            "--host",
            shlex.quote(host),
            *(shlex.quote(arg) for arg in args),
        ]
    )


def _parse_sentinel(logs: str, prefix: str) -> str:
    for line in reversed(logs.splitlines()):
        if line.startswith(prefix):
            value = line[len(prefix) :].strip()
            if not DIGEST_REF_RE.fullmatch(value):
                msg = f"manifest Job emitted malformed digest ref: {value!r}"
                raise OSError(msg)
            return value
    msg = f"manifest Job did not emit required sentinel {prefix.rstrip('=')!r}"
    raise OSError(msg)


def _manifest_job_name() -> str:
    return f"bertrand-manifest-{uuid.uuid4().hex[:32]}"
