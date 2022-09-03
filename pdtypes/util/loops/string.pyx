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


# TODO: this should go in a parse.pyx file


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




# TODO: duplicated in pdtypes.util.time
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



# TODO: move this to pdtypes.util.time

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
