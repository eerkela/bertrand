"""Build tools for bertrand-enabled C++ extensions.
"""
# pylint: disable=unused-argument
import json
import os
from pathlib import Path
from typing import Any

import numpy
from pybind11.setup_helpers import Pybind11Extension
from pybind11.setup_helpers import build_ext as pybind11_build_ext


def get_include() -> str:
    """Get the path to the include directory for this package, which is necessary to
    make C++ headers available to the compiler.

    Returns
    -------
    str
        The path to the include directory for this package.
    """
    return str(Path(__file__).absolute().parent.parent)


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
        **kwargs: Any
    ) -> None:
        super().__init__(*args, **kwargs)

        if cxx_std >= 20:
            self.cxx_std = cxx_std
        else:
            raise ValueError(
                "C++ standard must be at least C++20 to enable bertrand features"
            )

        self.extra_compile_args.append("-g")
        self.clangd = clangd
        self.include_dirs.append(get_include())
        self.include_dirs.append(numpy.get_include())


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
