cimport numpy as np

# constants
cdef set valid_timedelta_types
cdef long int[:] pytimedelta_ns_coefs

# scalar conversion functions
cdef long int pandas_timedelta_to_ns_scalar(object timedelta)
cdef object pytimedelta_to_ns_scalar(object pytimedelta)
cdef object numpy_timedelta64_to_ns_scalar(
    object timedelta64,
    object start_year,
    object start_month,
    object start_day
)

# object array conversion functions
cdef np.ndarray[long int] pandas_timedelta_to_ns_vector(
    np.ndarray[object] arr
)
cdef np.ndarray[object] pytimedelta_to_ns_vector(
    np.ndarray[object] arr
)
cdef np.ndarray[object] numpy_timedelta64_to_ns_vector(
    np.ndarray[object] arr,
    object start_year,
    object start_month,
    object start_day
)
cdef np.ndarray[object] mixed_timedelta_to_ns_vector(
    np.ndarray[object] arr,
    object start_year,
    object start_month,
    object start_day
)
