"""Bertrand DRA-backed device capability helpers."""

from __future__ import annotations

import asyncio
import hashlib
import json
import os
from contextlib import suppress
from typing import TYPE_CHECKING, Annotated

from pydantic import BaseModel, ConfigDict, Field, field_validator

from bertrand.env.config.core import _check_kube_name, _check_uuid
from bertrand.env.git import BERTRAND_ENV, BERTRAND_NAMESPACE, INFINITY, Deadline
from bertrand.env.kube.api.client import Kube
from bertrand.env.kube.api.spec import ContainerSpec, PodTemplateSpec
from bertrand.env.kube.custom_object import (
    CustomObject,
    CustomObjectMetadata,
    CustomObjectResource,
)
from bertrand.env.kube.daemonset import DaemonSet
from bertrand.env.kube.dra import (
    DEVICE_CLASS_PLURAL,
    DRA_GROUP,
    RESOURCE_CLAIM_PLURAL,
    RESOURCE_CLAIM_TEMPLATE_PLURAL,
    RESOURCE_SLICE_PLURAL,
    create_resource_claim_template,
    ensure_dra_api,
    upsert_device_class,
    upsert_resource_claim_template,
    upsert_resource_slice,
)
from bertrand.env.kube.rbac import (
    upsert_rbac_binding,
    upsert_rbac_role,
)
from bertrand.env.kube.service_account import ServiceAccount

if TYPE_CHECKING:
    from collections.abc import Collection, Mapping

    from bertrand.env.kube.api.spec import PodResourceClaimManifest, PolicyRuleManifest

DRA_DRIVER_NAME = "bertrand.dev"
DRA_DEVICE_CLASS = "bertrand-devices"
DRA_PROVIDER_NAME = "bertrand-dra-provider"
DRA_PROVIDER_SERVICE_ACCOUNT = DRA_PROVIDER_NAME
DRA_PROVIDER_LABEL = "bertrand.dev/dra-provider"
DRA_PROVIDER_LABEL_VALUE = "v1"
DRA_NODE_ENV = "NODE_NAME"
DRA_SYNC_SECONDS = 30.0
DRA_DEVICE_METADATA_ROOT = "/var/run/kubernetes.io/dra-device-attributes"
DRA_CDI_SELECTOR_ATTRIBUTE = "bertrand.dev/cdiSelector"

BERTRAND_DEVICE_GROUP = "dra.bertrand.dev"
BERTRAND_DEVICE_VERSION = "v1alpha1"
BERTRAND_DEVICE_KIND = "BertrandDevice"
BERTRAND_DEVICE_PLURAL = "bertranddevices"
BERTRAND_DEVICE_LABEL = "bertrand.dev/dra-device"
BERTRAND_DEVICE_LABEL_VALUE = "v1"
BERTRAND_DEVICE_CAPABILITY_LABEL = "bertrand.dev/dra-device-capability"
BERTRAND_DEVICE_HOST_LABEL = "bertrand.dev/dra-device-host"
BERTRAND_DEVICE_NODE_LABEL = "bertrand.dev/dra-device-node"

_DRA_LABELS = {
    BERTRAND_ENV: "1",
    DRA_PROVIDER_LABEL: DRA_PROVIDER_LABEL_VALUE,
}
_BERTRAND_DEVICE_LABELS = {
    BERTRAND_ENV: "1",
    BERTRAND_DEVICE_LABEL: BERTRAND_DEVICE_LABEL_VALUE,
}

type _NonEmptyString = Annotated[str, Field(min_length=1)]


class _BertrandDeviceSpec(BaseModel):
    model_config = ConfigDict(extra="forbid", frozen=True)
    capability_id: _NonEmptyString
    host_id: _NonEmptyString
    node_name: _NonEmptyString
    device_name: _NonEmptyString
    cdi_selector: _NonEmptyString
    attributes: dict[str, str] = Field(default_factory=dict)

    @field_validator("capability_id", "device_name")
    @classmethod
    def _validate_name(cls, value: str) -> str:
        return _check_kube_name(value.strip())

    @field_validator("host_id")
    @classmethod
    def _validate_host_id(cls, value: str) -> str:
        return _check_uuid(value.strip())

    @field_validator("node_name")
    @classmethod
    def _validate_text(cls, value: str) -> str:
        text = value.strip()
        if not text:
            msg = "DRA device inventory text fields cannot be empty"
            raise ValueError(msg)
        return text

    @field_validator("cdi_selector")
    @classmethod
    def _validate_cdi_selector(cls, value: str) -> str:
        text = value.strip()
        if not text:
            msg = "DRA CDI selector cannot be empty"
            raise ValueError(msg)
        if any(char.isspace() for char in text):
            msg = f"DRA CDI selector cannot contain whitespace: {text!r}"
            raise ValueError(msg)
        return text

    @field_validator("attributes")
    @classmethod
    def _normalize_attributes(cls, value: dict[str, str]) -> dict[str, str]:
        return {
            key.strip(): str(item) for key, item in sorted(value.items()) if key.strip()
        }


