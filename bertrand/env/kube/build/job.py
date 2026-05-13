"""Short-lived BuildKit client Jobs for publishing OCI images."""

from __future__ import annotations

import asyncio
import hashlib
import json
import uuid
from contextlib import asynccontextmanager
from dataclasses import dataclass, field
from pathlib import Path
from typing import TYPE_CHECKING, Literal

from bertrand.env.config.core import _check_kube_name, _check_uuid
from bertrand.env.git import (
    BERTRAND_ENV,
    BERTRAND_NAMESPACE,
)
from bertrand.env.kube.api import (
    ContainerSpec,
    Kube,
    PodTemplateSpec,
    VolumeMountSpec,
    VolumeSpec,
)
from bertrand.env.kube.build.daemon import (
    BUILDKIT_IMAGE,
    BUILDKIT_POOL,
    _BuildKitBuilder,
)
from bertrand.env.kube.build.execution import run_observed_job
from bertrand.env.kube.build.refs import (
    DIGEST_RE,
    digest_ref,
    platform_output_ref,
    replace_tag,
)
from bertrand.env.kube.build.repository import IMAGES
from bertrand.env.kube.capability.base import Capability, CapabilityKind
from bertrand.env.kube.ceph.volume import RepoVolume
from bertrand.env.kube.configmap import ConfigMap
from bertrand.env.kube.job import Job

if TYPE_CHECKING:
    from collections.abc import AsyncIterator, Awaitable, Callable, Mapping

    from bertrand.env.config.core import KubeName, NonEmpty, OCIImageRef, Trimmed

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
BUILD_CACHE_TAG = "buildcache"
BUILD_CACHE_MODE = "max"
BUILD_PROGRESS = "plain"
BUILD_CONTEXT_PREFIX = "bertrand-project-image"
CAPABILITY_VALUE_KEY = "value"
BUILD_PLATFORM_RUN_ID_BYTES = 12

type _BuildKitTarget = tuple[_BuildKitBuilder, tuple[str, ...]]
type _BuildNetworkMode = Literal["default", "none", "host"]


@dataclass(frozen=True)
class _PreparedBuildContext:
    path: str
    dockerfile_path: str
    volumes: tuple[VolumeSpec, ...]
    mounts: tuple[VolumeMountSpec, ...]
    cleanup_config_map: str | None


