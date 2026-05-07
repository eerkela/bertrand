"""Short-lived BuildKit client Jobs for publishing OCI images."""

from __future__ import annotations

import asyncio
import hashlib
import json
import shutil
import tempfile
import uuid
from dataclasses import dataclass, field
from pathlib import Path

from ...config.core import NonEmpty, OCIImageRef, Trimmed
from ...run import BERTRAND_ENV, BERTRAND_NAMESPACE, INFINITY, atomic_write_text
from ..api import ContainerSpec, Kube, VolumeMountSpec, VolumeSpec
from ..job import Job
from ..node import Node
from .daemon import BUILDKIT, BUILDKIT_IMAGE, BuildKit

BUILD_JOB_CONTEXT_MOUNT = "/workspace"
BUILD_JOB_CONTEXT_VOLUME = "context"
BUILD_JOB_LABEL = "bertrand.dev/build-job"
BUILD_JOB_LABEL_VALUE = "v1"
BUILD_JOB_TTL_SECONDS = 3600


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
    """

    image: OCIImageRef
    dockerfile: NonEmpty[Trimmed]
    context_copies: tuple[tuple[Path, Path], ...] = ()
    context_prefix: NonEmpty[Trimmed] = "bertrand-buildkit-image"
    build_args: dict[str, str] = field(default_factory=dict)
    build_labels: dict[str, str] = field(default_factory=dict)
    target: str | None = None
    progress: str = "plain"

    def __post_init__(self) -> None:
        if not self.image.strip():
            raise ValueError("BuildKit image reference cannot be empty")
        if not self.dockerfile.strip():
            raise ValueError("BuildKit image Containerfile cannot be empty")
        if not self.context_prefix.strip():
            raise ValueError("BuildKit image context prefix cannot be empty")
        if self.target is not None and not self.target.strip():
            raise ValueError("BuildKit target stage cannot be empty")
        if not self.progress.strip():
            raise ValueError("BuildKit progress mode cannot be empty")
        for source, target in self.context_copies:
            if source != source.expanduser().resolve():
                raise ValueError(
                    f"BuildKit context sources must be canonical absolute paths: {source}"
                )
            if target.is_absolute():
                raise ValueError(f"BuildKit context targets must be relative: {target}")
            if any(part in ("", ".", "..") for part in target.parts):
                raise ValueError(
                    "BuildKit context targets cannot contain '.', '..', or empty "
                    f"path segments: {target}"
                )

    @property
    def labels(self) -> dict[str, str]:
        """
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
        """
        Returns
        -------
        str
            Unique Kubernetes Job name for this build execution.
        """
        payload = {
            "image": self.image,
            "dockerfile": self.dockerfile,
            "context_copies": [
                (source.as_posix(), target.as_posix()) for source, target in self.context_copies
            ],
            "build_args": self.build_args,
            "build_labels": self.build_labels,
            "target": self.target,
        }
        text = json.dumps(payload, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
        digest = hashlib.sha256(text.encode("utf-8")).hexdigest()[:16]
        nonce = uuid.uuid4().hex[:8]
        return f"bertrand-build-{digest}-{nonce}"

    def buildctl_args(self, buildkit: BuildKit = BUILDKIT) -> list[str]:
        """Render the `buildctl` command arguments for this build.

        Parameters
        ----------
        buildkit : BuildKit, optional
            BuildKit daemon endpoint used by the client Job.

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
        args.extend(["--output", f"type=image,name={self.image},push=true"])
        return args

    async def publish(
        self,
        kube: Kube,
        *,
        timeout: float = INFINITY,
        buildkit: BuildKit = BUILDKIT,
    ) -> str:
        """Run a one-off BuildKit client Job and publish the image.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float, optional
            Maximum runtime budget in seconds. If infinite, wait indefinitely.
        buildkit : BuildKit, optional
            BuildKit daemon endpoint used by the client Job.

        Returns
        -------
        str
            Published image reference.

        Raises
        ------
        TimeoutError
            If staging, Job creation, or Job completion exceeds `timeout`.
        OSError
            If context staging fails, Kubernetes Job submission fails, or the Job
            reports failure.
        """
        if timeout <= 0:
            raise TimeoutError("BuildKit image build timeout must be non-negative")
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout

        with tempfile.TemporaryDirectory(prefix=f"{self.context_prefix}-") as tmp:
            context = Path(tmp).resolve()
            local_node = await Node.local(kube, timeout=deadline - loop.time())
            for source, target in self.context_copies:
                if not source.exists():
                    raise FileNotFoundError(
                        f"BuildKit image build context source does not exist: {source}"
                    )
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
                        args=self.buildctl_args(buildkit),
                        volume_mounts=[
                            VolumeMountSpec(
                                name=BUILD_JOB_CONTEXT_VOLUME,
                                mount_path=BUILD_JOB_CONTEXT_MOUNT,
                                read_only=True,
                            )
                        ],
                    )
                ],
                volumes=[
                    VolumeSpec.host_path(
                        BUILD_JOB_CONTEXT_VOLUME,
                        path=context,
                        host_path_type="Directory",
                    )
                ],
                ttl_seconds_after_finished=BUILD_JOB_TTL_SECONDS,
                node_name=local_node.name,
                timeout=deadline - loop.time(),
            )
            await job.wait_complete(kube, timeout=deadline - loop.time())
        return self.image
