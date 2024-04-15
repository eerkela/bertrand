"""Build tools for bertrand-enabled C++ extensions.
"""
# pylint: disable=unused-argument
import json
import os
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
    """A setuptools.Extension class that automatically includes the necessary
    bertrand/numpy/pybind11 headers and sets the minimum C++ standard to C++20.

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

    def __init__(
        self,
        *args: Any,
        cxx_std: int = 20,
        clangd: bool = True,
        traceback: bool = True,
        **kwargs: Any
    ) -> None:
        if cxx_std < 20:
            raise ValueError(
                "C++ standard must be at least C++20 to enable bertrand features"
            )

        super().__init__(*args, **kwargs)
        self.cxx_std = cxx_std
        self.clangd = clangd
        self.include_dirs.append(get_include())
        self.include_dirs.append(numpy.get_include())
        if traceback:
            self.extra_compile_args.extend(["-g", "-DBERTRAND_TRACEBACK"])
            self.extra_link_args.extend(["-g", "-DBERTRAND_TRACEBACK"])


class BuildExt(pybind11_build_ext):
    """A custom build_ext command that optionally emits a compile_commands.json file
    for use with clangd and related tools.
    """

    def build_compile_commands(self, cwd: Path) -> None:
        """For each Extension that defines `clangd=True`, emit the command that was
        used to build it into a compile_commands.json file, which is discoverable by
        clangd and related tools.
        
        Parameters
        ----------
        cwd : Path
            The current working directory.  This is usually the same directory as the
            build script, which is assumed to be the root of the project's source tree.
            The compile_commands.json file will be emitted in this directory.
        """
        commands = []
        for ext in self.extensions:
            if isinstance(ext, Extension) and not ext.clangd:
                continue

            for source in ext.sources:
                include_dirs = self.compiler.include_dirs + ext.include_dirs
                library_dirs = self.library_dirs + ext.library_dirs
                libraries = self.libraries + ext.libraries
                commands.append({
                    "directory": str(cwd),
                    "command": " ".join(
                        ["-DLINTER"] +
                        self.compiler.compiler_so +
                        ["-I" + header for header in include_dirs] +
                        ["-L" + lib_dir for lib_dir in library_dirs] +
                        ["-l" + lib for lib in libraries] +
                        ext.extra_compile_args
                    ),
                    "file": source,
                })

        if commands:
            with (cwd / "compile_commands.json").open("w") as file:
                json.dump(commands, file, indent=4)

    def run(self, *args: Any, clangd: bool = True, **kwargs: Any) -> None:
        """Compile all extensions and then emit a clangd compile_commands.json file if
        any extensions have `clangd=True`.

        Parameters
        ----------
        *args : Any
            Positional arguments forwarded to `pybind11.build_ext.run()`.
        **kwargs : Any
            Keyword arguments forwarded to `pybind11.build_ext.run()`.
        clangd : bool, default True
            If true, emit a compile_commands.json file for clangd integration.
        """
        super().run()
        if clangd:
            self.build_compile_commands(Path(os.getcwd()).absolute())
