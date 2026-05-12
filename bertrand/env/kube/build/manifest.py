"""OCI manifest assembly for Bertrand's in-cluster build runtime."""

from __future__ import annotations

import asyncio
import hashlib
import re
import shlex
from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING

from bertrand.env.config.core import _check_kube_name, _check_uuid
from bertrand.env.git import BERTRAND_ENV, BERTRAND_NAMESPACE
from bertrand.env.kube.api import ContainerSpec, EnvVarSpec, Kube
from bertrand.env.kube.build.repository import IMAGES, ImageRepository
from bertrand.env.kube.capability.base import Capability
from bertrand.env.kube.job import Job

if TYPE_CHECKING:
    from collections.abc import Mapping

    from bertrand.env.config.core import KubeName, OCIImageRef
    from bertrand.env.kube.build.job import BuildKitPlatformResult

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
_MANIFEST_DIGEST_RE = re.compile(r"^sha256:[0-9a-f]{64}$")
_MANIFEST_DIGEST_REF_RE = re.compile(r"^.+@sha256:[0-9a-f]{64}$")


@dataclass(frozen=True)
class ImageManifestResult:
    """Result for one assembled multi-platform image manifest.

    Parameters
    ----------
    image : OCIImageRef
        Canonical internal mutable image reference for the assembled manifest.
    digest : str
        OCI index digest reported by the internal registry.
    digest_ref : OCIImageRef
        Canonical internal digest-pinned image reference.
    platforms : tuple[str, ...]
        Platforms included in the assembled manifest.
    platform_images : Mapping[str, OCIImageRef]
        Read-only mapping from platform to platform-specific digest reference.
    external_image : OCIImageRef | None
        External mutable image reference copied from the internal manifest, if any.
    external_digest_ref : OCIImageRef | None
        External digest-pinned image reference reported after copy, if any.
    """

    image: OCIImageRef
    digest: str
    digest_ref: OCIImageRef
    platforms: tuple[str, ...]
    platform_images: Mapping[str, OCIImageRef]
    external_image: OCIImageRef | None = None
    external_digest_ref: OCIImageRef | None = None


async def publish_image_manifest(
    kube: Kube,
    *,
    image: str,
    platform_results: tuple[BuildKitPlatformResult, ...],
    timeout: float,
    external_image: str | None = None,
    auth_id: KubeName | None = None,
    env_id: str | None = None,
    repository: ImageRepository = IMAGES,
) -> ImageManifestResult:
    """Assemble and optionally copy one multi-platform OCI image manifest.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    image : str
        Canonical internal mutable image reference to publish.
    platform_results : tuple[BuildKitPlatformResult, ...]
        Platform image digests returned by native BuildKit executions.
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

    Returns
    -------
    ImageManifestResult
        Internal and optional external digest references for the assembled manifest.

    Raises
    ------
    TimeoutError
        If the assembly Job cannot complete before `timeout`.
    OSError
        If the assembly Job fails or does not emit digest sentinels.
    ValueError
        If refs, platforms, or auth inputs are malformed.
    """
    if timeout <= 0:
        msg = "image manifest publish timeout must be non-negative"
        raise TimeoutError(msg)
    image = _validate_tagged_ref(image, label="internal manifest target")
    internal_service_ref = repository.service_ref(image)
    if repository.pull_ref(internal_service_ref) != image:
        msg = f"internal manifest target is not canonical: {image!r}"
        raise ValueError(msg)
    external_image = (
        _validate_tagged_ref(external_image, label="external manifest target")
        if external_image is not None
        else None
    )
    if auth_id is not None and env_id is None:
        msg = "external registry auth capability requires an environment identity"
        raise ValueError(msg)
    if auth_id is not None:
        auth_id = _check_kube_name(auth_id)
    if env_id is not None:
        env_id = _check_uuid(env_id)

    platform_refs = _validate_platform_results(platform_results, repository)
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
        repository=repository,
    )
    job = await Job.create(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=_manifest_job_name(image, platform_refs, external_image),
        labels={
            BERTRAND_ENV: "1",
            MANIFEST_JOB_LABEL: MANIFEST_JOB_LABEL_VALUE,
        },
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
        volumes=(),
        ttl_seconds_after_finished=MANIFEST_JOB_TTL_SECONDS,
        timeout=deadline - loop.time(),
    )
    try:
        await job.wait_complete(kube, timeout=deadline - loop.time())
        logs = await _manifest_job_logs(
            kube,
            job,
            timeout=deadline - loop.time(),
        )
    except (OSError, TimeoutError) as err:
        logs = await _manifest_job_logs(
            kube,
            job,
            timeout=MANIFEST_JOB_DIAGNOSTIC_TIMEOUT_SECONDS,
        )
        await _delete_manifest_job(
            kube,
            job,
            timeout=MANIFEST_JOB_CLEANUP_TIMEOUT_SECONDS,
        )
        msg = _manifest_failure_message(image, err, logs)
        if isinstance(err, TimeoutError):
            raise TimeoutError(msg) from err
        raise OSError(msg) from err

    internal_digest_ref = repository.pull_ref(
        _parse_sentinel(logs, MANIFEST_INTERNAL_SENTINEL)
    )
    digest = _digest_from_ref(internal_digest_ref)
    external_digest_ref = (
        _parse_sentinel(logs, MANIFEST_EXTERNAL_SENTINEL)
        if external_image is not None
        else None
    )
    return ImageManifestResult(
        image=image,
        digest=digest,
        digest_ref=internal_digest_ref,
        platforms=tuple(platform_refs),
        platform_images=MappingProxyType(dict(platform_refs)),
        external_image=external_image,
        external_digest_ref=external_digest_ref,
    )


