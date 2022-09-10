import datetime
from cpython cimport datetime
import re

cimport cython
import dateutil
import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes.util.type_hints import datetime_like

from ..date import date_to_days
from ..timezone import timezone, localize_pandas_timestamp, localize_pydatetime, is_utc
from ..timezone cimport localize_pydatetime_scalar
from ..unit cimport as_ns

from .from_ns import ns_to_pydatetime, ns_to_numpy_datetime64


# possible formats
# iso 8601:     '1968-04-01 08:47:13.123456789+0730'
# J2000 years:  'J2000.12345'  (not covered by default)
# quarters:     '4Q2023'
# shorthand:    '4 jan 2022 at 7 AM'
# relative:     'today'


#########################
####    Constants    ####
#########################


cdef object build_iso_8601_regex():
    """TODO"""
    # extract date component
    cdef str sign = r"(?P<sign>-)"
    cdef str year = r"(?P<year>[0-9]+)"
    cdef str month = r"(?P<month>[0-9]{2})"
    cdef str day = r"(?P<day>[0-9]{2})"

    # ISO 8601 date/time separators
    cdef str separators = r"[T\s]"

    # extract time component
    cdef str hour = r"(?P<hour>[0-9]{2})"
    cdef str minute = r"(?P<minute>[0-9]{2})"
    cdef str second = r"(?P<second>[0-9]{2}\.?[0-9]*)"

    # extract utc offset
    cdef str utc_sign = r"(?P<utc_sign>[+-])"
    cdef str utc_hour = r"(?P<utc_hour>[0-9]{2})"
    cdef str utc_minute = r"(?P<utc_minute>[0-9]{2})"

    # combine
    return re.compile(
        rf"^{sign}?{year}-?{month}?-?{day}?"            # date
        rf"{separators}?"                               # separator
        rf"{hour}?:?{minute}?:?{second}?"               # time
        rf"(Z|{utc_sign}{utc_hour}:?{utc_minute})?$"    # utc offset
    )


cdef object iso_8601_pattern = build_iso_8601_regex()


cdef object min_pydatetime = np.datetime64(datetime.datetime.min)


cdef object max_pydatetime = np.datetime64(datetime.datetime.max)


#######################
####    Private    ####
#######################


cdef object iso_8601_string_to_ns_scalar(str string):
    """TODO"""
    # extract datetime components using regex
    cdef object match = iso_8601_pattern.match(string)

    if not match:
        raise ValueError(f"Invalid isoformat string: {repr(string)}")

    # get match group dictionary
    cdef dict components = match.groupdict()

    # extract date components
    cdef char sign = -1 if components["sign"] else 1
    cdef long int year = int(components["year"])
    cdef short month = int(components["month"] or 1)
    cdef short day = int(components["day"] or 1)

    # extract time components
    cdef long int hour = int(components["hour"] or 0)
    cdef long int minute = int(components["minute"] or 0)
    cdef double second = float(components["second"] or 0)

    # extract utc offset components
    cdef char utc_sign = -1 if components["utc_sign"] == "-" else 1
    cdef long int utc_hour = int(components["utc_hour"] or 0)
    cdef long int utc_minute = int(components["utc_minute"] or 0)

    # convert date to ns
    cdef object result = date_to_days(sign * year, month, day)
    result *= as_ns["D"]

    # add time component
    result += hour * as_ns["h"] + minute * as_ns["m"] + int(second * as_ns["s"])

    # subtract utc offset
    result -= utc_sign * (utc_hour * as_ns["h"] + utc_minute * as_ns["m"])

    # return
    return result


cdef inline datetime.datetime string_to_pydatetime_scalar_with_format(
    str string,
    str format,
    datetime.tzinfo tz
):
    """TODO"""
    cdef datetime.datetime result = datetime.datetime.strptime(string, format)

    if tz:  # return aware
        if result.tzinfo:  # result is aware
            return result.astimezone(tz)
        return result.replace(tzinfo=datetime.timezone.utc).astimezone(tz)

    # return naive
    return result


