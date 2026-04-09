"""Runtime build/create argument assembly for Bertrand's Kubernetes runtime.

This module consumes validated config models and emits concrete `nerdctl`
argument bundles for image builds and container creates.  It also owns
build-time capability staging and generated Containerfile rendering used by the
runtime bootstrap flow.
"""
from __future__ import annotations

import base64
import binascii
import json
import os
import re
import shlex
import shutil
import sys
import uuid
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path, PosixPath
from typing import Literal, Protocol

import jinja2
import packaging.version

from ..config import Bertrand, Config, PyProject
from ..config.core import (
    NonEmpty,
    NoWhiteSpace,
    OCIImageRef,
    TOMLKey,
    Trimmed,
    locate_template,
)
from ..run import (
    BERTRAND_ENV,
    BERTRAND_NAMESPACE,
    CONTAINER_ID_ENV,
    CONTAINER_RUNTIME_ENV,
    CONTAINER_RUNTIME_MOUNT,
    ENV_ID_ENV,
    IMAGE_ID_ENV,
    IMAGE_TAG_ENV,
    METADATA_DIR,
    PROJECT_ENV,
    PROJECT_MOUNT,
    TIMEOUT,
    TOOLS_TMP_DIR,
    WORKTREE_ENV,
    WORKTREE_MOUNT,
    Scalar,
    atomic_write_text,
    inside_image,
    kubectl,
)
from ..version import VERSION
from .volume import collect_mount_specs, format_volumes, gc_volumes


class _NetworkTableLike(Protocol):
    """Structural type for network table argument emitters."""

    mode: str
    options: list[str]
    dns: list[str]
    dns_search: list[str]
    dns_options: list[str]
    add_host: dict[str, str]


def _dependency_copy_specs(from_images: Sequence[OCIImageRef]) -> list[dict[str, str]]:
    out: list[dict[str, str]] = []
    for index, image_ref in enumerate(from_images, start=1):
        token = re.sub(r"[^a-z0-9]+", "-", image_ref.lower()).strip("-")
        if not token:
            token = "dependency"
        token = token[:64].rstrip("-")
        target = f"/opt/bertrand/deps/{index:02d}-{token}"
        out.append({"image": image_ref, "target": target})
    return out


def render_containerfile(
    model: Bertrand.Model,
    tag: TOMLKey,
    *,
    build_mounts: Sequence[str] = (),
) -> str:
    """Render a generated Containerfile for one build tag."""

    build = model.build.get(tag)
    if build is None:
        raise ValueError(f"unknown build tag '{tag}'")
    if build.containerfile is not None:
        raise ValueError(
            f"cannot render generated Containerfile for tag '{tag}' when a custom "
            "`containerfile` is configured"
        )

    jinja = jinja2.Environment(
        autoescape=False,
        undefined=jinja2.StrictUndefined,
        keep_trailing_newline=True,
        trim_blocks=False,
        lstrip_blocks=False,
    )
    template = jinja.from_string(
        locate_template("core", "containerfile.v1").read_text(encoding="utf-8")
    )
    bertrand_version = packaging.version.parse(VERSION.bertrand)
    python_version = packaging.version.parse(VERSION.python)
    try:
        page_size_kib = os.sysconf("SC_PAGE_SIZE") // 1024
    except (AttributeError, ValueError, OSError):
        page_size_kib = 4
    return template.render(
        python_major=python_version.major,
        python_minor=python_version.minor,
        python_patch=python_version.micro,
        bertrand_major=bertrand_version.major,
        bertrand_minor=bertrand_version.minor,
        bertrand_patch=bertrand_version.micro,
        cpus=0,
        page_size_kib=page_size_kib,
        env_mount=str(WORKTREE_MOUNT),
        build_mounts=list(build_mounts),
        dependency_copies=_dependency_copy_specs(build.from_),
    )


def _format_build_args(build_args: dict[str, Scalar]) -> list[str]:
    args: list[str] = []
    for key, value in build_args.items():
        args.extend(["--build-arg", f"{key}={value}"])
    return args


