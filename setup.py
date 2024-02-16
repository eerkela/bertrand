"""Setup script for Bertrand."""
from pathlib import Path
import os
import subprocess
import sys

import numpy
from Cython.Build import cythonize  # type: ignore
from setuptools import Extension, setup  # type: ignore
from pybind11.setup_helpers import Pybind11Extension, build_ext


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


class BuildExt(build_ext):
    """A custom build_ext command that installs PCRE2 alongside any C/C++ extensions.
    """

    def run(self) -> None:
        """Build PCRE2 from source before installing any extensions."""

        cwd = Path(os.getcwd()) / "third_party" / "pcre2-10.42"
        if sys.platform == "win32":  # Windows
            try:
                subprocess.check_call(["cmake", "--version"])
            except subprocess.CalledProcessError as exc:
                print("CMake not installed")
                sys.exit(1)

            try:
                subprocess.check_call(
                    [
                        "cmake", "-G", "NMake Makefiles", "..",
                        "-DCMAKE_BUILD_TYPE=Release"
                    ],
                    cwd=str(cwd)
                )
                subprocess.check_call(["nmake"], cwd=str(cwd))
            except subprocess.CalledProcessError as exc:
                print("failed to build PCRE2 on Windows:", exc)
                sys.exit(1)

        else:  # unix
            try:
                subprocess.check_call(["./configure"], cwd=str(cwd))
                subprocess.check_call(["make"], cwd=str(cwd))
            except subprocess.CalledProcessError as exc:
                print("failed to build PCRE2 on Unix:", exc)
                sys.exit(1)

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
