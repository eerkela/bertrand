"""Kubernetes Dynamic Resource Allocation object wrappers."""

from __future__ import annotations

from typing import TYPE_CHECKING

from bertrand.env.kube.custom_object import (
    CustomObjectResource,
    CustomObjectSpec,
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

_DEVICE_CLASS_SPEC = CustomObjectSpec(
    group=DRA_GROUP,
    version=DRA_VERSION,
    kind=DEVICE_CLASS_KIND,
    plural=DEVICE_CLASS_PLURAL,
    scope="cluster",
)
_RESOURCE_SLICE_SPEC = CustomObjectSpec(
    group=DRA_GROUP,
    version=DRA_VERSION,
    kind=RESOURCE_SLICE_KIND,
    plural=RESOURCE_SLICE_PLURAL,
    scope="cluster",
)
_RESOURCE_CLAIM_SPEC = CustomObjectSpec(
    group=DRA_GROUP,
    version=DRA_VERSION,
    kind=RESOURCE_CLAIM_KIND,
    plural=RESOURCE_CLAIM_PLURAL,
)
_RESOURCE_CLAIM_TEMPLATE_SPEC = CustomObjectSpec(
    group=DRA_GROUP,
    version=DRA_VERSION,
    kind=RESOURCE_CLAIM_TEMPLATE_KIND,
    plural=RESOURCE_CLAIM_TEMPLATE_PLURAL,
)


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

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            DeviceClass name.
        spec : Mapping[str, object]
            DeviceClass spec payload.
        timeout : float
            Maximum request budget in seconds.
        labels : Mapping[str, str] | None, optional
            Kubernetes labels to apply.

        Returns
        -------
        DeviceClass
            Wrapped class object.
        """
        return await _DEVICE_CLASS_RESOURCE.upsert(
            kube,
            name=name,
            spec=spec,
            labels=labels,
            timeout=timeout,
        )


_DEVICE_CLASS_RESOURCE: CustomObjectResource[DeviceClass] = CustomObjectResource(
    spec=_DEVICE_CLASS_SPEC,
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

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            ResourceSlice name.
        spec : Mapping[str, object]
            ResourceSlice spec payload.
        timeout : float
            Maximum request budget in seconds.
        labels : Mapping[str, str] | None, optional
            Kubernetes labels to apply.

        Returns
        -------
        ResourceSlice
            Wrapped ResourceSlice.
        """
        return await _RESOURCE_SLICE_RESOURCE.upsert(
            kube,
            name=name,
            spec=spec,
            labels=labels,
            timeout=timeout,
        )


_RESOURCE_SLICE_RESOURCE: CustomObjectResource[ResourceSlice] = CustomObjectResource(
    spec=_RESOURCE_SLICE_SPEC,
    parser=ResourceSlice._from_object,
)


class ResourceClaim(CustomObjectWrapper):
    """Wrapper around one namespaced DRA `ResourceClaim`."""

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        timeout: float,
    ) -> ResourceClaim | None:
        """Read one DRA `ResourceClaim` by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the claim.
        name : str
            Claim name.
        timeout : float
            Maximum request budget in seconds.

        Returns
        -------
        ResourceClaim | None
            Wrapped claim, or `None` when missing.
        """
        return await _RESOURCE_CLAIM_RESOURCE.get(
            kube,
            namespace=namespace,
            name=name,
            timeout=timeout,
        )

    @property
    def status(self) -> Mapping[str, object]:
        """Return the claim status payload.

        Returns
        -------
        Mapping[str, object]
            Read-only claim status.
        """
        return self._obj.status


_RESOURCE_CLAIM_RESOURCE: CustomObjectResource[ResourceClaim] = CustomObjectResource(
    spec=_RESOURCE_CLAIM_SPEC,
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

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the template.
        name : str
            ResourceClaimTemplate name.
        spec : Mapping[str, object]
            ResourceClaimTemplate spec payload.
        labels : Mapping[str, str]
            Kubernetes labels to apply.
        timeout : float
            Maximum request budget in seconds.

        Returns
        -------
        ResourceClaimTemplate
            Wrapped ResourceClaimTemplate.
        """
        return await _RESOURCE_CLAIM_TEMPLATE_RESOURCE.create(
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

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the template.
        name : str
            ResourceClaimTemplate name.
        spec : Mapping[str, object]
            ResourceClaimTemplate spec payload.
        labels : Mapping[str, str]
            Kubernetes labels to apply.
        timeout : float
            Maximum request budget in seconds.

        Returns
        -------
        ResourceClaimTemplate
            Wrapped created or patched ResourceClaimTemplate.
        """
        return await _RESOURCE_CLAIM_TEMPLATE_RESOURCE.upsert(
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
        await _RESOURCE_CLAIM_TEMPLATE_RESOURCE.delete_by_name(
            kube,
            namespace=self._obj.namespace,
            name=self._obj.name,
            timeout=timeout,
        )


_RESOURCE_CLAIM_TEMPLATE_RESOURCE: CustomObjectResource[ResourceClaimTemplate] = (
    CustomObjectResource(
        spec=_RESOURCE_CLAIM_TEMPLATE_SPEC,
        parser=ResourceClaimTemplate._from_object,
    )
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
        await _DEVICE_CLASS_RESOURCE.list(kube, timeout=timeout)
    except OSError as err:
        msg = (
            "Kubernetes Dynamic Resource Allocation API "
            f"{DRA_GROUP}/{DRA_VERSION} is unavailable. Enable DRA before "
            "rendering device-backed Bertrand builds or workloads."
        )
        raise OSError(msg) from err
