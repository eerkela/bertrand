from setuptools import setup
from Cython.Build import cythonize
import numpy


setup(
    name="pdtypes",
    ext_modules=cythonize(
        [
            "pdtypes/*.pyx",
            "pdtypes/types/*.pyx",
            "pdtypes/util/*.pyx",
            "pdtypes/util/round/*.pyx",
            "pdtypes/util/time/*.pyx",
        ],
        language_level="3",
        compiler_directives={"embedsignature": True}
    ),
    include_dirs=[numpy.get_include()]
)
