"""Applies AST-level transformations to Python code in order to efficiently bridge
static and dynamic typing, and allow for overload resolution and signature validation
to occur at compile time (or as close as Python comes to it).
"""
import ast
import sys
import os
import types
from collections.abc import Sequence

import importlib.abc
import importlib.machinery
import importlib.util


print(
    "TODO: create sitepackages/sitecustomize.py in the build system so that "
    "bertrand is implicitly imported at the beginning of every Python session, "
    "and then define custom import hooks (finder + loader) to give AST-level "
    "read/write access to the Python code as it is being compiled.  That gives "
    "a vector for implementing static analysis and code transformation across "
    "the language boundary, which would completely eliminate extra type checks "
    "and possibly even allow static argument validation + overload resolution."
)
print()


class Finder(importlib.abc.MetaPathFinder):
    """TODO: implement a custom finder that ...
    """

    def __init__(self) -> None:
        print("FINDER INIT")

    def find_spec(
        self,
        fullname: str,
        path: Sequence[str] | None,
        target: types.ModuleType | None = None
    ) -> importlib.machinery.ModuleSpec | None:
        """Attempt to locate the module specified by `fullname` and return a module
        specification if found.
        """
        print("FINDER FIND_SPEC")
        return None


class Loader(importlib.abc.Loader):
    """TODO: implement a custom loader for Python modules that ...
    """

    def __init__(self, fullname: str, path: str):
        self.fullname = fullname
        self.path = path

    def create_module(self, spec: importlib.machinery.ModuleSpec) -> None:
        """Use default module creation behavior.
        """
        return None

    def exec_module(self, module: types.ModuleType) -> None:
        """Execute the module code with AST-level access.
        """
        with open(self.path, "r", encoding="utf-8") as file:
            source = file.read()

        # parse source code into AST
        tree = ast.parse(source, filename=self.path)

        # modify AST
        visitor = Visitor()
        tree = visitor.visit(tree)
        ast.fix_missing_locations(tree)

        # compile modified AST
        code = compile(tree, filename=self.path, mode="exec")

        # execute modified code in the module's namespace
        exec(code, module.__dict__)


class Visitor(ast.NodeTransformer):
    """A visitor that traverses the Python AST and applies transformations to
    ensure that C++ interactions are type-safe and resolved at compile time wherever
    possible.
    """

    def visit_call(self, node: ast.Call) -> ast.Call:
        """Transform a `bertrand.Function` call in the AST to validate argument types
        and resolve overloads at compile time.
        """
        return node

    # TODO: maybe I can resolve templates at compile time as well?  Or pipe the
    # necessary information into the `bertrand()` module utility functions, which
    # would offload most of the metaprogramming to compile time in both languages.



sys.meta_path.append(Finder())
