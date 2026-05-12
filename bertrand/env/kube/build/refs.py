"""Image reference helpers for Bertrand's Kubernetes build runtime."""

from __future__ import annotations

import re
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence

IMAGE_REF_COMPONENT_RE = re.compile(r"^[a-z0-9]+(?:(?:[._]|__|[-]*)[a-z0-9]+)*$")
IMAGE_TAG_RE = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_.-]{0,127}$")
DIGEST_RE = re.compile(r"^sha256:[0-9a-f]{64}$")
DIGEST_REF_RE = re.compile(r"^.+@sha256:[0-9a-f]{64}$")
_PLATFORM_TOKEN_RE = re.compile(r"[^a-z0-9]+")
_RUN_ID_TOKEN_RE = re.compile(r"[^a-f0-9]+")


def validate_tag(tag: str, *, label: str = "image tag") -> str:
    """Validate one OCI tag supported by Bertrand.

    Parameters
    ----------
    tag : str
        Candidate tag value.
    label : str, optional
        Human-readable field label for diagnostics.

    Returns
    -------
    str
        Trimmed tag value.

    Raises
    ------
    ValueError
        If `tag` is empty or outside Bertrand's supported tag syntax.
    """
    normalized = tag.strip()
    if not normalized:
        msg = f"{label} cannot be empty"
        raise ValueError(msg)
    if not IMAGE_TAG_RE.fullmatch(normalized):
        msg = f"invalid {label}: {tag!r}"
        raise ValueError(msg)
    return normalized


def validate_digest(digest: str, *, label: str = "image digest") -> str:
    """Validate one sha256 image digest.

    Parameters
    ----------
    digest : str
        Candidate digest value.
    label : str, optional
        Human-readable field label for diagnostics.

    Returns
    -------
    str
        Trimmed sha256 digest.

    Raises
    ------
    ValueError
        If `digest` is not a sha256 digest.
    """
    normalized = digest.strip()
    if not DIGEST_RE.fullmatch(normalized):
        msg = f"{label} must be a sha256 digest: {digest!r}"
        raise ValueError(msg)
    return normalized


def split_tagged_ref(ref: str, *, label: str = "image reference") -> tuple[str, str]:
    """Split a tagged mutable image reference.

    Parameters
    ----------
    ref : str
        Mutable image reference with an explicit tag.
    label : str, optional
        Human-readable field label for diagnostics.

    Returns
    -------
    tuple[str, str]
        Repository reference and tag.

    Raises
    ------
    ValueError
        If `ref` is empty, digest-pinned, or missing an explicit tag.
    """
    value = ref.strip()
    if not value:
        msg = f"{label} cannot be empty"
        raise ValueError(msg)
    if "@" in value:
        msg = f"{label} must be a tagged mutable ref, not a digest ref: {ref!r}"
        raise ValueError(msg)
    slash = value.rfind("/")
    colon = value.rfind(":")
    if colon <= slash:
        msg = f"{label} must include a tag: {ref!r}"
        raise ValueError(msg)
    repository = value[:colon]
    tag = validate_tag(value[colon + 1 :], label=f"{label} tag")
    if not repository:
        msg = f"{label} must include a repository: {ref!r}"
        raise ValueError(msg)
    return repository, tag


def tagged_repository(ref: str, *, label: str = "image reference") -> str:
    """Return the repository portion of a tagged image reference.

    Parameters
    ----------
    ref : str
        Mutable image reference with an explicit tag.
    label : str, optional
        Human-readable field label for diagnostics.

    Returns
    -------
    str
        Repository portion before the tag separator.
    """
    repository, _ = split_tagged_ref(ref, label=label)
    return repository


def validate_tagged_ref(ref: str | None, *, label: str = "image reference") -> str:
    """Validate a tagged mutable image reference.

    Parameters
    ----------
    ref : str | None
        Candidate mutable image reference.
    label : str, optional
        Human-readable field label for diagnostics.

    Returns
    -------
    str
        Trimmed image reference.

    Raises
    ------
    ValueError
        If `ref` is empty, digest-pinned, or missing an explicit tag.
    """
    value = (ref or "").strip()
    try:
        split_tagged_ref(value, label=label)
    except ValueError as err:
        raise ValueError(str(err)) from err
    return value


def replace_tag(ref: str, tag: str, *, label: str = "image reference") -> str:
    """Replace the tag on a mutable image reference.

    Parameters
    ----------
    ref : str
        Mutable image reference with an explicit tag.
    tag : str
        Replacement tag.
    label : str, optional
        Human-readable field label for diagnostics.

    Returns
    -------
    str
        Image reference with the replacement tag.
    """
    repository, _ = split_tagged_ref(ref, label=label)
    return f"{repository}:{validate_tag(tag, label='image channel tag')}"


def digest_ref(image: str, digest: str) -> str:
    """Render an immutable digest-pinned image reference.

    Parameters
    ----------
    image : str
        Mutable tagged ref or existing digest-pinned ref.
    digest : str
        sha256 digest to pin.

    Returns
    -------
    str
        Repository reference pinned to `digest`.

    Raises
    ------
    ValueError
        If the repository or digest is invalid.
    """
    value = image.strip()
    if "@" in value:
        repository = value.rsplit("@", 1)[0]
    else:
        repository, _ = split_tagged_ref(value)
    if not repository:
        msg = f"cannot derive digest reference from image {image!r}"
        raise ValueError(msg)
    return f"{repository}@{validate_digest(digest)}"


