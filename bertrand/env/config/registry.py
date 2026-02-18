"""Unified config contracts + resource/provider catalog.

This module is the single extension surface for config internals. It contains:
1. provider protocols,
2. snapshot/result data types,
3. resource binding types,
4. the unified resource catalog,
5. lookup + strict layout alignment helpers.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Literal, Protocol, TypeAlias

from ..layout import (
    ROLE_ARTIFACT_CPP,
    ROLE_CONFIG_PRIMARY,
    ROLE_SOURCE_CPP_COMPILE_COMMANDS,
    Layout,
)


ConfigKey: TypeAlias = str | tuple[str, ...]
ProviderRole: TypeAlias = Literal["primary", "artifact", "source"]


@dataclass(frozen=True)
class PrimaryLoadResult:
    """Normalized output from a primary config source provider."""

    data: dict[str, Any]
    payload: Any | None = None


@dataclass(frozen=True)
class ConfigSnapshot:
    """In-memory snapshot of loaded configuration state."""

    root: Path
    primary_id: str
    primary_path: Path
    data: dict[str, Any]
    payload: Any | None = None

    def get(self, key: ConfigKey) -> Any:
        """Lookup a value in snapshot data.

        `str` keys resolve top-level values; tuple keys resolve nested values with
        `KeyError` semantics.
        """
        if isinstance(key, str):
            return self.data[key]

        if isinstance(key, tuple):
            value: Any = self.data
            for part in key:
                if not isinstance(part, str):
                    raise TypeError(f"invalid tuple key part type: {type(part)}")
                if not isinstance(value, dict):
                    raise KeyError(key)
                value = value[part]
            return value

        raise TypeError(f"invalid key type: {type(key)}")


class PrimarySourceProvider(Protocol):
    """Parser/validator for primary configuration sources."""

    def load(self, path: Path, *, resource_id: str) -> PrimaryLoadResult:
        """Load and validate primary config source data from `path`."""


class ArtifactProvider(Protocol):
    """Renderer for synchronized artifact resources."""

    def render(self, snapshot: ConfigSnapshot, *, resource_id: str, target: Path) -> str:
        """Render artifact text from config snapshot state."""


class SourceProvider(Protocol):
    """Reader for derived source paths from source resources."""

    def sources(
        self,
        path: Path,
        *,
        snapshot: ConfigSnapshot,
        resource_id: str,
    ) -> list[Path]:
        """Resolve source paths for the given resource path."""


@dataclass(frozen=True)
class ResourceProviders:
    """Provider slots attached to a config resource binding."""

    primary: PrimarySourceProvider | None = None
    artifact: ArtifactProvider | None = None
    source: SourceProvider | None = None


@dataclass(frozen=True)
class ConfigResourceBinding:
    """Config-side runtime metadata for a single layout resource ID."""

    resource_id: str
    roles: set[str] = field(default_factory=set)
    providers: ResourceProviders = field(default_factory=ResourceProviders)


CONFIG_RESOURCE_CATALOG: dict[str, ConfigResourceBinding] = {
    "pyproject": ConfigResourceBinding(
        resource_id="pyproject",
        roles={ROLE_CONFIG_PRIMARY},
        providers=ResourceProviders(primary=None),
    ),
    "clang_format": ConfigResourceBinding(
        resource_id="clang_format",
        roles={ROLE_ARTIFACT_CPP},
        providers=ResourceProviders(artifact=None),
    ),
    "clang_tidy": ConfigResourceBinding(
        resource_id="clang_tidy",
        roles={ROLE_ARTIFACT_CPP},
        providers=ResourceProviders(artifact=None),
    ),
    "clangd": ConfigResourceBinding(
        resource_id="clangd",
        roles={ROLE_ARTIFACT_CPP},
        providers=ResourceProviders(artifact=None),
    ),
    "compile_commands": ConfigResourceBinding(
        resource_id="compile_commands",
        roles={ROLE_SOURCE_CPP_COMPILE_COMMANDS},
        providers=ResourceProviders(source=None),
    ),
}


def _normalize_resource_id(resource_id: str) -> str:
    if not isinstance(resource_id, str):
        raise TypeError(f"resource_id must be a string, not {type(resource_id)}")
    normalized = resource_id.strip()
    if not normalized:
        raise ValueError("resource_id must be a non-empty string")
    return normalized


def _normalize_role(role: str) -> str:
    if not isinstance(role, str):
        raise TypeError(f"role must be a string, not {type(role)}")
    normalized = role.strip()
    if not normalized:
        raise ValueError("role must be a non-empty string")
    return normalized


def require_binding(resource_id: str) -> ConfigResourceBinding:
    key = _normalize_resource_id(resource_id)
    binding = CONFIG_RESOURCE_CATALOG.get(key)
    if binding is None:
        raise KeyError(f"missing config resource binding for ID: {key}")
    return binding


def _require_role_membership(binding: ConfigResourceBinding, role: str) -> None:
    normalized_role = _normalize_role(role)
    if normalized_role not in binding.roles:
        raise KeyError(
            f"resource '{binding.resource_id}' is not tagged for role '{normalized_role}'"
        )


def require_primary(resource_id: str, *, role: str) -> PrimarySourceProvider:
    binding = require_binding(resource_id)
    _require_role_membership(binding, role)
    provider = binding.providers.primary
    if provider is None:
        raise KeyError(
            f"missing primary provider for resource '{binding.resource_id}' "
            f"(role '{_normalize_role(role)}')"
        )
    return provider


def require_artifact(resource_id: str, *, role: str) -> ArtifactProvider:
    binding = require_binding(resource_id)
    _require_role_membership(binding, role)
    provider = binding.providers.artifact
    if provider is None:
        raise KeyError(
            f"missing artifact provider for resource '{binding.resource_id}' "
            f"(role '{_normalize_role(role)}')"
        )
    return provider


def require_source(resource_id: str, *, role: str) -> SourceProvider:
    binding = require_binding(resource_id)
    _require_role_membership(binding, role)
    provider = binding.providers.source
    if provider is None:
        raise KeyError(
            f"missing source provider for resource '{binding.resource_id}' "
            f"(role '{_normalize_role(role)}')"
        )
    return provider


def _validate_role(
    layout: Layout,
    *,
    role: str,
    kind: str,
) -> None:
    for resource_id in layout.role(role):
        try:
            binding = require_binding(resource_id)
        except KeyError as err:
            raise OSError(
                f"layout role '{role}' references unmapped config resource ID "
                f"'{resource_id}'"
            ) from err

        if role not in binding.roles:
            raise OSError(
                f"config resource '{resource_id}' is not tagged for layout role '{role}'"
            )

        if kind == "primary" and binding.providers.primary is None:
            raise OSError(
                f"config resource '{resource_id}' in role '{role}' has no primary provider"
            )
        if kind == "artifact" and binding.providers.artifact is None:
            raise OSError(
                f"config resource '{resource_id}' in role '{role}' has no artifact provider"
            )
        if kind == "source" and binding.providers.source is None:
            raise OSError(
                f"config resource '{resource_id}' in role '{role}' has no source provider"
            )


def validate_layout_alignment(layout: Layout) -> None:
    """Fail-fast check that layout role mappings align with config catalog bindings."""
    _validate_role(layout, role=ROLE_CONFIG_PRIMARY, kind="primary")
    _validate_role(layout, role=ROLE_ARTIFACT_CPP, kind="artifact")
    _validate_role(layout, role=ROLE_SOURCE_CPP_COMPILE_COMMANDS, kind="source")
