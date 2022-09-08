cimport numpy as np

# constants
cdef list[object] timedelta_formats_regex()
cdef dict timedelta_regex

# scalar functions
cdef object timedelta_string_to_ns_scalar(
    str string,
    bint as_hours = *
)

# vectorized functions
cdef tuple timedelta_string_to_ns_vector(
    np.ndarray[str] arr,
    bint as_hours = *,
    str errors = *
)
