"""Short-lived BuildKit client Jobs for publishing OCI images."""

from __future__ import annotations

import asyncio
import hashlib
import json
import uuid
from contextlib import asynccontextmanager
from dataclasses import dataclass
from typing import TYPE_CHECKING

from bertrand.env.git import (
    BERTRAND_ENV,
    BERTRAND_NAMESPACE,
)
from bertrand.env.kube.api.spec import (
    ContainerResourcesSpec,
    ContainerSpec,
    PodTemplateSpec,
    VolumeMountSpec,
    VolumeSpec,
)
from bertrand.env.kube.build.daemon import (
    BUILDKIT_IMAGE,
    BUILDKIT_POOL,
    BUILDKIT_SOCKET_ADDR,
    BUILDKIT_SOCKET_DIR,
    BUILDKIT_SOCKET_VOLUME,
)
from bertrand.env.kube.build.execution import run_observed_job
from bertrand.env.kube.build.refs import (
    DIGEST_RE,
    digest_ref,
    platform_output_ref,
)
from bertrand.env.kube.build.repository import IMAGES
from bertrand.env.kube.capability.base import Capability, CapabilityKind
from bertrand.env.kube.capability.device import (
    DRADeviceRequest,
    allocated_selector_script,
    create_resource_claim_templates,
    resource_claim_intents,
    select_device_claims,
)
from bertrand.env.kube.ceph.snapshot import prepared_repository_build_source
from bertrand.env.kube.configmap import ConfigMap
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


@dataclass(frozen=True)
class _PreparedBuildContext:
    path: str
    dockerfile_path: str
    volumes: tuple[VolumeSpec, ...]
    mounts: tuple[VolumeMountSpec, ...]


@dataclass(frozen=True)
class _BuildKitTarget:
    platform: str
    node_selector: Mapping[str, str]
    device_requests: tuple[DRADeviceRequest, ...]


