import distutils.core
from Cython.Build import cythonize
import numpy

distutils.core.setup(
    ext_modules=cythonize("test_cython.pyx"),
    include_dirs=[numpy.get_include()]
)
