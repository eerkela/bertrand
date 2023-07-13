from os import cpu_count
from setuptools import Extension, find_packages, setup

from Cython.Build import cythonize
import numpy


extensions = [
    Extension(
        "*",
        ["pdcast/**/*.pyx"],
        define_macros=[("NPY_NO_DEPRECATED_API", "NPY_1_7_API_VERSION")],
    ),
]


setup(
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
    packages=find_packages(include=["pdcast", "pdcast.*"]),
)
