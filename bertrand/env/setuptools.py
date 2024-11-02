"""Build tools for bertrand-enabled C++ extensions."""
from __future__ import annotations

import hashlib
import importlib
import json
import re
import os
import shlex
import shutil
import subprocess
import sys
import sysconfig
from pathlib import Path
from typing import Any, Iterator, Iterable, overload, cast, SupportsIndex

import setuptools
from packaging.version import Version
from setuptools.command.build_ext import build_ext as setuptools_build_ext

from bertrand.codegen.python import PyModule
from .environment import env
from .messages import FAIL, WHITE, RED, YELLOW, GREEN, CYAN
from .package import Package
from .version import __version__


# pylint: disable=protected-access


class Source(setuptools.Extension):
    """Describes an arbitrary source file that can be scanned for dependencies and used
    to generate automated bindings.  One of these should be specified for every source
    file in the project.

    Notes
    -----
    Unlike `setuptools.Extension`, these objects are meant to represent only a single
    source file.  Bertrand's build system is powerful enough to automatically detect
    dependencies between files, so there is no need to specify them manually.  All the
    required information is extracted from the source file itself at build time.

    One important thing to note regards the way extra build options are interpreted on
    a per-source basis.  If you have a source file that requires special treatment, you
    can list additional arguments in the `Source` constructor, which will be used when
    that source is built as a target.  These flags will also apply to any dependencies
    that the source has, but *not* to any sources that depend on it in turn.  So, for
    instance, if you have 3 source files, A, B, and C, where A depends on B and B
    depends on C, then adding a `-DDEBUG` flag to B will cause both B and C to be built
    with the flag, while A will be built without it.  If A requires the flag as well,
    then it should be added to its `Source` constructor.
    """

    class Paths(list):  # type: ignore
        """A collection of paths that automatically converts between posix-style
        strings and formal Path objects when accessed.
        """

        def __init__(self, paths: Iterable[str]) -> None:
            super().__init__(paths)

        @overload
        def __getitem__(self, index: SupportsIndex) -> Path: ...
        @overload
        def __getitem__(self, index: slice) -> list[Path]: ...
        def __getitem__(self, index: SupportsIndex | slice) -> Path | list[Path]:
            if isinstance(index, slice):
                return [Path(p) for p in super().__getitem__(index)]
            return Path(super().__getitem__(index))

        @overload
        def __setitem__(self, index: SupportsIndex, value: Path) -> None: ...
        @overload
        def __setitem__(self, index: slice, value: Iterable[Path]) -> None: ...
        def __setitem__(
            self,
            index: SupportsIndex | slice,
            value: Path | Iterable[Path]
        ) -> None:
            if isinstance(index, slice):
                super().__setitem__(index, [p.as_posix() for p in cast(list[Path], value)])
            else:
                super().__setitem__(index, cast(Path, value).as_posix())

        def __contains__(self, path: Path) -> bool:  # type: ignore
            return super().__contains__(path.as_posix())

        def __iter__(self) -> Iterator[Path]:
            return (Path(p) for p in super().__iter__())

        def __reversed__(self) -> Iterator[Path]:
            return (Path(p) for p in super().__reversed__())

        def __add__(self, other: list[Path]) -> list[Path]:  # type: ignore
            return [Path(p) for p in super().__iter__()] + other

        def __iadd__(self, other: list[Path]) -> None:  # type: ignore
            super().extend(p.as_posix() for p in other)

        def __mul__(self, n: SupportsIndex) -> list[Path]:
            return [Path(p) for p in super().__iter__()] * n

        def __rmul__(self, n: SupportsIndex) -> list[Path]:
            return [Path(p) for p in super().__iter__()] * n

        def __lt__(self, other: list[Path]) -> bool:
            return [Path(p) for p in super().__iter__()] < other

        def __le__(self, other: list[Path]) -> bool:
            return [Path(p) for p in super().__iter__()] <= other

        def __eq__(self, other: Any) -> bool:
            return [Path(p) for p in super().__iter__()] == other

        def __ne__(self, other: Any) -> bool:
            return [Path(p) for p in super().__iter__()] != other

        def __ge__(self, other: list[Path]) -> bool:
            return [Path(p) for p in super().__iter__()] >= other

        def __gt__(self, other: list[Path]) -> bool:
            return [Path(p) for p in super().__iter__()] > other

        def index(
            self,
            path: Path,
            start: SupportsIndex = 0,
            stop: SupportsIndex | None = None
        ) -> int:
            """Find the index of a path in the collection.

            Parameters
            ----------
            path : Path
                The path to search for.
            start : int, optional
                The index to start searching from.
            stop : int, optional
                The index to stop searching at.

            Returns
            -------
            int
                The index of the path in the collection.
            """
            return super().index(
                path.as_posix(),
                start,
                len(self) if stop is None else stop
            )

        def count(self, path: Path) -> int:
            """Count the number of occurrences of a path in the collection.

            Parameters
            ----------
            path : Path
                The path to search for.

            Returns
            -------
            int
                The number of occurrences of the path in the collection.
            """
            return super().count(path.as_posix())

        def append(self, path: Path) -> None:
            """Append a new path to the collection.

            Parameters
            ----------
            path : str
                The path to append.
            """
            super().append(path.as_posix())

        def extend(self, paths: Iterable[Path]) -> None:
            """Extend the collection with a list of paths.

            Parameters
            ----------
            paths : list[str]
                The paths to append.
            """
            super().extend(p.as_posix() for p in paths)

        def copy(self) -> list[Path]:
            """Return a shallow copy of the collection.

            Returns
            -------
            list[str]
                A shallow copy of the collection.
            """
            return [Path(p) for p in super().__iter__()]

        def insert(self, index: SupportsIndex, path: Path) -> None:
            """Insert a path at a specific index in the collection.

            Parameters
            ----------
            index : int
                The index to insert the path at.
            path : str
                The path to insert.
            """
            super().insert(index, path.as_posix())

        def pop(self, index: SupportsIndex = -1) -> Path:
            """Remove and return a path from the collection.

            Parameters
            ----------
            index : int, optional
                The index of the path to remove.

            Returns
            -------
            str
                The removed path.
            """
            return Path(super().pop(index))

        def remove(self, path: Path) -> None:
            """Remove a specific path from the collection.

            Parameters
            ----------
            path : str
                The path to remove.
            """
            super().remove(path.as_posix())

    class Dependencies(dict[str, "Source"]):
        """A mapping of imported module names to their corresponding source files.
        Modifying the dictionary will automatically update the import graph for the
        owning source file.
        """

        def __init__(self, source: "Source") -> None:
            super().__init__()
            self._source = source

        def __setitem__(self, key: str, value: "Source") -> None:
            super().__setitem__(key, value)
            value._provides.add(self._source)

        def __delitem__(self, key: str) -> None:
            super().__delitem__(key)
            self[key]._provides.discard(self._source)

        def collapse(self) -> set["Source"]:
            """Collapse the dependency graph into a flat set of sources.

            Returns
            -------
            set[Source]
                A set of all sources that are dependencies of the owning source file.
            """
            stack = set(self.values())
            out = set()
            while stack:
                source = stack.pop()
                out.add(source)
                stack.update(s for s in source.requires.values() if s not in out)
            return out

        def clear(self) -> None:
            for source in self.values():
                source._provides.discard(self._source)
            super().clear()

        @overload  # type: ignore
        def pop(self, key: str) -> "Source | None": ...
        @overload
        def pop(self, key: str, default: None) -> "Source | None": ...
        @overload
        def pop(self, key: str, default: "Source") -> "Source": ...
        def pop(self, key: str, default: "Source | None" = None) -> "Source | None":
            """Pop a source from the dictionary and update the import graph.

            Parameters
            ----------
            key : str
                The module name to pop from the dictionary.
            default : Source, optional
                The source to return if the key is not found.

            Returns
            -------
            Source | None
                The source that was removed from the dictionary.
            """
            source = super().pop(key, default)
            if source is not None:
                source._provides.discard(self._source)
            return source

        def popitem(self) -> tuple[str, "Source"]:
            """Pop a random key-value pair from the dictionary and update the import
            graph.

            Returns
            -------
            tuple[str, Source]
                A key-value pair from the dictionary.

            Raises
            ------
            KeyError
                If the dictionary is empty.
            """
            key, source = super().popitem()
            source._provides.discard(self._source)
            return key, source

        def setdefault(self, key: str, default: "Source") -> "Source":
            """Set a default value for a key and update the import graph.

            Parameters
            ----------
            key : str
                The module name to set.
            default : Source
                The source to set as the default value.

            Returns
            -------
            Source
                The source that was set as the default value.
            """
            source = super().setdefault(key, default)
            source._provides.add(self._source)
            return source

        def update(  # type: ignore
            self,
            other: dict[str, "Source"] | Iterable[tuple[str, "Source"]]
        ) -> None:
            if isinstance(other, dict):
                for source in other.values():
                    source._provides.add(self._source)
            else:
                for _, source in other:
                    source._provides.add(self._source)
            super().update(other)

        def __ior__(  # type: ignore
            self,
            other: dict[str, "Source"] | Iterable[tuple[str, "Source"]]
        ) -> "Source.Dependencies":
            self.update(other)
            return self

    @staticmethod
    def _get_path(path: str | Path) -> Path:
        if isinstance(path, str):
            path = Path(path)
        path = path.resolve()
        if Path.cwd() not in path.parents:
            FAIL(
                f"source file must be contained within the project directory: "
                f"{CYAN}{path}{WHITE}"
            )
        path = path.relative_to(Path.cwd())
        if not path.exists():
            FAIL(f"source file does not exist: {CYAN}{path}{WHITE}")
        if path.is_dir():
            FAIL(f"source file is a directory: {CYAN}{path}{WHITE}")
        return path

    def __init__(
        self,
        path: str | Path,
        *,
        cpp_std: int | None = None,
        extra_sources: Iterable[str | Path | Source] | None = None,
        extra_include_dirs: Iterable[Path] | None = None,
        extra_define_macros: Iterable[tuple[str, str | None]] | None = None,
        extra_library_dirs: Iterable[Path] | None = None,
        extra_libraries: Iterable[str] | None = None,
        extra_runtime_library_dirs: Iterable[Path] | None = None,
        extra_compile_args: Iterable[str] | None = None,
        extra_link_args: Iterable[str] | None = None,
        extra_cmake_args: Iterable[tuple[str, str | None]] | None = None,
    ) -> None:
        path = self._get_path(path)
        super().__init__(
            ".".join(path.with_suffix("").parts),
            [path.as_posix()],
            include_dirs=[p.as_posix() for p in extra_include_dirs or []],
            define_macros=list(extra_define_macros) if extra_define_macros else [],
            library_dirs=[p.as_posix() for p in extra_library_dirs or []],
            libraries=list(extra_libraries) if extra_libraries else [],
            runtime_library_dirs=[p.as_posix() for p in extra_runtime_library_dirs or []],
            extra_compile_args=list(extra_compile_args) if extra_compile_args else [],
            extra_link_args=list(extra_link_args) if extra_link_args else [],
            language="c++",
        )
        self._path = path
        self._cpp_std = cpp_std
        self._extra_sources = set(
            s if isinstance(s, Source) else self.spawn(s) for s in extra_sources or []
        )
        self._extra_cmake_args = list(extra_cmake_args) if extra_cmake_args else []
        self._makedeps: set[Path] = {self.path}
        self._requires = self.Dependencies(self)
        self._provides: set[Source] = set()
        self._module = ""
        self._command: BuildSources  # set in finalize_options
        self._is_interface = False  # set in stage 1
        self._is_executable = False  # set in stage 2
        self._previous_compile_hash = ""  # set in stage 2

    ######################
    ####    CONFIG    ####
    ######################

    @property
    def path(self) -> Path:
        """The relative path to the source file.

        Returns
        -------
        Path
            The path to the source file.
        """
        return self._path

    @property
    def name(self) -> str:
        """A dotted name synthesized from the relative path of the source file.

        Returns
        -------
        str
            A unique identifier for the source file.
        """
        return self.__dict__["name"]

    @name.setter
    def name(self, value: str) -> None:
        self.__dict__["name"] = value

    @name.deleter
    def name(self) -> None:
        self.__dict__["name"] = ".".join(self.path.with_suffix("").parts)

    @property
    def cpp_std(self) -> int:
        """The C++ standard to use when compiling the source as a target.

        Returns
        -------
        int
            The C++ standard version to use when compiling the source file.

        Raises
        ------
        ValueError
            If the C++ standard is < 23.

        See Also
        --------
        bertrand.setup : sets global options and executes the build process.

        Notes
        -----
        This property is initialized by the equivalent `setup()` argument if it is not
        explicitly overridden in the `Source` constructor.
        """
        std = self.command._bertrand_cpp_std if self._cpp_std is None else self._cpp_std
        if std < 23:
            raise ValueError(f"C++ standard must be >= 23: {self._cpp_std}")
        return std

    @cpp_std.setter
    def cpp_std(self, value: int) -> None:
        if value < 23:
            raise ValueError(f"C++ standard must be >= 23: {value}")
        self._cpp_std = value

    @cpp_std.deleter
    def cpp_std(self) -> None:
        self._cpp_std = None

    @property
    def extra_sources(self) -> set[Source]:
        """A set of additional source files that should be included in the final
        build, but do not contribute to the module hierarchy.

        Returns
        -------
        set[Source]
            A set of additional source files to include in the build.

        Notes
        -----
        The sources that appear in this set will be compiled and linked into the final
        binary, but may not otherwise be reachable from the module hierarchy.  In
        typical usage, this will remain empty, with all dependencies being
        automatically detected via module imports and exports.  In some cases, however,
        one may need to link against a static binary that is not part of the module
        system, in which case it can be added here.  Bertrand does this internally
        whenever Python bindings are generated for a C++ module, in order to provide a
        stable entry point for the Python interpreter.
        """
        return self._extra_sources

    @extra_sources.setter
    def extra_sources(self, value: set[Source]) -> None:
        self._extra_sources = value

    @extra_sources.deleter
    def extra_sources(self) -> None:
        self._extra_sources.clear()

    @property
    def extra_include_dirs(self) -> Paths:
        """A list of additional `-I` directories to pass to the compiler when this
        source is built.

        Returns
        -------
        list[Path]
            A list of path objects representing additional include directories.

        See Also
        --------
        bertrand.setup : sets global options and executes the build process.

        Notes
        -----
        These directories are passed *in addition* to any that were specified in the
        `bertrand.setup()` call.  
        """
        return self.Paths(self.include_dirs)

    @extra_include_dirs.setter
    def extra_include_dirs(self, value: list[Path]) -> None:
        self.include_dirs = [p.as_posix() for p in value]

    @extra_include_dirs.deleter
    def extra_include_dirs(self) -> None:
        self.include_dirs.clear()

    @property
    def extra_define_macros(self) -> list[tuple[str, str | None]]:
        """A list of `-D`/`#define` macros to pass to the compiler when this source is
        built.

        Returns
        -------
        list[tuple[str, str | None]]
            A list of tuples representing additional macros to define.  The second
            element of each tuple is optional, and will be omitted if set to None.

        See Also
        --------
        bertrand.setup : sets global options and executes the build process.

        Notes
        -----
        These macros are passed *in addition* to any that were specified in the
        `bertrand.setup()` call.
        """
        return self.define_macros

    @extra_define_macros.setter
    def extra_define_macros(self, value: list[tuple[str, str | None]]) -> None:
        self.define_macros = value

    @extra_define_macros.deleter
    def extra_define_macros(self) -> None:
        self.define_macros.clear()

    @property
    def extra_library_dirs(self) -> Paths:
        """A list of additional `-L` directories to pass to the linker when this
        source is built.

        Returns
        -------
        list[Path]
            A list of path objects representing additional library directories.

        See Also
        --------
        bertrand.setup : sets global options and executes the build process.

        Notes
        -----
        These directories are passed *in addition* to any that were specified in the
        `bertrand.setup()` call.
        """
        return self.Paths(self.library_dirs)

    @extra_library_dirs.setter
    def extra_library_dirs(self, value: list[Path]) -> None:
        self.library_dirs = [p.as_posix() for p in value]

    @extra_library_dirs.deleter
    def extra_library_dirs(self) -> None:
        self.library_dirs.clear()

    @property
    def extra_libraries(self) -> list[str]:
        """A list of additional `-l` symbols to link against when this source is
        built.

        Returns
        -------
        list[str]
            A list of library names to link against.

        See Also
        --------
        bertrand.setup : sets global options and executes the build process.

        Notes
        -----
        These libraries are linked *in addition* to any that were specified in the
        `bertrand.setup()` call.
        """
        return self.libraries

    @extra_libraries.setter
    def extra_libraries(self, value: list[str]) -> None:
        self.libraries = value

    @extra_libraries.deleter
    def extra_libraries(self) -> None:
        self.libraries.clear()

    @property
    def extra_runtime_library_dirs(self) -> Paths:
        """A list of additional `rpath` library directories to pass to the linker when
        this source is built.

        Returns
        -------
        list[Path]
            A list of path objects representing additional runtime library directories.

        See Also
        --------
        bertrand.setup : sets global options and executes the build process.

        Notes
        -----
        These directories are passed *in addition* to any that were specified in the
        `bertrand.setup()` call.
        """
        return self.Paths(self.runtime_library_dirs)

    @extra_runtime_library_dirs.setter
    def extra_runtime_library_dirs(self, value: list[Path]) -> None:
        self.runtime_library_dirs = [p.as_posix() for p in value]

    @extra_runtime_library_dirs.deleter
    def extra_runtime_library_dirs(self) -> None:
        self.runtime_library_dirs.clear()

    @property
    def extra_compile_args(self) -> list[str]:
        """A list of additional flags to pass to the compiler when this source is built.

        Returns
        -------
        list[str]
            A list of additional compiler flags.

        See Also
        --------
        bertrand.setup : sets global options and executes the build process.

        Notes
        -----
        These flags are passed *in addition* to any that were specified in the
        `bertrand.setup()` call.
        """
        return self.__dict__["extra_compile_args"]

    @extra_compile_args.setter
    def extra_compile_args(self, value: list[str]) -> None:
        self.__dict__["extra_compile_args"] = value

    @extra_compile_args.deleter
    def extra_compile_args(self) -> None:
        self.__dict__["extra_compile_args"].clear()

    @property
    def extra_link_args(self) -> list[str]:
        """A list of additional flags to pass to the linker when this source is built.

        Returns
        -------
        list[str]
            A list of additional linker flags.

        See Also
        --------
        bertrand.setup : sets global options and executes the build process.

        Notes
        -----
        These flags are passed *in addition* to any that were specified in the
        `bertrand.setup()` call.
        """
        return self.__dict__["extra_link_args"]

    @extra_link_args.setter
    def extra_link_args(self, value: list[str]) -> None:
        self.__dict__["extra_link_args"] = value

    @extra_link_args.deleter
    def extra_link_args(self) -> None:
        self.__dict__["extra_link_args"].clear()

    @property
    def extra_cmake_args(self) -> list[tuple[str, str | None]]:
        """A list of additional cmake arguments to pass to the build system when this
        source is built.

        Returns
        -------
        list[tuple[str, str | None]]
            A list of tuples representing additional cmake arguments.  The second
            element of each tuple is optional, and will be omitted if set to None.

        See Also
        --------
        bertrand.setup : sets global options and executes the build process.

        Notes
        -----
        These arguments are passed *in addition* to any that were specified in the
        `bertrand.setup()` call.
        """
        return self._extra_cmake_args

    @extra_cmake_args.setter
    def extra_cmake_args(self, value: list[tuple[str, str | None]]) -> None:
        self._extra_cmake_args = value

    @extra_cmake_args.deleter
    def extra_cmake_args(self) -> None:
        self._extra_cmake_args.clear()

    ############################
    ####    BUILD SYSTEM    ####
    ############################

    @property
    def command(self) -> BuildSources:
        """A reference to the build command being used to compile this source.

        Returns
        -------
        BuildSources
            The build command that was set by the `bertrand.setup()` function.
        """
        return self._command

    @property
    def in_tree(self) -> bool:
        """Indicates whether this source is part of the project tree.

        Returns
        -------
        bool
            True if the source was provided to `bertrand.setup()`, or False if it was
            synthesized by the build system.

        Notes
        -----
        In-tree sources will be built as targets over the course of the build process,
        while out-of-tree sources are used to model external dependencies.  The latter
        are typically modules that have been imported from the environment or emitted
        by the build system itself (i.e. Python bindings).
        """
        return self in self.command.extensions

    @property
    def makedeps(self) -> set[Path]:
        """A set of Make-style dependencies for this source file.

        Returns
        -------
        set[Path]
            A set of paths that the source file depends on, including headers and the
            source file itself.

        Notes
        -----
        This set contributes to the `outdated` property.  Since it includes header
        dependencies, any changes to the source file or any of its included headers
        will trigger a rebuild.
        """
        return self._makedeps

    @property
    def relative_headers(self) -> set[Path]:
        """A set of in-tree header files that may be the target of a relative
        `#include` directive.

        Returns
        -------
        set[Path]
            A set of header files that are accessible via a relative path from the
            source file.

        Notes
        -----
        This property is used to copy over header dependencies when installing the
        project into the environment's `modules/` directory.  Each header will be
        copied into the module's subdirectory, such that the relative path from the
        source file is preserved.  When the module is included in a different project,
        all the headers will be in the expected location, and the compiler will be able
        to find them without any additional configuration.
        """
        return {
            p.relative_to(Path.cwd()) for p in self.makedeps if (
                Path.cwd() in p.parents and
                env.dir not in p.parents and
                p != self.path.absolute()
            )
        }

    @property
    def requires(self) -> Dependencies:
        """A mapping of imported module names to their corresponding Source objects.

        Note that some sources may be brought in from the environment or automatically
        generated by the build system over the course of the build process.

        Returns
        -------
        Dependencies
            A dictionary subclass that automatically updates the import graph when
            modified.

        Notes
        -----
        This property provides a way for users to traverse a source's dependency graph
        after a stage 1 dependency scan.  Modifying the dictionary after that point
        will update the import graph for subsequent builds.  This is not typically
        needed, but can be useful for debugging or manual intervention.
        """
        return self._requires

    @property
    def provides(self) -> set[Source]:
        """The set of sources that import this file.

        Returns
        -------
        set[Source]
            A set of sources that depend on this source file.

        Notes
        -----
        This property is automatically updated whenever the `requires` dictionary is
        modified, and should never be directly mutated.  It allows upward traversal of
        the dependency graph, which can be useful when updating the build
        configuration.
        """
        return self._provides

    @property
    def module(self) -> str:
        """The logical (dotted) name of the C++ module that this source exports, if
        any.

        Returns
        -------
        str
            The name of the module or an empty string if no module is exported.

        Notes
        -----
        This property is set after the initial dependency scan in stage 1, and will
        always match the import statement in any dependent files.
        """
        return self._module

    @property
    def is_interface(self) -> bool:
        """Indicates whether this source file exports a module interface.

        Returns
        -------
        bool
            True if the source exports a module interface.  False otherwise.

        Notes
        -----
        This property detects the presence of the `export` keyword in the module
        declaration, which differentiates betwen module implementation units and their
        associated interfaces.
        """
        return self._is_interface

    @property
    def is_primary_module_interface(self) -> bool:
        """Indicates whether this source represents a primary module interface unit.

        Returns
        -------
        bool
            True if the source exports a module that lacks partitions.  False
            otherwise.
        """
        return self._is_interface and ":" not in self._module

    @property
    def is_executable(self) -> bool:
        """Indicates whether this source possesses a main() entry point.

        Returns
        -------
        bool
            True if a main() function was detected during AST parsing.  False
            otherwise.

        Notes
        -----
        This property is determined during stage 2 by bertrand's AST plugin.  If it is
        set to True, then the source will be compiled as an executable target, possibly
        in addition to a shared library if it also exports a primary module interface.
        """
        return self._is_executable

    @property
    def build_dir(self) -> Path:
        """The path to the build directory for this source.

        Returns
        -------
        Path
            The path to the CMake build directory for this source.

        Notes
        -----
        This directory is used to store the intermediate build artifacts when this
        source is built as a target in stage 2.
        """
        return self.command._bertrand_build_dir / "CMakeFiles" / f"{self.name}.dir"

    @property
    def mtime(self) -> float:
        """Get the latest modification time of the source file or any of its
        dependencies.

        Returns
        -------
        float
            The most recent modification time observed for the source file and its
            dependencies.  If any of the files are missing, then this value will be set
            to infinity.

        Notes
        -----
        This property is used to determine whether the source file has been modified
        since the last build.  If any of the build products are older than this time,
        they will be rebuilt automatically in stage 2.

        The modification time is computed by taking the maximum of the source file's
        modification time and the modification times of all its header and module
        dependencies.  If any of these files are missing, then the modification time is
        set to infinity, which will always trigger a rebuild, and therefore force the
        compiler to emit an error if the file is not found.
        """
        times = [
            dep.stat().st_mtime if dep.exists() else float("inf")
            for dep in self.makedeps
        ]
        times.extend(
            src.path.stat().st_mtime if src.path.exists() else float("inf")
            for src in self.extra_sources
        )
        times.extend(src.mtime for src in self.requires.collapse() if src is not self)
        return max(times)

    @property
    def object(self) -> Path:
        """The path to the compiled object for this source.

        Returns
        -------
        Path
            The path to the output object.

        Notes
        -----
        This file is guaranteed to exist after stage 2 of the build process, and will
        be reused in subsequent builds if the source file has not changed.  The object
        file is used to link the shared library and executable targets that depend on
        this source in stage 3.
        """
        path = self.build_dir / self.path.absolute().relative_to(Path.cwd().root)
        return path.parent / f"{path.name}.o"

    @property
    def compile_hash(self) -> str:
        """A token encoding the command-line arguments that are used to compile this
        source.

        Returns
        -------
        str
            A SHA256 hash of the relevant command-line arguments, in hexadecimal form.

        Notes
        -----
        This property is used to determine whether the source file has been modified
        since the last build, if the mtime is not sufficient.  This means that changing
        either the file itself, any of its dependencies, or the command used to compile
        it will trigger a rebuild.
        """
        return hashlib.sha256(" ".join([
            str(self.cpp_std),
            " ".join(str(p) for p in self.get_include_dirs()),
            " ".join(f"{k}={v}" if v else k for k, v in self.get_define_macros()),
            " ".join(str(p) for p in self.get_library_dirs()),
            " ".join(self.get_libraries()),
            " ".join(str(p) for p in self.get_runtime_library_dirs()),
            " ".join(self.get_compile_args()),
            " ".join(self.get_link_args()),
            " ".join(f"{k}={v}" if v else k for k, v in self._extra_cmake_args),
        ]).encode("utf-8")).hexdigest()

    @property
    def bmi_hash(self) -> str:
        """A token encoding the relevant command-line arguments to determine
        compatibility with prebuilt BMI files.

        Returns
        -------
        str
            A SHA256 hash of the relevant command-line arguments, in hexadecimal form.

        Notes
        -----
        Module interfaces are more stringent than headers when it comes to
        compatibility requirements.  Because they are essentially precompiled ASTs,
        they are sensitive to the exact compiler and command-line arguments that were
        used to produce them.  Luckily, we control the compiler since we're using
        Bertrand virtual environments, and we know the exact compile arguments because
        we're passing them in ourselves.  Thus, we can compute a unique identifier to
        label each BMI file and ensure that we only reuse them when the hash matches.
        """
        return hashlib.sha256(" ".join([
            str(self.cpp_std),
            " ".join(self.get_compile_args()),
            " ".join(f"{k}={v}" if v else k for k, v in self.get_define_macros()),
        ]).encode("utf-8")).hexdigest()

    @property
    def bmi_dir(self) -> Path:
        """Get the parent directory where compatible BMIs are stored.

        Returns
        -------
        Path
            A path to the build directory containing prebuilt BMI files that are
            compatible with this source.

        Notes
        -----
        When this source is compiled in stage 2, the build system will check this
        directory for cached BMI files that can be reused during incremental builds.
        By the end of stage 2, it is guaranteed to contain BMIs for this source and all
        of its dependencies.
        """
        return self.command._bertrand_module_cache / self.bmi_hash

    @property
    def bmis(self) -> Iterator[tuple[str, Path]]:
        """Return an iterator over the prebuilt BMIs that will be used when compiling
        this source.

        Yields
        ------
        tuple[str, Path]
            A tuple containing the cached module name and the path to the corresponding
            BMI file.

        Notes
        -----
        This property is used to determine which prebuilt BMIs can be reused when
        compiling this source in stage 2.  It will only include BMIs that are
        both compatible with this source and up-to-date with respect to its
        dependencies.
        """
        bmi_dir = self.bmi_dir
        mtime = self.mtime
        for dep in self.requires.collapse():
            if dep.module:
                bmi = bmi_dir / f"{dep.module}.pcm"
                if bmi.exists() and bmi.stat().st_mtime > mtime:
                    yield dep.module, bmi

    @property
    def outdated(self) -> bool:
        """Indicates whether this source should be rebuilt in stage 2.

        Returns
        -------
        bool
            True if any of the build artifacts are missing or out of date with respect
            to the source file or any of its dependencies.  False if they can be
            reused.

        Notes
        -----
        This property controls whether the target is added to the stage 2 build list,
        after which it will always evaluate to false.
        """
        # if a source's compile command has changed, then it is outdated by definition
        if self._previous_compile_hash and self.compile_hash != self._previous_compile_hash:
            return True

        # in-tree sources are associated with full objects and BMIs
        if self.in_tree:
            # check all bmis are up to date
            if len(list(self.bmis)) != len(self.requires):
                return True

            # check object output is up to date
            if self.is_primary_module_interface or not self.module:
                obj = self.object
                if not obj.exists() or obj.stat().st_mtime < self.mtime:
                    return True

        # out-of-tree sources are not covered by the dependency scan, and are only
        # reliably associated with a BMI file.
        elif self.module:
            bmi = self.bmi_dir / f"{self.module}.pcm"
            if not bmi.exists() or bmi.stat().st_mtime < self.mtime:
                return True

        return any(dep.outdated for dep in self.requires.values() if dep is not self)

    #######################
    ####    HELPERS    ####
    #######################

    def spawn(self, path: str | Path, module: str = "") -> Source:
        """Create a new Source that inherits the options from this source, but with a
        different path and optional module name.

        Parameters
        ----------
        path : str | Path
            The path to the new source file.
        module : str, optional
            The module name to assign to the new source.

        Returns
        -------
        Source
            A new Source object that inherits the options from this source.

        Notes
        -----
        This method is used to synthesize new source files during the build process and
        helps keep all the sources in sync with the original configuration.  Users
        should not need to consider it unless they are extending the build system to
        inject new sources into the dependency graph.
        """
        result = Source(
            self._get_path(path),
            cpp_std=self._cpp_std,
            extra_include_dirs=self.extra_include_dirs,
            extra_define_macros=self.extra_define_macros,
            extra_library_dirs=self.extra_library_dirs,
            extra_libraries=self.extra_libraries,
            extra_runtime_library_dirs=self.extra_runtime_library_dirs,
            extra_compile_args=self.extra_compile_args,
            extra_link_args=self.extra_link_args,
            extra_cmake_args=self.extra_cmake_args,
        )
        self._command._bertrand_source_lookup[result.path] = result
        result._command = self._command
        if module:
            self._command._bertrand_module_lookup[module] = result
            result._module = module
            result._is_interface = True
        return result

    def get_include_dirs(self) -> list[Path]:
        """Get the complete list of include directories for this source.

        Returns
        -------
        list[Path]
            A list of paths that concatenates the global include directories with the
            extras for this source.
        """
        return self.extra_include_dirs + self.command.global_include_dirs

    def get_define_macros(self) -> list[tuple[str, str | None]]:
        """Get the complete list of define macros for this source.

        Returns
        -------
        list[tuple[str, str | None]]
            A list of tuples that concatenates the global define macros with the extras
            for this source.
        """
        return self.command.global_define_macros + self.extra_define_macros

    def get_library_dirs(self) -> list[Path]:
        """Get the complete list of library directories for this source.

        Returns
        -------
        list[Path]
            A list of paths that concatenates the global library directories with the
            extras for this source.
        """
        return self.extra_library_dirs + self.command.global_library_dirs

    def get_libraries(self) -> list[str]:
        """Get the complete list of libraries for this source.

        Returns
        -------
        list[str]
            A list of library names that concatenates the global libraries with the
            extras for this source.
        """
        return self.command.global_libraries + self.extra_libraries

    def get_runtime_library_dirs(self) -> list[Path]:
        """Get the complete list of runtime library directories for this source.

        Returns
        -------
        list[Path]
            A list of paths that concatenates the global runtime library directories
            with the extras for this source
        """
        return self.extra_runtime_library_dirs + self.command.global_runtime_library_dirs

    def get_compile_args(self) -> list[str]:
        """Get the complete list of compiler flags for this source.

        Returns
        -------
        list[str]
            A list of flags that concatenates the global compile flags with the extras
            for this source.
        """
        return self.command.global_compile_args + self.extra_compile_args + ["-g"]

    def get_link_args(self) -> list[str]:
        """Get the complete list of linker flags for this source.

        Returns
        -------
        list[str]
            A list of flags that concatenates the global linker flags with the extras
            for this source.
        """
        return self.command.global_link_args + self.extra_link_args

    def get_cmake_args(self) -> list[tuple[str, str | None]]:
        """Get the complete list of cmake arguments for this source.

        Returns
        -------
        list[tuple[str, str | None]]
            A list of tuples that concatenates the global cmake arguments with the
            extras for this source.
        """
        return self.command.global_cmake_args + self.extra_cmake_args

    def __repr__(self) -> str:
        return f"<Source {self.path}>"


