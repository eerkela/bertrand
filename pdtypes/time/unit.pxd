cimport numpy as np

# constants
cdef dict as_ns
cdef tuple valid_units

# vectorized functions
cdef np.ndarray[object] cast_to_int_vector(np.ndarray arr)
cdef object cast_to_int(object val)
