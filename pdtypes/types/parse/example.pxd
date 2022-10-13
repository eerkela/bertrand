cimport numpy as np

from pdtypes.types cimport ElementType

# parsing functions
cdef ElementType parse_example_scalar(
    object example,
    bint sparse = *,
    bint categorical = *,
    bint force_nullable = *
)
cdef set parse_example_vector(
    np.ndarray[object] arr,
    bint sparse = *,
    bint categorical = *,
    bint force_nullable = *
)
