"""Kubernetes Dynamic Resource Allocation custom-object wrappers."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from bertrand.env.git import EMPTY_MAPPING
from bertrand.env.kube.custom_object import CustomResource, custom_resource

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


@dataclass(frozen=True)
class DeviceClassManifest:
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

    name: str
    spec: Mapping[str, object]
    labels: Mapping[str, str] = EMPTY_MAPPING

    @property
    def namespace(self) -> None:
        """Return `None` because DeviceClass is cluster-scoped."""
        return None

    def manifest(self) -> Mapping[str, object]:
        """Render the Kubernetes DeviceClass manifest.

        Returns
        -------
        Mapping[str, object]
            Complete Kubernetes custom-object manifest.
        """
        return {
            "apiVersion": f"{DRA_GROUP}/{DRA_VERSION}",
            "kind": DEVICE_CLASS_KIND,
            "metadata": {"name": self.name, "labels": dict(self.labels)},
            "spec": dict(self.spec),
        }


@custom_resource(
    group=DRA_GROUP,
    version=DRA_VERSION,
    kind=DEVICE_CLASS_KIND,
    plural=DEVICE_CLASS_PLURAL,
    scope="cluster",
)
class DeviceClass(CustomResource):
    """Wrapper around one Kubernetes DRA DeviceClass object."""


@dataclass(frozen=True)
class ResourceSliceManifest:
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

    name: str
    spec: Mapping[str, object]
    labels: Mapping[str, str] = EMPTY_MAPPING

    @property
    def namespace(self) -> None:
        """Return `None` because ResourceSlice is cluster-scoped."""
        return None

    def manifest(self) -> Mapping[str, object]:
        """Render the Kubernetes ResourceSlice manifest.

        Returns
        -------
        Mapping[str, object]
            Complete Kubernetes custom-object manifest.
        """
        return {
            "apiVersion": f"{DRA_GROUP}/{DRA_VERSION}",
            "kind": RESOURCE_SLICE_KIND,
            "metadata": {"name": self.name, "labels": dict(self.labels)},
            "spec": dict(self.spec),
        }


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


@dataclass(frozen=True)
class ResourceClaimTemplateManifest:
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

    namespace: str
    name: str
    spec: Mapping[str, object]
    labels: Mapping[str, str] = EMPTY_MAPPING

    def manifest(self) -> Mapping[str, object]:
        """Render the Kubernetes ResourceClaimTemplate manifest.

        Returns
        -------
        Mapping[str, object]
            Complete Kubernetes custom-object manifest.
        """
        return {
            "apiVersion": f"{DRA_GROUP}/{DRA_VERSION}",
            "kind": RESOURCE_CLAIM_TEMPLATE_KIND,
            "metadata": {
                "namespace": self.namespace,
                "name": self.name,
                "labels": dict(self.labels),
            },
            "spec": dict(self.spec),
        }


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