cdef inline datetime.datetime string_to_pydatetime_scalar_parsed(
    str string,
    object parser_info,
    datetime.tzinfo tz
):
    """TODO"""
    cdef datetime.datetime result
    
    # check for relative date
    if string in ("today", "now"):
        result = datetime.datetime.now()

    # check for quarterly date (fall back to dateutil if not quarterly)
    elif "q" in string.lower():
        try:
            period = pd.Period(string, freq="Q") - 1
            days = date_to_days(period.year, 1, 1 + period.day_of_year)
            result = ns_to_pydatetime(days * as_ns["D"])
        except pd._libs.tslibls.parsing.DateParseError:
            result = dateutil.parser.parse(
                string,
                fuzzy=True,
                parserinfo=parser_info
            )

    # parse using dateutil
    else:
        result = dateutil.parser.parse(
            string,
            fuzzy=True,
            parserinfo=parser_info
        )

    # apply timezone
    if tz:  # return aware
        if result.tzinfo:  # result is aware
            return result.astimezone(tz)
        return result.replace(tzinfo=datetime.timezone.utc).astimezone(tz)

    # return naive
    return result


cdef inline datetime.datetime string_to_pydatetime_scalar_with_fallback(
    str string,
    str format,
    object parser_info,
    datetime.tzinfo tz
):
    """TODO"""
    try:
        return string_to_pydatetime_scalar_with_format(
            string,
            format=format,
            tz=tz
        )
    except ValueError as err:
        return string_to_pydatetime_scalar_parsed(
            string,
            parser_info=parser_info,
            tz=tz
        )


@cython.boundscheck(False)
@cython.wraparound(False)
cdef tuple iso_8601_string_to_ns_vector(
    np.ndarray[str] arr,
    str errors
):
    """TODO"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")
    cdef bint has_errors = False

    for i in range(arr_length):
        try:
            result[i] = iso_8601_string_to_ns_scalar(arr[i])
        except ValueError as err:
            if errors == "raise":
                raise err
            has_errors = True
            result[i] = None

    return result, has_errors


@cython.boundscheck(False)
@cython.wraparound(False)
cdef tuple string_to_pydatetime_vector_with_format(
    np.ndarray[str] arr,
    str format,
    datetime.tzinfo tz,
    str errors
):
    """TODO"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")
    cdef bint has_errors = False

    for i in range(arr_length):
        try:
            result[i] = string_to_pydatetime_scalar_with_format(
                arr[i],
                format=format,
                tz=tz
            )
        except ValueError as err:
            if errors != "coerce":
                raise err
            result[i] = pd.NaT
            has_errors = True

    return result, has_errors


@cython.boundscheck(False)
@cython.wraparound(False)
cdef tuple string_to_pydatetime_vector_parsed(
    np.ndarray[str] arr,
    object parser_info,
    datetime.tzinfo tz,
    str errors
):
    """TODO"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")
    cdef bint has_errors = False

    for i in range(arr_length):
        try:
            result[i] = string_to_pydatetime_scalar_parsed(
                arr[i],
                parser_info=parser_info,
                tz=tz
            )
        except dateutil.parser.ParserError as err:
            if errors != "coerce":
                raise err
            result[i] = pd.NaT
            has_errors = True

    return result, has_errors


@cython.boundscheck(False)
@cython.wraparound(False)
cdef tuple string_to_pydatetime_vector_with_fallback(
    np.ndarray[str] arr,
    str format,
    object parser_info,
    datetime.tzinfo tz,
    str errors
):
    """TODO"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")
    cdef bint has_errors = False

    for i in range(arr_length):
        try:
            result[i] = string_to_pydatetime_scalar_with_fallback(
                arr[i],
                format=format,
                parser_info=parser_info,
                tz=tz
            )
        except dateutil.parser.ParserError as err:
            if errors != "coerce":
                raise err
            result[i] = pd.NaT
            has_errors = True

    return result, has_errors