class BuildSources(setuptools_build_ext):
    """A custom build_ext command that uses a clang plugin to automatically generate
    cross-language Python/C++ bindings via ordinary import/export semantics.

    Must be used with the coupled `Source` class to describe build targets.

    Notes
    -----
    This command is intended to be placed within the `cmdclass` dictionary of a
    `setuptools.setup` call.  The `bertrand.setup()` function will normally do this
    automatically, but if you'd like to customize the build process in any way, you can
    subclass this command and pass it in manually.  Bertrand does this itself in order
    to customize the build process based on whether the user is installing within a
    virtual environment or not.
    """

    MIN_CMAKE_VERSION = Version("3.28")

    # additional command-line options accepted by the command
    user_options = setuptools_build_ext.user_options + [
        (
            "workers=",
            "j",
            "The number of parallel workers to use when building CMake extensions"
        ),
    ]

    def __init__(
        self,
        *args: Any,
        bertrand_conan_deps: list[Package],
        bertrand_cpp_std: int,
        bertrand_traceback: bool,
        bertrand_include_dirs: list[Path],
        bertrand_define_macros: list[tuple[str, str | None]],
        bertrand_compile_args: list[str],
        bertrand_library_dirs: list[Path],
        bertrand_libraries: list[str],
        bertrand_link_args: list[str],
        bertrand_cmake_args: list[tuple[str, str | None]],
        bertrand_runtime_library_dirs: list[Path],
        bertrand_workers: int,
        **kwargs: Any,
    ) -> None:
        super().__init__(*args, **kwargs)
        self.workers = bertrand_workers
        self._bertrand_cpp_std = bertrand_cpp_std
        self._bertrand_traceback = bertrand_traceback
        self._bertrand_conan_deps = bertrand_conan_deps
        self._bertrand_include_dirs = bertrand_include_dirs
        self._bertrand_define_macros = bertrand_define_macros
        self._bertrand_compile_args = bertrand_compile_args
        self._bertrand_library_dirs = bertrand_library_dirs
        self._bertrand_libraries = bertrand_libraries
        self._bertrand_link_args = bertrand_link_args
        self._bertrand_cmake_args = bertrand_cmake_args
        self._bertrand_runtime_library_dirs = bertrand_runtime_library_dirs
        self._bertrand_source_lookup: dict[Path, Source] = {}
        self._bertrand_module_lookup: dict[str, Source] = {}
        self._bertrand_build_dir: Path
        self._bertrand_binding_root: Path
        self._bertrand_module_root: Path
        self._bertrand_module_cache: Path
        self._bertrand_ast_cache: Path
        self._bertrand_clean_cache: Path

    def finalize_options(self) -> None:
        """Parse command-line options and convert them to the appropriate types.

        Raises
        ------
        TypeError
            If any extensions are not of type bertrand.Source
        ValueError
            If the workers option is not set to a positive integer or 0.
        """
        super().finalize_options()
        self._bertrand_build_dir = Path(self.build_lib)
        self._bertrand_binding_root = self._bertrand_build_dir / "bindings"
        self._bertrand_module_root = self._bertrand_build_dir / "modules"
        self._bertrand_module_cache = self._bertrand_build_dir / ".modules"
        self._bertrand_ast_cache = self._bertrand_build_dir / ".bertrand-ast.json"
        self._bertrand_clean_cache = env / "clean.json"
        if "-fdeclspec" not in self._bertrand_compile_args:
            self._bertrand_compile_args.append("-fdeclspec")

        # give each source a backreference to the global command
        self.check_extensions_list(self.extensions)
        for source in self.extensions:
            if not isinstance(source, Source):
                FAIL(
                    f"Extensions must be of type bertrand.Source: "
                    f"{YELLOW}{repr(source)}{WHITE}"
                )
            source._command = self
            self._bertrand_source_lookup[source.path] = source

        # add environment variables to the build configuration
        get_include = Path(__file__).parent.parent.parent  # evaluates to site-packages
        self._bertrand_include_dirs.append(get_include)
        cpath = env.get("CPATH", "")
        if cpath:
            self._bertrand_include_dirs.extend(
                Path(s) for s in cpath.split(os.pathsep)
            )
        ld_library_path = env.get("LD_LIBRARY_PATH", "")
        if ld_library_path:
            self._bertrand_library_dirs.extend(
                Path(s) for s in ld_library_path.split(os.pathsep)
            )
        runtime_library_path = env.get("RUNTIME_LIBRARY_PATH", "")
        if runtime_library_path:
            self._bertrand_runtime_library_dirs.extend(
                Path(s) for s in runtime_library_path.split(os.pathsep)
            )
        cxxflags = env.get("CXXFLAGS", "")
        if cxxflags:
            self._bertrand_compile_args = (
                shlex.split(cxxflags) + self._bertrand_compile_args
            )
        ldflags = env.get("LDFLAGS", "")
        if ldflags:
            self._bertrand_link_args = (
                shlex.split(ldflags) + self._bertrand_link_args
            )

        # parse and validate command line options
        if self.workers:
            self.workers = int(self.workers)
            if self.workers == 0:
                self.workers = os.cpu_count() or 1
            elif self.workers < 0:
                FAIL(
                    f"workers must be set to a positive integer or 0 to use all cores, "
                    f"not {CYAN}{self.workers}{WHITE}"
                )

    @property
    def global_build_dir(self) -> Path:
        """The path to the build directory for this command.

        Returns
        -------
        Path
            The path to the CMake build directory for this command.
        """
        return self._bertrand_build_dir

    @property
    def global_include_dirs(self) -> list[Path]:
        """Get the global list of include directories.

        Returns
        -------
        list[Path]
            A list of paths that will be appended as include directories of every
            source file.
        """
        return self._bertrand_include_dirs

    @property
    def global_define_macros(self) -> list[tuple[str, str | None]]:
        """Get the global list of define macros.

        Returns
        -------
        list[tuple[str, str | None]]
            A list of tuples that will be appended as define macros of every source
            file.
        """
        if not self._bertrand_traceback:
            # TODO: BERTRAND_NO_TRACEBACK is no longer supported - just use NDEBUG
            # to indicate a release build instead, which also disables assertions
            return self._bertrand_define_macros + [("BERTRAND_NO_TRACEBACK", None)]
        return self._bertrand_define_macros

    @property
    def global_library_dirs(self) -> list[Path]:
        """Get the global list of library directories.

        Returns
        -------
        list[Path]
            A list of paths that will be appended as library directories of every
            source file.
        """
        return self._bertrand_library_dirs

    @property
    def global_libraries(self) -> list[str]:
        """Get the global list of libraries.

        Returns
        -------
        list[str]
            A list of library names that will be appended as libraries of every source
            file.
        """
        return self._bertrand_libraries

    @property
    def global_runtime_library_dirs(self) -> list[Path]:
        """Get the global list of runtime library directories.

        Returns
        -------
        list[Path]
            A list of paths that will be appended as runtime library directories of
            every source file.
        """
        return self._bertrand_runtime_library_dirs

    @property
    def global_compile_args(self) -> list[str]:
        """Get the global list of compile arguments for a given source.

        Returns
        -------
        list[str]
            A list of compiler flags that will be appended as compile args of every
            source file.
        """
        return self._bertrand_compile_args

    @property
    def global_link_args(self) -> list[str]:
        """Get the global list of link arguments.

        Returns
        -------
        list[str]
            A list of linker flags that will be appended as link args of every source
            file.
        """
        return self._bertrand_link_args

    @property
    def global_cmake_args(self) -> list[tuple[str, str | None]]:
        """Get the global list of CMake arguments.

        Returns
        -------
        list[tuple[str, str | None]]
            A list of tuples that will be appended as CMake arguments of every source
            file.
        """
        return self._bertrand_cmake_args

    def build_extensions(self) -> None:
        """Run Bertrand's automated build process."""
        if self.extensions and "BERTRAND_HOME" not in os.environ:
            FAIL(
                "setup.py must be run inside a bertrand virtual environment in order "
                "to compile C++ extensions."
            )
        self.stage0()
        self.stage1()
        self.stage2()
        self.stage3()

    def stage0(self) -> None:
        """Install C++ dependencies into the environment before building the project.

        Notes
        -----
        This method represents a prerequisite step to the main build process, allowing
        users to specify C++ dependencies via the Conan package manager.  When Bertrand
        encounters these dependencies, it will automatically download and install them
        into the environment's `.conan` directory, and then export the necessary
        symbols to `env.toml` so that CMake can find them during the rest of the build
        process.
        """
        if self._bertrand_conan_deps:
            self._conan_install()
        self.global_libraries.extend(p.link for p in env.packages)  # type: ignore

    def _conan_install(self) -> None:
        """Invoke conan to install C++ dependencies for the project and link them
        against the environment.
        """
        path = self.global_build_dir / "conanfile.txt"
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("w") as f:
            f.write("[requires]\n")
            for package in self._bertrand_conan_deps:
                f.write(f"{package.name}/{package.version}\n")
            f.write("\n")
            f.write("[generators]\n")
            f.write("CMakeDeps\n")
            f.write("CMakeToolchain\n")
            f.write("\n")
            f.write("[layout]\n")
            f.write("cmake_layout\n")

        subprocess.check_call(
            [
                str(env / "bin" / "conan"),
                "install",
                str(path.absolute()),
                "--build=missing",
                "--output-folder",
                str(path.parent.absolute()),
                "-verror",
            ],
            cwd=path.parent,
        )
        env.packages.extend(p for p in self._bertrand_conan_deps if p not in env.packages)

    def stage1(self) -> None:
        """Run a dependency scan over the source files to resolve imports and exports.

        Notes
        -----
        This method represents the first stage of the automated build process.  Its
        primary goal is to convert a flat list of source files into an accurate
        dependency graph that can be used to generate build targets in stages 2 and 3.

        Here's a brief overview of the process:

            1.  Generate a temporary CMakeLists.txt file that includes all sources as a
                single build target.
            2.  Configure the project using CMake to emit a compile_commands.json
                database for use with clang-scan-deps.
            3.  Run clang-scan-deps to generate a graph of all imports and exports in
                the source tree.  Note that the dependency graph also includes logical
                imports for out-of-tree modules, which have to be synthesized by the
                build system.
            4.  Iterate over the dependency graph to identify exported modules and
                primary module interface units.  The latter will be built as
                Python-compatible shared libraries in stage 3.
            5.  Do a second pass over the dependency graph to resolve imports and emit
                C++ bindings where necessary.

        By the end of this process, each Source object will have its `requires` field
        populated with the dependency information necessary to build it as a target.
        The dependency graph can then be traversed in either direction, modified, or
        flattened into a set suitable for use in CMake.
        """
        cmakelists = self._stage1_cmakelists()
        compile_commands = self._get_compile_commands(cmakelists)
        self._get_makedeps(compile_commands)
        self._resolve_imports(compile_commands)

    def _cmakelists_header(self) -> str:
        build_type = "Debug" if self.debug else "Release"
        toolchain = (
            self.global_build_dir / "build" / build_type / "generators" /
            "conan_toolchain.cmake"
        )

        out = f"# CMakeLists.txt automatically generated by bertrand {__version__}\n"
        out += f"cmake_minimum_required(VERSION {self.MIN_CMAKE_VERSION})\n"
        out += f"project({self.distribution.get_name()} LANGUAGES CXX)\n"
        out += "\n"
        out += "# global config\n"
        out += f"set(CMAKE_BUILD_TYPE {build_type})\n"
        out += f"set(PYTHON_EXECUTABLE {sys.executable})\n"
        out +=  "set(CMAKE_POSITION_INDEPENDENT_CODE ON)\n"
        out +=  "set(CMAKE_CXX_SCAN_FOR_MODULES ON)\n"
        out +=  "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n"
        out +=  "set(CMAKE_COLOR_DIAGNOSTICS ON)\n"
        for k, v in self.global_cmake_args:
            if v is None:
                out += f"set({k})\n"
            else:
                out += f"set({k} {v})\n"
        if self.global_define_macros:
            out += "add_compile_definitions(\n"
            for define in self.global_define_macros:
                out += f"    {define[0]}={define[1]}\n"
            out += ")\n"
        if self.global_compile_args:
            out += "add_compile_options(\n"
            for flag in self.global_compile_args:
                out += f"    {flag}\n"
            out += ")\n"
        if self.global_link_args:
            out += "add_link_options(\n"
            for flag in self.global_link_args:
                out += f"    {flag}\n"
            out += ")\n"
        out += "\n"
        out += "# package management\n"
        out += f"include({toolchain.absolute()})\n"
        out += "\n".join(f'find_package({p.find} REQUIRED)' for p in env.packages)
        out += "\n"
        if self.global_include_dirs:
            out += "include_directories(\n"
            for include in self.global_include_dirs:
                out += f"    {include.absolute()}\n"
            out += ")\n"
        if self.global_library_dirs:
            out += "link_directories(\n"
            for lib_dir in self.global_library_dirs:
                out += f"    {lib_dir.absolute()}\n"
            out += ")\n"
        if self.global_libraries:
            out += "link_libraries(\n"
            for lib in self.global_libraries:
                out += f"    {lib}\n"
            out += ")\n"
        if self.global_runtime_library_dirs:
            out += "set(CMAKE_INSTALL_RPATH\n"
            for lib_dir in self.global_runtime_library_dirs:
                out += f"    \"{lib_dir.absolute()}\"\n"
            out += ")\n"
        return out

    def _stage1_cmakelists(self) -> Path:
        path = self.global_build_dir / "CMakeLists.txt"
        include_dirs: set[Path] = set()
        for source in self.extensions:
            include_dirs.update(p for p in source.extra_include_dirs)

        with path.open("w") as f:
            out = self._cmakelists_header()
            out += "\n"
            out += "# stage 1 target includes all sources\n"
            out += "add_library(${PROJECT_NAME} OBJECT\n"
            for source in self.extensions:
                out += f"    {source.path.absolute()}\n"
            out += ")\n"
            out += "target_sources(${PROJECT_NAME} PRIVATE\n"
            out +=  "    FILE_SET CXX_MODULES\n"
            out += f"    BASE_DIRS {Path.cwd()}\n"
            out +=  "    FILES\n"
            for source in self.extensions:
                out += f"        {source.path.absolute()}\n"
            out += ")\n"
            out += "set_target_properties(${PROJECT_NAME} PROPERTIES\n"
            out +=  "    PREFIX \"\"\n"
            out +=  "    OUTPUT_NAME ${PROJECT_NAME}\n"
            out +=  "    SUFFIX \"\"\n"
            out += f"    CXX_STANDARD {self._bertrand_cpp_std}\n"
            out +=  "    CXX_STANDARD_REQUIRED ON\n"
            out += ")\n"
            if include_dirs:
                out += "target_include_directories(${PROJECT_NAME} PRIVATE\n"
                for include in include_dirs:
                    out += f"    {include.absolute()}\n"
                out += ")\n"
            out += "\n"
            f.write(out)

        return path

    def _get_compile_commands(self, cmakelists: Path) -> Path:
        # configuring the project (but not building it!) will generate a complete
        # enough compilation database for clang-scan-deps to use
        cmakelists.parent.mkdir(parents=True, exist_ok=True)
        subprocess.check_call(
            [
                str(env / "bin" / "cmake"),
                "-G",
                "Ninja",
                "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                str(cmakelists.parent.absolute()),
            ],
            cwd=cmakelists.parent,
            stdout=subprocess.PIPE,  # NOTE: silences noisy CMake/Conan output
        )

        # ... but first, we have to filter out any lazily-evaluated arguments that
        # might not be present at the time this method is invoked
        path = cmakelists.parent / "compile_commands.json"
        with path.open("r+") as f:
            contents = json.load(f)
            filtered = [
                {
                    "directory": cmd["directory"],
                    "command": " ".join(
                        c for c in cmd["command"].split() if not c.startswith("@")
                    ),
                    "file": cmd["file"],
                    "output": cmd["output"],
                }
                for cmd in contents
            ]
            f.seek(0)
            json.dump(filtered, f, indent=4)
            f.truncate()

        return path

    def _find_source_from_output(self, output: Path) -> Source:
        # clang-scan-deps lists the dependencies by build *target* rather than
        # source path, so we need to cross reference that with our Source objects.
        # Luckily, CMake stores the targets in a nested source tree, so we can strip
        # the appropriate prefix/suffix and do some path arithmetic to get a 1:1 map.
        trunk = Path("CMakeFiles") / f"{self.distribution.get_name()}.dir"
        path = (Path.cwd().root / output.relative_to(trunk)).with_suffix("")
        return self._bertrand_source_lookup[path.relative_to(Path.cwd())]

    def _get_makedeps(self, compile_commands: Path) -> None:
        # p1689 format doesn't include header dependencies, so we have to re-run
        # clang-scan-deps to ensure these are considered when checking for updates.
        make = subprocess.run(
            [
                str(env / "bin" / "clang-scan-deps"),
                "-format=make",
                "-compilation-database",
                str(compile_commands),
            ],
            check=True,
            stdout=subprocess.PIPE,
        ).stdout.decode("utf-8").strip()

        # the output is in makefile format, consisting of a target followed by a
        # colon, and then a list of dependencies separated by spaces.  Trailing
        # backslashes are used for line continuation.
        source: Source | None = None
        sep = re.compile(r"(?<!\\)\s+")  # split on non-escaped whitespace
        for line in make.splitlines():
            line = line.rstrip("\\").strip()
            if line.endswith(":"):  # this is a target
                source = self._find_source_from_output(Path(line[:-1]))
            else:  # this is a dependency
                assert source is not None
                for dep in sep.split(line):
                    source.makedeps.add(Path(dep))

    def _resolve_imports(self, compile_commands: Path) -> None:
        # module dependencies are represented according to the p1689r5 spec, which is
        # what CMake uses internally to generate the build graph.  We need some
        # translation in order to parse this spec for our purposes.
        p1689 = json.loads(subprocess.run(
            [
                str(env / "bin" / "clang-scan-deps"),
                "-format=p1689",
                "-compilation-database",
                str(compile_commands),
            ],
            check=True,
            stdout=subprocess.PIPE,
        ).stdout.decode("utf-8").strip())

        # exports are represented by a "provides" array, which - for C++ modules -
        # should only have a single entry (but possibly more as an implementation
        # detail for private partitions, etc. depending on vendor).
        for module in p1689["rules"]:
            provides = module.get("provides", [])
            if provides:
                source = self._find_source_from_output(Path(module["primary-output"]))
                edge = provides[0]
                source._module = edge["logical-name"]
                source._is_interface = edge["is-interface"]
                source.requires[source.module] = source
                self._bertrand_module_lookup[source.module] = source

                # C++ does not enforce any specific meaning for dots in the module
                # name, so we need to force them to conform with Python semantics.
                # This maintains consistency between the two, and makes binding
                # generation much more intuitive/consistent.
                if source.is_primary_module_interface:
                    import_path = Path(*source.module.split("."))

                    # TODO: __init__.cpp is conceptually very nice, but conflicts
                    # with Python and makes it impossible to export Python-only
                    # code at the same time as an equivalent C++ module.  There
                    # has to be some way to resolve this, either by special rules
                    # in the build system or by some kind of in-code convention.
                    # -> The only thing I can think of is to mangle the compiled
                    # __init__ library slightly, and then importing from that in the
                    # Python code.  Maybe this could be done whenever a .py file is
                    # found with the same name as a source file.  It will always be
                    # prefixed with cpp_ to avoid conflicts with the Python module.
                    # The module can then import from it to expose python-only code.
                    # -> Maybe if a Python module is found with the same name, then
                    # the C++ library is automatically emitted with a cpp_prefix.  Or
                    # I can just fail the build, indicating that there's an ambiguity
                    # that needs to be resolved.  That forces me to decouple import
                    # semantics between the two, however, which might not be favorable.
                    # The prefix approach maintains consistent imports in both
                    # languages, but allows Python to intercept C++ code and filter it
                    # accordingly.  Ideally, this would be a .cpp suffix, but I'm not
                    # sure that works in Python.
                    #
                    #   # __init__.py
                    #   from .__init__.cpp import *

                    # __init__.cpp is a valid way of exporting a directory as a
                    # Python-style subpackage.
                    if source.path.stem == "__init__":
                        import_path /= "__init__"

                    expected = source.path.with_suffix("")
                    if import_path != expected:
                        correct = import_path.with_suffix(source.path.suffix)
                        alternative = '.'.join(expected.parts)
                        FAIL(
                            f"primary module interface '{YELLOW}{source.module}{WHITE}' "
                            f"must be exported from:\n"
                            f"    + {GREEN}{correct.absolute()}{WHITE}\n"
                            f"    - {RED}{source.path.absolute()}{WHITE}\n"
                            f"\n"
                            f"... or be renamed to '{YELLOW}{alternative}{WHITE}' to "
                            f"match Python semantics."
                        )

        # imports are represented by a "requires" array, which has one entry for
        # each import statement in the source file.  These can come in 3 flavors:
        #       1. in-tree imports between Sources
        #       2. out-of-tree imports from the env/modules/ directory
        #       3. Python imports that require a binding to be synthesized
        # These are written to the module lookup table for future reference.  Note that
        # exports must be resolved first in order to ensure that the module lookup
        # table is correctly populated.
        for module in p1689["rules"]:
            if "requires" in module:
                source = self._find_source_from_output(Path(module["primary-output"]))
                for edge in module["requires"]:
                    name = edge["logical-name"]
                    source.requires[name] = self._import(source, name)

    # TODO: importing bertrand.python from out-of-tree causes duplicate linker symbols
    # for some reason.

    def _import(self, source: Source, logical_name: str) -> Source:
        # check for a cached result first.  This covers absolute imports that are
        # contained within the source tree, as well as out-of-tree imports that have
        # already been resolved.
        if logical_name in self._bertrand_module_lookup:
            return self._bertrand_module_lookup[logical_name]

        # check for an equivalent module directory under env/modules/ and copy it into
        # the build tree along with its dependencies.  This covers out-of-tree imports
        # that are provided by the environment, including `import std`.
        search = env / "modules" / logical_name
        if search.exists():
            self._bertrand_module_root.mkdir(parents=True, exist_ok=True)
            dest = self._bertrand_module_root / logical_name
            shutil.copytree(search, dest, dirs_exist_ok=True)

            # the primary module interface's dotted name always matches the directory
            # structure, so we can trace it to obtain the correct source file.
            primary = dest / Path(*logical_name.split("."))
            primary_cppm = primary.with_suffix(".cppm")
            primary_init = primary / "__init__.cppm"
            if primary_cppm.exists():
                result = source.spawn(primary_cppm, logical_name)
            elif primary.exists() and primary.is_dir() and primary_init.exists():
                result = source.spawn(primary_init, logical_name)
            else:
                FAIL(
                    f"Could not locate primary module interface for out-of-tree import: "
                    f"'{YELLOW}{logical_name}{WHITE}' in {CYAN}{source.path}{WHITE}\n"
                    f"\n"
                    f"Please ensure that the primary module interface is located at "
                    f"one of the following paths:\n"
                    f"    + {GREEN}{primary_cppm.absolute()}{WHITE}\n"
                    f"    + {GREEN}{primary_init.absolute()}{WHITE}"
                )

            # dependencies are attached to the resulting Source just like normal, under
            # keys that may or may not reflect the actual imports within the .cppm file
            # itself.  This is done to avoid an additional scan over the file, and does
            # not impact how the module is built.
            for path in dest.rglob("*.cppm"):
                name = ".".join(path.relative_to(dest).with_suffix("").parts)
                result.requires[name] = source.spawn(path, name)

            return result

        breakpoint()

        # if the import is not found at the C++ level, then we attempt it from Python
        # and synthesize an equivalent C++ binding to resolve it.
        try:
            raise ImportError()
            module = importlib.import_module(logical_name)
        except ImportError:
            FAIL(
                f"unresolved import: '{YELLOW}{logical_name}{WHITE}' in "
                f"{CYAN}{source.path}{WHITE}"
            )

        self._bertrand_module_root.mkdir(parents=True, exist_ok=True)
        binding = self._bertrand_module_root / f"{logical_name}.cppm"
        PyModule(module).generate(binding)
        result = source.spawn(binding, logical_name)
        self._import(result, "bertrand.python")
        return result

    def stage2(self) -> None:
        """Instrument clang with bertrand's AST plugin and compile the project.

        Notes
        -----
        This method represents the second stage of the automated build process.  This
        is the stage where main compilation occurs, which produces complete shared
        libraries, executables, and Built Module Interfaces (BMIs).  It is also where
        the AST parser is invoked to generate Python bindings, which will be
        progressively folded into the products as they are built.  

        Here's a brief overview of the process:

            1.  Generate a CMakeLists.txt file that builds primary module interfaces
                as shared libraries and all other non-module sources as OBJECT files.
            2.  Compile the project using CMake.  This will run the AST parser over all
                sources and extend the libraries with the necessary Python bindings.
                OBJECTS are also checked for a `main()` entry point, which is written
                to a temporary cache.
            3.  Once all the OBJECTs are built, check the cache and generate
                corresponding executable targets via a custom CMake command.
            4.  Link the executables from the OBJECT files to form the final build
                artifacts.

        The products from this stage will then be passed into stage 3, where they will
        be installed into the environment and made available to the user.
        """
        self._ast_setup()
        cmakelists = self._stage2_cmakelists()
        self._cmake_build(cmakelists)
        self._ast_teardown()
        for source in self.extensions:
            bmi_dir = source.bmi_dir
            for pcm in source.build_dir.glob("*.pcm"):
                bmi_dir.mkdir(parents=True, exist_ok=True)
                # NOTE: don't use shutil.copy2 here, since we want the modification
                # time of the BMI to come AFTER the creation of the binding file.
                # This ensures stage 3 supports incremental builds like stage 2.
                shutil.copy(pcm, bmi_dir / pcm.name)

    # TODO: CMake can't read JSON, so I should probably add a custom command that
    # invokes Python to read the JSON output from the AST parser and produce a list
    # of executables that need to be built.  Or I can write to a separate cache that's
    # easier for CMake to parse.  A Python command would probably be faster though
    # since it doesn't require any separate IO.

    def _ast_setup(self) -> None:
        # if we're doing an incremental build, then we need to parse the cache and
        # identify which sources have been modified since the last build.  We then
        # reset those sources and allow the AST parser to update them accordingly.
        if self._bertrand_ast_cache.exists():
            cwd = Path.cwd()
            with self._bertrand_ast_cache.open("r+") as f:
                content = json.load(f)
                for k in content.copy():
                    p = Path(k).relative_to(cwd)
                    if p in self._bertrand_source_lookup:
                        v = content[k]
                        source = self._bertrand_source_lookup[p]
                        source._previous_compile_hash = v["command"]
                        if source.outdated:
                            v["command"] = source.compile_hash
                            v["executable"] = False
                            v["parsed"] = ""
                    else:
                        del content[k]
                f.seek(0)
                json.dump(content, f, indent=4)
                f.truncate()

        # if this is a fresh build, then everything is initialized to false and will
        # be populated by the AST parser directly.
        else:
            with self._bertrand_ast_cache.open("w") as f:
                init: dict[str, dict[str, str | bool]] = {}
                for source in self.extensions:
                    source._previous_compile_hash = source.compile_hash
                    init[str(source.path.absolute())] = {
                        "command": source._previous_compile_hash,
                        "executable": False,
                        "parsed": "",
                    }
                json.dump(init, f, indent=4)

    def _stage2_cmakelists(self) -> Path:
        out = self._cmakelists_header()
        out += "\n"

        out += "# stage 2 builds objects and shared libraries using the AST parser\n"
        out += "# and then backtracks to identify executables from the AST output\n"
        out += "set(OBJECTS)\n"
        out += "set(LIBRARIES)\n"

        # compile each source as an OBJECT library using the AST parser
        for source in self.extensions:
            if not source.module:
                target = f"{source.name}.o"
                out += f"add_library({target} OBJECT\n"
                out += f"    {source.path.absolute()}\n"
                for s in source.extra_sources:
                    out += f"    {s.path.absolute()}\n"
                out += ")\n"
                out += self._stage2_lib(source, target)
                out += f"list(APPEND OBJECTS ${{{target}}})\n"
                out += "\n"

            elif source.is_primary_module_interface:
                target = f"{source.name}.lib"
                out += f"add_library({target} SHARED\n"
                out += f"    {source.path.absolute()}\n"
                for s in source.extra_sources:
                    out += f"    {s.path.absolute()}\n"
                out += ")\n"
                out += self._stage2_lib(source, target)
                out += f"list(APPEND LIBRARIES ${{{target}}})\n"
                out += "\n"

        # add opaque targets for granular control over the build process
        out += "add_custom_target(objects ALL DEPENDS ${OBJECTS})\n"
        out += "add_custom_target(libraries ALL DEPENDS ${LIBRARIES})\n"

        # generate an executable target for each source that has a `main()` entry point
        out +=  "function(add_main_entry_point source)\n"
        out +=  "    # normalize the source path\n"
        out += f"    cmake_path(CONVERT \"{Path.cwd()}\" TO_CMAKE_PATH_LIST cwd NORMALIZE)\n"
        out +=  "\n"
        out +=  "    # extract the source path and stem minus the last extension\n"
        out +=  "    file(RELATIVE_PATH source_path \"${cwd}\" \"${source}\")\n"
        out +=  "    cmake_path(GET source_path PARENT_PATH source_dirs)\n"
        out +=  "    cmake_path(GET source_path STEM LAST_ONLY source_stem)\n"
        out +=  "\n"
        out +=  "    # concatenate to form a dotted name and OBJECT library\n"
        out +=  "    if (\"${source_dirs}\" STREQUAL \"\")\n"
        out +=  "        set(target \"${source_stem}\")\n"
        out +=  "    else()\n"
        out +=  "        string(REPLACE \"/\" \".\" dotted ${source_dirs})\n"
        out +=  "        set(target \"${dotted}.${source_stem}\")\n"
        out +=  "    endif()\n"
        out +=  "\n"
        out +=  "    # forward the object library's properties\n"
        out +=  "    get_target_property(source_cxx_std ${target}.o CXX_STANDARD)\n"
        out +=  "    get_target_property(source_cxx_std_req ${target}.o CXX_STANDARD_REQUIRED)\n"
        out +=  "    get_target_property(source_linker_lang ${target}.o LINKER_LANGUAGE)\n"
        out +=  "    get_target_property(source_link_dirs ${target}.o LINK_DIRECTORIES)\n"
        out +=  "    get_target_property(source_link_libs ${target}.o LINK_LIBRARIES)\n"
        out +=  "    get_target_property(source_cmake_args ${target}.o BERTRAND_CMAKE_ARGS)\n"
        out +=  "    get_target_property(source_bmi_dir ${target}.o BERTRAND_BMI_DIR)\n"
        out +=  "    get_target_property(source_link_args ${target}.o BERTRAND_LINK_ARGS)\n"
        out +=  "    get_target_property(source_runtime_library_dirs ${target}.o BERTRAND_RUNTIME_LIBRARY_DIRS)\n"
        out +=  "\n"
        out +=  "    # add an executable target that imports the precompiled OBJECT library\n"
        out +=  "    add_executable(${target}.exe $<TARGET_OBJECTS:${target}.o>)\n"
        out +=  "    set_target_properties(${target}.exe PROPERTIES\n"
        out +=  "        PREFIX \"\"\n"
        out += f"        OUTPUT_NAME ${{target}}{sysconfig.get_config_var('EXT_SUFFIX')}\n"
        out +=  "        SUFFIX \"\"\n"
        out +=  "        CXX_STANDARD ${source_cxx_std}\n"
        out +=  "        CXX_STANDARD_REQUIRED ${source_cxx_std_req}\n"
        out +=  "        LINKER_LANGUAGE ${source_linker_lang}\n"
        out +=  "        ${source_cmake_args}\n"
        out +=  "    )\n"
        out +=  "    target_link_options(${target}.exe PRIVATE\n"
        out +=  "        -fprebuilt-module-path=${source_bmi_dir}\n"
        out +=  "        ${source_link_args}"
        out +=  "        ${source_runtime_library_dirs}\n"
        out +=  "    )\n"
        out +=  "    target_link_directories(${target}.exe PRIVATE ${source_link_dirs})\n"
        out +=  "    target_link_libraries(${target}.exe PRIVATE ${source_link_libs})\n"
        out +=  "endfunction()\n"
        out +=  "\n"

        # define a custom command that runs after the OBJECT libraries are built
        out +=  "add_custom_command(\n"
        out +=  "    OUTPUT ${CMAKE_BINARY_DIR}/executables\n"
        out +=  "    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/executables.cmake\n"
        out +=  "    DEPENDS ${objects}\n"
        out +=  "    COMMENT \"Identifying executable targets\"\n"
        out +=  ")\n"
        out +=  "\n"

        # TODO: make sure the cache is written to the expected path in the correct
        # format.  I should probably use a flat list of paths to make this easier.
        # Whatever is easiest to handle on the CMake end.

        # write the custom cmake script that will be run by the command
        out += "file(WRITE ${CMAKE_BINARY_DIR}/executables.cmake \"\n"
        out +=  "    file(READ ${CMAKE_BINARY_DIR}/executable_cache contents)\n"
        out +=  "    foreach(entry IN LISTS contents)\n"
        out +=  "        add_main_entry_point(${entry})\n"
        out +=  "    endforeach()\n"
        out +=  "\")\n"
        out +=  "\n"

        # add a custom target that triggers the command and gives granular control
        out +=  "add_custom_target(executables ALL DEPENDS ${CMAKE_BINARY_DIR}/executables)\n"
        out +=  "add_custom_target(stage2 ALL DEPENDS ${objects} ${libraries} ${executables})\n"

        path = self.global_build_dir / "CMakeLists.txt"
        with path.open("w") as f:
            f.write(out)
        return path

    def _stage2_lib(self, source: Source, target: str) -> str:
        out = f"set({source.name}.cmake_args)\n"
        for key, value in source.get_cmake_args():
            if value is None:
                out += f"list(APPEND {source.name}.cmake_args {key})\n"
            else:
                out += f"list(APPEND {source.name}.cmake_args {key} {value})\n"
        out += f"set({source.name}.link_args)\n"
        for flag in source.get_link_args():
            out += f"list(APPEND {source.name}.link_args {flag})\n"
        out += f"set({source.name}.runtime_library_dirs)\n"
        for lib_dir in source.get_runtime_library_dirs():
            out += f"list(APPEND {source.name}.runtime_library_dirs {lib_dir.absolute()})\n"

        # add FILE_SET for all modules that need to be built
        prebuilt = dict(source.bmis)
        modules = {s for s in source.requires.collapse() if s.module not in prebuilt}
        if source.module:
            modules.add(source)
        if modules:
            out += f"target_sources({target} PRIVATE\n"
            out += f"    FILE_SET CXX_MODULES BASE_DIRS {Path.cwd()}\n"
            out +=  "    FILES\n"
            for s in modules:
                out += f"    {s.path.absolute()}\n"
            out += ")\n"

        # enable AST plugins + add prebuilt modules to compile commands
        # NOTE: importing std from env/modules/ causes clang to emit a warning
        # about a reserved module name, even when `std.cppm` came from clang
        # itself.  When `import std;` is officially supported by CMake, this can be
        # removed.
        # NOTE: attribute plugins mess with clangd, which can't recognize them by
        # default.  Until LLVM implements some way around this, disabling the
        # warning is the only option.
        out += f"target_compile_options({target} PRIVATE\n"
        out +=  "    -Wno-reserved-module-identifier\n"
        out +=  "    -Wno-unknown-attributes\n"
        for name, bmi in prebuilt.items():
            out += f"    -fmodule-file={name}={bmi.absolute()}\n"
        out += f"    -fplugin={env / 'lib' / 'bertrand-attrs.so'}\n"
        out += f"    -fplugin={env / 'lib' / 'bertrand-ast.so'}\n"
        if not source.module:
            cache_path = self._bertrand_ast_cache
            out +=  "    -fplugin-arg-main-run\n"
            out += f"    -fplugin-arg-main-cache={cache_path.absolute()}\n"
        if source.is_primary_module_interface:
            cache_path = self._bertrand_ast_cache
            binding_path = self._bertrand_binding_root / source.path
            binding_path.parent.mkdir(parents=True, exist_ok=True)
            imported_cpp_module = source.module
            exported_python_module = source.module.split(".")[-1]
            out +=  "    -fplugin-arg-export-run\n"
            out +=  "    -fparse-all-comments\n"
            out += f"    -fplugin-arg-export-module={source.path.absolute()}\n"
            out += f"    -fplugin-arg-export-import={imported_cpp_module}\n"
            out += f"    -fplugin-arg-export-export={exported_python_module}\n"
            out += f"    -fplugin-arg-export-binding={binding_path.absolute()}\n"
            out += f"    -fplugin-arg-export-cache={cache_path.absolute()}\n"
        compile_args = source.get_compile_args()
        for flag in compile_args:
            out += f"    {flag}\n"
        out += ")\n"
        out += f"target_link_options({target} PRIVATE\n"
        for name, bmi in prebuilt.items():
            out += f"    -fmodule-file={name}={bmi.absolute()}\n"
        out += f"    ${{{source.name}.link_args}}\n"
        out += f"    ${{{source.name}.runtime_library_dirs}}\n"
        out += ")\n"

        # pass any extra compile definitions, include directories, link dirs, and
        # link libraries to the target
        define_macros = source.get_define_macros()
        if define_macros:
            out += f"target_compile_definitions({target} PRIVATE\n"
            for first, second in define_macros:
                if second is None:
                    out += f"    {first}\n"
                else:
                    out += f"    {first}={second}\n"
            out += ")\n"
        include_dirs = source.get_include_dirs()
        if include_dirs:
            out += f"target_include_directories({target} PRIVATE\n"
            for include in include_dirs:
                out += f"    {include.absolute()}\n"
            out += ")\n"
        library_dirs = source.get_library_dirs()
        if library_dirs:
            out += f"target_link_directories({target} PRIVATE\n"
            for lib_dir in library_dirs:
                out += f"    {lib_dir.absolute()}\n"
            out += ")\n"
        libraries = source.get_libraries()
        if libraries:
            out += f"target_link_libraries({target} PRIVATE\n"
            for lib in libraries:
                out += f"    {lib}\n"
            out += ")\n"

        # configure target with CXX_STANDARD and any extra CMake arguments
        out += f"set_target_properties({target} PROPERTIES\n"
        out +=  "    PREFIX \"\"\n"
        out += f"    OUTPUT_NAME {target}\n"
        out +=  "    SUFFIX \"\"\n"
        try:
            out += f"    CXX_STANDARD {source.cpp_std}\n"
        except ValueError:
            FAIL(
                f"C++ standard must be >= 23: found {YELLOW}{source._cpp_std}"
                f"{WHITE} in {CYAN}{source.path}{WHITE}"
            )
        out +=  "    CXX_STANDARD_REQUIRED ON\n"
        out += f"    BERTRAND_CMAKE_ARGS \"${{{source.name}.cmake_args}}\""
        out += f"    BERTRAND_BMI_DIR \"{source.bmi_dir.absolute()}\"\n"
        out += f"    BERTRAND_LINK_ARGS \"${{{source.name}.link_args}}\"\n"
        out += f"    BERTRAND_RUNTIME_LIBRARY_DIRS \"${{{source.name}.runtime_library_dirs}}\"\n"
        out +=  ")\n"
        return out

    def _cmake_build(self, cmakelists: Path) -> None:
        # building the project using the AST plugin will emit the Python bindings
        # automatically as part of compilation, so we don't need to do anything
        # special to trigger it.
        subprocess.check_call(
            [
                str(env / "bin" / "cmake"),
                "-G",
                "Ninja",
                str(cmakelists.parent.absolute()),
                "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
            ],
            cwd=cmakelists.parent,
            stdout=subprocess.PIPE,
        )
        build_args = [str(env / "bin" / "cmake"), "--build", "."]
        if self.workers:
            build_args += ["--parallel", str(self.workers)]
        try:
            subprocess.check_call(build_args, cwd=cmakelists.parent)
        except subprocess.CalledProcessError:
            sys.exit()  # errors are already printed to the console

    def _ast_teardown(self) -> None:
        # The AST plugin modifies the build cache in-place, so we just read the results
        # back in to update the build system.
        with self._bertrand_ast_cache.open("r") as f:
            cwd = Path.cwd()
            for k, v in json.load(f).items():
                source = self._bertrand_source_lookup[Path(k).relative_to(cwd)]
                source._is_executable = v["executable"]
                if source._is_executable and source.module:
                    FAIL(
                        f"source file {CYAN}{source.path}{WHITE} cannot be both a "
                        f"{YELLOW}module{WHITE} and contain a {YELLOW}main(){WHITE} "
                        f"entry point.  Please import the module in a separate source "
                        f"file instead."
                    )

    def stage3(self) -> None:
        """

        Notes
        -----
        This method distributes the built binaries into the environment's directories
        such that they can be accessed from the command line or used by other projects.

        Executables will be installed to the environment's `bin/` directory, which is
        automatically added to the system path when the environment is activated.  This
        means that after installation, the executables can be run directly from the
        command line without any additional setup.  They will always be listed under
        the same name as the source file minus the file extension, so installing an
        executable with the source path `path/to/my_executable.cpp` will yield a binary
        called `my_executable` in the `bin/` directory, which can be invoked by running
        `$ my_executable` from the command line when the environment is active.

        Sources that describe primary module interface units will also be copied to the
        environment's `modules/` directory, which is automatically checked whenever an
        out-of-tree import is detected by Bertrand's build system.  This allows users
        to semantically `import` the module's dotted name from any other project
        compiled within the same environment, and the build system will pull in the
        necessary dependencies automatically.

        Shared libraries (including Python bindings) will be exported to the
        environment's `lib/` directory under a nested source tree.  External C++
        projects can link against these libraries by specifying the appropriate
        directory and library names in their compile command.  Additionally, if the
        `--inplace` flag is provided to `setup.py`, then the shared libraries will also
        be copied directly into the source tree, alongside their original source files.
        This facilitates editable installs, and avoids the need to go through
        `pip install` to test incremental changes.

        As the build products are copied out of the build directory, they are also
        added to a persistent cache that is stored within the environment.  This cache
        is used to implement the `$ bertrand clean` command, which removes any files
        that were copied out by this method.

        If any pre-existing files are detected in the environment that conflict with
        the final products, the build will be aborted by default and the environment
        will be left untouched.  This is to prevent accidental overwrites of core tools
        or libraries that may be in use by other projects.  Note that this behavior
        will *not* be triggered for files that were previously generated by Bertrand,
        as these are considered safe to overwrite.  If you wish to force the build and
        overwrite any conflicting files, you can use the `--force` flag when invoking
        `setup.py`.

        Once all of the above has been completed, the stage 2 compile_commands.json
        will also be copied into the project root, allowing IDEs and other tools to
        provide accurate syntax highlighting/code completion, etc.
        """
        # we need to load the previous contents of the clean.json file (if any) in
        # order to detect which files were previously generated by bertrand in the
        # event of a conflict.  Such files are safe to overwrite, but others are not.
        if self._bertrand_clean_cache.exists():
            with self._bertrand_clean_cache.open("r") as f:
                prev_cache = json.load(f)
        else:
            prev_cache = {}

        def _file_conflict(p: Path) -> bool:
            p = p.resolve()
            return (
                not self.force and
                p.exists() and
                str(p) not in prev_cache.get(str(Path.cwd()), [])
            )

        def _dir_conflict(p: Path) -> bool:
            p = p.resolve()
            return (
                not self.force and
                p.exists() and
                not any(p in Path(p2).parents for p2 in prev_cache.get(str(Path.cwd()), []))
            )

        try:
            cache: set[str] = set()
            for source in self.extensions:
                # executables are installed to the environment's bin/ directory
                if source.is_executable:
                    target = f"{source.name}.exe"
                    exe = env / "bin" / source.path.stem
                    if _file_conflict(exe):
                        raise FileExistsError(
                            f"cannot overwrite existing executable at {CYAN}{exe}{WHITE}"
                        )
                    shutil.copy2(self.global_build_dir / target, exe)
                    cache.add(str(exe))

                # primary module interface units are installed to both the environment's
                # lib/ and modules/ directories
                if source.is_primary_module_interface:
                    # shared libraries go in lib/
                    target = f"{source.name}{sysconfig.get_config_var('EXT_SUFFIX')}"
                    lib = env / "lib" / source.path.with_suffix(
                        sysconfig.get_config_var("SHLIB_SUFFIX")
                    )
                    if _file_conflict(lib):
                        raise FileExistsError(
                            f"cannot overwrite existing library at {CYAN}{lib}{WHITE}"
                        )
                    lib.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(self.global_build_dir / target, lib)
                    cache.add(str(lib))

                    # source files for the module interfaces go in modules/
                    module = env / "modules" / source.module
                    if _dir_conflict(module):
                        raise FileExistsError(
                            f"cannot overwrite existing module at {CYAN}{module}{WHITE}"
                        )
                    primary = module / source.path.with_suffix(".cppm")
                    primary.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(source.path, primary)
                    cache.add(str(primary))
                    for header in source.relative_headers:
                        p = module / header
                        p.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copy2(header, p)
                        cache.add(str(p))
                    for dep in source.requires.collapse():
                        if (
                            dep is not source and
                            self.global_build_dir not in dep.path.parents
                        ):
                            p = module / dep.path.with_suffix(".cppm")
                            p.parent.mkdir(parents=True, exist_ok=True)
                            shutil.copy2(dep.path, p)
                            cache.add(str(p))
                            for header in dep.relative_headers:
                                p = module / header
                                p.parent.mkdir(parents=True, exist_ok=True)
                                shutil.copy2(header, p)
                                cache.add(str(p))

            compile_commands = Path.cwd() / "compile_commands.json"
            shutil.copy2(
                self.global_build_dir / "compile_commands.json",
                compile_commands
            )
            cache.add(str(compile_commands))

        except FileExistsError as e:
            self._update_clean_cache(cache)
            clean(Path.cwd())
            FAIL(str(e))

        finally:
            self._update_clean_cache(cache)

    def _update_clean_cache(self, cache: set[str]) -> None:
        if not self._bertrand_clean_cache.exists():
            with self._bertrand_clean_cache.open("w") as f:
                f.write("{}")

        with self._bertrand_clean_cache.open("r+") as f:
            contents = json.load(f)
            cwd = str(Path.cwd())
            current = contents.get(cwd, [])
            current.extend(p for p in sorted(cache) if p not in current)
            contents[cwd] = current
            f.seek(0)
            json.dump(contents, f, indent=4)
            f.truncate()

    def copy_extensions_to_source(self) -> None:
        """Link the shared libraries into the source tree if the `--inplace` flag was
        used.

        Notes
        -----
        This method is only called if the `--inplace` flag is provided to setup.py.
        It creates a series of symlinks in the source tree that point to the shared
        libraries that were installed into the environment's `lib/` directory, allowing
        them to be imported locally alongside their source files, for rapid development
        and testing.
        """
        cache: set[str] = set()
        for source in self.extensions:
            lib_path = env / "lib" / source.path.with_suffix(
                sysconfig.get_config_var("SHLIB_SUFFIX")
            )
            str_path = self.get_ext_fullpath(source.name)
            cache.add(str_path)
            dest = Path(str_path)
            if lib_path.exists() and not dest.exists():
                os.symlink(lib_path, dest)

        self._update_clean_cache(cache)


