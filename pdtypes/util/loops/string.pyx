"""Simple Cython loops for typing-related operations on np arrays.

Further reading:
    https://blog.paperspace.com/faster-np-array-processing-ndarray-cython/
"""
import datetime
from cpython cimport datetime
import decimal
import re

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

import dateutil


cdef dict bool_strings = {
    # True
    "y": True,
    "yes": True,
    "t": True,
    "true": True,
    "on": True,
    "1": True,

    # False
    "n": False,
    "no": False,
    "f": False,
    "false": False,
    "off": False,
    "0": False
}


@cython.boundscheck(False)
@cython.wraparound(False)
def string_to_boolean(
    np.ndarray[object] arr
) -> tuple[np.ndarray[object], np.ndarray[bool]]:
    """Convert a boolean string ('True', 'False', 'yes', 'no', 'on', 'off',
    etc.) to its equivalent boolean value.  Invalid results are converted to
    `pd.NA`, and their locations are returned as the second element of the
    resulting tuple `(result, invalid)`.

    Parameters
    ----------
    arr (np.ndarray[object]):
        The array to convert.  Must contain strings and have `dtype='O'`.

    Returns
    -------
    tuple[np.ndarray[object], np.ndarray[bool]]:
        A tuple of np arrays.  The first index contains the result of the
        boolean conversion, including invalid results.  The second index is
        a boolean mask indicating the locations of invalid results.
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef str s
    cdef np.ndarray[object] result
    cdef np.ndarray[np.uint8_t, ndim=1, cast=True] invalid

    result = np.empty(arr_length, dtype="O")
    invalid = np.empty(arr_length, dtype=bool)

    for i in range(arr_length):
        s = arr[i].replace(" ", "").lower()
        result[i] = bool_strings.get(s, pd.NA)
        invalid[i] = s not in bool_strings
    return result, invalid


@cython.boundscheck(False)
@cython.wraparound(False)
def split_complex_strings(
    np.ndarray[object] arr
) -> tuple[np.ndarray[object], np.ndarray[object], np.ndarray[bool]]:
    """test"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef str element
    cdef list[str] components
    cdef np.ndarray[object] real
    cdef np.ndarray[object] imag
    cdef np.ndarray[np.uint8_t, ndim=1, cast=True] invalid

    real = np.empty(arr_length, dtype="O")
    imag = np.empty(arr_length, dtype="O")
    invalid = np.empty(arr_length, dtype=bool)

    # build regex to extract real, imaginary components from complex string
    cdef str number_regex = r"[+-]?[\d.]+(e[+-]?\d+)?"
    cdef str nan_or_inf = r"[+-]?(nan|infinity|infty|inf)"
    cdef str real_regex = rf"(?P<real>{number_regex}|{nan_or_inf})(?![\d.eji])"
    cdef str imag_regex = rf"(?P<imag>{number_regex}|{nan_or_inf})[ji]"
    cdef list patterns = [
        re.compile(rf"{real_regex}{imag_regex}"),
        re.compile(rf"{imag_regex}{real_regex}"),
        re.compile(rf"{real_regex}"),
        re.compile(rf"{imag_regex}")
    ]
    cdef object pat  # re.Pattern
    cdef object match  # re.Match
    cdef dict groups = None

    # iterate through array
    for i in range(arr_length):
        element = arr[i].replace(" ", "").lower()

        # attempt to match element
        for pat in patterns:
            match = pat.search(element)
            if match:
                groups = match.groupdict()
                break

        # extract real, imaginary components
        if groups is None:
            real[i] = "nan"
            imag[i] = "nan"
            invalid[i] = True
        else:
            real[i] = groups.get("real", "0")
            imag[i] = groups.get("imag", "0")
            invalid[i] = False

        # reset for next iteration
        groups = None

    return real, imag, invalid


