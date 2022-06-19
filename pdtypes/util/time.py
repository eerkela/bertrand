from __future__ import annotations
import datetime
from lib2to3.pytree import convert
import re
from typing import Union

import numpy as np
import pandas as pd
import pytz
import tzlocal

from pdtypes.error import error_trace


"""
TODO: https://i.stack.imgur.com/uiXQd.png
"""


datetime_like = Union[pd.Timestamp, datetime.datetime, np.datetime64]
timedelta_like = Union[pd.Timedelta, datetime.timedelta, np.timedelta64]


_time_unit_regex = re.compile(r'^[^\[]+\[([^\]]+)\]$')
_to_ns = {
    "as": 1e-9,
    "fs": 1e-6,
    "ps": 1e-3,
    "ns": 1,
    "nanosecond": 1,
    "nanoseconds": 1,
    "us": int(1e3),
    "microsecond": int(1e3),
    "microseconds": int(1e3),
    "ms": int(1e6),
    "millisecond": int(1e6),
    "milliseconds": int(1e6),
    "s": int(1e9),
    "sec": int(1e9),
    "second": int(1e9),
    "seconds": int(1e9),
    "m": 60 * int(1e9),
    "minute": 60 * int(1e9),
    "minutes": 60 * int(1e9),
    "h": 60 * 60 * int(1e9),
    "hour": 60 * 60 * int(1e9),
    "hours": 60 * 60 * int(1e9),
    "D": 24 * 60 * 60 * int(1e9),
    "day": 24 * 60 * 60 * int(1e9),
    "days": 24 * 60 * 60 * int(1e9),
    "W": 7 * 24 * 60 * 60 * int(1e9),
    "week": 7 * 24 * 60 * 60 * int(1e9),
    "weeks": 7 * 24 * 60 * 60 * int(1e9)
}


def is_leap(
    year: int | pd.Series | np.ndarray
) -> bool | pd.Series | np.ndarray:
    """Returns True if the given year is a leap year."""
    return (year % 4 == 0) & ((year % 100 != 0) | (year % 400 == 0))


def leaps_between(
    begin: int | pd.Series | np.ndarray,
    end: int | pd.Series | np.ndarray
) -> int | pd.Series | np.ndarray:
    """Return the number of leap days between the years `begin` and `end`.

    Counts from the beginning of each year.  This means that
    `leaps_between(x, x + 1)` will return 1 if and only if `x` was a leap year.

    Identical to `calendar.leapdays()` from the built-in `calendar` package,
    but avoids an import and is very slightly faster (~10%).
    """
    count = lambda x: x // 4 - x // 100 + x // 400
    return count(end - 1) - count(begin - 1)  # range must be 0-indexed


def datetime64_components(dt: np.datetime64) -> tuple[int, int, int]:
    """
    Convert array of datetime64 to a calendar array of year, month, day, hour,
    minute, seconds, microsecond with these quantites indexed on the last axis.

    Parameters
    ----------
    dt : datetime64 array (...)
        numpy.ndarray of datetimes of arbitrary shape

    Returns
    -------
    cal : uint32 array (..., 7)
        calendar array with last axis representing year, month, day, hour,
        minute, second, microsecond
    """
    dt = np.array(dt)
    dtype = np.dtype([("year", "O"), ("month", "u1"), ("day", "u1"),
                      ("hour", "u1"), ("minute", "u1"), ("second", "u1"),
                      ("millisecond", "u2"), ("microsecond", "u4"),
                      ("nanosecond", "u4")])
    Y, M, D, h, m, s = [dt.astype(f"M8[{x}]") for x in "YMDhms"]
    return np.rec.fromarrays([Y.astype(np.int64).astype(object) + 1970,  # year
                              (M - Y) + 1,  # month
                              (D - M) + 1,  # day
                              (dt - D).astype("m8[h]"),  # hour
                              (dt - h).astype("m8[m]"),  # minute
                              (dt - m).astype("m8[s]"),  # second
                              (dt - s).astype("m8[ms]"),  # millisecond
                              (dt - s).astype("m8[us]"),  # microsecond
                              (dt - s).astype("m8[ns]")],  # nanosecond
                              dtype=dtype)


