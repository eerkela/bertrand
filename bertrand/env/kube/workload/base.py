"""Minimal native Kubernetes workload rendering intents."""

from __future__ import annotations

import hashlib
import shlex
from dataclasses import dataclass, replace
from pathlib import PurePosixPath
from types import MappingProxyType
from typing import TYPE_CHECKING, cast
from uuid import UUID

from bertrand.env.git.bertrand_git import (
    BERTRAND_ENV,
    PROJECT_ENV,
    PROJECT_MOUNT,
    REPO_ID_ENV,
    WORKTREE_ENV,
    WORKTREE_ID_ENV,
    WORKTREE_MOUNT,
)
from bertrand.env.kube.api.spec import EnvVarSpec, VolumeMountSpec, VolumeSpec
from bertrand.env.kube.ceph.volume import RepoVolume

if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence

    from bertrand.env.kube.api.spec import ContainerSpec, PodTemplateSpec
    from bertrand.env.kube.capability.device import DRAResourceClaimIntent

WORKLOAD_REPOSITORY_VOLUME = "bertrand-repository"
WORKLOAD_BOOTSTRAP_COMMAND = ("/bin/sh", "-c")
WORKLOAD_BOOTSTRAP_ARG0 = "bertrand-workload-bootstrap"
WORKLOAD_NAME_PREFIX = "bertrand-workload-"
WORKLOAD_NAME_HASH_CHARS = 44
WORKLOAD_LABEL = "bertrand.dev/workload"
WORKLOAD_LABEL_VALUE = "v1"
WORKLOAD_ID_LABEL = "bertrand.dev/workload-id"
WORKLOAD_REPO_LABEL = "bertrand.dev/workload-repo"
WORKLOAD_WORKTREE_LABEL = "bertrand.dev/workload-worktree"
WORKLOAD_WORKTREE_ID_LABEL = "bertrand.dev/workload-worktree-id"


@dataclass(frozen=True)
class WorkloadIdentity:
    """Stable Kubernetes identity for one Bertrand workload.

    Parameters
    ----------
    repo_id : str
        Stable repository UUID containing the workload worktree.
    worktree_id : str
        Stable UUID for this concrete checkout instance.
    worktree : str | PurePosixPath
        Relative worktree path inside the repository volume. Use ``"."`` for the
        repository root.
    """

    repo_id: str
    worktree_id: str
    worktree: str | PurePosixPath

    def __post_init__(self) -> None:
        """Validate and normalize workload identity fields.

        Raises
        ------
        ValueError
            If `repo_id` is not a UUID or `worktree` is absolute or escapes the
            repository.
        """
        try:
            repo_id = UUID(self.repo_id).hex
        except (TypeError, ValueError) as err:
            msg = f"invalid workload repository id: {self.repo_id!r}"
            raise ValueError(msg) from err
        try:
            worktree_id = UUID(self.worktree_id).hex
        except (TypeError, ValueError) as err:
            msg = f"invalid workload worktree id: {self.worktree_id!r}"
            raise ValueError(msg) from err

        object.__setattr__(self, "repo_id", repo_id)
        object.__setattr__(self, "worktree_id", worktree_id)
        object.__setattr__(self, "worktree", _worktree_path(self.worktree))

    @property
    def worktree_env(self) -> str:
        """Return the normalized worktree identity.

        Returns
        -------
        str
            ``"."`` for root worktrees, otherwise the POSIX relative path.
        """
        worktree = cast("PurePosixPath", self.worktree)
        if not worktree.parts:
            return "."
        return worktree.as_posix()

    @property
    def workload_id(self) -> str:
        """Return the compact workload ID used in selector labels.

        Returns
        -------
        str
            Deterministic hash label value for this workload.
        """
        return _label_hash(f"{self.repo_id}\0{self.worktree_id}")

    @property
    def name(self) -> str:
        """Return the canonical Kubernetes resource name.

        Returns
        -------
        str
            DNS-label-safe name shared by the workload Deployment and Service.
        """
        payload = f"{self.repo_id}\0{self.worktree_id}".encode()
        digest = hashlib.sha256(payload).hexdigest()
        return f"{WORKLOAD_NAME_PREFIX}{digest[:WORKLOAD_NAME_HASH_CHARS]}"

    @property
    def selector(self) -> Mapping[str, str]:
        """Return immutable pod selector labels for this workload.

        Returns
        -------
        Mapping[str, str]
            Labels that select only this workload's pods.
        """
        return MappingProxyType(
            {
                BERTRAND_ENV: "1",
                WORKLOAD_ID_LABEL: self.workload_id,
            }
        )

    @property
    def labels(self) -> Mapping[str, str]:
        """Return stable Bertrand labels for workload-owned resources.

        Returns
        -------
        Mapping[str, str]
            Labels applied to workload Services, HTTPRoutes, pod templates, and
            workload controllers.
        """
        labels = {
            "app.kubernetes.io/name": self.name,
            "app.kubernetes.io/part-of": "bertrand",
            "app.kubernetes.io/component": "workload",
            WORKLOAD_LABEL: WORKLOAD_LABEL_VALUE,
            WORKLOAD_REPO_LABEL: _label_hash(self.repo_id),
            WORKLOAD_WORKTREE_LABEL: _label_hash(self.worktree_env),
            WORKLOAD_WORKTREE_ID_LABEL: _label_hash(self.worktree_id),
        }
        labels.update(self.selector)
        return MappingProxyType(labels)


