"""Kubernetes Dynamic Resource Allocation object wrappers."""

from __future__ import annotations

from typing import TYPE_CHECKING

from bertrand.env.kube.custom_object import (
    CustomObjectResource,
    CustomObjectWrapper,
)

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


class DeviceClass(CustomObjectWrapper):
    """Wrapper around one Kubernetes DRA `DeviceClass`."""

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        name: str,
        spec: Mapping[str, object],
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> DeviceClass:
        """Create or patch a DRA `DeviceClass`.

        Returns
        -------
        DeviceClass
            Wrapped class object.
        """
        return await cls.resource.upsert(
            kube,
            name=name,
            spec=spec,
            labels=labels,
            timeout=timeout,
        )


DeviceClass.resource = CustomObjectResource(
    group=DRA_GROUP,
    version=DRA_VERSION,
    kind=DEVICE_CLASS_KIND,
    plural=DEVICE_CLASS_PLURAL,
    scope="cluster",
    parser=DeviceClass._from_object,
)


class ResourceSlice(CustomObjectWrapper):
    """Wrapper around one Kubernetes DRA `ResourceSlice`."""

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        name: str,
        spec: Mapping[str, object],
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> ResourceSlice:
        """Create or patch a DRA `ResourceSlice`.

        Returns
        -------
        ResourceSlice
            Wrapped ResourceSlice.
        """
        return await cls.resource.upsert(
            kube,
            name=name,
            spec=spec,
            labels=labels,
            timeout=timeout,
        )


ResourceSlice.resource = CustomObjectResource(
    group=DRA_GROUP,
    version=DRA_VERSION,
    kind=RESOURCE_SLICE_KIND,
    plural=RESOURCE_SLICE_PLURAL,
    scope="cluster",
    parser=ResourceSlice._from_object,
)


class ResourceClaim(CustomObjectWrapper):
    """Wrapper around one namespaced DRA `ResourceClaim`."""

    @property
    def status(self) -> Mapping[str, object]:
        """Return the claim status payload.

        Returns
        -------
        Mapping[str, object]
            Read-only claim status.
        """
        return self._obj.status


ResourceClaim.resource = CustomObjectResource(
    group=DRA_GROUP,
    version=DRA_VERSION,
    kind=RESOURCE_CLAIM_KIND,
    plural=RESOURCE_CLAIM_PLURAL,
    parser=ResourceClaim._from_object,
)


class ResourceClaimTemplate(CustomObjectWrapper):
    """Wrapper around one namespaced DRA `ResourceClaimTemplate`."""

    @classmethod
    async def create(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        spec: Mapping[str, object],
        labels: Mapping[str, str],
        timeout: float,
    ) -> ResourceClaimTemplate:
        """Create one DRA `ResourceClaimTemplate`.

        Returns
        -------
        ResourceClaimTemplate
            Wrapped ResourceClaimTemplate.
        """
        return await cls.resource.create(
            kube,
            namespace=namespace,
            name=name,
            spec=spec,
            labels=labels,
            timeout=timeout,
        )

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        spec: Mapping[str, object],
        labels: Mapping[str, str],
        timeout: float,
    ) -> ResourceClaimTemplate:
        """Create or patch one DRA `ResourceClaimTemplate`.

        Returns
        -------
        ResourceClaimTemplate
            Wrapped created or patched ResourceClaimTemplate.
        """
        return await cls.resource.upsert(
            kube,
            namespace=namespace,
            name=name,
            spec=spec,
            labels=labels,
            timeout=timeout,
        )

    async def delete(self, kube: Kube, *, timeout: float) -> None:
        """Delete this ResourceClaimTemplate.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum request budget in seconds.
        """
        if not self._obj.namespace or not self._obj.name:
            return
        await type(self).resource.delete_by_name(
            kube,
            namespace=self._obj.namespace,
            name=self._obj.name,
            timeout=timeout,
        )


ResourceClaimTemplate.resource = CustomObjectResource(
    group=DRA_GROUP,
    version=DRA_VERSION,
    kind=RESOURCE_CLAIM_TEMPLATE_KIND,
    plural=RESOURCE_CLAIM_TEMPLATE_PLURAL,
    parser=ResourceClaimTemplate._from_object,
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
        await DeviceClass.list(kube, timeout=timeout)
    except OSError as err:
        msg = (
            "Kubernetes Dynamic Resource Allocation API "
            f"{DRA_GROUP}/{DRA_VERSION} is unavailable. Enable DRA before "
            "rendering device-backed Bertrand builds or workloads."
        )
        raise OSError(msg) from err
