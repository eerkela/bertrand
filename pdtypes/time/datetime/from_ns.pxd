from cpython cimport datetime
cimport numpy as np

# constants
cdef object utc_epoch
cdef object utc_naive_pydatetime
cdef object utc_aware_pydatetime
cdef long int min_pandas_timestamp_ns
cdef long int max_pandas_timestamp_ns
cdef object min_pydatetime_ns
cdef object max_pydatetime_ns
cdef object min_numpy_datetime64_ns
cdef object max_numpy_datetime64_ns

# scalar functions
cdef object ns_to_pydatetime_scalar(
    object ns,
    datetime.tzinfo tz
)

# vectorized functions
cdef np.ndarray[object] ns_to_pydatetime_vector(
    np.ndarray arr,
    datetime.tzinfo tz
)
