"""Implements 2 functions, `localize()` and `timezone()`, which allow for
vectorized, dtype-agnostic timezone manipulations on datetime scalars, arrays,
and Series.

`timezone()` accepts a timezone specifier (IANA string, instance of
datetime.tzinfo, or None), validates it, and returns it for use in
`localize()`.

`localize()` takes a datetime-like scalar/array/Series and localizes each
element to the given timezone specifier.  Automatically handles mixed
aware/naive or mixed timezone inputs, including mixed datetime types and
object-dtyped arrays/Series.

# TODO: add is_utc
"""
import datetime
from cpython cimport datetime
import zoneinfo

cimport cython
import dateutil
import numpy as np
cimport numpy as np
import pandas as pd
import pytz
import tzlocal

from pdtypes.check import get_dtype


########################
####   Constants    ####
########################


cdef tuple utc_timezones = (
    datetime.timezone(datetime.timedelta(0)),
    datetime.timezone.utc,
    pytz.utc,
    zoneinfo.ZoneInfo("UTC"),
    dateutil.tz.UTC
)


cdef set valid_datetime_types = {
    pd.Timestamp,
    datetime.datetime
}


#######################
####    Private    ####
#######################


cdef inline object localize_pandas_timestamp_scalar(
    object timestamp,
    datetime.tzinfo tz
):
    """Internal C interface for public-facing localize() function when used on
    `pd.Timestamp` scalars.
    """
    if not tz:  # return naive
        if timestamp.tzinfo:  # timestamp is not naive
            timestamp = timestamp.tz_convert("UTC")
            return timestamp.tz_localize(None)
        return timestamp

    # return aware
    if not timestamp.tzinfo:  # timestamp is naive
        timestamp = timestamp.tz_localize("UTC")
    return timestamp.tz_convert(tz)


