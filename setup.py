import distutils.core
from Cython.Build import cythonize
import numpy


distutils.core.setup(
    ext_modules=cythonize("pdtypes/cython/loops.pyx"),
    include_dirs=[numpy.get_include()]
)
