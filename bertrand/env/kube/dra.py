"""Kubernetes Dynamic Resource Allocation custom-object helpers."""

from __future__ import annotations

from typing import TYPE_CHECKING

from bertrand.env.kube.custom_object import CustomObject, CustomObjectResource

if TYPE_CHECKING:
    from collections.abc import Mapping

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


DEVICE_CLASS_RESOURCE = CustomObjectResource[CustomObject](
    group=DRA_GROUP,
    version=DRA_VERSION,
    kind=DEVICE_CLASS_KIND,
    plural=DEVICE_CLASS_PLURAL,
    scope="cluster",
)


RESOURCE_SLICE_RESOURCE = CustomObjectResource[CustomObject](
    group=DRA_GROUP,
    version=DRA_VERSION,
    kind=RESOURCE_SLICE_KIND,
    plural=RESOURCE_SLICE_PLURAL,
    scope="cluster",
)


RESOURCE_CLAIM_TEMPLATE_RESOURCE = CustomObjectResource[CustomObject](
    group=DRA_GROUP,
    version=DRA_VERSION,
    kind=RESOURCE_CLAIM_TEMPLATE_KIND,
    plural=RESOURCE_CLAIM_TEMPLATE_PLURAL,
)


async def upsert_device_class(
    kube: Kube,
    *,
    name: str,
    spec: Mapping[str, object],
    timeout: float,
    labels: Mapping[str, str] | None = None,
) -> CustomObject:
    """Create or patch a DRA `DeviceClass`.

    Returns
    -------
    CustomObject
        Created or patched DeviceClass.
    """
    return await DEVICE_CLASS_RESOURCE.upsert(
        kube,
        name=name,
        spec=spec,
        labels=labels,
        timeout=timeout,
    )


async def upsert_resource_slice(
    kube: Kube,
    *,
    name: str,
    spec: Mapping[str, object],
    timeout: float,
    labels: Mapping[str, str] | None = None,
) -> CustomObject:
    """Create or patch a DRA `ResourceSlice`.

    Returns
    -------
    CustomObject
        Created or patched ResourceSlice.
    """
    return await RESOURCE_SLICE_RESOURCE.upsert(
        kube,
        name=name,
        spec=spec,
        labels=labels,
        timeout=timeout,
    )


async def create_resource_claim_template(
    kube: Kube,
    *,
    namespace: str,
    name: str,
    spec: Mapping[str, object],
    labels: Mapping[str, str],
    timeout: float,
) -> CustomObject:
    """Create one DRA `ResourceClaimTemplate`.

    Returns
    -------
    CustomObject
        Created ResourceClaimTemplate.
    """
    return await RESOURCE_CLAIM_TEMPLATE_RESOURCE.create(
        kube,
        namespace=namespace,
        name=name,
        spec=spec,
        labels=labels,
        timeout=timeout,
    )


async def upsert_resource_claim_template(
    kube: Kube,
    *,
    namespace: str,
    name: str,
    spec: Mapping[str, object],
    labels: Mapping[str, str],
    timeout: float,
) -> CustomObject:
    """Create or patch one DRA `ResourceClaimTemplate`.

    Returns
    -------
    CustomObject
        Created or patched ResourceClaimTemplate.
    """
    return await RESOURCE_CLAIM_TEMPLATE_RESOURCE.upsert(
        kube,
        namespace=namespace,
        name=name,
        spec=spec,
        labels=labels,
        timeout=timeout,
    )


async def delete_resource_claim_template(
    kube: Kube,
    template: CustomObject,
    *,
    timeout: float,
) -> None:
    """Delete one DRA `ResourceClaimTemplate` when metadata is complete."""
    if not template.namespace or not template.name:
        return
    await RESOURCE_CLAIM_TEMPLATE_RESOURCE.delete_by_name(
        kube,
        namespace=template.namespace,
        name=template.name,
        timeout=timeout,
    )


async def ensure_dra_api(kube: Kube, *, timeout: float) -> None:
    """Validate Kubernetes DRA API availability.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum request budget in seconds.

    Raises
    ------
    OSError
        If the Kubernetes DRA API is unavailable.
    """
    try:
        await DEVICE_CLASS_RESOURCE.list(kube, timeout=timeout)
    except OSError as err:
        msg = (
            "Kubernetes Dynamic Resource Allocation API "
            f"{DRA_GROUP}/{DRA_VERSION} is unavailable. Enable DRA before "
            "rendering device-backed Bertrand builds or workloads."
        )
        raise OSError(msg) from err