def _format_network(network: _NetworkTableLike) -> list[str]:
    args: list[str] = ["--network"]
    if network.options:
        args.append(f"{network.mode}:{','.join(network.options)}")
    else:
        args.append(network.mode)
    for dns in network.dns:
        args.extend(["--dns", dns])
    for search in network.dns_search:
        args.extend(["--dns-search", search])
    for option in network.dns_options:
        args.extend(["--dns-option", option])
    for host in sorted(network.add_host):
        args.extend(["--add-host", f"{host}:{network.add_host[host]}"])
    return args


def _format_cpus(cpus: float) -> list[str]:
    return ["--cpus", str(cpus)] if cpus > 0 else []


async def _resolve_scope(config: Config) -> str:
    if config.worktree.parts:
        scope = re.sub(r"[^a-zA-Z0-9._]+", "-", config.worktree.as_posix()).strip("-")
        if scope:
            return scope
    branch = await config.repo.head_branch()
    if branch:
        scope = re.sub(r"[^a-zA-Z0-9._]+", "-", branch).strip("-")
        if scope:
            return scope
    return "detached"


@dataclass(frozen=True)
class ImageArgs:
    """A full argument tail and metadata for `nerdctl build`."""

    argv: list[str]
    run_id: str
    image_name: str
    iid_file: Path
    containerfile: Path


async def image_args(
    config: Config,
    *,
    env_id: str,
    tag: TOMLKey,
) -> ImageArgs:
    """Assemble runtime image build arguments from config state."""

    if inside_image():
        raise RuntimeError("image_args() cannot be called from within a container")
    if not config:
        raise RuntimeError("image_args() requires an active config context")
    env_id = env_id.strip()
    if not env_id:
        raise ValueError("environment ID cannot be empty")

    python = config.get(PyProject)
    if python is None:
        raise OSError(
            f"missing 'python' configuration for environment at {config.root}"
        )
    bertrand = config.get(Bertrand)
    if bertrand is None:
        raise OSError(
            f"missing 'bertrand' configuration for environment at {config.root}"
        )
    build = bertrand.build.get(tag)
    if build is None:
        raise ValueError(
            f"unknown build tag '{tag}' for environment at {config.root}"
        )

    try:
        await gc_volumes(config, env_id)
    except Exception:
        pass

    run_id = uuid.uuid4().hex
    scope = await _resolve_scope(config)
    image_name = f"{python.project.name}.{scope}.{tag}.{run_id[:7]}"
    iid_file = config.root / METADATA_DIR / "images" / tag / "iid"
    iid_file.parent.mkdir(parents=True, exist_ok=True)

    if build.containerfile is None:
        containerfile = config.root / METADATA_DIR / "images" / tag / "Containerfile"
        containerfile.parent.mkdir(parents=True, exist_ok=True)
        build_mounts: list[str] = [
            f"--mount=type=cache,id={volume_name},target={volume_target},sharing=locked"
            for volume_name, volume_target in await collect_mount_specs(config, tag)
        ]

        atomic_write_text(
            containerfile,
            render_containerfile(
                bertrand,
                tag,
                build_mounts=build_mounts,
            ),
            encoding="utf-8",
        )
    else:
        containerfile = config.root / build.containerfile

    argv = [
        "-t",
        image_name,
        "--file",
        str(containerfile),
        "--iidfile",
        str(iid_file),
        "--label",
        f"{BERTRAND_ENV}=1",
        "--label",
        f"{ENV_ID_ENV}={env_id}",
        "--label",
        f"{IMAGE_TAG_ENV}={tag}",
        *_format_build_args(build.args),
        *_format_network(bertrand.network.build),
        str(config.root),
    ]
    return ImageArgs(
        argv=argv,
        run_id=run_id,
        image_name=image_name,
        iid_file=iid_file,
        containerfile=containerfile,
    )


