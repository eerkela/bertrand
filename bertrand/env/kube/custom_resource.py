"""Typed custom-resource descriptors."""

from __future__ import annotations

from dataclasses import dataclass
from types import MappingProxyType
from typing import TYPE_CHECKING, Any

from bertrand.env.git import Deadline
from bertrand.env.kube.crd import CustomResourceDefinition
from bertrand.env.kube.custom_object import CustomObjectClient, CustomObjectSpec

if TYPE_CHECKING:
    import builtins
    from collections.abc import AsyncIterator, Callable, Collection, Mapping

    from bertrand.env.kube.api.client import Kube
    from bertrand.env.kube.api.watch import WatchEvent
    from bertrand.env.kube.custom_object import CustomObject, CustomObjectScope


@dataclass(frozen=True)
class CustomResource[T_co]:
    """Typed descriptor for one Kubernetes custom resource.

    Parameters
    ----------
    group : str
        Kubernetes API group that owns the resource.
    version : str
        Served Kubernetes API version.
    kind : str
        Kubernetes resource kind.
    plural : str
        Plural REST resource name.
    singular : str
        Singular resource name used by the CRD.
    parser : Callable[[object], T_co]
        Validator that converts raw Kubernetes payloads into typed records.
    spec_schema : Mapping[str, object]
        Explicit OpenAPI schema for the resource ``spec`` field.
    labels : Mapping[str, str], optional
        Default labels applied to objects and list/watch selectors.
    short_names : Collection[str], optional
        Optional short names exposed through Kubernetes discovery.
    status_schema : Mapping[str, object] | None, optional
        Explicit OpenAPI schema for the resource ``status`` field.
    crd_labels : Mapping[str, str] | None, optional
        Labels applied to the CRD object itself. Defaults to ``labels``.
    scope : {"namespaced", "cluster"}, optional
        Kubernetes API scope, either ``"namespaced"`` or ``"cluster"``.
    default_namespace : str | None, optional
        Namespace to use when a namespaced operation does not receive an explicit
        namespace.
    """

    group: str
    version: str
    kind: str
    plural: str
    singular: str
    parser: Callable[[object], T_co]
    spec_schema: Mapping[str, object]
    labels: Mapping[str, str] = MappingProxyType({})
    short_names: Collection[str] = ()
    status_schema: Mapping[str, object] | None = None
    crd_labels: Mapping[str, str] | None = None
    scope: CustomObjectScope = "namespaced"
    default_namespace: str | None = None

    @property
    def spec(self) -> CustomObjectSpec:
        """Return the bound low-level custom-object specification.

        Returns
        -------
        CustomObjectSpec
            Generic Kubernetes custom-object spec for this resource.
        """
        return CustomObjectSpec(
            group=self.group,
            version=self.version,
            kind=self.kind,
            plural=self.plural,
            scope=self.scope,
            labels=self.labels,
        )

    @property
    def client(self) -> CustomObjectClient:
        """Return a low-level Kubernetes client for this resource.

        Returns
        -------
        CustomObjectClient
            Generic Kubernetes custom-object client bound to this descriptor.
        """
        return CustomObjectClient(self.spec)

    async def ensure_crd(self, kube: Kube, *, timeout: float) -> None:
        """Converge this resource's CRD and wait until it is established.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum convergence budget in seconds.

        Raises
        ------
        TimeoutError
            If `timeout` is non-positive or establishment exceeds the budget.
        """
        message = f"{self.kind} CRD timeout must be non-negative"
        if timeout <= 0:
            raise TimeoutError(message)
        deadline = Deadline.from_timeout(timeout, message=message)
        crd = await CustomResourceDefinition.upsert(
            kube,
            group=self.group,
            version=self.version,
            plural=self.plural,
            singular=self.singular,
            kind=self.kind,
            short_names=self.short_names,
            spec_schema=self.spec_schema,
            status_schema=self.status_schema,
            labels=self.crd_labels or self.labels,
            scope="Cluster" if self.scope == "cluster" else "Namespaced",
            timeout=deadline.remaining(),
        )
        await crd.wait_established(kube, timeout=deadline.remaining())

    async def get(
        self,
        kube: Kube,
        *,
        name: str,
        timeout: float,
        namespace: str | None = None,
    ) -> T_co | None:
        """Read and validate one custom resource by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Custom resource name.
        timeout : float
            Maximum request budget in seconds.
        namespace : str | None, optional
            Namespace that owns the resource.

        Returns
        -------
        T_co | None
            Validated record, or ``None`` when the object does not exist.
        """
        obj = await self.client.get(
            kube,
            namespace=self._namespace(namespace),
            name=name,
            timeout=timeout,
        )
        if obj is None:
            return None
        return self.parser(obj.payload)

    async def list(
        self,
        kube: Kube,
        *,
        timeout: float,
        namespace: str | None = None,
        labels: Mapping[str, str] | None = None,
    ) -> builtins.list[T_co]:
        """List and validate resources using default label selectors.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum list budget in seconds.
        namespace : str | None, optional
            Optional namespace filter.
        labels : Mapping[str, str] | None, optional
            Additional exact-match labels to merge into the selector.

        Returns
        -------
        list[T_co]
            Validated records returned by Kubernetes.
        """
        objects = await self.client.list(
            kube,
            namespace=self._namespace(namespace),
            labels=self._selector(labels),
            timeout=timeout,
        )
        return [self.parser(obj.payload) for obj in objects]

    async def watch(
        self,
        kube: Kube,
        *,
        timeout: float,
        namespace: str | None = None,
        labels: Mapping[str, str] | None = None,
        resource_version: str | None = None,
        emit_initial: bool = False,
    ) -> AsyncIterator[WatchEvent[CustomObject]]:
        """Watch resources using default label selectors.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        timeout : float
            Maximum watch budget in seconds.
        namespace : str | None, optional
            Optional namespace filter.
        labels : Mapping[str, str] | None, optional
            Additional exact-match labels to merge into the selector.
        resource_version : str | None, optional
            Resource version to resume from.
        emit_initial : bool, optional
            Whether to emit current objects before live watch events.

        Yields
        ------
        WatchEvent[CustomObject]
            Watch events from the underlying Kubernetes API.
        """
        async for event in self.client.watch(
            kube,
            namespace=self._namespace(namespace),
            labels=self._selector(labels),
            timeout=timeout,
            resource_version=resource_version,
            emit_initial=emit_initial,
        ):
            yield event

    async def create(
        self,
        kube: Kube,
        *,
        name: str,
        spec: Mapping[str, object],
        timeout: float,
        namespace: str | None = None,
        labels: Mapping[str, str] | None = None,
    ) -> T_co:
        """Create and validate one custom resource.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Custom resource name.
        spec : Mapping[str, object]
            Desired resource spec payload.
        timeout : float
            Maximum creation budget in seconds.
        namespace : str | None, optional
            Namespace that owns the resource.
        labels : Mapping[str, str] | None, optional
            Additional labels to apply to the object.

        Returns
        -------
        T_co
            Validated created record.
        """
        obj = await self.client.create(
            kube,
            namespace=self._namespace(namespace),
            name=name,
            spec=spec,
            labels=labels,
            timeout=timeout,
        )
        return self.parser(obj.payload)

    async def upsert(
        self,
        kube: Kube,
        *,
        name: str,
        spec: Mapping[str, object],
        timeout: float,
        namespace: str | None = None,
        labels: Mapping[str, str] | None = None,
    ) -> T_co:
        """Create or patch and validate one custom resource.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Custom resource name.
        spec : Mapping[str, object]
            Desired resource spec payload.
        timeout : float
            Maximum upsert budget in seconds.
        namespace : str | None, optional
            Namespace that owns the resource.
        labels : Mapping[str, str] | None, optional
            Additional labels to apply to the object.

        Returns
        -------
        T_co
            Validated created or patched record.
        """
        obj = await self.client.upsert(
            kube,
            namespace=self._namespace(namespace),
            name=name,
            spec=spec,
            labels=labels,
            timeout=timeout,
        )
        return self.parser(obj.payload)

    async def patch_status(
        self,
        kube: Kube,
        *,
        name: str,
        status: Mapping[str, object],
        timeout: float,
        namespace: str | None = None,
    ) -> T_co:
        """Patch and validate one custom resource status.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Custom resource name.
        status : Mapping[str, object]
            Status payload to merge through the status subresource.
        timeout : float
            Maximum patch budget in seconds.
        namespace : str | None, optional
            Namespace that owns the resource.

        Returns
        -------
        T_co
            Validated record returned by Kubernetes.
        """
        obj = await self.client.patch_status(
            kube,
            namespace=self._namespace(namespace),
            name=name,
            status=status,
            timeout=timeout,
        )
        return self.parser(obj.payload)

    async def delete_by_name(
        self,
        kube: Kube,
        *,
        name: str,
        timeout: float,
        namespace: str | None = None,
    ) -> None:
        """Delete one custom resource by name.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        name : str
            Custom resource name.
        timeout : float
            Maximum delete budget in seconds.
        namespace : str | None, optional
            Namespace that owns the resource.
        """
        await self.client.delete_by_name(
            kube,
            namespace=self._namespace(namespace),
            name=name,
            timeout=timeout,
        )

    def _namespace(self, namespace: str | None) -> str | None:
        return namespace if namespace is not None else self.default_namespace

    def _selector(self, labels: Mapping[str, str] | None) -> dict[str, str]:
        merged = dict(self.labels)
        merged.update(labels or {})
        return merged


async def ensure_custom_resources(
    kube: Kube,
    *,
    resources: Collection[CustomResource[Any]],
    timeout: float,
) -> None:
    """Converge a sequence of custom-resource definitions.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    resources : Collection[CustomResource[Any]]
        Custom-resource descriptors to establish.
    timeout : float
        Maximum convergence budget in seconds.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive or establishment exceeds the budget.
    """
    message = "custom resource CRD timeout must be non-negative"
    if timeout <= 0:
        raise TimeoutError(message)
    deadline = Deadline.from_timeout(timeout, message=message)
    for resource in resources:
        await resource.ensure_crd(kube, timeout=deadline.remaining())
