"""Timezone interface for datetime localizations.

Functions
---------
    is_utc(tz: None | str | datetime.tzinfo) -> bool:
        Check whether a timezone specifier is utc.

    localize_pandas_timestamp(
        arg: pd.Timestamp | np.ndarray | pd.Series,
        tz: str | datetime.tzinfo | None,
        utc: bool = True
    ) -> pd.Timestamp | np.ndarray | pd.Series:
        Localize `pandas.Timestamp` objects to the given timezone.

    localize_pydatetime(
        arg: datetime.datetime | np.ndarray | pd.Series,
        tz: str | datetime.tzinfo | None,
        utc: bool = True
    ) -> datetime.datetime | np.ndarray | pd.Series:
        Localize `datetime.datetime` objects to the given timezone.

    localize(
        arg: pd.Timestamp | datetime.datetime | np.ndarray | pd.Series,
        tz: str | datetime.tzinfo | None,
        utc: bool = True
    ) -> pd.Timestamp | datetime.datetime | np.ndarray | pd.Series:
        Localize arbitrary datetime objects to the given timezone.

    timezone(tz: str | datetime.tzinfo | None) -> datetime.tzinfo | None:
        Convert a timezone specifier into a corresponding timezone object.

Examples
--------
Datetime objects can be `pandas.Timestamp`:

>>> localize(pd.Timestamp("2022-01-04 00:00:00"), "UTC")
Timestamp('2022-01-04 00:00:00+0000', tz='UTC')
>>> localize(pd.Timestamp("2022-01-04 00:00:00"), "US/Pacific")
Timestamp('2022-01-03 16:00:00-0800', tz='US/Pacific')
>>> localize(pd.Timestamp("2022-01-04 00:00:00"), "US/Pacific", utc=False)
Timestamp('2022-01-04 00:00:00-0800', tz='US/Pacific')
>>> localize(pd.Timestamp("2022-01-04 00:00:00+0000"), "UTC")
Timestamp('2022-01-04 00:00:00+0000', tz='UTC')
>>> localize(pd.Timestamp("2022-01-04 00:00:00+0000"), "US/Pacific")
Timestamp('2022-01-03 16:00:00-0800', tz='US/Pacific')
>>> series = pd.to_datetime(pd.Series([1, 2, 3]), unit="s")
>>> series
0   1970-01-01 00:00:01
1   1970-01-01 00:00:02
2   1970-01-01 00:00:03
dtype: datetime64[ns]
>>> localize(series, "US/Pacific")
0   1969-12-31 16:00:01-08:00
1   1969-12-31 16:00:02-08:00
2   1969-12-31 16:00:03-08:00
dtype: datetime64[ns, US/Pacific]
>>> localize(series, "US/Pacific", utc=False)
0   1970-01-01 00:00:01-08:00
1   1970-01-01 00:00:02-08:00
2   1970-01-01 00:00:03-08:00
dtype: datetime64[ns, US/Pacific]

Or `datetime.datetime`:

>>> localize(datetime.datetime.fromisoformat("2022-01-04 00:00:00"), "UTC")
datetime.datetime(2022, 1, 4, 0, 0, tzinfo=<UTC>)
>>> localize(
...     datetime.datetime.fromisoformat("2022-01-04 00:00:00"),
...     "US/Pacific"
... )
datetime.datetime(2022, 1, 3, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
>>> localize(
...     datetime.datetime.fromisoformat("2022-01-04 00:00:00"),
...     "US/Pacific",
...     utc=False
... )
datetime.datetime(2022, 1, 4, 0, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
>>> localize(
...     datetime.datetime.fromtimestamp(0, datetime.timezone.utc),
...     "UTC"
... )
datetime.datetime(1970, 1, 1, 0, 0, 0, tzinfo=<UTC>)
>>> localize(
...     datetime.datetime.fromtimestamp(0, datetime.timezone.utc),
...     "US/Pacific"
... )
datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
>>> array = np.array(
...     [datetime.datetime.utcfromtimestamp(i) for i in range(1, 4)]
... )
>>> array
array([datetime.datetime(1970, 1, 1, 0, 0, 1),
    datetime.datetime(1970, 1, 1, 0, 0, 2),
    datetime.datetime(1970, 1, 1, 0, 0, 3)], dtype=object)
>>> localize(array, "US/Pacific")
array([datetime.datetime(1969, 12, 31, 16, 0, 1, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
    datetime.datetime(1969, 12, 31, 16, 0, 2, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
    datetime.datetime(1969, 12, 31, 16, 0, 3, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)],
    dtype=object)
>>> localize(array, "US/Pacific", utc=False)
array([datetime.datetime(1970, 1, 1, 0, 0, 1, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
    datetime.datetime(1970, 1, 1, 0, 0, 2, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
    datetime.datetime(1970, 1, 1, 0, 0, 3, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)],
    dtype=object)

Or even mixed:

>>> mixed = np.where([True, False, True], series.to_numpy(dtype="O"), array)
>>> mixed
array([Timestamp('1970-01-01 00:00:01'),
    datetime.datetime(1970, 1, 1, 0, 0, 2),
    Timestamp('1970-01-01 00:00:03')], dtype=object)
>>> localize(mixed, "US/Pacific")
array([Timestamp('1969-12-31 16:00:01-0800', tz='US/Pacific'),
    datetime.datetime(1969, 12, 31, 16, 0, 2, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
    Timestamp('1969-12-31 16:00:03-0800', tz='US/Pacific')],
    dtype=object)
>>> localize(mixed, "US/Pacific", utc=False)
array([Timestamp('1970-01-01 00:00:01-0800', tz='US/Pacific'),
    datetime.datetime(1970, 1, 1, 0, 0, 2, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
    Timestamp('1970-01-01 00:00:03-0800', tz='US/Pacific')],
    dtype=object)

Timezone specifiers can be IANA strings:

>>> timezone("UTC")
<UTC>
>>> timezone("US/Pacific")
<DstTzInfo 'US/Pacific' LMT-1 day, 16:07:00 STD>
>>> timezone("Europe/Berlin")
<DstTzInfo 'Europe/Berlin' LMT+0:53:00 STD>

Or previously instantiated timezone objects:

>>> timezone(pytz.timezone("Asia/Hong_Kong"))
<DstTzInfo 'Asia/Hong_Kong' LMT+7:37:00 STD>
>>> timezone(zoneinfo.ZoneInfo("Australia/Sydney"))
zoneinfo.ZoneInfo(key='Australia/Sydney')
>>> timezone(dateutil.tz.gettz('America/Sao_Paulo'))
tzfile('/usr/share/zoneinfo/America/Sao_Paulo')

Or `None`, indicating a naive timezone:

>>> timezone(None)
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


# TODO: pytz uses LMT (local mean time) for dates prior to 1902 for some
# reason.  This appears to be a known pytz limitation.
# https://stackoverflow.com/questions/24188060/in-pandas-why-does-tz-convert-change-the-timezone-used-from-est-to-lmt
# https://github.com/pandas-dev/pandas/issues/41834
# solution: use zoneinfo.ZoneInfo instead once pandas supports it
# https://github.com/pandas-dev/pandas/pull/46425


########################
####   Constants    ####
########################


# a list of timezones recognized as UTC
cdef tuple utc_timezones = (
    datetime.timezone(datetime.timedelta(0)),
    datetime.timezone.utc,
    pytz.utc,
    zoneinfo.ZoneInfo("UTC"),
    dateutil.tz.UTC
)


# localizable datetime types
cdef set valid_datetime_types = {
    pd.Timestamp,
    datetime.datetime
}


#######################
####    Private    ####
#######################


cdef inline object localize_pandas_timestamp_scalar(
    object timestamp,
    datetime.tzinfo tz,
    bint utc
):
    """Localize a scalar `pandas.Timestamp` object to the given timezone."""
    # return naive
    if not tz:
        if not timestamp.tzinfo:  # timestamp is already naive
            return timestamp
        return timestamp.tz_convert(None)  # return as naive utc

    # return aware
    if not timestamp.tzinfo:  # timestamp is naive
        if not utc:  # localize directly to given timezone
            return timestamp.tz_localize(tz)
        timestamp = timestamp.tz_localize("UTC")  # interpret as utc
    return timestamp.tz_convert(tz)


cdef inline datetime.datetime localize_pydatetime_scalar(
    datetime.datetime pydatetime,
    datetime.tzinfo tz,
    bint utc
):
    """Localize a scalar `datetime.datetime` object to the given timezone."""
    # return naive
    if not tz:
        if not pydatetime.tzinfo:  # datetime is already naive
            return pydatetime
        pydatetime = pydatetime.astimezone(datetime.timezone.utc)  # as utc
        return pydatetime.replace(tzinfo=None)  # make naive

    # return aware
    if not pydatetime.tzinfo:  # datetime is naive
        if not utc:  # localize directly to given timezone
            # TODO: ZoneInfo objects do not have a .localize() method.  If
            # converting to use zoneinfo, use dt.replace(tzinfo=...) instead.
            return tz.localize(pydatetime)
        pydatetime = pydatetime.replace(tzinfo=datetime.timezone.utc)  # as utc
    return pydatetime.astimezone(tz)


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] localize_pandas_timestamp_vector(
    np.ndarray[object] arr,
    datetime.tzinfo tz,
    bint utc
):
    """Localize an array of `pandas.Timestamp` objects to the given timezone.
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        result[i] = localize_pandas_timestamp_scalar(arr[i], tz, utc=utc)

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] localize_pydatetime_vector(
    np.ndarray[object] arr,
    datetime.tzinfo tz,
    bint utc
):
    """Localize an array of `datetime.datetime` objects to the given timezone.
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        result[i] = localize_pydatetime_scalar(arr[i], tz, utc=utc)

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] localize_mixed_datetimelike_vector(
    np.ndarray[object] arr,
    datetime.tzinfo tz,
    bint utc
):
    """Localize an array of mixed datetime objects to the given timezone."""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef object element
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        element = arr[i]
        if isinstance(element, pd.Timestamp):
            result[i] = localize_pandas_timestamp_scalar(element, tz, utc=utc)
        else:
            result[i] = localize_pydatetime_scalar(element, tz, utc=utc)

    return result


######################
####    Public    ####
######################


def is_utc(tz: None | str | datetime.tzinfo) -> bool:
    """Check whether a timezone specifier is utc."""
    return timezone(tz) in utc_timezones


def localize_pandas_timestamp(
    arg: pd.Timestamp | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo | None,
    utc: bool = True
) -> pd.Timestamp | np.ndarray | pd.Series:
    """Localize `pandas.Timestamp` objects to the given timezone.

    Parameters
    ----------
    arg : pd.Timestamp | array-like
        A `pandas.Timestamp` object or vector of such objects to be localized.
    tz : str | datetime.tzinfo | None
        The timezone to localize results to.  This can be `None`, indicating
        a naive return type, an instance of `datetime.tzinfo` or one of its
        derivatives (from `pytz`, `zoneinfo`, etc.), or an IANA timezone
        database string ('US/Eastern', 'UTC', etc.).
    utc : bool, default True
        Controls whether to interpret naive datetimes as UTC (`True`) or to
        localize them directly to the given timezone (`False`).  If
        interpreting as utc, a naive input will first be localized to UTC and
        then converted to the given timezone.  Otherwise, it will be localized
        to `tz` directly.

    Returns
    -------
    pd.Timestamp | array-like
        A copy of `arg`, localized to the given timezone.  If `tz=None`, these
        will always be UTC times.

    Raises
    ------
    TypeError
        If `tz` is not an IANA-recognized timezone string, a `datetime.tzinfo`
        object, or `None`.

    Examples
    --------
    Inputs can be naive:

    >>> localize_pandas_timestamp(pd.Timestamp("2022-01-04 00:00:00"), "UTC")
    Timestamp('2022-01-04 00:00:00+0000', tz='UTC')
    >>> localize_pandas_timestamp(
    ...     pd.Timestamp("2022-01-04 00:00:00"),
    ...     "US/Pacific"
    ... )
    Timestamp('2022-01-03 16:00:00-0800', tz='US/Pacific')
    >>> localize_pandas_timestamp(
    ...     pd.Timestamp("2022-01-04 00:00:00"),
    ...     "US/Pacific",
    ...     utc=False
    ... )
    Timestamp('2022-01-04 00:00:00-0800', tz='US/Pacific')

    Or aware:

    >>> localize_pandas_timestamp(
    ...     pd.Timestamp("2022-01-04 00:00:00+0000"),
    ...     "UTC"
    ... )
    Timestamp('2022-01-04 00:00:00+0000', tz='UTC')
    >>> localize_pandas_timestamp(
    ...     pd.Timestamp("2022-01-04 00:00:00+0000"),
    ...     "US/Pacific"
    ... )
    Timestamp('2022-01-03 16:00:00-0800', tz='US/Pacific')

    And potentially vectorized:

    >>> series = pd.to_datetime(pd.Series([1, 2, 3]), unit="s")
    >>> series
    0   1970-01-01 00:00:01
    1   1970-01-01 00:00:02
    2   1970-01-01 00:00:03
    dtype: datetime64[ns]
    >>> localize_pandas_timestamp(series, "US/Pacific")
    0   1969-12-31 16:00:01-08:00
    1   1969-12-31 16:00:02-08:00
    2   1969-12-31 16:00:03-08:00
    dtype: datetime64[ns, US/Pacific]
    >>> localize_pandas_timestamp(series, "US/Pacific", utc=False)
    0   1970-01-01 00:00:01-08:00
    1   1970-01-01 00:00:02-08:00
    2   1970-01-01 00:00:03-08:00
    dtype: datetime64[ns, US/Pacific]
    """
    # resolve timezone
    tz = timezone(tz)

    # np.array
    if isinstance(arg, np.ndarray):
        return localize_pandas_timestamp_vector(arg, tz, utc=utc)

    # pd.Series
    if isinstance(arg, pd.Series):
        if pd.api.types.is_datetime64_dtype(arg):  # use `.dt` namespace
            # return naive
            if not tz:
                if not arg.dt.tz:  # series is already naive
                    return arg
                return arg.dt.tz_convert(None)  # return as naive utc

            # return aware
            if not arg.dt.tz:  # series is naive
                if not utc:  # localize directly to given timezone
                    return arg.dt.tz_localize(tz)
                arg = arg.dt.tz_localize("UTC")  # interpret as utc
            return arg.dt.tz_convert(tz)

        # localize elementwise
        index = arg.index
        arg = arg.to_numpy(dtype="O")
        arg = localize_pandas_timestamp_vector(arg, tz, utc=utc)
        return pd.Series(arg, index=index, copy=False)

    # scalar
    return localize_pandas_timestamp_scalar(arg, tz, utc=utc)


def localize_pydatetime(
    arg: datetime.datetime | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo | None,
    utc: bool = True
) -> datetime.datetime | np.ndarray | pd.Series:
    """Localize `datetime.datetime` objects to the given timezone.

    Parameters
    ----------
    arg : datetime.datetime | array-like
        A `datetime.datetime` object or vector of such objects to be localized.
    tz : str | datetime.tzinfo | None
        The timezone to localize results to.  This can be `None`, indicating a
        naive return type, an instance of `datetime.tzinfo` or one of its
        derivatives (from `pytz`, `zoneinfo`, etc.), or an IANA timezone
        database string ('US/Eastern', 'UTC', etc.).
    utc : bool, default True
        Controls whether to interpret naive datetimes as UTC (`True`) or to
        localize them directly to the given timezone (`False`).  If
        interpreting as utc, a naive input will first be localized to UTC and
        then converted to the given timezone.  Otherwise, it will be localized
        to `tz` directly.

    Returns
    -------
    datetime.datetime | array-like
        A copy of `arg`, localized to the given timezone.  If `tz=None`,
        these will always be UTC times.

    Raises
    ------
    TypeError
        If `tz` is not an IANA-recognized timezone string, a
        `datetime.tzinfo` object, or `None`.

    Examples
    --------
    Inputs can be naive:

    >>> localize_pydatetime(
    ...     datetime.datetime.fromisoformat("2022-01-04 00:00:00"),
    ...     "UTC"
    ... )
    datetime.datetime(2022, 1, 4, 0, 0, tzinfo=<UTC>)
    >>> localize_pydatetime(
    ...     datetime.datetime.fromisoformat("2022-01-04 00:00:00"),
    ...     "US/Pacific"
    ... )
    datetime.datetime(2022, 1, 3, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
    >>> localize_pydatetime(
    ...     datetime.datetime.fromisoformat("2022-01-04 00:00:00"),
    ...     "US/Pacific",
    ...     utc=False
    ... )
    datetime.datetime(2022, 1, 4, 0, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)

    Or aware:

    >>> localize_pydatetime(
    ...     datetime.datetime.fromtimestamp(0, datetime.timezone.utc),
    ...     "UTC"
    ... )
    datetime.datetime(1970, 1, 1, 0, 0, 0, tzinfo=<UTC>)
    >>> localize_pydatetime(
    ...     datetime.datetime.fromtimestamp(0, datetime.timezone.utc),
    ...     "US/Pacific"
    ... )
    datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)

    And potentially vectorized:

    >>> array = np.array(
    ...     [datetime.datetime.utcfromtimestamp(i) for i in range(1, 4)]
    ... )
    >>> array
    array([datetime.datetime(1970, 1, 1, 0, 0, 1),
       datetime.datetime(1970, 1, 1, 0, 0, 2),
       datetime.datetime(1970, 1, 1, 0, 0, 3)], dtype=object)
    >>> localize_pydatetime(array, "US/Pacific")
    array([datetime.datetime(1969, 12, 31, 16, 0, 1, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
       datetime.datetime(1969, 12, 31, 16, 0, 2, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
       datetime.datetime(1969, 12, 31, 16, 0, 3, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)],
      dtype=object)
    >>> localize_pydatetime(array, "US/Pacific", utc=False)
    array([datetime.datetime(1970, 1, 1, 0, 0, 1, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
       datetime.datetime(1970, 1, 1, 0, 0, 2, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
       datetime.datetime(1970, 1, 1, 0, 0, 3, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)],
      dtype=object)
    """
    # resolve timezone
    tz = timezone(tz)

    # np.array
    if isinstance(arg, np.ndarray):
        return localize_pydatetime_vector(arg, tz, utc=utc)

    # pd.Series
    if isinstance(arg, pd.Series):
        index = arg.index
        arg = arg.to_numpy(dtype="O")
        arg = localize_pydatetime_vector(arg, tz, utc=utc)
        return pd.Series(arg, index=index, copy=False, dtype="O")

    # scalar
    return localize_pydatetime_scalar(arg, tz, utc=utc)


def localize(
    arg: pd.Timestamp | datetime.datetime | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo | None,
    utc: bool = True
) -> pd.Timestamp | datetime.datetime | np.ndarray | pd.Series:
    """Localize arbitrary datetime objects to the given timezone.

    Parameters
    ----------
    arg : datetime-like | array-like
        A `pandas.Timestamp` or `datetime.datetime` object, or a vector of
        such objects to be localized.
    tz : str | datetime.tzinfo | None
        The timezone to localize results to.  This can be `None`, indicating
        a naive return type, an instance of `datetime.tzinfo` or one of its
        derivatives (from `pytz`, `zoneinfo`, etc.), or an IANA timezone
        database string ('US/Eastern', 'UTC', etc.).
    utc : bool, default True
        Controls whether to interpret naive datetimes as UTC (`True`) or to
        localize them directly to the given timezone (`False`).  If
        interpreting as utc, a naive input will first be localized to UTC and
        then converted to the given timezone.  Otherwise, it will be localized
        to `tz` directly.

    Returns
    -------
    datetime-like | array-like
        A copy of `arg`, localized to the given timezone.  If `tz=None`,
        these will always be UTC times.

    Raises
    ------
    TypeError
        If `tz` is not an IANA-recognized timezone string, a `datetime.tzinfo`
        object, or `None`.

    Examples
    --------
    Inputs can be `pandas.Timestamp` objects:

    >>> localize(pd.Timestamp("2022-01-04 00:00:00"), "US/Pacific")
    Timestamp('2022-01-03 16:00:00-0800', tz='US/Pacific')
    >>> localize(pd.Timestamp("2022-01-04 00:00:00"), "US/Pacific", utc=False)
    Timestamp('2022-01-04 00:00:00-0800', tz='US/Pacific')
    >>> localize(pd.Timestamp("2022-01-04 00:00:00+0000"), "US/Pacific")
    Timestamp('2022-01-03 16:00:00-0800', tz='US/Pacific')

    Or `datetime.datetime` objects:

    >>> localize_pydatetime(
    ...     datetime.datetime.fromisoformat("2022-01-04 00:00:00"),
    ...     "US/Pacific"
    ... )
    datetime.datetime(2022, 1, 3, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
    >>> localize_pydatetime(
    ...     datetime.datetime.fromisoformat("2022-01-04 00:00:00"),
    ...     "US/Pacific",
    ...     utc=False
    ... )
    datetime.datetime(2022, 1, 4, 0, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
    >>> localize_pydatetime(
    ...     datetime.datetime.fromtimestamp(0, datetime.timezone.utc),
    ...     "US/Pacific"
    ... )
    datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)

    And potentially vectorized:

    >>> series = pd.to_datetime(pd.Series([1, 2, 3]), unit="s")
    >>> series
    0   1970-01-01 00:00:01
    1   1970-01-01 00:00:02
    2   1970-01-01 00:00:03
    dtype: datetime64[ns]
    >>> localize(series, "US/Pacific")
    0   1969-12-31 16:00:01-08:00
    1   1969-12-31 16:00:02-08:00
    2   1969-12-31 16:00:03-08:00
    dtype: datetime64[ns, US/Pacific]
    >>> localize(series, "US/Pacific", utc=False)
    0   1970-01-01 00:00:01-08:00
    1   1970-01-01 00:00:02-08:00
    2   1970-01-01 00:00:03-08:00
    dtype: datetime64[ns, US/Pacific]
    >>> array = np.array(
    ...     [datetime.datetime.utcfromtimestamp(i) for i in range(1, 4)]
    ... )
    >>> array
    array([datetime.datetime(1970, 1, 1, 0, 0, 1),
       datetime.datetime(1970, 1, 1, 0, 0, 2),
       datetime.datetime(1970, 1, 1, 0, 0, 3)], dtype=object)
    >>> localize_pydatetime(array, "US/Pacific")
    array([datetime.datetime(1969, 12, 31, 16, 0, 1, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
       datetime.datetime(1969, 12, 31, 16, 0, 2, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
       datetime.datetime(1969, 12, 31, 16, 0, 3, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)],
      dtype=object)
    >>> localize_pydatetime(array, "US/Pacific", utc=False)
    array([datetime.datetime(1970, 1, 1, 0, 0, 1, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
       datetime.datetime(1970, 1, 1, 0, 0, 2, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
       datetime.datetime(1970, 1, 1, 0, 0, 3, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)],
      dtype=object)

    They can even contain mixed datetime representations:

    >>> mixed = np.where(
    ...     [True, False, True],
    ...     series.to_numpy(dtype="O"),
    ...     array
    ... )
    >>> mixed
    array([Timestamp('1970-01-01 00:00:01'),
       datetime.datetime(1970, 1, 1, 0, 0, 2),
       Timestamp('1970-01-01 00:00:03')], dtype=object)
    >>> localize(mixed, "US/Pacific")
    array([Timestamp('1969-12-31 16:00:01-0800', tz='US/Pacific'),
       datetime.datetime(1969, 12, 31, 16, 0, 2, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
       Timestamp('1969-12-31 16:00:03-0800', tz='US/Pacific')],
      dtype=object)
    >>> localize(mixed, "US/Pacific", utc=False)
    array([Timestamp('1970-01-01 00:00:01-0800', tz='US/Pacific'),
       datetime.datetime(1970, 1, 1, 0, 0, 2, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
       Timestamp('1970-01-01 00:00:03-0800', tz='US/Pacific')],
      dtype=object)
    """
    dtype = get_dtype(arg)
    if isinstance(dtype, set) and dtype - valid_datetime_types:
        raise TypeError(f"`arg` must contain only localizable datetime "
                        f"elements, not {dtype - valid_datetime_types}")

    # resolve timezone
    tz = timezone(tz)

    # pd.Timestamp
    if dtype == pd.Timestamp:
        return localize_pandas_timestamp(arg, tz, utc=utc)

    # datetime.datetime
    if dtype == datetime.datetime:
        return localize_pydatetime(arg, tz, utc=utc)

    # mixed pd.Timestamp/datetime.datetime
    if isinstance(dtype, set):
        # pd.Series
        if isinstance(arg, pd.Series):
            index = arg.index
            arg = arg.to_numpy(dtype="O")
            arg = localize_mixed_datetimelike_vector(arg, tz, utc=utc)
            return pd.Series(arg, index=index, copy=False, dtype="O")

        # array
        return localize_mixed_datetimelike_vector(arg, tz, utc=utc)

    # datetime object can't be localized
    raise TypeError(f"could not localize datetime object of type {dtype}")


def timezone(tz: str | datetime.tzinfo | None) -> datetime.tzinfo | None:
    """Convert a timezone specifier into a corresponding timezone object.

    Parameters
    ----------
    tz : str | datetime.tzinfo | None
        A timezone specifier.  This can be an IANA-recognized timezone string
        ('UTC', 'US/Pacific', 'Europe/Berlin', etc.), a subclass of
        `datetime.tzinfo` (from `pytz`, `zoneinfo`, etc.), or `None`,
        indicating a naive timezone.

    Returns
    -------
    datetime.tzinfo | None
        A `datetime.tzinfo` object, which can be used in conjunction with
        `datetime.datetime.astimezone()` and `pandas.Timestamp.tz_convert()`.
        Can also be `None`.

    Raises
    ------
    TypeError
        If `tz` is not an IANA-recognized timezone string, a `datetime.tzinfo`
        object, or `None`.

    Examples
    --------
    Timezone specifiers can be IANA strings:

    >>> timezone("UTC")
    <UTC>
    >>> timezone("US/Pacific")
    <DstTzInfo 'US/Pacific' LMT-1 day, 16:07:00 STD>
    >>> timezone("Europe/Berlin")
    <DstTzInfo 'Europe/Berlin' LMT+0:53:00 STD>

    Or previously instantiated timezone objects:

    >>> timezone(pytz.timezone("Asia/Hong_Kong"))
    <DstTzInfo 'Asia/Hong_Kong' LMT+7:37:00 STD>
    >>> timezone(zoneinfo.ZoneInfo("Australia/Sydney"))
    zoneinfo.ZoneInfo(key='Australia/Sydney')
    >>> timezone(dateutil.tz.gettz('America/Sao_Paulo'))
    tzfile('/usr/share/zoneinfo/America/Sao_Paulo')

    The special value `None` is also accepted, indicating a naive timezone:

    >>> timezone(None)
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