class BertrandDeviceRecord(BaseModel):
    """Managed node-scoped DRA device inventory record.

    Parameters
    ----------
    metadata : CustomObjectMetadata
        Kubernetes metadata for the inventory record.
    spec : _BertrandDeviceSpec
        Validated device inventory payload.
    """

    model_config = ConfigDict(extra="ignore", frozen=True, populate_by_name=True)
    api_version: str = Field(default="", alias="apiVersion")
    kind: str = ""
    metadata: CustomObjectMetadata
    spec: _BertrandDeviceSpec

    @property
    def name(self) -> str:
        """Return the Kubernetes record name.

        Returns
        -------
        str
            Kubernetes `metadata.name`.
        """
        return self.metadata.name

    @property
    def capability_id(self) -> str:
        """Return the host-agnostic capability ID.

        Returns
        -------
        str
            Capability ID requested by project config.
        """
        return self.spec.capability_id

    @property
    def host_id(self) -> str:
        """Return the Bertrand host UUID that owns this device.

        Returns
        -------
        str
            Durable Bertrand host UUID.
        """
        return self.spec.host_id

    @property
    def node_name(self) -> str:
        """Return the Kubernetes node that owns this device.

        Returns
        -------
        str
            Kubernetes node name.
        """
        return self.spec.node_name

    @property
    def cdi_selector(self) -> str:
        """Return the CDI selector exposed after DRA allocation.

        Returns
        -------
        str
            CDI selector for BuildKit or container runtime device projection.
        """
        return self.spec.cdi_selector


BERTRAND_DEVICE_RESOURCE = CustomObjectResource[BertrandDeviceRecord](
    group=BERTRAND_DEVICE_GROUP,
    version=BERTRAND_DEVICE_VERSION,
    kind=BERTRAND_DEVICE_KIND,
    plural=BERTRAND_DEVICE_PLURAL,
    scope="cluster",
    labels=_BERTRAND_DEVICE_LABELS,
    singular="bertranddevice",
    payload_parser=BertrandDeviceRecord.model_validate,
    payload_error_context=f"{BERTRAND_DEVICE_KIND} payload",
    spec_model=_BertrandDeviceSpec,
)


async def ensure_dra_backend(
    kube: Kube,
    *,
    image: str,
    timeout: float,
) -> None:
    """Converge Bertrand's backend-only DRA device publisher.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    image : str
        Bertrand control-plane image used by the node publisher.
    timeout : float
        Maximum convergence budget in seconds.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    ValueError
        If `image` is empty.
    """
    image = image.strip()
    if not image:
        msg = "DRA provider image cannot be empty"
        raise ValueError(msg)
    message = "DRA backend convergence timeout must be non-negative"
    if timeout <= 0:
        raise TimeoutError(message)
    deadline = Deadline.from_timeout(timeout, message=message)
    await ensure_dra_api(kube, timeout=deadline.remaining())
    await BERTRAND_DEVICE_RESOURCE.ensure_crd(kube, timeout=deadline.remaining())
    await upsert_device_class(
        kube,
        name=DRA_DEVICE_CLASS,
        spec=_device_class_spec(),
        labels=_DRA_LABELS,
        timeout=deadline.remaining(),
    )
    await ServiceAccount.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=DRA_PROVIDER_SERVICE_ACCOUNT,
        labels=_DRA_LABELS,
        timeout=deadline.remaining(),
    )
    await upsert_rbac_role(
        kube,
        kind="ClusterRole",
        name=DRA_PROVIDER_NAME,
        labels=_DRA_LABELS,
        rules=_provider_rules(),
        timeout=deadline.remaining(),
    )
    await upsert_rbac_binding(
        kube,
        kind="ClusterRoleBinding",
        name=DRA_PROVIDER_NAME,
        role_kind="ClusterRole",
        role_name=DRA_PROVIDER_NAME,
        service_account_name=DRA_PROVIDER_SERVICE_ACCOUNT,
        service_account_namespace=BERTRAND_NAMESPACE,
        labels=_DRA_LABELS,
        timeout=deadline.remaining(),
    )
    daemonset = await DaemonSet.upsert(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=DRA_PROVIDER_NAME,
        labels=_DRA_LABELS,
        selector={DRA_PROVIDER_LABEL: DRA_PROVIDER_LABEL_VALUE},
        pod_template=PodTemplateSpec(
            containers=[
                ContainerSpec(
                    name="publisher",
                    image=image,
                    image_pull_policy="IfNotPresent",
                    command=[
                        "python",
                        "-m",
                        "bertrand.env.kube.capability.device",
                        "agent",
                    ],
                    env=[
                        {
                            "name": DRA_NODE_ENV,
                            "valueFrom": {"fieldRef": {"fieldPath": "spec.nodeName"}},
                        }
                    ],
                )
            ],
            service_account_name=DRA_PROVIDER_SERVICE_ACCOUNT,
            automount_service_account_token=True,
            node_selector={"kubernetes.io/os": "linux"},
        ),
        timeout=deadline.remaining(),
    )
    await daemonset.wait_rollout(kube, timeout=deadline.remaining())


