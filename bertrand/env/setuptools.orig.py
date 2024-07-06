"""Build tools for bertrand-enabled C++ extensions."""
from __future__ import annotations

import json
import importlib
import os
import re
import shutil
import subprocess
import sys
import sysconfig
from pathlib import Path
from typing import Any, Iterable, TextIO

import setuptools
from pybind11.setup_helpers import Pybind11Extension
from pybind11.setup_helpers import build_ext as pybind11_build_ext

from .codegen import PyModule
from .environment import env
from .package import Package, PackageLike
from .version import __version__


# TODO: eventually, get_include() won't be necessary, since all the headers will be
# converted into modules.


def get_include() -> str:
    """Get the path to the include directory for this package, which is necessary to
    make C++ headers available to the compiler.

    Returns
    -------
    str
        The path to the include directory for this package.
    """
    return str(Path(__file__).absolute().parent.parent.parent)


# TODO: write an AST parser and invoke it in the Extension constructor to detect
# module imports, main() function, and the public interface of the final Python module.


class Extension(Pybind11Extension):
    """A setuptools.Extension class that builds using CMake and supports C++20 modules.

    Parameters
    ----------
    *args, **kwargs : Any
        Arbitrary arguments passed to the Pybind11Extension constructor.
    conan : list[str], optional
        A list of Conan package names to install before building the extension.
    cxx_std : int, default 20
        The C++ standard to use when compiling the extension.  Values less than 20 will
        raise a ValueError.
    traceback : bool, default True
        If set to false, add `BERTRAND_NO_TRACEBACK` to the compile definitions, which
        will disable cross-language tracebacks for the extension.
    extra_cmake_args : dict[str, Any], optional
        Additional arguments to pass to the Extension's CMake configuration.  These are
        emitted as key-value pairs into a `set_target_properties()` block in the
        generated CMakeLists.txt file.  Some options are filled in by default,
        including `PREFIX`, `LIBRARY_OUTPUT_DIRECTORY`, `LIBRARY_OUTPUT_NAME`,
        `SUFFIX`, `CXX_STANDARD`, and `CXX_STANDARD_REQUIRED`.
    """

    MODULE_REGEX = re.compile(r"\s*export\s+module\s+(\w+).*;", re.MULTILINE)
    MAIN_REGEX = re.compile(r"\s*int\s+main\s*\(", re.MULTILINE)

    def __init__(
        self,
        *args: Any,
        cxx_std: int = 23,
        traceback: bool = True,
        extra_cmake_args: dict[str, Any] | None = None,
        **kwargs: Any
    ) -> None:
        if cxx_std < 23:
            raise ValueError(
                "C++ standard must be at least C++23 to enable bertrand features"
            )

        super().__init__(*args, **kwargs)
        self.cxx_std = cxx_std
        self.traceback = traceback
        self.extra_cmake_args = extra_cmake_args or {}
        self.extra_link_args = [sysconfig.get_config_var("LDFLAGS")] + self.extra_link_args

        self.include_dirs.append(get_include())
        # self.include_dirs.append(numpy.get_include())
        if self.traceback:
            self.extra_compile_args.append("-g")
            self.extra_link_args.append("-g")
        else:
            self.define_macros.append(("BERTRAND_NO_TRACEBACK", "ON"))

        self.modules: list[Path] = []
        executables: list[Path] = []
        for source in self.sources:
            path = Path(source)
            with path.open("r", encoding="utf_8") as f:
                content = f.read()
                if self.MODULE_REGEX.search(content):
                    self.modules.append(path)
                if self.MAIN_REGEX.search(content):
                    executables.append(path)

        if len(executables) > 1:
            raise ValueError(
                f"Cannot build extension: multiple main() functions detected in "
                f"{[str(p) for p in executables]}"
            )

        self.executable = executables.pop() if executables else None

    def scan_modules(self) -> dict[str, Any]:
        """Run clang-scan-deps on the sources to extract module dependencies.

        Returns
        -------
        dict[str, Any]
            A dictionary mapping module names to their dependency graph, in p1689r5
            (https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p1689r5.html)
            format.  This is the same format that is ultimately used by CMake to
            build the modules.
        """
        return json.loads(subprocess.run(
            [
                str(env / "bin" / "clang-scan-deps"),
                "-format=p1689",
                "--",
                "clang++",
                *self.sources,
                # *self.extra_compile_args,
                # *compile_args
            ],
            check=True,
            capture_output=True,
        ).stdout.decode("utf-8").strip())


# TODO: ConanFile should have separate write() and generate() methods, similar to
# CMakeLists?