def localize(
    datetime.datetime dt,
    datetime.tzinfo tz = None
) -> datetime.datetime:
    """Localize a `datetime.datetime` object according to the given `tzinfo`
    object.  Naive datetimes are interpreted as UTC.

    Parameters
    ----------
    dt (datetime.datetime):
        A python `datetime.datetime` object to localize.
    tz (datetime.tzinfo):
        A `datetime.datetime`-compliant `tzinfo` object, which `dt` will be
        localized to.  Can be a subclass of `tzinfo`, including `pytz.timezone`
        or `zoneinfo.ZoneInfo`, or `None` to indicate a naive (UTC) result.
        Defaults to `None`.
    """
    if not tz:  # return naive
        if dt.tzinfo:  # datetime is not naive
            dt = dt.astimezone(datetime.timezone.utc)
            return dt.replace(tzinfo=None)
        return dt

    # return aware
    if not dt.tzinfo:  # datetime is naive
        dt = dt.replace(tzinfo=datetime.timezone.utc)
    return dt.astimezone(tz)


@cython.boundscheck(False)
@cython.wraparound(False)
def string_to_pydatetime(
    np.ndarray[object] arr,
    str format = None,
    datetime.tzinfo tz = None,
    bint day_first = False,
    bint year_first = False,
    bint fuzzy = False,
    str errors = "raise"
) -> np.ndarray[object]:
    """Converts an array of datetime strings into built-in python datetime
    objects, using dateutil parsing for ambiguous dates.

    Parameters
    ----------
    arr (np.ndarray[object]):
        The array to be converted.  Must contain strings and have `dtype='O'`.
    format (str):
        A format string that complies with the `datetime.datetime.strptime()`
        standard.  Can also be `None`.  If `format=None` or a string in `arr`
        does not match the given format, it will be interpreted according to
        `dateutil.parser.parse()` instead.
    tz (datetime.tzinfo):
        A `datetime.datetime`-compliant `tzinfo` object, which the resulting
        datetimes are localized to.  Can be a subclass of `tzinfo`, including
        `pytz.timezone` or `zoneinfo.ZoneInfo`, or `None` to indicate a naive
        (UTC) result.  Defaults to `None`.
    day_first (bint):
        Used for `dateutil` parsing.  Indicates whether to interpret the first
        value in an ambiguous 3-integer date (e.g. 01/05/09) as the day
        (`True`) or month (`False`).  If `year_first` is set to `True`, this
        distinguishes between YDM and YMD.  Defaults to `False`.
    year_first (bint):
        Used for `dateutil` parsing.  Indicates whether to interpret the first
        value in an ambiguous 3-integer date (e.g. 01/05/09) as the year.  If
        `True`, the first number is taken to be the year, otherwise the last
        number is taken to be the year.  Defaults to `False`.
    fuzzy (bint):
        Used for `dateutil` parsing.  Indicates whether to allow fuzzy parsing,
        which can match strings like "Today is January 1, 2047 at 8:21:00AM".
    errors (str):


    Returns
    -------
    np.ndarray[object]:
        An array of `datetime.datetime` objects that match the strings in
        `arr`.

    Raises
    ------
    dateutil.parser.ParserError:
        If `errors != 'coerce'` and an unparseable string is encountered, or
        the resulting datetime would fall outside the available range of
        `datetime.datetime` ("0001-01-01 00:00:00" -
        "9999-12-31 23:59:59.999999").
    """
    if not format:  # attempt to infer format
        infer = pd.core.tools.datetimes._guess_datetime_format_for_array
        format = infer(arr)
    parser_info = dateutil.parser.parserinfo(dayfirst=day_first,
                                             yearfirst=year_first)

    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef str element
    cdef datetime.datetime dt
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    if format:
        for i in range(arr_length):
            element = arr[i].strip()
            try:
                try:  # try to use given format string
                    dt = datetime.datetime.strptime(element, format)
                except ValueError:  # attempt flexible parse instead
                    dt = dateutil.parser.parse(element, fuzzy=fuzzy,
                                               parserinfo=parser_info)
            except dateutil.parser.ParserError as err:
                if errors != "coerce":
                    raise err
                result[i] = pd.NaT
            else:
                result[i] = localize(dt, tz)

    else:
        for i in range(arr_length):
            element = arr[i].strip()
            try:
                dt = dateutil.parser.parse(element, fuzzy=fuzzy,
                                           parserinfo=parser_info)
            except dateutil.parser.ParserError as err:
                if errors != "coerce":
                    raise err
                result[i] = pd.NaT
            else:
                result[i] = localize(dt, tz)

    return result




