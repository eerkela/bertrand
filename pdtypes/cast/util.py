from __future__ import annotations
import datetime
from functools import wraps
import re

import numpy as np
import pandas as pd

from pdtypes.error import error_trace


"""
TODO: have month/day conversions handle missing values
"""


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
    """Integer division `//` operator rounded toward 0 rather than -infinity."""
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


def is_leap_year(
    year: int | pd.Series | np.ndarray
) -> bool | pd.Series | np.ndarray:
    """Returns True if given year is a leap year."""
    return (year % 4 == 0) & ((year % 100 != 0) | (year % 400 == 0))


def leaps_between(
    begin: int | pd.Series | np.ndarray,
    end: int | pd.Series | np.ndarray
) -> int | pd.Series | np.ndarray:
    """Return the number of leap years between the years `begin` and `end`.

    Counts from the beginning of each year.  This means that
    `leaps_between(x, x + 1)` will return 1 if `x` was a leap year.
    """
    count = lambda x: x // 4 - x // 100 + x // 400
    return count(end - 1) - count(begin - 1)  # Exclusive


def month_mask(month_span: int, starting_month: int = 1) -> list[int]:
    """Convert an integer number of months into a 1x12 mask containing ones
    indicating which months have been selected, starting from `starting_month`.

    If `starting_month != 1` and `month_span` would carry over into a new year,
    the mask values are wrapped to the beginning of the array, indicating
    months in the following year.
    """
    month_span = month_span % 12
    offset = (starting_month - 1) % 12
    wrap = (offset + month_span) // 12 * (offset + month_span) % 12
    a = wrap
    b = offset - wrap
    c = month_span - wrap
    d = 12 - month_span - offset + wrap
    return [1] * a + [0] * b + [1] * c + [0] * d


def days_per_month(year: int, starting_month: int = 1) -> np.ndarray:
    """Get a 1x12 array of days in each month for a given year.
    
    If `starting_month != 1`, any month below `starting_month` is wrapped into
    the next year, with leap years taken into account.

    `days_per_month(1967, starting_month: int = 2)`:
    +----+----+----+----+----+----+----+----+----+----+----+----+
    | 31 | 29 | 31 | 30 | 31 | 30 | 31 | 31 | 30 | 31 | 30 | 31 |
    +----+----+----+----+----+----+----+----+----+----+----+----+
    
    ^ First 2 indices belong to January/February 1968, which was a leap year 
    """
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


def nullable(func):

    @wraps(func)
    def wrapper(*args, **kwargs):
        array_like = args[0]
        if isinstance(array_like, (pd.Series, np.ndarray)):
            original_dtype = array_like.dtype
            na_indices = pd.isna(array_like)
            nans = array_like[na_indices]
            array_like[na_indices] = 0

            result = func(array_like.astype(int), *args[1:], **kwargs)

            result[na_indices] = nans
            if (result.min() >= -2**(8 * original_dtype.itemsize - 1) and
                result.max() <= 2**(8 * original_dtype.itemsize - 1) - 1):
                return result.astype(original_dtype)
            return result
        return func(*args, **kwargs)

    return wrapper


@nullable
def years_to_days(
    years: int | pd.Series | np.ndarray,
    starting_year: int | pd.Series | np.ndarray = 1970
) -> int | pd.Series | np.ndarray:
    """Convert an integer number of years to an equivalent number of days,
    accounting for leap years.

    `starting_year` references January 1st of the given year, counting up/down
    from there.  `years_to_days(1, 1970)` will count how many days there were
    in 1970, and `years_to_days(-1, 1970)` will count the days in 1969.
    """
    # replicating the Gregorian calendar
    if isinstance(years, (pd.Series, np.ndarray)):
        if (isinstance(starting_year, (pd.Series, np.ndarray)) and
            len(starting_year) != len(years)):
            err_msg = (f"[{error_trace()}] `days` and `starting_year` must be "
                       f"the same length or scalar (days: {len(years)}, "
                       f"starting_year: {len(starting_year)})")
            raise ValueError(err_msg)
        years = years.astype("O")  # prevents overflow

    leaps = leaps_between(starting_year, starting_year + years)
    return (years - leaps) * 365 + leaps * 366


