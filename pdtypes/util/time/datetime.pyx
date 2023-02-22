import datetime
import re

import dateutil
import numpy as np
cimport numpy as np
import pandas as pd
import pytz

from pdtypes.type_hints import datetime_like

from .calendar import date_to_days, days_in_month
from .timedelta import pytimedelta_to_ns
from .unit cimport as_ns


#########################
####    CONSTANTS    ####
#########################


cdef object build_iso_8601_regex():
    """Compile a regex pattern to match arbitrary ISO-8601 datetime strings."""
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


cdef object build_iso_8601_strptime_format_regex():
    """Compile a regex to match strptime/strftime format strings if they
    are ISO 8601-compliant.
    """
    opt = lambda x: rf"({x})?"

    # work from right to left to fully account for optional components
    cdef str result = r"(%z|Z)?"
    result = rf"(.%f{result})?"
    result = rf"(:%S{result})?"
    result = rf"(:%M{result})?"
    result = rf"(( |T)%H{result})?"
    result = rf"(-%d{result})?"
    result = rf"(-%m{result})?"
    result = rf"^%Y{result}$"
    # reference: "%Y-%m-%d %H:%M:%S.%f%z"

    return re.compile(result)


cdef object iso_8601_format_pattern = build_iso_8601_strptime_format_regex()
cdef object iso_8601_pattern = build_iso_8601_regex()
cdef object parser_overflow_pattern = re.compile(r"out of range|must be in")


cdef object py_naive_utc = datetime.datetime.utcfromtimestamp(0)
cdef object py_aware_utc = pytz.timezone("UTC").localize(py_naive_utc)


######################
####    PUBLIC    ####
######################


def filter_dateutil_parser_error(err):
    err_msg = str(err)
    if parser_overflow_pattern.search(err_msg):
        return OverflowError(err_msg)
    return ValueError(err_msg)


def is_iso_8601_format_string(input_string: str) -> bool:
    """Returns `True` if an strftime/strptime format string complies with the
    ISO 8601 standard.
    """
    return iso_8601_format_pattern.match(input_string) is not None


def iso_8601_to_ns(str input_string) -> int:
    """Convert a scalar ISO 8601 string into a nanosecond offset from the
    utc epoch ('1970-01-01 00:00:00+0000').

    Returns a 2-tuple with the nanosecond offset as the first index.  The
    second index contains a boolean indicating whether the string had a
    timezone specifier (either 'Z' or a valid UTC offset).
    """
    match = iso_8601_pattern.match(input_string)
    if not match:
        raise ValueError(f"Invalid isoformat string: {repr(input_string)}")
    cdef dict parts = match.groupdict()

    # extract date components
    cdef char sign = -1 if parts["sign"] else 1
    cdef long int year = int(parts["year"])
    cdef short month = int(parts["month"] or 1)
    cdef short day = int(parts["day"] or 1)

    # extract time components
    cdef long int hour = int(parts["hour"] or 0)
    cdef long int minute = int(parts["minute"] or 0)
    cdef double second = float(parts["second"] or 0)

    # extract utc offset components
    cdef char utc_sign = -1 if parts["utc_sign"] == "-" else 1
    cdef long int utc_hour = int(parts["utc_hour"] or 0)
    cdef long int utc_minute = int(parts["utc_minute"] or 0)

    # check values are within normal bounds
    if not (
        1 <= month <= 12 and
        1 <= day <= days_in_month(month, year) and
        0 <= hour < 24 and
        0 <= minute < 60 and
        0 <= second < 60 and
        0 <= utc_hour < 24 and
        0 <= utc_minute < 60
    ):
        raise ValueError(f"invalid isoformat string {repr(input_string)}")

    # convert date to ns, add time component, and subtract utc offset
    cdef object result = date_to_days(sign * year, month, day) * as_ns["D"]
    result += hour * as_ns["h"] + minute * as_ns["m"] + int(second * as_ns["s"])
    result -= utc_sign * (utc_hour * as_ns["h"] + utc_minute * as_ns["m"])
    return result


