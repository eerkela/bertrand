from setuptools import setup
from Cython.Build import cythonize
import numpy


setup(
    ext_modules=cythonize(
        [
            "pdcast/*.pyx",
            "pdcast/types/*.pyx",
            "pdcast/util/round/*.pyx",
            "pdcast/util/structs/*.pyx",
            "pdcast/util/time/*.pyx",
        ],
        language_level="3",
        compiler_directives={"embedsignature": True}
    ),
    include_dirs=[numpy.get_include()],
    zip_safe=False,
)
