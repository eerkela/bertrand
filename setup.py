import distutils.core
from Cython.Build import cythonize
import numpy


distutils.core.setup(
    name="pdtypes",
    ext_modules=cythonize(
        [
            "pdtypes/check/*.pyx",
            "pdtypes/util/loops/*.pyx",
            "pdtypes/util/round/*.pyx"
        ],
        language_level="3"
    ),
    include_dirs=[numpy.get_include()]
)