@dataclass(frozen=True)
class WorkloadRepository:
    """Repository volume intent for native workload pods.

    Parameters
    ----------
    repo_id : str
        Stable repository UUID used to derive the managed Ceph PVC claim name.
    worktree_id : str
        Stable UUID for this concrete checkout instance.
    worktree : str | PurePosixPath
        Relative worktree path inside the repository volume. Use ``"."`` for the
        repository root.
    read_only : bool, optional
        Whether to mount the repository volume read-only.
    """

    repo_id: str
    worktree_id: str
    worktree: str | PurePosixPath
    read_only: bool = False

    def __post_init__(self) -> None:
        """Validate and normalize repository runtime intent.

        Raises
        ------
        ValueError
            If `repo_id` is not a UUID or `worktree` is absolute or escapes the
            repository.
        """
        try:
            repo_id = UUID(self.repo_id).hex
        except (TypeError, ValueError) as err:
            msg = f"invalid workload repository id: {self.repo_id!r}"
            raise ValueError(msg) from err
        try:
            worktree_id = UUID(self.worktree_id).hex
        except (TypeError, ValueError) as err:
            msg = f"invalid workload worktree id: {self.worktree_id!r}"
            raise ValueError(msg) from err

        object.__setattr__(self, "repo_id", repo_id)
        object.__setattr__(self, "worktree_id", worktree_id)
        object.__setattr__(self, "worktree", _worktree_path(self.worktree))

    @property
    def claim_name(self) -> str:
        """Return the managed repository PVC name.

        Returns
        -------
        str
            Deterministic PVC claim name for this repository.
        """
        return RepoVolume.claim_name(self.repo_id)

    @property
    def worktree_env(self) -> str:
        """Return the relative worktree value for `BERTRAND_WORKTREE`.

        Returns
        -------
        str
            ``"."`` for root worktrees, otherwise the POSIX relative path.
        """
        worktree = cast("PurePosixPath", self.worktree)
        if not worktree.parts:
            return "."
        return worktree.as_posix()

    @property
    def target_path(self) -> PurePosixPath:
        """Return the selected worktree path inside the repository mount.

        Returns
        -------
        PurePosixPath
            Container path that `/bertrand` should link to.
        """
        worktree = cast("PurePosixPath", self.worktree)
        if not worktree.parts:
            return PROJECT_MOUNT
        return PROJECT_MOUNT / worktree


