"""Wrappers for Kubernetes coordination Lease resources."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from kubernetes import client as kube_client

from .api.resource import (
    KubeResource,
    namespaced_resource,
)

if TYPE_CHECKING:
    from collections.abc import Mapping
    from datetime import datetime

    from bertrand.env.git import Deadline

    from .api.client import Kube


@dataclass(frozen=True)
class LeaseManifest:
    """Desired state for one Kubernetes Lease."""

    namespace: str
    name: str
    holder_identity: str
    lease_duration_seconds: int
    acquire_time: datetime | None = None
    renew_time: datetime | None = None
    labels: Mapping[str, str] | None = None
    annotations: Mapping[str, str] | None = None

    def manifest(self) -> Mapping[str, object]:
        """Render this desired state as a Kubernetes manifest.

        Returns
        -------
        Mapping[str, object]
            Kubernetes Lease manifest payload.

        Raises
        ------
        OSError
            If holder identity is empty or duration is not positive.
        """
        holder_identity = self.holder_identity.strip()
        if not holder_identity:
            msg = "Lease manifest requires a non-empty holder identity"
            raise OSError(msg)
        if self.lease_duration_seconds <= 0:
            msg = "Lease duration must be positive"
            raise OSError(msg)
        return _lease_manifest(
            namespace=self.namespace,
            name=self.name,
            holder_identity=holder_identity,
            lease_duration_seconds=self.lease_duration_seconds,
            acquire_time=self.acquire_time,
            renew_time=self.renew_time,
            labels=self.labels,
            annotations=self.annotations,
        )


@dataclass(frozen=True)
class LeaseReplacementManifest:
    """Version-guarded replacement state for one Kubernetes Lease."""

    namespace: str
    name: str
    holder_identity: str | None
    lease_duration_seconds: int
    resource_version: str
    acquire_time: datetime | None = None
    renew_time: datetime | None = None
    labels: Mapping[str, str] | None = None
    annotations: Mapping[str, str] | None = None

    def manifest(self) -> Mapping[str, object]:
        """Render this replacement state as a Kubernetes manifest.

        Returns
        -------
        Mapping[str, object]
            Kubernetes Lease replacement manifest payload.

        Raises
        ------
        OSError
            If holder identity, duration, or resource version are invalid.
        """
        holder = (
            self.holder_identity.strip()
            if self.holder_identity is not None
            else None
        )
        if holder == "":
            msg = "Lease holder identity must be non-empty when provided"
            raise OSError(msg)
        if self.lease_duration_seconds <= 0:
            msg = "Lease duration must be positive"
            raise OSError(msg)
        resource_version = self.resource_version.strip()
        if not resource_version:
            msg = "Lease replacement requires a non-empty resource version"
            raise OSError(msg)
        return _lease_manifest(
            namespace=self.namespace,
            name=self.name,
            holder_identity=holder,
            lease_duration_seconds=self.lease_duration_seconds,
            acquire_time=self.acquire_time,
            renew_time=self.renew_time,
            labels=self.labels,
            annotations=self.annotations,
            resource_version=resource_version,
        )


def _lease_manifest(
    *,
    namespace: str,
    name: str,
    holder_identity: str | None,
    lease_duration_seconds: int,
    acquire_time: datetime | None,
    renew_time: datetime | None,
    labels: Mapping[str, str] | None,
    annotations: Mapping[str, str] | None,
    resource_version: str | None = None,
) -> dict[str, object]:
    spec: dict[str, object] = {"leaseDurationSeconds": lease_duration_seconds}
    if holder_identity is not None:
        spec["holderIdentity"] = holder_identity
    if acquire_time is not None:
        spec["acquireTime"] = acquire_time
    if renew_time is not None:
        spec["renewTime"] = renew_time
    body: dict[str, object] = {
        "apiVersion": "coordination.k8s.io/v1",
        "kind": "Lease",
        "metadata": {
            "name": name,
            "namespace": namespace,
            "labels": dict(labels or {}),
            "annotations": dict(annotations or {}),
        },
        "spec": spec,
    }
    if resource_version:
        metadata = body["metadata"]
        if isinstance(metadata, dict):
            metadata["resourceVersion"] = resource_version
    return body


@namespaced_resource(
    api=kube_client.CoordinationV1Api,
    payload=kube_client.V1Lease,
    read=kube_client.CoordinationV1Api.read_namespaced_lease,
    list=kube_client.CoordinationV1Api.list_namespaced_lease,
    list_all=kube_client.CoordinationV1Api.list_lease_for_all_namespaces,
    create=kube_client.CoordinationV1Api.create_namespaced_lease,
    patch=kube_client.CoordinationV1Api.patch_namespaced_lease,
    delete=kube_client.CoordinationV1Api.delete_namespaced_lease,
)
@dataclass(frozen=True)
class Lease(
    KubeResource[kube_client.V1Lease, LeaseManifest],
):
    """General-purpose wrapper around one Kubernetes Lease object.

    Parameters
    ----------
    _obj : kube_client.V1Lease
        Typed Kubernetes Lease payload returned by the cluster API.
    """

    _obj: kube_client.V1Lease

    @classmethod
    async def create(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        holder_identity: str,
        lease_duration_seconds: int,
        deadline: Deadline,
        acquire_time: datetime | None = None,
        renew_time: datetime | None = None,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> Lease:
        """Create one Kubernetes Lease.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the Lease.
        name : str
            Lease name to create.
        holder_identity : str
            Identity string for the current lease holder.
        lease_duration_seconds : int
            Lease duration in seconds.
        deadline : Deadline
            Maximum request budget in seconds. If infinite, wait indefinitely.
        acquire_time : datetime | None, optional
            Time when the lease was first acquired.
        renew_time : datetime | None, optional
            Time when the lease was last renewed.
        labels : Mapping[str, str] | None, optional
            Labels to apply to `metadata.labels`.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to `metadata.annotations`.

        Returns
        -------
        Lease
            Wrapped created Lease.

        Raises
        ------
        OSError
            If required identity fields are empty, duration is invalid, or Kubernetes
            create fails or returns malformed data.
        """
        namespace = namespace.strip()
        name = name.strip()
        holder_identity = holder_identity.strip()
        if not namespace or not name or not holder_identity:
            msg = "Lease create requires non-empty namespace, name, and holder identity"
            raise OSError(msg)
        if lease_duration_seconds <= 0:
            msg = "Lease duration must be positive"
            raise OSError(msg)

        api = kube_client.CoordinationV1Api(kube.client)
        created = await kube.run(
            lambda request_timeout: api.create_namespaced_lease(
                namespace=namespace,
                body=LeaseManifest(
                    namespace=namespace,
                    name=name,
                    holder_identity=holder_identity,
                    lease_duration_seconds=lease_duration_seconds,
                    acquire_time=acquire_time,
                    renew_time=renew_time,
                    labels=labels,
                    annotations=annotations,
                ).manifest(),
                _request_timeout=request_timeout,
            ),
            deadline=deadline,
            context=f"failed to create Lease {namespace}/{name}",
            missing_ok=False,
        )
        if not isinstance(created, kube_client.V1Lease):
            msg = (
                f"malformed Kubernetes Lease payload while creating {namespace}/{name}"
            )
            raise OSError(msg)
        return cls(_obj=created)

    @classmethod
    async def replace(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        holder_identity: str | None,
        lease_duration_seconds: int,
        resource_version: str,
        deadline: Deadline,
        acquire_time: datetime | None = None,
        renew_time: datetime | None = None,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> Lease:
        """Replace one Kubernetes Lease with a resource-version guard.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the Lease.
        name : str
            Lease name to replace.
        holder_identity : str | None
            Identity string for the current lease holder. If `None`, the holder field
            is omitted.
        lease_duration_seconds : int
            Lease duration in seconds.
        resource_version : str
            Kubernetes resource version required for optimistic concurrency.
        deadline : Deadline
            Maximum request budget in seconds. If infinite, wait indefinitely.
        acquire_time : datetime | None, optional
            Time when the lease was first acquired.
        renew_time : datetime | None, optional
            Time when the lease was last renewed.
        labels : Mapping[str, str] | None, optional
            Labels to apply to `metadata.labels`.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to `metadata.annotations`.

        Returns
        -------
        Lease
            Wrapped replaced Lease.

        Raises
        ------
        OSError
            If required identity fields are empty, duration is invalid, or Kubernetes
            replace fails or returns malformed data.
        """
        namespace = namespace.strip()
        name = name.strip()
        resource_version = resource_version.strip()
        holder = holder_identity.strip() if holder_identity is not None else None
        if holder == "":
            msg = "Lease holder identity must be non-empty when provided"
            raise OSError(msg)
        if not namespace or not name or not resource_version:
            msg = (
                "Lease replace requires non-empty namespace, name, and resource version"
            )
            raise OSError(msg)
        if lease_duration_seconds <= 0:
            msg = "Lease duration must be positive"
            raise OSError(msg)

        api = kube_client.CoordinationV1Api(kube.client)
        replaced = await kube.run(
            lambda request_timeout: api.replace_namespaced_lease(
                name=name,
                namespace=namespace,
                body=LeaseReplacementManifest(
                    namespace=namespace,
                    name=name,
                    holder_identity=holder,
                    lease_duration_seconds=lease_duration_seconds,
                    acquire_time=acquire_time,
                    renew_time=renew_time,
                    labels=labels,
                    annotations=annotations,
                    resource_version=resource_version,
                ).manifest(),
                _request_timeout=request_timeout,
            ),
            deadline=deadline,
            context=f"failed to replace Lease {namespace}/{name}",
            missing_ok=False,
        )
        if not isinstance(replaced, kube_client.V1Lease):
            msg = (
                f"malformed Kubernetes Lease payload while replacing {namespace}/{name}"
            )
            raise OSError(msg)
        return cls(_obj=replaced)

    @property
    def holder_identity(self) -> str:
        """Return the current Lease holder identity.

        Returns
        -------
        str
            Lease `spec.holderIdentity`, or an empty string when unavailable.
        """
        spec = self._obj.spec
        return (spec.holder_identity or "").strip() if spec is not None else ""

    @property
    def lease_duration_seconds(self) -> int | None:
        """Return the Lease duration in seconds.

        Returns
        -------
        int | None
            Lease `spec.leaseDurationSeconds`, or `None` when unavailable.
        """
        spec = self._obj.spec
        return spec.lease_duration_seconds if spec is not None else None

    @property
    def acquire_time(self) -> datetime | None:
        """Return the Lease acquire time.

        Returns
        -------
        datetime | None
            Lease `spec.acquireTime`, or `None` when unavailable.
        """
        spec = self._obj.spec
        return spec.acquire_time if spec is not None else None

    @property
    def renew_time(self) -> datetime | None:
        """Return the Lease renew time.

        Returns
        -------
        datetime | None
            Lease `spec.renewTime`, or `None` when unavailable.
        """
        spec = self._obj.spec
        return spec.renew_time if spec is not None else None
