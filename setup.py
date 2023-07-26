from pathlib import Path
from os import cpu_count
from setuptools import Extension, setup

from Cython.Build import cythonize
import numpy


# include all .pyx files in the pdcast/ directory
extensions = [
    Extension(
        "*",
        sources=["pdcast/**/*.pyx"],
        define_macros=[("NPY_NO_DEPRECATED_API", "NPY_1_7_API_VERSION")],
    ),
]


# invoke setuptools
setup(
    long_description=Path("README.rst").read_text(),
    ext_modules=cythonize(
        extensions,
        language_level="3",
        compiler_directives={
            "embedsignature": True,
        },
        nthreads=cpu_count(),
    ),
    include_dirs=[numpy.get_include()],
    zip_safe=False,
)
