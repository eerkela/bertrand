"""Code generation tools to expose Python modules to C++."""
from __future__ import annotations

import importlib
import sys
from pathlib import Path


# TODO: this will eventually be called to produce a C++ module that mirrors a Python
# import, which will require some complicated work and possibly integration with the
# Python AST static analyzer, which might be able to generate these implicitly.
# Otherwise, the only way to do it is to parse the Python module's contents at runtime
# and generate the C++ module from that.  Maybe it's not that hard though, if I can
# detect the import within the build system, and then just run the static analyzer on
# it to generate the C++ module.  It might even just come down to a flag for the Python
# AST parser, which would output the C++ module to a particular path.  That would be
# by far the most elegant and flexible solution, if possible.


class PyModule:
    """Given an unresolved C++ import, search for an equivalent Python module and
    generate a corresponding C++ module to resolve the import.  The C++ module will
    be automatically added to the CMakeLists.txt file as a source, which can be
    cross-referenced to resolve additional imports beyond the first.
    """

    # TODO: maybe the path is interpolated from the dotted module name, which is what
    # is logically imported in C++.  This would naturally group the generated .cpp files
    # into a directory structure that mirrors the Python module hierarchy, which can
    # make it easier to navigate.  

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
