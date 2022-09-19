"""Convert integer nanosecond offsets (from utc epoch) to their corresponding
datetime representation.

Functions
---------
    ns_to_pandas_timestamp(
        arg: int | np.ndarray | pd.Series,
        tz: str | datetime.tzinfo | None = None,
        *,
        min_ns: int = None,
        max_ns: int = None
    ) -> pd.Timestamp | np.ndarray | pd.Series:
        Convert nanosecond offsets into `pandas.Timestamp` objects.

    ns_to_pydatetime(
        arg: int | np.ndarray | pd.Series,
        tz: str | datetime.tzinfo | None = None,
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
        tz: str | datetime.tzinfo | None = None
    ) -> datetime_like | np.ndarray | pd.Series:
        Convert nanosecond offsets into dynamic datetime objects.

Examples
--------
    >>> ns_to_pandas_timestamp(0)
    >>> ns_to_pandas_timestamp(0, tz="UTC")
    >>> ns_to_pandas_timestamp(0, tz="US/Pacific")
    >>> ns_to_pandas_timestamp(2**63 - 1)
    >>> ns_to_pandas_timestamp(2**63 - 1, tz="US/Pacific")

    >>> ns_to_pydatetime(0)
    >>> ns_to_pydatetime(0, tz="UTC")
    >>> ns_to_pydatetime(0, tz="US/Pacific")
    >>> ns_to_pydatetime(253402300799999999000)
    >>> ns_to_pydatetime(253402300799999999000, tz="US/Pacific")

    >>> ns_to_numpy_datetime64(0)
    >>> ns_to_numpy_datetime64(2**63 - 1)
    >>> ns_to_numpy_datetime64(2**63 - 1, unit="us")
    >>> ns_to_numpy_datetime64(2**63 - 1, unit="ms")
    >>> ns_to_numpy_datetime64(2**63 - 1, unit="s")
    >>> ns_to_numpy_datetime64(2**63 - 1, unit="m")
    >>> ns_to_numpy_datetime64(2**63 - 1, unit="h")
    >>> ns_to_numpy_datetime64(2**63 - 1, unit="D")
    >>> ns_to_numpy_datetime64(2**63 - 1, unit="W")
    >>> ns_to_numpy_datetime64(2**63 - 1, unit="M")
    >>> ns_to_numpy_datetime64(2**63 - 1, unit="Y")
    >>> ns_to_numpy_datetime64(2**65)
    >>> ns_to_numpy_datetime64(2**65, rounding="up")
    >>> ns_to_numpy_datetime64(2**65, unit="s", rounding="up")

    >>> ns_to_datetime(2**63 - 1)
    >>> ns_to_datetime(2**63)
    >>> ns_to_pydatetime(253402300799999999000)
    >>> ns_to_pydatetime(253402300799999999000 + 1)
    >>> ns_to_datetime(2**63 - 1, tz="US/Pacific")
    >>> ns_to_datetime(2**63 - 1, tz="Europe/Berlin")
"""
import datetime
from cpython cimport datetime

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes.util.type_hints import datetime_like

