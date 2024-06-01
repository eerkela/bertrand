"""Build tools for bertrand-enabled C++ extensions.
"""
# pylint: disable=unused-argument
import json
import os
import re
import subprocess
import sys
import sysconfig
from pathlib import Path
from typing import Any

import numpy
import pybind11
from pybind11.setup_helpers import Pybind11Extension
from pybind11.setup_helpers import build_ext as pybind11_build_ext


# TODO: list cpptrace, pcre2, googletest version #s explicitly here?

ROOT: Path = Path(__file__).absolute().parent.parent
DEPS: Path = ROOT / "third_party"


def get_include() -> str:
    """Get the path to the include directory for this package, which is necessary to
    make C++ headers available to the compiler.

    Returns
    -------
    str
        The path to the include directory for this package.
    """
    return str(ROOT)


def quick_include() -> list[str]:
    """Return the complete include and link flags necessary to build a pure-C++ project
    with bertrand as a dependency.

    Returns
    -------
    list
        A list of strings containing the various include and link libraries needed to
        build a bertrand-enabled project from the command line, as a single unit.
    """
    cpptrace = DEPS / "cpptrace-0.5.2"
    gtest = DEPS / "googletest-1.14.0"
    pcre2 = DEPS / "pcre2-10.43"
    return [
        f"-I{sysconfig.get_path('include')}",
        f"-I{get_include()}",
        f"-I{pybind11.get_include()}",
        f"-I{numpy.get_include()}",
        f"-I{str(cpptrace / 'include')}",
        f"-I{str(gtest / 'googletest' / 'include')}",
        f"-I{str(pcre2 / 'src')}",
        f"-L{str(cpptrace / 'build')}",
        f"-L{str(cpptrace / 'build' / '_deps' / 'libdwarf-build' / 'src' / 'lib' / 'libdwarf')}",
        f"-L{str(cpptrace / 'build' / '_deps' / 'zstd-build' / 'lib')}",
        f"-L{str(gtest / 'build' / 'lib')}",
        f"-L{str(pcre2 / '.libs')}",
        f"-lpython{sysconfig.get_python_version()}",
        "-lcpptrace",
        "-ldwarf",
        "-lz",
        "-lzstd",
        "-ldl",
        "-lgtest",
    ]


