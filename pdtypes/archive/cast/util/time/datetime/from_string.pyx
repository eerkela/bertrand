"""Convert datetime strings to their corresponding datetime representation.

Supports arbitrary string parsing through the `dateutil` library.  This applies
only to dates that would fall within `datetime.datetime` range
([`'0001-01-01 00:00:00'` - `'9999-12-31 23:59:59.999999'`]).  ISO 8601 date
strings can be parsed up to the full `numpy.datetime64` range.

Functions
---------
is_iso_8601(string: str) -> bool:
    Infer whether a scalar string can be interpreted as ISO 8601-compliant.

iso_8601_to_ns(
    arg: str | np.ndarray | pd.Series,
    errors: str = "raise"
) -> int | np.ndarray | pd.Series:
    Convert ISO 8601 strings into nanosecond offsets from the utc epoch.

string_to_pandas_timestamp(
    arg: str | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = None,
    format: str = None,
    day_first: bool = False,
    year_first: bool = False,
    utc: bool = False,
    errors: str = "raise"
) -> pd.Timestamp | np.ndarray | pd.Series:
    Convert datetime strings into `pandas.Timestamp` objects.

string_to_pydatetime(
    arg: str | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = None,
    format: str = None,
    day_first: bool = False,
    year_first: bool = False,
    utc: bool = False,
    errors: str = "raise"
) -> datetime.datetime | np.ndarray | pd.Series:
    Convert datetime strings into `datetime.datetime` objects.

string_to_numpy_datetime64(
    arg: str | np.ndarray | pd.Series,
    unit: str = None,
    rounding: str = "down",
    errors: str = "raise"
) -> np.datetime64 | np.ndarray | pd.Series:
    Convert datetime strings into `numpy.datetime64` objects.

string_to_datetime(
    arg: str | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = None,
    format: str = None,
    day_first: bool = False,
    year_first: bool = False,
    utc: bool = False,
    errors: str = "raise"
) -> datetime_like | np.ndarray | pd.Series:
    Convert datetime strings into dynamic datetime objects.

Examples
--------
Parsing ISO 8601 strings into nanosecond offsets from the UTC epoc:

>>> iso_8601_to_ns("2022")
1640995200000000000
>>> iso_8601_to_ns("2022-10")
1664582400000000000
>>> iso_8601_to_ns("2022-10-15")
1665792000000000000
>> iso_8601_to_ns("2022-10-15 08")
1665820800000000000
>>> iso_8601_to_ns("2022-10-15T08:47")
1665823620000000000
>>> iso_8601_to_ns("2022-10-15 08:47:23")
1665823643000000000
>>> iso_8601_to_ns("2022-10-15T08:47:23.123")
1665823643123000000
>>> iso_8601_to_ns("2022-10-15 08:47:23.123456")
1665823643123456000
>>> iso_8601_to_ns("2022-10-15T08:47:23.123456789")
1665823643123456789
>>> iso_8601_to_ns("-43-03-01")  # Assassination of Julius Caesar
-63519033600000000000
>>> iso_8601_to_ns("18-08-19")  # Death of Caesar Augustus
-61579267200000000000
>>> iso_8601_to_ns("1970-01-01 00:00:00")
0
>>> iso_8601_to_ns("1970-01-01 00:00:00Z")
0
>>> iso_8601_to_ns("1970-01-01 00:00:00+0800")
-28800000000000
>>> iso_8601_to_ns("1970-01-01 00:00:00-0800")
28800000000000
>>> iso_8601_to_ns(f"{2**63 - 1}-01-01 00:00:00.123456789")
291061508645168328945024000123456789
>>> iso_8601_to_ns("2001-13-15 00:00:00")
Traceback (most recent call last):
    ...
ValueError: invalid isoformat string '2001-13-15 00:00:00'
>>> iso_8601_to_ns("2001-02-29 00:00:00")
Traceback (most recent call last):
    ...
ValueError: invalid isoformat string '2001-02-29 00:00:00'
>>> iso_8601_to_ns("2001-01-01 60:00:00")
Traceback (most recent call last):
    ...
ValueError: invalid isoformat string '2001-01-01 60:00:00'
>>> iso_8601_to_ns("2001-01-01 00:60:00")
Traceback (most recent call last):
    ...
ValueError: invalid isoformat string '2001-01-01 00:60:00'
>>> iso_8601_to_ns("2001-01-01 00:00:60")
Traceback (most recent call last):
    ...
ValueError: invalid isoformat string '2001-01-01 00:00:60'
>>> iso_8601_to_ns("2000-02-29 00:00:00")
951782400000000000
>>> iso_8601_to_ns(
...     pd.Series([f"1970-01-01 00:00:00.00000000{i}" for i in range(1, 4)])
... )
0    1
1    2
2    3
dtype: object
>>> iso_8601_to_ns(
...     np.array([f"1970-01-01 00:00:00.00000000{i}" for i in range(1, 4)])
... )
array([1, 2, 3], dtype=object)

Converting strings into `pandas.Timestamp` objects:

>>> string_to_pandas_timestamp("1970-01-01 00:00:00")
Timestamp('1970-01-01 00:00:00')
>>> string_to_pandas_timestamp("1970-01-01 00:00:00", tz="US/Pacific")
Timestamp('1970-01-01 00:00:00-0800', tz='US/Pacific')
>>> string_to_pandas_timestamp("1970-01-01 00:00:00Z", tz="US/Pacific")
Timestamp('1969-12-31 16:00:00-0800', tz='US/Pacific')
>>> string_to_pandas_timestamp("1970-01-01 00:00:00+01:00", tz="US/Pacific")
Timestamp('1969-12-31 15:00:00-0800', tz='US/Pacific')
>>> string_to_pandas_timestamp("today")
Timestamp('2022-09-22 17:06:58.837417')  #random
>>> string_to_pandas_timestamp("4Q2023")
Timestamp('2023-10-01 00:00:00')
>>> string_to_pandas_timestamp("23q4")
Timestamp('2023-10-01 00:00:00')
>>> string_to_pandas_timestamp("04.01.2022", format="%d.%m.%Y")
Timestamp('2022-01-04 00:00:00')
>>> string_to_pandas_timestamp(
...     "04.01.2022",
...     format="%d.%m.%Y",
...     tz="US/Eastern"
... )
Timestamp('2022-01-04 00:00:00-0500', tz='US/Eastern')
>>> string_to_pandas_timestamp("4 Jan 2022")
Timestamp('2022-01-04 00:00:00')
>>> string_to_pandas_timestamp("December 7th, 1941 at 8 AM", tz="US/Hawaii")
Timestamp('1941-12-07 08:00:00-1030', tz='US/Hawaii')
>>> string_to_pandas_timestamp("01/05/09")
Timestamp('2009-01-05 00:00:00')
>>> string_to_pandas_timestamp("01/05/09", day_first=True)
Timestamp('2009-05-01 00:00:00')
>>> string_to_pandas_timestamp("01/05/09", year_first=True)
Timestamp('2001-05-09 00:00:00')
>>> string_to_pandas_timestamp("01/05/09", day_first=True, year_first=True)
Timestamp('2001-09-05 00:00:00')
>>> strings = [f"1970-01-01 00:00:00.00000000{i}" for i in range(1, 4)]
>>> strings
['1970-01-01 00:00:00.000000001', '1970-01-01 00:00:00.000000002', '1970-01-01 00:00:00.000000003']
>>> string_to_pandas_timestamp(pd.Series(strings))
0   1970-01-01 00:00:00.000000001
1   1970-01-01 00:00:00.000000002
2   1970-01-01 00:00:00.000000003
dtype: datetime64[ns]
>>> string_to_pandas_timestamp(np.array(strings))
array([Timestamp('1970-01-01 00:00:00.000000001'),
    Timestamp('1970-01-01 00:00:00.000000002'),
    Timestamp('1970-01-01 00:00:00.000000003')], dtype=object)

Converting strings into `datetime.datetime` objects:

>>> string_to_pydatetime("1970-01-01 00:00:00")
datetime.datetime(1970, 1, 1, 0, 0)
>>> string_to_pydatetime("1970-01-01 00:00:00", tz="US/Pacific")
datetime.datetime(1970, 1, 1, 0, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
>>> string_to_pydatetime("1970-01-01 00:00:00Z", tz="US/Pacific")
datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
>>> string_to_pydatetime("1970-01-01 00:00:00+01:00", tz="US/Pacific")
datetime.datetime(1969, 12, 31, 15, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
>>> string_to_pydatetime("today")
datetime.datetime(2022, 9, 23, 0, 8, 4, 855833)  #random
>>> string_to_pydatetime("4Q2023")
datetime.datetime(2023, 10, 1, 0, 0)
>>> string_to_pydatetime("23q4")
datetime.datetime(2023, 10, 1, 0, 0)
>>> string_to_pydatetime("04.01.2022", format="%d.%m.%Y")
datetime.datetime(2022, 1, 4, 0, 0)
>>> string_to_pydatetime(
...     "04.01.2022",
...     format="%d.%m.%Y",
...     tz="US/Eastern"
... )
datetime.datetime(2022, 1, 4, 0, 0, tzinfo=<DstTzInfo 'US/Eastern' EST-1 day, 19:00:00 STD>)
>>> string_to_pydatetime("4 Jan 2022")
datetime.datetime(2022, 1, 4, 0, 0)
>>> string_to_pydatetime("December 7th, 1941 at 8 AM", tz="US/Hawaii")
datetime.datetime(1941, 12, 7, 8, 0, tzinfo=<DstTzInfo 'US/Hawaii' HST-1 day, 13:30:00 STD>)
>>> string_to_pydatetime("Today is January 1, 2047 8:21:00AM", tz="UTC")
datetime.datetime(2047, 1, 1, 8, 21, tzinfo=<UTC>)
>>> string_to_pydatetime("01/05/09")
datetime.datetime(2009, 1, 5, 0, 0)
>>> string_to_pydatetime("01/05/09", day_first=True)
datetime.datetime(2009, 5, 1, 0, 0)
>>> string_to_pydatetime("01/05/09", year_first=True)
datetime.datetime(2001, 5, 9, 0, 0)
>>> string_to_pydatetime("01/05/09", day_first=True, year_first=True)
datetime.datetime(2001, 9, 5, 0, 0)
>>> strings = [f"1970-01-01 00:00:00.00000{i}" for i in range(1, 4)]
>>> strings
['1970-01-01 00:00:00.000001', '1970-01-01 00:00:00.000002', '1970-01-01 00:00:00.000003']
>>> string_to_pydatetime(pd.Series(strings))
0    1970-01-01 00:00:00.000001
1    1970-01-01 00:00:00.000002
2    1970-01-01 00:00:00.000003
dtype: object
>>> string_to_pydatetime(np.array(strings))
array([datetime.datetime(1970, 1, 1, 0, 0, 0, 1),
    datetime.datetime(1970, 1, 1, 0, 0, 0, 2),
    datetime.datetime(1970, 1, 1, 0, 0, 0, 3)], dtype=object)

Converting strings into `numpy.datetime64` objects:

>>> string_to_numpy_datetime64("1970-01-01 00:00:00")
numpy.datetime64('1970-01-01T00:00:00.000000000')
>>> string_to_numpy_datetime64("1970-01-01 00:00:00Z")
numpy.datetime64('1970-01-01T00:00:00.000000000')
>>> string_to_numpy_datetime64("1970-01-01 00:00:00-0800")
numpy.datetime64('1970-01-01T08:00:00.000000000')
>>> string_to_numpy_datetime64("2042-10-15 12:34:56.789101112", unit="ns")
numpy.datetime64('2042-10-15T12:34:56.789101112')
>>> string_to_numpy_datetime64("2042-10-15 12:34:56.789101112", unit="us")
numpy.datetime64('2042-10-15T12:34:56.789101')
>>> string_to_numpy_datetime64("2042-10-15 12:34:56.789101112", unit="ms")
numpy.datetime64('2042-10-15T12:34:56.789')
>>> string_to_numpy_datetime64("2042-10-15 12:34:56.789101112", unit="s")
numpy.datetime64('2042-10-15T12:34:56')
>>> string_to_numpy_datetime64("2042-10-15 12:34:56.789101112", unit="m")
numpy.datetime64('2042-10-15T12:34')
>>> string_to_numpy_datetime64("2042-10-15 12:34:56.789101112", unit="h")
numpy.datetime64('2042-10-15T12','h')
>>> string_to_numpy_datetime64("2042-10-15 12:34:56.789101112", unit="D")
numpy.datetime64('2042-10-15')
>>> string_to_numpy_datetime64("2042-10-15 12:34:56.789101112", unit="W")
numpy.datetime64('2042-10-09')
>>> string_to_numpy_datetime64("2042-10-15 12:34:56.789101112", unit="M")
numpy.datetime64('2042-10')
>>> string_to_numpy_datetime64("2042-10-15 12:34:56.789101112", unit="Y")
numpy.datetime64('2042')
>>> string_to_numpy_datetime64(
...     "2042-10-15 12:34:56.789101112",
...     unit="s",
...     rounding="up"
... )
numpy.datetime64('2042-10-15T12:34:57')
>>> string_to_numpy_datetime64(f"{2**50}-10-15 12:34:56")
numpy.datetime64('1125899906842624-10-15')
>>> string_to_numpy_datetime64(f"{2**50}-10-15 12:34:56", rounding="up")
numpy.datetime64('1125899906842624-10-16')

Converting strings into arbitrary datetime objects:

>>> string_to_datetime("1970-01-01 00:00:00")
Timestamp('1970-01-01 00:00:00')
>>> string_to_datetime("1970-01-01 00:00:00", tz="US/Pacific")
Timestamp('1970-01-01 00:00:00-0800', tz='US/Pacific')
>>> string_to_datetime("1970-01-01 00:00:00Z", tz="US/Pacific")
Timestamp('1969-12-31 16:00:00-0800', tz='US/Pacific')
>>> string_to_datetime("1970-01-01 00:00:00+01:00", tz="US/Pacific")
Timestamp('1969-12-31 15:00:00-0800', tz='US/Pacific')
>>> string_to_datetime("today")
Timestamp('2022-09-22 17:06:58.837417')  #random
>>> string_to_datetime("4Q2023")
Timestamp('2023-10-01 00:00:00')
>>> string_to_datetime("23q4")
Timestamp('2023-10-01 00:00:00')
>>> string_to_datetime("04.01.2022", format="%d.%m.%Y")
Timestamp('2022-01-04 00:00:00')
>>> string_to_datetime(
...     "04.01.2022",
...     format="%d.%m.%Y",
...     tz="US/Eastern"
... )
Timestamp('2022-01-04 00:00:00-0500', tz='US/Eastern')
>>> string_to_datetime("4 Jan 2022")
Timestamp('2022-01-04 00:00:00')
>>> string_to_datetime("December 7th, 1941 at 8 AM", tz="US/Hawaii")
Timestamp('1941-12-07 08:00:00-1030', tz='US/Hawaii')
>>> string_to_datetime("01/05/09")
Timestamp('2009-01-05 00:00:00')
>>> string_to_datetime("01/05/09", day_first=True)
Timestamp('2009-05-01 00:00:00')
>>> string_to_datetime("01/05/09", year_first=True)
Timestamp('2001-05-09 00:00:00')
>>> string_to_datetime("01/05/09", day_first=True, year_first=True)
Timestamp('2001-09-05 00:00:00')
>>> strings = [f"1970-01-01 00:00:00.00000000{i}" for i in range(1, 4)]
>>> strings
['1970-01-01 00:00:00.000000001', '1970-01-01 00:00:00.000000002', '1970-01-01 00:00:00.000000003']
>>> string_to_datetime(pd.Series(strings))
0   1970-01-01 00:00:00.000000001
1   1970-01-01 00:00:00.000000002
2   1970-01-01 00:00:00.000000003
dtype: datetime64[ns]
>>> string_to_datetime(np.array(strings))
array([Timestamp('1970-01-01 00:00:00.000000001'),
    Timestamp('1970-01-01 00:00:00.000000002'),
    Timestamp('1970-01-01 00:00:00.000000003')], dtype=object)
>>> string_to_datetime("2262-04-11 23:47:16.854775807")
Timestamp('2262-04-11 23:47:16.854775807')
>>> string_to_datetime("2262-04-11 23:47:16.854775808")
datetime.datetime(2262, 4, 11, 23, 47, 16, 854775)
>>> string_to_datetime("April 11th, 2262 at 23:47:16.854775")
Timestamp('2262-04-11 23:47:16.854775')
>>> string_to_datetime("April 11th, 2262 at 23:47:16.854776")
datetime.datetime(2262, 4, 11, 23, 47, 16, 854776)
>>> string_to_datetime("9999-12-31 23:59:59.999999")
datetime.datetime(9999, 12, 31, 23, 59, 59, 999999)
>>> string_to_datetime("10000-01-01 00:00:00")
numpy.datetime64('10000-01-01T00:00:00.000000')
>>> string_to_datetime("1677-09-21 00:12:43.145224193")
Timestamp('1677-09-21 00:12:43.145224193')
>>> string_to_datetime("1677-09-21 00:12:43.145224193", tz="Europe/Berlin")
datetime.datetime(1677, 9, 21, 0, 12, 43, 145224, tzinfo=<DstTzInfo 'Europe/Berlin' LMT+0:53:00 STD>)
>>> string_to_datetime("September 21st, 1677 at 00:12:43.145225")
Timestamp('1677-09-21 00:12:43.145225')
>>> string_to_datetime(
...     "September 21st, 1677 at 00:12:42.145225",
...     tz="Europe/Berlin"
... )
datetime.datetime(1677, 9, 21, 0, 12, 42, 145225, tzinfo=<DstTzInfo 'Europe/Berlin' LMT+0:53:00 STD>)
"""
import datetime
from cpython cimport datetime
import re