######################
####    Public    ####
######################


def is_iso_8601(str string) -> bool:
    """Infer whether a string can be interpreted as an ISO 8601 date."""
    return iso_8601_pattern.match(string) is not None


def iso_8601_to_ns(
    arg: str | np.ndarray | pd.Series,
    errors: str = "raise"
) -> tuple[int | np.ndarray | pd.Series, bool]:
    """TODO"""
    # np.ndarray
    if isinstance(arg, np.ndarray):
        # convert fixed-length numpy strings to python strings
        if np.issubdtype(arg.dtype, "U"):
            arg = arg.astype("O")

        result, has_errors = iso_8601_string_to_ns_vector(
            arg,
            errors=errors
        )
        if errors == "ignore" and has_errors:
            return arg, has_errors
        return result, has_errors

    # pd.Series
    if isinstance(arg, pd.Series):
        result, has_errors = iso_8601_string_to_ns_vector(
            arg.to_numpy(),
            errors=errors
        )
        if errors == "ignore" and has_errors:
            return arg, has_errors
        return pd.Series(result, index=arg.index, copy=False), has_errors

    # scalar
    try:
        return (iso_8601_string_to_ns_scalar(arg), False)
    except ValueError as err:
        if errors == "raise":
            raise err
        if errors == "ignore":
            return (arg, True)
        return (None, True)


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
    if format is not None and (day_first or year_first):
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

    try:
        arg = pd.to_datetime(arg, **kwargs).tz_convert(tz)
    except pd._libs.tslibs.np_datetime.OutOfBoundsDatetime as err:
        # outside pd.Timestamp range, but within datetime.datetime range
        raise OverflowError(str(err)) from err
    except dateutil.parser.ParserError as err:  # ambiguous
        # could be bad string or outside datetime.datetime range
        if err.__cause__:  # only overflow has a non-None __cause__ attr
            raise OverflowError(str(err)) from err
        raise err

    # np.ndarray
    if isinstance(arg, np.ndarray):  # convert back to array
        return arg.to_numpy(dtype="O")

    # pd.Series/scalar
    return arg


def _iso_8601_to_pydatetime(
    arg: str | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo,
    errors: str
) -> datetime.datetime | np.ndarray | pd.Series:
    """TODO"""
    # convert iso strings to ns, and then ns to np.datetime64
    result, has_errors = iso_8601_to_ns(arg, errors=errors)
    
    # check for parsing errors
    if has_errors:
        if errors == "ignore":
            return arg

        # pd.Series
        if isinstance(arg, (np.ndarray, pd.Series)):
            valid = (result != None)
            if valid.any():
                result[valid] = ns_to_pydatetime(result[valid], tz=tz)
            result[~valid] = pd.NaT
            return result

        # scalar
        return pd.NaT

    # no errors encountered
    return ns_to_pydatetime(result, tz=tz)


def _string_to_pydatetime_with_format(
    arg: str | np.ndarray | pd.Series,
    format: str,
    tz: datetime.tzinfo,
    errors: str
) -> datetime.datetime | np.ndarray | pd.Series:
    """TODO"""
    if isinstance(arg, np.ndarray):  # np.ndarray
        result, has_errors = string_to_pydatetime_vector_with_format(
            arg,
            format=format,
            tz=tz,
            errors=errors
        )
        if errors == "ignore" and has_errors:
            return arg
        return result

    if isinstance(arg, pd.Series):  # pd.Series
        result, has_errors = string_to_pydatetime_vector_with_format(
            arg.to_numpy(),
            format=format,
            tz=tz,
            errors=errors
        )
        if errors == "ignore" and has_errors:
            return arg
        return pd.Series(result, index=arg.index, copy=False)

    # scalar
    try:
        return string_to_pydatetime_scalar_with_format(
            arg,
            format=format,
            tz=tz
        )
    except ValueError as err:
        if errors == "raise":
            raise err
        if errors == "ignore":
            return arg
        return pd.NaT


