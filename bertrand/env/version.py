"""Version metadata for Bertrand and its canonical container toolchain."""

from __future__ import annotations

import re

from dataclasses import dataclass, field
from importlib import metadata as importlib_metadata
from pathlib import Path, PurePosixPath


CONTAINERFILE_ARG = re.compile(
    r"^\s*ARG\s+(?P<key>[A-Z][A-Z0-9_]*)\s*=\s*(?P<value>\S+)\s*$"
)
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
    bertrand: str = field(init=False)
    ninja: str = field(init=False)
    llvm: str = field(init=False)
    cmake: str = field(init=False)
    python: str = field(init=False)
    uv: str = field(init=False)
    ruff: str = field(init=False)
    ty: str = field(init=False)
    pytest: str = field(init=False)
    conan: str = field(init=False)
    cxx_std: str = field(init=False)

    def __post_init__(self) -> None:
        # get Bertrand package version
        try:
            object.__setattr__(
                self,
                "bertrand",
                importlib_metadata.version("bertrand").strip()
            )
        except importlib_metadata.PackageNotFoundError as err:
            raise OSError(
                "missing distribution metadata for 'bertrand'.  Version lookup requires "
                "an installed package context."
            ) from err
        if not self.bertrand:
            raise OSError("installed distribution version for 'bertrand' is empty")

        # get distribution metadata
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

        # search for original toolchain Containerfile record
        matches = [
            entry for entry in records if PurePosixPath(str(entry)).name == "Containerfile"
        ]
        if not matches:
            raise OSError(
                "could not find canonical toolchain Containerfile in installed 'bertrand' "
                "distribution files.  Ensure packaging includes the root Containerfile."
            )
        if len(matches) > 1:
            locations = ", ".join(sorted(str(match) for match in matches))
            raise OSError(
                "ambiguous toolchain Containerfile entries in installed 'bertrand' "
                f"distribution: {locations}"
            )

        # read the Containerfile text from disk
        path = Path(str(dist.locate_file(matches[0])))
        try:
            text = path.read_text(encoding="utf-8")
        except FileNotFoundError as err:
            raise OSError(
                "canonical toolchain Containerfile record was present in distribution "
                f"metadata, but the file does not exist on disk: {path}"
            ) from err
        except OSError as err:
            raise OSError(
                f"failed to read canonical toolchain Containerfile at {path}: {err}"
            ) from err

        # parse the Containerfile text for ARG values representing toolchain component
        # versions
        for line_number, line in enumerate(text.splitlines(), start=1):
            match = CONTAINERFILE_ARG.match(line)
            if match is None:
                continue
            key, value = match.groups()
            norm_key = CONTAINERFILE_ARGS.get(key)
            if norm_key is None:
                continue
            old = getattr(self, norm_key, value)
            if old != value:
                raise OSError(
                    "conflicting toolchain ARG values in canonical Containerfile for "
                    f"'{key}': '{old}' vs '{value}' (line {line_number})"
                )
            setattr(self, norm_key, value)


# global version singletons
VERSION = Versions()
__version__ = VERSION.bertrand