# TODO: LIBRARY_PATH -> library_dirs.  LD_LIBRARY_PATH -> runtime_library_dirs.
# https://www.baeldung.com/linux/library_path-vs-ld_library_path


def setup(
    *,
    cpp_deps: Iterable[Package] | None = None,
    sources: list[Source] | None = None,
    cpp_std: int = 23,
    traceback: bool = True,
    include_dirs: Iterable[str | Path] | None = None,
    define_macros: Iterable[tuple[str, str | None]] | None = None,
    compile_args: Iterable[str] | None = None,
    library_dirs: Iterable[str | Path] | None = None,
    libraries: Iterable[str] | None = None,
    link_args: Iterable[str] | None = None,
    cmake_args: Iterable[tuple[str, str | None]] | None = None,
    runtime_library_dirs: Iterable[str | Path] | None = None,
    workers: int = 0,
    cmdclass: dict[str, Any] | None = None,
    **kwargs: Any
) -> None:
    """A custom setup() function that automatically appends the BuildSources command to
    the setup commands.

    Parameters
    ----------
    cpp_deps : Iterable[Package], optional
        A list of Conan packages to install before building the project.  Each package
        should be an instance of the `Package` class and must list the package name,
        version, and CMake `find_package`/`target_link_libraries` symbols.  These
        identifiers can typically be found by browsing Conan.io.
    sources : list[Source], optional
        A list of C++ `Source` files to build as extensions.  A separate `Source`
        object should be instantiated for every source file in the project, and the
        final build targets/dependencies will be contextually inferred from AST
        analysis of the sources.  See the `Source` class for more information.
    cpp_std : int, default 23
        The global C++ standard to use when compiling the sources.  Individual sources
        can override this setting by specifying their own standard in the `Source`
        constructor.  Must be >= 23.
    traceback : bool, default True
        A global setting indicating whether to enable cross-language tracebacks for the
        compiled sources.  If set to True, then any error that is thrown will retain a
        coherent stack trace across the language boundary, at the cost of some
        performance on the unhappy path.  This is a global option by necessity, and can
        only be set during the setup() call.
    include_dirs : Iterable[str | Path], optional
        A list of directories to search for header files when compiling the sources.
        These are translated into `-I{path}` flags in the compiler invocation, and are
        appended to any directories specified by the sources themselves, before those
        from the environment's `CPATH` variable and any paths implied by a Conan
        `find_package` directive.
    define_macros : Iterable[tuple[str, str | None]], optional
        A list of preprocessor macros to define when compiling the sources.  Each macro
        should be a tuple of the form `(name, value)`, which translates to a
        `-D{name}={value}` flag in the compiler invocation.  If `value` is None, then
        the tuple will expand to `-D{name}` without a value.  Individual sources can
        specify their own macros, which will be appended to this list.
    compile_args : Iterable[str], optional
        A list of additional compiler flags to pass to the compiler when building the
        sources.  These flags will be prepended to any flags specified by the sources
        themselves, after those from the environment's `CXXFLAGS` variable.
    library_dirs : Iterable[str | Path], optional
        A list of directories to search for libraries when linking the sources.  These
        are translated into `-L{path}` flags in the linker invocation, and are appended
        to any directories specified by the sources themselves, before those from the
        environment's `LD_LIBRARY_PATH` variable.
    libraries : Iterable[str], optional
        A list of library names to link against when building the sources.  These are
        translated into `-l{name}` flags in the linker invocation, and are appended to
        any libraries specified by the sources themselves.
    link_args : Iterable[str], optional
        A list of additional linker flags to pass to the linker when building the
        sources.  These flags will be prepended to any flags specified by the sources
        themselves, after those from the environment's `LDFLAGS` variable.
    cmake_args : Iterable[tuple[str, str | None]], optional
        A list of additional CMake arguments to pass to the CMake build system when
        building the sources.  These arguments are specified in the same format as
        `define_macros`, and are prepended to any arguments specified by the sources
        themselves.
    runtime_library_dirs : Iterable[str | Path], optional
        A list of runtime directories to search against when loading shared libraries
        at runtime.  These are translated into `-Wl,-rpath,{path}` flags in the linker
        invocation, and are appended to any directories specified by the sources
        themselves, before those from the environment's `RUNTIME_LIBRARY_PATH` variable.
    workers : int, default 0
        The number of parallel workers to use when building the sources.  If set to
        zero, then the build will use all available cores.  This can also be set
        through the command line by supplying either `--workers=n` or `-j n` to
        `$ bertrand compile`.
    cmdclass : dict[str, Any], optional
        A dictionary of command classes to override the default setuptools commands.
        If no setting is given for `"build_ext"`, then it will be set to
        `bertrand.BuildSources`, which enables Bertrand's automated build process.
        Manually specifying this command allows users to customize the build process
        by subclassing `bertrand.BuildSources` and overriding its methods.
    **kwargs : Any
        Arbitrary keyword arguments forwarded to `setuptools.setup()`.

    Notes
    -----
    This function represents the entry point for a Bertrand-enabled build system.  It's
    a drop-in replacement for `setuptools.setup()`, which invokes Bertrand's AST parser
    and automated build system to compile C++ sources into Python extensions.  See the
    docs for `bertrand.Source` and `bertrand.BuildSources` for more information on how
    this is done.
    """
    _cpp_deps: list[Package] = list(env.packages) if env else []
    for dep in cpp_deps or []:
        if not isinstance(dep, Package) or dep.shorthand:
            FAIL(
                f"C++ dependencies must be specified as Package objects with full "
                f"'{YELLOW}link{WHITE}' and '{YELLOW}find{WHITE}' symbols: "
                f"{CYAN}{dep}{WHITE}"
            )
        if dep not in _cpp_deps:
            _cpp_deps.append(dep)

    for source in sources or []:
        if not isinstance(source, Source):
            FAIL(
                f"sources must be instances of `{YELLOW}bertrand.Source{WHITE}`: "
                f" {CYAN}{source}{WHITE}"
            )

    _cpp_std = int(cpp_std)
    if _cpp_std < 23:
        FAIL(f"C++ standard must be >= 23: found {YELLOW}{_cpp_std}{WHITE}")

    _traceback = bool(traceback)
    _include_dirs = [p if isinstance(p, Path) else Path(p) for p in include_dirs or []]

    _define_macros = list(define_macros) if define_macros else []
    for macro in _define_macros:
        if (
            not isinstance(macro, tuple) or
            len(macro) != 2 or
            not isinstance(macro[0], str) or
            not isinstance(macro[1], (str, type(None)))
        ):
            FAIL(
                f"define macros must be specified as 2-tuples containing strings of "
                f"the form `{YELLOW}(name, value){WHITE}`: {CYAN}{macro}{WHITE}"
            )

    _compile_args = list(compile_args) if compile_args else []
    for arg in _compile_args:
        if not isinstance(arg, str):
            FAIL(f"compile arguments must be strings: {CYAN}{arg}{WHITE}")

    _library_dirs = [p if isinstance(p, Path) else Path(p) for p in library_dirs or []]
    _libraries = list(libraries) if libraries else []
    for lib in _libraries:
        if not isinstance(lib, str):
            FAIL(f"libraries must be strings: {CYAN}{lib}{WHITE}")

    _link_args = list(link_args) if link_args else []
    for arg in _link_args:
        if not isinstance(arg, str):
            FAIL(f"link arguments must be strings: {CYAN}{arg}{WHITE}")

    _cmake_args = list(cmake_args) if cmake_args else []
    for macro in _cmake_args:
        if (
            not isinstance(macro, tuple) or
            len(macro) != 2 or
            not isinstance(macro[0], str) or
            not isinstance(macro[1], (str, type(None)))
        ):
            FAIL(
                f"CMake arguments must be specified as 2-tuples containing strings of "
                f"the form `{YELLOW}(key, value){WHITE}`: {CYAN}{macro}{WHITE}"
            )

    _runtime_library_dirs = [
        p if isinstance(p, Path) else Path(p) for p in runtime_library_dirs or []
    ]

    class _BuildSourcesWrapper(BuildSources):
        def __init__(self, *a: Any, **kw: Any):
            super().__init__(
                *a,
                bertrand_conan_deps=_cpp_deps,
                bertrand_cpp_std=_cpp_std,
                bertrand_traceback=_traceback,
                bertrand_include_dirs=_include_dirs,
                bertrand_define_macros=_define_macros,
                bertrand_compile_args=_compile_args,
                bertrand_library_dirs=_library_dirs,
                bertrand_libraries=_libraries,
                bertrand_link_args=_link_args,
                bertrand_cmake_args=_cmake_args,
                bertrand_runtime_library_dirs=_runtime_library_dirs,
                bertrand_workers=workers,
                **kw
            )

    if cmdclass is None:
        cmdclass = {"build_ext": _BuildSourcesWrapper}
    elif "build_ext" not in cmdclass:
        cmdclass["build_ext"] = _BuildSourcesWrapper
    else:
        cmd: type = cmdclass["build_ext"]
        if not issubclass(cmd, BuildSources):
            FAIL(
                f"custom build_ext commands must subclass "
                f"`{YELLOW}bertrand.BuildSources{WHITE}`: {CYAN}{cmd}{WHITE}"
            )

        class _BuildSourcesSubclassWrapper(cmd):
            def __init__(self, *a: Any, **kw: Any):
                super().__init__(
                    *a,
                    bertrand_conan_deps=_cpp_deps,
                    bertrand_cpp_std=_cpp_std,
                    bertrand_traceback=_traceback,
                    bertrand_include_dirs=_include_dirs,
                    bertrand_define_macros=_define_macros,
                    bertrand_compile_args=_compile_args,
                    bertrand_library_dirs=_library_dirs,
                    bertrand_libraries=_libraries,
                    bertrand_link_args=_link_args,
                    bertrand_cmake_args=_cmake_args,
                    bertrand_runtime_library_dirs=_runtime_library_dirs,
                    bertrand_workers=workers,
                    **kw
                )
        cmdclass["build_ext"] = _BuildSourcesSubclassWrapper

    # defer to setuptools
    setuptools.setup(
        ext_modules=sources or [],
        cmdclass=cmdclass,
        **kwargs
    )


