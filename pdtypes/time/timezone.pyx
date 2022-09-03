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
"""
import datetime

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd
import pytz
import tzlocal

from pdtypes.check import get_dtype


# TODO: pytz uses LMT (local mean time) for dates prior to 1902 for some
# reason.  This appears to be a known pytz limitation.
# https://stackoverflow.com/questions/24188060/in-pandas-why-does-tz-convert-change-the-timezone-used-from-est-to-lmt
# https://github.com/pandas-dev/pandas/issues/41834
# solution: use zoneinfo.ZoneInfo instead once pandas supports it
# https://github.com/pandas-dev/pandas/pull/46425


cdef object localize_pandas_timestamp(
    object datetime_like,
    object tz
):
    """Internal C interface for public-facing localize() function when used on
    `pd.Timestamp` scalars.
    """
    if not tz:  # return naive
        if datetime_like.tzinfo:  # timestamp is not naive
            datetime_like = datetime_like.tz_convert("UTC")
            return datetime_like.tz_localize(None)
        return datetime_like

    # return aware
    if not datetime_like.tzinfo:  # timestamp is naive
        datetime_like = datetime_like.tz_localize("UTC")
    return datetime_like.tz_convert(tz)


cdef object localize_pydatetime(
    object datetime_like,
    object tz
):
    """Internal C interface for public-facing localize() function when used on
    `datetime.datetime` scalars.
    """
    if not tz:  # return naive
        if datetime_like.tzinfo:  # timestamp is not naive
            datetime_like = datetime_like.astimezone(datetime.timezone.utc)
            return datetime_like.replace(tzinfo=None)
        return datetime_like

    # return aware
    if not datetime_like.tzinfo:  # timestamp is naive
        datetime_like = datetime_like.replace(tzinfo=datetime.timezone.utc)
    return datetime_like.astimezone(tz)


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] localize_pandas_timestamp_array(
    np.ndarray[object] arr,
    object tz
):
    """Internal C interface for public-facing localize() function when used on
    `pd.Timestamp` object arrays.
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        result[i] = localize_pandas_timestamp(arr[i], tz)

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] localize_pydatetime_array(
    np.ndarray[object] arr,
    object tz
):
    """Internal C interface for public-facing localize() function when used on
    `datetime.datetime` object arrays.
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        result[i] = localize_pydatetime(arr[i], tz)

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] localize_mixed_datetimelike_array(
    np.ndarray[object] arr,
    object tz
):
    """Internal C interface for public-facing localize() function when used on
    mixed datetime-like object arrays.
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef object element
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    cdef dict dispatch = {
        pd.Timestamp: localize_pandas_timestamp,
        datetime.datetime: localize_pydatetime
    }

    for i in range(arr_length):
        element = arr[i]
        result[i] = dispatch[type(element)](element, tz)

    return result


def localize(
    datetime_like: pd.Timestamp | datetime.datetime | np.ndarray | pd.Series,
    tz: None | str | datetime.tzinfo
) -> pd.Timestamp | datetime.datetime | np.ndarray | pd.Series:
    """TODO"""
    dtype = get_dtype(datetime_like)
    tz = timezone(tz)
    if isinstance(dtype, set) and dtype != {pd.Timestamp, datetime.datetime}:
        raise TypeError(f"`datetime_like` must have homogenous, datetime-like "
                        f"element types with a .tzinfo attribute, not {dtype}")

    # pd.Timestamp
    if dtype == pd.Timestamp:
        # np.array
        if isinstance(datetime_like, np.ndarray):
            return localize_pandas_timestamp_array(datetime_like, tz)

        # pd.Series
        if isinstance(datetime_like, pd.Series):
            # use `.dt` namespace if available
            if pd.api.types.is_datetime64_dtype(datetime_like):
                if not tz:  # return naive
                    if datetime_like.dt.tz:  # series is not naive
                        datetime_like = datetime_like.dt.tz_convert("UTC")
                        return datetime_like.dt.tz_localize(None)
                    return datetime_like

                # return aware
                if not datetime_like.dt.tz:  # timestamp is naive
                    datetime_like = datetime_like.dt.tz_localize("UTC")
                return datetime_like.dt.tz_convert(tz)

            # localize elementwise
            index = datetime_like.index
            datetime_like = datetime_like.to_numpy(dtype="O")
            result = localize_pandas_timestamp_array(datetime_like, tz)
            return pd.Series(result, index=index, copy=False)

        # scalar
        return localize_pandas_timestamp(datetime_like, tz)

    # datetime.datetime
    if dtype == datetime.datetime:
        # np.array
        if isinstance(datetime_like, np.ndarray):
            return localize_pydatetime_array(datetime_like, tz)

        # pd.Series
        if isinstance(datetime_like, pd.Series):
            index = datetime_like.index
            datetime_like = datetime_like.to_numpy(dtype="O")
            result = localize_pydatetime_array(datetime_like, tz)
            return pd.Series(result, index=index, copy=False, dtype="O")

        # scalar
        return localize_pydatetime(datetime_like, tz)

    # mixed pd.Timestamp/datetime.datetime
    if dtype == {pd.Timestamp, datetime.datetime}:
        # series
        if isinstance(datetime_like, pd.Series):
            index = datetime_like.index
            datetime_like = datetime_like.to_numpy(dtype="O")
            result = localize_mixed_datetimelike_array(datetime_like, tz)
            return pd.Series(result, index=index, copy=False, dtype="O")

        # array
        return localize_mixed_datetimelike_array(datetime_like, tz)

    # datetime object can't be localized
    raise TypeError(f"could not localize datetime object of type {dtype}")


def timezone(
    tz: None | str | datetime.tzinfo
) -> None | datetime.tzinfo:
    """Ensure an IANA timezone specifier `tz` is valid and return a
    corresponding `datetime.tzinfo` object.
    """
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