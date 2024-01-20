"""Setup script for bertrand package."""
from pathlib import Path
from os import cpu_count
from setuptools import Extension, setup

from Cython.Build import cythonize
import numpy


# TODO: invoke CMake to build the C++ data structures and link them to C++ compiler


# extensions = [
#     Extension(
#         "*",
#         sources=["bertrand/**/*.pyx"],
#         define_macros=[("NPY_NO_DEPRECATED_API", "NPY_1_7_API_VERSION")],
#     ),
# ]


setup(
    long_description=Path("README.rst").read_text(),
    # ext_modules=cythonize(
    #     extensions,
    #     language_level="3",
    #     compiler_directives={
    #         "embedsignature": True,
    #     },
    #     nthreads=cpu_count(),
    # ),
    ext_modules=[
        Extension(
            "bertrand.structs.linked",
            sources=["bertrand/structs/linked.cpp"],
            extra_compile_args=["-O3"]
        ),
    ],
    include_dirs=[numpy.get_include()],
    zip_safe=False,
)