async def list_device_inventory(
    kube: Kube,
    *,
    capability_id: str | None = None,
    host_ids: Collection[str] | None = None,
    node_names: Collection[str] | None = None,
    timeout: float,
) -> list[BertrandDeviceRecord]:
    """List managed DRA device inventory records.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    capability_id : str | None, optional
        Optional capability ID filter.
    host_ids : Collection[str] | None, optional
        Optional Bertrand host UUID filter applied client-side.
    node_names : Collection[str] | None, optional
        Optional node-name filter applied client-side.
    timeout : float
        Maximum request budget in seconds.

    Returns
    -------
    list[BertrandDeviceRecord]
        Validated inventory records.
    """
    labels: dict[str, str] = {}
    if capability_id is not None:
        labels[BERTRAND_DEVICE_CAPABILITY_LABEL] = _label_value(
            _check_kube_name(capability_id)
        )
    allowed_hosts = {_check_uuid(host_id) for host_id in host_ids or ()}
    if len(allowed_hosts) == 1:
        labels[BERTRAND_DEVICE_HOST_LABEL] = _label_value(next(iter(allowed_hosts)))
    records = await BERTRAND_DEVICE_RESOURCE.list(
        kube,
        labels=labels,
        timeout=timeout,
    )
    allowed_nodes = {name.strip() for name in node_names or () if name.strip()}
    if allowed_hosts:
        records = [record for record in records if record.host_id in allowed_hosts]
    if allowed_nodes:
        records = [record for record in records if record.node_name in allowed_nodes]
    return sorted(
        records,
        key=lambda item: (item.capability_id, item.host_id, item.node_name),
    )


async def upsert_device_inventory(
    kube: Kube,
    *,
    capability_id: str,
    host_id: str,
    node_name: str,
    device_name: str,
    cdi_selector: str,
    timeout: float,
    attributes: Mapping[str, str] | None = None,
) -> BertrandDeviceRecord:
    """Create or update one managed DRA device inventory record.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    capability_id : str
        Host-agnostic device capability ID.
    host_id : str
        Durable Bertrand host UUID that owns the concrete device.
    node_name : str
        Kubernetes node that owns the concrete device.
    device_name : str
        Node-local DRA device name.
    cdi_selector : str
        CDI selector exposed after allocation.
    timeout : float
        Maximum request budget in seconds.
    attributes : Mapping[str, str] | None, optional
        Additional string attributes published on the ResourceSlice device.

    Returns
    -------
    BertrandDeviceRecord
        Validated inventory record returned by Kubernetes.
    """
    spec = _BertrandDeviceSpec(
        capability_id=capability_id,
        host_id=host_id,
        node_name=node_name,
        device_name=device_name,
        cdi_selector=cdi_selector,
        attributes=dict(attributes or {}),
    )
    return await BERTRAND_DEVICE_RESOURCE.upsert(
        kube,
        name=_device_inventory_name(spec),
        spec=spec,
        labels={
            BERTRAND_DEVICE_CAPABILITY_LABEL: _label_value(spec.capability_id),
            BERTRAND_DEVICE_HOST_LABEL: _label_value(spec.host_id),
            BERTRAND_DEVICE_NODE_LABEL: _label_value(spec.node_name),
        },
        timeout=timeout,
    )