from pdtypes.util.time import _to_ns


# TODO: can probably remove whitespace from patterns


cdef list[object] timedelta_formats_regex():
    """Compile a set of regular expressions to capture and parse recognized
    timedelta strings.

    Matches both abbreviated ('1h22m', '1 hour, 22 minutes', etc.) and
    clock format ('01:22:00', '1:22', '00:01:22:00') strings, with precision up
    to days and/or weeks and down to nanoseconds.
    """
    # capture groups - abbreviated units ('h', 'min', 'seconds', etc.)
    # years = r"(?P<years>\d+)\s*(?:ys?|yrs?.?|years?)"
    # months = r"(?P<months>\d+)\s*(?:mos?.?|mths?.?|months?)"
    cdef str W = r"(?P<W>[\d.]+)\s*(?:w|wks?|weeks?)"
    cdef str D = r"(?P<D>[\d.]+)\s*(?:d|dys?|days?)"
    cdef str h = r"(?P<h>[\d.]+)\s*(?:h|hrs?|hours?)"
    cdef str m = r"(?P<m>[\d.]+)\s*(?:m|(mins?)|(minutes?))"
    cdef str s = r"(?P<s>[\d.]+)\s*(?:s|secs?|seconds?)"
    cdef str ms = r"(?P<ms>[\d.]+)\s*(?:ms|msecs?|millisecs?|milliseconds?)"
    cdef str us = r"(?P<us>[\d.]+)\s*(?:us|usecs?|microsecs?|microseconds?)"
    cdef str ns = r"(?P<ns>[\d.]+)\s*(?:ns|nsecs?|nanosecs?|nanoseconds?)"

    # capture groups - clock format (':' separated)
    cdef str day_clock = (r"(?P<D>\d+):(?P<h>\d{2}):(?P<m>\d{2}):"
                          r"(?P<s>\d{2}(?:\.\d+)?)")
    cdef str hour_clock = r"(?P<h>\d+):(?P<m>\d{2}):(?P<s>\d{2}(?:\.\d+)?)"
    cdef str minute_clock =  r"(?P<m>\d{1,2}):(?P<s>\d{2}(?:\.\d+)?)"
    cdef str second_clock = r":(?P<s>\d{2}(?:\.\d+)?)"

    # wrapping functions for capture groups
    cdef str separators = r"[,/]"  # these are ignored
    optional = lambda x: rf"(?:{x}\s*(?:{separators}\s*)?)?"

    # compiled timedelta formats
    return [
        re.compile(rf"\s*{optional(W)}\s*{optional(D)}\s*{optional(h)}\s*"
                   rf"{optional(m)}\s*{optional(s)}\s*{optional(ms)}\s*"
                   rf"{optional(us)}\s*{optional(ns)}\s*$"),
        re.compile(rf"\s*{optional(W)}\s*{optional(D)}\s*{hour_clock}\s*$"),
        re.compile(rf"\s*{day_clock}\s*$"),
        re.compile(rf"\s*{minute_clock}\s*$"),
        re.compile(rf"\s*{second_clock}\s*$")
    ]


cdef dict timedelta_regex = {
    "sign": re.compile(r"\s*(?P<sign>[+|-])?\s*(?P<unsigned>.*)$"),
    "formats": timedelta_formats_regex()
}