@dataclass(frozen=True)
class WorkloadPod:
    """Manual pod-template intent shared by native workloads.

    Parameters
    ----------
    template : PodTemplateSpec
        Base pod template to render. The template may contain multiple containers.
    primary_container : str
        Container name that receives command overrides.
    repository : WorkloadRepository
        Managed repository volume and selected worktree intent.
    resource_claim_templates : tuple[DRAResourceClaimIntent, ...], optional
        DRA ResourceClaimTemplate intents that must be converged with this
        workload.
    runtime_env : Mapping[str, str], optional
        Additional invariant runtime environment variables to apply to every
        container.
    """

    template: PodTemplateSpec
    primary_container: str
    repository: WorkloadRepository
    resource_claim_templates: tuple[DRAResourceClaimIntent, ...] = ()
    runtime_env: Mapping[str, str] = MappingProxyType({})

    def __post_init__(self) -> None:
        """Validate and normalize workload pod intent.

        Raises
        ------
        ValueError
            If the pod has no containers, names are duplicated, the primary
            container is not part of the template, or runtime environment entries
            are malformed.
        """
        containers = tuple(self.template.containers)
        container_names = _container_names(containers)
        primary = self.primary_container.strip()
        if primary not in container_names:
            msg = f"unknown primary workload container: {self.primary_container!r}"
            raise ValueError(msg)

        object.__setattr__(
            self,
            "template",
            replace(
                self.template,
                containers=containers,
                volumes=tuple(self.template.volumes),
                resource_claims=tuple(self.template.resource_claims),
            ),
        )
        object.__setattr__(self, "primary_container", primary)
        object.__setattr__(
            self,
            "resource_claim_templates",
            tuple(self.resource_claim_templates),
        )
        object.__setattr__(self, "runtime_env", _runtime_env(self.runtime_env))

    @property
    def identity(self) -> WorkloadIdentity:
        """Return this workload's stable Kubernetes identity.

        Returns
        -------
        WorkloadIdentity
            Identity derived from the managed repository and selected worktree.
        """
        return WorkloadIdentity(
            repo_id=self.repository.repo_id,
            worktree_id=self.repository.worktree_id,
            worktree=self.repository.worktree_env,
        )

    @property
    def name(self) -> str:
        """Return the canonical Kubernetes workload resource name.

        Returns
        -------
        str
            Stable name for this workload's Deployment, Service, NetworkPolicy, and
            HTTPRoute backends.
        """
        return self.identity.name

    @property
    def labels(self) -> Mapping[str, str]:
        """Return stable labels for workload-owned Kubernetes resources.

        Returns
        -------
        Mapping[str, str]
            Labels shared by the workload Service, NetworkPolicy, HTTPRoutes, and pod
            template.
        """
        return self.identity.labels

    @property
    def selector(self) -> Mapping[str, str]:
        """Return immutable pod selector labels for this workload.

        Returns
        -------
        Mapping[str, str]
            Labels used by Services, NetworkPolicies, and future controllers to select
            workload pods.
        """
        return self.identity.selector

    def pod_template(
        self,
        *,
        primary_command: Sequence[str] | None = None,
        primary_args: Sequence[str] | None = None,
        interactive: bool = False,
        stdin_once: bool = False,
    ) -> PodTemplateSpec:
        """Render this workload as a Kubernetes pod template.

        Parameters
        ----------
        primary_command : Sequence[str] | None, optional
            Optional command override for the primary container.
        primary_args : Sequence[str] | None, optional
            Optional runtime arguments to append to the primary container command.
        interactive : bool, optional
            Whether to configure the primary container for Kubernetes stdin/TTY
            attachment.
        stdin_once : bool, optional
            Whether Kubernetes should close primary-container stdin after the first
            attach session disconnects. This is useful for generated Job runs.

        Returns
        -------
        PodTemplateSpec
            Pod template with repository mounts, environment, and command bootstrap
            applied.
        """
        command = (
            _command(primary_command, label="primary command")
            if primary_command is not None
            else None
        )
        runtime_args = _runtime_args(primary_args) if primary_args is not None else None
        rendered_containers: list[ContainerSpec] = []
        repository_mount = VolumeMountSpec(
            name=WORKLOAD_REPOSITORY_VOLUME,
            mount_path=PROJECT_MOUNT.as_posix(),
            read_only=self.repository.read_only,
        )
        repository_env = _repository_env(self.repository, self.runtime_env)
        bootstrap = _bootstrap_script(self.repository)
        for container in self.template.containers:
            if container.name.strip() == self.primary_container:
                container_args = tuple(container.args or ())
                container = replace(
                    container,
                    command=command if command is not None else container.command,
                    args=(
                        (*container_args, *runtime_args)
                        if runtime_args is not None
                        else container.args
                    ),
                    stdin=True if interactive else container.stdin,
                    stdin_once=stdin_once if interactive else container.stdin_once,
                    tty=True if interactive else container.tty,
                )
            rendered_containers.append(
                _bootstrap_container(
                    container,
                    mount=repository_mount,
                    env=repository_env,
                    script=bootstrap,
                )
            )

        return replace(
            self.template,
            containers=tuple(rendered_containers),
            labels=_with_workload_labels(self.template.labels, self.labels),
            volumes=_repository_volumes(
                tuple(self.template.volumes),
                self.repository.claim_name,
            ),
        )