@dataclass(frozen=True)
class _ProjectBuildExecutor:
    """Low-level BuildKit Job executor for one project image request.

    Attributes
    ----------
    image : OCIImageRef
        Fully-qualified image reference to build and push.
    dockerfile : NonEmpty[Trimmed]
        Containerfile text to stage as the build frontend input.
    repo_id : str
        Stable repository UUID used to locate the managed repository PVC.
    worktree : str
        Repository-volume subpath for the worktree. ``"."`` targets the PVC root.
    build_args : dict[str, str]
        Dockerfile build arguments passed to BuildKit.
    image_labels : dict[str, str]
        Image labels applied by the Dockerfile frontend.
    target : str | None
        Optional target stage in a multi-stage Containerfile.
    network : {'default', 'none', 'host'}
        BuildKit network mode applied to build-time `RUN` instructions.
    secrets : Mapping[KubeName, bool]
        Secret capability IDs to expose to the build. Values indicate whether the
        capability is required.
    ssh : Mapping[KubeName, bool]
        SSH capability IDs to expose to the build. Values indicate whether the
        capability is required.
    devices : Mapping[KubeName, bool]
        CDI device capability IDs to allow during the build. Values indicate
        whether the capability is required.
    """

    image: OCIImageRef
    dockerfile: NonEmpty[Trimmed]
    repo_id: str
    worktree: str = "."
    build_args: dict[str, str] = field(default_factory=dict)
    image_labels: dict[str, str] = field(default_factory=dict)
    target: str | None = None
    network: _BuildNetworkMode = "default"
    secrets: Mapping[KubeName, bool] = field(default_factory=dict)
    ssh: Mapping[KubeName, bool] = field(default_factory=dict)
    devices: Mapping[KubeName, bool] = field(default_factory=dict)

    def __post_init__(self) -> None:
        """Validate immutable build contract fields.

        Raises
        ------
        ValueError
            If any build contract field is empty or structurally invalid.
        """
        if not self.image.strip():
            msg = "BuildKit image reference cannot be empty"
            raise ValueError(msg)
        if not self.dockerfile.strip():
            msg = "BuildKit image Containerfile cannot be empty"
            raise ValueError(msg)
        if self.target is not None and not self.target.strip():
            msg = "BuildKit target stage cannot be empty"
            raise ValueError(msg)
        if self.network not in ("default", "none", "host"):
            msg = f"unsupported BuildKit network mode: {self.network!r}"
            raise ValueError(msg)
        object.__setattr__(self, "repo_id", _check_uuid(self.repo_id))
        object.__setattr__(self, "worktree", _normalize_worktree(self.worktree))
        object.__setattr__(
            self,
            "secrets",
            _normalize_capability_requests(self.secrets, kind="secret"),
        )
        object.__setattr__(
            self,
            "ssh",
            _normalize_capability_requests(self.ssh, kind="ssh"),
        )
        object.__setattr__(
            self,
            "devices",
            _normalize_capability_requests(self.devices, kind="device"),
        )
        _default_cache_ref(self.image)

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

    @property
    def job_name(self) -> str:
        """Return a unique Kubernetes Job name for this build.

        Returns
        -------
        str
            Unique Kubernetes Job name for this build execution.
        """
        payload = {
            "image": self.image,
            "dockerfile": self.dockerfile,
            "repo_id": self.repo_id,
            "worktree": self.worktree,
            "build_args": self.build_args,
            "image_labels": self.image_labels,
            "target": self.target,
            "network": self.network,
            "secrets": dict(sorted(self.secrets.items())),
            "ssh": dict(sorted(self.ssh.items())),
            "devices": dict(sorted(self.devices.items())),
        }
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
        buildkit_addr: str,
        *,
        image: str | None = None,
        context_path: str = BUILD_JOB_CONTEXT_MOUNT,
        dockerfile_path: str = BUILD_JOB_CONTEXT_MOUNT,
        secret_paths: Mapping[str, str] | None = None,
        ssh_paths: Mapping[str, str] | None = None,
        device_selectors: tuple[str, ...] = (),
        metadata_file: str | None = None,
    ) -> list[str]:
        """Render the `buildctl` command arguments for this build.

        Parameters
        ----------
        buildkit_addr : str
            BuildKit daemon address used by the client Job.
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
        device_selectors : tuple[str, ...], optional
            CDI selectors allowed for `RUN --device` instructions.
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
        image = self.image if image is None else image.strip()
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
            buildkit_addr,
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
        if self.target is not None:
            args.extend(["--opt", f"target={self.target}"])
        if self.network != "default":
            args.extend(["--opt", f"force-network-mode={self.network}"])
        for key, value in sorted(self.build_args.items()):
            args.extend(["--opt", f"build-arg:{key}={value}"])
        for key, value in sorted(self.image_labels.items()):
            args.extend(["--opt", f"label:{key}={value}"])
        for capability_id, path in sorted((secret_paths or {}).items()):
            args.extend(["--secret", f"id={capability_id},src={path}"])
        for capability_id, path in sorted((ssh_paths or {}).items()):
            args.extend(["--ssh", f"{capability_id}={path}"])
        for selector in sorted(set(device_selectors)):
            args.extend(["--allow", f"device={selector}"])
        if self.network == "host":
            args.extend(["--allow", "network.host"])
        cache_ref = _default_cache_ref(self.image)
        args.extend(["--import-cache", f"type=registry,ref={cache_ref}"])
        args.extend(
            [
                "--export-cache",
                f"type=registry,ref={cache_ref},mode={BUILD_CACHE_MODE}",
            ]
        )
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
        env_id: str | None,
        timeout: float,
        job_observer: Callable[[str, Job], Awaitable[None]] | None = None,
    ) -> dict[str, str]:
        """Publish one native platform image per scheduled BuildKit target.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        env_id : str | None
            Environment UUID used to resolve secret, SSH, and device capabilities.
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
            If capability inputs are present without `env_id`, or if a platform
            output ref is invalid.
        """
        if timeout <= 0:
            msg = "BuildKit platform image build timeout must be non-negative"
            raise TimeoutError(msg)
        if env_id is None and (self.secrets or self.ssh or self.devices):
            msg = "BuildKit capability inputs require an environment identity"
            raise ValueError(msg)
        if env_id is not None:
            env_id = _check_uuid(env_id)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout

        async with self._prepared_source(
            kube,
            timeout=deadline - loop.time(),
        ) as source:
            (
                capability_volumes,
                capability_mounts,
                secret_paths,
                ssh_paths,
            ) = await self._capability_mounts(
                kube,
                env_id=env_id,
                timeout=deadline - loop.time(),
            )
            targets = await self._schedule(
                kube,
                env_id=env_id,
                timeout=deadline - loop.time(),
            )
            run_id = uuid.uuid4().hex[:BUILD_PLATFORM_RUN_ID_BYTES]
            results: dict[str, str] = {}
            for builder, device_selectors in sorted(
                targets,
                key=lambda item: item[0].platform,
            ):
                platform = builder.platform
                image = platform_output_ref(self.image, platform, run_id)
                image = image.strip()
                if not image:
                    msg = (
                        "BuildKit platform output ref is empty for platform "
                        f"{platform!r}"
                    )
                    raise ValueError(msg)
                digest = await self._publish_target(
                    kube,
                    builder=builder,
                    device_selectors=device_selectors,
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
        builder: _BuildKitBuilder,
        device_selectors: tuple[str, ...],
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
        job = await Job.create(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=self.job_name,
            labels=self.labels,
            pod_template=PodTemplateSpec(
                containers=[
                    ContainerSpec(
                        name="buildctl",
                        image=BUILDKIT_IMAGE,
                        image_pull_policy="IfNotPresent",
                        command=["/bin/sh", "-ec"],
                        args=[
                            _build_job_script(),
                            "buildctl",
                            *self.buildctl_args(
                                builder.addr,
                                image=image,
                                context_path=source.path,
                                dockerfile_path=source.dockerfile_path,
                                secret_paths=secret_paths,
                                ssh_paths=ssh_paths,
                                device_selectors=device_selectors,
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
                        ],
                    )
                ],
                volumes=[
                    *source.volumes,
                    VolumeSpec.empty_dir(BUILD_JOB_METADATA_VOLUME),
                    *capability_volumes,
                ],
                node_name=builder.node,
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

    @asynccontextmanager
    async def _prepared_source(
        self,
        kube: Kube,
        *,
        timeout: float,
    ) -> AsyncIterator[_PreparedBuildContext]:
        if timeout <= 0:
            msg = "BuildKit source preparation timeout must be non-negative"
            raise TimeoutError(msg)
        source = await self._prepare_pvc_source(kube, timeout=timeout)
        try:
            yield source
        finally:
            if source.cleanup_config_map is not None:
                await _delete_config_map(
                    kube,
                    name=source.cleanup_config_map,
                    timeout=BUILD_JOB_CLEANUP_TIMEOUT_SECONDS,
                )

    async def _prepare_pvc_source(
        self,
        kube: Kube,
        *,
        timeout: float,
    ) -> _PreparedBuildContext:
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        await _assert_repo_volume(
            kube,
            repo_id=self.repo_id,
            claim_name=RepoVolume.claim_name(self.repo_id),
            timeout=deadline - loop.time(),
        )
        config_name = _dockerfile_config_name()
        await ConfigMap.upsert(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=config_name,
            labels=self.labels,
            data={BUILD_JOB_DOCKERFILE_KEY: self.dockerfile},
            timeout=deadline - loop.time(),
        )
        return _PreparedBuildContext(
            path=BUILD_JOB_CONTEXT_MOUNT,
            dockerfile_path=BUILD_JOB_DOCKERFILE_MOUNT,
            volumes=(
                VolumeSpec.pvc(
                    BUILD_JOB_CONTEXT_VOLUME,
                    claim_name=RepoVolume.claim_name(self.repo_id),
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
                    sub_path=_worktree_sub_path(self.worktree),
                ),
                VolumeMountSpec(
                    name=BUILD_JOB_DOCKERFILE_VOLUME,
                    mount_path=BUILD_JOB_DOCKERFILE_MOUNT,
                    read_only=True,
                ),
            ),
            cleanup_config_map=config_name,
        )

    async def _capability_mounts(
        self,
        kube: Kube,
        *,
        env_id: str | None,
        timeout: float,
    ) -> tuple[
        list[VolumeSpec],
        list[VolumeMountSpec],
        dict[str, str],
        dict[str, str],
    ]:
        if env_id is None:
            return [], [], {}, {}
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
            ("secret", self.secrets, BUILD_JOB_SECRET_MOUNT, secret_paths),
            ("ssh", self.ssh, BUILD_JOB_SSH_MOUNT, ssh_paths),
        )

        for kind, requests, mount_root, paths in groups:
            for capability_id, required in sorted(requests.items()):
                capability = await Capability.resolve(
                    kube,
                    kind=kind,
                    capability_id=capability_id,
                    env_id=env_id,
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
        env_id: str | None,
        timeout: float,
    ) -> tuple[_BuildKitTarget, ...]:
        """Schedule this build onto native BuildKit builders.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        env_id : str | None
            Environment UUID used to resolve device capabilities.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        tuple[_BuildKitTarget, ...]
            Build-aware platform assignments with resolved device selectors.

        Raises
        ------
        TimeoutError
            If ``timeout`` is non-positive.
        ValueError
            If device inputs are present without ``env_id``.
        OSError
            If no scheduled builder can satisfy required device capabilities.
        """
        if timeout <= 0:
            msg = "BuildKit build scheduling timeout must be non-negative"
            raise TimeoutError(msg)
        if self.devices and env_id is None:
            msg = "BuildKit device inputs require an environment identity"
            raise ValueError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        config_hash = await IMAGES.current_buildkit_config_hash(
            kube,
            timeout=deadline - loop.time(),
        )
        groups = await BUILDKIT_POOL._builder_candidates(
            kube,
            timeout=deadline - loop.time(),
            config_hash=config_hash,
        )

        targets: list[_BuildKitTarget] = []
        for platform, candidates in groups.items():
            if not self.devices:
                targets.append((candidates[0], ()))
                continue
            errors: list[str] = []
            selected: _BuildKitTarget | None = None
            for candidate in candidates:
                try:
                    selectors = await self._device_selectors(
                        kube,
                        env_id=env_id,
                        node=candidate.node,
                        timeout=deadline - loop.time(),
                    )
                except OSError as err:
                    errors.append(f"{candidate.node}: {err}")
                    continue
                selected = (candidate, selectors)
                break
            if selected is None:
                detail = "; ".join(errors) if errors else "no candidate builders"
                msg = (
                    f"no ready BuildKit builder for platform {platform!r} "
                    f"can satisfy requested device capabilities: {detail}"
                )
                raise OSError(msg)
            targets.append(selected)
        return tuple(targets)

    async def _device_selectors(
        self,
        kube: Kube,
        *,
        env_id: str | None,
        node: str | None,
        timeout: float,
    ) -> tuple[str, ...]:
        if not self.devices:
            return ()
        if timeout <= 0:
            msg = "BuildKit device capability resolution timeout must be non-negative"
            raise TimeoutError(msg)
        if env_id is None:
            msg = "BuildKit device inputs require an environment identity"
            raise ValueError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        selectors: list[str] = []
        for capability_id, required in sorted(self.devices.items()):
            capability = await Capability.resolve_device(
                kube,
                capability_id=capability_id,
                env_id=env_id,
                node=node,
                required=required,
                timeout=deadline - loop.time(),
            )
            if capability is not None:
                selectors.append(capability.selector)
        return tuple(selectors)


def _normalize_capability_requests(
    requests: Mapping[KubeName, bool],
    *,
    kind: str,
) -> dict[KubeName, bool]:
    normalized: dict[KubeName, bool] = {}
    for capability_id, required in requests.items():
        checked = _check_kube_name(capability_id)
        if checked in normalized:
            msg = f"duplicate BuildKit {kind} capability ID: {checked!r}"
            raise ValueError(msg)
        if not isinstance(required, bool):
            msg = f"BuildKit {kind} capability {checked!r} required flag must be bool"
            raise TypeError(msg)
        normalized[checked] = required
    return normalized


def _default_cache_ref(image: str) -> str:
    try:
        return replace_tag(image, BUILD_CACHE_TAG, label="BuildKit image reference")
    except ValueError as err:
        msg = f"invalid BuildKit image reference {image!r}; cannot derive cache ref"
        raise ValueError(msg) from err


def _capability_volume_name(kind: CapabilityKind, capability_id: str) -> str:
    payload = f"{kind}:{capability_id}".encode()
    digest = hashlib.sha256(payload).hexdigest()[:16]
    return f"build-{kind}-{digest}"


def _normalize_worktree(worktree: str) -> str:
    value = worktree.strip().strip("/")
    if not value or value == ".":
        return "."
    path = Path(value)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        msg = f"BuildKit PVC worktree must be a relative subpath: {worktree!r}"
        raise ValueError(msg)
    return path.as_posix()


def _build_job_script() -> str:
    return "\n".join(
        (
            "set -eu",
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


async def _assert_repo_volume(
    kube: Kube,
    *,
    repo_id: str,
    claim_name: str,
    timeout: float,
) -> None:
    volumes = await RepoVolume.list(kube, repo_id, timeout=timeout)
    if len(volumes) != 1:
        msg = (
            f"project image build requires one managed repository PVC for "
            f"{repo_id!r}, found {len(volumes)}"
        )
        raise OSError(msg)
    if volumes[0].pvc.name != claim_name:
        msg = (
            f"managed repository PVC for {repo_id!r} has unexpected name "
            f"{volumes[0].pvc.name!r}, expected {claim_name!r}"
        )
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