def _render_bootstrap_script(
    *,
    container_cid: PosixPath,
    container_worktree: PosixPath,
    container_runtime: PosixPath,
) -> str:
    return "\n".join(
        [
            "#!/bin/sh",
            "set -eu",
            f"CID_FILE={shlex.quote(str(container_cid))}",
            f"TARGET_WORKTREE={shlex.quote(str(container_worktree))}",
            f"TARGET_RUNTIME={shlex.quote(str(container_runtime))}",
            f"rm -rf {shlex.quote(str(WORKTREE_MOUNT))}",
            (
                "ln -s "
                "\"$TARGET_WORKTREE\" "
                f"{shlex.quote(str(WORKTREE_MOUNT))}"
            ),
            f"rm -rf {shlex.quote(str(CONTAINER_RUNTIME_MOUNT))}",
            (
                "ln -s "
                "\"$TARGET_RUNTIME\" "
                f"{shlex.quote(str(CONTAINER_RUNTIME_MOUNT))}"
            ),
            "if command -v git >/dev/null 2>&1; then",
            (
                "    git config --global --add safe.directory "
                f"{shlex.quote(str(WORKTREE_MOUNT))} >/dev/null 2>&1 || true"
            ),
            (
                "    git config --global --add safe.directory "
                "\"$TARGET_WORKTREE\" >/dev/null 2>&1 || true"
            ),
            "fi",
            "if [ -f \"$CID_FILE\" ]; then",
            "    CID=\"$(cat \"$CID_FILE\" 2>/dev/null || true)\"",
            "    if [ -n \"$CID\" ]; then",
            f"        export {CONTAINER_ID_ENV}=\"$CID\"",
            "    fi",
            "fi",
            "exec \"$@\"",
            "",
        ]
    )


@dataclass(frozen=True)
class ContainerArgs:
    """A full argument tail and metadata for `nerdctl container create`."""

    argv: list[str]
    run_id: str
    runtime_dir: Path
    cid_file: Path
    bootstrap_script: Path


async def container_args(
    config: Config,
    *,
    env_id: str,
    tag: TOMLKey,
    image_id: str,
    cmd: Sequence[NonEmpty[Trimmed]] = (),
    env_vars: Mapping[NonEmpty[NoWhiteSpace], Trimmed] | None = None,
) -> ContainerArgs:
    """Assemble runtime container create arguments from config state."""

    if inside_image():
        raise RuntimeError("container_args() cannot be called from within a container")
    env_id = env_id.strip()
    if not env_id:
        raise ValueError("environment ID cannot be empty when forming container args")
    image_id = image_id.strip()
    if not image_id:
        raise ValueError("image ID cannot be empty when forming container args")

    bertrand = config.get(Bertrand)
    if bertrand is None:
        raise TypeError(
            f"missing 'bertrand' configuration for environment at {config.root}"
        )
    workload = bertrand.workload.get(tag)
    if workload is None:
        raise ValueError(
            f"unknown workload tag '{tag}' for environment at {config.root}"
        )
    if cmd:
        _cmd: list[str] = []
        for part in cmd:
            part = part.strip()
            if not part:
                raise ValueError("entry point arguments must be non-empty strings")
            _cmd.append(part)
        cmd = _cmd
    else:
        cmd = workload.cmd
        if not cmd:
            raise ValueError(
                f"tag '{tag}' has no effective entry point: provide a command "
                f"override or configure [tool.bertrand.workload.{tag}.cmd] for this "
                "tag"
            )

    run_id = uuid.uuid4().hex
    runtime = METADATA_DIR / "containers" / f"{tag}.{run_id}"
    host_runtime_dir = config.root / runtime
    host_runtime_dir.mkdir(parents=True, exist_ok=True)
    host_cid = host_runtime_dir / "cid"
    host_bootstrap = host_runtime_dir / "entrypoint.sh"
    if config.worktree.parts:
        worktree_env = config.worktree.as_posix()
        container_worktree = PROJECT_MOUNT / worktree_env
    else:
        worktree_env = "."
        container_worktree = PROJECT_MOUNT
    container_runtime = container_worktree / runtime
    container_cid = container_runtime / "cid"
    container_bootstrap = container_runtime / "entrypoint.sh"
    atomic_write_text(
        host_bootstrap,
        _render_bootstrap_script(
            container_cid=container_cid,
            container_worktree=container_worktree,
            container_runtime=container_runtime,
        ),
        encoding="utf-8",
    )
    host_bootstrap.chmod(0o755)

    argv = [
        "--init",
        "--rm",
        "--cidfile",
        str(host_cid),
        "--label",
        f"{BERTRAND_ENV}=1",
        "--label",
        f"{PROJECT_ENV}={config.repo.root}",
        "--label",
        f"{WORKTREE_ENV}={worktree_env}",
        "--label",
        f"{CONTAINER_RUNTIME_ENV}={runtime}",
        "--label",
        f"{ENV_ID_ENV}={env_id}",
        "--label",
        f"{IMAGE_ID_ENV}={image_id}",
        "--label",
        f"{IMAGE_TAG_ENV}={tag}",
        "-v",
        f"{config.repo.root}:{PROJECT_MOUNT}",
        "-e",
        f"{BERTRAND_ENV}=1",
        "-e",
        f"{PROJECT_ENV}={config.repo.root}",
        "-e",
        f"{WORKTREE_ENV}={worktree_env}",
        "-e",
        f"{CONTAINER_RUNTIME_ENV}={runtime}",
        "-e",
        f"{ENV_ID_ENV}={env_id}",
        "-e",
        f"{IMAGE_ID_ENV}={image_id}",
        "-e",
        f"{IMAGE_TAG_ENV}={tag}",
    ]
    if env_vars:
        for key, value in sorted(env_vars.items()):
            key = key.strip()
            if not key or any(c.isspace() for c in key):
                raise ValueError(
                    "environment variable keys must be non-empty strings without "
                    f"whitespace: {key!r}"
                )
            argv.extend(["-e", f"{key}={value.strip()}"])
    argv.extend(
        [
            *(await format_volumes(config, tag, env_id)),
            *_format_network(bertrand.network.run),
            *_format_cpus(workload.cpus),
            "--entrypoint",
            str(container_bootstrap),
            image_id,
            *cmd,
        ]
    )
    return ContainerArgs(
        argv=argv,
        run_id=run_id,
        runtime_dir=runtime,
        cid_file=runtime / "cid",
        bootstrap_script=runtime / "entrypoint.sh",
    )


