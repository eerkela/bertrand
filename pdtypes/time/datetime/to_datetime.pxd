cimport numpy as np

# vectorized functions
cdef np.ndarray[char, cast=True] is_aware_vector(
    np.ndarray[object] arr
)