cimport cython
import dateutil
import numpy as np
cimport numpy as np
import pandas as pd
import pytz

from pdtypes.util.type_hints import datetime_like

from ..calendar import date_to_days, days_in_month
from ..timezone import is_utc, localize_pydatetime, timezone
from ..unit cimport as_ns

from .from_ns import ns_to_pydatetime, ns_to_numpy_datetime64


# TODO: support J2000 dates through convert_unit_float?


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


# build ISO 8601 regex
cdef object iso_8601_pattern = build_iso_8601_regex()


#######################
####    Private    ####
#######################


cdef tuple iso_8601_string_to_ns_scalar(str string):
    """Convert a scalar ISO 8601 string into a nanosecond offset from the
    utc epoch ('1970-01-01 00:00:00+0000').

    Returns a 2-tuple with the nanosecond offset as the first index.  The
    second index contains a boolean indicating whether the string had a
    timezone specifier (either 'Z' or a valid UTC offset).
    """
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
    cdef bint has_offset = components["utc_sign"] is not None or "Z" in string
    cdef char utc_sign = -1 if components["utc_sign"] == "-" else 1
    cdef long int utc_hour = int(components["utc_hour"] or 0)
    cdef long int utc_minute = int(components["utc_minute"] or 0)

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
        raise ValueError(f"invalid isoformat string {repr(string)}")

    # convert date to ns
    cdef object result = date_to_days(sign * year, month, day) * as_ns["D"]

    # add time component
    result += hour * as_ns["h"] + minute * as_ns["m"] + int(second * as_ns["s"])

    # subtract utc offset
    result -= utc_sign * (utc_hour * as_ns["h"] + utc_minute * as_ns["m"])

    # return
    return result, has_offset


