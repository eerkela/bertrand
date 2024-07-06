"""Code generation tools for Python <-> C++ bindings."""
from __future__ import annotations

import importlib
import sys
from pathlib import Path


class Bindings:
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


class PyModule(Bindings):
    """Given an unresolved C++ import, search for an equivalent Python module and
    generate a corresponding C++ module to resolve the import.  The C++ module will
    be automatically added to the CMakeLists.txt file as a source, which can be
    cross-referenced to resolve additional imports beyond the first.
    """

    def __init__(self, path: Path) -> None:
        super().__init__(path)
        self.name = path.stem
        try:
            self.module = importlib.import_module(self.name)
        except ImportError:
            print(
                f"Unresolved import: could not locate C++/Python module '{self.name}'"
            )
            sys.exit(1)

        self.namespaces = self.name.split(".")

    @property
    def _begin_namespace(self) -> str:
        return "\n".join(f"export namespace {name} {{" for name in self.namespaces)

    @property
    def _end_namespace(self) -> str:
        return "\n".join(f"}}  // namespace {name}" for name in reversed(self.namespaces))

    def generate(self) -> str:
        """Generate a C++ module that mirrors the Python module.

        Returns
        -------
        str
            A string which will be written to a .cpp file in order to expose a Python
            module to C++.
        """
        return rf"""
export module {self.name};

#include <iostream>

{self._begin_namespace}

    void hello() {{
        std::cout << "Hello, World!\n";
    }}

{self._end_namespace}
"""


class CppModule(Bindings):
    pass
