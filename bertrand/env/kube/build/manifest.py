"""OCI manifest assembly for Bertrand's in-cluster build runtime."""

from __future__ import annotations

import asyncio
import hashlib
import shlex
import uuid
from types import MappingProxyType
from typing import TYPE_CHECKING

from bertrand.env.config.core import _check_kube_name, _check_uuid
from bertrand.env.git import BERTRAND_ENV, BERTRAND_NAMESPACE
from bertrand.env.kube.api.spec import ContainerSpec, EnvVarSpec, PodTemplateSpec
from bertrand.env.kube.build.execution import run_observed_job
from bertrand.env.kube.build.lifecycle import (
    ProjectImagePublication,
    record_project_image,
)
from bertrand.env.kube.build.refs import (
    DIGEST_REF_RE,
    channel_refs,
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
MANIFEST_INTERNAL_CHANNEL_SENTINEL = "BERTRAND_INTERNAL_CHANNEL_DIGEST_REF="
MANIFEST_EXTERNAL_CHANNEL_SENTINEL = "BERTRAND_EXTERNAL_CHANNEL_DIGEST_REF="
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
    env_id: str | None = None,
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
    env_id : str | None, optional
        Environment UUID used to resolve `auth_id`.
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
    internal_channels = channel_refs(image, identity.channels)
    for name, ref in internal_channels.items():
        if ref == image:
            msg = f"internal manifest channel {name!r} duplicates target image"
            raise ValueError(msg)
        service_ref = repository.service_ref(ref)
        if repository.pull_ref(service_ref) != ref:
            msg = f"internal manifest channel is not canonical: {ref!r}"
            raise ValueError(msg)
    external_channel_refs = (
        channel_refs(external_image, identity.channels)
        if external_image is not None
        else {}
    )
    if external_image is not None:
        for name, ref in external_channel_refs.items():
            if ref == external_image:
                msg = f"external manifest channel {name!r} duplicates target image"
                raise ValueError(msg)
    if auth_id is not None and env_id is None:
        msg = "external registry auth capability requires an environment identity"
        raise ValueError(msg)
    if auth_id is not None:
        auth_id = _check_kube_name(auth_id)
    if env_id is not None:
        env_id = _check_uuid(env_id)

    platform_refs = _validate_platform_refs(platform_refs, repository)
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    auth_secret = await _resolve_auth_secret(
        kube,
        auth_id=auth_id,
        env_id=env_id,
        timeout=deadline - loop.time(),
    )
    script = _manifest_script(
        image=internal_service_ref,
        platform_refs={
            platform: repository.service_ref(ref)
            for platform, ref in platform_refs.items()
        },
        external_image=external_image,
        channels={
            name: repository.service_ref(ref) for name, ref in internal_channels.items()
        },
        external_channels=external_channel_refs,
        repository=repository,
    )
    job = await Job.create(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=_manifest_job_name(
            image,
            platform_refs,
            external_image,
            internal_channels,
            external_channel_refs,
        ),
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
        timeout=deadline - loop.time(),
    )
    logs = await run_observed_job(
        kube,
        job,
        timeout=deadline - loop.time(),
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
    channel_digest_refs = MappingProxyType(
        {
            name: repository.pull_ref(ref)
            for name, ref in _parse_channel_sentinels(
                logs,
                MANIFEST_INTERNAL_CHANNEL_SENTINEL,
                expected=internal_channels,
            ).items()
        }
    )
    external_digest_ref = (
        _parse_sentinel(logs, MANIFEST_EXTERNAL_SENTINEL)
        if external_image is not None
        else None
    )
    external_channel_digest_refs = MappingProxyType(
        _parse_channel_sentinels(
            logs,
            MANIFEST_EXTERNAL_CHANNEL_SENTINEL,
            expected=external_channel_refs,
        )
    )
    record = await record_project_image(
        kube,
        identity=identity,
        image_digest_ref=internal_digest_ref,
        platform_images=MappingProxyType(dict(platform_refs)),
        channel_digest_refs=channel_digest_refs,
        timeout=deadline - loop.time(),
    )
    return ProjectImagePublication(
        record=record,
        external_digest_ref=external_digest_ref,
        external_channel_digest_refs=MappingProxyType(
            dict(external_channel_digest_refs)
        ),
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
    env_id: str | None,
    timeout: float,
) -> str | None:
    if auth_id is None:
        return None
    capability = await Capability.resolve(
        kube,
        kind="secret",
        capability_id=auth_id,
        env_id=env_id,
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
    channels: Mapping[str, str],
    external_channels: Mapping[str, str],
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
    lines.extend(
        _channel_copy_lines(
            host,
            source=image,
            channels=channels,
            sentinel=MANIFEST_INTERNAL_CHANNEL_SENTINEL,
            var_prefix="internal_channel_digest",
        )
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
        lines.extend(
            _channel_copy_lines(
                host,
                source=image,
                channels=external_channels,
                sentinel=MANIFEST_EXTERNAL_CHANNEL_SENTINEL,
                var_prefix="external_channel_digest",
            )
        )
    return "\n".join(lines)


def _channel_copy_lines(
    host: str,
    *,
    source: str,
    channels: Mapping[str, str],
    sentinel: str,
    var_prefix: str,
) -> list[str]:
    lines: list[str] = []
    for index, (name, channel_ref) in enumerate(channels.items()):
        channel_repo = tagged_repository(channel_ref)
        digest_var = f"{var_prefix}_{index}"
        lines.extend(
            [
                _regctl(host, "image", "copy", source, channel_ref),
                f"{digest_var}=$({_regctl(host, 'image', 'digest', channel_ref)})",
                (
                    f"printf '%s%s=%s@%s\\n' "
                    f"{shlex.quote(sentinel)} {shlex.quote(name)} "
                    f"{shlex.quote(channel_repo)} \"${digest_var}\""
                ),
            ]
        )
    return lines


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


def _parse_channel_sentinels(
    logs: str,
    prefix: str,
    *,
    expected: Mapping[str, str],
) -> dict[str, str]:
    out: dict[str, str] = {}
    for line in logs.splitlines():
        if not line.startswith(prefix):
            continue
        payload = line[len(prefix) :].strip()
        name, sep, ref = payload.partition("=")
        if not sep or not name:
            msg = f"manifest Job emitted malformed channel sentinel: {payload!r}"
            raise OSError(msg)
        if name not in expected:
            msg = f"manifest Job emitted unexpected channel digest: {name!r}"
            raise OSError(msg)
        if not DIGEST_REF_RE.fullmatch(ref):
            msg = f"manifest Job emitted malformed channel digest ref: {ref!r}"
            raise OSError(msg)
        out[name] = ref
    missing = set(expected).difference(out)
    if missing:
        formatted = ", ".join(repr(name) for name in sorted(missing))
        msg = f"manifest Job did not emit digest refs for channel(s): {formatted}"
        raise OSError(msg)
    return dict(sorted(out.items()))


def _manifest_job_name(
    image: str,
    platform_refs: Mapping[str, str],
    external_image: str | None,
    channels: Mapping[str, str],
    external_channels: Mapping[str, str],
) -> str:
    digest = hashlib.sha256()
    digest.update(image.encode("utf-8"))
    digest.update(b"\0")
    if external_image is not None:
        digest.update(external_image.encode("utf-8"))
    for name, ref in channels.items():
        digest.update(b"\0")
        digest.update(name.encode("utf-8"))
        digest.update(b"\0")
        digest.update(ref.encode("utf-8"))
    for name, ref in external_channels.items():
        digest.update(b"\0")
        digest.update(name.encode("utf-8"))
        digest.update(b"\0")
        digest.update(ref.encode("utf-8"))
    for platform, ref in platform_refs.items():
        digest.update(b"\0")
        digest.update(platform.encode("utf-8"))
        digest.update(b"\0")
        digest.update(ref.encode("utf-8"))
    return f"bertrand-manifest-{digest.hexdigest()[:24]}-{uuid.uuid4().hex[:8]}"
