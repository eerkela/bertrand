cimport numpy as np

# constants
cdef long int min_pandas_timedelta_ns
cdef long int max_pandas_timedelta_ns
cdef object min_pytimedelta_ns
cdef object max_pytimedelta_ns
cdef object min_numpy_timedelta64_ns
cdef object max_numpy_timedelta64_ns

# scalar functions
cdef object ns_to_pandas_timedelta_scalar(long int ns)

# vectorized functions
cdef np.ndarray[object] ns_to_pandas_timedelta_vector(
    np.ndarray arr
)
