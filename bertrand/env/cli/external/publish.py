"""The external CLI endpoint for building Bertrand images."""

from __future__ import annotations

import re
from typing import TYPE_CHECKING

from bertrand.env.config import DEFAULT_TAG, Bertrand, PyProject
from bertrand.env.git import NORMALIZE_ARCH
from bertrand.env.legacy.environment import Environment
from bertrand.env.legacy.nerdctl import TIMEOUT, nerdctl

if TYPE_CHECKING:
    from pathlib import Path

    from bertrand.env.legacy.image import Image


def _normalize_version(value: str) -> str:
    out = value.strip()
    if not out:
        msg = "version cannot be empty"
        raise ValueError(msg)
    if out.startswith("v") and len(out) > 1:
        out = out[1:]
    if not out:
        msg = "version cannot be empty"
        raise ValueError(msg)
    return out


def _normalize_arch(value: str) -> str:
    arch = value.strip().lower()
    if not arch:
        msg = "architecture cannot be empty"
        raise ValueError(msg)
    return NORMALIZE_ARCH.get(arch, re.sub(r"[^a-z0-9._-]+", "-", arch).strip("-"))


def _parse_manifest_arches(value: str | None) -> list[str]:
    if value is None:
        msg = "--manifest requires --manifest-arches"
        raise ValueError(msg)
    raw = value.strip()
    if not raw:
        msg = "--manifest-arches cannot be empty"
        raise ValueError(msg)
    out: list[str] = []
    seen: set[str] = set()
    for token in raw.split(","):
        arch = _normalize_arch(token)
        if not arch:
            msg = f"invalid architecture in --manifest-arches: {token!r}"
            raise ValueError(msg)
        if arch in seen:
            continue
        seen.add(arch)
        out.append(arch)
    if not out:
        msg = "--manifest-arches must include at least one architecture"
        raise ValueError(msg)
    return out


async def bertrand_publish(
    worktree: Path,
    *,
    repo: str,
    version: str | None,
    manifest: bool,
    manifest_arches: str | None,
) -> str | None:
    """Build and publish Bertrand images for all declared tags in an environment.

    Parameters
    ----------
    worktree : Path
        A valid environment worktree path.
    repo : str
        Remote OCI repository where tags/manifests should be published.
    version : str | None
        Optional release version to enforce. Accepts `X.Y.Z` or `vX.Y.Z`.
    manifest : bool
        If True, assemble and publish multi-arch manifests only.
    manifest_arches : str | None
        Comma-separated architectures for manifest assembly. Required when
        `manifest=True`.

    Returns
    -------
    str | None
        Normalized host architecture in build mode (`manifest=False`), otherwise
        None.

    Raises
    ------
    ValueError
        If repository/version/manifest architecture inputs are invalid.
    OSError
        If publish prerequisites fail or runtime publish operations fail.
    """
    repo = repo.strip().lower()
    if not repo:
        msg = "OCI repository must be non-empty when provided"
        raise ValueError(msg)

    async with await Environment.load(worktree, timeout=TIMEOUT) as env:
        bertrand = env.config.get(Bertrand)
        python = env.config.get(PyProject)
        if python is None:
            msg = "could not determine project version for publish"
            raise OSError(msg)
        if bertrand is None:
            msg = "could not determine configured tags for publish"
            raise OSError(msg)
        if not bertrand.image:
            msg = "publish requires at least one configured tag"
            raise OSError(msg)

        project_version = _normalize_version(python.project.version)
        publish_version = project_version
        if version is not None:
            publish_version = _normalize_version(version)
            if publish_version != project_version:
                msg = (
                    f"publish version '{version}' does not match project version "
                    f"'{project_version}'"
                )
                raise OSError(msg)

        if not manifest:
            arch = _normalize_arch(
                (
                    await nerdctl(
                        ["info", "--format", "{{.Host.Arch}}"],
                        capture_output=True,
                    )
                ).stdout
            )
            if not arch:
                msg = "could not determine host architecture from `nerdctl info` output"
                raise OSError(msg)

            built: dict[str, Image] = {}
            for current_tag in bertrand.image:
                try:
                    built[current_tag] = await env.build(current_tag, quiet=False)
                except Exception as err:
                    msg = f"failed to build tag '{current_tag}' for publish"
                    raise OSError(msg) from err

            for current_tag in bertrand.image:
                suffix = "" if current_tag == DEFAULT_TAG else f"-{current_tag}"
                image = built[current_tag]
                ref = f"{repo}:{publish_version}{suffix}-{arch}"
                await nerdctl(["tag", image.id, ref], capture_output=True)
                await nerdctl(["push", ref], attempts=3, capture_output=True)
            return arch

        arches = _parse_manifest_arches(manifest_arches)
        for current_tag in bertrand.image:
            suffix = "" if current_tag == DEFAULT_TAG else f"-{current_tag}"
            manifest_ref = f"{repo}:{publish_version}{suffix}"
            source_refs = [f"{manifest_ref}-{arch}" for arch in arches]
            for ref in source_refs:
                await nerdctl(
                    ["manifest", "inspect", f"docker://{ref}"],
                    attempts=3,
                    capture_output=True,
                )
            try:
                await nerdctl(
                    ["manifest", "rm", manifest_ref],
                    check=False,
                    capture_output=True,
                )
                await nerdctl(["manifest", "create", manifest_ref], capture_output=True)
                for ref in source_refs:
                    await nerdctl(
                        ["manifest", "add", manifest_ref, f"docker://{ref}"],
                        attempts=3,
                        capture_output=True,
                    )
                await nerdctl(
                    [
                        "manifest",
                        "push",
                        "--all",
                        manifest_ref,
                        f"docker://{manifest_ref}",
                    ],
                    attempts=3,
                    capture_output=True,
                )
            finally:
                await nerdctl(
                    ["manifest", "rm", manifest_ref],
                    check=False,
                    capture_output=True,
                )
        return None
