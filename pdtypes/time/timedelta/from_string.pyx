import datetime
import decimal
import re

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes.util.type_hints import datetime_like, timedelta_like

from ..epoch import epoch_date
from ..unit cimport as_ns, round_months_to_ns, round_years_to_ns

from .from_ns import (
    ns_to_pandas_timedelta, ns_to_pytimedelta, ns_to_numpy_timedelta64,
    ns_to_timedelta
)


#########################
####    Constants    ####
#########################


cdef list[object] timedelta_formats_regex():
    """Compile a set of regular expressions to capture and parse recognized
    timedelta strings.

    Matches both abbreviated ('1h22m', '1 hour, 22 minutes', etc.) and
    clock format ('01:22:00', '1:22', '00:01:22:00') strings, with precision up
    to days and/or weeks and down to nanoseconds.
    """
    # capture groups - abbreviated units ('h', 'min', 'seconds', etc.)
    cdef str Y = r"(?P<Y>[\d.]+)(?:ys?|yrs?.?|years?)"
    cdef str M = r"(?P<M>[\d.]+)(?:mos?.?|mths?.?|months?)"
    cdef str W = r"(?P<W>[\d.]+)(?:w|wks?|weeks?)"
    cdef str D = r"(?P<D>[\d.]+)(?:d|dys?|days?)"
    cdef str h = r"(?P<h>[\d.]+)(?:h|hrs?|hours?)"
    cdef str m = r"(?P<m>[\d.]+)(?:m|(mins?)|(minutes?))"
    cdef str s = r"(?P<s>[\d.]+)(?:s|secs?|seconds?)"
    cdef str ms = r"(?P<ms>[\d.]+)(?:ms|msecs?|millisecs?|milliseconds?)"
    cdef str us = r"(?P<us>[\d.]+)(?:us|usecs?|microsecs?|microseconds?)"
    cdef str ns = r"(?P<ns>[\d.]+)(?:ns|nsecs?|nanosecs?|nanoseconds?)"

    # capture groups - clock format (':' separated)
    cdef str day_clock = (r"(?P<D>\d+):(?P<h>\d{2}):(?P<m>\d{2}):"
                          r"(?P<s>\d{2}(?:\.\d+)?)")
    cdef str hour_clock = r"(?P<h>\d+):(?P<m>\d{2}):(?P<s>\d{2}(?:\.\d+)?)"
    cdef str minute_clock =  r"(?P<m>\d{1,2}):(?P<s>\d{2}(?:\.\d+)?)"
    cdef str second_clock = r":(?P<s>\d{2}(?:\.\d+)?)"

    # wrapping functions for capture groups
    cdef str separators = r"[,/]"  # these are ignored
    optional = lambda x: rf"(?:{x}(?:{separators})?)?"

    # compiled timedelta formats
    return [
        re.compile(rf"^{optional(Y)}{optional(M)}{optional(W)}{optional(D)}"
                   rf"{optional(h)}{optional(m)}{optional(s)}{optional(ms)}"
                   rf"{optional(us)}{optional(ns)}$"),
        re.compile(rf"^{optional(W)}{optional(D)}{hour_clock}$"),
        re.compile(rf"^{day_clock}$"),
        re.compile(rf"^{minute_clock}$"),
        re.compile(rf"^{second_clock}$")
    ]


cdef dict timedelta_regex = {
    "sign": re.compile(r"(?P<sign>[+|-])?(?P<unsigned>.*)$"),
    "formats": timedelta_formats_regex()
}


#######################
####    Private    ####
#######################


cdef object timedelta_string_to_ns_scalar(
    str string,
    bint as_hours,
    object start_year,
    object start_month,
    object start_day
):
    """Internal C interface for public-facing `string_to_ns()` function."""
    cdef object match
    cdef int sign
    cdef object time_format
    cdef dict groups
    cdef object result
    cdef str k
    cdef str v
    cdef object value

    # preprocess timedelta string
    string = string.replace(" ", "").lower()

    # get sign if present
    match = timedelta_regex["sign"].match(string)
    sign = -1 if match.groupdict()["sign"] == "-" else 1

    # strip sign and test all possible formats
    string = match.groupdict()["unsigned"]
    for time_format in timedelta_regex["formats"]:

        # attempt match
        match = time_format.match(string)
        if match and match.group().strip():  # match found and not empty

            # get dict of named subgroups (?P<...>) and associated values
            groups = match.groupdict()

            # strings of the form '1:22' are ambiguous; do they represent
            # minutes and seconds or hours and minutes?  By default, we assume
            # the former, but if `as_hours=True`, we reverse that assumption
            if (as_hours and string.count(":") == 1 and "." not in string and
                not any(groups.get(x, None) for x in ["h", "D", "W"])):
                groups["h"] = groups["m"]
                groups["m"] = groups["s"]
                groups.pop("s")

            # build result
            result = 0
            for k, v in groups.items():
                if not v:
                    continue
                if k == "M":
                    result += round_months_to_ns(
                        decimal.Decimal(v),
                        start_year=start_year,
                        start_month=start_month,
                        start_day=start_day
                    )
                elif k == "Y":
                    result += round_years_to_ns(
                        decimal.Decimal(v),
                        start_year=start_year,
                        start_month=start_month,
                        start_day=start_day
                    )
                else:
                    result += int(as_ns[k] * decimal.Decimal(v))
            return sign * result

    # string could not be matched
    raise ValueError(f"could not parse timedelta string: {repr(string)}")


