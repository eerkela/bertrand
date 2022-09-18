cimport numpy as np

# vectorized functions
cdef np.ndarray[object] _object_types(
    np.ndarray[object] arr,
    bint exact = *
)
