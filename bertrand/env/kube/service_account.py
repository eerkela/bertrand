"""Wrappers for the Kubernetes ServiceAccount API."""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
from types import MappingProxyType
from typing import Self

from kubernetes import client as kube_client

from .api import Kube


@dataclass(frozen=True)
class ServiceAccount:
    """General-purpose wrapper around one Kubernetes ServiceAccount object."""

    obj: kube_client.V1ServiceAccount

    @classmethod
    async def get(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        timeout: float,
    ) -> Self | None:
        """Read one Kubernetes ServiceAccount by name."""
        payload = await kube.run(
            lambda request_timeout: kube.core.read_namespaced_service_account(
                name=name,
                namespace=namespace,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to read ServiceAccount {name!r} in namespace {namespace!r}",
        )
        if payload is None:
            return None
        if not isinstance(payload, kube_client.V1ServiceAccount):
            raise OSError(
                f"malformed Kubernetes ServiceAccount payload for {name!r} "
                f"in namespace {namespace!r}"
            )
        return cls(obj=payload)

    @staticmethod
    def _manifest(
        *,
        namespace: str,
        name: str,
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
    ) -> dict[str, object]:
        return {
            "apiVersion": "v1",
            "kind": "ServiceAccount",
            "metadata": {
                "name": name,
                "namespace": namespace,
                "labels": dict(labels or {}),
                "annotations": dict(annotations or {}),
            },
        }

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        timeout: float,
        labels: Mapping[str, str] | None = None,
        annotations: Mapping[str, str] | None = None,
    ) -> Self:
        """Create or patch one Kubernetes ServiceAccount."""
        namespace = namespace.strip()
        name = name.strip()
        if not namespace or not name:
            raise OSError("ServiceAccount upsert requires non-empty namespace and name")
        manifest = cls._manifest(
            namespace=namespace,
            name=name,
            labels=labels,
            annotations=annotations,
        )
        try:
            created = await kube.run(
                lambda request_timeout: kube.core.create_namespaced_service_account(
                    namespace=namespace,
                    body=manifest,
                    _request_timeout=request_timeout,
                ),
                timeout=timeout,
                context=f"failed to create ServiceAccount {namespace}/{name}",
            )
            if not isinstance(created, kube_client.V1ServiceAccount):
                raise OSError(
                    f"malformed Kubernetes ServiceAccount payload while creating {name!r}"
                )
            return cls(obj=created)
        except OSError as err:
            detail = str(err).lower()
            if "status 409" not in detail and "already exists" not in detail:
                raise

        patched = await kube.run(
            lambda request_timeout: kube.core.patch_namespaced_service_account(
                name=name,
                namespace=namespace,
                body=manifest,
                _request_timeout=request_timeout,
            ),
            timeout=timeout,
            context=f"failed to patch ServiceAccount {namespace}/{name}",
        )
        if not isinstance(patched, kube_client.V1ServiceAccount):
            raise OSError(f"malformed Kubernetes ServiceAccount payload while patching {name!r}")
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
    def namespace(self) -> str:
        """
        Returns
        -------
        str
            Trimmed `metadata.namespace`, or an empty string when unavailable.
        """
        metadata = self.obj.metadata
        return (metadata.namespace or "").strip() if metadata is not None else ""

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
