"""Setup script for Bertrand."""
from pathlib import Path
import os

import numpy
from Cython.Build import cythonize  # type: ignore
from setuptools import Extension, setup  # type: ignore
from pybind11.setup_helpers import Pybind11Extension, build_ext


# TODO: C++ users have to execute $(python3 -m bertrand -I) to compile against bertrand.h
# c++ foo.cpp -o foo.out $(python3 -m bertrand -I)

# TODO: Python users writing C++ extensions should add
# include_dirs=[bertrand.get_include()] to their Extension objects and/or setup() call.


# NOTE: bertrand users environment variables to control the build process:
#   $ DEBUG=true pip install bertrand
#       build with logging enabled


# NOTE: See setuptools for more info on how extensions are built:
# https://setuptools.pypa.io/en/latest/userguide/ext_modules.html


TRUTHY = {
    "1", "true", "t", "yes", "y", "ok", "sure", "yep", "yap", "yup", "yeah", "indeed",
    "aye", "roger", "absolutely", "certainly", "definitely", "positively", "positive",
    "affirmative",
}
FALSY = {
    "0", "false", "f", "no", "n", "nah", "nope", "nop", "nay", "never", "negative",
    "negatory", "zip", "zilch", "nada", "nil", "null", "none",
}


DEBUG: bool
fdebug = os.environ.get("DEBUG", "0").lower()
if fdebug in TRUTHY:
    DEBUG = True
elif fdebug in FALSY:
    DEBUG = False
else:
    raise ValueError(f"DEBUG={repr(fdebug)} is not a valid boolean value")


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
    cmdclass={"build_ext": build_ext},
)
