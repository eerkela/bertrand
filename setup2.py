from pathlib import Path
import os
import subprocess
import sys

from setuptools import setup
from pybind11.setup_helpers import Pybind11Extension, build_ext

import bertrand

ext_modules = [
    Pybind11Extension(
        "example",
        ["example.cpp"],
    ),
]


class BuildExt(build_ext):
    """A custom build_ext command that installs PCRE2 alongside any C/C++ extensions.
    """

    def run(self) -> None:
        """Build PCRE2 from source before installing any extensions."""

        cwd = Path(os.getcwd()) / "third_party" / "pcre2-10.42"
        # if sys.platform == "win32":  # Windows
        #     try:
        #         subprocess.check_call(["cmake", "--version"])
        #     except subprocess.CalledProcessError as exc:
        #         print("CMake not installed")
        #         sys.exit(1)

        #     try:
        #         subprocess.check_call(
        #             [
        #                 "cmake", "-G", "NMake Makefiles", "..",
        #                 "-DCMAKE_BUILD_TYPE=Release"
        #             ],
        #             cwd=str(cwd)
        #         )
        #         subprocess.check_call(["nmake"], cwd=str(cwd))
        #     except subprocess.CalledProcessError as exc:
        #         print("failed to build PCRE2 on Windows:", exc)
        #         sys.exit(1)

        # else:  # unix
        #     try:
        #         subprocess.check_call(["./configure"], cwd=str(cwd))
        #         subprocess.check_call(["make"], cwd=str(cwd))
        #     except subprocess.CalledProcessError as exc:
        #         print("failed to build PCRE2 on Unix:", exc)
        #         sys.exit(1)

        self.include_dirs.append(str(cwd / "src"))
        self.library_dirs.append(str(cwd / ".libs"))
        self.libraries.append("pcre2-8")
        super().run()


setup(
    ext_modules=ext_modules,
    cmdclass={"build_ext": BuildExt},
    include_dirs=[bertrand.get_include()]
)