cdef inline datetime.datetime string_to_pydatetime_scalar_with_format(
    str string,
    str format,
    datetime.tzinfo tz,
    bint utc
):
    """Convert a scalar datetime string to a `datetime.datetime` object
    according to the given format string, localized to the given timezone.
    """
    # interpret using strptime
    cdef datetime.datetime result = datetime.datetime.strptime(string, format)

    # return aware
    if tz:
        if result.tzinfo:  # result is already aware
            return result.astimezone(tz)

        # result is naive
        if utc:  # interpret as UTC
            return result.replace(tzinfo=datetime.timezone.utc).astimezone(tz)
        if isinstance(tz, pytz.BaseTzInfo):  # use .localize()
            return tz.localize(result)
        return result.replace(tzinfo=tz)  # replace tzinfo directly

    # return naive
    return result


cdef inline datetime.datetime string_to_pydatetime_scalar_parsed(
    str string,
    object parser_info,
    datetime.tzinfo tz,
    bint utc
):
    """Convert a scalar datetime string to a `datetime.datetime` object using
    modified dateutil parsing rules, localized to the given timezone.
    """
    cdef datetime.datetime result
    
    # check for relative date
    if string in ("today", "now"):
        result = datetime.datetime.now()

    # check for quarterly date
    elif "q" in string.lower():
        # Interpret as a `pd.Period` object, which supports quarterly dates
        # by default.  If this is unsuccessful, fall back to dateutil.
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
        if result.tzinfo:  # result is already aware
            return result.astimezone(tz)

        # result is naive
        if utc:  # interpret as UTC
            return result.replace(tzinfo=datetime.timezone.utc).astimezone(tz)
        if isinstance(tz, pytz.BaseTzInfo):  # use .localize()
            return tz.localize(result)
        return result.replace(tzinfo=tz)  # replace tzinfo directly

    # return naive
    return result


cdef inline datetime.datetime string_to_pydatetime_scalar_with_fallback(
    str string,
    str format,
    object parser_info,
    datetime.tzinfo tz,
    bint utc
):
    """Convert a scalar datetime string to a `datetime.datetime` object, using
    the given format string where applicable and modified dateutil parsing
    where it is not.  Localizes result to the given timezone.
    """
    try:  # use format string
        return string_to_pydatetime_scalar_with_format(
            string,
            format=format,
            tz=tz,
            utc=utc
        )
    except ValueError as err:  # fall back to dateutil
        return string_to_pydatetime_scalar_parsed(
            string,
            parser_info=parser_info,
            tz=tz,
            utc=utc
        )


@cython.boundscheck(False)
@cython.wraparound(False)
cdef tuple iso_8601_string_to_ns_vector(
    np.ndarray[str] arr,
    str errors
):
    """Convert an array of ISO 8601 strings into nanosecond offsets from the
    utc epoch ('1970-01-01 00:00:00+0000').  Notes parsing errors according to
    the given error-handling rule.
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result
    cdef np.ndarray[char, cast=True] has_offset

    result = np.full(arr_length, pd.NA, dtype="O")
    has_offset = np.full(arr_length, False, dtype=bool)

    for i in range(arr_length):
        try:
            result[i], has_offset[i] = iso_8601_string_to_ns_scalar(arr[i])
        except ValueError as err:
            if errors == "coerce":
                continue  # np.full(...) implicitly fills with `pd.NA`
            raise err  # break loop and raise immediately

    return result, has_offset


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] string_to_pydatetime_vector_with_format(
    np.ndarray[str] arr,
    str format,
    datetime.tzinfo tz,
    bint utc,
    str errors
):
    """Convert an array of datetime strings into `datetime.datetime` objects
    using the given format string, localized to the given timezone.  Notes
    parsing errors according to the given error-handling rule.
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.full(arr_length, pd.NaT, dtype="O")

    for i in range(arr_length):
        try:
            result[i] = string_to_pydatetime_scalar_with_format(
                arr[i],
                format=format,
                tz=tz,
                utc=utc
            )
        except ValueError as err:
            if errors == "coerce":
                continue  # np.full(...) implicitly fills with `pd.NaT`
            raise err  # break loop and raise immediately

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] string_to_pydatetime_vector_parsed(
    np.ndarray[str] arr,
    object parser_info,
    datetime.tzinfo tz,
    bint utc,
    str errors
):
    """Convert an array of datetime strings into `datetime.datetime` objects
    using modified dateutil parsing, localized to the given timezone.  Notes
    parsing errors according to the given error-handling rule.
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.full(arr_length, pd.NaT)

    for i in range(arr_length):
        try:
            result[i] = string_to_pydatetime_scalar_parsed(
                arr[i],
                parser_info=parser_info,
                tz=tz,
                utc=utc
            )
        except dateutil.parser.ParserError as err:
            if errors == "coerce":
                continue  # np.full(...) implicitly fills with `pd.NaT`
            raise err  # break loop and raise immediately

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] string_to_pydatetime_vector_with_fallback(
    np.ndarray[str] arr,
    str format,
    object parser_info,
    datetime.tzinfo tz,
    bint utc,
    str errors
):
    """Convert an array of datetime strings into `datetime.datetime` objects
    using the given format string where applicable and modified dateutil
    parsing where it is not.  Localizes result to the given timezone and notes
    parsing errors according to the given error-handling rule.
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.full(arr_length, pd.NaT)

    for i in range(arr_length):
        try:
            result[i] = string_to_pydatetime_scalar_with_fallback(
                arr[i],
                format=format,
                parser_info=parser_info,
                tz=tz,
                utc=utc
            )
        except dateutil.parser.ParserError as err:
            if errors == "coerce":
                continue  # np.full(...) implicitly fills with `pd.NaT`
            raise err  # break loop and raise immediately

    return result


#######################
####    Helpers    ####
#######################


def _iso_8601_to_ns(
    arg: str | np.ndarray | pd.Series,
    errors: str = "raise"
) -> tuple[int | np.ndarray | pd.Series, bool | np.ndarray, bool]:
    """Helper to convert ISO 8601 strings into nanosecond offsets from the UTC
    epoch.

    Parameters
    ----------
    arg : str | array-like
        A scalar or vector of ISO 8601 strings.
    errors : {'raise', 'coerce'}, default 'raise'
        The error-handling rule to apply.  If `errors='ignore'`-like behavior
        is desired, catch the ValueError raised by this function in the caller.

    Returns
    -------
    tuple[int | array-like, bool | array-like]
        A 2-tuple, the first index of which contains the result of the
        nanosecond conversion, with `None` as a missing value in the case of
        `errors='coerce'`.  The second element contains an index of which
        strings in `arg` contained UTC offset information ('Z', '+xxxx', etc.)
    """
    # np.ndarray
    if isinstance(arg, np.ndarray):
        # convert fixed-length numpy strings into python strings
        if np.issubdtype(arg.dtype, "U"):
            arg = arg.astype("O")
        return iso_8601_string_to_ns_vector(arg, errors=errors)

    # pd.Series
    if isinstance(arg, pd.Series):
        result, has_offset = iso_8601_string_to_ns_vector(
            arg.to_numpy(),
            errors=errors
        )
        return pd.Series(result, index=arg.index, copy=False), has_offset

    # scalar
    try:
        return iso_8601_string_to_ns_scalar(arg)
    except ValueError as err:
        if errors == "coerce":
            return (None, False)
        raise err