def decompose_date(dt: pd.Timestamp | datetime.datetime | np.datetime64):
    """Splits a datetime object into years, months, and days."""
    if isinstance(dt, np.datetime64):
        parts = datetime64_components(dt)
        return int(parts["year"]), int(parts["month"]), int(parts["day"])
    return dt.year, dt.month, dt.day


def reconstructed_date_code(month: int, day_of_month: int, year: int) -> int:
    """An example from 1999 that does the same job as `date_to_days`.
    It has been reproduced here for testing purposes, and was originally
    written in native C.

    source:
        https://stackoverflow.com/questions/11609769/efficient-algorithm-for-converting-number-of-days-to-years-including-leap-years
    """
    nbrOfDaysPer400Years = 146097
    nbrOfDaysPer100Years = 36524
    nbrOfDaysPer4Years = 1461
    nbrOfDaysPerYear = 365
    unixEpochBeginsOnDay = 135080
    day_offset = np.array([0, 31, 61, 92, 122, 153, 184, 214, 245, 275, 306,
                           337, 366])

    bYear = year - 1600
    bYday = day_offset[(month - 3) % 12] + day_of_month - 1
    bYear += (month - 3) // 12

    days, bYear = divmod(bYear, 400)
    days *= nbrOfDaysPer400Years

    temp, bYear = divmod(bYear, 100)
    days += nbrOfDaysPer100Years * temp

    temp, bYear = divmod(bYear, 4)
    days += nbrOfDaysPer4Years * temp + nbrOfDaysPerYear * bYear + bYday
    return days - unixEpochBeginsOnDay


def reconstructed_decode_date(dateCode: int):
    """An example from 1999 that does the same job as `days_to_date`.
    It has been reproduced here for testing purposes, and was originally
    written in native C.

    source:
        https://stackoverflow.com/questions/11609769/efficient-algorithm-for-converting-number-of-days-to-years-including-leap-years
    """
    nbrOfDaysPer400Years = 146097
    nbrOfDaysPer100Years = 36524
    nbrOfDaysPer4Years = 1461
    nbrOfDaysPerYear = 365
    unixEpochBeginsOnDay = 135080
    day_offset = np.array([0, 31, 61, 92, 122, 153, 184, 214, 245, 275, 306,
                           337, 366])

    dateCode += unixEpochBeginsOnDay
    temp, dateCode = divmod(dateCode, nbrOfDaysPer400Years)
    year = 400 * temp
    temp, dateCode = divmod(dateCode, nbrOfDaysPer100Years)
    if temp == 4:
        temp -= 1
        dateCode += nbrOfDaysPer100Years
    year += 100 * temp
    temp, dateCode = divmod(dateCode, nbrOfDaysPer4Years)
    year += 4 * temp
    temp, dateCode = divmod(dateCode, nbrOfDaysPerYear)
    if temp == 4:
        temp -= 1
        dateCode += nbrOfDaysPerYear
    year += temp

    alpha = 0
    beta = 11
    gamma = 0
    while True:
        gamma = (alpha + beta) // 2
        diff = day_offset[gamma] - dateCode
        if diff < 0:
            diff2 = day_offset[gamma + 1] - dateCode
            if diff2 < 0:
                alpha = gamma + 1
            elif diff2 == 0:
                gamma += 1
                break
            else:
                break
        elif diff == 0:
            break
        else:
            beta = gamma
    if gamma >= 10:
        year += 1
    return (year + 1600, (gamma + 2) % 12 + 1, dateCode - day_offset[gamma] + 1)