cdef inline datetime.datetime localize_pydatetime_scalar(
    datetime.datetime pydatetime,
    datetime.tzinfo tz
):
    """Internal C interface for public-facing localize() function when used on
    `datetime.datetime` scalars.
    """
    if not tz:  # return naive
        if pydatetime.tzinfo:  # timestamp is not naive
            pydatetime = pydatetime.astimezone(datetime.timezone.utc)
            return pydatetime.replace(tzinfo=None)
        return pydatetime

    # return aware
    if not pydatetime.tzinfo:  # timestamp is naive
        pydatetime = pydatetime.replace(tzinfo=datetime.timezone.utc)
    return pydatetime.astimezone(tz)


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] localize_pandas_timestamp_vector(
    np.ndarray[object] arr,
    datetime.tzinfo tz
):
    """Internal C interface for public-facing localize() function when used on
    `pd.Timestamp` object arrays.
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        result[i] = localize_pandas_timestamp_scalar(arr[i], tz)

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] localize_pydatetime_vector(
    np.ndarray[object] arr,
    datetime.tzinfo tz
):
    """Internal C interface for public-facing localize() function when used on
    `datetime.datetime` object arrays.
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        result[i] = localize_pydatetime_scalar(arr[i], tz)

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] localize_mixed_datetimelike_vector(
    np.ndarray[object] arr,
    datetime.tzinfo tz
):
    """Internal C interface for public-facing localize() function when used on
    mixed datetime-like object arrays.
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef object element
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        element = arr[i]
        if isinstance(element, pd.Timestamp):
            result[i] = localize_pandas_timestamp_scalar(element, tz)
        else:
            result[i] = localize_pydatetime_scalar(element, tz)

    return result


######################
####    Public    ####
######################


def is_utc(tz: None | str | datetime.tzinfo) -> bool:
    """Check whether a timezone specifier is utc."""
    return timezone(tz) in utc_timezones


def localize_pandas_timestamp(
    arg: pd.Timestamp | np.ndarray | pd.Series,
    tz: None | str | datetime.tzinfo
) -> pd.Timestamp | np.ndarray | pd.Series:
    """TODO"""
    # np.array
    if isinstance(arg, np.ndarray):
        return localize_pandas_timestamp_vector(arg, tz)

    # pd.Series
    if isinstance(arg, pd.Series):
        if pd.api.types.is_datetime64_dtype(arg):  # use `.dt` namespace
            if not tz:  # return naive
                if arg.dt.tz:  # series is aware
                    return arg.dt.tz_convert(None)
                return arg

            # return aware
            if not arg.dt.tz:  # timestamp is naive
                arg = arg.dt.tz_localize("UTC")
            return arg.dt.tz_convert(tz)

        # localize elementwise
        index = arg.index
        arg = arg.to_numpy(dtype="O")
        arg = localize_pandas_timestamp_vector(arg, tz)
        return pd.Series(arg, index=index, copy=False)

    # scalar
    return localize_pandas_timestamp_scalar(arg, tz)


def localize_pydatetime(
    arg: datetime.datetime | np.ndarray | pd.Series,
    tz: None | str | datetime.tzinfo
) -> datetime.datetime | np.ndarray | pd.Series:
    """TODO"""
    # np.array
    if isinstance(arg, np.ndarray):
        return localize_pydatetime_vector(arg, tz)

    # pd.Series
    if isinstance(arg, pd.Series):
        index = arg.index
        arg = arg.to_numpy(dtype="O")
        arg = localize_pydatetime_vector(arg, tz)
        return pd.Series(arg, index=index, copy=False, dtype="O")

    # scalar
    return localize_pydatetime_scalar(arg, tz)


def localize(
    arg: pd.Timestamp | datetime.datetime | np.ndarray | pd.Series,
    tz: None | str | datetime.tzinfo
) -> pd.Timestamp | datetime.datetime | np.ndarray | pd.Series:
    """TODO"""
    dtype = get_dtype(arg)
    if isinstance(dtype, set) and dtype - valid_datetime_types:
        raise TypeError(f"`arg` must contain only localizable datetime "
                        f"elements, not {dtype - valid_datetime_types}")

    # resolve timezone
    tz = timezone(tz)

    # pd.Timestamp
    if dtype == pd.Timestamp:
        return localize_pandas_timestamp(arg, tz)

    # datetime.datetime
    if dtype == datetime.datetime:
        return localize_pydatetime(arg, tz)

    # mixed pd.Timestamp/datetime.datetime
    if isinstance(dtype, set):
        # pd.Series
        if isinstance(arg, pd.Series):
            index = arg.index
            arg = arg.to_numpy(dtype="O")
            arg = localize_mixed_datetimelike_vector(arg, tz)
            return pd.Series(arg, index=index, copy=False, dtype="O")

        # array
        return localize_mixed_datetimelike_vector(arg, tz)

    # datetime object can't be localized
    raise TypeError(f"could not localize datetime object of type {dtype}")


def timezone(
    tz: None | str | datetime.tzinfo
) -> None | datetime.tzinfo:
    """Ensure an IANA timezone specifier `tz` is valid and return a
    corresponding `datetime.tzinfo` object.
    """
    # TODO: pytz uses LMT (local mean time) for dates prior to 1902 for some
    # reason.  This appears to be a known pytz limitation.
    # https://stackoverflow.com/questions/24188060/in-pandas-why-does-tz-convert-change-the-timezone-used-from-est-to-lmt
    # https://github.com/pandas-dev/pandas/issues/41834
    # solution: use zoneinfo.ZoneInfo instead once pandas supports it
    # https://github.com/pandas-dev/pandas/pull/46425

    # IANA strings
    if isinstance(tz, str):
        if tz == "local":
            return pytz.timezone(tzlocal.get_localzone_name())
        return pytz.timezone(tz)

    # None or datetime.tzinfo (pytz.timezone/zoneinfo.ZoneInfo)
    if tz and not isinstance(tz, datetime.tzinfo):
        err_msg = (f"`tz` must be a datetime.tzinfo object or an "
                   f"IANA-recognized timezone string, not {type(tz)}")
        raise TypeError(err_msg)
    return tz
