from __future__ import annotations
import datetime
import re
from typing import Union
import warnings

import dateutil
import numpy as np
import pandas as pd
import pytz
from sympy import comp
import tzlocal
import zoneinfo

from pdtypes.error import error_trace


"""
TODO: https://i.stack.imgur.com/uiXQd.png
TODO: use numpy arrays (rather than pd.Series) for all functions in this module
"""


datetime_like = Union[pd.Timestamp, datetime.datetime, np.datetime64]
timedelta_like = Union[pd.Timedelta, datetime.timedelta, np.timedelta64]


_time_unit_regex = re.compile(r'^[^\[]+\[([^\]]+)\]$')
_to_ns = {  # TODO: add a bunch of synonyms for maximum flexibility
    # "as": 1e-9,
    # "fs": 1e-6,
    # "ps": 1e-3,
    "ns": 1,
    "us": int(1e3),
    "ms": int(1e6),
    "s": int(1e9),
    "m": 60 * int(1e9),
    "h": 60 * 60 * int(1e9),
    "D": 24 * 60 * 60 * int(1e9),
    "W": 7 * 24 * 60 * 60 * int(1e9),
}


def replace_with_dict(array: np.ndarray | pd.Series,
                      dictionary: dict) -> np.ndarray:
    """Test using dictionaries to replace values in a vectorized fashion."""
    # TODO: this should go in a separate module pdtypes.util.array
    keys = np.array(list(dictionary))
    vals = np.array(list(dictionary.values()))

    sorted_indices = keys.argsort()

    keys_sorted = keys[sorted_indices]
    vals_sorted = vals[sorted_indices]
    return vals_sorted[np.searchsorted(keys_sorted, array)]








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
    result = (day - 1) + (day_offset[month]) + (365 * year + leaps) + 60
    result -= 719528  # utc epoch
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
    # TODO: use np.datetime_data instead
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
            pd.Timestamp: pd.Timestamp,
            datetime.datetime: lambda dt: dt,
            np.datetime64: np.datetime64
        },
        np.datetime64: {
            pd.Timestamp: pd.Timestamp,
            datetime.datetime: datetime64_to_datetime_datetime,
            np.datetime64: lambda dt: dt
        }
    }
    # TODO: have to vectorize this
    return datetime_conversions[type(dt)][new_type](dt)












