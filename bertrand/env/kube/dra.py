"""Kubernetes Dynamic Resource Allocation custom-object wrappers."""

from __future__ import annotations

from typing import TYPE_CHECKING

from bertrand.env.kube.custom_object import CustomResource, custom_resource

if TYPE_CHECKING:
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


@custom_resource(
    group=DRA_GROUP,
    version=DRA_VERSION,
    kind=DEVICE_CLASS_KIND,
    plural=DEVICE_CLASS_PLURAL,
    scope="cluster",
)
class DeviceClass(CustomResource):
    """Wrapper around one Kubernetes DRA DeviceClass object."""


@custom_resource(
    group=DRA_GROUP,
    version=DRA_VERSION,
    kind=RESOURCE_SLICE_KIND,
    plural=RESOURCE_SLICE_PLURAL,
    scope="cluster",
)
class ResourceSlice(CustomResource):
    """Wrapper around one Kubernetes DRA ResourceSlice object."""


@custom_resource(
    group=DRA_GROUP,
    version=DRA_VERSION,
    kind=RESOURCE_CLAIM_KIND,
    plural=RESOURCE_CLAIM_PLURAL,
)
class ResourceClaim(CustomResource):
    """Wrapper around one Kubernetes DRA ResourceClaim object."""


@custom_resource(
    group=DRA_GROUP,
    version=DRA_VERSION,
    kind=RESOURCE_CLAIM_TEMPLATE_KIND,
    plural=RESOURCE_CLAIM_TEMPLATE_PLURAL,
)
class ResourceClaimTemplate(CustomResource):
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
