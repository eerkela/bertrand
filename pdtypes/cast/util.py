from __future__ import annotations
from calendar import month
import datetime
import re

import numpy as np
import pandas as pd

from pdtypes.error import error_trace


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


def trunc_div(a: int | pd.Series | np.ndarray, b: int) -> int | pd.Series:
    """Integer division `//` rounded toward 0 rather than +/- infinity."""
    if isinstance(a, (pd.Series, np.ndarray)):  # vectorized
        result = a.copy()
        positives = (a >= 0)
        result[positives] = a[positives] // b  # floor div
        result[~positives] = -(a[~positives] // -b)  # ceiling div
        return result
    # scalar
    if a * b > 0:
        return a // b
    return -(a // -b)


def is_leap_year(year: int | pd.Series | np.ndarray) -> bool:
    """Returns True if given year is a leap year."""
    return (year % 4 == 0) & ((year % 100 != 0) | (year % 400 == 0))


def n_leaps(year: int) -> int:
    """Return the number of leap years between year 0 and the given year."""
    return year // 4 - year // 100 + year // 400


def years_to_days(years: int | pd.Series,
                  starting_year: int = 1970) -> int | pd.Series:
    """Convert an integer number of years to an integer number of days,
    accounting for leap years.
    """
    # replicating the Gregorian calendar
    start_leaps = n_leaps(starting_year - 1)
    end_leaps = n_leaps(starting_year + years - 1)
    leap_years = end_leaps - start_leaps
    return (years - leap_years) * 365 + leap_years * 366


def days_to_years(days: int | pd.Series,
                  starting_year: int = 1970,
                  force: bool = False) -> int:
    """Convert an integer number of days to an integer number of years,
    accounting for leap years.
    """
    start_leaps = n_leaps(starting_year - 1)
    end_leaps = n_leaps(starting_year + trunc_div(days, 365) - 1)
    leap_years = end_leaps - start_leaps
    result = trunc_div(days - leap_years, 365)
    if not force:  # check for information loss
        reverse = years_to_days(result, starting_year=starting_year)
        if not np.array_equal(reverse, days):
            err_msg = (f"[{error_trace()}] could not convert days to years "
                       f"without losing precision: {days}")
            raise ValueError(err_msg)
    return result


def month_mask(month_span: int, starting_month: int = 1) -> list[int]:
    month_span = month_span % 12
    offset = (starting_month - 1) % 12
    wrap = (offset + month_span) // 12 * (offset + month_span) % 12
    a = wrap
    b = offset - wrap
    c = month_span - wrap
    d = 12 - month_span - offset + wrap
    return [1] * a + [0] * b + [1] * c + [0] * d


def days_per_month(year: int, starting_month: int = 1) -> np.ndarray:
    offset = (starting_month - 1) % 12
    normal = np.array([31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31])
    leap = np.array([31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31])
    if isinstance(year, (pd.Series, np.ndarray)):
        result = np.full((len(year), 12), normal)
        if offset >= 2:
            result[is_leap_year(year + 1)] = leap
        else:
            result[is_leap_year(year)] = leap
        return result
    if ((offset >= 2 and is_leap_year(year + 1)) or
        (offset < 2 and is_leap_year(year))):
        return leap
    return normal


def months_to_days(months: int | pd.Series | np.ndarray,
                   starting_month: int = 1,
                   starting_year: int = 1970) -> int | pd.Series | np.ndarray:
    """Convert an integer number of months into an integer number of days,
    accounting for leap years and unequal month lengths.
    """
    # only have to figure out days in last year, else use years_to_days
    years = months // 12
    base_days = years_to_days(years, starting_year=starting_year)

    # vectorized
    if isinstance(months, (pd.Series, np.ndarray)):
        # map month spans to masks, multiply by day count, then sum by row
        mapping = np.array([month_mask(i, starting_month) for i in range(12)])
        masks = mapping[months % 12]  # nx12
        days = days_per_month(years, starting_month)  # nx12
        return base_days + np.sum(np.multiply(masks, days), axis=1)  # nx1

    # scalar
    calendar = days_per_month(starting_year + years, starting_month)
    begin = starting_month - 1
    end = (starting_month - 1 + months) % 12
    return (base_days + np.sum(calendar[begin:end]))


def days_to_months(days: int,
                   starting_month: int = 1,
                   starting_year: int = 1970,
                   force: bool = False) -> int:
    pass
    




def total_nanoseconds(
    td: pd.Timedelta | datetime.timedelta | np.timedelta64,
    starting_year: int = 1970,
    starting_month: int = 1) -> int:
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
        if unit == "M":
            int_repr = months_to_days(int_repr, starting_year=starting_year,
                                      starting_month=starting_month)
            unit = "D"
        elif unit == "Y":  # convert years to days, accounting for leap years
            int_repr = years_to_days(int_repr, starting_year=starting_year)
            unit = "D"
        return int_repr * _to_ns[unit]

    # unrecognized
    err_msg = (f"[{error_trace()}] could not interpret timedelta of type "
               f"{type(td)}")
    raise TypeError(err_msg)


def ns_since_epoch(dt: pd.Timestamp | datetime.datetime | np.datetime64) -> int:
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
        if unit == "M":  # convert months to days, accounting for unequal length
            int_repr = months_to_days(int_repr, starting_year=1970,
                                      starting_month=1)
            unit = "D"
        elif unit == "Y":  # convert years to days, accounting for leap years
            int_repr = years_to_days(int_repr, starting_year=1970)
            unit = "D"
        return int_repr * _to_ns[unit]

    # unrecognized
    err_msg = (f"[{error_trace()}] could not interpret datetime of type "
               f"{type(dt)}")
    raise TypeError(err_msg)


def time_unit(t: str | type | np.datetime64 | np.timedelta64) -> str:
    if isinstance(t, (np.datetime64, np.timedelta64)):
        dtype_str = str(t.dtype)
    else:
        dtype_str = str(np.dtype(t))
    match = _time_unit_regex.match(dtype_str)
    if match:
        return match.group(1)
    return None