from ..timezone import is_utc, timezone
from ..timezone cimport utc_timezones
from ..unit import convert_unit_integer
from ..unit cimport as_ns, valid_units


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

    # naive
    if tz is None:
        return utc_naive_pydatetime + offset

    # aware (utc)
    if tz in utc_timezones:
        return utc_aware_pydatetime + offset

    # aware (non-utc)
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
    tz: str | datetime.tzinfo | None = None,
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
    tz : str | datetime.tzinfo | None
        An IANA timezone string ('US/Pacific', 'Europe/Berlin', etc.) or a
        `datetime.tzinfo`-compatible timezone offset (`pytz`, `zoneinfo`,
        etc.).  Can also be `None`, indicating a naive ('UTC') return type.
        Defaults to `None`.
    min_ns : int
        The minimum value stored in `arg`.  If given, this will be used for
        range checks directly, avoiding a call to `.min()`.  Optional.
        Keyword-only. Defaults to `None`.
    max_ns : int
        The maximum value stored in `arg`.  If given, this will be used for
        range checks directly, avoiding a call to `.max()`.  Optional.
        Keyword-only. Defaults to `None`.

    Returns
    -------
    pandas.Timestamp | array-like
        A `pandas.Timestamp` or vector of `pandas.Timestamp` objects containing
        the datetime equivalents of the given nanosecond offsets, localized to
        `tz`.

    Raises
    ------
    OverflowError
        If the range of `arg` exceeds the representable range of
        `pandas.Timestamp` objects ([`'1677-09-21 00:12:43.145224193'` -
        `'2262-04-11 23:47:16.854775807'`]), or if localizing to the given
        timezone would overflow past that range.

    Examples
    --------
        >>> ns_to_pandas_timestamp(0)
        >>> ns_to_pandas_timestamp(0, tz="UTC")
        >>> ns_to_pandas_timestamp(0, tz="US/Pacific")

        >>> ns_to_pandas_timestamp(2**63 - 1)
        >>> ns_to_pandas_timestamp(2**63)

        >>> ns_to_pandas_timestamp(2**63 - 1, tz="US/Pacific")
        >>> ns_to_pandas_timestamp(2**63 - 1, tz="Europe/Berlin")
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
                if issubclass(original_type, pd.Series):
                    arg = arg.dt.tz_convert(tz)  # use `.dt` namespace
                else:
                    arg = arg.tz_convert(tz)
            except pd._libs.tslibs.np_datetime.OutOfBoundsDatetime as err:
                err_msg = (f"localizing to {tz} causes `arg` to exceed "
                           f"pd.Timestamp range")
                raise OverflowError(err_msg) from err

    # convert array inputs back to array form (rather than DatetimeIndex)
    if issubclass(original_type, np.ndarray):
        return arg.to_numpy(dtype="O")  # slow

    return arg


