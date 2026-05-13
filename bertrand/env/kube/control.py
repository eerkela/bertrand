"""Shared Kubernetes control-plane image configuration."""

from __future__ import annotations

import os

CONTROL_PLANE_IMAGE_ENV = "BERTRAND_CONTROL_PLANE_IMAGE"


def control_plane_image() -> str:
    """Return the configured Bertrand control-plane image.

    Returns
    -------
    str
        Container image used by in-cluster Bertrand control-plane workloads.

    Raises
    ------
    ValueError
        If the image reference is missing or empty.
    """
    raw = os.environ.get(CONTROL_PLANE_IMAGE_ENV)
    if raw is None:
        msg = f"{CONTROL_PLANE_IMAGE_ENV} must be set to a pre-existing image"
        raise ValueError(msg)
    image = raw.strip()
    if not image:
        msg = f"{CONTROL_PLANE_IMAGE_ENV} cannot be empty"
        raise ValueError(msg)
    return image
