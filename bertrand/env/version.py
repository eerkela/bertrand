"""Version metadata for Bertrand and its canonical container toolchain."""

from __future__ import annotations

import re

from dataclasses import dataclass, field
from importlib import metadata as importlib_metadata
from pathlib import Path, PurePosixPath


CONTAINERFILE_ARG = re.compile(r"^\s*ARG\s+([A-Z][A-Z0-9_]*)\s*=\s*([^\s#]+)\s*(?:#.*)?$")
CONTAINERFILE_ARGS: dict[str, str] = {
    "NINJA_VERSION": "ninja",
    "LLVM_VERSION": "llvm",
    "CMAKE_VERSION": "cmake",
    "PYTHON_VERSION": "python",
    "UV_VERSION": "uv",
    "RUFF_VERSION": "ruff",
    "TY_VERSION": "ty",
    "PYTEST_VERSION": "pytest",
    "CONAN_VERSION": "conan",
    "CXX_STD": "cxx_std",
}


@dataclass(frozen=True)
class Versions:
    """Pinned versions for Bertrand and each component of its containerized toolchain,
    resolved from distribution metadata.
    """
    bertrand: str
    ninja: str | None = field(default=None)
    llvm: str | None = field(default=None)
    cmake: str | None = field(default=None)
    python: str | None = field(default=None)
    uv: str | None = field(default=None)
    ruff: str | None = field(default=None)
    ty: str | None = field(default=None)
    pytest: str | None = field(default=None)
    conan: str | None = field(default=None)
    cxx_std: str | None = field(default=None)


def _load_bertrand_version() -> str:
    try:
        version = importlib_metadata.version("bertrand").strip()
    except importlib_metadata.PackageNotFoundError as err:
        raise OSError(
            "missing distribution metadata for 'bertrand'.  Version lookup requires "
            "an installed package context."
        ) from err
    if not version:
        raise OSError("installed distribution version for 'bertrand' is empty")
    return version


def _load_containerfile() -> str:
    # get distribution metadata for bertrand
    try:
        dist = importlib_metadata.distribution("bertrand")
    except importlib_metadata.PackageNotFoundError as err:
        raise OSError(
            "missing distribution metadata for 'bertrand'.  Toolchain version lookup "
            "requires an installed package context."
        ) from err
    records = dist.files
    if records is None:
        raise OSError(
            "distribution metadata for 'bertrand' does not expose installed files.  "
            "Cannot locate canonical Containerfile."
        )

    # search for original Containerfile record
    matches = [
        entry for entry in records
        if PurePosixPath(str(entry)).name == "Containerfile"
    ]
    if not matches:
        raise OSError(
            "could not find canonical Containerfile in installed 'bertrand' "
            "distribution files.  Ensure packaging includes the root Containerfile."
        )
    if len(matches) > 1:
        locations = ", ".join(sorted(str(match) for match in matches))
        raise OSError(
            "ambiguous Containerfile entries in installed 'bertrand' distribution: "
            f"{locations}"
        )

    # read the Containerfile text from disk
    path = Path(str(dist.locate_file(matches[0])))
    try:
        return path.read_text(encoding="utf-8")
    except FileNotFoundError as err:
        raise OSError(
            "canonical Containerfile record was present in distribution metadata, but "
            f"the file does not exist on disk: {path}"
        ) from err
    except OSError as err:
        raise OSError(
            f"failed to read canonical Containerfile at {path}: {err}"
        ) from err


def _parse_containerfile(text: str) -> dict[str, str]:
    found: dict[str, str] = {}
    for line_number, line in enumerate(text.splitlines(), start=1):
        match = CONTAINERFILE_ARG.match(line)
        if match is None:
            continue
        key, value = match.groups()
        norm_key = CONTAINERFILE_ARGS.get(key)
        if norm_key is None:
            continue
        old = found.setdefault(norm_key, value)
        if old != value:
            raise OSError(
                "conflicting toolchain ARG values in canonical Containerfile for "
                f"'{key}': '{old}' vs '{value}' (line {line_number})"
            )
    return found


def _load_versions() -> Versions:
    bertrand = _load_bertrand_version()
    from_containerfile = _parse_containerfile(_load_containerfile())
    return Versions(bertrand=bertrand, **from_containerfile)


# global version singletons
VERSIONS = _load_versions()
__version__ = VERSIONS.bertrand
