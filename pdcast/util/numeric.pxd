cimport numpy as np

from pdcast cimport types
from pdcast.util.round cimport Tolerance


# functions
cpdef object boundscheck(
    object series,
    types.ScalarType dtype,
    object tol,
    str errors
)
cpdef object downcast_integer(
    object series,
    Tolerance tol,
    types.CompositeType smallest
)
cpdef object downcast_float(
    object series,
    Tolerance tol,
    types.CompositeType smallest
)
cpdef object downcast_complex(
    object series,
    Tolerance tol,
    types.CompositeType smallest
)
cpdef np.ndarray[np.uint8_t, cast=True] isinf(object series)
cpdef object real(object series)
cpdef object imag(object series)
