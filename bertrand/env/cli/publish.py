"""The external CLI endpoint for building Bertrand images."""
from __future__ import annotations

import re
from pathlib import Path

from ..config import DEFAULT_TAG, Bertrand, PyProject
from ..kube import Environment, Image
from ..run import NORMALIZE_ARCH, TIMEOUT, nerdctl


def _normalize_version(value: str) -> str:
    out = value.strip()
    if not out:
        raise ValueError("version cannot be empty")
    if out.startswith("v") and len(out) > 1:
        out = out[1:]
    if not out:
        raise ValueError("version cannot be empty")
    return out


def _normalize_arch(value: str) -> str:
    arch = value.strip().lower()
    if not arch:
        raise ValueError("architecture cannot be empty")
    return NORMALIZE_ARCH.get(
        arch,
        re.sub(r"[^a-z0-9._-]+", "-", arch).strip("-")
    )


def _parse_manifest_arches(value: str | None) -> list[str]:
    if value is None:
        raise ValueError("--manifest requires --manifest-arches")
    raw = value.strip()
    if not raw:
        raise ValueError("--manifest-arches cannot be empty")
    out: list[str] = []
    seen: set[str] = set()
    for token in raw.split(","):
        arch = _normalize_arch(token)
        if not arch:
            raise ValueError(f"invalid architecture in --manifest-arches: {token!r}")
        if arch in seen:
            continue
        seen.add(arch)
        out.append(arch)
    if not out:
        raise ValueError("--manifest-arches must include at least one architecture")
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
        raise ValueError("OCI repository must be non-empty when provided")

    async with await Environment.load(worktree, timeout=TIMEOUT) as env:
        bertrand = env.config.get(Bertrand)
        python = env.config.get(PyProject)
        if python is None:
            raise OSError("could not determine project version for publish")
        if bertrand is None:
            raise OSError("could not determine configured tags for publish")
        if not bertrand.build:
            raise OSError("publish requires at least one configured tag")

        project_version = _normalize_version(python.project.version)
        publish_version = project_version
        if version is not None:
            publish_version = _normalize_version(version)
            if publish_version != project_version:
                raise OSError(
                    f"publish version '{version}' does not match project version "
                    f"'{project_version}'"
                )

        if not manifest:
            arch = _normalize_arch((await nerdctl(
                ["info", "--format", "{{.Host.Arch}}"],
                capture_output=True,
            )).stdout)
            if not arch:
                raise OSError(
                    "could not determine host architecture from `nerdctl info` output"
                )

            built: dict[str, Image] = {}
            for current_tag in bertrand.build:
                try:
                    built[current_tag] = await env.build(current_tag, quiet=False)
                except Exception as err:
                    raise OSError(
                        f"failed to build tag '{current_tag}' for publish"
                    ) from err

            for current_tag in bertrand.build:
                suffix = "" if current_tag == DEFAULT_TAG else f"-{current_tag}"
                image = built[current_tag]
                ref = f"{repo}:{publish_version}{suffix}-{arch}"
                await nerdctl(["tag", image.id, ref], capture_output=True)
                await nerdctl(["push", ref], attempts=3, capture_output=True)
            return arch

        arches = _parse_manifest_arches(manifest_arches)
        for current_tag in bertrand.build:
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
                await nerdctl(
                    ["manifest", "create", manifest_ref],
                    capture_output=True
                )
                for ref in source_refs:
                    await nerdctl(
                        ["manifest", "add", manifest_ref, f"docker://{ref}"],
                        attempts=3,
                        capture_output=True
                    )
                await nerdctl(
                    ["manifest", "push", "--all", manifest_ref, f"docker://{manifest_ref}"],
                    attempts=3,
                    capture_output=True
                )
            finally:
                await nerdctl(
                    ["manifest", "rm", manifest_ref],
                    check=False,
                    capture_output=True,
                )
        return None