class ConanFile:
    """A wrapper around a temporary conanfile.txt file generated by the build system
    when compiling C/C++ extensions.
    """

    def __init__(self, build_lib: Path, packages: list[Package]) -> None:
        self.path = build_lib.absolute() / "conanfile.txt"
        self.packages = packages

        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.touch(exist_ok=True)
        with self.path.open("w") as f:
            f.write("[requires]\n")
            for p in self.packages:
                f.write(f"{p.name}/{p.version}\n")
            f.write("\n")
            f.write("[generators]\n")
            f.write("CMakeDeps\n")
            f.write("CMakeToolchain\n")
            f.write("\n")
            f.write("[layout]\n")
            f.write("cmake_layout\n")

    def install(self) -> None:
        """Install the Conan packages listed in the conanfile.txt file and write the
        newly-installed packages to the env.toml file.
        """
        subprocess.check_call(
            [
                "conan",
                "install",
                str(self.path),
                "--build=missing",
                "--output-folder",
                str(self.path.parent),
                "-verror",
            ],
            cwd=self.path.parent
        )
        env.packages.extend(p for p in self.packages if p not in env.packages)


class CMakeLists:
    """A wrapper around a temporary CMakeLists.txt file generated by the build system
    when compiling C/C++ extensions.
    """

    MIN_VERSION = "3.28"  # CMake 3.28+ is necessary for C++20 module support

    # TODO: each extension is stored by name in a dictionary, where the values are the
    # actual Extension objects that make up the project.  When a binding file is added,
    # the source is added to the relevant extension.  There is then a `write()` method
    # that dumps the configuration to an output file, which can be called once for the
    # preconfiguration, and then again after all the bindings have been accounted for.

    # TODO: the most difficult problem is that I need a way to associate a binding file
    # to one or more extensions.  Maybe the best way to do that is to generate a source
    # map, where each source is a key, and the value is a list of extensions in which
    # that source is included.  Then, when a binding is added, I just look it up in
    # this map and insert it as a source for each extension.

    # TODO: Only one Python module can be created per Extension, since they are
    # compiled as a shared library.  This means that if multiple sources export a
    # module, only one of them should be correct.  Perhaps the solution to this is to
    # restrict extensions to only one source?  That would simplify the code and allow
    # me to automatically infer module names, etc.  All other imports would be
    # resolved through the module database.
    # -> Actually, I still need to allow multiple sources to allow for module
    # partitions, but I should still enforce only one module interface unit per
    # Extension.  Imports should always be straightforward in both languages that way.



    # TODO: __init__ takes build_lib and a list of extensions.  The write() method must
    # be called to actually produce the CMakeLists.txt file, and it can be called with
    # the project name, debug flag, include_dirs, library_dirs, and libraries.  The

    def __init__(
        self,
        build_lib: Path,
        project: str,
        debug: bool,
        include_dirs: list[str],
        library_dirs: list[str],
        libraries: list[str],
    ) -> None:
        build_lib = build_lib.absolute()
        build_type = "Debug" if debug else "Release"
        # libraries.extend(p.link for p in env.packages)

        self.project = project
        self.debug = debug
        self.path = build_lib / "CMakeLists.txt"
        toolchain = (
            build_lib / "build" / build_type / "generators" / "conan_toolchain.cmake"
        )

        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.touch(exist_ok=True)
        with self.path.open("w") as f:
            f.write(f"# CMakeLists.txt automatically generated by bertrand {__version__}\n")
            f.write(f"cmake_minimum_required(VERSION {self.MIN_VERSION})\n")
            f.write(f"project({project} LANGUAGES CXX)\n")
            f.write("\n")
            f.write("# global config\n")
            f.write(f"set(CMAKE_BUILD_TYPE {build_type})\n")
            f.write(f"set(PYTHON_EXECUTABLE {sys.executable})\n")
            f.write("set(CMAKE_COLOR_DIAGNOSTICS ON)\n")
            f.write("set(CMAKE_CXX_SCAN_FOR_MODULES ON)\n")
            f.write("set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n")
            f.write("\n")
            f.write("# package management\n")
            f.write(f"include({toolchain})\n")
            f.write('\n'.join(f'find_package({p.find} REQUIRED)' for p in env.packages))
            f.write("\n")
            if include_dirs:
                f.write("include_directories(\n")
                for include in include_dirs:
                    f.write(f"    {include}\n")
                f.write(")\n")
            if library_dirs:
                f.write("link_directories(\n")
                for lib_dir in library_dirs:
                    f.write(f"    {lib_dir}\n")
                f.write(")\n")
            if libraries:
                f.write("link_libraries(\n")
                for lib in libraries:
                    f.write(f"    {lib}\n")
                f.write(")\n")
            f.write("\n")

    def _target_sources(self, f: TextIO, target: str, ext: Extension) -> None:
        f.write(f"target_sources({target} PRIVATE\n")
        f.write( "    FILE_SET CXX_MODULES\n")
        f.write(f"    BASE_DIRS {Path.cwd()}\n")
        f.write( "    FILES\n")
        for source in ext.modules:
            f.write(f"        {Path(source).absolute()}\n")
        f.write(")\n")

    def _target_include_directories(self, f: TextIO, target: str, ext: Extension) -> None:
        f.write(f"target_include_directories({target} PRIVATE\n")
        for include in ext.include_dirs:
            f.write(f"    {include}\n")
        f.write(")\n")

    def _target_link_directories(self, f: TextIO, target: str, ext: Extension) -> None:
        f.write(f"target_link_directories({target} PRIVATE\n")
        for lib_dir in ext.library_dirs:
            f.write(f"    {lib_dir}\n")
        f.write(")\n")

    def _target_link_libraries(self, f: TextIO, target: str, ext: Extension) -> None:
        f.write(f"target_link_libraries({target} PRIVATE\n")
        for lib in ext.libraries:
            f.write(f"    {lib}\n")
        f.write(")\n")

    def _target_compile_options(self, f: TextIO, target: str, ext: Extension) -> None:
        f.write(f"target_compile_options({target} PRIVATE\n")
        for flag in ext.extra_compile_args:
            f.write(f"    {flag}\n")
        f.write(")\n")

    def _target_link_options(self, f: TextIO, target: str, ext: Extension) -> None:
        f.write(f"target_link_options({target} PRIVATE\n")
        for flag in ext.extra_link_args:
            f.write(f"    {flag}\n")
        f.write(")\n")

    def _target_compile_definitions(self, f: TextIO, target: str, ext: Extension) -> None:
        f.write(f"target_compile_definitions({target} PRIVATE\n")
        for define in ext.define_macros:
            f.write(f"    {define[0]}={define[1]}\n")
        f.write(")\n")

    def _add_library(self, target: str, ext: Extension) -> None:
        with self.path.open("a") as f:
            f.write(f"# shared library: {ext.name}\n")
            f.write(f"add_library({target} MODULE\n")
            for source in ext.sources:
                f.write(f"    {Path(source).absolute()}\n")
            f.write(")\n")
            f.write(f"set_target_properties({target} PROPERTIES\n")
            f.write( "    PREFIX \"\"\n")
            f.write(f"    OUTPUT_NAME {target}\n")
            f.write( "    SUFFIX \"\"\n")
            f.write(f"    CXX_STANDARD {ext.cxx_std}\n")
            f.write( "    CXX_STANDARD_REQUIRED ON\n")
            for key, value in ext.extra_cmake_args.items():
                f.write(f"    {key} {value}\n")
            f.write(")\n")
            if ext.modules:
                self._target_sources(f, target, ext)
            if ext.include_dirs:
                self._target_include_directories(f, target, ext)
            if ext.library_dirs:
                self._target_link_directories(f, target, ext)
            if ext.libraries:
                self._target_link_libraries(f, target, ext)
            if ext.extra_compile_args:
                self._target_compile_options(f, target, ext)
            if ext.extra_link_args:
                self._target_link_options(f, target, ext)
            if ext.define_macros:
                self._target_compile_definitions(f, target, ext)
            f.write("\n")

    def _add_executable(self, target: str, ext: Extension) -> None:
        with self.path.open("a") as f:
            f.write(f"# executable: {ext.name}\n")
            f.write(f"add_executable({target}\n")
            for source in ext.sources:
                f.write(f"    {Path(source).absolute()}\n")
            f.write(")\n")
            f.write(f"set_target_properties({target} PROPERTIES\n")
            f.write( "    PREFIX \"\"\n")
            f.write(f"    OUTPUT_NAME {target}\n")
            f.write( "    SUFFIX \"\"\n")
            f.write(f"    CXX_STANDARD {ext.cxx_std}\n")
            f.write( "    CXX_STANDARD_REQUIRED ON\n")
            for key, value in ext.extra_cmake_args.items():
                f.write(f"    {key} {value}\n")
            f.write(")\n")
            if ext.modules:
                self._target_sources(f, target, ext)
            if ext.include_dirs:
                self._target_include_directories(f, target, ext)
            if ext.library_dirs:
                self._target_link_directories(f, target, ext)
            if ext.libraries:
                self._target_link_libraries(f, target, ext)
            if ext.extra_compile_args:
                self._target_compile_options(f, target, ext)
            if ext.extra_link_args:
                self._target_link_options(f, target, ext)
            if ext.define_macros:
                self._target_compile_definitions(f, target, ext)
            f.write("\n")

    def add(self, ext: Extension) -> None:
        """Add an extension to the CMakeLists.txt file.

        Parameters
        ----------
        ext : Extension
            An extension describing the library and/or executable to add.
        """
        self._add_library(f"{ext.name}{sysconfig.get_config_var('EXT_SUFFIX')}", ext)
        if ext.executable:
            self._add_executable(ext.name, ext)



    # def __init__(
    #     self,
    #     build_dir: Path,
    #     extensions: list[Extension],
    #     project_name: str,
    #     debug: bool,
    #     include_dirs: list[str],
    #     library_dirs: list[str],
    #     libraries: list[str],
    # ) -> None:
    #     self.build_dir = build_dir
    #     self.path = build_dir / "CMakeLists.txt"
    #     self.extensions = extensions
    #     self.project_name = project_name
    #     self.debug = debug
    #     self.include_dirs = include_dirs
    #     self.library_dirs = library_dirs
    #     self.libraries = libraries

    #     self.source_map: dict[Path, list[Extension]] = {}
    #     for ext in extensions:
    #         for source in ext.sources:
    #             self.source_map.setdefault(Path(source), []).append(ext)

    #     self.module_interfaces: dict[Extension, Path] = {}
    #     self.executables: dict[Extension, Path] = {}

    # def _header(self) -> str:
    #     build_type = "Debug" if self.debug else "Release"
    #     toolchain = (
    #         self.build_dir / "build" / build_type / "generators" /
    #         "conan_toolchain.cmake"
    #     )
    #     libraries = self.libraries + [p.link for p in env.packages]

    #     result = f"# CMakeLists.txt automatically generated by bertrand {__version__}\n"
    #     result += f"cmake_minimum_required(VERSION {self.MIN_VERSION})\n"
    #     result += f"project({self.project} LANGUAGES CXX)\n"
    #     result += "\n"
    #     result += "# global config\n"
    #     result += f"set(CMAKE_BUILD_TYPE {build_type})\n"
    #     result += f"set(PYTHON_EXECUTABLE {sys.executable})\n"
    #     result += "set(CMAKE_COLOR_DIAGNOSTICS ON)\n"
    #     result += "set(CMAKE_CXX_SCAN_FOR_MODULES ON)\n"
    #     result += "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n"
    #     result += "\n"
    #     result += "# package management\n"
    #     result += f"include({toolchain})\n"
    #     result += "\n".join(f'find_package({p.find} REQUIRED)' for p in env.packages)
    #     result += "\n"
    #     if self.include_dirs:
    #         result += "include_directories(\n"
    #         for include in self.include_dirs:
    #             result += f"    {include}\n"
    #         result += ")\n"
    #     if self.library_dirs:
    #         result += "link_directories(\n"
    #         for lib_dir in self.library_dirs:
    #             result += f"    {lib_dir}\n"
    #         result += ")\n"
    #     if libraries:
    #         result += "link_libraries(\n"
    #         for lib in libraries:
    #             result += f"    {lib}\n"
    #         result += ")\n"
    #     result += "\n"
    #     return result

    # def _library(self, ext: Extension) -> str:
    #     target = f"{ext.name}{sysconfig.get_config_var('EXT_SUFFIX')}"

    #     result = f"# shared library: {ext.name}\n"
    #     result += f"add_library({target} MODULE\n"
    #     for source in ext.sources:
    #         result += f"    {Path(source).absolute()}\n"
    #     result += ")\n"
    #     result += f"set_target_properties({target} PROPERTIES\n"
    #     result +=  "    PREFIX \"\"\n"
    #     result += f"    OUTPUT_NAME {target}\n"
    #     result +=  "    SUFFIX \"\"\n"
    #     result += f"    CXX_STANDARD {ext.cxx_std}\n"
    #     result +=  "    CXX_STANDARD_REQUIRED ON\n"
    #     for key, value in ext.extra_cmake_args.items():
    #         result += f"    {key} {value}\n"
    #     result += ")\n"
    #     if ext.modules:
    #         result += f"target_sources({target} PRIVATE\n"
    #         result +=  "    FILE_SET CXX_MODULES\n"
    #         result += f"    BASE_DIRS {Path.cwd()}\n"
    #         result +=  "    FILES\n"
    #         for source in ext.modules:
    #             result += f"        {Path(source).absolute()}\n"
    #         result += ")\n"
    #     if ext.include_dirs:
    #         result += f"target_include_directories({target} PRIVATE\n"
    #         for include in ext.include_dirs:
    #             result += f"    {include}\n"
    #         result += ")\n"
    #     if ext.library_dirs:
    #         result += f"target_link_directories({target} PRIVATE\n"
    #         for lib_dir in ext.library_dirs:
    #             result += f"    {lib_dir}\n"
    #         result += ")\n"
    #     if ext.libraries:
    #         result += f"target_link_libraries({target} PRIVATE\n"
    #         for lib in ext.libraries:
    #             result += f"    {lib}\n"
    #         result += ")\n"
    #     if ext.extra_compile_args:
    #         result += f"target_compile_options({target} PRIVATE\n"
    #         for flag in ext.extra_compile_args:
    #             result += f"    {flag}\n"
    #         result += ")\n"
    #     if ext.extra_link_args:
    #         result += f"target_link_options({target} PRIVATE\n"
    #         for flag in ext.extra_link_args:
    #             result += f"    {flag}\n"
    #         result += ")\n"
    #     if ext.define_macros:
    #         result += f"target_compile_definitions({target} PRIVATE\n"
    #         for define in ext.define_macros:
    #             result += f"    {define[0]}={define[1]}\n"
    #         result += ")\n"
    #     result += "\n"
    #     return result

    # def _executable(self, ext: Extension) -> str:
    #     result = f"# executable: {ext.name}\n"
    #     result += f"add_executable({ext.name}\n"
    #     for source in ext.sources:
    #         result += f"    {Path(source).absolute()}\n"
    #     result += ")\n"
    #     result += f"set_target_properties({ext.name} PROPERTIES\n"
    #     result +=  "    PREFIX \"\"\n"
    #     result += f"    OUTPUT_NAME {ext.name}\n"
    #     result +=  "    SUFFIX \"\"\n"
    #     result += f"    CXX_STANDARD {ext.cxx_std}\n"
    #     result +=  "    CXX_STANDARD_REQUIRED ON\n"
    #     for key, value in ext.extra_cmake_args.items():
    #         result += f"    {key} {value}\n"
    #     result += ")\n"
    #     if ext.modules:
    #         result += f"target_sources({ext.name} PRIVATE\n"
    #         result +=  "    FILE_SET CXX_MODULES\n"
    #         result += f"    BASE_DIRS {Path.cwd()}\n"
    #         result +=  "    FILES\n"
    #         for source in ext.modules:
    #             result += f"        {Path(source).absolute()}\n"
    #         result += ")\n"
    #     if ext.include_dirs:
    #         result += f"target_include_directories({ext.name} PRIVATE\n"
    #         for include in ext.include_dirs:
    #             result += f"    {include}\n"
    #         result += ")\n"
    #     if ext.library_dirs:
    #         result += f"target_link_directories({ext.name} PRIVATE\n"
    #         for lib_dir in ext.library_dirs:
    #             result += f"    {lib_dir}\n"
    #         result += ")\n"
    #     if ext.libraries:
    #         result += f"target_link_libraries({ext.name} PRIVATE\n"
    #         for lib in ext.libraries:
    #             result += f"    {lib}\n"
    #         result += ")\n"
    #     if ext.extra_compile_args:
    #         result += f"target_compile_options({ext.name} PRIVATE\n"
    #         for flag in ext.extra_compile_args:
    #             result += f"    {flag}\n"
    #         result += ")\n"
    #     if ext.extra_link_args:
    #         result += f"target_link_options({ext.name} PRIVATE\n"
    #         for flag in ext.extra_link_args:
    #             result += f"    {flag}\n"
    #         result += ")\n"
    #     if ext.define_macros:
    #         result += f"target_compile_definitions({ext.name} PRIVATE\n"
    #         for define in ext.define_macros:
    #             result += f"    {define[0]}={define[1]}\n"
    #         result += ")\n"
    #     result += "\n"
    #     return result

    # def write(self) -> CMakeLists:
    #     """Write the CMakeLists.txt file to the build directory with its current
    #     configuration.

    #     Returns
    #     -------
    #     CMakeLists
    #         A reference to the current CMakeLists object, for chaining.
    #     """
    #     self.path.parent.mkdir(parents=True, exist_ok=True)
    #     with self.path.open("w") as f:
    #         f.write(self._header())
    #         for ext in self.extensions:
    #             f.write(self._library(ext))
    #             if ext.executable:
    #                 f.write(self._executable(ext))
    #     return self

    def bind(self) -> CMakeLists:
        """Generate cross-language bindings for the C++ extensions and write them to
        the CMakeLists.txt file.

        Returns
        -------
        CMakeLists
            A reference to the current CMakeLists object, for chaining.
        """
        # preconfigure to emit preliminary compile_commands.json
        subprocess.check_call(
            [
                "cmake",
                "-G",
                "Ninja",
                str(self.path.parent),
            ],
            cwd=self.path.parent,
            stdout=subprocess.PIPE,  # NOTE: silences noisy CMake/Conan output
        )

        # filter out any lazily-evaluated cmake arguments from compile_commands.json
        # that are not supported by clang-scan-deps
        compile_commands = self.path.parent / "compile_commands.json"
        p1689_commands = self.path.parent / "p1689_commands.json"
        with compile_commands.open("r") as infile:
            with p1689_commands.open("w") as outfile:
                filtered = [
                    {
                        "directory": cmd["directory"],
                        "command": " ".join(
                            c for c in cmd["command"].split() if not c.startswith("@")
                        ),
                        "file": cmd["file"],
                        "output": cmd["output"],
                    }
                    for cmd in json.load(infile)
                ]
                json.dump(filtered, outfile, indent=4)

        # invoke clang-scan-deps to generate the p1689 dependency graph
        p1689 = json.loads(subprocess.run(
            [
                str(env / "bin" / "clang-scan-deps"),
                "-format=p1689",
                "-compilation-database",
                str(p1689_commands),
            ],
            check=True,
            capture_output=True,
        ).stdout.decode("utf-8").strip())

        # TODO: iterate through the p1689 graph and update self.module_interfaces
        # accordingly.  If any duplicates are found or the length after parsing does
        # not exactly match the length of self.extensions, fail the build with an error
        # message + sys.exit().
        # -> matching lengths means that every module is associated with exactly one
        # module interface unit.  It might be better to allow some extensions to not
        # have a module interface unit if they are pure executables.

        # analyze the p1689 dependency graph to extract module interface units and
        # ensure a 0:1 or 1:1 correspondence with Extension declarations
        for module in p1689["rules"]:
            for source in module.get("provides", []):
                path = Path(source["source-path"])
                for ext in self.source_map[path]:
                    if ext in self.module_interfaces:
                        print(
                            f"Multiple primary module interfaces found for "
                            f"'{ext.name}':\n"
                            f"    {self.module_interfaces[ext]}\n"
                            f"    {path}\n"
                            f"\n"
                            f"Please ensure that each extension has at most one "
                            f"top-level `export module` declaration amongst its "
                            f"sources (disregarding module partition units)."
                        )
                        sys.exit(1)
                    else:
                        self.module_interfaces[ext] = path

        # TODO: ensure each extension is associated with at most one executable source
        # -> This might be able to be lifted out of this method and into the
        # constructor?  That would populate the self.executables dictionary for us.
        # -> after creating source_map, iterate through it and check each key for
        # a main() function.  When one is found, push all of the values into the
        # executables dictionary and fail the build if there are any duplicates.

        # TODO: assuming above is done, merge the keys of self.module_interfaces and
        # self.executables into a single set.  If the length of the set does not match
        # the length of self.extensions, then there are some extensions that do not
        # have either a python module to export or an executable to build, and
        # therefore should not be included.  The user should refactor the extensions
        # in that case, and the build should fail with an error message + sys.exit().
        # -> Perhaps in the future, this should be allowed.  I just don't know atm.







        # extract exported C++ modules and generate equivalent Python bindings
        cpp_modules: dict[str, Path] = {}
        python_modules: dict[str, Path] = {}
        for module in p1689["rules"]:
            for source in module.get("provides", []):
                name = source["logical-name"]
                path = Path(source["source-path"])
                cpp_modules[name] = path
                # python_modules[name] = self.emit_python(path)

        # fill in unresolved C++ imports with Python modules
        for module in p1689["rules"]:
            for source in module.get("requires", []):
                name = source["logical-name"]
                if name not in cpp_modules:
                    destination = compile_commands.parent / "bindings" / f"{name}.cpp"
                    module = PyModule(destination)
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    with destination.open("w") as file:
                        file.write(module.generate())
                    cpp_modules[name] = destination

        # insert generated modules into CMakeLists.txt as additional sources
        print()
        print("exported to C++:")
        print("\n".join(f"{k}: {v}" for k, v in cpp_modules.items()))
        print()
        print("exported to Python:")
        print("\n".join(f"{k}: {v}" for k, v in python_modules.items()))
        print()


        return self



    # TODO: break this up into a builder pattern.
    #   cmake = CmakeLists(
    #       build_lib,
    #       self.extensions
    #       project,
    #       debug,
    #       include_dirs,
    #       library_dirs,
    #       libraries
    #   )
    #   cmake.write().bind().write().install(workers)

    def install(self, workers: int) -> None:
        """Invoke CMake to build the project.

        Parameters
        ----------
        workers : int
            The number of parallel workers to use when building the project.
        """
        # preconfigure to emit preliminary compile_commands.json
        subprocess.check_call(
            [
                str(env / "bin" / "cmake"),
                "-G",
                "Ninja",
                str(self.path.parent),
            ],
            cwd=self.path.parent,
            stdout=subprocess.PIPE,  # NOTE: silences noisy CMake/Conan output
        )

        # intercept compile_commands.json and filter out any lazily-evaluated cmake
        # arguments that are not supported by clang-scan-deps
        compile_commands = self.path.parent / "compile_commands.json"
        p1689_commands = compile_commands.parent / "p1689_commands.json"
        with compile_commands.open("r") as infile:
            with p1689_commands.open("w") as outfile:
                filtered = [
                    {
                        "directory": cmd["directory"],
                        "command": " ".join(
                            c for c in cmd["command"].split() if not c.startswith("@")
                        ),
                        "file": cmd["file"],
                        "output": cmd["output"],
                    }
                    for cmd in json.load(infile)
                ]
                json.dump(filtered, outfile, indent=4)

        # invoke clang-scan-deps to generate the p1689 dependency graph
        p1689 = json.loads(subprocess.run(
            [
                str(env / "bin" / "clang-scan-deps"),
                "-format=p1689",
                "-compilation-database",
                str(p1689_commands),
            ],
            check=True,
            capture_output=True,
        ).stdout.decode("utf-8").strip())

        # extract exported C++ modules and generate equivalent Python bindings
        cpp_modules: dict[str, Path] = {}
        python_modules: dict[str, Path] = {}
        for module in p1689["rules"]:
            for source in module.get("provides", []):
                name = source["logical-name"]
                path = Path(source["source-path"])
                cpp_modules[name] = path
                # python_modules[name] = self.emit_python(path)

        # fill in unresolved C++ imports with Python modules
        for module in p1689["rules"]:
            for source in module.get("requires", []):
                name = source["logical-name"]
                if name not in cpp_modules:
                    destination = compile_commands.parent / "bindings" / f"{name}.cpp"
                    module = PyModule(destination)
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    with destination.open("w") as file:
                        file.write(module.generate())
                    cpp_modules[name] = destination

        # insert generated modules into CMakeLists.txt as additional sources
        print()
        print("exported to C++:")
        print("\n".join(f"{k}: {v}" for k, v in cpp_modules.items()))
        print()
        print("exported to Python:")
        print("\n".join(f"{k}: {v}" for k, v in python_modules.items()))
        print()

        # TODO: it's possible to build a dependency graph by inserting
        # --graphviz=filepath into this second configure step.  This could be useful
        # for debugging or for visualizing the build process.  It could generate some
        # wicked art, too.

        # reconfigure to use the updated modules
        subprocess.check_call(
            [
                str(env / "bin" / "cmake"),
                "-G",
                "Ninja",
                str(self.path.parent),
            ],
            cwd=self.path.parent,
            stdout=subprocess.PIPE,
        )

        # build the extensions using CMake
        try:
            build_args = [
                str(env / "bin" / "cmake"),
                "--build",
                ".",
                "--config",
                "Debug" if self.debug else "Release",
            ]
            if workers:
                build_args += ["--parallel", str(workers)]
            subprocess.check_call(build_args, cwd=self.path.parent)
        except subprocess.CalledProcessError:
            sys.exit()  # errors are already printed to the console

        # export the compile_commands.json file so that it can be used by clangd, etc.
        shutil.copy2(self.path.parent / "compile_commands.json", "compile_commands.json")


