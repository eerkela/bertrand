from cpython cimport datetime
from .epoch cimport Epoch


# constants
cdef dict timedelta_regex


# functions
cpdef object pandas_timedelta_to_ns(object delta)
cpdef object pytimedelta_to_ns(datetime.timedelta delta)
cpdef object numpy_timedelta64_to_ns(
    object delta,
    Epoch since,
    str unit = *,
    long int step_size = *
)