def ns_to_pydatetime(
    arg: int | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo | None = None,
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
    tz : str | datetime.tzinfo | None
        An IANA timezone string ('US/Pacific', 'Europe/Berlin', etc.) or a
        `datetime.tzinfo`-compatible timezone offset (`pytz`, `zoneinfo`,
        etc.).  Can also be `None`, indicating a naive ('UTC') return type.
        Defaults to `None`.
    min_ns : int
        The minimum value stored in `arg`.  If given, this will be used for
        range checks directly, avoiding a call to `.min()`.  Optional.
        Keyword-only. Defaults to `None`.
    max_ns : int
        The maximum value stored in `arg`.  If given, this will be used for
        range checks directly, avoiding a call to `.max()`.  Optional.
        Keyword-only.  Defaults to `None`.

    Returns
    -------
    datetime.datetime | array-like
        A `datetime.datetime` or vector of `datetime.datetime` objects
        containing the datetime equivalents of the given nanosecond offsets,
        localized to `tz`.

    Raises
    ------
    OverflowError
        If the range of `arg` exceeds the representable range of
        `datetime.datetime` objects ([`'0001-01-01 00:00:00'` -
        `'9999-12-31 23:59:59.999999'`]), or if localizing to the given
        timezone would overflow past that range.

    Examples
    --------
        >>> ns_to_pydatetime(0)
        >>> ns_to_pydatetime(0, tz="UTC")
        >>> ns_to_pydatetime(0, tz="US/Pacific")

        >>> ns_to_pydatetime(253402300799999999000)
        >>> ns_to_pydatetime(253402300799999999000 + 1)

        >>> ns_to_pydatetime(253402300799999999000, tz="US/Pacific")
        >>> ns_to_pydatetime(253402300799999999000, tz="Europe/Berlin")
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
            return ns_to_pydatetime_vector(arg, tz)
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
                arg = ns_to_pydatetime_vector(arg, tz)
            except OverflowError as err:
                raise OverflowError(overflow_msg) from err
        return pd.Series(arg, index=index, copy=False, dtype="O")

    # scalar
    try:
        return ns_to_pydatetime_scalar(arg, tz)
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
    unit : None | {'ns', 'us', 'ms', 's', 'm', 'h', 'D', 'W', 'M', 'Y'}
        The unit to use for the returned datetimes.  If `None`, choose the
        highest resolution unit that can fully represent `arg`.  Defaults to
        `None`.
    rounding : str
        The rounding strategy to use when a value underflows beyond the
        resolution of `unit`.  Defaults to `'down'` (round toward zero).
    min_ns : int
        The minimum value stored in `arg`.  If given, this will be used for
        range checks directly, avoiding a call to `.min()`.  Optional.
        Keyword-only. Defaults to `None`.
    max_ns : int
        The maximum value stored in `arg`.  If given, this will be used for
        range checks directly, avoiding a call to `.max()`.  Optional.
        Keyword-only.  Defaults to `None`.

    Returns
    -------
    numpy.datetime64 | array-like
        A `numpy.datetime64` or vector of `numpy.datetime64` objects
        containing the datetime equivalents of the given nanosecond offsets,
        with the specified `unit`.

    Raises
    ------
    OverflowError
        If the range of `arg` exceeds the representable range of
        `numpy.datetime64` objects ([`'-9223372036854773837-01-01 00:00:00'` - 
        `'9223372036854775807-01-01 00:00:00'`]), or if `unit` is specified and
        the range of `arg` exceeds the 64-bit range of `unit`.

    Examples
    --------
        >>> ns_to_numpy_datetime64(0)
        >>> ns_to_numpy_datetime64(2**63 - 1)
        >>> ns_to_numpy_datetime64(2**63 - 1, unit="us")
        >>> ns_to_numpy_datetime64(2**63 - 1, unit="ms")
        >>> ns_to_numpy_datetime64(2**63 - 1, unit="s")
        >>> ns_to_numpy_datetime64(2**63 - 1, unit="m")
        >>> ns_to_numpy_datetime64(2**63 - 1, unit="h")
        >>> ns_to_numpy_datetime64(2**63 - 1, unit="D")
        >>> ns_to_numpy_datetime64(2**63 - 1, unit="W")
        >>> ns_to_numpy_datetime64(2**63 - 1, unit="M")
        >>> ns_to_numpy_datetime64(2**63 - 1, unit="Y")

        >>> ns_to_numpy_datetime64(2**65)
        >>> ns_to_numpy_datetime64(2**65, rounding="up")
        >>> ns_to_numpy_datetime64(2**65, unit="s", rounding="up")
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
    tz: str | datetime.tzinfo | None = None,
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
    tz : str | datetime.tzinfo | None
        An IANA timezone string ('US/Pacific', 'Europe/Berlin', etc.) or a
        `datetime.tzinfo`-compatible timezone offset (`pytz`, `zoneinfo`,
        etc.).  Can also be `None`, indicating a naive ('UTC') return type.
        Defaults to `None`.
    min_ns : int
        The minimum value stored in `arg`.  If given, this will be used for
        range checks directly, avoiding a call to `.min()`.  Optional.
        Keyword-only. Defaults to `None`.
    max_ns : int
        The maximum value stored in `arg`.  If given, this will be used for
        range checks directly, avoiding a call to `.max()`.  Optional.
        Keyword-only.  Defaults to `None`.

    Returns
    -------
    datetime | array-like
        A datetime or vector of datetime objects containing the datetime
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
        >>> ns_to_datetime(2**63 - 1)
        >>> ns_to_datetime(2**63)

        >>> ns_to_pydatetime(253402300799999999000)
        >>> ns_to_pydatetime(253402300799999999000 + 1)

        >>> ns_to_datetime(2**63 - 1, tz="US/Pacific")
        >>> ns_to_datetime(2**63 - 1, tz="Europe/Berlin")
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
