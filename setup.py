import distutils.core
from Cython.Build import cythonize
import numpy


distutils.core.setup(
    ext_modules=cythonize("pdtypes/cython/loops.pyx", language_level="3"),
    include_dirs=[numpy.get_include()]
)