def localize_pydatetime(
    date: datetime.datetime,
    tz: pytz.BaseTzInfo,
    utc: bool
) -> datetime.datetime:
    """Localize a scalar python datetime to the given timezone."""
    # return naive
    if not tz:
        if not date.tzinfo:  # datetime is already naive
            return date
        date = date.astimezone(datetime.timezone.utc)  # as utc
        return date.replace(tzinfo=None)  # make naive

    # return aware
    if not date.tzinfo:  # datetime is naive
        if not utc:  # localize directly to timezone
            return tz.localize(date)
        date = date.replace(tzinfo=datetime.timezone.utc)  # as utc
    return date.astimezone(tz)  # convert to final timezone


def ns_to_pydatetime(object ns, object tz = None) -> datetime.datetime:
    """Convert a nanosecond offset from UTC into a properly-localized
    `datetime.datetime` object.
    """
    cdef object offset = datetime.timedelta(microseconds=ns // as_ns["us"])
    if tz is None:
        return py_naive_utc + offset
    if tz == pytz.utc:
        return py_aware_utc + offset
    return (py_aware_utc + offset).astimezone(tz)


def numpy_datetime64_to_ns(
    object date,
    str unit = None,
    long int step_size = -1
) -> int:
    """Convert a numpy datetime64 into a nanosecond offset from UTC."""
    if unit is None or step_size < 0:
        unit, step_size = np.datetime_data(date)

    result = int(date.view(np.int64)) * step_size
    if unit == "ns":
        return result
    if unit in as_ns:
        return result * as_ns[unit]
    if unit == "M":
        return date_to_days(1970, 1 + result, 1) * as_ns["D"]
    return date_to_days(1970 + result, 1, 1) * as_ns["D"]


def pandas_timestamp_to_ns(object date, object tz = None) -> int:
    """Convert a pandas Timestamp into a nanosecond offset from UTC."""
    if tz and not date.tzinfo:
        date = date.tz_localize(tz)
    return date.value


def pydatetime_to_ns(object date, object tz = None) -> int:
    """Convert a python datetime into a nanosecond offset from UTC."""
    if tz and not date.tzinfo:
        date = tz.localize(date)
    date -= py_aware_utc if date.tzinfo else py_naive_utc
    return pytimedelta_to_ns(date)


def string_to_pydatetime(
    str input_string,
    str format = None,
    object parser_info = None,
    object tz = None,
    bint utc = False,
    str errors = "raise"
) -> datetime.datetime:
    """Convert a string to a python datetime object."""
    input_string = input_string.strip().lower()
    result = None

    # attempt to use format string if given
    if format is not None:
        try:
            result = datetime.datetime.strptime(input_string, format)
        except ValueError as err:
            if errors != "coerce":
                raise err            

    # check for relative date
    if result is None and input_string in ("today", "now"):
        if tz is None:
            result = datetime.datetime.now()
        else:
            result = datetime.datetime.now(tz)

    # check for quarterly date
    if result is None and "q" in input_string:
        try:
            period = pd.Period(input_string, freq="Q") - 1
            days = date_to_days(period.year, 1, 1 + period.day_of_year)
            result = ns_to_pydatetime(days * as_ns["D"])
        except pd._libs.tslibls.parsing.DateParseError:
            pass

    # parse using dateutil
    if result is None:
        try:
            result = dateutil.parser.parse(
                input_string,
                fuzzy=True,
                parserinfo=parser_info
            )
        except dateutil.parser.ParserError as err:
            raise filter_dateutil_parser_error(err) from None

    # apply timezone
    if tz:
        if result.tzinfo:
            return result.astimezone(tz)
        if utc:
            return pytz.utc.localize(result).astimezone(tz)
        return tz.localize(result)
    return result