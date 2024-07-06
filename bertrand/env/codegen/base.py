"""Base classes for auto-generated Python/C++ binding files."""
from __future__ import annotations

from pathlib import Path


class Module:
    """Base class for auto-generated Python/C++ binding files."""

    def __init__(self, path: Path) -> None:
        self.path = path

    def generate(self) -> str:
        """Generate the module's source code as a string.

        Returns
        -------
        str
            The module's source code, which is typically written to a .cpp file
            immediately after invoking this method.
        """
        raise NotImplementedError
