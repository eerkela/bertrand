cimport numpy as np

from pdtypes.types cimport ElementType, CompositeType

# parsing functions
cdef ElementType parse_example_scalar(
    object example,
    bint sparse = *,
    bint categorical = *,
    bint force_nullable = *
)
cdef CompositeType parse_example_vector(
    np.ndarray[object] arr,
    bint sparse = *,
    bint categorical = *,
    bint force_nullable = *
)