class Extension(Pybind11Extension):
    """A setuptools.Extension class that builds using CMake and supports C++20 modules.

    Parameters
    ----------
    *args, **kwargs : Any
        Arbitrary arguments passed to the Pybind11Extension constructor.
    cxx_std : int, default 20
        The C++ standard to use when compiling the extension.  Values less than 20 will
        raise a ValueError.
    clangd : bool, default True
        If true, include the source file in a compile_commands.json file that is
        emitted for use with clangd and related tools, which provide code completion
        and other features.
    """

    CMAKE_MIN_VERSION = "3.28" # CMake 3.28+ is necessary for C++20 module support
    MODULE_REGEX = re.compile(r"\s*export\s+module\s+(\w+)", re.MULTILINE)

    def __init__(
        self,
        *args: Any,
        cxx_std: int = 23,
        clangd: bool = True,
        traceback: bool = True,
        cmake_args: dict[str, Any] | None = None,
        **kwargs: Any
    ) -> None:
        if cxx_std < 23:
            raise ValueError(
                "C++ standard must be at least C++23 to enable bertrand features"
            )

        super().__init__(*args, **kwargs)
        self.cxx_std = cxx_std
        self.clangd = clangd
        self.include_dirs.append(get_include())
        self.include_dirs.append(numpy.get_include())
        self.cmake_args = cmake_args or {}
        if traceback:
            self.extra_compile_args.append("-g")
            self.extra_link_args.append("-g")
        else:
            self.extra_compile_args.append("-DBERTRAND_NO_TRACEBACK")
            self.extra_link_args.append("-DBERTRAND_NO_TRACEBACK")

    def exports_module(self, source: Path) -> bool:
        """Checks whether the given source file exports a module by searching for an
        `export module` declaration.

        Parameters
        ----------
        source : Path
            The path to the source file to check.

        Returns
        -------
        bool
            True if the source file exports a module, False otherwise.
        """
        with source.open("r") as file:
            return bool(self.MODULE_REGEX.search(file.read()))

    def build_cmakelists(
        self,
        file: Path,
        debug: bool,
        include_dirs: list[str],
        library_dirs: list[str],
        libraries: list[str],
    ) -> None:
        """Generate a temporary CMakeLists.txt that configures the extension for
        building with CMake.

        Parameters
        ----------
        file : Path
            The path to the generated CMakeLists.txt file.
        debug : bool
            If true, set --config=Debug when invoking CMake.  Otherwise, use Release.
        include_dirs : list[str]
            Additional include directories to pass to the compiler.  These are set in
            the build_ext command and must be passed to CMake as well.
        library_dirs : list[str]
            Additional library directories to pass to the linker.  These are set in the
            build_ext command and must be passed to CMake as well.
        libraries : list[str]
            Additional libraries to link against.  These are set in the build_ext
            command and must be passed to CMake as well.
        """
        file.touch(exist_ok=True)
        with file.open("w") as f:
            f.write(f"cmake_minimum_required(VERSION {self.CMAKE_MIN_VERSION})\n")
            f.write(f"project({self.name} LANGUAGES CXX)\n")
            f.write(f"set(CMAKE_CXX_STANDARD {self.cxx_std})\n")
            f.write("set(CMAKE_CXX_STANDARD_REQUIRED ON)\n")
            f.write(f"set(CMAKE_LIBRARY_OUTPUT_DIRECTORY {file.parent})\n")
            f.write(f"set(CMAKE_BUILD_TYPE {'Debug' if debug else 'Release'})\n")
            f.write(f"set(PYTHON_EXECUTABLE {sys.executable})\n")
            f.write("set(CMAKE_CXX_EXTENSIONS OFF)\n")
            f.write("set(CMAKE_CXX_SCAN_FOR_MODULES ON)\n")
            for key, value in self.cmake_args.items():
                f.write(f"set({key} {value})\n")

            f.write(f"add_library({self.name} MODULE\n")
            for source in self.sources:
                f.write(f"    {ROOT / source}\n")
            f.write(")\n")

            f.write(f"set_target_properties({self.name} PROPERTIES\n")
            f.write( "    PREFIX \"\"\n")
            f.write(f"    LIBRARY_OUTPUT_NAME {self.name}\n")
            f.write(f"    SUFFIX {sysconfig.get_config_var('EXT_SUFFIX')}\n")
            f.write(")\n")

            _include_dirs = include_dirs + self.include_dirs
            if _include_dirs:
                f.write(f"target_include_directories({self.name} PRIVATE\n")
                for include in _include_dirs:
                    f.write(f"    {include}\n")
                f.write(")\n")

            _lib_dirs = library_dirs + self.library_dirs
            if _lib_dirs:
                f.write(f"target_link_directories({self.name} PRIVATE\n")
                for lib_dir in _lib_dirs:
                    f.write(f"    {lib_dir}\n")
                f.write(")\n")

            _libraries = libraries + self.libraries
            if _libraries:
                f.write(f"target_link_libraries({self.name} PRIVATE\n")
                for lib in _libraries:
                    f.write(f"    {lib}\n")
                f.write(")\n")

            if self.extra_compile_args:
                f.write(f"target_compile_options({self.name} PRIVATE\n")
                for flag in self.extra_compile_args:
                    f.write(f"    {flag}\n")
                f.write(")\n")

            _link_options = [sysconfig.get_config_var("LDFLAGS")] + self.extra_link_args
            if _link_options:
                f.write(f"target_link_options({self.name} PRIVATE\n")
                for flag in _link_options:
                    f.write(f"    {flag}\n")
                f.write(")\n")

            if self.define_macros:
                f.write(f"target_compile_definitions({self.name} PRIVATE\n")
                for define in self.define_macros:
                    if isinstance(define, tuple):
                        f.write(f"    {define[0]}={define[1]}\n")
                    else:
                        f.write(f"    {define}\n")
                f.write(")\n")

            f.write(
                f"target_sources({self.name} PRIVATE FILE_SET CXX_MODULES "
                f"BASE_DIRS {ROOT} FILES\n"
            )
            for source in self.sources:
                if self.exports_module(Path(ROOT / source)):
                    f.write(f"    {ROOT / source}\n")
            f.write(")\n")

    def build(
        self,
        build_dir: Path,
        debug: bool,
        include_dirs: list[str],
        library_dirs: list[str],
        libraries: list[str],
    ) -> None:
        """Build the extension in the given directory, using CMake as the backend.

        Parameters
        ----------
        build_dir : Path
            The directory in which to build the extension.
        debug : bool
            If true, set --config=Debug when invoking CMake.  Otherwise, use Release.
        include_dirs : list[str]
            Additional include directories to pass to the compiler.  These are set in
            the build_ext command and must be passed to CMake as well.
        library_dirs : list[str]
            Additional library directories to pass to the linker.  These are set in the
            build_ext command and must be passed to CMake as well.
        libraries : list[str]
            Additional libraries to link against.  These are set in the build_ext
            command and must be passed to CMake as well.
        """
        build_dir.mkdir(parents=True, exist_ok=True)
        cmakelists = build_dir / "CMakeLists.txt"
        self.build_cmakelists(
            cmakelists,
            debug,
            include_dirs,
            library_dirs,
            libraries,
        )

        try:
            subprocess.check_call(
                ["cmake", "-G", "Ninja", str(build_dir)],
                cwd=build_dir,
            )
            subprocess.check_call(
                ["cmake", "--build", ".", "--config", "Debug" if debug else "Release"],
                cwd=build_dir,
            )
        except subprocess.CalledProcessError as e:
            print(e.stderr)


