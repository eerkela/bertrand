"""Typed CDI device capability payloads."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Literal

from pydantic import BaseModel, ConfigDict, ValidationError, field_validator

from bertrand.env.config.core import _check_kube_name

if TYPE_CHECKING:
    from bertrand.env.config.core import KubeName


@dataclass(frozen=True)
class DeviceCapability:
    """Resolved CDI device capability.

    Parameters
    ----------
    capability_id : KubeName
        Host-agnostic device capability ID from project configuration.
    selector : str
        CDI device selector requested from BuildKit or another CDI-aware runtime.
    """

    capability_id: KubeName
    selector: str

    def __post_init__(self) -> None:
        """Validate and normalize the resolved CDI capability."""
        object.__setattr__(
            self,
            "capability_id",
            _check_kube_name(self.capability_id),
        )
        object.__setattr__(self, "selector", _check_cdi_selector(self.selector))


class _DevicePayload(BaseModel):
    model_config = ConfigDict(extra="forbid")
    version: Literal[1]
    source: Literal["cdi"]
    selector: str

    @field_validator("selector")
    @classmethod
    def _validate_selector(cls, selector: str) -> str:
        return _check_cdi_selector(selector)


def _parse_device_selector(payload: bytes, *, capability_id: str) -> str:
    try:
        parsed = _DevicePayload.model_validate_json(payload)
    except ValidationError as err:
        msg = f"invalid CDI device capability {capability_id!r} payload: {err}"
        raise ValueError(msg) from err
    return parsed.selector


def _check_cdi_selector(selector: str) -> str:
    selector = selector.strip()
    if not selector:
        msg = "CDI device selector cannot be empty"
        raise ValueError(msg)
    if any(char.isspace() for char in selector):
        msg = f"CDI device selector cannot contain whitespace: {selector!r}"
        raise ValueError(msg)
    return selector
