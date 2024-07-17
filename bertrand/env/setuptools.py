"""Build tools for bertrand-enabled C++ extensions."""
from __future__ import annotations

import datetime
import hashlib
import importlib
import json
import os
import shlex
import shutil
import subprocess
import sys
import sysconfig
from pathlib import Path
from typing import Any, Iterator, Iterable, overload, cast, SupportsIndex, Mapping


import setuptools
from packaging.version import Version
from setuptools.command.build_ext import build_ext as setuptools_build_ext

from .codegen import PyModule
from .environment import env
from .messages import FAIL, WHITE, RED, YELLOW, GREEN, CYAN
from .package import Package, PackageLike
from .version import __version__


# import std; can be supported by doing this:
# https://discourse.llvm.org/t/libc-c-23-module-installation-support/77061/9
# https://discourse.llvm.org/t/llvm-discussion-forums-libc-c-23-module-installation-support/77087/27


# TODO: eventually, get_include() won't be necessary, since all the headers will be
# converted into modules, and the python module might be imported by default.


def get_include() -> str:
    """Get the path to the include directory for this package, which is necessary to
    make C++ headers available to the compiler.

    Returns
    -------
    str
        The path to the include directory for this package.
    """
    return str(Path(__file__).absolute().parent.parent.parent)


# TODO: I have to figure out how to get the stage 3 build to use the .pcm files
# generated in stage 2, and not recompile them itself.

# https://clang.llvm.org/docs/StandardCPlusPlusModules.html#header-units


# TODO: Object-orientation for Conan dependencies similar to Source?  Users would
# specify dependencies as Package("name", "version", "find", "link").


