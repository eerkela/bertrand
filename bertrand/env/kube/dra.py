"""Kubernetes Dynamic Resource Allocation object wrappers."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Self

from bertrand.env.kube.custom_object import (
    CustomObject,
    CustomObjectClient,
    CustomObjectSpec,
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

_DEVICE_CLASS_CLIENT = CustomObjectClient(
    CustomObjectSpec(
        group=DRA_GROUP,
        version=DRA_VERSION,
        kind=DEVICE_CLASS_KIND,
        plural=DEVICE_CLASS_PLURAL,
        scope="cluster",
    )
)
_RESOURCE_SLICE_CLIENT = CustomObjectClient(
    CustomObjectSpec(
        group=DRA_GROUP,
        version=DRA_VERSION,
        kind=RESOURCE_SLICE_KIND,
        plural=RESOURCE_SLICE_PLURAL,
        scope="cluster",
    )
)
_RESOURCE_CLAIM_CLIENT = CustomObjectClient(
    CustomObjectSpec(
        group=DRA_GROUP,
        version=DRA_VERSION,
        kind=RESOURCE_CLAIM_KIND,
        plural=RESOURCE_CLAIM_PLURAL,
    )
)
_RESOURCE_CLAIM_TEMPLATE_CLIENT = CustomObjectClient(
    CustomObjectSpec(
        group=DRA_GROUP,
        version=DRA_VERSION,
        kind=RESOURCE_CLAIM_TEMPLATE_KIND,
        plural=RESOURCE_CLAIM_TEMPLATE_PLURAL,
    )
)


@dataclass(frozen=True)
class DeviceClass:
    """Wrapper around one Kubernetes DRA `DeviceClass`."""

    _obj: CustomObject

    @classmethod
    def _from_object(cls, obj: CustomObject) -> Self:
        return cls(_obj=obj)

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        name: str,
        spec: Mapping[str, object],
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> Self:
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
        Self
            Wrapped class object.
        """
        obj = await _DEVICE_CLASS_CLIENT.upsert(
            kube,
            name=name,
            spec=spec,
            labels=labels,
            timeout=timeout,
        )
        return cls._from_object(obj)


@dataclass(frozen=True)
class ResourceSlice:
    """Wrapper around one Kubernetes DRA `ResourceSlice`."""

    _obj: CustomObject

    @classmethod
    def _from_object(cls, obj: CustomObject) -> Self:
        return cls(_obj=obj)

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        name: str,
        spec: Mapping[str, object],
        timeout: float,
        labels: Mapping[str, str] | None = None,
    ) -> Self:
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
        Self
            Wrapped ResourceSlice.
        """
        obj = await _RESOURCE_SLICE_CLIENT.upsert(
            kube,
            name=name,
            spec=spec,
            labels=labels,
            timeout=timeout,
        )
        return cls._from_object(obj)


@dataclass(frozen=True)
class ResourceClaim:
    """Wrapper around one namespaced DRA `ResourceClaim`."""

    _obj: CustomObject

    @classmethod
    def _from_object(cls, obj: CustomObject) -> Self:
        return cls(_obj=obj)

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        timeout: float,
    ) -> Self | None:
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
        Self | None
            Wrapped claim, or `None` when missing.
        """
        obj = await _RESOURCE_CLAIM_CLIENT.get(
            kube,
            namespace=namespace,
            name=name,
            timeout=timeout,
        )
        return None if obj is None else cls._from_object(obj)

    @property
    def name(self) -> str:
        """Return the claim name.

        Returns
        -------
        str
            Kubernetes `metadata.name`.
        """
        return self._obj.name

    @property
    def namespace(self) -> str:
        """Return the claim namespace.

        Returns
        -------
        str
            Kubernetes `metadata.namespace`.
        """
        return self._obj.namespace

    @property
    def status(self) -> Mapping[str, object]:
        """Return the claim status payload.

        Returns
        -------
        Mapping[str, object]
            Read-only claim status.
        """
        return self._obj.status


@dataclass(frozen=True)
class ResourceClaimTemplate:
    """Wrapper around one namespaced DRA `ResourceClaimTemplate`."""

    _obj: CustomObject

    @classmethod
    def _from_object(cls, obj: CustomObject) -> Self:
        return cls(_obj=obj)

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
    ) -> Self:
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
        Self
            Wrapped ResourceClaimTemplate.
        """
        obj = await _RESOURCE_CLAIM_TEMPLATE_CLIENT.create(
            kube,
            namespace=namespace,
            name=name,
            spec=spec,
            labels=labels,
            timeout=timeout,
        )
        return cls._from_object(obj)

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
    ) -> Self:
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
        Self
            Wrapped created or patched ResourceClaimTemplate.
        """
        obj = await _RESOURCE_CLAIM_TEMPLATE_CLIENT.upsert(
            kube,
            namespace=namespace,
            name=name,
            spec=spec,
            labels=labels,
            timeout=timeout,
        )
        return cls._from_object(obj)

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
        await _RESOURCE_CLAIM_TEMPLATE_CLIENT.delete_by_name(
            kube,
            namespace=self._obj.namespace,
            name=self._obj.name,
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
        await _DEVICE_CLASS_CLIENT.list(kube, timeout=timeout)
    except OSError as err:
        msg = (
            "Kubernetes Dynamic Resource Allocation API "
            f"{DRA_GROUP}/{DRA_VERSION} is unavailable. Enable DRA before "
            "rendering device-backed Bertrand builds or workloads."
        )
        raise OSError(msg) from err
