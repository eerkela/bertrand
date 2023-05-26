from setuptools import setup
from Cython.Build import cythonize
import numpy


setup(
    ext_modules=cythonize(
        [
            "*.pyx",
            "pdcast/*.pyx",
            "pdcast/types/array/*.pyx",
            "pdcast/types/base/*.pyx",
            "pdcast/types/*.pyx",
            "pdcast/util/*.pyx",
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