def _container_names(containers: tuple[ContainerSpec, ...]) -> set[str]:
    if not containers:
        msg = "workload pod requires at least one container"
        raise ValueError(msg)
    names: set[str] = set()
    for container in containers:
        name = container.name.strip()
        if not name:
            msg = "workload container names cannot be empty"
            raise ValueError(msg)
        if name in names:
            msg = f"duplicate workload container name: {name!r}"
            raise ValueError(msg)
        names.add(name)
    return names


def _command(
    command: Sequence[str],
    *,
    label: str = "job entrypoint",
    allow_empty: bool = False,
) -> tuple[str, ...]:
    out: list[str] = []
    for raw in command:
        part = raw.strip()
        if not part:
            msg = f"{label} entries cannot be empty"
            raise ValueError(msg)
        out.append(part)
    if not allow_empty and not out:
        msg = f"{label} cannot be empty"
        raise ValueError(msg)
    return tuple(out)


def _runtime_args(args: Sequence[str]) -> tuple[str, ...]:
    return tuple(args)


def _worktree_path(worktree: str | PurePosixPath) -> PurePosixPath:
    text = str(worktree).strip()
    path = PurePosixPath(text if text else ".")
    if path.is_absolute() or ".." in path.parts:
        msg = f"workload worktree must be relative and cannot escape repo: {text!r}"
        raise ValueError(msg)
    return path


def _runtime_env(env: Mapping[str, str]) -> MappingProxyType[str, str]:
    out: dict[str, str] = {}
    for key, value in env.items():
        name = key.strip()
        if not name or any(part.isspace() for part in name):
            msg = f"workload runtime environment key is invalid: {key!r}"
            raise ValueError(msg)
        out[name] = str(value)
    return MappingProxyType(out)


def _repository_env(
    repository: WorkloadRepository,
    runtime_env: Mapping[str, str],
) -> dict[str, str]:
    env = {
        BERTRAND_ENV: "1",
        REPO_ID_ENV: repository.repo_id,
        WORKTREE_ID_ENV: repository.worktree_id,
        PROJECT_ENV: PROJECT_MOUNT.as_posix(),
        WORKTREE_ENV: repository.worktree_env,
    }
    for key, value in runtime_env.items():
        if key in env and env[key] != value:
            msg = (
                f"workload runtime environment cannot override {key!r}: "
                f"{value!r} != {env[key]!r}"
            )
            raise ValueError(msg)
        env[key] = value
    return env


def _repository_volumes(
    volumes: tuple[VolumeSpec, ...],
    claim_name: str,
) -> tuple[VolumeSpec, ...]:
    repository = VolumeSpec.pvc(WORKLOAD_REPOSITORY_VOLUME, claim_name=claim_name)
    out: list[VolumeSpec] = []
    found = False
    for volume in volumes:
        if volume.name == WORKLOAD_REPOSITORY_VOLUME:
            if volume != repository:
                msg = (
                    f"workload volume {WORKLOAD_REPOSITORY_VOLUME!r} is reserved "
                    "for the managed repository PVC"
                )
                raise ValueError(msg)
            found = True
        out.append(volume)
    if not found:
        out.append(repository)
    return tuple(out)


def _bootstrap_container(
    container: ContainerSpec,
    *,
    mount: VolumeMountSpec,
    env: Mapping[str, str],
    script: str,
) -> ContainerSpec:
    command = _explicit_command(container)
    args = _command(
        container.args or (),
        label=f"container {container.name!r} args",
        allow_empty=True,
    )
    return replace(
        container,
        command=WORKLOAD_BOOTSTRAP_COMMAND,
        args=(script, WORKLOAD_BOOTSTRAP_ARG0, *command, *args),
        env=_with_env(tuple(container.env), env),
        volume_mounts=_with_repository_mount(tuple(container.volume_mounts), mount),
    )