def _iso_8601_to_pydatetime(
    arg: str | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo,
    utc: bool,
    errors: str
) -> datetime.datetime | np.ndarray | pd.Series:
    """Helper to convert ISO 8601 strings into `datetime.datetime` objects.

    Does so in 2 passes, one which converts each ISO 8601 string into
    a nanosecond offset from utc, and then another that converts those
    nanosecond offsets into `datetime.datetime` objects.  Localization of naive
    datetimes to a non-naive/UTC timezone occurs in a potential 3rd pass.

    Parameters
    ----------
    arg : str | array-like
        A scalar or vector of ISO 8601 strings.
    tz : datetime.tzinfo | None
        The timezone to localize results to.
    errors : {'raise', 'coerce'}
        The error-handling rule to apply.  If `errors='ignore'`-like behavior
        is desired, catch the ValueError raised by this function in the caller.

    Returns
    -------
    datetime.datetime | array-like
        A scalar or vector of `datetime.datetime` objects, localized to `tz`.
    """
    # convert iso strings to ns, and then ns to np.datetime64
    result, has_offset = _iso_8601_to_ns(arg, errors=errors)

    # TODO: return to has_errors approach?  Saves a call to pd.notna() and a
    # level of indentation here - no `valid.all()`, `result is None`

    # check for parsing errors
    if errors == "coerce":
        # np.ndarray/pd.Series
        if isinstance(arg, (np.ndarray, pd.Series)):
            valid = pd.notna(arg)
            if not valid.all():  # at least 1 missing value
                if valid.any():  # at least 1 non-missing value
                    if utc or tz is None or is_utc(tz):
                        result[valid] = ns_to_pydatetime(result[valid], tz=tz)
                    else:
                        subset = ns_to_pydatetime(result[valid], tz=None)
                        result[valid] = localize_pydatetime(
                            subset,
                            tz=tz,
                            utc=has_offset
                        )
                result[~valid] = pd.NaT
                return result

        # scalar
        if result is None:
            return pd.NaT

    # no errors encountered
    if utc or tz is None or is_utc(tz):
        return ns_to_pydatetime(result, tz=tz)
    return localize_pydatetime(
        ns_to_pydatetime(result, tz=None),
        tz=tz,
        utc=has_offset
    )


def _string_to_pydatetime_with_format(
    arg: str | np.ndarray | pd.Series,
    format: str,
    tz: datetime.tzinfo,
    utc: bool,
    errors: str
) -> datetime.datetime | np.ndarray | pd.Series:
    """Helper to convert datetime strings into `datetime.datetime` objects
    using the given format string.

    Parameters
    ----------
    arg : str | array-like
        A scalar or vector of datetime strings.
    format : str
        An `strftime()`-compatible format string.
    tz : datetime.tzinfo | None
        The timezone to localize results to.
    errors : {'raise', 'coerce'}
        The error-handling rule to apply.  If `errors='ignore'`-like behavior
        is desired, catch the ValueError raised by this function in the caller.

    Returns
    -------
    datetime.datetime | array-like
        A scalar or vector of `datetime.datetime` objects, localized to `tz`.
    """
    # np.ndarray
    if isinstance(arg, np.ndarray):
        return string_to_pydatetime_vector_with_format(
            arg,
            format=format,
            tz=tz,
            utc=utc,
            errors=errors
        )

    # pd.Series
    if isinstance(arg, pd.Series):
        index = arg.index
        arg = arg.to_numpy()
        arg = string_to_pydatetime_vector_with_format(
            arg,
            format=format,
            tz=tz,
            utc=utc,
            errors=errors
        )
        return pd.Series(arg, index=index, dtype="O", copy=False)

    # scalar
    try:
        return string_to_pydatetime_scalar_with_format(
            arg,
            format=format,
            tz=tz,
            utc=utc
        )
    except ValueError as err:
        if errors == "coerce":
            return pd.NaT
        raise err


def _string_to_pydatetime_parsed(
    arg: str | np.ndarray | pd.Series,
    parser_info: dateutil.parser.parserinfo,
    tz: datetime.tzinfo,
    utc: bool,
    errors: str
) -> datetime.datetime | np.ndarray | pd.Series:
    """Helper to convert datetime strings into `datetime.datetime` objects
    using dateutil parsing rules.

    Parameters
    ----------
    arg : str | array-like
        A scalar or vector of ISO 8601 strings.
    parser_info : dateutil.parser.parserinfo
        A `dateutil` parserinfo object defining the parsing rules to apply.
        This can specify values for `dayfirst` and `yearfirst` (among others)
        for the `dateutil` parsing pipeline.
    tz : datetime.tzinfo | None
        The timezone to localize results to.
    errors : {'raise', 'coerce'}
        The error-handling rule to apply.  If `errors='ignore'`-like behavior
        is desired, catch the ValueError raised by this function in the caller.

    Returns
    -------
    datetime.datetime | array-like
        A scalar or vector of `datetime.datetime` objects, localized to `tz`.
    """
    # np.ndarray
    if isinstance(arg, np.ndarray):
        return string_to_pydatetime_vector_parsed(
            arg,
            parser_info=parser_info,
            tz=tz,
            utc=utc,
            errors=errors
        )

    # pd.Series
    if isinstance(arg, pd.Series):
        index = arg.index
        arg = arg.to_numpy()
        arg = string_to_pydatetime_vector_parsed(
            arg,
            parser_info=parser_info,
            tz=tz,
            utc=utc,
            errors=errors
        )
        return pd.Series(arg, index=index, dtype="O", copy=False)

    # scalar
    try:
        return string_to_pydatetime_scalar_parsed(
            arg,
            parser_info=parser_info,
            tz=tz,
            utc=utc
        )
    except dateutil.parser.ParserError as err:
        if errors == "coerce":
            return pd.NaT
        raise err


def _string_to_pydatetime_with_fallback(
    arg: str | np.ndarray | pd.Series,
    format: str,
    parser_info: dateutil.parser.parserinfo,
    tz: datetime.tzinfo,
    utc: bool,
    errors: str
) -> datetime.datetime | np.ndarray | pd.Series:
    """Helper to convert datetime strings into `datetime.datetime` objects,
    using the given format string where applicable and falling back to dateutil
    where it is not.

    Parameters
    ----------
    arg : str | array-like
        A scalar or vector of ISO 8601 strings.
    format : str
        An `strftime()`-compatible format string.
    parser_info : dateutil.parser.parserinfo
        A `dateutil` parserinfo object defining the parsing rules to apply.
        This can specify values for `dayfirst` and `yearfirst` (among others)
        for the `dateutil` parsing pipeline.
    tz : datetime.tzinfo | None
        The timezone to localize results to.
    errors : {'raise', 'coerce'}
        The error-handling rule to apply.  If `errors='ignore'`-like behavior
        is desired, catch the ValueError raised by this function in the caller.

    Returns
    -------
    datetime.datetime | array-like
        A scalar or vector of `datetime.datetime` objects, localized to `tz`.
    """
    # np.ndarray
    if isinstance(arg, np.ndarray):
        return string_to_pydatetime_vector_with_fallback(
            arg,
            format=format,
            parser_info=parser_info,
            tz=tz,
            utc=utc,
            errors=errors
        )

    # pd.Series
    if isinstance(arg, pd.Series):
        index = arg.index
        arg = arg.to_numpy()
        arg = string_to_pydatetime_vector_with_fallback(
            arg,
            format=format,
            parser_info=parser_info,
            tz=tz,
            utc=utc,
            errors=errors
        )
        return pd.Series(arg, index=index, dtype="O", copy=False)

    # scalar
    try:
        return string_to_pydatetime_scalar_with_fallback(
            arg,
            format=format,
            parser_info=parser_info,
            utc=utc,
            tz=tz
        )
    except dateutil.parser.ParserError as err:
        if errors == "coerce":
            return pd.NaT
        raise err


######################
####    Public    ####
######################


def is_iso_8601(string: str) -> bool:
    """Infer whether a string can be interpreted as an ISO 8601 date."""
    return iso_8601_pattern.match(string) is not None


