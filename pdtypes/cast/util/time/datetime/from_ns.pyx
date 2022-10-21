"""Convert integer nanosecond offsets (from the utc epoch) to their
corresponding datetime representation.

Functions
---------
ns_to_pandas_timestamp(
    arg: int | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = None,
    *,
    min_ns: int = None,
    max_ns: int = None
) -> pd.Timestamp | np.ndarray | pd.Series:
    Convert nanosecond offsets into `pandas.Timestamp` objects.

ns_to_pydatetime(
    arg: int | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = None,
    min_ns: int = None,
    max_ns: int = None
) -> datetime.datetime | np.ndarray | pd.Series:
    Convert nanosecond offsets into `datetime.datetime` objects.

ns_to_numpy_datetime64(
    arg: int | np.ndarray | pd.Series,
    unit: str = None,
    rounding: str = "down",
    *,
    min_ns: int = None,
    max_ns: int = None
) -> np.datetime64 | np.ndarray | pd.Series:
    Convert nanosecond offsets into `numpy.datetime64` objects.

ns_to_datetime(
    arg: int | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = None
) -> datetime_like | np.ndarray | pd.Series:
    Convert nanosecond offsets into dynamic datetime objects.

Examples
--------
Converting to `pandas.Timestamp`:

>>> ns_to_pandas_timestamp(0)
Timestamp('1970-01-01 00:00:00')
>>> ns_to_pandas_timestamp(0, tz="UTC")
Timestamp('1970-01-01 00:00:00+0000', tz='UTC')
>>> ns_to_pandas_timestamp(0, tz="US/Pacific")
Timestamp('1969-12-31 16:00:00-0800', tz='US/Pacific')
>>> ns_to_pandas_timestamp(2**63 - 1)
Timestamp('2262-04-11 23:47:16.854775807')
>>> ns_to_pandas_timestamp(2**63)
Traceback (most recent call last):
    ...
OverflowError: `arg` exceeds pd.Timestamp range
>>> ns_to_pandas_timestamp(2**63 - 1, tz="US/Pacific")
Timestamp('2262-04-11 15:47:16.854775807-0800', tz='US/Pacific')
>>> ns_to_pandas_timestamp(2**63 - 1, tz="Europe/Berlin")
Traceback (most recent call last):
    ...
OverflowError: localizing to Europe/Berlin causes `arg` to exceed pd.Timestamp range
>>> ns_to_pandas_timestamp(np.arange(1, 4))
array([Timestamp('1970-01-01 00:00:00.000000001'),
    Timestamp('1970-01-01 00:00:00.000000002'),
    Timestamp('1970-01-01 00:00:00.000000003')], dtype=object)
>>> ns_to_pandas_timestamp(pd.Series([1, 2, 3]))
0   1970-01-01 00:00:00.000000001
1   1970-01-01 00:00:00.000000002
2   1970-01-01 00:00:00.000000003
dtype: datetime64[ns]

Converting to `datetime.datetime`:

>>> ns_to_pydatetime(0)
datetime.datetime(1970, 1, 1, 0, 0)
>>> ns_to_pydatetime(0, tz="UTC")
datetime.datetime(1970, 1, 1, 0, 0, tzinfo=datetime.timezone.utc)
>>> ns_to_pydatetime(0, tz="US/Pacific")
datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
>>> ns_to_pydatetime(253402300799999999000)
datetime.datetime(9999, 12, 31, 23, 59, 59, 999999)
>>> ns_to_pydatetime(253402300799999999000 + 1)
Traceback (most recent call last):
    ...
OverflowError: `arg` exceeds datetime.datetime range

Look out for localization-induced overflow:
>>> ns_to_pydatetime(253402300799999999000, tz="US/Pacific")
datetime.datetime(9999, 12, 31, 15, 59, 59, 999999, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
>>> ns_to_pydatetime(253402300799999999000, tz="Europe/Berlin")
Traceback (most recent call last):
    ...
OverflowError: localizing to Europe/Berlin causes `arg` to exceed datetime.datetime range
>>> ns_to_pydatetime(np.arange(1, 4))
array([datetime.datetime(1970, 1, 1, 0, 0),
    datetime.datetime(1970, 1, 1, 0, 0),
    datetime.datetime(1970, 1, 1, 0, 0)], dtype=object)
>>> ns_to_pydatetime(pd.Series([1, 2, 3]))
0    1970-01-01 00:00:00
1    1970-01-01 00:00:00
2    1970-01-01 00:00:00
dtype: object

Converting to `numpy.datetime64`:

>>> ns_to_numpy_datetime64(2**63 - 1, unit="ns")
numpy.datetime64('2262-04-11T23:47:16.854775807')
>>> ns_to_numpy_datetime64(2**63 - 1, unit="us")
numpy.datetime64('2262-04-11T23:47:16.854775')
>>> ns_to_numpy_datetime64(2**63 - 1, unit="ms")
numpy.datetime64('2262-04-11T23:47:16.854')
>>> ns_to_numpy_datetime64(2**63 - 1, unit="s")
numpy.datetime64('2262-04-11T23:47:16')
>>> ns_to_numpy_datetime64(2**63 - 1, unit="m")
numpy.datetime64('2262-04-11T23:47')
>>> ns_to_numpy_datetime64(2**63 - 1, unit="h")
numpy.datetime64('2262-04-11T23','h')
>>> ns_to_numpy_datetime64(2**63 - 1, unit="D")
numpy.datetime64('2262-04-11')
>>> ns_to_numpy_datetime64(2**63 - 1, unit="W")
numpy.datetime64('2262-04-10')
>>> ns_to_numpy_datetime64(2**63 - 1, unit="M")
numpy.datetime64('2262-04')
>>> ns_to_numpy_datetime64(2**63 - 1, unit="Y")
numpy.datetime64('2262')
>>> ns_to_numpy_datetime64(0)
numpy.datetime64('1970-01-01T00:00:00.000000000')
>>> ns_to_numpy_datetime64(2**65)
numpy.datetime64('3139-02-09T23:09:07.419103')
>>> ns_to_numpy_datetime64(2**65, rounding="up")
numpy.datetime64('3139-02-09T23:09:07.419104')
>>> ns_to_numpy_datetime64(2**65, unit="s", rounding="up")
numpy.datetime64('3139-02-09T23:09:08')
>>> ns_to_numpy_datetime64(np.arange(1, 4))
array(['1970-01-01T00:00:00.000000001', '1970-01-01T00:00:00.000000002',
    '1970-01-01T00:00:00.000000003'], dtype='datetime64[ns]')
>>> ns_to_numpy_datetime64(pd.Series([1, 2, 3]))
0    1970-01-01T00:00:00.000000001
1    1970-01-01T00:00:00.000000002
2    1970-01-01T00:00:00.000000003
dtype: object

Or to arbitrary datetime objects:

>>> ns_to_datetime(2**63 - 1)
Timestamp('2262-04-11 23:47:16.854775807')
>>> ns_to_datetime(2**63)
datetime.datetime(2262, 4, 11, 23, 47, 16, 854775)
>>> ns_to_datetime(253402300799999999000)
datetime.datetime(9999, 12, 31, 23, 59, 59, 999999)
>>> ns_to_datetime(253402300799999999000 + 1)
numpy.datetime64('9999-12-31T23:59:59.999999')
>>> ns_to_datetime(2**63 - 1, tz="US/Pacific")
Timestamp('2262-04-11 15:47:16.854775807-0800', tz='US/Pacific')
>>> ns_to_datetime(2**63 - 1, tz="Europe/Berlin")
datetime.datetime(2262, 4, 12, 0, 47, 16, 854775, tzinfo=<DstTzInfo 'Europe/Berlin' CET+1:00:00 STD>)
>>> ns_to_datetime(np.arange(1, 4))
array(['1970-01-01T00:00:00.000000001', '1970-01-01T00:00:00.000000002',
    '1970-01-01T00:00:00.000000003'], dtype='datetime64[ns]')
>>> ns_to_datetime(pd.Series([1, 2, 3]))
0   1970-01-01 00:00:00.000000001
1   1970-01-01 00:00:00.000000002
2   1970-01-01 00:00:00.000000003
dtype: datetime64[ns]
"""
import datetime
from cpython cimport datetime

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes.util.type_hints import datetime_like

