cimport numpy as np

cdef set _valid_timedelta_types
cdef long int[:] _pytimedelta_ns_coefs

cdef long int _pandas_timedelta_to_ns(object timedelta)
cdef object _pytimedelta_to_ns(object pytimedelta)
cdef object _numpy_timedelta64_to_ns(
    object timedelta64,
    object start_year,
    object start_month,
    object start_day
)
cdef np.ndarray[long int] _pandas_timedelta_objects_to_ns(
    np.ndarray[object] arr
)
cdef np.ndarray[object] _pytimedelta_objects_to_ns(
    np.ndarray[object] arr
)
cdef np.ndarray[object] _numpy_timedelta64_objects_to_ns(
    np.ndarray[object] arr,
    object start_year,
    object start_month,
    object start_day
)
cdef np.ndarray[object] _mixed_timedelta_objects_to_ns(
    np.ndarray[object] arr,
    object start_year,
    object start_month,
    object start_day
)
