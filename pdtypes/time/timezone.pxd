from cpython cimport datetime
cimport numpy as np

# constants
cdef tuple utc_timezones
cdef set valid_datetime_types

# scalar localization functions
cdef object localize_pandas_timestamp_scalar(
    object timestamp,
    datetime.tzinfo tz
)
cdef datetime.datetime localize_pydatetime_scalar(
    datetime.datetime pydatetime,
    datetime.tzinfo tz
)

# vectorized localization functions
cdef np.ndarray[object] localize_pandas_timestamp_vector(
    np.ndarray[object] arr,
    datetime.tzinfo tz
)
cdef np.ndarray[object] localize_pydatetime_vector(
    np.ndarray[object] arr,
    datetime.tzinfo tz
)
cdef np.ndarray[object] localize_mixed_datetimelike_vector(
    np.ndarray[object] arr,
    datetime.tzinfo tz
)