def date_to_days(year: int | list | tuple | np.ndarray,
                 month: int | list | tuple | np.ndarray,
                 day: int | list | tuple | np.ndarray) -> np.ndarray:
    """Converts a calendar date (year, month, day) into a day offset from the
    UTC epoch (1970-01-01).  Accepts >64-bit and missing values.
    """
    # using masked arrays to handle missing values
    nan_months = pd.isna(month)  # keep this around for later
    year = np.atleast_1d(np.ma.array(year,
                                     mask=pd.isna(year),
                                     fill_value=np.nan,
                                     dtype="O").filled())
    # have to fill months with 1 rather than nan so they can be used as index
    month = np.atleast_1d(np.ma.array(month,
                                      mask=nan_months,
                                      fill_value=1,
                                      dtype="O").filled())
    day = np.atleast_1d(np.ma.array(day,
                                    mask=pd.isna(day),
                                    fill_value=np.nan,
                                    dtype="O").filled())

    # normalize months to start with March 1st, indexed from 1 (January)
    year = year + (month - 3) // 12
    month = np.array((month - 3) % 12, dtype="i1")  # residual as integer index

    # convert months to day offsets from March 1st
    day_offset = np.array([0, 31, 61, 92, 122, 153, 184, 214, 245, 275, 306,
                           337, 366])
    leaps = leaps_between(0, year + 1) - 1
    result = (day - 1) + (day_offset[month]) + (365 * year + leaps) + 60 - 719528  # utc epoch
    result[nan_months] = np.nan  # correct for non-nan fill value
    return result