async def delete_device_inventory(
    kube: Kube,
    *,
    capability_id: str,
    host_id: str,
    node_name: str,
    device_name: str,
    timeout: float,
) -> bool:
    """Delete one managed DRA device inventory record.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    capability_id : str
        Host-agnostic device capability ID.
    host_id : str
        Durable Bertrand host UUID that owns the concrete device.
    node_name : str
        Kubernetes node that owns the concrete device.
    device_name : str
        Node-local DRA device name.
    timeout : float
        Maximum request budget in seconds.

    Returns
    -------
    bool
        Whether a matching inventory record was deleted.
    """
    spec = _BertrandDeviceSpec(
        capability_id=capability_id,
        host_id=host_id,
        node_name=node_name,
        device_name=device_name,
        cdi_selector="placeholder.invalid/device=0",
    )
    name = _device_inventory_name(spec)
    records = await list_device_inventory(
        kube,
        capability_id=spec.capability_id,
        host_ids=(spec.host_id,),
        node_names=(spec.node_name,),
        timeout=timeout,
    )
    if not any(
        record.name == name and record.spec.device_name == spec.device_name
        for record in records
    ):
        return False
    await BERTRAND_DEVICE_RESOURCE.delete_by_name(
        kube,
        name=name,
        timeout=timeout,
    )
    return True


async def delete_device_inventory_for_host(
    kube: Kube,
    *,
    host_id: str,
    timeout: float,
) -> tuple[BertrandDeviceRecord, ...]:
    """Delete managed DRA inventory records owned by one Bertrand host UUID.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    host_id : str
        Durable Bertrand host UUID whose local inventory should be removed.
    timeout : float
        Maximum deletion budget.

    Returns
    -------
    tuple[BertrandDeviceRecord, ...]
        Inventory records deleted from the cluster.
    """
    records = await list_device_inventory(
        kube,
        host_ids=(_check_uuid(host_id),),
        timeout=timeout,
    )
    for record in records:
        await BERTRAND_DEVICE_RESOURCE.delete_by_name(
            kube,
            name=record.name,
            timeout=timeout,
        )
    return tuple(records)


async def refresh_node_resource_slice(
    kube: Kube,
    *,
    node_name: str,
    timeout: float,
) -> None:
    """Refresh the managed ResourceSlice for one Kubernetes node.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    node_name : str
        Kubernetes node name whose local inventory should be published.
    timeout : float
        Maximum request budget in seconds.
    """
    await _publish_node_slice(kube, node_name=node_name, timeout=timeout)


async def select_device_claims(
    kube: Kube,
    *,
    requests: Mapping[str, bool],
    host_ids: Collection[str] | None = None,
    node_names: Collection[str] | None = None,
    timeout: float,
) -> tuple[str, ...]:
    """Return selected DRA device capability IDs.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    requests : Mapping[str, bool]
        Capability IDs mapped to `required` flags.
    host_ids : Collection[str] | None, optional
        Optional Bertrand host UUID filter for preflight validation.
    node_names : Collection[str] | None, optional
        Optional candidate node filter for preflight validation.
    timeout : float
        Maximum request budget in seconds.

    Returns
    -------
    tuple[str, ...]
        Required and available optional device capability IDs.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    OSError
        If a required device capability has no matching inventory.
    """
    message = "DRA device request resolution timeout must be non-negative"
    if timeout <= 0:
        raise TimeoutError(message)
    deadline = Deadline.from_timeout(timeout, message=message)
    selected: list[str] = []
    for raw_id, required in sorted(requests.items()):
        capability_id = _check_kube_name(str(raw_id))
        inventory = await list_device_inventory(
            kube,
            capability_id=capability_id,
            host_ids=host_ids,
            node_names=node_names,
            timeout=deadline.remaining(),
        )
        if not inventory:
            if required:
                locations: list[str] = []
                if host_ids is not None:
                    locations.append(f"host(s) {', '.join(sorted(host_ids))}")
                if node_names is not None:
                    locations.append(f"node(s) {', '.join(sorted(node_names))}")
                where = f" on candidate {' / '.join(locations)}" if locations else ""
                msg = (
                    f"required DRA device capability {capability_id!r} has no "
                    f"managed inventory{where}"
                )
                raise OSError(msg)
            continue
        selected.append(capability_id)
    return tuple(selected)