@nullable
def days_to_years(
    days: int | pd.Series | np.ndarray,
    starting_year: int | pd.Series | np.ndarray = 1970,
    force: bool = False
) -> int | pd.Series | np.ndarray:
    """Convert an integer number of days to an equivalent number of years,
    accounting for leap years.

    `starting_year` references January 1st of the given year, counting up/down
    from there.

    `force=False` rejects any input that would lead to a non-integer number of
    years.
    """
    # TODO: profile this
    # TODO: replace `force` with `residuals: bool = True` to return residuals
    # as tuple.  Setting this false simulates `force=True`.  Have to invert
    # negative residuals for this to be accurate

    # vectorized case
    vectorized_days = isinstance(days, (pd.Series, np.ndarray))
    vectorized_start_years = isinstance(starting_year, (pd.Series, np.ndarray))
    if (vectorized_days or vectorized_start_years):
        # check input and cast to object to prevent overflow
        if (vectorized_days and
            vectorized_start_years and
            len(starting_year) != len(days)):
            err_msg = (f"[{error_trace()}] `days` and `starting_year` must be "
                       f"the same length or scalar (days: {len(days)}, "
                       f"starting_year: {len(starting_year)})")
            raise ValueError(err_msg)
        if vectorized_days:
            days = days.astype("O")
        if vectorized_start_years:
            starting_year = starting_year.astype("O")

        # use days since year 0 to enable accurate leap year estimation
        since_0 = days + years_to_days(starting_year, starting_year=0)
        year_estimate = (400 * since_0) // 146097  # gets within 1 year

        # estimate sometimes undershoots by a year due to rounding errors
        residuals = since_0 - years_to_days(year_estimate, starting_year=0)
        div_correct = (days < 0) & (residuals > 0)  # correcting for floor div
        carry = (residuals == years_to_days(1, starting_year=year_estimate))
        year_estimate[div_correct | carry] += 1

        # check for information loss
        if not force:
            residuals[carry] = 0
            if residuals.any():
                bad = residuals[residuals != 0].index.values
                if len(bad) == 1:  # singular
                    err_msg = (f"[{error_trace()}] could not convert days to "
                               f"years without losing precision (index: "
                               f"{list(bad)})")
                elif len(bad) <= 5:  # plural
                    err_msg = (f"[{error_trace()}] could not convert days to "
                               f"years without losing precision (indices: "
                               f"{list(bad)})")
                else:  # plural, shortened for brevity
                    shortened = ", ".join(str(i) for i in bad[:5])
                    err_msg = (f"[{error_trace()}] could not convert days to "
                               f"years without losing precision (indices: "
                               f"[{shortened}, ...] ({len(bad)}))")
                raise ValueError(err_msg)
        return year_estimate - starting_year

    # scalar case
    # use days since year 0 to enable accurate leap year estimation
    since_0 = days + years_to_days(starting_year, starting_year=0)
    year_estimate = (400 * since_0) // 146097  # gets within 1 year

    # estimate sometimes undershoots by a year due to rounding errors
    residuals = since_0 - years_to_days(year_estimate, starting_year=0)
    if days < 0 and residuals > 0:  # correct for floor div
        year_estimate += 1
    elif residuals == years_to_days(1, starting_year=year_estimate):  # carry
        year_estimate += 1
        residuals = 0

    # check for information loss
    if not force and residuals:
        err_msg = (f"[{error_trace()}] could not convert days to years "
                   f"without losing precision: {days}")
        raise ValueError(err_msg)
    return year_estimate - starting_year


def months_to_days(months: int | pd.Series | np.ndarray,
                   starting_year: int = 1970,
                   starting_month: int = 1) -> int | pd.Series | np.ndarray:
    """Convert an integer number of months into an equivalent number of days,
    accounting for unequal month lengths and leap years.

    `starting_year` references the year to begin counting from, in order to
    account for leap years.  `starting_month` references the month to count
    from, between 1 (January) and 12 (December).  If `months` would carry over
    into another year with `starting_month != 1`, leap years are still taken
    into account.
    """
    # only have to figure out days in last year, else use years_to_days
    years = months // 12
    base_days = years_to_days(years, starting_year=starting_year)

    # vectorized
    if isinstance(months, (pd.Series, np.ndarray)):
        # get na indices
        nan_indices = pd.isna(months)
        nans = months[nan_indices]
        original_dtype = months.dtype
        months[nan_indices] = 0
        months = months.astype(int)  # removes extension dtype

        # map month spans to masks, multiply by day count, then sum by row
        mapping = np.array([month_mask(i, starting_month) for i in range(12)])
        masks = mapping[months % 12]  # nx12
        calendar = days_per_month(starting_year + years, starting_month)  # nx12
        result = base_days.astype(object) + np.sum(np.multiply(masks, calendar), axis=1).astype(object)  # nx1
        result[nan_indices] = nans
        return result.astype(original_dtype)

    # scalar
    calendar = days_per_month(starting_year + years, starting_month)
    begin = starting_month - 1
    end = (starting_month - 1 + months) % 12
    return (base_days + np.sum(calendar[begin:end]))
    result[nan_indices] = nans
    return result.astype(original_dtype)


