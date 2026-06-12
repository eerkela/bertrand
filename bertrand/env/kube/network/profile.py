"""Cluster-owned networking profile for Bertrand's Kubernetes control plane."""

from __future__ import annotations

import hashlib
import ipaddress
import json
import re
from typing import TYPE_CHECKING, Self

from pydantic import BaseModel, ConfigDict, Field, ValidationError, field_validator

from bertrand.env.git import (
    BERTRAND_LABEL,
    BERTRAND_LABEL_MANAGED,
    BERTRAND_NAMESPACE,
    Deadline,
)
from bertrand.env.kube.configmap import ConfigMap, ConfigMapManifest

if TYPE_CHECKING:
    from bertrand.env.kube.api.client import Kube

NETWORK_PROFILE_NAME = "bertrand-network"
NETWORK_PROFILE_KEY = "profile.json"
NETWORK_PROFILE_LABEL = "bertrand.dev/network-profile"
NETWORK_PROFILE_LABEL_VALUE = "v1"
NETWORK_PROFILE_LABELS = {
    "app.kubernetes.io/name": NETWORK_PROFILE_NAME,
    "app.kubernetes.io/part-of": "bertrand",
    BERTRAND_LABEL: BERTRAND_LABEL_MANAGED,
    NETWORK_PROFILE_LABEL: NETWORK_PROFILE_LABEL_VALUE,
}
DNS_SEARCH_DOMAIN_RE = re.compile(
    r"^[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?"
    r"(?:\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*$"
)


def _dedupe(values: tuple[str, ...], *, field: str) -> tuple[str, ...]:
    seen: set[str] = set()
    deduped: list[str] = []
    for value in values:
        if value in seen:
            msg = f"duplicate network {field}: {value!r}"
            raise ValueError(msg)
        seen.add(value)
        deduped.append(value)
    return tuple(deduped)


def _toml_array(values: tuple[str, ...]) -> str:
    return "[" + ", ".join(json.dumps(value) for value in values) + "]"


class NetworkProfile(BaseModel):
    """Cluster-level networking profile for Bertrand-managed infrastructure.

    Parameters
    ----------
    nameservers : tuple[str, ...], optional
        DNS nameserver IP addresses exposed to BuildKit daemon workers.
    search_domains : tuple[str, ...], optional
        DNS search domains exposed to BuildKit daemon workers.
    options : tuple[str, ...], optional
        Resolver options exposed to BuildKit daemon workers.
    """

    model_config = ConfigDict(extra="forbid", frozen=True)
    nameservers: tuple[str, ...] = Field(default_factory=tuple)
    search_domains: tuple[str, ...] = Field(default_factory=tuple)
    options: tuple[str, ...] = Field(default_factory=tuple)

    @field_validator("nameservers")
    @classmethod
    def _validate_nameservers(cls, values: tuple[str, ...]) -> tuple[str, ...]:
        normalized: list[str] = []
        for value in values:
            try:
                normalized.append(str(ipaddress.ip_address(value.strip())))
            except ValueError as err:
                msg = f"invalid DNS nameserver IP address: {value!r}"
                raise ValueError(msg) from err
        return _dedupe(tuple(normalized), field="nameserver")

    @field_validator("search_domains")
    @classmethod
    def _validate_search_domains(cls, values: tuple[str, ...]) -> tuple[str, ...]:
        normalized: list[str] = []
        for value in values:
            domain = value.strip().lower().rstrip(".")
            if not domain or not DNS_SEARCH_DOMAIN_RE.fullmatch(domain):
                msg = f"invalid DNS search domain: {value!r}"
                raise ValueError(msg)
            normalized.append(domain)
        return _dedupe(tuple(normalized), field="search domain")

    @field_validator("options")
    @classmethod
    def _validate_options(cls, values: tuple[str, ...]) -> tuple[str, ...]:
        normalized: list[str] = []
        for value in values:
            option = value.strip()
            if not option or any(char.isspace() for char in option):
                msg = f"invalid DNS resolver option: {value!r}"
                raise ValueError(msg)
            normalized.append(option)
        return _dedupe(tuple(normalized), field="resolver option")

    @property
    def json_data(self) -> str:
        """Return deterministic JSON data for ConfigMap storage.

        Returns
        -------
        str
            Serialized profile payload.
        """
        return json.dumps(
            self.model_dump(mode="json"),
            sort_keys=True,
            separators=(",", ":"),
        )

    @property
    def profile_hash(self) -> str:
        """Return a deterministic hash for this profile.

        Returns
        -------
        str
            SHA-256 digest of the serialized profile payload.
        """
        return hashlib.sha256(self.json_data.encode("utf-8")).hexdigest()

    @property
    def has_dns(self) -> bool:
        """Return whether this profile contains BuildKit DNS configuration.

        Returns
        -------
        bool
            `True` when any DNS field is set.
        """
        return bool(self.nameservers or self.search_domains or self.options)

    def buildkit_toml(self) -> str:
        """Render this profile's BuildKit daemon TOML fragment.

        Returns
        -------
        str
            TOML fragment for BuildKit's `[dns]` section, or an empty string
            when no DNS overrides are configured.
        """
        if not self.has_dns:
            return ""
        lines = ["[dns]"]
        if self.nameservers:
            lines.append(f"  nameservers = {_toml_array(self.nameservers)}")
        if self.options:
            lines.append(f"  options = {_toml_array(self.options)}")
        if self.search_domains:
            lines.append(f"  searchDomains = {_toml_array(self.search_domains)}")
        return "\n".join(lines) + "\n"

    def configmap_data(self) -> dict[str, str]:
        """Return the ConfigMap data payload for this profile.

        Returns
        -------
        dict[str, str]
            Kubernetes ConfigMap text data.
        """
        return {NETWORK_PROFILE_KEY: self.json_data}

    @classmethod
    async def get(cls, kube: Kube, *, deadline: Deadline) -> Self:
        """Read the cluster networking profile.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        NetworkProfile
            Persisted cluster networking profile, or the empty default profile when
            no profile ConfigMap exists.

        Raises
        ------
        OSError
            If the persisted profile is malformed.
        """
        config = await ConfigMap.get(
            kube,
            namespace=BERTRAND_NAMESPACE,
            name=NETWORK_PROFILE_NAME,
            deadline=deadline,
        )
        if config is None:
            return cls()
        raw = config.data.get(NETWORK_PROFILE_KEY)
        if raw is None:
            return cls()
        try:
            return cls.model_validate(json.loads(raw))
        except (json.JSONDecodeError, ValidationError) as err:
            msg = (
                f"network profile ConfigMap {BERTRAND_NAMESPACE}/"
                f"{NETWORK_PROFILE_NAME} is malformed"
            )
            raise OSError(msg) from err

    async def upsert(self, kube: Kube, *, deadline: Deadline) -> None:
        """Persist this profile in the cluster.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        deadline : Deadline
            Maximum request budget in seconds. If infinite, wait indefinitely.
        """
        await ConfigMap.upsert(
            kube,
            intent=ConfigMapManifest(
                namespace=BERTRAND_NAMESPACE,
                name=NETWORK_PROFILE_NAME,
                data=self.configmap_data(),
                labels=NETWORK_PROFILE_LABELS,
            ),
            deadline=deadline,
        )
