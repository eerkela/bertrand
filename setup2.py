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


WINDOWS = sys.platform == "win32"


class BuildExt(build_ext):
    """A custom build_ext command that installs PCRE2 alongside any C/C++ extensions.
    """

    def extract_pcre2(self, cwd: Path, pcre_version: str) -> None:
        """Extract the PCRE2 tarball included in the source distribution.

        Parameters
        ----------
        cwd : Path
            A path to the directory where the tarball is located.
        pcre_version : str
            The version of PCRE2 to extract.
        """
        try:
            subprocess.check_call(
                ["tar", "-xzf", f"pcre2-{pcre_version}.tar.gz"],
                cwd=str(cwd)
            )
        except subprocess.CalledProcessError as exc:
            print("failed to extract PCRE2:", exc)
            sys.exit(1)

    def install_pcre2(self, cwd: Path) -> None:
        """Build PCRE2 from source after extracting it.

        Parameters
        ----------
        cwd : Path
            A path to the directory where the tarball was extracted.
        """
        if WINDOWS:
            try:  # check for cmake
                subprocess.check_call(["cmake", "--version"])
            except subprocess.CalledProcessError:
                print("CMake not installed")
                sys.exit(1)

            try:
                cmake_command = [
                    "cmake",
                    "-G",
                    "NMake Makefiles",
                    "..",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DSUPPORT_JIT=ON",
                ]
                try:  # try with JIT enabled, then fall back if we encounter an error
                    subprocess.check_call(cmake_command, cwd=str(cwd))
                except subprocess.CalledProcessError:
                    subprocess.check_call(cmake_command[:-1], cwd=str(cwd))
                subprocess.check_call(["nmake"], cwd=str(cwd))
            except subprocess.CalledProcessError as exc:
                print("failed to build PCRE2 on Windows:", exc)
                sys.exit(1)

        # unix
        else:
            try:
                subprocess.check_call(["./configure", "--enable-jit=auto"], cwd=str(cwd))
                subprocess.check_call(["make"], cwd=str(cwd))
            except subprocess.CalledProcessError as exc:
                print("failed to build PCRE2 on Unix:", exc)
                sys.exit(1)

    def run(self) -> None:
        """Build PCRE2 from source before installing any extensions."""
        cwd = Path(os.getcwd()) / "third_party"
        pcre_version = "10.43"

        if (cwd / f"pcre2-{pcre_version}").exists():
            cwd /= f"pcre2-{pcre_version}"
        else:
            self.extract_pcre2(cwd, pcre_version)
            cwd /= f"pcre2-{pcre_version}"
            self.install_pcre2(cwd)

        self.include_dirs.append(str(cwd / "src"))
        self.library_dirs.append(str(cwd / ".libs"))
        self.libraries.append("pcre2-8")
        super().run()


setup(
    ext_modules=ext_modules,
    cmdclass={"build_ext": BuildExt},
    include_dirs=[bertrand.get_include()]
)