cdef object _string_to_ns(str delta, bint as_hours = False):
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
    >>> parse_timedelta('1:24')
    84000000000
    >>> parse_timedelta(':22')
    22000000000
    >>> parse_timedelta('1 minute, 24 secs')
    84000000000
    >>> parse_timedelta('1m24s')
    84000000000
    >>> parse_timedelta('1.2 minutes')
    72000000000
    >>> parse_timedelta('1.2 seconds')
    1200000000

    Time expressions can be signed.
    >>> parse_timedelta('- 1 minute')
    -60000000000
    >>> parse_timedelta('+ 1 minute')
    60000000000

    If `as_hours=True`, then ambiguous digits following a colon will be
    interpreted as minutes; otherwise they are considered to be seconds.
    >>> timeparse('1:30', as_hours=False)
    90000000000
    >>> timeparse('1:30', as_hours=True)
    5400000000000
    """
    cdef object match  # re.Match
    cdef int sign
    cdef object time_format  # re.Pattern
    cdef dict groups
    cdef object result  # python integer

    # preprocess delta string
    delta = delta.replace(" ", "").lower()

    # get sign if present
    match = timedelta_regex["sign"].match(delta)
    sign = -1 if match.groupdict()["sign"] == "-" else 1

    # strip sign and test all possible formats
    delta = match.groupdict()["unsigned"]
    for time_format in timedelta_regex["formats"]:

        # attempt match
        match = time_format.match(delta)
        if match and match.group().strip():  # match found and not empty

            # get dict of named subgroups (?P<...>) and associated values
            groups = match.groupdict()

            # strings of the form '1:22' are ambiguous.  By default, we assume
            # minutes and seconds, but if `as_hours=True`, we interpret as
            # hours and minutes instead
            if (as_hours and delta.count(":") == 1 and "." not in delta and
                not any(groups.get(x, None) for x in ["h", "D", "W"])):
                groups["h"] = groups["m"]
                groups["m"] = groups["s"]
                groups.pop("s")

            # build result
            result = sum(_to_ns[k] * decimal.Decimal(v)
                         for k, v in groups.items() if v)
            return int(sign * result)

    # string could not be matched
    raise ValueError(f"could not parse timedelta string: {repr(delta)}")


def string_to_ns(str delta, bint as_hours = False) -> object:
    """Exported python alias for _string_to_ns"""
    return _string_to_ns(delta, as_hours)



@cython.boundscheck(False)
@cython.wraparound(False)
def string_to_pytimedelta(
    np.ndarray[object] arr,
    bint as_hours = False,
    str errors = "raise"
) -> np.ndarray[object]:
    """test"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef str element
    cdef object us  # python integer
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        element = arr[i]
        try:
            us = _string_to_ns(element, as_hours=as_hours) // 1000
            result[i] = datetime.timedelta(microseconds=us)
        except (ValueError, OverflowError) as err:
            if errors == "coerce":
                result[i] = pd.NaT
            else:
                raise err

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
def string_to_numpy_timedelta64(
    np.ndarray[object] arr,
    bint as_hours = False,
    str errors = "raise"
) -> np.ndarray[object]:
    """test"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] as_ns = np.empty(arr_length, dtype="O")
    cdef object ns  # python integer
    cdef object min_ns = 0  # python integer
    cdef object max_ns = 0  # python integer

    # convert each string to integer number of nanoseconds
    for i in range(arr_length):
        try:
            ns = _string_to_ns(arr[i], as_hours=as_hours)
        except ValueError as err:
            if errors != "coerce":
                raise err
        else:
            as_ns[i] = ns
            if ns < min_ns:
                min_ns = ns
            elif ns > max_ns:
                max_ns = ns

    # identify smallest unit that can represent data, starting with ns
    cdef str unit
    cdef long long int scale_factor
    cdef bint converged = False

    for unit, scale_factor in _to_ns.items():
        if (-2**63 + 1 <= min_ns // scale_factor and
            max_ns // scale_factor <= 2**63 - 1):
            converged = True
            break

    if not converged:  # no shared unit could be identified
        raise ValueError("string values exceed numpy.timedelta64 range for "
                         "every choice of unit up to 'W'")

    # convert each nanosecond value to numpy.timedelta64 with appropriate unit
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        ns = as_ns[i]
        if ns is None:
            result[i] = np.timedelta64("nat")
        else:
            result[i] = np.timedelta64(ns // scale_factor, unit)

    # return
    return result
