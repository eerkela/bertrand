from cpython cimport datetime
cimport numpy as np

# constants
cdef set valid_datetime_types
cdef object utc_naive_pydatetime
cdef object utc_aware_pydatetime
cdef long int[:] pytimedelta_ns_coefs

# scalar functions
cdef long int pandas_timestamp_to_ns_scalar(object timestamp)
cdef object pydatetime_to_ns_scalar(object pydatetime)
cdef object numpy_datetime64_to_ns_scalar(object datetime64)

# vectorized functions
cdef np.ndarray[long int] pandas_timestamp_to_ns_vector(np.ndarray[object] arr)
cdef np.ndarray[object] pydatetime_to_ns_vector(np.ndarray[object] arr)
cdef np.ndarray[object] numpy_datetime64_to_ns_vector(np.ndarray[object] arr)
cdef np.ndarray[object] mixed_datetime_to_ns_vector(np.ndarray[object] arr)
