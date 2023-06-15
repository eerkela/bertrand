from setuptools import setup, Extension
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
    ),
    include_dirs=[numpy.get_include()],
    zip_safe=False,
)