class BuildExt(pybind11_build_ext):
    """A custom build_ext command that uses CMake to build extensions with support for
    C++20 modules, parallel builds, clangd, executable targets, and bertrand's core
    dependencies without any extra configuration.
    """

    user_options = pybind11_build_ext.user_options + [
        (
            "workers=",
            "j",
            "The number of parallel workers to use when building CMake extensions"
        )
    ]

    def __init__(self, *args: Any, conan: list[Package], workers: int, **kwargs: Any) -> None:
        breakpoint()
        super().__init__(*args, **kwargs)
        self.conan = conan
        self.workers = workers

    def finalize_options(self) -> None:
        """Parse command-line options and convert them to the appropriate types.

        Raises
        ------
        ValueError
            If the workers option is not set to a positive integer or 0.
        """
        super().finalize_options()

        if self.workers:
            self.workers = int(self.workers)
            if self.workers == 0:
                self.workers = os.cpu_count() or 1
            elif self.workers < 0:
                raise ValueError(
                    "workers must be set to a positive integer or 0 to use all cores"
                )

    def build_extensions(self) -> None:
        """Build all extensions in the project.

        Raises
        ------
        RuntimeError
            If setup.py is invoked outside of a bertrand virtual environment.
        TypeError
            If any extensions are not of type bertrand.Extension.
        """
        if self.extensions and "BERTRAND_HOME" not in os.environ:
            raise RuntimeError(
                "setup.py must be run inside a bertrand virtual environment in order "
                "to compile C++ extensions"
            )

        # force the use of the coupled Extension class to describe build targets
        self.check_extensions_list(self.extensions)
        incompabile_extensions = [
            ext for ext in self.extensions if not isinstance(ext, Extension)
        ]
        if incompabile_extensions:
            raise TypeError(
                f"Extensions must be of type bertrand.Extension: "
                f"{incompabile_extensions}"
            )

        # install conan dependencies
        build_lib = Path(self.build_lib)
        if self.conan:
            ConanFile(build_lib, self.conan).install()

        # TODO:
        #   if self.extensions:
        #       CMakeLists(
        #           build_lib,
        #           self.extensions,
        #           self.distribution.get_name(),
        #           self.debug,
        #           self.compiler.include_dirs,
        #           self.compiler.library_dirs,
        #           self.compiler.libraries + [p.link for p in env.packages],
        #       ).write().bind().write().install()

        cmakelists = CMakeLists(
            build_lib=build_lib,
            project=self.distribution.get_name(),
            debug=self.debug,
            include_dirs=self.compiler.include_dirs,
            library_dirs=self.compiler.library_dirs,
            libraries=self.compiler.libraries + [p.link for p in env.packages],
        )
        for ext in self.extensions:
            cmakelists.add(ext)

        cmakelists.install(self.workers)

    def copy_extensions_to_source(self) -> None:
        """Copy executables as well as shared libraries to the source directory if
        setup.py was invoked with the --inplace option.
        """
        for ext in self.extensions:
            lib_path = Path(self.build_lib) / f"{ext.name}{sysconfig.get_config_var('EXT_SUFFIX')}"
            exe_path = Path(self.build_lib) / ext.name
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
    *args: Any,
    cmdclass: dict[str, Any] | None = None,
    cpp_deps: Iterable[PackageLike] | None = None,
    workers: int = 0,
    **kwargs: Any
) -> None:
    """A custom setup() function that automatically appends the BuildExt command to the
    setup commands.

    Parameters
    ----------
    *args : Any
        Arbitrary positional arguments passed to the setuptools.setup() function.
    cmdclass : dict[str, Any] | None, default None
        A dictionary of command classes to override the default setuptools commands.
        If no setting is given for "build_ext", then it will be set to
        bertrand.setuptools.BuildExt.
    cpp_deps : Iterable[PackageLike] | None, default None
        A list of C++ dependencies to install before building the extensions.  Each
        dependency should be specified as a string of the form
        `{name}/{version}@{find_package}/{target_link_libraries}`, where the
        `find_package` and `target_link_libraries` symbols are passed to the CMake
        commands of the same name.  These identifiers can typically be found by running
        `conan search ${package_name}` or by browsing conan.io.
    workers : int, default 0
        The number of parallel workers to use when building extensions.  If set to
        zero, then the build will use all available cores.  This can also be set
        through the command line by supplying either `--workers=n` or `-j n`.
    **kwargs : Any
        Arbitrary keyword arguments passed to the setuptools.setup() function.
    """
    deps: list[Package] = []
    if env:
        deps.extend(env.packages)

    for p in cpp_deps or []:
        package = Package(p, allow_shorthand=False)
        if package not in deps:
            deps.append(package)

    # deps = [Package(p, allow_shorthand=False) for p in conan or []]
    class _BuildExtWrapper(BuildExt):
        def __init__(self, *a: Any, **kw: Any):
            super().__init__(*a, conan=deps, workers=workers, **kw)

    if cmdclass is None:
        cmdclass = {"build_ext": _BuildExtWrapper}
    elif "build_ext" not in cmdclass:
        cmdclass["build_ext"] = _BuildExtWrapper
    else:
        cmd: type = cmdclass["build_ext"]
        if issubclass(cmd, BuildExt):
            class _BuildExtSubclassWrapper(cmd):
                def __init__(self, *a: Any, **kw: Any):
                    super().__init__(*a, conan=deps, workers=workers, **kw)
            cmdclass["build_ext"] = _BuildExtSubclassWrapper

    setuptools.setup(*args, cmdclass=cmdclass, **kwargs)




