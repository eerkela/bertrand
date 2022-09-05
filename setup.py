import distutils.core
from Cython.Build import cythonize
import numpy


distutils.core.setup(
    name="pdtypes",
    ext_modules=cythonize(
        [
            "pdtypes/check/*.pyx",
            "pdtypes/round/*.pyx",
            "pdtypes/time/*.pyx",
            "pdtypes/time/datetime/*.pyx",
            "pdtypes/time/timedelta/*.pyx",
            "pdtypes/util/loops/*.pyx"
        ],
        language_level="3",
        compiler_directives={"embedsignature": True}
    ),
    include_dirs=[numpy.get_include()]
)
