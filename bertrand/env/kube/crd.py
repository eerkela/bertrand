"""Wrappers for Kubernetes CustomResourceDefinition objects."""

from __future__ import annotations

import asyncio
from collections.abc import Collection, Mapping
from dataclasses import dataclass
from types import MappingProxyType
from typing import Self

from kubernetes import client as kube_client

from .api import Kube

CRD_WAIT_POLL_INTERVAL_SECONDS = 0.5


@dataclass(frozen=True)
class CustomResourceDefinition:
    """General-purpose wrapper around one Kubernetes CRD object."""

    obj: kube_client.V1CustomResourceDefinition

    @staticmethod
    def _manifest(
        *,
        group: str,
        version: str,
        plural: str,
        singular: str,
        kind: str,
        spec_schema: Mapping[str, object],
        status_schema: Mapping[str, object],
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
        scope: str,
        short_names: Collection[str],
    ) -> dict[str, object]:
        return {
            "apiVersion": "apiextensions.k8s.io/v1",
            "kind": "CustomResourceDefinition",
            "metadata": {
                "name": f"{plural}.{group}",
                "labels": dict(labels or {}),
                "annotations": dict(annotations or {}),
            },
            "spec": {
                "group": group,
                "scope": scope,
                "names": {
                    "plural": plural,
                    "singular": singular,
                    "kind": kind,
                    "shortNames": list(short_names),
                },
                "versions": [
                    {
                        "name": version,
                        "served": True,
                        "storage": True,
                        "schema": {
                            "openAPIV3Schema": {
                                "type": "object",
                                "properties": {
                                    "spec": dict(spec_schema),
                                    "status": dict(status_schema),
                                },
                            }
                        },
                        "subresources": {"status": {}},
                    }
                ],
            },
        }

    @classmethod
    async def get(cls, kube: Kube, *, name: str, timeout: float) -> Self | None:
        """Read one Kubernetes CustomResourceDefinition by name."""
        payload = await kube.run(
            lambda request_timeout: kube.apiextensions.read_custom_resource_definition(
                name=name,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to read CustomResourceDefinition {name!r}",
        )
        if payload is None:
            return None
        if not isinstance(payload, kube_client.V1CustomResourceDefinition):
            raise OSError(f"malformed Kubernetes CRD payload for {name!r}")
        return cls(obj=payload)

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        group: str,
        version: str,
        plural: str,
        singular: str,
        kind: str,
        spec_schema: Mapping[str, object],
        status_schema: Mapping[str, object],
        timeout: float,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
        scope: str = "Namespaced",
        short_names: Collection[str] = (),
    ) -> Self:
        """Create or patch one Kubernetes CustomResourceDefinition.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        group : str
            API group that owns the custom resource.
        version : str
            Served and stored API version.
        plural : str
            Plural REST resource name.
        singular : str
            Singular resource name.
        kind : str
            Kubernetes kind name.
        spec_schema : Mapping[str, object]
            OpenAPI schema for the custom resource `spec` object.
        status_schema : Mapping[str, object]
            OpenAPI schema for the custom resource `status` object.
        timeout : float
            Maximum request budget in seconds.
        labels : Mapping[str, str] | None, optional
            Labels to apply to the CRD object.
        annotations : Mapping[str, str] | None, optional
            Annotations to apply to the CRD object.
        scope : str, optional
            CRD scope, typically `"Namespaced"` or `"Cluster"`.
        short_names : Collection[str], optional
            Optional short names for kubectl discovery.

        Returns
        -------
        CustomResourceDefinition
            Wrapped CRD returned by the cluster.
        """
        group = group.strip()
        version = version.strip()
        plural = plural.strip()
        singular = singular.strip()
        kind = kind.strip()
        scope = scope.strip()
        if not all((group, version, plural, singular, kind, scope)):
            raise OSError("CRD upsert requires non-empty group, version, names, kind, and scope")
        name = f"{plural}.{group}"
        body = cls._manifest(
            group=group,
            version=version,
            plural=plural,
            singular=singular,
            kind=kind,
            spec_schema=spec_schema,
            status_schema=status_schema,
            labels=labels,
            annotations=annotations,
            scope=scope,
            short_names=short_names,
        )
        try:
            created = await kube.run(
                lambda request_timeout: kube.apiextensions.create_custom_resource_definition(
                    body=body,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=f"failed to create CustomResourceDefinition {name}",
            )
            if not isinstance(created, kube_client.V1CustomResourceDefinition):
                raise OSError(f"malformed Kubernetes CRD payload while creating {name!r}")
            return cls(obj=created)
        except OSError as err:
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise

        patched = await kube.run(
            lambda request_timeout: kube.apiextensions.patch_custom_resource_definition(
                name=name,
                body=body,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to patch CustomResourceDefinition {name}",
        )
        if not isinstance(patched, kube_client.V1CustomResourceDefinition):
            raise OSError(f"malformed Kubernetes CRD payload while patching {name!r}")
        return cls(obj=patched)

    @property
    def name(self) -> str:
        """
        Returns
        -------
        str
            Trimmed `metadata.name`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.name or "").strip() if metadata is not None else ""

    @property
    def labels(self) -> Mapping[str, str]:
        """
        Returns
        -------
        Mapping[str, str]
            Read-only view of `metadata.labels`, or an empty mapping when unavailable.
        """
        metadata = self.obj.metadata
        if metadata is None or metadata.labels is None:
            return MappingProxyType({})
        return MappingProxyType(metadata.labels)

    @property
    def is_established(self) -> bool:
        """
        Returns
        -------
        bool
            Whether the CRD has an `Established=True` condition.
        """
        status = self.obj.status
        for condition in (status.conditions or []) if status is not None else []:
            if condition.type == "Established" and condition.status == "True":
                return True
        return False

    async def refresh(self, kube: Kube, *, timeout: float) -> Self | None:
        """Re-read this CRD by name."""
        name = self.name
        if not name:
            raise OSError("cannot refresh CRD with missing metadata.name")
        return await type(self).get(kube, name=name, timeout=timeout)

    async def wait_established(self, kube: Kube, *, timeout: float) -> Self:
        """Wait until this CRD reports `Established=True`."""
        if timeout <= 0:
            raise TimeoutError(f"timed out waiting for CRD {self.name!r} establishment")
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        current: Self = self
        while True:
            if current.is_established:
                return current
            remaining = deadline - loop.time()
            if remaining <= 0:
                raise TimeoutError(f"timed out waiting for CRD {self.name!r} establishment")
            await asyncio.sleep(min(CRD_WAIT_POLL_INTERVAL_SECONDS, remaining))
            refreshed = await current.refresh(kube, timeout=deadline - loop.time())
            if refreshed is None:
                raise OSError(f"CRD {self.name!r} disappeared")
            current = refreshed