def _capability_token(value: str) -> str:
    token = re.sub(r"[^a-z0-9_]+", "_", value.strip().lower()).strip("_")
    if not token:
        raise ValueError("capability ID cannot be empty")
    return token


def _capability_secret_name(
    *,
    env_id: str,
    kind: Literal["ssh", "secret"],
    capability_id: str,
) -> str:
    env_token = re.sub(r"[^a-z0-9]+", "", env_id.strip().lower())[:12]
    if not env_token:
        raise ValueError("environment ID cannot be empty")
    capability_token = _capability_token(capability_id).replace("_", "-")
    return f"bertrand-{env_token}-{kind}-{capability_token}"[:253]


async def _cluster_secret_data(
    *,
    name: str,
    timeout: float,
) -> dict[str, str] | None:
    result = await kubectl(
        [
            "get",
            "secret",
            name,
            "-o",
            "json",
        ],
        check=False,
        capture_output=True,
        timeout=timeout,
    )
    if result.returncode != 0:
        return None
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as err:
        raise OSError(
            f"cluster secret '{name}' returned malformed JSON payload"
        ) from err
    raw = payload.get("data", {})
    if not isinstance(raw, dict):
        raise OSError(f"cluster secret '{name}' is missing a valid data mapping")
    out: dict[str, str] = {}
    for key, value in raw.items():
        if isinstance(key, str) and isinstance(value, str):
            out[key] = value
    return out


def _decode_cluster_secret(
    *,
    name: str,
    data: Mapping[str, str],
    preferred_keys: Sequence[str],
) -> bytes:
    keys = [key for key in preferred_keys if key in data]
    if not keys and len(data) == 1:
        keys = list(data)
    if not keys:
        raise OSError(
            f"cluster secret '{name}' does not contain a recognized data key"
        )
    key = keys[0]
    try:
        return base64.b64decode(data[key], validate=True)
    except (binascii.Error, ValueError) as err:
        raise OSError(
            f"cluster secret '{name}' contains invalid base64 data for key '{key}'"
        ) from err


def _device_env_var(capability_id: str) -> str:
    token = re.sub(r"[^A-Za-z0-9]+", "_", capability_id.strip().upper()).strip("_")
    if not token:
        raise ValueError("capability ID cannot be empty")
    return f"BERTRAND_DEVICE_{token}"


def cleanup_capability_dir(path: Path | None, *, suppress_errors: bool = True) -> None:
    """Remove staged build capability material."""

    if path is None:
        return
    try:
        shutil.rmtree(path)
    except OSError:
        if not suppress_errors:
            raise


