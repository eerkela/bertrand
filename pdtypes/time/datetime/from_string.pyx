import datetime
from cpython cimport datetime
import re

cimport cython
import dateutil
import numpy as np
cimport numpy as np
import pandas as pd

from ..timezone import timezone, localize_pandas_timestamp, localize_pydatetime
from ..timezone cimport localize_pydatetime_scalar


# possible formats
# iso 8601:     '1968-04-01 08:47:13.123456789+0730'
# J2000 years:  'J2000.12345'
# quarters:     '4Q2023'
# shorthand:    '4 jan 2022 at 7 AM'
# relative:     'today'


def test(np.ndarray[str] arr):
    return arr


#########################
####    Constants    ####
#########################


cdef object j2000_pattern = re.compile(r"J[0-9]+.?[0-9]*")


cdef object min_pydatetime = np.datetime64(datetime.datetime.min)


cdef object max_pydatetime = np.datetime64(datetime.datetime.max)


#######################
####    Private    ####
#######################


cdef bint is_iso_8601(str string):
    """Infer whether a string can be interpreted as an ISO 8601 date."""
    try:
        return np.datetime64(string) is not None
    except ValueError:
        return False


# TODO: cython except -1?

cdef inline object string_to_pydatetime_scalar(
    str string,
    str format,
    object parser_info
):
    """TODO"""
    if format is not None:  # use given format string
        try:
            return datetime.datetime.strptime(string, format)
        except ValueError:  # attempt flexible parse instead
            return dateutil.parser.parse(string, fuzzy=True,
                                         parserinfo=parser_info)
    return dateutil.parser.parse(string, fuzzy=True, parserinfo=parser_info)


cdef np.ndarray[object] iso_8601_string_to_pydatetime_vector(
    np.ndarray arr
):
    # convert ISO strings to M8[us]
    arr = arr.astype("M8[us]")

    # check for overflow
    cdef object min_val = arr.min()
    cdef object max_val = arr.max()

    if min_val < min_pydatetime or max_val > max_pydatetime:
        # TODO: write message
        raise ValueError()

    # convert M8 array into datetime.datetime objects
    return arr.astype("O")


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] string_to_pydatetime_vector(
    np.ndarray[object] arr,
    datetime.tzinfo tz,
    str format,
    object parser_info,
    str errors
):
    """TODO"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef str string
    cdef object parsed
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        string = arr[i]
        try:
            parsed = string_to_pydatetime_scalar(string, format=format,
                                                 parser_info=parser_info)
        except dateutil.parser.ParserError as err:
            if errors != "coerce":
                raise err
            result[i] = pd.NaT
        else:
            result[i] = localize_pydatetime_scalar(parsed, tz)

    return result


######################
####    Public    ####
######################


def string_to_pandas_timestamp(
    arg: str | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = None,
    format: str = None,
    day_first: bool = False,
    year_first: bool = False,
    errors: str = "raise"
):
    """TODO"""
    # ensure format doesn't contradict day_first, year_first
    if format is not None and day_first or year_first:
        raise RuntimeError(f"if a `format` string is given, both `day_first` "
                           f"and `year_first` must be False")

    # resolve timezone
    tz = timezone(tz)

    # get kwarg dict for pd.to_datetime
    if format is not None:
        kwargs = {"format": format, "exact": False, "utc": True,
                  "errors": errors}
    else:
        kwargs = {"dayfirst": day_first, "yearfirst": year_first,
                  "infer_datetime_format": True, "utc": True, "errors": errors}

    # np.ndarray
    if isinstance(arg, np.ndarray):  # convert back to array
        arg = pd.to_datetime(arg, **kwargs).tz_convert(tz)
        return arg.to_numpy(dtype="O")

    # pd.Series/scalar
    return pd.to_datetime(arg).tz_convert(tz)


def string_to_pydatetime(
    arg: str | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = None,
    format: str = None,
    day_first: bool = False,
    year_first: bool = False,
    errors: str = "raise"
):
    """TODO"""
    # ensure format doesn't contradict day_first, year_first
    if format is not None and day_first or year_first:
        raise RuntimeError(f"if a `format` string is given, both `day_first` "
                           f"and `year_first` must be False")

    # resolve timezone
    tz = timezone(tz)

    # convert fixed-length numpy string arrays to python strings
    if isinstance(arg, np.ndarray) and np.issubdtype(arg.dtype, "U"):
        arg = arg.astype("O")


    # TODO: consider converting iso strings elementwise.  cimport
    # localize_pydatetime_scalar and run it where needed


    # fastpath for ISO 8601 strings
    if isinstance(arg, np.ndarray):
        if is_iso_8601(arg[0]):
            arg = iso_8601_string_to_pydatetime_vector(arg)
            return localize_pydatetime(arg, tz) if tz else arg
    elif isinstance(arg, pd.Series):
        if is_iso_8601(arg[0]):
            index = arg.index
            arg = arg.to_numpy(dtype="O")
            arg = iso_8601_string_to_pydatetime_vector(arg)
            if tz:
                arg = localize_pydatetime(arg, tz)
            return pd.Series(arg, index=index, copy=False, dtype="O")
    elif is_iso_8601(arg):
        arg = datetime.datetime.fromisoformat(arg)
        return localize_pydatetime_scalar(arg, tz)

    # use dateutil parsing
    if not format:  # attempt to infer format just like pd.to_datetime
        infer = pd.core.tools.datetimes._guess_datetime_format_for_array
        if not isinstance(arg, (np.ndarray, pd.Series)):
            format = infer(np.array([arg]))
        else:
            format = infer(arg)
    parser_info = dateutil.parser.parserinfo(dayfirst=day_first,
                                             yearfirst=year_first)

    # np.ndarray
    if isinstance(arg, np.ndarray):
        return string_to_pydatetime_vector(
            arg,
            tz=tz,
            format=format,
            parser_info=parser_info,
            errors=errors
        )

    # pd.Series
    if isinstance(arg, pd.Series):
        return string_to_pydatetime_vector(
            arg,
            tz=tz,
            format=format,
            parser_info=parser_info,
            errors=errors
        )

    # scalar
    try:
        arg = string_to_pydatetime_scalar(arg, format=format, 
                                          parser_info=parser_info)
    except dateutil.parser.ParserError as err:
        if errors != "coerce":
            raise err
        return pd.NaT

    return localize_pydatetime_scalar(arg, tz)
