"""Short-lived BuildKit client Jobs for publishing OCI images."""

from __future__ import annotations

import asyncio
import hashlib
import json
import shutil
import tempfile
import uuid
from collections.abc import AsyncIterator, Iterable, Mapping
from contextlib import asynccontextmanager
from dataclasses import dataclass, field
from pathlib import Path
from types import MappingProxyType
from typing import TYPE_CHECKING, Literal, cast

from bertrand.env.config.core import _check_kube_name, _check_uuid
from bertrand.env.git import (
    BERTRAND_ENV,
    BERTRAND_NAMESPACE,
    INFINITY,
    atomic_write_text,
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
    BuildKitBuilder,
    BuildKitPool,
)
from bertrand.env.kube.build.execution import wait_job_complete
from bertrand.env.kube.build.refs import (
    digest_ref,
    platform_output_ref,
    platform_token,
    replace_tag,
)
from bertrand.env.kube.capability.base import Capability, CapabilityKind
from bertrand.env.kube.job import Job
from bertrand.env.kube.node import Node

if TYPE_CHECKING:
    from collections.abc import Callable

    from bertrand.env.config.core import KubeName, NonEmpty, OCIImageRef, Trimmed

BUILD_JOB_CONTEXT_MOUNT = "/workspace"
BUILD_JOB_CONTEXT_VOLUME = "context"
BUILD_JOB_METADATA_FILE = "/run/bertrand/buildkit/metadata/metadata.json"
BUILD_JOB_METADATA_MOUNT = "/run/bertrand/buildkit/metadata"
BUILD_JOB_METADATA_VOLUME = "metadata"
BUILD_JOB_SECRET_MOUNT = "/run/bertrand/buildkit/secrets"
BUILD_JOB_SSH_MOUNT = "/run/bertrand/buildkit/ssh"
BUILD_JOB_LABEL = "bertrand.dev/build-job"
BUILD_JOB_LABEL_VALUE = "v1"
BUILD_JOB_TTL_SECONDS = 3600
BUILD_JOB_LOG_TAIL_LINES = 120
BUILD_JOB_DIAGNOSTIC_TIMEOUT_SECONDS = 10.0
BUILD_JOB_CLEANUP_TIMEOUT_SECONDS = 10.0
BUILD_CACHE_TAG = "buildcache"
CAPABILITY_VALUE_KEY = "value"
BUILD_PLATFORM_RUN_ID_BYTES = 12


@dataclass(frozen=True)
class BuildKitImageResult:
    """Result metadata for one published BuildKit image.

    Parameters
    ----------
    image : OCIImageRef
        Fully-qualified image reference that was pushed.
    digest : str
        Pushed image digest reported by BuildKit.
    config_digest : str
        Image config digest reported by BuildKit, or an empty string if unavailable.
    descriptor : Mapping[str, object]
        Read-only image descriptor metadata reported by BuildKit.
    metadata : Mapping[str, object]
        Read-only raw metadata loaded from BuildKit's metadata file.
    """

    image: OCIImageRef
    digest: str
    config_digest: str
    descriptor: Mapping[str, object]
    metadata: Mapping[str, object]


@dataclass(frozen=True)
class BuildKitJobTarget:
    """BuildKit builder target for one scheduled image build.

    Parameters
    ----------
    platform : str
        Native OCI platform assigned to the build target.
    builder : BuildKitBuilder
        Ready BuildKit builder selected for the target.
    device_selectors : tuple[str, ...]
        CDI selectors allowed for this target.
    """

    platform: str
    builder: BuildKitBuilder
    device_selectors: tuple[str, ...]


@dataclass(frozen=True)
class BuildKitPlatformResult:
    """Result metadata for one native platform build.

    Parameters
    ----------
    platform : str
        Native OCI platform assigned to this build result.
    builder : BuildKitBuilder
        BuildKit builder that executed this platform build.
    image : OCIImageRef
        Temporary platform image reference pushed by BuildKit.
    digest : str
        Platform image digest reported by BuildKit.
    digest_ref : str
        Immutable digest-pinned reference for the platform image.
    build : BuildKitImageResult
        Raw BuildKit result metadata for this platform build.
    """

    platform: str
    builder: BuildKitBuilder
    image: OCIImageRef
    digest: str
    digest_ref: str
    build: BuildKitImageResult