def _warn_optional(kind: str, capability_id: str) -> None:
    print(
        f"bertrand: optional {kind} capability '{capability_id}' was not found; "
        "continuing without it",
        file=sys.stderr,
    )


def _stage_payload(
    *,
    capability_dir: Path,
    category: Literal["secrets", "ssh"],
    capability_id: str,
    payload: bytes,
) -> Path:
    target = capability_dir / category / _capability_token(capability_id)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(payload)
    target.chmod(0o600)
    return target


async def _resolve_cluster_payload(
    *,
    env_id: str,
    timeout: float,
    kind: Literal["secret", "ssh"],
    capability_id: str,
    preferred_keys: Sequence[str],
) -> bytes | None:
    secret_name = _capability_secret_name(
        env_id=env_id,
        kind=kind,
        capability_id=capability_id,
    )
    secret_data = await _cluster_secret_data(name=secret_name, timeout=timeout)
    if secret_data is None:
        return None
    return _decode_cluster_secret(
        name=secret_name,
        data=secret_data,
        preferred_keys=preferred_keys,
    )


async def build_capability_flags(
    *,
    env_id: str,
    tag: TOMLKey,
    build: Bertrand.Model.Build,
    timeout: float = TIMEOUT,
) -> tuple[list[str], Path | None]:
    """Resolve build capability flags and stage temporary payload files."""

    del tag
    run_id = uuid.uuid4().hex
    capability_dir: Path | None = None
    flags: list[str] = []

    try:
        for req in build.secrets:
            payload = await _resolve_cluster_payload(
                env_id=env_id,
                timeout=timeout,
                kind="secret",
                capability_id=req.id,
                preferred_keys=("value", "secret", req.id),
            )
            if payload is None:
                if req.required:
                    secret_name = _capability_secret_name(
                        env_id=env_id,
                        kind="secret",
                        capability_id=req.id,
                    )
                    raise OSError(
                        f"missing required build secret capability '{req.id}' "
                        f"(cluster secret '{secret_name}' not found in namespace "
                        f"'{BERTRAND_NAMESPACE}')"
                    )
                _warn_optional("secret", req.id)
                continue
            if capability_dir is None:
                capability_dir = (
                    TOOLS_TMP_DIR / "build-capabilities" / f"{run_id}.{uuid.uuid4().hex}"
                )
                capability_dir.mkdir(parents=True, exist_ok=True)
            target = _stage_payload(
                capability_dir=capability_dir,
                category="secrets",
                capability_id=req.id,
                payload=payload,
            )
            flags.extend(["--secret", f"id={req.id},src={target}"])

        for req in build.ssh:
            payload = await _resolve_cluster_payload(
                env_id=env_id,
                timeout=timeout,
                kind="ssh",
                capability_id=req.id,
                preferred_keys=("private_key", "id_rsa", "value", req.id),
            )
            if payload is None:
                if req.required:
                    secret_name = _capability_secret_name(
                        env_id=env_id,
                        kind="ssh",
                        capability_id=req.id,
                    )
                    raise OSError(
                        f"missing required build ssh capability '{req.id}' "
                        f"(cluster secret '{secret_name}' not found in namespace "
                        f"'{BERTRAND_NAMESPACE}')"
                    )
                _warn_optional("ssh", req.id)
                continue
            if capability_dir is None:
                capability_dir = (
                    TOOLS_TMP_DIR / "build-capabilities" / f"{run_id}.{uuid.uuid4().hex}"
                )
                capability_dir.mkdir(parents=True, exist_ok=True)
            target = _stage_payload(
                capability_dir=capability_dir,
                category="ssh",
                capability_id=req.id,
                payload=payload,
            )
            flags.extend(["--ssh", f"{req.id}={target}"])

        for req in build.devices:
            env_var = _device_env_var(req.id)
            selector = os.environ.get(env_var, "").strip()
            if not selector:
                if req.required:
                    raise OSError(
                        f"missing required build device capability '{req.id}' "
                        f"(set {env_var} to a CDI selector or device path)"
                    )
                _warn_optional("device", req.id)
                continue
            flags.extend(["--device", f"{selector}:{req.permissions}"])

        return flags, capability_dir
    except Exception:
        cleanup_capability_dir(capability_dir)
        raise