def iso_8601_to_ns(
    arg: str | np.ndarray | pd.Series,
    errors: str = "raise"
) -> int | np.ndarray | pd.Series:
    """Convert ISO 8601 datetime strings into nanosecond offsets from the utc
    epoch ('1970-01-01 00:00:00+0000').

    This function can accept any valid ISO 8601 string, positive or negative,
    of any magnitude, with or without an included timezone offset.  It does not
    overflow, and is fully vectorized for both numpy arrays and pandas series.

    Parameters
    ----------
    arg : str | array-like
        An ISO 8601 datetime string, or a vector of such strings.
    errors : {'raise', 'ignore', 'coerce'}, default 'raise'
        The error-handling rule to use if an invalid ISO 8601 string is
        encountered during parsing.  The behaviors are as follows:
            * `'raise'` - immediately raise a `ValueError`
            * `'ignore'` - return `arg` unmodified
            * `'coerce'` - fill with `pandas.NA` and continue

    Returns
    -------
    int | array-like
        The integer nanosecond offset(s) associated with the given datetime
        string(s).

    Raises
    ------
    ValueError
        If `errors='raise'` and `arg` contains an invalid ISO 8601 string.

    Examples
    --------
    Strings can have precision from years to nanoseconds:

    >>> iso_8601_to_ns("2022")
    1640995200000000000
    >>> iso_8601_to_ns("2022-10")
    1664582400000000000
    >>> iso_8601_to_ns("2022-10-15")
    1665792000000000000
    >> iso_8601_to_ns("2022-10-15 08")
    1665820800000000000
    >>> iso_8601_to_ns("2022-10-15T08:47")
    1665823620000000000
    >>> iso_8601_to_ns("2022-10-15 08:47:23")
    1665823643000000000
    >>> iso_8601_to_ns("2022-10-15T08:47:23.123")
    1665823643123000000
    >>> iso_8601_to_ns("2022-10-15 08:47:23.123456")
    1665823643123456000
    >>> iso_8601_to_ns("2022-10-15T08:47:23.123456789")
    1665823643123456789

    They can be signed or unsigned:

    >>> iso_8601_to_ns("-43-03-01")  # Assassination of Julius Caesar
    -63519033600000000000
    >>> iso_8601_to_ns("18-08-19")  # Death of Caesar Augustus
    -61579267200000000000

    With or without timezone information:

    >>> iso_8601_to_ns("1970-01-01 00:00:00")
    0
    >>> iso_8601_to_ns("1970-01-01 00:00:00Z")
    0
    >>> iso_8601_to_ns("1970-01-01 00:00:00+0800")
    -28800000000000
    >>> iso_8601_to_ns("1970-01-01 00:00:00-0800")
    28800000000000

    And can be arbitrarily large:

    >>> iso_8601_to_ns(f"{2**63 - 1}-01-01 00:00:00.123456789")
    291061508645168328945024000123456789

    Unrealistic ISO 8601 strings are rejected:

    >>> iso_8601_to_ns("2001-13-15 00:00:00")
    Traceback (most recent call last):
        ...
    ValueError: invalid isoformat string '2001-13-15 00:00:00'
    >>> iso_8601_to_ns("2001-02-29 00:00:00")
    Traceback (most recent call last):
        ...
    ValueError: invalid isoformat string '2001-02-29 00:00:00'
    >>> iso_8601_to_ns("2001-01-01 60:00:00")
    Traceback (most recent call last):
        ...
    ValueError: invalid isoformat string '2001-01-01 60:00:00'
    >>> iso_8601_to_ns("2001-01-01 00:60:00")
    Traceback (most recent call last):
        ...
    ValueError: invalid isoformat string '2001-01-01 00:60:00'
    >>> iso_8601_to_ns("2001-01-01 00:00:60")
    Traceback (most recent call last):
        ...
    ValueError: invalid isoformat string '2001-01-01 00:00:60'

    And leap days are handled correctly:

    >>> iso_8601_to_ns("2000-02-29 00:00:00")
    951782400000000000

    Strings can also be vectorized:

    >>> iso_8601_to_ns(
    ...     pd.Series([f"1970-01-01 00:00:00.00000000{i}" for i in range(1, 4)])
    ... )
    0    1
    1    2
    2    3
    dtype: object
    >>> iso_8601_to_ns(
    ...     np.array([f"1970-01-01 00:00:00.00000000{i}" for i in range(1, 4)])
    ... )
    array([1, 2, 3], dtype=object)
    """
    try:
        return _iso_8601_to_ns(arg, errors=errors)[0]
    except ValueError as err:
        if errors == "ignore":
            return arg
        raise err


def string_to_pandas_timestamp(
    arg: str | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = None,
    format: str = None,
    day_first: bool = False,
    year_first: bool = False,
    utc: bool = False,
    errors: str = "raise"
) -> pd.Timestamp | np.ndarray | pd.Series:
    """Convert datetime strings into `pandas.Timestamp` objects.

    Parameters
    ----------
    arg : str | array-like
        A datetime string or vector of such strings.  These can be in any
        form accepted by `pandas.to_datetime()`, which can include shorthand
        date strings in a variety of formats.
    tz : str | datetime.tzinfo | None, default None
        The timezone to localize results to.  This can be `None`, indicating a
        naive return type, an instance of `datetime.tzinfo` or one of its
        derivatives (from `pytz`, `zoneinfo`, etc.), or an IANA timezone
        database string ('US/Eastern', 'UTC', etc.).  The special value
        `'local'` is also accepted, referencing the system's local time zone.
    format : str, default None
        A `datetime.datetime.strftime()`-compliant format string to parse the
        given string(s) (e.g. '%d/%m/%Y').  Note that `'%f'` will parse all the
        way up to nanoseconds.  If this is omitted, this function will default
        to dateutil parsing with the `day_first` and `year_first` parameters.
    day_first : bool, default False
        Whether to interpret the first value in an ambiguous 3-integer date
        (e.g. '01/05/09') as the day (`True`) or month (`False`). If
        `year_first` is set to `True`, this distinguishes between YDM and YMD.
    year_first : bool, default False
        Whether to interpret the first value in an ambiguous 3-integer date
        (e.g. '01/05/09') as the year. If `True`, the first number is taken to
        be the year, otherwise the last number is taken to be the year.
    utc : bool, default False
        Controls the localization behavior of timezone-naive datetime strings.
        If this is set to `True`, naive datetime strings will be interpreted as
        UTC times, and will be *converted* from UTC to the specified `tz`.  If
        this is `False` (the default), naive datetime strings will be
        *localized* directly to `tz` instead.

        .. note:: If `utc=False` and `arg` contains mixed aware/naive and/or
            mixed timezone ISO strings, then nanosecond precision will be lost.
            This is a technical limitation of `pandas.to_datetime()`.

    errors : {'raise', 'ignore', 'coerce'}, default 'raise'
        The error-handling rule to use if an invalid datetime string is
        encountered during parsing.  The behaviors are as follows:
            * `'raise'` - immediately raise a `ValueError`
            * `'ignore'` - return `arg` unmodified
            * `'coerce'` - fill with `None` and continue

    Returns
    -------
    pd.Timestamp | array-like
        A `pandas.Timestamp` or vector of `pandas.Timestamp` objects containing
        the datetime equivalents of the given strings, localized to `tz`.

    Raises
    ------
    dateutil.parser.ParserError
        If `errors='raise'` and one or more strings in `arg` could not be
        parsed.
    OverflowError
        If `errors='raise'` and one or more strings in `arg` exceed the
        representable range of `pandas.Timestamp` objects
        ([`'1677-09-21 00:12:43.145224193'` -
        `'2262-04-11 23:47:16.854775807'`]).

    Examples
    --------
    Strings can be in ISO 8601 format:

    >>> string_to_pandas_timestamp("1970-01-01 00:00:00")
    Timestamp('1970-01-01 00:00:00')

    Localized to any timezone:

    >>> string_to_pandas_timestamp("1970-01-01 00:00:00", tz="US/Pacific")
    Timestamp('1970-01-01 00:00:00-0800', tz='US/Pacific')
    >>> string_to_pandas_timestamp("1970-01-01 00:00:00Z", tz="US/Pacific")
    Timestamp('1969-12-31 16:00:00-0800', tz='US/Pacific')
    >>> string_to_pandas_timestamp("1970-01-01 00:00:00+01:00", tz="US/Pacific")
    Timestamp('1969-12-31 15:00:00-0800', tz='US/Pacific')

    They can also be relative signifiers ('Today', 'Now'):

    >>> string_to_pandas_timestamp("today")
    Timestamp('2022-09-22 17:06:58.837417')  #random

    Or quarterly dates:

    >>> string_to_pandas_timestamp("4Q2023")
    Timestamp('2023-10-01 00:00:00')
    >>> string_to_pandas_timestamp("23q4")
    Timestamp('2023-10-01 00:00:00')

    You can use `strftime()`-like format strings:

    >>> string_to_pandas_timestamp("04.01.2022", format="%d.%m.%Y")
    Timestamp('2022-01-04 00:00:00')
    >>> string_to_pandas_timestamp(
    ...     "04.01.2022",
    ...     format="%d.%m.%Y",
    ...     tz="US/Eastern"
    ... )
    Timestamp('2022-01-04 00:00:00-0500', tz='US/Eastern')

    Or rely on `dateutil` parsing:

    >>> string_to_pandas_timestamp("4 Jan 2022")
    Timestamp('2022-01-04 00:00:00')
    >>> string_to_pandas_timestamp("December 7th, 1941 at 8 AM", tz="US/Hawaii")
    Timestamp('1941-12-07 08:00:00-1030', tz='US/Hawaii')

    With ambiguous dates:

    >>> string_to_pandas_timestamp("01/05/09")
    Timestamp('2009-01-05 00:00:00')
    >>> string_to_pandas_timestamp("01/05/09", day_first=True)
    Timestamp('2009-05-01 00:00:00')
    >>> string_to_pandas_timestamp("01/05/09", year_first=True)
    Timestamp('2001-05-09 00:00:00')
    >>> string_to_pandas_timestamp("01/05/09", day_first=True, year_first=True)
    Timestamp('2001-09-05 00:00:00')

    Strings can also be vectorized:

    >>> strings = [f"1970-01-01 00:00:00.00000000{i}" for i in range(1, 4)]
    >>> strings
    ['1970-01-01 00:00:00.000000001', '1970-01-01 00:00:00.000000002', '1970-01-01 00:00:00.000000003']
    >>> string_to_pandas_timestamp(pd.Series(strings))
    0   1970-01-01 00:00:00.000000001
    1   1970-01-01 00:00:00.000000002
    2   1970-01-01 00:00:00.000000003
    dtype: datetime64[ns]
    >>> string_to_pandas_timestamp(np.array(strings))
    array([Timestamp('1970-01-01 00:00:00.000000001'),
       Timestamp('1970-01-01 00:00:00.000000002'),
       Timestamp('1970-01-01 00:00:00.000000003')], dtype=object)
    """
    # ensure format doesn't contradict day_first, year_first
    if format is not None and (day_first or year_first):
        raise RuntimeError(f"if a `format` string is given, both `day_first` "
                           f"and `year_first` must be False")

    # resolve timezone
    tz = timezone(tz)

    # get kwarg dict for pd.to_datetime
    if format is not None:
        kwargs = {"format": format, "exact": False, "utc": utc or is_utc(tz),
                  "errors": errors}
    else:
        kwargs = {"dayfirst": day_first, "yearfirst": year_first,
                  "infer_datetime_format": True, "utc": utc or is_utc(tz),
                  "errors": errors}

    # convert using pd.to_datetime
    try:
        arg = pd.to_datetime(arg, **kwargs)

    # exception 1: outside pd.Timestamp range, but within datetime.datetime
    except pd._libs.tslibs.np_datetime.OutOfBoundsDatetime as err:
        raise OverflowError(str(err)) from err

    # exception 2: bad string or outside datetime.datetime range
    except dateutil.parser.ParserError as err:  # ambiguous
        if err.__cause__:  # only overflow has a non-None __cause__ attr
            raise OverflowError(str(err)) from err
        raise err

    # localize to final timezone
    # Note: there are 3 possible output types from pd.to_datetime, depending
    # on input type and whether there are mixed timezones and/or aware/naive.
    try:
        # pd.Series -> pd.Series
        if isinstance(arg, pd.Series):
            # input is homogenous aware/naive with consistent timezone
            if pd.api.types.is_datetime64_dtype(arg):  # use `.dt` namespace
                if not arg.dt.tz:  # Series is naive
                    if tz is not None:  # replace with final tz
                        arg = arg.dt.tz_localize(tz)
                else:  # Series is aware
                    if not is_utc(tz):  # utc timezones caught by pd.to_datetime
                        arg = arg.dt.tz_convert(tz)
                return arg

            # input is possibly mixed aware/naive and/or mixed timezone
            return localize_pydatetime(arg, tz, utc=False).infer_objects()

        # np.ndarray -> DatetimeIndex or Index
        if isinstance(arg, pd.Index):
            # input is homogenous aware/naive with consistent timezone
            if isinstance(arg, pd.DatetimeIndex):  # use scalar accessors
                if not arg.tzinfo:  # DatetimeIndex is naive
                    if tz is not None:  # replace with final tz
                        arg = arg.tz_localize(tz)
                else:  # DatetimeIndex is aware
                    if not is_utc(tz):  # utc timezones caught by pd.to_datetime
                        arg = arg.tz_convert(tz)

            else:  # input is possibly mixed aware/naive and/or mixed timezone
                arg = localize_pydatetime(arg.to_series(), tz, utc=False)
                arg = arg.infer_objects()

            # convert back to numpy array
            return arg.to_numpy(dtype="O")

        # scalar -> pd.Timestamp
        if not arg.tzinfo and tz is not None:  # replace with final tz
            return arg.tz_localize(tz)
        if arg.tzinfo and not is_utc(tz):  # convert to final tz
            return arg.tz_convert(tz)
        return arg

    # exception 3: overflow induced by timezone localization
    except pd._libs.tslibs.np_datetime.OutOfBoundsDatetime as err:
        raise OverflowError(str(err)) from err


