import datetime
import decimal
import re

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from ..unit cimport as_ns


# TODO: can probably remove whitespace from patterns

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


#######################
####    Private    ####
#######################


cdef object _string_to_ns(
    str delta,
    bint as_hours = False
):
    """Internal C interface for public-facing `string_to_ns()` function."""
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
            result = sum(as_ns[k] * decimal.Decimal(v)
                         for k, v in groups.items() if v)
            return int(sign * result)

    # string could not be matched
    raise ValueError(f"could not parse timedelta string: {repr(delta)}")


def string_to_ns(
    str delta,
    bint as_hours = False
) -> int:
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
    >>> string_to_ns('1:24')
    84000000000
    >>> string_to_ns(':22')
    22000000000
    >>> string_to_ns('1 minute, 24 secs')
    84000000000
    >>> string_to_ns('1m24s')
    84000000000
    >>> string_to_ns('1.2 minutes')
    72000000000
    >>> string_to_ns('1.2 seconds')
    1200000000

    Time expressions can be signed.
    >>> string_to_ns('- 1 minute')
    -60000000000
    >>> string_to_ns('+ 1 minute')
    60000000000

    If `as_hours=True`, then ambiguous digits following a colon will be
    interpreted as minutes; otherwise they are considered to be seconds.
    >>> timeparse('1:30', as_hours=False)
    90000000000
    >>> timeparse('1:30', as_hours=True)
    5400000000000
    """
    return _string_to_ns(delta, as_hours)





# TODO: vectorize string_to_ns so it accepts scalar/array/Series

# TODO: add string_to_pandas_timedelta
# TODO: add string_to_timedelta that accepts arbitrary input






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

    for unit, scale_factor in as_ns.items():
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