from ..timezone import is_utc, localize_pandas_timestamp, timezone
from ..timezone cimport utc_timezones
from ..unit import convert_unit_integer
from ..unit cimport as_ns, valid_units


# TODO: string_to_datetime with ISO strings doesn't have proper localization
# due to this module.
# -> add utc flag to ns_to_pandas_timestamp and ns_to_pydatetime
# -> utc flag fucks up parsing of aware ISO 8601 datetime strings

# Modify iso 8601 to ns to also return whether a UTC offset was encountered?
# -> then do an if check on the localization to determine if it is utc
# -> then remove `utc` arg in ns_to_datetime


#########################
####    Constants    ####
#########################


# Unix/POSIX epoch time (naive/aware, datetime.datetime)
cdef object utc_naive_pydatetime = datetime.datetime.utcfromtimestamp(0)
cdef object utc_aware_pydatetime
utc_aware_pydatetime = datetime.datetime.fromtimestamp(0, datetime.timezone.utc)


# min/max representable ns values for each datetime type
cdef long int min_pandas_timestamp_ns = -2**63 + 1
cdef long int max_pandas_timestamp_ns = 2**63 - 1
cdef object min_pydatetime_ns = -62135596800000000000
cdef object max_pydatetime_ns = 253402300799999999000
cdef object min_numpy_datetime64_ns = -291061508645168391112243200000000000
cdef object max_numpy_datetime64_ns = 291061508645168328945024000000000000