@dataclass(frozen=True)
class _BuildKitExecutionWorkspace:
    context: Path
    metadata: Path
    local_node: Node
    capability_volumes: tuple[VolumeSpec, ...]
    capability_mounts: tuple[VolumeMountSpec, ...]
    secret_paths: Mapping[str, str]
    ssh_paths: Mapping[str, str]


@dataclass(frozen=True)
class BuildKitImageBuild:
    """Declarative BuildKit Job contract for one published image.

    Attributes
    ----------
    image : OCIImageRef
        Fully-qualified image reference to build and push.
    dockerfile : NonEmpty[Trimmed]
        Containerfile text to stage as the build frontend input.
    context_copies : tuple[tuple[Path, Path], ...]
        Source/target copy specifications for build context assembly. Source paths
        are absolute host paths; target paths are relative paths inside the staged
        context root.
    context_prefix : NonEmpty[Trimmed]
        Prefix for temporary build-context directory names.
    build_args : dict[str, str]
        Dockerfile build arguments passed to BuildKit.
    build_labels : dict[str, str]
        Image labels applied by the Dockerfile frontend.
    target : str | None
        Optional target stage in a multi-stage Containerfile.
    progress : str
        BuildKit progress mode.
    secrets : Mapping[KubeName, bool]
        Secret capability IDs to expose to the build. Values indicate whether the
        capability is required.
    ssh : Mapping[KubeName, bool]
        SSH capability IDs to expose to the build. Values indicate whether the
        capability is required.
    devices : Mapping[KubeName, bool]
        CDI device capability IDs to allow during the build. Values indicate
        whether the capability is required.
    cache : bool
        Whether to import and export registry-backed BuildKit cache.
    cache_ref : str | None
        Explicit registry cache reference. If omitted, one is derived from `image`.
    cache_mode : Literal["min", "max"]
        Registry cache export mode.
    """

    image: OCIImageRef
    dockerfile: NonEmpty[Trimmed]
    context_copies: tuple[tuple[Path, Path], ...] = ()
    context_prefix: NonEmpty[Trimmed] = "bertrand-buildkit-image"
    build_args: dict[str, str] = field(default_factory=dict)
    build_labels: dict[str, str] = field(default_factory=dict)
    target: str | None = None
    progress: str = "plain"
    secrets: Mapping[KubeName, bool] = field(default_factory=dict)
    ssh: Mapping[KubeName, bool] = field(default_factory=dict)
    devices: Mapping[KubeName, bool] = field(default_factory=dict)
    cache: bool = True
    cache_ref: str | None = None
    cache_mode: Literal["min", "max"] = "max"

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
        if not self.context_prefix.strip():
            msg = "BuildKit image context prefix cannot be empty"
            raise ValueError(msg)
        if self.target is not None and not self.target.strip():
            msg = "BuildKit target stage cannot be empty"
            raise ValueError(msg)
        if not self.progress.strip():
            msg = "BuildKit progress mode cannot be empty"
            raise ValueError(msg)
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
        if self.cache_mode not in ("min", "max"):
            msg = "BuildKit registry cache mode must be 'min' or 'max'"
            raise ValueError(msg)
        if self.cache_ref is not None:
            cache_ref = self.cache_ref.strip()
            if not cache_ref:
                msg = "BuildKit registry cache ref cannot be empty"
                raise ValueError(msg)
            object.__setattr__(self, "cache_ref", cache_ref)
        if self.cache and self.cache_ref is None:
            _default_cache_ref(self.image)
        for source, target in self.context_copies:
            if source != source.expanduser().resolve():
                msg = (
                    "BuildKit context sources must be canonical absolute paths: "
                    f"{source}"
                )
                raise ValueError(msg)
            if target.is_absolute():
                msg = f"BuildKit context targets must be relative: {target}"
                raise ValueError(msg)
            if any(part in ("", ".", "..") for part in target.parts):
                msg = (
                    "BuildKit context targets cannot contain '.', '..', or empty "
                    f"path segments: {target}"
                )
                raise ValueError(msg)

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
            "context_copies": [
                (source.as_posix(), target.as_posix())
                for source, target in self.context_copies
            ],
            "build_args": self.build_args,
            "build_labels": self.build_labels,
            "target": self.target,
            "secrets": dict(sorted(self.secrets.items())),
            "ssh": dict(sorted(self.ssh.items())),
            "devices": dict(sorted(self.devices.items())),
            "cache": self.cache,
            "cache_ref": self.cache_ref,
            "cache_mode": self.cache_mode,
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

    @property
    def registry_cache_ref(self) -> str | None:
        """Return the registry cache reference for this build.

        Returns
        -------
        str | None
            Registry cache reference, or `None` when registry cache is disabled.

        Raises
        ------
        ValueError
            If cache is enabled and no explicit or derivable cache reference exists.
        """
        if not self.cache:
            return None
        if self.cache_ref is not None:
            return self.cache_ref
        try:
            return _default_cache_ref(self.image)
        except ValueError as err:
            msg = str(err)
            raise ValueError(msg) from err

    def buildctl_args(
        self,
        buildkit_addr: str,
        *,
        image: str | None = None,
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
        args = [
            "--addr",
            buildkit_addr,
            "build",
            "--progress",
            self.progress,
            "--frontend",
            "dockerfile.v0",
            "--local",
            f"context={BUILD_JOB_CONTEXT_MOUNT}",
            "--local",
            f"dockerfile={BUILD_JOB_CONTEXT_MOUNT}",
            "--opt",
            "filename=Containerfile",
        ]
        if self.target is not None:
            args.extend(["--opt", f"target={self.target}"])
        for key, value in sorted(self.build_args.items()):
            args.extend(["--opt", f"build-arg:{key}={value}"])
        for key, value in sorted(self.build_labels.items()):
            args.extend(["--opt", f"label:{key}={value}"])
        for capability_id, path in sorted((secret_paths or {}).items()):
            args.extend(["--secret", f"id={capability_id},src={path}"])
        for capability_id, path in sorted((ssh_paths or {}).items()):
            args.extend(["--ssh", f"{capability_id}={path}"])
        for selector in sorted(set(device_selectors)):
            args.extend(["--allow", f"device={selector}"])
        cache_ref = self.registry_cache_ref
        if cache_ref is not None:
            args.extend(["--import-cache", f"type=registry,ref={cache_ref}"])
            args.extend(
                [
                    "--export-cache",
                    f"type=registry,ref={cache_ref},mode={self.cache_mode}",
                ]
            )
        if metadata_file is not None:
            metadata_file = metadata_file.strip()
            if metadata_file:
                args.extend(["--metadata-file", metadata_file])
        args.extend(["--output", f"type=image,name={image},push=true"])
        return args

    async def publish(
        self,
        kube: Kube,
        *,
        timeout: float = INFINITY,
        buildkit_pool: BuildKitPool = BUILDKIT_POOL,
        env_id: str | None = None,
    ) -> BuildKitImageResult:
        """Run a one-off BuildKit client Job and publish the image.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float, optional
            Maximum runtime budget in seconds. If infinite, wait indefinitely.
        buildkit_pool : BuildKitPool, optional
            BuildKit builder pool used to select a native daemon address.
        env_id : str | None, optional
            Environment UUID used to resolve secret, SSH, and device capabilities.

        Returns
        -------
        BuildKitImageResult
            Published image reference and digest metadata.

        Raises
        ------
        TimeoutError
            If staging, Job creation, or Job completion exceeds `timeout`.
        OSError
            If the Kubernetes build Job fails, Job creation fails, or BuildKit
            metadata cannot be loaded.
        ValueError
            If capability inputs are present without `env_id`.
        """
        if timeout <= 0:
            msg = "BuildKit image build timeout must be non-negative"
            raise TimeoutError(msg)
        if env_id is None and (self.secrets or self.ssh or self.devices):
            msg = "BuildKit capability inputs require an environment identity"
            raise ValueError(msg)
        if env_id is not None:
            env_id = _check_uuid(env_id)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout

        async with self._execution_workspace(
            kube,
            env_id=env_id,
            timeout=deadline - loop.time(),
        ) as workspace:
            local_platform = workspace.local_node.platform
            if not local_platform:
                msg = (
                    f"local Kubernetes node {workspace.local_node.name!r} has no "
                    "usable "
                    "native platform labels"
                )
                raise OSError(msg)
            targets = await self.schedule(
                kube,
                env_id=env_id,
                platforms=(local_platform,),
                preferred_node=workspace.local_node.name,
                buildkit_pool=buildkit_pool,
                timeout=deadline - loop.time(),
            )
            if len(targets) != 1:
                msg = f"expected exactly one BuildKit job target, found {len(targets)}"
                raise OSError(msg)
            return await self._publish_target(
                kube,
                target=targets[0],
                image=self.image,
                context=workspace.context,
                metadata=workspace.metadata,
                capability_volumes=workspace.capability_volumes,
                capability_mounts=workspace.capability_mounts,
                secret_paths=workspace.secret_paths,
                ssh_paths=workspace.ssh_paths,
                node_name=workspace.local_node.name,
                timeout=deadline - loop.time(),
            )

    async def publish_platforms(
        self,
        kube: Kube,
        *,
        env_id: str | None,
        timeout: float,
        platforms: Iterable[str] | None = None,
        preferred_node: str | None = None,
        buildkit_pool: BuildKitPool = BUILDKIT_POOL,
        output_ref_factory: (
            Callable[[OCIImageRef, str, str], OCIImageRef] | None
        ) = None,
    ) -> tuple[BuildKitPlatformResult, ...]:
        """Publish one native platform image per scheduled BuildKit target.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        env_id : str | None
            Environment UUID used to resolve secret, SSH, and device capabilities.
        timeout : float
            Maximum runtime budget in seconds. If infinite, wait indefinitely.
        platforms : Iterable[str] | None, optional
            Explicit target platform filters. If omitted, every ready native cluster
            platform is targeted.
        preferred_node : str | None, optional
            Node to prefer when selecting a builder for its matching platform.
        buildkit_pool : BuildKitPool, optional
            BuildKit builder pool used to select native daemon addresses.
        output_ref_factory : callable, optional
            Factory receiving the final image ref, platform, and publish run ID.
            It must return the temporary output ref for that platform. If omitted,
            refs are derived from :attr:`image`.

        Returns
        -------
        tuple[BuildKitPlatformResult, ...]
            Per-platform image digests, sorted by platform.

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
        output_ref_factory = output_ref_factory or platform_output_ref

        async with self._execution_workspace(
            kube,
            env_id=env_id,
            timeout=deadline - loop.time(),
        ) as workspace:
            targets = await self.schedule(
                kube,
                env_id=env_id,
                platforms=platforms,
                preferred_node=preferred_node,
                buildkit_pool=buildkit_pool,
                timeout=deadline - loop.time(),
            )
            run_id = uuid.uuid4().hex[:BUILD_PLATFORM_RUN_ID_BYTES]
            results: list[BuildKitPlatformResult] = []
            for target in sorted(targets, key=lambda item: item.platform):
                image = output_ref_factory(self.image, target.platform, run_id)
                image = image.strip()
                if not image:
                    msg = (
                        "BuildKit platform output ref factory returned an empty "
                        f"image for platform {target.platform!r}"
                    )
                    raise ValueError(msg)
                build = await self._publish_target(
                    kube,
                    target=target,
                    image=image,
                    context=workspace.context,
                    metadata=workspace.metadata / platform_token(target.platform),
                    capability_volumes=workspace.capability_volumes,
                    capability_mounts=workspace.capability_mounts,
                    secret_paths=workspace.secret_paths,
                    ssh_paths=workspace.ssh_paths,
                    node_name=workspace.local_node.name,
                    timeout=deadline - loop.time(),
                )
                results.append(
                    BuildKitPlatformResult(
                        platform=target.platform,
                        builder=target.builder,
                        image=build.image,
                        digest=build.digest,
                        digest_ref=digest_ref(build.image, build.digest),
                        build=build,
                    )
                )
            return tuple(results)

    @asynccontextmanager
    async def _execution_workspace(
        self,
        kube: Kube,
        *,
        env_id: str | None,
        timeout: float,
    ) -> AsyncIterator[_BuildKitExecutionWorkspace]:
        if timeout <= 0:
            msg = "BuildKit execution workspace timeout must be non-negative"
            raise TimeoutError(msg)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        with tempfile.TemporaryDirectory(prefix=f"{self.context_prefix}-") as tmp:
            root = Path(tmp).resolve()
            try:
                context, metadata = self._stage_context(root)
            except FileNotFoundError as err:
                raise FileNotFoundError(str(err)) from err
            local_node = await Node.local(kube, timeout=deadline - loop.time())
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
            yield _BuildKitExecutionWorkspace(
                context=context,
                metadata=metadata,
                local_node=local_node,
                capability_volumes=tuple(capability_volumes),
                capability_mounts=tuple(capability_mounts),
                secret_paths=MappingProxyType(secret_paths),
                ssh_paths=MappingProxyType(ssh_paths),
            )

    async def _publish_target(
        self,
        kube: Kube,
        *,
        target: BuildKitJobTarget,
        image: str,
        context: Path,
        metadata: Path,
        capability_volumes: tuple[VolumeSpec, ...],
        capability_mounts: tuple[VolumeMountSpec, ...],
        secret_paths: Mapping[str, str],
        ssh_paths: Mapping[str, str],
        node_name: str,
        timeout: float,
    ) -> BuildKitImageResult:
        if timeout <= 0:
            msg = "BuildKit target image build timeout must be non-negative"
            raise TimeoutError(msg)
        metadata.mkdir(parents=True, exist_ok=True)
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
                        command=["buildctl"],
                        args=self.buildctl_args(
                            target.builder.addr,
                            image=image,
                            secret_paths=secret_paths,
                            ssh_paths=ssh_paths,
                            device_selectors=target.device_selectors,
                            metadata_file=BUILD_JOB_METADATA_FILE,
                        ),
                        volume_mounts=[
                            VolumeMountSpec(
                                name=BUILD_JOB_CONTEXT_VOLUME,
                                mount_path=BUILD_JOB_CONTEXT_MOUNT,
                                read_only=True,
                            ),
                            VolumeMountSpec(
                                name=BUILD_JOB_METADATA_VOLUME,
                                mount_path=BUILD_JOB_METADATA_MOUNT,
                            ),
                            *capability_mounts,
                        ],
                    )
                ],
                volumes=[
                    VolumeSpec.host_path(
                        BUILD_JOB_CONTEXT_VOLUME,
                        path=context,
                        host_path_type="Directory",
                    ),
                    VolumeSpec.host_path(
                        BUILD_JOB_METADATA_VOLUME,
                        path=metadata,
                        host_path_type="Directory",
                    ),
                    *capability_volumes,
                ],
                node_name=node_name,
            ),
            ttl_seconds_after_finished=BUILD_JOB_TTL_SECONDS,
            timeout=deadline - loop.time(),
        )
        await wait_job_complete(
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
        )
        return _load_build_metadata(
            metadata / Path(BUILD_JOB_METADATA_FILE).name,
            image=image,
        )

    def _stage_context(self, root: Path) -> tuple[Path, Path]:
        context = root / "context"
        metadata = root / "metadata"
        context.mkdir()
        metadata.mkdir()
        for source, target in self.context_copies:
            if not source.exists():
                msg = f"BuildKit image build context source does not exist: {source}"
                raise FileNotFoundError(msg)
            target = context / target
            target.parent.mkdir(parents=True, exist_ok=True)
            if source.is_dir():
                shutil.copytree(source, target, dirs_exist_ok=target.exists())
            else:
                shutil.copy2(source, target)
        atomic_write_text(
            context / "Containerfile",
            self.dockerfile,
            encoding="utf-8",
        )
        return context, metadata

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

    async def schedule(
        self,
        kube: Kube,
        *,
        env_id: str | None,
        timeout: float,
        platforms: Iterable[str] | None = None,
        preferred_node: str | None = None,
        buildkit_pool: BuildKitPool = BUILDKIT_POOL,
        config_hash: str | None = None,
    ) -> tuple[BuildKitJobTarget, ...]:
        """Schedule this build onto native BuildKit builders.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        env_id : str | None
            Environment UUID used to resolve device capabilities.
        timeout : float
            Maximum request budget in seconds. If infinite, wait indefinitely.
        platforms : tuple[str, ...] | None, optional
            Explicit platform filters. If omitted, all ready cluster platforms are
            targeted.
        preferred_node : str | None, optional
            Node to prefer when scheduling its matching platform.
        buildkit_pool : BuildKitPool, optional
            BuildKit builder pool used for scheduling.
        config_hash : str | None, optional
            Expected BuildKit config hash used to reject stale builders.

        Returns
        -------
        tuple[BuildKitJobTarget, ...]
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
        snapshot = await buildkit_pool._snapshot(
            kube,
            timeout=deadline - loop.time(),
            config_hash=config_hash,
        )
        groups = buildkit_pool._candidate_groups(
            snapshot,
            platforms=platforms,
            preferred_node=preferred_node,
            config_hash=config_hash,
        )

        targets: list[BuildKitJobTarget] = []
        for platform, candidates in groups.items():
            if not self.devices:
                targets.append(
                    BuildKitJobTarget(
                        platform=platform,
                        builder=candidates[0],
                        device_selectors=(),
                    )
                )
                continue
            errors: list[str] = []
            selected: BuildKitJobTarget | None = None
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
                selected = BuildKitJobTarget(
                    platform=platform,
                    builder=candidate,
                    device_selectors=selectors,
                )
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
        msg = (
            f"invalid BuildKit image reference {image!r}; provide explicit "
            "cache_ref or disable cache"
        )
        raise ValueError(msg) from err


def _capability_volume_name(kind: CapabilityKind, capability_id: str) -> str:
    payload = f"{kind}:{capability_id}".encode()
    digest = hashlib.sha256(payload).hexdigest()[:16]
    return f"build-{kind}-{digest}"


def _load_build_metadata(path: Path, *, image: str) -> BuildKitImageResult:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except OSError as err:
        msg = f"BuildKit did not write metadata file {path}: {err}"
        raise OSError(msg) from err
    except json.JSONDecodeError as err:
        msg = f"BuildKit metadata file {path} is not valid JSON: {err}"
        raise OSError(msg) from err
    if not isinstance(payload, Mapping):
        msg = f"BuildKit metadata file {path} must contain a JSON object"
        raise OSError(msg)
    metadata = dict(cast("Mapping[str, object]", payload))
    digest = str(metadata.get("containerimage.digest") or "").strip()
    if not digest:
        msg = "BuildKit metadata is missing containerimage.digest"
        raise OSError(msg)
    config_digest = str(metadata.get("containerimage.config.digest") or "").strip()
    raw_descriptor = metadata.get("containerimage.descriptor")
    descriptor = (
        MappingProxyType(dict(cast("Mapping[str, object]", raw_descriptor)))
        if isinstance(raw_descriptor, Mapping)
        else MappingProxyType({})
    )
    return BuildKitImageResult(
        image=image,
        digest=digest,
        config_digest=config_digest,
        descriptor=descriptor,
        metadata=MappingProxyType(metadata),
    )
