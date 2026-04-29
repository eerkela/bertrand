"""Shared node/topology primitives for Bertrand's Kubernetes runtime."""
from __future__ import annotations

import os
import platform
from typing import Self

from pydantic import BaseModel, ConfigDict, Field, ValidationError

from ..run import kubectl

CLUSTER_REGISTRY_READY_LABEL = "bertrand.dev/registry-ready"
CLUSTER_REGISTRY_READY_VALUE = "true"


class NodeAddress(BaseModel):
    """Validated subset of one Kubernetes node address entry."""

    model_config = ConfigDict(extra="ignore")
    type: str = ""
    address: str = ""


class NodeStatus(BaseModel):
    """Validated subset of Kubernetes node status payload."""

    model_config = ConfigDict(extra="ignore")
    addresses: list[NodeAddress] = Field(default_factory=list)


class NodeMetadata(BaseModel):
    """Validated subset of Kubernetes node metadata payload."""

    model_config = ConfigDict(extra="ignore")
    name: str = ""
    labels: dict[str, str] = Field(default_factory=dict)


class Node(BaseModel):
    """Validated subset of one Kubernetes node payload."""

    model_config = ConfigDict(extra="ignore")
    metadata: NodeMetadata
    status: NodeStatus = Field(default_factory=NodeStatus)


class NodeList(BaseModel):
    """Validated subset of Kubernetes node list payload."""

    model_config = ConfigDict(extra="ignore")
    items: list[Node] = Field(default_factory=list)

    @classmethod
    def parse(cls, payload: str | dict, *, context: str) -> Self:
        """Validate a Kubernetes node list payload.

        Parameters
        ----------
        payload : str | dict
            Raw JSON payload as text or decoded object.
        context : str
            Error-context label for diagnostics.

        Returns
        -------
        NodeList
            Validated node list payload.

        Raises
        ------
        OSError
            If payload validation fails.
        """

        try:
            if isinstance(payload, str):
                return cls.model_validate_json(payload)
            return cls.model_validate(payload)
        except ValidationError as err:
            raise OSError(f"malformed {context} payload: {err}") from err


async def list_nodes(*, timeout: float) -> NodeList:
    """Fetch cluster nodes via kubectl and validate structure."""

    result = await kubectl(
        ["get", "nodes", "-o", "json"],
        capture_output=True,
        timeout=timeout,
    )
    return NodeList.parse(result.stdout, context="node list")


def local_node_name(nodes: NodeList) -> str:
    """Resolve local host identity to one Kubernetes node name."""

    local_hints = {
        platform.node().strip(),
        os.uname().nodename.strip() if hasattr(os, "uname") else "",
        os.environ.get("HOSTNAME", "").strip(),
    }
    local_hints.discard("")

    for node in nodes.items:
        if node.metadata.name in local_hints:
            return node.metadata.name
        hostname = node.metadata.labels.get("kubernetes.io/hostname", "")
        if hostname in local_hints:
            return node.metadata.name
        for address in node.status.addresses:
            if address.address in local_hints:
                return node.metadata.name

    if len(nodes.items) == 1:
        return nodes.items[0].metadata.name

    names = ", ".join(sorted(node.metadata.name for node in nodes.items))
    raise OSError(
        "unable to map host identity to a unique Kubernetes node name; available "
        f"nodes: {names}"
    )


async def label_local_node(
    *,
    label: str,
    value: str,
    timeout: float,
) -> None:
    """Apply or overwrite one label on the local Kubernetes node."""

    local = local_node_name(await list_nodes(timeout=timeout))
    await kubectl(
        [
            "label",
            "node",
            local,
            f"{label}={value}",
            "--overwrite",
        ],
        timeout=timeout,
    )


def nodes_with_label(nodes: NodeList, *, label: str, value: str) -> list[str]:
    """Return sorted node names that match a required label value."""

    return sorted(
        node.metadata.name
        for node in nodes.items
        if node.metadata.labels.get(label) == value
    )


async def assert_nodes_labeled(
    *,
    label: str,
    value: str,
    timeout: float,
    context: str,
) -> None:
    """Fail closed if any cluster node lacks the expected label value."""

    nodes = await list_nodes(timeout=timeout)
    missing = sorted(
        node.metadata.name
        for node in nodes.items
        if node.metadata.labels.get(label) != value
    )
    if missing:
        raise OSError(
            f"{context}: required node label {label}={value} is missing on node(s): "
            f"{', '.join(missing)}"
        )