def _string_to_pydatetime_parsed(
    arg: str | np.ndarray | pd.Series,
    parser_info: dateutil.parser.parserinfo,
    tz: datetime.tzinfo,
    errors: str
) -> datetime.datetime | np.ndarray | pd.Series:
    """TODO"""
    if isinstance(arg, np.ndarray):  # np.ndarray
        result, has_errors = string_to_pydatetime_vector_parsed(
            arg,
            parser_info=parser_info,
            tz=tz,
            errors=errors
        )
        if errors == "ignore" and has_errors:
            return arg
        return result


    if isinstance(arg, pd.Series):  # pd.Series
        result, has_errors = string_to_pydatetime_vector_parsed(
            arg.to_numpy(),
            parser_info=parser_info,
            tz=tz,
            errors=errors
        )
        if errors == "ignore" and has_errors:
            return arg
        return pd.Series(result, index=arg.index, copy=False)

    # scalar
    try:
        return string_to_pydatetime_scalar_parsed(
            arg,
            parser_info=parser_info,
            tz=tz
        )
    except ValueError as err:
        if errors == "raise":
            raise err
        if errors == "ignore":
            return arg
        return pd.NaT


def _string_to_pydatetime_with_fallback(
    arg: str | np.ndarray | pd.Series,
    format: str,
    parser_info: dateutil.parser.parserinfo,
    tz: datetime.tzinfo,
    errors: str
) -> datetime.datetime | np.ndarray | pd.Series:
    """TODO"""
    if isinstance(arg, np.ndarray):  # np.ndarray
        result, has_errors = string_to_pydatetime_vector_with_fallback(
            arg,
            format=format,
            parser_info=parser_info,
            tz=tz,
            errors=errors
        )
        if errors == "ignore" and has_errors:
            return arg
        return result

    if isinstance(arg, pd.Series):  # pd.Series
        result, has_errors = string_to_pydatetime_vector_with_fallback(
            arg.to_numpy(),
            format=format,
            parser_info=parser_info,
            tz=tz,
            errors=errors
        )
        if errors == "ignore" and has_errors:
            return arg
        return pd.Series(result, index=arg.index, copy=False)

    # scalar
    try:
        return string_to_pydatetime_scalar_with_fallback(
            arg,
            format=format,
            parser_info=parser_info,
            tz=tz
        )
    except ValueError as err:
        if errors == "raise":
            raise err
        if errors == "ignore":
            return arg
        return pd.NaT


