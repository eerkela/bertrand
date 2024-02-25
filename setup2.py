from pathlib import Path
import os
import subprocess
import sys

from setuptools import setup
from pybind11.setup_helpers import Pybind11Extension, build_ext

import bertrand


class BuildExt(build_ext):
    """A custom build_ext command that installs third-party C++ packages and builds C++
    unit tests as part of pip install.
    """

    def extract(self, cwd: Path, tarball: str) -> None:
        """Extract a tarball in the current working directory.

        Parameters
        ----------
        cwd : Path
            A path to the directory where the tarball is located.
        tarball : str
            The name of the tarball to extract.
        """
        try:
            subprocess.check_call(["tar", "-xzf", tarball], cwd=str(cwd))
        except subprocess.CalledProcessError as exc:
            print(f"failed to extract {tarball}:", exc)
            sys.exit(1)

    def install_pcre2(self, cwd: Path) -> None:
        """Build PCRE2 from source after extracting it.

        Parameters
        ----------
        cwd : Path
            A path to the directory where the tarball was extracted.
        """
        pcre_dir = "pcre2-10.43"
        if (cwd / pcre_dir).exists():
            cwd = cwd / pcre_dir
        else:
            self.extract(cwd, f"{pcre_dir}.tar.gz")
            cwd = cwd / pcre_dir
            cwd.mkdir(parents=True, exist_ok=True)

            try:
                subprocess.check_call(["./configure", "--enable-jit=auto"], cwd=str(cwd))
                subprocess.check_call(["make"], cwd=str(cwd))
            except subprocess.CalledProcessError as exc:
                print("failed to build PCRE2:", exc)
                sys.exit(1)

        # add headers to include path and link against binary
        self.include_dirs.append(str(cwd / "src"))
        self.library_dirs.append(str(cwd / ".libs"))
        self.libraries.append("pcre2-8")

    def install_gtest(self, cwd: Path) -> None:
        """Build GoogleTest from source after extracting it.

        Parameters
        ----------
        cwd : Path
            A path to the directory where the tarball was extracted.
        """
        gtest_dir = "googletest-1.14.0"
        if (cwd / gtest_dir).exists():
            cwd = cwd / gtest_dir
        else:
            self.extract(cwd, f"{gtest_dir}.tar.gz")
            cwd = cwd / gtest_dir / "build"
            cwd.mkdir(parents=True, exist_ok=True)

            try:
                subprocess.check_call(
                    ["cmake", "..", "-DCMAKE_CXX_FLAGS=-fPIC"],
                    cwd=str(cwd)
                )
                subprocess.check_call(["make"], cwd=str(cwd))
            except subprocess.CalledProcessError as exc:
                print("failed to build GoogleTest:", exc)
                sys.exit(1)

            cwd = cwd.parent  # back out to root dir

        # add headers to include path and link against binary
        self.include_dirs.append(str(cwd / "googletest/include"))
        self.library_dirs.append(str(cwd / "build/lib"))
        self.libraries.append("gtest")

    def run(self) -> None:
        """Build third-party libraries from source before installing any extensions."""
        try: # check for cmake
            subprocess.check_call(
                ["cmake", "--version"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL
            )
        except subprocess.CalledProcessError:
            print("CMake not installed")
            sys.exit(1)

        # install third-party C++ packages
        cwd = Path(os.getcwd()) / "third_party"
        self.install_pcre2(cwd)
        self.install_gtest(cwd)

        # compile Python extensions
        super().run()

        # save compiler flags to test/ so that we can use the same configuration when
        # building the test suite
        cwd = cwd.parent / "test"
        with (cwd / ".compile_flags").open("w") as file:
            file.write(" ".join(
                self.compiler.compiler_so[1:] +  # remove the compiler name
                ["-fvisibility=hidden", "-g0"]  # these are added by Pybind11Extension
            ))


ext_modules = [
    Pybind11Extension(
        "example",
        ["example.cpp"],
    ),
]

setup(
    ext_modules=ext_modules,
    cmdclass={"build_ext": BuildExt},
    include_dirs=[bertrand.get_include()],
)