def string_to_pydatetime(
    arg: str | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = None,
    format: str = None,
    day_first: bool = False,
    year_first: bool = False,
    utc: bool = False,
    errors: str = "raise"
) -> datetime.datetime | np.ndarray | pd.Series:
    """Convert datetime strings into `datetime.datetime` objects.

    Parameters
    ----------
    arg : str | array-like
        A datetime string or vector of such strings.  These can be in any
        format recognized by dateutil, as well as the relative signifiers
        'now', and 'today', as well as quarterly dates ('4Q2022', '22q1',
        etc.).  This attempts to match the accepted values of
        `pandas.to_datetime()`, though it does not call that function
        explicitly.
    tz : str | datetime.tzinfo | None, default None
        The timezone to localize results to.  This can be `None`, indicating a
        naive return type, an instance of `datetime.tzinfo` or one of its
        derivatives (from `pytz`, `zoneinfo`, etc.), or an IANA timezone
        database string ('US/Eastern', 'UTC', etc.).  The special value
        `'local'` is also accepted, referencing the system's local time zone.
    format : str, default None
        A `datetime.datetime.strftime()`-compliant format string to parse the
        given string(s) (e.g. '%d/%m/%Y').  If this is omitted, this function
        will default to dateutil parsing with the `day_first` and `year_first`
        parameters.
    day_first : bool, default False
        Whether to interpret the first value in an ambiguous 3-integer date
        (e.g. '01/05/09') as the day (`True`) or month (`False`). If
        `year_first` is set to `True`, this distinguishes between YDM and YMD.
    year_first : bool, default False
        Whether to interpret the first value in an ambiguous 3-integer date
        (e.g. '01/05/09') as the year. If `True`, the first number is taken to
        be the year, otherwise the last number is taken to be the year.
    utc : bool, default False
        Controls the localization behavior of timezone-naive datetime strings.
        If this is set to `True`, naive datetime strings will be interpreted as
        UTC times, and will be *converted* from UTC to the specified `tz`.  If
        this is `False` (the default), naive datetime strings will be
        *localized* directly to `tz` instead.
    errors : {'raise', 'ignore', 'coerce'}, default 'raise'
        The error-handling rule to use if an invalid datetime string is
        encountered during parsing.  The behaviors are as follows:
            * `'raise'` - immediately raise a `ValueError`
            * `'ignore'` - return `arg` unmodified
            * `'coerce'` - fill with `None` and continue

    Returns
    -------
    datetime.datetime | array-like
        A `datetime.datetime` or vector of `datetime.datetime` objects
        containing the datetime equivalents of the given strings, localized to
        `tz`.

    Raises
    ------
    dateutil.parser.ParserError
        If `errors='raise'` and one or more strings in `arg` could not be
        parsed.
    ValueError
        If `errors='raise'` and `arg` contains ISO 8601 strings, and one or
        more of those strings is invalid.
    OverflowError
        If `errors='raise'` and one or more strings in `arg` exceed the
        representable range of `datetime.datetime` objects
        ([`'0001-01-01 00:00:00'` - `'9999-12-31 23:59:59.999999'`]).

    Examples
    --------
    Strings can be in ISO 8601 format:

    >>> string_to_pydatetime("1970-01-01 00:00:00")
    datetime.datetime(1970, 1, 1, 0, 0)

    Localized to any timezone:

    >>> string_to_pydatetime("1970-01-01 00:00:00", tz="US/Pacific")
    datetime.datetime(1970, 1, 1, 0, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
    >>> string_to_pydatetime("1970-01-01 00:00:00Z", tz="US/Pacific")
    datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
    >>> string_to_pydatetime("1970-01-01 00:00:00+01:00", tz="US/Pacific")
    datetime.datetime(1969, 12, 31, 15, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)

    They can also be relative signifiers ('Today', 'Now'):

    >>> string_to_pydatetime("today")
    datetime.datetime(2022, 9, 23, 0, 8, 4, 855833)  #random

    Or quarterly dates:

    >>> string_to_pydatetime("4Q2023")
    datetime.datetime(2023, 10, 1, 0, 0)
    >>> string_to_pydatetime("23q4")
    datetime.datetime(2023, 10, 1, 0, 0)

    You can use `strftime()`-like format strings:

    >>> string_to_pydatetime("04.01.2022", format="%d.%m.%Y")
    datetime.datetime(2022, 1, 4, 0, 0)
    >>> string_to_pydatetime(
    ...     "04.01.2022",
    ...     format="%d.%m.%Y",
    ...     tz="US/Eastern"
    ... )
    datetime.datetime(2022, 1, 4, 0, 0, tzinfo=<DstTzInfo 'US/Eastern' EST-1 day, 19:00:00 STD>)

    Or rely on `dateutil` parsing:

    >>> string_to_pydatetime("4 Jan 2022")
    datetime.datetime(2022, 1, 4, 0, 0)
    >>> string_to_pydatetime("December 7th, 1941 at 8 AM", tz="US/Hawaii")
    datetime.datetime(1941, 12, 7, 8, 0, tzinfo=<DstTzInfo 'US/Hawaii' HST-1 day, 13:30:00 STD>)
    >>> string_to_pydatetime("Today is January 1, 2047 8:21:00AM", tz="UTC")
    datetime.datetime(2047, 1, 1, 8, 21, tzinfo=<UTC>)

    With ambiguous dates:

    >>> string_to_pydatetime("01/05/09")
    datetime.datetime(2009, 1, 5, 0, 0)
    >>> string_to_pydatetime("01/05/09", day_first=True)
    datetime.datetime(2009, 5, 1, 0, 0)
    >>> string_to_pydatetime("01/05/09", year_first=True)
    datetime.datetime(2001, 5, 9, 0, 0)
    >>> string_to_pydatetime("01/05/09", day_first=True, year_first=True)
    datetime.datetime(2001, 9, 5, 0, 0)

    Strings can also be vectorized:

    >>> strings = [f"1970-01-01 00:00:00.00000{i}" for i in range(1, 4)]
    >>> strings
    ['1970-01-01 00:00:00.000001', '1970-01-01 00:00:00.000002', '1970-01-01 00:00:00.000003']
    >>> string_to_pydatetime(pd.Series(strings))
    0    1970-01-01 00:00:00.000001
    1    1970-01-01 00:00:00.000002
    2    1970-01-01 00:00:00.000003
    dtype: object
    >>> string_to_pydatetime(np.array(strings))
    array([datetime.datetime(1970, 1, 1, 0, 0, 0, 1),
       datetime.datetime(1970, 1, 1, 0, 0, 0, 2),
       datetime.datetime(1970, 1, 1, 0, 0, 0, 3)], dtype=object)
    """
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
        try:
            return _string_to_pydatetime_with_format(
                arg,
                format=format,
                tz=tz,
                utc=utc,
                errors=errors
            )
        except ValueError as err:
            if errors == "ignore":
                return arg
            raise err

    # if no format is given, try ISO 8601
    if isinstance(arg, np.ndarray):
        element = arg[0]
    elif isinstance(arg, pd.Series):
        element = arg.iloc[0]
    else:
        element = arg
    if is_iso_8601(element) and "-" in element:  # ignore naked years
        try:
            return _iso_8601_to_pydatetime(arg, tz=tz, utc=utc, errors="raise")
        except ValueError as err:
            pass  # fall back to dateutil

    # if no format and not ISO 8601, try to infer format like pd.to_datetime
    infer = pd.core.tools.datetimes._guess_datetime_format_for_array
    if isinstance(arg, np.ndarray):
        format = infer(arg)
    elif isinstance(arg, pd.Series):
        format = infer(arg.to_numpy())
    else:
        format = infer(np.array([arg]))

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

        try:
            return _string_to_pydatetime_with_fallback(
                arg,
                format=format,
                parser_info=parser_info,
                tz=tz,
                utc=utc,
                errors=errors
            )
        except dateutil.parser.ParserError as err:
            if errors == "ignore":
                return arg
            raise err

    # if no format could be inferred, use dateutil parsing directly
    try:
        return _string_to_pydatetime_parsed(
            arg,
            parser_info = parser_info,
            tz=tz,
            utc=utc,
            errors=errors
        )
    except dateutil.parser.ParserError as err:
        if errors == "ignore":
            return arg
        raise err