@cython.boundscheck(False)
@cython.wraparound(False)
cdef tuple timedelta_string_to_ns_vector(
    np.ndarray[str] arr,
    bint as_hours,
    object start_year,
    object start_month,
    object start_day,
    str errors
):
    """TODO"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")
    cdef bint has_errors = False

    for i in range(arr_length):
        try:
            result[i] = timedelta_string_to_ns_scalar(
                arr[i],
                as_hours=as_hours,
                start_year=start_year,
                start_month=start_month,
                start_day=start_day
            )
        except ValueError as err:
            if errors == "raise":
                raise err
            has_errors = True
            result[i] = None

    return result, has_errors


######################
####    Public    ####
######################


def timedelta_string_to_ns(
    arg: str | np.ndarray | pd.Series,
    as_hours: bool = False,
    since: str | datetime_like = "2001-01-01 00:00:00+0000",
    errors: str = "raise"
) -> tuple[int | np.ndarray | pd.Series, bool]:
    """Parse a timedelta string, returning its associated value as an integer
    number of nanoseconds.

    See also: https://github.com/wroberts/pytimeparse

    Parameters
    ----------
    delta (str):
        Timedelta string to parse.  Can be in either abbreviated ('1h22m',
        '1 hour, 22 minutes', ...) or clock format ('01:22:00', '1:22',
        '00:01:22:00', ...), with precision up to days/weeks and down to
        nanoseconds.  Can be either signed or unsigned.
    as_hours (bool):
        Whether to parse ambiguous timedelta strings of the form '1:22' as
        containing hours and minutes (`True`), or minutes and seconds
        (`False`).  Does not affect any other string format.

    Returns
    -------
    int:
        An integer number of nanoseconds associated with the given timedelta
        string.  If the string contains digits below nanosecond precision,
        they are destroyed.

    Raises
    ------
    ValueError:
        If the passed timedelta string does not match any of the recognized
        formats.

    Examples
    --------
    >>> timedelta_string_to_ns('1:24')
    84000000000
    >>> timedelta_string_to_ns(':22')
    22000000000
    >>> timedelta_string_to_ns('1 minute, 24 secs')
    84000000000
    >>> timedelta_string_to_ns('1m24s')
    84000000000
    >>> timedelta_string_to_ns('1.2 minutes')
    72000000000
    >>> timedelta_string_to_ns('1.2 seconds')
    1200000000

    Time expressions can be signed.
    >>> timedelta_string_to_ns('- 1 minute')
    -60000000000
    >>> timedelta_string_to_ns('+ 1 minute')
    60000000000

    If `as_hours=True`, then ambiguous digits following a colon will be
    interpreted as minutes; otherwise they are considered to be seconds.
    >>> timeparse('1:30', as_hours=False)
    90000000000
    >>> timeparse('1:30', as_hours=True)
    5400000000000
    """
    # resolve `since` epoch
    since = epoch_date(since)

    # np.ndarray
    if isinstance(arg, np.ndarray):
        # convert fixed-length numpy strings to python strings
        if np.issubdtype(arg.dtype, "U"):
            arg = arg.astype("O")

        result, has_errors = timedelta_string_to_ns_vector(
            arg,
            as_hours=as_hours,
            start_year=since["year"],
            start_month=since["month"],
            start_day=since["day"],
            errors=errors
        )
        if errors == "ignore" and has_errors:
            return arg, has_errors
        return result, has_errors

    # pd.Series
    if isinstance(arg, pd.Series):
        result, has_errors = timedelta_string_to_ns_vector(
            arg.to_numpy(),
            as_hours=as_hours,
            start_year=since["year"],
            start_month=since["month"],
            start_day=since["day"],
            errors=errors
        )
        if errors == "ignore" and has_errors:
            return arg, has_errors
        return pd.Series(result, index=arg.index, copy=False), has_errors

    # scalar
    try:
        return (
            timedelta_string_to_ns_scalar(
                arg,
                as_hours=as_hours,
                start_year=since["year"],
                start_month=since["month"],
                start_day=since["day"]
            ),
            False
        )
    except ValueError as err:
        if errors == "raise":
            raise err
        if errors == "ignore":
            return (arg, True)
        return (None, True)


def string_to_pandas_timedelta(
    arg: str | np.ndarray | pd.Series,
    as_hours: bool = False,
    since: str | datetime_like = "2001-01-01 00:00:00+0000",
    errors: str = "raise"
) -> pd.Timedelta | np.ndarray | pd.Series:
    """TODO"""
    # convert strings to ns, then ns to pd.Timedelta
    result, has_errors = timedelta_string_to_ns(
        arg,
        as_hours=as_hours,
        since=since,
        errors=errors
    )

    # check for parsing errors
    if has_errors:
        if errors == "ignore":
            return arg

        # np.ndarray
        if isinstance(arg, np.ndarray):
            valid = (result != None)
            if valid.any():
                result[valid] = ns_to_pandas_timedelta(result[valid])
            result[~valid] = pd.NaT
            return result

        # pd.Series
        if isinstance(arg, pd.Series):
            valid = (result != None)
            if valid.any():
                result[valid] = ns_to_pandas_timedelta(result[valid])
            return result.astype("m8[ns]")

        # scalar
        return pd.NaT

    # no errors encountered
    return ns_to_pandas_timedelta(result)


def string_to_pytimedelta(
    arg: str | np.ndarray | pd.Series,
    as_hours: bool = False,
    since: str | datetime_like = "2001-01-01 00:00:00+0000",
    errors: str = "raise"
) -> datetime.timedelta | np.ndarray | pd.Series:
    """TODO"""
    # convert strings to ns, then ns to pd.Timedelta
    result, has_errors = timedelta_string_to_ns(
        arg,
        as_hours=as_hours,
        since=since,
        errors=errors
    )

    # check for parsing errors
    if has_errors:
        if errors == "ignore":
            return arg

        # np.ndarray/pd.Series
        if isinstance(arg, (np.ndarray, pd.Series)):
            valid = (result != None)
            if valid.any():
                result[valid] = ns_to_pytimedelta(result[valid])
            result[~valid] = pd.NaT
            return result

        # scalar
        return pd.NaT

    # no errors encountered
    return ns_to_pytimedelta(result)


def string_to_numpy_timedelta64(
    arg: str | np.ndarray | pd.Series,
    as_hours: bool = False,
    since: str | datetime_like = "2001-01-01 00:00:00+0000",
    unit: str = None,
    rounding: str = "down",
    errors: str = "raise"
) -> np.timedelta64 | np.ndarray | pd.Series:
    """TODO"""
    # convert strings to ns, then ns to pd.Timedelta
    result, has_errors = timedelta_string_to_ns(
        arg,
        as_hours=as_hours,
        since=since,
        errors=errors
    )

    # check for parsing errors
    if has_errors:
        if errors == "ignore":
            return arg

        # np.ndarray
        if isinstance(arg, np.ndarray):
            valid = (result != None)
            if valid.any():
                arg = ns_to_numpy_timedelta64(
                    result[valid],
                    unit=unit,
                    since=since,
                    rounding=rounding
                )
                result[valid] = arg
                unit, _ = np.datetime_data(arg.dtype)
                return result.astype(f"m8[{unit}]")

            # no valid inputs
            if unit is None:
                unit = "ns"
            return result.astype(f"m8[{unit}]")

        # pd.Series
        if isinstance(arg, pd.Series):
            valid = (result != None)
            if valid.any():
                arg = ns_to_numpy_timedelta64(
                    result[valid],
                    unit=unit,
                    since=since,
                    rounding=rounding
                )
                result[valid] = arg
            result[~valid] = np.timedelta64("nat")
            return result

        # scalar
        return np.timedelta64("nat")

    # no errors encountered
    return ns_to_numpy_timedelta64(
        result,
        unit=unit,
        since=since,
        rounding=rounding
    )


def string_to_timedelta(
    arg: str | np.ndarray | pd.Series,
    as_hours: bool = False,
    since: str | datetime_like = "2001-01-01 00:00:00+0000",
    errors: str = "raise"
) -> timedelta_like | np.ndarray | pd.Series:
    """TODO"""
    # convert strings to ns, then ns to pd.Timedelta
    result, has_errors = timedelta_string_to_ns(
        arg,
        as_hours=as_hours,
        since=since,
        errors=errors
    )

    # check for parsing errors
    if has_errors:
        if errors == "ignore":
            return arg

        # np.ndarray
        if isinstance(arg, np.ndarray):  # return a timedelta64 array
            valid = (result != None)
            if valid.any():
                arg = ns_to_timedelta(result[valid], since=since)
                result[valid] = arg
                unit, _ = np.datetime_data(arg)
                return result.astype(f"m8[{unit}]")

            # no valid inputs
            return result.astype(f"m8[ns]")

        # pd.Series
        if isinstance(arg, pd.Series):  # return an m8[ns] series, if possible
            valid = (result != None)
            if valid.any():
                arg = ns_to_timedelta(result[valid], since=since)
                result[valid] = arg
                if pd.api.types.is_datetime64_ns_dtype(arg):
                    return result.infer_objects()  # return as m8[ns] series

            # return as object array
            result[~valid] = pd.NaT
            return result

        # scalar
        return pd.NaT

    # no errors were encountered
    return ns_to_timedelta(result, since=since)