def days_to_months(days: int | pd.Series | np.ndarray,
                   starting_year: int = 1970,
                   starting_month: int = 1,
                   force: bool = False) -> int | pd.Series | np.ndarray:
    """Convert an integer number of days into an equivalent number of months,
    accounting for unequal month lengths and leap years.

    `starting_year` references the year to begin counting from, in order to
    account for leap years.  `starting_month` references the month to count
    from, between 1 (January) and 12 (December).  If `days` would carry over
    into another year with `starting_month != 1`, leap years are still taken
    into account.

    `force=False` rejects any input that would lead to a non-integer number of
    months.
    """
    # only have to figure out months in last year, else use days_to_years
    years = days_to_years(days, force=True)
    result = years * 12

    # vectorized
    if isinstance(days, (pd.Series, np.ndarray)):
        # have to handle positive and negative day spans differently
        negative = (days < 0)

        # set up calendar with days per month for each month in last year
        calendar = np.full((len(days), 12), 0)
        calendar[~negative] = days_per_month(starting_year + years,
                                             starting_month=starting_month)
        # negative days needs to use the previous year
        calendar[negative] = days_per_month(starting_year + years - 1,
                                            starting_month=starting_month)
        # transform calendar into span thresholds using cumsum()
        calendar = np.cumsum(calendar, axis=1)

        # figure out how many days are in last year, modulo length of year
        last_year = ((days - years_to_days(years,
                                           starting_year=starting_year)) %
                     calendar[:, -1])
        # transform into same dimensions as calendar thresholds
        last_year = np.array([last_year] * 12).T

        # count thresholds that are over (negative) or under (positive)
        result[~negative] += np.sum(calendar <= last_year, axis=1)
        result[negative] -= np.sum(calendar > last_year, axis=1)

    # scalar
    else:
        # get calendar with days in each month for last year
        if days < 0:  # negative days use previous year
            calendar = days_per_month(starting_year + years - 1,
                                      starting_month=starting_month)
        else:
            calendar = days_per_month(starting_year + years,
                                      starting_month=starting_month)
        calendar = np.cumsum(calendar)

        # figure out how many days are in last year, modulo length of year
        last_year = ((days - years_to_days(years,
                                          starting_year=starting_year)) %
                     calendar[-1])
        # transform into same dimensions as calendar thresholds
        last_year = np.array([last_year] * 12)

        # count thresholds that are over (negative) or under (positive)
        if days < 0:
            result -= np.sum(calendar > last_year)
        else:
            result += np.sum(calendar <= last_year)

    # check for information loss
    if not force:
        reverse = months_to_days(result, starting_month=starting_month,
                                 starting_year=starting_year)
        if not np.array_equal(reverse[pd.notna(reverse)], days[pd.notna(days)]):
            err_msg = (f"[{error_trace()}] could not convert days to years "
                    f"without losing precision: {days}")
            raise ValueError(err_msg)
    return result


def total_nanoseconds(
    td: pd.Timedelta | datetime.timedelta | np.timedelta64,
    starting_year: int = 1970,
    starting_month: int = 1) -> int:
    """Get the total number of nanoseconds stored in a timedelta object as an
    integer.  Essentially the equivalent of timedelta.total_seconds(), except
    it returns an integer number of nanoseconds rather than a float
    representing seconds.
    
    timedelta64 units 'Y' and 'M' are supported via the `years_to_days` and
    `months_to_days` functions defined above.  Leap years and unequal month
    lengths are handled by the `starting_year` and `starting_month` arguments,
    defaulting to UTC epoch time (1970-01-01).
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