def digest_from_ref(ref: str, *, label: str = "image digest ref") -> str:
    """Extract a sha256 digest from an immutable image reference.

    Parameters
    ----------
    ref : str
        Digest-pinned image reference.
    label : str, optional
        Human-readable field label for diagnostics.

    Returns
    -------
    str
        sha256 digest portion of `ref`.

    Raises
    ------
    ValueError
        If `ref` is not digest-pinned to a sha256 digest.
    """
    value = ref.strip()
    if not DIGEST_REF_RE.fullmatch(value):
        msg = f"{label} must include a sha256 digest: {ref!r}"
        raise ValueError(msg)
    return validate_digest(value.rpartition("@")[2], label=label)


def channel_refs(image: str, channels: Sequence[str]) -> dict[str, str]:
    """Derive tagged image refs for moving channel names.

    Parameters
    ----------
    image : str
        Versioned mutable image ref.
    channels : Sequence[str]
        Channel tag names.

    Returns
    -------
    dict[str, str]
        Mapping from channel name to mutable image ref.
    """
    return {channel: replace_tag(image, channel) for channel in channels}


def platform_token(platform: str) -> str:
    """Normalize an OCI platform name for use in a temporary tag.

    Parameters
    ----------
    platform : str
        OCI platform string such as ``"linux/amd64"``.

    Returns
    -------
    str
        Lowercase token safe for inclusion in an OCI tag.

    Raises
    ------
    ValueError
        If `platform` is empty.
    """
    token = _PLATFORM_TOKEN_RE.sub("-", platform.strip().lower()).strip("-")
    if not token:
        msg = "BuildKit platform cannot be empty"
        raise ValueError(msg)
    return token[:48].rstrip("-") or "platform"


def platform_output_ref(image: str, platform: str, run_id: str) -> str:
    """Render a temporary per-platform BuildKit output ref.

    Parameters
    ----------
    image : str
        Final mutable image reference.
    platform : str
        OCI platform string assigned to the build.
    run_id : str
        Stable run identifier used to avoid tag collisions.

    Returns
    -------
    str
        Mutable temporary image ref for one platform build.

    Raises
    ------
    ValueError
        If `image`, `platform`, or `run_id` is invalid.
    """
    repository, tag = split_tagged_ref(image, label="BuildKit image reference")
    platform_part = platform_token(platform)
    token = _RUN_ID_TOKEN_RE.sub("", run_id.lower()).strip()
    if not token:
        msg = "BuildKit platform output run ID cannot be empty"
        raise ValueError(msg)
    suffix = f"{platform_part}-{token}"
    max_prefix = max(1, 127 - len(suffix))
    tag_prefix = tag[:max_prefix].rstrip("._-") or "image"
    return f"{repository}:{tag_prefix}-{suffix}"


def rewrite_registry_ref(
    ref: str,
    *,
    source: str,
    target: str,
    canonical_host: str,
) -> str:
    """Rewrite a Bertrand registry reference between equivalent hosts.

    Parameters
    ----------
    ref : str
        Image reference rooted at `source` or `target`.
    source : str
        Accepted source registry host.
    target : str
        Registry host to render.
    canonical_host : str
        Canonical host used in diagnostics.

    Returns
    -------
    str
        Image reference rooted at `target`.

    Raises
    ------
    ValueError
        If `ref` is empty or rooted at another registry.
    """
    normalized = ref.strip()
    if not normalized:
        msg = "image reference cannot be empty"
        raise ValueError(msg)
    source_prefix = f"{source}/"
    target_prefix = f"{target}/"
    if normalized.startswith(target_prefix):
        return normalized
    if normalized.startswith(source_prefix):
        return f"{target_prefix}{normalized[len(source_prefix) :]}"
    msg = f"image reference {ref!r} does not belong to registry {canonical_host!r}"
    raise ValueError(msg)


def validate_channel_refs(
    refs: Mapping[str, str] | None,
    *,
    label: str,
) -> dict[str, str]:
    """Validate a mapping of named mutable channel refs.

    Parameters
    ----------
    refs : Mapping[str, str] | None
        Optional channel-name to image-ref mapping.
    label : str
        Human-readable channel label for diagnostics.

    Returns
    -------
    dict[str, str]
        Sorted channel mapping with validated tagged refs.

    Raises
    ------
    ValueError
        If any channel name or image reference is invalid.
    """
    if refs is None:
        return {}
    out: dict[str, str] = {}
    for raw_name, raw_ref in refs.items():
        name = str(raw_name).strip()
        if not name:
            msg = f"{label} name cannot be empty"
            raise ValueError(msg)
        if name in out:
            msg = f"duplicate {label} name: {name!r}"
            raise ValueError(msg)
        out[name] = validate_tagged_ref(raw_ref, label=f"{label} {name!r}")
    return dict(sorted(out.items()))
