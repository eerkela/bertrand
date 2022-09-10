from cpython cimport datetime
cimport numpy as np

# constants
cdef object build_iso_8601_regex()
cdef object iso_8601_pattern
cdef np.ndarray month_length
cdef object min_pydatetime
cdef object max_pydatetime

# scalar functions
cdef object iso_8601_string_to_ns_scalar(str string)
cdef datetime.datetime string_to_pydatetime_scalar_with_format(
    str string,
    str format,
    datetime.tzinfo tz
)
cdef datetime.datetime string_to_pydatetime_scalar_parsed(
    str string,
    object parser_info,
    datetime.tzinfo tz
)
cdef datetime.datetime string_to_pydatetime_scalar_with_fallback(
    str string,
    str format,
    object parser_info,
    datetime.tzinfo tz
)

# vectorized functions
cdef tuple iso_8601_string_to_ns_vector(
    np.ndarray[str] arr,
    str errors
)
cdef tuple string_to_pydatetime_vector_with_format(
    np.ndarray[str] arr,
    str format,
    datetime.tzinfo tz,
    str errors
)
cdef tuple string_to_pydatetime_vector_parsed(
    np.ndarray[str] arr,
    object parser_info,
    datetime.tzinfo tz,
    str errors
)
cdef tuple string_to_pydatetime_vector_with_fallback(
    np.ndarray[str] arr,
    str format,
    object parser_info,
    datetime.tzinfo tz,
    str errors
)
