"""Kubernetes Dynamic Resource Allocation custom-object wrappers."""

from __future__ import annotations

from typing import TYPE_CHECKING

from pydantic import Field

from bertrand.env.git import EMPTY_MAPPING
from bertrand.env.kube.custom_object import (
    CustomObjectManifest,
    CustomResource,
    custom_resource,
)

if TYPE_CHECKING:
    from collections.abc import Mapping

    from bertrand.env.git import Deadline
    from bertrand.env.kube.api.client import Kube

DRA_GROUP = "resource.k8s.io"
DRA_VERSION = "v1"

DEVICE_CLASS_KIND = "DeviceClass"
DEVICE_CLASS_PLURAL = "deviceclasses"
RESOURCE_SLICE_KIND = "ResourceSlice"
RESOURCE_SLICE_PLURAL = "resourceslices"
RESOURCE_CLAIM_KIND = "ResourceClaim"
RESOURCE_CLAIM_PLURAL = "resourceclaims"
RESOURCE_CLAIM_TEMPLATE_KIND = "ResourceClaimTemplate"
RESOURCE_CLAIM_TEMPLATE_PLURAL = "resourceclaimtemplates"


class DeviceClassManifest(CustomObjectManifest):
    """Push-side manifest for a Kubernetes DRA DeviceClass.

    Parameters
    ----------
    name : str
        Kubernetes DeviceClass name.
    spec : Mapping[str, object]
        DRA DeviceClass spec.
    labels : Mapping[str, str]
        Metadata labels to apply.
    """

    api_version: str = Field(
        default=f"{DRA_GROUP}/{DRA_VERSION}",
        alias="apiVersion",
    )
    kind: str = DEVICE_CLASS_KIND
    spec: Mapping[str, object]
    status: Mapping[str, object] = EMPTY_MAPPING


@custom_resource(
    manifest=DeviceClassManifest,
    group=DRA_GROUP,
    version=DRA_VERSION,
    kind=DEVICE_CLASS_KIND,
    plural=DEVICE_CLASS_PLURAL,
    scope="cluster",
)
class DeviceClass(CustomResource[DeviceClassManifest]):
    """Wrapper around one Kubernetes DRA DeviceClass object."""


class ResourceSliceManifest(CustomObjectManifest):
    """Push-side manifest for a Kubernetes DRA ResourceSlice.

    Parameters
    ----------
    name : str
        Kubernetes ResourceSlice name.
    spec : Mapping[str, object]
        DRA ResourceSlice spec.
    labels : Mapping[str, str]
        Metadata labels to apply.
    """

    api_version: str = Field(
        default=f"{DRA_GROUP}/{DRA_VERSION}",
        alias="apiVersion",
    )
    kind: str = RESOURCE_SLICE_KIND
    spec: Mapping[str, object]
    status: Mapping[str, object] = EMPTY_MAPPING


@custom_resource(
    manifest=ResourceSliceManifest,
    group=DRA_GROUP,
    version=DRA_VERSION,
    kind=RESOURCE_SLICE_KIND,
    plural=RESOURCE_SLICE_PLURAL,
    scope="cluster",
)
class ResourceSlice(CustomResource[ResourceSliceManifest]):
    """Wrapper around one Kubernetes DRA ResourceSlice object."""


class ResourceClaimManifest(CustomObjectManifest):
    """Pull-side manifest for a Kubernetes DRA ResourceClaim."""

    api_version: str = Field(
        default=f"{DRA_GROUP}/{DRA_VERSION}",
        alias="apiVersion",
    )
    kind: str = RESOURCE_CLAIM_KIND
    spec: Mapping[str, object] = EMPTY_MAPPING
    status: Mapping[str, object] = EMPTY_MAPPING


@custom_resource(
    manifest=ResourceClaimManifest,
    group=DRA_GROUP,
    version=DRA_VERSION,
    kind=RESOURCE_CLAIM_KIND,
    plural=RESOURCE_CLAIM_PLURAL,
)
class ResourceClaim(CustomResource[ResourceClaimManifest]):
    """Wrapper around one Kubernetes DRA ResourceClaim object."""


class ResourceClaimTemplateManifest(CustomObjectManifest):
    """Push-side manifest for a Kubernetes DRA ResourceClaimTemplate.

    Parameters
    ----------
    namespace : str
        Namespace that owns the ResourceClaimTemplate.
    name : str
        Kubernetes ResourceClaimTemplate name.
    spec : Mapping[str, object]
        DRA ResourceClaimTemplate spec.
    labels : Mapping[str, str]
        Metadata labels to apply.
    """

    api_version: str = Field(
        default=f"{DRA_GROUP}/{DRA_VERSION}",
        alias="apiVersion",
    )
    kind: str = RESOURCE_CLAIM_TEMPLATE_KIND
    spec: Mapping[str, object]
    status: Mapping[str, object] = EMPTY_MAPPING


@custom_resource(
    manifest=ResourceClaimTemplateManifest,
    group=DRA_GROUP,
    version=DRA_VERSION,
    kind=RESOURCE_CLAIM_TEMPLATE_KIND,
    plural=RESOURCE_CLAIM_TEMPLATE_PLURAL,
)
class ResourceClaimTemplate(CustomResource[ResourceClaimTemplateManifest]):
    """Wrapper around one Kubernetes DRA ResourceClaimTemplate object."""


async def ensure_dra_api(kube: Kube, *, deadline: Deadline) -> None:
    """Validate Kubernetes DRA API availability.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    deadline : Deadline
        Maximum request budget in seconds.

    Raises
    ------
    OSError
        If the Kubernetes DRA API is unavailable.
    """
    try:
        await DeviceClass.list(kube, deadline=deadline)
    except OSError as err:
        msg = (
            "Kubernetes Dynamic Resource Allocation API "
            f"{DRA_GROUP}/{DRA_VERSION} is unavailable. Enable DRA before "
            "rendering device-backed Bertrand builds or workloads."
        )
        raise OSError(msg) from err