#######################
####    Private    ####
#######################


cdef inline object ns_to_pydatetime_scalar(
    object ns,
    datetime.tzinfo tz
):
    """Convert a scalar nanosecond offset into a properly-localized
    `datetime.datetime` object.
    """
    cdef object offset = datetime.timedelta(microseconds=int(ns) // as_ns["us"])

    # return naive
    if tz is None:
        return utc_naive_pydatetime + offset

    # return aware (utc)
    if tz in utc_timezones:
        return utc_aware_pydatetime + offset

    # return aware (non-utc)
    return (utc_aware_pydatetime + offset).astimezone(tz)


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] ns_to_pydatetime_vector(
    np.ndarray arr,
    datetime.tzinfo tz
):
    """Convert an array of nanosecond offsets into properly-localized
    `datetime.datetime` objects.
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        result[i] = ns_to_pydatetime_scalar(arr[i], tz=tz)

    return result


######################
####    Public    ####
######################


def ns_to_pandas_timestamp(
    arg: int | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = None,
    *,
    min_ns: int = None,
    max_ns: int = None
) -> pd.Timestamp | np.ndarray | pd.Series:
    """Convert nanosecond offsets into `pandas.Timestamp` objects with the
    given timezone.

    Parameters
    ----------
    arg : int | array-like
        An integer or vector of integers representing nanosecond offsets from
        the utc epoch ('1970-01-01 00:00:00+0000').
    tz : str | datetime.tzinfo, default None
        An IANA timezone string ('US/Pacific', 'Europe/Berlin', etc.) or a
        `datetime.tzinfo`-compatible timezone offset (`pytz`, `zoneinfo`,
        etc.).  Can also be `None`, indicating a naive ('UTC') return type.
    min_ns : int, default None
        The minimum value stored in `arg`.  If given, this will be used for
        range checks directly, avoiding a call to `.min()`.  Keyword-only.
    max_ns : int, default None
        The maximum value stored in `arg`.  If given, this will be used for
        range checks directly, avoiding a call to `.max()`. Keyword-only.

    Returns
    -------
    pandas.Timestamp | array-like
        A `pandas.Timestamp` or vector of such objects containing the datetime
        equivalents of the given nanosecond offsets, localized to `tz`.

    Raises
    ------
    OverflowError
        If the range of `arg` exceeds the representable range of
        `pandas.Timestamp` objects ([`'1677-09-21 00:12:43.145224193'` -
        `'2262-04-11 23:47:16.854775807'`]), or if localizing to the given
        timezone would overflow past that range.

    Examples
    --------
    Nanosecond offsets are always interpreted from the UTC epoch:

    >>> ns_to_pandas_timestamp(0)
    Timestamp('1970-01-01 00:00:00')
    >>> ns_to_pandas_timestamp(0, tz="UTC")
    Timestamp('1970-01-01 00:00:00+0000', tz='UTC')
    >>> ns_to_pandas_timestamp(0, tz="US/Pacific")
    Timestamp('1969-12-31 16:00:00-0800', tz='US/Pacific')

    And they can be up to 64 bits wide:

    >>> ns_to_pandas_timestamp(2**63 - 1)
    Timestamp('2262-04-11 23:47:16.854775807')
    >>> ns_to_pandas_timestamp(2**63)
    Traceback (most recent call last):
        ...
    OverflowError: `arg` exceeds pd.Timestamp range

    Look out for localization-induced overflow:

    >>> ns_to_pandas_timestamp(2**63 - 1, tz="US/Pacific")
    Timestamp('2262-04-11 15:47:16.854775807-0800', tz='US/Pacific')
    >>> ns_to_pandas_timestamp(2**63 - 1, tz="Europe/Berlin")
    Traceback (most recent call last):
        ...
    OverflowError: localizing to Europe/Berlin causes `arg` to exceed pd.Timestamp range

    Offsets can also be vectorized:

    >>> ns_to_pandas_timestamp(np.arange(1, 4))
    array([Timestamp('1970-01-01 00:00:00.000000001'),
       Timestamp('1970-01-01 00:00:00.000000002'),
       Timestamp('1970-01-01 00:00:00.000000003')], dtype=object)
    >>> ns_to_pandas_timestamp(pd.Series([1, 2, 3]))
    0   1970-01-01 00:00:00.000000001
    1   1970-01-01 00:00:00.000000002
    2   1970-01-01 00:00:00.000000003
    dtype: datetime64[ns]
    """
    # ensure min/max fall within representable range
    is_array_like = isinstance(arg, (np.ndarray, pd.Series))
    if min_ns is None:
        min_ns = int(arg.min()) if is_array_like else int(arg)
    if max_ns is None:
        max_ns = int(arg.max()) if is_array_like else int(arg)
    if min_ns < min_pandas_timestamp_ns or max_ns > max_pandas_timestamp_ns:
        raise OverflowError(f"`arg` exceeds pd.Timestamp range")

    # resolve timezone
    tz = timezone(tz)

    # convert using pd.to_datetime, accounting for timezone
    original_type = type(arg)
    if tz is None:
        arg = pd.to_datetime(arg, unit="ns")
    else:
        arg = pd.to_datetime(arg, unit="ns", utc=True)
        if not is_utc(tz):
            try:  # localization can overflow if close to min/max timestamp
                if issubclass(original_type, pd.Series):  # use `.dt` namespace
                    arg = arg.dt.tz_convert(tz)
                else:
                    arg = arg.tz_convert(tz)
            except pd._libs.tslibs.np_datetime.OutOfBoundsDatetime as err:
                err_msg = (f"localizing to {repr(str(tz))} causes `arg` to "
                           f"exceed pd.Timestamp range")
                raise OverflowError(err_msg) from err

    # convert array inputs back to array form (rather than DatetimeIndex)
    if issubclass(original_type, np.ndarray):
        return arg.to_numpy(dtype="O")  # slow

    return arg


def ns_to_pydatetime(
    arg: int | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = None,
    *,
    min_ns: int = None,
    max_ns: int = None
) -> datetime.datetime | np.ndarray | pd.Series:
    """Convert nanosecond offsets into `datetime.datetime` objects with the
    given timezone.

    Parameters
    ----------
    arg : int | array-like
        An integer or vector of integers representing nanosecond offsets from
        the utc epoch ('1970-01-01 00:00:00+0000').
    tz : str | datetime.tzinfo, default None
        An IANA timezone string ('US/Pacific', 'Europe/Berlin', etc.) or a
        `datetime.tzinfo`-compatible timezone offset (`pytz`, `zoneinfo`,
        etc.).  Can also be `None`, indicating a naive ('UTC') return type.
    min_ns : int, default None
        The minimum value stored in `arg`.  If given, this will be used for
        range checks directly, avoiding a call to `.min()`. Keyword-only.
    max_ns : int, default None
        The maximum value stored in `arg`.  If given, this will be used for
        range checks directly, avoiding a call to `.max()`.  Keyword-only.

    Returns
    -------
    datetime.datetime | array-like
        A `datetime.datetime` or vector of such objects containing the datetime
        equivalents of the given nanosecond offsets, localized to `tz`.

    Raises
    ------
    OverflowError
        If the range of `arg` exceeds the representable range of
        `datetime.datetime` objects ([`'0001-01-01 00:00:00'` -
        `'9999-12-31 23:59:59.999999'`]), or if localizing to the given
        timezone would overflow past that range.

    Examples
    --------
    Nanosecond offsets are always interpreted from the UTC epoch:

    >>> ns_to_pydatetime(0)
    datetime.datetime(1970, 1, 1, 0, 0)
    >>> ns_to_pydatetime(0, tz="UTC")
    datetime.datetime(1970, 1, 1, 0, 0, tzinfo=datetime.timezone.utc)
    >>> ns_to_pydatetime(0, tz="US/Pacific")
    datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)

    And they can be up to the maximum range of `datetime.datetime` objects:

    >>> ns_to_pydatetime(253402300799999999000)
    datetime.datetime(9999, 12, 31, 23, 59, 59, 999999)
    >>> ns_to_pydatetime(253402300799999999000 + 1)
    Traceback (most recent call last):
        ...
    OverflowError: `arg` exceeds datetime.datetime range

    Look out for localization-induced overflow:

    >>> ns_to_pydatetime(253402300799999999000, tz="US/Pacific")
    datetime.datetime(9999, 12, 31, 15, 59, 59, 999999, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
    >>> ns_to_pydatetime(253402300799999999000, tz="Europe/Berlin")
    Traceback (most recent call last):
        ...
    OverflowError: localizing to Europe/Berlin causes `arg` to exceed datetime.datetime range

    Offsets can also be vectorized:

    >>> ns_to_pydatetime(np.arange(1, 4))
    array([datetime.datetime(1970, 1, 1, 0, 0),
       datetime.datetime(1970, 1, 1, 0, 0),
       datetime.datetime(1970, 1, 1, 0, 0)], dtype=object)
    >>> ns_to_pydatetime(pd.Series([1, 2, 3]))
    0    1970-01-01 00:00:00
    1    1970-01-01 00:00:00
    2    1970-01-01 00:00:00
    dtype: object
    """
    # ensure min/max fall within representable range
    is_array_like = isinstance(arg, (np.ndarray, pd.Series))
    if min_ns is None:
        min_ns = int(arg.min()) if is_array_like else int(arg)
    if max_ns is None:
        max_ns = int(arg.max()) if is_array_like else int(arg)
    if min_ns < min_pydatetime_ns or max_ns > max_pydatetime_ns:
        raise OverflowError(f"`arg` exceeds datetime.datetime range")

    # resolve timezone
    tz = timezone(tz)

    # localization can cause overflow if close to min/max datetime
    overflow_msg = (f"localizing to {tz} causes `arg` to exceed "
                    f"datetime.datetime range")

    # np.ndarray
    if isinstance(arg, np.ndarray):
        if tz is None:  # fastpath for naive utc datetimes
            arg = arg // as_ns["us"]
            return arg.astype("M8[us]").astype("O")
        try:
            return ns_to_pydatetime_vector(arg, tz=tz)
        except OverflowError as err:
            raise OverflowError(overflow_msg) from err

    # pd.Series
    if isinstance(arg, pd.Series):
        index = arg.index
        arg = arg.to_numpy()
        if tz is None:  # fastpath for naive utc datetimes
            arg = arg // as_ns["us"]
            arg = arg.astype("M8[us]").astype("O")
        else:
            try:
                arg = ns_to_pydatetime_vector(arg, tz=tz)
            except OverflowError as err:
                raise OverflowError(overflow_msg) from err
        return pd.Series(arg, index=index, copy=False, dtype="O")

    # scalar
    try:
        return ns_to_pydatetime_scalar(arg, tz=tz)
    except OverflowError as err:
        raise OverflowError(overflow_msg) from err


def ns_to_numpy_datetime64(
    arg: int | np.ndarray | pd.Series,
    unit: str = None,
    rounding: str = "down",
    *,
    min_ns: int = None,
    max_ns: int = None
) -> np.datetime64 | np.ndarray | pd.Series:
    """Convert nanosecond offsets into `numpy.datetime64` objects with the
    given unit.

    Parameters
    ----------
    arg : int | array-like
        An integer or vector of integers representing nanosecond offsets from
        the utc epoch ('1970-01-01 00:00:00+0000').
    unit : {'ns', 'us', 'ms', 's', 'm', 'h', 'D', 'W', 'M', 'Y'}, default None
        The unit to use for the returned datetimes.  If `None`, choose the
        highest resolution unit that can fully represent `arg`.
    rounding : str, default 'down'
        The rounding strategy to use when a value underflows beyond the
        resolution of `unit`.  Defaults to `'down'` (round toward zero).
    min_ns : int, default None
        The minimum value stored in `arg`.  If given, this will be used for
        range checks directly, avoiding a call to `.min()`.  Keyword-only.
    max_ns : int, default None
        The maximum value stored in `arg`.  If given, this will be used for
        range checks directly, avoiding a call to `.max()`.  Keyword-only.

    Returns
    -------
    numpy.datetime64 | array-like
        A `numpy.datetime64` or vector of such objects containing the datetime
        equivalents of the given nanosecond offsets, with the specified `unit`.

    Raises
    ------
    OverflowError
        If the range of `arg` exceeds the representable range of
        `numpy.datetime64` objects with the given `unit` (up to
        [`'-9223372036854773837-01-01 00:00:00'` - 
        `'9223372036854775807-01-01 00:00:00'`]).

    Examples
    --------
    Units can be any of those recognized by `numpy.datetime64` objects:

    >>> ns_to_numpy_datetime64(2**63 - 1, unit="ns")
    numpy.datetime64('2262-04-11T23:47:16.854775807')
    >>> ns_to_numpy_datetime64(2**63 - 1, unit="us")
    numpy.datetime64('2262-04-11T23:47:16.854775')
    >>> ns_to_numpy_datetime64(2**63 - 1, unit="ms")
    numpy.datetime64('2262-04-11T23:47:16.854')
    >>> ns_to_numpy_datetime64(2**63 - 1, unit="s")
    numpy.datetime64('2262-04-11T23:47:16')
    >>> ns_to_numpy_datetime64(2**63 - 1, unit="m")
    numpy.datetime64('2262-04-11T23:47')
    >>> ns_to_numpy_datetime64(2**63 - 1, unit="h")
    numpy.datetime64('2262-04-11T23','h')
    >>> ns_to_numpy_datetime64(2**63 - 1, unit="D")
    numpy.datetime64('2262-04-11')
    >>> ns_to_numpy_datetime64(2**63 - 1, unit="W")
    numpy.datetime64('2262-04-10')
    >>> ns_to_numpy_datetime64(2**63 - 1, unit="M")
    numpy.datetime64('2262-04')
    >>> ns_to_numpy_datetime64(2**63 - 1, unit="Y")
    numpy.datetime64('2262')

    Units can also be automatically inferred from the data:

    >>> ns_to_numpy_datetime64(0)
    numpy.datetime64('1970-01-01T00:00:00.000000000')
    >>> ns_to_numpy_datetime64(2**65)
    numpy.datetime64('3139-02-09T23:09:07.419103')

    With customizeable rounding:

    >>> ns_to_numpy_datetime64(2**65, rounding="up")
    numpy.datetime64('3139-02-09T23:09:07.419104')
    >>> ns_to_numpy_datetime64(2**65, unit="s", rounding="up")
    numpy.datetime64('3139-02-09T23:09:08')

    Offsets can also be vectorized:

    >>> ns_to_numpy_datetime64(np.arange(1, 4))
    array(['1970-01-01T00:00:00.000000001', '1970-01-01T00:00:00.000000002',
       '1970-01-01T00:00:00.000000003'], dtype='datetime64[ns]')
    >>> ns_to_numpy_datetime64(pd.Series([1, 2, 3]))
    0    1970-01-01T00:00:00.000000001
    1    1970-01-01T00:00:00.000000002
    2    1970-01-01T00:00:00.000000003
    dtype: object
    """
    # ensure min/max fall within representable range
    is_array_like = isinstance(arg, (np.ndarray, pd.Series))
    if min_ns is None:
        min_ns = int(arg.min()) if is_array_like else int(arg)
    if max_ns is None:
        max_ns = int(arg.max()) if is_array_like else int(arg)
    if min_ns < min_numpy_datetime64_ns or max_ns > max_numpy_datetime64_ns:
        raise OverflowError(f"`arg` exceeds np.datetime64 range")

    # kwargs for convert_unit_integer
    unit_kwargs = {"since": utc_aware_pydatetime, "rounding": rounding}

    # if `unit` is already defined, rescale `arg` to match
    if unit is not None:
        min_ns = convert_unit_integer(min_ns, "ns", unit, **unit_kwargs)
        max_ns = convert_unit_integer(max_ns, "ns", unit, **unit_kwargs)

        # get allowable limits for given unit
        lower_lim = -2**63 + 1
        upper_lim = 2**63 - 1
        if unit == "W":
            lower_lim //= 7  # wierd overflow behavior related to weeks
            upper_lim //= 7
        elif unit == "Y":
            upper_lim -= 1970  # appears to be utc offset

        # check for overflow
        if min_ns < lower_lim or max_ns > upper_lim:
            raise OverflowError(f"`arg` exceeds np.datetime64 range with "
                                f"unit={repr(unit)}")
        arg = convert_unit_integer(arg, "ns", unit, **unit_kwargs)

    else:  # choose unit to fit range and rescale `arg` appropriately
        for test_unit in valid_units:
            if test_unit == "W":  # skip weeks
                # this unit has some really wierd (inconsistent) overflow
                # behavior that makes it practically useless over unit="D"
                continue

            # convert min/max to test unit
            test_min = convert_unit_integer(
                min_ns,
                "ns",
                test_unit,
                **unit_kwargs
            )
            test_max = convert_unit_integer(
                max_ns,
                "ns",
                test_unit,
                **unit_kwargs
            )

            # get allowable limits for test unit
            lower_lim = -2**63 + 1
            upper_lim = 2**63 - 1
            if unit == "W":
                lower_lim //= 7  # wierd overflow behavior related to weeks
                upper_lim //= 7
            elif unit == "Y":
                upper_lim -= 1970  # appears to be utc offset

            # check for overflow
            if test_min >= lower_lim and test_max <= upper_lim:
                unit = test_unit
                arg = convert_unit_integer(arg, "ns", test_unit, **unit_kwargs)
                break  # stop at highest resolution

    # np.ndarray
    if isinstance(arg, np.ndarray):
        return arg.astype(f"M8[{unit}]")

    # pd.Series
    if isinstance(arg, pd.Series):
        index = arg.index
        arg = arg.to_numpy().astype(f"M8[{unit}]")
        return pd.Series(list(arg), index=index, dtype="O")

    # scalar
    return np.datetime64(int(arg), unit)


def ns_to_datetime(
    arg: int | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = None,
    *,
    min_ns: int = None,
    max_ns: int = None
) -> datetime_like | np.ndarray | pd.Series:
    """Convert nanosecond offsets into datetime objects with the given
    timezone.

    Parameters
    ----------
    arg : int | array-like
        An integer or vector of integers representing nanosecond offsets from
        the utc epoch ('1970-01-01 00:00:00+0000').
    tz : str | datetime.tzinfo, default None
        An IANA timezone string ('US/Pacific', 'Europe/Berlin', etc.) or a
        `datetime.tzinfo`-compatible timezone offset (`pytz`, `zoneinfo`,
        etc.).  Can also be `None`, indicating a naive ('UTC') return type.
    min_ns : int, default None
        The minimum value stored in `arg`.  If given, this will be used for
        range checks directly, avoiding a call to `.min()`.  Keyword-only.
    max_ns : int, default None
        The maximum value stored in `arg`.  If given, this will be used for
        range checks directly, avoiding a call to `.max()`.  Keyword-only.

    Returns
    -------
    datetime-like | array-like
        A datetime or vector of such objects containing the datetime
        equivalents of the given nanosecond offsets, localized to `tz`.

    Raises
    ------
    OverflowError
        If the range of `arg` exceeds the representable range of
        `numpy.datetime64` objects ([`'-9223372036854773837-01-01 00:00:00'` - 
        `'9223372036854775807-01-01 00:00:00'`]).
    RuntimeError
        If `tz` is specified and is not utc, and the range of `arg` exceeds
        `datetime.datetime` range ([`'0001-01-01 00:00:00'` -
        `'9999-12-31 23:59:59.999999'`]).

    Examples
    --------
    Transitions between datetime regimes are handled gracefully:

    >>> ns_to_datetime(2**63 - 1)
    Timestamp('2262-04-11 23:47:16.854775807')
    >>> ns_to_datetime(2**63)
    datetime.datetime(2262, 4, 11, 23, 47, 16, 854775)
    >>> ns_to_datetime(253402300799999999000)
    datetime.datetime(9999, 12, 31, 23, 59, 59, 999999)
    >>> ns_to_datetime(253402300799999999000 + 1)
    numpy.datetime64('9999-12-31T23:59:59.999999')

    And can be localized if they fall within `pandas.Timestamp` or
    `datetime.datetime` range:

    >>> ns_to_datetime(2**63 - 1, tz="US/Pacific")
    Timestamp('2262-04-11 15:47:16.854775807-0800', tz='US/Pacific')
    >>> ns_to_datetime(2**63 - 1, tz="Europe/Berlin")
    datetime.datetime(2262, 4, 12, 0, 47, 16, 854775, tzinfo=<DstTzInfo 'Europe/Berlin' CET+1:00:00 STD>)

    And potentially vectorized:

    >>> ns_to_datetime(np.arange(1, 4))
    array(['1970-01-01T00:00:00.000000001', '1970-01-01T00:00:00.000000002',
       '1970-01-01T00:00:00.000000003'], dtype='datetime64[ns]')
    >>> ns_to_datetime(pd.Series([1, 2, 3]))
    0   1970-01-01 00:00:00.000000001
    1   1970-01-01 00:00:00.000000002
    2   1970-01-01 00:00:00.000000003
    dtype: datetime64[ns]
    """
    # ensure min/max fall within representable range
    is_array_like = isinstance(arg, (np.ndarray, pd.Series))
    if min_ns is None:
        min_ns = int(arg.min()) if is_array_like else int(arg)
    if max_ns is None:
        max_ns = int(arg.max()) if is_array_like else int(arg)

    # resolve timezone and check if utc
    tz = timezone(tz)

    # if `arg` is a numpy array and `tz` is utc, skip straight to np.datetime64
    if isinstance(arg, np.ndarray) and (not tz or is_utc(tz)):
        return ns_to_numpy_datetime64(arg, min_ns=min_ns, max_ns=max_ns)

    # pd.Timestamp
    try:
        return ns_to_pandas_timestamp(arg, tz, min_ns=min_ns, max_ns=max_ns)
    except OverflowError:
        pass

    # datetime.datetime
    try:
        return ns_to_pydatetime(arg, tz, min_ns=min_ns, max_ns=max_ns)
    except OverflowError:
        pass

    # np.datetime64
    if tz and not is_utc(tz):
        err_msg = ("`numpy.datetime64` objects do not carry timezone "
                   "information (must be utc)")
        raise RuntimeError(err_msg)
    return ns_to_numpy_datetime64(arg)
