"""Setup script for Bertrand."""
from pathlib import Path
import os
import subprocess
import sys

import numpy
from Cython.Build import cythonize  # type: ignore
from setuptools import Extension, setup  # type: ignore
from pybind11.setup_helpers import Pybind11Extension, build_ext


# TODO: Should distribute with pcre packed into a tarball excluded from git repo,
# and then unpack and build it during the build process.



TRUTHY = {
    "1", "true", "t", "yes", "y", "ok", "sure", "yep", "yap", "yup", "yeah", "indeed",
    "aye", "roger", "absolutely", "certainly", "definitely", "positively", "positive",
    "affirmative",
}
FALSY = {
    "0", "false", "f", "no", "n", "nah", "nope", "nop", "nay", "never", "negative",
    "negatory", "zip", "zilch", "nada", "nil", "null", "none",
}


# TODO: C++ users have to execute $(python3 -m bertrand -I) to compile against bertrand.h
# c++ foo.cpp -o foo.out $(python3 -m bertrand -I)

# TODO: Python users writing C++ extensions should add
# include_dirs=[bertrand.get_include()] to their Extension objects and/or setup() call.


# NOTE: bertrand users environment variables to control the build process:
#   $ DEBUG=true pip install bertrand
#       build with logging enabled


# NOTE: See setuptools for more info on how extensions are built:
# https://setuptools.pypa.io/en/latest/userguide/ext_modules.html


DEBUG: bool
fdebug = os.environ.get("DEBUG", "0").lower()
if fdebug in TRUTHY:
    DEBUG = True
elif fdebug in FALSY:
    DEBUG = False
else:
    raise ValueError(f"DEBUG={repr(fdebug)} is not a valid boolean value")


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


EXTENSIONS = [
    Pybind11Extension(
        "bertrand.structs.linked",
        sources=["bertrand/structs/linked.cpp"],
        extra_compile_args=["-O3"]
    ),
#     Extension(
#         "*",
#         sources=["bertrand/**/*.pyx"],
#         define_macros=[("NPY_NO_DEPRECATED_API", "NPY_1_7_API_VERSION")],
#     ),
]


def apply_debug_flags(extensions: list[Extension]) -> None:
    """Apply debug flags to extensions if DEBUG=True.

    Parameters
    ----------
    extensions : list[Extension]
        The list of extensions to modify.
    """
    for ext in extensions:
        ext.extra_compile_args.extend(["-g", "-DBERTRAND_DEBUG"])
        ext.extra_link_args.extend(["-g", "-DBERTRAND_DEBUG"])


if DEBUG:
    apply_debug_flags(EXTENSIONS)


setup(
    long_description=Path("README.rst").read_text("utf-8"),
    ext_modules=cythonize(
        EXTENSIONS,
        language_level="3",
        compiler_directives={
            "embedsignature": True,
        },
    ),
    include_dirs=[
        "bertrand/",
        numpy.get_include(),
    ],
    zip_safe=False,  # TODO: maybe true without cython?
    cmdclass={"build_ext": BuildExt},
)