class BuildExt(pybind11_build_ext):
    """A custom build_ext command that uses CMake to build the extensions with C++20
    module support and optionally emits a compile_commands.json file for use with
    clangd and related tools.
    """

    def build_extensions(self) -> None:
        """Build all extensions in the project."""
        # super().build_extensions()  # TODO: break in case of emergency
        self.check_extensions_list(self.extensions)
        cwd = Path(os.getcwd()).absolute()

        for ext in self.extensions:
            if isinstance(ext, Extension):
                ext.build(
                    (ROOT / self.get_ext_fullpath(ext.name)).parent,
                    self.debug,
                    self.compiler.include_dirs,
                    self.compiler.library_dirs,
                    self.compiler.libraries,
                )
            else:
                super().build_extension(ext)

        self.build_compile_commands(cwd)

    def build_compile_commands(self, cwd: Path) -> None:
        """For each Extension that defines `clangd=True`, emit the command that was
        used to build it into a compile_commands.json file, which is discoverable by
        clangd and related tools.
        
        Parameters
        ----------
        cwd : Path
            The current working directory in which to place the generated file.  This
            is usually the same directory as the build script, which is assumed to be
            the root of the project's source tree.
        """
        commands = []
        for ext in self.extensions:
            if isinstance(ext, Extension) and not ext.clangd:
                continue

            for source in ext.sources:
                include_dirs = self.compiler.include_dirs + ext.include_dirs
                library_dirs = self.library_dirs + ext.library_dirs
                libraries = self.libraries + ext.libraries
                link_options = [sysconfig.get_config_var("LDFLAGS")] + ext.extra_link_args
                commands.append({
                    "directory": str(cwd),
                    "file": source,
                    "command": " ".join(
                        self.compiler.compiler_so +
                        ["-I" + header for header in include_dirs] +
                        ["-L" + lib_dir for lib_dir in library_dirs] +
                        ["-l" + lib for lib in libraries] +
                        [
                            f"-D{define[0]}={define[1]}" if isinstance(define, tuple)
                            else f"-D{define}"
                            for define in ext.define_macros
                        ] +
                        ext.extra_compile_args +
                        link_options +
                        ["-DLINTER"]
                    ),
                })

        if commands:
            with (cwd / "compile_commands.json").open("w") as file:
                json.dump(commands, file, indent=4)
