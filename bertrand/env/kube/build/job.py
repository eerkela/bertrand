"""Short-lived BuildKit client Jobs for publishing OCI images."""

from __future__ import annotations

import asyncio
import hashlib
import json
import shutil
import tempfile
import uuid
from collections.abc import Mapping
from dataclasses import dataclass, field
from pathlib import Path
from types import MappingProxyType
from typing import TYPE_CHECKING, Literal, cast

from bertrand.env.config.core import _check_kube_name, _check_uuid
from bertrand.env.kube.api import ContainerSpec, Kube, VolumeMountSpec, VolumeSpec
from bertrand.env.kube.build.daemon import BUILDKIT, BUILDKIT_IMAGE, BuildKit
from bertrand.env.kube.capability.base import Capability, CapabilityKind
from bertrand.env.kube.job import Job
from bertrand.env.kube.node import Node
from bertrand.env.git import (
    BERTRAND_ENV,
    BERTRAND_NAMESPACE,
    INFINITY,
    atomic_write_text,
)

if TYPE_CHECKING:
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
        buildkit: BuildKit = BUILDKIT,
        *,
        secret_paths: Mapping[str, str] | None = None,
        ssh_paths: Mapping[str, str] | None = None,
        metadata_file: str | None = None,
    ) -> list[str]:
        """Render the `buildctl` command arguments for this build.

        Parameters
        ----------
        buildkit : BuildKit, optional
            BuildKit daemon endpoint used by the client Job.
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
        """
        args = [
            "--addr",
            buildkit.addr,
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
        args.extend(["--output", f"type=image,name={self.image},push=true"])
        return args

    async def publish(
        self,
        kube: Kube,
        *,
        timeout: float = INFINITY,
        buildkit: BuildKit = BUILDKIT,
        env_id: str | None = None,
    ) -> BuildKitImageResult:
        """Run a one-off BuildKit client Job and publish the image.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float, optional
            Maximum runtime budget in seconds. If infinite, wait indefinitely.
        buildkit : BuildKit, optional
            BuildKit daemon endpoint used by the client Job.
        env_id : str | None, optional
            Environment UUID used to resolve secret and SSH capabilities.

        Returns
        -------
        BuildKitImageResult
            Published image reference and digest metadata.

        Raises
        ------
        TimeoutError
            If staging, Job creation, or Job completion exceeds `timeout`.
        FileNotFoundError
            If a declared build context source does not exist.
        OSError
            If the Kubernetes build Job fails, Job creation fails, or BuildKit
            metadata cannot be loaded.
        ValueError
            If capability inputs are present without `env_id`.
        """
        if timeout <= 0:
            msg = "BuildKit image build timeout must be non-negative"
            raise TimeoutError(msg)
        if env_id is None and (self.secrets or self.ssh):
            msg = "BuildKit secret and SSH inputs require an environment identity"
            raise ValueError(msg)
        if env_id is not None:
            env_id = _check_uuid(env_id)
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

            job = await Job.create(
                kube,
                namespace=BERTRAND_NAMESPACE,
                name=self.job_name,
                labels=self.labels,
                containers=[
                    ContainerSpec(
                        name="buildctl",
                        image=BUILDKIT_IMAGE,
                        image_pull_policy="IfNotPresent",
                        command=["buildctl"],
                        args=self.buildctl_args(
                            buildkit,
                            secret_paths=secret_paths,
                            ssh_paths=ssh_paths,
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
                ttl_seconds_after_finished=BUILD_JOB_TTL_SECONDS,
                node_name=local_node.name,
                timeout=deadline - loop.time(),
            )
            try:
                await job.wait_complete(kube, timeout=deadline - loop.time())
                return _load_build_metadata(
                    metadata / Path(BUILD_JOB_METADATA_FILE).name,
                    image=self.image,
                )
            except (OSError, TimeoutError) as err:
                logs = await _build_job_log_tail(
                    kube,
                    job,
                    timeout=BUILD_JOB_DIAGNOSTIC_TIMEOUT_SECONDS,
                )
                await _delete_build_job(
                    kube,
                    job,
                    timeout=BUILD_JOB_CLEANUP_TIMEOUT_SECONDS,
                )
                msg = _build_failure_message(self.image, err, logs)
                if isinstance(err, TimeoutError):
                    raise TimeoutError(msg) from err
                raise OSError(msg) from err

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
                shutil.copytree(source, target)
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


async def _build_job_log_tail(kube: Kube, job: Job, *, timeout: float) -> str:
    if timeout <= 0:
        return ""
    try:
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        pods = await job.pods(kube, timeout=deadline - loop.time())
        chunks: list[str] = []
        for pod in pods:
            remaining = deadline - loop.time()
            if remaining <= 0:
                break
            log = await pod.logs(
                kube,
                timeout=remaining,
                tail_lines=BUILD_JOB_LOG_TAIL_LINES,
            )
            log = log.strip()
            if log:
                chunks.append(f"--- {pod.namespace}/{pod.name} ---\n{log}")
        return "\n\n".join(chunks)
    except (OSError, TimeoutError, ValueError) as err:
        return f"<failed to read BuildKit build pod logs: {err}>"


async def _delete_build_job(kube: Kube, job: Job, *, timeout: float) -> None:
    if timeout <= 0:
        return
    try:
        await job.delete(
            kube,
            timeout=timeout,
            propagation_policy="Foreground",
        )
    except (OSError, TimeoutError):
        return


def _build_failure_message(image: str, err: BaseException, logs: str) -> str:
    msg = f"BuildKit image build for {image!r} failed: {err}"
    logs = logs.strip()
    if logs:
        msg = f"{msg}\n\nBuild pod logs:\n{logs}"
    return msg


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
    image = image.strip()
    if "@" in image:
        msg = (
            f"BuildKit image reference {image!r} uses a digest; provide explicit "
            "cache_ref or disable cache"
        )
        raise ValueError(msg)
    slash = image.rfind("/")
    colon = image.rfind(":")
    if colon <= slash or colon == len(image) - 1:
        msg = (
            f"BuildKit image reference {image!r} has no tag; provide explicit "
            "cache_ref or disable cache"
        )
        raise ValueError(msg)
    return f"{image[:colon]}:{BUILD_CACHE_TAG}"


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