# TODO: each Source should hold a path to its associated build directory, under which
# .pcm files will be emitted.  The .o file is emitted under a nested source tree in
# the same directory, so we need to do some path manipulation to get it right.
# -> The build directory can be set during finalize_options, before the build process
# begins.  Finalize_options will set each source's _build_directory attribute, which
# is used in a public property of the same name, and a .object accessor that points
# to the output .o file.  There should also be another property that checks whether
# the .o file needs to be rebuilt, or if it can be reused from a previous build.  This
# has to recur through the dependency graph in order to account for changes in
# dependencies.


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

        # pylint: disable=protected-access

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

    def __init__(
        self,
        path: str | Path,
        *,
        cpp_std: int | None = None,
        extra_sources: list[str | Path | Source] | None = None,
        extra_include_dirs: list[Path] | None = None,
        extra_define_macros: list[tuple[str, str | None]] | None = None,
        extra_library_dirs: list[Path] | None = None,
        extra_libraries: list[str] | None = None,
        extra_runtime_library_dirs: list[Path] | None = None,
        extra_compile_args: list[str] | None = None,
        extra_link_args: list[str] | None = None,
        extra_cmake_args: list[tuple[str, str | None]] | None = None,
        traceback: bool | None = None,
    ) -> None:
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

        super().__init__(
            ".".join(path.with_suffix("").parts),
            [path.as_posix()],
            include_dirs=[p.as_posix() for p in extra_include_dirs or []],
            define_macros=extra_define_macros or [],
            library_dirs=[p.as_posix() for p in extra_library_dirs or []],
            libraries=extra_libraries or [],
            runtime_library_dirs=[p.as_posix() for p in extra_runtime_library_dirs or []],
            extra_compile_args=extra_compile_args or [],
            extra_link_args=extra_link_args or [],
            language="c++",
        )
        self._in_tree = False  # set in finalize_options
        self._path = path
        self._cpp_std = cpp_std
        self._extra_sources = set(
            s if isinstance(s, Source) else Source(s)
            for s in extra_sources or []
        )
        self._extra_cmake_args = extra_cmake_args or []
        self._traceback = traceback
        self._requires = self.Dependencies(self)
        self._provides: set[Source] = set()
        self._module = ""
        self._is_interface = False  # set in stage 1
        self._is_executable = False  # set in stage 2
        self._stage2_sources: set[Source] = set()  # set in stage 2

        self.include_dirs.append(get_include())  # TODO: eventually not necessary

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
        return self._in_tree

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
        assert self._cpp_std is not None
        if self._cpp_std < 23:
            raise ValueError(f"C++ standard must be >= 23: {self._cpp_std}")
        return self._cpp_std

    @cpp_std.setter
    def cpp_std(self, value: int) -> None:
        if value < 23:
            raise ValueError(f"C++ standard must be >= 23: {value}")
        self._cpp_std = value

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

    @property
    def traceback(self) -> bool:
        """Indicates whether mixed-language tracebacks are enabled for this source.

        Returns
        -------
        bool
            True if errors should maintain a full traceback across the language
            boundary.  False if they should be truncated instead.

        See Also
        --------
        bertrand.setup : sets global options and executes the build process.

        Notes
        -----
        This property is initialized by the equivalent `setup()` argument if it is not
        explicitly overridden in the `Source` constructor.
        """
        assert self._traceback is not None
        return self._traceback

    @traceback.setter
    def traceback(self, value: bool) -> None:
        self._traceback = value

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

    @requires.setter
    def requires(self, value: dict[str, Source]) -> None:
        self._requires.clear()
        self._requires.update(value)

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

    def spawn(self, path: Path, module: str = "") -> Source:
        """Create a new Source that inherits the options from this source, but with a
        different path and optional module name.

        Parameters
        ----------
        path : Path
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
            path,
            cpp_std=self._cpp_std,
            extra_sources=list(self.extra_sources),
            extra_include_dirs=self.extra_include_dirs,
            extra_define_macros=self.extra_define_macros,
            extra_library_dirs=self.extra_library_dirs,
            extra_libraries=self.extra_libraries,
            extra_runtime_library_dirs=self.extra_runtime_library_dirs,
            extra_compile_args=self.extra_compile_args,
            extra_link_args=self.extra_link_args,
            extra_cmake_args=self.extra_cmake_args,
            traceback=self.traceback,
        )
        if module:
            # pylint: disable=protected-access
            result._module = module
            result._is_interface = True
        return result

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
        bertrand_cpp_std: int,
        bertrand_cpp_deps: list[Package],
        bertrand_include_dirs: list[str],
        bertrand_define_macros: list[tuple[str, str]],
        bertrand_compile_args: list[str],
        bertrand_library_dirs: list[str],
        bertrand_libraries: list[str],
        bertrand_link_args: list[str],
        bertrand_cmake_args: dict[str, str],
        bertrand_runtime_library_dirs: list[str],
        bertrand_export_symbols: list[str],
        bertrand_workers: int,
        **kwargs: Any,
    ) -> None:
        super().__init__(*args, **kwargs)
        self.workers = bertrand_workers
        self._bertrand_cpp_std = bertrand_cpp_std
        self._bertrand_cpp_deps = bertrand_cpp_deps
        self._bertrand_include_dirs = bertrand_include_dirs
        self._bertrand_define_macros = bertrand_define_macros
        self._bertrand_compile_args = bertrand_compile_args
        self._bertrand_library_dirs = bertrand_library_dirs
        self._bertrand_libraries = bertrand_libraries
        self._bertrand_link_args = bertrand_link_args
        self._bertrand_cmake_args = bertrand_cmake_args
        self._bertrand_runtime_library_dirs = bertrand_runtime_library_dirs
        self._bertrand_export_symbols = bertrand_export_symbols
        self._bertrand_source_lookup: dict[Path, Source] = {}
        self._bertrand_module_lookup: dict[str, Source] = {}
        self._bertrand_build_dir: Path
        self._bertrand_module_root: Path
        self._bertrand_module_cache: Path
        self._bertrand_binding_root: Path
        self._bertrand_binding_cache: Path
        self._bertrand_executable_cache: Path

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
        self._bertrand_source_lookup = {s.path: s for s in self.extensions}
        self._bertrand_build_dir = Path(self.build_lib)
        self._bertrand_module_root = self._bertrand_build_dir / "modules"
        self._bertrand_module_cache = self._bertrand_build_dir / ".modules"
        self._bertrand_binding_root = self._bertrand_build_dir / "bindings"
        self._bertrand_binding_cache = self._bertrand_build_dir / ".bindings"
        self._bertrand_executable_cache = self._bertrand_build_dir / ".executables"
        if "-fdeclspec" not in self._bertrand_compile_args:
            self._bertrand_compile_args.append("-fdeclspec")

        # force the use of the coupled Source class to describe build targets
        self.check_extensions_list(self.extensions)
        incompabile_extensions = [
            ext for ext in self.extensions if not isinstance(ext, Source)
        ]
        if incompabile_extensions:
            FAIL(
                f"Extensions must be of type bertrand.Source: "
                f"{YELLOW}{incompabile_extensions}{WHITE}"
            )

        # add environment variables to the build configuration
        cpath = env.get("CPATH", "")
        if cpath:
            self._bertrand_include_dirs = (
                self._bertrand_include_dirs + cpath.split(os.pathsep)
            )

        ld_library_path = env.get("LD_LIBRARY_PATH", "")
        if ld_library_path:
            self._bertrand_library_dirs = (
                self._bertrand_library_dirs + ld_library_path.split(os.pathsep)
            )

        runtime_library_path = env.get("RUNTIME_LIBRARY_PATH", "")
        if runtime_library_path:
            self._bertrand_runtime_library_dirs = (
                self._bertrand_runtime_library_dirs +
                runtime_library_path.split(os.pathsep)
            )

        cxxflags = env.get("CXXFLAGS", "")
        if cxxflags:
            self._bertrand_compile_args = shlex.split(cxxflags) + self._bertrand_compile_args

        ldflags = env.get("LDFLAGS", "")
        if ldflags:
            self._bertrand_link_args = shlex.split(ldflags) + self._bertrand_link_args

        # parse workers from command line
        if self.workers:
            self.workers = int(self.workers)
            if self.workers == 0:
                self.workers = os.cpu_count() or 1
            elif self.workers < 0:
                FAIL(
                    f"workers must be set to a positive integer or 0 to use all cores, "
                    f"not {CYAN}{self.workers}{WHITE}"
                )

    def find_source(self, path: Path) -> Source:
        """Find the Source object corresponding to a given source file path.

        Parameters
        ----------
        path : Path
            The path to the source file.  Absolute paths will be normalized with
            respect to the current working directory.

        Returns
        -------
        Source
            The Source object that corresponds to the given file path.
        """
        return self._bertrand_source_lookup[path.resolve().relative_to(Path.cwd())]

    def find_module(self, name: str) -> Source:
        """Find the Source object corresponding to a given module name.

        Parameters
        ----------
        name : str
            The fully-qualified (dotted) name of the module, which must be absolute,
            with no leading dots.

        Returns
        -------
        Source
            The Source object that corresponds to the module name.
        """
        return self._bertrand_module_lookup[name]

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
        if self._bertrand_cpp_deps:
            self._conan_install()

    def _conan_install(self) -> None:
        """Invoke conan to install C++ dependencies for the project and link them
        against the environment.
        """
        path = self._bertrand_build_dir / "conanfile.txt"
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("w") as f:
            f.write("[requires]\n")
            for package in self._bertrand_cpp_deps:
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
                "conan",
                "install",
                str(path.absolute()),
                "--build=missing",
                "--output-folder",
                str(path.parent),
                "-verror",
            ],
            cwd=path.parent,
        )
        env.packages.extend(p for p in self._bertrand_cpp_deps if p not in env.packages)

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
        self._resolve_imports(compile_commands)

    def _cmakelists_header(self) -> str:
        build_type = "Debug" if self.debug else "Release"
        toolchain = (
            self._bertrand_build_dir / "build" / build_type / "generators" /
            "conan_toolchain.cmake"
        )
        libraries = self._bertrand_libraries + [p.link for p in env.packages]

        out = f"# CMakeLists.txt automatically generated by bertrand {__version__}\n"
        out += f"cmake_minimum_required(VERSION {self.MIN_CMAKE_VERSION})\n"
        out += f"project({self.distribution.get_name()} LANGUAGES CXX)\n"
        out += "\n"
        out += "# global config\n"
        out += f"set(CMAKE_BUILD_TYPE {build_type})\n"
        out += f"set(PYTHON_EXECUTABLE {sys.executable})\n"
        out += "set(CMAKE_CXX_SCAN_FOR_MODULES ON)\n"
        out += "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n"
        out += "set(CMAKE_COLOR_DIAGNOSTICS ON)\n"
        for k, v in self._bertrand_cmake_args.items():
            out += f"set({k} {v})\n"
        if self._bertrand_define_macros:
            out += "add_compile_definitions(\n"
            for define in self._bertrand_define_macros:
                out += f"    {define[0]}={define[1]}\n"
            out += ")\n"
        if self._bertrand_compile_args:
            out += "add_compile_options(\n"
            for flag in self._bertrand_compile_args:
                out += f"    {flag}\n"
            out += ")\n"
        if self._bertrand_link_args:
            out += "add_link_options(\n"
            for flag in self._bertrand_link_args:
                out += f"    {flag}\n"
            out += ")\n"
        out += "\n"
        out += "# package management\n"
        out += f"include({toolchain})\n"
        out += "\n".join(f'find_package({p.find} REQUIRED)' for p in env.packages)
        out += "\n"
        if self._bertrand_include_dirs:
            out += "include_directories(\n"
            for include in self._bertrand_include_dirs:
                out += f"    {include}\n"
            out += ")\n"
        if self._bertrand_library_dirs:
            out += "link_directories(\n"
            for lib_dir in self._bertrand_library_dirs:
                out += f"    {lib_dir}\n"
            out += ")\n"
        if libraries:
            out += "link_libraries(\n"
            for lib in libraries:
                out += f"    {lib}\n"
            out += ")\n"
        if self._bertrand_runtime_library_dirs:
            out += "set(CMAKE_INSTALL_RPATH\n"
            for lib_dir in self._bertrand_runtime_library_dirs:
                out += f"    \"{lib_dir}\"\n"
            out += ")\n"
        # TODO: what the hell to do with export_symbols?
        return out

    def _stage1_cmakelists(self) -> Path:
        path = self._bertrand_build_dir / "CMakeLists.txt"
        extra_include_dirs: set[Path] = set()
        extra_library_dirs: set[Path] = set()
        extra_libraries: set[str] = set()
        for source in self.extensions:
            extra_include_dirs.update(include for include in source.include_dirs)
            extra_library_dirs.update(lib_dir for lib_dir in source.library_dirs)
            extra_libraries.update(lib for lib in source.libraries)

        with path.open("w") as f:
            out = self._cmakelists_header()
            out += "\n"
            out += "# stage 1 target includes all sources\n"
            out += "add_library(${PROJECT_NAME}\n"
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
            if extra_include_dirs:
                out += "target_include_directories(${PROJECT_NAME} PRIVATE\n"
                for include in extra_include_dirs:
                    out += f"    {include}\n"
                out += ")\n"
            if extra_library_dirs:
                out += "target_link_directories(${PROJECT_NAME} PRIVATE\n"
                for lib_dir in extra_library_dirs:
                    out += f"    {lib_dir}\n"
                out += ")\n"
            if extra_libraries:
                out += "target_link_libraries(${PROJECT_NAME} PRIVATE\n"
                for lib in extra_libraries:
                    out += f"    {lib}\n"
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
            filtered = [
                {
                    "directory": cmd["directory"],
                    "command": " ".join(
                        c for c in cmd["command"].split() if not c.startswith("@")
                    ),
                    "file": cmd["file"],
                    "output": cmd["output"],
                }
                for cmd in json.load(f)
            ]
            f.seek(0)
            json.dump(filtered, f, indent=4)
            f.truncate()

        return path

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

        # first of all, p1689 lists the dependencies by build *target* rather than
        # source path, so we need to cross reference that with our Source objects.
        # Luckily, CMake stores the targets in a nested source tree, so we can strip
        # the appropriate prefix/suffix and do some path arithmetic to get a 1:1 map.
        trunk = Path("CMakeFiles") / f"{self.distribution.get_name()}.dir"
        cwd = Path.cwd()
        def _find(module: dict[str, Any]) -> Source:
            path = Path(module["primary-output"]).relative_to(trunk)
            path = (cwd.root / path).with_suffix("")  # strip .o suffix
            return self.find_source(path)

        # exports are represented by a "provides" array, which - for C++ modules -
        # should only have a single entry (but possibly more as an implementation
        # detail for private partitions, etc. depending on vendor).
        for module in p1689["rules"]:
            provides = module.get("provides", [])
            if provides:
                source = _find(module)
                edge = provides[0]
                source._module = edge["logical-name"]  # pylint: disable=protected-access
                source._is_interface = edge["is-interface"]  # pylint: disable=protected-access
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
                source = _find(module)
                for edge in module["requires"]:
                    name = edge["logical-name"]
                    source.requires[name] = self._import(source, name)

    def _import(self, source: Source, logical_name: str) -> Source:
        # check for a cached result first.  This covers absolute imports that are
        # contained within the source tree, as well as out-of-tree imports that have
        # already been resolved.
        if logical_name in self._bertrand_module_lookup:
            return self._bertrand_module_lookup[logical_name]

        # check for an equivalent .cppm file under env/modules/ and copy it into the
        # build directory along with its dependencies.  This covers out-of-tree imports
        # that are provided by the environment, including `import std`.
        search = env / "modules" / f"{logical_name}.cppm"
        if search.exists():
            self._bertrand_module_root.mkdir(parents=True, exist_ok=True)
            dest = self._bertrand_module_root / search.name
            shutil.copy2(search, dest)
            result = source.spawn(dest, logical_name)

            # dependencies are attached to the resulting Source just like normal, under
            # keys that may or may not reflect the actual imports within the .cppm file
            # itself.  This is done to avoid an additional scan over the file, and does
            # not impact how the module is built.
            deps = search.with_suffix("")
            if deps.exists() and deps.is_dir():
                dest = self._bertrand_module_root / deps.name
                shutil.copytree(deps, dest, dirs_exist_ok=True)
                for path in dest.rglob("*.cppm"):
                    dep = source.spawn(path, path.stem)
                    name = ".".join(path.relative_to(dest).with_suffix("").parts)
                    result.requires[name] = dep
                    self._bertrand_source_lookup[dep.path] = dep

            self._bertrand_source_lookup[result.path] = result
            self._bertrand_module_lookup[logical_name] = result
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
        result = Source(binding)
        self._import(result, "bertrand.python")
        self._bertrand_source_lookup[result.path] = result
        self._bertrand_module_lookup[logical_name] = result
        return result

    def stage2(self) -> None:
        """Instrument clang with bertrand's AST plugin and compile the project.

        Notes
        -----
        This method represents the second stage of the automated build process.  Its
        goal is to compile the source files into objects and built module interfaces
        (BMIs), parse the AST to compute Python bindings, and cache the results for
        future use.  The AST parser also identifies which source files contain a
        `main()` entry point, which will be linked as executables in stage 3.

        Here's a brief overview of the process:

            1.  Generate a temporary CMakeLists.txt file that builds all sources as
                OBJECT libraries, excluding private module partitions or implementation
                units, which are implicitly included in the primary module interface.
            2.  Check the build directory for precompiled BMIs and reuse them where
                possible to speed up compilation.
            3.  Pass the AST parser and related attributes to clang using the
                `-fplugin` flag, which will emit Python bindings as part of the
                compilation process.
            4.  Build the project, which emits `.o` artifacts for each source, along
                with BMIs for uncached modules and additional Python bindings for
                primary module interface units.
            5.  Copy the BMIs into the build directory's `.modules/` cache, which
                allows them to be reused on subsequent builds provided the compile
                command remains compatible and the underlying modules have not changed.

        The output from this stage will then be reused during stage 3 to link the
        final build artifacts using the results of the AST parser.
        """
        try:
            cmakelists = self._stage2_cmakelists()
            self._cmake_build(cmakelists)
            self._update_ast()
            for source in self.extensions:
                bmi_dir = self._bertrand_module_cache / self._bmi_hash(source)
                for pcm in self._build_dir(source).glob("*.pcm"):
                    bmi_dir.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(pcm, bmi_dir / pcm.name)
        finally:
            self._bertrand_binding_cache.unlink(missing_ok=True)
            self._bertrand_executable_cache.unlink(missing_ok=True)

    def _build_dir(self, source: Source) -> Path:
        return self._bertrand_build_dir / "CMakeFiles" / f"{source.name}.dir"

    def _object(self, source: Source) -> Path:
        path = self._build_dir(source) / source.path.absolute().relative_to(Path.cwd().root)
        return path.parent / f"{path.name}.o"

    def _needs_updating(self, source: Source) -> bool:
        # out-of-tree sources need special treatment since they are not included in the
        # stage 1 dependency scan.  In this case, we only need to check for an
        # equivalent .pcm file in the module cache and compare its modification time
        # against the source file.
        if not source.in_tree:
            bmi = self._bertrand_module_cache / self._bmi_hash(source)
            bmi /= f"{source.module}.pcm"
            return not bmi.exists() or bmi.stat().st_mtime < source.path.stat().st_mtime

        # if the source is in-tree, then it will be associated with a `.o` file in the
        # final build products, possibly in addition to a `.pcm` file if it is a module.
        # If these don't exist or are outdated for the source or any of its
        # dependencies, then it needs to be rebuilt.
        obj = self._object(source)
        if not obj.exists() or obj.stat().st_mtime < source.path.stat().st_mtime:
            return True
        if source.module:
            bmi = self._bertrand_module_cache / self._bmi_hash(source)
            bmi /= f"{source.module}.pcm"
            if not bmi.exists() or bmi.stat().st_mtime < source.path.stat().st_mtime:
                return True

        return any(
            self._needs_updating(dep)
            for dep in source.requires.collapse()
            if dep is not source
        )

    def _bmi_hash(self, source: Source) -> str:
        args = self._bertrand_compile_args + source.extra_compile_args
        definitions = self._bertrand_define_macros + source.extra_define_macros
        return hashlib.sha256(" ".join([
            str(source.cpp_std),
            " ".join(args),
            " ".join(
                f"{first}={second}" if second else first
                for first, second in definitions
            ),
        ]).encode("utf-8")).hexdigest()

    def _prebuilt_bmis(self, source: Source) -> Iterator[tuple[str, Path]]:
        bmi_dir = self._bertrand_module_cache / self._bmi_hash(source)
        for dep in source.requires.collapse():
            if dep.module:
                pcm = bmi_dir / f"{dep.module}.pcm"
                if pcm.exists() and pcm.stat().st_mtime > dep.path.stat().st_mtime:
                    yield dep.module, pcm

    def _stage2_cmakelists(self) -> Path:
        # pylint: disable=protected-access
        out = self._cmakelists_header()
        out += "\n"
        for source in self.extensions:
            # avoid rebuilding sources that have not been modified since last build
            if not self._needs_updating(source):
                continue

            # do not build private module partitions - they are implicitly included in
            # the primary module interface
            if source.module and not source.is_primary_module_interface:
                continue

            # BMIs can be reused where possible to speed up compilation
            bmis: dict[str, Path] = {}
            for dep in source.requires.collapse():
                if dep.module:
                    pcm = self._bertrand_module_cache / self._bmi_hash(dep)
                    pcm /= f"{dep.module}.pcm"
                    if pcm.exists() and pcm.stat().st_mtime > dep.path.stat().st_mtime:
                        bmis[dep.module] = pcm

            # build as a CMake OBJECT library
            target = source.name
            out += f"add_library({target} OBJECT\n"
            source._stage2_sources = {source, *source.extra_sources}
            for s in source._stage2_sources:  
                out += f"    {s.path.absolute()}\n"
            out += ")\n"

            # add FILE_SET for all modules that need to be built
            prebuilt = {name: path for name, path in self._prebuilt_bmis(source)}
            modules = {s for s in source.requires.collapse() if s.module not in prebuilt}
            if modules:
                out += f"target_sources({target} PRIVATE\n"
                out += f"    FILE_SET CXX_MODULES BASE_DIRS {Path.cwd()}\n"
                out +=  "    FILES\n"
                for s in modules:
                    out += f"    {s.path.absolute()}\n"
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
            for key, value in source.extra_cmake_args:
                if value is None:
                    out += f"    {key}\n"
                else:
                    out += f"    {key} {value}\n"
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
            out += f"    -fplugin-arg-main-cache={self._bertrand_executable_cache.absolute()}\n"
            if source.is_primary_module_interface:
                binding_cache = self._bertrand_binding_cache
                python_path = self._bertrand_binding_root / source.path
                python_path.parent.mkdir(parents=True, exist_ok=True)
                python_module = source.module.split(".")[-1]
                out += f"    -fplugin-arg-export-module={source.path.absolute()}\n"
                out += f"    -fplugin-arg-export-import={source.module}\n"
                out += f"    -fplugin-arg-export-export={python_module}\n"
                out += f"    -fplugin-arg-export-python={python_path.absolute()}\n"
                out += f"    -fplugin-arg-export-cache={binding_cache.absolute()}\n"
            for flag in source.extra_compile_args:
                out += f"    {flag}\n"
            out += ")\n"

            # pass any extra compile definitions, include directories, link dirs, and
            # link libraries to the target
            if source.extra_define_macros:
                out += f"target_compile_definitions({target} PRIVATE\n"
                for first, second in source.extra_define_macros:
                    if second is None:
                        out += f"    {first}\n"
                    else:
                        out += f"    {first}={second}\n"
                out += ")\n"
            if source.extra_include_dirs:
                out += f"target_include_directories({target} PRIVATE\n"
                for include in source.extra_include_dirs:
                    out += f"    {include.absolute()}\n"
                out += ")\n"
            if source.extra_library_dirs:
                out += f"target_link_directories({target} PRIVATE\n"
                for lib_dir in source.extra_library_dirs:
                    out += f"    {lib_dir.absolute()}\n"
                out += ")\n"
            if source.extra_libraries:
                out += f"target_link_libraries({target} PRIVATE\n"
                for lib in source.extra_libraries:
                    out += f"    {lib}\n"
                out += ")\n"
            if source.extra_link_args or source.extra_runtime_library_dirs:
                out += f"target_link_options({target} PRIVATE\n"
                for flag in source.extra_link_args:
                    out += f"    {flag}\n"
                for lib_dir in source.extra_runtime_library_dirs:
                    out += f"    -Wl,-rpath,{lib_dir.absolute()}\n"
                out += ")\n"

            out += "\n"

        path = self._bertrand_build_dir / "CMakeLists.txt"
        with path.open("w") as f:
            f.write(out)
        return path

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
        build_args = [
            str(env / "bin" / "cmake"),
            "--build",
            ".",
            "--config",
            "Debug" if self.debug else "Release",  # TODO: probably not necessary
        ]
        if self.workers:
            build_args += ["--parallel", str(self.workers)]
        try:
            subprocess.check_call(build_args, cwd=cmakelists.parent)
        except subprocess.CalledProcessError:
            sys.exit()  # errors are already printed to the console

    # TODO: the AST parser should only consider in-tree sources when generating
    # Python bindings.  Maybe the solution to that is to store a separate set
    # containing the sources that were manually provided to setup().  I can then
    # use the argument system to pass this information along to the parser.
    # -> These extra sources would not appear in the command's .extensions list, which
    # allows us to segregate them from the main build process.

    def _update_ast(self) -> None:
        # The AST plugin dumps the Python bindings to the build directory using a
        # nested directory structure under bindings/ that mirrors the source tree.  We
        # need to add these bindings to the appropriate Source objects so that they can
        # be included in the final build.
        for source in self.extensions:
            if source.is_primary_module_interface:
                source.extra_sources.add(
                    Source(self._bertrand_binding_root / source.path)
                )

        # Additionally, the AST plugin produces a cache that notes all of the source
        # files that possess a `main()` entry point.  We mark these in order to build
        # them as executables in stage 3.
        if self._bertrand_executable_cache.exists():
            with self._bertrand_executable_cache.open("r") as f:
                for line in f:
                    source = self.find_source(Path(line.strip()))
                    source._is_executable = True  # pylint: disable=protected-access

    # TODO: stage3 AST can use .o objects that were compiled in stage 2 to avoid
    # recompiling the world
    # https://stackoverflow.com/questions/38609303/how-to-add-prebuilt-object-files-to-executable-in-cmake
    # https://groups.google.com/g/dealii/c/HIUzF7fPyjs

    def stage3(self) -> None:
        """Link the products from stage 2 into Python-compatible shared libraries and
        executables.

        Notes
        -----
        This method represents the third and final stage of the automated build
        process.  Its goal is to compile the bindings emitted from stage 2, and then
        invoke the linker to complete the build.

        The outputs from this stage are shared libraries that expose the modules to the
        Python interpreter, and executables that can be run from the command line.
        The executables will be placed into the environment's `bin` directory, which is
        automatically added to the system path when the environment is activated,
        allowing them to be run directly from the command line.  Additionally, if the
        `--inplace` option was given to `setup.py`, the shared libraries will be copied
        into the source tree, allowing them to be imported alongside their sources.

        When the products are copied out of the build directory, they are also added to
        a persistent registry so that they can be cleaned up later with a
        `$ bertrand clean` command.  This will remove the files from the environment,
        but otherwise leave the build directory untouched.  If the project is
        reinstalled without any changes to the source tree, the products will be
        simply copied out of the build directory, avoiding the need to recompile them.
        """
        cmakelists = self._stage3_cmakelists()
        breakpoint()
        self._cmake_build(cmakelists)
        shutil.copy2(
            cmakelists.parent / "compile_commands.json",
            Path.cwd() / "compile_commands.json"
        )

    # TODO: stage 3 cmake build should also install the executables to env/bin and
    # store them in the registry for cleanup.  The shared libraries will be handled
    # by copy_extensions_to_source().

    def _stage3_cmakelists(self) -> Path:
        """Emit a final CMakeLists.txt file that includes semantically-correct
        shared library and executable targets based on the AST analysis, along with
        extra Python bindings to expose the modules to the interpreter.

        Returns
        -------
        Path
            The path to the generated CMakeLists.txt.
        """
        # pylint: disable=protected-access
        out = self._cmakelists_header()
        out += "\n"

        # import precompiled objects from stage 2
        timestamp = datetime.datetime.now().strftime("%Y%m%d%H%M%S.%f")
        out += "# stage 3 imports the object files that were compiled in stage 2\n"
        for source in self.extensions:
            out += f"add_library({source.name}.{timestamp} OBJECT IMPORTED)\n"
            out += f"set_target_properties({source.name}.{timestamp} PROPERTIES\n"
            out += f"    IMPORTED_LOCATION \"{self._object(source)}\"\n"
            out += ")\n"
        out += "\n"

        # link the final shared libraries and executables
        for source in self.extensions:
            if source.is_primary_module_interface:
                target = f"{source.name}{sysconfig.get_config_var('EXT_SUFFIX')}"
                stage3_sources = source.extra_sources - source._stage2_sources
                if not stage3_sources:
                    out += f"add_library({target} SHARED)\n"
                else:
                    out += f"add_library({target} SHARED\n"
                    for s in stage3_sources:
                        out += f"    {s.path.absolute()}\n"
                    out += ")\n"
                out += self._stage3_target(source, target, timestamp) + "\n"

            if source.is_executable:
                target = source.name
                stage3_sources = source.extra_sources - source._stage2_sources
                if not stage3_sources:
                    out += f"add_executable({target})\n"
                else:
                    out += f"add_executable({target}\n"
                    for s in stage3_sources:
                        out += f"    {s.path.absolute()}\n"
                    out += ")\n"
                out += self._stage3_target(source, target, timestamp) + "\n"

        path = self._bertrand_build_dir / "CMakeLists.txt"
        with path.open("w") as f:
            f.write(out)
        return path

    def _stage3_target(self, source: Source, target: str, timestamp: str) -> str:
        # modules built in stage 2 will be reused in stage 3, but it is possible that
        # between the two, a new module is added that needs to be built from scratch.
        # Typically, this should be empty, but it is included here for completeness.
        prebuilt = {name: path for name, path in self._prebuilt_bmis(source)}
        modules = {s for s in source.requires.collapse() if s.module not in prebuilt}
        out = ""
        if modules:
            out += f"target_sources({target} PRIVATE\n"
            out += f"    FILE_SET CXX_MODULES BASE_DIRS {Path.cwd()}\n"
            out +=  "    FILES\n"
            for s in modules:
                out += f"    {s.path.absolute()}\n"
            out += ")\n"

        # configure target with CXX_STANDARD and any extra CMake arguments
        out += f"set_target_properties({target} PROPERTIES\n"
        out +=  "    PREFIX \"\"\n"
        out += f"    OUTPUT_NAME {target}\n"
        out +=  "    SUFFIX \"\"\n"
        try:
            out += f"    CXX_STANDARD {source.cpp_std}\n"
        except ValueError:
            # pylint: disable=protected-access
            FAIL(
                f"C++ standard must be >= 23: found {YELLOW}{source._cpp_std}"
                f"{WHITE} in {CYAN}{source.path}{WHITE}"
            )
        out +=  "    CXX_STANDARD_REQUIRED ON\n"
        for key, value in source.extra_cmake_args:
            if value is None:
                out += f"    {key}\n"
            else:
                out += f"    {key} {value}\n"
        out += ")\n"

        # pass prebuilt modules from stage 2 and disable AST plugins
        bmi_dir = self._bertrand_module_cache / self._bmi_hash(source)
        out += f"target_compile_options({target} PRIVATE\n"
        out +=  "    -Wno-reserved-module-identifier\n"
        out +=  "    -Wno-unknown-attributes\n"
        out += f"    -fprebuilt-module-path={bmi_dir.absolute()}\n"
        out += f"    -fplugin={env / 'lib' / 'bertrand-attrs.so'}\n"
        for flag in source.extra_compile_args:
            out += f"    {flag}\n"
        out += ")\n"

        # link against imported objects from stage 2, as well as any additional link
        # libraries.
        out += f"target_link_libraries({target} PRIVATE\n"
        out += f"    {source.name}.{timestamp}\n"
        for lib in source.extra_libraries:
            out += f"    {lib}\n"
        out += ")\n"

        # link against imported objects from stage 2 and pass any extra compile
        # definitions, include directories, link dirs, and link libraries to the target.
        if source.extra_define_macros:
            out += f"target_compile_definitions({target} PRIVATE\n"
            for first, second in source.extra_define_macros:
                if second is None:
                    out += f"    {first}\n"
                else:
                    out += f"    {first}={second}\n"
            out += ")\n"
        if source.extra_include_dirs:
            out += f"target_include_directories({target} PRIVATE\n"
            for include in source.extra_include_dirs:
                out += f"    {include.absolute()}\n"
            out += ")\n"
        if source.extra_library_dirs:
            out += f"target_link_directories({target} PRIVATE\n"
            for lib_dir in source.extra_library_dirs:
                out += f"    {lib_dir.absolute()}\n"
            out += ")\n"
        if source.extra_link_args or source.extra_runtime_library_dirs:
            out += f"target_link_options({target} PRIVATE\n"
            for flag in source.extra_link_args:
                out += f"    {flag}\n"
            for lib_dir in source.extra_runtime_library_dirs:
                out += f"    -Wl,-rpath,{lib_dir.absolute()}\n"
            out += ")\n"

        return out

    # TODO: after everything is built, export the module interface files and their
    # dependencies to the env/modules/ directory, with the .cppm suffix.  Those source
    # files will be discovered by any other projects that are compiled in the same
    # environment.  It may not even need to be overridden, other than to record the
    # resulting paths in the registry for cleanup.

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
        breakpoint()
        self.stage3()

    # TODO: copy_extensions_to_source() will only be called if setup.py was invoked with
    # --inplace, so it can't do any copying that would otherwise be done by the install
    # process.  It should *only* copy the shared libraries into the source tree.
    # Everything else (exporting executables to bin/, module sources to env/modules/,
    # etc.) should be handled by a stage 4 method that is called by build_extensions().
    # This has the sole job of copying the build products to the appropriate
    # directories.

    # TODO: if files are copied out of the build directory, then they should be added
    # to a persistent registry so that they can be cleaned up later with a simple
    # command.  This is not necessary unless the project is being built inplace (i.e.
    # through `$ bertrand compile`), in which case the copy_extensions_to_source()
    # method will be called.  It should update a temp file in the registry with the
    # paths of all copied files, and then the clean command should read this file and
    # delete all the paths listed in it if they exist.
    # -> perhaps this is modeled as a directory within the virtual environment that
    # replicates the root directory structure.  Whenever `$ bertrand clean` is called,
    # we glob all the files in this directory and delete them, reflecting any changes
    # to the equivalent files in the source directory.  If `$ bertrand clean` is given
    # a particular file or directory, then it will only delete that file or directory
    # from the build directory.  If called without any arguments, it will clean the
    # whole environment.

    # TODO: `$ bertrand compile`` should also have an --install option that will copy
    # executables and shared libraries into the environment's bin and lib directories.
    # These should also be reflected in the `$ bertrand clean` command, which should
    # remove them if they exist, forcing a recompile.  `pip install .` will bypass
    # this by not writing them to the environment, which will disregard them from the
    # cleaning process unless they are recompiled with `$ bertrand compile`.

    def copy_extensions_to_source(self) -> None:
        """Copy executables as well as shared libraries to the source directory if
        setup.py was invoked with the --inplace option.
        """
        for ext in self.extensions:
            lib_path = (
                self._bertrand_build_dir /
                f"{ext.name}{sysconfig.get_config_var('EXT_SUFFIX')}"
            )
            exe_path = self._bertrand_build_dir / ext.name
            if lib_path.exists():
                self.copy_file(
                    lib_path,
                    self.get_ext_fullpath(ext.name),
                    level=self.verbose,  # type: ignore
                )
            if exe_path.exists():
                new_path = Path(self.get_ext_fullpath(ext.name)).parent
                idx = ext.name.rfind(".")
                if idx < 0:
                    new_path /= ext.name
                else:
                    new_path /= ext.name[idx + 1:]
                self.copy_file(exe_path, new_path, level=self.verbose)  # type: ignore


def setup(
    *,
    sources: list[Source] | None = None,
    cpp_std: int = 23,
    cpp_deps: Iterable[PackageLike] | None = None,
    traceback: bool = True,
    include_dirs: Iterable[str] | None = None,
    define_macros: Iterable[tuple[str, str]] | None = None,
    compile_args: Iterable[str] | None = None,
    library_dirs: Iterable[str] | None = None,
    libraries: Iterable[str] | None = None,
    link_args: Iterable[str] | None = None,
    cmake_args: dict[str, str] | None = None,
    runtime_library_dirs: Iterable[str] | None = None,
    export_symbols: Iterable[str] | None = None,
    workers: int = 0,
    cmdclass: dict[str, Any] | None = None,
    **kwargs: Any
) -> None:
    """A custom setup() function that automatically appends the BuildSources command to
    the setup commands.

    Parameters
    ----------
    sources : list[Source] | None, default None
        A list of C++ source files to build as extensions.  A separate Source should be
        given for every source file in the project, and the build targets and
        dependencies will be inferred from the AST analysis of the sources.
    cpp_deps : Iterable[PackageLike] | None, default None
        A list of C++ dependencies to install before building the extensions.  Each
        dependency should be specified as a string of the form
        `{name}/{version}@{find_package}/{target_link_libraries}`, where the
        `find_package` and `target_link_libraries` symbols are passed to the CMake
        commands of the same name.  These identifiers can typically be found by running
        `conan search ${package_name}` or by browsing conan.io.
    cmdclass : dict[str, Any] | None, default None
        A dictionary of command classes to override the default setuptools commands.
        If no setting is given for "build_ext", then it will be set to
        bertrand.setuptools.BuildSources.
    workers : int, default 0
        The number of parallel workers to use when building extensions.  If set to
        zero, then the build will use all available cores.  This can also be set
        through the command line by supplying either `--workers=n` or `-j n`.
    **kwargs : Any
        Arbitrary keyword arguments passed to the setuptools.setup() function.
    """
    deps: list[Package] = list(env.packages) if env else []
    for p in cpp_deps or []:
        package = Package(p, allow_shorthand=False)
        if package not in deps:
            deps.append(package)

    class _BuildSourcesWrapper(BuildSources):
        def __init__(self, *a: Any, **kw: Any):
            super().__init__(
                *a,
                bertrand_cpp_std=cpp_std,
                bertrand_cpp_deps=deps,
                bertrand_include_dirs=list(include_dirs) if include_dirs else [],
                bertrand_define_macros=list(define_macros) if define_macros else [],
                bertrand_compile_args=list(compile_args) if compile_args else [],
                bertrand_library_dirs=list(library_dirs) if library_dirs else [],
                bertrand_libraries=list(libraries) if libraries else [],
                bertrand_link_args=list(link_args) if link_args else [],
                bertrand_cmake_args=dict(cmake_args) if cmake_args else {},
                bertrand_runtime_library_dirs=
                    list(runtime_library_dirs) if runtime_library_dirs else [],
                bertrand_export_symbols=list(export_symbols) if export_symbols else [],
                bertrand_workers=workers,
                **kw
            )

    if cmdclass is None:
        cmdclass = {"build_ext": _BuildSourcesWrapper}
    elif "build_ext" not in cmdclass:
        cmdclass["build_ext"] = _BuildSourcesWrapper
    else:
        cmd: type = cmdclass["build_ext"]
        if issubclass(cmd, BuildSources):
            class _BuildSourcesSubclassWrapper(cmd):
                def __init__(self, *a: Any, **kw: Any):
                    super().__init__(
                        *a,
                        bertrand_cpp_std=cpp_std,
                        bertrand_cpp_deps=deps,
                        bertrand_include_dirs=
                            list(include_dirs) if include_dirs else [],
                        bertrand_define_macros=
                            list(define_macros) if define_macros else [],
                        bertrand_compile_args=
                            list(compile_args) if compile_args else [],
                        bertrand_library_dirs=
                            list(library_dirs) if library_dirs else [],
                        bertrand_libraries=
                            list(libraries) if libraries else [],
                        bertrand_link_args=
                            list(link_args) if link_args else [],
                        bertrand_cmake_args=
                            dict(cmake_args) if cmake_args else {},
                        bertrand_runtime_library_dirs=
                            list(runtime_library_dirs) if runtime_library_dirs else [],
                        bertrand_export_symbols=
                            list(export_symbols) if export_symbols else [],
                        bertrand_workers=workers,
                        **kw
                    )
            cmdclass["build_ext"] = _BuildSourcesSubclassWrapper

    # update sources with the global settings
    sources = sources or []
    for source in sources:
        # pylint: disable=protected-access
        source._in_tree = True

        if source._cpp_std is None:
            source._cpp_std = cpp_std

        if source._traceback is None:
            source._traceback = traceback
        if source.traceback:
            source.extra_compile_args.append("-g")
        else:
            source.define_macros.append(("BERTRAND_NO_TRACEBACK", None))

    # defer to setuptools
    setuptools.setup(
        ext_modules=sources,
        cmdclass=cmdclass,
        **kwargs
    )