def string_to_pydatetime(
    arg: str | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = None,
    format: str = None,
    day_first: bool = False,
    year_first: bool = False,
    errors: str = "raise"
) -> datetime.datetime | np.ndarray | pd.Series:
    """TODO"""
    # ensure format doesn't contradict day_first, year_first
    if format is not None and (day_first or year_first):
        raise RuntimeError(f"if a `format` string is given, both `day_first` "
                           f"and `year_first` must be False")

    # resolve timezone
    tz = timezone(tz)

    # convert fixed-length numpy string arrays to python strings
    if isinstance(arg, np.ndarray) and np.issubdtype(arg.dtype, "U"):
        arg = arg.astype("O")

    # if a format string is given, use it directly
    if format is not None:
        return _string_to_pydatetime_with_format(
            arg,
            format=format,
            tz=tz,
            errors=errors
        )

    # if no format is given, try ISO 8601
    element = arg[0] if isinstance(arg, (np.ndarray, pd.Series)) else arg
    if is_iso_8601(element) and "-" in element:  # ignore naked years
        return _iso_8601_to_pydatetime(arg, tz=tz, errors=errors)

    # if no format and not ISO 8601, try to infer format like pd.to_datetime
    infer = pd.core.tools.datetimes._guess_datetime_format_for_array
    if not isinstance(arg, (np.ndarray, pd.Series)):
        format = infer(np.array([arg]))
    else:
        format = infer(arg)

    # set up dateutil parserinfo
    parser_info = dateutil.parser.parserinfo(dayfirst=day_first,
                                             yearfirst=year_first)

    # if a format could be inferred, try it and fall back to dateutil
    if format:
        # no `year_first`; infer() ignores strings with ambiguous years
        if day_first:  # swap month/day components
            month_index = format.find("%m")
            day_index = format.find("%d")
            if month_index > -1 and day_index > -1 and month_index < day_index:
                format = (format[:month_index + 1] + "d" +
                          format[month_index + 2:day_index + 1] + "m" +
                          format[day_index + 2:])

        return _string_to_pydatetime_with_fallback(
            arg,
            format=format,
            parser_info=parser_info,
            tz=tz,
            errors=errors
        )

    # if no format could be inferred, use dateutil parsing directly
    return _string_to_pydatetime_parsed(
        arg,
        parser_info = parser_info,
        tz=tz,
        errors=errors
    )


def string_to_numpy_datetime64(
    arg: str | np.ndarray | pd.Series,
    unit: str = None,
    errors: str = "raise"
) -> np.datetime64 | np.ndarray | pd.Series:
    """TODO"""
    # convert iso strings to ns, and then ns to np.datetime64
    result, has_errors = iso_8601_to_ns(arg, errors=errors)
    
    # check for parsing errors
    if has_errors:
        if errors == "ignore":
            return arg

        # np.ndarray
        if isinstance(arg, np.ndarray):
            valid = (result != None)
            if valid.any():
                arg = ns_to_numpy_datetime64(result[valid], unit=unit)
                result[valid] = arg
                unit, _ = np.datetime_data(arg.dtype)
                return result.astype(f"M8[{unit}]")

            # no valid inputs
            if unit is None:
                unit = "ns"
            return result.astype(f"M8[{unit}]")

        # pd.Series
        if isinstance(arg, pd.Series):
            valid = (result != None)
            if valid.any():
                result[valid] = ns_to_numpy_datetime64(result[valid], unit=unit)
            result[~valid] = np.datetime64("nat")
            return result

        # scalar
        return np.datetime64("nat")

    # no errors encountered
    return ns_to_numpy_datetime64(result, unit=unit)


def string_to_datetime(
    arg: str | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = None,
    format: str = None,
    day_first: bool = False,
    year_first: bool = False,
    errors: str = "raise"
) -> datetime_like | np.ndarray | pd.Series:
    """TODO"""
    # ensure format doesn't contradict day_first, year_first
    if format is not None and (day_first or year_first):
        raise RuntimeError(f"if a `format` string is given, both `day_first` "
                           f"and `year_first` must be False")

    # resolve timezone
    tz = timezone(tz)

    # convert fixed-length numpy string arrays to python strings
    if isinstance(arg, np.ndarray) and np.issubdtype(arg.dtype, "U"):
        arg = arg.astype("O")

    try:  # pd.Timestamp
        return string_to_pandas_timestamp(
            arg,
            tz=tz,
            format=format,
            day_first=day_first,
            year_first=year_first,
            errors=errors
        )
    except OverflowError:
        pass

    try:  # datetime.datetime
        return string_to_pydatetime(
            arg,
            tz=tz,
            format=format,
            day_first=day_first,
            year_first=year_first,
            errors=errors
        )
    except OverflowError:
        pass

    # np.datetime64
    if tz and not is_utc(tz):
        err_msg = ("`numpy.datetime64` objects do not carry timezone "
                   "information (must be utc)")
        raise RuntimeError(err_msg)
    return string_to_numpy_datetime64(arg, errors=errors)