def days_to_date(days: int | list | tuple | np.ndarray) -> np.ndarray:
    """Converts an integer day offset from the UTC epoch (1970-01-01) back to
    its corresponding date, according to the proleptic Gregorian calendar.
    Accepts >64-bit and missing values.

    This function returns a `numpy.recarray`, whose elements are tuples
    `(year, month, day)`.  Each field can be independently accessed by
    attribute, i.e. `days_to_date(n_days)["year"]`.  This object can also be
    passed to `pandas.DataFrame` for easy conversion.
    """
    # use masked arrays to handle missing values
    days = np.atleast_1d(np.ma.array(days,
                                     mask=pd.isna(days),
                                     fill_value=np.nan,
                                     dtype="O").filled()) + 719468  # utc epoch


    # figure out years
    days_per_400_years = 146097
    days_per_100_years = 36524
    days_per_4_years = 1461
    days_per_year = 365

    # 400-year cycle
    years = 400 * (days // days_per_400_years)
    days = days % days_per_400_years

    # 100-year cycle
    temp = days // days_per_100_years
    days = days % days_per_100_years
    # put the leap day at end of the 400-year cycle
    days[temp == 4] += days_per_100_years
    temp[temp == 4] -= 1
    years += 100 * temp

    # 4-year cycle
    years += 4 * (days // days_per_4_years)
    days = days % days_per_4_years

    # 1-year
    temp = days // days_per_year
    days = days % days_per_year
    # put the leap day at the end of the 4-year cycle
    days[temp == 4] += days_per_year
    temp[temp == 4] -= 1
    years += temp

    # figure out months
    # optimization: treat March 1st as the first day of the year, forcing the
    # leap day (if present) to fall at the end of the year.  `day_offset` is
    # thus accurate for both leap and non-leap years.  Because of this, we
    # treat january and february as if they belong to the next year
    day_offset = np.array([0, 31, 61, 92, 122, 153, 184, 214, 245, 275, 306,
                           337, 366], dtype="i2")
    # searchsorted(..., side="right") - 1 puts ties on the right
    month_indices = day_offset.searchsorted(days, side="right") - 1
    months = (month_indices + 2) % 12 + 1  # convert to ordinary month index
    years[month_indices >= 10] += 1  # account for year offset

    # subtract off months to get final days in month
    days = days - day_offset[month_indices] + 1

    # return as recarray
    dtype = np.dtype([("year", "O"), ("month", "u1"), ("day", "u1")])
    return np.rec.fromarrays([years, months, days], dtype=dtype)


def alternate_days_to_date(days: int | list | tuple | np.ndarray) -> np.ndarray:
    """Identical to `days_to_date`, but attempts to find a direct conversion
    from days to years.  This works almost perfectly, but fails due to rounding
    errors for certain dates (<0.1% of results).
    """
    # using masked arrays to handle missing values
    days = np.atleast_1d(np.ma.array(days,
                                     mask=pd.isna(days),
                                     fill_value=np.nan,
                                     dtype="O").filled()) + 719528  # utc epoch

    # figure out years
    from_0 = 400 * days + 97
    years = from_0 // 146097  # exact -> NOT for 1095 +/- n * 1461, 4-year cycle
    leaps = is_leap(years)
    days = (from_0 % 146097) // 400 - 59 - leaps # residual days since March 1st

    # figure out months
    day_offset = np.array([0, 31, 61, 92, 122, 153, 184, 214, 245, 275, 306,
                           337, 366], dtype="i2")
    days = days % (365 + leaps)  # convert residuals to positive
    month_indices = day_offset.searchsorted(days, side="right") - 1
    months = (month_indices + 2) % 12 + 1

    # subtract off months to get final days in month
    days = days - day_offset[month_indices] + 1

    # return as recarray
    dtype = np.dtype([("year", "O"), ("month", "u1"), ("day", "u1")])
    return np.rec.fromarrays([years, months, days], dtype=dtype)


def days_accuracy(start_year: int, finish_year: int) -> None:
    """Testing function for `days_to_date`"""
    days_per_month = {
        "jan": 31,
        "feb": 28,
        "mar": 31,
        "apr": 30,
        "may": 31,
        "jun": 30,
        "jul": 31,
        "aug": 31,
        "sep": 30,
        "oct": 31,
        "nov": 30,
        "dec": 31
    }
    for year in range(start_year, finish_year + 1):
        for idx, (month, length) in enumerate(days_per_month.items()):
            if month == "feb":
                length += is_leap(year)
            for day in range(1, length + 1):
                mine = date_to_days(year, idx + 1, day)
                theirs = reconstructed_date_code(idx + 1, day, year)
                try:
                    assert mine == theirs
                except AssertionError:
                    err_msg = (f"date_to_days({year, idx + 1, day}) != "
                               f"reconstructed_date_code({idx + 1, day, year})")
                    raise AssertionError(err_msg)


def date_accuracy(low: int, high: int) -> None:
    """Testing function for `date_to_days`"""
    for offset in range(low, high + 1):
        y1, m1, d1 = days_to_date(offset)
        y2, m2, d2 = reconstructed_decode_date(offset)
        try:
            assert y1[0] == y2
            assert m1[0] == m2
            assert d1[0] == d2
        except AssertionError:
            err_msg = (f"{(y1[0], m1[0], d1[0])} != {(y2, m2, d2)}, "
                       f"offset={offset}")
            raise AssertionError(err_msg)


def ns_since_epoch(dt: pd.Timestamp | datetime.datetime | np.datetime64) -> int:
    """Returns the UTC timestamp of a datetime object as an integer number of
    nanoseconds.

    datetime64 units 'Y' and 'M' are supported via the `years_to_days` and
    `months_to_days` functions defined above.
    """
    # pd.Timestamp
    if isinstance(dt, pd.Timestamp):
        return dt.asm8.astype(int)

    # datetime.datetime
    if isinstance(dt, datetime.datetime):
        if dt.tzinfo:  # aware
            utc = datetime.timezone.utc
            offset = datetime.datetime.fromtimestamp(0, utc)
        else:
            offset = datetime.datetime.fromtimestamp(0)
        return total_nanoseconds(dt - offset)

    # np.datetime64
    if isinstance(dt, np.datetime64):
        int_repr = int(dt.astype(int))
        unit = time_unit(dt)
        if unit == "M":  # convert months to days, accounting for mixed length
            int_repr = date_to_days(1970, 1 + int_repr, 1)[0]
            unit = "D"
        elif unit == "Y":  # convert years to days, accounting for leap years
            int_repr = date_to_days(1970 + int_repr, 1, 1)[0]
            unit = "D"
        return int_repr * _to_ns[unit]

    # unrecognized datetime type
    err_msg = (f"[{error_trace()}] could not interpret datetime of type "
               f"{type(dt)}")
    raise TypeError(err_msg)


def time_unit(t: str | type | np.datetime64 | np.timedelta64) -> str:
    """Returns the resolution of a datetime64 or timedelta64 object, dtype,
    or array protocol type string.  Returns `None` if argument has no specified
    resolution.
    """
    if isinstance(t, (np.datetime64, np.timedelta64)):
        dtype_str = str(t.dtype)
    else:
        dtype_str = str(np.dtype(t))
    match = _time_unit_regex.match(dtype_str)
    if match:
        return match.group(1)
    return None


def to_utc(
    dt: pd.Timestamp | datetime.datetime
) -> pd.Timestamp | datetime.datetime:
    # pd.Timestamp
    if isinstance(dt, pd.Timestamp):
        if dt.tzinfo is None:
            dt = dt.tz_localize(tzlocal.get_localzone_name())
        return dt.tz_convert("UTC")

    # datetime.datetime
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=tzlocal.get_localzone())
    return dt.astimezone(datetime.timezone.utc)


def localize_mixed_timezone(series: pd.Series,
                            naive_tz: str | None = None) -> pd.Series:
    # TODO: change default to 'local' and have None just strip away the tzinfo
    naive = series.apply(lambda x: x.tzinfo is None)
    if naive_tz is None:
        naive_tz = tzlocal.get_localzone_name()
    series.loc[naive] = pd.to_datetime(series[naive]).dt.tz_localize(naive_tz)
    return pd.to_datetime(series, utc=True)


def total_nanoseconds(
    td: pd.Timedelta | datetime.timedelta | np.timedelta64,
    starting_from: tuple[int, int, int] = (1970, 1, 1)) -> int:
    """Get the total number of nanoseconds stored in a timedelta object as an
    integer.  Essentially the equivalent of timedelta.total_seconds(), except
    it returns an integer number of nanoseconds rather than a float
    representing seconds.

    `np.timedelta64` units 'Y' and 'M' are supported through the `date_to_days`
    function defined above.  Leap years and unequal month lengths are accounted
    for, and their positions may be customized using the `starting_from`
    argument, which describes a date from which to begin counting.  This
    argument is only used for timedeltas that have units in years or months.
    It defaults to the UTC epoch time (1970-01-01 00:00:00 UTC), causing this
    function and `ns_since_epoch` to return identical results for dates and
    their analogous UTC offsets.
    """
    # pd.Timedelta
    if isinstance(td, pd.Timedelta):
        return td.asm8.astype(int)

    # datetime.timedelta
    if isinstance(td, datetime.timedelta):
        # casting to object dtype prevents overflow
        coefficients = np.array([24 * 60 * 60 * int(1e9), int(1e9), int(1e3)],
                                dtype="O")
        components = np.array([td.days, td.seconds, td.microseconds], dtype="O")
        return np.sum(coefficients * components)

    # np.timedelta64
    if isinstance(td, np.timedelta64):
        unit = time_unit(td)
        int_repr = int(td.astype(int))
        if unit == "M":  # convert months to days, accounting for mixed length
            Y, M, D = starting_from
            conv = int_repr + M
            int_repr = (date_to_days(Y, conv, D) - date_to_days(Y, M, D))[0]
            unit = "D"
        elif unit == "Y":  # convert years to days, accounting for leap years
            Y, M, D = starting_from
            conv = int_repr + Y
            int_repr = (date_to_days(conv, M, D) - date_to_days(Y, M, D))[0]
            unit = "D"
        return int_repr * _to_ns[unit]

    # unrecognized timedelta type
    err_msg = (f"[{error_trace()}] could not interpret timedelta of type "
               f"{type(td)}")
    raise TypeError(err_msg)




def total_units(
    td: pd.Timedelta | datetime.timedelta | np.timedelta64,
    unit: str,
    since: str | pd.Timestamp | datetime.datetime | np.datetime64 | None = None
) -> tuple[int, int]:
    """Get the total number of specified units that are contained in the
    given timedelta.

    Args:
        td (pd.Timedelta | datetime.timedelta | np.timedelta64):
            timedelta to be converted.
        unit (str):
            unit with which to interpret result.
        since (str | pd.Timestamp | datetime.datetime | np.datetime64 | None,
               optional):
            begin counting from the chosen date, accounting for unequal month
            lengths and leap years.  Only used for `unit='M'`, `unit='Y'`, or
            numpy timedeltas measured in months or years.  Strings are
            interpreted as ISO 8601 dates.  If None, starts from the beginning
            of a 400-year Gregorian calendar cycle.  Defaults to None.

    Raises:
        TypeError: if `td` is not an instance of `pandas.Timedelta`,
            `datetime.timedelta`, or `numpy.timedelta64`.

    Returns:
        tuple[int, int]: tuple `(result, residual)`, where `result` is measured
            in the specified units and `residual` is the remainder in
            nanoseconds.
    """
    # get start date, convert to days, and store as offset for units "M" and "Y"
    if since is None:  # start at beginning of 400-year Gregorian cycle
        start_year, start_month, start_day = (2001, 1, 1)
    else:
        if isinstance(since, str):  # interpret as ISO 8601 string
            since = np.datetime64(since)
        if isinstance(since, np.datetime64):
            components = datetime64_components(since)
            start_year = int(components["year"])
            start_month = int(components["month"])
            start_day = int(components["day"])
        else:
            start_year = since.year
            start_month = since.month
            start_day = since.day
    day_offset = int(date_to_days(start_year, start_month, start_day))

    # convert timedelta to nanoseconds
    # pd.Timedelta
    if isinstance(td, pd.Timedelta):
        nanoseconds = int(td.asm8.astype(int))

    # datetime.timedelta
    elif isinstance(td, datetime.timedelta):
        coefficients = np.array([24 * 60 * 60 * int(1e9), int(1e9), int(1e3)],
                                dtype="O")
        components = np.array([td.days, td.seconds, td.microseconds], dtype="O")
        nanoseconds = int(np.sum(coefficients * components))

    # np.timedelta64
    elif isinstance(td, np.timedelta64):
        # convert to underlying integer and gather unit info
        int_repr = int(td.astype(int))
        td_unit, _ = np.datetime_data(td)

        # account for leap years/unequal month length, if appropriate
        if td_unit in ("M", "Y"):
            # add timedelta to start date as appropriate unit
            if td_unit == "M":
                conv = (start_year, start_month + int_repr, start_day)
            else:
                conv = (start_year + int_repr, start_month, start_day)
            # convert years/months to days
            int_repr = int(date_to_days(*conv)) - day_offset
            td_unit = "D"

        # multiply by appropriate scale factor to get nanoseconds
        nanoseconds = int_repr * _to_ns[td_unit]

    # unrecognized timedelta type
    else:
        err_msg = (f"[{error_trace()}] could not interpret timedelta of type "
                   f"{type(td)}")
        raise TypeError(err_msg)

    # convert nanoseconds to final result with residual
    if unit in ("M", "Y"):
        # convert nanoseconds to days, then days to date
        ns_per_day = 24 * 60 * 60 * int(1e9)
        date = days_to_date(nanoseconds // ns_per_day + day_offset)
        years = int(date["year"] - start_year)
        months = int(date["month"] - start_month)
        days = int(date["day"] - start_day)

        # parse date as chosen unit
        if unit == "M":
            result = 12 * years + months
            residual = ns_per_day * days + nanoseconds % ns_per_day
        else:
            # get days in last year
            start = (start_year + years, start_month, start_day)
            end = (start_year + years, start_month + months, start_day + days)
            days_in_last_year = int(date_to_days(*start) - date_to_days(*end))

            # convert days in last year to nanosecond residual
            result = years
            residual = ns_per_day * days_in_last_year + nanoseconds % ns_per_day
        return result, residual
    return nanoseconds // _to_ns[unit], nanoseconds % _to_ns[unit]


def units_since_epoch(
    dt: str | pd.Timestamp | datetime.datetime | np.datetime64,
    unit: str
) -> tuple[int, int]:
    """Get the difference between the given datetime and the UTC epoch in the
    specified units.

    Args:
        dt (str | pd.Timestamp | datetime.datetime | np.datetime64):
            datetime to be converted.
        unit (str):
            unit with which to interpret result.

    Returns:
        tuple[int, int]: tuple `(result, residual)`, where `result` is measured
            in the specified units and `residual` is the remainder in
            nanoseconds.
    """
    # dt = pd.Series(dt)
    # if pd.api.types.is_datetime64_ns_dtype(dt)
    # if pd.api.types.infer_dtype(dt) == "datetime64"
    # if pd.api.types.infer_dtype(dt) == "datetime"
    # if pd.api.types.infer_dtype(dt) == "date"

    # interpret string as ISO 8601
    if isinstance(dt, str):
        dt = np.datetime64(dt)

    # pd.Timestamp
    if isinstance(dt, pd.Timestamp):
        if dt.tzinfo is None:  # localize
            dt = dt.tz_localize(tzlocal.get_localzone_name())
        epoch = pd.Timestamp.fromtimestamp(0, "UTC")

    # datetime.datetime
    elif isinstance(dt, datetime.datetime):
        if dt.tzinfo is None:
            dt = dt.replace(tzinfo=tzlocal.get_localzone())
        epoch = datetime.datetime.fromtimestamp(0, datetime.timezone.utc)

    # np.datetime64
    elif isinstance(dt, np.datetime64):
        dt_unit, _ = np.datetime_data(dt)
        epoch = np.datetime64(0, dt_unit)

    # unrecognized datetime
    else:
        err_msg = (f"[{error_trace()}] could not interpret datetime of type "
                   f"{type(dt)}")
        raise TypeError(err_msg)

    return total_units(dt - epoch, unit=unit, since=epoch)









def convert_datetime_type(dt: datetime_like,
                          new_type: datetime_like) -> datetime_like:
    """Convert a `pandas.Timestamp`, `datetime.timedelta`, or `np.timedelta64`
    object into one of the other types.
    """
    def datetime64_to_datetime_datetime(dt: np.datetime64) -> datetime.datetime:
        result = dt.item()
        if isinstance(result, datetime.datetime):
            return result
        err_msg = (f"[{error_trace(stack_index=2)}] can't convert datetime64 "
                   f"to datetime.datetime: {dt}")
        raise ValueError(err_msg)

    datetime_conversions = {
        pd.Timestamp: {
            pd.Timestamp: lambda dt: dt,
            datetime.datetime: lambda dt: dt.to_pydatetime(warn=False),
            np.datetime64: lambda dt: dt.to_datetime64()
        },
        datetime.datetime: {
            pd.Timestamp: lambda dt: pd.Timestamp(dt),
            datetime.datetime: lambda dt: dt,
            np.datetime64: lambda dt: np.datetime64(dt)
        },
        np.datetime64: {
            pd.Timestamp: lambda dt: pd.Timestamp(dt),
            datetime.datetime: datetime64_to_datetime_datetime,
            np.datetime64: lambda dt: dt
        }
    }
    return datetime_conversions[type(dt)][new_type](dt)


def convert_unit(
    val: int,
    before: str,
    after: str,
    starting_from: str | datetime_like = "1970-01-01 00:00:00+0000"
) -> int:
    """Convert an integer number of the given units to another unit."""
    # val = np.array(val)
    _from_ns = {
        "ns": 1,
        "us": int(1e3),
        "ms": int(1e6),
        "s": int(1e9),
        "m": 60 * int(1e9),
        "h": 60 * 60 * int(1e9),
        "D": 24 * 60 * 60 * int(1e9),
        "W": 7 * 24 * 60 * 60 * int(1e9),
    }

    # TODO: this is vectorized, but has wierd overflow behavior

    # check units are valid
    valid_units = list(_from_ns) + ["M", "Y"]
    if not (before in valid_units and after in valid_units):
        bad = before if before not in valid_units else after
        err_msg = (f"[{error_trace()}] unit {repr(bad)} not recognized - "
                   f"must be in {valid_units}")
        raise ValueError(err_msg)

    # trivial cases
    if before == after:
        return val
    if before == "M" and after == "Y":
        return val // 12
    if before == "Y" and after == "M":
        return val * 12

    # get start date and establish year/month/day conversion functions
    if isinstance(starting_from, (str, np.datetime64)):
        components = datetime64_components(np.datetime64(starting_from))
        start_year = int(components["year"])
        start_month = int(components["month"])
        start_day = int(components["day"])
    else:
        start_year = starting_from.year
        start_month = starting_from.month
        start_day = starting_from.day
    y2d = lambda y: date_to_days(start_year + y, start_month, start_day)
    m2d = lambda m: date_to_days(start_year, start_month + m, start_day)
    d2y = lambda d: days_to_date(d)["year"]
    d2m = lambda d: 12 * (cal := days_to_date(d))["year"] + cal["month"]

    # convert to nanoseconds
    if before == "M":
        nanoseconds = int((m2d(val) - m2d(0)) * _from_ns["D"])
    elif before == "Y":
        nanoseconds = int((y2d(val) - y2d(0)) * _from_ns["D"])
    else:
        nanoseconds = val * _from_ns[before]

    # convert nanoseconds to final unit
    if after == "M":
        start = 12 * (start_year) + start_month
        return int(d2m(nanoseconds // _from_ns["D"] + m2d(0)) - start)
    if after == "Y":
        return int(d2y(nanoseconds // _from_ns["D"] + y2d(0)) - start_year)
    return nanoseconds // _from_ns[after]


def to_unit(
    arg: tuple[int, str] | str | datetime_like | timedelta_like,
    unit: str,
    since: str | datetime_like = "1970-01-01 00:00:00+0000"
) -> int:
    """Convert a timedelta or an integer and associated unit into an integer
    number of the specified unit.

    Args:
        arg (tuple[int, str] | pd.Timedelta | datetime.timedelta | np.timedelta64): _description_
        unit (str): _description_

    Raises:
        ValueError: _description_

    Returns:
        int: _description_
    """
    # TODO: vectorize these
    if isinstance(since, str):
        since = np.datetime64(since)
    if isinstance(arg, str):
        arg = np.datetime64(arg)

    # convert datetimes to timedeltas
    # TODO: test each of these
    if isinstance(arg, pd.Timestamp):
        # TODO: handle timezone?
        try:
            offset = convert_datetime_type(since, pd.Timestamp)
            arg -= offset
        except pd._libs.tslibs.np_datetime.OutOfBoundsDatetime:
            arg = convert_datetime_type(arg, datetime.datetime)
    if isinstance(arg, datetime.datetime):
        # TODO: handle timezone?
        try:
            offset = convert_datetime_type(since, datetime.datetime)
            arg -= offset
        except Exception as err:
            print(type(err))
            raise err
    if isinstance(arg, np.datetime64):
        # TODO: strip timezone?
        arg_unit, _ = np.datetime_data(arg)
        offset = convert_datetime_type(since, np.datetime64)
        arg -= np.datetime64(offset, arg_unit)

    # convert timedeltas to final units
    if isinstance(arg, tuple):
        # arg = np.timedelta64(arg[0], arg[1])
        return convert_unit(arg[0], arg[1], unit, since)
    if isinstance(arg, pd.Timedelta):
        nanoseconds = int(arg.asm8.astype(np.int64))
        return convert_unit(nanoseconds, "ns", unit, since)
    if isinstance(arg, datetime.timedelta):
        coefs = np.array([24 * 60 * 60 * int(1e6), int(1e6), 1], dtype="O")
        comps = np.array([arg.days, arg.seconds, arg.microseconds], dtype="O")
        microseconds = int(np.sum(coefs * comps))
        return convert_unit(microseconds, "us", unit, since)
    if isinstance(arg, np.timedelta64):
        arg_unit, _ = np.datetime_data(arg)
        int_repr = int(arg.astype(np.int64))
        return convert_unit(int_repr, arg_unit, unit, since)

    raise RuntimeError()