def _explicit_command(container: ContainerSpec) -> tuple[str, ...]:
    if container.command is None:
        msg = (
            f"workload container {container.name!r} requires an explicit command "
            "before repository bootstrap"
        )
        raise ValueError(msg)
    return _command(container.command, label=f"container {container.name!r} command")


def _with_env(
    existing: tuple[EnvVarSpec, ...],
    env: Mapping[str, str],
) -> tuple[EnvVarSpec, ...]:
    out = list(existing)
    for key, value in env.items():
        current = next((var for var in out if var.name == key), None)
        if current is None:
            out.append(EnvVarSpec(name=key, value=value))
        elif not _same_literal_env(current, value):
            msg = f"workload environment variable {key!r} is reserved by Bertrand"
            raise ValueError(msg)
    return tuple(out)


def _same_literal_env(var: EnvVarSpec, value: str) -> bool:
    return (
        var.value == value
        and var.field_path is None
        and var.secret_name is None
        and var.secret_key is None
        and var.config_map_name is None
        and var.config_map_key is None
    )


def _with_repository_mount(
    existing: tuple[VolumeMountSpec, ...],
    mount: VolumeMountSpec,
) -> tuple[VolumeMountSpec, ...]:
    out = list(existing)
    for current in out:
        if current.mount_path == mount.mount_path:
            if _same_mount(current, mount):
                return tuple(out)
            msg = (
                f"workload mount path {mount.mount_path!r} is reserved for the "
                "managed repository volume"
            )
            raise ValueError(msg)
    out.append(mount)
    return tuple(out)


def _same_mount(left: VolumeMountSpec, right: VolumeMountSpec) -> bool:
    return (
        left.name == right.name
        and left.mount_path == right.mount_path
        and bool(left.read_only) == bool(right.read_only)
        and left.sub_path == right.sub_path
    )


def _with_workload_labels(
    existing: Mapping[str, str],
    labels: Mapping[str, str],
) -> dict[str, str]:
    out = dict(existing)
    for key, value in labels.items():
        current = out.get(key)
        if current is not None and current != value:
            msg = f"workload label {key!r} is reserved by Bertrand"
            raise ValueError(msg)
        out[key] = value
    return out


def _label_hash(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()[:16]


def _bootstrap_script(repository: WorkloadRepository) -> str:
    repo_root = shlex.quote(PROJECT_MOUNT.as_posix())
    worktree = shlex.quote(repository.worktree_env)
    target = shlex.quote(repository.target_path.as_posix())
    worktree_mount = shlex.quote(WORKTREE_MOUNT.as_posix())
    return "\n".join(
        (
            "set -eu",
            f"REPO_ROOT={repo_root}",
            f"WORKTREE={worktree}",
            f"TARGET_WORKTREE={target}",
            'if [ ! -d "$REPO_ROOT" ]; then',
            '    echo "Bertrand repository mount is missing: $REPO_ROOT" >&2',
            "    exit 1",
            "fi",
            'if [ ! -d "$TARGET_WORKTREE" ]; then',
            ('    echo "Bertrand worktree $WORKTREE is missing from $REPO_ROOT" >&2'),
            "    exit 1",
            "fi",
            "cd /",
            f"if [ -e {worktree_mount} ] && [ ! -L {worktree_mount} ]; then",
            (
                f"    echo \"Bertrand worktree mount exists and is not a symlink: "
                f"{worktree_mount}\" >&2"
            ),
            "    exit 1",
            "fi",
            f"rm -f {worktree_mount}",
            f"ln -s \"$TARGET_WORKTREE\" {worktree_mount}",
            "if command -v git >/dev/null 2>&1; then",
            (
                "    git config --global --add safe.directory "
                f"{worktree_mount} >/dev/null 2>&1 || true"
            ),
            (
                '    git config --global --add safe.directory "$TARGET_WORKTREE" '
                ">/dev/null 2>&1 || true"
            ),
            "fi",
            f"cd {worktree_mount}",
            'exec "$@"',
        )
    )