def resource_claim_name(
    *,
    owner: str,
    capability_id: str,
    container_name: str | None = None,
) -> str:
    """Return the deterministic pod-local DRA claim name.

    Parameters
    ----------
    owner : str
        Stable owner string used to derive names.
    capability_id : str
        Device capability ID matched by the claim.
    container_name : str | None, optional
        Container that should reference the claim.

    Returns
    -------
    str
        Pod-local claim name.
    """
    return _claim_name(
        _claim_owner(owner),
        _check_kube_name(capability_id),
        container_name,
    )


def resource_claim_template_name(
    *,
    owner: str,
    capability_id: str,
    container_name: str | None = None,
) -> str:
    """Return the deterministic DRA ResourceClaimTemplate name.

    Returns
    -------
    str
        Namespaced ResourceClaimTemplate name.
    """
    return _template_name(
        _claim_owner(owner),
        _check_kube_name(capability_id),
        container_name,
    )


def pod_resource_claim(
    *,
    owner: str,
    capability_id: str,
    container_name: str | None = None,
) -> PodResourceClaimManifest:
    """Render one pod-level DRA resource-claim reference.

    Returns
    -------
    PodResourceClaimManifest
        Pod resource-claim entry referencing a deterministic template.
    """
    return {
        "name": resource_claim_name(
            owner=owner,
            capability_id=capability_id,
            container_name=container_name,
        ),
        "resourceClaimTemplateName": resource_claim_template_name(
            owner=owner,
            capability_id=capability_id,
            container_name=container_name,
        ),
    }


def _claim_owner(owner: str) -> str:
    owner = owner.strip()
    if not owner:
        msg = "DRA resource claim owner cannot be empty"
        raise ValueError(msg)
    return owner


async def create_resource_claim_templates(
    kube: Kube,
    *,
    namespace: str,
    owner: str,
    capability_ids: Collection[str],
    container_name: str | None = None,
    labels: Mapping[str, str],
    timeout: float,
) -> tuple[CustomObject, ...]:
    """Create ResourceClaimTemplates for a pod or Job.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    namespace : str
        Namespace that owns the templates.
    owner : str
        Stable owner string used to derive template names.
    capability_ids : Collection[str]
        Device capability IDs to create claim templates for.
    container_name : str | None, optional
        Container that should reference the templates.
    labels : Mapping[str, str]
        Labels to apply to each template.
    timeout : float
        Maximum creation budget in seconds.

    Returns
    -------
    tuple[CustomObject, ...]
        Created templates.
    """
    capability_ids = tuple(sorted(_check_kube_name(item) for item in capability_ids))
    if not capability_ids:
        return ()
    deadline = Deadline.from_timeout(
        timeout,
        message="DRA ResourceClaimTemplate creation timeout must be positive",
    )
    created: list[CustomObject] = []
    template_labels = dict(_DRA_LABELS)
    template_labels.update(labels)
    for capability_id in capability_ids:
        template = await create_resource_claim_template(
            kube,
            namespace=namespace,
            name=resource_claim_template_name(
                owner=owner,
                capability_id=capability_id,
                container_name=container_name,
            ),
            spec={"spec": _resource_claim_spec(capability_id)},
            labels=template_labels,
            timeout=deadline.remaining(),
        )
        created.append(template)
    return tuple(created)


async def upsert_resource_claim_templates(
    kube: Kube,
    *,
    namespace: str,
    owner: str,
    capability_ids: Collection[str],
    container_name: str | None = None,
    labels: Mapping[str, str],
    timeout: float,
) -> tuple[CustomObject, ...]:
    """Create or patch ResourceClaimTemplates for a controller-backed workload.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    namespace : str
        Namespace that owns the templates.
    owner : str
        Stable owner string used to derive template names.
    capability_ids : Collection[str]
        Device capability IDs to converge claim templates for.
    container_name : str | None, optional
        Container that should reference the templates.
    labels : Mapping[str, str]
        Labels to apply to each template.
    timeout : float
        Maximum convergence budget in seconds.

    Returns
    -------
    tuple[CustomObject, ...]
        Converged templates.
    """
    capability_ids = tuple(sorted(_check_kube_name(item) for item in capability_ids))
    if not capability_ids:
        return ()
    deadline = Deadline.from_timeout(
        timeout,
        message="DRA ResourceClaimTemplate convergence timeout must be positive",
    )
    rendered: list[CustomObject] = []
    template_labels = dict(_DRA_LABELS)
    template_labels.update(labels)
    for capability_id in capability_ids:
        template = await upsert_resource_claim_template(
            kube,
            namespace=namespace,
            name=resource_claim_template_name(
                owner=owner,
                capability_id=capability_id,
                container_name=container_name,
            ),
            spec={"spec": _resource_claim_spec(capability_id)},
            labels=template_labels,
            timeout=deadline.remaining(),
        )
        rendered.append(template)
    return tuple(rendered)


