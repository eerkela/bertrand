"""Read-only Kubernetes API view objects."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class ServicePortView:
    """Read-only Kubernetes Service port view.

    Parameters
    ----------
    name : str
        Service port name.
    port : int
        Service port number.
    target_port : int | str
        Target container port number or name.
    protocol : str
        Service port protocol.
    node_port : int | None
        Allocated or requested NodePort value, when present.
    """

    name: str
    port: int
    target_port: int | str
    protocol: str
    node_port: int | None = None


@dataclass(frozen=True)
class TaintView:
    """Read-only Kubernetes Node taint view.

    Parameters
    ----------
    key : str
        Taint key.
    effect : str
        Taint effect, such as `"NoSchedule"`.
    value : str
        Optional taint value.
    """

    key: str
    effect: str
    value: str = ""