def _validate_platform_results(
    platform_results: tuple[BuildKitPlatformResult, ...],
    repository: ImageRepository,
) -> dict[str, str]:
    if not platform_results:
        msg = "image manifest assembly requires at least one platform result"
        raise ValueError(msg)
    platform_refs: dict[str, str] = {}
    for result in platform_results:
        platform = result.platform.strip()
        if not platform:
            msg = "image manifest platform cannot be empty"
            raise ValueError(msg)
        if platform in platform_refs:
            msg = f"duplicate image manifest platform: {platform!r}"
            raise ValueError(msg)
        digest_ref = result.digest_ref.strip()
        if not _MANIFEST_DIGEST_REF_RE.fullmatch(digest_ref):
            msg = f"platform image ref must include a sha256 digest: {digest_ref!r}"
            raise ValueError(msg)
        repository.service_ref(digest_ref)
        platform_refs[platform] = digest_ref
    return dict(sorted(platform_refs.items()))


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
    payload = capability.payload
    del payload
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
    internal_repo = _tagged_repository(image)
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
        external_repo = _tagged_repository(external_image)
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


async def _manifest_job_logs(kube: Kube, job: Job, *, timeout: float) -> str:
    try:
        pods = await job.pods(kube, timeout=timeout)
    except (OSError, TimeoutError) as err:
        return f"<unable to list manifest Job pods: {err}>"
    chunks: list[str] = []
    for pod in pods:
        try:
            chunks.append(
                await pod.logs(
                    kube,
                    timeout=timeout,
                    tail_lines=MANIFEST_JOB_LOG_TAIL_LINES,
                )
            )
        except (OSError, TimeoutError) as err:
            chunks.append(f"<unable to read pod logs: {err}>")
    return "\n".join(chunk for chunk in chunks if chunk)


async def _delete_manifest_job(kube: Kube, job: Job, *, timeout: float) -> None:
    try:
        await job.delete(kube, timeout=timeout, propagation_policy="Foreground")
    except (OSError, TimeoutError):
        return
    try:
        await job.wait_deleted(kube, timeout=timeout)
    except (OSError, TimeoutError):
        return


def _manifest_failure_message(image: str, err: BaseException, logs: str) -> str:
    msg = f"image manifest assembly failed for {image!r}: {err}"
    if logs.strip():
        msg = f"{msg}\n\nmanifest Job logs:\n{logs.strip()}"
    return msg


def _parse_sentinel(logs: str, prefix: str) -> str:
    for line in reversed(logs.splitlines()):
        if line.startswith(prefix):
            value = line[len(prefix) :].strip()
            if not _MANIFEST_DIGEST_REF_RE.fullmatch(value):
                msg = f"manifest Job emitted malformed digest ref: {value!r}"
                raise OSError(msg)
            return value
    msg = f"manifest Job did not emit required sentinel {prefix.rstrip('=')!r}"
    raise OSError(msg)


def _digest_from_ref(ref: str) -> str:
    digest = ref.rpartition("@")[2]
    if not _MANIFEST_DIGEST_RE.fullmatch(digest):
        msg = f"manifest digest ref does not contain a sha256 digest: {ref!r}"
        raise OSError(msg)
    return digest


def _validate_tagged_ref(ref: str | None, *, label: str) -> str:
    value = (ref or "").strip()
    if not value:
        msg = f"{label} cannot be empty"
        raise ValueError(msg)
    _tagged_repository(value)
    if "@" in value:
        msg = f"{label} must be a tagged mutable ref, not a digest ref: {ref!r}"
        raise ValueError(msg)
    return value


def _tagged_repository(ref: str) -> str:
    slash = ref.rfind("/")
    colon = ref.rfind(":")
    if colon <= slash:
        msg = f"image reference must include a tag: {ref!r}"
        raise ValueError(msg)
    repository = ref[:colon]
    tag = ref[colon + 1 :]
    if not repository or not tag:
        msg = f"image reference must include a non-empty repository and tag: {ref!r}"
        raise ValueError(msg)
    return repository


def _manifest_job_name(
    image: str,
    platform_refs: Mapping[str, str],
    external_image: str | None,
) -> str:
    digest = hashlib.sha256()
    digest.update(image.encode("utf-8"))
    digest.update(b"\0")
    if external_image is not None:
        digest.update(external_image.encode("utf-8"))
    for platform, ref in platform_refs.items():
        digest.update(b"\0")
        digest.update(platform.encode("utf-8"))
        digest.update(b"\0")
        digest.update(ref.encode("utf-8"))
    nonce = hashlib.sha256(str(asyncio.get_running_loop().time()).encode()).hexdigest()
    return f"bertrand-manifest-{digest.hexdigest()[:24]}-{nonce[:8]}"
