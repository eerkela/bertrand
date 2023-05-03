from cpython cimport datetime


# constants
cdef datetime.datetime py_naive_utc
cdef datetime.datetime py_aware_utc
cdef object iso_8601_format_pattern
cdef object iso_8601_pattern
cdef object parser_overflow_pattern


# functions
cpdef object pandas_timestamp_to_ns(object date, object tz = *)
cpdef datetime.datetime ns_to_pydatetime(object ns, object tz = *)
cpdef object pydatetime_to_ns(datetime.datetime date, object tz = *)
cpdef object numpy_datetime64_to_ns(
    object date,
    str unit = *,
    long int step_size = *
)
cpdef datetime.datetime localize_pydatetime_scalar(
    datetime.datetime dt,
    object tz
)
cpdef bint is_iso_8601_format_string(str input_string)
cpdef object iso_8601_to_ns(str input_string)
cpdef datetime.datetime string_to_pydatetime(
    str input_string,
    str format = *,
    object parser_info = *,
    object tz = *,
    str errors = *
)
cpdef Exception filter_dateutil_parser_error(Exception err)
cpdef bint is_utc(datetime.tzinfo tz)