def clean(path: Path | None = None) -> None:
    """Remove all files that were copied out of the build directory by Bertrand's
    automated build system.

    Parameters
    ----------
    path : Path
        The path to the project's root directory, which contains the `setup.py` file.

    Raises
    ------
    RuntimeError
        If no environment is currently active.
    FileNotFoundError
        If no `clean.json` cache file could be found in the environment directory.

    Notes
    -----
    This function is invoked by the `$ bertrand clean` command, which removes all files
    that were installed into the environment by the automated build process.  These are
    tracked using a cache in the environment itself, which means users don't need to
    specify any of the paths themselves.

    If the project was built with the `--inplace` flag, then the shared libraries that
    were copied into the source tree will also be removed, leaving the project in a
    clean state for the next build.
    """
    cache = env / "clean.json"
    if not cache.exists():
        raise FileNotFoundError(f"cache file not found: {cache}")

    with cache.open("r+") as f:
        contents = json.load(f)

        if not path:
            for k, v in contents.copy().items():
                for s in v:
                    p = Path(s)
                    p.unlink(missing_ok=True)
                    p = p.parent
                    while not next(p.iterdir(), False):
                        p.rmdir()
                        p = p.parent
                contents.pop(k, None)
        else:
            cwd = str(path.resolve())
            for s in contents.get(cwd, []):
                p = Path(s)
                p.unlink(missing_ok=True)
                p = p.parent
                while not next(p.iterdir(), False):
                    p.rmdir()
                    p = p.parent
            contents.pop(cwd, None)
        f.seek(0)
        json.dump(contents, f, indent=4)
        f.truncate()
