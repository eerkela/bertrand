"""A PEP517/660-compliant build backend for Bertrand projects.

This backend runs an instrumented CMake build to detect and compile C++ dependencies,
and generate bindings to back cross-language imports.
"""

from __future__ import annotations

import asyncio
from collections.abc import Mapping, Sequence
from pathlib import Path

import setuptools.build_meta

from .build_args import (
    IMAGE_BUILD_ARGS_CONFIG_SETTING,
    decode_image_build_args,
)
from .git import CONTAINER_TMP, run

ARTIFACT_ROOT = Path(CONTAINER_TMP)
CONANFILE = ARTIFACT_ROOT / "conanfile.py"
CONAN_LOCK = ARTIFACT_ROOT / "conan.lock"
CONAN_OUTPUT = ARTIFACT_ROOT / "conan"


type PEP517ConfigValue = str | Sequence[str]
type ConfigSettings = Mapping[str, PEP517ConfigValue] | None
type SetuptoolsConfigValue = str | list[str] | None
type SetuptoolsConfigSettings = dict[str, SetuptoolsConfigValue] | None


async def _conan_lock(*, quiet: bool) -> None:
    await run(
        [
            "conan",
            "lock",
            "create",
            str(CONANFILE),
            "--profile:host=default",  # always use generated default profile
            "--profile:build=default",  # always use generated default profile
            "--build=missing",  # incrementally build missing dependencies
            "--update",  # refresh dependency graph similarly to `uv lock`
            # write deterministic lock into artifact root
            f"--lockfile-out={CONAN_LOCK}",
            "-cc",
            "core:non_interactive=True",  # avoid prompts in image/container builds
        ],
        capture_output=quiet,
    )


async def _conan_install_lock(*, quiet: bool) -> None:
    await run(
        [
            "conan",
            "install",
            str(CONANFILE),
            "--profile:host=default",  # always use generated default profile
            "--profile:build=default",  # always use generated default profile
            "--build=missing",  # incrementally build missing dependencies
            f"--lockfile={CONAN_LOCK}",  # install using deterministic lockfile
            f"--output-folder={CONAN_OUTPUT}",  # install into artifact directory
            "-cc",
            "core:non_interactive=True",  # avoid prompts in image/container builds
        ],
        capture_output=quiet,
    )


def read_image_build_args(config_settings: ConfigSettings = None) -> dict[str, str]:
    """Read Bertrand image build arguments from PEP 517 config settings.

    Parameters
    ----------
    config_settings : Mapping[str, str | Sequence[str]] | None, optional
        Frontend-provided PEP 517/660 config settings.

    Returns
    -------
    dict[str, str]
        Normalized image build arguments provided by the internal CLI.

    """
    build_args, _ = _split_bertrand_config_settings(config_settings)
    return build_args


def _setuptools_config_settings(
    config_settings: ConfigSettings,
) -> SetuptoolsConfigSettings:
    _, config_settings = _split_bertrand_config_settings(config_settings)
    if config_settings is None:
        return None

    normalized: dict[str, SetuptoolsConfigValue] = {}
    for key, value in config_settings.items():
        if not isinstance(key, str):
            msg = (
                "invalid build backend config_settings key type: "
                f"expected 'str', got {type(key).__name__}"
            )
            raise TypeError(msg)
        if isinstance(value, str):
            normalized[key] = value
            continue
        if isinstance(value, Sequence):
            entries: list[str] = []
            for entry in value:
                if not isinstance(entry, str):
                    msg = (
                        "invalid build backend config_settings sequence value for key "
                        f"{key!r}: expected 'str', got {type(entry).__name__}"
                    )
                    raise TypeError(msg)
                entries.append(entry)
            normalized[key] = entries
            continue
        msg = (
            "invalid build backend config_settings value for key "
            f"{key!r}: expected 'str | Sequence[str]', got "
            f"{type(value).__name__}"
        )
        raise TypeError(msg)
    return normalized


def _split_bertrand_config_settings(
    config_settings: ConfigSettings,
) -> tuple[dict[str, str], ConfigSettings]:
    if config_settings is None:
        return {}, None

    build_args: dict[str, str] = {}
    remaining: dict[str, PEP517ConfigValue] = {}
    for key, value in config_settings.items():
        if key == IMAGE_BUILD_ARGS_CONFIG_SETTING:
            build_args = _decode_build_args_config_setting(value)
        else:
            remaining[key] = value
    return build_args, remaining or None


def _decode_build_args_config_setting(value: PEP517ConfigValue) -> dict[str, str]:
    if isinstance(value, str):
        return decode_image_build_args(value)
    if isinstance(value, Sequence):
        entries = list(value)
        if len(entries) != 1:
            msg = f"{IMAGE_BUILD_ARGS_CONFIG_SETTING!r} must be provided exactly once"
            raise TypeError(msg)
        entry = entries[0]
        if not isinstance(entry, str):
            msg = (
                f"{IMAGE_BUILD_ARGS_CONFIG_SETTING!r} must be encoded as a JSON string"
            )
            raise TypeError(msg)
        return decode_image_build_args(entry)
    msg = f"{IMAGE_BUILD_ARGS_CONFIG_SETTING!r} must be encoded as a JSON string"
    raise TypeError(msg)


def get_requires_for_build_wheel(config_settings: ConfigSettings = None) -> list[str]:
    """Return additional requirements needed to build a wheel.

    Parameters
    ----------
    config_settings : Mapping[str, str | Sequence[str]] | None, optional
        Frontend-provided PEP 517 config settings.

    Returns
    -------
    list[str]
        Build requirements requested by setuptools for wheel builds.

    Notes
    -----
    This hook delegates directly to setuptools and does not invoke Conan.
    """
    settings = _setuptools_config_settings(config_settings)
    return setuptools.build_meta.get_requires_for_build_wheel(settings)