def allocated_selector_script(*, required_count: int) -> str:
    """Return shell that appends allocated DRA selectors to `buildctl` args.

    Parameters
    ----------
    required_count : int
        Minimum number of CDI selectors expected for the current build job.

    Returns
    -------
    str
        POSIX shell fragment.
    """
    metadata_root = json.dumps(DRA_DEVICE_METADATA_ROOT)
    selector_attr = DRA_CDI_SELECTOR_ATTRIBUTE.replace("/", "\\/")
    return "\n".join(
        (
            f"DRA_METADATA_ROOT={metadata_root}",
            "selectors=''",
            'if [ -d "$DRA_METADATA_ROOT" ]; then',
            "    selectors=$(find \"$DRA_METADATA_ROOT\" -type f "
            "-name '*-metadata.json' 2>/dev/null | sort | "
            "while IFS= read -r file; do",
            "        tr '\\n' ' ' < \"$file\" | sed -n "
            f"'s/.*\"{selector_attr}\"[[:space:]]*:[[:space:]]*"
            "{[^}]*\"string\"[[:space:]]*:[[:space:]]*"
            "\"\\([^\"]*\\)\".*/\\1/p'",
            "    done | sort -u)",
            "fi",
            (
                "selectors=$(printf '%s\\n' \"$selectors\" | "
                "sed '/^[[:space:]]*$/d' | sort -u)"
            ),
            "selector_count=$(printf '%s\\n' \"$selectors\" | "
            "sed '/^[[:space:]]*$/d' | wc -l | tr -d '[:space:]')",
            (
                f"if [ \"$selector_count\" -lt {required_count!r} ]; then "
                "echo \"DRA did not expose the expected allocated CDI selectors\" >&2; "
                "exit 1; "
                "fi"
                if required_count
                else ":"
            ),
            "for selector in $selectors; do",
            "    set -- \"$@\" --allow \"device=$selector\"",
            "done",
        )
    )


async def run_dra_provider_agent(*, timeout: float = INFINITY) -> None:
    """Run the ResourceSlice publisher loop for one node.

    Parameters
    ----------
    timeout : float, optional
        Maximum runtime budget in seconds. If infinite, run indefinitely.

    Raises
    ------
    TimeoutError
        If `timeout` is non-positive.
    OSError
        If the node name cannot be resolved.
    """
    message = "DRA provider agent timeout must be positive"
    if timeout <= 0:
        raise TimeoutError(message)
    deadline = Deadline.from_timeout(timeout, message=message)
    node_name = os.environ.get(DRA_NODE_ENV, "").strip()
    if not node_name:
        msg = "DRA provider agent requires NODE_NAME from the Downward API"
        raise OSError(msg)
    with Kube.inside_cluster(namespace=BERTRAND_NAMESPACE) as kube:
        while True:
            remaining = deadline.remaining()
            if remaining <= 0:
                return
            with suppress(OSError, TimeoutError, ValueError):
                await _publish_node_slice(
                    kube,
                    node_name=node_name,
                    timeout=min(DRA_SYNC_SECONDS, remaining),
                )
            await asyncio.sleep(deadline.bounded(DRA_SYNC_SECONDS))


def main() -> int:
    """Run the DRA provider module entrypoint.

    Returns
    -------
    int
        Process exit status.
    """
    try:
        asyncio.run(run_dra_provider_agent())
    except KeyboardInterrupt:
        return 0
    return 0


