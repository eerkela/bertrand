import distutils.core
from Cython.Build import cythonize
import numpy


distutils.core.setup(
    name="pdtypes",
    ext_modules=cythonize(
        [
            "pdtypes/cython/*.pyx",
            "pdtypes/check/*.pyx"
        ],
        language_level="3"
    ),
    include_dirs=[numpy.get_include()]
)