@dataclass(frozen=True)
class _ProjectBuildExecutor:
    """Low-level BuildKit Job executor for one project image request.

    Parameters
    ----------
    spec : BuildKitBuildSpec
        Project image build request executed by short-lived BuildKit client Jobs.
    """

    spec: BuildKitBuildSpec

    @property
    def labels(self) -> dict[str, str]:
        """Return labels applied to the Kubernetes build Job.

        Returns
        -------
        dict[str, str]
            Labels applied to the Kubernetes build Job.
        """
        return {
            "app.kubernetes.io/name": "bertrand-build",
            "app.kubernetes.io/part-of": "bertrand",
            BERTRAND_ENV: "1",
            BUILD_JOB_LABEL: BUILD_JOB_LABEL_VALUE,
        }

    def _job_name(self) -> str:
        payload = self.spec.model_dump(mode="json")
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
        self,
        *,
        image: str | None = None,
        context_path: str = BUILD_JOB_CONTEXT_MOUNT,
        dockerfile_path: str = BUILD_JOB_CONTEXT_MOUNT,
        secret_paths: Mapping[str, str] | None = None,
        ssh_paths: Mapping[str, str] | None = None,
        metadata_file: str | None = None,
    ) -> list[str]:
        """Render the `buildctl` command arguments for this build.

        Parameters
        ----------
        image : str | None, optional
            Output image reference. If omitted, :attr:`image` is used.
        context_path : str, optional
            Container path used for the BuildKit build context.
        dockerfile_path : str, optional
            Container path used for the BuildKit Dockerfile source.
        secret_paths : Mapping[str, str] | None, optional
            BuildKit secret IDs mapped to mounted payload paths.
        ssh_paths : Mapping[str, str] | None, optional
            BuildKit SSH IDs mapped to mounted credential paths.
        metadata_file : str | None, optional
            Mounted path where BuildKit should write metadata JSON.

        Returns
        -------
        list[str]
            Argument vector beginning with `--addr`, suitable for a container whose
            command is `buildctl`.

        Raises
        ------
        ValueError
            If the output image reference is empty.
        """
        spec = self.spec
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

    async def publish_platforms(
        self,
        kube: Kube,
        *,
        build_name: str,
        timeout: float,
        job_observer: Callable[[str, Job], Awaitable[None]] | None = None,
    ) -> dict[str, str]:
        """Publish one native platform image per scheduled BuildKit target.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        build_name : str
            Durable `BuildKitBuild` request name used to label temporary snapshot
            source resources.
        timeout : float
            Maximum runtime budget in seconds. If infinite, wait indefinitely.
        job_observer : Callable[[str, Job], Awaitable[None]] | None, optional
            Callback invoked after each platform Job is created.

        Returns
        -------
        dict[str, str]
            Mapping of native OCI platform to published digest-pinned image ref,
            sorted by platform.

        Raises
        ------
        TimeoutError
            If staging, scheduling, Job creation, or Job completion exceeds
            `timeout`.
        ValueError
            If a platform output ref is invalid.
        """
        if timeout <= 0:
            msg = "BuildKit platform image build timeout must be non-negative"
            raise TimeoutError(msg)
        spec = self.spec
        worktree_id = spec.worktree_id
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout

        async with self._prepared_source(
            kube,
            build_name=build_name,
            timeout=deadline - loop.time(),
        ) as source:
            (
                capability_volumes,
                capability_mounts,
                secret_paths,
                ssh_paths,
            ) = await self._capability_mounts(
                kube,
                worktree_id=worktree_id,
                timeout=deadline - loop.time(),
            )
            targets = await self._schedule(
                kube,
                timeout=deadline - loop.time(),
            )
            run_id = uuid.uuid4().hex[:BUILD_PLATFORM_RUN_ID_BYTES]
            results: dict[str, str] = {}
            for target in sorted(targets, key=lambda item: item.platform):
                platform = target.platform
                image = platform_output_ref(spec.image, platform, run_id)
                image = image.strip()
                if not image:
                    msg = (
                        "BuildKit platform output ref is empty for platform "
                        f"{platform!r}"
                    )
                    raise ValueError(msg)
                digest = await self._publish_target(
                    kube,
                    target=target,
                    image=image,
                    source=source,
                    capability_volumes=tuple(capability_volumes),
                    capability_mounts=tuple(capability_mounts),
                    secret_paths=secret_paths,
                    ssh_paths=ssh_paths,
                    job_observer=(
                        None
                        if job_observer is None
                        else lambda job, platform=platform: job_observer(
                            platform,
                            job,
                        )
                    ),
                    timeout=deadline - loop.time(),
                )
                results[platform] = digest_ref(image, digest)
            return dict(sorted(results.items()))

    async def _publish_target(
        self,
        kube: Kube,
        *,
        target: _BuildKitTarget,
        image: str,
        source: _PreparedBuildContext,
        capability_volumes: tuple[VolumeSpec, ...],
        capability_mounts: tuple[VolumeMountSpec, ...],
        secret_paths: Mapping[str, str],
        ssh_paths: Mapping[str, str],
        timeout: float,
        job_observer: Callable[[Job], Awaitable[None]] | None = None,
    ) -> str:
        if timeout <= 0:
            msg = "BuildKit target image build timeout must be non-negative"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        job_name = self._job_name()
        dra_claims = resource_claim_intents(
            owner=job_name,
            requests=target.device_requests,
            container_name="buildctl",
        )
        created_claim_templates = await create_resource_claim_templates(
            kube,
            namespace=BERTRAND_NAMESPACE,
            intents=dra_claims,
            labels=self.labels,
            timeout=deadline - loop.time(),
        )
        try:
            job = await Job.create(
                kube,
                namespace=BERTRAND_NAMESPACE,
                name=job_name,
                labels=self.labels,
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
                                *self.buildctl_args(
                                    image=image,
                                    context_path=source.path,
                                    dockerfile_path=source.dockerfile_path,
                                    secret_paths=secret_paths,
                                    ssh_paths=ssh_paths,
                                    metadata_file=BUILD_JOB_METADATA_FILE,
                                ),
                            ],
                            volume_mounts=[
                                *source.mounts,
                                VolumeMountSpec(
                                    name=BUILD_JOB_METADATA_VOLUME,
                                    mount_path=BUILD_JOB_METADATA_MOUNT,
                                ),
                                *capability_mounts,
                                VolumeMountSpec(
                                    name=BUILDKIT_SOCKET_VOLUME,
                                    mount_path=BUILDKIT_SOCKET_DIR,
                                ),
                            ],
                            resources=(
                                ContainerResourcesSpec(
                                    claims=tuple(
                                        claim.claim_name for claim in dra_claims
                                    )
                                )
                                if dra_claims
                                else None
                            ),
                        )
                    ],
                    volumes=[
                        *source.volumes,
                        VolumeSpec.empty_dir(BUILD_JOB_METADATA_VOLUME),
                        *capability_volumes,
                        VolumeSpec.host_path(
                            BUILDKIT_SOCKET_VOLUME,
                            path=BUILDKIT_SOCKET_DIR,
                            host_path_type="Directory",
                        ),
                    ],
                    resource_claims=tuple(claim.pod_claim() for claim in dra_claims),
                    node_selector=target.node_selector,
                ),
                ttl_seconds_after_finished=BUILD_JOB_TTL_SECONDS,
                timeout=deadline - loop.time(),
            )
            logs = await run_observed_job(
                kube,
                job,
                timeout=deadline - loop.time(),
                failure_context=f"BuildKit image build for {image!r} failed",
                log_heading="Build pod logs",
                log_failure_label="BuildKit build pod logs",
                tail_lines=BUILD_JOB_LOG_TAIL_LINES,
                diagnostic_timeout=BUILD_JOB_DIAGNOSTIC_TIMEOUT_SECONDS,
                cleanup_timeout=BUILD_JOB_CLEANUP_TIMEOUT_SECONDS,
                include_log_headers=True,
                observer=job_observer,
            )
            return _parse_build_digest(logs)
        finally:
            for template in created_claim_templates:
                try:
                    await template.delete(
                        kube,
                        timeout=BUILD_JOB_CLEANUP_TIMEOUT_SECONDS,
                    )
                except (OSError, TimeoutError):
                    continue

    @asynccontextmanager
    async def _prepared_source(
        self,
        kube: Kube,
        *,
        build_name: str,
        timeout: float,
    ) -> AsyncIterator[_PreparedBuildContext]:
        if timeout <= 0:
            msg = "BuildKit source preparation timeout must be non-negative"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        config_name: str | None = None
        async with prepared_repository_build_source(
            kube,
            repo_id=self.spec.repo_id,
            build_name=build_name,
            timeout=deadline - loop.time(),
        ) as repo_source:
            config_name = _dockerfile_config_name()
            await ConfigMap.upsert(
                kube,
                namespace=BERTRAND_NAMESPACE,
                name=config_name,
                labels=self.labels,
                data={BUILD_JOB_DOCKERFILE_KEY: self.spec.dockerfile},
                timeout=deadline - loop.time(),
            )
            try:
                yield _PreparedBuildContext(
                    path=BUILD_JOB_CONTEXT_MOUNT,
                    dockerfile_path=BUILD_JOB_DOCKERFILE_MOUNT,
                    volumes=(
                        VolumeSpec.pvc(
                            BUILD_JOB_CONTEXT_VOLUME,
                            claim_name=repo_source,
                        ),
                        VolumeSpec.config_map(
                            BUILD_JOB_DOCKERFILE_VOLUME,
                            config_map_name=config_name,
                        ),
                    ),
                    mounts=(
                        VolumeMountSpec(
                            name=BUILD_JOB_CONTEXT_VOLUME,
                            mount_path=BUILD_JOB_CONTEXT_MOUNT,
                            read_only=True,
                            sub_path=_worktree_sub_path(self.spec.worktree),
                        ),
                        VolumeMountSpec(
                            name=BUILD_JOB_DOCKERFILE_VOLUME,
                            mount_path=BUILD_JOB_DOCKERFILE_MOUNT,
                            read_only=True,
                        ),
                    ),
                )
            finally:
                await _delete_config_map(
                    kube,
                    name=config_name,
                    timeout=BUILD_JOB_CLEANUP_TIMEOUT_SECONDS,
                )

    async def _capability_mounts(
        self,
        kube: Kube,
        *,
        worktree_id: str,
        timeout: float,
    ) -> tuple[
        list[VolumeSpec],
        list[VolumeMountSpec],
        dict[str, str],
        dict[str, str],
    ]:
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        volumes: list[VolumeSpec] = []
        mounts: list[VolumeMountSpec] = []
        secret_paths: dict[str, str] = {}
        ssh_paths: dict[str, str] = {}
        groups: tuple[
            tuple[CapabilityKind, Mapping[KubeName, bool], str, dict[str, str]],
            ...,
        ] = (
            ("secret", self.spec.secrets, BUILD_JOB_SECRET_MOUNT, secret_paths),
            ("ssh", self.spec.ssh, BUILD_JOB_SSH_MOUNT, ssh_paths),
        )

        for kind, requests, mount_root, paths in groups:
            for capability_id, required in sorted(requests.items()):
                capability = await Capability.resolve(
                    kube,
                    kind=kind,
                    capability_id=capability_id,
                    worktree_id=worktree_id,
                    repo_id=self.spec.repo_id,
                    required=required,
                    timeout=deadline - loop.time(),
                )
                if capability is None:
                    continue
                volume_name = _capability_volume_name(kind, capability_id)
                mount_path = f"{mount_root}/{capability_id}"
                volumes.append(
                    VolumeSpec.secret(
                        volume_name,
                        secret_name=capability.ref.name,
                        default_mode=0o400,
                    )
                )
                mounts.append(
                    VolumeMountSpec(
                        name=volume_name,
                        mount_path=mount_path,
                        read_only=True,
                    )
                )
                paths[capability_id] = f"{mount_path}/{CAPABILITY_VALUE_KEY}"

        return volumes, mounts, secret_paths, ssh_paths

    async def _schedule(
        self,
        kube: Kube,
        *,
        timeout: float,
    ) -> tuple[_BuildKitTarget, ...]:
        """Schedule this build onto native BuildKit builders.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        tuple[_BuildKitTarget, ...]
            Platform assignments whose device requests are expressed through DRA.

        Raises
        ------
        TimeoutError
            If ``timeout`` is non-positive.
        """
        if timeout <= 0:
            msg = "BuildKit build scheduling timeout must be non-negative"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        config_hash = await IMAGES.current_buildkit_config_hash(
            kube,
            timeout=deadline - loop.time(),
        )
        groups = await BUILDKIT_POOL._ready_platform_nodes(
            kube,
            timeout=deadline - loop.time(),
            config_hash=config_hash,
        )

        targets: list[_BuildKitTarget] = []
        for platform, node_names in groups.items():
            device_requests = await select_device_claims(
                kube,
                requests=self.spec.devices,
                node_names=node_names,
                timeout=deadline - loop.time(),
            )
            targets.append(
                _BuildKitTarget(
                    platform=platform,
                    node_selector=_platform_node_selector(platform),
                    device_requests=device_requests,
                )
            )
        return tuple(targets)


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
    timeout: float,
) -> None:
    if timeout <= 0:
        return
    try:
        config = await ConfigMap.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=name,
            timeout=timeout,
        )
        if config is not None:
            await config.delete(kube, timeout=timeout)
    except (OSError, TimeoutError):
        return