async def _publish_node_slice(
    kube: Kube,
    *,
    node_name: str,
    timeout: float,
) -> None:
    records = await list_device_inventory(
        kube,
        node_names=(node_name,),
        timeout=timeout,
    )
    node_name = node_name.strip()
    if not node_name:
        msg = "ResourceSlice publication requires a node name"
        raise OSError(msg)
    await upsert_resource_slice(
        kube,
        name=_resource_slice_name(node_name),
        spec=_resource_slice_spec(node_name, records),
        labels={**_DRA_LABELS, BERTRAND_DEVICE_NODE_LABEL: _label_value(node_name)},
        timeout=timeout,
    )


def _provider_rules() -> tuple[PolicyRuleManifest, ...]:
    return (
        {
            "apiGroups": [DRA_GROUP],
            "resources": [
                DEVICE_CLASS_PLURAL,
                RESOURCE_SLICE_PLURAL,
                RESOURCE_CLAIM_PLURAL,
                RESOURCE_CLAIM_TEMPLATE_PLURAL,
            ],
            "verbs": ["get", "list", "watch", "create", "update", "patch", "delete"],
        },
        {
            "apiGroups": [BERTRAND_DEVICE_GROUP],
            "resources": [BERTRAND_DEVICE_PLURAL],
            "verbs": ["get", "list", "watch"],
        },
    )


def _device_class_spec() -> dict[str, object]:
    return {
        "selectors": [
            {"cel": {"expression": (f'device.driver == "{DRA_DRIVER_NAME}"')}}
        ],
    }


def _resource_claim_spec(capability_id: str) -> dict[str, object]:
    capability_id = _check_kube_name(capability_id)
    expression = (
        f'device.driver == "{DRA_DRIVER_NAME}" && '
        f'device.attributes["bertrand.dev/capability"].string == "{capability_id}"'
    )
    return {
        "devices": {
            "requests": [
                {
                    "name": capability_id,
                    "exactly": {
                        "deviceClassName": DRA_DEVICE_CLASS,
                        "allocationMode": "ExactCount",
                        "count": 1,
                        "selectors": [{"cel": {"expression": expression}}],
                    },
                }
            ]
        }
    }


def _resource_slice_spec(
    node_name: str,
    devices: Collection[BertrandDeviceRecord],
) -> dict[str, object]:
    entries = [
        {
            "name": record.spec.device_name,
            "attributes": _resource_slice_attributes(record),
        }
        for record in sorted(
            devices,
            key=lambda item: (item.capability_id, item.spec.device_name),
        )
    ]
    return {
        "driver": DRA_DRIVER_NAME,
        "pool": {
            "name": node_name,
            "generation": 1,
            "resourceSliceCount": 1,
        },
        "nodeName": node_name,
        "devices": entries,
    }


def _resource_slice_attributes(
    record: BertrandDeviceRecord,
) -> dict[str, dict[str, str]]:
    attributes = {
        "bertrand.dev/capability": {"string": record.capability_id},
        "bertrand.dev/hostID": {"string": record.host_id},
        "bertrand.dev/cdiSelector": {"string": record.cdi_selector},
    }
    for key, value in record.spec.attributes.items():
        attributes[f"bertrand.dev/{key}"] = {"string": value}
    return attributes


def _claim_name(owner: str, capability_id: str, container_name: str | None) -> str:
    return f"dra-{_name_digest(owner, capability_id, container_name)[:24]}"


def _template_name(owner: str, capability_id: str, container_name: str | None) -> str:
    return f"dra-template-{_name_digest(owner, capability_id, container_name)[:18]}"


def _resource_slice_name(node_name: str) -> str:
    return f"bertrand-dra-{_label_value(node_name)}-{_hash(node_name)[:12]}"


def _device_inventory_name(spec: _BertrandDeviceSpec) -> str:
    digest = _name_digest(spec.host_id, spec.capability_id, spec.device_name)[:24]
    return f"bertrand-device-{digest}"


def _name_digest(owner: str, capability_id: str, container_name: str | None) -> str:
    payload = json.dumps(
        {
            "owner": owner,
            "capability_id": capability_id,
            "container_name": container_name or "",
        },
        sort_keys=True,
        separators=(",", ":"),
    )
    return _hash(payload)


def _hash(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def _label_value(value: str) -> str:
    text = value.strip().lower()
    out = [char if char.isalnum() or char in ".-_" else "-" for char in text]
    return "".join(out).strip(".-_")[:63] or _hash(value)[:16]


if __name__ == "__main__":
    raise SystemExit(main())