def string_to_numpy_datetime64(
    arg: str | np.ndarray | pd.Series,
    unit: str = None,
    rounding: str = "down",
    errors: str = "raise"
) -> np.datetime64 | np.ndarray | pd.Series:
    """Convert ISO 8601 datetime strings into `numpy.datetime64` objects.

    Parameters
    ----------
    arg : str | array-like
        An ISO 8601 datetime string or vector of such strings.  These must be
        in strict ISO format, as `numpy.datetime64` objects do not support
        arbitrary string parsing.  In exchange, they can represent almost
        arbitrarily large dates (beyond the current age of the universe).
    unit : {'ns', 'us', 'ms', 's', 'm', 'h', 'D', 'W', 'M', 'Y'}, default None
        The unit to use for the returned datetime64 objects.  If `None`, this
        will attempt to automatically find the highest resolution unit that
        can fully represent all of the given strings.  This unit promotion is
        overflow-safe.
    rounding : {'floor', 'ceiling', 'down', 'up', 'half_floor', 'half_ceiling',
    'half_down', 'half_up', 'half_even'}, default 'down'
        The rounding rule to use when one or more strings contain precision
        below `unit`.  This applies equally in the case of unit promotion with
        respect to the final chosen unit.
    errors : {'raise', 'ignore', 'coerce'}, default 'raise'
        The error-handling rule to use if an invalid ISO 8601 string is
        encountered during parsing.  The behaviors are as follows:
            * `'raise'` - immediately raise a `ValueError`
            * `'ignore'` - return `arg` unmodified
            * `'coerce'` - fill with `None` and continue

    Returns
    -------
    numpy.datetime64 | array-like
        A `numpy.datetime64` or vector of `numpy.datetime64` objects containing
        the datetime equivalents of the given ISO strings, with the given
        `unit`.

    Raises
    ------
    ValueError
        If `errors='raise'` and `arg` contains an invalid ISO 8601 string.
    OverflowError
        If `errors='raise'` and one or more strings in `arg` exceed the
        representable range of `numpy.datetime64` objects with the given `unit`
        (up to [`'-9223372036854773837-01-01 00:00:00'` -
        `'9223372036854775807-01-01 00:00:00'`]).

    Examples
    --------
    Strings must be in ISO 8601 format, with or without timezone information:

    >>> string_to_numpy_datetime64("1970-01-01 00:00:00")
    numpy.datetime64('1970-01-01T00:00:00.000000000')
    >>> string_to_numpy_datetime64("1970-01-01 00:00:00Z")
    numpy.datetime64('1970-01-01T00:00:00.000000000')
    >>> string_to_numpy_datetime64("1970-01-01 00:00:00-0800")
    numpy.datetime64('1970-01-01T08:00:00.000000000')

    `numpy.datetime64` units can be specified explicitly:

    >>> string_to_numpy_datetime64("2042-10-15 12:34:56.789101112", unit="ns")
    numpy.datetime64('2042-10-15T12:34:56.789101112')
    >>> string_to_numpy_datetime64("2042-10-15 12:34:56.789101112", unit="us")
    numpy.datetime64('2042-10-15T12:34:56.789101')
    >>> string_to_numpy_datetime64("2042-10-15 12:34:56.789101112", unit="ms")
    numpy.datetime64('2042-10-15T12:34:56.789')
    >>> string_to_numpy_datetime64("2042-10-15 12:34:56.789101112", unit="s")
    numpy.datetime64('2042-10-15T12:34:56')
    >>> string_to_numpy_datetime64("2042-10-15 12:34:56.789101112", unit="m")
    numpy.datetime64('2042-10-15T12:34')
    >>> string_to_numpy_datetime64("2042-10-15 12:34:56.789101112", unit="h")
    numpy.datetime64('2042-10-15T12','h')
    >>> string_to_numpy_datetime64("2042-10-15 12:34:56.789101112", unit="D")
    numpy.datetime64('2042-10-15')
    >>> string_to_numpy_datetime64("2042-10-15 12:34:56.789101112", unit="W")
    numpy.datetime64('2042-10-09')
    >>> string_to_numpy_datetime64("2042-10-15 12:34:56.789101112", unit="M")
    numpy.datetime64('2042-10')
    >>> string_to_numpy_datetime64("2042-10-15 12:34:56.789101112", unit="Y")
    numpy.datetime64('2042')

    With customizable rounding:

    >>> string_to_numpy_datetime64(
    ...     "2042-10-15 12:34:56.789101112",
    ...     unit="s",
    ...     rounding="up"
    ... )
    numpy.datetime64('2042-10-15T12:34:57')

    Or they can be determined automatically, based on the input data:

    >>> string_to_numpy_datetime64(f"{2**50}-10-15 12:34:56")
    numpy.datetime64('1125899906842624-10-15')
    >>> string_to_numpy_datetime64(f"{2**50}-10-15 12:34:56", rounding="up")
    numpy.datetime64('1125899906842624-10-16')
    """
    # convert iso strings to ns, and then ns to np.datetime64
    try:
        result, _ = _iso_8601_to_ns(arg, errors=errors)
    except ValueError as err:
        if errors == "ignore":
            return arg
        raise err

    # TODO: return to has_errors approach?  Saves a call to pd.notna() and a
    # level of indentation here - no `valid.all()`, `result is None`

    # check for parsing errors
    if errors == "coerce":  # result may have missing values
        # np.ndarray
        if isinstance(arg, np.ndarray):
            valid = pd.notna(result)
            if not valid.all():  # at least 1 missing value
                if valid.any():  # at least 1 non-missing value
                    subset = ns_to_numpy_datetime64(
                        result[valid],
                        unit=unit,
                        rounding=rounding
                    )
                    result[valid] = subset
                    unit, _ = np.datetime_data(subset.dtype)
                    return result.astype(f"M8[{unit}]")

                # only missing values
                if unit is None:
                    unit = "ns"
                return result.astype(f"M8[{unit}]")

        # pd.Series
        if isinstance(arg, pd.Series):
            valid = pd.notna(result)
            if not valid.all():  # at least 1 missing value
                if valid.any():  # at least 1 non-missing value
                    subset = ns_to_numpy_datetime64(
                        result[valid],
                        unit=unit,
                        rounding=rounding
                    )
                    result[valid] = subset
                    unit, _ = np.datetime_data(subset.iloc[0])
                elif unit is None:  # only missing values
                    unit = "ns"
                result[~valid] = np.datetime64("nat", unit)
                return result

        # scalar
        if result is None:
            if unit is None:
                unit = "ns"
            return np.datetime64("nat", unit)

    # no missing values encountered
    return ns_to_numpy_datetime64(
        result,
        unit=unit,
        rounding=rounding
    )