def _uniform_dt64_array_to_date(
    dt64s: np.ndarray
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    # assumes no missing values

    # get unit data, step size from array dtype
    dt64_unit, step_size = np.datetime_data(dt64s.dtype)

    # trivial units
    if dt64_unit in ("M", "Y"):
        dt64s = dt64s.view("i8").astype("O") * step_size
        if dt64_unit == "M":
            years = 1970 + (dt64s // 12)
            months = (1 + dt64s) % 12
            days = 1
        else:
            years = 1970 + dt64s
            months = 1
            days = 1
        return years, months, days

    # check for overflow due to nonstandard step size
    if len(dt64s) > 0:  # max()/min() fail on len 0 arrays
        max_ns = int(dt64s.max().view("i8")) * step_size * _to_ns[dt64_unit]
        min_ns = int(dt64s.min().view("i8")) * step_size * _to_ns[dt64_unit]
        if (min_ns < (-2**63 + 1) * _to_ns["D"] or
            max_ns > (2**63 - 1) * _to_ns["D"]):
            # astype("M8[D]") would exceed 64-bit range -> use days_to_date
            # this is ~5 times slower than fastpath below
            dt64s = dt64s.view("i8").astype("O") * step_size
            dt64s = days_to_date((dt64s * _to_ns[dt64_unit]) // _to_ns["D"])
            return dt64s["year"], dt64s["month"], dt64s["day"]

    # fastpath: optimize for no overflow
    Y, M, D = [dt64s.astype(f"M8[{x}]") for x in "YMD"]
    years = 1970 + Y.view("i8").astype("O")
    months = 1 + (M - Y).view("i8").astype("O")
    days = 1 + (D - M).view("i8").astype("O")
    return years, months, days


def _mixed_dt64_array_to_date(
    dt64s: np.ndarray
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    # assumes no missing values

    # initialize results
    years = np.full(dt64s.shape, pd.NA, dtype="O")
    months = years.copy()
    days = years.copy()

    # get integer value (adjusted for step size), unit data from array elements
    def val_unit(element: np.datetime64) -> tuple[int, str]:
        dt64_unit, step_size = np.datetime_data(element)
        return int(element.view("i8")) * step_size, dt64_unit

    vals, dt64_unit = np.frompyfunc(val_unit, 1, 2)(dt64s)

    # handle months separately
    dt64_months = (dt64_unit == "M")
    years[dt64_months] = 1970 + vals[dt64_months] // 12
    months[dt64_months] = (1 + vals[dt64_months]) % 12
    days[dt64_months] = 1

    # handle years separately
    dt64_years = (dt64_unit == "Y")
    years[dt64_years] = 1970 + vals[dt64_years]
    months[dt64_years] = 1
    days[dt64_years] = 1

    # handle all other values using days_to_date
    other = ~(dt64_months | dt64_years)
    vals = vals[other] * replace_with_dict(dt64_unit[other], _to_ns)
    result = days_to_date(vals // _to_ns["D"])
    years[other] = result["year"]
    months[other] = result["month"]
    days[other] = result["day"]
    return years, months, days


def _mixed_datelike_array_to_date(
    datelikes: np.ndarray
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    # assumes no missing values

    # initialize results
    years = np.full(datelikes.shape, pd.NA, dtype="O")
    months = years.copy()
    days = years.copy()

     # handle np.datetime64 elements separately
    type_ufunc = np.frompyfunc(type, 1, 1)
    dt64_indices = np.array(pd.Series(type_ufunc(datelikes)) == np.datetime64)

    # dispatch np.datetime64 elements to _mixed_dt64_array_to_date
    dt64_y, dt64_m, dt64_d = _mixed_dt64_array_to_date(datelikes[dt64_indices])

    # assign np.datetime64 elements to results
    years[dt64_indices] = dt64_y
    months[dt64_indices] = dt64_m
    days[dt64_indices] = dt64_d

    # continue converting non-np.datetime64 elements
    def datelike_to_date(element: datetime_like) -> tuple[int, int, int]:
        return element.year, element.month, element.day

    datelike_to_date = np.frompyfunc(datelike_to_date, 1, 3)
    dt_y, dt_m, dt_d = datelike_to_date(datelikes[~dt64_indices])

    # assign to results and return
    years[~dt64_indices] = dt_y
    months[~dt64_indices] = dt_m
    days[~dt64_indices] = dt_d
    return years, months, days


def date_components(
    date: datetime.date | datetime_like | list | np.ndarray | pd.Series
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Split a date-like object or series of objects into its constituent parts
    (year, month, day).
    """
    # TODO: does this function need to tolerate missing values?

    # np.datetime64 array with dtype == M8[(step_size)(dt64_unit)]
    if isinstance(date, np.ndarray) and np.issubdtype(date.dtype, "M8"):
        date = np.atleast_1d(date)  # ensure date can be indexed

        # initialize results
        years = np.full(date.shape, pd.NA, dtype="O")
        months = years.copy()
        days = years.copy()

        # detect missing values
        non_na = pd.notna(date)

        # dispatch to _uniform_dt64_array_to_date
        Y, M, D = _uniform_dt64_array_to_date(date[non_na])
        years[non_na], months[non_na], days[non_na] = Y, M, D
        return years, months, days

    # pd.Timestamp series with dtype == datetime64[ns]
    if (isinstance(date, pd.Series) and
        pd.api.types.is_datetime64_ns_dtype(date)):
        # initialize results
        years = np.full(date.shape, pd.NA, dtype="O")
        months = years.copy()
        days = years.copy()

        # detect missing values and subset
        non_na = pd.notna(date)
        subset = date[non_na]

        # extract using .dt accessor
        years[non_na] = subset.dt.year
        months[non_na] = subset.dt.month
        days[non_na] = subset.dt.day
        return (years, months, days)

    # scalar, list, or np.ndarray/pd.Series with dtype="O"
    date = np.atleast_1d(np.array(date, dtype="O"))  # date can have mixed units

    # initialize results
    years = np.full(date.shape, pd.NA, dtype="O")
    months = years.copy()
    days = years.copy()

    # detect missing values
    non_na = pd.notna(date)

    Y, M, D = _mixed_datelike_array_to_date(date[non_na])
    years[non_na], months[non_na], days[non_na] = Y, M, D
    return years, months, days


def months_to_ns(
    val: np.ndarray,
    since: datetime.date | datetime_like | list | np.ndarray | pd.Series
) -> np.ndarray:
    """Convert a number of months to a day offset from a given date."""
    start_year, start_month, start_day = date_components(since)
    offset = date_to_days(start_year, start_month, start_day)
    result = date_to_days(start_year, start_month + val, start_day)
    return (result - offset) * _to_ns["D"]


def years_to_ns(
    val: np.ndarray,
    since: datetime.date | datetime_like | list | np.ndarray | pd.Series
) -> np.ndarray:
    """Convert a number of years to a day offset from a given date."""
    start_year, start_month, start_day = date_components(since)
    offset = date_to_days(start_year, start_month, start_day)
    result = date_to_days(start_year + val, start_month, start_day)
    return (result - offset) * _to_ns["D"]


def ns_to_months(
    val: np.ndarray,
    since: datetime.date | datetime_like | list | np.ndarray | pd.Series,
    rounding: str = "truncate"
) -> np.ndarray:
    """Convert a number of days to a month offset from a given date."""
    # get (Y, M, D) components of `since` and establish UTC offset in days
    start_year, start_month, start_day = date_components(since)
    offset = date_to_days(start_year, start_month, start_day)

    # convert val to days and add offset to compute final calendar date
    val = round_div(val, _to_ns["D"], rounding=rounding) + offset
    end_date = days_to_date(val)

    # result is the difference between end month and start month, plus years
    result = (12 * (end_date["year"] - start_year) +
              end_date["month"] - start_month)

    # correct for premature rollover and apply floor/ceiling rules
    right = (end_date["day"] < start_day)
    if rounding == "floor":  # emulate floor rounding to -infinity
        return result - right
    left = (end_date["day"] > start_day)
    if rounding == "ceiling":  # emulate ceiling rounding to +infinity
        return result + left

    # truncate, using floor for positive values and ceiling for negative
    positive = (result > 0)
    negative = (result < 0)
    result[positive] -= right[positive]
    result[negative] += left[negative]
    if rounding == "truncate":  # skip processing residuals
        return result

    # compute residuals and round
    result_days = months_to_ns(result, since=since)
    residuals = val - result_days  # residual in nanoseconds
    days_in_last_month = months_to_ns(result + 1, since=since) // _to_ns["D"]
    days_in_last_month -= result_days
    return result + round_div(residuals, days_in_last_month, rounding="round")


def ns_to_years(
    val: np.ndarray,
    since: datetime.date | datetime_like | list | np.ndarray | pd.Series,
    rounding: str = "truncate"
) -> np.ndarray:
    """Convert a number of days to a month offset from a given date."""
    # get (Y, M, D) components of `since` and establish UTC offset in days
    start_year, start_month, start_day = date_components(since)
    offset = date_to_days(start_year, start_month, start_day)

    # convert val to days and add offset to compute final calendar date
    val = round_div(val, _to_ns["D"], rounding=rounding) + offset
    end_date = days_to_date(val)

    # result is the difference between end year and start year
    result = end_date["year"] - start_year

    # correct for premature rollover and apply floor/ceiling rules
    right = (end_date["month"] < start_month) | (end_date["day"] < start_day)
    if rounding == "floor":  # emulate floor rounding to -infinity
        return result - right
    left = (end_date["month"] > start_month) | (end_date["day"] > start_day)
    if rounding == "ceiling":  # emulate ceiling rounding to +infinity
        return result + left

    # truncate, using floor for positive values and ceiling for negative
    positive = (result > 0)
    negative = (result < 0)
    result[positive] -= right[positive]
    result[negative] += left[negative]
    if rounding == "truncate":  # skip processing residuals
        return result

    # compute residuals and round
    residuals = val - years_to_ns(result, since=since) // _to_ns["D"]
    days_in_last_year = 365 + is_leap(start_year + result)
    return result + round_div(residuals, days_in_last_year, rounding="round")


def round_div(
    val: int | list | np.ndarray | pd.Series,
    divisor: int | np.ndarray | pd.Series,
    rounding: str = "floor"
) -> np.ndarray:
    """Divide integers and integer arrays with specified rounding rule."""
    # TODO: this should go in a separate module pdtypes.util.array
    # vectorize input
    val = np.atleast_1d(np.array(val))
    divisor = np.atleast_1d(np.array(divisor))

    # broadcast to same size
    val, divisor = np.broadcast_arrays(val, divisor)

    # round towards -infinity
    if rounding == "floor":
        return val // divisor

    # round towards zero
    if rounding == "truncate":
        neg = (val < 0) ^ (divisor < 0)
        result = val.copy()
        result[neg] = (val[neg] + divisor[neg] - 1) // divisor[neg]  # ceiling
        result[~neg] //= divisor[~neg]  # floor
        return result

    # round towards closest integer
    if rounding == "round":
        return (val + divisor // 2) // divisor

    # round towards +infinity
    if rounding == "ceiling":
        return (val + divisor - 1) // divisor

    # error - rounding not recognized
    err_msg = (f"[{error_trace()}] `rounding` must be one of ['floor', "
               f"'truncate', 'round', 'ceiling'], not {repr(rounding)}")
    raise ValueError(err_msg)


def convert_unit(
    val: int | list | np.ndarray | pd.Series,
    before: str | list | np.ndarray | pd.Series,
    after: str | list | np.ndarray | pd.Series,
    since: datetime.date | datetime_like | list | np.ndarray | pd.Series,
    rounding: str = "truncate"
) -> np.ndarray:
    """Convert an integer number of the given units to another unit."""
    # TODO: for float input, round to nearest nanosecond and change `before`
    # to match prior to inputting to this function
    # vectorize input
    val = np.atleast_1d(np.array(val, dtype="O"))  # dtype="O" prevents overflow
    before = np.atleast_1d(np.array(before))
    after = np.atleast_1d(np.array(after))
    since = np.atleast_1d(np.array(since))

    # broadcast inputs and initialize result
    val, before, after, since = np.broadcast_arrays(val, before, after, since)
    val = val.copy()  # converts memory view into assignable result

    # check units are valid
    valid_units = list(_to_ns) + ["M", "Y"]
    if not np.isin(before, valid_units).all():
        bad = list(np.unique(before[~np.isin(before, valid_units)]))
        err_msg = (f"[{error_trace()}] `before` unit {bad} not recognized: "
                   f"must be in {valid_units}")
        raise ValueError(err_msg)
    if not np.isin(after, valid_units).all():
        bad = list(np.unique(after[~np.isin(after, valid_units)]))
        err_msg = (f"[{error_trace()}] `after` unit {bad} not recognized: "
                   f"must be in {valid_units}")
        raise ValueError(err_msg)

    # trivial case (no conversion)
    if np.array_equal(before, after):
        return val

    # get indices where conversion is necessary
    to_convert = (before != after)

    # trivial year/month conversions
    trivial_years = (before == "Y") & (after == "M")
    val[trivial_years] *= 12  # multiply years by 12 to get months
    to_convert ^= trivial_years  # ignore trivial indices

    # trivial month/year conversions
    trivial_months = (before == "M") & (after == "Y")
    val[trivial_months] = round_div(val[trivial_months], 12, rounding=rounding)
    to_convert ^= trivial_months  # ignore trivial indices

    # check for completeness
    if not to_convert.any():
        return val

    # continue converting non-trivial indices
    subset = val[to_convert]
    before = before[to_convert]
    after = after[to_convert]
    since = since[to_convert]

    # convert subset to nanoseconds
    months = (before == "M")
    years = (before == "Y")
    other = ~(months | years)
    subset[months] = months_to_ns(subset[months], since[months])
    subset[years] = years_to_ns(subset[years], since[years])
    subset[other] *= replace_with_dict(before[other], _to_ns)

    # convert subset nanoseconds to final unit
    months = (after == "M")
    years = (after == "Y")
    other = ~(months | years)
    subset[months] = ns_to_months(subset[months], since[months],
                                  rounding=rounding)
    subset[years] = ns_to_years(subset[years], since[years], rounding=rounding)
    coefficients = replace_with_dict(after[other], _to_ns)
    subset[other] = round_div(subset[other], coefficients, rounding=rounding)

    # reassign subset to val and return
    val[to_convert] = subset
    return val













# def convert_unit(
#     val: int | np.ndarray | pd.Series,
#     before: str,
#     after: str,
#     since: str | datetime_like = "1970-01-01 00:00:00+0000",
#     rounding: str = "floor"
# ) -> pd.Series | tuple[pd.Series, pd.Series]:
#     """Convert an integer number of the given units to another unit."""
#     # vectorize input
#     val = pd.Series(val, dtype="O")  # object dtype prevents overflow

#     # check units are valid
#     valid_units = list(_to_ns) + ["M", "Y"]
#     if not (before in valid_units and after in valid_units):
#         bad = before if before not in valid_units else after
#         err_msg = (f"[{error_trace()}] unit {repr(bad)} not recognized - "
#                    f"must be in {valid_units}")
#         raise ValueError(err_msg)

#     # trivial cases
#     if before == after:
#         return val
#     if before == "Y" and after == "M":
#         return val * 12
#     if before == "M" and after == "Y":
#         if rounding == "floor":
#             return val // 12
#         result = val // 12
#         residuals = ((val % 12) / 12).astype(float)
#         if rounding == "round":
#             result[residuals >= 0.5] += 1
#         else:  # rounding == "ceiling"
#             result[residuals > 0] += 1
#         return result

#     # get start date and establish year/month/day conversion functions
#     if isinstance(since, (str, np.datetime64)):
#         components = datetime64_components(np.datetime64(since))
#         start_year = int(components["year"])
#         start_month = int(components["month"])
#         start_day = int(components["day"])
#     else:
#         start_year = since.year
#         start_month = since.month
#         start_day = since.day
#     y2d = lambda y: date_to_days(start_year + y, start_month, start_day)
#     m2d = lambda m: date_to_days(start_year, start_month + m, start_day)
#     d2y = lambda d: days_to_date(d)["year"]
#     d2m = lambda d: 12 * (cal := days_to_date(d))["year"] + cal["month"]

#     # convert to nanoseconds
#     if before == "M":
#         # TODO: if vectorizing units, just convert both entries to the
#         # appropriate values
#         nanoseconds = pd.Series(m2d(val) - m2d(0)) * _to_ns["D"]
#     elif before == "Y":
#         # TODO: if vectorizing units, just convert both entries to the
#         # appropriate values
#         nanoseconds = pd.Series(y2d(val) - y2d(0)) * _to_ns["D"]
#     else:
#         # TODO: use replace_with_dict to get a vector of ns coefficients from
#         # _to_ns.  Multiply these to get nanoseconds
#         nanoseconds = val * _to_ns[before]

#     # convert nanoseconds to final unit
#     if after == "M":  # convert nanoseconds to days, then days to months
#         # TODO: apply this in a vectorized fashion wherever after == "M"
#         # modify both entries, as with nanoseconds above
#         days = nanoseconds // _to_ns["D"]
#         day_offset = m2d(0)

#         # get integer result
#         start_offset = 12 * (start_year) + start_month
#         result = pd.Series(d2m(days + day_offset) - start_offset)
#         if rounding == "floor":  # fastpath: don't bother calculating residuals
#             return result

#         # compute residuals
#         result_days = m2d(result)
#         residual_days = days - result_days + day_offset
#         days_in_last_month = m2d(1 + result) - result_days
#         residuals = (residual_days / days_in_last_month).astype(float)
#     elif after == "Y":  # convert nanoseconds to days, then days to years
#         # TODO: apply this in a vectorized fashion wherever after == "Y"
#         # modify both entries, as with nanoseconds above
#         days = nanoseconds // _to_ns["D"]
#         day_offset = y2d(0)

#         # get integer result
#         result = pd.Series(d2y(days + day_offset) - start_year)
#         if rounding == "floor":  # fastpath: don't bother calculating residuals
#             return result

#         # compute residuals
#         result_days = y2d(result)
#         residual_days = days - result_days + day_offset
#         days_in_last_year = 365 + is_leap(start_year + result)
#         residuals = (residual_days / days_in_last_year).astype(float)
#     else:  # use regular scale factor
#         # TODO: vectorized access via array indexing -> stack unit coefficients
#         # from lowest to highest and refer to them by index.
#         # TODO: could also just manually compare
#         result = nanoseconds // _to_ns[after]
#         if rounding == "floor":  # fastpath: don't bother calculating residuals
#             return result

#         # compute residuals
#         scale_factor = _to_ns[after]  # TODO: vectorized access via labeled array
#         residuals = ((nanoseconds % scale_factor) / scale_factor).astype(float)

#     # handle rounding if not floor
#     if rounding == "round":
#         result[residuals >= 0.5] += 1
#     else:  # rounding == "ceiling"
#         result[residuals > 0] += 1
#     return result


def to_unit(
    arg: tuple[int, str] | str | datetime_like | timedelta_like,
    unit: str,
    since: str | datetime_like = "1970-01-01 00:00:00+0000",
    tz: str | datetime.tzinfo = "UTC",
    format: str | None = None,
    day_first: bool = False,
    year_first: bool = False,
    fuzzy: bool = False,
    errors: str = "warn"
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
    # # vectorize inputs -> retain original arg if necessary
    # if isinstance(arg, tuple):
    #     series = pd.Series(arg[0], dtype="O")
    # else:
    #     series = pd.Series(arg, dtype="O")

    # # convert IANA timezone key to pytz.timezone
    # if isinstance(tz, str):
    #     tz = pytz.timezone(tz)

    # # convert ISO 8601 strings to datetimes
    # if pd.api.types.infer_dtype(series) == "string":
    #     series = string_to_datetime(series, tz=tz, format=format,
    #                                 day_first=day_first, year_first=year_first,
    #                                 fuzzy=fuzzy, errors=errors)
    # if isinstance(since, str):
    #     since = string_to_datetime(since, tz=tz, errors=errors)[0]

    # # convert datetimes to timedeltas
    # if pd.api.types.is_datetime64_any_dtype(series.infer_objects()):
    #     # series contains pd.Timestamp objects
    #     def localize_timestamp(timestamp: pd.Timestamp) -> pd.Timestamp:
    #         if timestamp.tzinfo is None:  # assume utc
    #             timestamp = timestamp.tz_localize(datetime.timezone.utc)
    #         return timestamp.tz_convert(tz)

    #     localize_timestamp = np.frompyfunc(localize_timestamp, 1, 1)
    #     try:
    #         offset = convert_datetime_type(since, pd.Timestamp)
    #         series = localize_timestamp(series) - offset
    #     except pd._libs.tslibs.np_datetime.OutOfBoundsDatetime:
    #         series = convert_datetime_type(series, datetime.datetime)
    # if pd.api.types.infer_dtype(series) == "datetime":
    #     # series contains datetime.datetime objects
    #     def localize_datetime(dt: datetime.datetime) -> datetime.datetime:
    #         if dt.tzinfo is None:
    #             dt = dt.replace(tzinfo=datetime.timezone.utc)
    #         return dt.astimezone(tz)

    #     localize_datetime = np.frompyfunc(localize_datetime, 1, 1)
    #     try:
    #         offset = convert_datetime_type(since, pd.Timestamp)
    #         series = localize_datetime(series) - offset
    #     except OverflowError:
    #         series = convert_datetime_type(series, np.datetime64)
    # if pd.api.types.infer_dtype(series) == "datetime64":
    #     # series contains np.datetime64 objects
    #     pass


    # return series

    # # if pd.api.types.infer_dtype(arg) == "datetime"


    # # return arg



    original_arg = arg  # TODO: add an explicit type check at the top
    # arg = np.array(arg, dtype="O")  # object dtype prevents overflow
    # TODO: allow both aware and naive args
    # TODO: explicitly vectorize - use pd.api.types.infer_dtype for isinstance

    if isinstance(arg, str):
        arg = convert_iso_string(arg, tz)
    if isinstance(since, str):
        since = convert_iso_string(since, tz)
    if isinstance(tz, str):
        tz = pytz.timezone(tz)

    # convert datetimes to timedeltas
    # TODO: test each of these
    if isinstance(arg, pd.Timestamp):
        if not arg.tzinfo:
            arg = arg.tz_localize(tz)
        else:
            arg = arg.tz_convert(tz)
        try:
            arg -= convert_datetime_type(since, pd.Timestamp)
        except pd._libs.tslibs.np_datetime.OutOfBoundsDatetime:
            arg = convert_datetime_type(arg, datetime.datetime)
    if isinstance(arg, datetime.datetime):
        if not arg.tzinfo:
            arg = arg.replace(tzinfo=tz)
        elif tz:
            arg = arg.astimezone(tz)
        else:
            arg = arg.astimezone(datetime.timezone.utc).replace(tzinfo=None)
        try:
            arg -= convert_datetime_type(since, datetime.datetime)
        except OverflowError:
            arg = convert_datetime_type(arg, np.datetime64)
    if isinstance(arg, np.datetime64):
        # TODO: strip timezone?
        arg_unit, _ = np.datetime_data(arg)
        offset = convert_datetime_type(since, np.datetime64)
        arg -= np.datetime64(offset, arg_unit)

    # convert timedeltas to final units
    if isinstance(arg, tuple):
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

    # TODO: fill out error message
    err_msg = (f"[{error_trace()}] could not convert value to unit "
               f"{repr(unit)}: {repr(original_arg)}")
    raise RuntimeError(err_msg)


def string_to_datetime(
    series: str | list | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = "UTC",
    format: str | None = None,
    day_first: bool = False,
    year_first: bool = False,
    fuzzy: bool = False,
    errors: str = "warn"
) -> pd.Series:
    """Convert an ISO 8601 string into a datetime object.  Properly accounts
    for timezone offsets in ISO format, and localizes to the timezone given by
    `tz`.  Naive ISO strings are interpreted as UTC.
    """
    # TODO: replicate behavior of pandas errors arg {'ignore', 'raise', 'coerce'}

    # vectorize input and check dtype
    series = pd.Series(series)
    if pd.api.types.infer_dtype(series) != "string":
        err_msg = (f"[{error_trace()}] `series` must contain string data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)

    # convert string timezone keys to tzinfo objects and check type
    if isinstance(tz, str):
        # tz = zoneinfo.ZoneInfo(tz)
        tz = pytz.timezone(tz)
    if not isinstance(tz, pytz.BaseTzInfo):
        err_msg = (f"[{error_trace()}] `tz` must be a IANA timezone "
                   f"string or a `pytz.timezone` object, not {type(tz)}")
        raise TypeError(err_msg)

    # check format is a string or None
    if format:
        if day_first or year_first:
            err_msg = (f"[{error_trace()}] `day_first` and `year_first` only "
                       f"apply when no format is specified")
            raise RuntimeError(err_msg)
        if not isinstance(format, str):
            err_msg = (f"[{error_trace()}] if given, `format` must be a "
                       f"datetime format string, not {type(format)}")
            raise TypeError(err_msg)

    # check errors is valid
    if errors not in ['raise', 'warn', 'ignore']:
        err_msg = (f"[{error_trace(stack_index=2)}] `errors` must one of "
                   f"{['raise', 'warn', 'ignore']}, not {repr(errors)}")
        raise ValueError(err_msg)

    # try pd.Timestamp -> use pd.to_datetime directly
    try:
        if format:  # use specified format
            result = pd.to_datetime(series, utc=True, format=format,
                                    exact=not fuzzy)
        else:  # infer format
            result = pd.to_datetime(series, utc=True, dayfirst=day_first,
                                    yearfirst=year_first,
                                    infer_datetime_format=True)
        # make room for timezone -> reject timestamps within 12 hours of min/max
        min_val = result.min()
        max_val = result.max()
        min_poss = pd.Timestamp.min.tz_localize("UTC") + pd.Timedelta(hours=12)
        max_poss = pd.Timestamp.max.tz_localize("UTC") - pd.Timedelta(hours=12)
        if min_val >= min_poss and max_val <= max_poss:
            return result.dt.tz_convert(tz)
    except (OverflowError, pd._libs.tslibs.np_datetime.OutOfBoundsDatetime,
            dateutil.parser.ParserError):
        pass

    # try datetime.datetime -> use an elementwise conversion ufunc + dateutil
    if format:
        def convert_to_datetime(datetime_string: str) -> datetime.datetime:
            try:
                result = datetime.datetime.strptime(datetime_string.strip(),
                                                    format)
            except ValueError as err:
                err_msg = (f"[{error_trace(stack_index=4)}] unable to "
                           f"interpret {repr(datetime_string)} according to "
                           f"format {repr(format)}")
                raise ValueError(err_msg) from err
            if not result.tzinfo:
                result = result.replace(tzinfo=datetime.timezone.utc)
            return result.astimezone(tz)
    else:
        def convert_to_datetime(datetime_string: str) -> datetime.datetime:
            result = dateutil.parser.parse(datetime_string, dayfirst=day_first,
                                           yearfirst=year_first, fuzzy=fuzzy)
            if not result.tzinfo:
                result = result.replace(tzinfo=datetime.timezone.utc)
            return result.astimezone(tz)
    convert_to_datetime = np.frompyfunc(convert_to_datetime, 1, 1)
    try:
        return pd.Series(convert_to_datetime(np.array(series)), dtype="O")
    except (OverflowError, dateutil.parser.ParserError):
        pass

    # try np.datetime64 -> requires ISO format and does not carry tzinfo
    if format:
        # TODO: this branch is never accessed.  Providing a format string to
        # pd.to_datetime with data outside year [0000-9999] throws ValueError
        # that cannot be easily distinguished from a simple failure to parse.
        err_msg = (f"[{error_trace()}] `numpy.datetime64` objects do not "
                   f"support arbitrary string parsing.  The provided string "
                   f"must be ISO 8601-compliant, with `format=None`.")
        raise NotImplementedError(err_msg)
    if tz and tz != pytz.timezone("UTC"):
        warn_msg = ("`numpy.datetime64` objects do not carry timezone "
                    "information")
        if errors == "raise":
            raise RuntimeError(f"[{error_trace()}] {warn_msg}")
        if errors == "warn":
            warnings.warn(f"{warn_msg} - returned time is UTC", RuntimeWarning)
    try:
        return pd.Series(list(series.array.astype("M8")), dtype="O")
    except ValueError as err:
        err_msg = (f"[{error_trace()}] could not interpret series as datetime")
        raise ValueError(err_msg) from err
