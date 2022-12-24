import distutils.core
from Cython.Build import cythonize
import numpy


distutils.core.setup(
    name="pdtypes",
    ext_modules=cythonize(
        [
            "pdtypes/cast/util/loops/*.pyx",
            "pdtypes/cast/util/round/*.pyx",
            "pdtypes/cast/util/time/*.pyx",
            "pdtypes/cast/util/time/datetime/*.pyx",
            "pdtypes/cast/util/time/timedelta/*.pyx",
            "pdtypes/check/*.pyx",
            "pdtypes/util/*.pyx",
            "pdtypes/types/*.pyx",
            "pdtypes/types/atomic/*.pyx",
            "pdtypes/types/parse/*.pyx",
            "pdtypes/types/resolve/*.pyx"
        ],
        language_level="3",
        compiler_directives={"embedsignature": True}
    ),
    include_dirs=[numpy.get_include()]
)