def string_to_datetime(
    arg: str | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = None,
    format: str = None,
    day_first: bool = False,
    year_first: bool = False,
    utc: bool = False,
    errors: str = "raise"
) -> datetime_like | np.ndarray | pd.Series:
    """Convert datetime strings into arbitrary datetime objects.

    Parameters
    ----------
    arg : str | array-like
        A datetime string or vector of such strings.  These can be in any
        format recognized by dateutil, as well as the relative signifiers
        'now', and 'today', as well as quarterly dates ('4Q2022', '22q1',
        etc.).

            Note: If targeting the extended `numpy.datetime64` range, these
            must be in strict ISO 8601 format.

    tz : str | datetime.tzinfo | None, default None
        The timezone to localize results to.  This can be `None`, indicating a
        naive return type, an instance of `datetime.tzinfo` or one of its
        derivatives (from `pytz`, `zoneinfo`, etc.), or an IANA timezone
        database string ('US/Eastern', 'UTC', etc.).  The special value
        `'local'` is also accepted, referencing the system's local time zone.

            Note: timezone-naive datetime strings ('2022-01-04 12:00:00',
            '4 jan 2022', etc.) are *localized* directly to this timezone,
            whereas timezone-aware strings ('2022-01-04 12:00:00-0800') are
            *converted* to it instead.  This is robust against mixed
            aware/naive and/or mixed timezone string sequences.

            Note: `numpy.datetime64` objects do not carry timezone information.
            They always represent UTC times.

    format : str, default None
        A `datetime.datetime.strftime()`-compliant format string to parse the
        given string(s) (e.g. '%d/%m/%Y').  If this is omitted, this function
        will default to dateutil parsing with the `day_first` and `year_first`
        parameters.
    day_first : bool, default False
        Whether to interpret the first value in an ambiguous 3-integer date
        (e.g. '01/05/09') as the day (`True`) or month (`False`). If
        `year_first` is set to `True`, this distinguishes between YDM and YMD.
    year_first : bool, default False
        Whether to interpret the first value in an ambiguous 3-integer date
        (e.g. '01/05/09') as the year. If `True`, the first number is taken to
        be the year, otherwise the last number is taken to be the year.
    utc : bool, default False
        Controls the localization behavior of timezone-naive datetime strings.
        If this is set to `True`, naive datetime strings will be interpreted as
        UTC times, and will be *converted* from UTC to the specified `tz`.  If
        this is `False` (the default), naive datetime strings will be
        *localized* directly to `tz` instead.

        .. note:: If `utc=False` and `arg` contains mixed aware/naive and/or
            mixed timezone ISO strings, then nanosecond precision will be lost.
            This is a technical limitation of `pandas.to_datetime()`.

    errors : {'raise', 'ignore', 'coerce'}, default 'raise'
        The error-handling rule to use if an invalid datetime string is
        encountered during parsing.  The behaviors are as follows:
            * `'raise'` - immediately raise a `ValueError`
            * `'ignore'` - return `arg` unmodified
            * `'coerce'` - fill with `None` and continue

    Returns
    -------
    pd.Timestamp | datetime.datetime | numpy.datetime64 | array-like
        A datetime object or vector of datetime objects containing the
        highest-resolution datetime equivalents of the given strings, localized
        to `tz` (if possible).

    Raises
    ------
    dateutil.parser.ParserError
        If `errors='raise'` and one or more strings in `arg` could not be
        parsed.
    ValueError
        If `errors='raise'` and `arg` contains ISO 8601 strings, and one or
        more of those strings is invalid.
    OverflowError
        If `errors='raise'` and one or more strings in `arg` exceed the
        representable range of `numpy.datetime64` objects
        ([`'-9223372036854773837-01-01 00:00:00'` -
        `'9223372036854775807-01-01 00:00:00'`]).
    RuntimeError
        If the strings in `arg` exceed `datetime.datetime` range
        ([`'0001-01-01 00:00:00'` - `'9999-12-31 23:59:59.999999'`]) and `tz`
        is not either `None` or UTC.

    Examples
    --------
    Strings can be in ISO 8601 format:

    >>> string_to_datetime("1970-01-01 00:00:00")
    Timestamp('1970-01-01 00:00:00')

    Localized to any timezone:

    >>> string_to_datetime("1970-01-01 00:00:00", tz="US/Pacific")
    Timestamp('1970-01-01 00:00:00-0800', tz='US/Pacific')
    >>> string_to_datetime("1970-01-01 00:00:00Z", tz="US/Pacific")
    Timestamp('1969-12-31 16:00:00-0800', tz='US/Pacific')
    >>> string_to_datetime("1970-01-01 00:00:00+01:00", tz="US/Pacific")
    Timestamp('1969-12-31 15:00:00-0800', tz='US/Pacific')

    They can also be relative signifiers ('Today', 'Now'):

    >>> string_to_datetime("today")
    Timestamp('2022-09-22 17:06:58.837417')  #random

    Or quarterly dates:

    >>> string_to_datetime("4Q2023")
    Timestamp('2023-10-01 00:00:00')
    >>> string_to_datetime("23q4")
    Timestamp('2023-10-01 00:00:00')

    You can use `strftime()`-like format strings:

    >>> string_to_datetime("04.01.2022", format="%d.%m.%Y")
    Timestamp('2022-01-04 00:00:00')
    >>> string_to_datetime(
    ...     "04.01.2022",
    ...     format="%d.%m.%Y",
    ...     tz="US/Eastern"
    ... )
    Timestamp('2022-01-04 00:00:00-0500', tz='US/Eastern')

    Or rely on `dateutil` parsing:

    >>> string_to_datetime("4 Jan 2022")
    Timestamp('2022-01-04 00:00:00')
    >>> string_to_datetime("December 7th, 1941 at 8 AM", tz="US/Hawaii")
    Timestamp('1941-12-07 08:00:00-1030', tz='US/Hawaii')

    With ambiguous dates:

    >>> string_to_datetime("01/05/09")
    Timestamp('2009-01-05 00:00:00')
    >>> string_to_datetime("01/05/09", day_first=True)
    Timestamp('2009-05-01 00:00:00')
    >>> string_to_datetime("01/05/09", year_first=True)
    Timestamp('2001-05-09 00:00:00')
    >>> string_to_datetime("01/05/09", day_first=True, year_first=True)
    Timestamp('2001-09-05 00:00:00')

    Strings can also be vectorized:

    >>> strings = [f"1970-01-01 00:00:00.00000000{i}" for i in range(1, 4)]
    >>> strings
    ['1970-01-01 00:00:00.000000001', '1970-01-01 00:00:00.000000002', '1970-01-01 00:00:00.000000003']
    >>> string_to_datetime(pd.Series(strings))
    0   1970-01-01 00:00:00.000000001
    1   1970-01-01 00:00:00.000000002
    2   1970-01-01 00:00:00.000000003
    dtype: datetime64[ns]
    >>> string_to_datetime(np.array(strings))
    array([Timestamp('1970-01-01 00:00:00.000000001'),
       Timestamp('1970-01-01 00:00:00.000000002'),
       Timestamp('1970-01-01 00:00:00.000000003')], dtype=object)

    And overflow between datetime types is handled gracefully:

    >>> string_to_datetime("2262-04-11 23:47:16.854775807")
    Timestamp('2262-04-11 23:47:16.854775807')
    >>> string_to_datetime("2262-04-11 23:47:16.854775808")
    datetime.datetime(2262, 4, 11, 23, 47, 16, 854775)
    >>> string_to_datetime("April 11th, 2262 at 23:47:16.854775")
    Timestamp('2262-04-11 23:47:16.854775')
    >>> string_to_datetime("April 11th, 2262 at 23:47:16.854776")
    datetime.datetime(2262, 4, 11, 23, 47, 16, 854776)

    All the way up to `numpy.datetime64` range in the case of ISO strings:

    >>> string_to_datetime("9999-12-31 23:59:59.999999")
    datetime.datetime(9999, 12, 31, 23, 59, 59, 999999)
    >>> string_to_datetime("10000-01-01 00:00:00")
    numpy.datetime64('10000-01-01T00:00:00.000000')

    This includes overflow caused by timezone localization:

    >>> string_to_datetime("1677-09-21 00:12:43.145224193")
    Timestamp('1677-09-21 00:12:43.145224193')
    >>> string_to_datetime("1677-09-21 00:12:43.145224193", tz="Europe/Berlin")
    datetime.datetime(1677, 9, 21, 0, 12, 43, 145224, tzinfo=<DstTzInfo 'Europe/Berlin' LMT+0:53:00 STD>)
    >>> string_to_datetime("September 21st, 1677 at 00:12:43.145225")
    Timestamp('1677-09-21 00:12:43.145225')
    >>> string_to_datetime(
    ...     "September 21st, 1677 at 00:12:42.145225",
    ...     tz="Europe/Berlin"
    ... )
    datetime.datetime(1677, 9, 21, 0, 12, 42, 145225, tzinfo=<DstTzInfo 'Europe/Berlin' LMT+0:53:00 STD>)
    """
    # ensure format doesn't contradict day_first, year_first
    if format is not None and (day_first or year_first):
        raise RuntimeError(f"if a `format` string is given, both `day_first` "
                           f"and `year_first` must be False")

    # resolve timezone
    tz = timezone(tz)

    # convert fixed-length numpy string arrays to python strings
    if isinstance(arg, np.ndarray) and np.issubdtype(arg.dtype, "U"):
        arg = arg.astype("O")

    # pd.Timestamp
    try:
        return string_to_pandas_timestamp(
            arg,
            tz=tz,
            format=format,
            day_first=day_first,
            year_first=year_first,
            utc=utc,
            errors=errors
        )
    except OverflowError:
        pass

    # datetime.datetime
    try:
        return string_to_pydatetime(
            arg,
            tz=tz,
            format=format,
            day_first=day_first,
            year_first=year_first,
            utc=utc,
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