def prepare_metadata_for_build_wheel(
    metadata_directory: str,
    config_settings: ConfigSettings = None,
) -> str:
    """Prepare wheel metadata without building a wheel artifact.

    Parameters
    ----------
    metadata_directory : str
        Destination directory where backend metadata should be written.
    config_settings : Mapping[str, str | Sequence[str]] | None, optional
        Frontend-provided PEP 517 config settings.

    Returns
    -------
    str
        The relative path to the generated metadata directory.

    Notes
    -----
    This hook delegates directly to setuptools and does not invoke Conan.
    """
    settings = _setuptools_config_settings(config_settings)
    return setuptools.build_meta.prepare_metadata_for_build_wheel(
        metadata_directory,
        settings,
    )


def build_wheel(
    wheel_directory: str,
    config_settings: ConfigSettings = None,
    metadata_directory: str | None = None,
) -> str:
    """Build a wheel artifact for the current project.

    Parameters
    ----------
    wheel_directory : str
        Destination directory for the produced wheel file.
    config_settings : Mapping[str, str | Sequence[str]] | None, optional
        Frontend-provided PEP 517 config settings.
    metadata_directory : str | None, optional
        Optional metadata directory previously returned by
        `prepare_metadata_for_build_wheel`.

    Returns
    -------
    str
        The wheel filename generated by setuptools.

    Notes
    -----
    This hook performs Bertrand's package-manager pre-step by running Conan install
    from synced artifacts, then delegates wheel construction to setuptools.
    """
    settings = _setuptools_config_settings(config_settings)
    if CONANFILE.exists() and CONANFILE.is_file():
        with asyncio.Runner() as runner:
            runner.run(_conan_lock(quiet=False))
            runner.run(_conan_install_lock(quiet=False))
    return setuptools.build_meta.build_wheel(
        wheel_directory,
        settings,
        metadata_directory,
    )


def get_requires_for_build_sdist(config_settings: ConfigSettings = None) -> list[str]:
    """Return additional requirements needed to build an sdist.

    Parameters
    ----------
    config_settings : Mapping[str, str | Sequence[str]] | None, optional
        Frontend-provided PEP 517 config settings.

    Returns
    -------
    list[str]
        Build requirements requested by setuptools for sdist builds.

    Notes
    -----
    This hook delegates directly to setuptools and does not invoke Conan.
    """
    settings = _setuptools_config_settings(config_settings)
    return setuptools.build_meta.get_requires_for_build_sdist(settings)


def build_sdist(sdist_directory: str, config_settings: ConfigSettings = None) -> str:
    """Build a source distribution (sdist) for the current project.

    Parameters
    ----------
    sdist_directory : str
        Destination directory for the produced source distribution.
    config_settings : Mapping[str, str | Sequence[str]] | None, optional
        Frontend-provided PEP 517 config settings.

    Returns
    -------
    str
        The sdist filename generated by setuptools.

    Notes
    -----
    This hook delegates directly to setuptools and does not invoke Conan.
    """
    settings = _setuptools_config_settings(config_settings)
    return setuptools.build_meta.build_sdist(sdist_directory, settings)


def get_requires_for_build_editable(
    config_settings: ConfigSettings = None,
) -> list[str]:
    """Return additional requirements needed to build an editable wheel.

    Parameters
    ----------
    config_settings : Mapping[str, str | Sequence[str]] | None, optional
        Frontend-provided PEP 660 config settings.

    Returns
    -------
    list[str]
        Build requirements requested by setuptools for editable wheel builds.

    Notes
    -----
    This hook delegates directly to setuptools and does not invoke Conan.
    """
    settings = _setuptools_config_settings(config_settings)
    return setuptools.build_meta.get_requires_for_build_editable(settings)


def prepare_metadata_for_build_editable(
    metadata_directory: str,
    config_settings: ConfigSettings = None,
) -> str:
    """Prepare editable metadata without building an editable wheel.

    Parameters
    ----------
    metadata_directory : str
        Destination directory where backend metadata should be written.
    config_settings : Mapping[str, str | Sequence[str]] | None, optional
        Frontend-provided PEP 660 config settings.

    Returns
    -------
    str
        The relative path to the generated metadata directory.

    Notes
    -----
    This hook delegates directly to setuptools and does not invoke Conan.
    """
    settings = _setuptools_config_settings(config_settings)
    return setuptools.build_meta.prepare_metadata_for_build_editable(
        metadata_directory,
        settings,
    )


def build_editable(
    wheel_directory: str,
    config_settings: ConfigSettings = None,
    metadata_directory: str | None = None,
) -> str:
    """Build an editable wheel artifact for the current project.

    Parameters
    ----------
    wheel_directory : str
        Destination directory for the produced editable wheel file.
    config_settings : Mapping[str, str | Sequence[str]] | None, optional
        Frontend-provided PEP 660 config settings.
    metadata_directory : str | None, optional
        Optional metadata directory previously returned by
        `prepare_metadata_for_build_editable`.

    Returns
    -------
    str
        The editable wheel filename generated by setuptools.

    Notes
    -----
    This hook performs Bertrand's package-manager pre-step by running Conan install
    from synced artifacts, then delegates editable wheel construction to setuptools.
    """
    settings = _setuptools_config_settings(config_settings)
    if CONANFILE.exists() and CONANFILE.is_file():
        with asyncio.Runner() as runner:
            runner.run(_conan_lock(quiet=False))
            runner.run(_conan_install_lock(quiet=False))
    return setuptools.build_meta.build_editable(
        wheel_directory,
        settings,
        metadata_directory,
    )
