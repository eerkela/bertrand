"""Read-only configuration orchestration over layout role mappings.

`Config` is intentionally format-agnostic. It resolves source/artifact roles via
`Layout`, dispatches to providers from the global config registry, and keeps lock
ownership around filesystem I/O only.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from ..layout import (
    MOUNT as LAYOUT_MOUNT,
    ROLE_ARTIFACT_CPP,
    ROLE_CONFIG_PRIMARY,
    ROLE_SOURCE_CPP_COMPILE_COMMANDS,
    Layout,
    lock_env,
)
from ..run import atomic_write_text
from .registry import (
    ConfigKey,
    ConfigSnapshot,
    PrimaryLoadResult,
    require_artifact,
    require_primary,
    require_source,
    validate_layout_alignment,
)

# pylint: disable=broad-exception-caught


CONTAINER_ID_ENV: str = "BERTRAND_CONTAINER_ID"
CONTAINER_BIN_ENV: str = "BERTRAND_CODE_PODMAN_BIN"
EDITOR_BIN_ENV: str = "BERTRAND_CODE_EDITOR_BIN"
HOST_ENV: str = "BERTRAND_HOST_ENV"

# Re-export mount path for existing imports from this module.
MOUNT: Path = LAYOUT_MOUNT


@dataclass
class Config:
    """Layout-driven configuration snapshot with provider-based orchestration."""
    layout: Layout = field()
    _snapshot: ConfigSnapshot = field(init=False, repr=False)

    def __post_init__(self) -> None:
        if not isinstance(self.layout, Layout):
            raise TypeError(f"layout must be a Layout, not {type(self.layout)}")
        with lock_env(self.layout.root):
            validate_layout_alignment(self.layout)
            self._snapshot = self._load_snapshot()

    def _resolve_primary_source(self) -> tuple[str, Path]:
        role_ids = self.layout.role(ROLE_CONFIG_PRIMARY)
        if not role_ids:
            raise OSError(
                f"missing role mapping for primary config source: {ROLE_CONFIG_PRIMARY}"
            )

        attempted: list[tuple[str, Path]] = []
        for resource_id in role_ids:
            try:
                require_primary(resource_id, role=ROLE_CONFIG_PRIMARY)
            except KeyError as err:
                raise OSError(
                    f"missing primary source provider for role '{ROLE_CONFIG_PRIMARY}' "
                    f"resource ID '{resource_id}'"
                ) from err

            path = self.layout.path(resource_id)
            attempted.append((resource_id, path))

            if not path.exists():
                continue
            if not path.is_file():
                raise OSError(f"primary config source path is not a file: {path}")
            return resource_id, path

        attempted_paths = ", ".join(f"{resource_id}={path}" for resource_id, path in attempted)
        raise OSError(
            f"no usable primary config source found in role '{ROLE_CONFIG_PRIMARY}': "
            f"{attempted_paths}"
        )

    @staticmethod
    def _normalize_primary_data(
        data: Any,
        *,
        resource_id: str,
        path: Path,
    ) -> dict[str, Any]:
        if not isinstance(data, dict):
            raise OSError(
                f"primary source provider returned non-mapping data for resource "
                f"'{resource_id}' at {path}"
            )
        normalized: dict[str, Any] = {}
        for key, value in data.items():
            if not isinstance(key, str):
                raise OSError(
                    f"primary source provider returned non-string key for resource "
                    f"'{resource_id}' at {path}: {key!r}"
                )
            normalized[key] = value
        return normalized

    def _load_snapshot(self) -> ConfigSnapshot:
        resource_id, path = self._resolve_primary_source()
        provider = require_primary(resource_id, role=ROLE_CONFIG_PRIMARY)

        try:
            result = provider.load(path, resource_id=resource_id)
        except OSError as err:
            raise OSError(
                f"failed to load primary config source '{resource_id}' at {path}: {err}"
            ) from err
        except Exception as err:
            raise OSError(
                f"unexpected error loading primary config source '{resource_id}' at {path}: "
                f"{err}"
            ) from err

        if not isinstance(result, PrimaryLoadResult):
            raise OSError(
                f"primary source provider returned invalid result type for '{resource_id}' at "
                f"{path}: {type(result)}"
            )

        data = self._normalize_primary_data(result.data, resource_id=resource_id, path=path)
        return ConfigSnapshot(
            root=self.layout.root,
            primary_id=resource_id,
            primary_path=path,
            data=data,
            payload=result.payload,
        )

    @property
    def snapshot(self) -> ConfigSnapshot:
        """Immutable snapshot of loaded config state."""
        return self._snapshot

    @property
    def file(self) -> Path:
        """Resolved path to the selected primary config source."""
        return self._snapshot.primary_path

    @property
    def root(self) -> Path:
        """Environment root for this config snapshot."""
        return self.layout.root

    def __getitem__(self, key: ConfigKey) -> Any:
        return self._snapshot.get(key)

    def sync(self) -> None:
        """Render and write configured artifact resources for active C++ role."""
        with lock_env(self.layout.root):
            artifact_ids = self.layout.role(ROLE_ARTIFACT_CPP)
            if not artifact_ids:
                return

            outputs: list[tuple[str, Path, str]] = []
            for resource_id in artifact_ids:
                target = self.layout.path(resource_id)
                try:
                    provider = require_artifact(resource_id, role=ROLE_ARTIFACT_CPP)
                except KeyError as err:
                    raise OSError(
                        f"missing artifact provider for role '{ROLE_ARTIFACT_CPP}' "
                        f"resource ID '{resource_id}'"
                    ) from err

                try:
                    text = provider.render(
                        self._snapshot,
                        resource_id=resource_id,
                        target=target,
                    )
                except OSError as err:
                    raise OSError(
                        f"failed to render artifact for role '{ROLE_ARTIFACT_CPP}' "
                        f"resource '{resource_id}' at {target}: {err}"
                    ) from err
                except Exception as err:
                    raise OSError(
                        f"unexpected error rendering artifact for role "
                        f"'{ROLE_ARTIFACT_CPP}' resource '{resource_id}' at {target}: {err}"
                    ) from err

                if not isinstance(text, str):
                    raise OSError(
                        f"artifact provider returned non-string output for role "
                        f"'{ROLE_ARTIFACT_CPP}' resource '{resource_id}' at {target}"
                    )
                outputs.append((resource_id, target, text))

            for _, target, text in outputs:
                if target.exists() and not target.is_file():
                    raise OSError(f"cannot write generated artifact; path is not a file: {target}")

                if target.exists():
                    try:
                        current = target.read_text(encoding="utf-8")
                    except OSError as err:
                        raise OSError(f"failed to read generated artifact at {target}: {err}") from err
                    if current == text:
                        continue

                try:
                    atomic_write_text(target, text, encoding="utf-8")
                except OSError as err:
                    raise OSError(f"failed to write generated artifact at {target}: {err}") from err

    def sources(self) -> list[Path]:
        """Resolve and deduplicate source file paths from source-role providers."""
        with lock_env(self.layout.root):
            source_ids = self.layout.role(ROLE_SOURCE_CPP_COMPILE_COMMANDS)
            if not source_ids:
                return []

            discovered: list[Path] = []
            for resource_id in source_ids:
                source_path = self.layout.path(resource_id)
                try:
                    provider = require_source(
                        resource_id,
                        role=ROLE_SOURCE_CPP_COMPILE_COMMANDS,
                    )
                except KeyError as err:
                    raise OSError(
                        f"missing source provider for role "
                        f"'{ROLE_SOURCE_CPP_COMPILE_COMMANDS}' resource ID '{resource_id}'"
                    ) from err

                try:
                    paths = provider.sources(
                        source_path,
                        snapshot=self._snapshot,
                        resource_id=resource_id,
                    )
                except OSError as err:
                    raise OSError(
                        f"failed to collect sources for role "
                        f"'{ROLE_SOURCE_CPP_COMPILE_COMMANDS}' resource '{resource_id}' at "
                        f"{source_path}: {err}"
                    ) from err
                except Exception as err:
                    raise OSError(
                        f"unexpected error collecting sources for role "
                        f"'{ROLE_SOURCE_CPP_COMPILE_COMMANDS}' resource '{resource_id}' at "
                        f"{source_path}: {err}"
                    ) from err

                for item in paths:
                    if not isinstance(item, Path):
                        raise OSError(
                            f"source provider returned non-path value for role "
                            f"'{ROLE_SOURCE_CPP_COMPILE_COMMANDS}' resource '{resource_id}': "
                            f"{item!r}"
                        )
                    discovered.append(item)

            out: list[Path] = []
            seen: set[Path] = set()
            for source in discovered:
                resolved = source.expanduser().resolve()
                if resolved in seen:
                    continue
                seen.add(resolved)
                out.append(resolved)
            return out
